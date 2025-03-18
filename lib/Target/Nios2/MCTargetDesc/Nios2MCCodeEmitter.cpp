//===-- Nios2MCCodeEmitter.cpp - Convert Nios2 Code to Machine Code ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Nios2MCCodeEmitter class.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/Nios2BaseInfo.h"
#include "MCTargetDesc/Nios2FixupKinds.h"
#include "MCTargetDesc/Nios2MCTargetDesc.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "mccodeemitter"

// If the D<shift> instruction has a shift amount that is greater
// than 31 (checked in calling routine), lower it to a D<shift>32 instruction
//void LowerLargeShift(MCInst& Inst) {
//
//  assert(Inst.getNumOperands() == 3 && "Invalid no. of operands for shift!");
//  assert(Inst.getOperand(2).isImm());
//
//  int64_t Shift = Inst.getOperand(2).getImm();
//  if (Shift <= 31)
//    return; // Do nothing
//  Shift -= 32;
//
//  // saminus32
//  Inst.getOperand(2).setImm(Shift);
//
//  switch (Inst.getOpcode()) {
//  default:
//    // Calling function is not synchronized
//    llvm_unreachable("Unexpected shift instruction");
//  case Nios2::DSLL:
//    Inst.setOpcode(Nios2::DSLL32);
//    return;
//  case Nios2::DSRL:
//    Inst.setOpcode(Nios2::DSRL32);
//    return;
//  case Nios2::DSRA:
//    Inst.setOpcode(Nios2::DSRA32);
//    return;
//  }
//}

// Pick a DEXT or DINS instruction variant based on the pos and size operands
//void LowerDextDins(MCInst& InstIn) {
//  int Opcode = InstIn.getOpcode();
//
//  if (Opcode == Nios2::DEXT)
//    assert(InstIn.getNumOperands() == 4 &&
//           "Invalid no. of machine operands for DEXT!");
//  else // Only DEXT and DINS are possible
//    assert(InstIn.getNumOperands() == 5 &&
//           "Invalid no. of machine operands for DINS!");
//
//  assert(InstIn.getOperand(2).isImm());
//  int64_t pos = InstIn.getOperand(2).getImm();
//  assert(InstIn.getOperand(3).isImm());
//  int64_t size = InstIn.getOperand(3).getImm();
//
//  if (size <= 32) {
//    if (pos < 32)  // DEXT/DINS, do nothing
//      return;
//    // DEXTU/DINSU
//    InstIn.getOperand(2).setImm(pos - 32);
//    InstIn.setOpcode((Opcode == Nios2::DEXT) ? Nios2::DEXTU : Nios2::DINSU);
//    return;
//  }
//  // DEXTM/DINSM
//  assert(pos < 32 && "DEXT/DINS cannot have both size and pos > 32");
//  InstIn.getOperand(3).setImm(size - 32);
//  InstIn.setOpcode((Opcode == Nios2::DEXT) ? Nios2::DEXTM : Nios2::DINSM);
//  return;
//}

namespace {
class Nios2MCCodeEmitter : public MCCodeEmitter {
  Nios2MCCodeEmitter(const Nios2MCCodeEmitter &) = delete;
  void operator=(const Nios2MCCodeEmitter &) = delete;
  const MCInstrInfo &MCII;
  MCContext &Ctx;
  bool IsLittleEndian;

public:
  Nios2MCCodeEmitter(const MCInstrInfo &mcii, MCContext &Ctx_, bool IsLittle) 
    : MCII(mcii), Ctx(Ctx_), IsLittleEndian(IsLittle) {}

  ~Nios2MCCodeEmitter() {}

  void EmitByte(unsigned char C, raw_ostream &OS) const;

  void EmitInstruction(uint64_t Val, unsigned Size, const MCSubtargetInfo &STI,
                       raw_ostream &OS) const;

