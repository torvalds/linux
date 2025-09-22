//===--- BackendUtil.cpp - LLVM Backend Utilities -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/CodeGen/BackendUtil.h"
#include "BackendConsumer.h"
#include "LinkInModulesPass.h"
#include "clang/Basic/CodeGenOptions.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/TargetOptions.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Frontend/Utils.h"
#include "clang/Lex/HeaderSearchOptions.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Bitcode/BitcodeWriterPass.h"
#include "llvm/CodeGen/RegAllocRegistry.h"
#include "llvm/CodeGen/SchedulerRegistry.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Frontend/Driver/CodeGenOptions.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRPrinter/IRPrintingPasses.h"
#include "llvm/LTO/LTOBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/OffloadBinary.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/StandardInstrumentations.h"
#include "llvm/ProfileData/InstrProfCorrelator.h"
#include "llvm/Support/BuryPointer.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/HipStdPar/HipStdPar.h"
#include "llvm/Transforms/IPO/EmbedBitcodePass.h"
#include "llvm/Transforms/IPO/LowerTypeTests.h"
#include "llvm/Transforms/IPO/ThinLTOBitcodeWriter.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/Transforms/Instrumentation/AddressSanitizer.h"
#include "llvm/Transforms/Instrumentation/AddressSanitizerOptions.h"
#include "llvm/Transforms/Instrumentation/BoundsChecking.h"
#include "llvm/Transforms/Instrumentation/DataFlowSanitizer.h"
#include "llvm/Transforms/Instrumentation/GCOVProfiler.h"
#include "llvm/Transforms/Instrumentation/HWAddressSanitizer.h"
#include "llvm/Transforms/Instrumentation/InstrProfiling.h"
#include "llvm/Transforms/Instrumentation/KCFI.h"
#include "llvm/Transforms/Instrumentation/LowerAllowCheckPass.h"
#include "llvm/Transforms/Instrumentation/MemProfiler.h"
#include "llvm/Transforms/Instrumentation/MemorySanitizer.h"
#include "llvm/Transforms/Instrumentation/NumericalStabilitySanitizer.h"
#include "llvm/Transforms/Instrumentation/PGOInstrumentation.h"
#include "llvm/Transforms/Instrumentation/SanitizerBinaryMetadata.h"
#include "llvm/Transforms/Instrumentation/SanitizerCoverage.h"
#include "llvm/Transforms/Instrumentation/ThreadSanitizer.h"
#include "llvm/Transforms/ObjCARC.h"
#include "llvm/Transforms/Scalar/EarlyCSE.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/JumpThreading.h"
#include "llvm/Transforms/Utils/Debugify.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <memory>
#include <optional>
using namespace clang;
using namespace llvm;

#define HANDLE_EXTENSION(Ext)                                                  \
  llvm::PassPluginLibraryInfo get##Ext##PluginInfo();
#include "llvm/Support/Extension.def"

namespace llvm {
extern cl::opt<bool> PrintPipelinePasses;

// Experiment to move sanitizers earlier.
static cl::opt<bool> ClSanitizeOnOptimizerEarlyEP(
    "sanitizer-early-opt-ep", cl::Optional,
    cl::desc("Insert sanitizers on OptimizerEarlyEP."));

// Experiment to mark cold functions as optsize/minsize/optnone.
// TODO: remove once this is exposed as a proper driver flag.
static cl::opt<PGOOptions::ColdFuncOpt> ClPGOColdFuncAttr(
    "pgo-cold-func-opt", cl::init(PGOOptions::ColdFuncOpt::Default), cl::Hidden,
    cl::desc(
        "Function attribute to apply to cold functions as determined by PGO"),
    cl::values(clEnumValN(PGOOptions::ColdFuncOpt::Default, "default",
                          "Default (no attribute)"),
               clEnumValN(PGOOptions::ColdFuncOpt::OptSize, "optsize",
                          "Mark cold functions with optsize."),
               clEnumValN(PGOOptions::ColdFuncOpt::MinSize, "minsize",
                          "Mark cold functions with minsize."),
               clEnumValN(PGOOptions::ColdFuncOpt::OptNone, "optnone",
                          "Mark cold functions with optnone.")));

extern cl::opt<InstrProfCorrelator::ProfCorrelatorKind> ProfileCorrelate;
} // namespace llvm

namespace {

// Default filename used for profile generation.
std::string getDefaultProfileGenName() {
  return DebugInfoCorrelate || ProfileCorrelate != InstrProfCorrelator::NONE
             ? "default_%m.proflite"
             : "default_%m.profraw";
}

class EmitAssemblyHelper {
  DiagnosticsEngine &Diags;
  const HeaderSearchOptions &HSOpts;
  const CodeGenOptions &CodeGenOpts;
  const clang::TargetOptions &TargetOpts;
  const LangOptions &LangOpts;
  llvm::Module *TheModule;
  IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS;

  Timer CodeGenerationTime;

  std::unique_ptr<raw_pwrite_stream> OS;

  Triple TargetTriple;

  TargetIRAnalysis getTargetIRAnalysis() const {
    if (TM)
      return TM->getTargetIRAnalysis();

    return TargetIRAnalysis();
  }

  /// Generates the TargetMachine.
  /// Leaves TM unchanged if it is unable to create the target machine.
  /// Some of our clang tests specify triples which are not built
  /// into clang. This is okay because these tests check the generated
  /// IR, and they require DataLayout which depends on the triple.
  /// In this case, we allow this method to fail and not report an error.
  /// When MustCreateTM is used, we print an error if we are unable to load
  /// the requested target.
  void CreateTargetMachine(bool MustCreateTM);

  /// Add passes necessary to emit assembly or LLVM IR.
  ///
  /// \return True on success.
  bool AddEmitPasses(legacy::PassManager &CodeGenPasses, BackendAction Action,
                     raw_pwrite_stream &OS, raw_pwrite_stream *DwoOS);

  std::unique_ptr<llvm::ToolOutputFile> openOutputFile(StringRef Path) {
    std::error_code EC;
    auto F = std::make_unique<llvm::ToolOutputFile>(Path, EC,
                                                     llvm::sys::fs::OF_None);
    if (EC) {
      Diags.Report(diag::err_fe_unable_to_open_output) << Path << EC.message();
      F.reset();
    }
    return F;
  }

  void RunOptimizationPipeline(
      BackendAction Action, std::unique_ptr<raw_pwrite_stream> &OS,
      std::unique_ptr<llvm::ToolOutputFile> &ThinLinkOS, BackendConsumer *BC);
  void RunCodegenPipeline(BackendAction Action,
                          std::unique_ptr<raw_pwrite_stream> &OS,
                          std::unique_ptr<llvm::ToolOutputFile> &DwoOS);

  /// Check whether we should emit a module summary for regular LTO.
  /// The module summary should be emitted by default for regular LTO
  /// except for ld64 targets.
  ///
  /// \return True if the module summary should be emitted.
  bool shouldEmitRegularLTOSummary() const {
    return CodeGenOpts.PrepareForLTO && !CodeGenOpts.DisableLLVMPasses &&
           TargetTriple.getVendor() != llvm::Triple::Apple;
  }

  /// Check whether we should emit a flag for UnifiedLTO.
  /// The UnifiedLTO module flag should be set when UnifiedLTO is enabled for
  /// ThinLTO or Full LTO with module summaries.
  bool shouldEmitUnifiedLTOModueFlag() const {
    return CodeGenOpts.UnifiedLTO &&
           (CodeGenOpts.PrepareForThinLTO || shouldEmitRegularLTOSummary());
  }

public:
  EmitAssemblyHelper(DiagnosticsEngine &_Diags,
                     const HeaderSearchOptions &HeaderSearchOpts,
                     const CodeGenOptions &CGOpts,
                     const clang::TargetOptions &TOpts,
                     const LangOptions &LOpts, llvm::Module *M,
                     IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS)
      : Diags(_Diags), HSOpts(HeaderSearchOpts), CodeGenOpts(CGOpts),
        TargetOpts(TOpts), LangOpts(LOpts), TheModule(M), VFS(std::move(VFS)),
        CodeGenerationTime("codegen", "Code Generation Time"),
        TargetTriple(TheModule->getTargetTriple()) {}

