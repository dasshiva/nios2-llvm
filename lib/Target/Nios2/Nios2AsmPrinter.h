//===-- Nios2AsmPrinter.h - Nios2 LLVM Assembly Printer ----------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Nios2 Assembly printer class.
//
//===----------------------------------------------------------------------===//

#ifndef NIOS2ASMPRINTER_H
#define NIOS2ASMPRINTER_H

#include "Nios2MachineFunction.h"
#include "Nios2MCInstLower.h"
#include "Nios2Subtarget.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {
class MCStreamer;
class MachineInstr;
class MachineBasicBlock;
class Module;
class raw_ostream;

class LLVM_LIBRARY_VISIBILITY Nios2AsmPrinter : public AsmPrinter {

  void EmitInstrWithMacroNoAT(const MachineInstr *MI);

public:

  const Nios2Subtarget *Subtarget;
  const Nios2FunctionInfo *Nios2FI;
  Nios2MCInstLower MCInstLowering;

  explicit Nios2AsmPrinter(TargetMachine &TM, 
                           std::unique_ptr<MCStreamer> &Streamer)
    : AsmPrinter(TM, std::move(Streamer)), MCInstLowering(*this) {}

  virtual const char *getPassName() const {
    return "Nios2 Assembly Printer";
  }


  virtual bool runOnMachineFunction(MachineFunction &MF);

  void EmitInstruction(const MachineInstr *MI);
  virtual void EmitFunctionBodyStart();
  void printSavedRegsBitmask(raw_ostream &O);
  void printHex32(unsigned int Value, raw_ostream &O);
  void emitFrameDirective();
  virtual bool isBlockOnlyReachableByFallthrough(const MachineBasicBlock*
                                                 MBB) const;
  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                       unsigned AsmVariant, const char *ExtraCode,
                       raw_ostream &O);
  bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNum,
                             unsigned AsmVariant, const char *ExtraCode,
                             raw_ostream &O);
  void printOperand(const MachineInstr *MI, int opNum, raw_ostream &O);
  void printUnsignedImm(const MachineInstr *MI, int opNum, raw_ostream &O);
  void printMemOperand(const MachineInstr *MI, int opNum, raw_ostream &O);
  void printMemOperandEA(const MachineInstr *MI, int opNum, raw_ostream &O);
  virtual MachineLocation getDebugValueLocation(const MachineInstr *MI) const;
  void PrintDebugValueComment(const MachineInstr *MI, raw_ostream &OS);
};
}

#endif

