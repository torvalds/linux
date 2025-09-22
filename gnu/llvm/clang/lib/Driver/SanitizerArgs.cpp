//===--- SanitizerArgs.cpp - Arguments for sanitizer tools  ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "clang/Driver/SanitizerArgs.h"
#include "ToolChains/CommonArgs.h"
#include "clang/Basic/Sanitizers.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "clang/Driver/ToolChain.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SpecialCaseList.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/TargetParser/AArch64TargetParser.h"
#include "llvm/TargetParser/RISCVTargetParser.h"
#include "llvm/TargetParser/TargetParser.h"
#include "llvm/Transforms/Instrumentation/AddressSanitizerOptions.h"
#include <memory>

using namespace clang;
using namespace clang::driver;
using namespace llvm::opt;

static const SanitizerMask NeedsUbsanRt =
    SanitizerKind::Undefined | SanitizerKind::Integer |
    SanitizerKind::ImplicitConversion | SanitizerKind::Nullability |
    SanitizerKind::CFI | SanitizerKind::FloatDivideByZero |
    SanitizerKind::ObjCCast;
static const SanitizerMask NeedsUbsanCxxRt =
    SanitizerKind::Vptr | SanitizerKind::CFI;
static const SanitizerMask NotAllowedWithTrap = SanitizerKind::Vptr;
static const SanitizerMask NotAllowedWithMinimalRuntime = SanitizerKind::Vptr;
static const SanitizerMask NotAllowedWithExecuteOnly =
    SanitizerKind::Function | SanitizerKind::KCFI;
static const SanitizerMask NeedsUnwindTables =
    SanitizerKind::Address | SanitizerKind::HWAddress | SanitizerKind::Thread |
    SanitizerKind::Memory | SanitizerKind::DataFlow |
    SanitizerKind::NumericalStability;
static const SanitizerMask SupportsCoverage =
    SanitizerKind::Address | SanitizerKind::HWAddress |
    SanitizerKind::KernelAddress | SanitizerKind::KernelHWAddress |
    SanitizerKind::MemtagStack | SanitizerKind::MemtagHeap |
    SanitizerKind::MemtagGlobals | SanitizerKind::Memory |
    SanitizerKind::KernelMemory | SanitizerKind::Leak |
    SanitizerKind::Undefined | SanitizerKind::Integer | SanitizerKind::Bounds |
    SanitizerKind::ImplicitConversion | SanitizerKind::Nullability |
    SanitizerKind::DataFlow | SanitizerKind::Fuzzer |
    SanitizerKind::FuzzerNoLink | SanitizerKind::FloatDivideByZero |
    SanitizerKind::SafeStack | SanitizerKind::ShadowCallStack |
    SanitizerKind::Thread | SanitizerKind::ObjCCast | SanitizerKind::KCFI |
    SanitizerKind::NumericalStability;
static const SanitizerMask RecoverableByDefault =
    SanitizerKind::Undefined | SanitizerKind::Integer |
    SanitizerKind::ImplicitConversion | SanitizerKind::Nullability |
    SanitizerKind::FloatDivideByZero | SanitizerKind::ObjCCast;
static const SanitizerMask Unrecoverable =
    SanitizerKind::Unreachable | SanitizerKind::Return;
static const SanitizerMask AlwaysRecoverable = SanitizerKind::KernelAddress |
                                               SanitizerKind::KernelHWAddress |
                                               SanitizerKind::KCFI;
static const SanitizerMask NeedsLTO = SanitizerKind::CFI;
static const SanitizerMask TrappingSupported =
    (SanitizerKind::Undefined & ~SanitizerKind::Vptr) | SanitizerKind::Integer |
    SanitizerKind::Nullability | SanitizerKind::LocalBounds |
    SanitizerKind::CFI | SanitizerKind::FloatDivideByZero |
    SanitizerKind::ObjCCast;
static const SanitizerMask TrappingDefault = SanitizerKind::CFI;
static const SanitizerMask CFIClasses =
    SanitizerKind::CFIVCall | SanitizerKind::CFINVCall |
    SanitizerKind::CFIMFCall | SanitizerKind::CFIDerivedCast |
    SanitizerKind::CFIUnrelatedCast;
static const SanitizerMask CompatibleWithMinimalRuntime =
    TrappingSupported | SanitizerKind::Scudo | SanitizerKind::ShadowCallStack |
    SanitizerKind::MemtagStack | SanitizerKind::MemtagHeap |
    SanitizerKind::MemtagGlobals | SanitizerKind::KCFI;

enum CoverageFeature {
  CoverageFunc = 1 << 0,
  CoverageBB = 1 << 1,
  CoverageEdge = 1 << 2,
  CoverageIndirCall = 1 << 3,
  CoverageTraceBB = 1 << 4, // Deprecated.
  CoverageTraceCmp = 1 << 5,
  CoverageTraceDiv = 1 << 6,
  CoverageTraceGep = 1 << 7,
  Coverage8bitCounters = 1 << 8, // Deprecated.
  CoverageTracePC = 1 << 9,
  CoverageTracePCGuard = 1 << 10,
  CoverageNoPrune = 1 << 11,
  CoverageInline8bitCounters = 1 << 12,
  CoveragePCTable = 1 << 13,
  CoverageStackDepth = 1 << 14,
  CoverageInlineBoolFlag = 1 << 15,
  CoverageTraceLoads = 1 << 16,
  CoverageTraceStores = 1 << 17,
  CoverageControlFlow = 1 << 18,
};

enum BinaryMetadataFeature {
  BinaryMetadataCovered = 1 << 0,
  BinaryMetadataAtomics = 1 << 1,
  BinaryMetadataUAR = 1 << 2,
};

/// Parse a -fsanitize= or -fno-sanitize= argument's values, diagnosing any
/// invalid components. Returns a SanitizerMask.
static SanitizerMask parseArgValues(const Driver &D, const llvm::opt::Arg *A,
                                    bool DiagnoseErrors);

/// Parse -f(no-)?sanitize-coverage= flag values, diagnosing any invalid
/// components. Returns OR of members of \c CoverageFeature enumeration.
static int parseCoverageFeatures(const Driver &D, const llvm::opt::Arg *A,
                                 bool DiagnoseErrors);

/// Parse -f(no-)?sanitize-metadata= flag values, diagnosing any invalid
/// components. Returns OR of members of \c BinaryMetadataFeature enumeration.
static int parseBinaryMetadataFeatures(const Driver &D, const llvm::opt::Arg *A,
                                       bool DiagnoseErrors);

/// Produce an argument string from ArgList \p Args, which shows how it
/// provides some sanitizer kind from \p Mask. For example, the argument list
/// "-fsanitize=thread,vptr -fsanitize=address" with mask \c NeedsUbsanRt
/// would produce "-fsanitize=vptr".
static std::string lastArgumentForMask(const Driver &D,
                                       const llvm::opt::ArgList &Args,
                                       SanitizerMask Mask);

/// Produce an argument string from argument \p A, which shows how it provides
/// a value in \p Mask. For instance, the argument
/// "-fsanitize=address,alignment" with mask \c NeedsUbsanRt would produce
/// "-fsanitize=alignment".
static std::string describeSanitizeArg(const llvm::opt::Arg *A,
                                       SanitizerMask Mask);

/// Produce a string containing comma-separated names of sanitizers in \p
/// Sanitizers set.
static std::string toString(const clang::SanitizerSet &Sanitizers);

/// Return true if an execute-only target disallows data access to code
/// sections.
static bool isExecuteOnlyTarget(const llvm::Triple &Triple,
                                const llvm::opt::ArgList &Args) {
  if (Triple.isPS5())
    return true;
  return Args.hasFlagNoClaim(options::OPT_mexecute_only,
                             options::OPT_mno_execute_only, false);
}

static void validateSpecialCaseListFormat(const Driver &D,
                                          std::vector<std::string> &SCLFiles,
                                          unsigned MalformedSCLErrorDiagID,
                                          bool DiagnoseErrors) {
  if (SCLFiles.empty())
    return;

  std::string BLError;
  std::unique_ptr<llvm::SpecialCaseList> SCL(
      llvm::SpecialCaseList::create(SCLFiles, D.getVFS(), BLError));
  if (!SCL.get() && DiagnoseErrors)
    D.Diag(MalformedSCLErrorDiagID) << BLError;
}

