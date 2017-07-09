//===-- Nios2RegisterInfo.cpp - NIOS2 Register Information -== --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the NIOS2 implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#include "Nios2RegisterInfo.h"
#include "Nios2.h"
#include "Nios2InstrInfo.h"
#include "Nios2Subtarget.h"
#include "Nios2MachineFunction.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Type.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetFrameLowering.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Target/TargetInstrInfo.h"

using namespace llvm;

#define DEBUG_TYPE "nios2-reg-info"

#define GET_REGINFO_TARGET_DESC
#include "Nios2GenRegisterInfo.inc"

Nios2RegisterInfo::Nios2RegisterInfo(): Nios2GenRegisterInfo(Nios2::RA) {}

unsigned Nios2RegisterInfo::getPICCallReg() { return Nios2::GP; }

//===----------------------------------------------------------------------===//
// Callee Saved Registers methods
//===----------------------------------------------------------------------===//

/// Nios2 Callee Saved Registers
const uint16_t* Nios2RegisterInfo::
getCalleeSavedRegs(const MachineFunction *MF) const {
  return CSR_STD_SaveList;
}

const uint32_t*
Nios2RegisterInfo::getCallPreservedMask(CallingConv::ID) const {
  return CSR_STD_RegMask;
}

BitVector Nios2RegisterInfo::
getReservedRegs(const MachineFunction &MF) const {
  static const uint16_t ReservedCPURegs[] = {
    Nios2::ZERO, Nios2::AT, Nios2::ET, Nios2::BT, Nios2::GP,
    Nios2::SP, Nios2::EA, Nios2::BA, Nios2::RA, Nios2::PC,
    Nios2::CTL0, Nios2::CTL1, Nios2::CTL2, Nios2::CTL3,
    Nios2::CTL4, Nios2::CTL5, Nios2::CTL7,
    Nios2::CTL8, Nios2::CTL9, Nios2::CTL10,
    Nios2::CTL12, Nios2::CTL13, Nios2::CTL14, Nios2::CTL15,
  };

  BitVector Reserved(getNumRegs());
  typedef TargetRegisterClass::const_iterator RegIter;

  for (unsigned I = 0; I < array_lengthof(ReservedCPURegs); ++I)
    Reserved.set(ReservedCPURegs[I]);

  // Reserve FP if this function should have a dedicated frame pointer register.
  if (MF.getSubtarget().getFrameLowering()->hasFP(MF))
    Reserved.set(Nios2::FP);

  return Reserved;
}

bool
Nios2RegisterInfo::requiresRegisterScavenging(const MachineFunction &MF) const {
  return true;
}

bool
Nios2RegisterInfo::trackLivenessAfterRegAlloc(const MachineFunction &MF) const {
  return true;
}

// FrameIndex represent objects inside a abstract stack.
// We must replace FrameIndex with an stack/frame pointer
// direct reference.
void Nios2RegisterInfo::
eliminateFrameIndex(MachineBasicBlock::iterator II, int SPAdj,
                    unsigned FIOperandNum, RegScavenger *RS) const {
  MachineInstr &MI = *II;
  MachineFunction &MF = *MI.getParent()->getParent();

  DEBUG(dbgs() << "\nFunction : " << MF.getName() << "\n";
        dbgs() << "<--------->\n" << MI);

  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();
  uint64_t stackSize = MF.getFrameInfo()->getStackSize();
  int64_t spOffset = MF.getFrameInfo()->getObjectOffset(FrameIndex);

  DEBUG(dbgs() << "FrameIndex : " << FrameIndex << "\n"
               << "spOffset   : " << spOffset << "\n"
               << "stackSize  : " << stackSize << "\n");

  eliminateFI(MI, FIOperandNum, FrameIndex, stackSize, spOffset);
}

