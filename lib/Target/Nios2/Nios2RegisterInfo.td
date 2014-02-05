//===-- Nios2RegisterInfo.td - Nios2 Register defs -----------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// We have banks of 32 registers each.
class Nios2Reg<string n> : Register<n> {
  field bits<5> Num;
  let Namespace = "Nios2";
}

// Nios2 CPU Registers
class Nios2GPRReg<bits<5> num, string n> : Nios2Reg<n> {
  let Num = num;
}

class Nios2GPRRegWithAltName<bits<5> num, string n, list<string> altNames> : Nios2Reg<n> {
  let Num = num;
  let AltNames = altNames;
}

//===----------------------------------------------------------------------===//
//  Registers
//===----------------------------------------------------------------------===//

let Namespace = "Nios2" in {
  // General Purpose Registers
  def ZERO : Nios2GPRReg< 0, "zero">, DwarfRegNum<[0]>;
  def AT   : Nios2GPRReg< 1, "at">,   DwarfRegNum<[1]>;
  foreach RegNum = 2-23 in {
    def R#RegNum : Nios2GPRReg< RegNum, "R"#RegNum>, DwarfRegNum<[RegNum]>;
  }
  def ET   : Nios2GPRReg< 24, "et">,  DwarfRegNum<[24]>;
  def BT   : Nios2GPRReg< 25, "bt">,  DwarfRegNum<[25]>;
  def GP   : Nios2GPRReg< 26, "gp">,  DwarfRegNum<[26]>;
  def SP   : Nios2GPRReg< 27, "sp">,  DwarfRegNum<[27]>;
  def FP   : Nios2GPRReg< 28, "fp">,  DwarfRegNum<[28]>;
  def EA   : Nios2GPRReg< 29, "ea">,  DwarfRegNum<[29]>;
  def BA   : Nios2GPRReg< 30, "ba">,  DwarfRegNum<[30]>;
  def RA   : Nios2GPRReg< 31, "ra">,  DwarfRegNum<[31]>;
  def PC   : Nios2Reg<"pc">,  DwarfRegNum<[32]>;
  def CTL0  : Nios2GPRRegWithAltName<0, "ctl0", ["status"]>;
  def CTL1  : Nios2GPRRegWithAltName<1, "ctl1", ["estatus"]>;
  def CTL2  : Nios2GPRRegWithAltName<2, "ctl2", ["bstatus"]>;
  def CTL3  : Nios2GPRRegWithAltName<3, "ctl3", ["ienable"]>;
  def CTL4  : Nios2GPRRegWithAltName<4, "ctl4", ["ipending"]>;
  def CTL5  : Nios2GPRRegWithAltName<5, "ctl5", ["cpuid"]>;
  def CTL7  : Nios2GPRRegWithAltName<7, "ctl7", ["exception"]>;
  def CTL8  : Nios2GPRRegWithAltName<8, "ctl8", ["pteaddr"]>;
  def CTL9  : Nios2GPRRegWithAltName<9, "ctl9", ["tlbacc"]>;
  def CTL10 : Nios2GPRRegWithAltName<10,"ctl10", ["tlbmisc"]>;
  def CTL12 : Nios2GPRRegWithAltName<12,"ctl12", ["badaddr"]>;
  def CTL13 : Nios2GPRRegWithAltName<13,"ctl13", ["config"]>;
  def CTL14 : Nios2GPRRegWithAltName<14,"ctl14", ["mpubase"]>;
  def CTL15 : Nios2GPRRegWithAltName<15,"ctl15", ["mpuacc"]>;
}

//===----------------------------------------------------------------------===//
// Register Classes
//===----------------------------------------------------------------------===//

def CPURegs : RegisterClass<"Nios2", [i32], 32, (add
  // Reserved
  ZERO, AT,
  // Return Values and Arguments
  (sequence "R%u", 2, 7),
  // Not preserved across procedure calls
  // Caller saved
  (sequence "R%u", 8, 15),
  // Callee saved
  (sequence "R%u", 16, 23),
  // Reserved
  ET, BT, GP, SP, FP, EA, BA, RA, PC)>;

def CtrlRegs : RegisterClass<"Nios2", [i32], 32,
    (add (sequence "CTL%u", 0, 5),
         (sequence "CTL%u", 7, 10),
         (sequence "CTL%u", 12, 15))>;

// Remove r31 (aka RA) & PC registers
def JMPRegs : RegisterClass<"Nios2", [i32], 32, (trunc CPURegs, 31)>;

