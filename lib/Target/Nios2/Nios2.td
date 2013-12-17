//===-- Nios2.td - Describe the Nios2 Target Machine ---------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// This is the top level entry point for the Nios2 target.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Target-independent interfaces
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

//===----------------------------------------------------------------------===//
// Register File, Calling Conv, Instruction Descriptions
//===----------------------------------------------------------------------===//

include "Nios2RegisterInfo.td"
include "Nios2Schedule.td"
include "Nios2InstrInfo.td"
include "Nios2CallingConv.td"

def Nios2InstrInfo : InstrInfo;

def FeatureHWMul   : SubtargetFeature<"hw-mul","HasHWMul", "true",
                                        "Enable hardware multiplier">;
def FeatureHWDiv   : SubtargetFeature<"hw-div","HasHWDiv", "true",
                                        "Enable hardware divider">;

//===----------------------------------------------------------------------===//
// Nios2 processors supported.
//===----------------------------------------------------------------------===//


class Proc<string Name, list<SubtargetFeature> Features = []>
 : Processor<Name, Nios2GenericItineraries, Features>;

def : Proc<"nios2", [FeatureHWMul, FeatureHWDiv]>;

def Nios2AsmWriter : AsmWriter {
  string AsmWriterClassName  = "InstPrinter";
  bit isMCAsmWriter = 1;
}

def Nios2 : Target {
  let InstructionSet = Nios2InstrInfo;
  let AssemblyWriters = [Nios2AsmWriter];
}
