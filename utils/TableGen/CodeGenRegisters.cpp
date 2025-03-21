//===- CodeGenRegisters.cpp - Register and RegisterClass Info -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines structures to encapsulate information gleaned from the
// target register and register class definitions.
//
//===----------------------------------------------------------------------===//

#include "CodeGenRegisters.h"
#include "CodeGenTarget.h"
#include "llvm/ADT/IntEqClasses.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Debug.h"
#include "llvm/TableGen/Error.h"

using namespace llvm;

#define DEBUG_TYPE "regalloc-emitter"

//===----------------------------------------------------------------------===//
//                             CodeGenSubRegIndex
//===----------------------------------------------------------------------===//

CodeGenSubRegIndex::CodeGenSubRegIndex(Record *R, unsigned Enum)
  : TheDef(R), EnumValue(Enum), LaneMask(0), AllSuperRegsCovered(true) {
  Name = R->getName();
  if (R->getValue("Namespace"))
    Namespace = R->getValueAsString("Namespace");
  Size = R->getValueAsInt("Size");
  Offset = R->getValueAsInt("Offset");
}

CodeGenSubRegIndex::CodeGenSubRegIndex(StringRef N, StringRef Nspace,
                                       unsigned Enum)
  : TheDef(nullptr), Name(N), Namespace(Nspace), Size(-1), Offset(-1),
    EnumValue(Enum), LaneMask(0), AllSuperRegsCovered(true) {
}

std::string CodeGenSubRegIndex::getQualifiedName() const {
  std::string N = getNamespace();
  if (!N.empty())
    N += "::";
  N += getName();
  return N;
}

void CodeGenSubRegIndex::updateComponents(CodeGenRegBank &RegBank) {
  if (!TheDef)
    return;

  std::vector<Record*> Comps = TheDef->getValueAsListOfDefs("ComposedOf");
  if (!Comps.empty()) {
    if (Comps.size() != 2)
      PrintFatalError(TheDef->getLoc(),
                      "ComposedOf must have exactly two entries");
    CodeGenSubRegIndex *A = RegBank.getSubRegIdx(Comps[0]);
    CodeGenSubRegIndex *B = RegBank.getSubRegIdx(Comps[1]);
    CodeGenSubRegIndex *X = A->addComposite(B, this);
    if (X)
      PrintFatalError(TheDef->getLoc(), "Ambiguous ComposedOf entries");
  }

  std::vector<Record*> Parts =
    TheDef->getValueAsListOfDefs("CoveringSubRegIndices");
  if (!Parts.empty()) {
    if (Parts.size() < 2)
      PrintFatalError(TheDef->getLoc(),
                      "CoveredBySubRegs must have two or more entries");
    SmallVector<CodeGenSubRegIndex*, 8> IdxParts;
    for (unsigned i = 0, e = Parts.size(); i != e; ++i)
      IdxParts.push_back(RegBank.getSubRegIdx(Parts[i]));
    RegBank.addConcatSubRegIndex(IdxParts, this);
  }
}

unsigned CodeGenSubRegIndex::computeLaneMask() const {
  // Already computed?
  if (LaneMask)
    return LaneMask;

  // Recursion guard, shouldn't be required.
  LaneMask = ~0u;

  // The lane mask is simply the union of all sub-indices.
  unsigned M = 0;
  for (const auto &C : Composed)
    M |= C.second->computeLaneMask();
  assert(M && "Missing lane mask, sub-register cycle?");
  LaneMask = M;
  return LaneMask;
}

//===----------------------------------------------------------------------===//
//                              CodeGenRegister
//===----------------------------------------------------------------------===//

CodeGenRegister::CodeGenRegister(Record *R, unsigned Enum)
  : TheDef(R),
    EnumValue(Enum),
    CostPerUse(R->getValueAsInt("CostPerUse")),
    CoveredBySubRegs(R->getValueAsBit("CoveredBySubRegs")),
    HasDisjunctSubRegs(false),
    SubRegsComplete(false),
    SuperRegsComplete(false),
    TopoSig(~0u)
{}

void CodeGenRegister::buildObjectGraph(CodeGenRegBank &RegBank) {
  std::vector<Record*> SRIs = TheDef->getValueAsListOfDefs("SubRegIndices");
  std::vector<Record*> SRs = TheDef->getValueAsListOfDefs("SubRegs");

  if (SRIs.size() != SRs.size())
    PrintFatalError(TheDef->getLoc(),
                    "SubRegs and SubRegIndices must have the same size");

  for (unsigned i = 0, e = SRIs.size(); i != e; ++i) {
    ExplicitSubRegIndices.push_back(RegBank.getSubRegIdx(SRIs[i]));
    ExplicitSubRegs.push_back(RegBank.getReg(SRs[i]));
  }

  // Also compute leading super-registers. Each register has a list of
  // covered-by-subregs super-registers where it appears as the first explicit
  // sub-register.
  //
  // This is used by computeSecondarySubRegs() to find candidates.
  if (CoveredBySubRegs && !ExplicitSubRegs.empty())
    ExplicitSubRegs.front()->LeadingSuperRegs.push_back(this);

  // Add ad hoc alias links. This is a symmetric relationship between two
  // registers, so build a symmetric graph by adding links in both ends.
  std::vector<Record*> Aliases = TheDef->getValueAsListOfDefs("Aliases");
  for (unsigned i = 0, e = Aliases.size(); i != e; ++i) {
    CodeGenRegister *Reg = RegBank.getReg(Aliases[i]);
    ExplicitAliases.push_back(Reg);
    Reg->ExplicitAliases.push_back(this);
  }
}

const std::string &CodeGenRegister::getName() const {
  assert(TheDef && "no def");
  return TheDef->getName();
}

namespace {
// Iterate over all register units in a set of registers.
class RegUnitIterator {
  CodeGenRegister::Vec::const_iterator RegI, RegE;
  CodeGenRegister::RegUnitList::iterator UnitI, UnitE;

public:
  RegUnitIterator(const CodeGenRegister::Vec &Regs):
    RegI(Regs.begin()), RegE(Regs.end()), UnitI(), UnitE() {

    if (RegI != RegE) {
      UnitI = (*RegI)->getRegUnits().begin();
      UnitE = (*RegI)->getRegUnits().end();
      advance();
    }
  }

  bool isValid() const { return UnitI != UnitE; }

  unsigned operator* () const { assert(isValid()); return *UnitI; }

  const CodeGenRegister *getReg() const { assert(isValid()); return *RegI; }

  /// Preincrement.  Move to the next unit.
  void operator++() {
    assert(isValid() && "Cannot advance beyond the last operand");
    ++UnitI;
    advance();
  }

protected:
  void advance() {
    while (UnitI == UnitE) {
      if (++RegI == RegE)
        break;
      UnitI = (*RegI)->getRegUnits().begin();
      UnitE = (*RegI)->getRegUnits().end();
    }
  }
};
} // namespace

// Return true of this unit appears in RegUnits.
static bool hasRegUnit(CodeGenRegister::RegUnitList &RegUnits, unsigned Unit) {
  return RegUnits.test(Unit);
}

// Inherit register units from subregisters.
// Return true if the RegUnits changed.
bool CodeGenRegister::inheritRegUnits(CodeGenRegBank &RegBank) {
  bool changed = false;
  for (SubRegMap::const_iterator I = SubRegs.begin(), E = SubRegs.end();
       I != E; ++I) {
    CodeGenRegister *SR = I->second;
    // Merge the subregister's units into this register's RegUnits.
    changed |= (RegUnits |= SR->RegUnits);
  }

  return changed;
}

