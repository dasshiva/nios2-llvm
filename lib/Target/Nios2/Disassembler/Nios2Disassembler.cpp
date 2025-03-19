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

namespace {
  extern Target TheNios2StdTarget;
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

DecodeStatus Nios2Disassembler::getInstruction(MCInst &Instr, uint64_t &Size,
                                              ArrayRef<uint8_t> Bytes,
                                              uint64_t Address,
                                              raw_ostream &VStream,
                                              raw_ostream &Cstream) const {
  // Stub function
  return MCDisassembler::Fail;
}
