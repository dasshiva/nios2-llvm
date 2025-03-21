//===-- RegAllocGreedy.cpp - greedy register allocator --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the RAGreedy function pass for register allocation in
// optimized builds.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/Passes.h"
#include "AllocationOrder.h"
#include "InterferenceCache.h"
#include "LiveDebugVariables.h"
#include "RegAllocBase.h"
#include "SpillPlacement.h"
#include "Spiller.h"
#include "SplitKit.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/CalcSpillWeights.h"
#include "llvm/CodeGen/EdgeBundles.h"
#include "llvm/CodeGen/LiveIntervalAnalysis.h"
#include "llvm/CodeGen/LiveRangeEdit.h"
#include "llvm/CodeGen/LiveRegMatrix.h"
#include "llvm/CodeGen/LiveStackAnalysis.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegAllocRegistry.h"
#include "llvm/CodeGen/RegisterClassInfo.h"
#include "llvm/CodeGen/VirtRegMap.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/PassAnalysisSupport.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetSubtargetInfo.h"
#include <queue>

using namespace llvm;

#define DEBUG_TYPE "regalloc"

STATISTIC(NumGlobalSplits, "Number of split global live ranges");
STATISTIC(NumLocalSplits,  "Number of split local live ranges");
STATISTIC(NumEvicted,      "Number of interferences evicted");

static cl::opt<SplitEditor::ComplementSpillMode>
SplitSpillMode("split-spill-mode", cl::Hidden,
  cl::desc("Spill mode for splitting live ranges"),
  cl::values(clEnumValN(SplitEditor::SM_Partition, "default", "Default"),
             clEnumValN(SplitEditor::SM_Size,  "size",  "Optimize for size"),
             clEnumValN(SplitEditor::SM_Speed, "speed", "Optimize for speed"),
             clEnumValEnd),
  cl::init(SplitEditor::SM_Partition));

static cl::opt<unsigned>
LastChanceRecoloringMaxDepth("lcr-max-depth", cl::Hidden,
                             cl::desc("Last chance recoloring max depth"),
                             cl::init(5));

static cl::opt<unsigned> LastChanceRecoloringMaxInterference(
    "lcr-max-interf", cl::Hidden,
    cl::desc("Last chance recoloring maximum number of considered"
             " interference at a time"),
    cl::init(8));

static cl::opt<bool>
ExhaustiveSearch("exhaustive-register-search", cl::NotHidden,
                 cl::desc("Exhaustive Search for registers bypassing the depth "
                          "and interference cutoffs of last chance recoloring"));

static cl::opt<bool> EnableLocalReassignment(
    "enable-local-reassign", cl::Hidden,
    cl::desc("Local reassignment can yield better allocation decisions, but "
             "may be compile time intensive"),
    cl::init(false));

static cl::opt<bool> EnableDeferredSpilling(
    "enable-deferred-spilling", cl::Hidden,
    cl::desc("Instead of spilling a variable right away, defer the actual "
             "code insertion to the end of the allocation. That way the "
             "allocator might still find a suitable coloring for this "
             "variable because of other evicted variables."),
    cl::init(false));

// FIXME: Find a good default for this flag and remove the flag.
static cl::opt<unsigned>
CSRFirstTimeCost("regalloc-csr-first-time-cost",
              cl::desc("Cost for first time use of callee-saved register."),
              cl::init(0), cl::Hidden);

static RegisterRegAlloc greedyRegAlloc("greedy", "greedy register allocator",
                                       createGreedyRegisterAllocator);

namespace {
class RAGreedy : public MachineFunctionPass,
                 public RegAllocBase,
                 private LiveRangeEdit::Delegate {
  // Convenient shortcuts.
  typedef std::priority_queue<std::pair<unsigned, unsigned> > PQueue;
  typedef SmallPtrSet<LiveInterval *, 4> SmallLISet;
  typedef SmallSet<unsigned, 16> SmallVirtRegSet;

  // context
  MachineFunction *MF;

  // Shortcuts to some useful interface.
  const TargetInstrInfo *TII;
  const TargetRegisterInfo *TRI;
  RegisterClassInfo RCI;

  // analyses
  SlotIndexes *Indexes;
  MachineBlockFrequencyInfo *MBFI;
  MachineDominatorTree *DomTree;
  MachineLoopInfo *Loops;
  EdgeBundles *Bundles;
  SpillPlacement *SpillPlacer;
  LiveDebugVariables *DebugVars;

  // state
  std::unique_ptr<Spiller> SpillerInstance;
  PQueue Queue;
  unsigned NextCascade;

  // Live ranges pass through a number of stages as we try to allocate them.
  // Some of the stages may also create new live ranges:
  //
  // - Region splitting.
  // - Per-block splitting.
  // - Local splitting.
  // - Spilling.
  //
  // Ranges produced by one of the stages skip the previous stages when they are
  // dequeued. This improves performance because we can skip interference checks
  // that are unlikely to give any results. It also guarantees that the live
  // range splitting algorithm terminates, something that is otherwise hard to
  // ensure.
  enum LiveRangeStage {
    /// Newly created live range that has never been queued.
    RS_New,

    /// Only attempt assignment and eviction. Then requeue as RS_Split.
    RS_Assign,

    /// Attempt live range splitting if assignment is impossible.
    RS_Split,

    /// Attempt more aggressive live range splitting that is guaranteed to make
    /// progress.  This is used for split products that may not be making
    /// progress.
    RS_Split2,

    /// Live range will be spilled.  No more splitting will be attempted.
    RS_Spill,


    /// Live range is in memory. Because of other evictions, it might get moved
    /// in a register in the end.
    RS_Memory,

    /// There is nothing more we can do to this live range.  Abort compilation
    /// if it can't be assigned.
    RS_Done
  };

  // Enum CutOffStage to keep a track whether the register allocation failed
  // because of the cutoffs encountered in last chance recoloring.
  // Note: This is used as bitmask. New value should be next power of 2.
  enum CutOffStage {
    // No cutoffs encountered
    CO_None = 0,

    // lcr-max-depth cutoff encountered
    CO_Depth = 1,

    // lcr-max-interf cutoff encountered
    CO_Interf = 2
  };

  uint8_t CutOffInfo;

#ifndef NDEBUG
  static const char *const StageName[];
#endif

  // RegInfo - Keep additional information about each live range.
  struct RegInfo {
    LiveRangeStage Stage;

    // Cascade - Eviction loop prevention. See canEvictInterference().
    unsigned Cascade;

    RegInfo() : Stage(RS_New), Cascade(0) {}
  };

  IndexedMap<RegInfo, VirtReg2IndexFunctor<unsigned>> ExtraRegInfo;

  LiveRangeStage getStage(const LiveInterval &VirtReg) const {
    return ExtraRegInfo[VirtReg.reg].Stage;
  }

  void setStage(const LiveInterval &VirtReg, LiveRangeStage Stage) {
    ExtraRegInfo.resize(MRI->getNumVirtRegs());
    ExtraRegInfo[VirtReg.reg].Stage = Stage;
  }

  template<typename Iterator>
  void setStage(Iterator Begin, Iterator End, LiveRangeStage NewStage) {
    ExtraRegInfo.resize(MRI->getNumVirtRegs());
    for (;Begin != End; ++Begin) {
      unsigned Reg = *Begin;
      if (ExtraRegInfo[Reg].Stage == RS_New)
        ExtraRegInfo[Reg].Stage = NewStage;
    }
  }

  /// Cost of evicting interference.
  struct EvictionCost {
    unsigned BrokenHints; ///< Total number of broken hints.
    float MaxWeight;      ///< Maximum spill weight evicted.

    EvictionCost(): BrokenHints(0), MaxWeight(0) {}

    bool isMax() const { return BrokenHints == ~0u; }

    void setMax() { BrokenHints = ~0u; }

    void setBrokenHints(unsigned NHints) { BrokenHints = NHints; }

    bool operator<(const EvictionCost &O) const {
      return std::tie(BrokenHints, MaxWeight) <
             std::tie(O.BrokenHints, O.MaxWeight);
    }
  };

  // splitting state.
  std::unique_ptr<SplitAnalysis> SA;
  std::unique_ptr<SplitEditor> SE;

  /// Cached per-block interference maps
  InterferenceCache IntfCache;

  /// All basic blocks where the current register has uses.
  SmallVector<SpillPlacement::BlockConstraint, 8> SplitConstraints;

  /// Global live range splitting candidate info.
  struct GlobalSplitCandidate {
    // Register intended for assignment, or 0.
    unsigned PhysReg;

    // SplitKit interval index for this candidate.
    unsigned IntvIdx;

    // Interference for PhysReg.
    InterferenceCache::Cursor Intf;

    // Bundles where this candidate should be live.
    BitVector LiveBundles;
    SmallVector<unsigned, 8> ActiveBlocks;

    void reset(InterferenceCache &Cache, unsigned Reg) {
      PhysReg = Reg;
      IntvIdx = 0;
      Intf.setPhysReg(Cache, Reg);
      LiveBundles.clear();
      ActiveBlocks.clear();
    }

    // Set B[i] = C for every live bundle where B[i] was NoCand.
    unsigned getBundles(SmallVectorImpl<unsigned> &B, unsigned C) {
      unsigned Count = 0;
      for (int i = LiveBundles.find_first(); i >= 0;
           i = LiveBundles.find_next(i))
        if (B[i] == NoCand) {
          B[i] = C;
          Count++;
        }
      return Count;
    }
  };

  /// Candidate info for each PhysReg in AllocationOrder.
  /// This vector never shrinks, but grows to the size of the largest register
  /// class.
  SmallVector<GlobalSplitCandidate, 32> GlobalCand;

  enum : unsigned { NoCand = ~0u };

  /// Candidate map. Each edge bundle is assigned to a GlobalCand entry, or to
  /// NoCand which indicates the stack interval.
  SmallVector<unsigned, 32> BundleCand;

  /// Callee-save register cost, calculated once per machine function.
  BlockFrequency CSRCost;

  /// Run or not the local reassignment heuristic. This information is
  /// obtained from the TargetSubtargetInfo.
  bool EnableLocalReassign;

  /// Set of broken hints that may be reconciled later because of eviction.
  SmallSetVector<LiveInterval *, 8> SetOfBrokenHints;

public:
  RAGreedy();

  /// Return the pass name.
  const char* getPassName() const override {
    return "Greedy Register Allocator";
  }

  /// RAGreedy analysis usage.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  void releaseMemory() override;
  Spiller &spiller() override { return *SpillerInstance; }
  void enqueue(LiveInterval *LI) override;
  LiveInterval *dequeue() override;
  unsigned selectOrSplit(LiveInterval&, SmallVectorImpl<unsigned>&) override;
  void aboutToRemoveInterval(LiveInterval &) override;

  /// Perform register allocation.
  bool runOnMachineFunction(MachineFunction &mf) override;

  static char ID;

private:
  unsigned selectOrSplitImpl(LiveInterval &, SmallVectorImpl<unsigned> &,
                             SmallVirtRegSet &, unsigned = 0);

  bool LRE_CanEraseVirtReg(unsigned) override;
  void LRE_WillShrinkVirtReg(unsigned) override;
  void LRE_DidCloneVirtReg(unsigned, unsigned) override;
  void enqueue(PQueue &CurQueue, LiveInterval *LI);
  LiveInterval *dequeue(PQueue &CurQueue);

  BlockFrequency calcSpillCost();
  bool addSplitConstraints(InterferenceCache::Cursor, BlockFrequency&);
  void addThroughConstraints(InterferenceCache::Cursor, ArrayRef<unsigned>);
  void growRegion(GlobalSplitCandidate &Cand);
  BlockFrequency calcGlobalSplitCost(GlobalSplitCandidate&);
  bool calcCompactRegion(GlobalSplitCandidate&);
  void splitAroundRegion(LiveRangeEdit&, ArrayRef<unsigned>);
  void calcGapWeights(unsigned, SmallVectorImpl<float>&);
  unsigned canReassign(LiveInterval &VirtReg, unsigned PhysReg);
  bool shouldEvict(LiveInterval &A, bool, LiveInterval &B, bool);
  bool canEvictInterference(LiveInterval&, unsigned, bool, EvictionCost&);
  void evictInterference(LiveInterval&, unsigned,
                         SmallVectorImpl<unsigned>&);
  bool mayRecolorAllInterferences(unsigned PhysReg, LiveInterval &VirtReg,
                                  SmallLISet &RecoloringCandidates,
                                  const SmallVirtRegSet &FixedRegisters);

  unsigned tryAssign(LiveInterval&, AllocationOrder&,
                     SmallVectorImpl<unsigned>&);
  unsigned tryEvict(LiveInterval&, AllocationOrder&,
                    SmallVectorImpl<unsigned>&, unsigned = ~0u);
  unsigned tryRegionSplit(LiveInterval&, AllocationOrder&,
                          SmallVectorImpl<unsigned>&);
  /// Calculate cost of region splitting.
  unsigned calculateRegionSplitCost(LiveInterval &VirtReg,
                                    AllocationOrder &Order,
                                    BlockFrequency &BestCost,
                                    unsigned &NumCands, bool IgnoreCSR);
  /// Perform region splitting.
  unsigned doRegionSplit(LiveInterval &VirtReg, unsigned BestCand,
                         bool HasCompact,
                         SmallVectorImpl<unsigned> &NewVRegs);
  /// Check other options before using a callee-saved register for the first
  /// time.
  unsigned tryAssignCSRFirstTime(LiveInterval &VirtReg, AllocationOrder &Order,
                                 unsigned PhysReg, unsigned &CostPerUseLimit,
                                 SmallVectorImpl<unsigned> &NewVRegs);
  void initializeCSRCost();
  unsigned tryBlockSplit(LiveInterval&, AllocationOrder&,
                         SmallVectorImpl<unsigned>&);
  unsigned tryInstructionSplit(LiveInterval&, AllocationOrder&,
                               SmallVectorImpl<unsigned>&);
  unsigned tryLocalSplit(LiveInterval&, AllocationOrder&,
    SmallVectorImpl<unsigned>&);
  unsigned trySplit(LiveInterval&, AllocationOrder&,
                    SmallVectorImpl<unsigned>&);
  unsigned tryLastChanceRecoloring(LiveInterval &, AllocationOrder &,
                                   SmallVectorImpl<unsigned> &,
                                   SmallVirtRegSet &, unsigned);
  bool tryRecoloringCandidates(PQueue &, SmallVectorImpl<unsigned> &,
                               SmallVirtRegSet &, unsigned);
  void tryHintRecoloring(LiveInterval &);
  void tryHintsRecoloring();

  /// Model the information carried by one end of a copy.
  struct HintInfo {
    /// The frequency of the copy.
    BlockFrequency Freq;
    /// The virtual register or physical register.
    unsigned Reg;
    /// Its currently assigned register.
    /// In case of a physical register Reg == PhysReg.
    unsigned PhysReg;
    HintInfo(BlockFrequency Freq, unsigned Reg, unsigned PhysReg)
        : Freq(Freq), Reg(Reg), PhysReg(PhysReg) {}
  };
  typedef SmallVector<HintInfo, 4> HintsInfo;
  BlockFrequency getBrokenHintFreq(const HintsInfo &, unsigned);
  void collectHintInfo(unsigned, HintsInfo &);

  bool isUnusedCalleeSavedReg(unsigned PhysReg) const;
};
} // end anonymous namespace