const CodeGenRegister::SubRegMap &
CodeGenRegister::computeSubRegs(CodeGenRegBank &RegBank) {
  // Only compute this map once.
  if (SubRegsComplete)
    return SubRegs;
  SubRegsComplete = true;

  HasDisjunctSubRegs = ExplicitSubRegs.size() > 1;

  // First insert the explicit subregs and make sure they are fully indexed.
  for (unsigned i = 0, e = ExplicitSubRegs.size(); i != e; ++i) {
    CodeGenRegister *SR = ExplicitSubRegs[i];
    CodeGenSubRegIndex *Idx = ExplicitSubRegIndices[i];
    if (!SubRegs.insert(std::make_pair(Idx, SR)).second)
      PrintFatalError(TheDef->getLoc(), "SubRegIndex " + Idx->getName() +
                      " appears twice in Register " + getName());
    // Map explicit sub-registers first, so the names take precedence.
    // The inherited sub-registers are mapped below.
    SubReg2Idx.insert(std::make_pair(SR, Idx));
  }

  // Keep track of inherited subregs and how they can be reached.
  SmallPtrSet<CodeGenRegister*, 8> Orphans;

  // Clone inherited subregs and place duplicate entries in Orphans.
  // Here the order is important - earlier subregs take precedence.
  for (unsigned i = 0, e = ExplicitSubRegs.size(); i != e; ++i) {
    CodeGenRegister *SR = ExplicitSubRegs[i];
    const SubRegMap &Map = SR->computeSubRegs(RegBank);
    HasDisjunctSubRegs |= SR->HasDisjunctSubRegs;

    for (SubRegMap::const_iterator SI = Map.begin(), SE = Map.end(); SI != SE;
         ++SI) {
      if (!SubRegs.insert(*SI).second)
        Orphans.insert(SI->second);
    }
  }

  // Expand any composed subreg indices.
  // If dsub_2 has ComposedOf = [qsub_1, dsub_0], and this register has a
  // qsub_1 subreg, add a dsub_2 subreg.  Keep growing Indices and process
  // expanded subreg indices recursively.
  SmallVector<CodeGenSubRegIndex*, 8> Indices = ExplicitSubRegIndices;
  for (unsigned i = 0; i != Indices.size(); ++i) {
    CodeGenSubRegIndex *Idx = Indices[i];
    const CodeGenSubRegIndex::CompMap &Comps = Idx->getComposites();
    CodeGenRegister *SR = SubRegs[Idx];
    const SubRegMap &Map = SR->computeSubRegs(RegBank);

    // Look at the possible compositions of Idx.
    // They may not all be supported by SR.
    for (CodeGenSubRegIndex::CompMap::const_iterator I = Comps.begin(),
           E = Comps.end(); I != E; ++I) {
      SubRegMap::const_iterator SRI = Map.find(I->first);
      if (SRI == Map.end())
        continue; // Idx + I->first doesn't exist in SR.
      // Add I->second as a name for the subreg SRI->second, assuming it is
      // orphaned, and the name isn't already used for something else.
      if (SubRegs.count(I->second) || !Orphans.erase(SRI->second))
        continue;
      // We found a new name for the orphaned sub-register.
      SubRegs.insert(std::make_pair(I->second, SRI->second));
      Indices.push_back(I->second);
    }
  }

  // Now Orphans contains the inherited subregisters without a direct index.
  // Create inferred indexes for all missing entries.
  // Work backwards in the Indices vector in order to compose subregs bottom-up.
  // Consider this subreg sequence:
  //
  //   qsub_1 -> dsub_0 -> ssub_0
  //
  // The qsub_1 -> dsub_0 composition becomes dsub_2, so the ssub_0 register
  // can be reached in two different ways:
  //
  //   qsub_1 -> ssub_0
  //   dsub_2 -> ssub_0
  //
  // We pick the latter composition because another register may have [dsub_0,
  // dsub_1, dsub_2] subregs without necessarily having a qsub_1 subreg.  The
  // dsub_2 -> ssub_0 composition can be shared.
  while (!Indices.empty() && !Orphans.empty()) {
    CodeGenSubRegIndex *Idx = Indices.pop_back_val();
    CodeGenRegister *SR = SubRegs[Idx];
    const SubRegMap &Map = SR->computeSubRegs(RegBank);
    for (SubRegMap::const_iterator SI = Map.begin(), SE = Map.end(); SI != SE;
         ++SI)
      if (Orphans.erase(SI->second))
        SubRegs[RegBank.getCompositeSubRegIndex(Idx, SI->first)] = SI->second;
  }

  // Compute the inverse SubReg -> Idx map.
  for (SubRegMap::const_iterator SI = SubRegs.begin(), SE = SubRegs.end();
       SI != SE; ++SI) {
    if (SI->second == this) {
      ArrayRef<SMLoc> Loc;
      if (TheDef)
        Loc = TheDef->getLoc();
      PrintFatalError(Loc, "Register " + getName() +
                      " has itself as a sub-register");
    }

    // Compute AllSuperRegsCovered.
    if (!CoveredBySubRegs)
      SI->first->AllSuperRegsCovered = false;

    // Ensure that every sub-register has a unique name.
    DenseMap<const CodeGenRegister*, CodeGenSubRegIndex*>::iterator Ins =
      SubReg2Idx.insert(std::make_pair(SI->second, SI->first)).first;
    if (Ins->second == SI->first)
      continue;
    // Trouble: Two different names for SI->second.
    ArrayRef<SMLoc> Loc;
    if (TheDef)
      Loc = TheDef->getLoc();
    PrintFatalError(Loc, "Sub-register can't have two names: " +
                  SI->second->getName() + " available as " +
                  SI->first->getName() + " and " + Ins->second->getName());
  }

  // Derive possible names for sub-register concatenations from any explicit
  // sub-registers. By doing this before computeSecondarySubRegs(), we ensure
  // that getConcatSubRegIndex() won't invent any concatenated indices that the
  // user already specified.
  for (unsigned i = 0, e = ExplicitSubRegs.size(); i != e; ++i) {
    CodeGenRegister *SR = ExplicitSubRegs[i];
    if (!SR->CoveredBySubRegs || SR->ExplicitSubRegs.size() <= 1)
      continue;

    // SR is composed of multiple sub-regs. Find their names in this register.
    SmallVector<CodeGenSubRegIndex*, 8> Parts;
    for (unsigned j = 0, e = SR->ExplicitSubRegs.size(); j != e; ++j)
      Parts.push_back(getSubRegIndex(SR->ExplicitSubRegs[j]));

    // Offer this as an existing spelling for the concatenation of Parts.
    RegBank.addConcatSubRegIndex(Parts, ExplicitSubRegIndices[i]);
  }

  // Initialize RegUnitList. Because getSubRegs is called recursively, this
  // processes the register hierarchy in postorder.
  //
  // Inherit all sub-register units. It is good enough to look at the explicit
  // sub-registers, the other registers won't contribute any more units.
  for (unsigned i = 0, e = ExplicitSubRegs.size(); i != e; ++i) {
    CodeGenRegister *SR = ExplicitSubRegs[i];
    RegUnits |= SR->RegUnits;
  }

  // Absent any ad hoc aliasing, we create one register unit per leaf register.
  // These units correspond to the maximal cliques in the register overlap
  // graph which is optimal.
  //
  // When there is ad hoc aliasing, we simply create one unit per edge in the
  // undirected ad hoc aliasing graph. Technically, we could do better by
  // identifying maximal cliques in the ad hoc graph, but cliques larger than 2
  // are extremely rare anyway (I've never seen one), so we don't bother with
  // the added complexity.
  for (unsigned i = 0, e = ExplicitAliases.size(); i != e; ++i) {
    CodeGenRegister *AR = ExplicitAliases[i];
    // Only visit each edge once.
    if (AR->SubRegsComplete)
      continue;
    // Create a RegUnit representing this alias edge, and add it to both
    // registers.
    unsigned Unit = RegBank.newRegUnit(this, AR);
    RegUnits.set(Unit);
    AR->RegUnits.set(Unit);
  }

  // Finally, create units for leaf registers without ad hoc aliases. Note that
  // a leaf register with ad hoc aliases doesn't get its own unit - it isn't
  // necessary. This means the aliasing leaf registers can share a single unit.
  if (RegUnits.empty())
    RegUnits.set(RegBank.newRegUnit(this));

  // We have now computed the native register units. More may be adopted later
  // for balancing purposes.
  NativeRegUnits = RegUnits;

  return SubRegs;
}

// In a register that is covered by its sub-registers, try to find redundant
// sub-registers. For example:
//
//   QQ0 = {Q0, Q1}
//   Q0 = {D0, D1}
//   Q1 = {D2, D3}
//
// We can infer that D1_D2 is also a sub-register, even if it wasn't named in
// the register definition.
//
// The explicitly specified registers form a tree. This function discovers
// sub-register relationships that would force a DAG.
//
void CodeGenRegister::computeSecondarySubRegs(CodeGenRegBank &RegBank) {
  // Collect new sub-registers first, add them later.
  SmallVector<SubRegMap::value_type, 8> NewSubRegs;

  // Look at the leading super-registers of each sub-register. Those are the
  // candidates for new sub-registers, assuming they are fully contained in
  // this register.
  for (SubRegMap::iterator I = SubRegs.begin(), E = SubRegs.end(); I != E; ++I){
    const CodeGenRegister *SubReg = I->second;
    const CodeGenRegister::SuperRegList &Leads = SubReg->LeadingSuperRegs;
    for (unsigned i = 0, e = Leads.size(); i != e; ++i) {
      CodeGenRegister *Cand = const_cast<CodeGenRegister*>(Leads[i]);
      // Already got this sub-register?
      if (Cand == this || getSubRegIndex(Cand))
        continue;
      // Check if each component of Cand is already a sub-register.
      // We know that the first component is I->second, and is present with the
      // name I->first.
      SmallVector<CodeGenSubRegIndex*, 8> Parts(1, I->first);
      assert(!Cand->ExplicitSubRegs.empty() &&
             "Super-register has no sub-registers");
      for (unsigned j = 1, e = Cand->ExplicitSubRegs.size(); j != e; ++j) {
        if (CodeGenSubRegIndex *Idx = getSubRegIndex(Cand->ExplicitSubRegs[j]))
          Parts.push_back(Idx);
        else {
          // Sub-register doesn't exist.
          Parts.clear();
          break;
        }
      }
      // If some Cand sub-register is not part of this register, or if Cand only
      // has one sub-register, there is nothing to do.
      if (Parts.size() <= 1)
        continue;

      // Each part of Cand is a sub-register of this. Make the full Cand also
      // a sub-register with a concatenated sub-register index.
      CodeGenSubRegIndex *Concat= RegBank.getConcatSubRegIndex(Parts);
      NewSubRegs.push_back(std::make_pair(Concat, Cand));
    }
  }

  // Now add all the new sub-registers.
  for (unsigned i = 0, e = NewSubRegs.size(); i != e; ++i) {
    // Don't add Cand if another sub-register is already using the index.
    if (!SubRegs.insert(NewSubRegs[i]).second)
      continue;

    CodeGenSubRegIndex *NewIdx = NewSubRegs[i].first;
    CodeGenRegister *NewSubReg = NewSubRegs[i].second;
    SubReg2Idx.insert(std::make_pair(NewSubReg, NewIdx));
  }

  // Create sub-register index composition maps for the synthesized indices.
  for (unsigned i = 0, e = NewSubRegs.size(); i != e; ++i) {
    CodeGenSubRegIndex *NewIdx = NewSubRegs[i].first;
    CodeGenRegister *NewSubReg = NewSubRegs[i].second;
    for (SubRegMap::const_iterator SI = NewSubReg->SubRegs.begin(),
           SE = NewSubReg->SubRegs.end(); SI != SE; ++SI) {
      CodeGenSubRegIndex *SubIdx = getSubRegIndex(SI->second);
      if (!SubIdx)
        PrintFatalError(TheDef->getLoc(), "No SubRegIndex for " +
                        SI->second->getName() + " in " + getName());
      NewIdx->addComposite(SI->first, SubIdx);
    }
  }
}

void CodeGenRegister::computeSuperRegs(CodeGenRegBank &RegBank) {
  // Only visit each register once.
  if (SuperRegsComplete)
    return;
  SuperRegsComplete = true;

  // Make sure all sub-registers have been visited first, so the super-reg
  // lists will be topologically ordered.
  for (SubRegMap::const_iterator I = SubRegs.begin(), E = SubRegs.end();
       I != E; ++I)
    I->second->computeSuperRegs(RegBank);

  // Now add this as a super-register on all sub-registers.
  // Also compute the TopoSigId in post-order.
  TopoSigId Id;
  for (SubRegMap::const_iterator I = SubRegs.begin(), E = SubRegs.end();
       I != E; ++I) {
    // Topological signature computed from SubIdx, TopoId(SubReg).
    // Loops and idempotent indices have TopoSig = ~0u.
    Id.push_back(I->first->EnumValue);
    Id.push_back(I->second->TopoSig);

    // Don't add duplicate entries.
    if (!I->second->SuperRegs.empty() && I->second->SuperRegs.back() == this)
      continue;
    I->second->SuperRegs.push_back(this);
  }
  TopoSig = RegBank.getTopoSig(Id);
}

void
CodeGenRegister::addSubRegsPreOrder(SetVector<const CodeGenRegister*> &OSet,
                                    CodeGenRegBank &RegBank) const {
  assert(SubRegsComplete && "Must precompute sub-registers");
  for (unsigned i = 0, e = ExplicitSubRegs.size(); i != e; ++i) {
    CodeGenRegister *SR = ExplicitSubRegs[i];
    if (OSet.insert(SR))
      SR->addSubRegsPreOrder(OSet, RegBank);
  }
  // Add any secondary sub-registers that weren't part of the explicit tree.
  for (SubRegMap::const_iterator I = SubRegs.begin(), E = SubRegs.end();
       I != E; ++I)
    OSet.insert(I->second);
}

// Get the sum of this register's unit weights.
unsigned CodeGenRegister::getWeight(const CodeGenRegBank &RegBank) const {
  unsigned Weight = 0;
  for (RegUnitList::iterator I = RegUnits.begin(), E = RegUnits.end();
       I != E; ++I) {
    Weight += RegBank.getRegUnit(*I).Weight;
  }
  return Weight;
}

