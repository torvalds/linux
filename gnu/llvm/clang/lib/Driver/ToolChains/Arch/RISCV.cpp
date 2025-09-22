//===--- RISCV.cpp - RISC-V Helpers for Tools -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCV.h"
#include "../Clang.h"
#include "ToolChains/CommonArgs.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/RISCVISAInfo.h"
#include "llvm/TargetParser/RISCVTargetParser.h"

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

// Returns false if an error is diagnosed.
static bool getArchFeatures(const Driver &D, StringRef Arch,
                            std::vector<StringRef> &Features,
                            const ArgList &Args) {
  bool EnableExperimentalExtensions =
      Args.hasArg(options::OPT_menable_experimental_extensions);
  auto ISAInfo =
      llvm::RISCVISAInfo::parseArchString(Arch, EnableExperimentalExtensions);
  if (!ISAInfo) {
    handleAllErrors(ISAInfo.takeError(), [&](llvm::StringError &ErrMsg) {
      D.Diag(diag::err_drv_invalid_riscv_arch_name)
          << Arch << ErrMsg.getMessage();
    });

    return false;
  }

  for (const std::string &Str : (*ISAInfo)->toFeatures(/*AddAllExtension=*/true,
                                                       /*IgnoreUnknown=*/false))
    Features.push_back(Args.MakeArgString(Str));

  if (EnableExperimentalExtensions)
    Features.push_back(Args.MakeArgString("+experimental"));

  return true;
}

// Get features except standard extension feature
static void getRISCFeaturesFromMcpu(const Driver &D, const Arg *A,
                                    const llvm::Triple &Triple,
                                    StringRef Mcpu,
                                    std::vector<StringRef> &Features) {
  bool Is64Bit = Triple.isRISCV64();
  if (!llvm::RISCV::parseCPU(Mcpu, Is64Bit)) {
    // Try inverting Is64Bit in case the CPU is valid, but for the wrong target.
    if (llvm::RISCV::parseCPU(Mcpu, !Is64Bit))
      D.Diag(clang::diag::err_drv_invalid_riscv_cpu_name_for_target)
          << Mcpu << Is64Bit;
    else
      D.Diag(clang::diag::err_drv_unsupported_option_argument)
          << A->getSpelling() << Mcpu;
  }
}

