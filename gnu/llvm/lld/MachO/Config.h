//===- Config.h -------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MACHO_CONFIG_H
#define LLD_MACHO_CONFIG_H

#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Support/CachePruning.h"
#include "llvm/Support/GlobPattern.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/TextAPI/Architecture.h"
#include "llvm/TextAPI/Platform.h"
#include "llvm/TextAPI/Target.h"

#include <vector>

namespace llvm {
enum class CodeGenOptLevel;
} // namespace llvm

namespace lld {
namespace macho {

class InputSection;
class Symbol;

using NamePair = std::pair<llvm::StringRef, llvm::StringRef>;
using SectionRenameMap = llvm::DenseMap<NamePair, NamePair>;
using SegmentRenameMap = llvm::DenseMap<llvm::StringRef, llvm::StringRef>;

struct PlatformInfo {
  llvm::MachO::Target target;
  llvm::VersionTuple sdk;
};

inline uint32_t encodeVersion(const llvm::VersionTuple &version) {
  return ((version.getMajor() << 020) |
          (version.getMinor().value_or(0) << 010) |
          version.getSubminor().value_or(0));
}

enum class NamespaceKind {
  twolevel,
  flat,
};

enum class UndefinedSymbolTreatment {
  unknown,
  error,
  warning,
  suppress,
  dynamic_lookup,
};

enum class ICFLevel {
  unknown,
  none,
  safe,
  all,
};

enum class ObjCStubsMode {
  fast,
  small,
};

struct SectionAlign {
  llvm::StringRef segName;
  llvm::StringRef sectName;
  uint32_t align;
};

struct SegmentProtection {
  llvm::StringRef name;
  uint32_t maxProt;
  uint32_t initProt;
};

class SymbolPatterns {
public:
  // GlobPattern can also match literals,
  // but we prefer the O(1) lookup of DenseSet.
  llvm::SetVector<llvm::CachedHashStringRef> literals;
  std::vector<llvm::GlobPattern> globs;

  bool empty() const { return literals.empty() && globs.empty(); }
  void clear();
  void insert(llvm::StringRef symbolName);
  bool matchLiteral(llvm::StringRef symbolName) const;
  bool matchGlob(llvm::StringRef symbolName) const;
  bool match(llvm::StringRef symbolName) const;
};

enum class SymtabPresence {
  All,
  None,
  SelectivelyIncluded,
  SelectivelyExcluded,
};

struct Configuration {
  Symbol *entry = nullptr;
  bool hasReexports = false;
  bool allLoad = false;
  bool applicationExtension = false;
  bool archMultiple = false;
  bool exportDynamic = false;
  bool forceLoadObjC = false;
  bool forceLoadSwift = false; // Only applies to LC_LINKER_OPTIONs.
  bool staticLink = false;
  bool implicitDylibs = false;
  bool isPic = false;
  bool headerPadMaxInstallNames = false;
  bool markDeadStrippableDylib = false;
  bool printDylibSearch = false;
  bool printEachFile = false;
  bool printWhyLoad = false;
  bool searchDylibsFirst = false;
  bool saveTemps = false;
  bool adhocCodesign = false;
  bool emitFunctionStarts = false;
  bool emitDataInCodeInfo = false;
  bool emitEncryptionInfo = false;
  bool emitInitOffsets = false;
  bool emitChainedFixups = false;
  bool emitRelativeMethodLists = false;
  bool thinLTOEmitImportsFiles;
  bool thinLTOEmitIndexFiles;
  bool thinLTOIndexOnly;
  bool timeTraceEnabled = false;
  bool dataConst = false;
  bool dedupStrings = true;
  bool deadStripDuplicates = false;
  bool omitDebugInfo = false;
  bool warnDylibInstallName = false;
  bool ignoreOptimizationHints = false;
  bool forceExactCpuSubtypeMatch = false;
  uint32_t headerPad;
  uint32_t dylibCompatibilityVersion = 0;
  uint32_t dylibCurrentVersion = 0;
  uint32_t timeTraceGranularity = 500;
  unsigned optimize;
  std::string progName;

