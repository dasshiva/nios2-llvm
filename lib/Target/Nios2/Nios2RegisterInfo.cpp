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

#define DEBUG_TYPE "nios2-reg-info"

#include "Nios2RegisterInfo.h"
#include "Nios2.h"
#include "Nios2InstrInfo.h"
#include "Nios2Subtarget.h"
#include "Nios2MachineFunction.h"
#include "llvm/Constants.h"
#include "llvm/DebugInfo.h"
#include "llvm/Type.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/Target/TargetFrameLowering.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"

#define GET_REGINFO_TARGET_DESC
#include "Nios2GenRegisterInfo.inc"

using namespace llvm;

Nios2RegisterInfo::Nios2RegisterInfo(const Nios2Subtarget &ST)
  : Nios2GenRegisterInfo(Nios2::RA), Subtarget(ST) {}

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
    Nios2::SP, Nios2::EA, Nios2::BA, Nios2::RA
  };

  BitVector Reserved(getNumRegs());
  typedef TargetRegisterClass::const_iterator RegIter;

  for (unsigned I = 0; I < array_lengthof(ReservedCPURegs); ++I)
    Reserved.set(ReservedCPURegs[I]);

  // Reserve FP if this function should have a dedicated frame pointer register.
  if (MF.getTarget().getFrameLowering()->hasFP(MF))
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
                    RegScavenger *RS) const {
  MachineInstr &MI = *II;
  MachineFunction &MF = *MI.getParent()->getParent();

  unsigned i = 0;
  while (!MI.getOperand(i).isFI()) {
    ++i;
    assert(i < MI.getNumOperands() &&
           "Instr doesn't have FrameIndex operand!");
  }

  DEBUG(errs() << "\nFunction : " << MF.getName() << "\n";
        errs() << "<--------->\n" << MI);

  int FrameIndex = MI.getOperand(i).getIndex();
  uint64_t stackSize = MF.getFrameInfo()->getStackSize();
  int64_t spOffset = MF.getFrameInfo()->getObjectOffset(FrameIndex);

  DEBUG(errs() << "FrameIndex : " << FrameIndex << "\n"
               << "spOffset   : " << spOffset << "\n"
               << "stackSize  : " << stackSize << "\n");

  eliminateFI(MI, i, FrameIndex, stackSize, spOffset);
}

void Nios2RegisterInfo::eliminateFI(MachineBasicBlock::iterator II,
                                     unsigned OpNo, int FrameIndex,
                                     uint64_t StackSize,
                                     int64_t SPOffset) const {
  MachineInstr &MI = *II;
  MachineFunction &MF = *MI.getParent()->getParent();
  MachineFrameInfo *MFI = MF.getFrameInfo();
  MipsFunctionInfo *Nios2FI = MF.getInfo<Nios2FunctionInfo>();

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
    unsigned ADDu = Mips::ADD;
    unsigned ATReg = Mips::AT;
    unsigned NewImm;

    Nios2FI->setEmitNOAT();
    unsigned Reg = TII.loadImmediate(Offset, MBB, II, DL, &NewImm);
    BuildMI(MBB, II, DL, TII.get(ADD), ATReg).addReg(FrameReg).addReg(Reg);

    FrameReg = ATReg;
    Offset = SignExtend64<16>(NewImm);
  }

  MI.getOperand(OpNo).ChangeToRegister(FrameReg, false);
  MI.getOperand(OpNo + 1).ChangeToImmediate(Offset);
}

unsigned Nios2RegisterInfo::
getFrameRegister(const MachineFunction &MF) const {
  const TargetFrameLowering *TFI = MF.getTarget().getFrameLowering();
  bool IsN64 = Subtarget.isABI_N64();

  return TFI->hasFP(MF) ? (IsN64 ? Nios2::FP_64 : Nios2::FP) :
                          (IsN64 ? Nios2::SP_64 : Nios2::SP);
}

unsigned Nios2RegisterInfo::
getEHExceptionRegister() const {
  llvm_unreachable("What is the exception register");
}

unsigned Nios2RegisterInfo::
getEHHandlerRegister() const {
  llvm_unreachable("What is the exception handler register");
}
