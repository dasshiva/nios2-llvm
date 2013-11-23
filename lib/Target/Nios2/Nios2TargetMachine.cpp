//===-- Nios2TargetMachine.cpp - Define TargetMachine for Nios2 -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implements the info about Nios2 target spec.
//
//===----------------------------------------------------------------------===//

#include "Nios2TargetMachine.h"
#include "Nios2.h"
#include "Nios2FrameLowering.h"
#include "Nios2InstrInfo.h"
#include "llvm/PassManager.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/TargetRegistry.h"
using namespace llvm;

extern "C" void LLVMInitializeNios2Target() {
  // Register the target.
  RegisterTargetMachine<Nios2StdTargetMachine> X(TheNios2StdTarget);
}

Nios2TargetMachine::
Nios2TargetMachine(const Target &T, StringRef TT,
                  StringRef CPU, StringRef FS, const TargetOptions &Options,
                  Reloc::Model RM, CodeModel::Model CM,
                  CodeGenOpt::Level OL)
  : LLVMTargetMachine(T, TT, CPU, FS, Options, RM, CM, OL),
    Subtarget(TT, CPU, FS, true, RM),
    Layout("e-p:32:32:32-i8:8:32-i16:16:32-n32"),
    InstrInfo(new Nios2InstrInfo(*this)),
    FrameLowering(new Nios2FrameLowering(Subtarget)),
    TLInfo(*this), TSInfo(*this)  {
  initAsmInfo();
}

void Nios2StdTargetMachine::anchor() { }

Nios2StdTargetMachine::
Nios2StdTargetMachine(const Target &T, StringRef TT,
                    StringRef CPU, StringRef FS, const TargetOptions &Options,
                    Reloc::Model RM, CodeModel::Model CM,
                    CodeGenOpt::Level OL)
  : Nios2TargetMachine(T, TT, CPU, FS, Options, RM, CM, OL) {
}

namespace {
/// Nios2 Code Generator Pass Configuration Options.
class Nios2PassConfig : public TargetPassConfig {
public:
  Nios2PassConfig(Nios2TargetMachine *TM, PassManagerBase &PM)
    : TargetPassConfig(TM, PM) {}

  Nios2TargetMachine &getNios2TargetMachine() const {
    return getTM<Nios2TargetMachine>();
  }

  const Nios2Subtarget &getNios2Subtarget() const {
    return *getNios2TargetMachine().getSubtargetImpl();
  }

  virtual bool addInstSelector();
};
} // namespace

TargetPassConfig *Nios2TargetMachine::createPassConfig(PassManagerBase &PM) {
  return new Nios2PassConfig(this, PM);
}

// Install an instruction selector pass using
// the ISelDag to gen Nios2 code.
bool Nios2PassConfig::addInstSelector() {
  addPass(createNios2ISelDag(getNios2TargetMachine()));
  return false;
}