void riscv::getRISCVTargetFeatures(const Driver &D, const llvm::Triple &Triple,
                                   const ArgList &Args,
                                   std::vector<StringRef> &Features) {
  std::string MArch = getRISCVArch(Args, Triple);

  if (!getArchFeatures(D, MArch, Features, Args))
    return;

  bool CPUFastScalarUnaligned = false;
  bool CPUFastVectorUnaligned = false;

  // If users give march and mcpu, get std extension feature from MArch
  // and other features (ex. mirco architecture feature) from mcpu
  if (Arg *A = Args.getLastArg(options::OPT_mcpu_EQ)) {
    StringRef CPU = A->getValue();
    if (CPU == "native")
      CPU = llvm::sys::getHostCPUName();

    getRISCFeaturesFromMcpu(D, A, Triple, CPU, Features);

    if (llvm::RISCV::hasFastScalarUnalignedAccess(CPU))
      CPUFastScalarUnaligned = true;
    if (llvm::RISCV::hasFastVectorUnalignedAccess(CPU))
      CPUFastVectorUnaligned = true;
  }

  // Handle features corresponding to "-ffixed-X" options
  if (Args.hasArg(options::OPT_ffixed_x1))
    Features.push_back("+reserve-x1");
  if (Args.hasArg(options::OPT_ffixed_x2))
    Features.push_back("+reserve-x2");
  if (Args.hasArg(options::OPT_ffixed_x3))
    Features.push_back("+reserve-x3");
  if (Args.hasArg(options::OPT_ffixed_x4))
    Features.push_back("+reserve-x4");
  if (Args.hasArg(options::OPT_ffixed_x5))
    Features.push_back("+reserve-x5");
  if (Args.hasArg(options::OPT_ffixed_x6))
    Features.push_back("+reserve-x6");
  if (Args.hasArg(options::OPT_ffixed_x7))
    Features.push_back("+reserve-x7");
  if (Args.hasArg(options::OPT_ffixed_x8))
    Features.push_back("+reserve-x8");
  if (Args.hasArg(options::OPT_ffixed_x9))
    Features.push_back("+reserve-x9");
  if (Args.hasArg(options::OPT_ffixed_x10))
    Features.push_back("+reserve-x10");
  if (Args.hasArg(options::OPT_ffixed_x11))
    Features.push_back("+reserve-x11");
  if (Args.hasArg(options::OPT_ffixed_x12))
    Features.push_back("+reserve-x12");
  if (Args.hasArg(options::OPT_ffixed_x13))
    Features.push_back("+reserve-x13");
  if (Args.hasArg(options::OPT_ffixed_x14))
    Features.push_back("+reserve-x14");
  if (Args.hasArg(options::OPT_ffixed_x15))
    Features.push_back("+reserve-x15");
  if (Args.hasArg(options::OPT_ffixed_x16))
    Features.push_back("+reserve-x16");
  if (Args.hasArg(options::OPT_ffixed_x17))
    Features.push_back("+reserve-x17");
  if (Args.hasArg(options::OPT_ffixed_x18))
    Features.push_back("+reserve-x18");
  if (Args.hasArg(options::OPT_ffixed_x19))
    Features.push_back("+reserve-x19");
  if (Args.hasArg(options::OPT_ffixed_x20))
    Features.push_back("+reserve-x20");
  if (Args.hasArg(options::OPT_ffixed_x21))
    Features.push_back("+reserve-x21");
  if (Args.hasArg(options::OPT_ffixed_x22))
    Features.push_back("+reserve-x22");
  if (Args.hasArg(options::OPT_ffixed_x23))
    Features.push_back("+reserve-x23");
  if (Args.hasArg(options::OPT_ffixed_x24))
    Features.push_back("+reserve-x24");
  if (Args.hasArg(options::OPT_ffixed_x25))
    Features.push_back("+reserve-x25");
  if (Args.hasArg(options::OPT_ffixed_x26))
    Features.push_back("+reserve-x26");
  if (Args.hasArg(options::OPT_ffixed_x27))
    Features.push_back("+reserve-x27");
  if (Args.hasArg(options::OPT_ffixed_x28))
    Features.push_back("+reserve-x28");
  if (Args.hasArg(options::OPT_ffixed_x29))
    Features.push_back("+reserve-x29");
  if (Args.hasArg(options::OPT_ffixed_x30))
    Features.push_back("+reserve-x30");
  if (Args.hasArg(options::OPT_ffixed_x31))
    Features.push_back("+reserve-x31");

#ifdef __OpenBSD__
  // -mno-relax is default, unless -mrelax is specified.
  if (Args.hasFlag(options::OPT_mrelax, options::OPT_mno_relax, false))
    Features.push_back("+relax");
  else
    Features.push_back("-relax");
#else
  // -mrelax is default, unless -mno-relax is specified.
  if (Args.hasFlag(options::OPT_mrelax, options::OPT_mno_relax, true)) {
    Features.push_back("+relax");
    // -gsplit-dwarf -mrelax requires DW_AT_high_pc/DW_AT_ranges/... indexing
    // into .debug_addr, which is currently not implemented.
    Arg *A;
    if (getDebugFissionKind(D, Args, A) != DwarfFissionKind::None)
      D.Diag(clang::diag::err_drv_riscv_unsupported_with_linker_relaxation)
          << A->getAsString(Args);
  } else {
    Features.push_back("-relax");
  }
#endif

  // If -mstrict-align, -mno-strict-align, -mscalar-strict-align, or
  // -mno-scalar-strict-align is passed, use it. Otherwise, the
  // unaligned-scalar-mem is enabled if the CPU supports it or the target is
  // Android.
  if (const Arg *A = Args.getLastArg(
          options::OPT_mno_strict_align, options::OPT_mscalar_strict_align,
          options::OPT_mstrict_align, options::OPT_mno_scalar_strict_align)) {
    if (A->getOption().matches(options::OPT_mno_strict_align) ||
        A->getOption().matches(options::OPT_mno_scalar_strict_align)) {
      Features.push_back("+unaligned-scalar-mem");
    } else {
      Features.push_back("-unaligned-scalar-mem");
    }
  } else if (CPUFastScalarUnaligned || Triple.isAndroid()) {
    Features.push_back("+unaligned-scalar-mem");
  }

  // If -mstrict-align, -mno-strict-align, -mvector-strict-align, or
  // -mno-vector-strict-align is passed, use it. Otherwise, the
  // unaligned-vector-mem is enabled if the CPU supports it or the target is
  // Android.
  if (const Arg *A = Args.getLastArg(
          options::OPT_mno_strict_align, options::OPT_mvector_strict_align,
          options::OPT_mstrict_align, options::OPT_mno_vector_strict_align)) {
    if (A->getOption().matches(options::OPT_mno_strict_align) ||
        A->getOption().matches(options::OPT_mno_vector_strict_align)) {
      Features.push_back("+unaligned-vector-mem");
    } else {
      Features.push_back("-unaligned-vector-mem");
    }
  } else if (CPUFastVectorUnaligned || Triple.isAndroid()) {
    Features.push_back("+unaligned-vector-mem");
  }

  // Now add any that the user explicitly requested on the command line,
  // which may override the defaults.
  handleTargetFeaturesGroup(D, Triple, Args, Features,
                            options::OPT_m_riscv_Features_Group);
}

