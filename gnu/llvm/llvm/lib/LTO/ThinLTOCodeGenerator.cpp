//===-ThinLTOCodeGenerator.cpp - LLVM Link Time Optimizer -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Thin Link Time Optimization library. This library is
// intended to be used by linker to optimize code at link time.
//
//===----------------------------------------------------------------------===//

#include "llvm/LTO/legacy/ThinLTOCodeGenerator.h"
#include "llvm/Support/CommandLine.h"

#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/ModuleSummaryAnalysis.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Bitcode/BitcodeWriterPass.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LLVMRemarkStreamer.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/PassTimingInfo.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/LTO/LTO.h"
#include "llvm/LTO/SummaryBasedOptimizations.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/IRObjectFile.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/StandardInstrumentations.h"
#include "llvm/Remarks/HotnessThresholdParser.h"
#include "llvm/Support/CachePruning.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SHA1.h"
#include "llvm/Support/SmallVectorMemoryBuffer.h"
#include "llvm/Support/ThreadPool.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include "llvm/Transforms/IPO/FunctionAttrs.h"
#include "llvm/Transforms/IPO/FunctionImport.h"
#include "llvm/Transforms/IPO/Internalize.h"
#include "llvm/Transforms/IPO/WholeProgramDevirt.h"
#include "llvm/Transforms/ObjCARC.h"
#include "llvm/Transforms/Utils/FunctionImportUtils.h"

#include <numeric>

#if !defined(_MSC_VER) && !defined(__MINGW32__)
#include <unistd.h>
#else
#include <io.h>
#endif

using namespace llvm;

#define DEBUG_TYPE "thinlto"

namespace llvm {
// Flags -discard-value-names, defined in LTOCodeGenerator.cpp
extern cl::opt<bool> LTODiscardValueNames;
extern cl::opt<std::string> RemarksFilename;
extern cl::opt<std::string> RemarksPasses;
extern cl::opt<bool> RemarksWithHotness;
extern cl::opt<std::optional<uint64_t>, false, remarks::HotnessThresholdParser>
    RemarksHotnessThreshold;
extern cl::opt<std::string> RemarksFormat;
}

