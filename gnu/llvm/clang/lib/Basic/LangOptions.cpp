//===- LangOptions.cpp - C Language Family Language Options ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the LangOptions class.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/LangOptions.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Path.h"

using namespace clang;

LangOptions::LangOptions() : LangStd(LangStandard::lang_unspecified) {
#define LANGOPT(Name, Bits, Default, Description) Name = Default;
#define ENUM_LANGOPT(Name, Type, Bits, Default, Description) set##Name(Default);
#include "clang/Basic/LangOptions.def"
}

void LangOptions::resetNonModularOptions() {
#define LANGOPT(Name, Bits, Default, Description)
#define BENIGN_LANGOPT(Name, Bits, Default, Description) Name = Default;
#define BENIGN_ENUM_LANGOPT(Name, Type, Bits, Default, Description) \
  Name = static_cast<unsigned>(Default);
#include "clang/Basic/LangOptions.def"

  // Reset "benign" options with implied values (Options.td ImpliedBy relations)
  // rather than their defaults. This avoids unexpected combinations and
  // invocations that cannot be round-tripped to arguments.
  // FIXME: we should derive this automatically from ImpliedBy in tablegen.
  AllowFPReassoc = UnsafeFPMath;
  NoHonorNaNs = FiniteMathOnly;
  NoHonorInfs = FiniteMathOnly;

  // These options do not affect AST generation.
  NoSanitizeFiles.clear();
  XRayAlwaysInstrumentFiles.clear();
  XRayNeverInstrumentFiles.clear();

  CurrentModule.clear();
  IsHeaderFile = false;
}

bool LangOptions::isNoBuiltinFunc(StringRef FuncName) const {
  for (unsigned i = 0, e = NoBuiltinFuncs.size(); i != e; ++i)
    if (FuncName == NoBuiltinFuncs[i])
      return true;
  return false;
}

VersionTuple LangOptions::getOpenCLVersionTuple() const {
  const int Ver = OpenCLCPlusPlus ? OpenCLCPlusPlusVersion : OpenCLVersion;
  if (OpenCLCPlusPlus && Ver != 100)
    return VersionTuple(Ver / 100);
  return VersionTuple(Ver / 100, (Ver % 100) / 10);
}

unsigned LangOptions::getOpenCLCompatibleVersion() const {
  if (!OpenCLCPlusPlus)
    return OpenCLVersion;
  if (OpenCLCPlusPlusVersion == 100)
    return 200;
  if (OpenCLCPlusPlusVersion == 202100)
    return 300;
  llvm_unreachable("Unknown OpenCL version");
}

void LangOptions::remapPathPrefix(SmallVectorImpl<char> &Path) const {
  for (const auto &Entry : MacroPrefixMap)
    if (llvm::sys::path::replace_path_prefix(Path, Entry.first, Entry.second))
      break;
}

std::string LangOptions::getOpenCLVersionString() const {
  std::string Result;
  {
    llvm::raw_string_ostream Out(Result);
    Out << (OpenCLCPlusPlus ? "C++ for OpenCL" : "OpenCL C") << " version "
        << getOpenCLVersionTuple().getAsString();
  }
  return Result;
}

