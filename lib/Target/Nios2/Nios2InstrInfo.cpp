//===-- Nios2InstrInfo.cpp - Nios2 Instruction Information ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Nios2 implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "Nios2InstrInfo.h"
#include "Nios2TargetMachine.h"
#include "Nios2MachineFunction.h"
#include "Nios2TargetObjectFile.h"
#include "InstPrinter/Nios2InstPrinter.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

#define GET_INSTRINFO_CTOR_DTOR
#include "Nios2GenInstrInfo.inc"

// Pin the vtable to this file.
void Nios2InstrInfo::anchor() {}

Nios2InstrInfo::Nios2InstrInfo(const Nios2Subtarget &STI)
  : Nios2GenInstrInfo(Nios2::ADJCALLSTACKDOWN, Nios2::ADJCALLSTACKUP),
    Subtarget(STI), UncondBrOpc(Nios2::BR), RI() {}

bool Nios2InstrInfo::isZeroImm(const MachineOperand &op) const {
  return op.isImm() && op.getImm() == 0;
}

/// insertNoop - If data hazard condition is found insert the target nop
/// instruction.
void Nios2InstrInfo::
insertNoop(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI) const {
  DebugLoc DL;
  unsigned ZEROReg = Nios2::ZERO;
  // NOP expansion
  BuildMI(MBB, MI, DL, get(Nios2::ADD), ZEROReg).addReg(ZEROReg)
    .addReg(ZEROReg);
}

MachineMemOperand *Nios2InstrInfo::GetMemOperand(MachineBasicBlock &MBB, int FI,
                                                unsigned Flag) const {
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = *MF.getFrameInfo();
  unsigned Align = MFI.getObjectAlignment(FI);

  return MF.getMachineMemOperand(MachinePointerInfo::getFixedStack(MF, FI), Flag,
                                 MFI.getObjectSize(FI), Align);
}

MachineInstr*
Nios2InstrInfo::emitFrameIndexDebugValue(MachineFunction &MF, int FrameIx,
                                        uint64_t Offset, const MDNode *MDPtr,
                                        DebugLoc DL) const {
  MachineInstrBuilder MIB = BuildMI(MF, DL, get(Nios2::DBG_VALUE))
    .addFrameIndex(FrameIx).addImm(0).addImm(Offset).addMetadata(MDPtr);
  return &*MIB;
}

//===----------------------------------------------------------------------===//
// Branch Analysis
//===----------------------------------------------------------------------===//

void Nios2InstrInfo::AnalyzeCondBr(const MachineInstr *Inst, unsigned Opc,
                                  MachineBasicBlock *&BB,
                                  SmallVectorImpl<MachineOperand> &Cond) const {
  assert(GetAnalyzableBrOpc(Opc) && "Not an analyzable branch");
  int NumOp = Inst->getNumExplicitOperands();

  // for both int and fp branches, the last explicit operand is the
  // MBB.
  BB = Inst->getOperand(NumOp-1).getMBB();
  Cond.push_back(MachineOperand::CreateImm(Opc));

  for (int i=0; i<NumOp-1; i++)
    Cond.push_back(Inst->getOperand(i));
}

unsigned Nios2InstrInfo::GetAnalyzableBrOpc(unsigned Opc) const {
  return (Opc == Nios2::BEQ   || Opc == Nios2::BNE   || Opc == Nios2::BGT   ||
          Opc == Nios2::BGE   || Opc == Nios2::BLT   || Opc == Nios2::BLE   ||
          Opc == Nios2::BR    || Opc == Nios2::JMP) ?
         Opc : 0;
}

bool Nios2InstrInfo::AnalyzeBranch(MachineBasicBlock &MBB,
                                  MachineBasicBlock *&TBB,
                                  MachineBasicBlock *&FBB,
                                  SmallVectorImpl<MachineOperand> &Cond,
                                  bool AllowModify) const
{

  MachineBasicBlock::reverse_iterator I = MBB.rbegin(), REnd = MBB.rend();

  // Skip all the debug instructions.
  while (I != REnd && I->isDebugValue())
    ++I;

  if (I == REnd || !isUnpredicatedTerminator(&*I)) {
    // If this block ends with no branches (it just falls through to its succ)
    // just return false, leaving TBB/FBB null.
    TBB = FBB = NULL;
    return false;
  }

  MachineInstr *LastInst = &*I;
  unsigned LastOpc = LastInst->getOpcode();

  // Not an analyzable branch (must be an indirect jump).
  if (!GetAnalyzableBrOpc(LastOpc))
    return true;

  // Get the second to last instruction in the block.
  unsigned SecondLastOpc = 0;
  MachineInstr *SecondLastInst = NULL;

  if (++I != REnd) {
    SecondLastInst = &*I;
    SecondLastOpc = GetAnalyzableBrOpc(SecondLastInst->getOpcode());

    // Not an analyzable branch (must be an indirect jump).
    if (isUnpredicatedTerminator(SecondLastInst) && !SecondLastOpc)
      return true;
  }

  // If there is only one terminator instruction, process it.
  if (!SecondLastOpc) {
    // Unconditional branch
    if (LastOpc == UncondBrOpc) {
      TBB = LastInst->getOperand(0).getMBB();
      return false;
    }

    // Conditional branch
    AnalyzeCondBr(LastInst, LastOpc, TBB, Cond);
    return false;
  }

  // If we reached here, there are two branches.
  // If there are three terminators, we don't know what sort of block this is.
  if (++I != REnd && isUnpredicatedTerminator(&*I))
    return true;

  // If second to last instruction is an unconditional branch,
  // analyze it and remove the last instruction.
  if (SecondLastOpc == UncondBrOpc) {
    // Return if the last instruction cannot be removed.
    if (!AllowModify)
      return true;

    TBB = SecondLastInst->getOperand(0).getMBB();
    LastInst->eraseFromParent();
    return false;
  }

  // Conditional branch followed by an unconditional branch.
  // The last one must be unconditional.
  if (LastOpc != UncondBrOpc)
    return true;

  AnalyzeCondBr(SecondLastInst, SecondLastOpc, TBB, Cond);
  FBB = LastInst->getOperand(0).getMBB();

  return false;
}