namespace {

// Default to using all available threads in the system, but using only one
// thred per core, as indicated by the usage of
// heavyweight_hardware_concurrency() below.
static cl::opt<int> ThreadCount("threads", cl::init(0));

// Simple helper to save temporary files for debug.
static void saveTempBitcode(const Module &TheModule, StringRef TempDir,
                            unsigned count, StringRef Suffix) {
  if (TempDir.empty())
    return;
  // User asked to save temps, let dump the bitcode file after import.
  std::string SaveTempPath = (TempDir + llvm::Twine(count) + Suffix).str();
  std::error_code EC;
  raw_fd_ostream OS(SaveTempPath, EC, sys::fs::OF_None);
  if (EC)
    report_fatal_error(Twine("Failed to open ") + SaveTempPath +
                       " to save optimized bitcode\n");
  WriteBitcodeToFile(TheModule, OS, /* ShouldPreserveUseListOrder */ true);
}

static const GlobalValueSummary *
getFirstDefinitionForLinker(const GlobalValueSummaryList &GVSummaryList) {
  // If there is any strong definition anywhere, get it.
  auto StrongDefForLinker = llvm::find_if(
      GVSummaryList, [](const std::unique_ptr<GlobalValueSummary> &Summary) {
        auto Linkage = Summary->linkage();
        return !GlobalValue::isAvailableExternallyLinkage(Linkage) &&
               !GlobalValue::isWeakForLinker(Linkage);
      });
  if (StrongDefForLinker != GVSummaryList.end())
    return StrongDefForLinker->get();
  // Get the first *linker visible* definition for this global in the summary
  // list.
  auto FirstDefForLinker = llvm::find_if(
      GVSummaryList, [](const std::unique_ptr<GlobalValueSummary> &Summary) {
        auto Linkage = Summary->linkage();
        return !GlobalValue::isAvailableExternallyLinkage(Linkage);
      });
  // Extern templates can be emitted as available_externally.
  if (FirstDefForLinker == GVSummaryList.end())
    return nullptr;
  return FirstDefForLinker->get();
}

// Populate map of GUID to the prevailing copy for any multiply defined
// symbols. Currently assume first copy is prevailing, or any strong
// definition. Can be refined with Linker information in the future.
static void computePrevailingCopies(
    const ModuleSummaryIndex &Index,
    DenseMap<GlobalValue::GUID, const GlobalValueSummary *> &PrevailingCopy) {
  auto HasMultipleCopies = [&](const GlobalValueSummaryList &GVSummaryList) {
    return GVSummaryList.size() > 1;
  };

  for (auto &I : Index) {
    if (HasMultipleCopies(I.second.SummaryList))
      PrevailingCopy[I.first] =
          getFirstDefinitionForLinker(I.second.SummaryList);
  }
}

static StringMap<lto::InputFile *>
generateModuleMap(std::vector<std::unique_ptr<lto::InputFile>> &Modules) {
  StringMap<lto::InputFile *> ModuleMap;
  for (auto &M : Modules) {
    LLVM_DEBUG(dbgs() << "Adding module " << M->getName() << " to ModuleMap\n");
    assert(!ModuleMap.contains(M->getName()) &&
           "Expect unique Buffer Identifier");
    ModuleMap[M->getName()] = M.get();
  }
  return ModuleMap;
}

static void promoteModule(Module &TheModule, const ModuleSummaryIndex &Index,
                          bool ClearDSOLocalOnDeclarations) {
  if (renameModuleForThinLTO(TheModule, Index, ClearDSOLocalOnDeclarations))
    report_fatal_error("renameModuleForThinLTO failed");
}

namespace {
class ThinLTODiagnosticInfo : public DiagnosticInfo {
  const Twine &Msg;
public:
  ThinLTODiagnosticInfo(const Twine &DiagMsg,
                        DiagnosticSeverity Severity = DS_Error)
      : DiagnosticInfo(DK_Linker, Severity), Msg(DiagMsg) {}
  void print(DiagnosticPrinter &DP) const override { DP << Msg; }
};
}

/// Verify the module and strip broken debug info.
static void verifyLoadedModule(Module &TheModule) {
  bool BrokenDebugInfo = false;
  if (verifyModule(TheModule, &dbgs(), &BrokenDebugInfo))
    report_fatal_error("Broken module found, compilation aborted!");
  if (BrokenDebugInfo) {
    TheModule.getContext().diagnose(ThinLTODiagnosticInfo(
        "Invalid debug info found, debug info will be stripped", DS_Warning));
    StripDebugInfo(TheModule);
  }
}

static std::unique_ptr<Module> loadModuleFromInput(lto::InputFile *Input,
                                                   LLVMContext &Context,
                                                   bool Lazy,
                                                   bool IsImporting) {
  auto &Mod = Input->getSingleBitcodeModule();
  SMDiagnostic Err;
  Expected<std::unique_ptr<Module>> ModuleOrErr =
      Lazy ? Mod.getLazyModule(Context,
                               /* ShouldLazyLoadMetadata */ true, IsImporting)
           : Mod.parseModule(Context);
  if (!ModuleOrErr) {
    handleAllErrors(ModuleOrErr.takeError(), [&](ErrorInfoBase &EIB) {
      SMDiagnostic Err = SMDiagnostic(Mod.getModuleIdentifier(),
                                      SourceMgr::DK_Error, EIB.message());
      Err.print("ThinLTO", errs());
    });
    report_fatal_error("Can't load module, abort.");
  }
  if (!Lazy)
    verifyLoadedModule(*ModuleOrErr.get());
  return std::move(*ModuleOrErr);
}

static void
crossImportIntoModule(Module &TheModule, const ModuleSummaryIndex &Index,
                      StringMap<lto::InputFile *> &ModuleMap,
                      const FunctionImporter::ImportMapTy &ImportList,
                      bool ClearDSOLocalOnDeclarations) {
  auto Loader = [&](StringRef Identifier) {
    auto &Input = ModuleMap[Identifier];
    return loadModuleFromInput(Input, TheModule.getContext(),
                               /*Lazy=*/true, /*IsImporting*/ true);
  };

  FunctionImporter Importer(Index, Loader, ClearDSOLocalOnDeclarations);
  Expected<bool> Result = Importer.importFunctions(TheModule, ImportList);
  if (!Result) {
    handleAllErrors(Result.takeError(), [&](ErrorInfoBase &EIB) {
      SMDiagnostic Err = SMDiagnostic(TheModule.getModuleIdentifier(),
                                      SourceMgr::DK_Error, EIB.message());
      Err.print("ThinLTO", errs());
    });
    report_fatal_error("importFunctions failed");
  }
  // Verify again after cross-importing.
  verifyLoadedModule(TheModule);
}

static void optimizeModule(Module &TheModule, TargetMachine &TM,
                           unsigned OptLevel, bool Freestanding,
                           bool DebugPassManager, ModuleSummaryIndex *Index) {
  std::optional<PGOOptions> PGOOpt;
  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;

  PassInstrumentationCallbacks PIC;
  StandardInstrumentations SI(TheModule.getContext(), DebugPassManager);
  SI.registerCallbacks(PIC, &MAM);
  PipelineTuningOptions PTO;
  PTO.LoopVectorization = true;
  PTO.SLPVectorization = true;
  PassBuilder PB(&TM, PTO, PGOOpt, &PIC);

  std::unique_ptr<TargetLibraryInfoImpl> TLII(
      new TargetLibraryInfoImpl(Triple(TM.getTargetTriple())));
  if (Freestanding)
    TLII->disableAllFunctions();
  FAM.registerPass([&] { return TargetLibraryAnalysis(*TLII); });

  // Register all the basic analyses with the managers.
  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  ModulePassManager MPM;

  OptimizationLevel OL;

  switch (OptLevel) {
  default:
    llvm_unreachable("Invalid optimization level");
  case 0:
    OL = OptimizationLevel::O0;
    break;
  case 1:
    OL = OptimizationLevel::O1;
    break;
  case 2:
    OL = OptimizationLevel::O2;
    break;
  case 3:
    OL = OptimizationLevel::O3;
    break;
  }

  MPM.addPass(PB.buildThinLTODefaultPipeline(OL, Index));

  MPM.run(TheModule, MAM);
}

static void
addUsedSymbolToPreservedGUID(const lto::InputFile &File,
                             DenseSet<GlobalValue::GUID> &PreservedGUID) {
  for (const auto &Sym : File.symbols()) {
    if (Sym.isUsed())
      PreservedGUID.insert(GlobalValue::getGUID(Sym.getIRName()));
  }
}

// Convert the PreservedSymbols map from "Name" based to "GUID" based.
static void computeGUIDPreservedSymbols(const lto::InputFile &File,
                                        const StringSet<> &PreservedSymbols,
                                        const Triple &TheTriple,
                                        DenseSet<GlobalValue::GUID> &GUIDs) {
  // Iterate the symbols in the input file and if the input has preserved symbol
  // compute the GUID for the symbol.
  for (const auto &Sym : File.symbols()) {
    if (PreservedSymbols.count(Sym.getName()) && !Sym.getIRName().empty())
      GUIDs.insert(GlobalValue::getGUID(GlobalValue::getGlobalIdentifier(
          Sym.getIRName(), GlobalValue::ExternalLinkage, "")));
  }
}

static DenseSet<GlobalValue::GUID>
computeGUIDPreservedSymbols(const lto::InputFile &File,
                            const StringSet<> &PreservedSymbols,
                            const Triple &TheTriple) {
  DenseSet<GlobalValue::GUID> GUIDPreservedSymbols(PreservedSymbols.size());
  computeGUIDPreservedSymbols(File, PreservedSymbols, TheTriple,
                              GUIDPreservedSymbols);
  return GUIDPreservedSymbols;
}

std::unique_ptr<MemoryBuffer> codegenModule(Module &TheModule,
                                            TargetMachine &TM) {
  SmallVector<char, 128> OutputBuffer;

  // CodeGen
  {
    raw_svector_ostream OS(OutputBuffer);
    legacy::PassManager PM;

    // If the bitcode files contain ARC code and were compiled with optimization,
    // the ObjCARCContractPass must be run, so do it unconditionally here.
    PM.add(createObjCARCContractPass());

    // Setup the codegen now.
    if (TM.addPassesToEmitFile(PM, OS, nullptr, CodeGenFileType::ObjectFile,
                               /* DisableVerify */ true))
      report_fatal_error("Failed to setup codegen");

    // Run codegen now. resulting binary is in OutputBuffer.
    PM.run(TheModule);
  }
  return std::make_unique<SmallVectorMemoryBuffer>(
      std::move(OutputBuffer), /*RequiresNullTerminator=*/false);
}

/// Manage caching for a single Module.
class ModuleCacheEntry {
  SmallString<128> EntryPath;

public:
  // Create a cache entry. This compute a unique hash for the Module considering
  // the current list of export/import, and offer an interface to query to
  // access the content in the cache.
  ModuleCacheEntry(
      StringRef CachePath, const ModuleSummaryIndex &Index, StringRef ModuleID,
      const FunctionImporter::ImportMapTy &ImportList,
      const FunctionImporter::ExportSetTy &ExportList,
      const std::map<GlobalValue::GUID, GlobalValue::LinkageTypes> &ResolvedODR,
      const GVSummaryMapTy &DefinedGVSummaries, unsigned OptLevel,
      bool Freestanding, const TargetMachineBuilder &TMBuilder) {
    if (CachePath.empty())
      return;

    if (!Index.modulePaths().count(ModuleID))
      // The module does not have an entry, it can't have a hash at all
      return;

    if (all_of(Index.getModuleHash(ModuleID),
               [](uint32_t V) { return V == 0; }))
      // No hash entry, no caching!
      return;

    llvm::lto::Config Conf;
    Conf.OptLevel = OptLevel;
    Conf.Options = TMBuilder.Options;
    Conf.CPU = TMBuilder.MCpu;
    Conf.MAttrs.push_back(TMBuilder.MAttr);
    Conf.RelocModel = TMBuilder.RelocModel;
    Conf.CGOptLevel = TMBuilder.CGOptLevel;
    Conf.Freestanding = Freestanding;
    SmallString<40> Key;
    computeLTOCacheKey(Key, Conf, Index, ModuleID, ImportList, ExportList,
                       ResolvedODR, DefinedGVSummaries);

    // This choice of file name allows the cache to be pruned (see pruneCache()
    // in include/llvm/Support/CachePruning.h).
    sys::path::append(EntryPath, CachePath, "llvmcache-" + Key);
  }

