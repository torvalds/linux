//===--- X86.cpp - X86 Helpers for Tools ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "ToolChains/CommonArgs.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Option/ArgList.h"
#include "llvm/TargetParser/Host.h"

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

std::string x86::getX86TargetCPU(const Driver &D, const ArgList &Args,
                                 const llvm::Triple &Triple) {
  if (const Arg *A = Args.getLastArg(clang::driver::options::OPT_march_EQ)) {
    StringRef CPU = A->getValue();
    if (CPU != "native")
      return std::string(CPU);

    // FIXME: Reject attempts to use -march=native unless the target matches
    // the host.
    CPU = llvm::sys::getHostCPUName();
    if (!CPU.empty() && CPU != "generic")
      return std::string(CPU);
  }

  if (const Arg *A = Args.getLastArg(options::OPT__SLASH_arch)) {
    // Mapping built by looking at lib/Basic's X86TargetInfo::initFeatureMap().
    // The keys are case-sensitive; this matches link.exe.
    // 32-bit and 64-bit /arch: flags.
    llvm::StringMap<StringRef> ArchMap({
        {"AVX", "sandybridge"},
        {"AVX2", "haswell"},
        {"AVX512F", "knl"},
        {"AVX512", "skylake-avx512"},
    });
    if (Triple.getArch() == llvm::Triple::x86) {
      // 32-bit-only /arch: flags.
      ArchMap.insert({
          {"IA32", "i386"},
          {"SSE", "pentium3"},
          {"SSE2", "pentium4"},
      });
    }
    StringRef CPU = ArchMap.lookup(A->getValue());
    if (CPU.empty()) {
      std::vector<StringRef> ValidArchs{ArchMap.keys().begin(),
                                        ArchMap.keys().end()};
      sort(ValidArchs);
      D.Diag(diag::warn_drv_invalid_arch_name_with_suggestion)
          << A->getValue() << (Triple.getArch() == llvm::Triple::x86)
          << join(ValidArchs, ", ");
    }
    return std::string(CPU);
  }

  // Select the default CPU if none was given (or detection failed).

  if (!Triple.isX86())
    return ""; // This routine is only handling x86 targets.

  bool Is64Bit = Triple.getArch() == llvm::Triple::x86_64;

  // FIXME: Need target hooks.
  if (Triple.isOSDarwin()) {
    if (Triple.getArchName() == "x86_64h")
      return "core-avx2";
    // macosx10.12 drops support for all pre-Penryn Macs.
    // Simulators can still run on 10.11 though, like Xcode.
    if (Triple.isMacOSX() && !Triple.isOSVersionLT(10, 12))
      return "penryn";

    if (Triple.isDriverKit())
      return "nehalem";

    // The oldest x86_64 Macs have core2/Merom; the oldest x86 Macs have Yonah.
    return Is64Bit ? "core2" : "yonah";
  }

  // Set up default CPU name for PS4/PS5 compilers.
  if (Triple.isPS4())
    return "btver2";
  if (Triple.isPS5())
    return "znver2";

  // On Android use targets compatible with gcc
  if (Triple.isAndroid())
    return Is64Bit ? "x86-64" : "i686";

  // Everything else goes to x86-64 in 64-bit mode.
  if (Is64Bit)
    return "x86-64";

  switch (Triple.getOS()) {
  case llvm::Triple::NetBSD:
    return "i486";
  case llvm::Triple::Haiku:
  case llvm::Triple::OpenBSD:
    return "i586";
  case llvm::Triple::FreeBSD:
    return "i686";
  default:
    // Fallback to p4.
    return "pentium4";
  }
}