char RAGreedy::ID = 0;

#ifndef NDEBUG
const char *const RAGreedy::StageName[] = {
    "RS_New",
    "RS_Assign",
    "RS_Split",
    "RS_Split2",
    "RS_Spill",
    "RS_Memory",
    "RS_Done"
};
#endif

// Hysteresis to use when comparing floats.
// This helps stabilize decisions based on float comparisons.
const float Hysteresis = (2007 / 2048.0f); // 0.97998046875


FunctionPass* llvm::createGreedyRegisterAllocator() {
  return new RAGreedy();
}

RAGreedy::RAGreedy(): MachineFunctionPass(ID) {
  initializeLiveDebugVariablesPass(*PassRegistry::getPassRegistry());
  initializeSlotIndexesPass(*PassRegistry::getPassRegistry());
  initializeLiveIntervalsPass(*PassRegistry::getPassRegistry());
  initializeSlotIndexesPass(*PassRegistry::getPassRegistry());
  initializeRegisterCoalescerPass(*PassRegistry::getPassRegistry());
  initializeMachineSchedulerPass(*PassRegistry::getPassRegistry());
  initializeLiveStacksPass(*PassRegistry::getPassRegistry());
  initializeMachineDominatorTreePass(*PassRegistry::getPassRegistry());
  initializeMachineLoopInfoPass(*PassRegistry::getPassRegistry());
  initializeVirtRegMapPass(*PassRegistry::getPassRegistry());
  initializeLiveRegMatrixPass(*PassRegistry::getPassRegistry());
  initializeEdgeBundlesPass(*PassRegistry::getPassRegistry());
  initializeSpillPlacementPass(*PassRegistry::getPassRegistry());
}

void RAGreedy::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
  AU.addRequired<MachineBlockFrequencyInfo>();
  AU.addPreserved<MachineBlockFrequencyInfo>();
  AU.addRequired<AAResultsWrapperPass>();
  AU.addPreserved<AAResultsWrapperPass>();
  AU.addRequired<LiveIntervals>();
  AU.addPreserved<LiveIntervals>();
  AU.addRequired<SlotIndexes>();
  AU.addPreserved<SlotIndexes>();
  AU.addRequired<LiveDebugVariables>();
  AU.addPreserved<LiveDebugVariables>();
  AU.addRequired<LiveStacks>();
  AU.addPreserved<LiveStacks>();
  AU.addRequired<MachineDominatorTree>();
  AU.addPreserved<MachineDominatorTree>();
  AU.addRequired<MachineLoopInfo>();
  AU.addPreserved<MachineLoopInfo>();
  AU.addRequired<VirtRegMap>();
  AU.addPreserved<VirtRegMap>();
  AU.addRequired<LiveRegMatrix>();
  AU.addPreserved<LiveRegMatrix>();
  AU.addRequired<EdgeBundles>();
  AU.addRequired<SpillPlacement>();
  MachineFunctionPass::getAnalysisUsage(AU);
}


//===----------------------------------------------------------------------===//
//                     LiveRangeEdit delegate methods
//===----------------------------------------------------------------------===//

bool RAGreedy::LRE_CanEraseVirtReg(unsigned VirtReg) {
  if (VRM->hasPhys(VirtReg)) {
    LiveInterval &LI = LIS->getInterval(VirtReg);
    Matrix->unassign(LI);
    aboutToRemoveInterval(LI);
    return true;
  }
  // Unassigned virtreg is probably in the priority queue.
  // RegAllocBase will erase it after dequeueing.
  return false;
}

void RAGreedy::LRE_WillShrinkVirtReg(unsigned VirtReg) {
  if (!VRM->hasPhys(VirtReg))
    return;

  // Register is assigned, put it back on the queue for reassignment.
  LiveInterval &LI = LIS->getInterval(VirtReg);
  Matrix->unassign(LI);
  enqueue(&LI);
}

void RAGreedy::LRE_DidCloneVirtReg(unsigned New, unsigned Old) {
  // Cloning a register we haven't even heard about yet?  Just ignore it.
  if (!ExtraRegInfo.inBounds(Old))
    return;

  // LRE may clone a virtual register because dead code elimination causes it to
  // be split into connected components. The new components are much smaller
  // than the original, so they should get a new chance at being assigned.
  // same stage as the parent.
  ExtraRegInfo[Old].Stage = RS_Assign;
  ExtraRegInfo.grow(New);
  ExtraRegInfo[New] = ExtraRegInfo[Old];
}

void RAGreedy::releaseMemory() {
  SpillerInstance.reset();
  ExtraRegInfo.clear();
  GlobalCand.clear();
}

void RAGreedy::enqueue(LiveInterval *LI) { enqueue(Queue, LI); }

void RAGreedy::enqueue(PQueue &CurQueue, LiveInterval *LI) {
  // Prioritize live ranges by size, assigning larger ranges first.
  // The queue holds (size, reg) pairs.
  const unsigned Size = LI->getSize();
  const unsigned Reg = LI->reg;
  assert(TargetRegisterInfo::isVirtualRegister(Reg) &&
         "Can only enqueue virtual registers");
  unsigned Prio;

  ExtraRegInfo.grow(Reg);
  if (ExtraRegInfo[Reg].Stage == RS_New)
    ExtraRegInfo[Reg].Stage = RS_Assign;

  if (ExtraRegInfo[Reg].Stage == RS_Split) {
    // Unsplit ranges that couldn't be allocated immediately are deferred until
    // everything else has been allocated.
    Prio = Size;
  } else if (ExtraRegInfo[Reg].Stage == RS_Memory) {
    // Memory operand should be considered last.
    // Change the priority such that Memory operand are assigned in
    // the reverse order that they came in.
    // TODO: Make this a member variable and probably do something about hints.
    static unsigned MemOp = 0;
    Prio = MemOp++;
  } else {
    // Giant live ranges fall back to the global assignment heuristic, which
    // prevents excessive spilling in pathological cases.
    bool ReverseLocal = TRI->reverseLocalAssignment();
    const TargetRegisterClass &RC = *MRI->getRegClass(Reg);
    bool ForceGlobal = !ReverseLocal &&
      (Size / SlotIndex::InstrDist) > (2 * RC.getNumRegs());

    if (ExtraRegInfo[Reg].Stage == RS_Assign && !ForceGlobal && !LI->empty() &&
        LIS->intervalIsInOneMBB(*LI)) {
      // Allocate original local ranges in linear instruction order. Since they
      // are singly defined, this produces optimal coloring in the absence of
      // global interference and other constraints.
      if (!ReverseLocal)
        Prio = LI->beginIndex().getInstrDistance(Indexes->getLastIndex());
      else {
        // Allocating bottom up may allow many short LRGs to be assigned first
        // to one of the cheap registers. This could be much faster for very
        // large blocks on targets with many physical registers.
        Prio = Indexes->getZeroIndex().getInstrDistance(LI->endIndex());
      }
      Prio |= RC.AllocationPriority << 24;
    } else {
      // Allocate global and split ranges in long->short order. Long ranges that
      // don't fit should be spilled (or split) ASAP so they don't create
      // interference.  Mark a bit to prioritize global above local ranges.
      Prio = (1u << 29) + Size;
    }
    // Mark a higher bit to prioritize global and local above RS_Split.
    Prio |= (1u << 31);

    // Boost ranges that have a physical register hint.
    if (VRM->hasKnownPreference(Reg))
      Prio |= (1u << 30);
  }
  // The virtual register number is a tie breaker for same-sized ranges.
  // Give lower vreg numbers higher priority to assign them first.
  CurQueue.push(std::make_pair(Prio, ~Reg));
}

LiveInterval *RAGreedy::dequeue() { return dequeue(Queue); }

LiveInterval *RAGreedy::dequeue(PQueue &CurQueue) {
  if (CurQueue.empty())
    return nullptr;
  LiveInterval *LI = &LIS->getInterval(~CurQueue.top().second);
  CurQueue.pop();
  return LI;
}


//===----------------------------------------------------------------------===//
//                            Direct Assignment
//===----------------------------------------------------------------------===//

/// tryAssign - Try to assign VirtReg to an available register.
unsigned RAGreedy::tryAssign(LiveInterval &VirtReg,
                             AllocationOrder &Order,
                             SmallVectorImpl<unsigned> &NewVRegs) {
  Order.rewind();
  unsigned PhysReg;
  while ((PhysReg = Order.next()))
    if (!Matrix->checkInterference(VirtReg, PhysReg))
      break;
  if (!PhysReg || Order.isHint())
    return PhysReg;

  // PhysReg is available, but there may be a better choice.

  // If we missed a simple hint, try to cheaply evict interference from the
  // preferred register.
  if (unsigned Hint = MRI->getSimpleHint(VirtReg.reg))
    if (Order.isHint(Hint)) {
      DEBUG(dbgs() << "missed hint " << PrintReg(Hint, TRI) << '\n');
      EvictionCost MaxCost;
      MaxCost.setBrokenHints(1);
      if (canEvictInterference(VirtReg, Hint, true, MaxCost)) {
        evictInterference(VirtReg, Hint, NewVRegs);
        return Hint;
      }
    }

  // Try to evict interference from a cheaper alternative.
  unsigned Cost = TRI->getCostPerUse(PhysReg);

  // Most registers have 0 additional cost.
  if (!Cost)
    return PhysReg;

  DEBUG(dbgs() << PrintReg(PhysReg, TRI) << " is available at cost " << Cost
               << '\n');
  unsigned CheapReg = tryEvict(VirtReg, Order, NewVRegs, Cost);
  return CheapReg ? CheapReg : PhysReg;
}


//===----------------------------------------------------------------------===//
//                         Interference eviction
//===----------------------------------------------------------------------===//

unsigned RAGreedy::canReassign(LiveInterval &VirtReg, unsigned PrevReg) {
  AllocationOrder Order(VirtReg.reg, *VRM, RegClassInfo, Matrix);
  unsigned PhysReg;
  while ((PhysReg = Order.next())) {
    if (PhysReg == PrevReg)
      continue;

    MCRegUnitIterator Units(PhysReg, TRI);
    for (; Units.isValid(); ++Units) {
      // Instantiate a "subquery", not to be confused with the Queries array.
      LiveIntervalUnion::Query subQ(&VirtReg, &Matrix->getLiveUnions()[*Units]);
      if (subQ.checkInterference())
        break;
    }
    // If no units have interference, break out with the current PhysReg.
    if (!Units.isValid())
      break;
  }
  if (PhysReg)
    DEBUG(dbgs() << "can reassign: " << VirtReg << " from "
          << PrintReg(PrevReg, TRI) << " to " << PrintReg(PhysReg, TRI)
          << '\n');
  return PhysReg;
}

/// shouldEvict - determine if A should evict the assigned live range B. The
/// eviction policy defined by this function together with the allocation order
/// defined by enqueue() decides which registers ultimately end up being split
/// and spilled.
///
/// Cascade numbers are used to prevent infinite loops if this function is a
/// cyclic relation.
///
/// @param A          The live range to be assigned.
/// @param IsHint     True when A is about to be assigned to its preferred
///                   register.
/// @param B          The live range to be evicted.
/// @param BreaksHint True when B is already assigned to its preferred register.
bool RAGreedy::shouldEvict(LiveInterval &A, bool IsHint,
                           LiveInterval &B, bool BreaksHint) {
  bool CanSplit = getStage(B) < RS_Spill;

  // Be fairly aggressive about following hints as long as the evictee can be
  // split.
  if (CanSplit && IsHint && !BreaksHint)
    return true;

  if (A.weight > B.weight) {
    DEBUG(dbgs() << "should evict: " << B << " w= " << B.weight << '\n');
    return true;
  }
  return false;
}

