//===- Nios2InstrInfo.td - Target Description for Nios2 Target -*- tablegen -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Nios2 implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

//===----------------------------------
// Nios2 Instruction predicates
//===----------------------------------
def RelocStatic : Predicate<"TM.getRelocationModel() == Reloc::Static">;


//===----------------------------------------------------------------------===//
// Nios2 profiles and nodes
//===----------------------------------------------------------------------===//

def SDT_Nios2JmpLink      : SDTypeProfile<0, 1, [SDTCisVT<0, iPTR>]>;
def SDT_Nios2CMov         : SDTypeProfile<1, 4, [SDTCisSameAs<0, 1>,
                                                SDTCisSameAs<1, 2>,
                                                SDTCisSameAs<3, 4>,
                                                SDTCisInt<4>]>;
def SDT_Nios2CallSeqStart : SDCallSeqStart<[SDTCisVT<0, i32>]>;
def SDT_Nios2CallSeqEnd   : SDCallSeqEnd<[SDTCisVT<0, i32>, SDTCisVT<1, i32>]>;
def SDT_Nios2MAddMSub     : SDTypeProfile<0, 4,
                                         [SDTCisVT<0, i32>, SDTCisSameAs<0, 1>,
                                          SDTCisSameAs<1, 2>,
                                          SDTCisSameAs<2, 3>]>;
def SDT_Nios2DivRem       : SDTypeProfile<0, 2,
                                         [SDTCisInt<0>,
                                          SDTCisSameAs<0, 1>]>;

def SDT_Nios2ThreadPointer : SDTypeProfile<1, 0, [SDTCisPtrTy<0>]>;

def SDT_Nios2DynAlloc    : SDTypeProfile<1, 1, [SDTCisVT<0, iPTR>,
                                               SDTCisSameAs<0, 1>]>;
def SDT_Sync             : SDTypeProfile<0, 1, [SDTCisVT<0, i32>]>;

def SDT_Ext : SDTypeProfile<1, 3, [SDTCisInt<0>, SDTCisSameAs<0, 1>,
                                   SDTCisVT<2, i32>, SDTCisSameAs<2, 3>]>;
def SDT_Ins : SDTypeProfile<1, 4, [SDTCisInt<0>, SDTCisSameAs<0, 1>,
                                   SDTCisVT<2, i32>, SDTCisSameAs<2, 3>,
                                   SDTCisSameAs<0, 4>]>;

def SDTNios2LoadLR  : SDTypeProfile<1, 2,
                                   [SDTCisInt<0>, SDTCisPtrTy<1>,
                                    SDTCisSameAs<0, 2>]>;

// Call
def Nios2JmpLink : SDNode<"Nios2ISD::JmpLink",SDT_Nios2JmpLink,
                         [SDNPHasChain, SDNPOutGlue, SDNPOptInGlue,
                          SDNPVariadic]>;

// Hi and Lo nodes are used to handle global addresses. Used on
// Nios2ISelLowering to lower stuff like GlobalAddress, ExternalSymbol
// static model. (nothing to do with Nios2 Registers Hi and Lo)
def Nios2Hi    : SDNode<"Nios2ISD::Hi", SDTIntUnaryOp>;
def Nios2Lo    : SDNode<"Nios2ISD::Lo", SDTIntUnaryOp>;
def Nios2GPRel : SDNode<"Nios2ISD::GPRel", SDTIntUnaryOp>;

// Return
def Nios2Ret : SDNode<"Nios2ISD::Ret", SDTNone,
    [SDNPHasChain, SDNPOptInGlue,SDNPVariadic]>;

// Select
def Nios2Select : SDNode<"Nios2ISD::Select", SDTSelect>;

// These are target-independent nodes, but have target-specific formats.
def callseq_start : SDNode<"ISD::CALLSEQ_START", SDT_Nios2CallSeqStart,
                           [SDNPHasChain, SDNPSideEffect, SDNPOutGlue]>;
def callseq_end   : SDNode<"ISD::CALLSEQ_END", SDT_Nios2CallSeqEnd,
                           [SDNPHasChain, SDNPSideEffect,
                            SDNPOptInGlue, SDNPOutGlue]>;

// Target constant nodes that are not part of any isel patterns and remain
// unchanged can cause instructions with illegal operands to be emitted.
// Wrapper node patterns give the instruction selector a chance to replace
// target constant nodes that would otherwise remain unchanged with ADDiu
// nodes. Without these wrapper node patterns, the following conditional move
// instrucion is emitted when function cmov2 in test/CodeGen/Nios2/cmov.ll is
// compiled:
//  movn  %got(d)($gp), %got(c)($gp), $4
// This instruction is illegal since movn can take only register operands.

def Nios2Wrapper    : SDNode<"Nios2ISD::Wrapper", SDTIntBinOp>;

def Nios2Sync : SDNode<"Nios2ISD::Sync", SDTNone, [SDNPHasChain,SDNPSideEffect]>;

class Nios2Pat<dag pattern, dag result> : Pat<pattern, result> {
}

//===----------------------------------------------------------------------===//
// Instruction format superclass
//===----------------------------------------------------------------------===//