  void encodeInstruction(const MCInst &MI, raw_ostream &OS,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const;

  // getBinaryCodeForInstr - TableGen'erated function for getting the
  // binary encoding for an instruction.
  uint64_t getBinaryCodeForInstr(const MCInst &MI,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const;

  // getBranchJumpOpValue - Return binary encoding of the jump
  // target operand. If the machine operand requires relocation,
  // record the relocation and return zero.
  unsigned getJumpTargetOpValue(const MCInst &MI, unsigned OpNo,
                                SmallVectorImpl<MCFixup> &Fixups,
                                const MCSubtargetInfo &STI) const;

   // getBranchTargetOpValue - Return binary encoding of the branch
   // target operand. If the machine operand requires relocation,
   // record the relocation and return zero.
  unsigned getBranchTargetOpValue(const MCInst &MI, unsigned OpNo,
                                  SmallVectorImpl<MCFixup> &Fixups,
                                  const MCSubtargetInfo &STI) const;

   // getMachineOpValue - Return binary encoding of operand. If the machin
   // operand requires relocation, record the relocation and return zero.
  unsigned getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                             SmallVectorImpl<MCFixup> &Fixups,
                             const MCSubtargetInfo &STI) const;

  unsigned getMemEncoding(const MCInst &MI, unsigned OpNo,
                          SmallVectorImpl<MCFixup> &Fixups,
                          const MCSubtargetInfo &STI) const;
  unsigned getSizeExtEncoding(const MCInst &MI, unsigned OpNo,
                              SmallVectorImpl<MCFixup> &Fixups,
                              const MCSubtargetInfo &STI) const;
  unsigned getSizeInsEncoding(const MCInst &MI, unsigned OpNo,
                              SmallVectorImpl<MCFixup> &Fixups,
                              const MCSubtargetInfo &STI) const;

}; // class Nios2MCCodeEmitter
}  // namespace

MCCodeEmitter *llvm::createNios2MCCodeEmitter(const MCInstrInfo &MCII,
                                              const MCRegisterInfo &MRI,
                                              MCContext &Ctx) {
  return new Nios2MCCodeEmitter(MCII, Ctx, true);
}

void Nios2MCCodeEmitter::EmitByte(unsigned char C, raw_ostream &OS) const {
  OS << (char)C;
}

void Nios2MCCodeEmitter::EmitInstruction(uint64_t Val, unsigned Size,
                                         const MCSubtargetInfo &STI,
                                         raw_ostream &OS) const {
  // Output the instruction encoding in little endian byte order.
  // Little-endian byte ordering:
  //   nios2:   4 | 3 | 2 | 1
  for (unsigned i = 0; i < Size; ++i) {
    unsigned Shift = IsLittleEndian ? i * 8 : (Size - 1 - i) * 8;
    EmitByte((Val >> Shift) & 0xff, OS);
  }
}

/// EncodeInstruction - Emit the instruction.
/// Size the instruction (currently only 4 bytes
void Nios2MCCodeEmitter::
encodeInstruction(const MCInst &MI, raw_ostream &OS,
                  SmallVectorImpl<MCFixup> &Fixups,
                  const MCSubtargetInfo &STI) const {

  // Non-pseudo instructions that get changed for direct object
  // only based on operand values.
  // If this list of instructions get much longer we will move
  // the check to a function call. Until then, this is more efficient.
  MCInst TmpInst = MI;
  //switch (MI.getOpcode()) {
  //// If shift amount is >= 32 it the inst needs to be lowered further
  //case Nios2::DSLL:
  //case Nios2::DSRL:
  //case Nios2::DSRA:
  //  LowerLargeShift(TmpInst);
  //  break;
  //  // Double extract instruction is chosen by pos and size operands
  //case Nios2::DEXT:
  //case Nios2::DINS:
  //  LowerDextDins(TmpInst);
  //}

  uint32_t Binary = getBinaryCodeForInstr(TmpInst, Fixups, STI);

  // Check for unimplemented opcodes.
  // Unfortunately in NIOS2 both NOP and SLL will come in with Binary == 0
  // so we have to special check for them.
  unsigned Opcode = TmpInst.getOpcode();
  if ((Opcode != Nios2::NOP) && !Binary)
    llvm_unreachable("unimplemented opcode in EncodeInstruction()");

  const MCInstrDesc &Desc = MCII.get(TmpInst.getOpcode());
  uint64_t TSFlags = Desc.TSFlags;

  // Pseudo instructions don't get encoded and shouldn't be here
  // in the first place!
  if ((TSFlags & Nios2II::FormMask) == Nios2II::Pseudo)
    llvm_unreachable("Pseudo opcode found in EncodeInstruction()");

  // Get byte count of instruction
  unsigned Size = Desc.getSize();
  if (!Size)
    llvm_unreachable("Desc.getSize() returns 0");

  EmitInstruction(Binary, Size, STI, OS);
}

/// getBranchTargetOpValue - Return binary encoding of the branch
/// target operand. If the machine operand requires relocation,
/// record the relocation and return zero.
unsigned Nios2MCCodeEmitter::
getBranchTargetOpValue(const MCInst &MI, unsigned OpNo,
                       SmallVectorImpl<MCFixup> &Fixups,
                       const MCSubtargetInfo &STI) const {

  const MCOperand &MO = MI.getOperand(OpNo);

  // If the destination is an immediate, we have nothing to do.
  if (MO.isImm()) return MO.getImm();

  assert(MO.isExpr() &&
         "getBranchTargetOpValue expects only expressions or immediates");

  const MCExpr *Expr = MO.getExpr();
  Fixups.push_back(MCFixup::create(0, Expr,
                                   MCFixupKind(Nios2::fixup_Nios2_PC16)));
  return 0;
}

/// getJumpTargetOpValue - Return binary encoding of the jump
/// target operand. If the machine operand requires relocation,
/// record the relocation and return zero.
unsigned Nios2MCCodeEmitter::
getJumpTargetOpValue(const MCInst &MI, unsigned OpNo,
                     SmallVectorImpl<MCFixup> &Fixups,
                     const MCSubtargetInfo &STI) const {

  const MCOperand &MO = MI.getOperand(OpNo);

  // If the destination is an immediate, we have nothing to do.
  if (MO.isImm()) return MO.getImm();

  assert(MO.isExpr() &&
         "getJumpTargetOpValue expects only expressions or an immediate");

  const MCExpr *Expr = MO.getExpr();
  Fixups.push_back(MCFixup::create(0, Expr,
                                   MCFixupKind(Nios2::fixup_Nios2_26)));
  return 0;
}

/// getMachineOpValue - Return binary encoding of operand. If the machine
/// operand requires relocation, record the relocation and return zero.
unsigned Nios2MCCodeEmitter::
getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                  SmallVectorImpl<MCFixup> &Fixups,
                  const MCSubtargetInfo &STI) const {
  if (MO.isReg()) {
    unsigned Reg = MO.getReg();
    unsigned RegNo = Ctx.getRegisterInfo()->getEncodingValue(Reg);
    return RegNo;
  } else if (MO.isImm()) {
    return static_cast<unsigned>(MO.getImm());
  } else if (MO.isFPImm()) {
    return static_cast<unsigned>(APFloat(MO.getFPImm())
        .bitcastToAPInt().getHiBits(32).getLimitedValue());
  }

  // MO must be an Expr.
  assert(MO.isExpr());

  const MCExpr *Expr = MO.getExpr();
  MCExpr::ExprKind Kind = Expr->getKind();

  if (Kind == MCExpr::Binary) {
    Expr = static_cast<const MCBinaryExpr*>(Expr)->getLHS();
    Kind = Expr->getKind();
  }

  assert (Kind == MCExpr::SymbolRef);

  Nios2::Fixups FixupKind = Nios2::Fixups(0);

  switch(cast<MCSymbolRefExpr>(Expr)->getKind()) {
  default: llvm_unreachable("Unknown fixup kind!");
    break;
  case MCSymbolRefExpr::VK_Mips_GPOFF_HI :
    FixupKind = Nios2::fixup_Nios2_GPOFF_HI;
    break;
  case MCSymbolRefExpr::VK_Mips_GPOFF_LO :
    FixupKind = Nios2::fixup_Nios2_GPOFF_LO;
    break;
  case MCSymbolRefExpr::VK_Mips_GOT_PAGE :
    FixupKind = Nios2::fixup_Nios2_GOT_PAGE;
    break;
  case MCSymbolRefExpr::VK_Mips_GOT_OFST :
    FixupKind = Nios2::fixup_Nios2_GOT_OFST;
    break;
  case MCSymbolRefExpr::VK_Mips_GOT_DISP :
    FixupKind = Nios2::fixup_Nios2_GOT_DISP;
    break;
  case MCSymbolRefExpr::VK_Mips_GPREL:
    FixupKind = Nios2::fixup_Nios2_GPREL16;
    break;
  case MCSymbolRefExpr::VK_Mips_GOT_CALL:
    FixupKind = Nios2::fixup_Nios2_CALL16;
    break;
  case MCSymbolRefExpr::VK_Mips_GOT16:
    FixupKind = Nios2::fixup_Nios2_GOT_Global;
    break;
  case MCSymbolRefExpr::VK_Mips_GOT:
    FixupKind = Nios2::fixup_Nios2_GOT_Local;
    break;
  case MCSymbolRefExpr::VK_Mips_ABS_HI:
    FixupKind = Nios2::fixup_Nios2_HI16;
    break;
  case MCSymbolRefExpr::VK_Mips_ABS_LO:
    FixupKind = Nios2::fixup_Nios2_LO16;
    break;
  case MCSymbolRefExpr::VK_Mips_TLSGD:
    FixupKind = Nios2::fixup_Nios2_TLSGD;
    break;
  case MCSymbolRefExpr::VK_Mips_TLSLDM:
    FixupKind = Nios2::fixup_Nios2_TLSLDM;
    break;
  case MCSymbolRefExpr::VK_Mips_DTPREL_HI:
    FixupKind = Nios2::fixup_Nios2_DTPREL_HI;
    break;
  case MCSymbolRefExpr::VK_Mips_DTPREL_LO:
    FixupKind = Nios2::fixup_Nios2_DTPREL_LO;
    break;
  case MCSymbolRefExpr::VK_Mips_GOTTPREL:
    FixupKind = Nios2::fixup_Nios2_GOTTPREL;
    break;
  case MCSymbolRefExpr::VK_Mips_TPREL_HI:
    FixupKind = Nios2::fixup_Nios2_TPREL_HI;
    break;
  case MCSymbolRefExpr::VK_Mips_TPREL_LO:
    FixupKind = Nios2::fixup_Nios2_TPREL_LO;
    break;
  case MCSymbolRefExpr::VK_Mips_HIGHER:
    FixupKind = Nios2::fixup_Nios2_HIGHER;
    break;
  case MCSymbolRefExpr::VK_Mips_HIGHEST:
    FixupKind = Nios2::fixup_Nios2_HIGHEST;
    break;
  case MCSymbolRefExpr::VK_Mips_GOT_HI16:
    FixupKind = Nios2::fixup_Nios2_GOT_HI16;
    break;
  case MCSymbolRefExpr::VK_Mips_GOT_LO16:
    FixupKind = Nios2::fixup_Nios2_GOT_LO16;
    break;
  case MCSymbolRefExpr::VK_Mips_CALL_HI16:
    FixupKind = Nios2::fixup_Nios2_CALL_HI16;
    break;
  case MCSymbolRefExpr::VK_Mips_CALL_LO16:
    FixupKind = Nios2::fixup_Nios2_CALL_LO16;
    break;
  } // switch

  Fixups.push_back(MCFixup::create(0, MO.getExpr(), MCFixupKind(FixupKind)));

  // All of the information is in the fixup.
  return 0;
}