/// canEvictInterference - Return true if all interferences between VirtReg and
/// PhysReg can be evicted.
///
/// @param VirtReg Live range that is about to be assigned.
/// @param PhysReg Desired register for assignment.
/// @param IsHint  True when PhysReg is VirtReg's preferred register.
/// @param MaxCost Only look for cheaper candidates and update with new cost
///                when returning true.
/// @returns True when interference can be evicted cheaper than MaxCost.
bool RAGreedy::canEvictInterference(LiveInterval &VirtReg, unsigned PhysReg,
                                    bool IsHint, EvictionCost &MaxCost) {
  // It is only possible to evict virtual register interference.
  if (Matrix->checkInterference(VirtReg, PhysReg) > LiveRegMatrix::IK_VirtReg)
    return false;

  bool IsLocal = LIS->intervalIsInOneMBB(VirtReg);

  // Find VirtReg's cascade number. This will be unassigned if VirtReg was never
  // involved in an eviction before. If a cascade number was assigned, deny
  // evicting anything with the same or a newer cascade number. This prevents
  // infinite eviction loops.
  //
  // This works out so a register without a cascade number is allowed to evict
  // anything, and it can be evicted by anything.
  unsigned Cascade = ExtraRegInfo[VirtReg.reg].Cascade;
  if (!Cascade)
    Cascade = NextCascade;

  EvictionCost Cost;
  for (MCRegUnitIterator Units(PhysReg, TRI); Units.isValid(); ++Units) {
    LiveIntervalUnion::Query &Q = Matrix->query(VirtReg, *Units);
    // If there is 10 or more interferences, chances are one is heavier.
    if (Q.collectInterferingVRegs(10) >= 10)
      return false;

    // Check if any interfering live range is heavier than MaxWeight.
    for (unsigned i = Q.interferingVRegs().size(); i; --i) {
      LiveInterval *Intf = Q.interferingVRegs()[i - 1];
      assert(TargetRegisterInfo::isVirtualRegister(Intf->reg) &&
             "Only expecting virtual register interference from query");
      // Never evict spill products. They cannot split or spill.
      if (getStage(*Intf) == RS_Done)
        return false;
      // Once a live range becomes small enough, it is urgent that we find a
      // register for it. This is indicated by an infinite spill weight. These
      // urgent live ranges get to evict almost anything.
      //
      // Also allow urgent evictions of unspillable ranges from a strictly
      // larger allocation order.
      bool Urgent = !VirtReg.isSpillable() &&
        (Intf->isSpillable() ||
         RegClassInfo.getNumAllocatableRegs(MRI->getRegClass(VirtReg.reg)) <
         RegClassInfo.getNumAllocatableRegs(MRI->getRegClass(Intf->reg)));
      // Only evict older cascades or live ranges without a cascade.
      unsigned IntfCascade = ExtraRegInfo[Intf->reg].Cascade;
      if (Cascade <= IntfCascade) {
        if (!Urgent)
          return false;
        // We permit breaking cascades for urgent evictions. It should be the
        // last resort, though, so make it really expensive.
        Cost.BrokenHints += 10;
      }
      // Would this break a satisfied hint?
      bool BreaksHint = VRM->hasPreferredPhys(Intf->reg);
      // Update eviction cost.
      Cost.BrokenHints += BreaksHint;
      Cost.MaxWeight = std::max(Cost.MaxWeight, Intf->weight);
      // Abort if this would be too expensive.
      if (!(Cost < MaxCost))
        return false;
      if (Urgent)
        continue;
      // Apply the eviction policy for non-urgent evictions.
      if (!shouldEvict(VirtReg, IsHint, *Intf, BreaksHint))
        return false;
      // If !MaxCost.isMax(), then we're just looking for a cheap register.
      // Evicting another local live range in this case could lead to suboptimal
      // coloring.
      if (!MaxCost.isMax() && IsLocal && LIS->intervalIsInOneMBB(*Intf) &&
          (!EnableLocalReassign || !canReassign(*Intf, PhysReg))) {
        return false;
      }
    }
  }
  MaxCost = Cost;
  return true;
}

/// evictInterference - Evict any interferring registers that prevent VirtReg
/// from being assigned to Physreg. This assumes that canEvictInterference
/// returned true.
void RAGreedy::evictInterference(LiveInterval &VirtReg, unsigned PhysReg,
                                 SmallVectorImpl<unsigned> &NewVRegs) {
  // Make sure that VirtReg has a cascade number, and assign that cascade
  // number to every evicted register. These live ranges than then only be
  // evicted by a newer cascade, preventing infinite loops.
  unsigned Cascade = ExtraRegInfo[VirtReg.reg].Cascade;
  if (!Cascade)
    Cascade = ExtraRegInfo[VirtReg.reg].Cascade = NextCascade++;

  DEBUG(dbgs() << "evicting " << PrintReg(PhysReg, TRI)
               << " interference: Cascade " << Cascade << '\n');

  // Collect all interfering virtregs first.
  SmallVector<LiveInterval*, 8> Intfs;
  for (MCRegUnitIterator Units(PhysReg, TRI); Units.isValid(); ++Units) {
    LiveIntervalUnion::Query &Q = Matrix->query(VirtReg, *Units);
    assert(Q.seenAllInterferences() && "Didn't check all interfererences.");
    ArrayRef<LiveInterval*> IVR = Q.interferingVRegs();
    Intfs.append(IVR.begin(), IVR.end());
  }

  // Evict them second. This will invalidate the queries.
  for (unsigned i = 0, e = Intfs.size(); i != e; ++i) {
    LiveInterval *Intf = Intfs[i];
    // The same VirtReg may be present in multiple RegUnits. Skip duplicates.
    if (!VRM->hasPhys(Intf->reg))
      continue;
    Matrix->unassign(*Intf);
    assert((ExtraRegInfo[Intf->reg].Cascade < Cascade ||
            VirtReg.isSpillable() < Intf->isSpillable()) &&
           "Cannot decrease cascade number, illegal eviction");
    ExtraRegInfo[Intf->reg].Cascade = Cascade;
    ++NumEvicted;
    NewVRegs.push_back(Intf->reg);
  }
}

/// Returns true if the given \p PhysReg is a callee saved register and has not
/// been used for allocation yet.
bool RAGreedy::isUnusedCalleeSavedReg(unsigned PhysReg) const {
  unsigned CSR = RegClassInfo.getLastCalleeSavedAlias(PhysReg);
  if (CSR == 0)
    return false;

  return !Matrix->isPhysRegUsed(PhysReg);
}

/// tryEvict - Try to evict all interferences for a physreg.
/// @param  VirtReg Currently unassigned virtual register.
/// @param  Order   Physregs to try.
/// @return         Physreg to assign VirtReg, or 0.
unsigned RAGreedy::tryEvict(LiveInterval &VirtReg,
                            AllocationOrder &Order,
                            SmallVectorImpl<unsigned> &NewVRegs,
                            unsigned CostPerUseLimit) {
  NamedRegionTimer T("Evict", TimerGroupName, TimePassesIsEnabled);

  // Keep track of the cheapest interference seen so far.
  EvictionCost BestCost;
  BestCost.setMax();
  unsigned BestPhys = 0;
  unsigned OrderLimit = Order.getOrder().size();

  // When we are just looking for a reduced cost per use, don't break any
  // hints, and only evict smaller spill weights.
  if (CostPerUseLimit < ~0u) {
    BestCost.BrokenHints = 0;
    BestCost.MaxWeight = VirtReg.weight;

    // Check of any registers in RC are below CostPerUseLimit.
    const TargetRegisterClass *RC = MRI->getRegClass(VirtReg.reg);
    unsigned MinCost = RegClassInfo.getMinCost(RC);
    if (MinCost >= CostPerUseLimit) {
      DEBUG(dbgs() << TRI->getRegClassName(RC) << " minimum cost = " << MinCost
                   << ", no cheaper registers to be found.\n");
      return 0;
    }

    // It is normal for register classes to have a long tail of registers with
    // the same cost. We don't need to look at them if they're too expensive.
    if (TRI->getCostPerUse(Order.getOrder().back()) >= CostPerUseLimit) {
      OrderLimit = RegClassInfo.getLastCostChange(RC);
      DEBUG(dbgs() << "Only trying the first " << OrderLimit << " regs.\n");
    }
  }

  Order.rewind();
  while (unsigned PhysReg = Order.next(OrderLimit)) {
    if (TRI->getCostPerUse(PhysReg) >= CostPerUseLimit)
      continue;
    // The first use of a callee-saved register in a function has cost 1.
    // Don't start using a CSR when the CostPerUseLimit is low.
    if (CostPerUseLimit == 1 && isUnusedCalleeSavedReg(PhysReg)) {
      DEBUG(dbgs() << PrintReg(PhysReg, TRI) << " would clobber CSR "
            << PrintReg(RegClassInfo.getLastCalleeSavedAlias(PhysReg), TRI)
            << '\n');
      continue;
    }

    if (!canEvictInterference(VirtReg, PhysReg, false, BestCost))
      continue;

    // Best so far.
    BestPhys = PhysReg;

    // Stop if the hint can be used.
    if (Order.isHint())
      break;
  }

  if (!BestPhys)
    return 0;

  evictInterference(VirtReg, BestPhys, NewVRegs);
  return BestPhys;
}


//===----------------------------------------------------------------------===//
//                              Region Splitting
//===----------------------------------------------------------------------===//

/// addSplitConstraints - Fill out the SplitConstraints vector based on the
/// interference pattern in Physreg and its aliases. Add the constraints to
/// SpillPlacement and return the static cost of this split in Cost, assuming
/// that all preferences in SplitConstraints are met.
/// Return false if there are no bundles with positive bias.
bool RAGreedy::addSplitConstraints(InterferenceCache::Cursor Intf,
                                   BlockFrequency &Cost) {
  ArrayRef<SplitAnalysis::BlockInfo> UseBlocks = SA->getUseBlocks();

  // Reset interference dependent info.
  SplitConstraints.resize(UseBlocks.size());
  BlockFrequency StaticCost = 0;
  for (unsigned i = 0; i != UseBlocks.size(); ++i) {
    const SplitAnalysis::BlockInfo &BI = UseBlocks[i];
    SpillPlacement::BlockConstraint &BC = SplitConstraints[i];

    BC.Number = BI.MBB->getNumber();
    Intf.moveToBlock(BC.Number);
    BC.Entry = BI.LiveIn ? SpillPlacement::PrefReg : SpillPlacement::DontCare;
    BC.Exit = BI.LiveOut ? SpillPlacement::PrefReg : SpillPlacement::DontCare;
    BC.ChangesValue = BI.FirstDef.isValid();

    if (!Intf.hasInterference())
      continue;

    // Number of spill code instructions to insert.
    unsigned Ins = 0;

    // Interference for the live-in value.
    if (BI.LiveIn) {
      if (Intf.first() <= Indexes->getMBBStartIdx(BC.Number))
        BC.Entry = SpillPlacement::MustSpill, ++Ins;
      else if (Intf.first() < BI.FirstInstr)
        BC.Entry = SpillPlacement::PrefSpill, ++Ins;
      else if (Intf.first() < BI.LastInstr)
        ++Ins;
    }

    // Interference for the live-out value.
    if (BI.LiveOut) {
      if (Intf.last() >= SA->getLastSplitPoint(BC.Number))
        BC.Exit = SpillPlacement::MustSpill, ++Ins;
      else if (Intf.last() > BI.LastInstr)
        BC.Exit = SpillPlacement::PrefSpill, ++Ins;
      else if (Intf.last() > BI.FirstInstr)
        ++Ins;
    }

    // Accumulate the total frequency of inserted spill code.
    while (Ins--)
      StaticCost += SpillPlacer->getBlockFrequency(BC.Number);
  }
  Cost = StaticCost;

  // Add constraints for use-blocks. Note that these are the only constraints
  // that may add a positive bias, it is downhill from here.
  SpillPlacer->addConstraints(SplitConstraints);
  return SpillPlacer->scanActiveBundles();
}


/// addThroughConstraints - Add constraints and links to SpillPlacer from the
/// live-through blocks in Blocks.
void RAGreedy::addThroughConstraints(InterferenceCache::Cursor Intf,
                                     ArrayRef<unsigned> Blocks) {
  const unsigned GroupSize = 8;
  SpillPlacement::BlockConstraint BCS[GroupSize];
  unsigned TBS[GroupSize];
  unsigned B = 0, T = 0;

  for (unsigned i = 0; i != Blocks.size(); ++i) {
    unsigned Number = Blocks[i];
    Intf.moveToBlock(Number);

    if (!Intf.hasInterference()) {
      assert(T < GroupSize && "Array overflow");
      TBS[T] = Number;
      if (++T == GroupSize) {
        SpillPlacer->addLinks(makeArrayRef(TBS, T));
        T = 0;
      }
      continue;
    }

    assert(B < GroupSize && "Array overflow");
    BCS[B].Number = Number;

    // Interference for the live-in value.
    if (Intf.first() <= Indexes->getMBBStartIdx(Number))
      BCS[B].Entry = SpillPlacement::MustSpill;
    else
      BCS[B].Entry = SpillPlacement::PrefSpill;

    // Interference for the live-out value.
    if (Intf.last() >= SA->getLastSplitPoint(Number))
      BCS[B].Exit = SpillPlacement::MustSpill;
    else
      BCS[B].Exit = SpillPlacement::PrefSpill;

    if (++B == GroupSize) {
      SpillPlacer->addConstraints(makeArrayRef(BCS, B));
      B = 0;
    }
  }

  SpillPlacer->addConstraints(makeArrayRef(BCS, B));
  SpillPlacer->addLinks(makeArrayRef(TBS, T));
}