include "Nios2InstrFormats.td"

//===----------------------------------------------------------------------===//
// Nios2 Operand, Complex Patterns and Transformations Definitions.
//===----------------------------------------------------------------------===//

// Instruction operand types
def jmptarget   : Operand<OtherVT> {
  let EncoderMethod = "getJumpTargetOpValue";
}
def brtarget    : Operand<OtherVT> {
  let EncoderMethod = "getBranchTargetOpValue";
  let OperandType = "OPERAND_PCREL";
  let DecoderMethod = "DecodeBranchTarget";
}
def calltarget  : Operand<iPTR> {
  let EncoderMethod = "getJumpTargetOpValue";
}
def calltarget64: Operand<i64>;
def simm16      : Operand<i32> {
  let DecoderMethod= "DecodeSimm16";
}
def simm16_64   : Operand<i64>;
def shamt       : Operand<i32>;

// Unsigned Operand
def uimm16      : Operand<i32> {
  let PrintMethod = "printUnsignedImm";
}

// Address operand
def mem : Operand<i32> {
  let PrintMethod = "printMemOperand";
  let MIOperandInfo = (ops CPURegs, simm16);
}

def mem_ea : Operand<i32> {
  let PrintMethod = "printMemOperandEA";
  let MIOperandInfo = (ops CPURegs, simm16);
}

// size operand of ext instruction
def size_ext : Operand<i32> {
  let EncoderMethod = "getSizeExtEncoding";
  let DecoderMethod = "DecodeExtSize";
}

// size operand of ins instruction
def size_ins : Operand<i32> {
  let EncoderMethod = "getSizeInsEncoding";
  let DecoderMethod = "DecodeInsSize";
}

// Transformation Function - get the lower 16 bits.
def LO16 : SDNodeXForm<imm, [{
  return getImm(N, N->getZExtValue() & 0xFFFF);
}]>;

// Transformation Function - get the higher 16 bits.
def HI16 : SDNodeXForm<imm, [{
  return getImm(N, (N->getZExtValue() >> 16) & 0xFFFF);
}]>;

// Transformation Function - get the negative number
def NEG16 : SDNodeXForm<imm, [{
  return getImm(N, -N->getSExtValue() );
}]>;

// Transformation Function - get the immediate + 1
def IMMPLUS1 : SDNodeXForm<imm, [{
  return getImm(N, N->getSExtValue()+1);
}]>;

// Map immediates to control registers
def ICTLREG : SDNodeXForm<imm, [{
  return getRegister(Nios2::CTL0 + N->getZExtValue(), MVT::i32);
}]>;

// Node immediate fits as 16-bit sign extended on target immediate.
// e.g. addi, andi
def immSExt16  : PatLeaf<(imm), [{ return isInt<16>(N->getSExtValue()); }]>;

// Node immediate fits as 16-bit zero extended on target immediate.
// The LO16 param means that only the lower 16 bits of the node
// immediate are caught.
// e.g. addiu, sltiu
def immZExt16  : PatLeaf<(imm), [{
  if (N->getValueType(0) == MVT::i32)
    return (uint32_t)N->getZExtValue() == (unsigned short)N->getZExtValue();
  else
    return (uint64_t)N->getZExtValue() == (unsigned short)N->getZExtValue();
}], LO16>;

// Immediate can be loaded with LUi (32-bit int with lower 16-bit cleared).
def immLow16Zero : PatLeaf<(imm), [{
  int64_t Val = N->getSExtValue();
  return isInt<32>(Val) && !(Val & 0xffff);
}]>;

// shamt field must fit in 5 bits.
def immZExt5 : ImmLeaf<i32, [{return Imm == (Imm & 0x1f);}]>;

// Nios2 Address Mode! SDNode frameindex could possibily be a match
// since load and store instructions from stack used it.
def addr :
  ComplexPattern<iPTR, 2, "SelectAddr", [frameindex], [SDNPWantParent]>;

//===----------------------------------------------------------------------===//
// Instructions specific format
//===----------------------------------------------------------------------===//

// Arithmetic and logical instructions with 3 register operands.
class ArithLogicR<bits<6> op, bits<6> func, string instr_asm, SDNode OpNode,
                  InstrItinClass itin, RegisterClass RC, bit isComm = 0>:
  FR<op, func, 0, (outs RC:$rC), (ins RC:$rA, RC:$rB),
     !strconcat(instr_asm, "\t$rC, $rA, $rB"),
     [(set RC:$rC, (OpNode RC:$rA, RC:$rB))], itin> {
  let isCommutable = isComm;
  let isReMaterializable = 1;
}

class ArithOverflowR<bits<6> op, bits<6> func, string instr_asm,
                    InstrItinClass itin, RegisterClass RC, bit isComm = 0>:
  FR<op, func, 0, (outs RC:$rC), (ins RC:$rA, RC:$rB),
     !strconcat(instr_asm, "\t$rC, $rA, $rB"), [], itin> {
  let isCommutable = isComm;
}

