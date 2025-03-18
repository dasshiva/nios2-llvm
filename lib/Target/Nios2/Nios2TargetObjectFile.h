//===-- llvm/Target/MipsTargetObjectFile.h - Mips Object Info ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_NIOS2_NIOS2TARGETOBJECTFILE_H
#define LLVM_LIB_TARGET_NIOS2_NIOS2TARGETOBJECTFILE_H

#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"

namespace llvm {
class Nios2TargetMachine;

class Nios2TargetObjectFile : public TargetLoweringObjectFileELF {
public:

  void Initialize(MCContext &Ctx, const TargetMachine &TM) override {
    TargetLoweringObjectFileELF::Initialize(Ctx, TM);
  }
};
} // end namespace llvm

#endif