void Nios2InstrInfo::BuildCondBr(MachineBasicBlock &MBB,
                                MachineBasicBlock *TBB, DebugLoc DL,
                                const SmallVectorImpl<MachineOperand>& Cond)
  const {
  unsigned Opc = Cond[0].getImm();
  const MCInstrDesc &MCID = get(Opc);
  MachineInstrBuilder MIB = BuildMI(&MBB, DL, MCID);

  for (unsigned i = 1; i < Cond.size(); ++i) {
    if (Cond[i].isReg())
      MIB.addReg(Cond[i].getReg());
    else if (Cond[i].isImm())
      MIB.addImm(Cond[i].getImm());
    else
       assert(true && "Cannot copy operand");
  }
  MIB.addMBB(TBB);
}

unsigned Nios2InstrInfo::
InsertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
             MachineBasicBlock *FBB,
             const SmallVectorImpl<MachineOperand> &Cond,
             DebugLoc DL) const {
  // Shouldn't be a fall through.
  assert(TBB && "InsertBranch must not be told to insert a fallthrough");

  // # of condition operands:
  //  Unconditional branches: 0
  //  Floating point branches: 1 (opc)
  //  Int BranchZero: 2 (opc, reg)
  //  Int Branch: 3 (opc, reg0, reg1)
  assert((Cond.size() <= 3) &&
         "# of Nios2 branch conditions must be <= 3!");

  // Two-way Conditional branch.
  if (FBB) {
    BuildCondBr(MBB, TBB, DL, Cond);
    BuildMI(&MBB, DL, get(UncondBrOpc)).addMBB(FBB);
    return 2;
  }

  // One way branch.
  // Unconditional branch.
  if (Cond.empty())
    BuildMI(&MBB, DL, get(UncondBrOpc)).addMBB(TBB);
  else // Conditional branch.
    BuildCondBr(MBB, TBB, DL, Cond);
  return 1;
}

unsigned Nios2InstrInfo::
RemoveBranch(MachineBasicBlock &MBB) const
{
  MachineBasicBlock::reverse_iterator I = MBB.rbegin(), REnd = MBB.rend();
  MachineBasicBlock::reverse_iterator FirstBr;
  unsigned removed;

  // Skip all the debug instructions.
  while (I != REnd && I->isDebugValue())
    ++I;

  FirstBr = I;

  // Up to 2 branches are removed.
  // Note that indirect branches are not removed.
  for(removed = 0; I != REnd && removed < 2; ++I, ++removed)
    if (!GetAnalyzableBrOpc(I->getOpcode()))
      break;

  MBB.erase(I.base(), FirstBr.base());

  return removed;
}

/// GetOppositeBranchOpc - Return the inverse of the specified
/// opcode, e.g. turning BEQ to BNE.
unsigned Nios2InstrInfo::GetOppositeBranchOpc(unsigned Opc) const {
  switch (Opc) {
  default:           llvm_unreachable("Illegal opcode!");
  case Nios2::BEQ:    return Nios2::BNE;
  case Nios2::BNE:    return Nios2::BEQ;
  case Nios2::BGT:   return Nios2::BLE;
  case Nios2::BGE:   return Nios2::BLT;
  case Nios2::BLT:   return Nios2::BGE;
  case Nios2::BLE:   return Nios2::BGT;
  }
}

/// ReverseBranchCondition - Return the inverse opcode of the
/// specified Branch instruction.
bool Nios2InstrInfo::
ReverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const
{
  assert( (Cond.size() && Cond.size() <= 3) &&
          "Invalid Nios2 branch condition!");
  Cond[0].setImm(GetOppositeBranchOpc(Cond[0].getImm()));
  return false;
}