  ~EmitAssemblyHelper() {
    if (CodeGenOpts.DisableFree)
      BuryPointer(std::move(TM));
  }

  std::unique_ptr<TargetMachine> TM;

  // Emit output using the new pass manager for the optimization pipeline.
  void EmitAssembly(BackendAction Action, std::unique_ptr<raw_pwrite_stream> OS,
                    BackendConsumer *BC);
};
} // namespace

static SanitizerCoverageOptions
getSancovOptsFromCGOpts(const CodeGenOptions &CGOpts) {
  SanitizerCoverageOptions Opts;
  Opts.CoverageType =
      static_cast<SanitizerCoverageOptions::Type>(CGOpts.SanitizeCoverageType);
  Opts.IndirectCalls = CGOpts.SanitizeCoverageIndirectCalls;
  Opts.TraceBB = CGOpts.SanitizeCoverageTraceBB;
  Opts.TraceCmp = CGOpts.SanitizeCoverageTraceCmp;
  Opts.TraceDiv = CGOpts.SanitizeCoverageTraceDiv;
  Opts.TraceGep = CGOpts.SanitizeCoverageTraceGep;
  Opts.Use8bitCounters = CGOpts.SanitizeCoverage8bitCounters;
  Opts.TracePC = CGOpts.SanitizeCoverageTracePC;
  Opts.TracePCGuard = CGOpts.SanitizeCoverageTracePCGuard;
  Opts.NoPrune = CGOpts.SanitizeCoverageNoPrune;
  Opts.Inline8bitCounters = CGOpts.SanitizeCoverageInline8bitCounters;
  Opts.InlineBoolFlag = CGOpts.SanitizeCoverageInlineBoolFlag;
  Opts.PCTable = CGOpts.SanitizeCoveragePCTable;
  Opts.StackDepth = CGOpts.SanitizeCoverageStackDepth;
  Opts.TraceLoads = CGOpts.SanitizeCoverageTraceLoads;
  Opts.TraceStores = CGOpts.SanitizeCoverageTraceStores;
  Opts.CollectControlFlow = CGOpts.SanitizeCoverageControlFlow;
  return Opts;
}

static SanitizerBinaryMetadataOptions
getSanitizerBinaryMetadataOptions(const CodeGenOptions &CGOpts) {
  SanitizerBinaryMetadataOptions Opts;
  Opts.Covered = CGOpts.SanitizeBinaryMetadataCovered;
  Opts.Atomics = CGOpts.SanitizeBinaryMetadataAtomics;
  Opts.UAR = CGOpts.SanitizeBinaryMetadataUAR;
  return Opts;
}

// Check if ASan should use GC-friendly instrumentation for globals.
// First of all, there is no point if -fdata-sections is off (expect for MachO,
// where this is not a factor). Also, on ELF this feature requires an assembler
// extension that only works with -integrated-as at the moment.
static bool asanUseGlobalsGC(const Triple &T, const CodeGenOptions &CGOpts) {
  if (!CGOpts.SanitizeAddressGlobalsDeadStripping)
    return false;
  switch (T.getObjectFormat()) {
  case Triple::MachO:
  case Triple::COFF:
    return true;
  case Triple::ELF:
    return !CGOpts.DisableIntegratedAS;
  case Triple::GOFF:
    llvm::report_fatal_error("ASan not implemented for GOFF");
  case Triple::XCOFF:
    llvm::report_fatal_error("ASan not implemented for XCOFF.");
  case Triple::Wasm:
  case Triple::DXContainer:
  case Triple::SPIRV:
  case Triple::UnknownObjectFormat:
    break;
  }
  return false;
}

static std::optional<llvm::CodeModel::Model>
getCodeModel(const CodeGenOptions &CodeGenOpts) {
  unsigned CodeModel = llvm::StringSwitch<unsigned>(CodeGenOpts.CodeModel)
                           .Case("tiny", llvm::CodeModel::Tiny)
                           .Case("small", llvm::CodeModel::Small)
                           .Case("kernel", llvm::CodeModel::Kernel)
                           .Case("medium", llvm::CodeModel::Medium)
                           .Case("large", llvm::CodeModel::Large)
                           .Case("default", ~1u)
                           .Default(~0u);
  assert(CodeModel != ~0u && "invalid code model!");
  if (CodeModel == ~1u)
    return std::nullopt;
  return static_cast<llvm::CodeModel::Model>(CodeModel);
}

static CodeGenFileType getCodeGenFileType(BackendAction Action) {
  if (Action == Backend_EmitObj)
    return CodeGenFileType::ObjectFile;
  else if (Action == Backend_EmitMCNull)
    return CodeGenFileType::Null;
  else {
    assert(Action == Backend_EmitAssembly && "Invalid action!");
    return CodeGenFileType::AssemblyFile;
  }
}

static bool actionRequiresCodeGen(BackendAction Action) {
  return Action != Backend_EmitNothing && Action != Backend_EmitBC &&
         Action != Backend_EmitLL;
}