void RAGreedy::growRegion(GlobalSplitCandidate &Cand) {
  // Keep track of through blocks that have not been added to SpillPlacer.
  BitVector Todo = SA->getThroughBlocks();
  SmallVectorImpl<unsigned> &ActiveBlocks = Cand.ActiveBlocks;
  unsigned AddedTo = 0;
#ifndef NDEBUG
  unsigned Visited = 0;
#endif

  for (;;) {
    ArrayRef<unsigned> NewBundles = SpillPlacer->getRecentPositive();
    // Find new through blocks in the periphery of PrefRegBundles.
    for (int i = 0, e = NewBundles.size(); i != e; ++i) {
      unsigned Bundle = NewBundles[i];
      // Look at all blocks connected to Bundle in the full graph.
      ArrayRef<unsigned> Blocks = Bundles->getBlocks(Bundle);
      for (ArrayRef<unsigned>::iterator I = Blocks.begin(), E = Blocks.end();
           I != E; ++I) {
        unsigned Block = *I;
        if (!Todo.test(Block))
          continue;
        Todo.reset(Block);
        // This is a new through block. Add it to SpillPlacer later.
        ActiveBlocks.push_back(Block);
#ifndef NDEBUG
        ++Visited;
#endif
      }
    }
    // Any new blocks to add?
    if (ActiveBlocks.size() == AddedTo)
      break;

    // Compute through constraints from the interference, or assume that all
    // through blocks prefer spilling when forming compact regions.
    auto NewBlocks = makeArrayRef(ActiveBlocks).slice(AddedTo);
    if (Cand.PhysReg)
      addThroughConstraints(Cand.Intf, NewBlocks);
    else
      // Provide a strong negative bias on through blocks to prevent unwanted
      // liveness on loop backedges.
      SpillPlacer->addPrefSpill(NewBlocks, /* Strong= */ true);
    AddedTo = ActiveBlocks.size();

    // Perhaps iterating can enable more bundles?
    SpillPlacer->iterate();
  }
  DEBUG(dbgs() << ", v=" << Visited);
}

/// calcCompactRegion - Compute the set of edge bundles that should be live
/// when splitting the current live range into compact regions.  Compact
/// regions can be computed without looking at interference.  They are the
/// regions formed by removing all the live-through blocks from the live range.
///
/// Returns false if the current live range is already compact, or if the
/// compact regions would form single block regions anyway.
bool RAGreedy::calcCompactRegion(GlobalSplitCandidate &Cand) {
  // Without any through blocks, the live range is already compact.
  if (!SA->getNumThroughBlocks())
    return false;

  // Compact regions don't correspond to any physreg.
  Cand.reset(IntfCache, 0);

  DEBUG(dbgs() << "Compact region bundles");

  // Use the spill placer to determine the live bundles. GrowRegion pretends
  // that all the through blocks have interference when PhysReg is unset.
  SpillPlacer->prepare(Cand.LiveBundles);

  // The static split cost will be zero since Cand.Intf reports no interference.
  BlockFrequency Cost;
  if (!addSplitConstraints(Cand.Intf, Cost)) {
    DEBUG(dbgs() << ", none.\n");
    return false;
  }

  growRegion(Cand);
  SpillPlacer->finish();

  if (!Cand.LiveBundles.any()) {
    DEBUG(dbgs() << ", none.\n");
    return false;
  }

  DEBUG({
    for (int i = Cand.LiveBundles.find_first(); i>=0;
         i = Cand.LiveBundles.find_next(i))
    dbgs() << " EB#" << i;
    dbgs() << ".\n";
  });
  return true;
}

/// calcSpillCost - Compute how expensive it would be to split the live range in
/// SA around all use blocks instead of forming bundle regions.
BlockFrequency RAGreedy::calcSpillCost() {
  BlockFrequency Cost = 0;
  ArrayRef<SplitAnalysis::BlockInfo> UseBlocks = SA->getUseBlocks();
  for (unsigned i = 0; i != UseBlocks.size(); ++i) {
    const SplitAnalysis::BlockInfo &BI = UseBlocks[i];
    unsigned Number = BI.MBB->getNumber();
    // We normally only need one spill instruction - a load or a store.
    Cost += SpillPlacer->getBlockFrequency(Number);

    // Unless the value is redefined in the block.
    if (BI.LiveIn && BI.LiveOut && BI.FirstDef)
      Cost += SpillPlacer->getBlockFrequency(Number);
  }
  return Cost;
}

/// calcGlobalSplitCost - Return the global split cost of following the split
/// pattern in LiveBundles. This cost should be added to the local cost of the
/// interference pattern in SplitConstraints.
///
BlockFrequency RAGreedy::calcGlobalSplitCost(GlobalSplitCandidate &Cand) {
  BlockFrequency GlobalCost = 0;
  const BitVector &LiveBundles = Cand.LiveBundles;
  ArrayRef<SplitAnalysis::BlockInfo> UseBlocks = SA->getUseBlocks();
  for (unsigned i = 0; i != UseBlocks.size(); ++i) {
    const SplitAnalysis::BlockInfo &BI = UseBlocks[i];
    SpillPlacement::BlockConstraint &BC = SplitConstraints[i];
    bool RegIn  = LiveBundles[Bundles->getBundle(BC.Number, 0)];
    bool RegOut = LiveBundles[Bundles->getBundle(BC.Number, 1)];
    unsigned Ins = 0;

    if (BI.LiveIn)
      Ins += RegIn != (BC.Entry == SpillPlacement::PrefReg);
    if (BI.LiveOut)
      Ins += RegOut != (BC.Exit == SpillPlacement::PrefReg);
    while (Ins--)
      GlobalCost += SpillPlacer->getBlockFrequency(BC.Number);
  }

  for (unsigned i = 0, e = Cand.ActiveBlocks.size(); i != e; ++i) {
    unsigned Number = Cand.ActiveBlocks[i];
    bool RegIn  = LiveBundles[Bundles->getBundle(Number, 0)];
    bool RegOut = LiveBundles[Bundles->getBundle(Number, 1)];
    if (!RegIn && !RegOut)
      continue;
    if (RegIn && RegOut) {
      // We need double spill code if this block has interference.
      Cand.Intf.moveToBlock(Number);
      if (Cand.Intf.hasInterference()) {
        GlobalCost += SpillPlacer->getBlockFrequency(Number);
        GlobalCost += SpillPlacer->getBlockFrequency(Number);
      }
      continue;
    }
    // live-in / stack-out or stack-in live-out.
    GlobalCost += SpillPlacer->getBlockFrequency(Number);
  }
  return GlobalCost;
}

/// splitAroundRegion - Split the current live range around the regions
/// determined by BundleCand and GlobalCand.
///
/// Before calling this function, GlobalCand and BundleCand must be initialized
/// so each bundle is assigned to a valid candidate, or NoCand for the
/// stack-bound bundles.  The shared SA/SE SplitAnalysis and SplitEditor
/// objects must be initialized for the current live range, and intervals
/// created for the used candidates.
///
/// @param LREdit    The LiveRangeEdit object handling the current split.
/// @param UsedCands List of used GlobalCand entries. Every BundleCand value
///                  must appear in this list.
void RAGreedy::splitAroundRegion(LiveRangeEdit &LREdit,
                                 ArrayRef<unsigned> UsedCands) {
  // These are the intervals created for new global ranges. We may create more
  // intervals for local ranges.
  const unsigned NumGlobalIntvs = LREdit.size();
  DEBUG(dbgs() << "splitAroundRegion with " << NumGlobalIntvs << " globals.\n");
  assert(NumGlobalIntvs && "No global intervals configured");

  // Isolate even single instructions when dealing with a proper sub-class.
  // That guarantees register class inflation for the stack interval because it
  // is all copies.
  unsigned Reg = SA->getParent().reg;
  bool SingleInstrs = RegClassInfo.isProperSubClass(MRI->getRegClass(Reg));

  // First handle all the blocks with uses.
  ArrayRef<SplitAnalysis::BlockInfo> UseBlocks = SA->getUseBlocks();
  for (unsigned i = 0; i != UseBlocks.size(); ++i) {
    const SplitAnalysis::BlockInfo &BI = UseBlocks[i];
    unsigned Number = BI.MBB->getNumber();
    unsigned IntvIn = 0, IntvOut = 0;
    SlotIndex IntfIn, IntfOut;
    if (BI.LiveIn) {
      unsigned CandIn = BundleCand[Bundles->getBundle(Number, 0)];
      if (CandIn != NoCand) {
        GlobalSplitCandidate &Cand = GlobalCand[CandIn];
        IntvIn = Cand.IntvIdx;
        Cand.Intf.moveToBlock(Number);
        IntfIn = Cand.Intf.first();
      }
    }
    if (BI.LiveOut) {
      unsigned CandOut = BundleCand[Bundles->getBundle(Number, 1)];
      if (CandOut != NoCand) {
        GlobalSplitCandidate &Cand = GlobalCand[CandOut];
        IntvOut = Cand.IntvIdx;
        Cand.Intf.moveToBlock(Number);
        IntfOut = Cand.Intf.last();
      }
    }

    // Create separate intervals for isolated blocks with multiple uses.
    if (!IntvIn && !IntvOut) {
      DEBUG(dbgs() << "BB#" << BI.MBB->getNumber() << " isolated.\n");
      if (SA->shouldSplitSingleBlock(BI, SingleInstrs))
        SE->splitSingleBlock(BI);
      continue;
    }

    if (IntvIn && IntvOut)
      SE->splitLiveThroughBlock(Number, IntvIn, IntfIn, IntvOut, IntfOut);
    else if (IntvIn)
      SE->splitRegInBlock(BI, IntvIn, IntfIn);
    else
      SE->splitRegOutBlock(BI, IntvOut, IntfOut);
  }

  // Handle live-through blocks. The relevant live-through blocks are stored in
  // the ActiveBlocks list with each candidate. We need to filter out
  // duplicates.
  BitVector Todo = SA->getThroughBlocks();
  for (unsigned c = 0; c != UsedCands.size(); ++c) {
    ArrayRef<unsigned> Blocks = GlobalCand[UsedCands[c]].ActiveBlocks;
    for (unsigned i = 0, e = Blocks.size(); i != e; ++i) {
      unsigned Number = Blocks[i];
      if (!Todo.test(Number))
        continue;
      Todo.reset(Number);

      unsigned IntvIn = 0, IntvOut = 0;
      SlotIndex IntfIn, IntfOut;

      unsigned CandIn = BundleCand[Bundles->getBundle(Number, 0)];
      if (CandIn != NoCand) {
        GlobalSplitCandidate &Cand = GlobalCand[CandIn];
        IntvIn = Cand.IntvIdx;
        Cand.Intf.moveToBlock(Number);
        IntfIn = Cand.Intf.first();
      }

      unsigned CandOut = BundleCand[Bundles->getBundle(Number, 1)];
      if (CandOut != NoCand) {
        GlobalSplitCandidate &Cand = GlobalCand[CandOut];
        IntvOut = Cand.IntvIdx;
        Cand.Intf.moveToBlock(Number);
        IntfOut = Cand.Intf.last();
      }
      if (!IntvIn && !IntvOut)
        continue;
      SE->splitLiveThroughBlock(Number, IntvIn, IntfIn, IntvOut, IntfOut);
    }
  }

  ++NumGlobalSplits;

  SmallVector<unsigned, 8> IntvMap;
  SE->finish(&IntvMap);
  DebugVars->splitRegister(Reg, LREdit.regs(), *LIS);

  ExtraRegInfo.resize(MRI->getNumVirtRegs());
  unsigned OrigBlocks = SA->getNumLiveBlocks();

  // Sort out the new intervals created by splitting. We get four kinds:
  // - Remainder intervals should not be split again.
  // - Candidate intervals can be assigned to Cand.PhysReg.
  // - Block-local splits are candidates for local splitting.
  // - DCE leftovers should go back on the queue.
  for (unsigned i = 0, e = LREdit.size(); i != e; ++i) {
    LiveInterval &Reg = LIS->getInterval(LREdit.get(i));

    // Ignore old intervals from DCE.
    if (getStage(Reg) != RS_New)
      continue;

    // Remainder interval. Don't try splitting again, spill if it doesn't
    // allocate.
    if (IntvMap[i] == 0) {
      setStage(Reg, RS_Spill);
      continue;
    }

    // Global intervals. Allow repeated splitting as long as the number of live
    // blocks is strictly decreasing.
    if (IntvMap[i] < NumGlobalIntvs) {
      if (SA->countLiveBlocks(&Reg) >= OrigBlocks) {
        DEBUG(dbgs() << "Main interval covers the same " << OrigBlocks
                     << " blocks as original.\n");
        // Don't allow repeated splitting as a safe guard against looping.
        setStage(Reg, RS_Split2);
      }
      continue;
    }

    // Other intervals are treated as new. This includes local intervals created
    // for blocks with multiple uses, and anything created by DCE.
  }

  if (VerifyEnabled)
    MF->verify(this, "After splitting live range around region");
}

