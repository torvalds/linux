//===-- CommandFlags.cpp - Command Line Flags Interface ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains codegen-specific flags that are shared between different
// command line tools. The tools "llc" and "opt" both use this file to prevent
// flag duplication.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCTargetOptionsCommandFlags.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include "llvm/TargetParser/Triple.h"
#include <optional>

using namespace llvm;

#define CGOPT(TY, NAME)                                                        \
  static cl::opt<TY> *NAME##View;                                              \
  TY codegen::get##NAME() {                                                    \
    assert(NAME##View && "RegisterCodeGenFlags not created.");                 \
    return *NAME##View;                                                        \
  }

#define CGLIST(TY, NAME)                                                       \
  static cl::list<TY> *NAME##View;                                             \
  std::vector<TY> codegen::get##NAME() {                                       \
    assert(NAME##View && "RegisterCodeGenFlags not created.");                 \
    return *NAME##View;                                                        \
  }

// Temporary macro for incremental transition to std::optional.
#define CGOPT_EXP(TY, NAME)                                                    \
  CGOPT(TY, NAME)                                                              \
  std::optional<TY> codegen::getExplicit##NAME() {                             \
    if (NAME##View->getNumOccurrences()) {                                     \
      TY res = *NAME##View;                                                    \
      return res;                                                              \
    }                                                                          \
    return std::nullopt;                                                       \
  }

CGOPT(std::string, MArch)
CGOPT(std::string, MCPU)
CGLIST(std::string, MAttrs)
CGOPT_EXP(Reloc::Model, RelocModel)
CGOPT(ThreadModel::Model, ThreadModel)
CGOPT_EXP(CodeModel::Model, CodeModel)
CGOPT_EXP(uint64_t, LargeDataThreshold)
CGOPT(ExceptionHandling, ExceptionModel)
CGOPT_EXP(CodeGenFileType, FileType)
CGOPT(FramePointerKind, FramePointerUsage)
CGOPT(bool, EnableUnsafeFPMath)
CGOPT(bool, EnableNoInfsFPMath)
CGOPT(bool, EnableNoNaNsFPMath)
CGOPT(bool, EnableNoSignedZerosFPMath)
CGOPT(bool, EnableApproxFuncFPMath)
CGOPT(bool, EnableNoTrappingFPMath)
CGOPT(bool, EnableAIXExtendedAltivecABI)
CGOPT(DenormalMode::DenormalModeKind, DenormalFPMath)
CGOPT(DenormalMode::DenormalModeKind, DenormalFP32Math)
CGOPT(bool, EnableHonorSignDependentRoundingFPMath)
CGOPT(FloatABI::ABIType, FloatABIForCalls)
CGOPT(FPOpFusion::FPOpFusionMode, FuseFPOps)
CGOPT(SwiftAsyncFramePointerMode, SwiftAsyncFramePointer)
CGOPT(bool, DontPlaceZerosInBSS)
CGOPT(bool, EnableGuaranteedTailCallOpt)
CGOPT(bool, DisableTailCalls)
CGOPT(bool, StackSymbolOrdering)
CGOPT(bool, StackRealign)
CGOPT(std::string, TrapFuncName)
CGOPT(bool, UseCtors)
CGOPT(bool, DisableIntegratedAS)
CGOPT_EXP(bool, DataSections)
CGOPT_EXP(bool, FunctionSections)
CGOPT(bool, IgnoreXCOFFVisibility)
CGOPT(bool, XCOFFTracebackTable)
CGOPT(bool, EnableBBAddrMap)
CGOPT(std::string, BBSections)
CGOPT(unsigned, TLSSize)
CGOPT_EXP(bool, EmulatedTLS)
CGOPT_EXP(bool, EnableTLSDESC)
CGOPT(bool, UniqueSectionNames)
CGOPT(bool, UniqueBasicBlockSectionNames)
CGOPT(bool, SeparateNamedSections)
CGOPT(EABI, EABIVersion)
CGOPT(DebuggerKind, DebuggerTuningOpt)
CGOPT(bool, EnableStackSizeSection)
CGOPT(bool, EnableAddrsig)
CGOPT(bool, EmitCallSiteInfo)
CGOPT(bool, EnableMachineFunctionSplitter)
CGOPT(bool, EnableDebugEntryValues)
CGOPT(bool, ForceDwarfFrameSection)
CGOPT(bool, XRayFunctionIndex)
CGOPT(bool, DebugStrictDwarf)
CGOPT(unsigned, AlignLoops)
CGOPT(bool, JMCInstrument)
CGOPT(bool, XCOFFReadOnlyPointers)

