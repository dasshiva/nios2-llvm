//===-- Nios2ASMBackend.cpp - Nios2 Asm Backend  ----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Nios2AsmBackend and Nios2ELFObjectWriter classes.
//
//===----------------------------------------------------------------------===//
//

#include "Nios2FixupKinds.h"
#include "MCTargetDesc/Nios2MCTargetDesc.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixupKindInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

// Prepare value for the target space for it
static unsigned adjustFixupValue(unsigned Kind, uint64_t Value) {

  // Add/subtract and shift
  switch (Kind) {
  default:
    return 0;
  case FK_GPRel_4:
  case FK_Data_4:
  case FK_Data_8:
  case Nios2::fixup_Nios2_LO16:
  case Nios2::fixup_Nios2_GPREL16:
  case Nios2::fixup_Nios2_GPOFF_HI:
  case Nios2::fixup_Nios2_GPOFF_LO:
  case Nios2::fixup_Nios2_GOT_PAGE:
  case Nios2::fixup_Nios2_GOT_OFST:
  case Nios2::fixup_Nios2_GOT_DISP:
  case Nios2::fixup_Nios2_GOT_LO16:
  case Nios2::fixup_Nios2_CALL_LO16:
    break;
  case Nios2::fixup_Nios2_PC16:
    // So far we are only using this type for branches.
    // For branches we start 1 instruction after the branch
    // so the displacement will be one instruction size less.
    Value -= 4;
    // The displacement is then divided by 4 to give us an 18 bit
    // address range.
    Value >>= 2;
    break;
  case Nios2::fixup_Nios2_26:
    // So far we are only using this type for jumps.
    // The displacement is then divided by 4 to give us an 28 bit
    // address range.
    Value >>= 2;
    break;
  case Nios2::fixup_Nios2_HI16:
  case Nios2::fixup_Nios2_GOT_Local:
  case Nios2::fixup_Nios2_GOT_HI16:
  case Nios2::fixup_Nios2_CALL_HI16:
    // Get the 2nd 16-bits. Also add 1 if bit 15 is 1.
    Value = ((Value + 0x8000) >> 16) & 0xffff;
    break;
  case Nios2::fixup_Nios2_HIGHER:
    // Get the 3rd 16-bits.
    Value = ((Value + 0x80008000LL) >> 32) & 0xffff;
    break;
  case Nios2::fixup_Nios2_HIGHEST:
    // Get the 4th 16-bits.
    Value = ((Value + 0x800080008000LL) >> 48) & 0xffff;
    break;
  }

  return Value;
}

namespace {
class Nios2AsmBackend : public MCAsmBackend {
  Triple::OSType OSType;
  bool IsLittle; // Big or little endian
  bool Is64Bit;  // 32 or 64 bit words

public:
  Nios2AsmBackend(const Target &T,  Triple::OSType _OSType,
                 bool _isLittle, bool _is64Bit)
    : MCAsmBackend(), OSType(_OSType), IsLittle(_isLittle), Is64Bit(_is64Bit) {}

  MCObjectWriter *createObjectWriter(raw_pwrite_stream &OS) const override {
    return createNios2ELFObjectWriter(OS,
      MCELFObjectTargetWriter::getOSABI(OSType), IsLittle, Is64Bit);
  }

  /// ApplyFixup - Apply the \p Value for given \p Fixup into the provided
  /// data fragment, at the offset specified by the fixup and following the
  /// fixup kind as appropriate.
  void applyFixup(const MCFixup &Fixup, char *Data, unsigned DataSize,
                  uint64_t Value, bool isPCRel) const override {
    MCFixupKind Kind = Fixup.getKind();
    Value = adjustFixupValue((unsigned)Kind, Value);

    if (!Value)
      return; // Doesn't change encoding.

    // Where do we start in the object
    unsigned Offset = Fixup.getOffset();
    // Number of bytes we need to fixup
    unsigned NumBytes = (getFixupKindInfo(Kind).TargetSize + 7) / 8;
    // Used to point to big endian bytes
    unsigned FullSize;

    switch ((unsigned)Kind) {
    case Nios2::fixup_Nios2_16:
      FullSize = 2;
      break;
    case Nios2::fixup_Nios2_64:
      FullSize = 8;
      break;
    default:
      FullSize = 4;
      break;
    }

    // Grab current value, if any, from bits.
    uint64_t CurVal = 0;

    for (unsigned i = 0; i != NumBytes; ++i) {
      unsigned Idx = IsLittle ? i : (FullSize - 1 - i);
      CurVal |= (uint64_t)((uint8_t)Data[Offset + Idx]) << (i*8);
    }

    uint64_t Mask = ((uint64_t)(-1) >>
                     (64 - getFixupKindInfo(Kind).TargetSize));
    CurVal |= Value & Mask;

    // Write out the fixed up bytes back to the code/data bits.
    for (unsigned i = 0; i != NumBytes; ++i) {
      unsigned Idx = IsLittle ? i : (FullSize - 1 - i);
      Data[Offset + Idx] = (uint8_t)((CurVal >> (i*8)) & 0xff);
    }
  }

  unsigned getNumFixupKinds() const override { return Nios2::NumTargetFixupKinds; }