//===----------------------------------------------------------------------===//
//                               RegisterTuples
//===----------------------------------------------------------------------===//

// A RegisterTuples def is used to generate pseudo-registers from lists of
// sub-registers. We provide a SetTheory expander class that returns the new
// registers.
namespace {
struct TupleExpander : SetTheory::Expander {
  void expand(SetTheory &ST, Record *Def, SetTheory::RecSet &Elts) override {
    std::vector<Record*> Indices = Def->getValueAsListOfDefs("SubRegIndices");
    unsigned Dim = Indices.size();
    ListInit *SubRegs = Def->getValueAsListInit("SubRegs");
    if (Dim != SubRegs->size())
      PrintFatalError(Def->getLoc(), "SubRegIndices and SubRegs size mismatch");
    if (Dim < 2)
      PrintFatalError(Def->getLoc(),
                      "Tuples must have at least 2 sub-registers");

    // Evaluate the sub-register lists to be zipped.
    unsigned Length = ~0u;
    SmallVector<SetTheory::RecSet, 4> Lists(Dim);
    for (unsigned i = 0; i != Dim; ++i) {
      ST.evaluate(SubRegs->getElement(i), Lists[i], Def->getLoc());
      Length = std::min(Length, unsigned(Lists[i].size()));
    }

    if (Length == 0)
      return;

    // Precompute some types.
    Record *RegisterCl = Def->getRecords().getClass("Register");
    RecTy *RegisterRecTy = RecordRecTy::get(RegisterCl);
    StringInit *BlankName = StringInit::get("");

    // Zip them up.
    for (unsigned n = 0; n != Length; ++n) {
      std::string Name;
      Record *Proto = Lists[0][n];
      std::vector<Init*> Tuple;
      unsigned CostPerUse = 0;
      for (unsigned i = 0; i != Dim; ++i) {
        Record *Reg = Lists[i][n];
        if (i) Name += '_';
        Name += Reg->getName();
        Tuple.push_back(DefInit::get(Reg));
        CostPerUse = std::max(CostPerUse,
                              unsigned(Reg->getValueAsInt("CostPerUse")));
      }

      // Create a new Record representing the synthesized register. This record
      // is only for consumption by CodeGenRegister, it is not added to the
      // RecordKeeper.
      Record *NewReg = new Record(Name, Def->getLoc(), Def->getRecords());
      Elts.insert(NewReg);

      // Copy Proto super-classes.
      ArrayRef<Record *> Supers = Proto->getSuperClasses();
      ArrayRef<SMRange> Ranges = Proto->getSuperClassRanges();
      for (unsigned i = 0, e = Supers.size(); i != e; ++i)
        NewReg->addSuperClass(Supers[i], Ranges[i]);

      // Copy Proto fields.
      for (unsigned i = 0, e = Proto->getValues().size(); i != e; ++i) {
        RecordVal RV = Proto->getValues()[i];

        // Skip existing fields, like NAME.
        if (NewReg->getValue(RV.getNameInit()))
          continue;

        StringRef Field = RV.getName();

        // Replace the sub-register list with Tuple.
        if (Field == "SubRegs")
          RV.setValue(ListInit::get(Tuple, RegisterRecTy));

        // Provide a blank AsmName. MC hacks are required anyway.
        if (Field == "AsmName")
          RV.setValue(BlankName);

        // CostPerUse is aggregated from all Tuple members.
        if (Field == "CostPerUse")
          RV.setValue(IntInit::get(CostPerUse));

        // Composite registers are always covered by sub-registers.
        if (Field == "CoveredBySubRegs")
          RV.setValue(BitInit::get(true));

        // Copy fields from the RegisterTuples def.
        if (Field == "SubRegIndices" ||
            Field == "CompositeIndices") {
          NewReg->addValue(*Def->getValue(Field));
          continue;
        }

        // Some fields get their default uninitialized value.
        if (Field == "DwarfNumbers" ||
            Field == "DwarfAlias" ||
            Field == "Aliases") {
          if (const RecordVal *DefRV = RegisterCl->getValue(Field))
            NewReg->addValue(*DefRV);
          continue;
        }

        // Everything else is copied from Proto.
        NewReg->addValue(RV);
      }
    }
  }
};
}

//===----------------------------------------------------------------------===//
//                            CodeGenRegisterClass
//===----------------------------------------------------------------------===//

static void sortAndUniqueRegisters(CodeGenRegister::Vec &M) {
  std::sort(M.begin(), M.end(), deref<llvm::less>());
  M.erase(std::unique(M.begin(), M.end(), deref<llvm::equal>()), M.end());
}

CodeGenRegisterClass::CodeGenRegisterClass(CodeGenRegBank &RegBank, Record *R)
  : TheDef(R),
    Name(R->getName()),
    TopoSigs(RegBank.getNumTopoSigs()),
    EnumValue(-1),
    LaneMask(0) {
  // Rename anonymous register classes.
  if (R->getName().size() > 9 && R->getName()[9] == '.') {
    static unsigned AnonCounter = 0;
    R->setName("AnonRegClass_" + utostr(AnonCounter++));
  }

  std::vector<Record*> TypeList = R->getValueAsListOfDefs("RegTypes");
  for (unsigned i = 0, e = TypeList.size(); i != e; ++i) {
    Record *Type = TypeList[i];
    if (!Type->isSubClassOf("ValueType"))
      PrintFatalError("RegTypes list member '" + Type->getName() +
        "' does not derive from the ValueType class!");
    VTs.push_back(getValueType(Type));
  }
  assert(!VTs.empty() && "RegisterClass must contain at least one ValueType!");

  // Allocation order 0 is the full set. AltOrders provides others.
  const SetTheory::RecVec *Elements = RegBank.getSets().expand(R);
  ListInit *AltOrders = R->getValueAsListInit("AltOrders");
  Orders.resize(1 + AltOrders->size());

  // Default allocation order always contains all registers.
  for (unsigned i = 0, e = Elements->size(); i != e; ++i) {
    Orders[0].push_back((*Elements)[i]);
    const CodeGenRegister *Reg = RegBank.getReg((*Elements)[i]);
    Members.push_back(Reg);
    TopoSigs.set(Reg->getTopoSig());
  }
  sortAndUniqueRegisters(Members);

  // Alternative allocation orders may be subsets.
  SetTheory::RecSet Order;
  for (unsigned i = 0, e = AltOrders->size(); i != e; ++i) {
    RegBank.getSets().evaluate(AltOrders->getElement(i), Order, R->getLoc());
    Orders[1 + i].append(Order.begin(), Order.end());
    // Verify that all altorder members are regclass members.
    while (!Order.empty()) {
      CodeGenRegister *Reg = RegBank.getReg(Order.back());
      Order.pop_back();
      if (!contains(Reg))
        PrintFatalError(R->getLoc(), " AltOrder register " + Reg->getName() +
                      " is not a class member");
    }
  }

  // Allow targets to override the size in bits of the RegisterClass.
  unsigned Size = R->getValueAsInt("Size");

  Namespace = R->getValueAsString("Namespace");
  SpillSize = Size ? Size : MVT(VTs[0]).getSizeInBits();
  SpillAlignment = R->getValueAsInt("Alignment");
  CopyCost = R->getValueAsInt("CopyCost");
  Allocatable = R->getValueAsBit("isAllocatable");
  AltOrderSelect = R->getValueAsString("AltOrderSelect");
  int AllocationPriority = R->getValueAsInt("AllocationPriority");
  if (AllocationPriority < 0 || AllocationPriority > 63)
    PrintFatalError(R->getLoc(), "AllocationPriority out of range [0,63]");
  this->AllocationPriority = AllocationPriority;
}

// Create an inferred register class that was missing from the .td files.
// Most properties will be inherited from the closest super-class after the
// class structure has been computed.
CodeGenRegisterClass::CodeGenRegisterClass(CodeGenRegBank &RegBank,
                                           StringRef Name, Key Props)
  : Members(*Props.Members),
    TheDef(nullptr),
    Name(Name),
    TopoSigs(RegBank.getNumTopoSigs()),
    EnumValue(-1),
    SpillSize(Props.SpillSize),
    SpillAlignment(Props.SpillAlignment),
    CopyCost(0),
    Allocatable(true),
    AllocationPriority(0) {
  for (const auto R : Members)
    TopoSigs.set(R->getTopoSig());
}

// Compute inherited propertied for a synthesized register class.
void CodeGenRegisterClass::inheritProperties(CodeGenRegBank &RegBank) {
  assert(!getDef() && "Only synthesized classes can inherit properties");
  assert(!SuperClasses.empty() && "Synthesized class without super class");

  // The last super-class is the smallest one.
  CodeGenRegisterClass &Super = *SuperClasses.back();

  // Most properties are copied directly.
  // Exceptions are members, size, and alignment
  Namespace = Super.Namespace;
  VTs = Super.VTs;
  CopyCost = Super.CopyCost;
  Allocatable = Super.Allocatable;
  AltOrderSelect = Super.AltOrderSelect;
  AllocationPriority = Super.AllocationPriority;

  // Copy all allocation orders, filter out foreign registers from the larger
  // super-class.
  Orders.resize(Super.Orders.size());
  for (unsigned i = 0, ie = Super.Orders.size(); i != ie; ++i)
    for (unsigned j = 0, je = Super.Orders[i].size(); j != je; ++j)
      if (contains(RegBank.getReg(Super.Orders[i][j])))
        Orders[i].push_back(Super.Orders[i][j]);
}

bool CodeGenRegisterClass::contains(const CodeGenRegister *Reg) const {
  return std::binary_search(Members.begin(), Members.end(), Reg,
                            deref<llvm::less>());
}

namespace llvm {
  raw_ostream &operator<<(raw_ostream &OS, const CodeGenRegisterClass::Key &K) {
    OS << "{ S=" << K.SpillSize << ", A=" << K.SpillAlignment;
    for (const auto R : *K.Members)
      OS << ", " << R->getName();
    return OS << " }";
  }
}

// This is a simple lexicographical order that can be used to search for sets.
// It is not the same as the topological order provided by TopoOrderRC.
bool CodeGenRegisterClass::Key::
operator<(const CodeGenRegisterClass::Key &B) const {
  assert(Members && B.Members);
  return std::tie(*Members, SpillSize, SpillAlignment) <
         std::tie(*B.Members, B.SpillSize, B.SpillAlignment);
}

// Returns true if RC is a strict subclass.
// RC is a sub-class of this class if it is a valid replacement for any
// instruction operand where a register of this classis required. It must
// satisfy these conditions:
//
// 1. All RC registers are also in this.
// 2. The RC spill size must not be smaller than our spill size.
// 3. RC spill alignment must be compatible with ours.
//
static bool testSubClass(const CodeGenRegisterClass *A,
                         const CodeGenRegisterClass *B) {
  return A->SpillAlignment && B->SpillAlignment % A->SpillAlignment == 0 &&
         A->SpillSize <= B->SpillSize &&
         std::includes(A->getMembers().begin(), A->getMembers().end(),
                       B->getMembers().begin(), B->getMembers().end(),
                       deref<llvm::less>());
}

