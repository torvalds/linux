//===- CompilerInvocation.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/CompilerInvocation.h"
#include "TestModuleFileExtension.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/CodeGenOptions.h"
#include "clang/Basic/CommentOptions.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticDriver.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/FileSystemOptions.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/LangStandard.h"
#include "clang/Basic/ObjCRuntime.h"
#include "clang/Basic/Sanitizers.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/TargetOptions.h"
#include "clang/Basic/Version.h"
#include "clang/Basic/Visibility.h"
#include "clang/Basic/XRayInstr.h"
#include "clang/Config/config.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "clang/Frontend/CommandLineSourceLoc.h"
#include "clang/Frontend/DependencyOutputOptions.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Frontend/FrontendOptions.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Frontend/MigratorOptions.h"
#include "clang/Frontend/PreprocessorOutputOptions.h"
#include "clang/Frontend/TextDiagnosticBuffer.h"
#include "clang/Frontend/Utils.h"
#include "clang/Lex/HeaderSearchOptions.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Sema/CodeCompleteOptions.h"
#include "clang/Serialization/ASTBitCodes.h"
#include "clang/Serialization/ModuleFileExtension.h"
#include "clang/StaticAnalyzer/Core/AnalyzerOptions.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/FloatingPointMode.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Frontend/Debug/Options.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/Linker/Linker.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/OptSpecifier.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"
#include "llvm/ProfileData/InstrProfReader.h"
#include "llvm/Remarks/HotnessThresholdParser.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/HashBuilder.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <ctime>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

using namespace clang;
using namespace driver;
using namespace options;
using namespace llvm::opt;

//===----------------------------------------------------------------------===//
// Helpers.
//===----------------------------------------------------------------------===//

// Parse misexpect tolerance argument value.
// Valid option values are integers in the range [0, 100)
static Expected<std::optional<uint32_t>> parseToleranceOption(StringRef Arg) {
  uint32_t Val;
  if (Arg.getAsInteger(10, Val))
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "Not an integer: %s", Arg.data());
  return Val;
}

//===----------------------------------------------------------------------===//
// Initialization.
//===----------------------------------------------------------------------===//

namespace {
template <class T> std::shared_ptr<T> make_shared_copy(const T &X) {
  return std::make_shared<T>(X);
}

template <class T>
llvm::IntrusiveRefCntPtr<T> makeIntrusiveRefCntCopy(const T &X) {
  return llvm::makeIntrusiveRefCnt<T>(X);
}
} // namespace

CompilerInvocationBase::CompilerInvocationBase()
    : LangOpts(std::make_shared<LangOptions>()),
      TargetOpts(std::make_shared<TargetOptions>()),
      DiagnosticOpts(llvm::makeIntrusiveRefCnt<DiagnosticOptions>()),
      HSOpts(std::make_shared<HeaderSearchOptions>()),
      PPOpts(std::make_shared<PreprocessorOptions>()),
      AnalyzerOpts(llvm::makeIntrusiveRefCnt<AnalyzerOptions>()),
      MigratorOpts(std::make_shared<MigratorOptions>()),
      APINotesOpts(std::make_shared<APINotesOptions>()),
      CodeGenOpts(std::make_shared<CodeGenOptions>()),
      FSOpts(std::make_shared<FileSystemOptions>()),
      FrontendOpts(std::make_shared<FrontendOptions>()),
      DependencyOutputOpts(std::make_shared<DependencyOutputOptions>()),
      PreprocessorOutputOpts(std::make_shared<PreprocessorOutputOptions>()) {}

CompilerInvocationBase &
CompilerInvocationBase::deep_copy_assign(const CompilerInvocationBase &X) {
  if (this != &X) {
    LangOpts = make_shared_copy(X.getLangOpts());
    TargetOpts = make_shared_copy(X.getTargetOpts());
    DiagnosticOpts = makeIntrusiveRefCntCopy(X.getDiagnosticOpts());
    HSOpts = make_shared_copy(X.getHeaderSearchOpts());
    PPOpts = make_shared_copy(X.getPreprocessorOpts());
    AnalyzerOpts = makeIntrusiveRefCntCopy(X.getAnalyzerOpts());
    MigratorOpts = make_shared_copy(X.getMigratorOpts());
    APINotesOpts = make_shared_copy(X.getAPINotesOpts());
    CodeGenOpts = make_shared_copy(X.getCodeGenOpts());
    FSOpts = make_shared_copy(X.getFileSystemOpts());
    FrontendOpts = make_shared_copy(X.getFrontendOpts());
    DependencyOutputOpts = make_shared_copy(X.getDependencyOutputOpts());
    PreprocessorOutputOpts = make_shared_copy(X.getPreprocessorOutputOpts());
  }
  return *this;
}

CompilerInvocationBase &
CompilerInvocationBase::shallow_copy_assign(const CompilerInvocationBase &X) {
  if (this != &X) {
    LangOpts = X.LangOpts;
    TargetOpts = X.TargetOpts;
    DiagnosticOpts = X.DiagnosticOpts;
    HSOpts = X.HSOpts;
    PPOpts = X.PPOpts;
    AnalyzerOpts = X.AnalyzerOpts;
    MigratorOpts = X.MigratorOpts;
    APINotesOpts = X.APINotesOpts;
    CodeGenOpts = X.CodeGenOpts;
    FSOpts = X.FSOpts;
    FrontendOpts = X.FrontendOpts;
    DependencyOutputOpts = X.DependencyOutputOpts;
    PreprocessorOutputOpts = X.PreprocessorOutputOpts;
  }
  return *this;
}

CompilerInvocation::CompilerInvocation(const CowCompilerInvocation &X)
    : CompilerInvocationBase(EmptyConstructor{}) {
  CompilerInvocationBase::deep_copy_assign(X);
}

CompilerInvocation &
CompilerInvocation::operator=(const CowCompilerInvocation &X) {
  CompilerInvocationBase::deep_copy_assign(X);
  return *this;
}

namespace {
template <typename T>
T &ensureOwned(std::shared_ptr<T> &Storage) {
  if (Storage.use_count() > 1)
    Storage = std::make_shared<T>(*Storage);
  return *Storage;
}

template <typename T>
T &ensureOwned(llvm::IntrusiveRefCntPtr<T> &Storage) {
  if (Storage.useCount() > 1)
    Storage = llvm::makeIntrusiveRefCnt<T>(*Storage);
  return *Storage;
}
} // namespace

LangOptions &CowCompilerInvocation::getMutLangOpts() {
  return ensureOwned(LangOpts);
}

TargetOptions &CowCompilerInvocation::getMutTargetOpts() {
  return ensureOwned(TargetOpts);
}

DiagnosticOptions &CowCompilerInvocation::getMutDiagnosticOpts() {
  return ensureOwned(DiagnosticOpts);
}

HeaderSearchOptions &CowCompilerInvocation::getMutHeaderSearchOpts() {
  return ensureOwned(HSOpts);
}

PreprocessorOptions &CowCompilerInvocation::getMutPreprocessorOpts() {
  return ensureOwned(PPOpts);
}

AnalyzerOptions &CowCompilerInvocation::getMutAnalyzerOpts() {
  return ensureOwned(AnalyzerOpts);
}

MigratorOptions &CowCompilerInvocation::getMutMigratorOpts() {
  return ensureOwned(MigratorOpts);
}

APINotesOptions &CowCompilerInvocation::getMutAPINotesOpts() {
  return ensureOwned(APINotesOpts);
}

CodeGenOptions &CowCompilerInvocation::getMutCodeGenOpts() {
  return ensureOwned(CodeGenOpts);
}

FileSystemOptions &CowCompilerInvocation::getMutFileSystemOpts() {
  return ensureOwned(FSOpts);
}

FrontendOptions &CowCompilerInvocation::getMutFrontendOpts() {
  return ensureOwned(FrontendOpts);
}

DependencyOutputOptions &CowCompilerInvocation::getMutDependencyOutputOpts() {
  return ensureOwned(DependencyOutputOpts);
}

PreprocessorOutputOptions &
CowCompilerInvocation::getMutPreprocessorOutputOpts() {
  return ensureOwned(PreprocessorOutputOpts);
}

//===----------------------------------------------------------------------===//
// Normalizers
//===----------------------------------------------------------------------===//

using ArgumentConsumer = CompilerInvocation::ArgumentConsumer;

#define SIMPLE_ENUM_VALUE_TABLE
#include "clang/Driver/Options.inc"
#undef SIMPLE_ENUM_VALUE_TABLE

static std::optional<bool> normalizeSimpleFlag(OptSpecifier Opt,
                                               unsigned TableIndex,
                                               const ArgList &Args,
                                               DiagnosticsEngine &Diags) {
  if (Args.hasArg(Opt))
    return true;
  return std::nullopt;
}

static std::optional<bool> normalizeSimpleNegativeFlag(OptSpecifier Opt,
                                                       unsigned,
                                                       const ArgList &Args,
                                                       DiagnosticsEngine &) {
  if (Args.hasArg(Opt))
    return false;
  return std::nullopt;
}

/// The tblgen-erated code passes in a fifth parameter of an arbitrary type, but
/// denormalizeSimpleFlags never looks at it. Avoid bloating compile-time with
/// unnecessary template instantiations and just ignore it with a variadic
/// argument.
static void denormalizeSimpleFlag(ArgumentConsumer Consumer,
                                  const Twine &Spelling, Option::OptionClass,
                                  unsigned, /*T*/...) {
  Consumer(Spelling);
}

template <typename T> static constexpr bool is_uint64_t_convertible() {
  return !std::is_same_v<T, uint64_t> && llvm::is_integral_or_enum<T>::value;
}

template <typename T,
          std::enable_if_t<!is_uint64_t_convertible<T>(), bool> = false>
static auto makeFlagToValueNormalizer(T Value) {
  return [Value](OptSpecifier Opt, unsigned, const ArgList &Args,
                 DiagnosticsEngine &) -> std::optional<T> {
    if (Args.hasArg(Opt))
      return Value;
    return std::nullopt;
  };
}

template <typename T,
          std::enable_if_t<is_uint64_t_convertible<T>(), bool> = false>
static auto makeFlagToValueNormalizer(T Value) {
  return makeFlagToValueNormalizer(uint64_t(Value));
}

static auto makeBooleanOptionNormalizer(bool Value, bool OtherValue,
                                        OptSpecifier OtherOpt) {
  return [Value, OtherValue,
          OtherOpt](OptSpecifier Opt, unsigned, const ArgList &Args,
                    DiagnosticsEngine &) -> std::optional<bool> {
    if (const Arg *A = Args.getLastArg(Opt, OtherOpt)) {
      return A->getOption().matches(Opt) ? Value : OtherValue;
    }
    return std::nullopt;
  };
}

static auto makeBooleanOptionDenormalizer(bool Value) {
  return [Value](ArgumentConsumer Consumer, const Twine &Spelling,
                 Option::OptionClass, unsigned, bool KeyPath) {
    if (KeyPath == Value)
      Consumer(Spelling);
  };
}

static void denormalizeStringImpl(ArgumentConsumer Consumer,
                                  const Twine &Spelling,
                                  Option::OptionClass OptClass, unsigned,
                                  const Twine &Value) {
  switch (OptClass) {
  case Option::SeparateClass:
  case Option::JoinedOrSeparateClass:
  case Option::JoinedAndSeparateClass:
    Consumer(Spelling);
    Consumer(Value);
    break;
  case Option::JoinedClass:
  case Option::CommaJoinedClass:
    Consumer(Spelling + Value);
    break;
  default:
    llvm_unreachable("Cannot denormalize an option with option class "
                     "incompatible with string denormalization.");
  }
}

template <typename T>
static void denormalizeString(ArgumentConsumer Consumer, const Twine &Spelling,
                              Option::OptionClass OptClass, unsigned TableIndex,
                              T Value) {
  denormalizeStringImpl(Consumer, Spelling, OptClass, TableIndex, Twine(Value));
}

static std::optional<SimpleEnumValue>
findValueTableByName(const SimpleEnumValueTable &Table, StringRef Name) {
  for (int I = 0, E = Table.Size; I != E; ++I)
    if (Name == Table.Table[I].Name)
      return Table.Table[I];

  return std::nullopt;
}

static std::optional<SimpleEnumValue>
findValueTableByValue(const SimpleEnumValueTable &Table, unsigned Value) {
  for (int I = 0, E = Table.Size; I != E; ++I)
    if (Value == Table.Table[I].Value)
      return Table.Table[I];

  return std::nullopt;
}

static std::optional<unsigned> normalizeSimpleEnum(OptSpecifier Opt,
                                                   unsigned TableIndex,
                                                   const ArgList &Args,
                                                   DiagnosticsEngine &Diags) {
  assert(TableIndex < SimpleEnumValueTablesSize);
  const SimpleEnumValueTable &Table = SimpleEnumValueTables[TableIndex];

  auto *Arg = Args.getLastArg(Opt);
  if (!Arg)
    return std::nullopt;

  StringRef ArgValue = Arg->getValue();
  if (auto MaybeEnumVal = findValueTableByName(Table, ArgValue))
    return MaybeEnumVal->Value;

  Diags.Report(diag::err_drv_invalid_value)
      << Arg->getAsString(Args) << ArgValue;
  return std::nullopt;
}

static void denormalizeSimpleEnumImpl(ArgumentConsumer Consumer,
                                      const Twine &Spelling,
                                      Option::OptionClass OptClass,
                                      unsigned TableIndex, unsigned Value) {
  assert(TableIndex < SimpleEnumValueTablesSize);
  const SimpleEnumValueTable &Table = SimpleEnumValueTables[TableIndex];
  if (auto MaybeEnumVal = findValueTableByValue(Table, Value)) {
    denormalizeString(Consumer, Spelling, OptClass, TableIndex,
                      MaybeEnumVal->Name);
  } else {
    llvm_unreachable("The simple enum value was not correctly defined in "
                     "the tablegen option description");
  }
}

template <typename T>
static void denormalizeSimpleEnum(ArgumentConsumer Consumer,
                                  const Twine &Spelling,
                                  Option::OptionClass OptClass,
                                  unsigned TableIndex, T Value) {
  return denormalizeSimpleEnumImpl(Consumer, Spelling, OptClass, TableIndex,
                                   static_cast<unsigned>(Value));
}

static std::optional<std::string> normalizeString(OptSpecifier Opt,
                                                  int TableIndex,
                                                  const ArgList &Args,
                                                  DiagnosticsEngine &Diags) {
  auto *Arg = Args.getLastArg(Opt);
  if (!Arg)
    return std::nullopt;
  return std::string(Arg->getValue());
}

template <typename IntTy>
static std::optional<IntTy> normalizeStringIntegral(OptSpecifier Opt, int,
                                                    const ArgList &Args,
                                                    DiagnosticsEngine &Diags) {
  auto *Arg = Args.getLastArg(Opt);
  if (!Arg)
    return std::nullopt;
  IntTy Res;
  if (StringRef(Arg->getValue()).getAsInteger(0, Res)) {
    Diags.Report(diag::err_drv_invalid_int_value)
        << Arg->getAsString(Args) << Arg->getValue();
    return std::nullopt;
  }
  return Res;
}

static std::optional<std::vector<std::string>>
normalizeStringVector(OptSpecifier Opt, int, const ArgList &Args,
                      DiagnosticsEngine &) {
  return Args.getAllArgValues(Opt);
}

static void denormalizeStringVector(ArgumentConsumer Consumer,
                                    const Twine &Spelling,
                                    Option::OptionClass OptClass,
                                    unsigned TableIndex,
                                    const std::vector<std::string> &Values) {
  switch (OptClass) {
  case Option::CommaJoinedClass: {
    std::string CommaJoinedValue;
    if (!Values.empty()) {
      CommaJoinedValue.append(Values.front());
      for (const std::string &Value : llvm::drop_begin(Values, 1)) {
        CommaJoinedValue.append(",");
        CommaJoinedValue.append(Value);
      }
    }
    denormalizeString(Consumer, Spelling, Option::OptionClass::JoinedClass,
                      TableIndex, CommaJoinedValue);
    break;
  }
  case Option::JoinedClass:
  case Option::SeparateClass:
  case Option::JoinedOrSeparateClass:
    for (const std::string &Value : Values)
      denormalizeString(Consumer, Spelling, OptClass, TableIndex, Value);
    break;
  default:
    llvm_unreachable("Cannot denormalize an option with option class "
                     "incompatible with string vector denormalization.");
  }
}

static std::optional<std::string> normalizeTriple(OptSpecifier Opt,
                                                  int TableIndex,
                                                  const ArgList &Args,
                                                  DiagnosticsEngine &Diags) {
  auto *Arg = Args.getLastArg(Opt);
  if (!Arg)
    return std::nullopt;
  return llvm::Triple::normalize(Arg->getValue());
}

template <typename T, typename U>
static T mergeForwardValue(T KeyPath, U Value) {
  return static_cast<T>(Value);
}

template <typename T, typename U> static T mergeMaskValue(T KeyPath, U Value) {
  return KeyPath | Value;
}

template <typename T> static T extractForwardValue(T KeyPath) {
  return KeyPath;
}

template <typename T, typename U, U Value>
static T extractMaskValue(T KeyPath) {
  return ((KeyPath & Value) == Value) ? static_cast<T>(Value) : T();
}

#define PARSE_OPTION_WITH_MARSHALLING(                                         \
    ARGS, DIAGS, PREFIX_TYPE, SPELLING, ID, KIND, GROUP, ALIAS, ALIASARGS,     \
    FLAGS, VISIBILITY, PARAM, HELPTEXT, HELPTEXTSFORVARIANTS, METAVAR, VALUES, \
    SHOULD_PARSE, ALWAYS_EMIT, KEYPATH, DEFAULT_VALUE, IMPLIED_CHECK,          \
    IMPLIED_VALUE, NORMALIZER, DENORMALIZER, MERGER, EXTRACTOR, TABLE_INDEX)   \
  if ((VISIBILITY) & options::CC1Option) {                                     \
    KEYPATH = MERGER(KEYPATH, DEFAULT_VALUE);                                  \
    if (IMPLIED_CHECK)                                                         \
      KEYPATH = MERGER(KEYPATH, IMPLIED_VALUE);                                \
    if (SHOULD_PARSE)                                                          \
      if (auto MaybeValue = NORMALIZER(OPT_##ID, TABLE_INDEX, ARGS, DIAGS))    \
        KEYPATH =                                                              \
            MERGER(KEYPATH, static_cast<decltype(KEYPATH)>(*MaybeValue));      \
  }

// Capture the extracted value as a lambda argument to avoid potential issues
// with lifetime extension of the reference.
#define GENERATE_OPTION_WITH_MARSHALLING(                                      \
    CONSUMER, PREFIX_TYPE, SPELLING, ID, KIND, GROUP, ALIAS, ALIASARGS, FLAGS, \
    VISIBILITY, PARAM, HELPTEXT, HELPTEXTSFORVARIANTS, METAVAR, VALUES,        \
    SHOULD_PARSE, ALWAYS_EMIT, KEYPATH, DEFAULT_VALUE, IMPLIED_CHECK,          \
    IMPLIED_VALUE, NORMALIZER, DENORMALIZER, MERGER, EXTRACTOR, TABLE_INDEX)   \
  if ((VISIBILITY) & options::CC1Option) {                                     \
    [&](const auto &Extracted) {                                               \
      if (ALWAYS_EMIT ||                                                       \
          (Extracted !=                                                        \
           static_cast<decltype(KEYPATH)>((IMPLIED_CHECK) ? (IMPLIED_VALUE)    \
                                                          : (DEFAULT_VALUE)))) \
        DENORMALIZER(CONSUMER, SPELLING, Option::KIND##Class, TABLE_INDEX,     \
                     Extracted);                                               \
    }(EXTRACTOR(KEYPATH));                                                     \
  }

static StringRef GetInputKindName(InputKind IK);

static bool FixupInvocation(CompilerInvocation &Invocation,
                            DiagnosticsEngine &Diags, const ArgList &Args,
                            InputKind IK) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  LangOptions &LangOpts = Invocation.getLangOpts();
  CodeGenOptions &CodeGenOpts = Invocation.getCodeGenOpts();
  TargetOptions &TargetOpts = Invocation.getTargetOpts();
  FrontendOptions &FrontendOpts = Invocation.getFrontendOpts();
  CodeGenOpts.XRayInstrumentFunctions = LangOpts.XRayInstrument;
  CodeGenOpts.XRayAlwaysEmitCustomEvents = LangOpts.XRayAlwaysEmitCustomEvents;
  CodeGenOpts.XRayAlwaysEmitTypedEvents = LangOpts.XRayAlwaysEmitTypedEvents;
  CodeGenOpts.DisableFree = FrontendOpts.DisableFree;
  FrontendOpts.GenerateGlobalModuleIndex = FrontendOpts.UseGlobalModuleIndex;
  if (FrontendOpts.ShowStats)
    CodeGenOpts.ClearASTBeforeBackend = false;
  LangOpts.SanitizeCoverage = CodeGenOpts.hasSanitizeCoverage();
  LangOpts.ForceEmitVTables = CodeGenOpts.ForceEmitVTables;
  LangOpts.SpeculativeLoadHardening = CodeGenOpts.SpeculativeLoadHardening;
  LangOpts.CurrentModule = LangOpts.ModuleName;

  llvm::Triple T(TargetOpts.Triple);
  llvm::Triple::ArchType Arch = T.getArch();

  CodeGenOpts.CodeModel = TargetOpts.CodeModel;
  CodeGenOpts.LargeDataThreshold = TargetOpts.LargeDataThreshold;

  if (LangOpts.getExceptionHandling() !=
          LangOptions::ExceptionHandlingKind::None &&
      T.isWindowsMSVCEnvironment())
    Diags.Report(diag::err_fe_invalid_exception_model)
        << static_cast<unsigned>(LangOpts.getExceptionHandling()) << T.str();

  if (LangOpts.AppleKext && !LangOpts.CPlusPlus)
    Diags.Report(diag::warn_c_kext);

  if (LangOpts.NewAlignOverride &&
      !llvm::isPowerOf2_32(LangOpts.NewAlignOverride)) {
    Arg *A = Args.getLastArg(OPT_fnew_alignment_EQ);
    Diags.Report(diag::err_fe_invalid_alignment)
        << A->getAsString(Args) << A->getValue();
    LangOpts.NewAlignOverride = 0;
  }

  // The -f[no-]raw-string-literals option is only valid in C and in C++
  // standards before C++11.
  if (LangOpts.CPlusPlus11) {
    if (Args.hasArg(OPT_fraw_string_literals, OPT_fno_raw_string_literals)) {
      Args.claimAllArgs(OPT_fraw_string_literals, OPT_fno_raw_string_literals);
      Diags.Report(diag::warn_drv_fraw_string_literals_in_cxx11)
          << bool(LangOpts.RawStringLiterals);
    }

    // Do not allow disabling raw string literals in C++11 or later.
    LangOpts.RawStringLiterals = true;
  }

  // Prevent the user from specifying both -fsycl-is-device and -fsycl-is-host.
  if (LangOpts.SYCLIsDevice && LangOpts.SYCLIsHost)
    Diags.Report(diag::err_drv_argument_not_allowed_with) << "-fsycl-is-device"
                                                          << "-fsycl-is-host";

  if (Args.hasArg(OPT_fgnu89_inline) && LangOpts.CPlusPlus)
    Diags.Report(diag::err_drv_argument_not_allowed_with)
        << "-fgnu89-inline" << GetInputKindName(IK);

  if (Args.hasArg(OPT_hlsl_entrypoint) && !LangOpts.HLSL)
    Diags.Report(diag::err_drv_argument_not_allowed_with)
        << "-hlsl-entry" << GetInputKindName(IK);

  if (Args.hasArg(OPT_fgpu_allow_device_init) && !LangOpts.HIP)
    Diags.Report(diag::warn_ignored_hip_only_option)
        << Args.getLastArg(OPT_fgpu_allow_device_init)->getAsString(Args);

  if (Args.hasArg(OPT_gpu_max_threads_per_block_EQ) && !LangOpts.HIP)
    Diags.Report(diag::warn_ignored_hip_only_option)
        << Args.getLastArg(OPT_gpu_max_threads_per_block_EQ)->getAsString(Args);

  // When these options are used, the compiler is allowed to apply
  // optimizations that may affect the final result. For example
  // (x+y)+z is transformed to x+(y+z) but may not give the same
  // final result; it's not value safe.
  // Another example can be to simplify x/x to 1.0 but x could be 0.0, INF
  // or NaN. Final result may then differ. An error is issued when the eval
  // method is set with one of these options.
  if (Args.hasArg(OPT_ffp_eval_method_EQ)) {
    if (LangOpts.ApproxFunc)
      Diags.Report(diag::err_incompatible_fp_eval_method_options) << 0;
    if (LangOpts.AllowFPReassoc)
      Diags.Report(diag::err_incompatible_fp_eval_method_options) << 1;
    if (LangOpts.AllowRecip)
      Diags.Report(diag::err_incompatible_fp_eval_method_options) << 2;
  }

  // -cl-strict-aliasing needs to emit diagnostic in the case where CL > 1.0.
  // This option should be deprecated for CL > 1.0 because
  // this option was added for compatibility with OpenCL 1.0.
  if (Args.getLastArg(OPT_cl_strict_aliasing) &&
      (LangOpts.getOpenCLCompatibleVersion() > 100))
    Diags.Report(diag::warn_option_invalid_ocl_version)
        << LangOpts.getOpenCLVersionString()
        << Args.getLastArg(OPT_cl_strict_aliasing)->getAsString(Args);

  if (Arg *A = Args.getLastArg(OPT_fdefault_calling_conv_EQ)) {
    auto DefaultCC = LangOpts.getDefaultCallingConv();

    bool emitError = (DefaultCC == LangOptions::DCC_FastCall ||
                      DefaultCC == LangOptions::DCC_StdCall) &&
                     Arch != llvm::Triple::x86;
    emitError |= (DefaultCC == LangOptions::DCC_VectorCall ||
                  DefaultCC == LangOptions::DCC_RegCall) &&
                 !T.isX86();
    emitError |= DefaultCC == LangOptions::DCC_RtdCall && Arch != llvm::Triple::m68k;
    if (emitError)
      Diags.Report(diag::err_drv_argument_not_allowed_with)
          << A->getSpelling() << T.getTriple();
  }

  return Diags.getNumErrors() == NumErrorsBefore;
}

//===----------------------------------------------------------------------===//
// Deserialization (from args)
//===----------------------------------------------------------------------===//

static unsigned getOptimizationLevel(ArgList &Args, InputKind IK,
                                     DiagnosticsEngine &Diags) {
  unsigned DefaultOpt = 0;
  if ((IK.getLanguage() == Language::OpenCL ||
       IK.getLanguage() == Language::OpenCLCXX) &&
      !Args.hasArg(OPT_cl_opt_disable))
    DefaultOpt = 2;

  if (Arg *A = Args.getLastArg(options::OPT_O_Group)) {
    if (A->getOption().matches(options::OPT_O0))
      return 0;

    if (A->getOption().matches(options::OPT_Ofast))
      return 3;

    assert(A->getOption().matches(options::OPT_O));

    StringRef S(A->getValue());
    if (S == "s" || S == "z")
      return 2;

    if (S == "g")
      return 1;

    return getLastArgIntValue(Args, OPT_O, DefaultOpt, Diags);
  }

  return DefaultOpt;
}

static unsigned getOptimizationLevelSize(ArgList &Args) {
  if (Arg *A = Args.getLastArg(options::OPT_O_Group)) {
    if (A->getOption().matches(options::OPT_O)) {
      switch (A->getValue()[0]) {
      default:
        return 0;
      case 's':
        return 1;
      case 'z':
        return 2;
      }
    }
  }
  return 0;
}

static void GenerateArg(ArgumentConsumer Consumer,
                        llvm::opt::OptSpecifier OptSpecifier) {
  Option Opt = getDriverOptTable().getOption(OptSpecifier);
  denormalizeSimpleFlag(Consumer, Opt.getPrefixedName(),
                        Option::OptionClass::FlagClass, 0);
}

static void GenerateArg(ArgumentConsumer Consumer,
                        llvm::opt::OptSpecifier OptSpecifier,
                        const Twine &Value) {
  Option Opt = getDriverOptTable().getOption(OptSpecifier);
  denormalizeString(Consumer, Opt.getPrefixedName(), Opt.getKind(), 0, Value);
}

// Parse command line arguments into CompilerInvocation.
using ParseFn =
    llvm::function_ref<bool(CompilerInvocation &, ArrayRef<const char *>,
                            DiagnosticsEngine &, const char *)>;

