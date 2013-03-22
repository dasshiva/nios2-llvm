//===-- MipsRelocations.h - Mips Code Relocations ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the Nios2 target-specific relocation types
// (for relocation-model=static).
//
//===----------------------------------------------------------------------===//

#ifndef NIOS2RELOCATIONS_H_
#define NIOS2RELOCATIONS_H_

#include "llvm/CodeGen/MachineRelocation.h"

namespace llvm {
  namespace Nios2{
    enum RelocationType {
      // reloc_nios2_pc16 - pc relative relocation for branches. The lower 18
      // bits of the difference between the branch target and the branch
      // instruction, shifted right by 2.
      reloc_nios2_pc16 = 1,

      // reloc_nios2_hi - upper 16 bits of the address (modified by +1 if the
      // lower 16 bits of the address is negative).
      reloc_nios2_hi = 2,

      // reloc_nios2_lo - lower 16 bits of the address.
      reloc_nios2_lo = 3,

      // reloc_nios2_26 - lower 28 bits of the address, shifted right by 2.
      reloc_nios2_26 = 4
    };
  }
}

#endif /* NIOS2RELOCATIONS_H_ */