// Arithmetic and logical instructions with 2 register operands.
class ArithLogicI<bits<6> op, string instr_asm, SDNode OpNode,
                  Operand Od, PatLeaf imm_type, RegisterClass RC> :
  FI<op, (outs RC:$rB), (ins RC:$rA, Od:$imm16),
     !strconcat(instr_asm, "\t$rB, $rA, $imm16"),
     [(set RC:$rB, (OpNode RC:$rA, imm_type:$imm16))], IIAlu> {
  let isReMaterializable = 1;
}

class ArithOverflowI<bits<6> op, string instr_asm, SDNode OpNode,
                     Operand Od, PatLeaf imm_type, RegisterClass RC> :
  FI<op, (outs RC:$rB), (ins RC:$rA, Od:$imm16),
     !strconcat(instr_asm, "\t$rB, $rA, $imm16"), [], IIAlu>;

//  Logical
class LogicNOR<bits<6> op, bits<6> func, string instr_asm, RegisterClass RC>:
  FR<op, func, 0, (outs RC:$rC), (ins RC:$rA, RC:$rB),
     !strconcat(instr_asm, "\t$rC, $rA, $rB"),
     [(set RC:$rC, (not (or RC:$rA, RC:$rB)))], IIAlu> {
  let isCommutable = 1;
}

// Shifts
class shift_rotate_imm<bits<6> func, bits<5> isRotate, string instr_asm,
                       SDNode OpNode, PatFrag PF, Operand ImmOpnd,
                       RegisterClass RC>:
  FR<0x3a, func, isRotate, (outs RC:$rC), (ins RC:$rA, ImmOpnd:$shamt),
     !strconcat(instr_asm, "\t$rC, $rA, $shamt"),
     [(set RC:$rC, (OpNode RC:$rA, PF:$shamt))], IIAlu> {
  let rB = 0;
  let shamt = isRotate;
}

// 32-bit shift instructions.
class shift_rotate_imm32<bits<6> func, bits<5> isRotate, string instr_asm,
                         SDNode OpNode>:
  shift_rotate_imm<func, isRotate, instr_asm, OpNode, immZExt5, shamt, CPURegs>;

class shift_rotate_reg<bits<6> func, bits<5> isRotate, string instr_asm,
                       SDNode OpNode, RegisterClass RC>:
  FR<0x3a, func, 0, (outs RC:$rC), (ins CPURegs:$rA, RC:$rB),
     !strconcat(instr_asm, "\t$rC, $rA, $rB"),
     [(set RC:$rC, (OpNode RC:$rA, CPURegs:$rB))], IIAlu> {
  let shamt = isRotate;
}

// Memory Load/Store
let canFoldAsLoad = 1 in
class LoadM<bits<6> op, string instr_asm, PatFrag OpNode, RegisterClass RC,
            Operand MemOpnd, bit Pseudo>:
  FI<op, (outs RC:$rB), (ins MemOpnd:$addr),
     !strconcat(instr_asm, "\t$rB, $addr"),
     [(set RC:$rB, (OpNode addr:$addr))], IILoad> {
  let isPseudo = Pseudo;
}

class StoreM<bits<6> op, string instr_asm, PatFrag OpNode, RegisterClass RC,
             Operand MemOpnd, bit Pseudo>:
  FI<op, (outs), (ins RC:$rB, MemOpnd:$addr),
     !strconcat(instr_asm, "\t$rB, $addr"),
     [(OpNode RC:$rB, addr:$addr)], IIStore> {
  let isPseudo = Pseudo;
}

// 32-bit load.
class LoadM32<bits<6> op, string instr_asm, PatFrag OpNode, bit Pseudo = 0>:
  LoadM<op, instr_asm, OpNode, CPURegs, mem, Pseudo>;

// 32-bit store.
class StoreM32<bits<6> op, string instr_asm, PatFrag OpNode,
           bit Pseudo = 0>:
           StoreM<op, instr_asm, OpNode, CPURegs, mem, Pseudo>;

// Conditional Branch
class CBranch<bits<6> op, string instr_asm, PatFrag cond_op, RegisterClass RC>:
  FI<op, (outs), (ins RC:$rA, RC:$rB, brtarget:$imm16),
             !strconcat(instr_asm, "\t$rA, $rB, $imm16"),
             [(brcond (i32 (cond_op RC:$rA, RC:$rB)), bb:$imm16)], IIBranch> {
  let isBranch = 1;
  let isTerminator = 1;
  let hasDelaySlot = 1;
  let Defs = [PC];
}

// Pseudo Conditional Branch
class CBranchPseudo<string instr_asm, PatFrag cond_op, RegisterClass RC>:
  Nios2Pseudo<(outs), (ins RC:$rA, RC:$rB, brtarget:$imm16),
             !strconcat(instr_asm, "\t$rA, $rB, $imm16"),
             [(brcond (i32 (cond_op RC:$rA, RC:$rB)), bb:$imm16)], IIBranch> {
  let isBranch = 1;
  let isTerminator = 1;
  let hasDelaySlot = 1;
  let Defs = [PC];
}