static void addDefaultIgnorelists(const Driver &D, SanitizerMask Kinds,
                                  std::vector<std::string> &IgnorelistFiles,
                                  bool DiagnoseErrors) {
  struct Ignorelist {
    const char *File;
    SanitizerMask Mask;
  } Ignorelists[] = {{"asan_ignorelist.txt", SanitizerKind::Address},
                     {"hwasan_ignorelist.txt", SanitizerKind::HWAddress},
                     {"memtag_ignorelist.txt", SanitizerKind::MemTag},
                     {"msan_ignorelist.txt", SanitizerKind::Memory},
                     {"nsan_ignorelist.txt", SanitizerKind::NumericalStability},
                     {"tsan_ignorelist.txt", SanitizerKind::Thread},
                     {"dfsan_abilist.txt", SanitizerKind::DataFlow},
                     {"cfi_ignorelist.txt", SanitizerKind::CFI},
                     {"ubsan_ignorelist.txt",
                      SanitizerKind::Undefined | SanitizerKind::Integer |
                          SanitizerKind::Nullability |
                          SanitizerKind::FloatDivideByZero}};

  for (auto BL : Ignorelists) {
    if (!(Kinds & BL.Mask))
      continue;

    clang::SmallString<64> Path(D.ResourceDir);
    llvm::sys::path::append(Path, "share", BL.File);
    if (D.getVFS().exists(Path))
      IgnorelistFiles.push_back(std::string(Path));
    else if (BL.Mask == SanitizerKind::CFI && DiagnoseErrors)
      // If cfi_ignorelist.txt cannot be found in the resource dir, driver
      // should fail.
      D.Diag(clang::diag::err_drv_missing_sanitizer_ignorelist) << Path;
  }
  validateSpecialCaseListFormat(
      D, IgnorelistFiles, clang::diag::err_drv_malformed_sanitizer_ignorelist,
      DiagnoseErrors);
}

/// Parse -f(no-)?sanitize-(coverage-)?(allow|ignore)list argument's values,
/// diagnosing any invalid file paths and validating special case list format.
static void parseSpecialCaseListArg(const Driver &D,
                                    const llvm::opt::ArgList &Args,
                                    std::vector<std::string> &SCLFiles,
                                    llvm::opt::OptSpecifier SCLOptionID,
                                    llvm::opt::OptSpecifier NoSCLOptionID,
                                    unsigned MalformedSCLErrorDiagID,
                                    bool DiagnoseErrors) {
  for (const auto *Arg : Args) {
    // Match -fsanitize-(coverage-)?(allow|ignore)list.
    if (Arg->getOption().matches(SCLOptionID)) {
      Arg->claim();
      std::string SCLPath = Arg->getValue();
      if (D.getVFS().exists(SCLPath)) {
        SCLFiles.push_back(SCLPath);
      } else if (DiagnoseErrors) {
        D.Diag(clang::diag::err_drv_no_such_file) << SCLPath;
      }
      // Match -fno-sanitize-ignorelist.
    } else if (Arg->getOption().matches(NoSCLOptionID)) {
      Arg->claim();
      SCLFiles.clear();
    }
  }
  validateSpecialCaseListFormat(D, SCLFiles, MalformedSCLErrorDiagID,
                                DiagnoseErrors);
}

/// Sets group bits for every group that has at least one representative already
/// enabled in \p Kinds.
static SanitizerMask setGroupBits(SanitizerMask Kinds) {
#define SANITIZER(NAME, ID)
#define SANITIZER_GROUP(NAME, ID, ALIAS)                                       \
  if (Kinds & SanitizerKind::ID)                                               \
    Kinds |= SanitizerKind::ID##Group;
#include "clang/Basic/Sanitizers.def"
  return Kinds;
}

static SanitizerMask parseSanitizeTrapArgs(const Driver &D,
                                           const llvm::opt::ArgList &Args,
                                           bool DiagnoseErrors) {
  SanitizerMask TrapRemove; // During the loop below, the accumulated set of
                            // sanitizers disabled by the current sanitizer
                            // argument or any argument after it.
  SanitizerMask TrappingKinds;
  SanitizerMask TrappingSupportedWithGroups = setGroupBits(TrappingSupported);

  for (const llvm::opt::Arg *Arg : llvm::reverse(Args)) {
    if (Arg->getOption().matches(options::OPT_fsanitize_trap_EQ)) {
      Arg->claim();
      SanitizerMask Add = parseArgValues(D, Arg, true);
      Add &= ~TrapRemove;
      SanitizerMask InvalidValues = Add & ~TrappingSupportedWithGroups;
      if (InvalidValues && DiagnoseErrors) {
        SanitizerSet S;
        S.Mask = InvalidValues;
        D.Diag(diag::err_drv_unsupported_option_argument)
            << Arg->getSpelling() << toString(S);
      }
      TrappingKinds |= expandSanitizerGroups(Add) & ~TrapRemove;
    } else if (Arg->getOption().matches(options::OPT_fno_sanitize_trap_EQ)) {
      Arg->claim();
      TrapRemove |=
          expandSanitizerGroups(parseArgValues(D, Arg, DiagnoseErrors));
    }
  }

  // Apply default trapping behavior.
  TrappingKinds |= TrappingDefault & ~TrapRemove;

  return TrappingKinds;
}

bool SanitizerArgs::needsFuzzerInterceptors() const {
  return needsFuzzer() && !needsAsanRt() && !needsTsanRt() && !needsMsanRt();
}

bool SanitizerArgs::needsUbsanRt() const {
  // All of these include ubsan.
  if (needsAsanRt() || needsMsanRt() || needsNsanRt() || needsHwasanRt() ||
      needsTsanRt() || needsDfsanRt() || needsLsanRt() || needsCfiDiagRt() ||
      (needsScudoRt() && !requiresMinimalRuntime()))
    return false;

  return (Sanitizers.Mask & NeedsUbsanRt & ~TrapSanitizers.Mask) ||
         CoverageFeatures;
}

bool SanitizerArgs::needsCfiRt() const {
  return !(Sanitizers.Mask & SanitizerKind::CFI & ~TrapSanitizers.Mask) &&
         CfiCrossDso && !ImplicitCfiRuntime;
}

bool SanitizerArgs::needsCfiDiagRt() const {
  return (Sanitizers.Mask & SanitizerKind::CFI & ~TrapSanitizers.Mask) &&
         CfiCrossDso && !ImplicitCfiRuntime;
}

bool SanitizerArgs::requiresPIE() const { return NeedPIE; }

bool SanitizerArgs::needsUnwindTables() const {
  return static_cast<bool>(Sanitizers.Mask & NeedsUnwindTables);
}

bool SanitizerArgs::needsLTO() const {
  return static_cast<bool>(Sanitizers.Mask & NeedsLTO);
}

