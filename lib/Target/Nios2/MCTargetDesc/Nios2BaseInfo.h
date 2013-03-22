//===-- Nios2BaseInfo.h - Top level definitions for NIOS2 MC ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains small standalone helper functions and enum definitions for
// the Nios2 target useful for the compiler back-end and the MC libraries.
//
//===----------------------------------------------------------------------===//
#ifndef NIOS2BASEINFO_H
#define NIOS2BASEINFO_H

#include "Nios2FixupKinds.h"
#include "Nios2MCTargetDesc.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/ErrorHandling.h"

namespace llvm {

/// Nios2II - This namespace holds all of the target specific flags that
/// instruction info tracks.
///
namespace Nios2II {
  /// Target Operand Flag enum.
  enum TOF {
    //===------------------------------------------------------------------===//
    // Nios2 Specific MachineOperand flags.

    MO_NO_FLAG,

    /// MO_GOT16 - Represents the offset into the global offset table at which
    /// the address the relocation entry symbol resides during execution.
    MO_GOT16,
    MO_GOT,

    /// MO_GOT_CALL - Represents the offset into the global offset table at
    /// which the address of a call site relocation entry symbol resides
    /// during execution. This is different from the above since this flag
    /// can only be present in call instructions.
    MO_GOT_CALL,

    /// MO_GPREL - Represents the offset from the current gp value to be used
    /// for the relocatable object file being produced.
    MO_GPREL,

    /// MO_ABS_HI/LO - Represents the hi or low part of an absolute symbol
    /// address.
    MO_ABS_HI,
    MO_ABS_LO,

    /// MO_TLSGD - Represents the offset into the global offset table at which
    // the module ID and TSL block offset reside during execution (General
    // Dynamic TLS).
    MO_TLSGD,

    /// MO_TLSLDM - Represents the offset into the global offset table at which
    // the module ID and TSL block offset reside during execution (Local
    // Dynamic TLS).
    MO_TLSLDM,
    MO_DTPREL_HI,
    MO_DTPREL_LO,

    /// MO_GOTTPREL - Represents the offset from the thread pointer (Initial
    // Exec TLS).
    MO_GOTTPREL,

    /// MO_TPREL_HI/LO - Represents the hi and low part of the offset from
    // the thread pointer (Local Exec TLS).
    MO_TPREL_HI,
    MO_TPREL_LO,

    // N32/64 Flags.
    MO_GPOFF_HI,
    MO_GPOFF_LO,
    MO_GOT_DISP,
    MO_GOT_PAGE,
    MO_GOT_OFST,

    /// MO_HIGHER/HIGHEST - Represents the highest or higher half word of a
    /// 64-bit symbol address.
    MO_HIGHER,
    MO_HIGHEST
  };

  enum {
    //===------------------------------------------------------------------===//
    // Instruction encodings.  These are the standard/most common forms for
    // Nios2 instructions.
    //

    // Pseudo - This represents an instruction that is a pseudo instruction
    // or one that has not been implemented yet.  It is illegal to code generate
    // it, but tolerated for intermediate implementation stages.
    Pseudo   = 0,

    /// FrmR - This form is for instructions of the format R.
    FrmR  = 1,
    /// FrmI - This form is for instructions of the format I.
    FrmI  = 2,
    /// FrmJ - This form is for instructions of the format J.
    FrmJ  = 3,
    /// FrmOther - This form is for instructions that have no specific format.
    FrmOther = 4,

    FormMask = 15
  };
}

/// getNios2RegisterNumbering - Given the enum value for some register,
/// return the number that it corresponds to.
inline static unsigned getNios2RegisterNumbering(unsigned RegEnum)
{
  switch (RegEnum) {
  case Nios2::ZERO:
    return 0;
  case Nios2::AT:
    return 1;
  case Nios2::R2:
    return 2;
  case Nios2::R3:
    return 3;
  case Nios2::R4:
    return 4;
  case Nios2::R5:
    return 5;
  case Nios2::R6:
    return 6;
  case Nios2::R7:
    return 7;
  case Nios2::R8:
    return 8;
  case Nios2::R9:
    return 9;
  case Nios2::R10:
    return 10;
  case Nios2::R11:
    return 11;
  case Nios2::R12:
    return 12;
  case Nios2::R13:
    return 13;
  case Nios2::R14:
    return 14;
  case Nios2::R15:
    return 15;
  case Nios2::R16:
    return 16;
  case Nios2::R17:
    return 17;
  case Nios2::R18:
    return 18;
  case Nios2::R19:
    return 19;
  case Nios2::R20:
    return 20;
  case Nios2::R21:
    return 21;
  case Nios2::R22:
    return 22;
  case Nios2::R23:
    return 23;
  case Nios2::ET:
    return 24;
  case Nios2::BT:
    return 25;
  case Nios2::GP:
    return 26;
  case Nios2::SP:
    return 27;
  case Nios2::FP:
    return 28;
  case Nios2::EA:
    return 29;
  case Nios2::BA:
    return 30;
  case Nios2::RA:
    return 31;
  default: llvm_unreachable("Unknown register number!");
  }
}

inline static std::pair<const MCSymbolRefExpr*, int64_t>
Nios2GetSymAndOffset(const MCFixup &Fixup) {
  MCFixupKind FixupKind = Fixup.getKind();

  if ((FixupKind < FirstTargetFixupKind) ||
      (FixupKind >= MCFixupKind(Nios2::LastTargetFixupKind)))
    return std::make_pair((const MCSymbolRefExpr*)0, (int64_t)0);

  const MCExpr *Expr = Fixup.getValue();
  MCExpr::ExprKind Kind = Expr->getKind();

  if (Kind == MCExpr::Binary) {
    const MCBinaryExpr *BE = static_cast<const MCBinaryExpr*>(Expr);
    const MCExpr *LHS = BE->getLHS();
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(BE->getRHS());

    if ((LHS->getKind() != MCExpr::SymbolRef) || !CE)
      return std::make_pair((const MCSymbolRefExpr*)0, (int64_t)0);

    return std::make_pair(cast<MCSymbolRefExpr>(LHS), CE->getValue());
  }

  if (Kind != MCExpr::SymbolRef)
    return std::make_pair((const MCSymbolRefExpr*)0, (int64_t)0);

  return std::make_pair(cast<MCSymbolRefExpr>(Expr), 0);
}

}

#endif