unsigned RAGreedy::tryRegionSplit(LiveInterval &VirtReg, AllocationOrder &Order,
                                  SmallVectorImpl<unsigned> &NewVRegs) {
  unsigned NumCands = 0;
  BlockFrequency BestCost;

  // Check if we can split this live range around a compact region.
  bool HasCompact = calcCompactRegion(GlobalCand.front());
  if (HasCompact) {
    // Yes, keep GlobalCand[0] as the compact region candidate.
    NumCands = 1;
    BestCost = BlockFrequency::getMaxFrequency();
  } else {
    // No benefit from the compact region, our fallback will be per-block
    // splitting. Make sure we find a solution that is cheaper than spilling.
    BestCost = calcSpillCost();
    DEBUG(dbgs() << "Cost of isolating all blocks = ";
                 MBFI->printBlockFreq(dbgs(), BestCost) << '\n');
  }

  unsigned BestCand =
      calculateRegionSplitCost(VirtReg, Order, BestCost, NumCands,
                               false/*IgnoreCSR*/);

  // No solutions found, fall back to single block splitting.
  if (!HasCompact && BestCand == NoCand)
    return 0;

  return doRegionSplit(VirtReg, BestCand, HasCompact, NewVRegs);
}

unsigned RAGreedy::calculateRegionSplitCost(LiveInterval &VirtReg,
                                            AllocationOrder &Order,
                                            BlockFrequency &BestCost,
                                            unsigned &NumCands,
                                            bool IgnoreCSR) {
  unsigned BestCand = NoCand;
  Order.rewind();
  while (unsigned PhysReg = Order.next()) {
    if (IgnoreCSR && isUnusedCalleeSavedReg(PhysReg))
      continue;

    // Discard bad candidates before we run out of interference cache cursors.
    // This will only affect register classes with a lot of registers (>32).
    if (NumCands == IntfCache.getMaxCursors()) {
      unsigned WorstCount = ~0u;
      unsigned Worst = 0;
      for (unsigned i = 0; i != NumCands; ++i) {
        if (i == BestCand || !GlobalCand[i].PhysReg)
          continue;
        unsigned Count = GlobalCand[i].LiveBundles.count();
        if (Count < WorstCount)
          Worst = i, WorstCount = Count;
      }
      --NumCands;
      GlobalCand[Worst] = GlobalCand[NumCands];
      if (BestCand == NumCands)
        BestCand = Worst;
    }

    if (GlobalCand.size() <= NumCands)
      GlobalCand.resize(NumCands+1);
    GlobalSplitCandidate &Cand = GlobalCand[NumCands];
    Cand.reset(IntfCache, PhysReg);

    SpillPlacer->prepare(Cand.LiveBundles);
    BlockFrequency Cost;
    if (!addSplitConstraints(Cand.Intf, Cost)) {
      DEBUG(dbgs() << PrintReg(PhysReg, TRI) << "\tno positive bundles\n");
      continue;
    }
    DEBUG(dbgs() << PrintReg(PhysReg, TRI) << "\tstatic = ";
                 MBFI->printBlockFreq(dbgs(), Cost));
    if (Cost >= BestCost) {
      DEBUG({
        if (BestCand == NoCand)
          dbgs() << " worse than no bundles\n";
        else
          dbgs() << " worse than "
                 << PrintReg(GlobalCand[BestCand].PhysReg, TRI) << '\n';
      });
      continue;
    }
    growRegion(Cand);

    SpillPlacer->finish();

    // No live bundles, defer to splitSingleBlocks().
    if (!Cand.LiveBundles.any()) {
      DEBUG(dbgs() << " no bundles.\n");
      continue;
    }

    Cost += calcGlobalSplitCost(Cand);
    DEBUG({
      dbgs() << ", total = "; MBFI->printBlockFreq(dbgs(), Cost)
                                << " with bundles";
      for (int i = Cand.LiveBundles.find_first(); i>=0;
           i = Cand.LiveBundles.find_next(i))
        dbgs() << " EB#" << i;
      dbgs() << ".\n";
    });
    if (Cost < BestCost) {
      BestCand = NumCands;
      BestCost = Cost;
    }
    ++NumCands;
  }
  return BestCand;
}

unsigned RAGreedy::doRegionSplit(LiveInterval &VirtReg, unsigned BestCand,
                                 bool HasCompact,
                                 SmallVectorImpl<unsigned> &NewVRegs) {
  SmallVector<unsigned, 8> UsedCands;
  // Prepare split editor.
  LiveRangeEdit LREdit(&VirtReg, NewVRegs, *MF, *LIS, VRM, this);
  SE->reset(LREdit, SplitSpillMode);

  // Assign all edge bundles to the preferred candidate, or NoCand.
  BundleCand.assign(Bundles->getNumBundles(), NoCand);

  // Assign bundles for the best candidate region.
  if (BestCand != NoCand) {
    GlobalSplitCandidate &Cand = GlobalCand[BestCand];
    if (unsigned B = Cand.getBundles(BundleCand, BestCand)) {
      UsedCands.push_back(BestCand);
      Cand.IntvIdx = SE->openIntv();
      DEBUG(dbgs() << "Split for " << PrintReg(Cand.PhysReg, TRI) << " in "
                   << B << " bundles, intv " << Cand.IntvIdx << ".\n");
      (void)B;
    }
  }

  // Assign bundles for the compact region.
  if (HasCompact) {
    GlobalSplitCandidate &Cand = GlobalCand.front();
    assert(!Cand.PhysReg && "Compact region has no physreg");
    if (unsigned B = Cand.getBundles(BundleCand, 0)) {
      UsedCands.push_back(0);
      Cand.IntvIdx = SE->openIntv();
      DEBUG(dbgs() << "Split for compact region in " << B << " bundles, intv "
                   << Cand.IntvIdx << ".\n");
      (void)B;
    }
  }

  splitAroundRegion(LREdit, UsedCands);
  return 0;
}


//===----------------------------------------------------------------------===//
//                            Per-Block Splitting
//===----------------------------------------------------------------------===//

/// tryBlockSplit - Split a global live range around every block with uses. This
/// creates a lot of local live ranges, that will be split by tryLocalSplit if
/// they don't allocate.
unsigned RAGreedy::tryBlockSplit(LiveInterval &VirtReg, AllocationOrder &Order,
                                 SmallVectorImpl<unsigned> &NewVRegs) {
  assert(&SA->getParent() == &VirtReg && "Live range wasn't analyzed");
  unsigned Reg = VirtReg.reg;
  bool SingleInstrs = RegClassInfo.isProperSubClass(MRI->getRegClass(Reg));
  LiveRangeEdit LREdit(&VirtReg, NewVRegs, *MF, *LIS, VRM, this);
  SE->reset(LREdit, SplitSpillMode);
  ArrayRef<SplitAnalysis::BlockInfo> UseBlocks = SA->getUseBlocks();
  for (unsigned i = 0; i != UseBlocks.size(); ++i) {
    const SplitAnalysis::BlockInfo &BI = UseBlocks[i];
    if (SA->shouldSplitSingleBlock(BI, SingleInstrs))
      SE->splitSingleBlock(BI);
  }
  // No blocks were split.
  if (LREdit.empty())
    return 0;

  // We did split for some blocks.
  SmallVector<unsigned, 8> IntvMap;
  SE->finish(&IntvMap);

  // Tell LiveDebugVariables about the new ranges.
  DebugVars->splitRegister(Reg, LREdit.regs(), *LIS);

  ExtraRegInfo.resize(MRI->getNumVirtRegs());

  // Sort out the new intervals created by splitting. The remainder interval
  // goes straight to spilling, the new local ranges get to stay RS_New.
  for (unsigned i = 0, e = LREdit.size(); i != e; ++i) {
    LiveInterval &LI = LIS->getInterval(LREdit.get(i));
    if (getStage(LI) == RS_New && IntvMap[i] == 0)
      setStage(LI, RS_Spill);
  }

  if (VerifyEnabled)
    MF->verify(this, "After splitting live range around basic blocks");
  return 0;
}


//===----------------------------------------------------------------------===//
//                         Per-Instruction Splitting
//===----------------------------------------------------------------------===//

/// Get the number of allocatable registers that match the constraints of \p Reg
/// on \p MI and that are also in \p SuperRC.
static unsigned getNumAllocatableRegsForConstraints(
    const MachineInstr *MI, unsigned Reg, const TargetRegisterClass *SuperRC,
    const TargetInstrInfo *TII, const TargetRegisterInfo *TRI,
    const RegisterClassInfo &RCI) {
  assert(SuperRC && "Invalid register class");

  const TargetRegisterClass *ConstrainedRC =
      MI->getRegClassConstraintEffectForVReg(Reg, SuperRC, TII, TRI,
                                             /* ExploreBundle */ true);
  if (!ConstrainedRC)
    return 0;
  return RCI.getNumAllocatableRegs(ConstrainedRC);
}

/// tryInstructionSplit - Split a live range around individual instructions.
/// This is normally not worthwhile since the spiller is doing essentially the
/// same thing. However, when the live range is in a constrained register
/// class, it may help to insert copies such that parts of the live range can
/// be moved to a larger register class.
///
/// This is similar to spilling to a larger register class.
unsigned
RAGreedy::tryInstructionSplit(LiveInterval &VirtReg, AllocationOrder &Order,
                              SmallVectorImpl<unsigned> &NewVRegs) {
  const TargetRegisterClass *CurRC = MRI->getRegClass(VirtReg.reg);
  // There is no point to this if there are no larger sub-classes.
  if (!RegClassInfo.isProperSubClass(CurRC))
    return 0;

  // Always enable split spill mode, since we're effectively spilling to a
  // register.
  LiveRangeEdit LREdit(&VirtReg, NewVRegs, *MF, *LIS, VRM, this);
  SE->reset(LREdit, SplitEditor::SM_Size);

  ArrayRef<SlotIndex> Uses = SA->getUseSlots();
  if (Uses.size() <= 1)
    return 0;

  DEBUG(dbgs() << "Split around " << Uses.size() << " individual instrs.\n");

  const TargetRegisterClass *SuperRC =
      TRI->getLargestLegalSuperClass(CurRC, *MF);
  unsigned SuperRCNumAllocatableRegs = RCI.getNumAllocatableRegs(SuperRC);
  // Split around every non-copy instruction if this split will relax
  // the constraints on the virtual register.
  // Otherwise, splitting just inserts uncoalescable copies that do not help
  // the allocation.
  for (unsigned i = 0; i != Uses.size(); ++i) {
    if (const MachineInstr *MI = Indexes->getInstructionFromIndex(Uses[i]))
      if (MI->isFullCopy() ||
          SuperRCNumAllocatableRegs ==
              getNumAllocatableRegsForConstraints(MI, VirtReg.reg, SuperRC, TII,
                                                  TRI, RCI)) {
        DEBUG(dbgs() << "    skip:\t" << Uses[i] << '\t' << *MI);
        continue;
      }
    SE->openIntv();
    SlotIndex SegStart = SE->enterIntvBefore(Uses[i]);
    SlotIndex SegStop  = SE->leaveIntvAfter(Uses[i]);
    SE->useIntv(SegStart, SegStop);
  }

  if (LREdit.empty()) {
    DEBUG(dbgs() << "All uses were copies.\n");
    return 0;
  }

  SmallVector<unsigned, 8> IntvMap;
  SE->finish(&IntvMap);
  DebugVars->splitRegister(VirtReg.reg, LREdit.regs(), *LIS);
  ExtraRegInfo.resize(MRI->getNumVirtRegs());

  // Assign all new registers to RS_Spill. This was the last chance.
  setStage(LREdit.begin(), LREdit.end(), RS_Spill);
  return 0;
}


//===----------------------------------------------------------------------===//
//                             Local Splitting
//===----------------------------------------------------------------------===//


