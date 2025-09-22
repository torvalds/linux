//===- llvm/MC/MCWinCOFFObjectWriter.h - Win COFF Object Writer -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCWINCOFFOBJECTWRITER_H
#define LLVM_MC_MCWINCOFFOBJECTWRITER_H

#include "llvm/MC/MCObjectWriter.h"
#include <memory>

namespace llvm {

class MCAsmBackend;
class MCContext;
class MCFixup;
class MCValue;
class raw_pwrite_stream;

class MCWinCOFFObjectTargetWriter : public MCObjectTargetWriter {
  virtual void anchor();

  const unsigned Machine;

protected:
  MCWinCOFFObjectTargetWriter(unsigned Machine_);

public:
  virtual ~MCWinCOFFObjectTargetWriter() = default;

  Triple::ObjectFormatType getFormat() const override { return Triple::COFF; }
  static bool classof(const MCObjectTargetWriter *W) {
    return W->getFormat() == Triple::COFF;
  }

  unsigned getMachine() const { return Machine; }
  virtual unsigned getRelocType(MCContext &Ctx, const MCValue &Target,
                                const MCFixup &Fixup, bool IsCrossSection,
                                const MCAsmBackend &MAB) const = 0;
  virtual bool recordRelocation(const MCFixup &) const { return true; }
};

class WinCOFFWriter;

class WinCOFFObjectWriter : public MCObjectWriter {
  friend class WinCOFFWriter;

  std::unique_ptr<MCWinCOFFObjectTargetWriter> TargetObjectWriter;
  std::unique_ptr<WinCOFFWriter> ObjWriter, DwoWriter;
  bool IncrementalLinkerCompatible = false;

public:
  WinCOFFObjectWriter(std::unique_ptr<MCWinCOFFObjectTargetWriter> MOTW,
                      raw_pwrite_stream &OS);
  WinCOFFObjectWriter(std::unique_ptr<MCWinCOFFObjectTargetWriter> MOTW,
                      raw_pwrite_stream &OS, raw_pwrite_stream &DwoOS);

  // MCObjectWriter interface implementation.
  void reset() override;
  void setIncrementalLinkerCompatible(bool Value) {
    IncrementalLinkerCompatible = Value;
  }
  void executePostLayoutBinding(MCAssembler &Asm) override;
  bool isSymbolRefDifferenceFullyResolvedImpl(const MCAssembler &Asm,
                                              const MCSymbol &SymA,
                                              const MCFragment &FB, bool InSet,
                                              bool IsPCRel) const override;
  void recordRelocation(MCAssembler &Asm, const MCFragment *Fragment,
                        const MCFixup &Fixup, MCValue Target,
                        uint64_t &FixedValue) override;
  uint64_t writeObject(MCAssembler &Asm) override;
};

/// Construct a new Win COFF writer instance.
///
/// \param MOTW - The target specific WinCOFF writer subclass.
/// \param OS - The stream to write to.
/// \returns The constructed object writer.
std::unique_ptr<MCObjectWriter>
createWinCOFFObjectWriter(std::unique_ptr<MCWinCOFFObjectTargetWriter> MOTW,
                          raw_pwrite_stream &OS);

std::unique_ptr<MCObjectWriter>
createWinCOFFDwoObjectWriter(std::unique_ptr<MCWinCOFFObjectTargetWriter> MOTW,
                             raw_pwrite_stream &OS, raw_pwrite_stream &DwoOS);
} // end namespace llvm

#endif // LLVM_MC_MCWINCOFFOBJECTWRITER_H