SanitizerArgs::SanitizerArgs(const ToolChain &TC,
                             const llvm::opt::ArgList &Args,
                             bool DiagnoseErrors) {
  SanitizerMask AllRemove;      // During the loop below, the accumulated set of
                                // sanitizers disabled by the current sanitizer
                                // argument or any argument after it.
  SanitizerMask AllAddedKinds;  // Mask of all sanitizers ever enabled by
                                // -fsanitize= flags (directly or via group
                                // expansion), some of which may be disabled
                                // later. Used to carefully prune
                                // unused-argument diagnostics.
  SanitizerMask DiagnosedKinds; // All Kinds we have diagnosed up to now.
                                // Used to deduplicate diagnostics.
  SanitizerMask Kinds;
  const SanitizerMask Supported = setGroupBits(TC.getSupportedSanitizers());

  CfiCrossDso = Args.hasFlag(options::OPT_fsanitize_cfi_cross_dso,
                             options::OPT_fno_sanitize_cfi_cross_dso, false);

  ToolChain::RTTIMode RTTIMode = TC.getRTTIMode();

  const Driver &D = TC.getDriver();
  SanitizerMask TrappingKinds = parseSanitizeTrapArgs(D, Args, DiagnoseErrors);
  SanitizerMask InvalidTrappingKinds = TrappingKinds & NotAllowedWithTrap;

  MinimalRuntime =
      Args.hasFlag(options::OPT_fsanitize_minimal_runtime,
                   options::OPT_fno_sanitize_minimal_runtime, MinimalRuntime);

  // The object size sanitizer should not be enabled at -O0.
  Arg *OptLevel = Args.getLastArg(options::OPT_O_Group);
  bool RemoveObjectSizeAtO0 =
      !OptLevel || OptLevel->getOption().matches(options::OPT_O0);

  for (const llvm::opt::Arg *Arg : llvm::reverse(Args)) {
    if (Arg->getOption().matches(options::OPT_fsanitize_EQ)) {
      Arg->claim();
      SanitizerMask Add = parseArgValues(D, Arg, DiagnoseErrors);

      if (RemoveObjectSizeAtO0) {
        AllRemove |= SanitizerKind::ObjectSize;

        // The user explicitly enabled the object size sanitizer. Warn
        // that this does nothing at -O0.
        if ((Add & SanitizerKind::ObjectSize) && DiagnoseErrors)
          D.Diag(diag::warn_drv_object_size_disabled_O0)
              << Arg->getAsString(Args);
      }

      AllAddedKinds |= expandSanitizerGroups(Add);

      // Avoid diagnosing any sanitizer which is disabled later.
      Add &= ~AllRemove;
      // At this point we have not expanded groups, so any unsupported
      // sanitizers in Add are those which have been explicitly enabled.
      // Diagnose them.
      if (SanitizerMask KindsToDiagnose =
              Add & InvalidTrappingKinds & ~DiagnosedKinds) {
        if (DiagnoseErrors) {
          std::string Desc = describeSanitizeArg(Arg, KindsToDiagnose);
          D.Diag(diag::err_drv_argument_not_allowed_with)
              << Desc << "-fsanitize-trap=undefined";
        }
        DiagnosedKinds |= KindsToDiagnose;
      }
      Add &= ~InvalidTrappingKinds;

      if (MinimalRuntime) {
        if (SanitizerMask KindsToDiagnose =
                Add & NotAllowedWithMinimalRuntime & ~DiagnosedKinds) {
          if (DiagnoseErrors) {
            std::string Desc = describeSanitizeArg(Arg, KindsToDiagnose);
            D.Diag(diag::err_drv_argument_not_allowed_with)
                << Desc << "-fsanitize-minimal-runtime";
          }
          DiagnosedKinds |= KindsToDiagnose;
        }
        Add &= ~NotAllowedWithMinimalRuntime;
      }

      if (llvm::opt::Arg *A = Args.getLastArg(options::OPT_mcmodel_EQ)) {
        StringRef CM = A->getValue();
        if (CM != "small" &&
            (Add & SanitizerKind::Function & ~DiagnosedKinds)) {
          if (DiagnoseErrors)
            D.Diag(diag::err_drv_argument_only_allowed_with)
                << "-fsanitize=function"
                << "-mcmodel=small";
          Add &= ~SanitizerKind::Function;
          DiagnosedKinds |= SanitizerKind::Function;
        }
      }
      // -fsanitize=function and -fsanitize=kcfi instrument indirect function
      // calls to load a type hash before the function label. Therefore, an
      // execute-only target doesn't support the function and kcfi sanitizers.
      const llvm::Triple &Triple = TC.getTriple();
      if (isExecuteOnlyTarget(Triple, Args)) {
        if (SanitizerMask KindsToDiagnose =
                Add & NotAllowedWithExecuteOnly & ~DiagnosedKinds) {
          if (DiagnoseErrors) {
            std::string Desc = describeSanitizeArg(Arg, KindsToDiagnose);
            D.Diag(diag::err_drv_argument_not_allowed_with)
                << Desc << Triple.str();
          }
          DiagnosedKinds |= KindsToDiagnose;
        }
        Add &= ~NotAllowedWithExecuteOnly;
      }

      // FIXME: Make CFI on member function calls compatible with cross-DSO CFI.
      // There are currently two problems:
      // - Virtual function call checks need to pass a pointer to the function
      //   address to llvm.type.test and a pointer to the address point to the
      //   diagnostic function. Currently we pass the same pointer to both
      //   places.
      // - Non-virtual function call checks may need to check multiple type
      //   identifiers.
      // Fixing both of those may require changes to the cross-DSO CFI
      // interface.
      if (CfiCrossDso && (Add & SanitizerKind::CFIMFCall & ~DiagnosedKinds)) {
        if (DiagnoseErrors)
          D.Diag(diag::err_drv_argument_not_allowed_with)
              << "-fsanitize=cfi-mfcall"
              << "-fsanitize-cfi-cross-dso";
        Add &= ~SanitizerKind::CFIMFCall;
        DiagnosedKinds |= SanitizerKind::CFIMFCall;
      }

      if (SanitizerMask KindsToDiagnose = Add & ~Supported & ~DiagnosedKinds) {
        if (DiagnoseErrors) {
          std::string Desc = describeSanitizeArg(Arg, KindsToDiagnose);
          D.Diag(diag::err_drv_unsupported_opt_for_target)
              << Desc << TC.getTriple().str();
        }
        DiagnosedKinds |= KindsToDiagnose;
      }
      Add &= Supported;

      // Test for -fno-rtti + explicit -fsanitizer=vptr before expanding groups
      // so we don't error out if -fno-rtti and -fsanitize=undefined were
      // passed.
      if ((Add & SanitizerKind::Vptr) && (RTTIMode == ToolChain::RM_Disabled)) {
        if (const llvm::opt::Arg *NoRTTIArg = TC.getRTTIArg()) {
          assert(NoRTTIArg->getOption().matches(options::OPT_fno_rtti) &&
                 "RTTI disabled without -fno-rtti option?");
          // The user explicitly passed -fno-rtti with -fsanitize=vptr, but
          // the vptr sanitizer requires RTTI, so this is a user error.
          if (DiagnoseErrors)
            D.Diag(diag::err_drv_argument_not_allowed_with)
                << "-fsanitize=vptr" << NoRTTIArg->getAsString(Args);
        } else {
          // The vptr sanitizer requires RTTI, but RTTI is disabled (by
          // default). Warn that the vptr sanitizer is being disabled.
          if (DiagnoseErrors)
            D.Diag(diag::warn_drv_disabling_vptr_no_rtti_default);
        }

        // Take out the Vptr sanitizer from the enabled sanitizers
        AllRemove |= SanitizerKind::Vptr;
      }

      Add = expandSanitizerGroups(Add);
      // Group expansion may have enabled a sanitizer which is disabled later.
      Add &= ~AllRemove;
      // Silently discard any unsupported sanitizers implicitly enabled through
      // group expansion.
      Add &= ~InvalidTrappingKinds;
      if (MinimalRuntime) {
        Add &= ~NotAllowedWithMinimalRuntime;
      }
      // NotAllowedWithExecuteOnly is silently discarded on an execute-only
      // target if implicitly enabled through group expansion.
      if (isExecuteOnlyTarget(Triple, Args))
        Add &= ~NotAllowedWithExecuteOnly;
      if (CfiCrossDso)
        Add &= ~SanitizerKind::CFIMFCall;
      // -fsanitize=undefined does not expand to signed-integer-overflow in
      // -fwrapv (implied by -fno-strict-overflow) mode.
      if (Add & SanitizerKind::UndefinedGroup) {
        bool S = Args.hasFlagNoClaim(options::OPT_fno_strict_overflow,
                                     options::OPT_fstrict_overflow, false);
        if (Args.hasFlagNoClaim(options::OPT_fwrapv, options::OPT_fno_wrapv, S))
          Add &= ~SanitizerKind::SignedIntegerOverflow;
      }
      Add &= Supported;

      if (Add & SanitizerKind::Fuzzer)
        Add |= SanitizerKind::FuzzerNoLink;

      // Enable coverage if the fuzzing flag is set.
      if (Add & SanitizerKind::FuzzerNoLink) {
        CoverageFeatures |= CoverageInline8bitCounters | CoverageIndirCall |
                            CoverageTraceCmp | CoveragePCTable;
        // Due to TLS differences, stack depth tracking is only enabled on Linux
        if (TC.getTriple().isOSLinux())
          CoverageFeatures |= CoverageStackDepth;
      }

      Kinds |= Add;
    } else if (Arg->getOption().matches(options::OPT_fno_sanitize_EQ)) {
      Arg->claim();
      SanitizerMask Remove = parseArgValues(D, Arg, DiagnoseErrors);
      AllRemove |= expandSanitizerGroups(Remove);
    }
  }

  std::pair<SanitizerMask, SanitizerMask> IncompatibleGroups[] = {
      std::make_pair(SanitizerKind::Address,
                     SanitizerKind::Thread | SanitizerKind::Memory),
      std::make_pair(SanitizerKind::Thread, SanitizerKind::Memory),
      std::make_pair(SanitizerKind::Leak,
                     SanitizerKind::Thread | SanitizerKind::Memory),
      std::make_pair(SanitizerKind::KernelAddress,
                     SanitizerKind::Address | SanitizerKind::Leak |
                         SanitizerKind::Thread | SanitizerKind::Memory),
      std::make_pair(SanitizerKind::HWAddress,
                     SanitizerKind::Address | SanitizerKind::Thread |
                         SanitizerKind::Memory | SanitizerKind::KernelAddress),
      std::make_pair(SanitizerKind::Scudo,
                     SanitizerKind::Address | SanitizerKind::HWAddress |
                         SanitizerKind::Leak | SanitizerKind::Thread |
                         SanitizerKind::Memory | SanitizerKind::KernelAddress),
      std::make_pair(SanitizerKind::SafeStack,
                     (TC.getTriple().isOSFuchsia() ? SanitizerMask()
                                                   : SanitizerKind::Leak) |
                         SanitizerKind::Address | SanitizerKind::HWAddress |
                         SanitizerKind::Thread | SanitizerKind::Memory |
                         SanitizerKind::KernelAddress),
      std::make_pair(SanitizerKind::KernelHWAddress,
                     SanitizerKind::Address | SanitizerKind::HWAddress |
                         SanitizerKind::Leak | SanitizerKind::Thread |
                         SanitizerKind::Memory | SanitizerKind::KernelAddress |
                         SanitizerKind::SafeStack),
      std::make_pair(SanitizerKind::KernelMemory,
                     SanitizerKind::Address | SanitizerKind::HWAddress |
                         SanitizerKind::Leak | SanitizerKind::Thread |
                         SanitizerKind::Memory | SanitizerKind::KernelAddress |
                         SanitizerKind::Scudo | SanitizerKind::SafeStack),
      std::make_pair(SanitizerKind::MemTag,
                     SanitizerKind::Address | SanitizerKind::KernelAddress |
                         SanitizerKind::HWAddress |
                         SanitizerKind::KernelHWAddress),
      std::make_pair(SanitizerKind::KCFI, SanitizerKind::Function)};
  // Enable toolchain specific default sanitizers if not explicitly disabled.
  SanitizerMask Default = TC.getDefaultSanitizers() & ~AllRemove;

  // Disable default sanitizers that are incompatible with explicitly requested
  // ones.
  for (auto G : IncompatibleGroups) {
    SanitizerMask Group = G.first;
    if ((Default & Group) && (Kinds & G.second))
      Default &= ~Group;
  }

  Kinds |= Default;

  // We disable the vptr sanitizer if it was enabled by group expansion but RTTI
  // is disabled.
  if ((Kinds & SanitizerKind::Vptr) && (RTTIMode == ToolChain::RM_Disabled)) {
    Kinds &= ~SanitizerKind::Vptr;
  }

  // Check that LTO is enabled if we need it.
  if ((Kinds & NeedsLTO) && !D.isUsingLTO() && DiagnoseErrors) {
    D.Diag(diag::err_drv_argument_only_allowed_with)
        << lastArgumentForMask(D, Args, Kinds & NeedsLTO) << "-flto";
  }

  if ((Kinds & SanitizerKind::ShadowCallStack) && TC.getTriple().isAArch64() &&
      !llvm::AArch64::isX18ReservedByDefault(TC.getTriple()) &&
      !Args.hasArg(options::OPT_ffixed_x18) && DiagnoseErrors) {
    D.Diag(diag::err_drv_argument_only_allowed_with)
        << lastArgumentForMask(D, Args, Kinds & SanitizerKind::ShadowCallStack)
        << "-ffixed-x18";
  }

  // Report error if there are non-trapping sanitizers that require
  // c++abi-specific  parts of UBSan runtime, and they are not provided by the
  // toolchain. We don't have a good way to check the latter, so we just
  // check if the toolchan supports vptr.
  if (~Supported & SanitizerKind::Vptr) {
    SanitizerMask KindsToDiagnose = Kinds & ~TrappingKinds & NeedsUbsanCxxRt;
    // The runtime library supports the Microsoft C++ ABI, but only well enough
    // for CFI. FIXME: Remove this once we support vptr on Windows.
    if (TC.getTriple().isOSWindows())
      KindsToDiagnose &= ~SanitizerKind::CFI;
    if (KindsToDiagnose) {
      SanitizerSet S;
      S.Mask = KindsToDiagnose;
      if (DiagnoseErrors)
        D.Diag(diag::err_drv_unsupported_opt_for_target)
            << ("-fno-sanitize-trap=" + toString(S)) << TC.getTriple().str();
      Kinds &= ~KindsToDiagnose;
    }
  }

  // Warn about incompatible groups of sanitizers.
  for (auto G : IncompatibleGroups) {
    SanitizerMask Group = G.first;
    if (Kinds & Group) {
      if (SanitizerMask Incompatible = Kinds & G.second) {
        if (DiagnoseErrors)
          D.Diag(clang::diag::err_drv_argument_not_allowed_with)
              << lastArgumentForMask(D, Args, Group)
              << lastArgumentForMask(D, Args, Incompatible);
        Kinds &= ~Incompatible;
      }
    }
  }
  // FIXME: Currently -fsanitize=leak is silently ignored in the presence of
  // -fsanitize=address. Perhaps it should print an error, or perhaps
  // -f(-no)sanitize=leak should change whether leak detection is enabled by
  // default in ASan?

  // Parse -f(no-)?sanitize-recover flags.
  SanitizerMask RecoverableKinds = RecoverableByDefault | AlwaysRecoverable;
  SanitizerMask DiagnosedUnrecoverableKinds;
  SanitizerMask DiagnosedAlwaysRecoverableKinds;
  for (const auto *Arg : Args) {
    if (Arg->getOption().matches(options::OPT_fsanitize_recover_EQ)) {
      SanitizerMask Add = parseArgValues(D, Arg, DiagnoseErrors);
      // Report error if user explicitly tries to recover from unrecoverable
      // sanitizer.
      if (SanitizerMask KindsToDiagnose =
              Add & Unrecoverable & ~DiagnosedUnrecoverableKinds) {
        SanitizerSet SetToDiagnose;
        SetToDiagnose.Mask |= KindsToDiagnose;
        if (DiagnoseErrors)
          D.Diag(diag::err_drv_unsupported_option_argument)
              << Arg->getSpelling() << toString(SetToDiagnose);
        DiagnosedUnrecoverableKinds |= KindsToDiagnose;
      }
      RecoverableKinds |= expandSanitizerGroups(Add);
      Arg->claim();
    } else if (Arg->getOption().matches(options::OPT_fno_sanitize_recover_EQ)) {
      SanitizerMask Remove = parseArgValues(D, Arg, DiagnoseErrors);
      // Report error if user explicitly tries to disable recovery from
      // always recoverable sanitizer.
      if (SanitizerMask KindsToDiagnose =
              Remove & AlwaysRecoverable & ~DiagnosedAlwaysRecoverableKinds) {
        SanitizerSet SetToDiagnose;
        SetToDiagnose.Mask |= KindsToDiagnose;
        if (DiagnoseErrors)
          D.Diag(diag::err_drv_unsupported_option_argument)
              << Arg->getSpelling() << toString(SetToDiagnose);
        DiagnosedAlwaysRecoverableKinds |= KindsToDiagnose;
      }
      RecoverableKinds &= ~expandSanitizerGroups(Remove);
      Arg->claim();
    }
  }
  RecoverableKinds &= Kinds;
  RecoverableKinds &= ~Unrecoverable;

  TrappingKinds &= Kinds;
  RecoverableKinds &= ~TrappingKinds;

  // Setup ignorelist files.
  // Add default ignorelist from resource directory for activated sanitizers,
  // and validate special case lists format.
  if (!Args.hasArgNoClaim(options::OPT_fno_sanitize_ignorelist))
    addDefaultIgnorelists(D, Kinds, SystemIgnorelistFiles, DiagnoseErrors);

  // Parse -f(no-)?sanitize-ignorelist options.
  // This also validates special case lists format.
  parseSpecialCaseListArg(
      D, Args, UserIgnorelistFiles, options::OPT_fsanitize_ignorelist_EQ,
      options::OPT_fno_sanitize_ignorelist,
      clang::diag::err_drv_malformed_sanitizer_ignorelist, DiagnoseErrors);

  // Parse -f[no-]sanitize-memory-track-origins[=level] options.
  if (AllAddedKinds & SanitizerKind::Memory) {
    if (Arg *A =
            Args.getLastArg(options::OPT_fsanitize_memory_track_origins_EQ,
                            options::OPT_fno_sanitize_memory_track_origins)) {
      if (!A->getOption().matches(
              options::OPT_fno_sanitize_memory_track_origins)) {
        StringRef S = A->getValue();
        if (S.getAsInteger(0, MsanTrackOrigins) || MsanTrackOrigins < 0 ||
            MsanTrackOrigins > 2) {
          if (DiagnoseErrors)
            D.Diag(clang::diag::err_drv_invalid_value)
                << A->getAsString(Args) << S;
        }
      }
    }
    MsanUseAfterDtor = Args.hasFlag(
        options::OPT_fsanitize_memory_use_after_dtor,
        options::OPT_fno_sanitize_memory_use_after_dtor, MsanUseAfterDtor);
    MsanParamRetval = Args.hasFlag(
        options::OPT_fsanitize_memory_param_retval,
        options::OPT_fno_sanitize_memory_param_retval, MsanParamRetval);
  } else if (AllAddedKinds & SanitizerKind::KernelMemory) {
    MsanUseAfterDtor = false;
    MsanParamRetval = Args.hasFlag(
        options::OPT_fsanitize_memory_param_retval,
        options::OPT_fno_sanitize_memory_param_retval, MsanParamRetval);
  } else {
    MsanUseAfterDtor = false;
    MsanParamRetval = false;
  }

  if (AllAddedKinds & SanitizerKind::MemTag) {
    StringRef S =
        Args.getLastArgValue(options::OPT_fsanitize_memtag_mode_EQ, "sync");
    if (S == "async" || S == "sync") {
      MemtagMode = S.str();
    } else {
      D.Diag(clang::diag::err_drv_invalid_value_with_suggestion)
          << "-fsanitize-memtag-mode=" << S << "{async, sync}";
      MemtagMode = "sync";
    }
  }

  if (AllAddedKinds & SanitizerKind::Thread) {
    TsanMemoryAccess = Args.hasFlag(
        options::OPT_fsanitize_thread_memory_access,
        options::OPT_fno_sanitize_thread_memory_access, TsanMemoryAccess);
    TsanFuncEntryExit = Args.hasFlag(
        options::OPT_fsanitize_thread_func_entry_exit,
        options::OPT_fno_sanitize_thread_func_entry_exit, TsanFuncEntryExit);
    TsanAtomics =
        Args.hasFlag(options::OPT_fsanitize_thread_atomics,
                     options::OPT_fno_sanitize_thread_atomics, TsanAtomics);
  }

  if (AllAddedKinds & SanitizerKind::CFI) {
    // Without PIE, external function address may resolve to a PLT record, which
    // can not be verified by the target module.
    NeedPIE |= CfiCrossDso;
    CfiICallGeneralizePointers =
        Args.hasArg(options::OPT_fsanitize_cfi_icall_generalize_pointers);

    CfiICallNormalizeIntegers =
        Args.hasArg(options::OPT_fsanitize_cfi_icall_normalize_integers);

    if (CfiCrossDso && CfiICallGeneralizePointers && DiagnoseErrors)
      D.Diag(diag::err_drv_argument_not_allowed_with)
          << "-fsanitize-cfi-cross-dso"
          << "-fsanitize-cfi-icall-generalize-pointers";

    CfiCanonicalJumpTables =
        Args.hasFlag(options::OPT_fsanitize_cfi_canonical_jump_tables,
                     options::OPT_fno_sanitize_cfi_canonical_jump_tables, true);
  }

  if (AllAddedKinds & SanitizerKind::KCFI) {
    CfiICallNormalizeIntegers =
        Args.hasArg(options::OPT_fsanitize_cfi_icall_normalize_integers);

    if (AllAddedKinds & SanitizerKind::CFI && DiagnoseErrors)
      D.Diag(diag::err_drv_argument_not_allowed_with)
          << "-fsanitize=kcfi"
          << lastArgumentForMask(D, Args, SanitizerKind::CFI);
  }

  Stats = Args.hasFlag(options::OPT_fsanitize_stats,
                       options::OPT_fno_sanitize_stats, false);

  if (MinimalRuntime) {
    SanitizerMask IncompatibleMask =
        Kinds & ~setGroupBits(CompatibleWithMinimalRuntime);
    if (IncompatibleMask && DiagnoseErrors)
      D.Diag(clang::diag::err_drv_argument_not_allowed_with)
          << "-fsanitize-minimal-runtime"
          << lastArgumentForMask(D, Args, IncompatibleMask);

    SanitizerMask NonTrappingCfi = Kinds & SanitizerKind::CFI & ~TrappingKinds;
    if (NonTrappingCfi && DiagnoseErrors)
      D.Diag(clang::diag::err_drv_argument_only_allowed_with)
          << "fsanitize-minimal-runtime"
          << "fsanitize-trap=cfi";
  }

  // Parse -f(no-)?sanitize-coverage flags if coverage is supported by the
  // enabled sanitizers.
  for (const auto *Arg : Args) {
    if (Arg->getOption().matches(options::OPT_fsanitize_coverage)) {
      int LegacySanitizeCoverage;
      if (Arg->getNumValues() == 1 &&
          !StringRef(Arg->getValue(0))
               .getAsInteger(0, LegacySanitizeCoverage)) {
        CoverageFeatures = 0;
        Arg->claim();
        if (LegacySanitizeCoverage != 0 && DiagnoseErrors) {
          D.Diag(diag::warn_drv_deprecated_arg)
              << Arg->getAsString(Args) << /*hasReplacement=*/true
              << "-fsanitize-coverage=trace-pc-guard";
        }
        continue;
      }
      CoverageFeatures |= parseCoverageFeatures(D, Arg, DiagnoseErrors);

      // Disable coverage and not claim the flags if there is at least one
      // non-supporting sanitizer.
      if (!(AllAddedKinds & ~AllRemove & ~setGroupBits(SupportsCoverage))) {
        Arg->claim();
      } else {
        CoverageFeatures = 0;
      }
    } else if (Arg->getOption().matches(options::OPT_fno_sanitize_coverage)) {
      Arg->claim();
      CoverageFeatures &= ~parseCoverageFeatures(D, Arg, DiagnoseErrors);
    }
  }
  // Choose at most one coverage type: function, bb, or edge.
  if (DiagnoseErrors) {
    if ((CoverageFeatures & CoverageFunc) && (CoverageFeatures & CoverageBB))
      D.Diag(clang::diag::err_drv_argument_not_allowed_with)
          << "-fsanitize-coverage=func"
          << "-fsanitize-coverage=bb";
    if ((CoverageFeatures & CoverageFunc) && (CoverageFeatures & CoverageEdge))
      D.Diag(clang::diag::err_drv_argument_not_allowed_with)
          << "-fsanitize-coverage=func"
          << "-fsanitize-coverage=edge";
    if ((CoverageFeatures & CoverageBB) && (CoverageFeatures & CoverageEdge))
      D.Diag(clang::diag::err_drv_argument_not_allowed_with)
          << "-fsanitize-coverage=bb"
          << "-fsanitize-coverage=edge";
    // Basic block tracing and 8-bit counters require some type of coverage
    // enabled.
    if (CoverageFeatures & CoverageTraceBB)
      D.Diag(clang::diag::warn_drv_deprecated_arg)
          << "-fsanitize-coverage=trace-bb" << /*hasReplacement=*/true
          << "-fsanitize-coverage=trace-pc-guard";
    if (CoverageFeatures & Coverage8bitCounters)
      D.Diag(clang::diag::warn_drv_deprecated_arg)
          << "-fsanitize-coverage=8bit-counters" << /*hasReplacement=*/true
          << "-fsanitize-coverage=trace-pc-guard";
  }

  int InsertionPointTypes = CoverageFunc | CoverageBB | CoverageEdge;
  int InstrumentationTypes = CoverageTracePC | CoverageTracePCGuard |
                             CoverageInline8bitCounters | CoverageTraceLoads |
                             CoverageTraceStores | CoverageInlineBoolFlag |
                             CoverageControlFlow;
  if ((CoverageFeatures & InsertionPointTypes) &&
      !(CoverageFeatures & InstrumentationTypes) && DiagnoseErrors) {
    D.Diag(clang::diag::warn_drv_deprecated_arg)
        << "-fsanitize-coverage=[func|bb|edge]" << /*hasReplacement=*/true
        << "-fsanitize-coverage=[func|bb|edge],[trace-pc-guard|trace-pc],["
           "control-flow]";
  }

  // trace-pc w/o func/bb/edge implies edge.
  if (!(CoverageFeatures & InsertionPointTypes)) {
    if (CoverageFeatures &
        (CoverageTracePC | CoverageTracePCGuard | CoverageInline8bitCounters |
         CoverageInlineBoolFlag | CoverageControlFlow))
      CoverageFeatures |= CoverageEdge;

    if (CoverageFeatures & CoverageStackDepth)
      CoverageFeatures |= CoverageFunc;
  }

  // Parse -fsanitize-coverage-(allow|ignore)list options if coverage enabled.
  // This also validates special case lists format.
  // Here, OptSpecifier() acts as a never-matching command-line argument.
  // So, there is no way to clear coverage lists but you can append to them.
  if (CoverageFeatures) {
    parseSpecialCaseListArg(
        D, Args, CoverageAllowlistFiles,
        options::OPT_fsanitize_coverage_allowlist, OptSpecifier(),
        clang::diag::err_drv_malformed_sanitizer_coverage_allowlist,
        DiagnoseErrors);
    parseSpecialCaseListArg(
        D, Args, CoverageIgnorelistFiles,
        options::OPT_fsanitize_coverage_ignorelist, OptSpecifier(),
        clang::diag::err_drv_malformed_sanitizer_coverage_ignorelist,
        DiagnoseErrors);
  }

  // Parse -f(no-)?sanitize-metadata.
  for (const auto *Arg :
       Args.filtered(options::OPT_fexperimental_sanitize_metadata_EQ,
                     options::OPT_fno_experimental_sanitize_metadata_EQ)) {
    if (Arg->getOption().matches(
            options::OPT_fexperimental_sanitize_metadata_EQ)) {
      Arg->claim();
      BinaryMetadataFeatures |=
          parseBinaryMetadataFeatures(D, Arg, DiagnoseErrors);
    } else {
      Arg->claim();
      BinaryMetadataFeatures &=
          ~parseBinaryMetadataFeatures(D, Arg, DiagnoseErrors);
    }
  }

  // Parse -fsanitize-metadata-ignorelist option if enabled.
  if (BinaryMetadataFeatures) {
    parseSpecialCaseListArg(
        D, Args, BinaryMetadataIgnorelistFiles,
        options::OPT_fexperimental_sanitize_metadata_ignorelist_EQ,
        OptSpecifier(), // Cannot clear ignore list, only append.
        clang::diag::err_drv_malformed_sanitizer_metadata_ignorelist,
        DiagnoseErrors);
  }

  SharedRuntime =
      Args.hasFlag(options::OPT_shared_libsan, options::OPT_static_libsan,
                   TC.getTriple().isAndroid() || TC.getTriple().isOSFuchsia() ||
                       TC.getTriple().isOSDarwin());

  ImplicitCfiRuntime = TC.getTriple().isAndroid();

  if (AllAddedKinds & SanitizerKind::Address) {
    NeedPIE |= TC.getTriple().isOSFuchsia();
    if (Arg *A =
            Args.getLastArg(options::OPT_fsanitize_address_field_padding)) {
      StringRef S = A->getValue();
      // Legal values are 0 and 1, 2, but in future we may add more levels.
      if ((S.getAsInteger(0, AsanFieldPadding) || AsanFieldPadding < 0 ||
           AsanFieldPadding > 2) &&
          DiagnoseErrors) {
        D.Diag(clang::diag::err_drv_invalid_value) << A->getAsString(Args) << S;
      }
    }

    if (Arg *WindowsDebugRTArg =
            Args.getLastArg(options::OPT__SLASH_MTd, options::OPT__SLASH_MT,
                            options::OPT__SLASH_MDd, options::OPT__SLASH_MD,
                            options::OPT__SLASH_LDd, options::OPT__SLASH_LD)) {
      switch (WindowsDebugRTArg->getOption().getID()) {
      case options::OPT__SLASH_MTd:
      case options::OPT__SLASH_MDd:
      case options::OPT__SLASH_LDd:
        if (DiagnoseErrors) {
          D.Diag(clang::diag::err_drv_argument_not_allowed_with)
              << WindowsDebugRTArg->getAsString(Args)
              << lastArgumentForMask(D, Args, SanitizerKind::Address);
          D.Diag(clang::diag::note_drv_address_sanitizer_debug_runtime);
        }
      }
    }

    StableABI = Args.hasFlag(options::OPT_fsanitize_stable_abi,
                             options::OPT_fno_sanitize_stable_abi, false);

    AsanUseAfterScope = Args.hasFlag(
        options::OPT_fsanitize_address_use_after_scope,
        options::OPT_fno_sanitize_address_use_after_scope, AsanUseAfterScope);

    AsanPoisonCustomArrayCookie = Args.hasFlag(
        options::OPT_fsanitize_address_poison_custom_array_cookie,
        options::OPT_fno_sanitize_address_poison_custom_array_cookie,
        AsanPoisonCustomArrayCookie);

    AsanOutlineInstrumentation =
        Args.hasFlag(options::OPT_fsanitize_address_outline_instrumentation,
                     options::OPT_fno_sanitize_address_outline_instrumentation,
                     AsanOutlineInstrumentation);

    AsanGlobalsDeadStripping = Args.hasFlag(
        options::OPT_fsanitize_address_globals_dead_stripping,
        options::OPT_fno_sanitize_address_globals_dead_stripping, true);

    // Enable ODR indicators which allow better handling of mixed instrumented
    // and uninstrumented globals. Disable them for Windows where weak odr
    // indicators (.weak.__odr_asan_gen*) may cause multiple definition linker
    // errors in the absence of -lldmingw.
    AsanUseOdrIndicator =
        Args.hasFlag(options::OPT_fsanitize_address_use_odr_indicator,
                     options::OPT_fno_sanitize_address_use_odr_indicator,
                     !TC.getTriple().isOSWindows());

    if (AllAddedKinds & SanitizerKind::PointerCompare & ~AllRemove) {
      AsanInvalidPointerCmp = true;
    }

    if (AllAddedKinds & SanitizerKind::PointerSubtract & ~AllRemove) {
      AsanInvalidPointerSub = true;
    }

    if (TC.getTriple().isOSDarwin() &&
        (Args.hasArg(options::OPT_mkernel) ||
         Args.hasArg(options::OPT_fapple_kext))) {
      AsanDtorKind = llvm::AsanDtorKind::None;
    }

    if (const auto *Arg =
            Args.getLastArg(options::OPT_sanitize_address_destructor_EQ)) {
      auto parsedAsanDtorKind = AsanDtorKindFromString(Arg->getValue());
      if (parsedAsanDtorKind == llvm::AsanDtorKind::Invalid && DiagnoseErrors) {
        TC.getDriver().Diag(clang::diag::err_drv_unsupported_option_argument)
            << Arg->getSpelling() << Arg->getValue();
      }
      AsanDtorKind = parsedAsanDtorKind;
    }

    if (const auto *Arg = Args.getLastArg(
            options::OPT_sanitize_address_use_after_return_EQ)) {
      auto parsedAsanUseAfterReturn =
          AsanDetectStackUseAfterReturnModeFromString(Arg->getValue());
      if (parsedAsanUseAfterReturn ==
              llvm::AsanDetectStackUseAfterReturnMode::Invalid &&
          DiagnoseErrors) {
        TC.getDriver().Diag(clang::diag::err_drv_unsupported_option_argument)
            << Arg->getSpelling() << Arg->getValue();
      }
      AsanUseAfterReturn = parsedAsanUseAfterReturn;
    }

  } else {
    AsanUseAfterScope = false;
    // -fsanitize=pointer-compare/pointer-subtract requires -fsanitize=address.
    SanitizerMask DetectInvalidPointerPairs =
        SanitizerKind::PointerCompare | SanitizerKind::PointerSubtract;
    if ((AllAddedKinds & DetectInvalidPointerPairs & ~AllRemove) &&
        DiagnoseErrors) {
      TC.getDriver().Diag(clang::diag::err_drv_argument_only_allowed_with)
          << lastArgumentForMask(D, Args,
                                 SanitizerKind::PointerCompare |
                                     SanitizerKind::PointerSubtract)
          << "-fsanitize=address";
    }
  }

  if (AllAddedKinds & SanitizerKind::HWAddress) {
    if (Arg *HwasanAbiArg =
            Args.getLastArg(options::OPT_fsanitize_hwaddress_abi_EQ)) {
      HwasanAbi = HwasanAbiArg->getValue();
      if (HwasanAbi != "platform" && HwasanAbi != "interceptor" &&
          DiagnoseErrors)
        D.Diag(clang::diag::err_drv_invalid_value)
            << HwasanAbiArg->getAsString(Args) << HwasanAbi;
    } else {
      HwasanAbi = "interceptor";
    }
    if (TC.getTriple().getArch() == llvm::Triple::x86_64)
      HwasanUseAliases = Args.hasFlag(
          options::OPT_fsanitize_hwaddress_experimental_aliasing,
          options::OPT_fno_sanitize_hwaddress_experimental_aliasing,
          HwasanUseAliases);
  }

  if (AllAddedKinds & SanitizerKind::SafeStack) {
    // SafeStack runtime is built into the system on Android and Fuchsia.
    SafeStackRuntime =
        !TC.getTriple().isAndroid() && !TC.getTriple().isOSFuchsia();
  }

  LinkRuntimes =
      Args.hasFlag(options::OPT_fsanitize_link_runtime,
                   options::OPT_fno_sanitize_link_runtime, LinkRuntimes);

  // Parse -link-cxx-sanitizer flag.
  LinkCXXRuntimes = Args.hasArg(options::OPT_fsanitize_link_cxx_runtime,
                                options::OPT_fno_sanitize_link_cxx_runtime,
                                LinkCXXRuntimes) ||
                    D.CCCIsCXX();

  NeedsMemProfRt = Args.hasFlag(options::OPT_fmemory_profile,
                                options::OPT_fmemory_profile_EQ,
                                options::OPT_fno_memory_profile, false);

  // Finally, initialize the set of available and recoverable sanitizers.
  Sanitizers.Mask |= Kinds;
  RecoverableSanitizers.Mask |= RecoverableKinds;
  TrapSanitizers.Mask |= TrappingKinds;
  assert(!(RecoverableKinds & TrappingKinds) &&
         "Overlap between recoverable and trapping sanitizers");
}

