//===-- Nios2MCAsmInfo.h - Nios2 Asm Info ------------------------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the Nios2MCAsmInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef NIOS2TARGETASMINFO_H
#define NIOS2TARGETASMINFO_H

#include "llvm/MC/MCAsmInfo.h"

namespace llvm {
  class StringRef;
  class Triple;

  class Nios2MCAsmInfo : public MCAsmInfo {
    virtual void anchor();
  public:
    explicit Nios2MCAsmInfo(const Triple &TT);
  };

} // namespace llvm

#endif