static bool initTargetOptions(DiagnosticsEngine &Diags,
                              llvm::TargetOptions &Options,
                              const CodeGenOptions &CodeGenOpts,
                              const clang::TargetOptions &TargetOpts,
                              const LangOptions &LangOpts,
                              const HeaderSearchOptions &HSOpts) {
  switch (LangOpts.getThreadModel()) {
  case LangOptions::ThreadModelKind::POSIX:
    Options.ThreadModel = llvm::ThreadModel::POSIX;
    break;
  case LangOptions::ThreadModelKind::Single:
    Options.ThreadModel = llvm::ThreadModel::Single;
    break;
  }

  // Set float ABI type.
  assert((CodeGenOpts.FloatABI == "soft" || CodeGenOpts.FloatABI == "softfp" ||
          CodeGenOpts.FloatABI == "hard" || CodeGenOpts.FloatABI.empty()) &&
         "Invalid Floating Point ABI!");
  Options.FloatABIType =
      llvm::StringSwitch<llvm::FloatABI::ABIType>(CodeGenOpts.FloatABI)
          .Case("soft", llvm::FloatABI::Soft)
          .Case("softfp", llvm::FloatABI::Soft)
          .Case("hard", llvm::FloatABI::Hard)
          .Default(llvm::FloatABI::Default);

  // Set FP fusion mode.
  switch (LangOpts.getDefaultFPContractMode()) {
  case LangOptions::FPM_Off:
    // Preserve any contraction performed by the front-end.  (Strict performs
    // splitting of the muladd intrinsic in the backend.)
    Options.AllowFPOpFusion = llvm::FPOpFusion::Standard;
    break;
  case LangOptions::FPM_On:
  case LangOptions::FPM_FastHonorPragmas:
    Options.AllowFPOpFusion = llvm::FPOpFusion::Standard;
    break;
  case LangOptions::FPM_Fast:
    Options.AllowFPOpFusion = llvm::FPOpFusion::Fast;
    break;
  }

  Options.BinutilsVersion =
      llvm::TargetMachine::parseBinutilsVersion(CodeGenOpts.BinutilsVersion);
  Options.UseInitArray = CodeGenOpts.UseInitArray;
  Options.DisableIntegratedAS = CodeGenOpts.DisableIntegratedAS;

  // Set EABI version.
  Options.EABIVersion = TargetOpts.EABIVersion;

  if (LangOpts.hasSjLjExceptions())
    Options.ExceptionModel = llvm::ExceptionHandling::SjLj;
  if (LangOpts.hasSEHExceptions())
    Options.ExceptionModel = llvm::ExceptionHandling::WinEH;
  if (LangOpts.hasDWARFExceptions())
    Options.ExceptionModel = llvm::ExceptionHandling::DwarfCFI;
  if (LangOpts.hasWasmExceptions())
    Options.ExceptionModel = llvm::ExceptionHandling::Wasm;

  Options.NoInfsFPMath = LangOpts.NoHonorInfs;
  Options.NoNaNsFPMath = LangOpts.NoHonorNaNs;
  Options.NoZerosInBSS = CodeGenOpts.NoZeroInitializedInBSS;
  Options.UnsafeFPMath = LangOpts.AllowFPReassoc && LangOpts.AllowRecip &&
                         LangOpts.NoSignedZero && LangOpts.ApproxFunc &&
                         (LangOpts.getDefaultFPContractMode() ==
                              LangOptions::FPModeKind::FPM_Fast ||
                          LangOpts.getDefaultFPContractMode() ==
                              LangOptions::FPModeKind::FPM_FastHonorPragmas);
  Options.ApproxFuncFPMath = LangOpts.ApproxFunc;

  Options.BBAddrMap = CodeGenOpts.BBAddrMap;
  Options.BBSections =
      llvm::StringSwitch<llvm::BasicBlockSection>(CodeGenOpts.BBSections)
          .Case("all", llvm::BasicBlockSection::All)
          .Case("labels", llvm::BasicBlockSection::Labels)
          .StartsWith("list=", llvm::BasicBlockSection::List)
          .Case("none", llvm::BasicBlockSection::None)
          .Default(llvm::BasicBlockSection::None);

  if (Options.BBSections == llvm::BasicBlockSection::List) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> MBOrErr =
        MemoryBuffer::getFile(CodeGenOpts.BBSections.substr(5));
    if (!MBOrErr) {
      Diags.Report(diag::err_fe_unable_to_load_basic_block_sections_file)
          << MBOrErr.getError().message();
      return false;
    }
    Options.BBSectionsFuncListBuf = std::move(*MBOrErr);
  }

  Options.EnableMachineFunctionSplitter = CodeGenOpts.SplitMachineFunctions;
  Options.FunctionSections = CodeGenOpts.FunctionSections;
  Options.DataSections = CodeGenOpts.DataSections;
  Options.IgnoreXCOFFVisibility = LangOpts.IgnoreXCOFFVisibility;
  Options.UniqueSectionNames = CodeGenOpts.UniqueSectionNames;
  Options.UniqueBasicBlockSectionNames =
      CodeGenOpts.UniqueBasicBlockSectionNames;
  Options.SeparateNamedSections = CodeGenOpts.SeparateNamedSections;
  Options.TLSSize = CodeGenOpts.TLSSize;
  Options.EnableTLSDESC = CodeGenOpts.EnableTLSDESC;
  Options.EmulatedTLS = CodeGenOpts.EmulatedTLS;
  Options.DebuggerTuning = CodeGenOpts.getDebuggerTuning();
  Options.EmitStackSizeSection = CodeGenOpts.StackSizeSection;
  Options.StackUsageOutput = CodeGenOpts.StackUsageOutput;
  Options.EmitAddrsig = CodeGenOpts.Addrsig;
  Options.ForceDwarfFrameSection = CodeGenOpts.ForceDwarfFrameSection;
  Options.EmitCallSiteInfo = CodeGenOpts.EmitCallSiteInfo;
  Options.EnableAIXExtendedAltivecABI = LangOpts.EnableAIXExtendedAltivecABI;
  Options.XRayFunctionIndex = CodeGenOpts.XRayFunctionIndex;
  Options.LoopAlignment = CodeGenOpts.LoopAlignment;
  Options.DebugStrictDwarf = CodeGenOpts.DebugStrictDwarf;
  Options.ObjectFilenameForDebug = CodeGenOpts.ObjectFilenameForDebug;
  Options.Hotpatch = CodeGenOpts.HotPatch;
  Options.JMCInstrument = CodeGenOpts.JMCInstrument;
  Options.XCOFFReadOnlyPointers = CodeGenOpts.XCOFFReadOnlyPointers;

  switch (CodeGenOpts.getSwiftAsyncFramePointer()) {
  case CodeGenOptions::SwiftAsyncFramePointerKind::Auto:
    Options.SwiftAsyncFramePointer =
        SwiftAsyncFramePointerMode::DeploymentBased;
    break;

  case CodeGenOptions::SwiftAsyncFramePointerKind::Always:
    Options.SwiftAsyncFramePointer = SwiftAsyncFramePointerMode::Always;
    break;

  case CodeGenOptions::SwiftAsyncFramePointerKind::Never:
    Options.SwiftAsyncFramePointer = SwiftAsyncFramePointerMode::Never;
    break;
  }

  Options.MCOptions.SplitDwarfFile = CodeGenOpts.SplitDwarfFile;
  Options.MCOptions.EmitDwarfUnwind = CodeGenOpts.getEmitDwarfUnwind();
  Options.MCOptions.EmitCompactUnwindNonCanonical =
      CodeGenOpts.EmitCompactUnwindNonCanonical;
  Options.MCOptions.MCRelaxAll = CodeGenOpts.RelaxAll;
  Options.MCOptions.MCSaveTempLabels = CodeGenOpts.SaveTempLabels;
  Options.MCOptions.MCUseDwarfDirectory =
      CodeGenOpts.NoDwarfDirectoryAsm
          ? llvm::MCTargetOptions::DisableDwarfDirectory
          : llvm::MCTargetOptions::EnableDwarfDirectory;
  Options.MCOptions.MCNoExecStack = CodeGenOpts.NoExecStack;
  Options.MCOptions.MCIncrementalLinkerCompatible =
      CodeGenOpts.IncrementalLinkerCompatible;
  Options.MCOptions.MCFatalWarnings = CodeGenOpts.FatalWarnings;
  Options.MCOptions.MCNoWarn = CodeGenOpts.NoWarn;
  Options.MCOptions.AsmVerbose = CodeGenOpts.AsmVerbose;
  Options.MCOptions.Dwarf64 = CodeGenOpts.Dwarf64;
  Options.MCOptions.PreserveAsmComments = CodeGenOpts.PreserveAsmComments;
  Options.MCOptions.Crel = CodeGenOpts.Crel;
  Options.MCOptions.X86RelaxRelocations = CodeGenOpts.RelaxELFRelocations;
  Options.MCOptions.CompressDebugSections =
      CodeGenOpts.getCompressDebugSections();
  Options.MCOptions.ABIName = TargetOpts.ABI;
  for (const auto &Entry : HSOpts.UserEntries)
    if (!Entry.IsFramework &&
        (Entry.Group == frontend::IncludeDirGroup::Quoted ||
         Entry.Group == frontend::IncludeDirGroup::Angled ||
         Entry.Group == frontend::IncludeDirGroup::System))
      Options.MCOptions.IASSearchPaths.push_back(
          Entry.IgnoreSysRoot ? Entry.Path : HSOpts.Sysroot + Entry.Path);
  Options.MCOptions.Argv0 = CodeGenOpts.Argv0;
  Options.MCOptions.CommandLineArgs = CodeGenOpts.CommandLineArgs;
  Options.MCOptions.AsSecureLogFile = CodeGenOpts.AsSecureLogFile;
  Options.MCOptions.PPCUseFullRegisterNames =
      CodeGenOpts.PPCUseFullRegisterNames;
  Options.MisExpect = CodeGenOpts.MisExpect;

  return true;
}

static std::optional<GCOVOptions>
getGCOVOptions(const CodeGenOptions &CodeGenOpts, const LangOptions &LangOpts) {
  if (CodeGenOpts.CoverageNotesFile.empty() &&
      CodeGenOpts.CoverageDataFile.empty())
    return std::nullopt;
  // Not using 'GCOVOptions::getDefault' allows us to avoid exiting if
  // LLVM's -default-gcov-version flag is set to something invalid.
  GCOVOptions Options;
  Options.EmitNotes = !CodeGenOpts.CoverageNotesFile.empty();
  Options.EmitData = !CodeGenOpts.CoverageDataFile.empty();
  llvm::copy(CodeGenOpts.CoverageVersion, std::begin(Options.Version));
  Options.NoRedZone = CodeGenOpts.DisableRedZone;
  Options.Filter = CodeGenOpts.ProfileFilterFiles;
  Options.Exclude = CodeGenOpts.ProfileExcludeFiles;
  Options.Atomic = CodeGenOpts.AtomicProfileUpdate;
  return Options;
}

static std::optional<InstrProfOptions>
getInstrProfOptions(const CodeGenOptions &CodeGenOpts,
                    const LangOptions &LangOpts) {
  if (!CodeGenOpts.hasProfileClangInstr())
    return std::nullopt;
  InstrProfOptions Options;
  Options.NoRedZone = CodeGenOpts.DisableRedZone;
  Options.InstrProfileOutput = CodeGenOpts.InstrProfileOutput;
  Options.Atomic = CodeGenOpts.AtomicProfileUpdate;
  return Options;
}

