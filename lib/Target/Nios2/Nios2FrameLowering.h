//===-- Nios2FrameLowering.h - Define frame lowering for Nios2 ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_NIOS2_NIOS2FRAMELOWERING_H
#define LLVM_LIB_TARGET_NIOS2_NIOS2FRAMELOWERING_H

#include "Nios2.h"
#include "llvm/Target/TargetFrameLowering.h"

namespace llvm {
class Nios2Subtarget;

class Nios2FrameLowering : public TargetFrameLowering {
protected:
  const Nios2Subtarget &STI;

public:
  explicit Nios2FrameLowering(const Nios2Subtarget &ST)
    : TargetFrameLowering(StackGrowsDown, 8, 0, 8), STI(ST) {}

  static const Nios2FrameLowering *create(const Nios2Subtarget &ST);

  bool hasFP(const MachineFunction &MF) const override;

  void eliminateCallFramePseudoInstr(MachineFunction &MF,
                                     MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator I) const override;

  /// emitProlog/emitEpilog - These methods insert prolog and epilog code into
  /// the function.
  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
};

} // End llvm namespace

#endif