static std::string toString(const clang::SanitizerSet &Sanitizers) {
  std::string Res;
#define SANITIZER(NAME, ID)                                                    \
  if (Sanitizers.has(SanitizerKind::ID)) {                                     \
    if (!Res.empty())                                                          \
      Res += ",";                                                              \
    Res += NAME;                                                               \
  }
#include "clang/Basic/Sanitizers.def"
  return Res;
}

static void addSpecialCaseListOpt(const llvm::opt::ArgList &Args,
                                  llvm::opt::ArgStringList &CmdArgs,
                                  const char *SCLOptFlag,
                                  const std::vector<std::string> &SCLFiles) {
  for (const auto &SCLPath : SCLFiles) {
    SmallString<64> SCLOpt(SCLOptFlag);
    SCLOpt += SCLPath;
    CmdArgs.push_back(Args.MakeArgString(SCLOpt));
  }
}

static void addIncludeLinkerOption(const ToolChain &TC,
                                   const llvm::opt::ArgList &Args,
                                   llvm::opt::ArgStringList &CmdArgs,
                                   StringRef SymbolName) {
  SmallString<64> LinkerOptionFlag;
  LinkerOptionFlag = "--linker-option=/include:";
  if (TC.getTriple().getArch() == llvm::Triple::x86) {
    // Win32 mangles C function names with a '_' prefix.
    LinkerOptionFlag += '_';
  }
  LinkerOptionFlag += SymbolName;
  CmdArgs.push_back(Args.MakeArgString(LinkerOptionFlag));
}

