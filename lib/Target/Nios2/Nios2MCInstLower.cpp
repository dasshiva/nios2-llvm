//===-- Nios2MCInstLower.cpp - Convert Nios2 MachineInstr to MCInst ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains code to lower Nios2 MachineInstrs to their corresponding
// MCInst records.
//
//===----------------------------------------------------------------------===//
#include "Nios2MCInstLower.h"
#include "Nios2AsmPrinter.h"
#include "Nios2InstrInfo.h"
#include "MCTargetDesc/Nios2BaseInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Target/Mangler.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

Nios2MCInstLower::Nios2MCInstLower(Nios2AsmPrinter &asmprinter)
  : AsmPrinter(asmprinter) {}

void Nios2MCInstLower::Initialize(Mangler *M, MCContext *C) {
  Mang = M;
  Ctx = C;
}

MCOperand Nios2MCInstLower::LowerSymbolOperand(const MachineOperand &MO,
                                              MachineOperandType MOTy,
                                              unsigned Offset) const {
  //MCSymbolRefExpr::VariantKind Kind;
  const MCSymbol *Symbol;

//  switch(MO.getTargetFlags()) {
//  default:                   llvm_unreachable("Invalid target flag!");
//  case Nios2II::MO_NO_FLAG:   Kind = MCSymbolRefExpr::VK_None; break;
//  case Nios2II::MO_GPREL:     Kind = MCSymbolRefExpr::VK_Nios2_GPREL; break;
//  case Nios2II::MO_GOT_CALL:  Kind = MCSymbolRefExpr::VK_Nios2_GOT_CALL; break;
//  case Nios2II::MO_GOT16:     Kind = MCSymbolRefExpr::VK_Nios2_GOT16; break;
//  case Nios2II::MO_GOT:       Kind = MCSymbolRefExpr::VK_Nios2_GOT; break;
//  case Nios2II::MO_ABS_HI:    Kind = MCSymbolRefExpr::VK_Nios2_ABS_HI; break;
//  case Nios2II::MO_ABS_LO:    Kind = MCSymbolRefExpr::VK_Nios2_ABS_LO; break;
//  case Nios2II::MO_TLSGD:     Kind = MCSymbolRefExpr::VK_Nios2_TLSGD; break;
//  case Nios2II::MO_TLSLDM:    Kind = MCSymbolRefExpr::VK_Nios2_TLSLDM; break;
//  case Nios2II::MO_DTPREL_HI: Kind = MCSymbolRefExpr::VK_Nios2_DTPREL_HI; break;
//  case Nios2II::MO_DTPREL_LO: Kind = MCSymbolRefExpr::VK_Nios2_DTPREL_LO; break;
//  case Nios2II::MO_GOTTPREL:  Kind = MCSymbolRefExpr::VK_Nios2_GOTTPREL; break;
//  case Nios2II::MO_TPREL_HI:  Kind = MCSymbolRefExpr::VK_Nios2_TPREL_HI; break;
//  case Nios2II::MO_TPREL_LO:  Kind = MCSymbolRefExpr::VK_Nios2_TPREL_LO; break;
//  case Nios2II::MO_GPOFF_HI:  Kind = MCSymbolRefExpr::VK_Nios2_GPOFF_HI; break;
//  case Nios2II::MO_GPOFF_LO:  Kind = MCSymbolRefExpr::VK_Nios2_GPOFF_LO; break;
//  case Nios2II::MO_GOT_DISP:  Kind = MCSymbolRefExpr::VK_Nios2_GOT_DISP; break;
//  case Nios2II::MO_GOT_PAGE:  Kind = MCSymbolRefExpr::VK_Nios2_GOT_PAGE; break;
//  case Nios2II::MO_GOT_OFST:  Kind = MCSymbolRefExpr::VK_Nios2_GOT_OFST; break;
//  case Nios2II::MO_HIGHER:    Kind = MCSymbolRefExpr::VK_Nios2_HIGHER; break;
//  case Nios2II::MO_HIGHEST:   Kind = MCSymbolRefExpr::VK_Nios2_HIGHEST; break;
//  }

  switch (MOTy) {
  case MachineOperand::MO_MachineBasicBlock:
    Symbol = MO.getMBB()->getSymbol();
    break;

  case MachineOperand::MO_GlobalAddress:
    Symbol = AsmPrinter.getSymbol(MO.getGlobal());
    Offset += MO.getOffset();
    break;

  case MachineOperand::MO_BlockAddress:
    Symbol = AsmPrinter.GetBlockAddressSymbol(MO.getBlockAddress());
    Offset += MO.getOffset();
    break;

  case MachineOperand::MO_ExternalSymbol:
    Symbol = AsmPrinter.GetExternalSymbolSymbol(MO.getSymbolName());
    Offset += MO.getOffset();
    break;

  case MachineOperand::MO_JumpTableIndex:
    Symbol = AsmPrinter.GetJTISymbol(MO.getIndex());
    break;

  case MachineOperand::MO_ConstantPoolIndex:
    Symbol = AsmPrinter.GetCPISymbol(MO.getIndex());
    Offset += MO.getOffset();
    break;

  default:
    llvm_unreachable("<unknown operand type>");
  }

  //const MCSymbolRefExpr *MCSym = MCSymbolRefExpr::Create(Symbol, Kind, *Ctx);
  const MCSymbolRefExpr *MCSym = MCSymbolRefExpr::Create(Symbol, *Ctx);

  if (!Offset)
    return MCOperand::CreateExpr(MCSym);

  // Assume offset is never negative.
  assert(Offset > 0);

  const MCConstantExpr *OffsetExpr =  MCConstantExpr::Create(Offset, *Ctx);
  const MCBinaryExpr *Add = MCBinaryExpr::CreateAdd(MCSym, OffsetExpr, *Ctx);
  return MCOperand::CreateExpr(Add);
}

