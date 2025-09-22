//===- Config.h -------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_COFF_CONFIG_H
#define LLD_COFF_CONFIG_H

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/CachePruning.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <cstdint>
#include <map>
#include <set>
#include <string>

namespace lld::coff {

using llvm::COFF::IMAGE_FILE_MACHINE_UNKNOWN;
using llvm::COFF::WindowsSubsystem;
using llvm::StringRef;
class DefinedAbsolute;
class StringChunk;
class Symbol;
class InputFile;
class SectionChunk;

// Short aliases.
static const auto AMD64 = llvm::COFF::IMAGE_FILE_MACHINE_AMD64;
static const auto ARM64 = llvm::COFF::IMAGE_FILE_MACHINE_ARM64;
static const auto ARM64EC = llvm::COFF::IMAGE_FILE_MACHINE_ARM64EC;
static const auto ARM64X = llvm::COFF::IMAGE_FILE_MACHINE_ARM64X;
static const auto ARMNT = llvm::COFF::IMAGE_FILE_MACHINE_ARMNT;
static const auto I386 = llvm::COFF::IMAGE_FILE_MACHINE_I386;

enum class ExportSource {
  Unset,
  Directives,
  Export,
  ModuleDefinition,
};

enum class EmitKind { Obj, LLVM, ASM };

// Represents an /export option.
struct Export {
  StringRef name;       // N in /export:N or /export:E=N
  StringRef extName;    // E in /export:E=N
  StringRef exportAs;   // E in /export:N,EXPORTAS,E
  StringRef importName; // GNU specific: N in "othername == N"
  Symbol *sym = nullptr;
  uint16_t ordinal = 0;
  bool noname = false;
  bool data = false;
  bool isPrivate = false;
  bool constant = false;

  // If an export is a form of /export:foo=dllname.bar, that means
  // that foo should be exported as an alias to bar in the DLL.
  // forwardTo is set to "dllname.bar" part. Usually empty.
  StringRef forwardTo;
  StringChunk *forwardChunk = nullptr;

  ExportSource source = ExportSource::Unset;
  StringRef symbolName;
  StringRef exportName; // Name in DLL

  bool operator==(const Export &e) const {
    return (name == e.name && extName == e.extName && exportAs == e.exportAs &&
            importName == e.importName && ordinal == e.ordinal &&
            noname == e.noname && data == e.data && isPrivate == e.isPrivate);
  }
};

enum class DebugType {
  None  = 0x0,
  CV    = 0x1,  /// CodeView
  PData = 0x2,  /// Procedure Data
  Fixup = 0x4,  /// Relocation Table
};

enum GuardCFLevel {
  Off     = 0x0,
  CF      = 0x1, /// Emit gfids tables
  LongJmp = 0x2, /// Emit longjmp tables
  EHCont  = 0x4, /// Emit ehcont tables
  All     = 0x7  /// Enable all protections
};

enum class ICFLevel {
  None,
  Safe, // Safe ICF for all sections.
  All,  // Aggressive ICF for code, but safe ICF for data, similar to MSVC's
        // behavior.
};

enum class BuildIDHash {
  None,
  PDB,
  Binary,
};

// Global configuration.
struct Configuration {
  enum ManifestKind { Default, SideBySide, Embed, No };
  bool is64() const { return llvm::COFF::is64Bit(machine); }

  llvm::COFF::MachineTypes machine = IMAGE_FILE_MACHINE_UNKNOWN;
  size_t wordsize;
  bool verbose = false;
  WindowsSubsystem subsystem = llvm::COFF::IMAGE_SUBSYSTEM_UNKNOWN;
  Symbol *entry = nullptr;
  bool noEntry = false;
  std::string outputFile;
  std::string importName;
  bool demangle = true;
  bool doGC = true;
  ICFLevel doICF = ICFLevel::None;
  bool tailMerge;
  bool relocatable = true;
  bool forceMultiple = false;
  bool forceMultipleRes = false;
  bool forceUnresolved = false;
  bool debug = false;
  bool includeDwarfChunks = false;
  bool debugGHashes = false;
  bool writeSymtab = false;
  bool driver = false;
  bool driverUponly = false;
  bool driverWdm = false;
  bool showTiming = false;
  bool showSummary = false;
  bool printSearchPaths = false;
  unsigned debugTypes = static_cast<unsigned>(DebugType::None);
  llvm::SmallVector<llvm::StringRef, 0> mllvmOpts;
  std::vector<std::string> natvisFiles;
  llvm::StringMap<std::string> namedStreams;
  llvm::SmallString<128> pdbAltPath;
  int pdbPageSize = 4096;
  llvm::SmallString<128> pdbPath;
  llvm::SmallString<128> pdbSourcePath;
  std::vector<llvm::StringRef> argv;

  // Symbols in this set are considered as live by the garbage collector.
  std::vector<Symbol *> gcroot;

  std::set<std::string> noDefaultLibs;
  bool noDefaultLibAll = false;

  // True if we are creating a DLL.
  bool dll = false;
  StringRef implib;
  bool noimplib = false;
  std::vector<Export> exports;
  bool hadExplicitExports;
  std::set<std::string> delayLoads;
  std::map<std::string, int> dllOrder;
  Symbol *delayLoadHelper = nullptr;

  bool saveTemps = false;

  // /guard:cf
  int guardCF = GuardCFLevel::Off;

  // Used for SafeSEH.
  bool safeSEH = false;
  Symbol *sehTable = nullptr;
  Symbol *sehCount = nullptr;
  bool noSEH = false;