// SetCC
class SetCC_R<bits<6> op, bits<6> func, string instr_asm, PatFrag cond_op,
              RegisterClass RC>:
  FR<op, func, 0, (outs CPURegs:$rC), (ins RC:$rA, RC:$rB),
     !strconcat(instr_asm, "\t$rC, $rA, $rB"),
     [(set CPURegs:$rC, (cond_op RC:$rA, RC:$rB))],
     IIAlu>;

class SetCC_I<bits<6> op, string instr_asm, PatFrag cond_op, Operand Od,
              PatLeaf imm_type, RegisterClass RC>:
  FI<op, (outs CPURegs:$rB), (ins RC:$rA, Od:$imm16),
     !strconcat(instr_asm, "\t$rB, $rA, $imm16"),
     [(set CPURegs:$rB, (cond_op RC:$rA, imm_type:$imm16))],
     IIAlu>;

// Signed compares
multiclass CompareS<bits<6> opc, string cmpstr, PatFrag setop> {
  def #NAME#  : SetCC_R<0x3a, opc, cmpstr, setop, CPURegs>;
  def i       : SetCC_I<opc, !strconcat(cmpstr, "i"), setop, simm16, immSExt16, CPURegs>;
}

// Signed and unsigned compares
multiclass CompareSU<bits<6> opc, bits<6> opcu, string cmpstr, PatFrag setop, PatFrag setopu>:
  CompareS<opc, cmpstr, setop> {
  def u       : SetCC_R<0x3a, opcu, !strconcat(cmpstr, "u"), setopu, CPURegs>;
  def ui      : SetCC_I<opcu, !strconcat(cmpstr, "ui"), setopu, simm16, immSExt16, CPURegs>;
}

// Jump
class JumpFJ<bits<6> op, string instr_asm>:
  FJ<op, (outs), (ins jmptarget:$target),
     !strconcat(instr_asm, "\t$target"), [(br bb:$target)], IIBranch> {
  let isBranch=1;
  let isTerminator=1;
  let isBarrier=1;
  let hasDelaySlot = 1;
  let Defs = [PC];
}

// Unconditional branch
class UncondBranch<bits<6> op, string instr_asm>:
  FI<op, (outs), (ins brtarget:$imm16),
             !strconcat(instr_asm, "\t$imm16"), [(br bb:$imm16)], IIBranch> {
  let rA = 0;
  let rB = 0;
  let isBranch = 1;
  let isTerminator = 1;
  let isBarrier = 1;
  let hasDelaySlot = 1;
  let Defs = [PC];
}

// Base class for indirect branch and return instruction classes.
let isTerminator=1, isBarrier=1, hasDelaySlot = 1 in
class JumpFR<RegisterClass RC, list<dag> pattern>:
  FR<0x3a, 0xd, 0, (outs), (ins RC:$rA), "jmp\t$rA", pattern, IIBranch> {
  let rB = 0;
  let rC = 0;
}

// Indirect branch
class IndirectBranch<RegisterClass RC>: JumpFR<RC, [(brind RC:$rA)]> {
  let isBranch = 1;
  let isIndirectBranch = 1;
}

let isTerminator=1, isBarrier=1, Defs=[PC] in
class CallFR<RegisterClass RC>:
  FR<0x3a, 0x5, 0, (outs), (ins RC:$rA), "ret", [], IIBranch> {
  let rB = 0;
  let rC = 0;
}

// Return instruction
class RetBase<RegisterClass RC>: CallFR<RC> {
  let isReturn = 1;
  let isCodeGenOnly = 1;
  let hasCtrlDep = 1;
  let hasExtraSrcRegAllocReq = 1;
}

// Jump and Link (Call)
let isCall=1, hasDelaySlot=1, Defs = [RA] in {
  class JumpLink<bits<6> op, string instr_asm>:
    FJ<op, (outs), (ins calltarget:$target),
       !strconcat(instr_asm, "\t$target"), [(Nios2JmpLink imm:$target)],
       IIBranch> {
       let DecoderMethod = "DecodeJumpTarget";
       }

  class JumpLinkReg<bits<6> op, bits<6> func, string instr_asm,
                    RegisterClass RC>:
    FR<op, func, 0, (outs), (ins RC:$rA),
       !strconcat(instr_asm, "\t$rA"), [(Nios2JmpLink RC:$rA)], IIBranch> {
    let rB = 0;
    let rC = 0x1f;
    let shamt = 0;
  }

  class BranchLink<string instr_asm, bits<5> _rB, RegisterClass RC>:
    FI<0x1, (outs), (ins RC:$rA, brtarget:$imm16),
       !strconcat(instr_asm, "\t$rA, $imm16"), [], IIBranch> {
    let rB = _rB;
  }
}

// Mul, Div
class Mult<bits<6> func, string instr_asm, InstrItinClass itin,
           RegisterClass RC, list<Register> DefRegs>:
  FR<0x00, func, 0, (outs), (ins RC:$rA, RC:$rB),
     !strconcat(instr_asm, "\t$rA, $rB"), [], itin> {
  let rC = 0;
  let shamt = 0;
  let isCommutable = 1;
  let Defs = DefRegs;
  let hasSideEffects = 0;
}