/// Sorting predicate for register classes.  This provides a topological
/// ordering that arranges all register classes before their sub-classes.
///
/// Register classes with the same registers, spill size, and alignment form a
/// clique.  They will be ordered alphabetically.
///
static bool TopoOrderRC(const CodeGenRegisterClass &PA,
                        const CodeGenRegisterClass &PB) {
  auto *A = &PA;
  auto *B = &PB;
  if (A == B)
    return 0;

  // Order by ascending spill size.
  if (A->SpillSize < B->SpillSize)
    return true;
  if (A->SpillSize > B->SpillSize)
    return false;

  // Order by ascending spill alignment.
  if (A->SpillAlignment < B->SpillAlignment)
    return true;
  if (A->SpillAlignment > B->SpillAlignment)
    return false;

  // Order by descending set size.  Note that the classes' allocation order may
  // not have been computed yet.  The Members set is always vaild.
  if (A->getMembers().size() > B->getMembers().size())
    return true;
  if (A->getMembers().size() < B->getMembers().size())
    return false;

  // Finally order by name as a tie breaker.
  return StringRef(A->getName()) < B->getName();
}

std::string CodeGenRegisterClass::getQualifiedName() const {
  if (Namespace.empty())
    return getName();
  else
    return Namespace + "::" + getName();
}

// Compute sub-classes of all register classes.
// Assume the classes are ordered topologically.
void CodeGenRegisterClass::computeSubClasses(CodeGenRegBank &RegBank) {
  auto &RegClasses = RegBank.getRegClasses();

  // Visit backwards so sub-classes are seen first.
  for (auto I = RegClasses.rbegin(), E = RegClasses.rend(); I != E; ++I) {
    CodeGenRegisterClass &RC = *I;
    RC.SubClasses.resize(RegClasses.size());
    RC.SubClasses.set(RC.EnumValue);

    // Normally, all subclasses have IDs >= rci, unless RC is part of a clique.
    for (auto I2 = I.base(), E2 = RegClasses.end(); I2 != E2; ++I2) {
      CodeGenRegisterClass &SubRC = *I2;
      if (RC.SubClasses.test(SubRC.EnumValue))
        continue;
      if (!testSubClass(&RC, &SubRC))
        continue;
      // SubRC is a sub-class. Grap all its sub-classes so we won't have to
      // check them again.
      RC.SubClasses |= SubRC.SubClasses;
    }

    // Sweep up missed clique members.  They will be immediately preceding RC.
    for (auto I2 = std::next(I); I2 != E && testSubClass(&RC, &*I2); ++I2)
      RC.SubClasses.set(I2->EnumValue);
  }

  // Compute the SuperClasses lists from the SubClasses vectors.
  for (auto &RC : RegClasses) {
    const BitVector &SC = RC.getSubClasses();
    auto I = RegClasses.begin();
    for (int s = 0, next_s = SC.find_first(); next_s != -1;
         next_s = SC.find_next(s)) {
      std::advance(I, next_s - s);
      s = next_s;
      if (&*I == &RC)
        continue;
      I->SuperClasses.push_back(&RC);
    }
  }

  // With the class hierarchy in place, let synthesized register classes inherit
  // properties from their closest super-class. The iteration order here can
  // propagate properties down multiple levels.
  for (auto &RC : RegClasses)
    if (!RC.getDef())
      RC.inheritProperties(RegBank);
}

void CodeGenRegisterClass::getSuperRegClasses(const CodeGenSubRegIndex *SubIdx,
                                              BitVector &Out) const {
  auto FindI = SuperRegClasses.find(SubIdx);
  if (FindI == SuperRegClasses.end())
    return;
  for (CodeGenRegisterClass *RC : FindI->second)
    Out.set(RC->EnumValue);
}

// Populate a unique sorted list of units from a register set.
void CodeGenRegisterClass::buildRegUnitSet(
  std::vector<unsigned> &RegUnits) const {
  std::vector<unsigned> TmpUnits;
  for (RegUnitIterator UnitI(Members); UnitI.isValid(); ++UnitI)
    TmpUnits.push_back(*UnitI);
  std::sort(TmpUnits.begin(), TmpUnits.end());
  std::unique_copy(TmpUnits.begin(), TmpUnits.end(),
                   std::back_inserter(RegUnits));
}

//===----------------------------------------------------------------------===//
//                               CodeGenRegBank
//===----------------------------------------------------------------------===//

CodeGenRegBank::CodeGenRegBank(RecordKeeper &Records) {
  // Configure register Sets to understand register classes and tuples.
  Sets.addFieldExpander("RegisterClass", "MemberList");
  Sets.addFieldExpander("CalleeSavedRegs", "SaveList");
  Sets.addExpander("RegisterTuples", llvm::make_unique<TupleExpander>());

  // Read in the user-defined (named) sub-register indices.
  // More indices will be synthesized later.
  std::vector<Record*> SRIs = Records.getAllDerivedDefinitions("SubRegIndex");
  std::sort(SRIs.begin(), SRIs.end(), LessRecord());
  for (unsigned i = 0, e = SRIs.size(); i != e; ++i)
    getSubRegIdx(SRIs[i]);
  // Build composite maps from ComposedOf fields.
  for (auto &Idx : SubRegIndices)
    Idx.updateComponents(*this);

  // Read in the register definitions.
  std::vector<Record*> Regs = Records.getAllDerivedDefinitions("Register");
  std::sort(Regs.begin(), Regs.end(), LessRecordRegister());
  // Assign the enumeration values.
  for (unsigned i = 0, e = Regs.size(); i != e; ++i)
    getReg(Regs[i]);

  // Expand tuples and number the new registers.
  std::vector<Record*> Tups =
    Records.getAllDerivedDefinitions("RegisterTuples");

  for (Record *R : Tups) {
    std::vector<Record *> TupRegs = *Sets.expand(R);
    std::sort(TupRegs.begin(), TupRegs.end(), LessRecordRegister());
    for (Record *RC : TupRegs)
      getReg(RC);
  }

  // Now all the registers are known. Build the object graph of explicit
  // register-register references.
  for (auto &Reg : Registers)
    Reg.buildObjectGraph(*this);

  // Compute register name map.
  for (auto &Reg : Registers)
    // FIXME: This could just be RegistersByName[name] = register, except that
    // causes some failures in MIPS - perhaps they have duplicate register name
    // entries? (or maybe there's a reason for it - I don't know much about this
    // code, just drive-by refactoring)
    RegistersByName.insert(
        std::make_pair(Reg.TheDef->getValueAsString("AsmName"), &Reg));

  // Precompute all sub-register maps.
  // This will create Composite entries for all inferred sub-register indices.
  for (auto &Reg : Registers)
    Reg.computeSubRegs(*this);

  // Infer even more sub-registers by combining leading super-registers.
  for (auto &Reg : Registers)
    if (Reg.CoveredBySubRegs)
      Reg.computeSecondarySubRegs(*this);

  // After the sub-register graph is complete, compute the topologically
  // ordered SuperRegs list.
  for (auto &Reg : Registers)
    Reg.computeSuperRegs(*this);

  // Native register units are associated with a leaf register. They've all been
  // discovered now.
  NumNativeRegUnits = RegUnits.size();

  // Read in register class definitions.
  std::vector<Record*> RCs = Records.getAllDerivedDefinitions("RegisterClass");
  if (RCs.empty())
    PrintFatalError("No 'RegisterClass' subclasses defined!");

  // Allocate user-defined register classes.
  for (auto *RC : RCs) {
    RegClasses.emplace_back(*this, RC);
    addToMaps(&RegClasses.back());
  }

  // Infer missing classes to create a full algebra.
  computeInferredRegisterClasses();

  // Order register classes topologically and assign enum values.
  RegClasses.sort(TopoOrderRC);
  unsigned i = 0;
  for (auto &RC : RegClasses)
    RC.EnumValue = i++;
  CodeGenRegisterClass::computeSubClasses(*this);
}

// Create a synthetic CodeGenSubRegIndex without a corresponding Record.
CodeGenSubRegIndex*
CodeGenRegBank::createSubRegIndex(StringRef Name, StringRef Namespace) {
  SubRegIndices.emplace_back(Name, Namespace, SubRegIndices.size() + 1);
  return &SubRegIndices.back();
}

CodeGenSubRegIndex *CodeGenRegBank::getSubRegIdx(Record *Def) {
  CodeGenSubRegIndex *&Idx = Def2SubRegIdx[Def];
  if (Idx)
    return Idx;
  SubRegIndices.emplace_back(Def, SubRegIndices.size() + 1);
  Idx = &SubRegIndices.back();
  return Idx;
}

CodeGenRegister *CodeGenRegBank::getReg(Record *Def) {
  CodeGenRegister *&Reg = Def2Reg[Def];
  if (Reg)
    return Reg;
  Registers.emplace_back(Def, Registers.size() + 1);
  Reg = &Registers.back();
  return Reg;
}

void CodeGenRegBank::addToMaps(CodeGenRegisterClass *RC) {
  if (Record *Def = RC->getDef())
    Def2RC.insert(std::make_pair(Def, RC));

  // Duplicate classes are rejected by insert().
  // That's OK, we only care about the properties handled by CGRC::Key.
  CodeGenRegisterClass::Key K(*RC);
  Key2RC.insert(std::make_pair(K, RC));
}

// Create a synthetic sub-class if it is missing.
CodeGenRegisterClass*
CodeGenRegBank::getOrCreateSubClass(const CodeGenRegisterClass *RC,
                                    const CodeGenRegister::Vec *Members,
                                    StringRef Name) {
  // Synthetic sub-class has the same size and alignment as RC.
  CodeGenRegisterClass::Key K(Members, RC->SpillSize, RC->SpillAlignment);
  RCKeyMap::const_iterator FoundI = Key2RC.find(K);
  if (FoundI != Key2RC.end())
    return FoundI->second;

  // Sub-class doesn't exist, create a new one.
  RegClasses.emplace_back(*this, Name, K);
  addToMaps(&RegClasses.back());
  return &RegClasses.back();
}

CodeGenRegisterClass *CodeGenRegBank::getRegClass(Record *Def) {
  if (CodeGenRegisterClass *RC = Def2RC[Def])
    return RC;

  PrintFatalError(Def->getLoc(), "Not a known RegisterClass!");
}

CodeGenSubRegIndex*
CodeGenRegBank::getCompositeSubRegIndex(CodeGenSubRegIndex *A,
                                        CodeGenSubRegIndex *B) {
  // Look for an existing entry.
  CodeGenSubRegIndex *Comp = A->compose(B);
  if (Comp)
    return Comp;

  // None exists, synthesize one.
  std::string Name = A->getName() + "_then_" + B->getName();
  Comp = createSubRegIndex(Name, A->getNamespace());
  A->addComposite(B, Comp);
  return Comp;
}

