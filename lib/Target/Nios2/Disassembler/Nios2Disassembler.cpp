#include "Nios2.h"
#include "Nios2RegisterInfo.h"
#include "Nios2Subtarget.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler.h"
#include "llvm/MC/MCFixedLenDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/TargetRegistry.h"


using namespace llvm;
#define DEBUG_TYPE "nios2-disassembler"

typedef MCDisassembler::DecodeStatus DecodeStatus;

namespace {
  class Nios2Disassembler : public MCDisassembler {
    public:
    Nios2Disassembler(const MCSubtargetInfo &STI, MCContext &Ctx) 
      : MCDisassembler(STI, Ctx) {}

    DecodeStatus getInstruction(MCInst &Instr, uint64_t &Size,
        ArrayRef<uint8_t> Bytes, uint64_t Address, 
        raw_ostream &VStream, raw_ostream &CStream) const override;
    ~Nios2Disassembler() override {}
  };
}

static MCDisassembler *createNios2Disassembler(
              const Target &T,
              const MCSubtargetInfo &STI,
              MCContext &Ctx) {
  return new Nios2Disassembler(STI, Ctx);
}

extern "C" void LLVMInitializeNios2Disassembler() {
  TargetRegistry::RegisterMCDisassembler(llvm::TheNios2StdTarget,
      createNios2Disassembler);
}

#define OPCODE_OPX 0x3A
#define OPCODE(x) (x & ((1 << 6) - 1))
enum OPX {
  MIN_OPX     = 0,
  OPX_ERET    = 0x01,
  OPX_ROLI    = 0x02,
  OPX_ROL     = 0x03,
  OPX_FLUSHP  = 0x04,
  OPX_RET     = 0x05,
  OPX_NOR     = 0x06,
  OPX_MULXUU  = 0x07,
  OPX_CMPGE   = 0x08,
  OPX_BRET    = 0x09,
  OPX_ROR     = 0x0B,
  OPX_FLUSHI  = 0x0C,
  OPX_JMP     = 0x0D,
  OPX_AND     = 0x0E,
  OPX_CMPLT   = 0x10,
  OPX_SLLI    = 0x12,
  OPX_SLL     = 0x13,
  OPX_WRPRS   = 0x14,
  OPX_OR      = 0x16,
  OPX_MULXSU  = 0x17,
  OPX_CMPNE   = 0x18,
  OPX_SRLI    = 0x1A,
  OPX_SRL     = 0x1B,
  OPX_NEXTPC  = 0x1C,
  OPX_CALLR   = 0x1D,
  OPX_XOR     = 0x1E,
  OPX_MULXSS  = 0x1F,
  OPX_CMPEQ   = 0x20,
  OPX_DIVU    = 0x24,
  OPX_DIV     = 0x25,
  OPX_RDCTL   = 0x26,
  OPX_MUL     = 0x27,
  OPX_CMPGEu  = 0x28,
  OPX_INITI   = 0x29,
  OPX_TRAP    = 0x2D,
  OPX_WRCTL   = 0x2E,
  OPX_CMPLTu  = 0x30,
  OPX_ADD     = 0x31,
  OPX_BREAK   = 0x34,
  OPX_SYNC    = 0x36,
  OPX_SUB     = 0x39,
  OPX_SRAI    = 0x3A,
  OPX_SRA     = 0x3B,
  MAX_OPX
};

uint8_t convertOpcodeRegToLLVM(uint8_t reg) {
  if (!reg) // this is the Zero Register
    return Nios2::ZERO;

  if (reg == 1) // this is AT
    return Nios2::AT;

  // Only R2-R23 are defined by numbers others have names
  if (reg >= 2 && reg <= 23)
    return (reg - 2) + Nios2::R2;

  // At this point, we have reg > 23 && reg < 31
  return (reg - 24) + Nios2::BA;
}


using namespace Nios2;
#define NO_IMM_OPX(op) \
  case OPX_##op: \
  Instr.setOpcode(op); \
  break;