codegen::RegisterCodeGenFlags::RegisterCodeGenFlags() {
#define CGBINDOPT(NAME)                                                        \
  do {                                                                         \
    NAME##View = std::addressof(NAME);                                         \
  } while (0)

  static cl::opt<std::string> MArch(
      "march", cl::desc("Architecture to generate code for (see --version)"));
  CGBINDOPT(MArch);

  static cl::opt<std::string> MCPU(
      "mcpu", cl::desc("Target a specific cpu type (-mcpu=help for details)"),
      cl::value_desc("cpu-name"), cl::init(""));
  CGBINDOPT(MCPU);

  static cl::list<std::string> MAttrs(
      "mattr", cl::CommaSeparated,
      cl::desc("Target specific attributes (-mattr=help for details)"),
      cl::value_desc("a1,+a2,-a3,..."));
  CGBINDOPT(MAttrs);

  static cl::opt<Reloc::Model> RelocModel(
      "relocation-model", cl::desc("Choose relocation model"),
      cl::values(
          clEnumValN(Reloc::Static, "static", "Non-relocatable code"),
          clEnumValN(Reloc::PIC_, "pic",
                     "Fully relocatable, position independent code"),
          clEnumValN(Reloc::DynamicNoPIC, "dynamic-no-pic",
                     "Relocatable external references, non-relocatable code"),
          clEnumValN(
              Reloc::ROPI, "ropi",
              "Code and read-only data relocatable, accessed PC-relative"),
          clEnumValN(
              Reloc::RWPI, "rwpi",
              "Read-write data relocatable, accessed relative to static base"),
          clEnumValN(Reloc::ROPI_RWPI, "ropi-rwpi",
                     "Combination of ropi and rwpi")));
  CGBINDOPT(RelocModel);

  static cl::opt<ThreadModel::Model> ThreadModel(
      "thread-model", cl::desc("Choose threading model"),
      cl::init(ThreadModel::POSIX),
      cl::values(
          clEnumValN(ThreadModel::POSIX, "posix", "POSIX thread model"),
          clEnumValN(ThreadModel::Single, "single", "Single thread model")));
  CGBINDOPT(ThreadModel);

  static cl::opt<CodeModel::Model> CodeModel(
      "code-model", cl::desc("Choose code model"),
      cl::values(clEnumValN(CodeModel::Tiny, "tiny", "Tiny code model"),
                 clEnumValN(CodeModel::Small, "small", "Small code model"),
                 clEnumValN(CodeModel::Kernel, "kernel", "Kernel code model"),
                 clEnumValN(CodeModel::Medium, "medium", "Medium code model"),
                 clEnumValN(CodeModel::Large, "large", "Large code model")));
  CGBINDOPT(CodeModel);

  static cl::opt<uint64_t> LargeDataThreshold(
      "large-data-threshold",
      cl::desc("Choose large data threshold for x86_64 medium code model"),
      cl::init(0));
  CGBINDOPT(LargeDataThreshold);

  static cl::opt<ExceptionHandling> ExceptionModel(
      "exception-model", cl::desc("exception model"),
      cl::init(ExceptionHandling::None),
      cl::values(
          clEnumValN(ExceptionHandling::None, "default",
                     "default exception handling model"),
          clEnumValN(ExceptionHandling::DwarfCFI, "dwarf",
                     "DWARF-like CFI based exception handling"),
          clEnumValN(ExceptionHandling::SjLj, "sjlj",
                     "SjLj exception handling"),
          clEnumValN(ExceptionHandling::ARM, "arm", "ARM EHABI exceptions"),
          clEnumValN(ExceptionHandling::WinEH, "wineh",
                     "Windows exception model"),
          clEnumValN(ExceptionHandling::Wasm, "wasm",
                     "WebAssembly exception handling")));
  CGBINDOPT(ExceptionModel);

  static cl::opt<CodeGenFileType> FileType(
      "filetype", cl::init(CodeGenFileType::AssemblyFile),
      cl::desc(
          "Choose a file type (not all types are supported by all targets):"),
      cl::values(clEnumValN(CodeGenFileType::AssemblyFile, "asm",
                            "Emit an assembly ('.s') file"),
                 clEnumValN(CodeGenFileType::ObjectFile, "obj",
                            "Emit a native object ('.o') file"),
                 clEnumValN(CodeGenFileType::Null, "null",
                            "Emit nothing, for performance testing")));
  CGBINDOPT(FileType);

  static cl::opt<FramePointerKind> FramePointerUsage(
      "frame-pointer",
      cl::desc("Specify frame pointer elimination optimization"),
      cl::init(FramePointerKind::None),
      cl::values(
          clEnumValN(FramePointerKind::All, "all",
                     "Disable frame pointer elimination"),
          clEnumValN(FramePointerKind::NonLeaf, "non-leaf",
                     "Disable frame pointer elimination for non-leaf frame"),
          clEnumValN(FramePointerKind::Reserved, "reserved",
                     "Enable frame pointer elimination, but reserve the frame "
                     "pointer register"),
          clEnumValN(FramePointerKind::None, "none",
                     "Enable frame pointer elimination")));
  CGBINDOPT(FramePointerUsage);

  static cl::opt<bool> EnableUnsafeFPMath(
      "enable-unsafe-fp-math",
      cl::desc("Enable optimizations that may decrease FP precision"),
      cl::init(false));
  CGBINDOPT(EnableUnsafeFPMath);

  static cl::opt<bool> EnableNoInfsFPMath(
      "enable-no-infs-fp-math",
      cl::desc("Enable FP math optimizations that assume no +-Infs"),
      cl::init(false));
  CGBINDOPT(EnableNoInfsFPMath);

  static cl::opt<bool> EnableNoNaNsFPMath(
      "enable-no-nans-fp-math",
      cl::desc("Enable FP math optimizations that assume no NaNs"),
      cl::init(false));
  CGBINDOPT(EnableNoNaNsFPMath);

  static cl::opt<bool> EnableNoSignedZerosFPMath(
      "enable-no-signed-zeros-fp-math",
      cl::desc("Enable FP math optimizations that assume "
               "the sign of 0 is insignificant"),
      cl::init(false));
  CGBINDOPT(EnableNoSignedZerosFPMath);

  static cl::opt<bool> EnableApproxFuncFPMath(
      "enable-approx-func-fp-math",
      cl::desc("Enable FP math optimizations that assume approx func"),
      cl::init(false));
  CGBINDOPT(EnableApproxFuncFPMath);

  static cl::opt<bool> EnableNoTrappingFPMath(
      "enable-no-trapping-fp-math",
      cl::desc("Enable setting the FP exceptions build "
               "attribute not to use exceptions"),
      cl::init(false));
  CGBINDOPT(EnableNoTrappingFPMath);

  static const auto DenormFlagEnumOptions = cl::values(
      clEnumValN(DenormalMode::IEEE, "ieee", "IEEE 754 denormal numbers"),
      clEnumValN(DenormalMode::PreserveSign, "preserve-sign",
                 "the sign of a  flushed-to-zero number is preserved "
                 "in the sign of 0"),
      clEnumValN(DenormalMode::PositiveZero, "positive-zero",
                 "denormals are flushed to positive zero"),
      clEnumValN(DenormalMode::Dynamic, "dynamic",
                 "denormals have unknown treatment"));

  // FIXME: Doesn't have way to specify separate input and output modes.
  static cl::opt<DenormalMode::DenormalModeKind> DenormalFPMath(
    "denormal-fp-math",
    cl::desc("Select which denormal numbers the code is permitted to require"),
    cl::init(DenormalMode::IEEE),
    DenormFlagEnumOptions);
  CGBINDOPT(DenormalFPMath);

  static cl::opt<DenormalMode::DenormalModeKind> DenormalFP32Math(
    "denormal-fp-math-f32",
    cl::desc("Select which denormal numbers the code is permitted to require for float"),
    cl::init(DenormalMode::Invalid),
    DenormFlagEnumOptions);
  CGBINDOPT(DenormalFP32Math);

  static cl::opt<bool> EnableHonorSignDependentRoundingFPMath(
      "enable-sign-dependent-rounding-fp-math", cl::Hidden,
      cl::desc("Force codegen to assume rounding mode can change dynamically"),
      cl::init(false));
  CGBINDOPT(EnableHonorSignDependentRoundingFPMath);

  static cl::opt<FloatABI::ABIType> FloatABIForCalls(
      "float-abi", cl::desc("Choose float ABI type"),
      cl::init(FloatABI::Default),
      cl::values(clEnumValN(FloatABI::Default, "default",
                            "Target default float ABI type"),
                 clEnumValN(FloatABI::Soft, "soft",
                            "Soft float ABI (implied by -soft-float)"),
                 clEnumValN(FloatABI::Hard, "hard",
                            "Hard float ABI (uses FP registers)")));
  CGBINDOPT(FloatABIForCalls);

  static cl::opt<FPOpFusion::FPOpFusionMode> FuseFPOps(
      "fp-contract", cl::desc("Enable aggressive formation of fused FP ops"),
      cl::init(FPOpFusion::Standard),
      cl::values(
          clEnumValN(FPOpFusion::Fast, "fast",
                     "Fuse FP ops whenever profitable"),
          clEnumValN(FPOpFusion::Standard, "on", "Only fuse 'blessed' FP ops."),
          clEnumValN(FPOpFusion::Strict, "off",
                     "Only fuse FP ops when the result won't be affected.")));
  CGBINDOPT(FuseFPOps);

  static cl::opt<SwiftAsyncFramePointerMode> SwiftAsyncFramePointer(
      "swift-async-fp",
      cl::desc("Determine when the Swift async frame pointer should be set"),
      cl::init(SwiftAsyncFramePointerMode::Always),
      cl::values(clEnumValN(SwiftAsyncFramePointerMode::DeploymentBased, "auto",
                            "Determine based on deployment target"),
                 clEnumValN(SwiftAsyncFramePointerMode::Always, "always",
                            "Always set the bit"),
                 clEnumValN(SwiftAsyncFramePointerMode::Never, "never",
                            "Never set the bit")));
  CGBINDOPT(SwiftAsyncFramePointer);

  static cl::opt<bool> DontPlaceZerosInBSS(
      "nozero-initialized-in-bss",
      cl::desc("Don't place zero-initialized symbols into bss section"),
      cl::init(false));
  CGBINDOPT(DontPlaceZerosInBSS);

  static cl::opt<bool> EnableAIXExtendedAltivecABI(
      "vec-extabi", cl::desc("Enable the AIX Extended Altivec ABI."),
      cl::init(false));
  CGBINDOPT(EnableAIXExtendedAltivecABI);

  static cl::opt<bool> EnableGuaranteedTailCallOpt(
      "tailcallopt",
      cl::desc(
          "Turn fastcc calls into tail calls by (potentially) changing ABI."),
      cl::init(false));
  CGBINDOPT(EnableGuaranteedTailCallOpt);

  static cl::opt<bool> DisableTailCalls(
      "disable-tail-calls", cl::desc("Never emit tail calls"), cl::init(false));
  CGBINDOPT(DisableTailCalls);

  static cl::opt<bool> StackSymbolOrdering(
      "stack-symbol-ordering", cl::desc("Order local stack symbols."),
      cl::init(true));
  CGBINDOPT(StackSymbolOrdering);

  static cl::opt<bool> StackRealign(
      "stackrealign",
      cl::desc("Force align the stack to the minimum alignment"),
      cl::init(false));
  CGBINDOPT(StackRealign);

  static cl::opt<std::string> TrapFuncName(
      "trap-func", cl::Hidden,
      cl::desc("Emit a call to trap function rather than a trap instruction"),
      cl::init(""));
  CGBINDOPT(TrapFuncName);

  static cl::opt<bool> UseCtors("use-ctors",
                                cl::desc("Use .ctors instead of .init_array."),
                                cl::init(false));
  CGBINDOPT(UseCtors);

  static cl::opt<bool> DataSections(
      "data-sections", cl::desc("Emit data into separate sections"),
      cl::init(false));
  CGBINDOPT(DataSections);

  static cl::opt<bool> FunctionSections(
      "function-sections", cl::desc("Emit functions into separate sections"),
      cl::init(false));
  CGBINDOPT(FunctionSections);

  static cl::opt<bool> IgnoreXCOFFVisibility(
      "ignore-xcoff-visibility",
      cl::desc("Not emit the visibility attribute for asm in AIX OS or give "
               "all symbols 'unspecified' visibility in XCOFF object file"),
      cl::init(false));
  CGBINDOPT(IgnoreXCOFFVisibility);

  static cl::opt<bool> XCOFFTracebackTable(
      "xcoff-traceback-table", cl::desc("Emit the XCOFF traceback table"),
      cl::init(true));
  CGBINDOPT(XCOFFTracebackTable);

  static cl::opt<bool> EnableBBAddrMap(
      "basic-block-address-map",
      cl::desc("Emit the basic block address map section"), cl::init(false));
  CGBINDOPT(EnableBBAddrMap);

  static cl::opt<std::string> BBSections(
      "basic-block-sections",
      cl::desc("Emit basic blocks into separate sections"),
      cl::value_desc("all | <function list (file)> | labels | none"),
      cl::init("none"));
  CGBINDOPT(BBSections);

  static cl::opt<unsigned> TLSSize(
      "tls-size", cl::desc("Bit size of immediate TLS offsets"), cl::init(0));
  CGBINDOPT(TLSSize);

  static cl::opt<bool> EmulatedTLS(
      "emulated-tls", cl::desc("Use emulated TLS model"), cl::init(false));
  CGBINDOPT(EmulatedTLS);

  static cl::opt<bool> EnableTLSDESC(
      "enable-tlsdesc", cl::desc("Enable the use of TLS Descriptors"),
      cl::init(false));
  CGBINDOPT(EnableTLSDESC);

  static cl::opt<bool> UniqueSectionNames(
      "unique-section-names", cl::desc("Give unique names to every section"),
      cl::init(true));
  CGBINDOPT(UniqueSectionNames);

  static cl::opt<bool> UniqueBasicBlockSectionNames(
      "unique-basic-block-section-names",
      cl::desc("Give unique names to every basic block section"),
      cl::init(false));
  CGBINDOPT(UniqueBasicBlockSectionNames);

  static cl::opt<bool> SeparateNamedSections(
      "separate-named-sections",
      cl::desc("Use separate unique sections for named sections"),
      cl::init(false));
  CGBINDOPT(SeparateNamedSections);

  static cl::opt<EABI> EABIVersion(
      "meabi", cl::desc("Set EABI type (default depends on triple):"),
      cl::init(EABI::Default),
      cl::values(
          clEnumValN(EABI::Default, "default", "Triple default EABI version"),
          clEnumValN(EABI::EABI4, "4", "EABI version 4"),
          clEnumValN(EABI::EABI5, "5", "EABI version 5"),
          clEnumValN(EABI::GNU, "gnu", "EABI GNU")));
  CGBINDOPT(EABIVersion);

  static cl::opt<DebuggerKind> DebuggerTuningOpt(
      "debugger-tune", cl::desc("Tune debug info for a particular debugger"),
      cl::init(DebuggerKind::Default),
      cl::values(
          clEnumValN(DebuggerKind::GDB, "gdb", "gdb"),
          clEnumValN(DebuggerKind::LLDB, "lldb", "lldb"),
          clEnumValN(DebuggerKind::DBX, "dbx", "dbx"),
          clEnumValN(DebuggerKind::SCE, "sce", "SCE targets (e.g. PS4)")));
  CGBINDOPT(DebuggerTuningOpt);

  static cl::opt<bool> EnableStackSizeSection(
      "stack-size-section",
      cl::desc("Emit a section containing stack size metadata"),
      cl::init(false));
  CGBINDOPT(EnableStackSizeSection);

  static cl::opt<bool> EnableAddrsig(
      "addrsig", cl::desc("Emit an address-significance table"),
      cl::init(false));
  CGBINDOPT(EnableAddrsig);

  static cl::opt<bool> EmitCallSiteInfo(
      "emit-call-site-info",
      cl::desc(
          "Emit call site debug information, if debug information is enabled."),
      cl::init(false));
  CGBINDOPT(EmitCallSiteInfo);

  static cl::opt<bool> EnableDebugEntryValues(
      "debug-entry-values",
      cl::desc("Enable debug info for the debug entry values."),
      cl::init(false));
  CGBINDOPT(EnableDebugEntryValues);

  static cl::opt<bool> EnableMachineFunctionSplitter(
      "split-machine-functions",
      cl::desc("Split out cold basic blocks from machine functions based on "
               "profile information"),
      cl::init(false));
  CGBINDOPT(EnableMachineFunctionSplitter);

  static cl::opt<bool> ForceDwarfFrameSection(
      "force-dwarf-frame-section",
      cl::desc("Always emit a debug frame section."), cl::init(false));
  CGBINDOPT(ForceDwarfFrameSection);

  static cl::opt<bool> XRayFunctionIndex("xray-function-index",
                                         cl::desc("Emit xray_fn_idx section"),
                                         cl::init(true));
  CGBINDOPT(XRayFunctionIndex);

  static cl::opt<bool> DebugStrictDwarf(
      "strict-dwarf", cl::desc("use strict dwarf"), cl::init(false));
  CGBINDOPT(DebugStrictDwarf);

  static cl::opt<unsigned> AlignLoops("align-loops",
                                      cl::desc("Default alignment for loops"));
  CGBINDOPT(AlignLoops);

  static cl::opt<bool> JMCInstrument(
      "enable-jmc-instrument",
      cl::desc("Instrument functions with a call to __CheckForDebuggerJustMyCode"),
      cl::init(false));
  CGBINDOPT(JMCInstrument);

  static cl::opt<bool> XCOFFReadOnlyPointers(
      "mxcoff-roptr",
      cl::desc("When set to true, const objects with relocatable address "
               "values are put into the RO data section."),
      cl::init(false));
  CGBINDOPT(XCOFFReadOnlyPointers);

  static cl::opt<bool> DisableIntegratedAS(
      "no-integrated-as", cl::desc("Disable integrated assembler"),
      cl::init(false));
  CGBINDOPT(DisableIntegratedAS);

#undef CGBINDOPT

  mc::RegisterMCTargetOptionsFlags();
}

