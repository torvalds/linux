//===- llvm/MC/MCMachObjectWriter.h - Mach Object Writer --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCMACHOBJECTWRITER_H
#define LLVM_MC_MCMACHOBJECTWRITER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCLinkerOptimizationHint.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/StringTableBuilder.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/VersionTuple.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace llvm {

class MachObjectWriter;

class MCMachObjectTargetWriter : public MCObjectTargetWriter {
  const unsigned Is64Bit : 1;
  const uint32_t CPUType;
protected:
  uint32_t CPUSubtype;
public:
  unsigned LocalDifference_RIT = 0;

protected:
  MCMachObjectTargetWriter(bool Is64Bit_, uint32_t CPUType_,
                           uint32_t CPUSubtype_);

  void setLocalDifferenceRelocationType(unsigned Type) {
    LocalDifference_RIT = Type;
  }

public:
  virtual ~MCMachObjectTargetWriter();

  Triple::ObjectFormatType getFormat() const override { return Triple::MachO; }
  static bool classof(const MCObjectTargetWriter *W) {
    return W->getFormat() == Triple::MachO;
  }

  /// \name Lifetime Management
  /// @{

  virtual void reset() {}

  /// @}

  /// \name Accessors
  /// @{

  bool is64Bit() const { return Is64Bit; }
  uint32_t getCPUType() const { return CPUType; }
  uint32_t getCPUSubtype() const { return CPUSubtype; }
  unsigned getLocalDifferenceRelocationType() const {
    return LocalDifference_RIT;
  }

  /// @}

  /// \name API
  /// @{

  virtual void recordRelocation(MachObjectWriter *Writer, MCAssembler &Asm,
                                const MCFragment *Fragment,
                                const MCFixup &Fixup, MCValue Target,
                                uint64_t &FixedValue) = 0;

  /// @}
};

class MachObjectWriter : public MCObjectWriter {
public:
  struct DataRegionData {
    MachO::DataRegionType Kind;
    MCSymbol *Start;
    MCSymbol *End;
  };

  // A Major version of 0 indicates that no version information was supplied
  // and so the corresponding load command should not be emitted.
  using VersionInfoType = struct {
    bool EmitBuildVersion;
    union {
      MCVersionMinType Type;        ///< Used when EmitBuildVersion==false.
      MachO::PlatformType Platform; ///< Used when EmitBuildVersion==true.
    } TypeOrPlatform;
    unsigned Major;
    unsigned Minor;
    unsigned Update;
    /// An optional version of the SDK that was used to build the source.
    VersionTuple SDKVersion;
  };

private:
  /// Helper struct for containing some precomputed information on symbols.
  struct MachSymbolData {
    const MCSymbol *Symbol;
    uint64_t StringIndex;
    uint8_t SectionIndex;

    // Support lexicographic sorting.
    bool operator<(const MachSymbolData &RHS) const;
  };

  struct IndirectSymbolData {
    MCSymbol *Symbol;
    MCSection *Section;
  };

  /// The target specific Mach-O writer instance.
  std::unique_ptr<MCMachObjectTargetWriter> TargetObjectWriter;

  /// \name Relocation Data
  /// @{

  struct RelAndSymbol {
    const MCSymbol *Sym;
    MachO::any_relocation_info MRE;
    RelAndSymbol(const MCSymbol *Sym, const MachO::any_relocation_info &MRE)
        : Sym(Sym), MRE(MRE) {}
  };

  DenseMap<const MCSection *, std::vector<RelAndSymbol>> Relocations;
  std::vector<IndirectSymbolData> IndirectSymbols;
  DenseMap<const MCSection *, unsigned> IndirectSymBase;

  std::vector<DataRegionData> DataRegions;

  SectionAddrMap SectionAddress;

  // List of sections in layout order. Virtual sections are after non-virtual
  // sections.
  SmallVector<MCSection *, 0> SectionOrder;

  /// @}
  /// \name Symbol Table Data
  /// @{

  StringTableBuilder StringTable;
  std::vector<MachSymbolData> LocalSymbolData;
  std::vector<MachSymbolData> ExternalSymbolData;
  std::vector<MachSymbolData> UndefinedSymbolData;

  /// @}

  // Used to communicate Linker Optimization Hint information.
  MCLOHContainer LOHContainer;