StringRef riscv::getRISCVABI(const ArgList &Args, const llvm::Triple &Triple) {
  assert(Triple.isRISCV() && "Unexpected triple");

  // GCC's logic around choosing a default `-mabi=` is complex. If GCC is not
  // configured using `--with-abi=`, then the logic for the default choice is
  // defined in config.gcc. This function is based on the logic in GCC 9.2.0.
  //
  // The logic used in GCC 9.2.0 is the following, in order:
  // 1. Explicit choices using `--with-abi=`
  // 2. A default based on `--with-arch=`, if provided
  // 3. A default based on the target triple's arch
  //
  // The logic in config.gcc is a little circular but it is not inconsistent.
  //
  // Clang does not have `--with-arch=` or `--with-abi=`, so we use `-march=`
  // and `-mabi=` respectively instead.
  //
  // In order to make chosing logic more clear, Clang uses the following logic,
  // in order:
  // 1. Explicit choices using `-mabi=`
  // 2. A default based on the architecture as determined by getRISCVArch
  // 3. Choose a default based on the triple

  // 1. If `-mabi=` is specified, use it.
  if (const Arg *A = Args.getLastArg(options::OPT_mabi_EQ))
    return A->getValue();

  // 2. Choose a default based on the target architecture.
  //
  // rv32g | rv32*d -> ilp32d
  // rv32e -> ilp32e
  // rv32* -> ilp32
  // rv64g | rv64*d -> lp64d
  // rv64e -> lp64e
  // rv64* -> lp64
  std::string Arch = getRISCVArch(Args, Triple);

  auto ParseResult = llvm::RISCVISAInfo::parseArchString(
      Arch, /* EnableExperimentalExtension */ true);
  // Ignore parsing error, just go 3rd step.
  if (!llvm::errorToBool(ParseResult.takeError()))
    return (*ParseResult)->computeDefaultABI();

  // 3. Choose a default based on the triple
  //
  // We deviate from GCC's defaults here:
  // - On `riscv{XLEN}-unknown-elf` we use the integer calling convention only.
  // - On all other OSs we use the double floating point calling convention.
  if (Triple.isRISCV32()) {
    if (Triple.getOS() == llvm::Triple::UnknownOS)
      return "ilp32";
    else
      return "ilp32d";
  } else {
    if (Triple.getOS() == llvm::Triple::UnknownOS)
      return "lp64";
    else
      return "lp64d";
  }
}

