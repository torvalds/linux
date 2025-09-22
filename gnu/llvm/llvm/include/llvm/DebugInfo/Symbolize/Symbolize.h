//===- Symbolize.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Header for LLVM symbolization library.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_SYMBOLIZE_SYMBOLIZE_H
#define LLVM_DEBUGINFO_SYMBOLIZE_SYMBOLIZE_H

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/ADT/simple_ilist.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/BuildID.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace llvm {
namespace object {
class ELFObjectFileBase;
class MachOObjectFile;
class ObjectFile;
struct SectionedAddress;
} // namespace object

namespace symbolize {

class SymbolizableModule;

using namespace object;

using FunctionNameKind = DILineInfoSpecifier::FunctionNameKind;
using FileLineInfoKind = DILineInfoSpecifier::FileLineInfoKind;

class CachedBinary;

class LLVMSymbolizer {
public:
  struct Options {
    FunctionNameKind PrintFunctions = FunctionNameKind::LinkageName;
    FileLineInfoKind PathStyle = FileLineInfoKind::AbsoluteFilePath;
    bool UseSymbolTable = true;
    bool Demangle = true;
    bool RelativeAddresses = false;
    bool UntagAddresses = false;
    bool UseDIA = false;
    std::string DefaultArch;
    std::vector<std::string> DsymHints;
    std::string FallbackDebugPath;
    std::string DWPName;
    std::vector<std::string> DebugFileDirectory;
    size_t MaxCacheSize =
        sizeof(size_t) == 4
            ? 512 * 1024 * 1024 /* 512 MiB */
            : static_cast<size_t>(4ULL * 1024 * 1024 * 1024) /* 4 GiB */;
  };

  LLVMSymbolizer();
  LLVMSymbolizer(const Options &Opts);

  ~LLVMSymbolizer();

  // Overloads accepting ObjectFile does not support COFF currently
  Expected<DILineInfo> symbolizeCode(const ObjectFile &Obj,
                                     object::SectionedAddress ModuleOffset);
  Expected<DILineInfo> symbolizeCode(const std::string &ModuleName,
                                     object::SectionedAddress ModuleOffset);
  Expected<DILineInfo> symbolizeCode(ArrayRef<uint8_t> BuildID,
                                     object::SectionedAddress ModuleOffset);
  Expected<DIInliningInfo>
  symbolizeInlinedCode(const ObjectFile &Obj,
                       object::SectionedAddress ModuleOffset);
  Expected<DIInliningInfo>
  symbolizeInlinedCode(const std::string &ModuleName,
                       object::SectionedAddress ModuleOffset);
  Expected<DIInliningInfo>
  symbolizeInlinedCode(ArrayRef<uint8_t> BuildID,
                       object::SectionedAddress ModuleOffset);

  Expected<DIGlobal> symbolizeData(const ObjectFile &Obj,
                                   object::SectionedAddress ModuleOffset);
  Expected<DIGlobal> symbolizeData(const std::string &ModuleName,
                                   object::SectionedAddress ModuleOffset);
  Expected<DIGlobal> symbolizeData(ArrayRef<uint8_t> BuildID,
                                   object::SectionedAddress ModuleOffset);
  Expected<std::vector<DILocal>>
  symbolizeFrame(const ObjectFile &Obj, object::SectionedAddress ModuleOffset);
  Expected<std::vector<DILocal>>
  symbolizeFrame(const std::string &ModuleName,
                 object::SectionedAddress ModuleOffset);
  Expected<std::vector<DILocal>>
  symbolizeFrame(ArrayRef<uint8_t> BuildID,
                 object::SectionedAddress ModuleOffset);

  Expected<std::vector<DILineInfo>>
  findSymbol(const ObjectFile &Obj, StringRef Symbol, uint64_t Offset);
  Expected<std::vector<DILineInfo>>
  findSymbol(const std::string &ModuleName, StringRef Symbol, uint64_t Offset);
  Expected<std::vector<DILineInfo>>
  findSymbol(ArrayRef<uint8_t> BuildID, StringRef Symbol, uint64_t Offset);

  void flush();

  // Evict entries from the binary cache until it is under the maximum size
  // given in the options. Calling this invalidates references in the DI...
  // objects returned by the methods above.
  void pruneCache();

  static std::string
  DemangleName(const std::string &Name,
               const SymbolizableModule *DbiModuleDescriptor);

  void setBuildIDFetcher(std::unique_ptr<BuildIDFetcher> Fetcher) {
    BIDFetcher = std::move(Fetcher);
  }

  /// Returns a SymbolizableModule or an error if loading debug info failed.
  /// Only one attempt is made to load a module, and errors during loading are
  /// only reported once. Subsequent calls to get module info for a module that
  /// failed to load will return nullptr.
  Expected<SymbolizableModule *>
  getOrCreateModuleInfo(const std::string &ModuleName);

private:
  // Bundles together object file with code/data and object file with
  // corresponding debug info. These objects can be the same.
  using ObjectPair = std::pair<const ObjectFile *, const ObjectFile *>;

  template <typename T>
  Expected<DILineInfo>
  symbolizeCodeCommon(const T &ModuleSpecifier,
                      object::SectionedAddress ModuleOffset);
  template <typename T>
  Expected<DIInliningInfo>
  symbolizeInlinedCodeCommon(const T &ModuleSpecifier,
                             object::SectionedAddress ModuleOffset);
  template <typename T>
  Expected<DIGlobal> symbolizeDataCommon(const T &ModuleSpecifier,
                                         object::SectionedAddress ModuleOffset);
  template <typename T>
  Expected<std::vector<DILocal>>
  symbolizeFrameCommon(const T &ModuleSpecifier,
                       object::SectionedAddress ModuleOffset);
  template <typename T>
  Expected<std::vector<DILineInfo>>
  findSymbolCommon(const T &ModuleSpecifier, StringRef Symbol, uint64_t Offset);

  Expected<SymbolizableModule *> getOrCreateModuleInfo(const ObjectFile &Obj);

  /// Returns a SymbolizableModule or an error if loading debug info failed.
  /// Unlike the above, errors are reported each time, since they are more
  /// likely to be transient.
  Expected<SymbolizableModule *>
  getOrCreateModuleInfo(ArrayRef<uint8_t> BuildID);

  Expected<SymbolizableModule *>
  createModuleInfo(const ObjectFile *Obj, std::unique_ptr<DIContext> Context,
                   StringRef ModuleName);

  ObjectFile *lookUpDsymFile(const std::string &Path,
                             const MachOObjectFile *ExeObj,
                             const std::string &ArchName);
  ObjectFile *lookUpDebuglinkObject(const std::string &Path,
                                    const ObjectFile *Obj,
                                    const std::string &ArchName);
  ObjectFile *lookUpBuildIDObject(const std::string &Path,
                                  const ELFObjectFileBase *Obj,
                                  const std::string &ArchName);

  bool findDebugBinary(const std::string &OrigPath,
                       const std::string &DebuglinkName, uint32_t CRCHash,
                       std::string &Result);

  bool getOrFindDebugBinary(const ArrayRef<uint8_t> BuildID,
                            std::string &Result);

  /// Returns pair of pointers to object and debug object.
  Expected<ObjectPair> getOrCreateObjectPair(const std::string &Path,
                                             const std::string &ArchName);

  /// Return a pointer to object file at specified path, for a specified
  /// architecture (e.g. if path refers to a Mach-O universal binary, only one
  /// object file from it will be returned).
  Expected<ObjectFile *> getOrCreateObject(const std::string &Path,
                                           const std::string &ArchName);

  /// Update the LRU cache order when a binary is accessed.
  void recordAccess(CachedBinary &Bin);

  std::map<std::string, std::unique_ptr<SymbolizableModule>, std::less<>>
      Modules;
  StringMap<std::string> BuildIDPaths;

  /// Contains cached results of getOrCreateObjectPair().
  std::map<std::pair<std::string, std::string>, ObjectPair>
      ObjectPairForPathArch;

  /// Contains parsed binary for each path, or parsing error.
  std::map<std::string, CachedBinary> BinaryForPath;

  /// A list of cached binaries in LRU order.
  simple_ilist<CachedBinary> LRUBinaries;
  /// Sum of the sizes of the cached binaries.
  size_t CacheSize = 0;

  /// Parsed object file for path/architecture pair, where "path" refers
  /// to Mach-O universal binary.
  std::map<std::pair<std::string, std::string>, std::unique_ptr<ObjectFile>>
      ObjectForUBPathAndArch;

  Options Opts;

  std::unique_ptr<BuildIDFetcher> BIDFetcher;
};

// A binary intrusively linked into a LRU cache list. If the binary is empty,
// then the entry marks that an error occurred, and it is not part of the LRU
// list.
class CachedBinary : public ilist_node<CachedBinary> {
public:
  CachedBinary() = default;
  CachedBinary(OwningBinary<Binary> Bin) : Bin(std::move(Bin)) {}

  OwningBinary<Binary> &operator*() { return Bin; }
  OwningBinary<Binary> *operator->() { return &Bin; }

  // Add an action to be performed when the binary is evicted, before all
  // previously registered evictors.
  void pushEvictor(std::function<void()> Evictor);

  // Run all registered evictors in the reverse of the order in which they were
  // added.
  void evict() {
    if (Evictor)
      Evictor();
  }

  size_t size() { return Bin.getBinary()->getData().size(); }

private:
  OwningBinary<Binary> Bin;
  std::function<void()> Evictor;
};

} // end namespace symbolize
} // end namespace llvm

#endif // LLVM_DEBUGINFO_SYMBOLIZE_SYMBOLIZE_H