// Access control registers
class CtlInst<dag outs, dag ins, list<dag> pattern, string asm_instr> :
  FR<0x3a, 0x26, 0, outs, ins, asm_instr, pattern, NoItinerary> {
  bits<5> rCtl;
  let shamt = rCtl;
}

def RDCTL : CtlInst<(outs CPURegs:$rC), (ins CtlRegs:$rCtl), [],
    "rdctl $rC, $rCtl"> {
  let rA = 0;
  let rB = 0;
}

let hasSideEffects = 1 in
def WRCTL : CtlInst<(outs), (ins CtlRegs:$rCtl, CPURegs:$rA), [],
    "wrctl $rCtl, $rA"> {
  let rB = 0;
  let rC = 0;
}

def : Pat<(int_nios2_rdctl imm:$rCtl),
    (RDCTL (ICTLREG imm:$rCtl))>;
def : Pat<(int_nios2_wrctl imm:$rCtl, CPURegs:$rA),
    (WRCTL (ICTLREG imm:$rCtl), CPURegs:$rA)>;

//===----------------------------------------------------------------------===//
// Pseudo instructions
//===----------------------------------------------------------------------===//

// Return RA.
let isReturn=1, isTerminator=1, hasDelaySlot=1, isBarrier=1, hasCtrlDep=1 in
def RetRA : Nios2Pseudo<(outs), (ins), "", [(Nios2Ret)]>;

let Defs = [SP], Uses = [SP], hasSideEffects = 1 in {
def ADJCALLSTACKDOWN : Nios2Pseudo<(outs), (ins i32imm:$amt),
                                  "!ADJCALLSTACKDOWN $amt",
                                  [(callseq_start timm:$amt)]>;
def ADJCALLSTACKUP   : Nios2Pseudo<(outs), (ins i32imm:$amt1, i32imm:$amt2),
                                  "!ADJCALLSTACKUP $amt1",
                                  [(callseq_end timm:$amt1, timm:$amt2)]>;
}

// When handling PIC code the assembler needs .cpload and .cprestore
// directives. If the real instructions corresponding these directives
// are used, we have the same behavior, but get also a bunch of warnings
// from the assembler.
let hasSideEffects = 0 in
def CPRESTORE : Nios2Pseudo<(outs), (ins i32imm:$loc, CPURegs:$gp),
                         ".cprestore\t$loc", []>;

//===----------------------------------------------------------------------===//
// Instruction definition
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Nios2I Instructions
//===----------------------------------------------------------------------===//

/// Arithmetic Instructions (ALU Immediate)
def ADDi    : ArithLogicI<0x04, "addi", add, simm16, immSExt16, CPURegs>;
def ANDi    : ArithLogicI<0x0c, "andi", and, uimm16, immZExt16, CPURegs>;
def ANDhi   : ArithLogicI<0x2c, "andhi", and, uimm16, immLow16Zero, CPURegs>;
def ORi     : ArithLogicI<0x14, "ori", or, uimm16, immZExt16, CPURegs>;
def ORhi    : ArithLogicI<0x34, "orhi", or, uimm16, immLow16Zero, CPURegs>;
def XORi    : ArithLogicI<0x1c, "xori", xor, uimm16, immZExt16, CPURegs>;
def XORhi    : ArithLogicI<0x3c, "xorhi", xor, uimm16, immLow16Zero, CPURegs>;

/// Arithmetic Instructions (3-Operand, R-Type)
def ADD     : ArithOverflowR<0x3a, 0x31, "add", IIAlu, CPURegs, 1>;
def SUB     : ArithOverflowR<0x3a, 0x39, "sub", IIAlu, CPURegs>;
def AND     : ArithLogicR<0x3a, 0x0e, "and", and, IIAlu, CPURegs, 1>;
def OR      : ArithLogicR<0x3a, 0x16, "or",  or, IIAlu, CPURegs, 1>;
def XOR     : ArithLogicR<0x3a, 0x1e, "xor", xor, IIAlu, CPURegs, 1>;
def NOR     : LogicNOR<0x3a, 0x06, "nor", CPURegs>;

/// Shift Instructions
def SLLi     : shift_rotate_imm32<0x12, 0x00, "slli", shl>;
def SRLi     : shift_rotate_imm32<0x1a, 0x00, "srli", srl>;
def SRAi     : shift_rotate_imm32<0x3a, 0x00, "srai", sra>;
def SLL    : shift_rotate_reg<0x13, 0x00, "sll", shl, CPURegs>;
def SRL    : shift_rotate_reg<0x1b, 0x00, "srl", srl, CPURegs>;
def SRA    : shift_rotate_reg<0x3b, 0x00, "sra", sra, CPURegs>;

/// Rotate Instructions
def ROR   : shift_rotate_reg<0x0b, 0x01, "ror", rotr, CPURegs>;
def ROLi  : shift_rotate_reg<0x02, 0x01, "roli", rotl, CPURegs>;
def ROL   : shift_rotate_reg<0x03, 0x01, "rol", rotl, CPURegs>;