static void setCommandLineOpts(const CodeGenOptions &CodeGenOpts) {
  SmallVector<const char *, 16> BackendArgs;
  BackendArgs.push_back("clang"); // Fake program name.
  if (!CodeGenOpts.DebugPass.empty()) {
    BackendArgs.push_back("-debug-pass");
    BackendArgs.push_back(CodeGenOpts.DebugPass.c_str());
  }
  if (!CodeGenOpts.LimitFloatPrecision.empty()) {
    BackendArgs.push_back("-limit-float-precision");
    BackendArgs.push_back(CodeGenOpts.LimitFloatPrecision.c_str());
  }
  // Check for the default "clang" invocation that won't set any cl::opt values.
  // Skip trying to parse the command line invocation to avoid the issues
  // described below.
  if (BackendArgs.size() == 1)
    return;
  BackendArgs.push_back(nullptr);
  // FIXME: The command line parser below is not thread-safe and shares a global
  // state, so this call might crash or overwrite the options of another Clang
  // instance in the same process.
  llvm::cl::ParseCommandLineOptions(BackendArgs.size() - 1,
                                    BackendArgs.data());
}

void EmitAssemblyHelper::CreateTargetMachine(bool MustCreateTM) {
  // Create the TargetMachine for generating code.
  std::string Error;
  std::string Triple = TheModule->getTargetTriple();
  const llvm::Target *TheTarget = TargetRegistry::lookupTarget(Triple, Error);
  if (!TheTarget) {
    if (MustCreateTM)
      Diags.Report(diag::err_fe_unable_to_create_target) << Error;
    return;
  }

  std::optional<llvm::CodeModel::Model> CM = getCodeModel(CodeGenOpts);
  std::string FeaturesStr =
      llvm::join(TargetOpts.Features.begin(), TargetOpts.Features.end(), ",");
  llvm::Reloc::Model RM = CodeGenOpts.RelocationModel;
  std::optional<CodeGenOptLevel> OptLevelOrNone =
      CodeGenOpt::getLevel(CodeGenOpts.OptimizationLevel);
  assert(OptLevelOrNone && "Invalid optimization level!");
  CodeGenOptLevel OptLevel = *OptLevelOrNone;

  llvm::TargetOptions Options;
  if (!initTargetOptions(Diags, Options, CodeGenOpts, TargetOpts, LangOpts,
                         HSOpts))
    return;
  TM.reset(TheTarget->createTargetMachine(Triple, TargetOpts.CPU, FeaturesStr,
                                          Options, RM, CM, OptLevel));
  TM->setLargeDataThreshold(CodeGenOpts.LargeDataThreshold);
}

bool EmitAssemblyHelper::AddEmitPasses(legacy::PassManager &CodeGenPasses,
                                       BackendAction Action,
                                       raw_pwrite_stream &OS,
                                       raw_pwrite_stream *DwoOS) {
  // Add LibraryInfo.
  std::unique_ptr<TargetLibraryInfoImpl> TLII(
      llvm::driver::createTLII(TargetTriple, CodeGenOpts.getVecLib()));
  CodeGenPasses.add(new TargetLibraryInfoWrapperPass(*TLII));

  // Normal mode, emit a .s or .o file by running the code generator. Note,
  // this also adds codegenerator level optimization passes.
  CodeGenFileType CGFT = getCodeGenFileType(Action);

  // Add ObjC ARC final-cleanup optimizations. This is done as part of the
  // "codegen" passes so that it isn't run multiple times when there is
  // inlining happening.
  if (CodeGenOpts.OptimizationLevel > 0)
    CodeGenPasses.add(createObjCARCContractPass());

  if (TM->addPassesToEmitFile(CodeGenPasses, OS, DwoOS, CGFT,
                              /*DisableVerify=*/!CodeGenOpts.VerifyModule)) {
    Diags.Report(diag::err_fe_unable_to_interface_with_target);
    return false;
  }

  return true;
}

static OptimizationLevel mapToLevel(const CodeGenOptions &Opts) {
  switch (Opts.OptimizationLevel) {
  default:
    llvm_unreachable("Invalid optimization level!");

  case 0:
    return OptimizationLevel::O0;

  case 1:
    return OptimizationLevel::O1;

  case 2:
    switch (Opts.OptimizeSize) {
    default:
      llvm_unreachable("Invalid optimization level for size!");

    case 0:
      return OptimizationLevel::O2;

    case 1:
      return OptimizationLevel::Os;

    case 2:
      return OptimizationLevel::Oz;
    }

  case 3:
    return OptimizationLevel::O3;
  }
}

static void addKCFIPass(const Triple &TargetTriple, const LangOptions &LangOpts,
                        PassBuilder &PB) {
  // If the back-end supports KCFI operand bundle lowering, skip KCFIPass.
  if (TargetTriple.getArch() == llvm::Triple::x86_64 ||
      TargetTriple.isAArch64(64) || TargetTriple.isRISCV())
    return;

  // Ensure we lower KCFI operand bundles with -O0.
  PB.registerOptimizerLastEPCallback(
      [&](ModulePassManager &MPM, OptimizationLevel Level) {
        if (Level == OptimizationLevel::O0 &&
            LangOpts.Sanitize.has(SanitizerKind::KCFI))
          MPM.addPass(createModuleToFunctionPassAdaptor(KCFIPass()));
      });

  // When optimizations are requested, run KCIFPass after InstCombine to
  // avoid unnecessary checks.
  PB.registerPeepholeEPCallback(
      [&](FunctionPassManager &FPM, OptimizationLevel Level) {
        if (Level != OptimizationLevel::O0 &&
            LangOpts.Sanitize.has(SanitizerKind::KCFI))
          FPM.addPass(KCFIPass());
      });
}