// Generate command line arguments from CompilerInvocation.
using GenerateFn = llvm::function_ref<void(
    CompilerInvocation &, SmallVectorImpl<const char *> &,
    CompilerInvocation::StringAllocator)>;

/// May perform round-trip of command line arguments. By default, the round-trip
/// is enabled in assert builds. This can be overwritten at run-time via the
/// "-round-trip-args" and "-no-round-trip-args" command line flags, or via the
/// ForceRoundTrip parameter.
///
/// During round-trip, the command line arguments are parsed into a dummy
/// CompilerInvocation, which is used to generate the command line arguments
/// again. The real CompilerInvocation is then created by parsing the generated
/// arguments, not the original ones. This (in combination with tests covering
/// argument behavior) ensures the generated command line is complete (doesn't
/// drop/mangle any arguments).
///
/// Finally, we check the command line that was used to create the real
/// CompilerInvocation instance. By default, we compare it to the command line
/// the real CompilerInvocation generates. This checks whether the generator is
/// deterministic. If \p CheckAgainstOriginalInvocation is enabled, we instead
/// compare it to the original command line to verify the original command-line
/// was canonical and can round-trip exactly.
static bool RoundTrip(ParseFn Parse, GenerateFn Generate,
                      CompilerInvocation &RealInvocation,
                      CompilerInvocation &DummyInvocation,
                      ArrayRef<const char *> CommandLineArgs,
                      DiagnosticsEngine &Diags, const char *Argv0,
                      bool CheckAgainstOriginalInvocation = false,
                      bool ForceRoundTrip = false) {
#ifndef NDEBUG
  bool DoRoundTripDefault = true;
#else
  bool DoRoundTripDefault = false;
#endif

  bool DoRoundTrip = DoRoundTripDefault;
  if (ForceRoundTrip) {
    DoRoundTrip = true;
  } else {
    for (const auto *Arg : CommandLineArgs) {
      if (Arg == StringRef("-round-trip-args"))
        DoRoundTrip = true;
      if (Arg == StringRef("-no-round-trip-args"))
        DoRoundTrip = false;
    }
  }

  // If round-trip was not requested, simply run the parser with the real
  // invocation diagnostics.
  if (!DoRoundTrip)
    return Parse(RealInvocation, CommandLineArgs, Diags, Argv0);

  // Serializes quoted (and potentially escaped) arguments.
  auto SerializeArgs = [](ArrayRef<const char *> Args) {
    std::string Buffer;
    llvm::raw_string_ostream OS(Buffer);
    for (const char *Arg : Args) {
      llvm::sys::printArg(OS, Arg, /*Quote=*/true);
      OS << ' ';
    }
    OS.flush();
    return Buffer;
  };

  // Setup a dummy DiagnosticsEngine.
  DiagnosticsEngine DummyDiags(new DiagnosticIDs(), new DiagnosticOptions());
  DummyDiags.setClient(new TextDiagnosticBuffer());

  // Run the first parse on the original arguments with the dummy invocation and
  // diagnostics.
  if (!Parse(DummyInvocation, CommandLineArgs, DummyDiags, Argv0) ||
      DummyDiags.getNumWarnings() != 0) {
    // If the first parse did not succeed, it must be user mistake (invalid
    // command line arguments). We won't be able to generate arguments that
    // would reproduce the same result. Let's fail again with the real
    // invocation and diagnostics, so all side-effects of parsing are visible.
    unsigned NumWarningsBefore = Diags.getNumWarnings();
    auto Success = Parse(RealInvocation, CommandLineArgs, Diags, Argv0);
    if (!Success || Diags.getNumWarnings() != NumWarningsBefore)
      return Success;

    // Parse with original options and diagnostics succeeded even though it
    // shouldn't have. Something is off.
    Diags.Report(diag::err_cc1_round_trip_fail_then_ok);
    Diags.Report(diag::note_cc1_round_trip_original)
        << SerializeArgs(CommandLineArgs);
    return false;
  }

  // Setup string allocator.
  llvm::BumpPtrAllocator Alloc;
  llvm::StringSaver StringPool(Alloc);
  auto SA = [&StringPool](const Twine &Arg) {
    return StringPool.save(Arg).data();
  };

  // Generate arguments from the dummy invocation. If Generate is the
  // inverse of Parse, the newly generated arguments must have the same
  // semantics as the original.
  SmallVector<const char *> GeneratedArgs;
  Generate(DummyInvocation, GeneratedArgs, SA);

  // Run the second parse, now on the generated arguments, and with the real
  // invocation and diagnostics. The result is what we will end up using for the
  // rest of compilation, so if Generate is not inverse of Parse, something down
  // the line will break.
  bool Success2 = Parse(RealInvocation, GeneratedArgs, Diags, Argv0);

  // The first parse on original arguments succeeded, but second parse of
  // generated arguments failed. Something must be wrong with the generator.
  if (!Success2) {
    Diags.Report(diag::err_cc1_round_trip_ok_then_fail);
    Diags.Report(diag::note_cc1_round_trip_generated)
        << 1 << SerializeArgs(GeneratedArgs);
    return false;
  }

  SmallVector<const char *> ComparisonArgs;
  if (CheckAgainstOriginalInvocation)
    // Compare against original arguments.
    ComparisonArgs.assign(CommandLineArgs.begin(), CommandLineArgs.end());
  else
    // Generate arguments again, this time from the options we will end up using
    // for the rest of the compilation.
    Generate(RealInvocation, ComparisonArgs, SA);

  // Compares two lists of arguments.
  auto Equal = [](const ArrayRef<const char *> A,
                  const ArrayRef<const char *> B) {
    return std::equal(A.begin(), A.end(), B.begin(), B.end(),
                      [](const char *AElem, const char *BElem) {
                        return StringRef(AElem) == StringRef(BElem);
                      });
  };

  // If we generated different arguments from what we assume are two
  // semantically equivalent CompilerInvocations, the Generate function may
  // be non-deterministic.
  if (!Equal(GeneratedArgs, ComparisonArgs)) {
    Diags.Report(diag::err_cc1_round_trip_mismatch);
    Diags.Report(diag::note_cc1_round_trip_generated)
        << 1 << SerializeArgs(GeneratedArgs);
    Diags.Report(diag::note_cc1_round_trip_generated)
        << 2 << SerializeArgs(ComparisonArgs);
    return false;
  }

  Diags.Report(diag::remark_cc1_round_trip_generated)
      << 1 << SerializeArgs(GeneratedArgs);
  Diags.Report(diag::remark_cc1_round_trip_generated)
      << 2 << SerializeArgs(ComparisonArgs);

  return Success2;
}

bool CompilerInvocation::checkCC1RoundTrip(ArrayRef<const char *> Args,
                                           DiagnosticsEngine &Diags,
                                           const char *Argv0) {
  CompilerInvocation DummyInvocation1, DummyInvocation2;
  return RoundTrip(
      [](CompilerInvocation &Invocation, ArrayRef<const char *> CommandLineArgs,
         DiagnosticsEngine &Diags, const char *Argv0) {
        return CreateFromArgsImpl(Invocation, CommandLineArgs, Diags, Argv0);
      },
      [](CompilerInvocation &Invocation, SmallVectorImpl<const char *> &Args,
         StringAllocator SA) {
        Args.push_back("-cc1");
        Invocation.generateCC1CommandLine(Args, SA);
      },
      DummyInvocation1, DummyInvocation2, Args, Diags, Argv0,
      /*CheckAgainstOriginalInvocation=*/true, /*ForceRoundTrip=*/true);
}

static void addDiagnosticArgs(ArgList &Args, OptSpecifier Group,
                              OptSpecifier GroupWithValue,
                              std::vector<std::string> &Diagnostics) {
  for (auto *A : Args.filtered(Group)) {
    if (A->getOption().getKind() == Option::FlagClass) {
      // The argument is a pure flag (such as OPT_Wall or OPT_Wdeprecated). Add
      // its name (minus the "W" or "R" at the beginning) to the diagnostics.
      Diagnostics.push_back(
          std::string(A->getOption().getName().drop_front(1)));
    } else if (A->getOption().matches(GroupWithValue)) {
      // This is -Wfoo= or -Rfoo=, where foo is the name of the diagnostic
      // group. Add only the group name to the diagnostics.
      Diagnostics.push_back(
          std::string(A->getOption().getName().drop_front(1).rtrim("=-")));
    } else {
      // Otherwise, add its value (for OPT_W_Joined and similar).
      Diagnostics.push_back(A->getValue());
    }
  }
}

// Parse the Static Analyzer configuration. If \p Diags is set to nullptr,
// it won't verify the input.
static void parseAnalyzerConfigs(AnalyzerOptions &AnOpts,
                                 DiagnosticsEngine *Diags);

static void getAllNoBuiltinFuncValues(ArgList &Args,
                                      std::vector<std::string> &Funcs) {
  std::vector<std::string> Values = Args.getAllArgValues(OPT_fno_builtin_);
  auto BuiltinEnd = llvm::partition(Values, Builtin::Context::isBuiltinFunc);
  Funcs.insert(Funcs.end(), Values.begin(), BuiltinEnd);
}

static void GenerateAnalyzerArgs(const AnalyzerOptions &Opts,
                                 ArgumentConsumer Consumer) {
  const AnalyzerOptions *AnalyzerOpts = &Opts;

#define ANALYZER_OPTION_WITH_MARSHALLING(...)                                  \
  GENERATE_OPTION_WITH_MARSHALLING(Consumer, __VA_ARGS__)
#include "clang/Driver/Options.inc"
#undef ANALYZER_OPTION_WITH_MARSHALLING

  if (Opts.AnalysisConstraintsOpt != RangeConstraintsModel) {
    switch (Opts.AnalysisConstraintsOpt) {
#define ANALYSIS_CONSTRAINTS(NAME, CMDFLAG, DESC, CREATFN)                     \
  case NAME##Model:                                                            \
    GenerateArg(Consumer, OPT_analyzer_constraints, CMDFLAG);                  \
    break;
#include "clang/StaticAnalyzer/Core/Analyses.def"
    default:
      llvm_unreachable("Tried to generate unknown analysis constraint.");
    }
  }

  if (Opts.AnalysisDiagOpt != PD_HTML) {
    switch (Opts.AnalysisDiagOpt) {
#define ANALYSIS_DIAGNOSTICS(NAME, CMDFLAG, DESC, CREATFN)                     \
  case PD_##NAME:                                                              \
    GenerateArg(Consumer, OPT_analyzer_output, CMDFLAG);                       \
    break;
#include "clang/StaticAnalyzer/Core/Analyses.def"
    default:
      llvm_unreachable("Tried to generate unknown analysis diagnostic client.");
    }
  }

  if (Opts.AnalysisPurgeOpt != PurgeStmt) {
    switch (Opts.AnalysisPurgeOpt) {
#define ANALYSIS_PURGE(NAME, CMDFLAG, DESC)                                    \
  case NAME:                                                                   \
    GenerateArg(Consumer, OPT_analyzer_purge, CMDFLAG);                        \
    break;
#include "clang/StaticAnalyzer/Core/Analyses.def"
    default:
      llvm_unreachable("Tried to generate unknown analysis purge mode.");
    }
  }

  if (Opts.InliningMode != NoRedundancy) {
    switch (Opts.InliningMode) {
#define ANALYSIS_INLINING_MODE(NAME, CMDFLAG, DESC)                            \
  case NAME:                                                                   \
    GenerateArg(Consumer, OPT_analyzer_inlining_mode, CMDFLAG);                \
    break;
#include "clang/StaticAnalyzer/Core/Analyses.def"
    default:
      llvm_unreachable("Tried to generate unknown analysis inlining mode.");
    }
  }

  for (const auto &CP : Opts.CheckersAndPackages) {
    OptSpecifier Opt =
        CP.second ? OPT_analyzer_checker : OPT_analyzer_disable_checker;
    GenerateArg(Consumer, Opt, CP.first);
  }

  AnalyzerOptions ConfigOpts;
  parseAnalyzerConfigs(ConfigOpts, nullptr);

  // Sort options by key to avoid relying on StringMap iteration order.
  SmallVector<std::pair<StringRef, StringRef>, 4> SortedConfigOpts;
  for (const auto &C : Opts.Config)
    SortedConfigOpts.emplace_back(C.getKey(), C.getValue());
  llvm::sort(SortedConfigOpts, llvm::less_first());

  for (const auto &[Key, Value] : SortedConfigOpts) {
    // Don't generate anything that came from parseAnalyzerConfigs. It would be
    // redundant and may not be valid on the command line.
    auto Entry = ConfigOpts.Config.find(Key);
    if (Entry != ConfigOpts.Config.end() && Entry->getValue() == Value)
      continue;

    GenerateArg(Consumer, OPT_analyzer_config, Key + "=" + Value);
  }

  // Nothing to generate for FullCompilerInvocation.
}

static bool ParseAnalyzerArgs(AnalyzerOptions &Opts, ArgList &Args,
                              DiagnosticsEngine &Diags) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  AnalyzerOptions *AnalyzerOpts = &Opts;

#define ANALYZER_OPTION_WITH_MARSHALLING(...)                                  \
  PARSE_OPTION_WITH_MARSHALLING(Args, Diags, __VA_ARGS__)
#include "clang/Driver/Options.inc"
#undef ANALYZER_OPTION_WITH_MARSHALLING

  if (Arg *A = Args.getLastArg(OPT_analyzer_constraints)) {
    StringRef Name = A->getValue();
    AnalysisConstraints Value = llvm::StringSwitch<AnalysisConstraints>(Name)
#define ANALYSIS_CONSTRAINTS(NAME, CMDFLAG, DESC, CREATFN) \
      .Case(CMDFLAG, NAME##Model)
#include "clang/StaticAnalyzer/Core/Analyses.def"
      .Default(NumConstraints);
    if (Value == NumConstraints) {
      Diags.Report(diag::err_drv_invalid_value)
        << A->getAsString(Args) << Name;
    } else {
#ifndef LLVM_WITH_Z3
      if (Value == AnalysisConstraints::Z3ConstraintsModel) {
        Diags.Report(diag::err_analyzer_not_built_with_z3);
      }
#endif // LLVM_WITH_Z3
      Opts.AnalysisConstraintsOpt = Value;
    }
  }

  if (Arg *A = Args.getLastArg(OPT_analyzer_output)) {
    StringRef Name = A->getValue();
    AnalysisDiagClients Value = llvm::StringSwitch<AnalysisDiagClients>(Name)
#define ANALYSIS_DIAGNOSTICS(NAME, CMDFLAG, DESC, CREATFN) \
      .Case(CMDFLAG, PD_##NAME)
#include "clang/StaticAnalyzer/Core/Analyses.def"
      .Default(NUM_ANALYSIS_DIAG_CLIENTS);
    if (Value == NUM_ANALYSIS_DIAG_CLIENTS) {
      Diags.Report(diag::err_drv_invalid_value)
        << A->getAsString(Args) << Name;
    } else {
      Opts.AnalysisDiagOpt = Value;
    }
  }

  if (Arg *A = Args.getLastArg(OPT_analyzer_purge)) {
    StringRef Name = A->getValue();
    AnalysisPurgeMode Value = llvm::StringSwitch<AnalysisPurgeMode>(Name)
#define ANALYSIS_PURGE(NAME, CMDFLAG, DESC) \
      .Case(CMDFLAG, NAME)
#include "clang/StaticAnalyzer/Core/Analyses.def"
      .Default(NumPurgeModes);
    if (Value == NumPurgeModes) {
      Diags.Report(diag::err_drv_invalid_value)
        << A->getAsString(Args) << Name;
    } else {
      Opts.AnalysisPurgeOpt = Value;
    }
  }

  if (Arg *A = Args.getLastArg(OPT_analyzer_inlining_mode)) {
    StringRef Name = A->getValue();
    AnalysisInliningMode Value = llvm::StringSwitch<AnalysisInliningMode>(Name)
#define ANALYSIS_INLINING_MODE(NAME, CMDFLAG, DESC) \
      .Case(CMDFLAG, NAME)
#include "clang/StaticAnalyzer/Core/Analyses.def"
      .Default(NumInliningModes);
    if (Value == NumInliningModes) {
      Diags.Report(diag::err_drv_invalid_value)
        << A->getAsString(Args) << Name;
    } else {
      Opts.InliningMode = Value;
    }
  }

  Opts.CheckersAndPackages.clear();
  for (const Arg *A :
       Args.filtered(OPT_analyzer_checker, OPT_analyzer_disable_checker)) {
    A->claim();
    bool IsEnabled = A->getOption().getID() == OPT_analyzer_checker;
    // We can have a list of comma separated checker names, e.g:
    // '-analyzer-checker=cocoa,unix'
    StringRef CheckerAndPackageList = A->getValue();
    SmallVector<StringRef, 16> CheckersAndPackages;
    CheckerAndPackageList.split(CheckersAndPackages, ",");
    for (const StringRef &CheckerOrPackage : CheckersAndPackages)
      Opts.CheckersAndPackages.emplace_back(std::string(CheckerOrPackage),
                                            IsEnabled);
  }

  // Go through the analyzer configuration options.
  for (const auto *A : Args.filtered(OPT_analyzer_config)) {

    // We can have a list of comma separated config names, e.g:
    // '-analyzer-config key1=val1,key2=val2'
    StringRef configList = A->getValue();
    SmallVector<StringRef, 4> configVals;
    configList.split(configVals, ",");
    for (const auto &configVal : configVals) {
      StringRef key, val;
      std::tie(key, val) = configVal.split("=");
      if (val.empty()) {
        Diags.Report(SourceLocation(),
                     diag::err_analyzer_config_no_value) << configVal;
        break;
      }
      if (val.contains('=')) {
        Diags.Report(SourceLocation(),
                     diag::err_analyzer_config_multiple_values)
          << configVal;
        break;
      }

      // TODO: Check checker options too, possibly in CheckerRegistry.
      // Leave unknown non-checker configs unclaimed.
      if (!key.contains(":") && Opts.isUnknownAnalyzerConfig(key)) {
        if (Opts.ShouldEmitErrorsOnInvalidConfigValue)
          Diags.Report(diag::err_analyzer_config_unknown) << key;
        continue;
      }

      A->claim();
      Opts.Config[key] = std::string(val);
    }
  }

  if (Opts.ShouldEmitErrorsOnInvalidConfigValue)
    parseAnalyzerConfigs(Opts, &Diags);
  else
    parseAnalyzerConfigs(Opts, nullptr);

  llvm::raw_string_ostream os(Opts.FullCompilerInvocation);
  for (unsigned i = 0; i < Args.getNumInputArgStrings(); ++i) {
    if (i != 0)
      os << " ";
    os << Args.getArgString(i);
  }
  os.flush();

  return Diags.getNumErrors() == NumErrorsBefore;
}

static StringRef getStringOption(AnalyzerOptions::ConfigTable &Config,
                                 StringRef OptionName, StringRef DefaultVal) {
  return Config.insert({OptionName, std::string(DefaultVal)}).first->second;
}

static void initOption(AnalyzerOptions::ConfigTable &Config,
                       DiagnosticsEngine *Diags,
                       StringRef &OptionField, StringRef Name,
                       StringRef DefaultVal) {
  // String options may be known to invalid (e.g. if the expected string is a
  // file name, but the file does not exist), those will have to be checked in
  // parseConfigs.
  OptionField = getStringOption(Config, Name, DefaultVal);
}

static void initOption(AnalyzerOptions::ConfigTable &Config,
                       DiagnosticsEngine *Diags,
                       bool &OptionField, StringRef Name, bool DefaultVal) {
  auto PossiblyInvalidVal =
      llvm::StringSwitch<std::optional<bool>>(
          getStringOption(Config, Name, (DefaultVal ? "true" : "false")))
          .Case("true", true)
          .Case("false", false)
          .Default(std::nullopt);

  if (!PossiblyInvalidVal) {
    if (Diags)
      Diags->Report(diag::err_analyzer_config_invalid_input)
        << Name << "a boolean";
    else
      OptionField = DefaultVal;
  } else
    OptionField = *PossiblyInvalidVal;
}

static void initOption(AnalyzerOptions::ConfigTable &Config,
                       DiagnosticsEngine *Diags,
                       unsigned &OptionField, StringRef Name,
                       unsigned DefaultVal) {

  OptionField = DefaultVal;
  bool HasFailed = getStringOption(Config, Name, std::to_string(DefaultVal))
                     .getAsInteger(0, OptionField);
  if (Diags && HasFailed)
    Diags->Report(diag::err_analyzer_config_invalid_input)
      << Name << "an unsigned";
}

static void parseAnalyzerConfigs(AnalyzerOptions &AnOpts,
                                 DiagnosticsEngine *Diags) {
  // TODO: There's no need to store the entire configtable, it'd be plenty
  // enough to store checker options.

#define ANALYZER_OPTION(TYPE, NAME, CMDFLAG, DESC, DEFAULT_VAL)                \
  initOption(AnOpts.Config, Diags, AnOpts.NAME, CMDFLAG, DEFAULT_VAL);
#define ANALYZER_OPTION_DEPENDS_ON_USER_MODE(...)
#include "clang/StaticAnalyzer/Core/AnalyzerOptions.def"

  assert(AnOpts.UserMode == "shallow" || AnOpts.UserMode == "deep");
  const bool InShallowMode = AnOpts.UserMode == "shallow";

#define ANALYZER_OPTION(...)
#define ANALYZER_OPTION_DEPENDS_ON_USER_MODE(TYPE, NAME, CMDFLAG, DESC,        \
                                             SHALLOW_VAL, DEEP_VAL)            \
  initOption(AnOpts.Config, Diags, AnOpts.NAME, CMDFLAG,                       \
             InShallowMode ? SHALLOW_VAL : DEEP_VAL);
#include "clang/StaticAnalyzer/Core/AnalyzerOptions.def"

  // At this point, AnalyzerOptions is configured. Let's validate some options.

  // FIXME: Here we try to validate the silenced checkers or packages are valid.
  // The current approach only validates the registered checkers which does not
  // contain the runtime enabled checkers and optimally we would validate both.
  if (!AnOpts.RawSilencedCheckersAndPackages.empty()) {
    std::vector<StringRef> Checkers =
        AnOpts.getRegisteredCheckers(/*IncludeExperimental=*/true);
    std::vector<StringRef> Packages =
        AnOpts.getRegisteredPackages(/*IncludeExperimental=*/true);

    SmallVector<StringRef, 16> CheckersAndPackages;
    AnOpts.RawSilencedCheckersAndPackages.split(CheckersAndPackages, ";");

    for (const StringRef &CheckerOrPackage : CheckersAndPackages) {
      if (Diags) {
        bool IsChecker = CheckerOrPackage.contains('.');
        bool IsValidName = IsChecker
                               ? llvm::is_contained(Checkers, CheckerOrPackage)
                               : llvm::is_contained(Packages, CheckerOrPackage);

        if (!IsValidName)
          Diags->Report(diag::err_unknown_analyzer_checker_or_package)
              << CheckerOrPackage;
      }

      AnOpts.SilencedCheckersAndPackages.emplace_back(CheckerOrPackage);
    }
  }

  if (!Diags)
    return;

  if (AnOpts.ShouldTrackConditionsDebug && !AnOpts.ShouldTrackConditions)
    Diags->Report(diag::err_analyzer_config_invalid_input)
        << "track-conditions-debug" << "'track-conditions' to also be enabled";

  if (!AnOpts.CTUDir.empty() && !llvm::sys::fs::is_directory(AnOpts.CTUDir))
    Diags->Report(diag::err_analyzer_config_invalid_input) << "ctu-dir"
                                                           << "a filename";

  if (!AnOpts.ModelPath.empty() &&
      !llvm::sys::fs::is_directory(AnOpts.ModelPath))
    Diags->Report(diag::err_analyzer_config_invalid_input) << "model-path"
                                                           << "a filename";
}

/// Generate a remark argument. This is an inverse of `ParseOptimizationRemark`.
static void
GenerateOptimizationRemark(ArgumentConsumer Consumer, OptSpecifier OptEQ,
                           StringRef Name,
                           const CodeGenOptions::OptRemark &Remark) {
  if (Remark.hasValidPattern()) {
    GenerateArg(Consumer, OptEQ, Remark.Pattern);
  } else if (Remark.Kind == CodeGenOptions::RK_Enabled) {
    GenerateArg(Consumer, OPT_R_Joined, Name);
  } else if (Remark.Kind == CodeGenOptions::RK_Disabled) {
    GenerateArg(Consumer, OPT_R_Joined, StringRef("no-") + Name);
  }
}

/// Parse a remark command line argument. It may be missing, disabled/enabled by
/// '-R[no-]group' or specified with a regular expression by '-Rgroup=regexp'.
/// On top of that, it can be disabled/enabled globally by '-R[no-]everything'.
static CodeGenOptions::OptRemark
ParseOptimizationRemark(DiagnosticsEngine &Diags, ArgList &Args,
                        OptSpecifier OptEQ, StringRef Name) {
  CodeGenOptions::OptRemark Result;

  auto InitializeResultPattern = [&Diags, &Args, &Result](const Arg *A,
                                                          StringRef Pattern) {
    Result.Pattern = Pattern.str();

    std::string RegexError;
    Result.Regex = std::make_shared<llvm::Regex>(Result.Pattern);
    if (!Result.Regex->isValid(RegexError)) {
      Diags.Report(diag::err_drv_optimization_remark_pattern)
          << RegexError << A->getAsString(Args);
      return false;
    }

    return true;
  };

  for (Arg *A : Args) {
    if (A->getOption().matches(OPT_R_Joined)) {
      StringRef Value = A->getValue();

      if (Value == Name)
        Result.Kind = CodeGenOptions::RK_Enabled;
      else if (Value == "everything")
        Result.Kind = CodeGenOptions::RK_EnabledEverything;
      else if (Value.split('-') == std::make_pair(StringRef("no"), Name))
        Result.Kind = CodeGenOptions::RK_Disabled;
      else if (Value == "no-everything")
        Result.Kind = CodeGenOptions::RK_DisabledEverything;
      else
        continue;

      if (Result.Kind == CodeGenOptions::RK_Disabled ||
          Result.Kind == CodeGenOptions::RK_DisabledEverything) {
        Result.Pattern = "";
        Result.Regex = nullptr;
      } else {
        InitializeResultPattern(A, ".*");
      }
    } else if (A->getOption().matches(OptEQ)) {
      Result.Kind = CodeGenOptions::RK_WithPattern;
      if (!InitializeResultPattern(A, A->getValue()))
        return CodeGenOptions::OptRemark();
    }
  }

  return Result;
}

static bool parseDiagnosticLevelMask(StringRef FlagName,
                                     const std::vector<std::string> &Levels,
                                     DiagnosticsEngine &Diags,
                                     DiagnosticLevelMask &M) {
  bool Success = true;
  for (const auto &Level : Levels) {
    DiagnosticLevelMask const PM =
      llvm::StringSwitch<DiagnosticLevelMask>(Level)
        .Case("note",    DiagnosticLevelMask::Note)
        .Case("remark",  DiagnosticLevelMask::Remark)
        .Case("warning", DiagnosticLevelMask::Warning)
        .Case("error",   DiagnosticLevelMask::Error)
        .Default(DiagnosticLevelMask::None);
    if (PM == DiagnosticLevelMask::None) {
      Success = false;
      Diags.Report(diag::err_drv_invalid_value) << FlagName << Level;
    }
    M = M | PM;
  }
  return Success;
}

static void parseSanitizerKinds(StringRef FlagName,
                                const std::vector<std::string> &Sanitizers,
                                DiagnosticsEngine &Diags, SanitizerSet &S) {
  for (const auto &Sanitizer : Sanitizers) {
    SanitizerMask K = parseSanitizerValue(Sanitizer, /*AllowGroups=*/false);
    if (K == SanitizerMask())
      Diags.Report(diag::err_drv_invalid_value) << FlagName << Sanitizer;
    else
      S.set(K, true);
  }
}

static SmallVector<StringRef, 4> serializeSanitizerKinds(SanitizerSet S) {
  SmallVector<StringRef, 4> Values;
  serializeSanitizerSet(S, Values);
  return Values;
}

static void parseXRayInstrumentationBundle(StringRef FlagName, StringRef Bundle,
                                           ArgList &Args, DiagnosticsEngine &D,
                                           XRayInstrSet &S) {
  llvm::SmallVector<StringRef, 2> BundleParts;
  llvm::SplitString(Bundle, BundleParts, ",");
  for (const auto &B : BundleParts) {
    auto Mask = parseXRayInstrValue(B);
    if (Mask == XRayInstrKind::None)
      if (B != "none")
        D.Report(diag::err_drv_invalid_value) << FlagName << Bundle;
      else
        S.Mask = Mask;
    else if (Mask == XRayInstrKind::All)
      S.Mask = Mask;
    else
      S.set(Mask, true);
  }
}

