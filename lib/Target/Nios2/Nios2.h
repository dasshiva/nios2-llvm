//===-- Nios2.h - Top-level interface for Nios2 representation ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in
// the LLVM Nios2 back-end.
//
//===----------------------------------------------------------------------===//

#ifndef TARGET_NIOS2_H
#define TARGET_NIOS2_H

#include "MCTargetDesc/Nios2MCTargetDesc.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {
  class Nios2TargetMachine;
  class FunctionPass;

  FunctionPass *createNios2ISelDag(Nios2TargetMachine &TM);
} // end namespace llvm;

#endif