CodeGenSubRegIndex *CodeGenRegBank::
getConcatSubRegIndex(const SmallVector<CodeGenSubRegIndex *, 8> &Parts) {
  assert(Parts.size() > 1 && "Need two parts to concatenate");

  // Look for an existing entry.
  CodeGenSubRegIndex *&Idx = ConcatIdx[Parts];
  if (Idx)
    return Idx;

  // None exists, synthesize one.
  std::string Name = Parts.front()->getName();
  // Determine whether all parts are contiguous.
  bool isContinuous = true;
  unsigned Size = Parts.front()->Size;
  unsigned LastOffset = Parts.front()->Offset;
  unsigned LastSize = Parts.front()->Size;
  for (unsigned i = 1, e = Parts.size(); i != e; ++i) {
    Name += '_';
    Name += Parts[i]->getName();
    Size += Parts[i]->Size;
    if (Parts[i]->Offset != (LastOffset + LastSize))
      isContinuous = false;
    LastOffset = Parts[i]->Offset;
    LastSize = Parts[i]->Size;
  }
  Idx = createSubRegIndex(Name, Parts.front()->getNamespace());
  Idx->Size = Size;
  Idx->Offset = isContinuous ? Parts.front()->Offset : -1;
  return Idx;
}

void CodeGenRegBank::computeComposites() {
  // Keep track of TopoSigs visited. We only need to visit each TopoSig once,
  // and many registers will share TopoSigs on regular architectures.
  BitVector TopoSigs(getNumTopoSigs());

  for (const auto &Reg1 : Registers) {
    // Skip identical subreg structures already processed.
    if (TopoSigs.test(Reg1.getTopoSig()))
      continue;
    TopoSigs.set(Reg1.getTopoSig());

    const CodeGenRegister::SubRegMap &SRM1 = Reg1.getSubRegs();
    for (CodeGenRegister::SubRegMap::const_iterator i1 = SRM1.begin(),
         e1 = SRM1.end(); i1 != e1; ++i1) {
      CodeGenSubRegIndex *Idx1 = i1->first;
      CodeGenRegister *Reg2 = i1->second;
      // Ignore identity compositions.
      if (&Reg1 == Reg2)
        continue;
      const CodeGenRegister::SubRegMap &SRM2 = Reg2->getSubRegs();
      // Try composing Idx1 with another SubRegIndex.
      for (CodeGenRegister::SubRegMap::const_iterator i2 = SRM2.begin(),
           e2 = SRM2.end(); i2 != e2; ++i2) {
        CodeGenSubRegIndex *Idx2 = i2->first;
        CodeGenRegister *Reg3 = i2->second;
        // Ignore identity compositions.
        if (Reg2 == Reg3)
          continue;
        // OK Reg1:IdxPair == Reg3. Find the index with Reg:Idx == Reg3.
        CodeGenSubRegIndex *Idx3 = Reg1.getSubRegIndex(Reg3);
        assert(Idx3 && "Sub-register doesn't have an index");

        // Conflicting composition? Emit a warning but allow it.
        if (CodeGenSubRegIndex *Prev = Idx1->addComposite(Idx2, Idx3))
          PrintWarning(Twine("SubRegIndex ") + Idx1->getQualifiedName() +
                       " and " + Idx2->getQualifiedName() +
                       " compose ambiguously as " + Prev->getQualifiedName() +
                       " or " + Idx3->getQualifiedName());
      }
    }
  }
}

// Compute lane masks. This is similar to register units, but at the
// sub-register index level. Each bit in the lane mask is like a register unit
// class, and two lane masks will have a bit in common if two sub-register
// indices overlap in some register.
//
// Conservatively share a lane mask bit if two sub-register indices overlap in
// some registers, but not in others. That shouldn't happen a lot.
void CodeGenRegBank::computeSubRegLaneMasks() {
  // First assign individual bits to all the leaf indices.
  unsigned Bit = 0;
  // Determine mask of lanes that cover their registers.
  CoveringLanes = ~0u;
  for (auto &Idx : SubRegIndices) {
    if (Idx.getComposites().empty()) {
      if (Bit > 32) {
        PrintFatalError(
          Twine("Ran out of lanemask bits to represent subregister ")
          + Idx.getName());
      }
      Idx.LaneMask = 1u << Bit;
      ++Bit;
    } else {
      Idx.LaneMask = 0;
    }
  }

  // Compute transformation sequences for composeSubRegIndexLaneMask. The idea
  // here is that for each possible target subregister we look at the leafs
  // in the subregister graph that compose for this target and create
  // transformation sequences for the lanemasks. Each step in the sequence
  // consists of a bitmask and a bitrotate operation. As the rotation amounts
  // are usually the same for many subregisters we can easily combine the steps
  // by combining the masks.
  for (const auto &Idx : SubRegIndices) {
    const auto &Composites = Idx.getComposites();
    auto &LaneTransforms = Idx.CompositionLaneMaskTransform;
    // Go through all leaf subregisters and find the ones that compose with Idx.
    // These make out all possible valid bits in the lane mask we want to
    // transform. Looking only at the leafs ensure that only a single bit in
    // the mask is set.
    unsigned NextBit = 0;
    for (auto &Idx2 : SubRegIndices) {
      // Skip non-leaf subregisters.
      if (!Idx2.getComposites().empty())
        continue;
      // Replicate the behaviour from the lane mask generation loop above.
      unsigned SrcBit = NextBit;
      unsigned SrcMask = 1u << SrcBit;
      if (NextBit < 31)
        ++NextBit;
      assert(Idx2.LaneMask == SrcMask);

      // Get the composed subregister if there is any.
      auto C = Composites.find(&Idx2);
      if (C == Composites.end())
        continue;
      const CodeGenSubRegIndex *Composite = C->second;
      // The Composed subreg should be a leaf subreg too
      assert(Composite->getComposites().empty());

      // Create Mask+Rotate operation and merge with existing ops if possible.
      unsigned DstBit = Log2_32(Composite->LaneMask);
      int Shift = DstBit - SrcBit;
      uint8_t RotateLeft = Shift >= 0 ? (uint8_t)Shift : 32+Shift;
      for (auto &I : LaneTransforms) {
        if (I.RotateLeft == RotateLeft) {
          I.Mask |= SrcMask;
          SrcMask = 0;
        }
      }
      if (SrcMask != 0) {
        MaskRolPair MaskRol = { SrcMask, RotateLeft };
        LaneTransforms.push_back(MaskRol);
      }
    }
    // Optimize if the transformation consists of one step only: Set mask to
    // 0xffffffff (including some irrelevant invalid bits) so that it should
    // merge with more entries later while compressing the table.
    if (LaneTransforms.size() == 1)
      LaneTransforms[0].Mask = ~0u;

    // Further compression optimization: For invalid compositions resulting
    // in a sequence with 0 entries we can just pick any other. Choose
    // Mask 0xffffffff with Rotation 0.
    if (LaneTransforms.size() == 0) {
      MaskRolPair P = { ~0u, 0 };
      LaneTransforms.push_back(P);
    }
  }

  // FIXME: What if ad-hoc aliasing introduces overlaps that aren't represented
  // by the sub-register graph? This doesn't occur in any known targets.

  // Inherit lanes from composites.
  for (const auto &Idx : SubRegIndices) {
    unsigned Mask = Idx.computeLaneMask();
    // If some super-registers without CoveredBySubRegs use this index, we can
    // no longer assume that the lanes are covering their registers.
    if (!Idx.AllSuperRegsCovered)
      CoveringLanes &= ~Mask;
  }

  // Compute lane mask combinations for register classes.
  for (auto &RegClass : RegClasses) {
    unsigned LaneMask = 0;
    for (const auto &SubRegIndex : SubRegIndices) {
      if (RegClass.getSubClassWithSubReg(&SubRegIndex) == nullptr)
        continue;
      LaneMask |= SubRegIndex.LaneMask;
    }

    // For classes without any subregisters set LaneMask to ~0u instead of 0.
    // This makes it easier for client code to handle classes uniformly.
    if (LaneMask == 0)
      LaneMask = ~0u;

    RegClass.LaneMask = LaneMask;
  }
}

namespace {
// UberRegSet is a helper class for computeRegUnitWeights. Each UberRegSet is
// the transitive closure of the union of overlapping register
// classes. Together, the UberRegSets form a partition of the registers. If we
// consider overlapping register classes to be connected, then each UberRegSet
// is a set of connected components.
//
// An UberRegSet will likely be a horizontal slice of register names of
// the same width. Nontrivial subregisters should then be in a separate
// UberRegSet. But this property isn't required for valid computation of
// register unit weights.
//
// A Weight field caches the max per-register unit weight in each UberRegSet.
//
// A set of SingularDeterminants flags single units of some register in this set
// for which the unit weight equals the set weight. These units should not have
// their weight increased.
struct UberRegSet {
  CodeGenRegister::Vec Regs;
  unsigned Weight;
  CodeGenRegister::RegUnitList SingularDeterminants;

  UberRegSet(): Weight(0) {}
};
} // namespace

// Partition registers into UberRegSets, where each set is the transitive
// closure of the union of overlapping register classes.
//
// UberRegSets[0] is a special non-allocatable set.
static void computeUberSets(std::vector<UberRegSet> &UberSets,
                            std::vector<UberRegSet*> &RegSets,
                            CodeGenRegBank &RegBank) {

  const auto &Registers = RegBank.getRegisters();

  // The Register EnumValue is one greater than its index into Registers.
  assert(Registers.size() == Registers.back().EnumValue &&
         "register enum value mismatch");

  // For simplicitly make the SetID the same as EnumValue.
  IntEqClasses UberSetIDs(Registers.size()+1);
  std::set<unsigned> AllocatableRegs;
  for (auto &RegClass : RegBank.getRegClasses()) {
    if (!RegClass.Allocatable)
      continue;

    const CodeGenRegister::Vec &Regs = RegClass.getMembers();
    if (Regs.empty())
      continue;

    unsigned USetID = UberSetIDs.findLeader((*Regs.begin())->EnumValue);
    assert(USetID && "register number 0 is invalid");

    AllocatableRegs.insert((*Regs.begin())->EnumValue);
    for (auto I = std::next(Regs.begin()), E = Regs.end(); I != E; ++I) {
      AllocatableRegs.insert((*I)->EnumValue);
      UberSetIDs.join(USetID, (*I)->EnumValue);
    }
  }
  // Combine non-allocatable regs.
  for (const auto &Reg : Registers) {
    unsigned RegNum = Reg.EnumValue;
    if (AllocatableRegs.count(RegNum))
      continue;

    UberSetIDs.join(0, RegNum);
  }
  UberSetIDs.compress();

  // Make the first UberSet a special unallocatable set.
  unsigned ZeroID = UberSetIDs[0];

  // Insert Registers into the UberSets formed by union-find.
  // Do not resize after this.
  UberSets.resize(UberSetIDs.getNumClasses());
  unsigned i = 0;
  for (const CodeGenRegister &Reg : Registers) {
    unsigned USetID = UberSetIDs[Reg.EnumValue];
    if (!USetID)
      USetID = ZeroID;
    else if (USetID == ZeroID)
      USetID = 0;

    UberRegSet *USet = &UberSets[USetID];
    USet->Regs.push_back(&Reg);
    sortAndUniqueRegisters(USet->Regs);
    RegSets[i++] = USet;
  }
}

