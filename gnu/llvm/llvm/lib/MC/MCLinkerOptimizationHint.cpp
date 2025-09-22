//===- llvm/MC/MCLinkerOptimizationHint.cpp ----- LOH handling ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCLinkerOptimizationHint.h"
#include "llvm/MC/MCMachObjectWriter.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/raw_ostream.h"
#include <cstddef>
#include <cstdint>

using namespace llvm;

// Each LOH is composed by, in this order (each field is encoded using ULEB128):
// - Its kind.
// - Its number of arguments (let say N).
// - Its arg1.
// - ...
// - Its argN.
// <arg1> to <argN> are absolute addresses in the object file, i.e.,
// relative addresses from the beginning of the object file.
void MCLOHDirective::emit_impl(const MCAssembler &Asm, raw_ostream &OutStream,
                               const MachObjectWriter &ObjWriter

) const {
  encodeULEB128(Kind, OutStream);
  encodeULEB128(Args.size(), OutStream);
  for (const MCSymbol *Arg : Args)
    encodeULEB128(ObjWriter.getSymbolAddress(*Arg, Asm), OutStream);
}

void MCLOHDirective::emit(const MCAssembler &Asm,
                          MachObjectWriter &ObjWriter) const {
  raw_ostream &OutStream = ObjWriter.W.OS;
  emit_impl(Asm, OutStream, ObjWriter);
}

uint64_t MCLOHDirective::getEmitSize(const MCAssembler &Asm,
                                     const MachObjectWriter &ObjWriter) const {
  class raw_counting_ostream : public raw_ostream {
    uint64_t Count = 0;

    void write_impl(const char *, size_t size) override { Count += size; }

    uint64_t current_pos() const override { return Count; }

  public:
    raw_counting_ostream() = default;
    ~raw_counting_ostream() override { flush(); }
  };

  raw_counting_ostream OutStream;
  emit_impl(Asm, OutStream, ObjWriter);
  return OutStream.tell();
}