void x86::getX86TargetFeatures(const Driver &D, const llvm::Triple &Triple,
                               const ArgList &Args,
                               std::vector<StringRef> &Features) {
  // Claim and report unsupported -mabi=. Note: we don't support "sysv_abi" or
  // "ms_abi" as default function attributes.
  if (const Arg *A = Args.getLastArg(clang::driver::options::OPT_mabi_EQ)) {
    StringRef DefaultAbi = Triple.isOSWindows() ? "ms" : "sysv";
    if (A->getValue() != DefaultAbi)
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getSpelling() << Triple.getTriple();
  }

  // If -march=native, autodetect the feature list.
  if (const Arg *A = Args.getLastArg(clang::driver::options::OPT_march_EQ)) {
    if (StringRef(A->getValue()) == "native") {
      for (auto &F : llvm::sys::getHostCPUFeatures())
        Features.push_back(
            Args.MakeArgString((F.second ? "+" : "-") + F.first()));
    }
  }

  if (Triple.getArchName() == "x86_64h") {
    // x86_64h implies quite a few of the more modern subtarget features
    // for Haswell class CPUs, but not all of them. Opt-out of a few.
    Features.push_back("-rdrnd");
    Features.push_back("-aes");
    Features.push_back("-pclmul");
    Features.push_back("-rtm");
    Features.push_back("-fsgsbase");
  }

  const llvm::Triple::ArchType ArchType = Triple.getArch();
  // Add features to be compatible with gcc for Android.
  if (Triple.isAndroid()) {
    if (ArchType == llvm::Triple::x86_64) {
      Features.push_back("+sse4.2");
      Features.push_back("+popcnt");
      Features.push_back("+cx16");
    } else
      Features.push_back("+ssse3");
  }

  // Translate the high level `-mretpoline` flag to the specific target feature
  // flags. We also detect if the user asked for retpoline external thunks but
  // failed to ask for retpolines themselves (through any of the different
  // flags). This is a bit hacky but keeps existing usages working. We should
  // consider deprecating this and instead warn if the user requests external
  // retpoline thunks and *doesn't* request some form of retpolines.
  auto SpectreOpt = clang::driver::options::ID::OPT_INVALID;
  if (Args.hasArgNoClaim(options::OPT_mretpoline, options::OPT_mno_retpoline,
                         options::OPT_mspeculative_load_hardening,
                         options::OPT_mno_speculative_load_hardening)) {
    if (Args.hasFlag(options::OPT_mretpoline, options::OPT_mno_retpoline,
                     false)) {
      Features.push_back("+retpoline-indirect-calls");
      Features.push_back("+retpoline-indirect-branches");
      SpectreOpt = options::OPT_mretpoline;
    } else if (Args.hasFlag(options::OPT_mspeculative_load_hardening,
                            options::OPT_mno_speculative_load_hardening,
                            false)) {
      // On x86, speculative load hardening relies on at least using retpolines
      // for indirect calls.
      Features.push_back("+retpoline-indirect-calls");
      SpectreOpt = options::OPT_mspeculative_load_hardening;
    }
  } else if (Args.hasFlag(options::OPT_mretpoline_external_thunk,
                          options::OPT_mno_retpoline_external_thunk, false)) {
    // FIXME: Add a warning about failing to specify `-mretpoline` and
    // eventually switch to an error here.
    Features.push_back("+retpoline-indirect-calls");
    Features.push_back("+retpoline-indirect-branches");
    SpectreOpt = options::OPT_mretpoline_external_thunk;
  }

  auto LVIOpt = clang::driver::options::ID::OPT_INVALID;
  if (Args.hasFlag(options::OPT_mlvi_hardening, options::OPT_mno_lvi_hardening,
                   false)) {
    Features.push_back("+lvi-load-hardening");
    Features.push_back("+lvi-cfi"); // load hardening implies CFI protection
    LVIOpt = options::OPT_mlvi_hardening;
  } else if (Args.hasFlag(options::OPT_mlvi_cfi, options::OPT_mno_lvi_cfi,
                          false)) {
    Features.push_back("+lvi-cfi");
    LVIOpt = options::OPT_mlvi_cfi;
  }

  if (Args.hasFlag(options::OPT_m_seses, options::OPT_mno_seses, false)) {
    if (LVIOpt == options::OPT_mlvi_hardening)
      D.Diag(diag::err_drv_argument_not_allowed_with)
          << D.getOpts().getOptionName(options::OPT_mlvi_hardening)
          << D.getOpts().getOptionName(options::OPT_m_seses);

    if (SpectreOpt != clang::driver::options::ID::OPT_INVALID)
      D.Diag(diag::err_drv_argument_not_allowed_with)
          << D.getOpts().getOptionName(SpectreOpt)
          << D.getOpts().getOptionName(options::OPT_m_seses);

    Features.push_back("+seses");
    if (!Args.hasArg(options::OPT_mno_lvi_cfi)) {
      Features.push_back("+lvi-cfi");
      LVIOpt = options::OPT_mlvi_cfi;
    }
  }

  if (SpectreOpt != clang::driver::options::ID::OPT_INVALID &&
      LVIOpt != clang::driver::options::ID::OPT_INVALID) {
    D.Diag(diag::err_drv_argument_not_allowed_with)
        << D.getOpts().getOptionName(SpectreOpt)
        << D.getOpts().getOptionName(LVIOpt);
  }

  for (const Arg *A : Args.filtered(options::OPT_m_x86_AVX10_Features_Group)) {
    StringRef Name = A->getOption().getName();
    A->claim();

    // Skip over "-m".
    assert(Name.starts_with("m") && "Invalid feature name.");
    Name = Name.substr(1);

    bool IsNegative = Name.consume_front("no-");

#ifndef NDEBUG
    assert(Name.starts_with("avx10.") && "Invalid AVX10 feature name.");
    StringRef Version, Width;
    std::tie(Version, Width) = Name.substr(6).split('-');
    assert(Version == "1" && "Invalid AVX10 feature name.");
    assert((Width == "256" || Width == "512") && "Invalid AVX10 feature name.");
#endif

    Features.push_back(Args.MakeArgString((IsNegative ? "-" : "+") + Name));
  }

  // Now add any that the user explicitly requested on the command line,
  // which may override the defaults.
  for (const Arg *A : Args.filtered(options::OPT_m_x86_Features_Group,
                                    options::OPT_mgeneral_regs_only)) {
    StringRef Name = A->getOption().getName();
    A->claim();

    // Skip over "-m".
    assert(Name.starts_with("m") && "Invalid feature name.");
    Name = Name.substr(1);

    // Replace -mgeneral-regs-only with -x87, -mmx, -sse
    if (A->getOption().getID() == options::OPT_mgeneral_regs_only) {
      Features.insert(Features.end(), {"-x87", "-mmx", "-sse"});
      continue;
    }

    bool IsNegative = Name.starts_with("no-");
    if (A->getOption().matches(options::OPT_mapx_features_EQ) ||
        A->getOption().matches(options::OPT_mno_apx_features_EQ)) {

      for (StringRef Value : A->getValues()) {
        if (Value == "egpr" || Value == "push2pop2" || Value == "ppx" ||
            Value == "ndd" || Value == "ccmp" || Value == "nf" ||
            Value == "cf" || Value == "zu") {
          Features.push_back(
              Args.MakeArgString((IsNegative ? "-" : "+") + Value));
          continue;
        }
        D.Diag(clang::diag::err_drv_unsupported_option_argument)
            << A->getSpelling() << Value;
      }
      continue;
    }
    if (IsNegative)
      Name = Name.substr(3);
    Features.push_back(Args.MakeArgString((IsNegative ? "-" : "+") + Name));
  }

  // Enable/disable straight line speculation hardening.
  if (Arg *A = Args.getLastArg(options::OPT_mharden_sls_EQ)) {
    StringRef Scope = A->getValue();
    if (Scope == "all") {
      Features.push_back("+harden-sls-ijmp");
      Features.push_back("+harden-sls-ret");
    } else if (Scope == "return") {
      Features.push_back("+harden-sls-ret");
    } else if (Scope == "indirect-jmp") {
      Features.push_back("+harden-sls-ijmp");
    } else if (Scope != "none") {
      D.Diag(diag::err_drv_unsupported_option_argument)
          << A->getSpelling() << Scope;
    }
  }

  // -mno-gather, -mno-scatter support
  if (Args.hasArg(options::OPT_mno_gather))
    Features.push_back("+prefer-no-gather");
  if (Args.hasArg(options::OPT_mno_scatter))
    Features.push_back("+prefer-no-scatter");
  if (Args.hasArg(options::OPT_mapx_inline_asm_use_gpr32))
    Features.push_back("+inline-asm-use-gpr32");

  // Warn for removed 3dnow support
  if (const Arg *A =
          Args.getLastArg(options::OPT_m3dnowa, options::OPT_mno_3dnowa,
                          options::OPT_mno_3dnow)) {
    if (A->getOption().matches(options::OPT_m3dnowa))
      D.Diag(diag::warn_drv_clang_unsupported) << A->getAsString(Args);
  }
  if (const Arg *A =
          Args.getLastArg(options::OPT_m3dnow, options::OPT_mno_3dnow)) {
    if (A->getOption().matches(options::OPT_m3dnow))
      D.Diag(diag::warn_drv_clang_unsupported) << A->getAsString(Args);
  }
}