/// Compares
defm CMPLT  : CompareSU<0x10, 0x30, "cmplt", setlt, setult>;
defm CMPGE  : CompareSU<0x08, 0x28, "cmpge", setge, setuge>;
defm CMPEQ  : CompareS<0x20, "cmpeq", seteq>;
defm CMPNE  : CompareS<0x18, "cmpne", setne>;


/// Load and Store Instructions
///  aligned
def LDB      : LoadM32<0x07, "ldb",  sextloadi8>;
def LDBu     : LoadM32<0x03, "ldbu", zextloadi8>;
def LDH      : LoadM32<0x0f, "ldh",  sextloadi16>;
def LDHu     : LoadM32<0x0b, "ldhu", zextloadi16>;
def LDW      : LoadM32<0x17, "ldw",  load>;
def STB      : StoreM32<0x05, "stb", truncstorei8>;
def STH      : StoreM32<0x0d, "sth", truncstorei16>;
def STW      : StoreM32<0x15, "stw", store>;

//def SYNC : FR<0x3a, 0x36, 0, (outs), (ins i32imm:$stype), "sync $stype",
//                  [(Nios2Sync imm:$stype)], NoItinerary> {
let hasSideEffects = 1 in
def SYNC : FR<0x3a, 0x36, 0, (outs), (ins), "sync",
                  [(Nios2Sync)], NoItinerary> {
  let rA = 0;
  let rB = 0;
  let rC = 0;
}

/// Jump and Branch Instructions
def JMPi    : JumpFJ<0x01, "jmpi">, Requires<[RelocStatic]>;
def JMP     : IndirectBranch<JMPRegs>;
def BR      : UncondBranch<0x06, "br">;
def BEQ     : CBranch<0x26, "beq", seteq, CPURegs>;
def BNE     : CBranch<0x1e, "bne", setne, CPURegs>;
def BGE     : CBranch<0x0e, "bge", setge, CPURegs>;
def BGEU    : CBranch<0x2e, "bgeu", setuge, CPURegs>;
def BLT     : CBranch<0x16, "blt", setlt, CPURegs>;
def BLTU    : CBranch<0x36, "bltu", setult, CPURegs>;
def BGT     : CBranchPseudo<"bgt", setgt, CPURegs>;
def BGTU    : CBranchPseudo<"bgtu", setugt, CPURegs>;
def BLE     : CBranchPseudo<"ble", setle, CPURegs>;
def BLEU    : CBranchPseudo<"bleu", setule, CPURegs>;

def CALL  : JumpLink<0, "call">;
def CALLR : JumpLinkReg<0x3a, 0x1d, "callr", CPURegs>;
def RET : RetBase<CPURegs>;

/// Divide Instructions.
def MUL       : ArithLogicR<0x3a, 0x27, "mul", mul, IIAlu, CPURegs, 1>;
def MULi      : ArithLogicI<0x24, "muli", mul, simm16, immSExt16, CPURegs>;
def MULXSS    : ArithLogicR<0x3a, 0x1f, "mulxss", mulhs, IIAlu, CPURegs, 1>;
def MULXUU    : ArithLogicR<0x3a, 0x07, "mulxuu", mulhu, IIAlu, CPURegs, 1>;
def DIV       : ArithLogicR<0x3a, 0x25, "div", sdiv, IIIdiv, CPURegs>;
def DIVU      : ArithLogicR<0x3a, 0x24, "divu", udiv, IIIdiv, CPURegs>;

/// No operation
def NOP   : Nios2Pseudo<(outs), (ins), "nop", []>;

/// Pseudo instruction that match copy to reg from frameindexes
let usesCustomInserter = 1 in
def MOVFI : Nios2Pseudo<(outs CPURegs:$rA), (ins mem:$addr), "movfi",
    [(set CPURegs:$rA, addr:$addr)]>;

//===----------------------------------------------------------------------===//
// Instruction aliases
//===----------------------------------------------------------------------===//
def : InstAlias<"mov $rC,$rA", (ADD CPURegs:$rC,CPURegs:$rA,ZERO)>;
def : InstAlias<"movi $rB,$imm", (ADDi CPURegs:$rB,ZERO, simm16:$imm)>;
def : InstAlias<"movui $rB,$imm",
                (ORi CPURegs:$rB,ZERO,simm16:$imm)>;
//def : InstAlias<"bgt $rA,$rB,$imm",
//                (BLT CPURegs:$rB,CPURegs:$rA,brtarget:$imm)>;
//def : InstAlias<"bgtu $rA,$rB,$imm",
//                (BLTU CPURegs:$rB,CPURegs:$rA,brtarget:$imm)>;
//def : InstAlias<"ble $rA,$rB,$imm",
//                (BGE CPURegs:$rB,CPURegs:$rA,brtarget:$imm)>;
//def : InstAlias<"bleu $rA,$rB,$imm",
//                (BGEU CPURegs:$rB,CPURegs:$rA,brtarget:$imm)>;
//def : InstAlias<"nop",
//                (ADD ZERO, ZERO, ZERO)>;
//def : InstAlias<"subi $rB,$rA,$imm",
//                (ADDi CPURegs:$rB, CPURegs:$rA, (NEG16 simm16:$imm))>;