static std::string serializeXRayInstrumentationBundle(const XRayInstrSet &S) {
  llvm::SmallVector<StringRef, 2> BundleParts;
  serializeXRayInstrValue(S, BundleParts);
  std::string Buffer;
  llvm::raw_string_ostream OS(Buffer);
  llvm::interleave(BundleParts, OS, [&OS](StringRef Part) { OS << Part; }, ",");
  return Buffer;
}

// Set the profile kind using fprofile-instrument-use-path.
static void setPGOUseInstrumentor(CodeGenOptions &Opts,
                                  const Twine &ProfileName,
                                  llvm::vfs::FileSystem &FS,
                                  DiagnosticsEngine &Diags) {
  auto ReaderOrErr = llvm::IndexedInstrProfReader::create(ProfileName, FS);
  if (auto E = ReaderOrErr.takeError()) {
    unsigned DiagID = Diags.getCustomDiagID(DiagnosticsEngine::Error,
                                            "Error in reading profile %0: %1");
    llvm::handleAllErrors(std::move(E), [&](const llvm::ErrorInfoBase &EI) {
      Diags.Report(DiagID) << ProfileName.str() << EI.message();
    });
    return;
  }
  std::unique_ptr<llvm::IndexedInstrProfReader> PGOReader =
    std::move(ReaderOrErr.get());
  // Currently memprof profiles are only added at the IR level. Mark the profile
  // type as IR in that case as well and the subsequent matching needs to detect
  // which is available (might be one or both).
  if (PGOReader->isIRLevelProfile() || PGOReader->hasMemoryProfile()) {
    if (PGOReader->hasCSIRLevelProfile())
      Opts.setProfileUse(CodeGenOptions::ProfileCSIRInstr);
    else
      Opts.setProfileUse(CodeGenOptions::ProfileIRInstr);
  } else
    Opts.setProfileUse(CodeGenOptions::ProfileClangInstr);
}

void CompilerInvocation::setDefaultPointerAuthOptions(
    PointerAuthOptions &Opts, const LangOptions &LangOpts,
    const llvm::Triple &Triple) {
  assert(Triple.getArch() == llvm::Triple::aarch64);
  if (LangOpts.PointerAuthCalls) {
    using Key = PointerAuthSchema::ARM8_3Key;
    using Discrimination = PointerAuthSchema::Discrimination;
    // If you change anything here, be sure to update <ptrauth.h>.
    Opts.FunctionPointers = PointerAuthSchema(
        Key::ASIA, false,
        LangOpts.PointerAuthFunctionTypeDiscrimination ? Discrimination::Type
                                                       : Discrimination::None);

    Opts.CXXVTablePointers = PointerAuthSchema(
        Key::ASDA, LangOpts.PointerAuthVTPtrAddressDiscrimination,
        LangOpts.PointerAuthVTPtrTypeDiscrimination ? Discrimination::Type
                                                    : Discrimination::None);

    if (LangOpts.PointerAuthTypeInfoVTPtrDiscrimination)
      Opts.CXXTypeInfoVTablePointer =
          PointerAuthSchema(Key::ASDA, true, Discrimination::Constant,
                            StdTypeInfoVTablePointerConstantDiscrimination);
    else
      Opts.CXXTypeInfoVTablePointer =
          PointerAuthSchema(Key::ASDA, false, Discrimination::None);

    Opts.CXXVTTVTablePointers =
        PointerAuthSchema(Key::ASDA, false, Discrimination::None);
    Opts.CXXVirtualFunctionPointers = Opts.CXXVirtualVariadicFunctionPointers =
        PointerAuthSchema(Key::ASIA, true, Discrimination::Decl);
    Opts.CXXMemberFunctionPointers =
        PointerAuthSchema(Key::ASIA, false, Discrimination::Type);
  }
  Opts.ReturnAddresses = LangOpts.PointerAuthReturns;
  Opts.AuthTraps = LangOpts.PointerAuthAuthTraps;
  Opts.IndirectGotos = LangOpts.PointerAuthIndirectGotos;
}

static void parsePointerAuthOptions(PointerAuthOptions &Opts,
                                    const LangOptions &LangOpts,
                                    const llvm::Triple &Triple,
                                    DiagnosticsEngine &Diags) {
  if (!LangOpts.PointerAuthCalls && !LangOpts.PointerAuthReturns &&
      !LangOpts.PointerAuthAuthTraps && !LangOpts.PointerAuthIndirectGotos)
    return;

  CompilerInvocation::setDefaultPointerAuthOptions(Opts, LangOpts, Triple);
}

void CompilerInvocationBase::GenerateCodeGenArgs(const CodeGenOptions &Opts,
                                                 ArgumentConsumer Consumer,
                                                 const llvm::Triple &T,
                                                 const std::string &OutputFile,
                                                 const LangOptions *LangOpts) {
  const CodeGenOptions &CodeGenOpts = Opts;

  if (Opts.OptimizationLevel == 0)
    GenerateArg(Consumer, OPT_O0);
  else
    GenerateArg(Consumer, OPT_O, Twine(Opts.OptimizationLevel));

#define CODEGEN_OPTION_WITH_MARSHALLING(...)                                   \
  GENERATE_OPTION_WITH_MARSHALLING(Consumer, __VA_ARGS__)
#include "clang/Driver/Options.inc"
#undef CODEGEN_OPTION_WITH_MARSHALLING

  if (Opts.OptimizationLevel > 0) {
    if (Opts.Inlining == CodeGenOptions::NormalInlining)
      GenerateArg(Consumer, OPT_finline_functions);
    else if (Opts.Inlining == CodeGenOptions::OnlyHintInlining)
      GenerateArg(Consumer, OPT_finline_hint_functions);
    else if (Opts.Inlining == CodeGenOptions::OnlyAlwaysInlining)
      GenerateArg(Consumer, OPT_fno_inline);
  }

  if (Opts.DirectAccessExternalData && LangOpts->PICLevel != 0)
    GenerateArg(Consumer, OPT_fdirect_access_external_data);
  else if (!Opts.DirectAccessExternalData && LangOpts->PICLevel == 0)
    GenerateArg(Consumer, OPT_fno_direct_access_external_data);

  std::optional<StringRef> DebugInfoVal;
  switch (Opts.DebugInfo) {
  case llvm::codegenoptions::DebugLineTablesOnly:
    DebugInfoVal = "line-tables-only";
    break;
  case llvm::codegenoptions::DebugDirectivesOnly:
    DebugInfoVal = "line-directives-only";
    break;
  case llvm::codegenoptions::DebugInfoConstructor:
    DebugInfoVal = "constructor";
    break;
  case llvm::codegenoptions::LimitedDebugInfo:
    DebugInfoVal = "limited";
    break;
  case llvm::codegenoptions::FullDebugInfo:
    DebugInfoVal = "standalone";
    break;
  case llvm::codegenoptions::UnusedTypeInfo:
    DebugInfoVal = "unused-types";
    break;
  case llvm::codegenoptions::NoDebugInfo: // default value
    DebugInfoVal = std::nullopt;
    break;
  case llvm::codegenoptions::LocTrackingOnly: // implied value
    DebugInfoVal = std::nullopt;
    break;
  }
  if (DebugInfoVal)
    GenerateArg(Consumer, OPT_debug_info_kind_EQ, *DebugInfoVal);

  for (const auto &Prefix : Opts.DebugPrefixMap)
    GenerateArg(Consumer, OPT_fdebug_prefix_map_EQ,
                Prefix.first + "=" + Prefix.second);

  for (const auto &Prefix : Opts.CoveragePrefixMap)
    GenerateArg(Consumer, OPT_fcoverage_prefix_map_EQ,
                Prefix.first + "=" + Prefix.second);

  if (Opts.NewStructPathTBAA)
    GenerateArg(Consumer, OPT_new_struct_path_tbaa);

  if (Opts.OptimizeSize == 1)
    GenerateArg(Consumer, OPT_O, "s");
  else if (Opts.OptimizeSize == 2)
    GenerateArg(Consumer, OPT_O, "z");

  // SimplifyLibCalls is set only in the absence of -fno-builtin and
  // -ffreestanding. We'll consider that when generating them.

  // NoBuiltinFuncs are generated by LangOptions.

  if (Opts.UnrollLoops && Opts.OptimizationLevel <= 1)
    GenerateArg(Consumer, OPT_funroll_loops);
  else if (!Opts.UnrollLoops && Opts.OptimizationLevel > 1)
    GenerateArg(Consumer, OPT_fno_unroll_loops);

  if (!Opts.BinutilsVersion.empty())
    GenerateArg(Consumer, OPT_fbinutils_version_EQ, Opts.BinutilsVersion);

  if (Opts.DebugNameTable ==
      static_cast<unsigned>(llvm::DICompileUnit::DebugNameTableKind::GNU))
    GenerateArg(Consumer, OPT_ggnu_pubnames);
  else if (Opts.DebugNameTable ==
           static_cast<unsigned>(
               llvm::DICompileUnit::DebugNameTableKind::Default))
    GenerateArg(Consumer, OPT_gpubnames);

  if (Opts.DebugTemplateAlias)
    GenerateArg(Consumer, OPT_gtemplate_alias);

  auto TNK = Opts.getDebugSimpleTemplateNames();
  if (TNK != llvm::codegenoptions::DebugTemplateNamesKind::Full) {
    if (TNK == llvm::codegenoptions::DebugTemplateNamesKind::Simple)
      GenerateArg(Consumer, OPT_gsimple_template_names_EQ, "simple");
    else if (TNK == llvm::codegenoptions::DebugTemplateNamesKind::Mangled)
      GenerateArg(Consumer, OPT_gsimple_template_names_EQ, "mangled");
  }
  // ProfileInstrumentUsePath is marshalled automatically, no need to generate
  // it or PGOUseInstrumentor.

  if (Opts.TimePasses) {
    if (Opts.TimePassesPerRun)
      GenerateArg(Consumer, OPT_ftime_report_EQ, "per-pass-run");
    else
      GenerateArg(Consumer, OPT_ftime_report);
  }

  if (Opts.PrepareForLTO && !Opts.PrepareForThinLTO)
    GenerateArg(Consumer, OPT_flto_EQ, "full");

  if (Opts.PrepareForThinLTO)
    GenerateArg(Consumer, OPT_flto_EQ, "thin");

  if (!Opts.ThinLTOIndexFile.empty())
    GenerateArg(Consumer, OPT_fthinlto_index_EQ, Opts.ThinLTOIndexFile);

  if (Opts.SaveTempsFilePrefix == OutputFile)
    GenerateArg(Consumer, OPT_save_temps_EQ, "obj");

  StringRef MemProfileBasename("memprof.profraw");
  if (!Opts.MemoryProfileOutput.empty()) {
    if (Opts.MemoryProfileOutput == MemProfileBasename) {
      GenerateArg(Consumer, OPT_fmemory_profile);
    } else {
      size_t ArgLength =
          Opts.MemoryProfileOutput.size() - MemProfileBasename.size();
      GenerateArg(Consumer, OPT_fmemory_profile_EQ,
                  Opts.MemoryProfileOutput.substr(0, ArgLength));
    }
  }

  if (memcmp(Opts.CoverageVersion, "408*", 4) != 0)
    GenerateArg(Consumer, OPT_coverage_version_EQ,
                StringRef(Opts.CoverageVersion, 4));

  // TODO: Check if we need to generate arguments stored in CmdArgs. (Namely
  //  '-fembed_bitcode', which does not map to any CompilerInvocation field and
  //  won't be generated.)

  if (Opts.XRayInstrumentationBundle.Mask != XRayInstrKind::All) {
    std::string InstrBundle =
        serializeXRayInstrumentationBundle(Opts.XRayInstrumentationBundle);
    if (!InstrBundle.empty())
      GenerateArg(Consumer, OPT_fxray_instrumentation_bundle, InstrBundle);
  }

  if (Opts.CFProtectionReturn && Opts.CFProtectionBranch)
    GenerateArg(Consumer, OPT_fcf_protection_EQ, "full");
  else if (Opts.CFProtectionReturn)
    GenerateArg(Consumer, OPT_fcf_protection_EQ, "return");
  else if (Opts.CFProtectionBranch)
    GenerateArg(Consumer, OPT_fcf_protection_EQ, "branch");

  if (Opts.FunctionReturnThunks)
    GenerateArg(Consumer, OPT_mfunction_return_EQ, "thunk-extern");

  for (const auto &F : Opts.LinkBitcodeFiles) {
    bool Builtint = F.LinkFlags == llvm::Linker::Flags::LinkOnlyNeeded &&
                    F.PropagateAttrs && F.Internalize;
    GenerateArg(Consumer,
                Builtint ? OPT_mlink_builtin_bitcode : OPT_mlink_bitcode_file,
                F.Filename);
  }

  if (Opts.ReturnProtector) {
    GenerateArg(Consumer, OPT_ret_protector);
  }

  if (Opts.EmulatedTLS)
    GenerateArg(Consumer, OPT_femulated_tls);

  if (Opts.FPDenormalMode != llvm::DenormalMode::getIEEE())
    GenerateArg(Consumer, OPT_fdenormal_fp_math_EQ, Opts.FPDenormalMode.str());

  if ((Opts.FPDenormalMode != Opts.FP32DenormalMode) ||
      (Opts.FP32DenormalMode != llvm::DenormalMode::getIEEE()))
    GenerateArg(Consumer, OPT_fdenormal_fp_math_f32_EQ,
                Opts.FP32DenormalMode.str());

  if (Opts.StructReturnConvention == CodeGenOptions::SRCK_OnStack) {
    OptSpecifier Opt =
        T.isPPC32() ? OPT_maix_struct_return : OPT_fpcc_struct_return;
    GenerateArg(Consumer, Opt);
  } else if (Opts.StructReturnConvention == CodeGenOptions::SRCK_InRegs) {
    OptSpecifier Opt =
        T.isPPC32() ? OPT_msvr4_struct_return : OPT_freg_struct_return;
    GenerateArg(Consumer, Opt);
  }

  if (Opts.EnableAIXExtendedAltivecABI)
    GenerateArg(Consumer, OPT_mabi_EQ_vec_extabi);

  if (Opts.XCOFFReadOnlyPointers)
    GenerateArg(Consumer, OPT_mxcoff_roptr);

  if (!Opts.OptRecordPasses.empty())
    GenerateArg(Consumer, OPT_opt_record_passes, Opts.OptRecordPasses);

  if (!Opts.OptRecordFormat.empty())
    GenerateArg(Consumer, OPT_opt_record_format, Opts.OptRecordFormat);

  GenerateOptimizationRemark(Consumer, OPT_Rpass_EQ, "pass",
                             Opts.OptimizationRemark);

  GenerateOptimizationRemark(Consumer, OPT_Rpass_missed_EQ, "pass-missed",
                             Opts.OptimizationRemarkMissed);

  GenerateOptimizationRemark(Consumer, OPT_Rpass_analysis_EQ, "pass-analysis",
                             Opts.OptimizationRemarkAnalysis);

  GenerateArg(Consumer, OPT_fdiagnostics_hotness_threshold_EQ,
              Opts.DiagnosticsHotnessThreshold
                  ? Twine(*Opts.DiagnosticsHotnessThreshold)
                  : "auto");

  GenerateArg(Consumer, OPT_fdiagnostics_misexpect_tolerance_EQ,
              Twine(*Opts.DiagnosticsMisExpectTolerance));

  for (StringRef Sanitizer : serializeSanitizerKinds(Opts.SanitizeRecover))
    GenerateArg(Consumer, OPT_fsanitize_recover_EQ, Sanitizer);

  for (StringRef Sanitizer : serializeSanitizerKinds(Opts.SanitizeTrap))
    GenerateArg(Consumer, OPT_fsanitize_trap_EQ, Sanitizer);

  if (!Opts.EmitVersionIdentMetadata)
    GenerateArg(Consumer, OPT_Qn);

  switch (Opts.FiniteLoops) {
  case CodeGenOptions::FiniteLoopsKind::Language:
    break;
  case CodeGenOptions::FiniteLoopsKind::Always:
    GenerateArg(Consumer, OPT_ffinite_loops);
    break;
  case CodeGenOptions::FiniteLoopsKind::Never:
    GenerateArg(Consumer, OPT_fno_finite_loops);
    break;
  }
}

bool CompilerInvocation::ParseCodeGenArgs(CodeGenOptions &Opts, ArgList &Args,
                                          InputKind IK,
                                          DiagnosticsEngine &Diags,
                                          const llvm::Triple &T,
                                          const std::string &OutputFile,
                                          const LangOptions &LangOptsRef) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  unsigned OptimizationLevel = getOptimizationLevel(Args, IK, Diags);
  // TODO: This could be done in Driver
  unsigned MaxOptLevel = 3;
  if (OptimizationLevel > MaxOptLevel) {
    // If the optimization level is not supported, fall back on the default
    // optimization
    Diags.Report(diag::warn_drv_optimization_value)
        << Args.getLastArg(OPT_O)->getAsString(Args) << "-O" << MaxOptLevel;
    OptimizationLevel = MaxOptLevel;
  }
  Opts.OptimizationLevel = OptimizationLevel;

  // The key paths of codegen options defined in Options.td start with
  // "CodeGenOpts.". Let's provide the expected variable name and type.
  CodeGenOptions &CodeGenOpts = Opts;
  // Some codegen options depend on language options. Let's provide the expected
  // variable name and type.
  const LangOptions *LangOpts = &LangOptsRef;

#define CODEGEN_OPTION_WITH_MARSHALLING(...)                                   \
  PARSE_OPTION_WITH_MARSHALLING(Args, Diags, __VA_ARGS__)
#include "clang/Driver/Options.inc"
#undef CODEGEN_OPTION_WITH_MARSHALLING

  // At O0 we want to fully disable inlining outside of cases marked with
  // 'alwaysinline' that are required for correctness.
  if (Opts.OptimizationLevel == 0) {
    Opts.setInlining(CodeGenOptions::OnlyAlwaysInlining);
  } else if (const Arg *A = Args.getLastArg(options::OPT_finline_functions,
                                            options::OPT_finline_hint_functions,
                                            options::OPT_fno_inline_functions,
                                            options::OPT_fno_inline)) {
    // Explicit inlining flags can disable some or all inlining even at
    // optimization levels above zero.
    if (A->getOption().matches(options::OPT_finline_functions))
      Opts.setInlining(CodeGenOptions::NormalInlining);
    else if (A->getOption().matches(options::OPT_finline_hint_functions))
      Opts.setInlining(CodeGenOptions::OnlyHintInlining);
    else
      Opts.setInlining(CodeGenOptions::OnlyAlwaysInlining);
  } else {
    Opts.setInlining(CodeGenOptions::NormalInlining);
  }

  // PIC defaults to -fno-direct-access-external-data while non-PIC defaults to
  // -fdirect-access-external-data.
  Opts.DirectAccessExternalData =
      Args.hasArg(OPT_fdirect_access_external_data) ||
      (!Args.hasArg(OPT_fno_direct_access_external_data) &&
       LangOpts->PICLevel == 0);

  if (Arg *A = Args.getLastArg(OPT_debug_info_kind_EQ)) {
    unsigned Val =
        llvm::StringSwitch<unsigned>(A->getValue())
            .Case("line-tables-only", llvm::codegenoptions::DebugLineTablesOnly)
            .Case("line-directives-only",
                  llvm::codegenoptions::DebugDirectivesOnly)
            .Case("constructor", llvm::codegenoptions::DebugInfoConstructor)
            .Case("limited", llvm::codegenoptions::LimitedDebugInfo)
            .Case("standalone", llvm::codegenoptions::FullDebugInfo)
            .Case("unused-types", llvm::codegenoptions::UnusedTypeInfo)
            .Default(~0U);
    if (Val == ~0U)
      Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args)
                                                << A->getValue();
    else
      Opts.setDebugInfo(static_cast<llvm::codegenoptions::DebugInfoKind>(Val));
  }

  // If -fuse-ctor-homing is set and limited debug info is already on, then use
  // constructor homing, and vice versa for -fno-use-ctor-homing.
  if (const Arg *A =
          Args.getLastArg(OPT_fuse_ctor_homing, OPT_fno_use_ctor_homing)) {
    if (A->getOption().matches(OPT_fuse_ctor_homing) &&
        Opts.getDebugInfo() == llvm::codegenoptions::LimitedDebugInfo)
      Opts.setDebugInfo(llvm::codegenoptions::DebugInfoConstructor);
    if (A->getOption().matches(OPT_fno_use_ctor_homing) &&
        Opts.getDebugInfo() == llvm::codegenoptions::DebugInfoConstructor)
      Opts.setDebugInfo(llvm::codegenoptions::LimitedDebugInfo);
  }

  for (const auto &Arg : Args.getAllArgValues(OPT_fdebug_prefix_map_EQ)) {
    auto Split = StringRef(Arg).split('=');
    Opts.DebugPrefixMap.emplace_back(Split.first, Split.second);
  }

  for (const auto &Arg : Args.getAllArgValues(OPT_fcoverage_prefix_map_EQ)) {
    auto Split = StringRef(Arg).split('=');
    Opts.CoveragePrefixMap.emplace_back(Split.first, Split.second);
  }

  const llvm::Triple::ArchType DebugEntryValueArchs[] = {
      llvm::Triple::x86, llvm::Triple::x86_64, llvm::Triple::aarch64,
      llvm::Triple::arm, llvm::Triple::armeb, llvm::Triple::mips,
      llvm::Triple::mipsel, llvm::Triple::mips64, llvm::Triple::mips64el};

  if (Opts.OptimizationLevel > 0 && Opts.hasReducedDebugInfo() &&
      llvm::is_contained(DebugEntryValueArchs, T.getArch()))
    Opts.EmitCallSiteInfo = true;

  if (!Opts.EnableDIPreservationVerify && Opts.DIBugsReportFilePath.size()) {
    Diags.Report(diag::warn_ignoring_verify_debuginfo_preserve_export)
        << Opts.DIBugsReportFilePath;
    Opts.DIBugsReportFilePath = "";
  }

  Opts.NewStructPathTBAA = !Args.hasArg(OPT_no_struct_path_tbaa) &&
                           Args.hasArg(OPT_new_struct_path_tbaa);
  Opts.OptimizeSize = getOptimizationLevelSize(Args);
  Opts.SimplifyLibCalls = !LangOpts->NoBuiltin;
  if (Opts.SimplifyLibCalls)
    Opts.NoBuiltinFuncs = LangOpts->NoBuiltinFuncs;
  Opts.UnrollLoops =
      Args.hasFlag(OPT_funroll_loops, OPT_fno_unroll_loops,
                   (Opts.OptimizationLevel > 1));
  Opts.BinutilsVersion =
      std::string(Args.getLastArgValue(OPT_fbinutils_version_EQ));

  Opts.DebugTemplateAlias = Args.hasArg(OPT_gtemplate_alias);

  Opts.DebugNameTable = static_cast<unsigned>(
      Args.hasArg(OPT_ggnu_pubnames)
          ? llvm::DICompileUnit::DebugNameTableKind::GNU
          : Args.hasArg(OPT_gpubnames)
                ? llvm::DICompileUnit::DebugNameTableKind::Default
                : llvm::DICompileUnit::DebugNameTableKind::None);
  if (const Arg *A = Args.getLastArg(OPT_gsimple_template_names_EQ)) {
    StringRef Value = A->getValue();
    if (Value != "simple" && Value != "mangled")
      Diags.Report(diag::err_drv_unsupported_option_argument)
          << A->getSpelling() << A->getValue();
    Opts.setDebugSimpleTemplateNames(
        StringRef(A->getValue()) == "simple"
            ? llvm::codegenoptions::DebugTemplateNamesKind::Simple
            : llvm::codegenoptions::DebugTemplateNamesKind::Mangled);
  }

  if (const Arg *A = Args.getLastArg(OPT_ftime_report, OPT_ftime_report_EQ)) {
    Opts.TimePasses = true;

    // -ftime-report= is only for new pass manager.
    if (A->getOption().getID() == OPT_ftime_report_EQ) {
      StringRef Val = A->getValue();
      if (Val == "per-pass")
        Opts.TimePassesPerRun = false;
      else if (Val == "per-pass-run")
        Opts.TimePassesPerRun = true;
      else
        Diags.Report(diag::err_drv_invalid_value)
            << A->getAsString(Args) << A->getValue();
    }
  }

  Opts.PrepareForLTO = false;
  Opts.PrepareForThinLTO = false;
  if (Arg *A = Args.getLastArg(OPT_flto_EQ)) {
    Opts.PrepareForLTO = true;
    StringRef S = A->getValue();
    if (S == "thin")
      Opts.PrepareForThinLTO = true;
    else if (S != "full")
      Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args) << S;
    if (Args.hasArg(OPT_funified_lto))
      Opts.PrepareForThinLTO = true;
  }
  if (Arg *A = Args.getLastArg(OPT_fthinlto_index_EQ)) {
    if (IK.getLanguage() != Language::LLVM_IR)
      Diags.Report(diag::err_drv_argument_only_allowed_with)
          << A->getAsString(Args) << "-x ir";
    Opts.ThinLTOIndexFile =
        std::string(Args.getLastArgValue(OPT_fthinlto_index_EQ));
  }
  if (Arg *A = Args.getLastArg(OPT_save_temps_EQ))
    Opts.SaveTempsFilePrefix =
        llvm::StringSwitch<std::string>(A->getValue())
            .Case("obj", OutputFile)
            .Default(llvm::sys::path::filename(OutputFile).str());

  // The memory profile runtime appends the pid to make this name more unique.
  const char *MemProfileBasename = "memprof.profraw";
  if (Args.hasArg(OPT_fmemory_profile_EQ)) {
    SmallString<128> Path(
        std::string(Args.getLastArgValue(OPT_fmemory_profile_EQ)));
    llvm::sys::path::append(Path, MemProfileBasename);
    Opts.MemoryProfileOutput = std::string(Path);
  } else if (Args.hasArg(OPT_fmemory_profile))
    Opts.MemoryProfileOutput = MemProfileBasename;

  memcpy(Opts.CoverageVersion, "408*", 4);
  if (Opts.CoverageNotesFile.size() || Opts.CoverageDataFile.size()) {
    if (Args.hasArg(OPT_coverage_version_EQ)) {
      StringRef CoverageVersion = Args.getLastArgValue(OPT_coverage_version_EQ);
      if (CoverageVersion.size() != 4) {
        Diags.Report(diag::err_drv_invalid_value)
            << Args.getLastArg(OPT_coverage_version_EQ)->getAsString(Args)
            << CoverageVersion;
      } else {
        memcpy(Opts.CoverageVersion, CoverageVersion.data(), 4);
      }
    }
  }
  // FIXME: For backend options that are not yet recorded as function
  // attributes in the IR, keep track of them so we can embed them in a
  // separate data section and use them when building the bitcode.
  for (const auto &A : Args) {
    // Do not encode output and input.
    if (A->getOption().getID() == options::OPT_o ||
        A->getOption().getID() == options::OPT_INPUT ||
        A->getOption().getID() == options::OPT_x ||
        A->getOption().getID() == options::OPT_fembed_bitcode ||
        A->getOption().matches(options::OPT_W_Group))
      continue;
    ArgStringList ASL;
    A->render(Args, ASL);
    for (const auto &arg : ASL) {
      StringRef ArgStr(arg);
      Opts.CmdArgs.insert(Opts.CmdArgs.end(), ArgStr.begin(), ArgStr.end());
      // using \00 to separate each commandline options.
      Opts.CmdArgs.push_back('\0');
    }
  }

  auto XRayInstrBundles =
      Args.getAllArgValues(OPT_fxray_instrumentation_bundle);
  if (XRayInstrBundles.empty())
    Opts.XRayInstrumentationBundle.Mask = XRayInstrKind::All;
  else
    for (const auto &A : XRayInstrBundles)
      parseXRayInstrumentationBundle("-fxray-instrumentation-bundle=", A, Args,
                                     Diags, Opts.XRayInstrumentationBundle);

  if (const Arg *A = Args.getLastArg(OPT_fcf_protection_EQ)) {
    StringRef Name = A->getValue();
    if (Name == "full") {
      Opts.CFProtectionReturn = 1;
      Opts.CFProtectionBranch = 1;
    } else if (Name == "return")
      Opts.CFProtectionReturn = 1;
    else if (Name == "branch")
      Opts.CFProtectionBranch = 1;
    else if (Name != "none")
      Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args) << Name;
  }

  if (const Arg *A = Args.getLastArg(OPT_mfunction_return_EQ)) {
    auto Val = llvm::StringSwitch<llvm::FunctionReturnThunksKind>(A->getValue())
                   .Case("keep", llvm::FunctionReturnThunksKind::Keep)
                   .Case("thunk-extern", llvm::FunctionReturnThunksKind::Extern)
                   .Default(llvm::FunctionReturnThunksKind::Invalid);
    // SystemZ might want to add support for "expolines."
    if (!T.isX86())
      Diags.Report(diag::err_drv_argument_not_allowed_with)
          << A->getSpelling() << T.getTriple();
    else if (Val == llvm::FunctionReturnThunksKind::Invalid)
      Diags.Report(diag::err_drv_invalid_value)
          << A->getAsString(Args) << A->getValue();
    else if (Val == llvm::FunctionReturnThunksKind::Extern &&
             Args.getLastArgValue(OPT_mcmodel_EQ) == "large")
      Diags.Report(diag::err_drv_argument_not_allowed_with)
          << A->getAsString(Args)
          << Args.getLastArg(OPT_mcmodel_EQ)->getAsString(Args);
    else
      Opts.FunctionReturnThunks = static_cast<unsigned>(Val);
  }

  for (auto *A :
       Args.filtered(OPT_mlink_bitcode_file, OPT_mlink_builtin_bitcode)) {
    CodeGenOptions::BitcodeFileToLink F;
    F.Filename = A->getValue();
    if (A->getOption().matches(OPT_mlink_builtin_bitcode)) {
      F.LinkFlags = llvm::Linker::Flags::LinkOnlyNeeded;
      // When linking CUDA bitcode, propagate function attributes so that
      // e.g. libdevice gets fast-math attrs if we're building with fast-math.
      F.PropagateAttrs = true;
      F.Internalize = true;
    }
    Opts.LinkBitcodeFiles.push_back(F);
  }

  Opts.ReturnProtector = Args.hasArg(OPT_ret_protector);

  if (Arg *A = Args.getLastArg(OPT_fdenormal_fp_math_EQ)) {
    StringRef Val = A->getValue();
    Opts.FPDenormalMode = llvm::parseDenormalFPAttribute(Val);
    Opts.FP32DenormalMode = Opts.FPDenormalMode;
    if (!Opts.FPDenormalMode.isValid())
      Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args) << Val;
  }

  if (Arg *A = Args.getLastArg(OPT_fdenormal_fp_math_f32_EQ)) {
    StringRef Val = A->getValue();
    Opts.FP32DenormalMode = llvm::parseDenormalFPAttribute(Val);
    if (!Opts.FP32DenormalMode.isValid())
      Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args) << Val;
  }

  // X86_32 has -fppc-struct-return and -freg-struct-return.
  // PPC32 has -maix-struct-return and -msvr4-struct-return.
  if (Arg *A =
          Args.getLastArg(OPT_fpcc_struct_return, OPT_freg_struct_return,
                          OPT_maix_struct_return, OPT_msvr4_struct_return)) {
    // TODO: We might want to consider enabling these options on AIX in the
    // future.
    if (T.isOSAIX())
      Diags.Report(diag::err_drv_unsupported_opt_for_target)
          << A->getSpelling() << T.str();

    const Option &O = A->getOption();
    if (O.matches(OPT_fpcc_struct_return) ||
        O.matches(OPT_maix_struct_return)) {
      Opts.setStructReturnConvention(CodeGenOptions::SRCK_OnStack);
    } else {
      assert(O.matches(OPT_freg_struct_return) ||
             O.matches(OPT_msvr4_struct_return));
      Opts.setStructReturnConvention(CodeGenOptions::SRCK_InRegs);
    }
  }

  if (Arg *A = Args.getLastArg(OPT_mxcoff_roptr)) {
    if (!T.isOSAIX())
      Diags.Report(diag::err_drv_unsupported_opt_for_target)
          << A->getSpelling() << T.str();

    // Since the storage mapping class is specified per csect,
    // without using data sections, it is less effective to use read-only
    // pointers. Using read-only pointers may cause other RO variables in the
    // same csect to become RW when the linker acts upon `-bforceimprw`;
    // therefore, we require that separate data sections
    // are used when `-mxcoff-roptr` is in effect. We respect the setting of
    // data-sections since we have not found reasons to do otherwise that
    // overcome the user surprise of not respecting the setting.
    if (!Args.hasFlag(OPT_fdata_sections, OPT_fno_data_sections, false))
      Diags.Report(diag::err_roptr_requires_data_sections);

    Opts.XCOFFReadOnlyPointers = true;
  }

  if (Arg *A = Args.getLastArg(OPT_mabi_EQ_quadword_atomics)) {
    if (!T.isOSAIX() || T.isPPC32())
      Diags.Report(diag::err_drv_unsupported_opt_for_target)
        << A->getSpelling() << T.str();
  }

  bool NeedLocTracking = false;

  if (!Opts.OptRecordFile.empty())
    NeedLocTracking = true;

  if (Arg *A = Args.getLastArg(OPT_opt_record_passes)) {
    Opts.OptRecordPasses = A->getValue();
    NeedLocTracking = true;
  }

  if (Arg *A = Args.getLastArg(OPT_opt_record_format)) {
    Opts.OptRecordFormat = A->getValue();
    NeedLocTracking = true;
  }

  Opts.OptimizationRemark =
      ParseOptimizationRemark(Diags, Args, OPT_Rpass_EQ, "pass");

  Opts.OptimizationRemarkMissed =
      ParseOptimizationRemark(Diags, Args, OPT_Rpass_missed_EQ, "pass-missed");

  Opts.OptimizationRemarkAnalysis = ParseOptimizationRemark(
      Diags, Args, OPT_Rpass_analysis_EQ, "pass-analysis");

  NeedLocTracking |= Opts.OptimizationRemark.hasValidPattern() ||
                     Opts.OptimizationRemarkMissed.hasValidPattern() ||
                     Opts.OptimizationRemarkAnalysis.hasValidPattern();

  bool UsingSampleProfile = !Opts.SampleProfileFile.empty();
  bool UsingProfile =
      UsingSampleProfile || !Opts.ProfileInstrumentUsePath.empty();

  if (Opts.DiagnosticsWithHotness && !UsingProfile &&
      // An IR file will contain PGO as metadata
      IK.getLanguage() != Language::LLVM_IR)
    Diags.Report(diag::warn_drv_diagnostics_hotness_requires_pgo)
        << "-fdiagnostics-show-hotness";

  // Parse remarks hotness threshold. Valid value is either integer or 'auto'.
  if (auto *arg =
          Args.getLastArg(options::OPT_fdiagnostics_hotness_threshold_EQ)) {
    auto ResultOrErr =
        llvm::remarks::parseHotnessThresholdOption(arg->getValue());

    if (!ResultOrErr) {
      Diags.Report(diag::err_drv_invalid_diagnotics_hotness_threshold)
          << "-fdiagnostics-hotness-threshold=";
    } else {
      Opts.DiagnosticsHotnessThreshold = *ResultOrErr;
      if ((!Opts.DiagnosticsHotnessThreshold ||
           *Opts.DiagnosticsHotnessThreshold > 0) &&
          !UsingProfile)
        Diags.Report(diag::warn_drv_diagnostics_hotness_requires_pgo)
            << "-fdiagnostics-hotness-threshold=";
    }
  }

  if (auto *arg =
          Args.getLastArg(options::OPT_fdiagnostics_misexpect_tolerance_EQ)) {
    auto ResultOrErr = parseToleranceOption(arg->getValue());

    if (!ResultOrErr) {
      Diags.Report(diag::err_drv_invalid_diagnotics_misexpect_tolerance)
          << "-fdiagnostics-misexpect-tolerance=";
    } else {
      Opts.DiagnosticsMisExpectTolerance = *ResultOrErr;
      if ((!Opts.DiagnosticsMisExpectTolerance ||
           *Opts.DiagnosticsMisExpectTolerance > 0) &&
          !UsingProfile)
        Diags.Report(diag::warn_drv_diagnostics_misexpect_requires_pgo)
            << "-fdiagnostics-misexpect-tolerance=";
    }
  }

  // If the user requested to use a sample profile for PGO, then the
  // backend will need to track source location information so the profile
  // can be incorporated into the IR.
  if (UsingSampleProfile)
    NeedLocTracking = true;

  if (!Opts.StackUsageOutput.empty())
    NeedLocTracking = true;

  // If the user requested a flag that requires source locations available in
  // the backend, make sure that the backend tracks source location information.
  if (NeedLocTracking &&
      Opts.getDebugInfo() == llvm::codegenoptions::NoDebugInfo)
    Opts.setDebugInfo(llvm::codegenoptions::LocTrackingOnly);

  // Parse -fsanitize-recover= arguments.
  // FIXME: Report unrecoverable sanitizers incorrectly specified here.
  parseSanitizerKinds("-fsanitize-recover=",
                      Args.getAllArgValues(OPT_fsanitize_recover_EQ), Diags,
                      Opts.SanitizeRecover);
  parseSanitizerKinds("-fsanitize-trap=",
                      Args.getAllArgValues(OPT_fsanitize_trap_EQ), Diags,
                      Opts.SanitizeTrap);

  Opts.EmitVersionIdentMetadata = Args.hasFlag(OPT_Qy, OPT_Qn, true);

  if (!LangOpts->CUDAIsDevice)
    parsePointerAuthOptions(Opts.PointerAuth, *LangOpts, T, Diags);

  if (Args.hasArg(options::OPT_ffinite_loops))
    Opts.FiniteLoops = CodeGenOptions::FiniteLoopsKind::Always;
  else if (Args.hasArg(options::OPT_fno_finite_loops))
    Opts.FiniteLoops = CodeGenOptions::FiniteLoopsKind::Never;

  Opts.EmitIEEENaNCompliantInsts = Args.hasFlag(
      options::OPT_mamdgpu_ieee, options::OPT_mno_amdgpu_ieee, true);
  if (!Opts.EmitIEEENaNCompliantInsts && !LangOptsRef.NoHonorNaNs)
    Diags.Report(diag::err_drv_amdgpu_ieee_without_no_honor_nans);

  return Diags.getNumErrors() == NumErrorsBefore;
}