std::string riscv::getRISCVArch(const llvm::opt::ArgList &Args,
                                const llvm::Triple &Triple) {
  assert(Triple.isRISCV() && "Unexpected triple");

  // GCC's logic around choosing a default `-march=` is complex. If GCC is not
  // configured using `--with-arch=`, then the logic for the default choice is
  // defined in config.gcc. This function is based on the logic in GCC 9.2.0. We
  // deviate from GCC's default on additional `-mcpu` option (GCC does not
  // support `-mcpu`) and baremetal targets (UnknownOS) where neither `-march`
  // nor `-mabi` is specified.
  //
  // The logic used in GCC 9.2.0 is the following, in order:
  // 1. Explicit choices using `--with-arch=`
  // 2. A default based on `--with-abi=`, if provided
  // 3. A default based on the target triple's arch
  //
  // The logic in config.gcc is a little circular but it is not inconsistent.
  //
  // Clang does not have `--with-arch=` or `--with-abi=`, so we use `-march=`
  // and `-mabi=` respectively instead.
  //
  // Clang uses the following logic, in order:
  // 1. Explicit choices using `-march=`
  // 2. Based on `-mcpu` if the target CPU has a default ISA string
  // 3. A default based on `-mabi`, if provided
  // 4. A default based on the target triple's arch
  //
  // Clang does not yet support MULTILIB_REUSE, so we use `rv{XLEN}imafdc`
  // instead of `rv{XLEN}gc` though they are (currently) equivalent.

  // 1. If `-march=` is specified, use it.
  if (const Arg *A = Args.getLastArg(options::OPT_march_EQ))
    return A->getValue();

  // 2. Get march (isa string) based on `-mcpu=`
  if (const Arg *A = Args.getLastArg(options::OPT_mcpu_EQ)) {
    StringRef CPU = A->getValue();
    if (CPU == "native") {
      CPU = llvm::sys::getHostCPUName();
      // If the target cpu is unrecognized, use target features.
      if (CPU.starts_with("generic")) {
        auto FeatureMap = llvm::sys::getHostCPUFeatures();
        // hwprobe may be unavailable on older Linux versions.
        if (!FeatureMap.empty()) {
          std::vector<std::string> Features;
          for (auto &F : FeatureMap)
            Features.push_back(((F.second ? "+" : "-") + F.first()).str());
          auto ParseResult = llvm::RISCVISAInfo::parseFeatures(
              Triple.isRISCV32() ? 32 : 64, Features);
          if (ParseResult)
            return (*ParseResult)->toString();
        }
      }
    }

    StringRef MArch = llvm::RISCV::getMArchFromMcpu(CPU);
    // Bypass if target cpu's default march is empty.
    if (MArch != "")
      return MArch.str();
  }

  // 3. Choose a default based on `-mabi=`
  //
  // ilp32e -> rv32e
  // lp64e -> rv64e
  // ilp32 | ilp32f | ilp32d -> rv32imafdc
  // lp64 | lp64f | lp64d -> rv64imafdc
  if (const Arg *A = Args.getLastArg(options::OPT_mabi_EQ)) {
    StringRef MABI = A->getValue();

    if (MABI.equals_insensitive("ilp32e"))
      return "rv32e";
    else if (MABI.equals_insensitive("lp64e"))
      return "rv64e";
    else if (MABI.starts_with_insensitive("ilp32"))
      return "rv32imafdc";
    else if (MABI.starts_with_insensitive("lp64")) {
      if (Triple.isAndroid())
        return "rv64imafdcv_zba_zbb_zbs";

      return "rv64imafdc";
    }
  }

  // 4. Choose a default based on the triple
  //
  // We deviate from GCC's defaults here:
  // - On `riscv{XLEN}-unknown-elf` we default to `rv{XLEN}imac`
  // - On all other OSs we use `rv{XLEN}imafdc` (equivalent to `rv{XLEN}gc`)
  if (Triple.isRISCV32()) {
    if (Triple.getOS() == llvm::Triple::UnknownOS)
      return "rv32imac";
    else
      return "rv32imafdc";
  } else {
    if (Triple.getOS() == llvm::Triple::UnknownOS)
      return "rv64imac";
    else if (Triple.isAndroid())
      return "rv64imafdcv_zba_zbb_zbs";
    else
      return "rv64imafdc";
  }
}

std::string riscv::getRISCVTargetCPU(const llvm::opt::ArgList &Args,
                                     const llvm::Triple &Triple) {
  std::string CPU;
  // If we have -mcpu, use that.
  if (const Arg *A = Args.getLastArg(options::OPT_mcpu_EQ))
    CPU = A->getValue();

  // Handle CPU name is 'native'.
  if (CPU == "native")
    CPU = llvm::sys::getHostCPUName();

  if (!CPU.empty())
    return CPU;

  return Triple.isRISCV64() ? "generic-rv64" : "generic-rv32";
}