void LangOptions::setLangDefaults(LangOptions &Opts, Language Lang,
                                  const llvm::Triple &T,
                                  std::vector<std::string> &Includes,
                                  LangStandard::Kind LangStd) {
  // Set some properties which depend solely on the input kind; it would be nice
  // to move these to the language standard, and have the driver resolve the
  // input kind + language standard.
  //
  // FIXME: Perhaps a better model would be for a single source file to have
  // multiple language standards (C / C++ std, ObjC std, OpenCL std, OpenMP std)
  // simultaneously active?
  if (Lang == Language::Asm) {
    Opts.AsmPreprocessor = 1;
  } else if (Lang == Language::ObjC || Lang == Language::ObjCXX) {
    Opts.ObjC = 1;
  }

  if (LangStd == LangStandard::lang_unspecified)
    LangStd = getDefaultLanguageStandard(Lang, T);
  const LangStandard &Std = LangStandard::getLangStandardForKind(LangStd);
  Opts.LangStd = LangStd;
  Opts.LineComment = Std.hasLineComments();
  Opts.C99 = Std.isC99();
  Opts.C11 = Std.isC11();
  Opts.C17 = Std.isC17();
  Opts.C23 = Std.isC23();
  Opts.C2y = Std.isC2y();
  Opts.CPlusPlus = Std.isCPlusPlus();
  Opts.CPlusPlus11 = Std.isCPlusPlus11();
  Opts.CPlusPlus14 = Std.isCPlusPlus14();
  Opts.CPlusPlus17 = Std.isCPlusPlus17();
  Opts.CPlusPlus20 = Std.isCPlusPlus20();
  Opts.CPlusPlus23 = Std.isCPlusPlus23();
  Opts.CPlusPlus26 = Std.isCPlusPlus26();
  Opts.GNUMode = Std.isGNUMode();
  Opts.GNUCVersion = 0;
  Opts.HexFloats = Std.hasHexFloats();
  Opts.WChar = Std.isCPlusPlus();
  Opts.Digraphs = Std.hasDigraphs();
  Opts.RawStringLiterals = Std.hasRawStringLiterals();

  Opts.HLSL = Lang == Language::HLSL;
  if (Opts.HLSL && Opts.IncludeDefaultHeader)
    Includes.push_back("hlsl.h");

  // Set OpenCL Version.
  Opts.OpenCL = Std.isOpenCL();
  if (LangStd == LangStandard::lang_opencl10)
    Opts.OpenCLVersion = 100;
  else if (LangStd == LangStandard::lang_opencl11)
    Opts.OpenCLVersion = 110;
  else if (LangStd == LangStandard::lang_opencl12)
    Opts.OpenCLVersion = 120;
  else if (LangStd == LangStandard::lang_opencl20)
    Opts.OpenCLVersion = 200;
  else if (LangStd == LangStandard::lang_opencl30)
    Opts.OpenCLVersion = 300;
  else if (LangStd == LangStandard::lang_openclcpp10)
    Opts.OpenCLCPlusPlusVersion = 100;
  else if (LangStd == LangStandard::lang_openclcpp2021)
    Opts.OpenCLCPlusPlusVersion = 202100;
  else if (LangStd == LangStandard::lang_hlsl2015)
    Opts.HLSLVersion = (unsigned)LangOptions::HLSL_2015;
  else if (LangStd == LangStandard::lang_hlsl2016)
    Opts.HLSLVersion = (unsigned)LangOptions::HLSL_2016;
  else if (LangStd == LangStandard::lang_hlsl2017)
    Opts.HLSLVersion = (unsigned)LangOptions::HLSL_2017;
  else if (LangStd == LangStandard::lang_hlsl2018)
    Opts.HLSLVersion = (unsigned)LangOptions::HLSL_2018;
  else if (LangStd == LangStandard::lang_hlsl2021)
    Opts.HLSLVersion = (unsigned)LangOptions::HLSL_2021;
  else if (LangStd == LangStandard::lang_hlsl202x)
    Opts.HLSLVersion = (unsigned)LangOptions::HLSL_202x;

  // OpenCL has some additional defaults.
  if (Opts.OpenCL) {
    Opts.AltiVec = 0;
    Opts.ZVector = 0;
    Opts.setDefaultFPContractMode(LangOptions::FPM_On);
    Opts.OpenCLCPlusPlus = Opts.CPlusPlus;
    Opts.OpenCLPipes = Opts.getOpenCLCompatibleVersion() == 200;
    Opts.OpenCLGenericAddressSpace = Opts.getOpenCLCompatibleVersion() == 200;

    // Include default header file for OpenCL.
    if (Opts.IncludeDefaultHeader) {
      if (Opts.DeclareOpenCLBuiltins) {
        // Only include base header file for builtin types and constants.
        Includes.push_back("opencl-c-base.h");
      } else {
        Includes.push_back("opencl-c.h");
      }
    }
  }

  Opts.HIP = Lang == Language::HIP;
  Opts.CUDA = Lang == Language::CUDA || Opts.HIP;
  if (Opts.HIP) {
    // HIP toolchain does not support 'Fast' FPOpFusion in backends since it
    // fuses multiplication/addition instructions without contract flag from
    // device library functions in LLVM bitcode, which causes accuracy loss in
    // certain math functions, e.g. tan(-1e20) becomes -0.933 instead of 0.8446.
    // For device library functions in bitcode to work, 'Strict' or 'Standard'
    // FPOpFusion options in backends is needed. Therefore 'fast-honor-pragmas'
    // FP contract option is used to allow fuse across statements in frontend
    // whereas respecting contract flag in backend.
    Opts.setDefaultFPContractMode(LangOptions::FPM_FastHonorPragmas);
  } else if (Opts.CUDA) {
    if (T.isSPIRV()) {
      // Emit OpenCL version metadata in LLVM IR when targeting SPIR-V.
      Opts.OpenCLVersion = 200;
    }
    // Allow fuse across statements disregarding pragmas.
    Opts.setDefaultFPContractMode(LangOptions::FPM_Fast);
  }

  Opts.RenderScript = Lang == Language::RenderScript;

  // OpenCL, C++ and C23 have bool, true, false keywords.
  Opts.Bool = Opts.OpenCL || Opts.CPlusPlus || Opts.C23;

  // OpenCL and HLSL have half keyword
  Opts.Half = Opts.OpenCL || Opts.HLSL;
}

FPOptions FPOptions::defaultWithoutTrailingStorage(const LangOptions &LO) {
  FPOptions result(LO);
  return result;
}

FPOptionsOverride FPOptions::getChangesSlow(const FPOptions &Base) const {
  FPOptions::storage_type OverrideMask = 0;
#define OPTION(NAME, TYPE, WIDTH, PREVIOUS)                                    \
  if (get##NAME() != Base.get##NAME())                                         \
    OverrideMask |= NAME##Mask;
#include "clang/Basic/FPOptions.def"
  return FPOptionsOverride(*this, OverrideMask);
}

LLVM_DUMP_METHOD void FPOptions::dump() {
#define OPTION(NAME, TYPE, WIDTH, PREVIOUS)                                    \
  llvm::errs() << "\n " #NAME " " << get##NAME();
#include "clang/Basic/FPOptions.def"
  llvm::errs() << "\n";
}

LLVM_DUMP_METHOD void FPOptionsOverride::dump() {
#define OPTION(NAME, TYPE, WIDTH, PREVIOUS)                                    \
  if (has##NAME##Override())                                                   \
    llvm::errs() << "\n " #NAME " Override is " << get##NAME##Override();
#include "clang/Basic/FPOptions.def"
  llvm::errs() << "\n";
}