static void GenerateDependencyOutputArgs(const DependencyOutputOptions &Opts,
                                         ArgumentConsumer Consumer) {
  const DependencyOutputOptions &DependencyOutputOpts = Opts;
#define DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING(...)                         \
  GENERATE_OPTION_WITH_MARSHALLING(Consumer, __VA_ARGS__)
#include "clang/Driver/Options.inc"
#undef DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING

  if (Opts.ShowIncludesDest != ShowIncludesDestination::None)
    GenerateArg(Consumer, OPT_show_includes);

  for (const auto &Dep : Opts.ExtraDeps) {
    switch (Dep.second) {
    case EDK_SanitizeIgnorelist:
      // Sanitizer ignorelist arguments are generated from LanguageOptions.
      continue;
    case EDK_ModuleFile:
      // Module file arguments are generated from FrontendOptions and
      // HeaderSearchOptions.
      continue;
    case EDK_ProfileList:
      // Profile list arguments are generated from LanguageOptions via the
      // marshalling infrastructure.
      continue;
    case EDK_DepFileEntry:
      GenerateArg(Consumer, OPT_fdepfile_entry, Dep.first);
      break;
    }
  }
}

static bool ParseDependencyOutputArgs(DependencyOutputOptions &Opts,
                                      ArgList &Args, DiagnosticsEngine &Diags,
                                      frontend::ActionKind Action,
                                      bool ShowLineMarkers) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  DependencyOutputOptions &DependencyOutputOpts = Opts;
#define DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING(...)                         \
  PARSE_OPTION_WITH_MARSHALLING(Args, Diags, __VA_ARGS__)
#include "clang/Driver/Options.inc"
#undef DEPENDENCY_OUTPUT_OPTION_WITH_MARSHALLING

  if (Args.hasArg(OPT_show_includes)) {
    // Writing both /showIncludes and preprocessor output to stdout
    // would produce interleaved output, so use stderr for /showIncludes.
    // This behaves the same as cl.exe, when /E, /EP or /P are passed.
    if (Action == frontend::PrintPreprocessedInput || !ShowLineMarkers)
      Opts.ShowIncludesDest = ShowIncludesDestination::Stderr;
    else
      Opts.ShowIncludesDest = ShowIncludesDestination::Stdout;
  } else {
    Opts.ShowIncludesDest = ShowIncludesDestination::None;
  }

  // Add sanitizer ignorelists as extra dependencies.
  // They won't be discovered by the regular preprocessor, so
  // we let make / ninja to know about this implicit dependency.
  if (!Args.hasArg(OPT_fno_sanitize_ignorelist)) {
    for (const auto *A : Args.filtered(OPT_fsanitize_ignorelist_EQ)) {
      StringRef Val = A->getValue();
      if (!Val.contains('='))
        Opts.ExtraDeps.emplace_back(std::string(Val), EDK_SanitizeIgnorelist);
    }
    if (Opts.IncludeSystemHeaders) {
      for (const auto *A : Args.filtered(OPT_fsanitize_system_ignorelist_EQ)) {
        StringRef Val = A->getValue();
        if (!Val.contains('='))
          Opts.ExtraDeps.emplace_back(std::string(Val), EDK_SanitizeIgnorelist);
      }
    }
  }

  // -fprofile-list= dependencies.
  for (const auto &Filename : Args.getAllArgValues(OPT_fprofile_list_EQ))
    Opts.ExtraDeps.emplace_back(Filename, EDK_ProfileList);

  // Propagate the extra dependencies.
  for (const auto *A : Args.filtered(OPT_fdepfile_entry))
    Opts.ExtraDeps.emplace_back(A->getValue(), EDK_DepFileEntry);

  // Only the -fmodule-file=<file> form.
  for (const auto *A : Args.filtered(OPT_fmodule_file)) {
    StringRef Val = A->getValue();
    if (!Val.contains('='))
      Opts.ExtraDeps.emplace_back(std::string(Val), EDK_ModuleFile);
  }

  // Check for invalid combinations of header-include-format
  // and header-include-filtering.
  if ((Opts.HeaderIncludeFormat == HIFMT_Textual &&
       Opts.HeaderIncludeFiltering != HIFIL_None) ||
      (Opts.HeaderIncludeFormat == HIFMT_JSON &&
       Opts.HeaderIncludeFiltering != HIFIL_Only_Direct_System))
    Diags.Report(diag::err_drv_print_header_env_var_combination_cc1)
        << Args.getLastArg(OPT_header_include_format_EQ)->getValue()
        << Args.getLastArg(OPT_header_include_filtering_EQ)->getValue();

  return Diags.getNumErrors() == NumErrorsBefore;
}

static bool parseShowColorsArgs(const ArgList &Args, bool DefaultColor) {
  // Color diagnostics default to auto ("on" if terminal supports) in the driver
  // but default to off in cc1, needing an explicit OPT_fdiagnostics_color.
  // Support both clang's -f[no-]color-diagnostics and gcc's
  // -f[no-]diagnostics-colors[=never|always|auto].
  enum {
    Colors_On,
    Colors_Off,
    Colors_Auto
  } ShowColors = DefaultColor ? Colors_Auto : Colors_Off;
  for (auto *A : Args) {
    const Option &O = A->getOption();
    if (O.matches(options::OPT_fcolor_diagnostics)) {
      ShowColors = Colors_On;
    } else if (O.matches(options::OPT_fno_color_diagnostics)) {
      ShowColors = Colors_Off;
    } else if (O.matches(options::OPT_fdiagnostics_color_EQ)) {
      StringRef Value(A->getValue());
      if (Value == "always")
        ShowColors = Colors_On;
      else if (Value == "never")
        ShowColors = Colors_Off;
      else if (Value == "auto")
        ShowColors = Colors_Auto;
    }
  }
  return ShowColors == Colors_On ||
         (ShowColors == Colors_Auto &&
          llvm::sys::Process::StandardErrHasColors());
}

static bool checkVerifyPrefixes(const std::vector<std::string> &VerifyPrefixes,
                                DiagnosticsEngine &Diags) {
  bool Success = true;
  for (const auto &Prefix : VerifyPrefixes) {
    // Every prefix must start with a letter and contain only alphanumeric
    // characters, hyphens, and underscores.
    auto BadChar = llvm::find_if(Prefix, [](char C) {
      return !isAlphanumeric(C) && C != '-' && C != '_';
    });
    if (BadChar != Prefix.end() || !isLetter(Prefix[0])) {
      Success = false;
      Diags.Report(diag::err_drv_invalid_value) << "-verify=" << Prefix;
      Diags.Report(diag::note_drv_verify_prefix_spelling);
    }
  }
  return Success;
}

static void GenerateFileSystemArgs(const FileSystemOptions &Opts,
                                   ArgumentConsumer Consumer) {
  const FileSystemOptions &FileSystemOpts = Opts;

#define FILE_SYSTEM_OPTION_WITH_MARSHALLING(...)                               \
  GENERATE_OPTION_WITH_MARSHALLING(Consumer, __VA_ARGS__)
#include "clang/Driver/Options.inc"
#undef FILE_SYSTEM_OPTION_WITH_MARSHALLING
}

static bool ParseFileSystemArgs(FileSystemOptions &Opts, const ArgList &Args,
                                DiagnosticsEngine &Diags) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  FileSystemOptions &FileSystemOpts = Opts;

#define FILE_SYSTEM_OPTION_WITH_MARSHALLING(...)                               \
  PARSE_OPTION_WITH_MARSHALLING(Args, Diags, __VA_ARGS__)
#include "clang/Driver/Options.inc"
#undef FILE_SYSTEM_OPTION_WITH_MARSHALLING

  return Diags.getNumErrors() == NumErrorsBefore;
}

static void GenerateMigratorArgs(const MigratorOptions &Opts,
                                 ArgumentConsumer Consumer) {
  const MigratorOptions &MigratorOpts = Opts;
#define MIGRATOR_OPTION_WITH_MARSHALLING(...)                                  \
  GENERATE_OPTION_WITH_MARSHALLING(Consumer, __VA_ARGS__)
#include "clang/Driver/Options.inc"
#undef MIGRATOR_OPTION_WITH_MARSHALLING
}

static bool ParseMigratorArgs(MigratorOptions &Opts, const ArgList &Args,
                              DiagnosticsEngine &Diags) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  MigratorOptions &MigratorOpts = Opts;

#define MIGRATOR_OPTION_WITH_MARSHALLING(...)                                  \
  PARSE_OPTION_WITH_MARSHALLING(Args, Diags, __VA_ARGS__)
#include "clang/Driver/Options.inc"
#undef MIGRATOR_OPTION_WITH_MARSHALLING

  return Diags.getNumErrors() == NumErrorsBefore;
}

void CompilerInvocationBase::GenerateDiagnosticArgs(
    const DiagnosticOptions &Opts, ArgumentConsumer Consumer,
    bool DefaultDiagColor) {
  const DiagnosticOptions *DiagnosticOpts = &Opts;
#define DIAG_OPTION_WITH_MARSHALLING(...)                                      \
  GENERATE_OPTION_WITH_MARSHALLING(Consumer, __VA_ARGS__)
#include "clang/Driver/Options.inc"
#undef DIAG_OPTION_WITH_MARSHALLING

  if (!Opts.DiagnosticSerializationFile.empty())
    GenerateArg(Consumer, OPT_diagnostic_serialized_file,
                Opts.DiagnosticSerializationFile);

  if (Opts.ShowColors)
    GenerateArg(Consumer, OPT_fcolor_diagnostics);

  if (Opts.VerifyDiagnostics &&
      llvm::is_contained(Opts.VerifyPrefixes, "expected"))
    GenerateArg(Consumer, OPT_verify);

  for (const auto &Prefix : Opts.VerifyPrefixes)
    if (Prefix != "expected")
      GenerateArg(Consumer, OPT_verify_EQ, Prefix);

  DiagnosticLevelMask VIU = Opts.getVerifyIgnoreUnexpected();
  if (VIU == DiagnosticLevelMask::None) {
    // This is the default, don't generate anything.
  } else if (VIU == DiagnosticLevelMask::All) {
    GenerateArg(Consumer, OPT_verify_ignore_unexpected);
  } else {
    if (static_cast<unsigned>(VIU & DiagnosticLevelMask::Note) != 0)
      GenerateArg(Consumer, OPT_verify_ignore_unexpected_EQ, "note");
    if (static_cast<unsigned>(VIU & DiagnosticLevelMask::Remark) != 0)
      GenerateArg(Consumer, OPT_verify_ignore_unexpected_EQ, "remark");
    if (static_cast<unsigned>(VIU & DiagnosticLevelMask::Warning) != 0)
      GenerateArg(Consumer, OPT_verify_ignore_unexpected_EQ, "warning");
    if (static_cast<unsigned>(VIU & DiagnosticLevelMask::Error) != 0)
      GenerateArg(Consumer, OPT_verify_ignore_unexpected_EQ, "error");
  }

  for (const auto &Warning : Opts.Warnings) {
    // This option is automatically generated from UndefPrefixes.
    if (Warning == "undef-prefix")
      continue;
    // This option is automatically generated from CheckConstexprFunctionBodies.
    if (Warning == "invalid-constexpr" || Warning == "no-invalid-constexpr")
      continue;
    Consumer(StringRef("-W") + Warning);
  }

  for (const auto &Remark : Opts.Remarks) {
    // These arguments are generated from OptimizationRemark fields of
    // CodeGenOptions.
    StringRef IgnoredRemarks[] = {"pass",          "no-pass",
                                  "pass-analysis", "no-pass-analysis",
                                  "pass-missed",   "no-pass-missed"};
    if (llvm::is_contained(IgnoredRemarks, Remark))
      continue;

    Consumer(StringRef("-R") + Remark);
  }
}

std::unique_ptr<DiagnosticOptions>
clang::CreateAndPopulateDiagOpts(ArrayRef<const char *> Argv) {
  auto DiagOpts = std::make_unique<DiagnosticOptions>();
  unsigned MissingArgIndex, MissingArgCount;
  InputArgList Args = getDriverOptTable().ParseArgs(
      Argv.slice(1), MissingArgIndex, MissingArgCount);

  bool ShowColors = true;
  if (std::optional<std::string> NoColor =
          llvm::sys::Process::GetEnv("NO_COLOR");
      NoColor && !NoColor->empty()) {
    // If the user set the NO_COLOR environment variable, we'll honor that
    // unless the command line overrides it.
    ShowColors = false;
  }

  // We ignore MissingArgCount and the return value of ParseDiagnosticArgs.
  // Any errors that would be diagnosed here will also be diagnosed later,
  // when the DiagnosticsEngine actually exists.
  (void)ParseDiagnosticArgs(*DiagOpts, Args, /*Diags=*/nullptr, ShowColors);
  return DiagOpts;
}

bool clang::ParseDiagnosticArgs(DiagnosticOptions &Opts, ArgList &Args,
                                DiagnosticsEngine *Diags,
                                bool DefaultDiagColor) {
  std::optional<DiagnosticsEngine> IgnoringDiags;
  if (!Diags) {
    IgnoringDiags.emplace(new DiagnosticIDs(), new DiagnosticOptions(),
                          new IgnoringDiagConsumer());
    Diags = &*IgnoringDiags;
  }

  unsigned NumErrorsBefore = Diags->getNumErrors();

  // The key paths of diagnostic options defined in Options.td start with
  // "DiagnosticOpts->". Let's provide the expected variable name and type.
  DiagnosticOptions *DiagnosticOpts = &Opts;

#define DIAG_OPTION_WITH_MARSHALLING(...)                                      \
  PARSE_OPTION_WITH_MARSHALLING(Args, *Diags, __VA_ARGS__)
#include "clang/Driver/Options.inc"
#undef DIAG_OPTION_WITH_MARSHALLING

  llvm::sys::Process::UseANSIEscapeCodes(Opts.UseANSIEscapeCodes);

  if (Arg *A =
          Args.getLastArg(OPT_diagnostic_serialized_file, OPT__serialize_diags))
    Opts.DiagnosticSerializationFile = A->getValue();
  Opts.ShowColors = parseShowColorsArgs(Args, DefaultDiagColor);

  Opts.VerifyDiagnostics = Args.hasArg(OPT_verify) || Args.hasArg(OPT_verify_EQ);
  Opts.VerifyPrefixes = Args.getAllArgValues(OPT_verify_EQ);
  if (Args.hasArg(OPT_verify))
    Opts.VerifyPrefixes.push_back("expected");
  // Keep VerifyPrefixes in its original order for the sake of diagnostics, and
  // then sort it to prepare for fast lookup using std::binary_search.
  if (!checkVerifyPrefixes(Opts.VerifyPrefixes, *Diags))
    Opts.VerifyDiagnostics = false;
  else
    llvm::sort(Opts.VerifyPrefixes);
  DiagnosticLevelMask DiagMask = DiagnosticLevelMask::None;
  parseDiagnosticLevelMask(
      "-verify-ignore-unexpected=",
      Args.getAllArgValues(OPT_verify_ignore_unexpected_EQ), *Diags, DiagMask);
  if (Args.hasArg(OPT_verify_ignore_unexpected))
    DiagMask = DiagnosticLevelMask::All;
  Opts.setVerifyIgnoreUnexpected(DiagMask);
  if (Opts.TabStop == 0 || Opts.TabStop > DiagnosticOptions::MaxTabStop) {
    Diags->Report(diag::warn_ignoring_ftabstop_value)
        << Opts.TabStop << DiagnosticOptions::DefaultTabStop;
    Opts.TabStop = DiagnosticOptions::DefaultTabStop;
  }

  addDiagnosticArgs(Args, OPT_W_Group, OPT_W_value_Group, Opts.Warnings);
  addDiagnosticArgs(Args, OPT_R_Group, OPT_R_value_Group, Opts.Remarks);

  return Diags->getNumErrors() == NumErrorsBefore;
}

/// Parse the argument to the -ftest-module-file-extension
/// command-line argument.
///
/// \returns true on error, false on success.
static bool parseTestModuleFileExtensionArg(StringRef Arg,
                                            std::string &BlockName,
                                            unsigned &MajorVersion,
                                            unsigned &MinorVersion,
                                            bool &Hashed,
                                            std::string &UserInfo) {
  SmallVector<StringRef, 5> Args;
  Arg.split(Args, ':', 5);
  if (Args.size() < 5)
    return true;

  BlockName = std::string(Args[0]);
  if (Args[1].getAsInteger(10, MajorVersion)) return true;
  if (Args[2].getAsInteger(10, MinorVersion)) return true;
  if (Args[3].getAsInteger(2, Hashed)) return true;
  if (Args.size() > 4)
    UserInfo = std::string(Args[4]);
  return false;
}

/// Return a table that associates command line option specifiers with the
/// frontend action. Note: The pair {frontend::PluginAction, OPT_plugin} is
/// intentionally missing, as this case is handled separately from other
/// frontend options.
static const auto &getFrontendActionTable() {
  static const std::pair<frontend::ActionKind, unsigned> Table[] = {
      {frontend::ASTDeclList, OPT_ast_list},

      {frontend::ASTDump, OPT_ast_dump_all_EQ},
      {frontend::ASTDump, OPT_ast_dump_all},
      {frontend::ASTDump, OPT_ast_dump_EQ},
      {frontend::ASTDump, OPT_ast_dump},
      {frontend::ASTDump, OPT_ast_dump_lookups},
      {frontend::ASTDump, OPT_ast_dump_decl_types},

      {frontend::ASTPrint, OPT_ast_print},
      {frontend::ASTView, OPT_ast_view},
      {frontend::DumpCompilerOptions, OPT_compiler_options_dump},
      {frontend::DumpRawTokens, OPT_dump_raw_tokens},
      {frontend::DumpTokens, OPT_dump_tokens},
      {frontend::EmitAssembly, OPT_S},
      {frontend::EmitBC, OPT_emit_llvm_bc},
      {frontend::EmitCIR, OPT_emit_cir},
      {frontend::EmitHTML, OPT_emit_html},
      {frontend::EmitLLVM, OPT_emit_llvm},
      {frontend::EmitLLVMOnly, OPT_emit_llvm_only},
      {frontend::EmitCodeGenOnly, OPT_emit_codegen_only},
      {frontend::EmitObj, OPT_emit_obj},
      {frontend::ExtractAPI, OPT_extract_api},

      {frontend::FixIt, OPT_fixit_EQ},
      {frontend::FixIt, OPT_fixit},

      {frontend::GenerateModule, OPT_emit_module},
      {frontend::GenerateModuleInterface, OPT_emit_module_interface},
      {frontend::GenerateReducedModuleInterface,
       OPT_emit_reduced_module_interface},
      {frontend::GenerateHeaderUnit, OPT_emit_header_unit},
      {frontend::GeneratePCH, OPT_emit_pch},
      {frontend::GenerateInterfaceStubs, OPT_emit_interface_stubs},
      {frontend::InitOnly, OPT_init_only},
      {frontend::ParseSyntaxOnly, OPT_fsyntax_only},
      {frontend::ModuleFileInfo, OPT_module_file_info},
      {frontend::VerifyPCH, OPT_verify_pch},
      {frontend::PrintPreamble, OPT_print_preamble},
      {frontend::PrintPreprocessedInput, OPT_E},
      {frontend::TemplightDump, OPT_templight_dump},
      {frontend::RewriteMacros, OPT_rewrite_macros},
      {frontend::RewriteObjC, OPT_rewrite_objc},
      {frontend::RewriteTest, OPT_rewrite_test},
      {frontend::RunAnalysis, OPT_analyze},
      {frontend::MigrateSource, OPT_migrate},
      {frontend::RunPreprocessorOnly, OPT_Eonly},
      {frontend::PrintDependencyDirectivesSourceMinimizerOutput,
       OPT_print_dependency_directives_minimized_source},
  };

  return Table;
}

