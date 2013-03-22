//===-- Nios2FixupKinds.h - Nios2 Specific Fixup Entries ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_NIOS2_NIOS2FIXUPKINDS_H
#define LLVM_NIOS2_NIOS2FIXUPKINDS_H

#include "llvm/MC/MCFixup.h"

namespace llvm {
namespace Nios2 {
  // Although most of the current fixup types reflect a unique relocation
  // one can have multiple fixup types for a given relocation and thus need
  // to be uniquely named.
  //
  // This table *must* be in the save order of
  // MCFixupKindInfo Infos[Nios2::NumTargetFixupKinds]
  // in Nios2AsmBackend.cpp.
  //
  enum Fixups {
    // Branch fixups resulting in R_NIOS2_16.
    fixup_Nios2_16 = FirstTargetFixupKind,

    // Pure 32 bit data fixup resulting in - R_NIOS2_32.
    fixup_Nios2_32,

    // Full 32 bit data relative data fixup resulting in - R_NIOS2_REL32.
    fixup_Nios2_REL32,

    // Jump 26 bit fixup resulting in - R_NIOS2_26.
    fixup_Nios2_26,

    // Pure upper 16 bit fixup resulting in - R_NIOS2_HI16.
    fixup_Nios2_HI16,

    // Pure lower 16 bit fixup resulting in - R_NIOS2_LO16.
    fixup_Nios2_LO16,

    // 16 bit fixup for GP offest resulting in - R_NIOS2_GPREL16.
    fixup_Nios2_GPREL16,

    // 16 bit literal fixup resulting in - R_NIOS2_LITERAL.
    fixup_Nios2_LITERAL,

    // Global symbol fixup resulting in - R_NIOS2_GOT16.
    fixup_Nios2_GOT_Global,

    // Local symbol fixup resulting in - R_NIOS2_GOT16.
    fixup_Nios2_GOT_Local,

    // PC relative branch fixup resulting in - R_NIOS2_PC16.
    fixup_Nios2_PC16,

    // resulting in - R_NIOS2_CALL16.
    fixup_Nios2_CALL16,

    // resulting in - R_NIOS2_GPREL32.
    fixup_Nios2_GPREL32,

    // resulting in - R_NIOS2_SHIFT5.
    fixup_Nios2_SHIFT5,

    // resulting in - R_NIOS2_SHIFT6.
    fixup_Nios2_SHIFT6,

    // Pure 64 bit data fixup resulting in - R_NIOS2_64.
    fixup_Nios2_64,

    // resulting in - R_NIOS2_TLS_GD.
    fixup_Nios2_TLSGD,

    // resulting in - R_NIOS2_TLS_GOTTPREL.
    fixup_Nios2_GOTTPREL,

    // resulting in - R_NIOS2_TLS_TPREL_HI16.
    fixup_Nios2_TPREL_HI,

    // resulting in - R_NIOS2_TLS_TPREL_LO16.
    fixup_Nios2_TPREL_LO,

    // resulting in - R_NIOS2_TLS_LDM.
    fixup_Nios2_TLSLDM,

    // resulting in - R_NIOS2_TLS_DTPREL_HI16.
    fixup_Nios2_DTPREL_HI,

    // resulting in - R_NIOS2_TLS_DTPREL_LO16.
    fixup_Nios2_DTPREL_LO,

    // PC relative branch fixup resulting in - R_NIOS2_PC16
    fixup_Nios2_Branch_PCRel,

    // resulting in - R_NIOS2_GPREL16/R_NIOS2_SUB/R_NIOS2_HI16
    fixup_Nios2_GPOFF_HI,

    // resulting in - R_NIOS2_GPREL16/R_NIOS2_SUB/R_NIOS2_LO16
    fixup_Nios2_GPOFF_LO,

    // resulting in - R_NIOS2_PAGE
    fixup_Nios2_GOT_PAGE,

    // resulting in - R_NIOS2_GOT_OFST
    fixup_Nios2_GOT_OFST,

    // resulting in - R_NIOS2_GOT_DISP
    fixup_Nios2_GOT_DISP,

    // resulting in - R_NIOS2_GOT_HIGHER
    fixup_Nios2_HIGHER,

    // resulting in - R_NIOS2_HIGHEST
    fixup_Nios2_HIGHEST,

    // resulting in - R_NIOS2_GOT_HI16
    fixup_Nios2_GOT_HI16,

    // resulting in - R_NIOS2_GOT_LO16
    fixup_Nios2_GOT_LO16,

    // resulting in - R_NIOS2_CALL_HI16
    fixup_Nios2_CALL_HI16,

    // resulting in - R_NIOS2_CALL_LO16
    fixup_Nios2_CALL_LO16,

    // Marker
    LastTargetFixupKind,
    NumTargetFixupKinds = LastTargetFixupKind - FirstTargetFixupKind
  };
} // namespace Nios2
} // namespace llvm


#endif // LLVM_NIOS2_NIOS2FIXUPKINDS_H