//===----------------------------------------------------------------------===//
//  Arbitrary patterns that map to one or more instructions
//===----------------------------------------------------------------------===//

// Small immediates
def : Nios2Pat<(i32 immSExt16:$in),
              (ADDi ZERO, imm:$in)>;
def : Nios2Pat<(i32 immZExt16:$in),
              (ORi ZERO, imm:$in)>;
def : Nios2Pat<(i32 immLow16Zero:$in),
              (ORhi ZERO, (HI16 imm:$in))>;

// Arbitrary immediates
def : Nios2Pat<(i32 imm:$imm),
          (ORi (ORi ZERO, (HI16 imm:$imm)), (LO16 imm:$imm))>;

// Carry Nios2Patterns
def : Nios2Pat<(add CPURegs:$lhs, CPURegs:$rhs),
              (ADD CPURegs:$lhs, CPURegs:$rhs)>;
def : Nios2Pat<(add  CPURegs:$src, immSExt16:$imm),
              (ADDi CPURegs:$src, imm:$imm)>;
def : Nios2Pat<(sub CPURegs:$lhs, CPURegs:$rhs),
              (SUB CPURegs:$lhs, CPURegs:$rhs)>;
def : Nios2Pat<(sub CPURegs:$lhs, immSExt16:$imm),
              (ADDi CPURegs:$lhs, (NEG16 imm:$imm))>;

// hi/lo relocs
def : Nios2Pat<(Nios2Hi tglobaladdr:$in), (ORhi ZERO, tglobaladdr:$in)>;
def : Nios2Pat<(Nios2Hi tblockaddress:$in), (ORhi ZERO, tblockaddress:$in)>;
def : Nios2Pat<(Nios2Hi tjumptable:$in), (ORhi ZERO, tjumptable:$in)>;
def : Nios2Pat<(Nios2Hi tconstpool:$in), (ORhi ZERO, tconstpool:$in)>;
def : Nios2Pat<(Nios2Hi tglobaltlsaddr:$in), (ORhi ZERO, tglobaltlsaddr:$in)>;

def : Nios2Pat<(Nios2Lo tglobaladdr:$in), (ORi ZERO, tglobaladdr:$in)>;
def : Nios2Pat<(Nios2Lo tblockaddress:$in), (ORi ZERO, tblockaddress:$in)>;
def : Nios2Pat<(Nios2Lo tjumptable:$in), (ORi ZERO, tjumptable:$in)>;
def : Nios2Pat<(Nios2Lo tconstpool:$in), (ORi ZERO, tconstpool:$in)>;
def : Nios2Pat<(Nios2Lo tglobaltlsaddr:$in), (ORi ZERO, tglobaltlsaddr:$in)>;

def : Nios2Pat<(add CPURegs:$hi, (Nios2Lo tglobaladdr:$lo)),
              (ADDi CPURegs:$hi, tglobaladdr:$lo)>;
def : Nios2Pat<(add CPURegs:$hi, (Nios2Lo tblockaddress:$lo)),
              (ADDi CPURegs:$hi, tblockaddress:$lo)>;
def : Nios2Pat<(add CPURegs:$hi, (Nios2Lo tjumptable:$lo)),
              (ADDi CPURegs:$hi, tjumptable:$lo)>;
def : Nios2Pat<(add CPURegs:$hi, (Nios2Lo tconstpool:$lo)),
              (ADDi CPURegs:$hi, tconstpool:$lo)>;
def : Nios2Pat<(add CPURegs:$hi, (Nios2Lo tglobaltlsaddr:$lo)),
              (ADDi CPURegs:$hi, tglobaltlsaddr:$lo)>;

// gp_rel relocs
def : Nios2Pat<(add CPURegs:$gp, (Nios2GPRel tglobaladdr:$in)),
              (ADDi CPURegs:$gp, tglobaladdr:$in)>;
def : Nios2Pat<(add CPURegs:$gp, (Nios2GPRel tconstpool:$in)),
              (ADDi CPURegs:$gp, tconstpool:$in)>;

def : Nios2Pat<(Nios2JmpLink (i32 tglobaladdr:$dst)),
              (CALL tglobaladdr:$dst)>;
def : Nios2Pat<(Nios2JmpLink (i32 texternalsym:$dst)),
              (CALL texternalsym:$dst)>;
//def : Nios2Pat<(Nios2JmpLink CPURegs:$dst),
//              (CALLR CPURegs:$dst)>;

// wrapper_pic
class WrapperPat<SDNode node, Instruction ADDiOp, RegisterClass RC>:
      Nios2Pat<(Nios2Wrapper RC:$gp, node:$in),
              (ADDiOp RC:$gp, node:$in)>;

def : WrapperPat<tglobaladdr, ADDi, CPURegs>;
def : WrapperPat<tconstpool, ADDi, CPURegs>;
def : WrapperPat<texternalsym, ADDi, CPURegs>;
def : WrapperPat<tblockaddress, ADDi, CPURegs>;
def : WrapperPat<tjumptable, ADDi, CPURegs>;
def : WrapperPat<tglobaltlsaddr, ADDi, CPURegs>;