// Recompute each UberSet weight after changing unit weights.
static void computeUberWeights(std::vector<UberRegSet> &UberSets,
                               CodeGenRegBank &RegBank) {
  // Skip the first unallocatable set.
  for (std::vector<UberRegSet>::iterator I = std::next(UberSets.begin()),
         E = UberSets.end(); I != E; ++I) {

    // Initialize all unit weights in this set, and remember the max units/reg.
    const CodeGenRegister *Reg = nullptr;
    unsigned MaxWeight = 0, Weight = 0;
    for (RegUnitIterator UnitI(I->Regs); UnitI.isValid(); ++UnitI) {
      if (Reg != UnitI.getReg()) {
        if (Weight > MaxWeight)
          MaxWeight = Weight;
        Reg = UnitI.getReg();
        Weight = 0;
      }
      unsigned UWeight = RegBank.getRegUnit(*UnitI).Weight;
      if (!UWeight) {
        UWeight = 1;
        RegBank.increaseRegUnitWeight(*UnitI, UWeight);
      }
      Weight += UWeight;
    }
    if (Weight > MaxWeight)
      MaxWeight = Weight;
    if (I->Weight != MaxWeight) {
      DEBUG(
        dbgs() << "UberSet " << I - UberSets.begin() << " Weight " << MaxWeight;
        for (auto &Unit : I->Regs)
          dbgs() << " " << Unit->getName();
        dbgs() << "\n");
      // Update the set weight.
      I->Weight = MaxWeight;
    }

    // Find singular determinants.
    for (const auto R : I->Regs) {
      if (R->getRegUnits().count() == 1 && R->getWeight(RegBank) == I->Weight) {
        I->SingularDeterminants |= R->getRegUnits();
      }
    }
  }
}

// normalizeWeight is a computeRegUnitWeights helper that adjusts the weight of
// a register and its subregisters so that they have the same weight as their
// UberSet. Self-recursion processes the subregister tree in postorder so
// subregisters are normalized first.
//
// Side effects:
// - creates new adopted register units
// - causes superregisters to inherit adopted units
// - increases the weight of "singular" units
// - induces recomputation of UberWeights.
static bool normalizeWeight(CodeGenRegister *Reg,
                            std::vector<UberRegSet> &UberSets,
                            std::vector<UberRegSet*> &RegSets,
                            SparseBitVector<> &NormalRegs,
                            CodeGenRegister::RegUnitList &NormalUnits,
                            CodeGenRegBank &RegBank) {
  if (NormalRegs.test(Reg->EnumValue))
    return false;
  NormalRegs.set(Reg->EnumValue);

  bool Changed = false;
  const CodeGenRegister::SubRegMap &SRM = Reg->getSubRegs();
  for (CodeGenRegister::SubRegMap::const_iterator SRI = SRM.begin(),
         SRE = SRM.end(); SRI != SRE; ++SRI) {
    if (SRI->second == Reg)
      continue; // self-cycles happen

    Changed |= normalizeWeight(SRI->second, UberSets, RegSets,
                               NormalRegs, NormalUnits, RegBank);
  }
  // Postorder register normalization.

  // Inherit register units newly adopted by subregisters.
  if (Reg->inheritRegUnits(RegBank))
    computeUberWeights(UberSets, RegBank);

  // Check if this register is too skinny for its UberRegSet.
  UberRegSet *UberSet = RegSets[RegBank.getRegIndex(Reg)];

  unsigned RegWeight = Reg->getWeight(RegBank);
  if (UberSet->Weight > RegWeight) {
    // A register unit's weight can be adjusted only if it is the singular unit
    // for this register, has not been used to normalize a subregister's set,
    // and has not already been used to singularly determine this UberRegSet.
    unsigned AdjustUnit = *Reg->getRegUnits().begin();
    if (Reg->getRegUnits().count() != 1
        || hasRegUnit(NormalUnits, AdjustUnit)
        || hasRegUnit(UberSet->SingularDeterminants, AdjustUnit)) {
      // We don't have an adjustable unit, so adopt a new one.
      AdjustUnit = RegBank.newRegUnit(UberSet->Weight - RegWeight);
      Reg->adoptRegUnit(AdjustUnit);
      // Adopting a unit does not immediately require recomputing set weights.
    }
    else {
      // Adjust the existing single unit.
      RegBank.increaseRegUnitWeight(AdjustUnit, UberSet->Weight - RegWeight);
      // The unit may be shared among sets and registers within this set.
      computeUberWeights(UberSets, RegBank);
    }
    Changed = true;
  }

  // Mark these units normalized so superregisters can't change their weights.
  NormalUnits |= Reg->getRegUnits();

  return Changed;
}

// Compute a weight for each register unit created during getSubRegs.
//
// The goal is that two registers in the same class will have the same weight,
// where each register's weight is defined as sum of its units' weights.
void CodeGenRegBank::computeRegUnitWeights() {
  std::vector<UberRegSet> UberSets;
  std::vector<UberRegSet*> RegSets(Registers.size());
  computeUberSets(UberSets, RegSets, *this);
  // UberSets and RegSets are now immutable.

  computeUberWeights(UberSets, *this);

  // Iterate over each Register, normalizing the unit weights until reaching
  // a fix point.
  unsigned NumIters = 0;
  for (bool Changed = true; Changed; ++NumIters) {
    assert(NumIters <= NumNativeRegUnits && "Runaway register unit weights");
    Changed = false;
    for (auto &Reg : Registers) {
      CodeGenRegister::RegUnitList NormalUnits;
      SparseBitVector<> NormalRegs;
      Changed |= normalizeWeight(&Reg, UberSets, RegSets, NormalRegs,
                                 NormalUnits, *this);
    }
  }

  // Remove warning for unused variable NumIters
  ((void)NumIters);
}

// Find a set in UniqueSets with the same elements as Set.
// Return an iterator into UniqueSets.
static std::vector<RegUnitSet>::const_iterator
findRegUnitSet(const std::vector<RegUnitSet> &UniqueSets,
               const RegUnitSet &Set) {
  std::vector<RegUnitSet>::const_iterator
    I = UniqueSets.begin(), E = UniqueSets.end();
  for(;I != E; ++I) {
    if (I->Units == Set.Units)
      break;
  }
  return I;
}

// Return true if the RUSubSet is a subset of RUSuperSet.
static bool isRegUnitSubSet(const std::vector<unsigned> &RUSubSet,
                            const std::vector<unsigned> &RUSuperSet) {
  return std::includes(RUSuperSet.begin(), RUSuperSet.end(),
                       RUSubSet.begin(), RUSubSet.end());
}

/// Iteratively prune unit sets. Prune subsets that are close to the superset,
/// but with one or two registers removed. We occasionally have registers like
/// APSR and PC thrown in with the general registers. We also see many
/// special-purpose register subsets, such as tail-call and Thumb
/// encodings. Generating all possible overlapping sets is combinatorial and
/// overkill for modeling pressure. Ideally we could fix this statically in
/// tablegen by (1) having the target define register classes that only include
/// the allocatable registers and marking other classes as non-allocatable and
/// (2) having a way to mark special purpose classes as "don't-care" classes for
/// the purpose of pressure.  However, we make an attempt to handle targets that
/// are not nicely defined by merging nearly identical register unit sets
/// statically. This generates smaller tables. Then, dynamically, we adjust the
/// set limit by filtering the reserved registers.
///
/// Merge sets only if the units have the same weight. For example, on ARM,
/// Q-tuples with ssub index 0 include all S regs but also include D16+. We
/// should not expand the S set to include D regs.
void CodeGenRegBank::pruneUnitSets() {
  assert(RegClassUnitSets.empty() && "this invalidates RegClassUnitSets");

  // Form an equivalence class of UnitSets with no significant difference.
  std::vector<unsigned> SuperSetIDs;
  for (unsigned SubIdx = 0, EndIdx = RegUnitSets.size();
       SubIdx != EndIdx; ++SubIdx) {
    const RegUnitSet &SubSet = RegUnitSets[SubIdx];
    unsigned SuperIdx = 0;
    for (; SuperIdx != EndIdx; ++SuperIdx) {
      if (SuperIdx == SubIdx)
        continue;

      unsigned UnitWeight = RegUnits[SubSet.Units[0]].Weight;
      const RegUnitSet &SuperSet = RegUnitSets[SuperIdx];
      if (isRegUnitSubSet(SubSet.Units, SuperSet.Units)
          && (SubSet.Units.size() + 3 > SuperSet.Units.size())
          && UnitWeight == RegUnits[SuperSet.Units[0]].Weight
          && UnitWeight == RegUnits[SuperSet.Units.back()].Weight) {
        DEBUG(dbgs() << "UnitSet " << SubIdx << " subsumed by " << SuperIdx
              << "\n");
        // We can pick any of the set names for the merged set. Go for the
        // shortest one to avoid picking the name of one of the classes that are
        // artificially created by tablegen. So "FPR128_lo" instead of
        // "QQQQ_with_qsub3_in_FPR128_lo".
        if (RegUnitSets[SubIdx].Name.size() < RegUnitSets[SuperIdx].Name.size())
          RegUnitSets[SuperIdx].Name = RegUnitSets[SubIdx].Name;
        break;
      }
    }
    if (SuperIdx == EndIdx)
      SuperSetIDs.push_back(SubIdx);
  }
  // Populate PrunedUnitSets with each equivalence class's superset.
  std::vector<RegUnitSet> PrunedUnitSets(SuperSetIDs.size());
  for (unsigned i = 0, e = SuperSetIDs.size(); i != e; ++i) {
    unsigned SuperIdx = SuperSetIDs[i];
    PrunedUnitSets[i].Name = RegUnitSets[SuperIdx].Name;
    PrunedUnitSets[i].Units.swap(RegUnitSets[SuperIdx].Units);
  }
  RegUnitSets.swap(PrunedUnitSets);
}

