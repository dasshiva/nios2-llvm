//===-- Nios2Subtarget.cpp - Nios2 Subtarget Information --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Nios2 specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#include "Nios2Subtarget.h"
#include "Nios2.h"
#include "Nios2RegisterInfo.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/Support/TargetRegistry.h"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "Nios2GenSubtargetInfo.inc"

using namespace llvm;

void Nios2Subtarget::anchor() { }

Nios2Subtarget::Nios2Subtarget(const std::string &TT, const std::string &CPU,
                             const std::string &FS, const TargetOptions &Options) :
  Nios2GenSubtargetInfo(TT, CPU, FS),
  Nios2ArchVersion(Nios2Std),
  Nios2ABI(UnknownABI),
  TargetTriple(TT),
  Options(Options)
{
  initializeEnvironment();
  resetSubtargetFeatures(CPU, FS);
}

void Nios2Subtarget::resetSubtargetFeatures(StringRef CPU, StringRef FS) {
  std::string CPUName = CPU;
  if (CPUName.empty())
    CPUName = "nios2";

  // Parse features string.
  ParseSubtargetFeatures(CPUName, FS);

  // Initialize scheduling itinerary for the specified CPU.
  InstrItins = getInstrItineraryForCPU(CPUName);

  // Set Nios2ABI if it hasn't been set yet.
  if (Nios2ABI == UnknownABI)
    Nios2ABI = O32;

  // Is the target system Linux ?
  if (TargetTriple.getOS() != Triple::Linux)
    IsLinux = false;

  // Set UseSmallSection.
  // UseSmallSection = !IsLinux && (RM == Reloc::Static);
}

void Nios2Subtarget::resetSubtargetFeatures(const MachineFunction *MF) {
  AttributeSet FnAttrs = MF->getFunction()->getAttributes();
  Attribute CPUAttr = FnAttrs.getAttribute(AttributeSet::FunctionIndex,
                                           "target-cpu");
  Attribute FSAttr = FnAttrs.getAttribute(AttributeSet::FunctionIndex,
                                          "target-features");
  std::string CPU =
    !CPUAttr.hasAttribute(Attribute::None) ? CPUAttr.getValueAsString() : "";
  std::string FS =
    !FSAttr.hasAttribute(Attribute::None) ? FSAttr.getValueAsString() : "";
  if (!FS.empty()) {
    initializeEnvironment();
    resetSubtargetFeatures(CPU, FS);
  }
}

void Nios2Subtarget::initializeEnvironment() {
  HasHWMul = false;
  HasHWDiv = false;
}

bool
Nios2Subtarget::enablePostRAScheduler(CodeGenOpt::Level OptLevel,
                                    TargetSubtargetInfo::AntiDepBreakMode &Mode,
                                     RegClassVector &CriticalPathRCs) const {
  Mode = TargetSubtargetInfo::ANTIDEP_NONE;
  CriticalPathRCs.clear();
  CriticalPathRCs.push_back(&Nios2::CPURegsRegClass);
  return OptLevel >= CodeGenOpt::Aggressive;
}