DecodeStatus decodeOPX(MCInst& Instr, uint64_t &Size, 
                      uint64_t Address, uint32_t Insn) {
  /* Some R-Type instructions embed a small immediate value in the 
    five low-order bits of OPX */
  uint8_t maybe_imm5 = Insn & 31;
  Insn >>= 5;
  uint8_t opx = Insn & 63;
  if (opx <= MIN_OPX || opx >= MAX_OPX) {
    Size = 0;
    return MCDisassembler::Fail;
  }

  Insn >>= 6; // Get rid of opx now

  switch (opx) {
    default:
      Size = 0;
      return MCDisassembler::Fail;
    NO_IMM_OPX(ADD);
    NO_IMM_OPX(SUB);
    NO_IMM_OPX(MUL);
    NO_IMM_OPX(DIV);
    NO_IMM_OPX(DIVU);
    NO_IMM_OPX(OR);
    NO_IMM_OPX(AND);
    NO_IMM_OPX(SRA);
    NO_IMM_OPX(SRL);
    NO_IMM_OPX(MULXSS);
    NO_IMM_OPX(MULXSU);
    NO_IMM_OPX(SLL);
    NO_IMM_OPX(ROL);
    NO_IMM_OPX(MULXUU);
    NO_IMM_OPX(CMPGE);
    NO_IMM_OPX(ROR);
    NO_IMM_OPX(CMPLT);
    NO_IMM_OPX(CMPNE);
    NO_IMM_OPX(CMPEQ);
    NO_IMM_OPX(CMPGEu);
    NO_IMM_OPX(CMPLTu);
    case OPX_RET: {
      if (((Insn >> 10) & 31) != 0x1F ||
          ((Insn >> 5) & 31) != 0 ||
          (Insn & 31) != 0) {
        Size = 0;
        return MCDisassembler::Fail;
      }

      Instr.setOpcode(Nios2::RET);
      goto done;
    }

    case OPX_ERET: {
      if (((Insn >> 10) & 31) != 0x1D ||
          ((Insn >> 5) & 31) != 0x1E) {
        Size = 0;
        return MCDisassembler::Fail;
      }

      Instr.setOpcode(Nios2::ERET);
      goto done;
    }
  }

  // Now, we only have the three registers A, B, C left to deal with
  // However it is not necessary that these fields will always have a meaning
  if (maybe_imm5 & (1 << 7)) { // This instruction has a 5 bit immediate
  }
  else {
    uint8_t rA = (Insn >> 10) & 31; // A
    uint8_t rB = (Insn >> 5)  & 31; // B
    uint8_t rC = Insn & 31;
    Instr.addOperand(MCOperand::createReg(convertOpcodeRegToLLVM(rC)));
    Instr.addOperand(MCOperand::createReg(convertOpcodeRegToLLVM(rA)));
    Instr.addOperand(MCOperand::createReg(convertOpcodeRegToLLVM(rB)));
  }

done:
  Size = 4;
  return MCDisassembler::Success;
}

DecodeStatus Nios2Disassembler::getInstruction(MCInst &Instr, uint64_t &Size,
                                              ArrayRef<uint8_t> Bytes,
                                              uint64_t Address,
                                              raw_ostream &VStream,
                                              raw_ostream &Cstream) const {
  uint32_t Insn = 0;
  // All Nios2 instructions are 4 bytes
  if (Bytes.size() < 4) {
    Size = 0;
    return MCDisassembler::Fail;
  }

  // We are always little endian
  Insn = ((Bytes[0] << 0) | (Bytes[1] << 8) | (Bytes[2] << 16) | (Bytes[3] << 24));
  uint8_t opcode = OPCODE(Insn);
  Insn >>= 6; // Remove opcode, we don't need it anymore
  switch (opcode) {
    default: 
      Size = 0;
      return MCDisassembler::Fail;
    case OPCODE_OPX: {
      return decodeOPX(Instr, Size, Address, Insn);
      break;
    }
  };
  return MCDisassembler::Fail;
}
