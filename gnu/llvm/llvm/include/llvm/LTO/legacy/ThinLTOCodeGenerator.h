//===-ThinLTOCodeGenerator.h - LLVM Link Time Optimizer -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the ThinLTOCodeGenerator class, similar to the
// LTOCodeGenerator but for the ThinLTO scheme. It provides an interface for
// linker plugin.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LTO_LEGACY_THINLTOCODEGENERATOR_H
#define LLVM_LTO_LEGACY_THINLTOCODEGENERATOR_H

#include "llvm-c/lto.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Support/CachePruning.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Triple.h"

#include <string>

namespace llvm {
class StringRef;
class TargetMachine;

/// Helper to gather options relevant to the target machine creation
struct TargetMachineBuilder {
  Triple TheTriple;
  std::string MCpu;
  std::string MAttr;
  TargetOptions Options;
  std::optional<Reloc::Model> RelocModel;
  CodeGenOptLevel CGOptLevel = CodeGenOptLevel::Aggressive;

  std::unique_ptr<TargetMachine> create() const;
};

/// This class define an interface similar to the LTOCodeGenerator, but adapted
/// for ThinLTO processing.
/// The ThinLTOCodeGenerator is not intended to be reuse for multiple
/// compilation: the model is that the client adds modules to the generator and
/// ask to perform the ThinLTO optimizations / codegen, and finally destroys the
/// codegenerator.
class ThinLTOCodeGenerator {
public:
  /// Add given module to the code generator.
  void addModule(StringRef Identifier, StringRef Data);

  /**
   * Adds to a list of all global symbols that must exist in the final generated
   * code. If a symbol is not listed there, it will be optimized away if it is
   * inlined into every usage.
   */
  void preserveSymbol(StringRef Name);

  /**
   * Adds to a list of all global symbols that are cross-referenced between
   * ThinLTO files. If the ThinLTO CodeGenerator can ensure that every
   * references from a ThinLTO module to this symbol is optimized away, then
   * the symbol can be discarded.
   */
  void crossReferenceSymbol(StringRef Name);

  /**
   * Process all the modules that were added to the code generator in parallel.
   *
   * Client can access the resulting object files using getProducedBinaries(),
   * unless setGeneratedObjectsDirectory() has been called, in which case
   * results are available through getProducedBinaryFiles().
   */
  void run();

  /**
   * Return the "in memory" binaries produced by the code generator. This is
   * filled after run() unless setGeneratedObjectsDirectory() has been
   * called, in which case results are available through
   * getProducedBinaryFiles().
   */
  std::vector<std::unique_ptr<MemoryBuffer>> &getProducedBinaries() {
    return ProducedBinaries;
  }

  /**
   * Return the "on-disk" binaries produced by the code generator. This is
   * filled after run() when setGeneratedObjectsDirectory() has been
   * called, in which case results are available through getProducedBinaries().
   */
  std::vector<std::string> &getProducedBinaryFiles() {
    return ProducedBinaryFiles;
  }

  /**
   * \defgroup Options setters
   * @{
   */

  /**
   * \defgroup Cache controlling options
   *
   * These entry points control the ThinLTO cache. The cache is intended to
   * support incremental build, and thus needs to be persistent accross build.
   * The client enabled the cache by supplying a path to an existing directory.
   * The code generator will use this to store objects files that may be reused
   * during a subsequent build.
   * To avoid filling the disk space, a few knobs are provided:
   *  - The pruning interval limit the frequency at which the garbage collector
   *    will try to scan the cache directory to prune it from expired entries.
   *    Setting to -1 disable the pruning (default). Setting to 0 will force
   *    pruning to occur.
   *  - The pruning expiration time indicates to the garbage collector how old
   *    an entry needs to be to be removed.
   *  - Finally, the garbage collector can be instructed to prune the cache till
   *    the occupied space goes below a threshold.
   * @{
   */

  struct CachingOptions {
    std::string Path;                    // Path to the cache, empty to disable.
    CachePruningPolicy Policy;
  };