static bool hasTargetFeatureMTE(const llvm::opt::ArgStringList &CmdArgs) {
  for (auto Start = CmdArgs.begin(), End = CmdArgs.end(); Start != End;
       ++Start) {
    auto It = std::find(Start, End, StringRef("+mte"));
    if (It == End)
      break;
    if (It > Start && *std::prev(It) == StringRef("-target-feature"))
      return true;
    Start = It;
  }
  return false;
}

void SanitizerArgs::addArgs(const ToolChain &TC, const llvm::opt::ArgList &Args,
                            llvm::opt::ArgStringList &CmdArgs,
                            types::ID InputType) const {
  // NVPTX doesn't currently support sanitizers.  Bailing out here means
  // that e.g. -fsanitize=address applies only to host code, which is what we
  // want for now.
  if (TC.getTriple().isNVPTX())
    return;
  // AMDGPU sanitizer support is experimental and controlled by -fgpu-sanitize.
  bool GPUSanitize = false;
  if (TC.getTriple().isAMDGPU()) {
    if (!Args.hasFlag(options::OPT_fgpu_sanitize, options::OPT_fno_gpu_sanitize,
                      true))
      return;
    GPUSanitize = true;
  }

  // Translate available CoverageFeatures to corresponding clang-cc1 flags.
  // Do it even if Sanitizers.empty() since some forms of coverage don't require
  // sanitizers.
  std::pair<int, const char *> CoverageFlags[] = {
      std::make_pair(CoverageFunc, "-fsanitize-coverage-type=1"),
      std::make_pair(CoverageBB, "-fsanitize-coverage-type=2"),
      std::make_pair(CoverageEdge, "-fsanitize-coverage-type=3"),
      std::make_pair(CoverageIndirCall, "-fsanitize-coverage-indirect-calls"),
      std::make_pair(CoverageTraceBB, "-fsanitize-coverage-trace-bb"),
      std::make_pair(CoverageTraceCmp, "-fsanitize-coverage-trace-cmp"),
      std::make_pair(CoverageTraceDiv, "-fsanitize-coverage-trace-div"),
      std::make_pair(CoverageTraceGep, "-fsanitize-coverage-trace-gep"),
      std::make_pair(Coverage8bitCounters, "-fsanitize-coverage-8bit-counters"),
      std::make_pair(CoverageTracePC, "-fsanitize-coverage-trace-pc"),
      std::make_pair(CoverageTracePCGuard,
                     "-fsanitize-coverage-trace-pc-guard"),
      std::make_pair(CoverageInline8bitCounters,
                     "-fsanitize-coverage-inline-8bit-counters"),
      std::make_pair(CoverageInlineBoolFlag,
                     "-fsanitize-coverage-inline-bool-flag"),
      std::make_pair(CoveragePCTable, "-fsanitize-coverage-pc-table"),
      std::make_pair(CoverageNoPrune, "-fsanitize-coverage-no-prune"),
      std::make_pair(CoverageStackDepth, "-fsanitize-coverage-stack-depth"),
      std::make_pair(CoverageTraceLoads, "-fsanitize-coverage-trace-loads"),
      std::make_pair(CoverageTraceStores, "-fsanitize-coverage-trace-stores"),
      std::make_pair(CoverageControlFlow, "-fsanitize-coverage-control-flow")};
  for (auto F : CoverageFlags) {
    if (CoverageFeatures & F.first)
      CmdArgs.push_back(F.second);
  }
  addSpecialCaseListOpt(
      Args, CmdArgs, "-fsanitize-coverage-allowlist=", CoverageAllowlistFiles);
  addSpecialCaseListOpt(Args, CmdArgs, "-fsanitize-coverage-ignorelist=",
                        CoverageIgnorelistFiles);

  if (!GPUSanitize) {
    // Translate available BinaryMetadataFeatures to corresponding clang-cc1
    // flags. Does not depend on any other sanitizers. Unsupported on GPUs.
    const std::pair<int, std::string> BinaryMetadataFlags[] = {
        std::make_pair(BinaryMetadataCovered, "covered"),
        std::make_pair(BinaryMetadataAtomics, "atomics"),
        std::make_pair(BinaryMetadataUAR, "uar")};
    for (const auto &F : BinaryMetadataFlags) {
      if (BinaryMetadataFeatures & F.first)
        CmdArgs.push_back(
            Args.MakeArgString("-fexperimental-sanitize-metadata=" + F.second));
    }
    addSpecialCaseListOpt(Args, CmdArgs,
                          "-fexperimental-sanitize-metadata-ignorelist=",
                          BinaryMetadataIgnorelistFiles);
  }

  if (TC.getTriple().isOSWindows() && needsUbsanRt() &&
      Args.hasFlag(options::OPT_frtlib_defaultlib,
                   options::OPT_fno_rtlib_defaultlib, true)) {
    // Instruct the code generator to embed linker directives in the object file
    // that cause the required runtime libraries to be linked.
    CmdArgs.push_back(
        Args.MakeArgString("--dependent-lib=" +
                           TC.getCompilerRTBasename(Args, "ubsan_standalone")));
    if (types::isCXX(InputType))
      CmdArgs.push_back(Args.MakeArgString(
          "--dependent-lib=" +
          TC.getCompilerRTBasename(Args, "ubsan_standalone_cxx")));
  }
  if (TC.getTriple().isOSWindows() && needsStatsRt() &&
      Args.hasFlag(options::OPT_frtlib_defaultlib,
                   options::OPT_fno_rtlib_defaultlib, true)) {
    CmdArgs.push_back(Args.MakeArgString(
        "--dependent-lib=" + TC.getCompilerRTBasename(Args, "stats_client")));

    // The main executable must export the stats runtime.
    // FIXME: Only exporting from the main executable (e.g. based on whether the
    // translation unit defines main()) would save a little space, but having
    // multiple copies of the runtime shouldn't hurt.
    CmdArgs.push_back(Args.MakeArgString(
        "--dependent-lib=" + TC.getCompilerRTBasename(Args, "stats")));
    addIncludeLinkerOption(TC, Args, CmdArgs, "__sanitizer_stats_register");
  }

  if (Sanitizers.empty())
    return;
  CmdArgs.push_back(Args.MakeArgString("-fsanitize=" + toString(Sanitizers)));

  if (!RecoverableSanitizers.empty())
    CmdArgs.push_back(Args.MakeArgString("-fsanitize-recover=" +
                                         toString(RecoverableSanitizers)));

  if (!TrapSanitizers.empty())
    CmdArgs.push_back(
        Args.MakeArgString("-fsanitize-trap=" + toString(TrapSanitizers)));

  addSpecialCaseListOpt(Args, CmdArgs,
                        "-fsanitize-ignorelist=", UserIgnorelistFiles);
  addSpecialCaseListOpt(Args, CmdArgs,
                        "-fsanitize-system-ignorelist=", SystemIgnorelistFiles);

  if (MsanTrackOrigins)
    CmdArgs.push_back(Args.MakeArgString("-fsanitize-memory-track-origins=" +
                                         Twine(MsanTrackOrigins)));

  if (MsanUseAfterDtor)
    CmdArgs.push_back("-fsanitize-memory-use-after-dtor");

  if (!MsanParamRetval)
    CmdArgs.push_back("-fno-sanitize-memory-param-retval");

  // FIXME: Pass these parameters as function attributes, not as -llvm flags.
  if (!TsanMemoryAccess) {
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back("-tsan-instrument-memory-accesses=0");
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back("-tsan-instrument-memintrinsics=0");
  }
  if (!TsanFuncEntryExit) {
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back("-tsan-instrument-func-entry-exit=0");
  }
  if (!TsanAtomics) {
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back("-tsan-instrument-atomics=0");
  }

  if (HwasanUseAliases) {
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back("-hwasan-experimental-use-page-aliases=1");
  }

  if (CfiCrossDso)
    CmdArgs.push_back("-fsanitize-cfi-cross-dso");

  if (CfiICallGeneralizePointers)
    CmdArgs.push_back("-fsanitize-cfi-icall-generalize-pointers");

  if (CfiICallNormalizeIntegers)
    CmdArgs.push_back("-fsanitize-cfi-icall-experimental-normalize-integers");

  if (CfiCanonicalJumpTables)
    CmdArgs.push_back("-fsanitize-cfi-canonical-jump-tables");

  if (Stats)
    CmdArgs.push_back("-fsanitize-stats");

  if (MinimalRuntime)
    CmdArgs.push_back("-fsanitize-minimal-runtime");

  if (AsanFieldPadding)
    CmdArgs.push_back(Args.MakeArgString("-fsanitize-address-field-padding=" +
                                         Twine(AsanFieldPadding)));

  if (AsanUseAfterScope)
    CmdArgs.push_back("-fsanitize-address-use-after-scope");

  if (AsanPoisonCustomArrayCookie)
    CmdArgs.push_back("-fsanitize-address-poison-custom-array-cookie");

  if (AsanGlobalsDeadStripping)
    CmdArgs.push_back("-fsanitize-address-globals-dead-stripping");

  if (!AsanUseOdrIndicator)
    CmdArgs.push_back("-fno-sanitize-address-use-odr-indicator");

  if (AsanInvalidPointerCmp) {
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back("-asan-detect-invalid-pointer-cmp");
  }

  if (AsanInvalidPointerSub) {
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back("-asan-detect-invalid-pointer-sub");
  }

  if (AsanOutlineInstrumentation) {
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back("-asan-instrumentation-with-call-threshold=0");
  }

  // When emitting Stable ABI instrumentation, force outlining calls and avoid
  // inlining shadow memory poisoning. While this is a big performance burden
  // for now it allows full abstraction from implementation details.
  if (StableABI) {
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back("-asan-instrumentation-with-call-threshold=0");
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back("-asan-max-inline-poisoning-size=0");
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back("-asan-guard-against-version-mismatch=0");
  }

  // Only pass the option to the frontend if the user requested,
  // otherwise the frontend will just use the codegen default.
  if (AsanDtorKind != llvm::AsanDtorKind::Invalid) {
    CmdArgs.push_back(Args.MakeArgString("-fsanitize-address-destructor=" +
                                         AsanDtorKindToString(AsanDtorKind)));
  }

  if (AsanUseAfterReturn != llvm::AsanDetectStackUseAfterReturnMode::Invalid) {
    CmdArgs.push_back(Args.MakeArgString(
        "-fsanitize-address-use-after-return=" +
        AsanDetectStackUseAfterReturnModeToString(AsanUseAfterReturn)));
  }

  if (!HwasanAbi.empty()) {
    CmdArgs.push_back("-default-function-attr");
    CmdArgs.push_back(Args.MakeArgString("hwasan-abi=" + HwasanAbi));
  }

  if (Sanitizers.has(SanitizerKind::HWAddress) && !HwasanUseAliases) {
    CmdArgs.push_back("-target-feature");
    CmdArgs.push_back("+tagged-globals");
  }

  // MSan: Workaround for PR16386.
  // ASan: This is mainly to help LSan with cases such as
  // https://github.com/google/sanitizers/issues/373
  // We can't make this conditional on -fsanitize=leak, as that flag shouldn't
  // affect compilation.
  if (Sanitizers.has(SanitizerKind::Memory) ||
      Sanitizers.has(SanitizerKind::Address))
    CmdArgs.push_back("-fno-assume-sane-operator-new");

  // libFuzzer wants to intercept calls to certain library functions, so the
  // following -fno-builtin-* flags force the compiler to emit interposable
  // libcalls to these functions. Other sanitizers effectively do the same thing
  // by marking all library call sites with NoBuiltin attribute in their LLVM
  // pass. (see llvm::maybeMarkSanitizerLibraryCallNoBuiltin)
  if (Sanitizers.has(SanitizerKind::FuzzerNoLink)) {
    CmdArgs.push_back("-fno-builtin-bcmp");
    CmdArgs.push_back("-fno-builtin-memcmp");
    CmdArgs.push_back("-fno-builtin-strncmp");
    CmdArgs.push_back("-fno-builtin-strcmp");
    CmdArgs.push_back("-fno-builtin-strncasecmp");
    CmdArgs.push_back("-fno-builtin-strcasecmp");
    CmdArgs.push_back("-fno-builtin-strstr");
    CmdArgs.push_back("-fno-builtin-strcasestr");
    CmdArgs.push_back("-fno-builtin-memmem");
  }

  // Require -fvisibility= flag on non-Windows when compiling if vptr CFI is
  // enabled.
  if (Sanitizers.hasOneOf(CFIClasses) && !TC.getTriple().isOSWindows() &&
      !Args.hasArg(options::OPT_fvisibility_EQ)) {
    TC.getDriver().Diag(clang::diag::err_drv_argument_only_allowed_with)
        << lastArgumentForMask(TC.getDriver(), Args,
                               Sanitizers.Mask & CFIClasses)
        << "-fvisibility=";
  }

  if (Sanitizers.has(SanitizerKind::MemtagStack) &&
      !hasTargetFeatureMTE(CmdArgs))
    TC.getDriver().Diag(diag::err_stack_tagging_requires_hardware_feature);
}