/*
static void CreateMCInst(MCInst& Inst, unsigned Opc, const MCOperand &Opnd0,
                         const MCOperand &Opnd1,
                         const MCOperand &Opnd2 = MCOperand()) {
  Inst.setOpcode(Opc);
  Inst.addOperand(Opnd0);
  Inst.addOperand(Opnd1);
  if (Opnd2.isValid())
    Inst.addOperand(Opnd2);
}
*/

MCOperand Nios2MCInstLower::LowerOperand(const MachineOperand &MO,
                                        unsigned offset) const {
  MachineOperandType MOTy = MO.getType();

  switch (MOTy) {
  default: llvm_unreachable("unknown operand type");
  case MachineOperand::MO_Register:
    // Ignore all implicit register operands.
    if (MO.isImplicit()) break;
    return MCOperand::CreateReg(MO.getReg());
  case MachineOperand::MO_Immediate:
    return MCOperand::CreateImm(MO.getImm() + offset);
  case MachineOperand::MO_MachineBasicBlock:
  case MachineOperand::MO_GlobalAddress:
  case MachineOperand::MO_ExternalSymbol:
  case MachineOperand::MO_JumpTableIndex:
  case MachineOperand::MO_ConstantPoolIndex:
  case MachineOperand::MO_BlockAddress:
    return LowerSymbolOperand(MO, MOTy, offset);
  case MachineOperand::MO_RegisterMask:
    break;
 }

  return MCOperand();
}

void Nios2MCInstLower::Lower(const MachineInstr *MI, MCInst &OutMI) const {
  DEBUG(dbgs() << " Lowering " << *MI);
  OutMI.setOpcode(MI->getOpcode());

  for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = MI->getOperand(i);
    MCOperand MCOp = LowerOperand(MO);

    if (MCOp.isValid())
      OutMI.addOperand(MCOp);
  }
}