void Nios2RegisterInfo::eliminateFI(MachineBasicBlock::iterator II,
                                     unsigned OpNo, int FrameIndex,
                                     uint64_t StackSize,
                                     int64_t SPOffset) const {
  MachineInstr &MI = *II;
  MachineFunction &MF = *MI.getParent()->getParent();
  MachineFrameInfo *MFI = MF.getFrameInfo();
  Nios2FunctionInfo *Nios2FI = MF.getInfo<Nios2FunctionInfo>();

  const std::vector<CalleeSavedInfo> &CSI = MFI->getCalleeSavedInfo();
  int MinCSFI = 0;
  int MaxCSFI = -1;

  if (CSI.size()) {
    MinCSFI = CSI[0].getFrameIdx();
    MaxCSFI = CSI[CSI.size() - 1].getFrameIdx();
  }

  // The following stack frame objects are always referenced relative to $sp:
  //  1. Outgoing arguments.
  //  2. Pointer to dynamically allocated stack space.
  //  3. Locations for callee-saved registers.
  // Everything else is referenced relative to whatever register
  // getFrameRegister() returns.
  unsigned FrameReg;

  if (Nios2FI->isOutArgFI(FrameIndex) ||
      (FrameIndex >= MinCSFI && FrameIndex <= MaxCSFI))
    FrameReg = Nios2::SP;
  else
    FrameReg = getFrameRegister(MF);

  // Calculate final offset.
  // - There is no need to change the offset if the frame object is one of the
  //   following: an outgoing argument, pointer to a dynamically allocated
  //   stack space or a $gp restore location,
  // - If the frame object is any of the following, its offset must be adjusted
  //   by adding the size of the stack:
  //   incoming argument, callee-saved register location or local variable.
  int32_t Offset;

  if (Nios2FI->isOutArgFI(FrameIndex))
    Offset = SPOffset;
  else
    Offset = SPOffset + (int64_t)StackSize;

  Offset    += MI.getOperand(OpNo + 1).getImm();

  DEBUG(errs() << "Offset     : " << Offset << "\n" << "<--------->\n");

  // If MI is not a debug value, make sure Offset fits in the 16-bit immediate
  // field.
  if (!MI.isDebugValue() && !isInt<16>(Offset)) {
    MachineBasicBlock &MBB = *MI.getParent();
    DebugLoc DL = II->getDebugLoc();
    unsigned ATReg = Nios2::AT;
    unsigned NewImm;

    Nios2FI->setEmitNOAT();
    const Nios2InstrInfo &TII =
      *static_cast<const Nios2InstrInfo *>(
        MBB.getParent()->getSubtarget().getInstrInfo());
    unsigned Reg = TII.loadImmediate(Offset, MBB, II, DL, &NewImm);
    BuildMI(MBB, II, DL, TII.get(Nios2::ADD), ATReg).addReg(FrameReg)
      .addReg(Reg);

    FrameReg = ATReg;
    Offset = SignExtend64<16>(NewImm);
  }

  MI.getOperand(OpNo).ChangeToRegister(FrameReg, false);
  MI.getOperand(OpNo + 1).ChangeToImmediate(Offset);
}

// This function eliminate ADJCALLSTACKDOWN,
// ADJCALLSTACKUP pseudo instructions
void Nios2RegisterInfo::
eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator I) const {
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();

  if (!TFI->hasReservedCallFrame(MF)) {
    int64_t Amount = I->getOperand(0).getImm();

    if (I->getOpcode() == Nios2::ADJCALLSTACKDOWN)
      Amount = -Amount;

    const Nios2InstrInfo &TII =
      *static_cast<const Nios2InstrInfo *>(
        MBB.getParent()->getSubtarget().getInstrInfo());

    TII.adjustStackPtr(Nios2::SP, Amount, MBB, I);
  }

  MBB.erase(I);
}

unsigned Nios2RegisterInfo::
getFrameRegister(const MachineFunction &MF) const {
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
  return TFI->hasFP(MF) ? Nios2::FP : Nios2::SP;
}

unsigned Nios2RegisterInfo::
getEHExceptionRegister() const {
  llvm_unreachable("What is the exception register");
}

unsigned Nios2RegisterInfo::
getEHHandlerRegister() const {
  llvm_unreachable("What is the exception handler register");
}