static void addSanitizers(const Triple &TargetTriple,
                          const CodeGenOptions &CodeGenOpts,
                          const LangOptions &LangOpts, PassBuilder &PB) {
  auto SanitizersCallback = [&](ModulePassManager &MPM,
                                OptimizationLevel Level) {
    if (CodeGenOpts.hasSanitizeCoverage()) {
      auto SancovOpts = getSancovOptsFromCGOpts(CodeGenOpts);
      MPM.addPass(SanitizerCoveragePass(
          SancovOpts, CodeGenOpts.SanitizeCoverageAllowlistFiles,
          CodeGenOpts.SanitizeCoverageIgnorelistFiles));
    }

    if (CodeGenOpts.hasSanitizeBinaryMetadata()) {
      MPM.addPass(SanitizerBinaryMetadataPass(
          getSanitizerBinaryMetadataOptions(CodeGenOpts),
          CodeGenOpts.SanitizeMetadataIgnorelistFiles));
    }

    auto MSanPass = [&](SanitizerMask Mask, bool CompileKernel) {
      if (LangOpts.Sanitize.has(Mask)) {
        int TrackOrigins = CodeGenOpts.SanitizeMemoryTrackOrigins;
        bool Recover = CodeGenOpts.SanitizeRecover.has(Mask);

        MemorySanitizerOptions options(TrackOrigins, Recover, CompileKernel,
                                       CodeGenOpts.SanitizeMemoryParamRetval);
        MPM.addPass(MemorySanitizerPass(options));
        if (Level != OptimizationLevel::O0) {
          // MemorySanitizer inserts complex instrumentation that mostly follows
          // the logic of the original code, but operates on "shadow" values. It
          // can benefit from re-running some general purpose optimization
          // passes.
          MPM.addPass(RequireAnalysisPass<GlobalsAA, llvm::Module>());
          FunctionPassManager FPM;
          FPM.addPass(EarlyCSEPass(true /* Enable mem-ssa. */));
          FPM.addPass(InstCombinePass());
          FPM.addPass(JumpThreadingPass());
          FPM.addPass(GVNPass());
          FPM.addPass(InstCombinePass());
          MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
        }
      }
    };
    MSanPass(SanitizerKind::Memory, false);
    MSanPass(SanitizerKind::KernelMemory, true);

    if (LangOpts.Sanitize.has(SanitizerKind::Thread)) {
      MPM.addPass(ModuleThreadSanitizerPass());
      MPM.addPass(createModuleToFunctionPassAdaptor(ThreadSanitizerPass()));
    }

    if (LangOpts.Sanitize.has(SanitizerKind::NumericalStability))
      MPM.addPass(NumericalStabilitySanitizerPass());

    auto ASanPass = [&](SanitizerMask Mask, bool CompileKernel) {
      if (LangOpts.Sanitize.has(Mask)) {
        bool UseGlobalGC = asanUseGlobalsGC(TargetTriple, CodeGenOpts);
        bool UseOdrIndicator = CodeGenOpts.SanitizeAddressUseOdrIndicator;
        llvm::AsanDtorKind DestructorKind =
            CodeGenOpts.getSanitizeAddressDtor();
        AddressSanitizerOptions Opts;
        Opts.CompileKernel = CompileKernel;
        Opts.Recover = CodeGenOpts.SanitizeRecover.has(Mask);
        Opts.UseAfterScope = CodeGenOpts.SanitizeAddressUseAfterScope;
        Opts.UseAfterReturn = CodeGenOpts.getSanitizeAddressUseAfterReturn();
        MPM.addPass(AddressSanitizerPass(Opts, UseGlobalGC, UseOdrIndicator,
                                         DestructorKind));
      }
    };
    ASanPass(SanitizerKind::Address, false);
    ASanPass(SanitizerKind::KernelAddress, true);

    auto HWASanPass = [&](SanitizerMask Mask, bool CompileKernel) {
      if (LangOpts.Sanitize.has(Mask)) {
        bool Recover = CodeGenOpts.SanitizeRecover.has(Mask);
        MPM.addPass(HWAddressSanitizerPass(
            {CompileKernel, Recover,
             /*DisableOptimization=*/CodeGenOpts.OptimizationLevel == 0}));
      }
    };
    HWASanPass(SanitizerKind::HWAddress, false);
    HWASanPass(SanitizerKind::KernelHWAddress, true);

    if (LangOpts.Sanitize.has(SanitizerKind::DataFlow)) {
      MPM.addPass(DataFlowSanitizerPass(LangOpts.NoSanitizeFiles));
    }
  };
  if (ClSanitizeOnOptimizerEarlyEP) {
    PB.registerOptimizerEarlyEPCallback(
        [SanitizersCallback](ModulePassManager &MPM, OptimizationLevel Level) {
          ModulePassManager NewMPM;
          SanitizersCallback(NewMPM, Level);
          if (!NewMPM.isEmpty()) {
            // Sanitizers can abandon<GlobalsAA>.
            NewMPM.addPass(RequireAnalysisPass<GlobalsAA, llvm::Module>());
            MPM.addPass(std::move(NewMPM));
          }
        });
  } else {
    // LastEP does not need GlobalsAA.
    PB.registerOptimizerLastEPCallback(SanitizersCallback);
  }

  if (LowerAllowCheckPass::IsRequested()) {
    // We can optimize after inliner, and PGO profile matching. The hook below
    // is called at the end `buildFunctionSimplificationPipeline`, which called
    // from `buildInlinerPipeline`, which called after profile matching.
    PB.registerScalarOptimizerLateEPCallback(
        [](FunctionPassManager &FPM, OptimizationLevel Level) {
          FPM.addPass(LowerAllowCheckPass());
        });
  }
}

