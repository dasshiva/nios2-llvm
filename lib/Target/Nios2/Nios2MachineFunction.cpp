//===-- Nios2MachineFunctionInfo.cpp - Private data used for Nios2 ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Nios2MachineFunction.h"
#include "Nios2InstrInfo.h"
#include "Nios2Subtarget.h"
#include "MCTargetDesc/Nios2BaseInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

bool Nios2FunctionInfo::globalBaseRegSet() const {
  return GlobalBaseReg;
}

unsigned Nios2FunctionInfo::getGlobalBaseReg() {
  // Return if it has already been initialized.
  if (GlobalBaseReg)
    return GlobalBaseReg;

  const TargetRegisterClass *RC = &Nios2::CPURegsRegClass;
  return GlobalBaseReg = MF.getRegInfo().createVirtualRegister(RC);
}

void Nios2FunctionInfo::anchor() { }