SanitizerMask parseArgValues(const Driver &D, const llvm::opt::Arg *A,
                             bool DiagnoseErrors) {
  assert((A->getOption().matches(options::OPT_fsanitize_EQ) ||
          A->getOption().matches(options::OPT_fno_sanitize_EQ) ||
          A->getOption().matches(options::OPT_fsanitize_recover_EQ) ||
          A->getOption().matches(options::OPT_fno_sanitize_recover_EQ) ||
          A->getOption().matches(options::OPT_fsanitize_trap_EQ) ||
          A->getOption().matches(options::OPT_fno_sanitize_trap_EQ)) &&
         "Invalid argument in parseArgValues!");
  SanitizerMask Kinds;
  for (int i = 0, n = A->getNumValues(); i != n; ++i) {
    const char *Value = A->getValue(i);
    SanitizerMask Kind;
    // Special case: don't accept -fsanitize=all.
    if (A->getOption().matches(options::OPT_fsanitize_EQ) &&
        0 == strcmp("all", Value))
      Kind = SanitizerMask();
    else
      Kind = parseSanitizerValue(Value, /*AllowGroups=*/true);

    if (Kind)
      Kinds |= Kind;
    else if (DiagnoseErrors)
      D.Diag(clang::diag::err_drv_unsupported_option_argument)
          << A->getSpelling() << Value;
  }
  return Kinds;
}

