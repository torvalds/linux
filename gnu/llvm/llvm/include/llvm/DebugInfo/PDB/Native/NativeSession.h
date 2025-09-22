//===- NativeSession.h - Native implementation of IPDBSession ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVESESSION_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVESESSION_H

#include "llvm/ADT/IntervalMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/PDB/IPDBSession.h"
#include "llvm/DebugInfo/PDB/Native/SymbolCache.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Error.h"

namespace llvm {
class MemoryBuffer;
namespace pdb {
class PDBFile;
class NativeExeSymbol;
class IPDBSourceFile;
class ModuleDebugStreamRef;
class PDBSymbol;
class PDBSymbolCompiland;
class PDBSymbolExe;
template <typename ChildType> class IPDBEnumChildren;

class NativeSession : public IPDBSession {
  struct PdbSearchOptions {
    StringRef ExePath;
    // FIXME: Add other PDB search options (_NT_SYMBOL_PATH, symsrv)
  };

public:
  NativeSession(std::unique_ptr<PDBFile> PdbFile,
                std::unique_ptr<BumpPtrAllocator> Allocator);
  ~NativeSession() override;

  static Error createFromPdb(std::unique_ptr<MemoryBuffer> MB,
                             std::unique_ptr<IPDBSession> &Session);
  static Error createFromPdbPath(StringRef PdbPath,
                                 std::unique_ptr<IPDBSession> &Session);
  static Error createFromExe(StringRef Path,
                             std::unique_ptr<IPDBSession> &Session);
  static Expected<std::string> searchForPdb(const PdbSearchOptions &Opts);

  uint64_t getLoadAddress() const override;
  bool setLoadAddress(uint64_t Address) override;
  std::unique_ptr<PDBSymbolExe> getGlobalScope() override;
  std::unique_ptr<PDBSymbol> getSymbolById(SymIndexId SymbolId) const override;

  bool addressForVA(uint64_t VA, uint32_t &Section,
                    uint32_t &Offset) const override;
  bool addressForRVA(uint32_t RVA, uint32_t &Section,
                     uint32_t &Offset) const override;

  std::unique_ptr<PDBSymbol> findSymbolByAddress(uint64_t Address,
                                                 PDB_SymType Type) override;
  std::unique_ptr<PDBSymbol> findSymbolByRVA(uint32_t RVA,
                                             PDB_SymType Type) override;
  std::unique_ptr<PDBSymbol> findSymbolBySectOffset(uint32_t Sect,
                                                    uint32_t Offset,
                                                    PDB_SymType Type) override;

  std::unique_ptr<IPDBEnumLineNumbers>
  findLineNumbers(const PDBSymbolCompiland &Compiland,
                  const IPDBSourceFile &File) const override;
  std::unique_ptr<IPDBEnumLineNumbers>
  findLineNumbersByAddress(uint64_t Address, uint32_t Length) const override;
  std::unique_ptr<IPDBEnumLineNumbers>
  findLineNumbersByRVA(uint32_t RVA, uint32_t Length) const override;
  std::unique_ptr<IPDBEnumLineNumbers>
  findLineNumbersBySectOffset(uint32_t Section, uint32_t Offset,
                              uint32_t Length) const override;

  std::unique_ptr<IPDBEnumSourceFiles>
  findSourceFiles(const PDBSymbolCompiland *Compiland, llvm::StringRef Pattern,
                  PDB_NameSearchFlags Flags) const override;
  std::unique_ptr<IPDBSourceFile>
  findOneSourceFile(const PDBSymbolCompiland *Compiland,
                    llvm::StringRef Pattern,
                    PDB_NameSearchFlags Flags) const override;
  std::unique_ptr<IPDBEnumChildren<PDBSymbolCompiland>>
  findCompilandsForSourceFile(llvm::StringRef Pattern,
                              PDB_NameSearchFlags Flags) const override;
  std::unique_ptr<PDBSymbolCompiland>
  findOneCompilandForSourceFile(llvm::StringRef Pattern,
                                PDB_NameSearchFlags Flags) const override;
  std::unique_ptr<IPDBEnumSourceFiles> getAllSourceFiles() const override;
  std::unique_ptr<IPDBEnumSourceFiles> getSourceFilesForCompiland(
      const PDBSymbolCompiland &Compiland) const override;
  std::unique_ptr<IPDBSourceFile>
  getSourceFileById(uint32_t FileId) const override;

  std::unique_ptr<IPDBEnumDataStreams> getDebugStreams() const override;

  std::unique_ptr<IPDBEnumTables> getEnumTables() const override;

  std::unique_ptr<IPDBEnumInjectedSources> getInjectedSources() const override;

  std::unique_ptr<IPDBEnumSectionContribs> getSectionContribs() const override;

  std::unique_ptr<IPDBEnumFrameData> getFrameData() const override;

  PDBFile &getPDBFile() { return *Pdb; }
  const PDBFile &getPDBFile() const { return *Pdb; }

  NativeExeSymbol &getNativeGlobalScope() const;
  SymbolCache &getSymbolCache() { return Cache; }
  const SymbolCache &getSymbolCache() const { return Cache; }
  uint32_t getRVAFromSectOffset(uint32_t Section, uint32_t Offset) const;
  uint64_t getVAFromSectOffset(uint32_t Section, uint32_t Offset) const;
  bool moduleIndexForVA(uint64_t VA, uint16_t &ModuleIndex) const;
  bool moduleIndexForSectOffset(uint32_t Sect, uint32_t Offset,
                                uint16_t &ModuleIndex) const;
  Expected<ModuleDebugStreamRef> getModuleDebugStream(uint32_t Index) const;

private:
  void initializeExeSymbol();
  void parseSectionContribs();

  std::unique_ptr<PDBFile> Pdb;
  std::unique_ptr<BumpPtrAllocator> Allocator;

  SymbolCache Cache;
  SymIndexId ExeSymbol = 0;
  uint64_t LoadAddress = 0;

  /// Map from virtual address to module index.
  using IMap =
      IntervalMap<uint64_t, uint16_t, 8, IntervalMapHalfOpenInfo<uint64_t>>;
  IMap::Allocator IMapAllocator;
  IMap AddrToModuleIndex;
};
} // namespace pdb
} // namespace llvm

#endif