void EmitAssemblyHelper::RunOptimizationPipeline(
    BackendAction Action, std::unique_ptr<raw_pwrite_stream> &OS,
    std::unique_ptr<llvm::ToolOutputFile> &ThinLinkOS, BackendConsumer *BC) {
  std::optional<PGOOptions> PGOOpt;

  if (CodeGenOpts.hasProfileIRInstr())
    // -fprofile-generate.
    PGOOpt = PGOOptions(
        CodeGenOpts.InstrProfileOutput.empty() ? getDefaultProfileGenName()
                                               : CodeGenOpts.InstrProfileOutput,
        "", "", CodeGenOpts.MemoryProfileUsePath, nullptr, PGOOptions::IRInstr,
        PGOOptions::NoCSAction, ClPGOColdFuncAttr,
        CodeGenOpts.DebugInfoForProfiling,
        /*PseudoProbeForProfiling=*/false, CodeGenOpts.AtomicProfileUpdate);
  else if (CodeGenOpts.hasProfileIRUse()) {
    // -fprofile-use.
    auto CSAction = CodeGenOpts.hasProfileCSIRUse() ? PGOOptions::CSIRUse
                                                    : PGOOptions::NoCSAction;
    PGOOpt = PGOOptions(CodeGenOpts.ProfileInstrumentUsePath, "",
                        CodeGenOpts.ProfileRemappingFile,
                        CodeGenOpts.MemoryProfileUsePath, VFS,
                        PGOOptions::IRUse, CSAction, ClPGOColdFuncAttr,
                        CodeGenOpts.DebugInfoForProfiling);
  } else if (!CodeGenOpts.SampleProfileFile.empty())
    // -fprofile-sample-use
    PGOOpt = PGOOptions(
        CodeGenOpts.SampleProfileFile, "", CodeGenOpts.ProfileRemappingFile,
        CodeGenOpts.MemoryProfileUsePath, VFS, PGOOptions::SampleUse,
        PGOOptions::NoCSAction, ClPGOColdFuncAttr,
        CodeGenOpts.DebugInfoForProfiling, CodeGenOpts.PseudoProbeForProfiling);
  else if (!CodeGenOpts.MemoryProfileUsePath.empty())
    // -fmemory-profile-use (without any of the above options)
    PGOOpt = PGOOptions("", "", "", CodeGenOpts.MemoryProfileUsePath, VFS,
                        PGOOptions::NoAction, PGOOptions::NoCSAction,
                        ClPGOColdFuncAttr, CodeGenOpts.DebugInfoForProfiling);
  else if (CodeGenOpts.PseudoProbeForProfiling)
    // -fpseudo-probe-for-profiling
    PGOOpt =
        PGOOptions("", "", "", /*MemoryProfile=*/"", nullptr,
                   PGOOptions::NoAction, PGOOptions::NoCSAction,
                   ClPGOColdFuncAttr, CodeGenOpts.DebugInfoForProfiling, true);
  else if (CodeGenOpts.DebugInfoForProfiling)
    // -fdebug-info-for-profiling
    PGOOpt = PGOOptions("", "", "", /*MemoryProfile=*/"", nullptr,
                        PGOOptions::NoAction, PGOOptions::NoCSAction,
                        ClPGOColdFuncAttr, true);

  // Check to see if we want to generate a CS profile.
  if (CodeGenOpts.hasProfileCSIRInstr()) {
    assert(!CodeGenOpts.hasProfileCSIRUse() &&
           "Cannot have both CSProfileUse pass and CSProfileGen pass at "
           "the same time");
    if (PGOOpt) {
      assert(PGOOpt->Action != PGOOptions::IRInstr &&
             PGOOpt->Action != PGOOptions::SampleUse &&
             "Cannot run CSProfileGen pass with ProfileGen or SampleUse "
             " pass");
      PGOOpt->CSProfileGenFile = CodeGenOpts.InstrProfileOutput.empty()
                                     ? getDefaultProfileGenName()
                                     : CodeGenOpts.InstrProfileOutput;
      PGOOpt->CSAction = PGOOptions::CSIRInstr;
    } else
      PGOOpt = PGOOptions("",
                          CodeGenOpts.InstrProfileOutput.empty()
                              ? getDefaultProfileGenName()
                              : CodeGenOpts.InstrProfileOutput,
                          "", /*MemoryProfile=*/"", nullptr,
                          PGOOptions::NoAction, PGOOptions::CSIRInstr,
                          ClPGOColdFuncAttr, CodeGenOpts.DebugInfoForProfiling);
  }
  if (TM)
    TM->setPGOOption(PGOOpt);

  PipelineTuningOptions PTO;
  PTO.LoopUnrolling = CodeGenOpts.UnrollLoops;
  // For historical reasons, loop interleaving is set to mirror setting for loop
  // unrolling.
  PTO.LoopInterleaving = CodeGenOpts.UnrollLoops;
  PTO.LoopVectorization = CodeGenOpts.VectorizeLoop;
  PTO.SLPVectorization = CodeGenOpts.VectorizeSLP;
  PTO.MergeFunctions = CodeGenOpts.MergeFunctions;
  // Only enable CGProfilePass when using integrated assembler, since
  // non-integrated assemblers don't recognize .cgprofile section.
  PTO.CallGraphProfile = !CodeGenOpts.DisableIntegratedAS;
  PTO.UnifiedLTO = CodeGenOpts.UnifiedLTO;

  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;

  bool DebugPassStructure = CodeGenOpts.DebugPass == "Structure";
  PassInstrumentationCallbacks PIC;
  PrintPassOptions PrintPassOpts;
  PrintPassOpts.Indent = DebugPassStructure;
  PrintPassOpts.SkipAnalyses = DebugPassStructure;
  StandardInstrumentations SI(
      TheModule->getContext(),
      (CodeGenOpts.DebugPassManager || DebugPassStructure),
      CodeGenOpts.VerifyEach, PrintPassOpts);
  SI.registerCallbacks(PIC, &MAM);
  PassBuilder PB(TM.get(), PTO, PGOOpt, &PIC);

  // Handle the assignment tracking feature options.
  switch (CodeGenOpts.getAssignmentTrackingMode()) {
  case CodeGenOptions::AssignmentTrackingOpts::Forced:
    PB.registerPipelineStartEPCallback(
        [&](ModulePassManager &MPM, OptimizationLevel Level) {
          MPM.addPass(AssignmentTrackingPass());
        });
    break;
  case CodeGenOptions::AssignmentTrackingOpts::Enabled:
    // Disable assignment tracking in LTO builds for now as the performance
    // cost is too high. Disable for LLDB tuning due to llvm.org/PR43126.
    if (!CodeGenOpts.PrepareForThinLTO && !CodeGenOpts.PrepareForLTO &&
        CodeGenOpts.getDebuggerTuning() != llvm::DebuggerKind::LLDB) {
      PB.registerPipelineStartEPCallback(
          [&](ModulePassManager &MPM, OptimizationLevel Level) {
            // Only use assignment tracking if optimisations are enabled.
            if (Level != OptimizationLevel::O0)
              MPM.addPass(AssignmentTrackingPass());
          });
    }
    break;
  case CodeGenOptions::AssignmentTrackingOpts::Disabled:
    break;
  }

  // Enable verify-debuginfo-preserve-each for new PM.
  DebugifyEachInstrumentation Debugify;
  DebugInfoPerPass DebugInfoBeforePass;
  if (CodeGenOpts.EnableDIPreservationVerify) {
    Debugify.setDebugifyMode(DebugifyMode::OriginalDebugInfo);
    Debugify.setDebugInfoBeforePass(DebugInfoBeforePass);

    if (!CodeGenOpts.DIBugsReportFilePath.empty())
      Debugify.setOrigDIVerifyBugsReportFilePath(
          CodeGenOpts.DIBugsReportFilePath);
    Debugify.registerCallbacks(PIC, MAM);
  }
  // Attempt to load pass plugins and register their callbacks with PB.
  for (auto &PluginFN : CodeGenOpts.PassPlugins) {
    auto PassPlugin = PassPlugin::Load(PluginFN);
    if (PassPlugin) {
      PassPlugin->registerPassBuilderCallbacks(PB);
    } else {
      Diags.Report(diag::err_fe_unable_to_load_plugin)
          << PluginFN << toString(PassPlugin.takeError());
    }
  }
  for (const auto &PassCallback : CodeGenOpts.PassBuilderCallbacks)
    PassCallback(PB);
#define HANDLE_EXTENSION(Ext)                                                  \
  get##Ext##PluginInfo().RegisterPassBuilderCallbacks(PB);
#include "llvm/Support/Extension.def"

  // Register the target library analysis directly and give it a customized
  // preset TLI.
  std::unique_ptr<TargetLibraryInfoImpl> TLII(
      llvm::driver::createTLII(TargetTriple, CodeGenOpts.getVecLib()));
  FAM.registerPass([&] { return TargetLibraryAnalysis(*TLII); });

  // Register all the basic analyses with the managers.
  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  ModulePassManager MPM;
  // Add a verifier pass, before any other passes, to catch CodeGen issues.
  if (CodeGenOpts.VerifyModule)
    MPM.addPass(VerifierPass());

  if (!CodeGenOpts.DisableLLVMPasses) {
    // Map our optimization levels into one of the distinct levels used to
    // configure the pipeline.
    OptimizationLevel Level = mapToLevel(CodeGenOpts);

    const bool PrepareForThinLTO = CodeGenOpts.PrepareForThinLTO;
    const bool PrepareForLTO = CodeGenOpts.PrepareForLTO;

    if (LangOpts.ObjCAutoRefCount) {
      PB.registerPipelineStartEPCallback(
          [](ModulePassManager &MPM, OptimizationLevel Level) {
            if (Level != OptimizationLevel::O0)
              MPM.addPass(
                  createModuleToFunctionPassAdaptor(ObjCARCExpandPass()));
          });
      PB.registerPipelineEarlySimplificationEPCallback(
          [](ModulePassManager &MPM, OptimizationLevel Level) {
            if (Level != OptimizationLevel::O0)
              MPM.addPass(ObjCARCAPElimPass());
          });
      PB.registerScalarOptimizerLateEPCallback(
          [](FunctionPassManager &FPM, OptimizationLevel Level) {
            if (Level != OptimizationLevel::O0)
              FPM.addPass(ObjCARCOptPass());
          });
    }

    // If we reached here with a non-empty index file name, then the index
    // file was empty and we are not performing ThinLTO backend compilation
    // (used in testing in a distributed build environment).
    bool IsThinLTOPostLink = !CodeGenOpts.ThinLTOIndexFile.empty();
    // If so drop any the type test assume sequences inserted for whole program
    // vtables so that codegen doesn't complain.
    if (IsThinLTOPostLink)
      PB.registerPipelineStartEPCallback(
          [](ModulePassManager &MPM, OptimizationLevel Level) {
            MPM.addPass(LowerTypeTestsPass(/*ExportSummary=*/nullptr,
                                           /*ImportSummary=*/nullptr,
                                           /*DropTypeTests=*/true));
          });

    // Register callbacks to schedule sanitizer passes at the appropriate part
    // of the pipeline.
    if (LangOpts.Sanitize.has(SanitizerKind::LocalBounds))
      PB.registerScalarOptimizerLateEPCallback(
          [](FunctionPassManager &FPM, OptimizationLevel Level) {
            FPM.addPass(BoundsCheckingPass());
          });

    // Don't add sanitizers if we are here from ThinLTO PostLink. That already
    // done on PreLink stage.
    if (!IsThinLTOPostLink) {
      addSanitizers(TargetTriple, CodeGenOpts, LangOpts, PB);
      addKCFIPass(TargetTriple, LangOpts, PB);
    }

    if (std::optional<GCOVOptions> Options =
            getGCOVOptions(CodeGenOpts, LangOpts))
      PB.registerPipelineStartEPCallback(
          [Options](ModulePassManager &MPM, OptimizationLevel Level) {
            MPM.addPass(GCOVProfilerPass(*Options));
          });
    if (std::optional<InstrProfOptions> Options =
            getInstrProfOptions(CodeGenOpts, LangOpts))
      PB.registerPipelineStartEPCallback(
          [Options](ModulePassManager &MPM, OptimizationLevel Level) {
            MPM.addPass(InstrProfilingLoweringPass(*Options, false));
          });

    // TODO: Consider passing the MemoryProfileOutput to the pass builder via
    // the PGOOptions, and set this up there.
    if (!CodeGenOpts.MemoryProfileOutput.empty()) {
      PB.registerOptimizerLastEPCallback(
          [](ModulePassManager &MPM, OptimizationLevel Level) {
            MPM.addPass(createModuleToFunctionPassAdaptor(MemProfilerPass()));
            MPM.addPass(ModuleMemProfilerPass());
          });
    }

    if (CodeGenOpts.FatLTO) {
      MPM.addPass(PB.buildFatLTODefaultPipeline(
          Level, PrepareForThinLTO,
          PrepareForThinLTO || shouldEmitRegularLTOSummary()));
    } else if (PrepareForThinLTO) {
      MPM.addPass(PB.buildThinLTOPreLinkDefaultPipeline(Level));
    } else if (PrepareForLTO) {
      MPM.addPass(PB.buildLTOPreLinkDefaultPipeline(Level));
    } else {
      MPM.addPass(PB.buildPerModuleDefaultPipeline(Level));
    }
  }

  // Link against bitcodes supplied via the -mlink-builtin-bitcode option
  if (CodeGenOpts.LinkBitcodePostopt)
    MPM.addPass(LinkInModulesPass(BC));

  // Add a verifier pass if requested. We don't have to do this if the action
  // requires code generation because there will already be a verifier pass in
  // the code-generation pipeline.
  // Since we already added a verifier pass above, this
  // might even not run the analysis, if previous passes caused no changes.
  if (!actionRequiresCodeGen(Action) && CodeGenOpts.VerifyModule)
    MPM.addPass(VerifierPass());

  if (Action == Backend_EmitBC || Action == Backend_EmitLL ||
      CodeGenOpts.FatLTO) {
    if (CodeGenOpts.PrepareForThinLTO && !CodeGenOpts.DisableLLVMPasses) {
      if (!TheModule->getModuleFlag("EnableSplitLTOUnit"))
        TheModule->addModuleFlag(llvm::Module::Error, "EnableSplitLTOUnit",
                                 CodeGenOpts.EnableSplitLTOUnit);
      if (Action == Backend_EmitBC) {
        if (!CodeGenOpts.ThinLinkBitcodeFile.empty()) {
          ThinLinkOS = openOutputFile(CodeGenOpts.ThinLinkBitcodeFile);
          if (!ThinLinkOS)
            return;
        }
        MPM.addPass(ThinLTOBitcodeWriterPass(
            *OS, ThinLinkOS ? &ThinLinkOS->os() : nullptr));
      } else if (Action == Backend_EmitLL) {
        MPM.addPass(PrintModulePass(*OS, "", CodeGenOpts.EmitLLVMUseLists,
                                    /*EmitLTOSummary=*/true));
      }
    } else {
      // Emit a module summary by default for Regular LTO except for ld64
      // targets
      bool EmitLTOSummary = shouldEmitRegularLTOSummary();
      if (EmitLTOSummary) {
        if (!TheModule->getModuleFlag("ThinLTO") && !CodeGenOpts.UnifiedLTO)
          TheModule->addModuleFlag(llvm::Module::Error, "ThinLTO", uint32_t(0));
        if (!TheModule->getModuleFlag("EnableSplitLTOUnit"))
          TheModule->addModuleFlag(llvm::Module::Error, "EnableSplitLTOUnit",
                                   uint32_t(1));
      }
      if (Action == Backend_EmitBC) {
        MPM.addPass(BitcodeWriterPass(*OS, CodeGenOpts.EmitLLVMUseLists,
                                      EmitLTOSummary));
      } else if (Action == Backend_EmitLL) {
        MPM.addPass(PrintModulePass(*OS, "", CodeGenOpts.EmitLLVMUseLists,
                                    EmitLTOSummary));
      }
    }

    if (shouldEmitUnifiedLTOModueFlag())
      TheModule->addModuleFlag(llvm::Module::Error, "UnifiedLTO", uint32_t(1));
  }

  // Print a textual, '-passes=' compatible, representation of pipeline if
  // requested.
  if (PrintPipelinePasses) {
    MPM.printPipeline(outs(), [&PIC](StringRef ClassName) {
      auto PassName = PIC.getPassNameForClassName(ClassName);
      return PassName.empty() ? ClassName : PassName;
    });
    outs() << "\n";
    return;
  }

  if (LangOpts.HIPStdPar && !LangOpts.CUDAIsDevice &&
      LangOpts.HIPStdParInterposeAlloc)
    MPM.addPass(HipStdParAllocationInterpositionPass());

  // Now that we have all of the passes ready, run them.
  {
    PrettyStackTraceString CrashInfo("Optimizer");
    llvm::TimeTraceScope TimeScope("Optimizer");
    MPM.run(*TheModule, MAM);
  }
}