  // For `clang -arch arm64 -arch x86_64`, clang will:
  // 1. invoke the linker twice, to write one temporary output per arch
  // 2. invoke `lipo` to merge the two outputs into a single file
  // `outputFile` is the name of the temporary file the linker writes to.
  // `finalOutput `is the name of the file lipo writes to after the link.
  llvm::StringRef outputFile;
  llvm::StringRef finalOutput;

  llvm::StringRef installName;
  llvm::StringRef mapFile;
  llvm::StringRef ltoObjPath;
  llvm::StringRef thinLTOJobs;
  llvm::StringRef umbrella;
  uint32_t ltoo = 2;
  llvm::CodeGenOptLevel ltoCgo;
  llvm::CachePruningPolicy thinLTOCachePolicy;
  llvm::StringRef thinLTOCacheDir;
  llvm::StringRef thinLTOIndexOnlyArg;
  std::pair<llvm::StringRef, llvm::StringRef> thinLTOObjectSuffixReplace;
  llvm::StringRef thinLTOPrefixReplaceOld;
  llvm::StringRef thinLTOPrefixReplaceNew;
  llvm::StringRef thinLTOPrefixReplaceNativeObject;
  bool deadStripDylibs = false;
  bool demangle = false;
  bool deadStrip = false;
  bool errorForArchMismatch = false;
  bool ignoreAutoLink = false;
  // ld64 allows invalid auto link options as long as the link succeeds. LLD
  // does not, but there are cases in the wild where the invalid linker options
  // exist. This allows users to ignore the specific invalid options in the case
  // they can't easily fix them.
  llvm::StringSet<> ignoreAutoLinkOptions;
  bool strictAutoLink = false;
  PlatformInfo platformInfo;
  std::optional<PlatformInfo> secondaryPlatformInfo;
  NamespaceKind namespaceKind = NamespaceKind::twolevel;
  UndefinedSymbolTreatment undefinedSymbolTreatment =
      UndefinedSymbolTreatment::error;
  ICFLevel icfLevel = ICFLevel::none;
  bool keepICFStabs = false;
  ObjCStubsMode objcStubsMode = ObjCStubsMode::fast;
  llvm::MachO::HeaderFileType outputType;
  std::vector<llvm::StringRef> systemLibraryRoots;
  std::vector<llvm::StringRef> librarySearchPaths;
  std::vector<llvm::StringRef> frameworkSearchPaths;
  bool warnDuplicateRpath = true;
  llvm::SmallVector<llvm::StringRef, 0> runtimePaths;
  std::vector<std::string> astPaths;
  std::vector<Symbol *> explicitUndefineds;
  llvm::StringSet<> explicitDynamicLookups;
  // There are typically few custom sectionAlignments or segmentProtections,
  // so use a vector instead of a map.
  std::vector<SectionAlign> sectionAlignments;
  std::vector<SegmentProtection> segmentProtections;
  bool ltoDebugPassManager = false;
  bool csProfileGenerate = false;
  llvm::StringRef csProfilePath;
  bool pgoWarnMismatch;
  bool warnThinArchiveMissingMembers;

  bool callGraphProfileSort = false;
  llvm::StringRef printSymbolOrder;

  SectionRenameMap sectionRenameMap;
  SegmentRenameMap segmentRenameMap;

  bool hasExplicitExports = false;
  SymbolPatterns exportedSymbols;
  SymbolPatterns unexportedSymbols;
  SymbolPatterns whyLive;

  std::vector<std::pair<llvm::StringRef, llvm::StringRef>> aliasedSymbols;

  SymtabPresence localSymbolsPresence = SymtabPresence::All;
  SymbolPatterns localSymbolPatterns;
  llvm::SmallVector<llvm::StringRef, 0> mllvmOpts;

  bool zeroModTime = true;
  bool generateUuid = true;

  llvm::StringRef osoPrefix;

  std::vector<llvm::StringRef> dyldEnvs;

  llvm::MachO::Architecture arch() const { return platformInfo.target.Arch; }

  llvm::MachO::PlatformType platform() const {
    return platformInfo.target.Platform;
  }
};

extern std::unique_ptr<Configuration> config;

} // namespace macho
} // namespace lld

#endif
