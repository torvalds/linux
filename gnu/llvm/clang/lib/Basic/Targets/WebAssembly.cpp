//===--- WebAssembly.cpp - Implement WebAssembly target feature support ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements WebAssembly TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "WebAssembly.h"
#include "Targets.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/TargetBuiltins.h"
#include "llvm/ADT/StringSwitch.h"

using namespace clang;
using namespace clang::targets;

static constexpr Builtin::Info BuiltinInfo[] = {
#define BUILTIN(ID, TYPE, ATTRS)                                               \
  {#ID, TYPE, ATTRS, nullptr, HeaderDesc::NO_HEADER, ALL_LANGUAGES},
#define TARGET_BUILTIN(ID, TYPE, ATTRS, FEATURE)                               \
  {#ID, TYPE, ATTRS, FEATURE, HeaderDesc::NO_HEADER, ALL_LANGUAGES},
#define LIBBUILTIN(ID, TYPE, ATTRS, HEADER)                                    \
  {#ID, TYPE, ATTRS, nullptr, HeaderDesc::HEADER, ALL_LANGUAGES},
#include "clang/Basic/BuiltinsWebAssembly.def"
};

static constexpr llvm::StringLiteral ValidCPUNames[] = {
    {"mvp"}, {"bleeding-edge"}, {"generic"}};

StringRef WebAssemblyTargetInfo::getABI() const { return ABI; }

bool WebAssemblyTargetInfo::setABI(const std::string &Name) {
  if (Name != "mvp" && Name != "experimental-mv")
    return false;

  ABI = Name;
  return true;
}

bool WebAssemblyTargetInfo::hasFeature(StringRef Feature) const {
  return llvm::StringSwitch<bool>(Feature)
      .Case("atomics", HasAtomics)
      .Case("bulk-memory", HasBulkMemory)
      .Case("exception-handling", HasExceptionHandling)
      .Case("extended-const", HasExtendedConst)
      .Case("half-precision", HasHalfPrecision)
      .Case("multimemory", HasMultiMemory)
      .Case("multivalue", HasMultivalue)
      .Case("mutable-globals", HasMutableGlobals)
      .Case("nontrapping-fptoint", HasNontrappingFPToInt)
      .Case("reference-types", HasReferenceTypes)
      .Case("relaxed-simd", SIMDLevel >= RelaxedSIMD)
      .Case("sign-ext", HasSignExt)
      .Case("simd128", SIMDLevel >= SIMD128)
      .Case("tail-call", HasTailCall)
      .Default(false);
}

bool WebAssemblyTargetInfo::isValidCPUName(StringRef Name) const {
  return llvm::is_contained(ValidCPUNames, Name);
}

void WebAssemblyTargetInfo::fillValidCPUList(
    SmallVectorImpl<StringRef> &Values) const {
  Values.append(std::begin(ValidCPUNames), std::end(ValidCPUNames));
}

void WebAssemblyTargetInfo::getTargetDefines(const LangOptions &Opts,
                                             MacroBuilder &Builder) const {
  defineCPUMacros(Builder, "wasm", /*Tuning=*/false);
  if (HasAtomics)
    Builder.defineMacro("__wasm_atomics__");
  if (HasBulkMemory)
    Builder.defineMacro("__wasm_bulk_memory__");
  if (HasExceptionHandling)
    Builder.defineMacro("__wasm_exception_handling__");
  if (HasExtendedConst)
    Builder.defineMacro("__wasm_extended_const__");
  if (HasMultiMemory)
    Builder.defineMacro("__wasm_multimemory__");
  if (HasHalfPrecision)
    Builder.defineMacro("__wasm_half_precision__");
  if (HasMultivalue)
    Builder.defineMacro("__wasm_multivalue__");
  if (HasMutableGlobals)
    Builder.defineMacro("__wasm_mutable_globals__");
  if (HasNontrappingFPToInt)
    Builder.defineMacro("__wasm_nontrapping_fptoint__");
  if (HasReferenceTypes)
    Builder.defineMacro("__wasm_reference_types__");
  if (SIMDLevel >= RelaxedSIMD)
    Builder.defineMacro("__wasm_relaxed_simd__");
  if (HasSignExt)
    Builder.defineMacro("__wasm_sign_ext__");
  if (SIMDLevel >= SIMD128)
    Builder.defineMacro("__wasm_simd128__");
  if (HasTailCall)
    Builder.defineMacro("__wasm_tail_call__");

  Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_1");
  Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_2");
  Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_4");
  Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_8");
}

void WebAssemblyTargetInfo::setSIMDLevel(llvm::StringMap<bool> &Features,
                                         SIMDEnum Level, bool Enabled) {
  if (Enabled) {
    switch (Level) {
    case RelaxedSIMD:
      Features["relaxed-simd"] = true;
      [[fallthrough]];
    case SIMD128:
      Features["simd128"] = true;
      [[fallthrough]];
    case NoSIMD:
      break;
    }
    return;
  }

  switch (Level) {
  case NoSIMD:
  case SIMD128:
    Features["simd128"] = false;
    [[fallthrough]];
  case RelaxedSIMD:
    Features["relaxed-simd"] = false;
    break;
  }
}

void WebAssemblyTargetInfo::setFeatureEnabled(llvm::StringMap<bool> &Features,
                                              StringRef Name,
                                              bool Enabled) const {
  if (Name == "simd128")
    setSIMDLevel(Features, SIMD128, Enabled);
  else if (Name == "relaxed-simd")
    setSIMDLevel(Features, RelaxedSIMD, Enabled);
  else
    Features[Name] = Enabled;
}

bool WebAssemblyTargetInfo::initFeatureMap(
    llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags, StringRef CPU,
    const std::vector<std::string> &FeaturesVec) const {
  auto addGenericFeatures = [&]() {
    Features["multivalue"] = true;
    Features["mutable-globals"] = true;
    Features["reference-types"] = true;
    Features["sign-ext"] = true;
  };
  auto addBleedingEdgeFeatures = [&]() {
    addGenericFeatures();
    Features["atomics"] = true;
    Features["bulk-memory"] = true;
    Features["exception-handling"] = true;
    Features["extended-const"] = true;
    Features["half-precision"] = true;
    Features["multimemory"] = true;
    Features["nontrapping-fptoint"] = true;
    Features["tail-call"] = true;
    setSIMDLevel(Features, RelaxedSIMD, true);
  };
  if (CPU == "generic") {
    addGenericFeatures();
  } else if (CPU == "bleeding-edge") {
    addBleedingEdgeFeatures();
  }

  return TargetInfo::initFeatureMap(Features, Diags, CPU, FeaturesVec);
}

bool WebAssemblyTargetInfo::handleTargetFeatures(
    std::vector<std::string> &Features, DiagnosticsEngine &Diags) {
  for (const auto &Feature : Features) {
    if (Feature == "+atomics") {
      HasAtomics = true;
      continue;
    }
    if (Feature == "-atomics") {
      HasAtomics = false;
      continue;
    }
    if (Feature == "+bulk-memory") {
      HasBulkMemory = true;
      continue;
    }
    if (Feature == "-bulk-memory") {
      HasBulkMemory = false;
      continue;
    }
    if (Feature == "+exception-handling") {
      HasExceptionHandling = true;
      continue;
    }
    if (Feature == "-exception-handling") {
      HasExceptionHandling = false;
      continue;
    }
    if (Feature == "+extended-const") {
      HasExtendedConst = true;
      continue;
    }
    if (Feature == "-extended-const") {
      HasExtendedConst = false;
      continue;
    }
    if (Feature == "+half-precision") {
      SIMDLevel = std::max(SIMDLevel, SIMD128);
      HasHalfPrecision = true;
      continue;
    }
    if (Feature == "-half-precision") {
      HasHalfPrecision = false;
      continue;
    }
    if (Feature == "+multimemory") {
      HasMultiMemory = true;
      continue;
    }
    if (Feature == "-multimemory") {
      HasMultiMemory = false;
      continue;
    }
    if (Feature == "+multivalue") {
      HasMultivalue = true;
      continue;
    }
    if (Feature == "-multivalue") {
      HasMultivalue = false;
      continue;
    }
    if (Feature == "+mutable-globals") {
      HasMutableGlobals = true;
      continue;
    }
    if (Feature == "-mutable-globals") {
      HasMutableGlobals = false;
      continue;
    }
    if (Feature == "+nontrapping-fptoint") {
      HasNontrappingFPToInt = true;
      continue;
    }
    if (Feature == "-nontrapping-fptoint") {
      HasNontrappingFPToInt = false;
      continue;
    }
    if (Feature == "+reference-types") {
      HasReferenceTypes = true;
      continue;
    }
    if (Feature == "-reference-types") {
      HasReferenceTypes = false;
      continue;
    }
    if (Feature == "+relaxed-simd") {
      SIMDLevel = std::max(SIMDLevel, RelaxedSIMD);
      continue;
    }
    if (Feature == "-relaxed-simd") {
      SIMDLevel = std::min(SIMDLevel, SIMDEnum(RelaxedSIMD - 1));
      continue;
    }
    if (Feature == "+sign-ext") {
      HasSignExt = true;
      continue;
    }
    if (Feature == "-sign-ext") {
      HasSignExt = false;
      continue;
    }
    if (Feature == "+simd128") {
      SIMDLevel = std::max(SIMDLevel, SIMD128);
      continue;
    }
    if (Feature == "-simd128") {
      SIMDLevel = std::min(SIMDLevel, SIMDEnum(SIMD128 - 1));
      continue;
    }
    if (Feature == "+tail-call") {
      HasTailCall = true;
      continue;
    }
    if (Feature == "-tail-call") {
      HasTailCall = false;
      continue;
    }

    Diags.Report(diag::err_opt_not_valid_with_opt)
        << Feature << "-target-feature";
    return false;
  }
  return true;
}

ArrayRef<Builtin::Info> WebAssemblyTargetInfo::getTargetBuiltins() const {
  return llvm::ArrayRef(BuiltinInfo, clang::WebAssembly::LastTSBuiltin -
                                         Builtin::FirstTSBuiltin);
}

void WebAssemblyTargetInfo::adjust(DiagnosticsEngine &Diags,
                                   LangOptions &Opts) {
  TargetInfo::adjust(Diags, Opts);
  // Turn off POSIXThreads and ThreadModel so that we don't predefine _REENTRANT
  // or __STDCPP_THREADS__ if we will eventually end up stripping atomics
  // because they are unsupported.
  if (!HasAtomics || !HasBulkMemory) {
    Opts.POSIXThreads = false;
    Opts.setThreadModel(LangOptions::ThreadModelKind::Single);
    Opts.ThreadsafeStatics = false;
  }
}

void WebAssembly32TargetInfo::getTargetDefines(const LangOptions &Opts,
                                               MacroBuilder &Builder) const {
  WebAssemblyTargetInfo::getTargetDefines(Opts, Builder);
  defineCPUMacros(Builder, "wasm32", /*Tuning=*/false);
}

void WebAssembly64TargetInfo::getTargetDefines(const LangOptions &Opts,
                                               MacroBuilder &Builder) const {
  WebAssemblyTargetInfo::getTargetDefines(Opts, Builder);
  defineCPUMacros(Builder, "wasm64", /*Tuning=*/false);
}
