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

#ifndef NIOS2_FRAMEINFO_H
#define NIOS2_FRAMEINFO_H

#include "Nios2.h"
#include "Nios2Subtarget.h"
#include "llvm/Target/TargetFrameLowering.h"

namespace llvm {
class Nios2Subtarget;

class Nios2FrameLowering : public TargetFrameLowering {
protected:
  const Nios2Subtarget &STI;

public:
  explicit Nios2FrameLowering(const Nios2Subtarget &sti)
    : TargetFrameLowering(StackGrowsDown, 8, 0, 8), STI(sti) {}

  bool hasFP(const MachineFunction &MF) const;
};

} // End llvm namespace

#endif