/// calcGapWeights - Compute the maximum spill weight that needs to be evicted
/// in order to use PhysReg between two entries in SA->UseSlots.
///
/// GapWeight[i] represents the gap between UseSlots[i] and UseSlots[i+1].
///
void RAGreedy::calcGapWeights(unsigned PhysReg,
                              SmallVectorImpl<float> &GapWeight) {
  assert(SA->getUseBlocks().size() == 1 && "Not a local interval");
  const SplitAnalysis::BlockInfo &BI = SA->getUseBlocks().front();
  ArrayRef<SlotIndex> Uses = SA->getUseSlots();
  const unsigned NumGaps = Uses.size()-1;

  // Start and end points for the interference check.
  SlotIndex StartIdx =
    BI.LiveIn ? BI.FirstInstr.getBaseIndex() : BI.FirstInstr;
  SlotIndex StopIdx =
    BI.LiveOut ? BI.LastInstr.getBoundaryIndex() : BI.LastInstr;

  GapWeight.assign(NumGaps, 0.0f);

  // Add interference from each overlapping register.
  for (MCRegUnitIterator Units(PhysReg, TRI); Units.isValid(); ++Units) {
    if (!Matrix->query(const_cast<LiveInterval&>(SA->getParent()), *Units)
          .checkInterference())
      continue;

    // We know that VirtReg is a continuous interval from FirstInstr to
    // LastInstr, so we don't need InterferenceQuery.
    //
    // Interference that overlaps an instruction is counted in both gaps
    // surrounding the instruction. The exception is interference before
    // StartIdx and after StopIdx.
    //
    LiveIntervalUnion::SegmentIter IntI =
      Matrix->getLiveUnions()[*Units] .find(StartIdx);
    for (unsigned Gap = 0; IntI.valid() && IntI.start() < StopIdx; ++IntI) {
      // Skip the gaps before IntI.
      while (Uses[Gap+1].getBoundaryIndex() < IntI.start())
        if (++Gap == NumGaps)
          break;
      if (Gap == NumGaps)
        break;

      // Update the gaps covered by IntI.
      const float weight = IntI.value()->weight;
      for (; Gap != NumGaps; ++Gap) {
        GapWeight[Gap] = std::max(GapWeight[Gap], weight);
        if (Uses[Gap+1].getBaseIndex() >= IntI.stop())
          break;
      }
      if (Gap == NumGaps)
        break;
    }
  }

  // Add fixed interference.
  for (MCRegUnitIterator Units(PhysReg, TRI); Units.isValid(); ++Units) {
    const LiveRange &LR = LIS->getRegUnit(*Units);
    LiveRange::const_iterator I = LR.find(StartIdx);
    LiveRange::const_iterator E = LR.end();

    // Same loop as above. Mark any overlapped gaps as HUGE_VALF.
    for (unsigned Gap = 0; I != E && I->start < StopIdx; ++I) {
      while (Uses[Gap+1].getBoundaryIndex() < I->start)
        if (++Gap == NumGaps)
          break;
      if (Gap == NumGaps)
        break;

      for (; Gap != NumGaps; ++Gap) {
        GapWeight[Gap] = llvm::huge_valf;
        if (Uses[Gap+1].getBaseIndex() >= I->end)
          break;
      }
      if (Gap == NumGaps)
        break;
    }
  }
}

/// tryLocalSplit - Try to split VirtReg into smaller intervals inside its only
/// basic block.
///
unsigned RAGreedy::tryLocalSplit(LiveInterval &VirtReg, AllocationOrder &Order,
                                 SmallVectorImpl<unsigned> &NewVRegs) {
  assert(SA->getUseBlocks().size() == 1 && "Not a local interval");
  const SplitAnalysis::BlockInfo &BI = SA->getUseBlocks().front();

  // Note that it is possible to have an interval that is live-in or live-out
  // while only covering a single block - A phi-def can use undef values from
  // predecessors, and the block could be a single-block loop.
  // We don't bother doing anything clever about such a case, we simply assume
  // that the interval is continuous from FirstInstr to LastInstr. We should
  // make sure that we don't do anything illegal to such an interval, though.

  ArrayRef<SlotIndex> Uses = SA->getUseSlots();
  if (Uses.size() <= 2)
    return 0;
  const unsigned NumGaps = Uses.size()-1;

  DEBUG({
    dbgs() << "tryLocalSplit: ";
    for (unsigned i = 0, e = Uses.size(); i != e; ++i)
      dbgs() << ' ' << Uses[i];
    dbgs() << '\n';
  });

  // If VirtReg is live across any register mask operands, compute a list of
  // gaps with register masks.
  SmallVector<unsigned, 8> RegMaskGaps;
  if (Matrix->checkRegMaskInterference(VirtReg)) {
    // Get regmask slots for the whole block.
    ArrayRef<SlotIndex> RMS = LIS->getRegMaskSlotsInBlock(BI.MBB->getNumber());
    DEBUG(dbgs() << RMS.size() << " regmasks in block:");
    // Constrain to VirtReg's live range.
    unsigned ri = std::lower_bound(RMS.begin(), RMS.end(),
                                   Uses.front().getRegSlot()) - RMS.begin();
    unsigned re = RMS.size();
    for (unsigned i = 0; i != NumGaps && ri != re; ++i) {
      // Look for Uses[i] <= RMS <= Uses[i+1].
      assert(!SlotIndex::isEarlierInstr(RMS[ri], Uses[i]));
      if (SlotIndex::isEarlierInstr(Uses[i+1], RMS[ri]))
        continue;
      // Skip a regmask on the same instruction as the last use. It doesn't
      // overlap the live range.
      if (SlotIndex::isSameInstr(Uses[i+1], RMS[ri]) && i+1 == NumGaps)
        break;
      DEBUG(dbgs() << ' ' << RMS[ri] << ':' << Uses[i] << '-' << Uses[i+1]);
      RegMaskGaps.push_back(i);
      // Advance ri to the next gap. A regmask on one of the uses counts in
      // both gaps.
      while (ri != re && SlotIndex::isEarlierInstr(RMS[ri], Uses[i+1]))
        ++ri;
    }
    DEBUG(dbgs() << '\n');
  }

  // Since we allow local split results to be split again, there is a risk of
  // creating infinite loops. It is tempting to require that the new live
  // ranges have less instructions than the original. That would guarantee
  // convergence, but it is too strict. A live range with 3 instructions can be
  // split 2+3 (including the COPY), and we want to allow that.
  //
  // Instead we use these rules:
  //
  // 1. Allow any split for ranges with getStage() < RS_Split2. (Except for the
  //    noop split, of course).
  // 2. Require progress be made for ranges with getStage() == RS_Split2. All
  //    the new ranges must have fewer instructions than before the split.
  // 3. New ranges with the same number of instructions are marked RS_Split2,
  //    smaller ranges are marked RS_New.
  //
  // These rules allow a 3 -> 2+3 split once, which we need. They also prevent
  // excessive splitting and infinite loops.
  //
  bool ProgressRequired = getStage(VirtReg) >= RS_Split2;

  // Best split candidate.
  unsigned BestBefore = NumGaps;
  unsigned BestAfter = 0;
  float BestDiff = 0;

  const float blockFreq =
    SpillPlacer->getBlockFrequency(BI.MBB->getNumber()).getFrequency() *
    (1.0f / MBFI->getEntryFreq());
  SmallVector<float, 8> GapWeight;

  Order.rewind();
  while (unsigned PhysReg = Order.next()) {
    // Keep track of the largest spill weight that would need to be evicted in
    // order to make use of PhysReg between UseSlots[i] and UseSlots[i+1].
    calcGapWeights(PhysReg, GapWeight);

    // Remove any gaps with regmask clobbers.
    if (Matrix->checkRegMaskInterference(VirtReg, PhysReg))
      for (unsigned i = 0, e = RegMaskGaps.size(); i != e; ++i)
        GapWeight[RegMaskGaps[i]] = llvm::huge_valf;

    // Try to find the best sequence of gaps to close.
    // The new spill weight must be larger than any gap interference.

    // We will split before Uses[SplitBefore] and after Uses[SplitAfter].
    unsigned SplitBefore = 0, SplitAfter = 1;

    // MaxGap should always be max(GapWeight[SplitBefore..SplitAfter-1]).
    // It is the spill weight that needs to be evicted.
    float MaxGap = GapWeight[0];

    for (;;) {
      // Live before/after split?
      const bool LiveBefore = SplitBefore != 0 || BI.LiveIn;
      const bool LiveAfter = SplitAfter != NumGaps || BI.LiveOut;

      DEBUG(dbgs() << PrintReg(PhysReg, TRI) << ' '
                   << Uses[SplitBefore] << '-' << Uses[SplitAfter]
                   << " i=" << MaxGap);

      // Stop before the interval gets so big we wouldn't be making progress.
      if (!LiveBefore && !LiveAfter) {
        DEBUG(dbgs() << " all\n");
        break;
      }
      // Should the interval be extended or shrunk?
      bool Shrink = true;

      // How many gaps would the new range have?
      unsigned NewGaps = LiveBefore + SplitAfter - SplitBefore + LiveAfter;

      // Legally, without causing looping?
      bool Legal = !ProgressRequired || NewGaps < NumGaps;

      if (Legal && MaxGap < llvm::huge_valf) {
        // Estimate the new spill weight. Each instruction reads or writes the
        // register. Conservatively assume there are no read-modify-write
        // instructions.
        //
        // Try to guess the size of the new interval.
        const float EstWeight = normalizeSpillWeight(
            blockFreq * (NewGaps + 1),
            Uses[SplitBefore].distance(Uses[SplitAfter]) +
                (LiveBefore + LiveAfter) * SlotIndex::InstrDist,
            1);
        // Would this split be possible to allocate?
        // Never allocate all gaps, we wouldn't be making progress.
        DEBUG(dbgs() << " w=" << EstWeight);
        if (EstWeight * Hysteresis >= MaxGap) {
          Shrink = false;
          float Diff = EstWeight - MaxGap;
          if (Diff > BestDiff) {
            DEBUG(dbgs() << " (best)");
            BestDiff = Hysteresis * Diff;
            BestBefore = SplitBefore;
            BestAfter = SplitAfter;
          }
        }
      }

      // Try to shrink.
      if (Shrink) {
        if (++SplitBefore < SplitAfter) {
          DEBUG(dbgs() << " shrink\n");
          // Recompute the max when necessary.
          if (GapWeight[SplitBefore - 1] >= MaxGap) {
            MaxGap = GapWeight[SplitBefore];
            for (unsigned i = SplitBefore + 1; i != SplitAfter; ++i)
              MaxGap = std::max(MaxGap, GapWeight[i]);
          }
          continue;
        }
        MaxGap = 0;
      }

      // Try to extend the interval.
      if (SplitAfter >= NumGaps) {
        DEBUG(dbgs() << " end\n");
        break;
      }

      DEBUG(dbgs() << " extend\n");
      MaxGap = std::max(MaxGap, GapWeight[SplitAfter++]);
    }
  }

  // Didn't find any candidates?
  if (BestBefore == NumGaps)
    return 0;

  DEBUG(dbgs() << "Best local split range: " << Uses[BestBefore]
               << '-' << Uses[BestAfter] << ", " << BestDiff
               << ", " << (BestAfter - BestBefore + 1) << " instrs\n");

  LiveRangeEdit LREdit(&VirtReg, NewVRegs, *MF, *LIS, VRM, this);
  SE->reset(LREdit);

  SE->openIntv();
  SlotIndex SegStart = SE->enterIntvBefore(Uses[BestBefore]);
  SlotIndex SegStop  = SE->leaveIntvAfter(Uses[BestAfter]);
  SE->useIntv(SegStart, SegStop);
  SmallVector<unsigned, 8> IntvMap;
  SE->finish(&IntvMap);
  DebugVars->splitRegister(VirtReg.reg, LREdit.regs(), *LIS);

  // If the new range has the same number of instructions as before, mark it as
  // RS_Split2 so the next split will be forced to make progress. Otherwise,
  // leave the new intervals as RS_New so they can compete.
  bool LiveBefore = BestBefore != 0 || BI.LiveIn;
  bool LiveAfter = BestAfter != NumGaps || BI.LiveOut;
  unsigned NewGaps = LiveBefore + BestAfter - BestBefore + LiveAfter;
  if (NewGaps >= NumGaps) {
    DEBUG(dbgs() << "Tagging non-progress ranges: ");
    assert(!ProgressRequired && "Didn't make progress when it was required.");
    for (unsigned i = 0, e = IntvMap.size(); i != e; ++i)
      if (IntvMap[i] == 1) {
        setStage(LIS->getInterval(LREdit.get(i)), RS_Split2);
        DEBUG(dbgs() << PrintReg(LREdit.get(i)));
      }
    DEBUG(dbgs() << '\n');
  }
  ++NumLocalSplits;

  return 0;
}

//===----------------------------------------------------------------------===//
//                          Live Range Splitting
//===----------------------------------------------------------------------===//

/// trySplit - Try to split VirtReg or one of its interferences, making it
/// assignable.
/// @return Physreg when VirtReg may be assigned and/or new NewVRegs.
unsigned RAGreedy::trySplit(LiveInterval &VirtReg, AllocationOrder &Order,
                            SmallVectorImpl<unsigned>&NewVRegs) {
  // Ranges must be Split2 or less.
  if (getStage(VirtReg) >= RS_Spill)
    return 0;

  // Local intervals are handled separately.
  if (LIS->intervalIsInOneMBB(VirtReg)) {
    NamedRegionTimer T("Local Splitting", TimerGroupName, TimePassesIsEnabled);
    SA->analyze(&VirtReg);
    unsigned PhysReg = tryLocalSplit(VirtReg, Order, NewVRegs);
    if (PhysReg || !NewVRegs.empty())
      return PhysReg;
    return tryInstructionSplit(VirtReg, Order, NewVRegs);
  }

  NamedRegionTimer T("Global Splitting", TimerGroupName, TimePassesIsEnabled);

  SA->analyze(&VirtReg);

  // FIXME: SplitAnalysis may repair broken live ranges coming from the
  // coalescer. That may cause the range to become allocatable which means that
  // tryRegionSplit won't be making progress. This check should be replaced with
  // an assertion when the coalescer is fixed.
  if (SA->didRepairRange()) {
    // VirtReg has changed, so all cached queries are invalid.
    Matrix->invalidateVirtRegs();
    if (unsigned PhysReg = tryAssign(VirtReg, Order, NewVRegs))
      return PhysReg;
  }

  // First try to split around a region spanning multiple blocks. RS_Split2
  // ranges already made dubious progress with region splitting, so they go
  // straight to single block splitting.
  if (getStage(VirtReg) < RS_Split2) {
    unsigned PhysReg = tryRegionSplit(VirtReg, Order, NewVRegs);
    if (PhysReg || !NewVRegs.empty())
      return PhysReg;
  }

  // Then isolate blocks.
  return tryBlockSplit(VirtReg, Order, NewVRegs);
}