llvm::BasicBlockSection
codegen::getBBSectionsMode(llvm::TargetOptions &Options) {
  if (getBBSections() == "all")
    return BasicBlockSection::All;
  else if (getBBSections() == "labels")
    return BasicBlockSection::Labels;
  else if (getBBSections() == "none")
    return BasicBlockSection::None;
  else {
    ErrorOr<std::unique_ptr<MemoryBuffer>> MBOrErr =
        MemoryBuffer::getFile(getBBSections());
    if (!MBOrErr) {
      errs() << "Error loading basic block sections function list file: "
             << MBOrErr.getError().message() << "\n";
    } else {
      Options.BBSectionsFuncListBuf = std::move(*MBOrErr);
    }
    return BasicBlockSection::List;
  }
}

// Common utility function tightly tied to the options listed here. Initializes
// a TargetOptions object with CodeGen flags and returns it.
TargetOptions
codegen::InitTargetOptionsFromCodeGenFlags(const Triple &TheTriple) {
  TargetOptions Options;
  Options.AllowFPOpFusion = getFuseFPOps();
  Options.UnsafeFPMath = getEnableUnsafeFPMath();
  Options.NoInfsFPMath = getEnableNoInfsFPMath();
  Options.NoNaNsFPMath = getEnableNoNaNsFPMath();
  Options.NoSignedZerosFPMath = getEnableNoSignedZerosFPMath();
  Options.ApproxFuncFPMath = getEnableApproxFuncFPMath();
  Options.NoTrappingFPMath = getEnableNoTrappingFPMath();

  DenormalMode::DenormalModeKind DenormKind = getDenormalFPMath();

  // FIXME: Should have separate input and output flags
  Options.setFPDenormalMode(DenormalMode(DenormKind, DenormKind));

  Options.HonorSignDependentRoundingFPMathOption =
      getEnableHonorSignDependentRoundingFPMath();
  if (getFloatABIForCalls() != FloatABI::Default)
    Options.FloatABIType = getFloatABIForCalls();
  Options.EnableAIXExtendedAltivecABI = getEnableAIXExtendedAltivecABI();
  Options.NoZerosInBSS = getDontPlaceZerosInBSS();
  Options.GuaranteedTailCallOpt = getEnableGuaranteedTailCallOpt();
  Options.StackSymbolOrdering = getStackSymbolOrdering();
  Options.UseInitArray = !getUseCtors();
  Options.DisableIntegratedAS = getDisableIntegratedAS();
  Options.DataSections =
      getExplicitDataSections().value_or(TheTriple.hasDefaultDataSections());
  Options.FunctionSections = getFunctionSections();
  Options.IgnoreXCOFFVisibility = getIgnoreXCOFFVisibility();
  Options.XCOFFTracebackTable = getXCOFFTracebackTable();
  Options.BBAddrMap = getEnableBBAddrMap();
  Options.BBSections = getBBSectionsMode(Options);
  Options.UniqueSectionNames = getUniqueSectionNames();
  Options.UniqueBasicBlockSectionNames = getUniqueBasicBlockSectionNames();
  Options.SeparateNamedSections = getSeparateNamedSections();
  Options.TLSSize = getTLSSize();
  Options.EmulatedTLS =
      getExplicitEmulatedTLS().value_or(TheTriple.hasDefaultEmulatedTLS());
  Options.EnableTLSDESC =
      getExplicitEnableTLSDESC().value_or(TheTriple.hasDefaultTLSDESC());
  Options.ExceptionModel = getExceptionModel();
  Options.EmitStackSizeSection = getEnableStackSizeSection();
  Options.EnableMachineFunctionSplitter = getEnableMachineFunctionSplitter();
  Options.EmitAddrsig = getEnableAddrsig();
  Options.EmitCallSiteInfo = getEmitCallSiteInfo();
  Options.EnableDebugEntryValues = getEnableDebugEntryValues();
  Options.ForceDwarfFrameSection = getForceDwarfFrameSection();
  Options.XRayFunctionIndex = getXRayFunctionIndex();
  Options.DebugStrictDwarf = getDebugStrictDwarf();
  Options.LoopAlignment = getAlignLoops();
  Options.JMCInstrument = getJMCInstrument();
  Options.XCOFFReadOnlyPointers = getXCOFFReadOnlyPointers();

  Options.MCOptions = mc::InitMCTargetOptionsFromFlags();

  Options.ThreadModel = getThreadModel();
  Options.EABIVersion = getEABIVersion();
  Options.DebuggerTuning = getDebuggerTuningOpt();
  Options.SwiftAsyncFramePointer = getSwiftAsyncFramePointer();
  return Options;
}