/// Maps command line option to frontend action.
static std::optional<frontend::ActionKind>
getFrontendAction(OptSpecifier &Opt) {
  for (const auto &ActionOpt : getFrontendActionTable())
    if (ActionOpt.second == Opt.getID())
      return ActionOpt.first;

  return std::nullopt;
}

/// Maps frontend action to command line option.
static std::optional<OptSpecifier>
getProgramActionOpt(frontend::ActionKind ProgramAction) {
  for (const auto &ActionOpt : getFrontendActionTable())
    if (ActionOpt.first == ProgramAction)
      return OptSpecifier(ActionOpt.second);

  return std::nullopt;
}

static void GenerateFrontendArgs(const FrontendOptions &Opts,
                                 ArgumentConsumer Consumer, bool IsHeader) {
  const FrontendOptions &FrontendOpts = Opts;
#define FRONTEND_OPTION_WITH_MARSHALLING(...)                                  \
  GENERATE_OPTION_WITH_MARSHALLING(Consumer, __VA_ARGS__)
#include "clang/Driver/Options.inc"
#undef FRONTEND_OPTION_WITH_MARSHALLING

  std::optional<OptSpecifier> ProgramActionOpt =
      getProgramActionOpt(Opts.ProgramAction);

  // Generating a simple flag covers most frontend actions.
  std::function<void()> GenerateProgramAction = [&]() {
    GenerateArg(Consumer, *ProgramActionOpt);
  };

  if (!ProgramActionOpt) {
    // PluginAction is the only program action handled separately.
    assert(Opts.ProgramAction == frontend::PluginAction &&
           "Frontend action without option.");
    GenerateProgramAction = [&]() {
      GenerateArg(Consumer, OPT_plugin, Opts.ActionName);
    };
  }

  // FIXME: Simplify the complex 'AST dump' command line.
  if (Opts.ProgramAction == frontend::ASTDump) {
    GenerateProgramAction = [&]() {
      // ASTDumpLookups, ASTDumpDeclTypes and ASTDumpFilter are generated via
      // marshalling infrastructure.

      if (Opts.ASTDumpFormat != ADOF_Default) {
        StringRef Format;
        switch (Opts.ASTDumpFormat) {
        case ADOF_Default:
          llvm_unreachable("Default AST dump format.");
        case ADOF_JSON:
          Format = "json";
          break;
        }

        if (Opts.ASTDumpAll)
          GenerateArg(Consumer, OPT_ast_dump_all_EQ, Format);
        if (Opts.ASTDumpDecls)
          GenerateArg(Consumer, OPT_ast_dump_EQ, Format);
      } else {
        if (Opts.ASTDumpAll)
          GenerateArg(Consumer, OPT_ast_dump_all);
        if (Opts.ASTDumpDecls)
          GenerateArg(Consumer, OPT_ast_dump);
      }
    };
  }

  if (Opts.ProgramAction == frontend::FixIt && !Opts.FixItSuffix.empty()) {
    GenerateProgramAction = [&]() {
      GenerateArg(Consumer, OPT_fixit_EQ, Opts.FixItSuffix);
    };
  }

  GenerateProgramAction();

  for (const auto &PluginArgs : Opts.PluginArgs) {
    Option Opt = getDriverOptTable().getOption(OPT_plugin_arg);
    for (const auto &PluginArg : PluginArgs.second)
      denormalizeString(Consumer,
                        Opt.getPrefix() + Opt.getName() + PluginArgs.first,
                        Opt.getKind(), 0, PluginArg);
  }

  for (const auto &Ext : Opts.ModuleFileExtensions)
    if (auto *TestExt = dyn_cast_or_null<TestModuleFileExtension>(Ext.get()))
      GenerateArg(Consumer, OPT_ftest_module_file_extension_EQ, TestExt->str());

  if (!Opts.CodeCompletionAt.FileName.empty())
    GenerateArg(Consumer, OPT_code_completion_at,
                Opts.CodeCompletionAt.ToString());

  for (const auto &Plugin : Opts.Plugins)
    GenerateArg(Consumer, OPT_load, Plugin);

  // ASTDumpDecls and ASTDumpAll already handled with ProgramAction.

  for (const auto &ModuleFile : Opts.ModuleFiles)
    GenerateArg(Consumer, OPT_fmodule_file, ModuleFile);

  if (Opts.AuxTargetCPU)
    GenerateArg(Consumer, OPT_aux_target_cpu, *Opts.AuxTargetCPU);

  if (Opts.AuxTargetFeatures)
    for (const auto &Feature : *Opts.AuxTargetFeatures)
      GenerateArg(Consumer, OPT_aux_target_feature, Feature);

  {
    StringRef Preprocessed = Opts.DashX.isPreprocessed() ? "-cpp-output" : "";
    StringRef ModuleMap =
        Opts.DashX.getFormat() == InputKind::ModuleMap ? "-module-map" : "";
    StringRef HeaderUnit = "";
    switch (Opts.DashX.getHeaderUnitKind()) {
    case InputKind::HeaderUnit_None:
      break;
    case InputKind::HeaderUnit_User:
      HeaderUnit = "-user";
      break;
    case InputKind::HeaderUnit_System:
      HeaderUnit = "-system";
      break;
    case InputKind::HeaderUnit_Abs:
      HeaderUnit = "-header-unit";
      break;
    }
    StringRef Header = IsHeader ? "-header" : "";

    StringRef Lang;
    switch (Opts.DashX.getLanguage()) {
    case Language::C:
      Lang = "c";
      break;
    case Language::OpenCL:
      Lang = "cl";
      break;
    case Language::OpenCLCXX:
      Lang = "clcpp";
      break;
    case Language::CUDA:
      Lang = "cuda";
      break;
    case Language::HIP:
      Lang = "hip";
      break;
    case Language::CXX:
      Lang = "c++";
      break;
    case Language::ObjC:
      Lang = "objective-c";
      break;
    case Language::ObjCXX:
      Lang = "objective-c++";
      break;
    case Language::RenderScript:
      Lang = "renderscript";
      break;
    case Language::Asm:
      Lang = "assembler-with-cpp";
      break;
    case Language::Unknown:
      assert(Opts.DashX.getFormat() == InputKind::Precompiled &&
             "Generating -x argument for unknown language (not precompiled).");
      Lang = "ast";
      break;
    case Language::LLVM_IR:
      Lang = "ir";
      break;
    case Language::HLSL:
      Lang = "hlsl";
      break;
    case Language::CIR:
      Lang = "cir";
      break;
    }

    GenerateArg(Consumer, OPT_x,
                Lang + HeaderUnit + Header + ModuleMap + Preprocessed);
  }

  // OPT_INPUT has a unique class, generate it directly.
  for (const auto &Input : Opts.Inputs)
    Consumer(Input.getFile());
}

static bool ParseFrontendArgs(FrontendOptions &Opts, ArgList &Args,
                              DiagnosticsEngine &Diags, bool &IsHeaderFile) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  FrontendOptions &FrontendOpts = Opts;

#define FRONTEND_OPTION_WITH_MARSHALLING(...)                                  \
  PARSE_OPTION_WITH_MARSHALLING(Args, Diags, __VA_ARGS__)
#include "clang/Driver/Options.inc"
#undef FRONTEND_OPTION_WITH_MARSHALLING

  Opts.ProgramAction = frontend::ParseSyntaxOnly;
  if (const Arg *A = Args.getLastArg(OPT_Action_Group)) {
    OptSpecifier Opt = OptSpecifier(A->getOption().getID());
    std::optional<frontend::ActionKind> ProgramAction = getFrontendAction(Opt);
    assert(ProgramAction && "Option specifier not in Action_Group.");

    if (ProgramAction == frontend::ASTDump &&
        (Opt == OPT_ast_dump_all_EQ || Opt == OPT_ast_dump_EQ)) {
      unsigned Val = llvm::StringSwitch<unsigned>(A->getValue())
                         .CaseLower("default", ADOF_Default)
                         .CaseLower("json", ADOF_JSON)
                         .Default(std::numeric_limits<unsigned>::max());

      if (Val != std::numeric_limits<unsigned>::max())
        Opts.ASTDumpFormat = static_cast<ASTDumpOutputFormat>(Val);
      else {
        Diags.Report(diag::err_drv_invalid_value)
            << A->getAsString(Args) << A->getValue();
        Opts.ASTDumpFormat = ADOF_Default;
      }
    }

    if (ProgramAction == frontend::FixIt && Opt == OPT_fixit_EQ)
      Opts.FixItSuffix = A->getValue();

    if (ProgramAction == frontend::GenerateInterfaceStubs) {
      StringRef ArgStr =
          Args.hasArg(OPT_interface_stub_version_EQ)
              ? Args.getLastArgValue(OPT_interface_stub_version_EQ)
              : "ifs-v1";
      if (ArgStr == "experimental-yaml-elf-v1" ||
          ArgStr == "experimental-ifs-v1" || ArgStr == "experimental-ifs-v2" ||
          ArgStr == "experimental-tapi-elf-v1") {
        std::string ErrorMessage =
            "Invalid interface stub format: " + ArgStr.str() +
            " is deprecated.";
        Diags.Report(diag::err_drv_invalid_value)
            << "Must specify a valid interface stub format type, ie: "
               "-interface-stub-version=ifs-v1"
            << ErrorMessage;
        ProgramAction = frontend::ParseSyntaxOnly;
      } else if (!ArgStr.starts_with("ifs-")) {
        std::string ErrorMessage =
            "Invalid interface stub format: " + ArgStr.str() + ".";
        Diags.Report(diag::err_drv_invalid_value)
            << "Must specify a valid interface stub format type, ie: "
               "-interface-stub-version=ifs-v1"
            << ErrorMessage;
        ProgramAction = frontend::ParseSyntaxOnly;
      }
    }

    Opts.ProgramAction = *ProgramAction;

    // Catch common mistakes when multiple actions are specified for cc1 (e.g.
    // -S -emit-llvm means -emit-llvm while -emit-llvm -S means -S). However, to
    // support driver `-c -Xclang ACTION` (-cc1 -emit-llvm file -main-file-name
    // X ACTION), we suppress the error when the two actions are separated by
    // -main-file-name.
    //
    // As an exception, accept composable -ast-dump*.
    if (!A->getSpelling().starts_with("-ast-dump")) {
      const Arg *SavedAction = nullptr;
      for (const Arg *AA :
           Args.filtered(OPT_Action_Group, OPT_main_file_name)) {
        if (AA->getOption().matches(OPT_main_file_name)) {
          SavedAction = nullptr;
        } else if (!SavedAction) {
          SavedAction = AA;
        } else {
          if (!A->getOption().matches(OPT_ast_dump_EQ))
            Diags.Report(diag::err_fe_invalid_multiple_actions)
                << SavedAction->getSpelling() << A->getSpelling();
          break;
        }
      }
    }
  }

  if (const Arg* A = Args.getLastArg(OPT_plugin)) {
    Opts.Plugins.emplace_back(A->getValue(0));
    Opts.ProgramAction = frontend::PluginAction;
    Opts.ActionName = A->getValue();
  }
  for (const auto *AA : Args.filtered(OPT_plugin_arg))
    Opts.PluginArgs[AA->getValue(0)].emplace_back(AA->getValue(1));

  for (const std::string &Arg :
         Args.getAllArgValues(OPT_ftest_module_file_extension_EQ)) {
    std::string BlockName;
    unsigned MajorVersion;
    unsigned MinorVersion;
    bool Hashed;
    std::string UserInfo;
    if (parseTestModuleFileExtensionArg(Arg, BlockName, MajorVersion,
                                        MinorVersion, Hashed, UserInfo)) {
      Diags.Report(diag::err_test_module_file_extension_format) << Arg;

      continue;
    }

    // Add the testing module file extension.
    Opts.ModuleFileExtensions.push_back(
        std::make_shared<TestModuleFileExtension>(
            BlockName, MajorVersion, MinorVersion, Hashed, UserInfo));
  }

  if (const Arg *A = Args.getLastArg(OPT_code_completion_at)) {
    Opts.CodeCompletionAt =
      ParsedSourceLocation::FromString(A->getValue());
    if (Opts.CodeCompletionAt.FileName.empty())
      Diags.Report(diag::err_drv_invalid_value)
        << A->getAsString(Args) << A->getValue();
  }

  Opts.Plugins = Args.getAllArgValues(OPT_load);
  Opts.ASTDumpDecls = Args.hasArg(OPT_ast_dump, OPT_ast_dump_EQ);
  Opts.ASTDumpAll = Args.hasArg(OPT_ast_dump_all, OPT_ast_dump_all_EQ);
  // Only the -fmodule-file=<file> form.
  for (const auto *A : Args.filtered(OPT_fmodule_file)) {
    StringRef Val = A->getValue();
    if (!Val.contains('='))
      Opts.ModuleFiles.push_back(std::string(Val));
  }

  if (Opts.ProgramAction != frontend::GenerateModule && Opts.IsSystemModule)
    Diags.Report(diag::err_drv_argument_only_allowed_with) << "-fsystem-module"
                                                           << "-emit-module";
  if (Args.hasArg(OPT_fclangir) || Args.hasArg(OPT_emit_cir))
    Opts.UseClangIRPipeline = true;

  if (Args.hasArg(OPT_aux_target_cpu))
    Opts.AuxTargetCPU = std::string(Args.getLastArgValue(OPT_aux_target_cpu));
  if (Args.hasArg(OPT_aux_target_feature))
    Opts.AuxTargetFeatures = Args.getAllArgValues(OPT_aux_target_feature);

  if (Opts.ARCMTAction != FrontendOptions::ARCMT_None &&
      Opts.ObjCMTAction != FrontendOptions::ObjCMT_None) {
    Diags.Report(diag::err_drv_argument_not_allowed_with)
      << "ARC migration" << "ObjC migration";
  }

  InputKind DashX(Language::Unknown);
  if (const Arg *A = Args.getLastArg(OPT_x)) {
    StringRef XValue = A->getValue();

    // Parse suffixes:
    // '<lang>(-[{header-unit,user,system}-]header|[-module-map][-cpp-output])'.
    // FIXME: Supporting '<lang>-header-cpp-output' would be useful.
    bool Preprocessed = XValue.consume_back("-cpp-output");
    bool ModuleMap = XValue.consume_back("-module-map");
    // Detect and consume the header indicator.
    bool IsHeader =
        XValue != "precompiled-header" && XValue.consume_back("-header");

    // If we have c++-{user,system}-header, that indicates a header unit input
    // likewise, if the user put -fmodule-header together with a header with an
    // absolute path (header-unit-header).
    InputKind::HeaderUnitKind HUK = InputKind::HeaderUnit_None;
    if (IsHeader || Preprocessed) {
      if (XValue.consume_back("-header-unit"))
        HUK = InputKind::HeaderUnit_Abs;
      else if (XValue.consume_back("-system"))
        HUK = InputKind::HeaderUnit_System;
      else if (XValue.consume_back("-user"))
        HUK = InputKind::HeaderUnit_User;
    }

    // The value set by this processing is an un-preprocessed source which is
    // not intended to be a module map or header unit.
    IsHeaderFile = IsHeader && !Preprocessed && !ModuleMap &&
                   HUK == InputKind::HeaderUnit_None;

    // Principal languages.
    DashX = llvm::StringSwitch<InputKind>(XValue)
                .Case("c", Language::C)
                .Case("cl", Language::OpenCL)
                .Case("clcpp", Language::OpenCLCXX)
                .Case("cuda", Language::CUDA)
                .Case("hip", Language::HIP)
                .Case("c++", Language::CXX)
                .Case("objective-c", Language::ObjC)
                .Case("objective-c++", Language::ObjCXX)
                .Case("renderscript", Language::RenderScript)
                .Case("hlsl", Language::HLSL)
                .Default(Language::Unknown);

    // "objc[++]-cpp-output" is an acceptable synonym for
    // "objective-c[++]-cpp-output".
    if (DashX.isUnknown() && Preprocessed && !IsHeaderFile && !ModuleMap &&
        HUK == InputKind::HeaderUnit_None)
      DashX = llvm::StringSwitch<InputKind>(XValue)
                  .Case("objc", Language::ObjC)
                  .Case("objc++", Language::ObjCXX)
                  .Default(Language::Unknown);

    // Some special cases cannot be combined with suffixes.
    if (DashX.isUnknown() && !Preprocessed && !IsHeaderFile && !ModuleMap &&
        HUK == InputKind::HeaderUnit_None)
      DashX = llvm::StringSwitch<InputKind>(XValue)
                  .Case("cpp-output", InputKind(Language::C).getPreprocessed())
                  .Case("assembler-with-cpp", Language::Asm)
                  .Cases("ast", "pcm", "precompiled-header",
                         InputKind(Language::Unknown, InputKind::Precompiled))
                  .Case("ir", Language::LLVM_IR)
                  .Case("cir", Language::CIR)
                  .Default(Language::Unknown);

    if (DashX.isUnknown())
      Diags.Report(diag::err_drv_invalid_value)
        << A->getAsString(Args) << A->getValue();

    if (Preprocessed)
      DashX = DashX.getPreprocessed();
    // A regular header is considered mutually exclusive with a header unit.
    if (HUK != InputKind::HeaderUnit_None) {
      DashX = DashX.withHeaderUnit(HUK);
      IsHeaderFile = true;
    } else if (IsHeaderFile)
      DashX = DashX.getHeader();
    if (ModuleMap)
      DashX = DashX.withFormat(InputKind::ModuleMap);
  }

  // '-' is the default input if none is given.
  std::vector<std::string> Inputs = Args.getAllArgValues(OPT_INPUT);
  Opts.Inputs.clear();
  if (Inputs.empty())
    Inputs.push_back("-");

  if (DashX.getHeaderUnitKind() != InputKind::HeaderUnit_None &&
      Inputs.size() > 1)
    Diags.Report(diag::err_drv_header_unit_extra_inputs) << Inputs[1];

  for (unsigned i = 0, e = Inputs.size(); i != e; ++i) {
    InputKind IK = DashX;
    if (IK.isUnknown()) {
      IK = FrontendOptions::getInputKindForExtension(
        StringRef(Inputs[i]).rsplit('.').second);
      // FIXME: Warn on this?
      if (IK.isUnknown())
        IK = Language::C;
      // FIXME: Remove this hack.
      if (i == 0)
        DashX = IK;
    }

    bool IsSystem = false;

    // The -emit-module action implicitly takes a module map.
    if (Opts.ProgramAction == frontend::GenerateModule &&
        IK.getFormat() == InputKind::Source) {
      IK = IK.withFormat(InputKind::ModuleMap);
      IsSystem = Opts.IsSystemModule;
    }

    Opts.Inputs.emplace_back(std::move(Inputs[i]), IK, IsSystem);
  }

  Opts.DashX = DashX;

  return Diags.getNumErrors() == NumErrorsBefore;
}

std::string CompilerInvocation::GetResourcesPath(const char *Argv0,
                                                 void *MainAddr) {
  std::string ClangExecutable =
      llvm::sys::fs::getMainExecutable(Argv0, MainAddr);
  return Driver::GetResourcesPath(ClangExecutable, CLANG_RESOURCE_DIR);
}

static void GenerateHeaderSearchArgs(const HeaderSearchOptions &Opts,
                                     ArgumentConsumer Consumer) {
  const HeaderSearchOptions *HeaderSearchOpts = &Opts;
#define HEADER_SEARCH_OPTION_WITH_MARSHALLING(...)                             \
  GENERATE_OPTION_WITH_MARSHALLING(Consumer, __VA_ARGS__)
#include "clang/Driver/Options.inc"
#undef HEADER_SEARCH_OPTION_WITH_MARSHALLING

  if (Opts.UseLibcxx)
    GenerateArg(Consumer, OPT_stdlib_EQ, "libc++");

  if (!Opts.ModuleCachePath.empty())
    GenerateArg(Consumer, OPT_fmodules_cache_path, Opts.ModuleCachePath);

  for (const auto &File : Opts.PrebuiltModuleFiles)
    GenerateArg(Consumer, OPT_fmodule_file, File.first + "=" + File.second);

  for (const auto &Path : Opts.PrebuiltModulePaths)
    GenerateArg(Consumer, OPT_fprebuilt_module_path, Path);

  for (const auto &Macro : Opts.ModulesIgnoreMacros)
    GenerateArg(Consumer, OPT_fmodules_ignore_macro, Macro.val());

  auto Matches = [](const HeaderSearchOptions::Entry &Entry,
                    llvm::ArrayRef<frontend::IncludeDirGroup> Groups,
                    std::optional<bool> IsFramework,
                    std::optional<bool> IgnoreSysRoot) {
    return llvm::is_contained(Groups, Entry.Group) &&
           (!IsFramework || (Entry.IsFramework == *IsFramework)) &&
           (!IgnoreSysRoot || (Entry.IgnoreSysRoot == *IgnoreSysRoot));
  };

  auto It = Opts.UserEntries.begin();
  auto End = Opts.UserEntries.end();

  // Add -I..., -F..., and -index-header-map options in order.
  for (; It < End && Matches(*It, {frontend::IndexHeaderMap, frontend::Angled},
                             std::nullopt, true);
       ++It) {
    OptSpecifier Opt = [It, Matches]() {
      if (Matches(*It, frontend::IndexHeaderMap, true, true))
        return OPT_F;
      if (Matches(*It, frontend::IndexHeaderMap, false, true))
        return OPT_I;
      if (Matches(*It, frontend::Angled, true, true))
        return OPT_F;
      if (Matches(*It, frontend::Angled, false, true))
        return OPT_I;
      llvm_unreachable("Unexpected HeaderSearchOptions::Entry.");
    }();

    if (It->Group == frontend::IndexHeaderMap)
      GenerateArg(Consumer, OPT_index_header_map);
    GenerateArg(Consumer, Opt, It->Path);
  };

  // Note: some paths that came from "[-iprefix=xx] -iwithprefixbefore=yy" may
  // have already been generated as "-I[xx]yy". If that's the case, their
  // position on command line was such that this has no semantic impact on
  // include paths.
  for (; It < End &&
         Matches(*It, {frontend::After, frontend::Angled}, false, true);
       ++It) {
    OptSpecifier Opt =
        It->Group == frontend::After ? OPT_iwithprefix : OPT_iwithprefixbefore;
    GenerateArg(Consumer, Opt, It->Path);
  }

  // Note: Some paths that came from "-idirafter=xxyy" may have already been
  // generated as "-iwithprefix=xxyy". If that's the case, their position on
  // command line was such that this has no semantic impact on include paths.
  for (; It < End && Matches(*It, {frontend::After}, false, true); ++It)
    GenerateArg(Consumer, OPT_idirafter, It->Path);
  for (; It < End && Matches(*It, {frontend::Quoted}, false, true); ++It)
    GenerateArg(Consumer, OPT_iquote, It->Path);
  for (; It < End && Matches(*It, {frontend::System}, false, std::nullopt);
       ++It)
    GenerateArg(Consumer, It->IgnoreSysRoot ? OPT_isystem : OPT_iwithsysroot,
                It->Path);
  for (; It < End && Matches(*It, {frontend::System}, true, true); ++It)
    GenerateArg(Consumer, OPT_iframework, It->Path);
  for (; It < End && Matches(*It, {frontend::System}, true, false); ++It)
    GenerateArg(Consumer, OPT_iframeworkwithsysroot, It->Path);

  // Add the paths for the various language specific isystem flags.
  for (; It < End && Matches(*It, {frontend::CSystem}, false, true); ++It)
    GenerateArg(Consumer, OPT_c_isystem, It->Path);
  for (; It < End && Matches(*It, {frontend::CXXSystem}, false, true); ++It)
    GenerateArg(Consumer, OPT_cxx_isystem, It->Path);
  for (; It < End && Matches(*It, {frontend::ObjCSystem}, false, true); ++It)
    GenerateArg(Consumer, OPT_objc_isystem, It->Path);
  for (; It < End && Matches(*It, {frontend::ObjCXXSystem}, false, true); ++It)
    GenerateArg(Consumer, OPT_objcxx_isystem, It->Path);

  // Add the internal paths from a driver that detects standard include paths.
  // Note: Some paths that came from "-internal-isystem" arguments may have
  // already been generated as "-isystem". If that's the case, their position on
  // command line was such that this has no semantic impact on include paths.
  for (; It < End &&
         Matches(*It, {frontend::System, frontend::ExternCSystem}, false, true);
       ++It) {
    OptSpecifier Opt = It->Group == frontend::System
                           ? OPT_internal_isystem
                           : OPT_internal_externc_isystem;
    GenerateArg(Consumer, Opt, It->Path);
  }

  assert(It == End && "Unhandled HeaderSearchOption::Entry.");

  // Add the path prefixes which are implicitly treated as being system headers.
  for (const auto &P : Opts.SystemHeaderPrefixes) {
    OptSpecifier Opt = P.IsSystemHeader ? OPT_system_header_prefix
                                        : OPT_no_system_header_prefix;
    GenerateArg(Consumer, Opt, P.Prefix);
  }

  for (const std::string &F : Opts.VFSOverlayFiles)
    GenerateArg(Consumer, OPT_ivfsoverlay, F);
}

static bool ParseHeaderSearchArgs(HeaderSearchOptions &Opts, ArgList &Args,
                                  DiagnosticsEngine &Diags,
                                  const std::string &WorkingDir) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  HeaderSearchOptions *HeaderSearchOpts = &Opts;

#define HEADER_SEARCH_OPTION_WITH_MARSHALLING(...)                             \
  PARSE_OPTION_WITH_MARSHALLING(Args, Diags, __VA_ARGS__)
