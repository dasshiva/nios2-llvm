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
#include "llvm/Support/TargetRegistry.h"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "Nios2GenSubtargetInfo.inc"

using namespace llvm;

void Nios2Subtarget::anchor() { }

Nios2Subtarget::Nios2Subtarget(const std::string &TT, const std::string &CPU,
                             const std::string &FS, bool little,
                             Reloc::Model RM) :
  Nios2GenSubtargetInfo(TT, CPU, FS),
  Nios2ArchVersion(Nios2Std), Nios2ABI(UnknownABI), IsLittle(little)
{
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
  if (TT.find("linux") == std::string::npos)
    IsLinux = false;

  // Set UseSmallSection.
  UseSmallSection = !IsLinux && (RM == Reloc::Static);
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