  VersionInfoType VersionInfo{};
  VersionInfoType TargetVariantVersionInfo{};

  // The list of linker options for LC_LINKER_OPTION.
  std::vector<std::vector<std::string>> LinkerOptions;

  MachSymbolData *findSymbolData(const MCSymbol &Sym);

  void writeWithPadding(StringRef Str, uint64_t Size);

public:
  MachObjectWriter(std::unique_ptr<MCMachObjectTargetWriter> MOTW,
                   raw_pwrite_stream &OS, bool IsLittleEndian)
      : TargetObjectWriter(std::move(MOTW)),
        StringTable(TargetObjectWriter->is64Bit() ? StringTableBuilder::MachO64
                                                  : StringTableBuilder::MachO),
        W(OS,
          IsLittleEndian ? llvm::endianness::little : llvm::endianness::big) {}

  support::endian::Writer W;

  const MCSymbol &findAliasedSymbol(const MCSymbol &Sym) const;

  /// \name Lifetime management Methods
  /// @{

  void reset() override;

  /// @}

  /// \name Utility Methods
  /// @{

  bool isFixupKindPCRel(const MCAssembler &Asm, unsigned Kind);

  std::vector<IndirectSymbolData> &getIndirectSymbols() {
    return IndirectSymbols;
  }
  std::vector<DataRegionData> &getDataRegions() { return DataRegions; }
  const llvm::SmallVectorImpl<MCSection *> &getSectionOrder() const {
    return SectionOrder;
  }
  SectionAddrMap &getSectionAddressMap() { return SectionAddress; }
  MCLOHContainer &getLOHContainer() { return LOHContainer; }

  uint64_t getSectionAddress(const MCSection *Sec) const {
    return SectionAddress.lookup(Sec);
  }
  uint64_t getSymbolAddress(const MCSymbol &S, const MCAssembler &Asm) const;

  uint64_t getFragmentAddress(const MCAssembler &Asm,
                              const MCFragment *Fragment) const;

  uint64_t getPaddingSize(const MCAssembler &Asm, const MCSection *SD) const;

  const MCSymbol *getAtom(const MCSymbol &S) const;

  bool doesSymbolRequireExternRelocation(const MCSymbol &S);

  /// Mach-O deployment target version information.
  void setVersionMin(MCVersionMinType Type, unsigned Major, unsigned Minor,
                     unsigned Update,
                     VersionTuple SDKVersion = VersionTuple()) {
    VersionInfo.EmitBuildVersion = false;
    VersionInfo.TypeOrPlatform.Type = Type;
    VersionInfo.Major = Major;
    VersionInfo.Minor = Minor;
    VersionInfo.Update = Update;
    VersionInfo.SDKVersion = SDKVersion;
  }
  void setBuildVersion(MachO::PlatformType Platform, unsigned Major,
                       unsigned Minor, unsigned Update,
                       VersionTuple SDKVersion = VersionTuple()) {
    VersionInfo.EmitBuildVersion = true;
    VersionInfo.TypeOrPlatform.Platform = Platform;
    VersionInfo.Major = Major;
    VersionInfo.Minor = Minor;
    VersionInfo.Update = Update;
    VersionInfo.SDKVersion = SDKVersion;
  }
  void setTargetVariantBuildVersion(MachO::PlatformType Platform,
                                    unsigned Major, unsigned Minor,
                                    unsigned Update, VersionTuple SDKVersion) {
    TargetVariantVersionInfo.EmitBuildVersion = true;
    TargetVariantVersionInfo.TypeOrPlatform.Platform = Platform;
    TargetVariantVersionInfo.Major = Major;
    TargetVariantVersionInfo.Minor = Minor;
    TargetVariantVersionInfo.Update = Update;
    TargetVariantVersionInfo.SDKVersion = SDKVersion;
  }

  std::vector<std::vector<std::string>> &getLinkerOptions() {
    return LinkerOptions;
  }

  /// @}

  /// \name Target Writer Proxy Accessors
  /// @{

  bool is64Bit() const { return TargetObjectWriter->is64Bit(); }
  bool isX86_64() const {
    uint32_t CPUType = TargetObjectWriter->getCPUType();
    return CPUType == MachO::CPU_TYPE_X86_64;
  }

  /// @}

