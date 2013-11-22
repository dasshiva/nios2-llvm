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
class MCCodeEmitter;
class MCObjectWriter;
class MCAsmBackend;

extern Target TheNios2StdTarget;

MCObjectWriter *createNios2ELFObjectWriter(raw_ostream &OS,
                                                uint8_t OSABI,
                                                bool IsLittleEndian,
                                                bool Is64Bit);


MCCodeEmitter *createNios2MCCodeEmitter(const MCInstrInfo &MCII,
                                               const MCRegisterInfo &MRI,
                                               const MCSubtargetInfo &STI,
                                               MCContext &Ctx);

MCAsmBackend *createNios2AsmBackend(const Target &T, 
                                    const MCRegisterInfo &MRI,
                                    StringRef TT,
                                    StringRef CPU);

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

