//===- IntrinsicsNios2.td - Defines Mips intrinsics ---------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines all of the Nios2-specific intrinsics.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//

let TargetPrefix = "nios2" in {  // All intrinsics start with "llvm.mips.".

// Read control reg: numreg -> int
def int_nios2_rdctl : GCCBuiltin<"__builtin_nios2_rdctl">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty]>;
// Write control reg: numreg -> val -> void
def int_nios2_wrctl : GCCBuiltin<"__builtin_nios2_wrctl">,
  Intrinsic<[], [llvm_i32_ty, llvm_i32_ty]>;

// IO instructions
def int_nios2_ldbio: GCCBuiltin<"__builtin_nios2_ldbio">,
  Intrinsic<[llvm_i8_ty], [llvm_anyptr_ty]>;
def int_nios2_ldbuio: GCCBuiltin<"__builtin_nios2_ldbuio">,
  Intrinsic<[llvm_i8_ty], [llvm_anyptr_ty]>;
def int_nios2_ldhio: GCCBuiltin<"__builtin_nios2_ldhio">,
  Intrinsic<[llvm_i16_ty], [llvm_anyptr_ty]>;
def int_nios2_ldhuio: GCCBuiltin<"__builtin_nios2_ldhuio">,
  Intrinsic<[llvm_i16_ty], [llvm_anyptr_ty]>;
def int_nios2_ldwio: GCCBuiltin<"__builtin_nios2_ldwio">,
  Intrinsic<[llvm_i32_ty], [llvm_anyptr_ty]>;
def int_nios2_ldwuio: GCCBuiltin<"__builtin_nios2_ldwuio">,
  Intrinsic<[llvm_i32_ty], [llvm_anyptr_ty]>;

def int_nios2_stbio: GCCBuiltin<"__builtin_nios2_stbio">,
  Intrinsic<[], [llvm_i8_ty, llvm_anyptr_ty]>;
def int_nios2_sthio: GCCBuiltin<"__builtin_nios2_sthio">,
  Intrinsic<[], [llvm_i16_ty, llvm_anyptr_ty]>;
def int_nios2_stwio: GCCBuiltin<"__builtin_nios2_stwio">,
  Intrinsic<[], [llvm_i32_ty, llvm_anyptr_ty]>;

// Barrier
def int_nios2_sync: GCCBuiltin<"__builtin_nios2_sync">,
  Intrinsic<[], [llvm_i32_ty]>;

}