  // Access the path to this entry in the cache.
  StringRef getEntryPath() { return EntryPath; }

  // Try loading the buffer for this cache entry.
  ErrorOr<std::unique_ptr<MemoryBuffer>> tryLoadingBuffer() {
    if (EntryPath.empty())
      return std::error_code();
    SmallString<64> ResultPath;
    Expected<sys::fs::file_t> FDOrErr = sys::fs::openNativeFileForRead(
        Twine(EntryPath), sys::fs::OF_UpdateAtime, &ResultPath);
    if (!FDOrErr)
      return errorToErrorCode(FDOrErr.takeError());
    ErrorOr<std::unique_ptr<MemoryBuffer>> MBOrErr = MemoryBuffer::getOpenFile(
        *FDOrErr, EntryPath, /*FileSize=*/-1, /*RequiresNullTerminator=*/false);
    sys::fs::closeFile(*FDOrErr);
    return MBOrErr;
  }

  // Cache the Produced object file
  void write(const MemoryBuffer &OutputBuffer) {
    if (EntryPath.empty())
      return;

    if (auto Err = llvm::writeToOutput(
            EntryPath, [&OutputBuffer](llvm::raw_ostream &OS) -> llvm::Error {
              OS << OutputBuffer.getBuffer();
              return llvm::Error::success();
            }))
      report_fatal_error(llvm::formatv("ThinLTO: Can't write file {0}: {1}",
                                       EntryPath,
                                       toString(std::move(Err)).c_str()));
  }
};

static std::unique_ptr<MemoryBuffer>
ProcessThinLTOModule(Module &TheModule, ModuleSummaryIndex &Index,
                     StringMap<lto::InputFile *> &ModuleMap, TargetMachine &TM,
                     const FunctionImporter::ImportMapTy &ImportList,
                     const FunctionImporter::ExportSetTy &ExportList,
                     const DenseSet<GlobalValue::GUID> &GUIDPreservedSymbols,
                     const GVSummaryMapTy &DefinedGlobals,
                     const ThinLTOCodeGenerator::CachingOptions &CacheOptions,
                     bool DisableCodeGen, StringRef SaveTempsDir,
                     bool Freestanding, unsigned OptLevel, unsigned count,
                     bool DebugPassManager) {
  // "Benchmark"-like optimization: single-source case
  bool SingleModule = (ModuleMap.size() == 1);

  // When linking an ELF shared object, dso_local should be dropped. We
  // conservatively do this for -fpic.
  bool ClearDSOLocalOnDeclarations =
      TM.getTargetTriple().isOSBinFormatELF() &&
      TM.getRelocationModel() != Reloc::Static &&
      TheModule.getPIELevel() == PIELevel::Default;

  if (!SingleModule) {
    promoteModule(TheModule, Index, ClearDSOLocalOnDeclarations);

    // Apply summary-based prevailing-symbol resolution decisions.
    thinLTOFinalizeInModule(TheModule, DefinedGlobals, /*PropagateAttrs=*/true);

    // Save temps: after promotion.
    saveTempBitcode(TheModule, SaveTempsDir, count, ".1.promoted.bc");
  }

  // Be friendly and don't nuke totally the module when the client didn't
  // supply anything to preserve.
  if (!ExportList.empty() || !GUIDPreservedSymbols.empty()) {
    // Apply summary-based internalization decisions.
    thinLTOInternalizeModule(TheModule, DefinedGlobals);
  }

  // Save internalized bitcode
  saveTempBitcode(TheModule, SaveTempsDir, count, ".2.internalized.bc");

  if (!SingleModule)
    crossImportIntoModule(TheModule, Index, ModuleMap, ImportList,
                          ClearDSOLocalOnDeclarations);

  // Do this after any importing so that imported code is updated.
  // See comment at call to updateVCallVisibilityInIndex() for why
  // WholeProgramVisibilityEnabledInLTO is false.
  updatePublicTypeTestCalls(TheModule,
                            /* WholeProgramVisibilityEnabledInLTO */ false);

  // Save temps: after cross-module import.
  saveTempBitcode(TheModule, SaveTempsDir, count, ".3.imported.bc");

  optimizeModule(TheModule, TM, OptLevel, Freestanding, DebugPassManager,
                 &Index);

  saveTempBitcode(TheModule, SaveTempsDir, count, ".4.opt.bc");

  if (DisableCodeGen) {
    // Configured to stop before CodeGen, serialize the bitcode and return.
    SmallVector<char, 128> OutputBuffer;
    {
      raw_svector_ostream OS(OutputBuffer);
      ProfileSummaryInfo PSI(TheModule);
      auto Index = buildModuleSummaryIndex(TheModule, nullptr, &PSI);
      WriteBitcodeToFile(TheModule, OS, true, &Index);
    }
    return std::make_unique<SmallVectorMemoryBuffer>(
        std::move(OutputBuffer), /*RequiresNullTerminator=*/false);
  }

  return codegenModule(TheModule, TM);
}

/// Resolve prevailing symbols. Record resolutions in the \p ResolvedODR map
/// for caching, and in the \p Index for application during the ThinLTO
/// backends. This is needed for correctness for exported symbols (ensure
/// at least one copy kept) and a compile-time optimization (to drop duplicate
/// copies when possible).
static void resolvePrevailingInIndex(
    ModuleSummaryIndex &Index,
    StringMap<std::map<GlobalValue::GUID, GlobalValue::LinkageTypes>>
        &ResolvedODR,
    const DenseSet<GlobalValue::GUID> &GUIDPreservedSymbols,
    const DenseMap<GlobalValue::GUID, const GlobalValueSummary *>
        &PrevailingCopy) {

  auto isPrevailing = [&](GlobalValue::GUID GUID, const GlobalValueSummary *S) {
    const auto &Prevailing = PrevailingCopy.find(GUID);
    // Not in map means that there was only one copy, which must be prevailing.
    if (Prevailing == PrevailingCopy.end())
      return true;
    return Prevailing->second == S;
  };

  auto recordNewLinkage = [&](StringRef ModuleIdentifier,
                              GlobalValue::GUID GUID,
                              GlobalValue::LinkageTypes NewLinkage) {
    ResolvedODR[ModuleIdentifier][GUID] = NewLinkage;
  };

  // TODO Conf.VisibilityScheme can be lto::Config::ELF for ELF.
  lto::Config Conf;
  thinLTOResolvePrevailingInIndex(Conf, Index, isPrevailing, recordNewLinkage,
                                  GUIDPreservedSymbols);
}

// Initialize the TargetMachine builder for a given Triple
static void initTMBuilder(TargetMachineBuilder &TMBuilder,
                          const Triple &TheTriple) {
  if (TMBuilder.MCpu.empty())
    TMBuilder.MCpu = lto::getThinLTODefaultCPU(TheTriple);
  TMBuilder.TheTriple = std::move(TheTriple);
}

} // end anonymous namespace