/// Return the number of bytes of code the specified instruction may be.
unsigned Nios2InstrInfo::GetInstSizeInBytes(const MachineInstr *MI) const {
  switch (MI->getOpcode()) {
  default:
    return MI->getDesc().getSize();
  case  TargetOpcode::INLINEASM: {       // Inline Asm: Variable size.
    const MachineFunction *MF = MI->getParent()->getParent();
    const char *AsmStr = MI->getOperand(0).getSymbolName();
    return getInlineAsmLength(AsmStr, *MF->getTarget().getMCAsmInfo());
  }
  }
}

/// This function generates the sequence of instructions needed to get the
/// result of adding register REG and immediate IMM.
unsigned
Nios2InstrInfo::loadImmediate(int32_t Imm, MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator II, DebugLoc DL,
                               unsigned *NewImm) const {
  unsigned ZEROReg = Nios2::ZERO;
  unsigned ATReg = Nios2::AT;

  if (isInt<16>(Imm)) {
    BuildMI(MBB, II, DL, get(Nios2::ADD), ATReg).addReg(ZEROReg)
      .addImm(Imm);
    if (NewImm)
      *NewImm = 0;
    return ATReg;
  }

  BuildMI(MBB, II, DL, get(Nios2::ORhi), ATReg).addReg(ZEROReg)
    .addImm(Imm >> 16);
  if (NewImm) {
    *NewImm = Imm & 0xffffU;
    return ATReg;
  }
  BuildMI(MBB, II, DL, get(Nios2::ORi), ATReg).addReg(ZEROReg)
    .addImm(Imm & 0xffffU);
  return ATReg;
}

/// Adjust SP by Amount bytes.
void Nios2InstrInfo::adjustStackPtr(unsigned SP, int64_t Amount,
                                     MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator I) const {
  DebugLoc DL = I != MBB.end() ? I->getDebugLoc() : DebugLoc();
  unsigned ADDu = Nios2::ADD;
  unsigned ADDiu = Nios2::ADDi;

  if (isInt<16>(Amount))// addi sp, sp, amount
    BuildMI(MBB, I, DL, get(ADDiu), SP).addReg(SP).addImm(Amount);
  else { // Expand immediate that doesn't fit in 16-bit.
    MBB.getParent()->getInfo<Nios2FunctionInfo>()->setEmitNOAT();
    unsigned Reg = loadImmediate(Amount, MBB, I, DL, 0);
    BuildMI(MBB, I, DL, get(ADDu), SP).addReg(SP).addReg(Reg);
  }
}

bool Nios2InstrInfo::expandPostRAPseudo(MachineBasicBlock::iterator MI) const {
  MachineBasicBlock &MBB = *MI->getParent();

  switch(MI->getDesc().getOpcode()) {
  default:
    return false;
  case Nios2::RetRA:
    BuildMI(MBB, MI, MI->getDebugLoc(), get(Nios2::RET)).addReg(Nios2::RA);
    break;
  }

  MBB.erase(MI);
  return true;
}


void Nios2InstrInfo::storeRegToStackSlot(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator I,
                                   unsigned SrcReg, bool isKill, int FI,
                                   const TargetRegisterClass *RC,
                                   const TargetRegisterInfo *TRI) const {
  DebugLoc DL;
  if (I != MBB.end()) DL = I->getDebugLoc();
  MachineMemOperand *MMO = GetMemOperand(MBB, FI, MachineMemOperand::MOStore);
  unsigned Opc = Nios2::STW;
  BuildMI(MBB, I, DL, get(Opc)).addReg(SrcReg, getKillRegState(isKill))
    .addFrameIndex(FI).addImm(0).addMemOperand(MMO);
}

void Nios2InstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator I,
                                    unsigned DestReg, int FI,
                                    const TargetRegisterClass *RC,
                                    const TargetRegisterInfo *TRI) const {
  DebugLoc DL;
  if (I != MBB.end()) DL = I->getDebugLoc();
  MachineMemOperand *MMO = GetMemOperand(MBB, FI, MachineMemOperand::MOLoad);
  unsigned Opc = Nios2::LDW;
  BuildMI(MBB, I, DL, get(Opc), DestReg).addFrameIndex(FI).addImm(0)
    .addMemOperand(MMO);
}

void Nios2InstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator I, DebugLoc DL,
                                  unsigned DestReg, unsigned SrcReg,
                                  bool KillSrc) const {
  unsigned Opc = 0, ZeroReg = 0;

  if (Nios2::CPURegsRegClass.contains(DestReg)) { // Copy to CPU Reg.
    if (Nios2::CPURegsRegClass.contains(SrcReg))
      Opc = Nios2::OR, ZeroReg = Nios2::ZERO;
  }

  assert(Opc && "Cannot copy registers");

  MachineInstrBuilder MIB = BuildMI(MBB, I, DL, get(Opc));

  if (DestReg)
    MIB.addReg(DestReg, RegState::Define);

  if (SrcReg)
    MIB.addReg(SrcReg, getKillRegState(KillSrc));

  if (ZeroReg)
    MIB.addReg(ZeroReg);
}