// Nios2 does not have "not", so we expand our way
def : Nios2Pat<(not CPURegs:$in),
              (NOR CPURegs:$in, ZERO)>;

// extended loads
def : Nios2Pat<(i32 (extloadi1  addr:$src)), (LDBu addr:$src)>;
def : Nios2Pat<(i32 (extloadi8  addr:$src)), (LDBu addr:$src)>;
def : Nios2Pat<(i32 (extloadi16 addr:$src)), (LDHu addr:$src)>;
def : Nios2Pat<(i32 (extloadi1  addr:$src)), (LDBu addr:$src)>;
def : Nios2Pat<(i32 (extloadi8  addr:$src)), (LDBu addr:$src)>;
def : Nios2Pat<(i32 (extloadi16 addr:$src)), (LDHu addr:$src)>;

def : Nios2Pat<(store (i32 0), addr:$dst), (STW ZERO, addr:$dst)>;

// brcond patterns
multiclass BrcondPats<RegisterClass RC, Instruction BEQOp, Instruction BNEOp,
                      Instruction SLTOp, Instruction SLTuOp, Instruction SLTiOp,
                      Instruction SLTiuOp, Register ZEROReg> {
def : Nios2Pat<(brcond (i32 (setne RC:$lhs, 0)), bb:$dst),
              (BNEOp RC:$lhs, ZEROReg, bb:$dst)>;
def : Nios2Pat<(brcond (i32 (seteq RC:$lhs, 0)), bb:$dst),
              (BEQOp RC:$lhs, ZEROReg, bb:$dst)>;

def : Nios2Pat<(brcond (i32 (setge RC:$lhs, RC:$rhs)), bb:$dst),
              (BEQ (SLTOp RC:$lhs, RC:$rhs), ZERO, bb:$dst)>;
def : Nios2Pat<(brcond (i32 (setuge RC:$lhs, RC:$rhs)), bb:$dst),
              (BEQ (SLTuOp RC:$lhs, RC:$rhs), ZERO, bb:$dst)>;
def : Nios2Pat<(brcond (i32 (setge RC:$lhs, immSExt16:$rhs)), bb:$dst),
              (BEQ (SLTiOp RC:$lhs, immSExt16:$rhs), ZERO, bb:$dst)>;
def : Nios2Pat<(brcond (i32 (setuge RC:$lhs, immSExt16:$rhs)), bb:$dst),
              (BEQ (SLTiuOp RC:$lhs, immSExt16:$rhs), ZERO, bb:$dst)>;

def : Nios2Pat<(brcond (i32 (setle RC:$lhs, RC:$rhs)), bb:$dst),
              (BEQ (SLTOp RC:$rhs, RC:$lhs), ZERO, bb:$dst)>;
def : Nios2Pat<(brcond (i32 (setule RC:$lhs, RC:$rhs)), bb:$dst),
              (BEQ (SLTuOp RC:$rhs, RC:$lhs), ZERO, bb:$dst)>;

def : Nios2Pat<(brcond RC:$cond, bb:$dst),
              (BNEOp RC:$cond, ZEROReg, bb:$dst)>;
}

defm : BrcondPats<CPURegs, BEQ, BNE, CMPLT, CMPLTu, CMPLTi, CMPLTui, ZERO>;

/// Use existent compares with imm + 1
multiclass SetCmpImmPats<RegisterClass RC, Instruction cmpi, Instruction cmpui> {
  def i : Nios2Pat<(setgt RC:$lhs, immSExt16:$rhs),
                (cmpi RC:$lhs, (IMMPLUS1 imm:$rhs))>;
  def ui : Nios2Pat<(setugt RC:$lhs, immSExt16:$rhs),
                (cmpui RC:$lhs, (IMMPLUS1 imm:$rhs))>;
}

/// Swapped rhs and lhs to match existent compares
multiclass SetCmpPats<RegisterClass RC, PatFrag setop, PatFrag setopu,
           Instruction cmp, Instruction cmpu,
           Instruction cmpi, Instruction cmpui>:
  SetCmpImmPats<RC, cmpi, cmpui> {
  def #NAME# : Nios2Pat<(setop RC:$lhs, RC:$rhs),
                (cmp RC:$rhs, RC:$lhs)>;
  def u : Nios2Pat<(setopu RC:$lhs, RC:$rhs),
                (cmpu RC:$rhs, RC:$lhs)>;
}

defm CMPGT : SetCmpPats<CPURegs, setgt, setugt, CMPLT, CMPLTu, CMPGEi, CMPGEui>;
defm CMPLE : SetCmpPats<CPURegs, setle, setule, CMPGE, CMPGEu, CMPLTi, CMPLTui>;

// Generic select instruction
let usesCustomInserter = 1 in
def SELECT   : Nios2Pseudo<(outs CPURegs:$res), (ins CPURegs:$a, CPURegs:$x, CPURegs:$y),
                                  "!select $res, $a, $x, $y",
                                  [(set CPURegs:$res, (Nios2Select CPURegs:$a, CPURegs:$x, CPURegs:$y))]>;