#include "clang/Driver/Options.inc"
#undef HEADER_SEARCH_OPTION_WITH_MARSHALLING

  if (const Arg *A = Args.getLastArg(OPT_stdlib_EQ))
    Opts.UseLibcxx = (strcmp(A->getValue(), "libc++") == 0);

  // Canonicalize -fmodules-cache-path before storing it.
  SmallString<128> P(Args.getLastArgValue(OPT_fmodules_cache_path));
  if (!(P.empty() || llvm::sys::path::is_absolute(P))) {
    if (WorkingDir.empty())
      llvm::sys::fs::make_absolute(P);
    else
      llvm::sys::fs::make_absolute(WorkingDir, P);
  }
  llvm::sys::path::remove_dots(P);
  Opts.ModuleCachePath = std::string(P);

  // Only the -fmodule-file=<name>=<file> form.
  for (const auto *A : Args.filtered(OPT_fmodule_file)) {
    StringRef Val = A->getValue();
    if (Val.contains('=')) {
      auto Split = Val.split('=');
      Opts.PrebuiltModuleFiles.insert_or_assign(
          std::string(Split.first), std::string(Split.second));
    }
  }
  for (const auto *A : Args.filtered(OPT_fprebuilt_module_path))
    Opts.AddPrebuiltModulePath(A->getValue());

  for (const auto *A : Args.filtered(OPT_fmodules_ignore_macro)) {
    StringRef MacroDef = A->getValue();
    Opts.ModulesIgnoreMacros.insert(
        llvm::CachedHashString(MacroDef.split('=').first));
  }

  // Add -I..., -F..., and -index-header-map options in order.
  bool IsIndexHeaderMap = false;
  bool IsSysrootSpecified =
      Args.hasArg(OPT__sysroot_EQ) || Args.hasArg(OPT_isysroot);

  // Expand a leading `=` to the sysroot if one was passed (and it's not a
  // framework flag).
  auto PrefixHeaderPath = [IsSysrootSpecified,
                           &Opts](const llvm::opt::Arg *A,
                                  bool IsFramework = false) -> std::string {
    assert(A->getNumValues() && "Unexpected empty search path flag!");
    if (IsSysrootSpecified && !IsFramework && A->getValue()[0] == '=') {
      SmallString<32> Buffer;
      llvm::sys::path::append(Buffer, Opts.Sysroot,
                              llvm::StringRef(A->getValue()).substr(1));
      return std::string(Buffer);
    }
    return A->getValue();
  };

  for (const auto *A : Args.filtered(OPT_I, OPT_F, OPT_index_header_map)) {
    if (A->getOption().matches(OPT_index_header_map)) {
      // -index-header-map applies to the next -I or -F.
      IsIndexHeaderMap = true;
      continue;
    }

    frontend::IncludeDirGroup Group =
        IsIndexHeaderMap ? frontend::IndexHeaderMap : frontend::Angled;

    bool IsFramework = A->getOption().matches(OPT_F);
    Opts.AddPath(PrefixHeaderPath(A, IsFramework), Group, IsFramework,
                 /*IgnoreSysroot*/ true);
    IsIndexHeaderMap = false;
  }

  // Add -iprefix/-iwithprefix/-iwithprefixbefore options.
  StringRef Prefix = ""; // FIXME: This isn't the correct default prefix.
  for (const auto *A :
       Args.filtered(OPT_iprefix, OPT_iwithprefix, OPT_iwithprefixbefore)) {
    if (A->getOption().matches(OPT_iprefix))
      Prefix = A->getValue();
    else if (A->getOption().matches(OPT_iwithprefix))
      Opts.AddPath(Prefix.str() + A->getValue(), frontend::After, false, true);
    else
      Opts.AddPath(Prefix.str() + A->getValue(), frontend::Angled, false, true);
  }

  for (const auto *A : Args.filtered(OPT_idirafter))
    Opts.AddPath(PrefixHeaderPath(A), frontend::After, false, true);
  for (const auto *A : Args.filtered(OPT_iquote))
    Opts.AddPath(PrefixHeaderPath(A), frontend::Quoted, false, true);

  for (const auto *A : Args.filtered(OPT_isystem, OPT_iwithsysroot)) {
    if (A->getOption().matches(OPT_iwithsysroot)) {
      Opts.AddPath(A->getValue(), frontend::System, false,
                   /*IgnoreSysRoot=*/false);
      continue;
    }
    Opts.AddPath(PrefixHeaderPath(A), frontend::System, false, true);
  }
  for (const auto *A : Args.filtered(OPT_iframework))
    Opts.AddPath(A->getValue(), frontend::System, true, true);
  for (const auto *A : Args.filtered(OPT_iframeworkwithsysroot))
    Opts.AddPath(A->getValue(), frontend::System, /*IsFramework=*/true,
                 /*IgnoreSysRoot=*/false);

  // Add the paths for the various language specific isystem flags.
  for (const auto *A : Args.filtered(OPT_c_isystem))
    Opts.AddPath(A->getValue(), frontend::CSystem, false, true);
  for (const auto *A : Args.filtered(OPT_cxx_isystem))
    Opts.AddPath(A->getValue(), frontend::CXXSystem, false, true);
  for (const auto *A : Args.filtered(OPT_objc_isystem))
    Opts.AddPath(A->getValue(), frontend::ObjCSystem, false,true);
  for (const auto *A : Args.filtered(OPT_objcxx_isystem))
    Opts.AddPath(A->getValue(), frontend::ObjCXXSystem, false, true);

  // Add the internal paths from a driver that detects standard include paths.
  for (const auto *A :
       Args.filtered(OPT_internal_isystem, OPT_internal_externc_isystem)) {
    frontend::IncludeDirGroup Group = frontend::System;
    if (A->getOption().matches(OPT_internal_externc_isystem))
      Group = frontend::ExternCSystem;
    Opts.AddPath(A->getValue(), Group, false, true);
  }

  // Add the path prefixes which are implicitly treated as being system headers.
  for (const auto *A :
       Args.filtered(OPT_system_header_prefix, OPT_no_system_header_prefix))
    Opts.AddSystemHeaderPrefix(
        A->getValue(), A->getOption().matches(OPT_system_header_prefix));

  for (const auto *A : Args.filtered(OPT_ivfsoverlay, OPT_vfsoverlay))
    Opts.AddVFSOverlayFile(A->getValue());

  return Diags.getNumErrors() == NumErrorsBefore;
}

static void GenerateAPINotesArgs(const APINotesOptions &Opts,
                                 ArgumentConsumer Consumer) {
  if (!Opts.SwiftVersion.empty())
    GenerateArg(Consumer, OPT_fapinotes_swift_version,
                Opts.SwiftVersion.getAsString());

  for (const auto &Path : Opts.ModuleSearchPaths)
    GenerateArg(Consumer, OPT_iapinotes_modules, Path);
}

static void ParseAPINotesArgs(APINotesOptions &Opts, ArgList &Args,
                              DiagnosticsEngine &diags) {
  if (const Arg *A = Args.getLastArg(OPT_fapinotes_swift_version)) {
    if (Opts.SwiftVersion.tryParse(A->getValue()))
      diags.Report(diag::err_drv_invalid_value)
          << A->getAsString(Args) << A->getValue();
  }
  for (const Arg *A : Args.filtered(OPT_iapinotes_modules))
    Opts.ModuleSearchPaths.push_back(A->getValue());
}

static void GeneratePointerAuthArgs(const LangOptions &Opts,
                                    ArgumentConsumer Consumer) {
  if (Opts.PointerAuthIntrinsics)
    GenerateArg(Consumer, OPT_fptrauth_intrinsics);
  if (Opts.PointerAuthCalls)
    GenerateArg(Consumer, OPT_fptrauth_calls);
  if (Opts.PointerAuthReturns)
    GenerateArg(Consumer, OPT_fptrauth_returns);
  if (Opts.PointerAuthIndirectGotos)
    GenerateArg(Consumer, OPT_fptrauth_indirect_gotos);
  if (Opts.PointerAuthAuthTraps)
    GenerateArg(Consumer, OPT_fptrauth_auth_traps);
  if (Opts.PointerAuthVTPtrAddressDiscrimination)
    GenerateArg(Consumer, OPT_fptrauth_vtable_pointer_address_discrimination);
  if (Opts.PointerAuthVTPtrTypeDiscrimination)
    GenerateArg(Consumer, OPT_fptrauth_vtable_pointer_type_discrimination);
  if (Opts.PointerAuthTypeInfoVTPtrDiscrimination)
    GenerateArg(Consumer, OPT_fptrauth_type_info_vtable_pointer_discrimination);

  if (Opts.PointerAuthInitFini)
    GenerateArg(Consumer, OPT_fptrauth_init_fini);
  if (Opts.PointerAuthFunctionTypeDiscrimination)
    GenerateArg(Consumer, OPT_fptrauth_function_pointer_type_discrimination);
}

static void ParsePointerAuthArgs(LangOptions &Opts, ArgList &Args,
                                 DiagnosticsEngine &Diags) {
  Opts.PointerAuthIntrinsics = Args.hasArg(OPT_fptrauth_intrinsics);
  Opts.PointerAuthCalls = Args.hasArg(OPT_fptrauth_calls);
  Opts.PointerAuthReturns = Args.hasArg(OPT_fptrauth_returns);
  Opts.PointerAuthIndirectGotos = Args.hasArg(OPT_fptrauth_indirect_gotos);
  Opts.PointerAuthAuthTraps = Args.hasArg(OPT_fptrauth_auth_traps);
  Opts.PointerAuthVTPtrAddressDiscrimination =
      Args.hasArg(OPT_fptrauth_vtable_pointer_address_discrimination);
  Opts.PointerAuthVTPtrTypeDiscrimination =
      Args.hasArg(OPT_fptrauth_vtable_pointer_type_discrimination);
  Opts.PointerAuthTypeInfoVTPtrDiscrimination =
      Args.hasArg(OPT_fptrauth_type_info_vtable_pointer_discrimination);

  Opts.PointerAuthInitFini = Args.hasArg(OPT_fptrauth_init_fini);
  Opts.PointerAuthFunctionTypeDiscrimination =
      Args.hasArg(OPT_fptrauth_function_pointer_type_discrimination);
}

/// Check if input file kind and language standard are compatible.
static bool IsInputCompatibleWithStandard(InputKind IK,
                                          const LangStandard &S) {
  switch (IK.getLanguage()) {
  case Language::Unknown:
  case Language::LLVM_IR:
  case Language::CIR:
    llvm_unreachable("should not parse language flags for this input");

  case Language::C:
  case Language::ObjC:
  case Language::RenderScript:
    return S.getLanguage() == Language::C;

  case Language::OpenCL:
    return S.getLanguage() == Language::OpenCL ||
           S.getLanguage() == Language::OpenCLCXX;

  case Language::OpenCLCXX:
    return S.getLanguage() == Language::OpenCLCXX;

  case Language::CXX:
  case Language::ObjCXX:
    return S.getLanguage() == Language::CXX;

  case Language::CUDA:
    // FIXME: What -std= values should be permitted for CUDA compilations?
    return S.getLanguage() == Language::CUDA ||
           S.getLanguage() == Language::CXX;

  case Language::HIP:
    return S.getLanguage() == Language::CXX || S.getLanguage() == Language::HIP;

  case Language::Asm:
    // Accept (and ignore) all -std= values.
    // FIXME: The -std= value is not ignored; it affects the tokenization
    // and preprocessing rules if we're preprocessing this asm input.
    return true;

  case Language::HLSL:
    return S.getLanguage() == Language::HLSL;
  }

  llvm_unreachable("unexpected input language");
}

/// Get language name for given input kind.
static StringRef GetInputKindName(InputKind IK) {
  switch (IK.getLanguage()) {
  case Language::C:
    return "C";
  case Language::ObjC:
    return "Objective-C";
  case Language::CXX:
    return "C++";
  case Language::ObjCXX:
    return "Objective-C++";
  case Language::OpenCL:
    return "OpenCL";
  case Language::OpenCLCXX:
    return "C++ for OpenCL";
  case Language::CUDA:
    return "CUDA";
  case Language::RenderScript:
    return "RenderScript";
  case Language::HIP:
    return "HIP";

  case Language::Asm:
    return "Asm";
  case Language::LLVM_IR:
    return "LLVM IR";
  case Language::CIR:
    return "Clang IR";

  case Language::HLSL:
    return "HLSL";

  case Language::Unknown:
    break;
  }
  llvm_unreachable("unknown input language");
}

void CompilerInvocationBase::GenerateLangArgs(const LangOptions &Opts,
                                              ArgumentConsumer Consumer,
                                              const llvm::Triple &T,
                                              InputKind IK) {
  if (IK.getFormat() == InputKind::Precompiled ||
      IK.getLanguage() == Language::LLVM_IR ||
      IK.getLanguage() == Language::CIR) {
    if (Opts.ObjCAutoRefCount)
      GenerateArg(Consumer, OPT_fobjc_arc);
    if (Opts.PICLevel != 0)
      GenerateArg(Consumer, OPT_pic_level, Twine(Opts.PICLevel));
    if (Opts.PIE)
      GenerateArg(Consumer, OPT_pic_is_pie);
    for (StringRef Sanitizer : serializeSanitizerKinds(Opts.Sanitize))
      GenerateArg(Consumer, OPT_fsanitize_EQ, Sanitizer);

    return;
  }

  OptSpecifier StdOpt;
  switch (Opts.LangStd) {
  case LangStandard::lang_opencl10:
  case LangStandard::lang_opencl11:
  case LangStandard::lang_opencl12:
  case LangStandard::lang_opencl20:
  case LangStandard::lang_opencl30:
  case LangStandard::lang_openclcpp10:
  case LangStandard::lang_openclcpp2021:
    StdOpt = OPT_cl_std_EQ;
    break;
  default:
    StdOpt = OPT_std_EQ;
    break;
  }

  auto LangStandard = LangStandard::getLangStandardForKind(Opts.LangStd);
  GenerateArg(Consumer, StdOpt, LangStandard.getName());

  if (Opts.IncludeDefaultHeader)
    GenerateArg(Consumer, OPT_finclude_default_header);
  if (Opts.DeclareOpenCLBuiltins)
    GenerateArg(Consumer, OPT_fdeclare_opencl_builtins);

  const LangOptions *LangOpts = &Opts;

#define LANG_OPTION_WITH_MARSHALLING(...)                                      \
  GENERATE_OPTION_WITH_MARSHALLING(Consumer, __VA_ARGS__)
#include "clang/Driver/Options.inc"
#undef LANG_OPTION_WITH_MARSHALLING

  // The '-fcf-protection=' option is generated by CodeGenOpts generator.

  if (Opts.ObjC) {
    GenerateArg(Consumer, OPT_fobjc_runtime_EQ, Opts.ObjCRuntime.getAsString());

    if (Opts.GC == LangOptions::GCOnly)
      GenerateArg(Consumer, OPT_fobjc_gc_only);
    else if (Opts.GC == LangOptions::HybridGC)
      GenerateArg(Consumer, OPT_fobjc_gc);
    else if (Opts.ObjCAutoRefCount == 1)
      GenerateArg(Consumer, OPT_fobjc_arc);

    if (Opts.ObjCWeakRuntime)
      GenerateArg(Consumer, OPT_fobjc_runtime_has_weak);

    if (Opts.ObjCWeak)
      GenerateArg(Consumer, OPT_fobjc_weak);

    if (Opts.ObjCSubscriptingLegacyRuntime)
      GenerateArg(Consumer, OPT_fobjc_subscripting_legacy_runtime);
  }

  if (Opts.GNUCVersion != 0) {
    unsigned Major = Opts.GNUCVersion / 100 / 100;
    unsigned Minor = (Opts.GNUCVersion / 100) % 100;
    unsigned Patch = Opts.GNUCVersion % 100;
    GenerateArg(Consumer, OPT_fgnuc_version_EQ,
                Twine(Major) + "." + Twine(Minor) + "." + Twine(Patch));
  }

  if (Opts.IgnoreXCOFFVisibility)
    GenerateArg(Consumer, OPT_mignore_xcoff_visibility);

  if (Opts.SignedOverflowBehavior == LangOptions::SOB_Trapping) {
    GenerateArg(Consumer, OPT_ftrapv);
    GenerateArg(Consumer, OPT_ftrapv_handler, Opts.OverflowHandler);
  } else if (Opts.SignedOverflowBehavior == LangOptions::SOB_Defined) {
    GenerateArg(Consumer, OPT_fwrapv);
  }

  if (Opts.MSCompatibilityVersion != 0) {
    unsigned Major = Opts.MSCompatibilityVersion / 10000000;
    unsigned Minor = (Opts.MSCompatibilityVersion / 100000) % 100;
    unsigned Subminor = Opts.MSCompatibilityVersion % 100000;
    GenerateArg(Consumer, OPT_fms_compatibility_version,
                Twine(Major) + "." + Twine(Minor) + "." + Twine(Subminor));
  }

  if ((!Opts.GNUMode && !Opts.MSVCCompat && !Opts.CPlusPlus17 && !Opts.C23) ||
      T.isOSzOS()) {
    if (!Opts.Trigraphs)
      GenerateArg(Consumer, OPT_fno_trigraphs);
  } else {
    if (Opts.Trigraphs)
      GenerateArg(Consumer, OPT_ftrigraphs);
  }

  if (Opts.Blocks && !(Opts.OpenCL && Opts.OpenCLVersion == 200))
    GenerateArg(Consumer, OPT_fblocks);

  if (Opts.ConvergentFunctions &&
      !(Opts.OpenCL || (Opts.CUDA && Opts.CUDAIsDevice) || Opts.SYCLIsDevice ||
        Opts.HLSL))
    GenerateArg(Consumer, OPT_fconvergent_functions);

  if (Opts.NoBuiltin && !Opts.Freestanding)
    GenerateArg(Consumer, OPT_fno_builtin);

  if (!Opts.NoBuiltin)
    for (const auto &Func : Opts.NoBuiltinFuncs)
      GenerateArg(Consumer, OPT_fno_builtin_, Func);

  if (Opts.LongDoubleSize == 128)
    GenerateArg(Consumer, OPT_mlong_double_128);
  else if (Opts.LongDoubleSize == 64)
    GenerateArg(Consumer, OPT_mlong_double_64);
  else if (Opts.LongDoubleSize == 80)
    GenerateArg(Consumer, OPT_mlong_double_80);

  // Not generating '-mrtd', it's just an alias for '-fdefault-calling-conv='.

  // OpenMP was requested via '-fopenmp', not implied by '-fopenmp-simd' or
  // '-fopenmp-targets='.
  if (Opts.OpenMP && !Opts.OpenMPSimd) {
    GenerateArg(Consumer, OPT_fopenmp);

    if (Opts.OpenMP != 51)
      GenerateArg(Consumer, OPT_fopenmp_version_EQ, Twine(Opts.OpenMP));

    if (!Opts.OpenMPUseTLS)
      GenerateArg(Consumer, OPT_fnoopenmp_use_tls);

    if (Opts.OpenMPIsTargetDevice)
      GenerateArg(Consumer, OPT_fopenmp_is_target_device);

    if (Opts.OpenMPIRBuilder)
      GenerateArg(Consumer, OPT_fopenmp_enable_irbuilder);
  }

  if (Opts.OpenMPSimd) {
    GenerateArg(Consumer, OPT_fopenmp_simd);

    if (Opts.OpenMP != 51)
      GenerateArg(Consumer, OPT_fopenmp_version_EQ, Twine(Opts.OpenMP));
  }

  if (Opts.OpenMPThreadSubscription)
    GenerateArg(Consumer, OPT_fopenmp_assume_threads_oversubscription);

  if (Opts.OpenMPTeamSubscription)
    GenerateArg(Consumer, OPT_fopenmp_assume_teams_oversubscription);

  if (Opts.OpenMPTargetDebug != 0)
    GenerateArg(Consumer, OPT_fopenmp_target_debug_EQ,
                Twine(Opts.OpenMPTargetDebug));

  if (Opts.OpenMPCUDANumSMs != 0)
    GenerateArg(Consumer, OPT_fopenmp_cuda_number_of_sm_EQ,
                Twine(Opts.OpenMPCUDANumSMs));

  if (Opts.OpenMPCUDABlocksPerSM != 0)
    GenerateArg(Consumer, OPT_fopenmp_cuda_blocks_per_sm_EQ,
                Twine(Opts.OpenMPCUDABlocksPerSM));

  if (Opts.OpenMPCUDAReductionBufNum != 1024)
    GenerateArg(Consumer, OPT_fopenmp_cuda_teams_reduction_recs_num_EQ,
                Twine(Opts.OpenMPCUDAReductionBufNum));

  if (!Opts.OMPTargetTriples.empty()) {
    std::string Targets;
    llvm::raw_string_ostream OS(Targets);
    llvm::interleave(
        Opts.OMPTargetTriples, OS,
        [&OS](const llvm::Triple &T) { OS << T.str(); }, ",");
    GenerateArg(Consumer, OPT_fopenmp_targets_EQ, OS.str());
  }

  if (!Opts.OMPHostIRFile.empty())
    GenerateArg(Consumer, OPT_fopenmp_host_ir_file_path, Opts.OMPHostIRFile);

  if (Opts.OpenMPCUDAMode)
    GenerateArg(Consumer, OPT_fopenmp_cuda_mode);

  if (Opts.OpenACC) {
    GenerateArg(Consumer, OPT_fopenacc);
    if (!Opts.OpenACCMacroOverride.empty())
      GenerateArg(Consumer, OPT_openacc_macro_override,
                  Opts.OpenACCMacroOverride);
  }

  // The arguments used to set Optimize, OptimizeSize and NoInlineDefine are
  // generated from CodeGenOptions.

  if (Opts.DefaultFPContractMode == LangOptions::FPM_Fast)
    GenerateArg(Consumer, OPT_ffp_contract, "fast");
  else if (Opts.DefaultFPContractMode == LangOptions::FPM_On)
    GenerateArg(Consumer, OPT_ffp_contract, "on");
  else if (Opts.DefaultFPContractMode == LangOptions::FPM_Off)
    GenerateArg(Consumer, OPT_ffp_contract, "off");
  else if (Opts.DefaultFPContractMode == LangOptions::FPM_FastHonorPragmas)
    GenerateArg(Consumer, OPT_ffp_contract, "fast-honor-pragmas");

  for (StringRef Sanitizer : serializeSanitizerKinds(Opts.Sanitize))
    GenerateArg(Consumer, OPT_fsanitize_EQ, Sanitizer);

  // Conflating '-fsanitize-system-ignorelist' and '-fsanitize-ignorelist'.
  for (const std::string &F : Opts.NoSanitizeFiles)
    GenerateArg(Consumer, OPT_fsanitize_ignorelist_EQ, F);

  switch (Opts.getClangABICompat()) {
  case LangOptions::ClangABI::Ver3_8:
    GenerateArg(Consumer, OPT_fclang_abi_compat_EQ, "3.8");
    break;
  case LangOptions::ClangABI::Ver4:
    GenerateArg(Consumer, OPT_fclang_abi_compat_EQ, "4.0");
    break;
  case LangOptions::ClangABI::Ver6:
    GenerateArg(Consumer, OPT_fclang_abi_compat_EQ, "6.0");
    break;
  case LangOptions::ClangABI::Ver7:
    GenerateArg(Consumer, OPT_fclang_abi_compat_EQ, "7.0");
    break;
  case LangOptions::ClangABI::Ver9:
    GenerateArg(Consumer, OPT_fclang_abi_compat_EQ, "9.0");
    break;
  case LangOptions::ClangABI::Ver11:
    GenerateArg(Consumer, OPT_fclang_abi_compat_EQ, "11.0");
    break;
  case LangOptions::ClangABI::Ver12:
    GenerateArg(Consumer, OPT_fclang_abi_compat_EQ, "12.0");
    break;
  case LangOptions::ClangABI::Ver14:
    GenerateArg(Consumer, OPT_fclang_abi_compat_EQ, "14.0");
    break;
  case LangOptions::ClangABI::Ver15:
    GenerateArg(Consumer, OPT_fclang_abi_compat_EQ, "15.0");
    break;
  case LangOptions::ClangABI::Ver17:
    GenerateArg(Consumer, OPT_fclang_abi_compat_EQ, "17.0");
    break;
  case LangOptions::ClangABI::Ver18:
    GenerateArg(Consumer, OPT_fclang_abi_compat_EQ, "18.0");
    break;
  case LangOptions::ClangABI::Latest:
    break;
  }

  if (Opts.getSignReturnAddressScope() ==
      LangOptions::SignReturnAddressScopeKind::All)
    GenerateArg(Consumer, OPT_msign_return_address_EQ, "all");
  else if (Opts.getSignReturnAddressScope() ==
           LangOptions::SignReturnAddressScopeKind::NonLeaf)
    GenerateArg(Consumer, OPT_msign_return_address_EQ, "non-leaf");

  if (Opts.getSignReturnAddressKey() ==
      LangOptions::SignReturnAddressKeyKind::BKey)
    GenerateArg(Consumer, OPT_msign_return_address_key_EQ, "b_key");

  if (Opts.CXXABI)
    GenerateArg(Consumer, OPT_fcxx_abi_EQ,
                TargetCXXABI::getSpelling(*Opts.CXXABI));

  if (Opts.RelativeCXXABIVTables)
    GenerateArg(Consumer, OPT_fexperimental_relative_cxx_abi_vtables);
  else
    GenerateArg(Consumer, OPT_fno_experimental_relative_cxx_abi_vtables);

  if (Opts.UseTargetPathSeparator)
    GenerateArg(Consumer, OPT_ffile_reproducible);
  else
    GenerateArg(Consumer, OPT_fno_file_reproducible);

  for (const auto &MP : Opts.MacroPrefixMap)
    GenerateArg(Consumer, OPT_fmacro_prefix_map_EQ, MP.first + "=" + MP.second);

  if (!Opts.RandstructSeed.empty())
    GenerateArg(Consumer, OPT_frandomize_layout_seed_EQ, Opts.RandstructSeed);
}