std::string codegen::getCPUStr() {
  // If user asked for the 'native' CPU, autodetect here. If autodection fails,
  // this will set the CPU to an empty string which tells the target to
  // pick a basic default.
  if (getMCPU() == "native")
    return std::string(sys::getHostCPUName());

  return getMCPU();
}

std::string codegen::getFeaturesStr() {
  SubtargetFeatures Features;

  // If user asked for the 'native' CPU, we need to autodetect features.
  // This is necessary for x86 where the CPU might not support all the
  // features the autodetected CPU name lists in the target. For example,
  // not all Sandybridge processors support AVX.
  if (getMCPU() == "native")
    for (const auto &[Feature, IsEnabled] : sys::getHostCPUFeatures())
      Features.AddFeature(Feature, IsEnabled);

  for (auto const &MAttr : getMAttrs())
    Features.AddFeature(MAttr);

  return Features.getString();
}

std::vector<std::string> codegen::getFeatureList() {
  SubtargetFeatures Features;

  // If user asked for the 'native' CPU, we need to autodetect features.
  // This is necessary for x86 where the CPU might not support all the
  // features the autodetected CPU name lists in the target. For example,
  // not all Sandybridge processors support AVX.
  if (getMCPU() == "native")
    for (const auto &[Feature, IsEnabled] : sys::getHostCPUFeatures())
      Features.AddFeature(Feature, IsEnabled);

  for (auto const &MAttr : getMAttrs())
    Features.AddFeature(MAttr);

  return Features.getFeatures();
}

