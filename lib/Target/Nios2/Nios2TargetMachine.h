//===-- Nios2TargetMachine.h - Define TargetMachine for Nios2 -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the Nios2 specific subclass of TargetMachine.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_NIOS2_NIOS2TARGETMACHINE_H
#define LLVM_LIB_TARGET_NIOS2_NIOS2TARGETMACHINE_H

#include "Nios2FrameLowering.h"
#include "Nios2InstrInfo.h"
#include "Nios2ISelLowering.h"
#include "Nios2SelectionDAGInfo.h"
#include "Nios2Subtarget.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetFrameLowering.h"

namespace llvm {
class formatted_raw_ostream;
class Nios2RegisterInfo;

class Nios2TargetMachine : public LLVMTargetMachine {
  std::unique_ptr<TargetLoweringObjectFile> TLOF;
  Nios2Subtarget Subtarget;

public:
  Nios2TargetMachine(const Target &T, const Triple &TT,
                    StringRef CPU, StringRef FS, const TargetOptions &Options,
                    Reloc::Model RM, CodeModel::Model CM,
                    CodeGenOpt::Level OL);

  ~Nios2TargetMachine() override { }
  
  const Nios2Subtarget *getSubtargetImpl() const { return &Subtarget; }
  const Nios2Subtarget *getSubtargetImpl(const Function &F) const override {
    return &Subtarget; 
  }

  // Pass Pipeline Configuration
  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;

  TargetLoweringObjectFile *getObjFileLowering() const override {
    return TLOF.get();
  }
};

/// Nios2StdTargetMachine - Nios2 "standard" version
///
class Nios2StdTargetMachine : public Nios2TargetMachine {
  virtual void anchor();
public:
  Nios2StdTargetMachine(const Target &T, const Triple &TT,
                        StringRef CPU, StringRef FS, const TargetOptions &Options,
                        Reloc::Model RM, CodeModel::Model CM,
                        CodeGenOpt::Level OL);
};


} // End llvm namespace

#endif
