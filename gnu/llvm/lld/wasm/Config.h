//===- Config.h -------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_WASM_CONFIG_H
#define LLD_WASM_CONFIG_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/BinaryFormat/Wasm.h"
#include "llvm/Support/CachePruning.h"
#include <optional>

namespace llvm {
enum class CodeGenOptLevel;
} // namespace llvm

namespace lld::wasm {

class InputFile;
class StubFile;
class ObjFile;
class SharedFile;
class BitcodeFile;
class InputTable;
class InputGlobal;
class InputFunction;
class Symbol;

// For --unresolved-symbols.
enum class UnresolvedPolicy { ReportError, Warn, Ignore, ImportDynamic };

// For --build-id.
enum class BuildIdKind { None, Fast, Sha1, Hexstring, Uuid };

// This struct contains the global configuration for the linker.
// Most fields are direct mapping from the command line options
// and such fields have the same name as the corresponding options.
// Most fields are initialized by the driver.
struct Configuration {
  bool bsymbolic;
  bool checkFeatures;
  bool compressRelocations;
  bool demangle;
  bool disableVerify;
  bool experimentalPic;
  bool emitRelocs;
  bool exportAll;
  bool exportDynamic;
  bool exportTable;
  bool extendedConst;
  bool growableTable;
  bool gcSections;
  llvm::StringSet<> keepSections;
  std::optional<std::pair<llvm::StringRef, llvm::StringRef>> memoryImport;
  std::optional<llvm::StringRef> memoryExport;
  bool sharedMemory;
  bool importTable;
  bool importUndefined;
  std::optional<bool> is64;
  bool mergeDataSegments;
  bool pie;
  bool printGcSections;
  bool relocatable;
  bool saveTemps;
  bool shared;
  bool shlibSigCheck;
  bool stripAll;
  bool stripDebug;
  bool stackFirst;
  // Because dyamanic linking under Wasm is still experimental we default to
  // static linking
  bool isStatic = true;
  bool trace;
  uint64_t globalBase;
  uint64_t initialHeap;
  uint64_t initialMemory;
  uint64_t maxMemory;
  bool noGrowableMemory;
  // The table offset at which to place function addresses.  We reserve zero
  // for the null function pointer.  This gets set to 1 for executables and 0
  // for shared libraries (since they always added to a dynamic offset at
  // runtime).
  uint64_t tableBase;
  uint64_t zStackSize;
  unsigned ltoPartitions;
  unsigned ltoo;
  llvm::CodeGenOptLevel ltoCgo;
  unsigned optimize;
  llvm::StringRef thinLTOJobs;
  bool ltoDebugPassManager;
  UnresolvedPolicy unresolvedSymbols;
  BuildIdKind buildId = BuildIdKind::None;

  llvm::StringRef entry;
  llvm::StringRef mapFile;
  llvm::StringRef outputFile;
  llvm::StringRef soName;
  llvm::StringRef thinLTOCacheDir;
  llvm::StringRef whyExtract;

  llvm::StringSet<> allowUndefinedSymbols;
  llvm::StringSet<> exportedSymbols;
  std::vector<llvm::StringRef> requiredExports;
  llvm::SmallVector<llvm::StringRef, 0> searchPaths;
  llvm::CachePruningPolicy thinLTOCachePolicy;
  std::optional<std::vector<std::string>> features;
  std::optional<std::vector<std::string>> extraFeatures;
  llvm::SmallVector<uint8_t, 0> buildIdVector;
};

// The only instance of Configuration struct.
extern Configuration *config;

// The Ctx object hold all other (non-configuration) global state.
struct Ctx {
  llvm::SmallVector<ObjFile *, 0> objectFiles;
  llvm::SmallVector<StubFile *, 0> stubFiles;
  llvm::SmallVector<SharedFile *, 0> sharedFiles;
  llvm::SmallVector<BitcodeFile *, 0> bitcodeFiles;
  llvm::SmallVector<InputFunction *, 0> syntheticFunctions;
  llvm::SmallVector<InputGlobal *, 0> syntheticGlobals;
  llvm::SmallVector<InputTable *, 0> syntheticTables;

  // True if we are creating position-independent code.
  bool isPic = false;

  // True if we have an MVP input that uses __indirect_function_table and which
  // requires it to be allocated to table number 0.
  bool legacyFunctionTable = false;

  // Will be set to true if bss data segments should be emitted. In most cases
  // this is not necessary.
  bool emitBssSegments = false;

  // A tuple of (reference, extractedFile, sym). Used by --why-extract=.
  llvm::SmallVector<std::tuple<std::string, const InputFile *, const Symbol &>,
                    0>
      whyExtractRecords;

  void reset();
};

extern Ctx ctx;

} // namespace lld::wasm

#endif