void codegen::renderBoolStringAttr(AttrBuilder &B, StringRef Name, bool Val) {
  B.addAttribute(Name, Val ? "true" : "false");
}

#define HANDLE_BOOL_ATTR(CL, AttrName)                                         \
  do {                                                                         \
    if (CL->getNumOccurrences() > 0 && !F.hasFnAttribute(AttrName))            \
      renderBoolStringAttr(NewAttrs, AttrName, *CL);                           \
  } while (0)

/// Set function attributes of function \p F based on CPU, Features, and command
/// line flags.
void codegen::setFunctionAttributes(StringRef CPU, StringRef Features,
                                    Function &F) {
  auto &Ctx = F.getContext();
  AttributeList Attrs = F.getAttributes();
  AttrBuilder NewAttrs(Ctx);

  if (!CPU.empty() && !F.hasFnAttribute("target-cpu"))
    NewAttrs.addAttribute("target-cpu", CPU);
  if (!Features.empty()) {
    // Append the command line features to any that are already on the function.
    StringRef OldFeatures =
        F.getFnAttribute("target-features").getValueAsString();
    if (OldFeatures.empty())
      NewAttrs.addAttribute("target-features", Features);
    else {
      SmallString<256> Appended(OldFeatures);
      Appended.push_back(',');
      Appended.append(Features);
      NewAttrs.addAttribute("target-features", Appended);
    }
  }
  if (FramePointerUsageView->getNumOccurrences() > 0 &&
      !F.hasFnAttribute("frame-pointer")) {
    if (getFramePointerUsage() == FramePointerKind::All)
      NewAttrs.addAttribute("frame-pointer", "all");
    else if (getFramePointerUsage() == FramePointerKind::NonLeaf)
      NewAttrs.addAttribute("frame-pointer", "non-leaf");
    else if (getFramePointerUsage() == FramePointerKind::Reserved)
      NewAttrs.addAttribute("frame-pointer", "reserved");
    else if (getFramePointerUsage() == FramePointerKind::None)
      NewAttrs.addAttribute("frame-pointer", "none");
  }
  if (DisableTailCallsView->getNumOccurrences() > 0)
    NewAttrs.addAttribute("disable-tail-calls",
                          toStringRef(getDisableTailCalls()));
  if (getStackRealign())
    NewAttrs.addAttribute("stackrealign");

  HANDLE_BOOL_ATTR(EnableUnsafeFPMathView, "unsafe-fp-math");
  HANDLE_BOOL_ATTR(EnableNoInfsFPMathView, "no-infs-fp-math");
  HANDLE_BOOL_ATTR(EnableNoNaNsFPMathView, "no-nans-fp-math");
  HANDLE_BOOL_ATTR(EnableNoSignedZerosFPMathView, "no-signed-zeros-fp-math");
  HANDLE_BOOL_ATTR(EnableApproxFuncFPMathView, "approx-func-fp-math");

  if (DenormalFPMathView->getNumOccurrences() > 0 &&
      !F.hasFnAttribute("denormal-fp-math")) {
    DenormalMode::DenormalModeKind DenormKind = getDenormalFPMath();

    // FIXME: Command line flag should expose separate input/output modes.
    NewAttrs.addAttribute("denormal-fp-math",
                          DenormalMode(DenormKind, DenormKind).str());
  }

  if (DenormalFP32MathView->getNumOccurrences() > 0 &&
      !F.hasFnAttribute("denormal-fp-math-f32")) {
    // FIXME: Command line flag should expose separate input/output modes.
    DenormalMode::DenormalModeKind DenormKind = getDenormalFP32Math();

    NewAttrs.addAttribute(
      "denormal-fp-math-f32",
      DenormalMode(DenormKind, DenormKind).str());
  }

  if (TrapFuncNameView->getNumOccurrences() > 0)
    for (auto &B : F)
      for (auto &I : B)
        if (auto *Call = dyn_cast<CallInst>(&I))
          if (const auto *F = Call->getCalledFunction())
            if (F->getIntrinsicID() == Intrinsic::debugtrap ||
                F->getIntrinsicID() == Intrinsic::trap)
              Call->addFnAttr(
                  Attribute::get(Ctx, "trap-func-name", getTrapFuncName()));

  // Let NewAttrs override Attrs.
  F.setAttributes(Attrs.addFnAttributes(Ctx, NewAttrs));
}

/// Set function attributes of functions in Module M based on CPU,
/// Features, and command line flags.
void codegen::setFunctionAttributes(StringRef CPU, StringRef Features,
                                    Module &M) {
  for (Function &F : M)
    setFunctionAttributes(CPU, Features, F);
}

Expected<std::unique_ptr<TargetMachine>>
codegen::createTargetMachineForTriple(StringRef TargetTriple,
                                      CodeGenOptLevel OptLevel) {
  Triple TheTriple(TargetTriple);
  std::string Error;
  const auto *TheTarget =
      TargetRegistry::lookupTarget(codegen::getMArch(), TheTriple, Error);
  if (!TheTarget)
    return createStringError(inconvertibleErrorCode(), Error);
  auto *Target = TheTarget->createTargetMachine(
      TheTriple.getTriple(), codegen::getCPUStr(), codegen::getFeaturesStr(),
      codegen::InitTargetOptionsFromCodeGenFlags(TheTriple),
      codegen::getExplicitRelocModel(), codegen::getExplicitCodeModel(),
      OptLevel);
  if (!Target)
    return createStringError(inconvertibleErrorCode(),
                             Twine("could not allocate target machine for ") +
                                 TargetTriple);
  return std::unique_ptr<TargetMachine>(Target);
}
