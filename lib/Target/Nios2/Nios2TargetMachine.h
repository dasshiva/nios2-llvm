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

#ifndef NIOS2TARGETMACHINE_H
#define NIOS2TARGETMACHINE_H

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
  Nios2Subtarget       Subtarget;
  const DataLayout    Layout; // Calculates type size & alignment
  const Nios2InstrInfo *InstrInfo;
  const Nios2FrameLowering *FrameLowering;
  Nios2TargetLowering  TLInfo;
  Nios2SelectionDAGInfo TSInfo;

public:
  Nios2TargetMachine(const Target &T, StringRef TT,
                    StringRef CPU, StringRef FS, const TargetOptions &Options,
                    Reloc::Model RM, CodeModel::Model CM,
                    CodeGenOpt::Level OL);

  virtual ~Nios2TargetMachine() { delete InstrInfo; }

  virtual const Nios2InstrInfo *getInstrInfo() const
  { return InstrInfo; }
  virtual const TargetFrameLowering *getFrameLowering() const
  { return FrameLowering; }
  virtual const Nios2Subtarget *getSubtargetImpl() const
  { return &Subtarget; }
  virtual const DataLayout *getDataLayout()    const
  { return &Layout;}

  virtual const Nios2RegisterInfo *getRegisterInfo()  const {
    return &InstrInfo->getRegisterInfo();
  }

  virtual const Nios2TargetLowering *getTargetLowering() const {
    return &TLInfo;
  }

  virtual const Nios2SelectionDAGInfo* getSelectionDAGInfo() const {
    return &TSInfo;
  }

  // Pass Pipeline Configuration
  virtual TargetPassConfig *createPassConfig(PassManagerBase &PM);
};

/// Nios2StdTargetMachine - Nios2 "standard" version
///
class Nios2StdTargetMachine : public Nios2TargetMachine {
  virtual void anchor();
public:
  Nios2StdTargetMachine(const Target &T, StringRef TT,
                      StringRef CPU, StringRef FS, const TargetOptions &Options,
                      Reloc::Model RM, CodeModel::Model CM,
                      CodeGenOpt::Level OL);
};


} // End llvm namespace

#endif