void EmitAssemblyHelper::RunCodegenPipeline(
    BackendAction Action, std::unique_ptr<raw_pwrite_stream> &OS,
    std::unique_ptr<llvm::ToolOutputFile> &DwoOS) {
  // We still use the legacy PM to run the codegen pipeline since the new PM
  // does not work with the codegen pipeline.
  // FIXME: make the new PM work with the codegen pipeline.
  legacy::PassManager CodeGenPasses;

  // Append any output we need to the pass manager.
  switch (Action) {
  case Backend_EmitAssembly:
  case Backend_EmitMCNull:
  case Backend_EmitObj:
    CodeGenPasses.add(
        createTargetTransformInfoWrapperPass(getTargetIRAnalysis()));
    if (!CodeGenOpts.SplitDwarfOutput.empty()) {
      DwoOS = openOutputFile(CodeGenOpts.SplitDwarfOutput);
      if (!DwoOS)
        return;
    }
    if (!AddEmitPasses(CodeGenPasses, Action, *OS,
                       DwoOS ? &DwoOS->os() : nullptr))
      // FIXME: Should we handle this error differently?
      return;
    break;
  default:
    return;
  }

  // If -print-pipeline-passes is requested, don't run the legacy pass manager.
  // FIXME: when codegen is switched to use the new pass manager, it should also
  // emit pass names here.
  if (PrintPipelinePasses) {
    return;
  }

  {
    PrettyStackTraceString CrashInfo("Code generation");
    llvm::TimeTraceScope TimeScope("CodeGenPasses");
    CodeGenPasses.run(*TheModule);
  }
}

void EmitAssemblyHelper::EmitAssembly(BackendAction Action,
                                      std::unique_ptr<raw_pwrite_stream> OS,
                                      BackendConsumer *BC) {
  TimeRegion Region(CodeGenOpts.TimePasses ? &CodeGenerationTime : nullptr);
  setCommandLineOpts(CodeGenOpts);

  bool RequiresCodeGen = actionRequiresCodeGen(Action);
  CreateTargetMachine(RequiresCodeGen);

  if (RequiresCodeGen && !TM)
    return;
  if (TM)
    TheModule->setDataLayout(TM->createDataLayout());

  // Before executing passes, print the final values of the LLVM options.
  cl::PrintOptionValues();

  std::unique_ptr<llvm::ToolOutputFile> ThinLinkOS, DwoOS;
  RunOptimizationPipeline(Action, OS, ThinLinkOS, BC);
  RunCodegenPipeline(Action, OS, DwoOS);

  if (ThinLinkOS)
    ThinLinkOS->keep();
  if (DwoOS)
    DwoOS->keep();
}