  // Used for /opt:lldlto=N
  unsigned ltoo = 2;
  // Used for /opt:lldltocgo=N
  std::optional<unsigned> ltoCgo;

  // Used for /opt:lldltojobs=N
  std::string thinLTOJobs;
  // Used for /opt:lldltopartitions=N
  unsigned ltoPartitions = 1;

  // Used for /lldltocache=path
  StringRef ltoCache;
  // Used for /lldltocachepolicy=policy
  llvm::CachePruningPolicy ltoCachePolicy;

  // Used for /opt:[no]ltodebugpassmanager
  bool ltoDebugPassManager = false;

  // Used for /merge:from=to (e.g. /merge:.rdata=.text)
  std::map<StringRef, StringRef> merge;

  // Used for /section=.name,{DEKPRSW} to set section attributes.
  std::map<StringRef, uint32_t> section;

  // Options for manifest files.
  ManifestKind manifest = Default;
  int manifestID = 1;
  llvm::SetVector<StringRef> manifestDependencies;
  bool manifestUAC = true;
  std::vector<std::string> manifestInput;
  StringRef manifestLevel = "'asInvoker'";
  StringRef manifestUIAccess = "'false'";
  StringRef manifestFile;

  // used for /dwodir
  StringRef dwoDir;

  // Used for /aligncomm.
  std::map<std::string, int> alignComm;

  // Used for /failifmismatch.
  std::map<StringRef, std::pair<StringRef, InputFile *>> mustMatch;

  // Used for /alternatename.
  std::map<StringRef, StringRef> alternateNames;

  // Used for /order.
  llvm::StringMap<int> order;

  // Used for /lldmap.
  std::string lldmapFile;

  // Used for /map.
  std::string mapFile;

  // Used for /mapinfo.
  bool mapInfo = false;

  // Used for /thinlto-index-only:
  llvm::StringRef thinLTOIndexOnlyArg;

  // Used for /thinlto-prefix-replace:
  // Replace the prefix in paths generated for ThinLTO, replacing
  // thinLTOPrefixReplaceOld with thinLTOPrefixReplaceNew. If
  // thinLTOPrefixReplaceNativeObject is defined, replace the prefix of object
  // file paths written to the response file given in the
  // --thinlto-index-only=${response} option with
  // thinLTOPrefixReplaceNativeObject, instead of thinLTOPrefixReplaceNew.
  llvm::StringRef thinLTOPrefixReplaceOld;
  llvm::StringRef thinLTOPrefixReplaceNew;
  llvm::StringRef thinLTOPrefixReplaceNativeObject;

  // Used for /thinlto-object-suffix-replace:
  std::pair<llvm::StringRef, llvm::StringRef> thinLTOObjectSuffixReplace;

  // Used for /lto-obj-path:
  llvm::StringRef ltoObjPath;

  // Used for /lto-cs-profile-generate:
  bool ltoCSProfileGenerate = false;

  // Used for /lto-cs-profile-path
  llvm::StringRef ltoCSProfileFile;

  // Used for /lto-pgo-warn-mismatch:
  bool ltoPGOWarnMismatch = true;

  // Used for /lto-sample-profile:
  llvm::StringRef ltoSampleProfileName;

  // Used for /call-graph-ordering-file:
  llvm::MapVector<std::pair<const SectionChunk *, const SectionChunk *>,
                  uint64_t>
      callGraphProfile;
  bool callGraphProfileSort = false;

  // Used for /print-symbol-order:
  StringRef printSymbolOrder;

  // Used for /vfsoverlay:
  std::unique_ptr<llvm::vfs::FileSystem> vfs;

  uint64_t align = 4096;
  uint64_t imageBase = -1;
  uint64_t fileAlign = 512;
  uint64_t stackReserve = 1024 * 1024;
  uint64_t stackCommit = 4096;
  uint64_t heapReserve = 1024 * 1024;
  uint64_t heapCommit = 4096;
  uint32_t majorImageVersion = 0;
  uint32_t minorImageVersion = 0;
  // If changing the default os/subsys version here, update the default in
  // the MinGW driver accordingly.
  uint32_t majorOSVersion = 6;
  uint32_t minorOSVersion = 0;
  uint32_t majorSubsystemVersion = 6;
  uint32_t minorSubsystemVersion = 0;
  uint32_t timestamp = 0;
  uint32_t functionPadMin = 0;
  uint32_t timeTraceGranularity = 0;
  uint16_t dependentLoadFlags = 0;
  bool dynamicBase = true;
  bool allowBind = true;
  bool cetCompat = false;
  bool nxCompat = true;
  bool allowIsolation = true;
  bool terminalServerAware = true;
  bool largeAddressAware = false;
  bool highEntropyVA = false;
  bool appContainer = false;
  bool mingw = false;
  bool warnMissingOrderSymbol = true;
  bool warnLocallyDefinedImported = true;
  bool warnDebugInfoUnusable = true;
  bool warnLongSectionNames = true;
  bool warnStdcallFixup = true;
  bool incremental = true;
  bool integrityCheck = false;
  bool killAt = false;
  bool repro = false;
  bool swaprunCD = false;
  bool swaprunNet = false;
  bool thinLTOEmitImportsFiles;
  bool thinLTOIndexOnly;
  bool timeTraceEnabled = false;
  bool autoImport = false;
  bool pseudoRelocs = false;
  bool stdcallFixup = false;
  bool writeCheckSum = false;
  EmitKind emit = EmitKind::Obj;
  bool allowDuplicateWeak = false;
  BuildIDHash buildIDHash = BuildIDHash::None;
};

} // namespace lld::coff

#endif