void ThinLTOCodeGenerator::addModule(StringRef Identifier, StringRef Data) {
  MemoryBufferRef Buffer(Data, Identifier);

  auto InputOrError = lto::InputFile::create(Buffer);
  if (!InputOrError)
    report_fatal_error(Twine("ThinLTO cannot create input file: ") +
                       toString(InputOrError.takeError()));

  auto TripleStr = (*InputOrError)->getTargetTriple();
  Triple TheTriple(TripleStr);

  if (Modules.empty())
    initTMBuilder(TMBuilder, Triple(TheTriple));
  else if (TMBuilder.TheTriple != TheTriple) {
    if (!TMBuilder.TheTriple.isCompatibleWith(TheTriple))
      report_fatal_error("ThinLTO modules with incompatible triples not "
                         "supported");
    initTMBuilder(TMBuilder, Triple(TMBuilder.TheTriple.merge(TheTriple)));
  }

  Modules.emplace_back(std::move(*InputOrError));
}

void ThinLTOCodeGenerator::preserveSymbol(StringRef Name) {
  PreservedSymbols.insert(Name);
}

void ThinLTOCodeGenerator::crossReferenceSymbol(StringRef Name) {
  // FIXME: At the moment, we don't take advantage of this extra information,
  // we're conservatively considering cross-references as preserved.
  //  CrossReferencedSymbols.insert(Name);
  PreservedSymbols.insert(Name);
}

// TargetMachine factory
std::unique_ptr<TargetMachine> TargetMachineBuilder::create() const {
  std::string ErrMsg;
  const Target *TheTarget =
      TargetRegistry::lookupTarget(TheTriple.str(), ErrMsg);
  if (!TheTarget) {
    report_fatal_error(Twine("Can't load target for this Triple: ") + ErrMsg);
  }

  // Use MAttr as the default set of features.
  SubtargetFeatures Features(MAttr);
  Features.getDefaultSubtargetFeatures(TheTriple);
  std::string FeatureStr = Features.getString();

  std::unique_ptr<TargetMachine> TM(
      TheTarget->createTargetMachine(TheTriple.str(), MCpu, FeatureStr, Options,
                                     RelocModel, std::nullopt, CGOptLevel));
  assert(TM && "Cannot create target machine");

  return TM;
}

/**
 * Produce the combined summary index from all the bitcode files:
 * "thin-link".
 */
std::unique_ptr<ModuleSummaryIndex> ThinLTOCodeGenerator::linkCombinedIndex() {
  std::unique_ptr<ModuleSummaryIndex> CombinedIndex =
      std::make_unique<ModuleSummaryIndex>(/*HaveGVs=*/false);
  for (auto &Mod : Modules) {
    auto &M = Mod->getSingleBitcodeModule();
    if (Error Err = M.readSummary(*CombinedIndex, Mod->getName())) {
      // FIXME diagnose
      logAllUnhandledErrors(
          std::move(Err), errs(),
          "error: can't create module summary index for buffer: ");
      return nullptr;
    }
  }
  return CombinedIndex;
}

namespace {
struct IsExported {
  const DenseMap<StringRef, FunctionImporter::ExportSetTy> &ExportLists;
  const DenseSet<GlobalValue::GUID> &GUIDPreservedSymbols;

  IsExported(
      const DenseMap<StringRef, FunctionImporter::ExportSetTy> &ExportLists,
      const DenseSet<GlobalValue::GUID> &GUIDPreservedSymbols)
      : ExportLists(ExportLists), GUIDPreservedSymbols(GUIDPreservedSymbols) {}

  bool operator()(StringRef ModuleIdentifier, ValueInfo VI) const {
    const auto &ExportList = ExportLists.find(ModuleIdentifier);
    return (ExportList != ExportLists.end() && ExportList->second.count(VI)) ||
           GUIDPreservedSymbols.count(VI.getGUID());
  }
};

struct IsPrevailing {
  const DenseMap<GlobalValue::GUID, const GlobalValueSummary *> &PrevailingCopy;
  IsPrevailing(const DenseMap<GlobalValue::GUID, const GlobalValueSummary *>
                   &PrevailingCopy)
      : PrevailingCopy(PrevailingCopy) {}

  bool operator()(GlobalValue::GUID GUID, const GlobalValueSummary *S) const {
    const auto &Prevailing = PrevailingCopy.find(GUID);
    // Not in map means that there was only one copy, which must be prevailing.
    if (Prevailing == PrevailingCopy.end())
      return true;
    return Prevailing->second == S;
  };
};
} // namespace

static void computeDeadSymbolsInIndex(
    ModuleSummaryIndex &Index,
    const DenseSet<GlobalValue::GUID> &GUIDPreservedSymbols) {
  // We have no symbols resolution available. And can't do any better now in the
  // case where the prevailing symbol is in a native object. It can be refined
  // with linker information in the future.
  auto isPrevailing = [&](GlobalValue::GUID G) {
    return PrevailingType::Unknown;
  };
  computeDeadSymbolsWithConstProp(Index, GUIDPreservedSymbols, isPrevailing,
                                  /* ImportEnabled = */ true);
}

