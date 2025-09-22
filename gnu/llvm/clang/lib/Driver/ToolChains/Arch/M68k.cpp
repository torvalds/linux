//===--- M68k.cpp - M68k Helpers for Tools -------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M68k.h"
#include "ToolChains/CommonArgs.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/Regex.h"
#include "llvm/TargetParser/Host.h"
#include <sstream>

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

/// getM68kTargetCPU - Get the (LLVM) name of the 68000 cpu we are targeting.
std::string m68k::getM68kTargetCPU(const ArgList &Args) {
  if (Arg *A = Args.getLastArg(clang::driver::options::OPT_mcpu_EQ)) {
    // The canonical CPU name is captalize. However, we allow
    // starting with lower case or numbers only
    StringRef CPUName = A->getValue();

    if (CPUName == "native") {
      std::string CPU = std::string(llvm::sys::getHostCPUName());
      if (!CPU.empty() && CPU != "generic")
        return CPU;
    }

    if (CPUName == "common")
      return "generic";

    return llvm::StringSwitch<std::string>(CPUName)
        .Cases("m68000", "68000", "M68000")
        .Cases("m68010", "68010", "M68010")
        .Cases("m68020", "68020", "M68020")
        .Cases("m68030", "68030", "M68030")
        .Cases("m68040", "68040", "M68040")
        .Cases("m68060", "68060", "M68060")
        .Default(CPUName.str());
  }
  // FIXME: Throw error when multiple sub-architecture flag exist
  if (Args.hasArg(clang::driver::options::OPT_m68000))
    return "M68000";
  if (Args.hasArg(clang::driver::options::OPT_m68010))
    return "M68010";
  if (Args.hasArg(clang::driver::options::OPT_m68020))
    return "M68020";
  if (Args.hasArg(clang::driver::options::OPT_m68030))
    return "M68030";
  if (Args.hasArg(clang::driver::options::OPT_m68040))
    return "M68040";
  if (Args.hasArg(clang::driver::options::OPT_m68060))
    return "M68060";

  return "";
}

static void addFloatABIFeatures(const llvm::opt::ArgList &Args,
                                std::vector<llvm::StringRef> &Features) {
  Arg *A = Args.getLastArg(options::OPT_msoft_float, options::OPT_mhard_float,
                           options::OPT_m68881);
  // Opt out FPU even for newer CPUs.
  if (A && A->getOption().matches(options::OPT_msoft_float)) {
    Features.push_back("-isa-68881");
    Features.push_back("-isa-68882");
    return;
  }

  std::string CPU = m68k::getM68kTargetCPU(Args);
  // Only enable M68881 for CPU < 68020 if the related flags are present.
  if ((A && (CPU == "M68000" || CPU == "M68010")) ||
      // Otherwise, by default we assume newer CPUs have M68881/2.
      CPU == "M68020")
    Features.push_back("+isa-68881");
  else if (CPU == "M68030" || CPU == "M68040" || CPU == "M68060")
    // Note that although CPU >= M68040 imply M68882, we still add `isa-68882`
    // anyway so that it's easier to add or not add the corresponding macro
    // definitions later, in case we want to disable 68881/2 in newer CPUs
    // (with -msoft-float, for instance).
    Features.push_back("+isa-68882");
}

void m68k::getM68kTargetFeatures(const Driver &D, const llvm::Triple &Triple,
                                 const ArgList &Args,
                                 std::vector<StringRef> &Features) {
  addFloatABIFeatures(Args, Features);

  // Handle '-ffixed-<register>' flags
  if (Args.hasArg(options::OPT_ffixed_a0))
    Features.push_back("+reserve-a0");
  if (Args.hasArg(options::OPT_ffixed_a1))
    Features.push_back("+reserve-a1");
  if (Args.hasArg(options::OPT_ffixed_a2))
    Features.push_back("+reserve-a2");
  if (Args.hasArg(options::OPT_ffixed_a3))
    Features.push_back("+reserve-a3");
  if (Args.hasArg(options::OPT_ffixed_a4))
    Features.push_back("+reserve-a4");
  if (Args.hasArg(options::OPT_ffixed_a5))
    Features.push_back("+reserve-a5");
  if (Args.hasArg(options::OPT_ffixed_a6))
    Features.push_back("+reserve-a6");
  if (Args.hasArg(options::OPT_ffixed_d0))
    Features.push_back("+reserve-d0");
  if (Args.hasArg(options::OPT_ffixed_d1))
    Features.push_back("+reserve-d1");
  if (Args.hasArg(options::OPT_ffixed_d2))
    Features.push_back("+reserve-d2");
  if (Args.hasArg(options::OPT_ffixed_d3))
    Features.push_back("+reserve-d3");
  if (Args.hasArg(options::OPT_ffixed_d4))
    Features.push_back("+reserve-d4");
  if (Args.hasArg(options::OPT_ffixed_d5))
    Features.push_back("+reserve-d5");
  if (Args.hasArg(options::OPT_ffixed_d6))
    Features.push_back("+reserve-d6");
  if (Args.hasArg(options::OPT_ffixed_d7))
    Features.push_back("+reserve-d7");
}
