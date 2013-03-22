//===-- Nios2TargetInfo.cpp - Nios2 Target Implementation -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Nios2.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/TargetRegistry.h"
using namespace llvm;

Target llvm::TheNios2StdTarget;

extern "C" void LLVMInitializeNios2TargetInfo() {
  RegisterTarget<Triple::nios2,
        /*HasJIT=*/false> X(TheNios2StdTarget, "nios2", "Nios2");
}