/**
 * Perform promotion and renaming of exported internal functions.
 * Index is updated to reflect linkage changes from weak resolution.
 */
void ThinLTOCodeGenerator::promote(Module &TheModule, ModuleSummaryIndex &Index,
                                   const lto::InputFile &File) {
  auto ModuleCount = Index.modulePaths().size();
  auto ModuleIdentifier = TheModule.getModuleIdentifier();

  // Collect for each module the list of function it defines (GUID -> Summary).
  DenseMap<StringRef, GVSummaryMapTy> ModuleToDefinedGVSummaries;
  Index.collectDefinedGVSummariesPerModule(ModuleToDefinedGVSummaries);

  // Convert the preserved symbols set from string to GUID
  auto GUIDPreservedSymbols = computeGUIDPreservedSymbols(
      File, PreservedSymbols, Triple(TheModule.getTargetTriple()));

  // Add used symbol to the preserved symbols.
  addUsedSymbolToPreservedGUID(File, GUIDPreservedSymbols);

  // Compute "dead" symbols, we don't want to import/export these!
  computeDeadSymbolsInIndex(Index, GUIDPreservedSymbols);

  // Compute prevailing symbols
  DenseMap<GlobalValue::GUID, const GlobalValueSummary *> PrevailingCopy;
  computePrevailingCopies(Index, PrevailingCopy);

  // Generate import/export list
  DenseMap<StringRef, FunctionImporter::ImportMapTy> ImportLists(ModuleCount);
  DenseMap<StringRef, FunctionImporter::ExportSetTy> ExportLists(ModuleCount);
  ComputeCrossModuleImport(Index, ModuleToDefinedGVSummaries,
                           IsPrevailing(PrevailingCopy), ImportLists,
                           ExportLists);

  // Resolve prevailing symbols
  StringMap<std::map<GlobalValue::GUID, GlobalValue::LinkageTypes>> ResolvedODR;
  resolvePrevailingInIndex(Index, ResolvedODR, GUIDPreservedSymbols,
                           PrevailingCopy);

  thinLTOFinalizeInModule(TheModule,
                          ModuleToDefinedGVSummaries[ModuleIdentifier],
                          /*PropagateAttrs=*/false);

  // Promote the exported values in the index, so that they are promoted
  // in the module.
  thinLTOInternalizeAndPromoteInIndex(
      Index, IsExported(ExportLists, GUIDPreservedSymbols),
      IsPrevailing(PrevailingCopy));

  // FIXME Set ClearDSOLocalOnDeclarations.
  promoteModule(TheModule, Index, /*ClearDSOLocalOnDeclarations=*/false);
}

/**
 * Perform cross-module importing for the module identified by ModuleIdentifier.
 */
void ThinLTOCodeGenerator::crossModuleImport(Module &TheModule,
                                             ModuleSummaryIndex &Index,
                                             const lto::InputFile &File) {
  auto ModuleMap = generateModuleMap(Modules);
  auto ModuleCount = Index.modulePaths().size();

  // Collect for each module the list of function it defines (GUID -> Summary).
  DenseMap<StringRef, GVSummaryMapTy> ModuleToDefinedGVSummaries(ModuleCount);
  Index.collectDefinedGVSummariesPerModule(ModuleToDefinedGVSummaries);

  // Convert the preserved symbols set from string to GUID
  auto GUIDPreservedSymbols = computeGUIDPreservedSymbols(
      File, PreservedSymbols, Triple(TheModule.getTargetTriple()));

  addUsedSymbolToPreservedGUID(File, GUIDPreservedSymbols);

  // Compute "dead" symbols, we don't want to import/export these!
  computeDeadSymbolsInIndex(Index, GUIDPreservedSymbols);

  // Compute prevailing symbols
  DenseMap<GlobalValue::GUID, const GlobalValueSummary *> PrevailingCopy;
  computePrevailingCopies(Index, PrevailingCopy);

  // Generate import/export list
  DenseMap<StringRef, FunctionImporter::ImportMapTy> ImportLists(ModuleCount);
  DenseMap<StringRef, FunctionImporter::ExportSetTy> ExportLists(ModuleCount);
  ComputeCrossModuleImport(Index, ModuleToDefinedGVSummaries,
                           IsPrevailing(PrevailingCopy), ImportLists,
                           ExportLists);
  auto &ImportList = ImportLists[TheModule.getModuleIdentifier()];

  // FIXME Set ClearDSOLocalOnDeclarations.
  crossImportIntoModule(TheModule, Index, ModuleMap, ImportList,
                        /*ClearDSOLocalOnDeclarations=*/false);
}

/**
 * Compute the list of summaries needed for importing into module.
 */
void ThinLTOCodeGenerator::gatherImportedSummariesForModule(
    Module &TheModule, ModuleSummaryIndex &Index,
    std::map<std::string, GVSummaryMapTy> &ModuleToSummariesForIndex,
    GVSummaryPtrSet &DecSummaries, const lto::InputFile &File) {
  auto ModuleCount = Index.modulePaths().size();
  auto ModuleIdentifier = TheModule.getModuleIdentifier();

  // Collect for each module the list of function it defines (GUID -> Summary).
  DenseMap<StringRef, GVSummaryMapTy> ModuleToDefinedGVSummaries(ModuleCount);
  Index.collectDefinedGVSummariesPerModule(ModuleToDefinedGVSummaries);

  // Convert the preserved symbols set from string to GUID
  auto GUIDPreservedSymbols = computeGUIDPreservedSymbols(
      File, PreservedSymbols, Triple(TheModule.getTargetTriple()));

  addUsedSymbolToPreservedGUID(File, GUIDPreservedSymbols);

  // Compute "dead" symbols, we don't want to import/export these!
  computeDeadSymbolsInIndex(Index, GUIDPreservedSymbols);

  // Compute prevailing symbols
  DenseMap<GlobalValue::GUID, const GlobalValueSummary *> PrevailingCopy;
  computePrevailingCopies(Index, PrevailingCopy);

  // Generate import/export list
  DenseMap<StringRef, FunctionImporter::ImportMapTy> ImportLists(ModuleCount);
  DenseMap<StringRef, FunctionImporter::ExportSetTy> ExportLists(ModuleCount);
  ComputeCrossModuleImport(Index, ModuleToDefinedGVSummaries,
                           IsPrevailing(PrevailingCopy), ImportLists,
                           ExportLists);

  llvm::gatherImportedSummariesForModule(
      ModuleIdentifier, ModuleToDefinedGVSummaries,
      ImportLists[ModuleIdentifier], ModuleToSummariesForIndex, DecSummaries);
}