  /// Provide a path to a directory where to store the cached files for
  /// incremental build.
  void setCacheDir(std::string Path) { CacheOptions.Path = std::move(Path); }

  /// Cache policy: interval (seconds) between two prunes of the cache. Set to a
  /// negative value to disable pruning. A value of 0 will force pruning to
  /// occur.
  void setCachePruningInterval(int Interval) {
    if(Interval < 0)
      CacheOptions.Policy.Interval.reset();
    else
      CacheOptions.Policy.Interval = std::chrono::seconds(Interval);
  }

  /// Cache policy: expiration (in seconds) for an entry.
  /// A value of 0 will be ignored.
  void setCacheEntryExpiration(unsigned Expiration) {
    if (Expiration)
      CacheOptions.Policy.Expiration = std::chrono::seconds(Expiration);
  }

  /**
   * Sets the maximum cache size that can be persistent across build, in terms
   * of percentage of the available space on the disk. Set to 100 to indicate
   * no limit, 50 to indicate that the cache size will not be left over
   * half the available space. A value over 100 will be reduced to 100, and a
   * value of 0 will be ignored.
   *
   *
   * The formula looks like:
   *  AvailableSpace = FreeSpace + ExistingCacheSize
   *  NewCacheSize = AvailableSpace * P/100
   *
   */
  void setMaxCacheSizeRelativeToAvailableSpace(unsigned Percentage) {
    if (Percentage)
      CacheOptions.Policy.MaxSizePercentageOfAvailableSpace = Percentage;
  }

  /// Cache policy: the maximum size for the cache directory in bytes. A value
  /// over the amount of available space on the disk will be reduced to the
  /// amount of available space. A value of 0 will be ignored.
  void setCacheMaxSizeBytes(uint64_t MaxSizeBytes) {
    if (MaxSizeBytes)
      CacheOptions.Policy.MaxSizeBytes = MaxSizeBytes;
  }

  /// Cache policy: the maximum number of files in the cache directory. A value
  /// of 0 will be ignored.
  void setCacheMaxSizeFiles(unsigned MaxSizeFiles) {
    if (MaxSizeFiles)
      CacheOptions.Policy.MaxSizeFiles = MaxSizeFiles;
  }

  /**@}*/

  /// Set the path to a directory where to save temporaries at various stages of
  /// the processing.
  void setSaveTempsDir(std::string Path) { SaveTempsDir = std::move(Path); }

  /// Set the path to a directory where to save generated object files. This
  /// path can be used by a linker to request on-disk files instead of in-memory
  /// buffers. When set, results are available through getProducedBinaryFiles()
  /// instead of getProducedBinaries().
  void setGeneratedObjectsDirectory(std::string Path) {
    SavedObjectsDirectoryPath = std::move(Path);
  }

  /// CPU to use to initialize the TargetMachine
  void setCpu(std::string Cpu) { TMBuilder.MCpu = std::move(Cpu); }

  /// Subtarget attributes
  void setAttr(std::string MAttr) { TMBuilder.MAttr = std::move(MAttr); }

  /// TargetMachine options
  void setTargetOptions(TargetOptions Options) {
    TMBuilder.Options = std::move(Options);
  }

  /// Enable the Freestanding mode: indicate that the optimizer should not
  /// assume builtins are present on the target.
  void setFreestanding(bool Enabled) { Freestanding = Enabled; }

  /// CodeModel
  void setCodePICModel(std::optional<Reloc::Model> Model) {
    TMBuilder.RelocModel = Model;
  }

  /// CodeGen optimization level
  void setCodeGenOptLevel(CodeGenOptLevel CGOptLevel) {
    TMBuilder.CGOptLevel = CGOptLevel;
  }

  /// IR optimization level: from 0 to 3.
  void setOptLevel(unsigned NewOptLevel) {
    OptLevel = (NewOptLevel > 3) ? 3 : NewOptLevel;
  }

  /// Enable or disable debug output for the new pass manager.
  void setDebugPassManager(unsigned Enabled) { DebugPassManager = Enabled; }