int parseCoverageFeatures(const Driver &D, const llvm::opt::Arg *A,
                          bool DiagnoseErrors) {
  assert(A->getOption().matches(options::OPT_fsanitize_coverage) ||
         A->getOption().matches(options::OPT_fno_sanitize_coverage));
  int Features = 0;
  for (int i = 0, n = A->getNumValues(); i != n; ++i) {
    const char *Value = A->getValue(i);
    int F = llvm::StringSwitch<int>(Value)
                .Case("func", CoverageFunc)
                .Case("bb", CoverageBB)
                .Case("edge", CoverageEdge)
                .Case("indirect-calls", CoverageIndirCall)
                .Case("trace-bb", CoverageTraceBB)
                .Case("trace-cmp", CoverageTraceCmp)
                .Case("trace-div", CoverageTraceDiv)
                .Case("trace-gep", CoverageTraceGep)
                .Case("8bit-counters", Coverage8bitCounters)
                .Case("trace-pc", CoverageTracePC)
                .Case("trace-pc-guard", CoverageTracePCGuard)
                .Case("no-prune", CoverageNoPrune)
                .Case("inline-8bit-counters", CoverageInline8bitCounters)
                .Case("inline-bool-flag", CoverageInlineBoolFlag)
                .Case("pc-table", CoveragePCTable)
                .Case("stack-depth", CoverageStackDepth)
                .Case("trace-loads", CoverageTraceLoads)
                .Case("trace-stores", CoverageTraceStores)
                .Case("control-flow", CoverageControlFlow)
                .Default(0);
    if (F == 0 && DiagnoseErrors)
      D.Diag(clang::diag::err_drv_unsupported_option_argument)
          << A->getSpelling() << Value;
    Features |= F;
  }
  return Features;
}