/**
 * Emit the list of files needed for importing into module.
 */
void ThinLTOCodeGenerator::emitImports(Module &TheModule, StringRef OutputName,
                                       ModuleSummaryIndex &Index,
                                       const lto::InputFile &File) {
  auto ModuleCount = Index.modulePaths().size();
  auto ModuleIdentifier = TheModule.getModuleIdentifier();

  // Collect for each module the list of function it defines (GUID -> Summary).
  DenseMap<StringRef, GVSummaryMapTy> ModuleToDefinedGVSummaries(ModuleCount);
  Index.collectDefinedGVSummariesPerModule(ModuleToDefinedGVSummaries);

  // Convert the preserved symbols set from string to GUID
  auto GUIDPreservedSymbols = computeGUIDPreservedSymbols(
      File, PreservedSymbols, Triple(TheModule.getTargetTriple()));

  addUsedSymbolToPreservedGUID(File, GUIDPreservedSymbols);

  // Compute "dead" symbols, we don't want to import/export these!
  computeDeadSymbolsInIndex(Index, GUIDPreservedSymbols);

  // Compute prevailing symbols
  DenseMap<GlobalValue::GUID, const GlobalValueSummary *> PrevailingCopy;
  computePrevailingCopies(Index, PrevailingCopy);

  // Generate import/export list
  DenseMap<StringRef, FunctionImporter::ImportMapTy> ImportLists(ModuleCount);
  DenseMap<StringRef, FunctionImporter::ExportSetTy> ExportLists(ModuleCount);
  ComputeCrossModuleImport(Index, ModuleToDefinedGVSummaries,
                           IsPrevailing(PrevailingCopy), ImportLists,
                           ExportLists);

  // 'EmitImportsFiles' emits the list of modules from which to import from, and
  // the set of keys in `ModuleToSummariesForIndex` should be a superset of keys
  // in `DecSummaries`, so no need to use `DecSummaries` in `EmitImportFiles`.
  GVSummaryPtrSet DecSummaries;
  std::map<std::string, GVSummaryMapTy> ModuleToSummariesForIndex;
  llvm::gatherImportedSummariesForModule(
      ModuleIdentifier, ModuleToDefinedGVSummaries,
      ImportLists[ModuleIdentifier], ModuleToSummariesForIndex, DecSummaries);

  std::error_code EC;
  if ((EC = EmitImportsFiles(ModuleIdentifier, OutputName,
                             ModuleToSummariesForIndex)))
    report_fatal_error(Twine("Failed to open ") + OutputName +
                       " to save imports lists\n");
}

/**
 * Perform internalization. Runs promote and internalization together.
 * Index is updated to reflect linkage changes.
 */
void ThinLTOCodeGenerator::internalize(Module &TheModule,
                                       ModuleSummaryIndex &Index,
                                       const lto::InputFile &File) {
  initTMBuilder(TMBuilder, Triple(TheModule.getTargetTriple()));
  auto ModuleCount = Index.modulePaths().size();
  auto ModuleIdentifier = TheModule.getModuleIdentifier();

  // Convert the preserved symbols set from string to GUID
  auto GUIDPreservedSymbols =
      computeGUIDPreservedSymbols(File, PreservedSymbols, TMBuilder.TheTriple);

  addUsedSymbolToPreservedGUID(File, GUIDPreservedSymbols);

  // Collect for each module the list of function it defines (GUID -> Summary).
  DenseMap<StringRef, GVSummaryMapTy> ModuleToDefinedGVSummaries(ModuleCount);
  Index.collectDefinedGVSummariesPerModule(ModuleToDefinedGVSummaries);

  // Compute "dead" symbols, we don't want to import/export these!
  computeDeadSymbolsInIndex(Index, GUIDPreservedSymbols);

  // Compute prevailing symbols
  DenseMap<GlobalValue::GUID, const GlobalValueSummary *> PrevailingCopy;
  computePrevailingCopies(Index, PrevailingCopy);

  // Generate import/export list
  DenseMap<StringRef, FunctionImporter::ImportMapTy> ImportLists(ModuleCount);
  DenseMap<StringRef, FunctionImporter::ExportSetTy> ExportLists(ModuleCount);
  ComputeCrossModuleImport(Index, ModuleToDefinedGVSummaries,
                           IsPrevailing(PrevailingCopy), ImportLists,
                           ExportLists);
  auto &ExportList = ExportLists[ModuleIdentifier];

  // Be friendly and don't nuke totally the module when the client didn't
  // supply anything to preserve.
  if (ExportList.empty() && GUIDPreservedSymbols.empty())
    return;

  // Resolve prevailing symbols
  StringMap<std::map<GlobalValue::GUID, GlobalValue::LinkageTypes>> ResolvedODR;
  resolvePrevailingInIndex(Index, ResolvedODR, GUIDPreservedSymbols,
                           PrevailingCopy);

  // Promote the exported values in the index, so that they are promoted
  // in the module.
  thinLTOInternalizeAndPromoteInIndex(
      Index, IsExported(ExportLists, GUIDPreservedSymbols),
      IsPrevailing(PrevailingCopy));

  // FIXME Set ClearDSOLocalOnDeclarations.
  promoteModule(TheModule, Index, /*ClearDSOLocalOnDeclarations=*/false);

  // Internalization
  thinLTOFinalizeInModule(TheModule,
                          ModuleToDefinedGVSummaries[ModuleIdentifier],
                          /*PropagateAttrs=*/false);

  thinLTOInternalizeModule(TheModule,
                           ModuleToDefinedGVSummaries[ModuleIdentifier]);
}

/**
 * Perform post-importing ThinLTO optimizations.
 */
void ThinLTOCodeGenerator::optimize(Module &TheModule) {
  initTMBuilder(TMBuilder, Triple(TheModule.getTargetTriple()));

  // Optimize now
  optimizeModule(TheModule, *TMBuilder.create(), OptLevel, Freestanding,
                 DebugPassManager, nullptr);
}