  const MCFixupKindInfo &getFixupKindInfo(MCFixupKind Kind) const {
    const static MCFixupKindInfo Infos[Nios2::NumTargetFixupKinds] = {
      // This table *must* be in same the order of fixup_* kinds in
      // Nios2FixupKinds.h.
      //
      // name                    offset  bits  flags
      { "fixup_Nios2_16",           0,     16,   0 },
      { "fixup_Nios2_32",           0,     32,   0 },
      { "fixup_Nios2_REL32",        0,     32,   0 },
      { "fixup_Nios2_26",           0,     26,   0 },
      { "fixup_Nios2_HI16",         0,     16,   0 },
      { "fixup_Nios2_LO16",         0,     16,   0 },
      { "fixup_Nios2_GPREL16",      0,     16,   0 },
      { "fixup_Nios2_LITERAL",      0,     16,   0 },
      { "fixup_Nios2_GOT_Global",   0,     16,   0 },
      { "fixup_Nios2_GOT_Local",    0,     16,   0 },
      { "fixup_Nios2_PC16",         0,     16,  MCFixupKindInfo::FKF_IsPCRel },
      { "fixup_Nios2_CALL16",       0,     16,   0 },
      { "fixup_Nios2_GPREL32",      0,     32,   0 },
      { "fixup_Nios2_SHIFT5",       6,      5,   0 },
      { "fixup_Nios2_SHIFT6",       6,      5,   0 },
      { "fixup_Nios2_64",           0,     64,   0 },
      { "fixup_Nios2_TLSGD",        0,     16,   0 },
      { "fixup_Nios2_GOTTPREL",     0,     16,   0 },
      { "fixup_Nios2_TPREL_HI",     0,     16,   0 },
      { "fixup_Nios2_TPREL_LO",     0,     16,   0 },
      { "fixup_Nios2_TLSLDM",       0,     16,   0 },
      { "fixup_Nios2_DTPREL_HI",    0,     16,   0 },
      { "fixup_Nios2_DTPREL_LO",    0,     16,   0 },
      { "fixup_Nios2_Branch_PCRel", 0,     16,  MCFixupKindInfo::FKF_IsPCRel },
      { "fixup_Nios2_GPOFF_HI",     0,     16,   0 },
      { "fixup_Nios2_GPOFF_LO",     0,     16,   0 },
      { "fixup_Nios2_GOT_PAGE",     0,     16,   0 },
      { "fixup_Nios2_GOT_OFST",     0,     16,   0 },
      { "fixup_Nios2_GOT_DISP",     0,     16,   0 },
      { "fixup_Nios2_HIGHER",       0,     16,   0 },
      { "fixup_Nios2_HIGHEST",      0,     16,   0 },
      { "fixup_Nios2_GOT_HI16",     0,     16,   0 },
      { "fixup_Nios2_GOT_LO16",     0,     16,   0 },
      { "fixup_Nios2_CALL_HI16",    0,     16,   0 },
      { "fixup_Nios2_CALL_LO16",    0,     16,   0 }
    };

    if (Kind < FirstTargetFixupKind)
      return MCAsmBackend::getFixupKindInfo(Kind);

    assert(unsigned(Kind - FirstTargetFixupKind) < getNumFixupKinds() &&
           "Invalid kind!");
    return Infos[Kind - FirstTargetFixupKind];
  }

  /// @name Target Relaxation Interfaces
  /// @{

  /// MayNeedRelaxation - Check whether the given instruction may need
  /// relaxation.
  ///
  /// \param Inst - The instruction to test.
  bool mayNeedRelaxation(const MCInst &Inst) const override {
    return false;
  }

  /// fixupNeedsRelaxation - Target specific predicate for whether a given
  /// fixup requires the associated instruction to be relaxed.
  bool fixupNeedsRelaxation(const MCFixup &Fixup,
                            uint64_t Value,
                            const MCRelaxableFragment *DF,
                            const MCAsmLayout &Layout) const override {
    // FIXME.
    assert(0 && "RelaxInstruction() unimplemented");
    return false;
  }

  /// RelaxInstruction - Relax the instruction in the given fragment
  /// to the next wider instruction.
  ///
  /// \param Inst - The instruction to relax, which may be the same
  /// as the output.
  /// \param [out] Res On return, the relaxed instruction.
  void relaxInstruction(const MCInst &Inst, MCInst &Res) const override {
  }

  /// @}

  /// WriteNopData - Write an (optimal) nop sequence of Count bytes
  /// to the given output. If the target cannot generate such a sequence,
  /// it should return an error.
  ///
  /// \return - True on success.
  bool writeNopData(uint64_t Count, MCObjectWriter *OW) const override {
    // Check for a less than instruction size number of bytes
    // FIXME: 16 bit instructions are not handled yet here.
    // We shouldn't be using a hard coded number for instruction size.
    if (Count % 4) return false;

    uint64_t NumNops = Count / 4;
    for (uint64_t i = 0; i != NumNops; ++i)
      OW->write32(0);
    return true;
  }
}; // class Nios2AsmBackend

} // namespace

// MCAsmBackend
MCAsmBackend *llvm::createNios2AsmBackend(const Target &T,
                                          const MCRegisterInfo &MRI,
                                          const Triple &TT, StringRef CPU) {
  return new Nios2AsmBackend(T, TT.getOS(), /*IsLittle*/true, /*Is64Bit*/false);
}

