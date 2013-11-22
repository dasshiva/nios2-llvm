//===-- Nios2RegisterInfo.h - Nios2 Register Information Impl -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Nios2 implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef NIOS2REGISTERINFO_H
#define NIOS2REGISTERINFO_H

#include "Nios2.h"
#include "llvm/Target/TargetRegisterInfo.h"

#define GET_REGINFO_HEADER
#include "Nios2GenRegisterInfo.inc"

namespace llvm {
class Nios2Subtarget;
class Nios2InstrInfo;
class Type;

class Nios2RegisterInfo : public Nios2GenRegisterInfo {
protected:
  const Nios2Subtarget &Subtarget;
  const Nios2InstrInfo &TII;

public:
  Nios2RegisterInfo(const Nios2Subtarget &Subtarget,
      const Nios2InstrInfo &I);

  /// getRegisterNumbering - Given the enum value for some register, e.g.
  /// Nios2::RA, return the number that it corresponds to (e.g. 31).
  static unsigned getRegisterNumbering(unsigned RegEnum);

  /// Get PIC indirect call register
  static unsigned getPICCallReg();

  /// Adjust the Nios2 stack frame.
  void adjustNios2StackFrame(MachineFunction &MF) const;

  /// Code Generation virtual methods...
  const uint16_t *getCalleeSavedRegs(const MachineFunction *MF = 0) const;
  const uint32_t *getCallPreservedMask(CallingConv::ID) const;

  BitVector getReservedRegs(const MachineFunction &MF) const;

  virtual bool requiresRegisterScavenging(const MachineFunction &MF) const;

  virtual bool trackLivenessAfterRegAlloc(const MachineFunction &MF) const;

  /// Stack Frame Processing Methods
  virtual void eliminateFrameIndex(MachineBasicBlock::iterator II,
                           int SPAdj, unsigned FIOperandNum,
                           RegScavenger *RS = NULL) const;

  void processFunctionBeforeFrameFinalized(MachineFunction &MF) const;

  virtual void eliminateCallFramePseudoInstr(MachineFunction &MF,
      MachineBasicBlock &MBB, MachineBasicBlock::iterator I) const;

  /// Debug information queries.
  unsigned getFrameRegister(const MachineFunction &MF) const;

  /// Exception handling queries.
  unsigned getEHExceptionRegister() const;
  unsigned getEHHandlerRegister() const;

private:
  virtual void eliminateFI(MachineBasicBlock::iterator II, unsigned OpNo,
                           int FrameIndex, uint64_t StackSize,
                           int64_t SPOffset) const;
};

} // end namespace llvm

#endif
