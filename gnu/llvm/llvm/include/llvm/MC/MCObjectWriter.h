//===- llvm/MC/MCObjectWriter.h - Object File Writer Interface --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCOBJECTWRITER_H
#define LLVM_MC_MCOBJECTWRITER_H

#include "llvm/MC/MCSymbol.h"
#include "llvm/TargetParser/Triple.h"
#include <cstdint>

namespace llvm {

class MCAssembler;
class MCFixup;
class MCFragment;
class MCSymbol;
class MCSymbolRefExpr;
class MCValue;

/// Defines the object file and target independent interfaces used by the
/// assembler backend to write native file format object files.
///
/// The object writer contains a few callbacks used by the assembler to allow
/// the object writer to modify the assembler data structures at appropriate
/// points. Once assembly is complete, the object writer is given the
/// MCAssembler instance, which contains all the symbol and section data which
/// should be emitted as part of writeObject().
class MCObjectWriter {
protected:
  /// List of declared file names
  SmallVector<std::pair<std::string, size_t>, 0> FileNames;
  // XCOFF specific: Optional compiler version.
  std::string CompilerVersion;
  std::vector<const MCSymbol *> AddrsigSyms;
  bool EmitAddrsigSection = false;
  bool SubsectionsViaSymbols = false;

  struct CGProfileEntry {
    const MCSymbolRefExpr *From;
    const MCSymbolRefExpr *To;
    uint64_t Count;
  };
  SmallVector<CGProfileEntry, 0> CGProfile;

  MCObjectWriter() = default;

public:
  MCObjectWriter(const MCObjectWriter &) = delete;
  MCObjectWriter &operator=(const MCObjectWriter &) = delete;
  virtual ~MCObjectWriter();

  /// lifetime management
  virtual void reset();

  /// \name High-Level API
  /// @{

  /// Perform any late binding of symbols (for example, to assign symbol
  /// indices for use when generating relocations).
  ///
  /// This routine is called by the assembler after layout and relaxation is
  /// complete.
  virtual void executePostLayoutBinding(MCAssembler &Asm) {}

  /// Record a relocation entry.
  ///
  /// This routine is called by the assembler after layout and relaxation, and
  /// post layout binding. The implementation is responsible for storing
  /// information about the relocation so that it can be emitted during
  /// writeObject().
  virtual void recordRelocation(MCAssembler &Asm, const MCFragment *Fragment,
                                const MCFixup &Fixup, MCValue Target,
                                uint64_t &FixedValue) = 0;

  /// Check whether the difference (A - B) between two symbol references is
  /// fully resolved.
  ///
  /// Clients are not required to answer precisely and may conservatively return
  /// false, even when a difference is fully resolved.
  bool isSymbolRefDifferenceFullyResolved(const MCAssembler &Asm,
                                          const MCSymbolRefExpr *A,
                                          const MCSymbolRefExpr *B,
                                          bool InSet) const;

  virtual bool isSymbolRefDifferenceFullyResolvedImpl(const MCAssembler &Asm,
                                                      const MCSymbol &SymA,
                                                      const MCFragment &FB,
                                                      bool InSet,
                                                      bool IsPCRel) const;

  MutableArrayRef<std::pair<std::string, size_t>> getFileNames() {
    return FileNames;
  }
  void addFileName(MCAssembler &Asm, StringRef FileName);
  void setCompilerVersion(StringRef CompilerVers) {
    CompilerVersion = CompilerVers;
  }

  /// Tell the object writer to emit an address-significance table during
  /// writeObject(). If this function is not called, all symbols are treated as
  /// address-significant.
  void emitAddrsigSection() { EmitAddrsigSection = true; }

  bool getEmitAddrsigSection() { return EmitAddrsigSection; }

  /// Record the given symbol in the address-significance table to be written
  /// diring writeObject().
  void addAddrsigSymbol(const MCSymbol *Sym) { AddrsigSyms.push_back(Sym); }

  std::vector<const MCSymbol *> &getAddrsigSyms() { return AddrsigSyms; }
  SmallVector<CGProfileEntry, 0> &getCGProfile() { return CGProfile; }

  // Mach-O specific: Whether .subsections_via_symbols is enabled.
  bool getSubsectionsViaSymbols() const { return SubsectionsViaSymbols; }
  void setSubsectionsViaSymbols(bool Value) { SubsectionsViaSymbols = Value; }

  /// Write the object file and returns the number of bytes written.
  ///
  /// This routine is called by the assembler after layout and relaxation is
  /// complete, fixups have been evaluated and applied, and relocations
  /// generated.
  virtual uint64_t writeObject(MCAssembler &Asm) = 0;

  /// @}
};

/// Base class for classes that define behaviour that is specific to both the
/// target and the object format.
class MCObjectTargetWriter {
public:
  virtual ~MCObjectTargetWriter() = default;
  virtual Triple::ObjectFormatType getFormat() const = 0;
};

} // end namespace llvm

#endif // LLVM_MC_MCOBJECTWRITER_H