// Create a RegUnitSet for each RegClass that contains all units in the class
// including adopted units that are necessary to model register pressure. Then
// iteratively compute RegUnitSets such that the union of any two overlapping
// RegUnitSets is repreresented.
//
// RegisterInfoEmitter will map each RegClass to its RegUnitClass and any
// RegUnitSet that is a superset of that RegUnitClass.
void CodeGenRegBank::computeRegUnitSets() {
  assert(RegUnitSets.empty() && "dirty RegUnitSets");

  // Compute a unique RegUnitSet for each RegClass.
  auto &RegClasses = getRegClasses();
  for (auto &RC : RegClasses) {
    if (!RC.Allocatable)
      continue;

    // Speculatively grow the RegUnitSets to hold the new set.
    RegUnitSets.resize(RegUnitSets.size() + 1);
    RegUnitSets.back().Name = RC.getName();

    // Compute a sorted list of units in this class.
    RC.buildRegUnitSet(RegUnitSets.back().Units);

    // Find an existing RegUnitSet.
    std::vector<RegUnitSet>::const_iterator SetI =
      findRegUnitSet(RegUnitSets, RegUnitSets.back());
    if (SetI != std::prev(RegUnitSets.end()))
      RegUnitSets.pop_back();
  }

  DEBUG(dbgs() << "\nBefore pruning:\n";
        for (unsigned USIdx = 0, USEnd = RegUnitSets.size();
             USIdx < USEnd; ++USIdx) {
          dbgs() << "UnitSet " << USIdx << " " << RegUnitSets[USIdx].Name
                 << ":";
          for (auto &U : RegUnitSets[USIdx].Units)
            dbgs() << " " << RegUnits[U].Roots[0]->getName();
          dbgs() << "\n";
        });

  // Iteratively prune unit sets.
  pruneUnitSets();

  DEBUG(dbgs() << "\nBefore union:\n";
        for (unsigned USIdx = 0, USEnd = RegUnitSets.size();
             USIdx < USEnd; ++USIdx) {
          dbgs() << "UnitSet " << USIdx << " " << RegUnitSets[USIdx].Name
                 << ":";
          for (auto &U : RegUnitSets[USIdx].Units)
            dbgs() << " " << RegUnits[U].Roots[0]->getName();
          dbgs() << "\n";
        }
        dbgs() << "\nUnion sets:\n");

  // Iterate over all unit sets, including new ones added by this loop.
  unsigned NumRegUnitSubSets = RegUnitSets.size();
  for (unsigned Idx = 0, EndIdx = RegUnitSets.size(); Idx != EndIdx; ++Idx) {
    // In theory, this is combinatorial. In practice, it needs to be bounded
    // by a small number of sets for regpressure to be efficient.
    // If the assert is hit, we need to implement pruning.
    assert(Idx < (2*NumRegUnitSubSets) && "runaway unit set inference");

    // Compare new sets with all original classes.
    for (unsigned SearchIdx = (Idx >= NumRegUnitSubSets) ? 0 : Idx+1;
         SearchIdx != EndIdx; ++SearchIdx) {
      std::set<unsigned> Intersection;
      std::set_intersection(RegUnitSets[Idx].Units.begin(),
                            RegUnitSets[Idx].Units.end(),
                            RegUnitSets[SearchIdx].Units.begin(),
                            RegUnitSets[SearchIdx].Units.end(),
                            std::inserter(Intersection, Intersection.begin()));
      if (Intersection.empty())
        continue;

      // Speculatively grow the RegUnitSets to hold the new set.
      RegUnitSets.resize(RegUnitSets.size() + 1);
      RegUnitSets.back().Name =
        RegUnitSets[Idx].Name + "+" + RegUnitSets[SearchIdx].Name;

      std::set_union(RegUnitSets[Idx].Units.begin(),
                     RegUnitSets[Idx].Units.end(),
                     RegUnitSets[SearchIdx].Units.begin(),
                     RegUnitSets[SearchIdx].Units.end(),
                     std::inserter(RegUnitSets.back().Units,
                                   RegUnitSets.back().Units.begin()));

      // Find an existing RegUnitSet, or add the union to the unique sets.
      std::vector<RegUnitSet>::const_iterator SetI =
        findRegUnitSet(RegUnitSets, RegUnitSets.back());
      if (SetI != std::prev(RegUnitSets.end()))
        RegUnitSets.pop_back();
      else {
        DEBUG(dbgs() << "UnitSet " << RegUnitSets.size()-1
              << " " << RegUnitSets.back().Name << ":";
              for (auto &U : RegUnitSets.back().Units)
                dbgs() << " " << RegUnits[U].Roots[0]->getName();
              dbgs() << "\n";);
      }
    }
  }

  // Iteratively prune unit sets after inferring supersets.
  pruneUnitSets();

  DEBUG(dbgs() << "\n";
        for (unsigned USIdx = 0, USEnd = RegUnitSets.size();
             USIdx < USEnd; ++USIdx) {
          dbgs() << "UnitSet " << USIdx << " " << RegUnitSets[USIdx].Name
                 << ":";
          for (auto &U : RegUnitSets[USIdx].Units)
            dbgs() << " " << RegUnits[U].Roots[0]->getName();
          dbgs() << "\n";
        });

  // For each register class, list the UnitSets that are supersets.
  RegClassUnitSets.resize(RegClasses.size());
  int RCIdx = -1;
  for (auto &RC : RegClasses) {
    ++RCIdx;
    if (!RC.Allocatable)
      continue;

    // Recompute the sorted list of units in this class.
    std::vector<unsigned> RCRegUnits;
    RC.buildRegUnitSet(RCRegUnits);

    // Don't increase pressure for unallocatable regclasses.
    if (RCRegUnits.empty())
      continue;

    DEBUG(dbgs() << "RC " << RC.getName() << " Units: \n";
          for (auto &U : RCRegUnits)
            dbgs() << RegUnits[U].getRoots()[0]->getName() << " ";
          dbgs() << "\n  UnitSetIDs:");

    // Find all supersets.
    for (unsigned USIdx = 0, USEnd = RegUnitSets.size();
         USIdx != USEnd; ++USIdx) {
      if (isRegUnitSubSet(RCRegUnits, RegUnitSets[USIdx].Units)) {
        DEBUG(dbgs() << " " << USIdx);
        RegClassUnitSets[RCIdx].push_back(USIdx);
      }
    }
    DEBUG(dbgs() << "\n");
    assert(!RegClassUnitSets[RCIdx].empty() && "missing unit set for regclass");
  }

  // For each register unit, ensure that we have the list of UnitSets that
  // contain the unit. Normally, this matches an existing list of UnitSets for a
  // register class. If not, we create a new entry in RegClassUnitSets as a
  // "fake" register class.
  for (unsigned UnitIdx = 0, UnitEnd = NumNativeRegUnits;
       UnitIdx < UnitEnd; ++UnitIdx) {
    std::vector<unsigned> RUSets;
    for (unsigned i = 0, e = RegUnitSets.size(); i != e; ++i) {
      RegUnitSet &RUSet = RegUnitSets[i];
      if (std::find(RUSet.Units.begin(), RUSet.Units.end(), UnitIdx)
          == RUSet.Units.end())
        continue;
      RUSets.push_back(i);
    }
    unsigned RCUnitSetsIdx = 0;
    for (unsigned e = RegClassUnitSets.size();
         RCUnitSetsIdx != e; ++RCUnitSetsIdx) {
      if (RegClassUnitSets[RCUnitSetsIdx] == RUSets) {
        break;
      }
    }
    RegUnits[UnitIdx].RegClassUnitSetsIdx = RCUnitSetsIdx;
    if (RCUnitSetsIdx == RegClassUnitSets.size()) {
      // Create a new list of UnitSets as a "fake" register class.
      RegClassUnitSets.resize(RCUnitSetsIdx + 1);
      RegClassUnitSets[RCUnitSetsIdx].swap(RUSets);
    }
  }
}

void CodeGenRegBank::computeRegUnitLaneMasks() {
  for (auto &Register : Registers) {
    // Create an initial lane mask for all register units.
    const auto &RegUnits = Register.getRegUnits();
    CodeGenRegister::RegUnitLaneMaskList RegUnitLaneMasks(RegUnits.count(), 0);
    // Iterate through SubRegisters.
    typedef CodeGenRegister::SubRegMap SubRegMap;
    const SubRegMap &SubRegs = Register.getSubRegs();
    for (SubRegMap::const_iterator S = SubRegs.begin(),
         SE = SubRegs.end(); S != SE; ++S) {
      CodeGenRegister *SubReg = S->second;
      // Ignore non-leaf subregisters, their lane masks are fully covered by
      // the leaf subregisters anyway.
      if (SubReg->getSubRegs().size() != 0)
        continue;
      CodeGenSubRegIndex *SubRegIndex = S->first;
      const CodeGenRegister *SubRegister = S->second;
      unsigned LaneMask = SubRegIndex->LaneMask;
      // Distribute LaneMask to Register Units touched.
      for (unsigned SUI : SubRegister->getRegUnits()) {
        bool Found = false;
        unsigned u = 0;
        for (unsigned RU : RegUnits) {
          if (SUI == RU) {
            RegUnitLaneMasks[u] |= LaneMask;
            assert(!Found);
            Found = true;
          }
          ++u;
        }
        (void)Found;
        assert(Found);
      }
    }
    Register.setRegUnitLaneMasks(RegUnitLaneMasks);
  }
}

void CodeGenRegBank::computeDerivedInfo() {
  computeComposites();
  computeSubRegLaneMasks();

  // Compute a weight for each register unit created during getSubRegs.
  // This may create adopted register units (with unit # >= NumNativeRegUnits).
  computeRegUnitWeights();

  // Compute a unique set of RegUnitSets. One for each RegClass and inferred
  // supersets for the union of overlapping sets.
  computeRegUnitSets();

  computeRegUnitLaneMasks();

  // Compute register class HasDisjunctSubRegs flag.
  for (CodeGenRegisterClass &RC : RegClasses) {
    RC.HasDisjunctSubRegs = false;
    for (const CodeGenRegister *Reg : RC.getMembers())
      RC.HasDisjunctSubRegs |= Reg->HasDisjunctSubRegs;
  }

  // Get the weight of each set.
  for (unsigned Idx = 0, EndIdx = RegUnitSets.size(); Idx != EndIdx; ++Idx)
    RegUnitSets[Idx].Weight = getRegUnitSetWeight(RegUnitSets[Idx].Units);

  // Find the order of each set.
  RegUnitSetOrder.reserve(RegUnitSets.size());
  for (unsigned Idx = 0, EndIdx = RegUnitSets.size(); Idx != EndIdx; ++Idx)
    RegUnitSetOrder.push_back(Idx);

  std::stable_sort(RegUnitSetOrder.begin(), RegUnitSetOrder.end(),
                   [this](unsigned ID1, unsigned ID2) {
    return getRegPressureSet(ID1).Units.size() <
           getRegPressureSet(ID2).Units.size();
  });
  for (unsigned Idx = 0, EndIdx = RegUnitSets.size(); Idx != EndIdx; ++Idx) {
    RegUnitSets[RegUnitSetOrder[Idx]].Order = Idx;
  }
}

