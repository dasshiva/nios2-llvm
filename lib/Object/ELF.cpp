///===- ELF.cpp - ELF object file implementation -----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/ELF.h"

namespace llvm {
namespace ELF {
	enum {
		R_NIOS2_NONE,
		R_NIOS2_S16,
		R_NIOS2_U16,
		R_NIOS2_PCREL16,
		R_NIOS2_CALL26,
		R_NIOS2_IMM5,
		R_NIOS2_CACHE_OPX,
		R_NIOS2_IMM6,
		R_NIOS2_IMM8,
		R_NIOS2_HI16,
		R_NIOS2_LO16,
		R_NIOS2_HIADJ16,
		R_NIOS2_BFD_ALLOC_32,
		R_NIOS2_BFD_ALLOC_16,
		R_NIOS2_BFD_ALLOC_8,
		R_NIOS2_GPREL,
		R_NIOS2_GNU_VTINHERIT,
		R_NIOS2_GNU_VTENTRY,
		R_NIOS2_UJMP,
		R_NIOS2_CJMP,
		R_NIOS2_CALLR,
		R_NIOS2_ALIGN,
		R_NIOS2_GOT16,
		R_NIOS2_CALL16,
		R_NIOS2_GOTOFF_LO,
		R_NIOS2_GOTOFF_HA,
		R_NIOS2_PCREL_LO,
		R_NIOS2_PCREL_HA,
		R_NIOS2_TLS_GD16,
		R_NIOS2_TLS_LDM16,
		R_NIOS2_TLS_LDO16,
		R_NIOS2_TLS_IE16,
		R_NIOS2_TLS_LE16,
		R_NIOS2_DTPMOD,
		R_NIOS2_DTPREL,
		R_NIOS2_TPREL,
		R_NIOS2_COPY,
		R_NIOS2_GLOB_DAT,
		R_NIOS2_JUMP_SLOT,
		R_NIOS2_RELATIVE,
		R_NIOS2_GOTOFF,
/*
ELF_RELOC(R_NIOS2_CALL26_NOAT,   41)
ELF_RELOC(R_NIOS2_GOT_LO,        42)
ELF_RELOC(R_NIOS2_GOT_HA,        43)
ELF_RELOC(R_NIOS2_CALL_LO,       44)
ELF_RELOC(R_NIOS2_CALL_HA,       45)
*/
	};
}
}
namespace llvm {
namespace object {

#define ELF_RELOC(name, value)                                          \
  case ELF::name:                                                       \
    return #name;                                                       \

StringRef getELFRelocationTypeName(uint32_t Machine, uint32_t Type) {
  switch (Machine) {
  case ELF::EM_X86_64:
    switch (Type) {
#include "llvm/Support/ELFRelocs/x86_64.def"
    default:
      break;
    }
    break;
  case ELF::EM_386:
  case ELF::EM_IAMCU:
    switch (Type) {
#include "llvm/Support/ELFRelocs/i386.def"
    default:
      break;
    }
    break;
  case ELF::EM_MIPS:
    switch (Type) {
#include "llvm/Support/ELFRelocs/Mips.def"
    default:
      break;
    }
    break;
  case ELF::EM_AARCH64:
    switch (Type) {
#include "llvm/Support/ELFRelocs/AArch64.def"
    default:
      break;
    }
    break;
  case ELF::EM_ARM:
    switch (Type) {
#include "llvm/Support/ELFRelocs/ARM.def"
    default:
      break;
    }
    break;
  case ELF::EM_HEXAGON:
    switch (Type) {
#include "llvm/Support/ELFRelocs/Hexagon.def"
    default:
      break;
    }
    break;
  case ELF::EM_PPC:
    switch (Type) {
#include "llvm/Support/ELFRelocs/PowerPC.def"
    default:
      break;
    }
    break;
  case ELF::EM_PPC64:
    switch (Type) {
#include "llvm/Support/ELFRelocs/PowerPC64.def"
    default:
      break;
    }
    break;
  case ELF::EM_S390:
    switch (Type) {
#include "llvm/Support/ELFRelocs/SystemZ.def"
    default:
      break;
    }
    break;
  case ELF::EM_SPARC:
  case ELF::EM_SPARC32PLUS:
  case ELF::EM_SPARCV9:
    switch (Type) {
#include "llvm/Support/ELFRelocs/Sparc.def"
    default:
      break;
    }
    break;
  case ELF::EM_WEBASSEMBLY:
    switch (Type) {
#include "llvm/Support/ELFRelocs/WebAssembly.def"
    default:
      break;
    }
    break;
  case ELF::EM_ALTERA_NIOS2:
#undef ELF_RELOC
#define ELF_RELOC(name)\
  case ELF::name:\
    return #name;
    switch (Type) {
	   ELF_RELOC(R_NIOS2_NONE)
	   ELF_RELOC(R_NIOS2_S16)
	   ELF_RELOC(R_NIOS2_U16)
	   ELF_RELOC(R_NIOS2_PCREL16)
	   ELF_RELOC(R_NIOS2_CALL26)
	   ELF_RELOC(R_NIOS2_IMM5)
	   ELF_RELOC(R_NIOS2_CACHE_OPX)
	   ELF_RELOC(R_NIOS2_IMM6)
	   ELF_RELOC(R_NIOS2_IMM8)
	   ELF_RELOC(R_NIOS2_HI16)
	   ELF_RELOC(R_NIOS2_LO16)
	   ELF_RELOC(R_NIOS2_HIADJ16)
	   ELF_RELOC(R_NIOS2_BFD_ALLOC_32)
	   ELF_RELOC(R_NIOS2_BFD_ALLOC_16)
	   ELF_RELOC(R_NIOS2_BFD_ALLOC_8)
	   ELF_RELOC(R_NIOS2_GPREL)
	   ELF_RELOC(R_NIOS2_GNU_VTINHERIT)
	   ELF_RELOC(R_NIOS2_GNU_VTENTRY)
	   ELF_RELOC(R_NIOS2_UJMP)
	   ELF_RELOC(R_NIOS2_CJMP)
	   ELF_RELOC(R_NIOS2_CALLR)
	   ELF_RELOC(R_NIOS2_ALIGN)
	   ELF_RELOC(R_NIOS2_GOT16)
	   ELF_RELOC(R_NIOS2_CALL16)
	   ELF_RELOC(R_NIOS2_GOTOFF_LO)
	   ELF_RELOC(R_NIOS2_GOTOFF_HA)
	   ELF_RELOC(R_NIOS2_PCREL_LO)
	   ELF_RELOC(R_NIOS2_PCREL_HA)
	   ELF_RELOC(R_NIOS2_TLS_GD16)
	   ELF_RELOC(R_NIOS2_TLS_LDM16)
	   ELF_RELOC(R_NIOS2_TLS_LDO16)
	   ELF_RELOC(R_NIOS2_TLS_IE16)
	   ELF_RELOC(R_NIOS2_TLS_LE16)
	   ELF_RELOC(R_NIOS2_DTPMOD)
	   ELF_RELOC(R_NIOS2_DTPREL)
	   ELF_RELOC(R_NIOS2_TPREL)
	   ELF_RELOC(R_NIOS2_COPY)
	   ELF_RELOC(R_NIOS2_GLOB_DAT)
	   ELF_RELOC(R_NIOS2_JUMP_SLOT)
	   ELF_RELOC(R_NIOS2_RELATIVE)
	   ELF_RELOC(R_NIOS2_GOTOFF)
 
    default:
      break;
    }

    break;
  default:
    break;
  }
  return "Unknown";
}

#undef ELF_RELOC

} // end namespace object
} // end namespace llvm
