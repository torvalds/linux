//===-- llvm/MC/MCXCOFFObjectWriter.h - XCOFF Object Writer ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCXCOFFOBJECTWRITER_H
#define LLVM_MC_MCXCOFFOBJECTWRITER_H

#include "llvm/MC/MCObjectWriter.h"

namespace llvm {

class raw_pwrite_stream;

class MCXCOFFObjectTargetWriter : public MCObjectTargetWriter {
protected:
  MCXCOFFObjectTargetWriter(bool Is64Bit);

public:
  ~MCXCOFFObjectTargetWriter() override;

  Triple::ObjectFormatType getFormat() const override { return Triple::XCOFF; }
  static bool classof(const MCObjectTargetWriter *W) {
    return W->getFormat() == Triple::XCOFF;
  }
  bool is64Bit() const { return Is64Bit; }

  // Returns relocation info such as type, sign and size.
  // First element of the pair contains type,
  // second element contains sign and size.
  virtual std::pair<uint8_t, uint8_t>
  getRelocTypeAndSignSize(const MCValue &Target, const MCFixup &Fixup,
                          bool IsPCRel) const = 0;

private:
  bool Is64Bit;
};

std::unique_ptr<MCObjectWriter>
createXCOFFObjectWriter(std::unique_ptr<MCXCOFFObjectTargetWriter> MOTW,
                        raw_pwrite_stream &OS);

namespace XCOFF {
void addExceptionEntry(MCObjectWriter &Writer, const MCSymbol *Symbol,
                       const MCSymbol *Trap, unsigned LanguageCode,
                       unsigned ReasonCode, unsigned FunctionSize,
                       bool hasDebug);
void addCInfoSymEntry(MCObjectWriter &Writer, StringRef Name,
                      StringRef Metadata);
} // namespace XCOFF

} // end namespace llvm

#endif // LLVM_MC_MCXCOFFOBJECTWRITER_H
