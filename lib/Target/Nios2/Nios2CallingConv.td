//===-- Nios2CallingConv.td - Calling Conventions for Nios2 --*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// This describes the calling conventions for Nios2 architecture.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Nios2 Std Calling Convention
//===----------------------------------------------------------------------===//

// The rules for argument passing are defined in Nios2ISelLowering.cpp.
def RetCC_Nios2Std : CallingConv<[
  // i32 are returned in registers V0, V1, A0, A1
  CCIfType<[i32], CCAssignToReg<[R2, R3]> >
]>;

//===----------------------------------------------------------------------===//
// Nios2 Std
//===----------------------------------------------------------------------===//

def CC_Nios2Std : CallingConv<[
  // Promote i8/i16 arguments to i32.
  CCIfType<[i8, i16], CCPromoteToType<i32>>,

  // Integer arguments are passed in integer registers.
  CCIfType<[i32], CCAssignToReg<[R4, R5, R6, R7]>>,

  CCIfType<[i32, f32], CCAssignToStack<4, 4>>
]>;

//===----------------------------------------------------------------------===//
// Nios2 FastCC Calling Convention
//===----------------------------------------------------------------------===//

def CC_Nios2_FastCC : CallingConv<[
  // Promote i8/i16 arguments to i32.
  CCIfType<[i8, i16], CCPromoteToType<i32>>,

  // Integer arguments are passed in integer registers.
  CCIfType<[i32], CCAssignToReg<[R4, R5, R6, R7]>>,

  // Stack parameter slots for i32 is 32-bit words and 4-byte aligned.
  CCIfType<[i32], CCAssignToStack<4, 4>>
]>;

//===----------------------------------------------------------------------===//
// Nios2 Calling Convention Dispatch
//===----------------------------------------------------------------------===//

def CC_Nios2 : CallingConv<[
  CCDelegateTo<CC_Nios2Std>
]>;

def RetCC_Nios2 : CallingConv<[
  CCDelegateTo<RetCC_Nios2Std>
]>;

//===----------------------------------------------------------------------===//
// Callee-saved register lists.
//===----------------------------------------------------------------------===//

def CSR_STD : CalleeSavedRegs<(add (sequence "R%u", 16, 23))>;