int parseBinaryMetadataFeatures(const Driver &D, const llvm::opt::Arg *A,
                                bool DiagnoseErrors) {
  assert(
      A->getOption().matches(options::OPT_fexperimental_sanitize_metadata_EQ) ||
      A->getOption().matches(
          options::OPT_fno_experimental_sanitize_metadata_EQ));
  int Features = 0;
  for (int i = 0, n = A->getNumValues(); i != n; ++i) {
    const char *Value = A->getValue(i);
    int F = llvm::StringSwitch<int>(Value)
                .Case("covered", BinaryMetadataCovered)
                .Case("atomics", BinaryMetadataAtomics)
                .Case("uar", BinaryMetadataUAR)
                .Case("all", ~0)
                .Default(0);
    if (F == 0 && DiagnoseErrors)
      D.Diag(clang::diag::err_drv_unsupported_option_argument)
          << A->getSpelling() << Value;
    Features |= F;
  }
  return Features;
}

std::string lastArgumentForMask(const Driver &D, const llvm::opt::ArgList &Args,
                                SanitizerMask Mask) {
  for (llvm::opt::ArgList::const_reverse_iterator I = Args.rbegin(),
                                                  E = Args.rend();
       I != E; ++I) {
    const auto *Arg = *I;
    if (Arg->getOption().matches(options::OPT_fsanitize_EQ)) {
      SanitizerMask AddKinds =
          expandSanitizerGroups(parseArgValues(D, Arg, false));
      if (AddKinds & Mask)
        return describeSanitizeArg(Arg, Mask);
    } else if (Arg->getOption().matches(options::OPT_fno_sanitize_EQ)) {
      SanitizerMask RemoveKinds =
          expandSanitizerGroups(parseArgValues(D, Arg, false));
      Mask &= ~RemoveKinds;
    }
  }
  llvm_unreachable("arg list didn't provide expected value");
}

std::string describeSanitizeArg(const llvm::opt::Arg *A, SanitizerMask Mask) {
  assert(A->getOption().matches(options::OPT_fsanitize_EQ) &&
         "Invalid argument in describeSanitizerArg!");

  std::string Sanitizers;
  for (int i = 0, n = A->getNumValues(); i != n; ++i) {
    if (expandSanitizerGroups(
            parseSanitizerValue(A->getValue(i), /*AllowGroups=*/true)) &
        Mask) {
      if (!Sanitizers.empty())
        Sanitizers += ",";
      Sanitizers += A->getValue(i);
    }
  }

  assert(!Sanitizers.empty() && "arg didn't provide expected value");
  return "-fsanitize=" + Sanitizers;
}