//
// Synthesize missing register class intersections.
//
// Make sure that sub-classes of RC exists such that getCommonSubClass(RC, X)
// returns a maximal register class for all X.
//
void CodeGenRegBank::inferCommonSubClass(CodeGenRegisterClass *RC) {
  assert(!RegClasses.empty());
  // Stash the iterator to the last element so that this loop doesn't visit
  // elements added by the getOrCreateSubClass call within it.
  for (auto I = RegClasses.begin(), E = std::prev(RegClasses.end());
       I != std::next(E); ++I) {
    CodeGenRegisterClass *RC1 = RC;
    CodeGenRegisterClass *RC2 = &*I;
    if (RC1 == RC2)
      continue;

    // Compute the set intersection of RC1 and RC2.
    const CodeGenRegister::Vec &Memb1 = RC1->getMembers();
    const CodeGenRegister::Vec &Memb2 = RC2->getMembers();
    CodeGenRegister::Vec Intersection;
    std::set_intersection(
        Memb1.begin(), Memb1.end(), Memb2.begin(), Memb2.end(),
        std::inserter(Intersection, Intersection.begin()), deref<llvm::less>());

    // Skip disjoint class pairs.
    if (Intersection.empty())
      continue;

    // If RC1 and RC2 have different spill sizes or alignments, use the
    // larger size for sub-classing.  If they are equal, prefer RC1.
    if (RC2->SpillSize > RC1->SpillSize ||
        (RC2->SpillSize == RC1->SpillSize &&
         RC2->SpillAlignment > RC1->SpillAlignment))
      std::swap(RC1, RC2);

    getOrCreateSubClass(RC1, &Intersection,
                        RC1->getName() + "_and_" + RC2->getName());
  }
}

//
// Synthesize missing sub-classes for getSubClassWithSubReg().
//
// Make sure that the set of registers in RC with a given SubIdx sub-register
// form a register class.  Update RC->SubClassWithSubReg.
//
void CodeGenRegBank::inferSubClassWithSubReg(CodeGenRegisterClass *RC) {
  // Map SubRegIndex to set of registers in RC supporting that SubRegIndex.
  typedef std::map<const CodeGenSubRegIndex *, CodeGenRegister::Vec,
                   deref<llvm::less>> SubReg2SetMap;

  // Compute the set of registers supporting each SubRegIndex.
  SubReg2SetMap SRSets;
  for (const auto R : RC->getMembers()) {
    const CodeGenRegister::SubRegMap &SRM = R->getSubRegs();
    for (CodeGenRegister::SubRegMap::const_iterator I = SRM.begin(),
         E = SRM.end(); I != E; ++I)
      SRSets[I->first].push_back(R);
  }

  for (auto I : SRSets)
    sortAndUniqueRegisters(I.second);

  // Find matching classes for all SRSets entries.  Iterate in SubRegIndex
  // numerical order to visit synthetic indices last.
  for (const auto &SubIdx : SubRegIndices) {
    SubReg2SetMap::const_iterator I = SRSets.find(&SubIdx);
    // Unsupported SubRegIndex. Skip it.
    if (I == SRSets.end())
      continue;
    // In most cases, all RC registers support the SubRegIndex.
    if (I->second.size() == RC->getMembers().size()) {
      RC->setSubClassWithSubReg(&SubIdx, RC);
      continue;
    }
    // This is a real subset.  See if we have a matching class.
    CodeGenRegisterClass *SubRC =
      getOrCreateSubClass(RC, &I->second,
                          RC->getName() + "_with_" + I->first->getName());
    RC->setSubClassWithSubReg(&SubIdx, SubRC);
  }
}

//
// Synthesize missing sub-classes of RC for getMatchingSuperRegClass().
//
// Create sub-classes of RC such that getMatchingSuperRegClass(RC, SubIdx, X)
// has a maximal result for any SubIdx and any X >= FirstSubRegRC.
//

void CodeGenRegBank::inferMatchingSuperRegClass(CodeGenRegisterClass *RC,
                                                std::list<CodeGenRegisterClass>::iterator FirstSubRegRC) {
  SmallVector<std::pair<const CodeGenRegister*,
                        const CodeGenRegister*>, 16> SSPairs;
  BitVector TopoSigs(getNumTopoSigs());

  // Iterate in SubRegIndex numerical order to visit synthetic indices last.
  for (auto &SubIdx : SubRegIndices) {
    // Skip indexes that aren't fully supported by RC's registers. This was
    // computed by inferSubClassWithSubReg() above which should have been
    // called first.
    if (RC->getSubClassWithSubReg(&SubIdx) != RC)
      continue;

    // Build list of (Super, Sub) pairs for this SubIdx.
    SSPairs.clear();
    TopoSigs.reset();
    for (const auto Super : RC->getMembers()) {
      const CodeGenRegister *Sub = Super->getSubRegs().find(&SubIdx)->second;
      assert(Sub && "Missing sub-register");
      SSPairs.push_back(std::make_pair(Super, Sub));
      TopoSigs.set(Sub->getTopoSig());
    }

    // Iterate over sub-register class candidates.  Ignore classes created by
    // this loop. They will never be useful.
    // Store an iterator to the last element (not end) so that this loop doesn't
    // visit newly inserted elements.
    assert(!RegClasses.empty());
    for (auto I = FirstSubRegRC, E = std::prev(RegClasses.end());
         I != std::next(E); ++I) {
      CodeGenRegisterClass &SubRC = *I;
      // Topological shortcut: SubRC members have the wrong shape.
      if (!TopoSigs.anyCommon(SubRC.getTopoSigs()))
        continue;
      // Compute the subset of RC that maps into SubRC.
      CodeGenRegister::Vec SubSetVec;
      for (unsigned i = 0, e = SSPairs.size(); i != e; ++i)
        if (SubRC.contains(SSPairs[i].second))
          SubSetVec.push_back(SSPairs[i].first);

      if (SubSetVec.empty())
        continue;

      // RC injects completely into SubRC.
      sortAndUniqueRegisters(SubSetVec);
      if (SubSetVec.size() == SSPairs.size()) {
        SubRC.addSuperRegClass(&SubIdx, RC);
        continue;
      }

      // Only a subset of RC maps into SubRC. Make sure it is represented by a
      // class.
      getOrCreateSubClass(RC, &SubSetVec, RC->getName() + "_with_" +
                                          SubIdx.getName() + "_in_" +
                                          SubRC.getName());
    }
  }
}


//
// Infer missing register classes.
//
void CodeGenRegBank::computeInferredRegisterClasses() {
  assert(!RegClasses.empty());
  // When this function is called, the register classes have not been sorted
  // and assigned EnumValues yet.  That means getSubClasses(),
  // getSuperClasses(), and hasSubClass() functions are defunct.

  // Use one-before-the-end so it doesn't move forward when new elements are
  // added.
  auto FirstNewRC = std::prev(RegClasses.end());

  // Visit all register classes, including the ones being added by the loop.
  // Watch out for iterator invalidation here.
  for (auto I = RegClasses.begin(), E = RegClasses.end(); I != E; ++I) {
    CodeGenRegisterClass *RC = &*I;

    // Synthesize answers for getSubClassWithSubReg().
    inferSubClassWithSubReg(RC);

    // Synthesize answers for getCommonSubClass().
    inferCommonSubClass(RC);

    // Synthesize answers for getMatchingSuperRegClass().
    inferMatchingSuperRegClass(RC);

    // New register classes are created while this loop is running, and we need
    // to visit all of them.  I  particular, inferMatchingSuperRegClass needs
    // to match old super-register classes with sub-register classes created
    // after inferMatchingSuperRegClass was called.  At this point,
    // inferMatchingSuperRegClass has checked SuperRC = [0..rci] with SubRC =
    // [0..FirstNewRC).  We need to cover SubRC = [FirstNewRC..rci].
    if (I == FirstNewRC) {
      auto NextNewRC = std::prev(RegClasses.end());
      for (auto I2 = RegClasses.begin(), E2 = std::next(FirstNewRC); I2 != E2;
           ++I2)
        inferMatchingSuperRegClass(&*I2, E2);
      FirstNewRC = NextNewRC;
    }
  }
}

/// getRegisterClassForRegister - Find the register class that contains the
/// specified physical register.  If the register is not in a register class,
/// return null. If the register is in multiple classes, and the classes have a
/// superset-subset relationship and the same set of types, return the
/// superclass.  Otherwise return null.
const CodeGenRegisterClass*
CodeGenRegBank::getRegClassForRegister(Record *R) {
  const CodeGenRegister *Reg = getReg(R);
  const CodeGenRegisterClass *FoundRC = nullptr;
  for (const auto &RC : getRegClasses()) {
    if (!RC.contains(Reg))
      continue;

    // If this is the first class that contains the register,
    // make a note of it and go on to the next class.
    if (!FoundRC) {
      FoundRC = &RC;
      continue;
    }

    // If a register's classes have different types, return null.
    if (RC.getValueTypes() != FoundRC->getValueTypes())
      return nullptr;

    // Check to see if the previously found class that contains
    // the register is a subclass of the current class. If so,
    // prefer the superclass.
    if (RC.hasSubClass(FoundRC)) {
      FoundRC = &RC;
      continue;
    }

    // Check to see if the previously found class that contains
    // the register is a superclass of the current class. If so,
    // prefer the superclass.
    if (FoundRC->hasSubClass(&RC))
      continue;

    // Multiple classes, and neither is a superclass of the other.
    // Return null.
    return nullptr;
  }
  return FoundRC;
}

BitVector CodeGenRegBank::computeCoveredRegisters(ArrayRef<Record*> Regs) {
  SetVector<const CodeGenRegister*> Set;

  // First add Regs with all sub-registers.
  for (unsigned i = 0, e = Regs.size(); i != e; ++i) {
    CodeGenRegister *Reg = getReg(Regs[i]);
    if (Set.insert(Reg))
      // Reg is new, add all sub-registers.
      // The pre-ordering is not important here.
      Reg->addSubRegsPreOrder(Set, *this);
  }

  // Second, find all super-registers that are completely covered by the set.
  for (unsigned i = 0; i != Set.size(); ++i) {
    const CodeGenRegister::SuperRegList &SR = Set[i]->getSuperRegs();
    for (unsigned j = 0, e = SR.size(); j != e; ++j) {
      const CodeGenRegister *Super = SR[j];
      if (!Super->CoveredBySubRegs || Set.count(Super))
        continue;
      // This new super-register is covered by its sub-registers.
      bool AllSubsInSet = true;
      const CodeGenRegister::SubRegMap &SRM = Super->getSubRegs();
      for (CodeGenRegister::SubRegMap::const_iterator I = SRM.begin(),
             E = SRM.end(); I != E; ++I)
        if (!Set.count(I->second)) {
          AllSubsInSet = false;
          break;
        }
      // All sub-registers in Set, add Super as well.
      // We will visit Super later to recheck its super-registers.
      if (AllSubsInSet)
        Set.insert(Super);
    }
  }

  // Convert to BitVector.
  BitVector BV(Registers.size() + 1);
  for (unsigned i = 0, e = Set.size(); i != e; ++i)
    BV.set(Set[i]->EnumValue);
  return BV;
}