/// Write out the generated object file, either from CacheEntryPath or from
/// OutputBuffer, preferring hard-link when possible.
/// Returns the path to the generated file in SavedObjectsDirectoryPath.
std::string
ThinLTOCodeGenerator::writeGeneratedObject(int count, StringRef CacheEntryPath,
                                           const MemoryBuffer &OutputBuffer) {
  auto ArchName = TMBuilder.TheTriple.getArchName();
  SmallString<128> OutputPath(SavedObjectsDirectoryPath);
  llvm::sys::path::append(OutputPath,
                          Twine(count) + "." + ArchName + ".thinlto.o");
  OutputPath.c_str(); // Ensure the string is null terminated.
  if (sys::fs::exists(OutputPath))
    sys::fs::remove(OutputPath);

  // We don't return a memory buffer to the linker, just a list of files.
  if (!CacheEntryPath.empty()) {
    // Cache is enabled, hard-link the entry (or copy if hard-link fails).
    auto Err = sys::fs::create_hard_link(CacheEntryPath, OutputPath);
    if (!Err)
      return std::string(OutputPath);
    // Hard linking failed, try to copy.
    Err = sys::fs::copy_file(CacheEntryPath, OutputPath);
    if (!Err)
      return std::string(OutputPath);
    // Copy failed (could be because the CacheEntry was removed from the cache
    // in the meantime by another process), fall back and try to write down the
    // buffer to the output.
    errs() << "remark: can't link or copy from cached entry '" << CacheEntryPath
           << "' to '" << OutputPath << "'\n";
  }
  // No cache entry, just write out the buffer.
  std::error_code Err;
  raw_fd_ostream OS(OutputPath, Err, sys::fs::OF_None);
  if (Err)
    report_fatal_error(Twine("Can't open output '") + OutputPath + "'\n");
  OS << OutputBuffer.getBuffer();
  return std::string(OutputPath);
}

