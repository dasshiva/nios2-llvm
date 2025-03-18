//===-- Nios2InstrFormats.td - Nios2 Instruction Formats -----*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Describe NIOS2 instructions format
//
//
//===----------------------------------------------------------------------===//

// Format specifies the encoding used by the instruction.  This is part of the
// ad-hoc solution used to emit machine instruction encodings by our machine
// code emitter.
class Format<bits<3> val> {
  bits<3> Value = val;
}

def Pseudo    : Format<0>;
def FrmI      : Format<1>;
def FrmR      : Format<2>;
def FrmJ      : Format<3>;
def FrmOther  : Format<4>; // Instruction w/ a custom format

// Generic Nios2 Format
class Nios2Inst<dag outs, dag ins, string asmstr, list<dag> pattern,
               InstrItinClass itin, Format f>: Instruction
{
  field bits<32> Inst;
  Format Form = f;

  let Namespace = "Nios2";

  let Size = 4;

  bits<6> Opcode = 0;
  bits<6> OpxH = 0;
  bits<5> shamt = 0;

  // Bottom 6 bits are the 'opcode' field
  let Inst{5-0} = Opcode;

  let OutOperandList = outs;
  let InOperandList  = ins;

  let AsmString   = asmstr;
  let Pattern     = pattern;
  let Itinerary   = itin;

  //
  // Attributes specific to Nios2 instructions...
  //
  bits<3> FormBits = Form.Value;

  // TSFlags layout should be kept in sync with Nios2InstrInfo.h.
  let TSFlags{2-0}   = FormBits;

  let DecoderNamespace = "Nios2";

  field bits<32> SoftFail = 0;
}

// Nios2 Instruction Format
class InstSE<dag outs, dag ins, string asmstr, list<dag> pattern,
             InstrItinClass itin, Format f>:
  Nios2Inst<outs, ins, asmstr, pattern, itin, f> {
}

// Nios2 Pseudo Instructions Format
class Nios2Pseudo<dag outs, dag ins, string asmstr, list<dag> pattern,
      InstrItinClass Itin = IIPseudo>:
  Nios2Inst<outs, ins, asmstr, pattern, Itin, Pseudo> {
  let isCodeGenOnly = 1;
  let isPseudo = 1;
}

//===----------------------------------------------------------------------===//
// Format I instruction class in Nios2 : <|A|B|immediate|opcode|>
//===----------------------------------------------------------------------===//

class FI<bits<6> op, dag outs, dag ins, string asmstr, list<dag> pattern,
         InstrItinClass itin>: InstSE<outs, ins, asmstr, pattern, itin, FrmI>
{
  bits<5>  rA;
  bits<5>  rB;
  bits<16> imm16;

  let Opcode = op;

  let Inst{31-27} = rA;
  let Inst{26-22} = rB;
  let Inst{21-6} = imm16;
}

//===----------------------------------------------------------------------===//
// Format R instruction class in Nios2 : <|A|B|C|opx|opcode|>
//===----------------------------------------------------------------------===//

class FR<bits<6> op, bits<6> _opxH, bits<5> _shamt, dag outs, dag ins, string asmstr,
         list<dag> pattern, InstrItinClass itin>:
  InstSE<outs, ins, asmstr, pattern, itin, FrmR>
{
  bits<5>  rA;
  bits<5>  rB;
  bits<5>  rC;
  bits<11>  opx;

  let Opcode = op;
  bits<6> opxH  = _opxH;
  bits<5> shamt  = _shamt;

  let Inst{31-27} = rA;
  let Inst{26-22} = rB;
  let Inst{21-17} = rC;
  let Inst{16-11}  = opxH;
  let Inst{10-6}  = shamt;
}

//===----------------------------------------------------------------------===//
// Format J instruction class in Nios2 : <|address|opcode|>
//===----------------------------------------------------------------------===//

class FJ<bits<6> op, dag outs, dag ins, string asmstr, list<dag> pattern,
         InstrItinClass itin>: InstSE<outs, ins, asmstr, pattern, itin, FrmJ>
{
  bits<26> addr;

  let Opcode = op;

  let Inst{31-6} = addr;
}