//===----------------------------------------------------------------------===//
//                          Last Chance Recoloring
//===----------------------------------------------------------------------===//

/// mayRecolorAllInterferences - Check if the virtual registers that
/// interfere with \p VirtReg on \p PhysReg (or one of its aliases) may be
/// recolored to free \p PhysReg.
/// When true is returned, \p RecoloringCandidates has been augmented with all
/// the live intervals that need to be recolored in order to free \p PhysReg
/// for \p VirtReg.
/// \p FixedRegisters contains all the virtual registers that cannot be
/// recolored.
bool
RAGreedy::mayRecolorAllInterferences(unsigned PhysReg, LiveInterval &VirtReg,
                                     SmallLISet &RecoloringCandidates,
                                     const SmallVirtRegSet &FixedRegisters) {
  const TargetRegisterClass *CurRC = MRI->getRegClass(VirtReg.reg);

  for (MCRegUnitIterator Units(PhysReg, TRI); Units.isValid(); ++Units) {
    LiveIntervalUnion::Query &Q = Matrix->query(VirtReg, *Units);
    // If there is LastChanceRecoloringMaxInterference or more interferences,
    // chances are one would not be recolorable.
    if (Q.collectInterferingVRegs(LastChanceRecoloringMaxInterference) >=
        LastChanceRecoloringMaxInterference && !ExhaustiveSearch) {
      DEBUG(dbgs() << "Early abort: too many interferences.\n");
      CutOffInfo |= CO_Interf;
      return false;
    }
    for (unsigned i = Q.interferingVRegs().size(); i; --i) {
      LiveInterval *Intf = Q.interferingVRegs()[i - 1];
      // If Intf is done and sit on the same register class as VirtReg,
      // it would not be recolorable as it is in the same state as VirtReg.
      if ((getStage(*Intf) == RS_Done &&
           MRI->getRegClass(Intf->reg) == CurRC) ||
          FixedRegisters.count(Intf->reg)) {
        DEBUG(dbgs() << "Early abort: the inteference is not recolorable.\n");
        return false;
      }
      RecoloringCandidates.insert(Intf);
    }
  }
  return true;
}

/// tryLastChanceRecoloring - Try to assign a color to \p VirtReg by recoloring
/// its interferences.
/// Last chance recoloring chooses a color for \p VirtReg and recolors every
/// virtual register that was using it. The recoloring process may recursively
/// use the last chance recoloring. Therefore, when a virtual register has been
/// assigned a color by this mechanism, it is marked as Fixed, i.e., it cannot
/// be last-chance-recolored again during this recoloring "session".
/// E.g.,
/// Let
/// vA can use {R1, R2    }
/// vB can use {    R2, R3}
/// vC can use {R1        }
/// Where vA, vB, and vC cannot be split anymore (they are reloads for
/// instance) and they all interfere.
///
/// vA is assigned R1
/// vB is assigned R2
/// vC tries to evict vA but vA is already done.
/// Regular register allocation fails.
///
/// Last chance recoloring kicks in:
/// vC does as if vA was evicted => vC uses R1.
/// vC is marked as fixed.
/// vA needs to find a color.
/// None are available.
/// vA cannot evict vC: vC is a fixed virtual register now.
/// vA does as if vB was evicted => vA uses R2.
/// vB needs to find a color.
/// R3 is available.
/// Recoloring => vC = R1, vA = R2, vB = R3
///
/// \p Order defines the preferred allocation order for \p VirtReg.
/// \p NewRegs will contain any new virtual register that have been created
/// (split, spill) during the process and that must be assigned.
/// \p FixedRegisters contains all the virtual registers that cannot be
/// recolored.
/// \p Depth gives the current depth of the last chance recoloring.
/// \return a physical register that can be used for VirtReg or ~0u if none
/// exists.
unsigned RAGreedy::tryLastChanceRecoloring(LiveInterval &VirtReg,
                                           AllocationOrder &Order,
                                           SmallVectorImpl<unsigned> &NewVRegs,
                                           SmallVirtRegSet &FixedRegisters,
                                           unsigned Depth) {
  DEBUG(dbgs() << "Try last chance recoloring for " << VirtReg << '\n');
  // Ranges must be Done.
  assert((getStage(VirtReg) >= RS_Done || !VirtReg.isSpillable()) &&
         "Last chance recoloring should really be last chance");
  // Set the max depth to LastChanceRecoloringMaxDepth.
  // We may want to reconsider that if we end up with a too large search space
  // for target with hundreds of registers.
  // Indeed, in that case we may want to cut the search space earlier.
  if (Depth >= LastChanceRecoloringMaxDepth && !ExhaustiveSearch) {
    DEBUG(dbgs() << "Abort because max depth has been reached.\n");
    CutOffInfo |= CO_Depth;
    return ~0u;
  }

  // Set of Live intervals that will need to be recolored.
  SmallLISet RecoloringCandidates;
  // Record the original mapping virtual register to physical register in case
  // the recoloring fails.
  DenseMap<unsigned, unsigned> VirtRegToPhysReg;
  // Mark VirtReg as fixed, i.e., it will not be recolored pass this point in
  // this recoloring "session".
  FixedRegisters.insert(VirtReg.reg);

  Order.rewind();
  while (unsigned PhysReg = Order.next()) {
    DEBUG(dbgs() << "Try to assign: " << VirtReg << " to "
                 << PrintReg(PhysReg, TRI) << '\n');
    RecoloringCandidates.clear();
    VirtRegToPhysReg.clear();

    // It is only possible to recolor virtual register interference.
    if (Matrix->checkInterference(VirtReg, PhysReg) >
        LiveRegMatrix::IK_VirtReg) {
      DEBUG(dbgs() << "Some inteferences are not with virtual registers.\n");

      continue;
    }

    // Early give up on this PhysReg if it is obvious we cannot recolor all
    // the interferences.
    if (!mayRecolorAllInterferences(PhysReg, VirtReg, RecoloringCandidates,
                                    FixedRegisters)) {
      DEBUG(dbgs() << "Some inteferences cannot be recolored.\n");
      continue;
    }

    // RecoloringCandidates contains all the virtual registers that interfer
    // with VirtReg on PhysReg (or one of its aliases).
    // Enqueue them for recoloring and perform the actual recoloring.
    PQueue RecoloringQueue;
    for (SmallLISet::iterator It = RecoloringCandidates.begin(),
                              EndIt = RecoloringCandidates.end();
         It != EndIt; ++It) {
      unsigned ItVirtReg = (*It)->reg;
      enqueue(RecoloringQueue, *It);
      assert(VRM->hasPhys(ItVirtReg) &&
             "Interferences are supposed to be with allocated vairables");

      // Record the current allocation.
      VirtRegToPhysReg[ItVirtReg] = VRM->getPhys(ItVirtReg);
      // unset the related struct.
      Matrix->unassign(**It);
    }

    // Do as if VirtReg was assigned to PhysReg so that the underlying
    // recoloring has the right information about the interferes and
    // available colors.
    Matrix->assign(VirtReg, PhysReg);

    // Save the current recoloring state.
    // If we cannot recolor all the interferences, we will have to start again
    // at this point for the next physical register.
    SmallVirtRegSet SaveFixedRegisters(FixedRegisters);
    if (tryRecoloringCandidates(RecoloringQueue, NewVRegs, FixedRegisters,
                                Depth)) {
      // Do not mess up with the global assignment process.
      // I.e., VirtReg must be unassigned.
      Matrix->unassign(VirtReg);
      return PhysReg;
    }

    DEBUG(dbgs() << "Fail to assign: " << VirtReg << " to "
                 << PrintReg(PhysReg, TRI) << '\n');

    // The recoloring attempt failed, undo the changes.
    FixedRegisters = SaveFixedRegisters;
    Matrix->unassign(VirtReg);

    for (SmallLISet::iterator It = RecoloringCandidates.begin(),
                              EndIt = RecoloringCandidates.end();
         It != EndIt; ++It) {
      unsigned ItVirtReg = (*It)->reg;
      if (VRM->hasPhys(ItVirtReg))
        Matrix->unassign(**It);
      unsigned ItPhysReg = VirtRegToPhysReg[ItVirtReg];
      Matrix->assign(**It, ItPhysReg);
    }
  }

  // Last chance recoloring did not worked either, give up.
  return ~0u;
}

/// tryRecoloringCandidates - Try to assign a new color to every register
/// in \RecoloringQueue.
/// \p NewRegs will contain any new virtual register created during the
/// recoloring process.
/// \p FixedRegisters[in/out] contains all the registers that have been
/// recolored.
/// \return true if all virtual registers in RecoloringQueue were successfully
/// recolored, false otherwise.
bool RAGreedy::tryRecoloringCandidates(PQueue &RecoloringQueue,
                                       SmallVectorImpl<unsigned> &NewVRegs,
                                       SmallVirtRegSet &FixedRegisters,
                                       unsigned Depth) {
  while (!RecoloringQueue.empty()) {
    LiveInterval *LI = dequeue(RecoloringQueue);
    DEBUG(dbgs() << "Try to recolor: " << *LI << '\n');
    unsigned PhysReg;
    PhysReg = selectOrSplitImpl(*LI, NewVRegs, FixedRegisters, Depth + 1);
    if (PhysReg == ~0u || !PhysReg)
      return false;
    DEBUG(dbgs() << "Recoloring of " << *LI
                 << " succeeded with: " << PrintReg(PhysReg, TRI) << '\n');
    Matrix->assign(*LI, PhysReg);
    FixedRegisters.insert(LI->reg);
  }
  return true;
}

//===----------------------------------------------------------------------===//
//                            Main Entry Point
//===----------------------------------------------------------------------===//

unsigned RAGreedy::selectOrSplit(LiveInterval &VirtReg,
                                 SmallVectorImpl<unsigned> &NewVRegs) {
  CutOffInfo = CO_None;
  LLVMContext &Ctx = MF->getFunction()->getContext();
  SmallVirtRegSet FixedRegisters;
  unsigned Reg = selectOrSplitImpl(VirtReg, NewVRegs, FixedRegisters);
  if (Reg == ~0U && (CutOffInfo != CO_None)) {
    uint8_t CutOffEncountered = CutOffInfo & (CO_Depth | CO_Interf);
    if (CutOffEncountered == CO_Depth)
      Ctx.emitError("register allocation failed: maximum depth for recoloring "
                    "reached. Use -fexhaustive-register-search to skip "
                    "cutoffs");
    else if (CutOffEncountered == CO_Interf)
      Ctx.emitError("register allocation failed: maximum interference for "
                    "recoloring reached. Use -fexhaustive-register-search "
                    "to skip cutoffs");
    else if (CutOffEncountered == (CO_Depth | CO_Interf))
      Ctx.emitError("register allocation failed: maximum interference and "
                    "depth for recoloring reached. Use "
                    "-fexhaustive-register-search to skip cutoffs");
  }
  return Reg;
}

/// Using a CSR for the first time has a cost because it causes push|pop
/// to be added to prologue|epilogue. Splitting a cold section of the live
/// range can have lower cost than using the CSR for the first time;
/// Spilling a live range in the cold path can have lower cost than using
/// the CSR for the first time. Returns the physical register if we decide
/// to use the CSR; otherwise return 0.
unsigned RAGreedy::tryAssignCSRFirstTime(LiveInterval &VirtReg,
                                         AllocationOrder &Order,
                                         unsigned PhysReg,
                                         unsigned &CostPerUseLimit,
                                         SmallVectorImpl<unsigned> &NewVRegs) {
  if (getStage(VirtReg) == RS_Spill && VirtReg.isSpillable()) {
    // We choose spill over using the CSR for the first time if the spill cost
    // is lower than CSRCost.
    SA->analyze(&VirtReg);
    if (calcSpillCost() >= CSRCost)
      return PhysReg;

    // We are going to spill, set CostPerUseLimit to 1 to make sure that
    // we will not use a callee-saved register in tryEvict.
    CostPerUseLimit = 1;
    return 0;
  }
  if (getStage(VirtReg) < RS_Split) {
    // We choose pre-splitting over using the CSR for the first time if
    // the cost of splitting is lower than CSRCost.
    SA->analyze(&VirtReg);
    unsigned NumCands = 0;
    BlockFrequency BestCost = CSRCost; // Don't modify CSRCost.
    unsigned BestCand = calculateRegionSplitCost(VirtReg, Order, BestCost,
                                                 NumCands, true /*IgnoreCSR*/);
    if (BestCand == NoCand)
      // Use the CSR if we can't find a region split below CSRCost.
      return PhysReg;

    // Perform the actual pre-splitting.
    doRegionSplit(VirtReg, BestCand, false/*HasCompact*/, NewVRegs);
    return 0;
  }
  return PhysReg;
}

void RAGreedy::aboutToRemoveInterval(LiveInterval &LI) {
  // Do not keep invalid information around.
  SetOfBrokenHints.remove(&LI);
}