  /// Disable CodeGen, only run the stages till codegen and stop. The output
  /// will be bitcode.
  void disableCodeGen(bool Disable) { DisableCodeGen = Disable; }

  /// Perform CodeGen only: disable all other stages.
  void setCodeGenOnly(bool CGOnly) { CodeGenOnly = CGOnly; }

  /**@}*/

  /**
   * \defgroup Set of APIs to run individual stages in isolation.
   * @{
   */

  /**
   * Produce the combined summary index from all the bitcode files:
   * "thin-link".
   */
  std::unique_ptr<ModuleSummaryIndex> linkCombinedIndex();

  /**
   * Perform promotion and renaming of exported internal functions,
   * and additionally resolve weak and linkonce symbols.
   * Index is updated to reflect linkage changes from weak resolution.
   */
  void promote(Module &Module, ModuleSummaryIndex &Index,
               const lto::InputFile &File);

  /**
   * Compute and emit the imported files for module at \p ModulePath.
   */
  void emitImports(Module &Module, StringRef OutputName,
                   ModuleSummaryIndex &Index,
                   const lto::InputFile &File);

  /**
   * Perform cross-module importing for the module identified by
   * ModuleIdentifier.
   */
  void crossModuleImport(Module &Module, ModuleSummaryIndex &Index,
                         const lto::InputFile &File);

  /**
   * Compute the list of summaries and the subset of declaration summaries
   * needed for importing into module.
   */
  void gatherImportedSummariesForModule(
      Module &Module, ModuleSummaryIndex &Index,
      std::map<std::string, GVSummaryMapTy> &ModuleToSummariesForIndex,
      GVSummaryPtrSet &DecSummaries, const lto::InputFile &File);

  /**
   * Perform internalization. Index is updated to reflect linkage changes.
   */
  void internalize(Module &Module, ModuleSummaryIndex &Index,
                   const lto::InputFile &File);

  /**
   * Perform post-importing ThinLTO optimizations.
   */
  void optimize(Module &Module);

  /**
   * Write temporary object file to SavedObjectDirectoryPath, write symlink
   * to Cache directory if needed. Returns the path to the generated file in
   * SavedObjectsDirectoryPath.
   */
  std::string writeGeneratedObject(int count, StringRef CacheEntryPath,
                                   const MemoryBuffer &OutputBuffer);
  /**@}*/

private:
  /// Helper factory to build a TargetMachine
  TargetMachineBuilder TMBuilder;

  /// Vector holding the in-memory buffer containing the produced binaries, when
  /// SavedObjectsDirectoryPath isn't set.
  std::vector<std::unique_ptr<MemoryBuffer>> ProducedBinaries;

  /// Path to generated files in the supplied SavedObjectsDirectoryPath if any.
  std::vector<std::string> ProducedBinaryFiles;

  /// Vector holding the input buffers containing the bitcode modules to
  /// process.
  std::vector<std::unique_ptr<lto::InputFile>> Modules;

  /// Set of symbols that need to be preserved outside of the set of bitcode
  /// files.
  StringSet<> PreservedSymbols;

  /// Set of symbols that are cross-referenced between bitcode files.
  StringSet<> CrossReferencedSymbols;

  /// Control the caching behavior.
  CachingOptions CacheOptions;

  /// Path to a directory to save the temporary bitcode files.
  std::string SaveTempsDir;

  /// Path to a directory to save the generated object files.
  std::string SavedObjectsDirectoryPath;

  /// Flag to enable/disable CodeGen. When set to true, the process stops after
  /// optimizations and a bitcode is produced.
  bool DisableCodeGen = false;

  /// Flag to indicate that only the CodeGen will be performed, no cross-module
  /// importing or optimization.
  bool CodeGenOnly = false;

  /// Flag to indicate that the optimizer should not assume builtins are present
  /// on the target.
  bool Freestanding = false;

  /// IR Optimization Level [0-3].
  unsigned OptLevel = 3;

  /// Flag to indicate whether debug output should be enabled for the new pass
  /// manager.
  bool DebugPassManager = false;
};
}
#endif
