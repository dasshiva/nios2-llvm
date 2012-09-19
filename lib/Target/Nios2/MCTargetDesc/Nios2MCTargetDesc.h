//===-- Nios2MCTargetDesc.h - Nios2 Target Descriptions -----------*- C++ -*-===//
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

#ifndef NIOS2MCTARGETDESC_H
#define NIOS2MCTARGETDESC_H

#include "llvm/Support/DataTypes.h"
#include "llvm/Support/TargetRegistry.h"

namespace llvm {
extern Target TheNios2Target;
}

// Defines symbolic names for Nios2 registers.  This defines a mapping from
// register name to register number.
#define GET_REGINFO_ENUM
#include "Nios2GenRegisterInfo.inc"

// Defines symbolic names for the Nios2 instructions.
#define GET_INSTRINFO_ENUM
#include "Nios2GenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "Nios2GenSubtargetInfo.inc"

#endif