bool CompilerInvocation::ParseLangArgs(LangOptions &Opts, ArgList &Args,
                                       InputKind IK, const llvm::Triple &T,
                                       std::vector<std::string> &Includes,
                                       DiagnosticsEngine &Diags) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  if (IK.getFormat() == InputKind::Precompiled ||
      IK.getLanguage() == Language::LLVM_IR ||
      IK.getLanguage() == Language::CIR) {
    // ObjCAAutoRefCount and Sanitize LangOpts are used to setup the
    // PassManager in BackendUtil.cpp. They need to be initialized no matter
    // what the input type is.
    if (Args.hasArg(OPT_fobjc_arc))
      Opts.ObjCAutoRefCount = 1;
    // PICLevel and PIELevel are needed during code generation and this should
    // be set regardless of the input type.
    Opts.PICLevel = getLastArgIntValue(Args, OPT_pic_level, 0, Diags);
    Opts.PIE = Args.hasArg(OPT_pic_is_pie);
    parseSanitizerKinds("-fsanitize=", Args.getAllArgValues(OPT_fsanitize_EQ),
                        Diags, Opts.Sanitize);

    return Diags.getNumErrors() == NumErrorsBefore;
  }

  // Other LangOpts are only initialized when the input is not AST or LLVM IR.
  // FIXME: Should we really be parsing this for an Language::Asm input?

  // FIXME: Cleanup per-file based stuff.
  LangStandard::Kind LangStd = LangStandard::lang_unspecified;
  if (const Arg *A = Args.getLastArg(OPT_std_EQ)) {
    LangStd = LangStandard::getLangKind(A->getValue());
    if (LangStd == LangStandard::lang_unspecified) {
      Diags.Report(diag::err_drv_invalid_value)
        << A->getAsString(Args) << A->getValue();
      // Report supported standards with short description.
      for (unsigned KindValue = 0;
           KindValue != LangStandard::lang_unspecified;
           ++KindValue) {
        const LangStandard &Std = LangStandard::getLangStandardForKind(
          static_cast<LangStandard::Kind>(KindValue));
        if (IsInputCompatibleWithStandard(IK, Std)) {
          auto Diag = Diags.Report(diag::note_drv_use_standard);
          Diag << Std.getName() << Std.getDescription();
          unsigned NumAliases = 0;
#define LANGSTANDARD(id, name, lang, desc, features)
#define LANGSTANDARD_ALIAS(id, alias) \
          if (KindValue == LangStandard::lang_##id) ++NumAliases;
#define LANGSTANDARD_ALIAS_DEPR(id, alias)
#include "clang/Basic/LangStandards.def"
          Diag << NumAliases;
#define LANGSTANDARD(id, name, lang, desc, features)
#define LANGSTANDARD_ALIAS(id, alias) \
          if (KindValue == LangStandard::lang_##id) Diag << alias;
#define LANGSTANDARD_ALIAS_DEPR(id, alias)
#include "clang/Basic/LangStandards.def"
        }
      }
    } else {
      // Valid standard, check to make sure language and standard are
      // compatible.
      const LangStandard &Std = LangStandard::getLangStandardForKind(LangStd);
      if (!IsInputCompatibleWithStandard(IK, Std)) {
        Diags.Report(diag::err_drv_argument_not_allowed_with)
          << A->getAsString(Args) << GetInputKindName(IK);
      }
    }
  }

  // -cl-std only applies for OpenCL language standards.
  // Override the -std option in this case.
  if (const Arg *A = Args.getLastArg(OPT_cl_std_EQ)) {
    LangStandard::Kind OpenCLLangStd
      = llvm::StringSwitch<LangStandard::Kind>(A->getValue())
        .Cases("cl", "CL", LangStandard::lang_opencl10)
        .Cases("cl1.0", "CL1.0", LangStandard::lang_opencl10)
        .Cases("cl1.1", "CL1.1", LangStandard::lang_opencl11)
        .Cases("cl1.2", "CL1.2", LangStandard::lang_opencl12)
        .Cases("cl2.0", "CL2.0", LangStandard::lang_opencl20)
        .Cases("cl3.0", "CL3.0", LangStandard::lang_opencl30)
        .Cases("clc++", "CLC++", LangStandard::lang_openclcpp10)
        .Cases("clc++1.0", "CLC++1.0", LangStandard::lang_openclcpp10)
        .Cases("clc++2021", "CLC++2021", LangStandard::lang_openclcpp2021)
        .Default(LangStandard::lang_unspecified);

    if (OpenCLLangStd == LangStandard::lang_unspecified) {
      Diags.Report(diag::err_drv_invalid_value)
        << A->getAsString(Args) << A->getValue();
    }
    else
      LangStd = OpenCLLangStd;
  }

  // These need to be parsed now. They are used to set OpenCL defaults.
  Opts.IncludeDefaultHeader = Args.hasArg(OPT_finclude_default_header);
  Opts.DeclareOpenCLBuiltins = Args.hasArg(OPT_fdeclare_opencl_builtins);

  LangOptions::setLangDefaults(Opts, IK.getLanguage(), T, Includes, LangStd);

  // The key paths of codegen options defined in Options.td start with
  // "LangOpts->". Let's provide the expected variable name and type.
  LangOptions *LangOpts = &Opts;

#define LANG_OPTION_WITH_MARSHALLING(...)                                      \
  PARSE_OPTION_WITH_MARSHALLING(Args, Diags, __VA_ARGS__)
#include "clang/Driver/Options.inc"
#undef LANG_OPTION_WITH_MARSHALLING

  if (const Arg *A = Args.getLastArg(OPT_fcf_protection_EQ)) {
    StringRef Name = A->getValue();
    if (Name == "full" || Name == "branch") {
      Opts.CFProtectionBranch = 1;
    }
  }

  if ((Args.hasArg(OPT_fsycl_is_device) || Args.hasArg(OPT_fsycl_is_host)) &&
      !Args.hasArg(OPT_sycl_std_EQ)) {
    // If the user supplied -fsycl-is-device or -fsycl-is-host, but failed to
    // provide -sycl-std=, we want to default it to whatever the default SYCL
    // version is. I could not find a way to express this with the options
    // tablegen because we still want this value to be SYCL_None when the user
    // is not in device or host mode.
    Opts.setSYCLVersion(LangOptions::SYCL_Default);
  }

  if (Opts.ObjC) {
    if (Arg *arg = Args.getLastArg(OPT_fobjc_runtime_EQ)) {
      StringRef value = arg->getValue();
      if (Opts.ObjCRuntime.tryParse(value))
        Diags.Report(diag::err_drv_unknown_objc_runtime) << value;
    }

    if (Args.hasArg(OPT_fobjc_gc_only))
      Opts.setGC(LangOptions::GCOnly);
    else if (Args.hasArg(OPT_fobjc_gc))
      Opts.setGC(LangOptions::HybridGC);
    else if (Args.hasArg(OPT_fobjc_arc)) {
      Opts.ObjCAutoRefCount = 1;
      if (!Opts.ObjCRuntime.allowsARC())
        Diags.Report(diag::err_arc_unsupported_on_runtime);
    }

    // ObjCWeakRuntime tracks whether the runtime supports __weak, not
    // whether the feature is actually enabled.  This is predominantly
    // determined by -fobjc-runtime, but we allow it to be overridden
    // from the command line for testing purposes.
    if (Args.hasArg(OPT_fobjc_runtime_has_weak))
      Opts.ObjCWeakRuntime = 1;
    else
      Opts.ObjCWeakRuntime = Opts.ObjCRuntime.allowsWeak();

    // ObjCWeak determines whether __weak is actually enabled.
    // Note that we allow -fno-objc-weak to disable this even in ARC mode.
    if (auto weakArg = Args.getLastArg(OPT_fobjc_weak, OPT_fno_objc_weak)) {
      if (!weakArg->getOption().matches(OPT_fobjc_weak)) {
        assert(!Opts.ObjCWeak);
      } else if (Opts.getGC() != LangOptions::NonGC) {
        Diags.Report(diag::err_objc_weak_with_gc);
      } else if (!Opts.ObjCWeakRuntime) {
        Diags.Report(diag::err_objc_weak_unsupported);
      } else {
        Opts.ObjCWeak = 1;
      }
    } else if (Opts.ObjCAutoRefCount) {
      Opts.ObjCWeak = Opts.ObjCWeakRuntime;
    }

    if (Args.hasArg(OPT_fobjc_subscripting_legacy_runtime))
      Opts.ObjCSubscriptingLegacyRuntime =
        (Opts.ObjCRuntime.getKind() == ObjCRuntime::FragileMacOSX);
  }

  if (Arg *A = Args.getLastArg(options::OPT_fgnuc_version_EQ)) {
    // Check that the version has 1 to 3 components and the minor and patch
    // versions fit in two decimal digits.
    VersionTuple GNUCVer;
    bool Invalid = GNUCVer.tryParse(A->getValue());
    unsigned Major = GNUCVer.getMajor();
    unsigned Minor = GNUCVer.getMinor().value_or(0);
    unsigned Patch = GNUCVer.getSubminor().value_or(0);
    if (Invalid || GNUCVer.getBuild() || Minor >= 100 || Patch >= 100) {
      Diags.Report(diag::err_drv_invalid_value)
          << A->getAsString(Args) << A->getValue();
    }
    Opts.GNUCVersion = Major * 100 * 100 + Minor * 100 + Patch;
  }

  if (T.isOSAIX() && (Args.hasArg(OPT_mignore_xcoff_visibility)))
    Opts.IgnoreXCOFFVisibility = 1;

  if (Args.hasArg(OPT_ftrapv)) {
    Opts.setSignedOverflowBehavior(LangOptions::SOB_Trapping);
    // Set the handler, if one is specified.
    Opts.OverflowHandler =
        std::string(Args.getLastArgValue(OPT_ftrapv_handler));
  }
  else if (Args.hasArg(OPT_fwrapv))
    Opts.setSignedOverflowBehavior(LangOptions::SOB_Defined);

  Opts.MSCompatibilityVersion = 0;
  if (const Arg *A = Args.getLastArg(OPT_fms_compatibility_version)) {
    VersionTuple VT;
    if (VT.tryParse(A->getValue()))
      Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args)
                                                << A->getValue();
    Opts.MSCompatibilityVersion = VT.getMajor() * 10000000 +
                                  VT.getMinor().value_or(0) * 100000 +
                                  VT.getSubminor().value_or(0);
  }

  // Mimicking gcc's behavior, trigraphs are only enabled if -trigraphs
  // is specified, or -std is set to a conforming mode.
  // Trigraphs are disabled by default in C++17 and C23 onwards.
  // For z/OS, trigraphs are enabled by default (without regard to the above).
  Opts.Trigraphs =
      (!Opts.GNUMode && !Opts.MSVCCompat && !Opts.CPlusPlus17 && !Opts.C23) ||
      T.isOSzOS();
  Opts.Trigraphs =
      Args.hasFlag(OPT_ftrigraphs, OPT_fno_trigraphs, Opts.Trigraphs);

  Opts.Blocks = Args.hasArg(OPT_fblocks) || (Opts.OpenCL
    && Opts.OpenCLVersion == 200);

  Opts.ConvergentFunctions = Args.hasArg(OPT_fconvergent_functions) ||
                             Opts.OpenCL || (Opts.CUDA && Opts.CUDAIsDevice) ||
                             Opts.SYCLIsDevice || Opts.HLSL;

  Opts.NoBuiltin = Args.hasArg(OPT_fno_builtin) || Opts.Freestanding;
  if (!Opts.NoBuiltin)
    getAllNoBuiltinFuncValues(Args, Opts.NoBuiltinFuncs);
  if (Arg *A = Args.getLastArg(options::OPT_LongDouble_Group)) {
    if (A->getOption().matches(options::OPT_mlong_double_64))
      Opts.LongDoubleSize = 64;
    else if (A->getOption().matches(options::OPT_mlong_double_80))
      Opts.LongDoubleSize = 80;
    else if (A->getOption().matches(options::OPT_mlong_double_128))
      Opts.LongDoubleSize = 128;
    else
      Opts.LongDoubleSize = 0;
  }
  if (Opts.FastRelaxedMath || Opts.CLUnsafeMath)
    Opts.setDefaultFPContractMode(LangOptions::FPM_Fast);

  llvm::sort(Opts.ModuleFeatures);

  // -mrtd option
  if (Arg *A = Args.getLastArg(OPT_mrtd)) {
    if (Opts.getDefaultCallingConv() != LangOptions::DCC_None)
      Diags.Report(diag::err_drv_argument_not_allowed_with)
          << A->getSpelling() << "-fdefault-calling-conv";
    else {
      switch (T.getArch()) {
      case llvm::Triple::x86:
        Opts.setDefaultCallingConv(LangOptions::DCC_StdCall);
        break;
      case llvm::Triple::m68k:
        Opts.setDefaultCallingConv(LangOptions::DCC_RtdCall);
        break;
      default:
        Diags.Report(diag::err_drv_argument_not_allowed_with)
            << A->getSpelling() << T.getTriple();
      }
    }
  }

  // Check if -fopenmp is specified and set default version to 5.0.
  Opts.OpenMP = Args.hasArg(OPT_fopenmp) ? 51 : 0;
  // Check if -fopenmp-simd is specified.
  bool IsSimdSpecified =
      Args.hasFlag(options::OPT_fopenmp_simd, options::OPT_fno_openmp_simd,
                   /*Default=*/false);
  Opts.OpenMPSimd = !Opts.OpenMP && IsSimdSpecified;
  Opts.OpenMPUseTLS =
      Opts.OpenMP && !Args.hasArg(options::OPT_fnoopenmp_use_tls);
  Opts.OpenMPIsTargetDevice =
      Opts.OpenMP && Args.hasArg(options::OPT_fopenmp_is_target_device);
  Opts.OpenMPIRBuilder =
      Opts.OpenMP && Args.hasArg(options::OPT_fopenmp_enable_irbuilder);
  bool IsTargetSpecified =
      Opts.OpenMPIsTargetDevice || Args.hasArg(options::OPT_fopenmp_targets_EQ);

  Opts.ConvergentFunctions =
      Opts.ConvergentFunctions || Opts.OpenMPIsTargetDevice;

  if (Opts.OpenMP || Opts.OpenMPSimd) {
    if (int Version = getLastArgIntValue(
            Args, OPT_fopenmp_version_EQ,
            (IsSimdSpecified || IsTargetSpecified) ? 51 : Opts.OpenMP, Diags))
      Opts.OpenMP = Version;
    // Provide diagnostic when a given target is not expected to be an OpenMP
    // device or host.
    if (!Opts.OpenMPIsTargetDevice) {
      switch (T.getArch()) {
      default:
        break;
      // Add unsupported host targets here:
      case llvm::Triple::nvptx:
      case llvm::Triple::nvptx64:
        Diags.Report(diag::err_drv_omp_host_target_not_supported) << T.str();
        break;
      }
    }
  }

  // Set the flag to prevent the implementation from emitting device exception
  // handling code for those requiring so.
  if ((Opts.OpenMPIsTargetDevice && (T.isNVPTX() || T.isAMDGCN())) ||
      Opts.OpenCLCPlusPlus) {

    Opts.Exceptions = 0;
    Opts.CXXExceptions = 0;
  }
  if (Opts.OpenMPIsTargetDevice && T.isNVPTX()) {
    Opts.OpenMPCUDANumSMs =
        getLastArgIntValue(Args, options::OPT_fopenmp_cuda_number_of_sm_EQ,
                           Opts.OpenMPCUDANumSMs, Diags);
    Opts.OpenMPCUDABlocksPerSM =
        getLastArgIntValue(Args, options::OPT_fopenmp_cuda_blocks_per_sm_EQ,
                           Opts.OpenMPCUDABlocksPerSM, Diags);
    Opts.OpenMPCUDAReductionBufNum = getLastArgIntValue(
        Args, options::OPT_fopenmp_cuda_teams_reduction_recs_num_EQ,
        Opts.OpenMPCUDAReductionBufNum, Diags);
  }

  // Set the value of the debugging flag used in the new offloading device RTL.
  // Set either by a specific value or to a default if not specified.
  if (Opts.OpenMPIsTargetDevice && (Args.hasArg(OPT_fopenmp_target_debug) ||
                                    Args.hasArg(OPT_fopenmp_target_debug_EQ))) {
    Opts.OpenMPTargetDebug = getLastArgIntValue(
        Args, OPT_fopenmp_target_debug_EQ, Opts.OpenMPTargetDebug, Diags);
    if (!Opts.OpenMPTargetDebug && Args.hasArg(OPT_fopenmp_target_debug))
      Opts.OpenMPTargetDebug = 1;
  }

  if (Opts.OpenMPIsTargetDevice) {
    if (Args.hasArg(OPT_fopenmp_assume_teams_oversubscription))
      Opts.OpenMPTeamSubscription = true;
    if (Args.hasArg(OPT_fopenmp_assume_threads_oversubscription))
      Opts.OpenMPThreadSubscription = true;
  }

  // Get the OpenMP target triples if any.
  if (Arg *A = Args.getLastArg(options::OPT_fopenmp_targets_EQ)) {
    enum ArchPtrSize { Arch16Bit, Arch32Bit, Arch64Bit };
    auto getArchPtrSize = [](const llvm::Triple &T) {
      if (T.isArch16Bit())
        return Arch16Bit;
      if (T.isArch32Bit())
        return Arch32Bit;
      assert(T.isArch64Bit() && "Expected 64-bit architecture");
      return Arch64Bit;
    };

    for (unsigned i = 0; i < A->getNumValues(); ++i) {
      llvm::Triple TT(A->getValue(i));

      if (TT.getArch() == llvm::Triple::UnknownArch ||
          !(TT.getArch() == llvm::Triple::aarch64 || TT.isPPC() ||
            TT.getArch() == llvm::Triple::systemz ||
            TT.getArch() == llvm::Triple::nvptx ||
            TT.getArch() == llvm::Triple::nvptx64 ||
            TT.getArch() == llvm::Triple::amdgcn ||
            TT.getArch() == llvm::Triple::x86 ||
            TT.getArch() == llvm::Triple::x86_64))
        Diags.Report(diag::err_drv_invalid_omp_target) << A->getValue(i);
      else if (getArchPtrSize(T) != getArchPtrSize(TT))
        Diags.Report(diag::err_drv_incompatible_omp_arch)
            << A->getValue(i) << T.str();
      else
        Opts.OMPTargetTriples.push_back(TT);
    }
  }

  // Get OpenMP host file path if any and report if a non existent file is
  // found
  if (Arg *A = Args.getLastArg(options::OPT_fopenmp_host_ir_file_path)) {
    Opts.OMPHostIRFile = A->getValue();
    if (!llvm::sys::fs::exists(Opts.OMPHostIRFile))
      Diags.Report(diag::err_drv_omp_host_ir_file_not_found)
          << Opts.OMPHostIRFile;
  }

  // Set CUDA mode for OpenMP target NVPTX/AMDGCN if specified in options
  Opts.OpenMPCUDAMode = Opts.OpenMPIsTargetDevice &&
                        (T.isNVPTX() || T.isAMDGCN()) &&
                        Args.hasArg(options::OPT_fopenmp_cuda_mode);

  // OpenACC Configuration.
  if (Args.hasArg(options::OPT_fopenacc)) {
    Opts.OpenACC = true;

    if (Arg *A = Args.getLastArg(options::OPT_openacc_macro_override))
      Opts.OpenACCMacroOverride = A->getValue();
  }

  // FIXME: Eliminate this dependency.
  unsigned Opt = getOptimizationLevel(Args, IK, Diags),
       OptSize = getOptimizationLevelSize(Args);
  Opts.Optimize = Opt != 0;
  Opts.OptimizeSize = OptSize != 0;

  // This is the __NO_INLINE__ define, which just depends on things like the
  // optimization level and -fno-inline, not actually whether the backend has
  // inlining enabled.
  Opts.NoInlineDefine = !Opts.Optimize;
  if (Arg *InlineArg = Args.getLastArg(
          options::OPT_finline_functions, options::OPT_finline_hint_functions,
          options::OPT_fno_inline_functions, options::OPT_fno_inline))
    if (InlineArg->getOption().matches(options::OPT_fno_inline))
      Opts.NoInlineDefine = true;

  if (Arg *A = Args.getLastArg(OPT_ffp_contract)) {
    StringRef Val = A->getValue();
    if (Val == "fast")
      Opts.setDefaultFPContractMode(LangOptions::FPM_Fast);
    else if (Val == "on")
      Opts.setDefaultFPContractMode(LangOptions::FPM_On);
    else if (Val == "off")
      Opts.setDefaultFPContractMode(LangOptions::FPM_Off);
    else if (Val == "fast-honor-pragmas")
      Opts.setDefaultFPContractMode(LangOptions::FPM_FastHonorPragmas);
    else
      Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args) << Val;
  }

  // Parse -fsanitize= arguments.
  parseSanitizerKinds("-fsanitize=", Args.getAllArgValues(OPT_fsanitize_EQ),
                      Diags, Opts.Sanitize);
  Opts.NoSanitizeFiles = Args.getAllArgValues(OPT_fsanitize_ignorelist_EQ);
  std::vector<std::string> systemIgnorelists =
      Args.getAllArgValues(OPT_fsanitize_system_ignorelist_EQ);
  Opts.NoSanitizeFiles.insert(Opts.NoSanitizeFiles.end(),
                              systemIgnorelists.begin(),
                              systemIgnorelists.end());

  if (Arg *A = Args.getLastArg(OPT_fclang_abi_compat_EQ)) {
    Opts.setClangABICompat(LangOptions::ClangABI::Latest);

    StringRef Ver = A->getValue();
    std::pair<StringRef, StringRef> VerParts = Ver.split('.');
    unsigned Major, Minor = 0;

    // Check the version number is valid: either 3.x (0 <= x <= 9) or
    // y or y.0 (4 <= y <= current version).
    if (!VerParts.first.starts_with("0") &&
        !VerParts.first.getAsInteger(10, Major) && 3 <= Major &&
        Major <= CLANG_VERSION_MAJOR &&
        (Major == 3
             ? VerParts.second.size() == 1 &&
                   !VerParts.second.getAsInteger(10, Minor)
             : VerParts.first.size() == Ver.size() || VerParts.second == "0")) {
      // Got a valid version number.
      if (Major == 3 && Minor <= 8)
        Opts.setClangABICompat(LangOptions::ClangABI::Ver3_8);
      else if (Major <= 4)
        Opts.setClangABICompat(LangOptions::ClangABI::Ver4);
      else if (Major <= 6)
        Opts.setClangABICompat(LangOptions::ClangABI::Ver6);
      else if (Major <= 7)
        Opts.setClangABICompat(LangOptions::ClangABI::Ver7);
      else if (Major <= 9)
        Opts.setClangABICompat(LangOptions::ClangABI::Ver9);
      else if (Major <= 11)
        Opts.setClangABICompat(LangOptions::ClangABI::Ver11);
      else if (Major <= 12)
        Opts.setClangABICompat(LangOptions::ClangABI::Ver12);
      else if (Major <= 14)
        Opts.setClangABICompat(LangOptions::ClangABI::Ver14);
      else if (Major <= 15)
        Opts.setClangABICompat(LangOptions::ClangABI::Ver15);
      else if (Major <= 17)
        Opts.setClangABICompat(LangOptions::ClangABI::Ver17);
      else if (Major <= 18)
        Opts.setClangABICompat(LangOptions::ClangABI::Ver18);
    } else if (Ver != "latest") {
      Diags.Report(diag::err_drv_invalid_value)
          << A->getAsString(Args) << A->getValue();
    }
  }

  if (Arg *A = Args.getLastArg(OPT_msign_return_address_EQ)) {
    StringRef SignScope = A->getValue();

    if (SignScope.equals_insensitive("none"))
      Opts.setSignReturnAddressScope(
          LangOptions::SignReturnAddressScopeKind::None);
    else if (SignScope.equals_insensitive("all"))
      Opts.setSignReturnAddressScope(
          LangOptions::SignReturnAddressScopeKind::All);
    else if (SignScope.equals_insensitive("non-leaf"))
      Opts.setSignReturnAddressScope(
          LangOptions::SignReturnAddressScopeKind::NonLeaf);
    else
      Diags.Report(diag::err_drv_invalid_value)
          << A->getAsString(Args) << SignScope;

    if (Arg *A = Args.getLastArg(OPT_msign_return_address_key_EQ)) {
      StringRef SignKey = A->getValue();
      if (!SignScope.empty() && !SignKey.empty()) {
        if (SignKey == "a_key")
          Opts.setSignReturnAddressKey(
              LangOptions::SignReturnAddressKeyKind::AKey);
        else if (SignKey == "b_key")
          Opts.setSignReturnAddressKey(
              LangOptions::SignReturnAddressKeyKind::BKey);
        else
          Diags.Report(diag::err_drv_invalid_value)
              << A->getAsString(Args) << SignKey;
      }
    }
  }

  // The value can be empty, which indicates the system default should be used.
  StringRef CXXABI = Args.getLastArgValue(OPT_fcxx_abi_EQ);
  if (!CXXABI.empty()) {
    if (!TargetCXXABI::isABI(CXXABI)) {
      Diags.Report(diag::err_invalid_cxx_abi) << CXXABI;
    } else {
      auto Kind = TargetCXXABI::getKind(CXXABI);
      if (!TargetCXXABI::isSupportedCXXABI(T, Kind))
        Diags.Report(diag::err_unsupported_cxx_abi) << CXXABI << T.str();
      else
        Opts.CXXABI = Kind;
    }
  }

  Opts.RelativeCXXABIVTables =
      Args.hasFlag(options::OPT_fexperimental_relative_cxx_abi_vtables,
                   options::OPT_fno_experimental_relative_cxx_abi_vtables,
                   TargetCXXABI::usesRelativeVTables(T));

  // RTTI is on by default.
  bool HasRTTI = !Args.hasArg(options::OPT_fno_rtti);
  Opts.OmitVTableRTTI =
      Args.hasFlag(options::OPT_fexperimental_omit_vtable_rtti,
                   options::OPT_fno_experimental_omit_vtable_rtti, false);
  if (Opts.OmitVTableRTTI && HasRTTI)
    Diags.Report(diag::err_drv_using_omit_rtti_component_without_no_rtti);

  for (const auto &A : Args.getAllArgValues(OPT_fmacro_prefix_map_EQ)) {
    auto Split = StringRef(A).split('=');
    Opts.MacroPrefixMap.insert(
        {std::string(Split.first), std::string(Split.second)});
  }

  Opts.UseTargetPathSeparator =
      !Args.getLastArg(OPT_fno_file_reproducible) &&
      (Args.getLastArg(OPT_ffile_compilation_dir_EQ) ||
       Args.getLastArg(OPT_fmacro_prefix_map_EQ) ||
       Args.getLastArg(OPT_ffile_reproducible));

  // Error if -mvscale-min is unbounded.
  if (Arg *A = Args.getLastArg(options::OPT_mvscale_min_EQ)) {
    unsigned VScaleMin;
    if (StringRef(A->getValue()).getAsInteger(10, VScaleMin) || VScaleMin == 0)
      Diags.Report(diag::err_cc1_unbounded_vscale_min);
  }

  if (const Arg *A = Args.getLastArg(OPT_frandomize_layout_seed_file_EQ)) {
    std::ifstream SeedFile(A->getValue(0));

    if (!SeedFile.is_open())
      Diags.Report(diag::err_drv_cannot_open_randomize_layout_seed_file)
          << A->getValue(0);

    std::getline(SeedFile, Opts.RandstructSeed);
  }

  if (const Arg *A = Args.getLastArg(OPT_frandomize_layout_seed_EQ))
    Opts.RandstructSeed = A->getValue(0);

  // Validate options for HLSL
  if (Opts.HLSL) {
    // TODO: Revisit restricting SPIR-V to logical once we've figured out how to
    // handle PhysicalStorageBuffer64 memory model
    if (T.isDXIL() || T.isSPIRVLogical()) {
      enum { ShaderModel, VulkanEnv, ShaderStage };
      enum { OS, Environment };

      int ExpectedOS = T.isSPIRVLogical() ? VulkanEnv : ShaderModel;

      if (T.getOSName().empty()) {
        Diags.Report(diag::err_drv_hlsl_bad_shader_required_in_target)
            << ExpectedOS << OS << T.str();
      } else if (T.getEnvironmentName().empty()) {
        Diags.Report(diag::err_drv_hlsl_bad_shader_required_in_target)
            << ShaderStage << Environment << T.str();
      } else if (!T.isShaderStageEnvironment()) {
        Diags.Report(diag::err_drv_hlsl_bad_shader_unsupported)
            << ShaderStage << T.getEnvironmentName() << T.str();
      }

      if (T.isDXIL()) {
        if (!T.isShaderModelOS() || T.getOSVersion() == VersionTuple(0)) {
          Diags.Report(diag::err_drv_hlsl_bad_shader_unsupported)
              << ShaderModel << T.getOSName() << T.str();
        }
        // Validate that if fnative-half-type is given, that
        // the language standard is at least hlsl2018, and that
        // the target shader model is at least 6.2.
        if (Args.getLastArg(OPT_fnative_half_type)) {
          const LangStandard &Std =
              LangStandard::getLangStandardForKind(Opts.LangStd);
          if (!(Opts.LangStd >= LangStandard::lang_hlsl2018 &&
                T.getOSVersion() >= VersionTuple(6, 2)))
            Diags.Report(diag::err_drv_hlsl_16bit_types_unsupported)
                << "-enable-16bit-types" << true << Std.getName()
                << T.getOSVersion().getAsString();
        }
      } else if (T.isSPIRVLogical()) {
        if (!T.isVulkanOS() || T.getVulkanVersion() == VersionTuple(0)) {
          Diags.Report(diag::err_drv_hlsl_bad_shader_unsupported)
              << VulkanEnv << T.getOSName() << T.str();
        }
        if (Args.getLastArg(OPT_fnative_half_type)) {
          const LangStandard &Std =
              LangStandard::getLangStandardForKind(Opts.LangStd);
          if (!(Opts.LangStd >= LangStandard::lang_hlsl2018))
            Diags.Report(diag::err_drv_hlsl_16bit_types_unsupported)
                << "-fnative-half-type" << false << Std.getName();
        }
      } else {
        llvm_unreachable("expected DXIL or SPIR-V target");
      }
    } else
      Diags.Report(diag::err_drv_hlsl_unsupported_target) << T.str();
  }

  return Diags.getNumErrors() == NumErrorsBefore;
}

static bool isStrictlyPreprocessorAction(frontend::ActionKind Action) {
  switch (Action) {
  case frontend::ASTDeclList:
  case frontend::ASTDump:
  case frontend::ASTPrint:
  case frontend::ASTView:
  case frontend::EmitAssembly:
  case frontend::EmitBC:
  case frontend::EmitCIR:
  case frontend::EmitHTML:
  case frontend::EmitLLVM:
  case frontend::EmitLLVMOnly:
  case frontend::EmitCodeGenOnly:
  case frontend::EmitObj:
  case frontend::ExtractAPI:
  case frontend::FixIt:
  case frontend::GenerateModule:
  case frontend::GenerateModuleInterface:
  case frontend::GenerateReducedModuleInterface:
  case frontend::GenerateHeaderUnit:
  case frontend::GeneratePCH:
  case frontend::GenerateInterfaceStubs:
  case frontend::ParseSyntaxOnly:
  case frontend::ModuleFileInfo:
  case frontend::VerifyPCH:
  case frontend::PluginAction:
  case frontend::RewriteObjC:
  case frontend::RewriteTest:
  case frontend::RunAnalysis:
  case frontend::TemplightDump:
  case frontend::MigrateSource:
    return false;

  case frontend::DumpCompilerOptions:
  case frontend::DumpRawTokens:
  case frontend::DumpTokens:
  case frontend::InitOnly:
  case frontend::PrintPreamble:
  case frontend::PrintPreprocessedInput:
  case frontend::RewriteMacros:
  case frontend::RunPreprocessorOnly:
  case frontend::PrintDependencyDirectivesSourceMinimizerOutput:
    return true;
  }
  llvm_unreachable("invalid frontend action");
}