  void writeHeader(MachO::HeaderFileType Type, unsigned NumLoadCommands,
                   unsigned LoadCommandsSize, bool SubsectionsViaSymbols);

  /// Write a segment load command.
  ///
  /// \param NumSections The number of sections in this segment.
  /// \param SectionDataSize The total size of the sections.
  void writeSegmentLoadCommand(StringRef Name, unsigned NumSections,
                               uint64_t VMAddr, uint64_t VMSize,
                               uint64_t SectionDataStartOffset,
                               uint64_t SectionDataSize, uint32_t MaxProt,
                               uint32_t InitProt);

  void writeSection(const MCAssembler &Asm, const MCSection &Sec,
                    uint64_t VMAddr, uint64_t FileOffset, unsigned Flags,
                    uint64_t RelocationsStart, unsigned NumRelocations);

  void writeSymtabLoadCommand(uint32_t SymbolOffset, uint32_t NumSymbols,
                              uint32_t StringTableOffset,
                              uint32_t StringTableSize);

  void writeDysymtabLoadCommand(
      uint32_t FirstLocalSymbol, uint32_t NumLocalSymbols,
      uint32_t FirstExternalSymbol, uint32_t NumExternalSymbols,
      uint32_t FirstUndefinedSymbol, uint32_t NumUndefinedSymbols,
      uint32_t IndirectSymbolOffset, uint32_t NumIndirectSymbols);

  void writeNlist(MachSymbolData &MSD, const MCAssembler &Asm);

  void writeLinkeditLoadCommand(uint32_t Type, uint32_t DataOffset,
                                uint32_t DataSize);

  void writeLinkerOptionsLoadCommand(const std::vector<std::string> &Options);

  // FIXME: We really need to improve the relocation validation. Basically, we
  // want to implement a separate computation which evaluates the relocation
  // entry as the linker would, and verifies that the resultant fixup value is
  // exactly what the encoder wanted. This will catch several classes of
  // problems:
  //
  //  - Relocation entry bugs, the two algorithms are unlikely to have the same
  //    exact bug.
  //
  //  - Relaxation issues, where we forget to relax something.
  //
  //  - Input errors, where something cannot be correctly encoded. 'as' allows
  //    these through in many cases.

  // Add a relocation to be output in the object file. At the time this is
  // called, the symbol indexes are not know, so if the relocation refers
  // to a symbol it should be passed as \p RelSymbol so that it can be updated
  // afterwards. If the relocation doesn't refer to a symbol, nullptr should be
  // used.
  void addRelocation(const MCSymbol *RelSymbol, const MCSection *Sec,
                     MachO::any_relocation_info &MRE) {
    RelAndSymbol P(RelSymbol, MRE);
    Relocations[Sec].push_back(P);
  }

  void recordRelocation(MCAssembler &Asm, const MCFragment *Fragment,
                        const MCFixup &Fixup, MCValue Target,
                        uint64_t &FixedValue) override;

  void bindIndirectSymbols(MCAssembler &Asm);

  /// Compute the symbol table data.
  void computeSymbolTable(MCAssembler &Asm,
                          std::vector<MachSymbolData> &LocalSymbolData,
                          std::vector<MachSymbolData> &ExternalSymbolData,
                          std::vector<MachSymbolData> &UndefinedSymbolData);

  void computeSectionAddresses(const MCAssembler &Asm);

  void executePostLayoutBinding(MCAssembler &Asm) override;

  bool isSymbolRefDifferenceFullyResolvedImpl(const MCAssembler &Asm,
                                              const MCSymbol &SymA,
                                              const MCFragment &FB, bool InSet,
                                              bool IsPCRel) const override;

  void populateAddrSigSection(MCAssembler &Asm);

  uint64_t writeObject(MCAssembler &Asm) override;
};

/// Construct a new Mach-O writer instance.
///
/// This routine takes ownership of the target writer subclass.
///
/// \param MOTW - The target specific Mach-O writer subclass.
/// \param OS - The stream to write to.
/// \returns The constructed object writer.
std::unique_ptr<MCObjectWriter>
createMachObjectWriter(std::unique_ptr<MCMachObjectTargetWriter> MOTW,
                       raw_pwrite_stream &OS, bool IsLittleEndian);

} // end namespace llvm

#endif // LLVM_MC_MCMACHOBJECTWRITER_H