static void runThinLTOBackend(
    DiagnosticsEngine &Diags, ModuleSummaryIndex *CombinedIndex,
    llvm::Module *M, const HeaderSearchOptions &HeaderOpts,
    const CodeGenOptions &CGOpts, const clang::TargetOptions &TOpts,
    const LangOptions &LOpts, std::unique_ptr<raw_pwrite_stream> OS,
    std::string SampleProfile, std::string ProfileRemapping,
    BackendAction Action) {
  DenseMap<StringRef, DenseMap<GlobalValue::GUID, GlobalValueSummary *>>
      ModuleToDefinedGVSummaries;
  CombinedIndex->collectDefinedGVSummariesPerModule(ModuleToDefinedGVSummaries);

  setCommandLineOpts(CGOpts);

  // We can simply import the values mentioned in the combined index, since
  // we should only invoke this using the individual indexes written out
  // via a WriteIndexesThinBackend.
  FunctionImporter::ImportMapTy ImportList;
  if (!lto::initImportList(*M, *CombinedIndex, ImportList))
    return;

  auto AddStream = [&](size_t Task, const Twine &ModuleName) {
    return std::make_unique<CachedFileStream>(std::move(OS),
                                              CGOpts.ObjectFilenameForDebug);
  };
  lto::Config Conf;
  if (CGOpts.SaveTempsFilePrefix != "") {
    if (Error E = Conf.addSaveTemps(CGOpts.SaveTempsFilePrefix + ".",
                                    /* UseInputModulePath */ false)) {
      handleAllErrors(std::move(E), [&](ErrorInfoBase &EIB) {
        errs() << "Error setting up ThinLTO save-temps: " << EIB.message()
               << '\n';
      });
    }
  }
  Conf.CPU = TOpts.CPU;
  Conf.CodeModel = getCodeModel(CGOpts);
  Conf.MAttrs = TOpts.Features;
  Conf.RelocModel = CGOpts.RelocationModel;
  std::optional<CodeGenOptLevel> OptLevelOrNone =
      CodeGenOpt::getLevel(CGOpts.OptimizationLevel);
  assert(OptLevelOrNone && "Invalid optimization level!");
  Conf.CGOptLevel = *OptLevelOrNone;
  Conf.OptLevel = CGOpts.OptimizationLevel;
  initTargetOptions(Diags, Conf.Options, CGOpts, TOpts, LOpts, HeaderOpts);
  Conf.SampleProfile = std::move(SampleProfile);
  Conf.PTO.LoopUnrolling = CGOpts.UnrollLoops;
  // For historical reasons, loop interleaving is set to mirror setting for loop
  // unrolling.
  Conf.PTO.LoopInterleaving = CGOpts.UnrollLoops;
  Conf.PTO.LoopVectorization = CGOpts.VectorizeLoop;
  Conf.PTO.SLPVectorization = CGOpts.VectorizeSLP;
  // Only enable CGProfilePass when using integrated assembler, since
  // non-integrated assemblers don't recognize .cgprofile section.
  Conf.PTO.CallGraphProfile = !CGOpts.DisableIntegratedAS;

  // Context sensitive profile.
  if (CGOpts.hasProfileCSIRInstr()) {
    Conf.RunCSIRInstr = true;
    Conf.CSIRProfile = std::move(CGOpts.InstrProfileOutput);
  } else if (CGOpts.hasProfileCSIRUse()) {
    Conf.RunCSIRInstr = false;
    Conf.CSIRProfile = std::move(CGOpts.ProfileInstrumentUsePath);
  }

  Conf.ProfileRemapping = std::move(ProfileRemapping);
  Conf.DebugPassManager = CGOpts.DebugPassManager;
  Conf.VerifyEach = CGOpts.VerifyEach;
  Conf.RemarksWithHotness = CGOpts.DiagnosticsWithHotness;
  Conf.RemarksFilename = CGOpts.OptRecordFile;
  Conf.RemarksPasses = CGOpts.OptRecordPasses;
  Conf.RemarksFormat = CGOpts.OptRecordFormat;
  Conf.SplitDwarfFile = CGOpts.SplitDwarfFile;
  Conf.SplitDwarfOutput = CGOpts.SplitDwarfOutput;
  switch (Action) {
  case Backend_EmitNothing:
    Conf.PreCodeGenModuleHook = [](size_t Task, const llvm::Module &Mod) {
      return false;
    };
    break;
  case Backend_EmitLL:
    Conf.PreCodeGenModuleHook = [&](size_t Task, const llvm::Module &Mod) {
      M->print(*OS, nullptr, CGOpts.EmitLLVMUseLists);
      return false;
    };
    break;
  case Backend_EmitBC:
    Conf.PreCodeGenModuleHook = [&](size_t Task, const llvm::Module &Mod) {
      WriteBitcodeToFile(*M, *OS, CGOpts.EmitLLVMUseLists);
      return false;
    };
    break;
  default:
    Conf.CGFileType = getCodeGenFileType(Action);
    break;
  }
  if (Error E =
          thinBackend(Conf, -1, AddStream, *M, *CombinedIndex, ImportList,
                      ModuleToDefinedGVSummaries[M->getModuleIdentifier()],
                      /* ModuleMap */ nullptr, CGOpts.CmdArgs)) {
    handleAllErrors(std::move(E), [&](ErrorInfoBase &EIB) {
      errs() << "Error running ThinLTO backend: " << EIB.message() << '\n';
    });
  }
}

void clang::EmitBackendOutput(
    DiagnosticsEngine &Diags, const HeaderSearchOptions &HeaderOpts,
    const CodeGenOptions &CGOpts, const clang::TargetOptions &TOpts,
    const LangOptions &LOpts, StringRef TDesc, llvm::Module *M,
    BackendAction Action, IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS,
    std::unique_ptr<raw_pwrite_stream> OS, BackendConsumer *BC) {

  llvm::TimeTraceScope TimeScope("Backend");

  std::unique_ptr<llvm::Module> EmptyModule;
  if (!CGOpts.ThinLTOIndexFile.empty()) {
    // If we are performing a ThinLTO importing compile, load the function index
    // into memory and pass it into runThinLTOBackend, which will run the
    // function importer and invoke LTO passes.
    std::unique_ptr<ModuleSummaryIndex> CombinedIndex;
    if (Error E = llvm::getModuleSummaryIndexForFile(
                      CGOpts.ThinLTOIndexFile,
                      /*IgnoreEmptyThinLTOIndexFile*/ true)
                      .moveInto(CombinedIndex)) {
      logAllUnhandledErrors(std::move(E), errs(),
                            "Error loading index file '" +
                            CGOpts.ThinLTOIndexFile + "': ");
      return;
    }

    // A null CombinedIndex means we should skip ThinLTO compilation
    // (LLVM will optionally ignore empty index files, returning null instead
    // of an error).
    if (CombinedIndex) {
      if (!CombinedIndex->skipModuleByDistributedBackend()) {
        runThinLTOBackend(Diags, CombinedIndex.get(), M, HeaderOpts, CGOpts,
                          TOpts, LOpts, std::move(OS), CGOpts.SampleProfileFile,
                          CGOpts.ProfileRemappingFile, Action);
        return;
      }
      // Distributed indexing detected that nothing from the module is needed
      // for the final linking. So we can skip the compilation. We sill need to
      // output an empty object file to make sure that a linker does not fail
      // trying to read it. Also for some features, like CFI, we must skip
      // the compilation as CombinedIndex does not contain all required
      // information.
      EmptyModule = std::make_unique<llvm::Module>("empty", M->getContext());
      EmptyModule->setTargetTriple(M->getTargetTriple());
      M = EmptyModule.get();
    }
  }

  EmitAssemblyHelper AsmHelper(Diags, HeaderOpts, CGOpts, TOpts, LOpts, M, VFS);
  AsmHelper.EmitAssembly(Action, std::move(OS), BC);

  // Verify clang's TargetInfo DataLayout against the LLVM TargetMachine's
  // DataLayout.
  if (AsmHelper.TM) {
    std::string DLDesc = M->getDataLayout().getStringRepresentation();
    if (DLDesc != TDesc) {
      unsigned DiagID = Diags.getCustomDiagID(
          DiagnosticsEngine::Error, "backend data layout '%0' does not match "
                                    "expected target description '%1'");
      Diags.Report(DiagID) << DLDesc << TDesc;
    }
  }
}

// With -fembed-bitcode, save a copy of the llvm IR as data in the
// __LLVM,__bitcode section.
void clang::EmbedBitcode(llvm::Module *M, const CodeGenOptions &CGOpts,
                         llvm::MemoryBufferRef Buf) {
  if (CGOpts.getEmbedBitcode() == CodeGenOptions::Embed_Off)
    return;
  llvm::embedBitcodeInModule(
      *M, Buf, CGOpts.getEmbedBitcode() != CodeGenOptions::Embed_Marker,
      CGOpts.getEmbedBitcode() != CodeGenOptions::Embed_Bitcode,
      CGOpts.CmdArgs);
}

void clang::EmbedObject(llvm::Module *M, const CodeGenOptions &CGOpts,
                        DiagnosticsEngine &Diags) {
  if (CGOpts.OffloadObjects.empty())
    return;

  for (StringRef OffloadObject : CGOpts.OffloadObjects) {
    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> ObjectOrErr =
        llvm::MemoryBuffer::getFileOrSTDIN(OffloadObject);
    if (ObjectOrErr.getError()) {
      auto DiagID = Diags.getCustomDiagID(DiagnosticsEngine::Error,
                                          "could not open '%0' for embedding");
      Diags.Report(DiagID) << OffloadObject;
      return;
    }

    llvm::embedBufferInModule(*M, **ObjectOrErr, ".llvm.offloading",
                              Align(object::OffloadBinary::getAlignment()));
  }
}