// Main entry point for the ThinLTO processing
void ThinLTOCodeGenerator::run() {
  timeTraceProfilerBegin("ThinLink", StringRef(""));
  auto TimeTraceScopeExit = llvm::make_scope_exit([]() {
    if (llvm::timeTraceProfilerEnabled())
      llvm::timeTraceProfilerEnd();
  });
  // Prepare the resulting object vector
  assert(ProducedBinaries.empty() && "The generator should not be reused");
  if (SavedObjectsDirectoryPath.empty())
    ProducedBinaries.resize(Modules.size());
  else {
    sys::fs::create_directories(SavedObjectsDirectoryPath);
    bool IsDir;
    sys::fs::is_directory(SavedObjectsDirectoryPath, IsDir);
    if (!IsDir)
      report_fatal_error(Twine("Unexistent dir: '") + SavedObjectsDirectoryPath + "'");
    ProducedBinaryFiles.resize(Modules.size());
  }

  if (CodeGenOnly) {
    // Perform only parallel codegen and return.
    DefaultThreadPool Pool;
    int count = 0;
    for (auto &Mod : Modules) {
      Pool.async([&](int count) {
        LLVMContext Context;
        Context.setDiscardValueNames(LTODiscardValueNames);

        // Parse module now
        auto TheModule = loadModuleFromInput(Mod.get(), Context, false,
                                             /*IsImporting*/ false);

        // CodeGen
        auto OutputBuffer = codegenModule(*TheModule, *TMBuilder.create());
        if (SavedObjectsDirectoryPath.empty())
          ProducedBinaries[count] = std::move(OutputBuffer);
        else
          ProducedBinaryFiles[count] =
              writeGeneratedObject(count, "", *OutputBuffer);
      }, count++);
    }

    return;
  }

  // Sequential linking phase
  auto Index = linkCombinedIndex();

  // Save temps: index.
  if (!SaveTempsDir.empty()) {
    auto SaveTempPath = SaveTempsDir + "index.bc";
    std::error_code EC;
    raw_fd_ostream OS(SaveTempPath, EC, sys::fs::OF_None);
    if (EC)
      report_fatal_error(Twine("Failed to open ") + SaveTempPath +
                         " to save optimized bitcode\n");
    writeIndexToFile(*Index, OS);
  }


  // Prepare the module map.
  auto ModuleMap = generateModuleMap(Modules);
  auto ModuleCount = Modules.size();

  // Collect for each module the list of function it defines (GUID -> Summary).
  DenseMap<StringRef, GVSummaryMapTy> ModuleToDefinedGVSummaries(ModuleCount);
  Index->collectDefinedGVSummariesPerModule(ModuleToDefinedGVSummaries);

  // Convert the preserved symbols set from string to GUID, this is needed for
  // computing the caching hash and the internalization.
  DenseSet<GlobalValue::GUID> GUIDPreservedSymbols;
  for (const auto &M : Modules)
    computeGUIDPreservedSymbols(*M, PreservedSymbols, TMBuilder.TheTriple,
                                GUIDPreservedSymbols);

  // Add used symbol from inputs to the preserved symbols.
  for (const auto &M : Modules)
    addUsedSymbolToPreservedGUID(*M, GUIDPreservedSymbols);

  // Compute "dead" symbols, we don't want to import/export these!
  computeDeadSymbolsInIndex(*Index, GUIDPreservedSymbols);

  // Synthesize entry counts for functions in the combined index.
  computeSyntheticCounts(*Index);

  // Currently there is no support for enabling whole program visibility via a
  // linker option in the old LTO API, but this call allows it to be specified
  // via the internal option. Must be done before WPD below.
  if (hasWholeProgramVisibility(/* WholeProgramVisibilityEnabledInLTO */ false))
    Index->setWithWholeProgramVisibility();

  // FIXME: This needs linker information via a TBD new interface
  updateVCallVisibilityInIndex(*Index,
                               /*WholeProgramVisibilityEnabledInLTO=*/false,
                               // FIXME: These need linker information via a
                               // TBD new interface.
                               /*DynamicExportSymbols=*/{},
                               /*VisibleToRegularObjSymbols=*/{});

  // Perform index-based WPD. This will return immediately if there are
  // no index entries in the typeIdMetadata map (e.g. if we are instead
  // performing IR-based WPD in hybrid regular/thin LTO mode).
  std::map<ValueInfo, std::vector<VTableSlotSummary>> LocalWPDTargetsMap;
  std::set<GlobalValue::GUID> ExportedGUIDs;
  runWholeProgramDevirtOnIndex(*Index, ExportedGUIDs, LocalWPDTargetsMap);
  for (auto GUID : ExportedGUIDs)
    GUIDPreservedSymbols.insert(GUID);

  // Compute prevailing symbols
  DenseMap<GlobalValue::GUID, const GlobalValueSummary *> PrevailingCopy;
  computePrevailingCopies(*Index, PrevailingCopy);

  // Collect the import/export lists for all modules from the call-graph in the
  // combined index.
  DenseMap<StringRef, FunctionImporter::ImportMapTy> ImportLists(ModuleCount);
  DenseMap<StringRef, FunctionImporter::ExportSetTy> ExportLists(ModuleCount);
  ComputeCrossModuleImport(*Index, ModuleToDefinedGVSummaries,
                           IsPrevailing(PrevailingCopy), ImportLists,
                           ExportLists);

  // We use a std::map here to be able to have a defined ordering when
  // producing a hash for the cache entry.
  // FIXME: we should be able to compute the caching hash for the entry based
  // on the index, and nuke this map.
  StringMap<std::map<GlobalValue::GUID, GlobalValue::LinkageTypes>> ResolvedODR;

  // Resolve prevailing symbols, this has to be computed early because it
  // impacts the caching.
  resolvePrevailingInIndex(*Index, ResolvedODR, GUIDPreservedSymbols,
                           PrevailingCopy);

  // Use global summary-based analysis to identify symbols that can be
  // internalized (because they aren't exported or preserved as per callback).
  // Changes are made in the index, consumed in the ThinLTO backends.
  updateIndexWPDForExports(*Index,
                           IsExported(ExportLists, GUIDPreservedSymbols),
                           LocalWPDTargetsMap);
  thinLTOInternalizeAndPromoteInIndex(
      *Index, IsExported(ExportLists, GUIDPreservedSymbols),
      IsPrevailing(PrevailingCopy));

  thinLTOPropagateFunctionAttrs(*Index, IsPrevailing(PrevailingCopy));

  // Make sure that every module has an entry in the ExportLists, ImportList,
  // GVSummary and ResolvedODR maps to enable threaded access to these maps
  // below.
  for (auto &Module : Modules) {
    auto ModuleIdentifier = Module->getName();
    ExportLists[ModuleIdentifier];
    ImportLists[ModuleIdentifier];
    ResolvedODR[ModuleIdentifier];
    ModuleToDefinedGVSummaries[ModuleIdentifier];
  }

  std::vector<BitcodeModule *> ModulesVec;
  ModulesVec.reserve(Modules.size());
  for (auto &Mod : Modules)
    ModulesVec.push_back(&Mod->getSingleBitcodeModule());
  std::vector<int> ModulesOrdering = lto::generateModulesOrdering(ModulesVec);

  if (llvm::timeTraceProfilerEnabled())
    llvm::timeTraceProfilerEnd();

  TimeTraceScopeExit.release();

  // Parallel optimizer + codegen
  {
    DefaultThreadPool Pool(heavyweight_hardware_concurrency(ThreadCount));
    for (auto IndexCount : ModulesOrdering) {
      auto &Mod = Modules[IndexCount];
      Pool.async([&](int count) {
        auto ModuleIdentifier = Mod->getName();
        auto &ExportList = ExportLists[ModuleIdentifier];

        auto &DefinedGVSummaries = ModuleToDefinedGVSummaries[ModuleIdentifier];

        // The module may be cached, this helps handling it.
        ModuleCacheEntry CacheEntry(CacheOptions.Path, *Index, ModuleIdentifier,
                                    ImportLists[ModuleIdentifier], ExportList,
                                    ResolvedODR[ModuleIdentifier],
                                    DefinedGVSummaries, OptLevel, Freestanding,
                                    TMBuilder);
        auto CacheEntryPath = CacheEntry.getEntryPath();

        {
          auto ErrOrBuffer = CacheEntry.tryLoadingBuffer();
          LLVM_DEBUG(dbgs() << "Cache " << (ErrOrBuffer ? "hit" : "miss")
                            << " '" << CacheEntryPath << "' for buffer "
                            << count << " " << ModuleIdentifier << "\n");

          if (ErrOrBuffer) {
            // Cache Hit!
            if (SavedObjectsDirectoryPath.empty())
              ProducedBinaries[count] = std::move(ErrOrBuffer.get());
            else
              ProducedBinaryFiles[count] = writeGeneratedObject(
                  count, CacheEntryPath, *ErrOrBuffer.get());
            return;
          }
        }

        LLVMContext Context;
        Context.setDiscardValueNames(LTODiscardValueNames);
        Context.enableDebugTypeODRUniquing();
        auto DiagFileOrErr = lto::setupLLVMOptimizationRemarks(
            Context, RemarksFilename, RemarksPasses, RemarksFormat,
            RemarksWithHotness, RemarksHotnessThreshold, count);
        if (!DiagFileOrErr) {
          errs() << "Error: " << toString(DiagFileOrErr.takeError()) << "\n";
          report_fatal_error("ThinLTO: Can't get an output file for the "
                             "remarks");
        }

        // Parse module now
        auto TheModule = loadModuleFromInput(Mod.get(), Context, false,
                                             /*IsImporting*/ false);

        // Save temps: original file.
        saveTempBitcode(*TheModule, SaveTempsDir, count, ".0.original.bc");

        auto &ImportList = ImportLists[ModuleIdentifier];
        // Run the main process now, and generates a binary
        auto OutputBuffer = ProcessThinLTOModule(
            *TheModule, *Index, ModuleMap, *TMBuilder.create(), ImportList,
            ExportList, GUIDPreservedSymbols,
            ModuleToDefinedGVSummaries[ModuleIdentifier], CacheOptions,
            DisableCodeGen, SaveTempsDir, Freestanding, OptLevel, count,
            DebugPassManager);

        // Commit to the cache (if enabled)
        CacheEntry.write(*OutputBuffer);

        if (SavedObjectsDirectoryPath.empty()) {
          // We need to generated a memory buffer for the linker.
          if (!CacheEntryPath.empty()) {
            // When cache is enabled, reload from the cache if possible.
            // Releasing the buffer from the heap and reloading it from the
            // cache file with mmap helps us to lower memory pressure.
            // The freed memory can be used for the next input file.
            // The final binary link will read from the VFS cache (hopefully!)
            // or from disk (if the memory pressure was too high).
            auto ReloadedBufferOrErr = CacheEntry.tryLoadingBuffer();
            if (auto EC = ReloadedBufferOrErr.getError()) {
              // On error, keep the preexisting buffer and print a diagnostic.
              errs() << "remark: can't reload cached file '" << CacheEntryPath
                     << "': " << EC.message() << "\n";
            } else {
              OutputBuffer = std::move(*ReloadedBufferOrErr);
            }
          }
          ProducedBinaries[count] = std::move(OutputBuffer);
          return;
        }
        ProducedBinaryFiles[count] = writeGeneratedObject(
            count, CacheEntryPath, *OutputBuffer);
      }, IndexCount);
    }
  }

  pruneCache(CacheOptions.Path, CacheOptions.Policy, ProducedBinaries);

  // If statistics were requested, print them out now.
  if (llvm::AreStatisticsEnabled())
    llvm::PrintStatistics();
  reportAndResetTimings();
}
