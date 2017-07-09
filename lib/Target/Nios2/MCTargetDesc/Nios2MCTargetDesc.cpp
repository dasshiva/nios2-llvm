//===-- Nios2MCTargetDesc.cpp - Nios2 Target Descriptions -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides Nios2 specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "Nios2MCAsmInfo.h"
#include "Nios2MCTargetDesc.h"
#include "InstPrinter/Nios2InstPrinter.h"
#include "llvm/MC/MachineLocation.h"
#include "llvm/MC/MCCodeGenInfo.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#include "Nios2GenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "Nios2GenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "Nios2GenRegisterInfo.inc"

static MCInstrInfo *createNios2MCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitNios2MCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createNios2MCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitNios2MCRegisterInfo(X, Nios2::RA, 0, 0, Nios2::PC);
  return X;
}

static MCSubtargetInfo *createNios2MCSubtargetInfo(const Triple &TT, 
                                                   StringRef CPU,
                                                   StringRef FS) {
  return createNios2MCSubtargetInfoImpl(TT, CPU, FS);
}

static MCAsmInfo *createNios2MCAsmInfo(const MCRegisterInfo &MRI, const Triple &TT) {
  MCAsmInfo *MAI = new Nios2MCAsmInfo(TT);

  unsigned SP = MRI.getDwarfRegNum(Nios2::SP, true);
  MCCFIInstruction Inst = MCCFIInstruction::createDefCfa(0, SP, 0);
  MAI->addInitialFrameState(Inst);

  return MAI;
}

static MCCodeGenInfo *createNios2MCCodeGenInfo(const Triple &TT, Reloc::Model RM,
                                               CodeModel::Model CM,
                                               CodeGenOpt::Level OL) {
  MCCodeGenInfo *X = new MCCodeGenInfo();
  //if (CM == CodeModel::JITDefault)
  //  RM = Reloc::Static;
  //else if (RM == Reloc::Default)
  //  RM = Reloc::PIC_;
  X->initMCCodeGenInfo(RM, CM, OL);
  return X;
}

static MCInstPrinter *createNios2MCInstPrinter(const Triple &T,
                                               unsigned SyntaxVariant,
                                               const MCAsmInfo &MAI,
                                               const MCInstrInfo &MII,
                                               const MCRegisterInfo &MRI) {
  return new Nios2InstPrinter(MAI, MII, MRI);
}

extern "C" void LLVMInitializeNios2TargetMC() {
  // Register the MC asm info.
  RegisterMCAsmInfoFn X(TheNios2StdTarget, createNios2MCAsmInfo);

  // Register the MC codegen info.
  TargetRegistry::RegisterMCCodeGenInfo(TheNios2StdTarget,
                                        createNios2MCCodeGenInfo);

  // Register the asm backend.
  TargetRegistry::RegisterMCAsmBackend(TheNios2StdTarget,
                                       createNios2AsmBackend);

  // Register the MC Code Emitter
  TargetRegistry::RegisterMCCodeEmitter(TheNios2StdTarget,
                                        createNios2MCCodeEmitter);

  // Register the MC instruction info.
  TargetRegistry::RegisterMCInstrInfo(TheNios2StdTarget, createNios2MCInstrInfo);

  // Register the MC register info.
  TargetRegistry::RegisterMCRegInfo(TheNios2StdTarget, createNios2MCRegisterInfo);

  // Register the MC subtarget info.
  TargetRegistry::RegisterMCSubtargetInfo(TheNios2StdTarget,
                                          createNios2MCSubtargetInfo);

  // Register the MCInstPrinter.
  TargetRegistry::RegisterMCInstPrinter(TheNios2StdTarget,
                                        createNios2MCInstPrinter);
}