/// getMemEncoding - Return binary encoding of memory related operand.
/// If the offset operand requires relocation, record the relocation.
unsigned
Nios2MCCodeEmitter::getMemEncoding(const MCInst &MI, unsigned OpNo,
                                  SmallVectorImpl<MCFixup> &Fixups,
                                  const MCSubtargetInfo &STI) const {
  // Base register is encoded in bits 20-16, offset is encoded in bits 15-0.
  assert(MI.getOperand(OpNo).isReg());
  unsigned RegBits = getMachineOpValue(MI, MI.getOperand(OpNo), Fixups, STI) << 16;
  unsigned OffBits = getMachineOpValue(MI, MI.getOperand(OpNo+1), Fixups, STI);

  return (OffBits & 0xFFFF) | RegBits;
}

unsigned
Nios2MCCodeEmitter::getSizeExtEncoding(const MCInst &MI, unsigned OpNo,
                                      SmallVectorImpl<MCFixup> &Fixups,
                                      const MCSubtargetInfo &STI) const {
  assert(MI.getOperand(OpNo).isImm());
  unsigned SizeEncoding = getMachineOpValue(MI, MI.getOperand(OpNo), Fixups, STI);
  return SizeEncoding - 1;
}

// FIXME: should be called getMSBEncoding
//
unsigned
Nios2MCCodeEmitter::getSizeInsEncoding(const MCInst &MI, unsigned OpNo,
                                      SmallVectorImpl<MCFixup> &Fixups,
                                      const MCSubtargetInfo &STI) const {
  assert(MI.getOperand(OpNo-1).isImm());
  assert(MI.getOperand(OpNo).isImm());
  unsigned Position = getMachineOpValue(MI, MI.getOperand(OpNo-1), Fixups, STI);
  unsigned Size = getMachineOpValue(MI, MI.getOperand(OpNo), Fixups, STI);

  return Position + Size - 1;
}

#include "Nios2GenMCCodeEmitter.inc"