void RAGreedy::initializeCSRCost() {
  // We use the larger one out of the command-line option and the value report
  // by TRI.
  CSRCost = BlockFrequency(
      std::max((unsigned)CSRFirstTimeCost, TRI->getCSRFirstUseCost()));
  if (!CSRCost.getFrequency())
    return;

  // Raw cost is relative to Entry == 2^14; scale it appropriately.
  uint64_t ActualEntry = MBFI->getEntryFreq();
  if (!ActualEntry) {
    CSRCost = 0;
    return;
  }
  uint64_t FixedEntry = 1 << 14;
  if (ActualEntry < FixedEntry)
    CSRCost *= BranchProbability(ActualEntry, FixedEntry);
  else if (ActualEntry <= UINT32_MAX)
    // Invert the fraction and divide.
    CSRCost /= BranchProbability(FixedEntry, ActualEntry);
  else
    // Can't use BranchProbability in general, since it takes 32-bit numbers.
    CSRCost = CSRCost.getFrequency() * (ActualEntry / FixedEntry);
}

/// \brief Collect the hint info for \p Reg.
/// The results are stored into \p Out.
/// \p Out is not cleared before being populated.
void RAGreedy::collectHintInfo(unsigned Reg, HintsInfo &Out) {
  for (const MachineInstr &Instr : MRI->reg_nodbg_instructions(Reg)) {
    if (!Instr.isFullCopy())
      continue;
    // Look for the other end of the copy.
    unsigned OtherReg = Instr.getOperand(0).getReg();
    if (OtherReg == Reg) {
      OtherReg = Instr.getOperand(1).getReg();
      if (OtherReg == Reg)
        continue;
    }
    // Get the current assignment.
    unsigned OtherPhysReg = TargetRegisterInfo::isPhysicalRegister(OtherReg)
                                ? OtherReg
                                : VRM->getPhys(OtherReg);
    // Push the collected information.
    Out.push_back(HintInfo(MBFI->getBlockFreq(Instr.getParent()), OtherReg,
                           OtherPhysReg));
  }
}

/// \brief Using the given \p List, compute the cost of the broken hints if
/// \p PhysReg was used.
/// \return The cost of \p List for \p PhysReg.
BlockFrequency RAGreedy::getBrokenHintFreq(const HintsInfo &List,
                                           unsigned PhysReg) {
  BlockFrequency Cost = 0;
  for (const HintInfo &Info : List) {
    if (Info.PhysReg != PhysReg)
      Cost += Info.Freq;
  }
  return Cost;
}

/// \brief Using the register assigned to \p VirtReg, try to recolor
/// all the live ranges that are copy-related with \p VirtReg.
/// The recoloring is then propagated to all the live-ranges that have
/// been recolored and so on, until no more copies can be coalesced or
/// it is not profitable.
/// For a given live range, profitability is determined by the sum of the
/// frequencies of the non-identity copies it would introduce with the old
/// and new register.
void RAGreedy::tryHintRecoloring(LiveInterval &VirtReg) {
  // We have a broken hint, check if it is possible to fix it by
  // reusing PhysReg for the copy-related live-ranges. Indeed, we evicted
  // some register and PhysReg may be available for the other live-ranges.
  SmallSet<unsigned, 4> Visited;
  SmallVector<unsigned, 2> RecoloringCandidates;
  HintsInfo Info;
  unsigned Reg = VirtReg.reg;
  unsigned PhysReg = VRM->getPhys(Reg);
  // Start the recoloring algorithm from the input live-interval, then
  // it will propagate to the ones that are copy-related with it.
  Visited.insert(Reg);
  RecoloringCandidates.push_back(Reg);

  DEBUG(dbgs() << "Trying to reconcile hints for: " << PrintReg(Reg, TRI) << '('
               << PrintReg(PhysReg, TRI) << ")\n");

  do {
    Reg = RecoloringCandidates.pop_back_val();

    // We cannot recolor physcal register.
    if (TargetRegisterInfo::isPhysicalRegister(Reg))
      continue;

    assert(VRM->hasPhys(Reg) && "We have unallocated variable!!");

    // Get the live interval mapped with this virtual register to be able
    // to check for the interference with the new color.
    LiveInterval &LI = LIS->getInterval(Reg);
    unsigned CurrPhys = VRM->getPhys(Reg);
    // Check that the new color matches the register class constraints and
    // that it is free for this live range.
    if (CurrPhys != PhysReg && (!MRI->getRegClass(Reg)->contains(PhysReg) ||
                                Matrix->checkInterference(LI, PhysReg)))
      continue;

    DEBUG(dbgs() << PrintReg(Reg, TRI) << '(' << PrintReg(CurrPhys, TRI)
                 << ") is recolorable.\n");

    // Gather the hint info.
    Info.clear();
    collectHintInfo(Reg, Info);
    // Check if recoloring the live-range will increase the cost of the
    // non-identity copies.
    if (CurrPhys != PhysReg) {
      DEBUG(dbgs() << "Checking profitability:\n");
      BlockFrequency OldCopiesCost = getBrokenHintFreq(Info, CurrPhys);
      BlockFrequency NewCopiesCost = getBrokenHintFreq(Info, PhysReg);
      DEBUG(dbgs() << "Old Cost: " << OldCopiesCost.getFrequency()
                   << "\nNew Cost: " << NewCopiesCost.getFrequency() << '\n');
      if (OldCopiesCost < NewCopiesCost) {
        DEBUG(dbgs() << "=> Not profitable.\n");
        continue;
      }
      // At this point, the cost is either cheaper or equal. If it is
      // equal, we consider this is profitable because it may expose
      // more recoloring opportunities.
      DEBUG(dbgs() << "=> Profitable.\n");
      // Recolor the live-range.
      Matrix->unassign(LI);
      Matrix->assign(LI, PhysReg);
    }
    // Push all copy-related live-ranges to keep reconciling the broken
    // hints.
    for (const HintInfo &HI : Info) {
      if (Visited.insert(HI.Reg).second)
        RecoloringCandidates.push_back(HI.Reg);
    }
  } while (!RecoloringCandidates.empty());
}

/// \brief Try to recolor broken hints.
/// Broken hints may be repaired by recoloring when an evicted variable
/// freed up a register for a larger live-range.
/// Consider the following example:
/// BB1:
///   a =
///   b =
/// BB2:
///   ...
///   = b
///   = a
/// Let us assume b gets split:
/// BB1:
///   a =
///   b =
/// BB2:
///   c = b
///   ...
///   d = c
///   = d
///   = a
/// Because of how the allocation work, b, c, and d may be assigned different
/// colors. Now, if a gets evicted later:
/// BB1:
///   a =
///   st a, SpillSlot
///   b =
/// BB2:
///   c = b
///   ...
///   d = c
///   = d
///   e = ld SpillSlot
///   = e
/// This is likely that we can assign the same register for b, c, and d,
/// getting rid of 2 copies.
void RAGreedy::tryHintsRecoloring() {
  for (LiveInterval *LI : SetOfBrokenHints) {
    assert(TargetRegisterInfo::isVirtualRegister(LI->reg) &&
           "Recoloring is possible only for virtual registers");
    // Some dead defs may be around (e.g., because of debug uses).
    // Ignore those.
    if (!VRM->hasPhys(LI->reg))
      continue;
    tryHintRecoloring(*LI);
  }
}

unsigned RAGreedy::selectOrSplitImpl(LiveInterval &VirtReg,
                                     SmallVectorImpl<unsigned> &NewVRegs,
                                     SmallVirtRegSet &FixedRegisters,
                                     unsigned Depth) {
  unsigned CostPerUseLimit = ~0u;
  // First try assigning a free register.
  AllocationOrder Order(VirtReg.reg, *VRM, RegClassInfo, Matrix);
  if (unsigned PhysReg = tryAssign(VirtReg, Order, NewVRegs)) {
    // When NewVRegs is not empty, we may have made decisions such as evicting
    // a virtual register, go with the earlier decisions and use the physical
    // register.
    if (CSRCost.getFrequency() && isUnusedCalleeSavedReg(PhysReg) &&
        NewVRegs.empty()) {
      unsigned CSRReg = tryAssignCSRFirstTime(VirtReg, Order, PhysReg,
                                              CostPerUseLimit, NewVRegs);
      if (CSRReg || !NewVRegs.empty())
        // Return now if we decide to use a CSR or create new vregs due to
        // pre-splitting.
        return CSRReg;
    } else
      return PhysReg;
  }

  LiveRangeStage Stage = getStage(VirtReg);
  DEBUG(dbgs() << StageName[Stage]
               << " Cascade " << ExtraRegInfo[VirtReg.reg].Cascade << '\n');

  // Try to evict a less worthy live range, but only for ranges from the primary
  // queue. The RS_Split ranges already failed to do this, and they should not
  // get a second chance until they have been split.
  if (Stage != RS_Split)
    if (unsigned PhysReg =
            tryEvict(VirtReg, Order, NewVRegs, CostPerUseLimit)) {
      unsigned Hint = MRI->getSimpleHint(VirtReg.reg);
      // If VirtReg has a hint and that hint is broken record this
      // virtual register as a recoloring candidate for broken hint.
      // Indeed, since we evicted a variable in its neighborhood it is
      // likely we can at least partially recolor some of the
      // copy-related live-ranges.
      if (Hint && Hint != PhysReg)
        SetOfBrokenHints.insert(&VirtReg);
      return PhysReg;
    }

  assert(NewVRegs.empty() && "Cannot append to existing NewVRegs");

  // The first time we see a live range, don't try to split or spill.
  // Wait until the second time, when all smaller ranges have been allocated.
  // This gives a better picture of the interference to split around.
  if (Stage < RS_Split) {
    setStage(VirtReg, RS_Split);
    DEBUG(dbgs() << "wait for second round\n");
    NewVRegs.push_back(VirtReg.reg);
    return 0;
  }

  // If we couldn't allocate a register from spilling, there is probably some
  // invalid inline assembly. The base class wil report it.
  if (Stage >= RS_Done || !VirtReg.isSpillable())
    return tryLastChanceRecoloring(VirtReg, Order, NewVRegs, FixedRegisters,
                                   Depth);

  // Try splitting VirtReg or interferences.
  unsigned PhysReg = trySplit(VirtReg, Order, NewVRegs);
  if (PhysReg || !NewVRegs.empty())
    return PhysReg;

  // Finally spill VirtReg itself.
  if (EnableDeferredSpilling && getStage(VirtReg) < RS_Memory) {
    // TODO: This is experimental and in particular, we do not model
    // the live range splitting done by spilling correctly.
    // We would need a deep integration with the spiller to do the
    // right thing here. Anyway, that is still good for early testing.
    setStage(VirtReg, RS_Memory);
    DEBUG(dbgs() << "Do as if this register is in memory\n");
    NewVRegs.push_back(VirtReg.reg);
  } else {
    NamedRegionTimer T("Spiller", TimerGroupName, TimePassesIsEnabled);
    LiveRangeEdit LRE(&VirtReg, NewVRegs, *MF, *LIS, VRM, this);
    spiller().spill(LRE);
    setStage(NewVRegs.begin(), NewVRegs.end(), RS_Done);

    if (VerifyEnabled)
      MF->verify(this, "After spilling");
  }

  // The live virtual register requesting allocation was spilled, so tell
  // the caller not to allocate anything during this round.
  return 0;
}

bool RAGreedy::runOnMachineFunction(MachineFunction &mf) {
  DEBUG(dbgs() << "********** GREEDY REGISTER ALLOCATION **********\n"
               << "********** Function: " << mf.getName() << '\n');

  MF = &mf;
  TRI = MF->getSubtarget().getRegisterInfo();
  TII = MF->getSubtarget().getInstrInfo();
  RCI.runOnMachineFunction(mf);

  EnableLocalReassign = EnableLocalReassignment ||
                        MF->getSubtarget().enableRALocalReassignment(
                            MF->getTarget().getOptLevel());

  if (VerifyEnabled)
    MF->verify(this, "Before greedy register allocator");

  RegAllocBase::init(getAnalysis<VirtRegMap>(),
                     getAnalysis<LiveIntervals>(),
                     getAnalysis<LiveRegMatrix>());
  Indexes = &getAnalysis<SlotIndexes>();
  MBFI = &getAnalysis<MachineBlockFrequencyInfo>();
  DomTree = &getAnalysis<MachineDominatorTree>();
  SpillerInstance.reset(createInlineSpiller(*this, *MF, *VRM));
  Loops = &getAnalysis<MachineLoopInfo>();
  Bundles = &getAnalysis<EdgeBundles>();
  SpillPlacer = &getAnalysis<SpillPlacement>();
  DebugVars = &getAnalysis<LiveDebugVariables>();

  initializeCSRCost();

  calculateSpillWeightsAndHints(*LIS, mf, VRM, *Loops, *MBFI);

  DEBUG(LIS->dump());

  SA.reset(new SplitAnalysis(*VRM, *LIS, *Loops));
  SE.reset(new SplitEditor(*SA, *LIS, *VRM, *DomTree, *MBFI));
  ExtraRegInfo.clear();
  ExtraRegInfo.resize(MRI->getNumVirtRegs());
  NextCascade = 1;
  IntfCache.init(MF, Matrix->getLiveUnions(), Indexes, LIS, TRI);
  GlobalCand.resize(32);  // This will grow as needed.
  SetOfBrokenHints.clear();

  allocatePhysRegs();
  tryHintsRecoloring();
  releaseMemory();
  return true;
}