static void GeneratePreprocessorArgs(const PreprocessorOptions &Opts,
                                     ArgumentConsumer Consumer,
                                     const LangOptions &LangOpts,
                                     const FrontendOptions &FrontendOpts,
                                     const CodeGenOptions &CodeGenOpts) {
  const PreprocessorOptions *PreprocessorOpts = &Opts;

#define PREPROCESSOR_OPTION_WITH_MARSHALLING(...)                              \
  GENERATE_OPTION_WITH_MARSHALLING(Consumer, __VA_ARGS__)
#include "clang/Driver/Options.inc"
#undef PREPROCESSOR_OPTION_WITH_MARSHALLING

  if (Opts.PCHWithHdrStop && !Opts.PCHWithHdrStopCreate)
    GenerateArg(Consumer, OPT_pch_through_hdrstop_use);

  for (const auto &D : Opts.DeserializedPCHDeclsToErrorOn)
    GenerateArg(Consumer, OPT_error_on_deserialized_pch_decl, D);

  if (Opts.PrecompiledPreambleBytes != std::make_pair(0u, false))
    GenerateArg(Consumer, OPT_preamble_bytes_EQ,
                Twine(Opts.PrecompiledPreambleBytes.first) + "," +
                    (Opts.PrecompiledPreambleBytes.second ? "1" : "0"));

  for (const auto &M : Opts.Macros) {
    // Don't generate __CET__ macro definitions. They are implied by the
    // -fcf-protection option that is generated elsewhere.
    if (M.first == "__CET__=1" && !M.second &&
        !CodeGenOpts.CFProtectionReturn && CodeGenOpts.CFProtectionBranch)
      continue;
    if (M.first == "__CET__=2" && !M.second && CodeGenOpts.CFProtectionReturn &&
        !CodeGenOpts.CFProtectionBranch)
      continue;
    if (M.first == "__CET__=3" && !M.second && CodeGenOpts.CFProtectionReturn &&
        CodeGenOpts.CFProtectionBranch)
      continue;

    GenerateArg(Consumer, M.second ? OPT_U : OPT_D, M.first);
  }

  for (const auto &I : Opts.Includes) {
    // Don't generate OpenCL includes. They are implied by other flags that are
    // generated elsewhere.
    if (LangOpts.OpenCL && LangOpts.IncludeDefaultHeader &&
        ((LangOpts.DeclareOpenCLBuiltins && I == "opencl-c-base.h") ||
         I == "opencl-c.h"))
      continue;
    // Don't generate HLSL includes. They are implied by other flags that are
    // generated elsewhere.
    if (LangOpts.HLSL && I == "hlsl.h")
      continue;

    GenerateArg(Consumer, OPT_include, I);
  }

  for (const auto &CI : Opts.ChainedIncludes)
    GenerateArg(Consumer, OPT_chain_include, CI);

  for (const auto &RF : Opts.RemappedFiles)
    GenerateArg(Consumer, OPT_remap_file, RF.first + ";" + RF.second);

  if (Opts.SourceDateEpoch)
    GenerateArg(Consumer, OPT_source_date_epoch, Twine(*Opts.SourceDateEpoch));

  if (Opts.DefineTargetOSMacros)
    GenerateArg(Consumer, OPT_fdefine_target_os_macros);

  for (const auto &EmbedEntry : Opts.EmbedEntries)
    GenerateArg(Consumer, OPT_embed_dir_EQ, EmbedEntry);

  // Don't handle LexEditorPlaceholders. It is implied by the action that is
  // generated elsewhere.
}

static bool ParsePreprocessorArgs(PreprocessorOptions &Opts, ArgList &Args,
                                  DiagnosticsEngine &Diags,
                                  frontend::ActionKind Action,
                                  const FrontendOptions &FrontendOpts) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  PreprocessorOptions *PreprocessorOpts = &Opts;

#define PREPROCESSOR_OPTION_WITH_MARSHALLING(...)                              \
  PARSE_OPTION_WITH_MARSHALLING(Args, Diags, __VA_ARGS__)
#include "clang/Driver/Options.inc"
#undef PREPROCESSOR_OPTION_WITH_MARSHALLING

  Opts.PCHWithHdrStop = Args.hasArg(OPT_pch_through_hdrstop_create) ||
                        Args.hasArg(OPT_pch_through_hdrstop_use);

  for (const auto *A : Args.filtered(OPT_error_on_deserialized_pch_decl))
    Opts.DeserializedPCHDeclsToErrorOn.insert(A->getValue());

  if (const Arg *A = Args.getLastArg(OPT_preamble_bytes_EQ)) {
    StringRef Value(A->getValue());
    size_t Comma = Value.find(',');
    unsigned Bytes = 0;
    unsigned EndOfLine = 0;

    if (Comma == StringRef::npos ||
        Value.substr(0, Comma).getAsInteger(10, Bytes) ||
        Value.substr(Comma + 1).getAsInteger(10, EndOfLine))
      Diags.Report(diag::err_drv_preamble_format);
    else {
      Opts.PrecompiledPreambleBytes.first = Bytes;
      Opts.PrecompiledPreambleBytes.second = (EndOfLine != 0);
    }
  }

  // Add the __CET__ macro if a CFProtection option is set.
  if (const Arg *A = Args.getLastArg(OPT_fcf_protection_EQ)) {
    StringRef Name = A->getValue();
    if (Name == "branch")
      Opts.addMacroDef("__CET__=1");
    else if (Name == "return")
      Opts.addMacroDef("__CET__=2");
    else if (Name == "full")
      Opts.addMacroDef("__CET__=3");
  }

  // Add macros from the command line.
  for (const auto *A : Args.filtered(OPT_D, OPT_U)) {
    if (A->getOption().matches(OPT_D))
      Opts.addMacroDef(A->getValue());
    else
      Opts.addMacroUndef(A->getValue());
  }

  // Add the ordered list of -includes.
  for (const auto *A : Args.filtered(OPT_include))
    Opts.Includes.emplace_back(A->getValue());

  for (const auto *A : Args.filtered(OPT_chain_include))
    Opts.ChainedIncludes.emplace_back(A->getValue());

  for (const auto *A : Args.filtered(OPT_remap_file)) {
    std::pair<StringRef, StringRef> Split = StringRef(A->getValue()).split(';');

    if (Split.second.empty()) {
      Diags.Report(diag::err_drv_invalid_remap_file) << A->getAsString(Args);
      continue;
    }

    Opts.addRemappedFile(Split.first, Split.second);
  }

  if (const Arg *A = Args.getLastArg(OPT_source_date_epoch)) {
    StringRef Epoch = A->getValue();
    // SOURCE_DATE_EPOCH, if specified, must be a non-negative decimal integer.
    // On time64 systems, pick 253402300799 (the UNIX timestamp of
    // 9999-12-31T23:59:59Z) as the upper bound.
    const uint64_t MaxTimestamp =
        std::min<uint64_t>(std::numeric_limits<time_t>::max(), 253402300799);
    uint64_t V;
    if (Epoch.getAsInteger(10, V) || V > MaxTimestamp) {
      Diags.Report(diag::err_fe_invalid_source_date_epoch)
          << Epoch << MaxTimestamp;
    } else {
      Opts.SourceDateEpoch = V;
    }
  }

  for (const auto *A : Args.filtered(OPT_embed_dir_EQ)) {
    StringRef Val = A->getValue();
    Opts.EmbedEntries.push_back(std::string(Val));
  }

  // Always avoid lexing editor placeholders when we're just running the
  // preprocessor as we never want to emit the
  // "editor placeholder in source file" error in PP only mode.
  if (isStrictlyPreprocessorAction(Action))
    Opts.LexEditorPlaceholders = false;

  Opts.DefineTargetOSMacros =
      Args.hasFlag(OPT_fdefine_target_os_macros,
                   OPT_fno_define_target_os_macros, Opts.DefineTargetOSMacros);

  return Diags.getNumErrors() == NumErrorsBefore;
}

static void
GeneratePreprocessorOutputArgs(const PreprocessorOutputOptions &Opts,
                               ArgumentConsumer Consumer,
                               frontend::ActionKind Action) {
  const PreprocessorOutputOptions &PreprocessorOutputOpts = Opts;

#define PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING(...)                       \
  GENERATE_OPTION_WITH_MARSHALLING(Consumer, __VA_ARGS__)
#include "clang/Driver/Options.inc"
#undef PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING

  bool Generate_dM = isStrictlyPreprocessorAction(Action) && !Opts.ShowCPP;
  if (Generate_dM)
    GenerateArg(Consumer, OPT_dM);
  if (!Generate_dM && Opts.ShowMacros)
    GenerateArg(Consumer, OPT_dD);
  if (Opts.DirectivesOnly)
    GenerateArg(Consumer, OPT_fdirectives_only);
}

static bool ParsePreprocessorOutputArgs(PreprocessorOutputOptions &Opts,
                                        ArgList &Args, DiagnosticsEngine &Diags,
                                        frontend::ActionKind Action) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  PreprocessorOutputOptions &PreprocessorOutputOpts = Opts;

#define PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING(...)                       \
  PARSE_OPTION_WITH_MARSHALLING(Args, Diags, __VA_ARGS__)
#include "clang/Driver/Options.inc"
#undef PREPROCESSOR_OUTPUT_OPTION_WITH_MARSHALLING

  Opts.ShowCPP = isStrictlyPreprocessorAction(Action) && !Args.hasArg(OPT_dM);
  Opts.ShowMacros = Args.hasArg(OPT_dM) || Args.hasArg(OPT_dD);
  Opts.DirectivesOnly = Args.hasArg(OPT_fdirectives_only);

  return Diags.getNumErrors() == NumErrorsBefore;
}

static void GenerateTargetArgs(const TargetOptions &Opts,
                               ArgumentConsumer Consumer) {
  const TargetOptions *TargetOpts = &Opts;
#define TARGET_OPTION_WITH_MARSHALLING(...)                                    \
  GENERATE_OPTION_WITH_MARSHALLING(Consumer, __VA_ARGS__)
#include "clang/Driver/Options.inc"
#undef TARGET_OPTION_WITH_MARSHALLING

  if (!Opts.SDKVersion.empty())
    GenerateArg(Consumer, OPT_target_sdk_version_EQ,
                Opts.SDKVersion.getAsString());
  if (!Opts.DarwinTargetVariantSDKVersion.empty())
    GenerateArg(Consumer, OPT_darwin_target_variant_sdk_version_EQ,
                Opts.DarwinTargetVariantSDKVersion.getAsString());
}

static bool ParseTargetArgs(TargetOptions &Opts, ArgList &Args,
                            DiagnosticsEngine &Diags) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  TargetOptions *TargetOpts = &Opts;

#define TARGET_OPTION_WITH_MARSHALLING(...)                                    \
  PARSE_OPTION_WITH_MARSHALLING(Args, Diags, __VA_ARGS__)
#include "clang/Driver/Options.inc"
#undef TARGET_OPTION_WITH_MARSHALLING

  if (Arg *A = Args.getLastArg(options::OPT_target_sdk_version_EQ)) {
    llvm::VersionTuple Version;
    if (Version.tryParse(A->getValue()))
      Diags.Report(diag::err_drv_invalid_value)
          << A->getAsString(Args) << A->getValue();
    else
      Opts.SDKVersion = Version;
  }
  if (Arg *A =
          Args.getLastArg(options::OPT_darwin_target_variant_sdk_version_EQ)) {
    llvm::VersionTuple Version;
    if (Version.tryParse(A->getValue()))
      Diags.Report(diag::err_drv_invalid_value)
          << A->getAsString(Args) << A->getValue();
    else
      Opts.DarwinTargetVariantSDKVersion = Version;
  }

  return Diags.getNumErrors() == NumErrorsBefore;
}

bool CompilerInvocation::CreateFromArgsImpl(
    CompilerInvocation &Res, ArrayRef<const char *> CommandLineArgs,
    DiagnosticsEngine &Diags, const char *Argv0) {
  unsigned NumErrorsBefore = Diags.getNumErrors();

  // Parse the arguments.
  const OptTable &Opts = getDriverOptTable();
  llvm::opt::Visibility VisibilityMask(options::CC1Option);
  unsigned MissingArgIndex, MissingArgCount;
  InputArgList Args = Opts.ParseArgs(CommandLineArgs, MissingArgIndex,
                                     MissingArgCount, VisibilityMask);
  LangOptions &LangOpts = Res.getLangOpts();

  // Check for missing argument error.
  if (MissingArgCount)
    Diags.Report(diag::err_drv_missing_argument)
        << Args.getArgString(MissingArgIndex) << MissingArgCount;

  // Issue errors on unknown arguments.
  for (const auto *A : Args.filtered(OPT_UNKNOWN)) {
    auto ArgString = A->getAsString(Args);
    std::string Nearest;
    if (Opts.findNearest(ArgString, Nearest, VisibilityMask) > 1)
      Diags.Report(diag::err_drv_unknown_argument) << ArgString;
    else
      Diags.Report(diag::err_drv_unknown_argument_with_suggestion)
          << ArgString << Nearest;
  }

  ParseFileSystemArgs(Res.getFileSystemOpts(), Args, Diags);
  ParseMigratorArgs(Res.getMigratorOpts(), Args, Diags);
  ParseAnalyzerArgs(Res.getAnalyzerOpts(), Args, Diags);
  ParseDiagnosticArgs(Res.getDiagnosticOpts(), Args, &Diags,
                      /*DefaultDiagColor=*/false);
  ParseFrontendArgs(Res.getFrontendOpts(), Args, Diags, LangOpts.IsHeaderFile);
  // FIXME: We shouldn't have to pass the DashX option around here
  InputKind DashX = Res.getFrontendOpts().DashX;
  ParseTargetArgs(Res.getTargetOpts(), Args, Diags);
  llvm::Triple T(Res.getTargetOpts().Triple);
  ParseHeaderSearchArgs(Res.getHeaderSearchOpts(), Args, Diags,
                        Res.getFileSystemOpts().WorkingDir);
  ParseAPINotesArgs(Res.getAPINotesOpts(), Args, Diags);

  ParsePointerAuthArgs(LangOpts, Args, Diags);

  ParseLangArgs(LangOpts, Args, DashX, T, Res.getPreprocessorOpts().Includes,
                Diags);
  if (Res.getFrontendOpts().ProgramAction == frontend::RewriteObjC)
    LangOpts.ObjCExceptions = 1;

  for (auto Warning : Res.getDiagnosticOpts().Warnings) {
    if (Warning == "misexpect" &&
        !Diags.isIgnored(diag::warn_profile_data_misexpect, SourceLocation())) {
      Res.getCodeGenOpts().MisExpect = true;
    }
  }

  if (LangOpts.CUDA) {
    // During CUDA device-side compilation, the aux triple is the
    // triple used for host compilation.
    if (LangOpts.CUDAIsDevice)
      Res.getTargetOpts().HostTriple = Res.getFrontendOpts().AuxTriple;
  }

  // Set the triple of the host for OpenMP device compile.
  if (LangOpts.OpenMPIsTargetDevice)
    Res.getTargetOpts().HostTriple = Res.getFrontendOpts().AuxTriple;

  ParseCodeGenArgs(Res.getCodeGenOpts(), Args, DashX, Diags, T,
                   Res.getFrontendOpts().OutputFile, LangOpts);

  // FIXME: Override value name discarding when asan or msan is used because the
  // backend passes depend on the name of the alloca in order to print out
  // names.
  Res.getCodeGenOpts().DiscardValueNames &=
      !LangOpts.Sanitize.has(SanitizerKind::Address) &&
      !LangOpts.Sanitize.has(SanitizerKind::KernelAddress) &&
      !LangOpts.Sanitize.has(SanitizerKind::Memory) &&
      !LangOpts.Sanitize.has(SanitizerKind::KernelMemory);

  ParsePreprocessorArgs(Res.getPreprocessorOpts(), Args, Diags,
                        Res.getFrontendOpts().ProgramAction,
                        Res.getFrontendOpts());
  ParsePreprocessorOutputArgs(Res.getPreprocessorOutputOpts(), Args, Diags,
                              Res.getFrontendOpts().ProgramAction);

  ParseDependencyOutputArgs(Res.getDependencyOutputOpts(), Args, Diags,
                            Res.getFrontendOpts().ProgramAction,
                            Res.getPreprocessorOutputOpts().ShowLineMarkers);
  if (!Res.getDependencyOutputOpts().OutputFile.empty() &&
      Res.getDependencyOutputOpts().Targets.empty())
    Diags.Report(diag::err_fe_dependency_file_requires_MT);

  // If sanitizer is enabled, disable OPT_ffine_grained_bitfield_accesses.
  if (Res.getCodeGenOpts().FineGrainedBitfieldAccesses &&
      !Res.getLangOpts().Sanitize.empty()) {
    Res.getCodeGenOpts().FineGrainedBitfieldAccesses = false;
    Diags.Report(diag::warn_drv_fine_grained_bitfield_accesses_ignored);
  }

  // Store the command-line for using in the CodeView backend.
  if (Res.getCodeGenOpts().CodeViewCommandLine) {
    Res.getCodeGenOpts().Argv0 = Argv0;
    append_range(Res.getCodeGenOpts().CommandLineArgs, CommandLineArgs);
  }

  // Set PGOOptions. Need to create a temporary VFS to read the profile
  // to determine the PGO type.
  if (!Res.getCodeGenOpts().ProfileInstrumentUsePath.empty()) {
    auto FS =
        createVFSFromOverlayFiles(Res.getHeaderSearchOpts().VFSOverlayFiles,
                                  Diags, llvm::vfs::getRealFileSystem());
    setPGOUseInstrumentor(Res.getCodeGenOpts(),
                          Res.getCodeGenOpts().ProfileInstrumentUsePath, *FS,
                          Diags);
  }

  FixupInvocation(Res, Diags, Args, DashX);

  return Diags.getNumErrors() == NumErrorsBefore;
}

bool CompilerInvocation::CreateFromArgs(CompilerInvocation &Invocation,
                                        ArrayRef<const char *> CommandLineArgs,
                                        DiagnosticsEngine &Diags,
                                        const char *Argv0) {
  CompilerInvocation DummyInvocation;

  return RoundTrip(
      [](CompilerInvocation &Invocation, ArrayRef<const char *> CommandLineArgs,
         DiagnosticsEngine &Diags, const char *Argv0) {
        return CreateFromArgsImpl(Invocation, CommandLineArgs, Diags, Argv0);
      },
      [](CompilerInvocation &Invocation, SmallVectorImpl<const char *> &Args,
         StringAllocator SA) {
        Args.push_back("-cc1");
        Invocation.generateCC1CommandLine(Args, SA);
      },
      Invocation, DummyInvocation, CommandLineArgs, Diags, Argv0);
}

std::string CompilerInvocation::getModuleHash() const {
  // FIXME: Consider using SHA1 instead of MD5.
  llvm::HashBuilder<llvm::MD5, llvm::endianness::native> HBuilder;

  // Note: For QoI reasons, the things we use as a hash here should all be
  // dumped via the -module-info flag.

  // Start the signature with the compiler version.
  HBuilder.add(getClangFullRepositoryVersion());

  // Also include the serialization version, in case LLVM_APPEND_VC_REV is off
  // and getClangFullRepositoryVersion() doesn't include git revision.
  HBuilder.add(serialization::VERSION_MAJOR, serialization::VERSION_MINOR);

  // Extend the signature with the language options
#define LANGOPT(Name, Bits, Default, Description) HBuilder.add(LangOpts->Name);
#define ENUM_LANGOPT(Name, Type, Bits, Default, Description)                   \
  HBuilder.add(static_cast<unsigned>(LangOpts->get##Name()));
#define BENIGN_LANGOPT(Name, Bits, Default, Description)
#define BENIGN_ENUM_LANGOPT(Name, Type, Bits, Default, Description)
#include "clang/Basic/LangOptions.def"

  HBuilder.addRange(getLangOpts().ModuleFeatures);

  HBuilder.add(getLangOpts().ObjCRuntime);
  HBuilder.addRange(getLangOpts().CommentOpts.BlockCommandNames);

  // Extend the signature with the target options.
  HBuilder.add(getTargetOpts().Triple, getTargetOpts().CPU,
               getTargetOpts().TuneCPU, getTargetOpts().ABI);
  HBuilder.addRange(getTargetOpts().FeaturesAsWritten);

  // Extend the signature with preprocessor options.
  const PreprocessorOptions &ppOpts = getPreprocessorOpts();
  HBuilder.add(ppOpts.UsePredefines, ppOpts.DetailedRecord);

  const HeaderSearchOptions &hsOpts = getHeaderSearchOpts();
  for (const auto &Macro : getPreprocessorOpts().Macros) {
    // If we're supposed to ignore this macro for the purposes of modules,
    // don't put it into the hash.
    if (!hsOpts.ModulesIgnoreMacros.empty()) {
      // Check whether we're ignoring this macro.
      StringRef MacroDef = Macro.first;
      if (hsOpts.ModulesIgnoreMacros.count(
              llvm::CachedHashString(MacroDef.split('=').first)))
        continue;
    }

    HBuilder.add(Macro);
  }

  // Extend the signature with the sysroot and other header search options.
  HBuilder.add(hsOpts.Sysroot, hsOpts.ModuleFormat, hsOpts.UseDebugInfo,
               hsOpts.UseBuiltinIncludes, hsOpts.UseStandardSystemIncludes,
               hsOpts.UseStandardCXXIncludes, hsOpts.UseLibcxx,
               hsOpts.ModulesValidateDiagnosticOptions);
  HBuilder.add(hsOpts.ResourceDir);

  if (hsOpts.ModulesStrictContextHash) {
    HBuilder.addRange(hsOpts.SystemHeaderPrefixes);
    HBuilder.addRange(hsOpts.UserEntries);
    HBuilder.addRange(hsOpts.VFSOverlayFiles);

    const DiagnosticOptions &diagOpts = getDiagnosticOpts();
#define DIAGOPT(Name, Bits, Default) HBuilder.add(diagOpts.Name);
#define ENUM_DIAGOPT(Name, Type, Bits, Default)                                \
  HBuilder.add(diagOpts.get##Name());
#include "clang/Basic/DiagnosticOptions.def"
#undef DIAGOPT
#undef ENUM_DIAGOPT
  }

  // Extend the signature with the user build path.
  HBuilder.add(hsOpts.ModuleUserBuildPath);

  // Extend the signature with the module file extensions.
  for (const auto &ext : getFrontendOpts().ModuleFileExtensions)
    ext->hashExtension(HBuilder);

  // Extend the signature with the Swift version for API notes.
  const APINotesOptions &APINotesOpts = getAPINotesOpts();
  if (!APINotesOpts.SwiftVersion.empty()) {
    HBuilder.add(APINotesOpts.SwiftVersion.getMajor());
    if (auto Minor = APINotesOpts.SwiftVersion.getMinor())
      HBuilder.add(*Minor);
    if (auto Subminor = APINotesOpts.SwiftVersion.getSubminor())
      HBuilder.add(*Subminor);
    if (auto Build = APINotesOpts.SwiftVersion.getBuild())
      HBuilder.add(*Build);
  }

  // When compiling with -gmodules, also hash -fdebug-prefix-map as it
  // affects the debug info in the PCM.
  if (getCodeGenOpts().DebugTypeExtRefs)
    HBuilder.addRange(getCodeGenOpts().DebugPrefixMap);

  // Extend the signature with the affecting debug options.
  if (getHeaderSearchOpts().ModuleFormat == "obj") {
#define DEBUGOPT(Name, Bits, Default) HBuilder.add(CodeGenOpts->Name);
#define VALUE_DEBUGOPT(Name, Bits, Default) HBuilder.add(CodeGenOpts->Name);
#define ENUM_DEBUGOPT(Name, Type, Bits, Default)                               \
  HBuilder.add(static_cast<unsigned>(CodeGenOpts->get##Name()));
#define BENIGN_DEBUGOPT(Name, Bits, Default)
#define BENIGN_VALUE_DEBUGOPT(Name, Bits, Default)
#define BENIGN_ENUM_DEBUGOPT(Name, Type, Bits, Default)
#include "clang/Basic/DebugOptions.def"
  }

  // Extend the signature with the enabled sanitizers, if at least one is
  // enabled. Sanitizers which cannot affect AST generation aren't hashed.
  SanitizerSet SanHash = getLangOpts().Sanitize;
  SanHash.clear(getPPTransparentSanitizers());
  if (!SanHash.empty())
    HBuilder.add(SanHash.Mask);

  llvm::MD5::MD5Result Result;
  HBuilder.getHasher().final(Result);
  uint64_t Hash = Result.high() ^ Result.low();
  return toString(llvm::APInt(64, Hash), 36, /*Signed=*/false);
}

void CompilerInvocationBase::generateCC1CommandLine(
    ArgumentConsumer Consumer) const {
  llvm::Triple T(getTargetOpts().Triple);

  GenerateFileSystemArgs(getFileSystemOpts(), Consumer);
  GenerateMigratorArgs(getMigratorOpts(), Consumer);
  GenerateAnalyzerArgs(getAnalyzerOpts(), Consumer);
  GenerateDiagnosticArgs(getDiagnosticOpts(), Consumer,
                         /*DefaultDiagColor=*/false);
  GenerateFrontendArgs(getFrontendOpts(), Consumer, getLangOpts().IsHeaderFile);
  GenerateTargetArgs(getTargetOpts(), Consumer);
  GenerateHeaderSearchArgs(getHeaderSearchOpts(), Consumer);
  GenerateAPINotesArgs(getAPINotesOpts(), Consumer);
  GeneratePointerAuthArgs(getLangOpts(), Consumer);
  GenerateLangArgs(getLangOpts(), Consumer, T, getFrontendOpts().DashX);
  GenerateCodeGenArgs(getCodeGenOpts(), Consumer, T,
                      getFrontendOpts().OutputFile, &getLangOpts());
  GeneratePreprocessorArgs(getPreprocessorOpts(), Consumer, getLangOpts(),
                           getFrontendOpts(), getCodeGenOpts());
  GeneratePreprocessorOutputArgs(getPreprocessorOutputOpts(), Consumer,
                                 getFrontendOpts().ProgramAction);
  GenerateDependencyOutputArgs(getDependencyOutputOpts(), Consumer);
}

std::vector<std::string> CompilerInvocationBase::getCC1CommandLine() const {
  std::vector<std::string> Args{"-cc1"};
  generateCC1CommandLine(
      [&Args](const Twine &Arg) { Args.push_back(Arg.str()); });
  return Args;
}

void CompilerInvocation::resetNonModularOptions() {
  getLangOpts().resetNonModularOptions();
  getPreprocessorOpts().resetNonModularOptions();
  getCodeGenOpts().resetNonModularOptions(getHeaderSearchOpts().ModuleFormat);
}

void CompilerInvocation::clearImplicitModuleBuildOptions() {
  getLangOpts().ImplicitModules = false;
  getHeaderSearchOpts().ImplicitModuleMaps = false;
  getHeaderSearchOpts().ModuleCachePath.clear();
  getHeaderSearchOpts().ModulesValidateOncePerBuildSession = false;
  getHeaderSearchOpts().BuildSessionTimestamp = 0;
  // The specific values we canonicalize to for pruning don't affect behaviour,
  /// so use the default values so they may be dropped from the command-line.
  getHeaderSearchOpts().ModuleCachePruneInterval = 7 * 24 * 60 * 60;
  getHeaderSearchOpts().ModuleCachePruneAfter = 31 * 24 * 60 * 60;
}

IntrusiveRefCntPtr<llvm::vfs::FileSystem>
clang::createVFSFromCompilerInvocation(const CompilerInvocation &CI,
                                       DiagnosticsEngine &Diags) {
  return createVFSFromCompilerInvocation(CI, Diags,
                                         llvm::vfs::getRealFileSystem());
}

IntrusiveRefCntPtr<llvm::vfs::FileSystem>
clang::createVFSFromCompilerInvocation(
    const CompilerInvocation &CI, DiagnosticsEngine &Diags,
    IntrusiveRefCntPtr<llvm::vfs::FileSystem> BaseFS) {
  return createVFSFromOverlayFiles(CI.getHeaderSearchOpts().VFSOverlayFiles,
                                   Diags, std::move(BaseFS));
}

IntrusiveRefCntPtr<llvm::vfs::FileSystem> clang::createVFSFromOverlayFiles(
    ArrayRef<std::string> VFSOverlayFiles, DiagnosticsEngine &Diags,
    IntrusiveRefCntPtr<llvm::vfs::FileSystem> BaseFS) {
  if (VFSOverlayFiles.empty())
    return BaseFS;

  IntrusiveRefCntPtr<llvm::vfs::FileSystem> Result = BaseFS;
  // earlier vfs files are on the bottom
  for (const auto &File : VFSOverlayFiles) {
    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> Buffer =
        Result->getBufferForFile(File);
    if (!Buffer) {
      Diags.Report(diag::err_missing_vfs_overlay_file) << File;
      continue;
    }

    IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS = llvm::vfs::getVFSFromYAML(
        std::move(Buffer.get()), /*DiagHandler*/ nullptr, File,
        /*DiagContext*/ nullptr, Result);
    if (!FS) {
      Diags.Report(diag::err_invalid_vfs_overlay) << File;
      continue;
    }

    Result = FS;
  }
  return Result;
}
