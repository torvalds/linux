//===-- Flang.cpp - Flang+LLVM ToolChain Implementations --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Flang.h"
#include "Arch/RISCV.h"
#include "CommonArgs.h"

#include "clang/Basic/CodeGenOptions.h"
#include "clang/Driver/Options.h"
#include "llvm/Frontend/Debug/Options.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/RISCVISAInfo.h"
#include "llvm/TargetParser/RISCVTargetParser.h"

#include <cassert>

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

/// Add -x lang to \p CmdArgs for \p Input.
static void addDashXForInput(const ArgList &Args, const InputInfo &Input,
                             ArgStringList &CmdArgs) {
  CmdArgs.push_back("-x");
  // Map the driver type to the frontend type.
  CmdArgs.push_back(types::getTypeName(Input.getType()));
}

void Flang::addFortranDialectOptions(const ArgList &Args,
                                     ArgStringList &CmdArgs) const {
  Args.addAllArgs(CmdArgs, {options::OPT_ffixed_form,
                            options::OPT_ffree_form,
                            options::OPT_ffixed_line_length_EQ,
                            options::OPT_fopenacc,
                            options::OPT_finput_charset_EQ,
                            options::OPT_fimplicit_none,
                            options::OPT_fno_implicit_none,
                            options::OPT_fbackslash,
                            options::OPT_fno_backslash,
                            options::OPT_flogical_abbreviations,
                            options::OPT_fno_logical_abbreviations,
                            options::OPT_fxor_operator,
                            options::OPT_fno_xor_operator,
                            options::OPT_falternative_parameter_statement,
                            options::OPT_fdefault_real_8,
                            options::OPT_fdefault_integer_8,
                            options::OPT_fdefault_double_8,
                            options::OPT_flarge_sizes,
                            options::OPT_fno_automatic,
                            options::OPT_fhermetic_module_files});
}

void Flang::addPreprocessingOptions(const ArgList &Args,
                                    ArgStringList &CmdArgs) const {
  Args.addAllArgs(CmdArgs,
                  {options::OPT_P, options::OPT_D, options::OPT_U,
                   options::OPT_I, options::OPT_cpp, options::OPT_nocpp});
}

/// @C shouldLoopVersion
///
/// Check if Loop Versioning should be enabled.
/// We look for the last of one of the following:
///   -Ofast, -O4, -O<number> and -f[no-]version-loops-for-stride.
/// Loop versioning is disabled if the last option is
///  -fno-version-loops-for-stride.
/// Loop versioning is enabled if the last option is one of:
///  -floop-versioning
///  -Ofast
///  -O4
///  -O3
/// For all other cases, loop versioning is is disabled.
///
/// The gfortran compiler automatically enables the option for -O3 or -Ofast.
///
/// @return true if loop-versioning should be enabled, otherwise false.
static bool shouldLoopVersion(const ArgList &Args) {
  const Arg *LoopVersioningArg = Args.getLastArg(
      options::OPT_Ofast, options::OPT_O, options::OPT_O4,
      options::OPT_floop_versioning, options::OPT_fno_loop_versioning);
  if (!LoopVersioningArg)
    return false;

  if (LoopVersioningArg->getOption().matches(options::OPT_fno_loop_versioning))
    return false;

  if (LoopVersioningArg->getOption().matches(options::OPT_floop_versioning))
    return true;

  if (LoopVersioningArg->getOption().matches(options::OPT_Ofast) ||
      LoopVersioningArg->getOption().matches(options::OPT_O4))
    return true;

  if (LoopVersioningArg->getOption().matches(options::OPT_O)) {
    StringRef S(LoopVersioningArg->getValue());
    unsigned OptLevel = 0;
    // Note -Os or Oz woould "fail" here, so return false. Which is the
    // desiered behavior.
    if (S.getAsInteger(10, OptLevel))
      return false;

    return OptLevel > 2;
  }

  llvm_unreachable("We should not end up here");
  return false;
}

void Flang::addOtherOptions(const ArgList &Args, ArgStringList &CmdArgs) const {
  Args.addAllArgs(CmdArgs,
                  {options::OPT_module_dir, options::OPT_fdebug_module_writer,
                   options::OPT_fintrinsic_modules_path, options::OPT_pedantic,
                   options::OPT_std_EQ, options::OPT_W_Joined,
                   options::OPT_fconvert_EQ, options::OPT_fpass_plugin_EQ,
                   options::OPT_funderscoring, options::OPT_fno_underscoring});

  llvm::codegenoptions::DebugInfoKind DebugInfoKind;
  if (Args.hasArg(options::OPT_gN_Group)) {
    Arg *gNArg = Args.getLastArg(options::OPT_gN_Group);
    DebugInfoKind = debugLevelToInfoKind(*gNArg);
  } else if (Args.hasArg(options::OPT_g_Flag)) {
    DebugInfoKind = llvm::codegenoptions::FullDebugInfo;
  } else {
    DebugInfoKind = llvm::codegenoptions::NoDebugInfo;
  }
  addDebugInfoKind(CmdArgs, DebugInfoKind);
}

void Flang::addCodegenOptions(const ArgList &Args,
                              ArgStringList &CmdArgs) const {
  Arg *stackArrays =
      Args.getLastArg(options::OPT_Ofast, options::OPT_fstack_arrays,
                      options::OPT_fno_stack_arrays);
  if (stackArrays &&
      !stackArrays->getOption().matches(options::OPT_fno_stack_arrays))
    CmdArgs.push_back("-fstack-arrays");

  if (shouldLoopVersion(Args))
    CmdArgs.push_back("-fversion-loops-for-stride");

  Args.addAllArgs(CmdArgs, {options::OPT_flang_experimental_hlfir,
                            options::OPT_flang_deprecated_no_hlfir,
                            options::OPT_flang_experimental_integer_overflow,
                            options::OPT_fno_ppc_native_vec_elem_order,
                            options::OPT_fppc_native_vec_elem_order});
}

void Flang::addPicOptions(const ArgList &Args, ArgStringList &CmdArgs) const {
  // ParsePICArgs parses -fPIC/-fPIE and their variants and returns a tuple of
  // (RelocationModel, PICLevel, IsPIE).
  llvm::Reloc::Model RelocationModel;
  unsigned PICLevel;
  bool IsPIE;
  std::tie(RelocationModel, PICLevel, IsPIE) =
      ParsePICArgs(getToolChain(), Args);

  if (auto *RMName = RelocationModelName(RelocationModel)) {
    CmdArgs.push_back("-mrelocation-model");
    CmdArgs.push_back(RMName);
  }
  if (PICLevel > 0) {
    CmdArgs.push_back("-pic-level");
    CmdArgs.push_back(PICLevel == 1 ? "1" : "2");
    if (IsPIE)
      CmdArgs.push_back("-pic-is-pie");
  }
}

void Flang::AddAArch64TargetArgs(const ArgList &Args,
                                 ArgStringList &CmdArgs) const {
  // Handle -msve_vector_bits=<bits>
  if (Arg *A = Args.getLastArg(options::OPT_msve_vector_bits_EQ)) {
    StringRef Val = A->getValue();
    const Driver &D = getToolChain().getDriver();
    if (Val == "128" || Val == "256" || Val == "512" || Val == "1024" ||
        Val == "2048" || Val == "128+" || Val == "256+" || Val == "512+" ||
        Val == "1024+" || Val == "2048+") {
      unsigned Bits = 0;
      if (!Val.consume_back("+")) {
        [[maybe_unused]] bool Invalid = Val.getAsInteger(10, Bits);
        assert(!Invalid && "Failed to parse value");
        CmdArgs.push_back(
            Args.MakeArgString("-mvscale-max=" + llvm::Twine(Bits / 128)));
      }

      [[maybe_unused]] bool Invalid = Val.getAsInteger(10, Bits);
      assert(!Invalid && "Failed to parse value");
      CmdArgs.push_back(
          Args.MakeArgString("-mvscale-min=" + llvm::Twine(Bits / 128)));
      // Silently drop requests for vector-length agnostic code as it's implied.
    } else if (Val != "scalable")
      // Handle the unsupported values passed to msve-vector-bits.
      D.Diag(diag::err_drv_unsupported_option_argument)
          << A->getSpelling() << Val;
  }
}

void Flang::AddRISCVTargetArgs(const ArgList &Args,
                               ArgStringList &CmdArgs) const {
  const llvm::Triple &Triple = getToolChain().getTriple();
  // Handle -mrvv-vector-bits=<bits>
  if (Arg *A = Args.getLastArg(options::OPT_mrvv_vector_bits_EQ)) {
    StringRef Val = A->getValue();
    const Driver &D = getToolChain().getDriver();

    // Get minimum VLen from march.
    unsigned MinVLen = 0;
    std::string Arch = riscv::getRISCVArch(Args, Triple);
    auto ISAInfo = llvm::RISCVISAInfo::parseArchString(
        Arch, /*EnableExperimentalExtensions*/ true);
    // Ignore parsing error.
    if (!errorToBool(ISAInfo.takeError()))
      MinVLen = (*ISAInfo)->getMinVLen();

    // If the value is "zvl", use MinVLen from march. Otherwise, try to parse
    // as integer as long as we have a MinVLen.
    unsigned Bits = 0;
    if (Val == "zvl" && MinVLen >= llvm::RISCV::RVVBitsPerBlock) {
      Bits = MinVLen;
    } else if (!Val.getAsInteger(10, Bits)) {
      // Only accept power of 2 values beteen RVVBitsPerBlock and 65536 that
      // at least MinVLen.
      if (Bits < MinVLen || Bits < llvm::RISCV::RVVBitsPerBlock ||
          Bits > 65536 || !llvm::isPowerOf2_32(Bits))
        Bits = 0;
    }

    // If we got a valid value try to use it.
    if (Bits != 0) {
      unsigned VScaleMin = Bits / llvm::RISCV::RVVBitsPerBlock;
      CmdArgs.push_back(
          Args.MakeArgString("-mvscale-max=" + llvm::Twine(VScaleMin)));
      CmdArgs.push_back(
          Args.MakeArgString("-mvscale-min=" + llvm::Twine(VScaleMin)));
    } else if (Val != "scalable") {
      // Handle the unsupported values passed to mrvv-vector-bits.
      D.Diag(diag::err_drv_unsupported_option_argument)
          << A->getSpelling() << Val;
    }
  }
}

void Flang::AddX86_64TargetArgs(const ArgList &Args,
                                ArgStringList &CmdArgs) const {
  if (Arg *A = Args.getLastArg(options::OPT_masm_EQ)) {
    StringRef Value = A->getValue();
    if (Value == "intel" || Value == "att") {
      CmdArgs.push_back(Args.MakeArgString("-mllvm"));
      CmdArgs.push_back(Args.MakeArgString("-x86-asm-syntax=" + Value));
    } else {
      getToolChain().getDriver().Diag(diag::err_drv_unsupported_option_argument)
          << A->getSpelling() << Value;
    }
  }
}

static void addVSDefines(const ToolChain &TC, const ArgList &Args,
                         ArgStringList &CmdArgs) {

  unsigned ver = 0;
  const VersionTuple vt = TC.computeMSVCVersion(nullptr, Args);
  ver = vt.getMajor() * 10000000 + vt.getMinor().value_or(0) * 100000 +
        vt.getSubminor().value_or(0);
  CmdArgs.push_back(Args.MakeArgString("-D_MSC_VER=" + Twine(ver / 100000)));
  CmdArgs.push_back(Args.MakeArgString("-D_MSC_FULL_VER=" + Twine(ver)));
  CmdArgs.push_back(Args.MakeArgString("-D_WIN32"));

  const llvm::Triple &triple = TC.getTriple();
  if (triple.isAArch64()) {
    CmdArgs.push_back("-D_M_ARM64=1");
  } else if (triple.isX86() && triple.isArch32Bit()) {
    CmdArgs.push_back("-D_M_IX86=600");
  } else if (triple.isX86() && triple.isArch64Bit()) {
    CmdArgs.push_back("-D_M_X64=100");
  } else {
    llvm_unreachable(
        "Flang on Windows only supports X86_32, X86_64 and AArch64");
  }
}

static void processVSRuntimeLibrary(const ToolChain &TC, const ArgList &Args,
                                    ArgStringList &CmdArgs) {
  assert(TC.getTriple().isKnownWindowsMSVCEnvironment() &&
         "can only add VS runtime library on Windows!");
  // if -fno-fortran-main has been passed, skip linking Fortran_main.a
  if (TC.getTriple().isKnownWindowsMSVCEnvironment()) {
    CmdArgs.push_back(Args.MakeArgString(
        "--dependent-lib=" + TC.getCompilerRTBasename(Args, "builtins")));
  }
  unsigned RTOptionID = options::OPT__SLASH_MT;
  if (auto *rtl = Args.getLastArg(options::OPT_fms_runtime_lib_EQ)) {
    RTOptionID = llvm::StringSwitch<unsigned>(rtl->getValue())
                     .Case("static", options::OPT__SLASH_MT)
                     .Case("static_dbg", options::OPT__SLASH_MTd)
                     .Case("dll", options::OPT__SLASH_MD)
                     .Case("dll_dbg", options::OPT__SLASH_MDd)
                     .Default(options::OPT__SLASH_MT);
  }
  switch (RTOptionID) {
  case options::OPT__SLASH_MT:
    CmdArgs.push_back("-D_MT");
    CmdArgs.push_back("--dependent-lib=libcmt");
    CmdArgs.push_back("--dependent-lib=FortranRuntime.static.lib");
    CmdArgs.push_back("--dependent-lib=FortranDecimal.static.lib");
    break;
  case options::OPT__SLASH_MTd:
    CmdArgs.push_back("-D_MT");
    CmdArgs.push_back("-D_DEBUG");
    CmdArgs.push_back("--dependent-lib=libcmtd");
    CmdArgs.push_back("--dependent-lib=FortranRuntime.static_dbg.lib");
    CmdArgs.push_back("--dependent-lib=FortranDecimal.static_dbg.lib");
    break;
  case options::OPT__SLASH_MD:
    CmdArgs.push_back("-D_MT");
    CmdArgs.push_back("-D_DLL");
    CmdArgs.push_back("--dependent-lib=msvcrt");
    CmdArgs.push_back("--dependent-lib=FortranRuntime.dynamic.lib");
    CmdArgs.push_back("--dependent-lib=FortranDecimal.dynamic.lib");
    break;
  case options::OPT__SLASH_MDd:
    CmdArgs.push_back("-D_MT");
    CmdArgs.push_back("-D_DEBUG");
    CmdArgs.push_back("-D_DLL");
    CmdArgs.push_back("--dependent-lib=msvcrtd");
    CmdArgs.push_back("--dependent-lib=FortranRuntime.dynamic_dbg.lib");
    CmdArgs.push_back("--dependent-lib=FortranDecimal.dynamic_dbg.lib");
    break;
  }
}

void Flang::AddAMDGPUTargetArgs(const ArgList &Args,
                                ArgStringList &CmdArgs) const {
  if (Arg *A = Args.getLastArg(options::OPT_mcode_object_version_EQ)) {
    StringRef Val = A->getValue();
    CmdArgs.push_back(Args.MakeArgString("-mcode-object-version=" + Val));
  }
}

void Flang::addTargetOptions(const ArgList &Args,
                             ArgStringList &CmdArgs) const {
  const ToolChain &TC = getToolChain();
  const llvm::Triple &Triple = TC.getEffectiveTriple();
  const Driver &D = TC.getDriver();

  std::string CPU = getCPUName(D, Args, Triple);
  if (!CPU.empty()) {
    CmdArgs.push_back("-target-cpu");
    CmdArgs.push_back(Args.MakeArgString(CPU));
  }

  addOutlineAtomicsArgs(D, getToolChain(), Args, CmdArgs, Triple);

  // Add the target features.
  switch (TC.getArch()) {
  default:
    break;
  case llvm::Triple::aarch64:
    getTargetFeatures(D, Triple, Args, CmdArgs, /*ForAs*/ false);
    AddAArch64TargetArgs(Args, CmdArgs);
    break;

  case llvm::Triple::r600:
  case llvm::Triple::amdgcn:
    getTargetFeatures(D, Triple, Args, CmdArgs, /*ForAs*/ false);
    AddAMDGPUTargetArgs(Args, CmdArgs);
    break;
  case llvm::Triple::riscv64:
    getTargetFeatures(D, Triple, Args, CmdArgs, /*ForAs*/ false);
    AddRISCVTargetArgs(Args, CmdArgs);
    break;
  case llvm::Triple::x86_64:
    getTargetFeatures(D, Triple, Args, CmdArgs, /*ForAs*/ false);
    AddX86_64TargetArgs(Args, CmdArgs);
    break;
  }

  if (Arg *A = Args.getLastArg(options::OPT_fveclib)) {
    StringRef Name = A->getValue();
    if (Name == "SVML") {
      if (Triple.getArch() != llvm::Triple::x86 &&
          Triple.getArch() != llvm::Triple::x86_64)
        D.Diag(diag::err_drv_unsupported_opt_for_target)
            << Name << Triple.getArchName();
    } else if (Name == "LIBMVEC-X86") {
      if (Triple.getArch() != llvm::Triple::x86 &&
          Triple.getArch() != llvm::Triple::x86_64)
        D.Diag(diag::err_drv_unsupported_opt_for_target)
            << Name << Triple.getArchName();
    } else if (Name == "SLEEF" || Name == "ArmPL") {
      if (Triple.getArch() != llvm::Triple::aarch64 &&
          Triple.getArch() != llvm::Triple::aarch64_be)
        D.Diag(diag::err_drv_unsupported_opt_for_target)
            << Name << Triple.getArchName();
    }

    if (Triple.isOSDarwin()) {
      // flang doesn't currently suport nostdlib, nodefaultlibs. Adding these
      // here incase they are added someday
      if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs)) {
        if (A->getValue() == StringRef{"Accelerate"}) {
          CmdArgs.push_back("-framework");
          CmdArgs.push_back("Accelerate");
        }
      }
    }
    A->render(Args, CmdArgs);
  }

  if (Triple.isKnownWindowsMSVCEnvironment()) {
    processVSRuntimeLibrary(TC, Args, CmdArgs);
    addVSDefines(TC, Args, CmdArgs);
  }

  // TODO: Add target specific flags, ABI, mtune option etc.
  if (const Arg *A = Args.getLastArg(options::OPT_mtune_EQ)) {
    CmdArgs.push_back("-tune-cpu");
    if (A->getValue() == StringRef{"native"})
      CmdArgs.push_back(Args.MakeArgString(llvm::sys::getHostCPUName()));
    else
      CmdArgs.push_back(A->getValue());
  }
}

void Flang::addOffloadOptions(Compilation &C, const InputInfoList &Inputs,
                              const JobAction &JA, const ArgList &Args,
                              ArgStringList &CmdArgs) const {
  bool IsOpenMPDevice = JA.isDeviceOffloading(Action::OFK_OpenMP);
  bool IsHostOffloadingAction = JA.isHostOffloading(Action::OFK_OpenMP) ||
                                JA.isHostOffloading(C.getActiveOffloadKinds());

  // Skips the primary input file, which is the input file that the compilation
  // proccess will be executed upon (e.g. the host bitcode file) and
  // adds other secondary input (e.g. device bitcode files for embedding to the
  // -fembed-offload-object argument or the host IR file for proccessing
  // during device compilation to the fopenmp-host-ir-file-path argument via
  // OpenMPDeviceInput). This is condensed logic from the ConstructJob
  // function inside of the Clang driver for pushing on further input arguments
  // needed for offloading during various phases of compilation.
  for (size_t i = 1; i < Inputs.size(); ++i) {
    if (Inputs[i].getType() == types::TY_Nothing) {
      // contains nothing, so it's skippable
    } else if (IsHostOffloadingAction) {
      CmdArgs.push_back(
          Args.MakeArgString("-fembed-offload-object=" +
                             getToolChain().getInputFilename(Inputs[i])));
    } else if (IsOpenMPDevice) {
      if (Inputs[i].getFilename()) {
        CmdArgs.push_back("-fopenmp-host-ir-file-path");
        CmdArgs.push_back(Args.MakeArgString(Inputs[i].getFilename()));
      } else {
        llvm_unreachable("missing openmp host-ir file for device offloading");
      }
    } else {
      llvm_unreachable(
          "unexpectedly given multiple inputs or given unknown input");
    }
  }

  if (IsOpenMPDevice) {
    // -fopenmp-is-target-device is passed along to tell the frontend that it is
    // generating code for a device, so that only the relevant code is emitted.
    CmdArgs.push_back("-fopenmp-is-target-device");

    // When in OpenMP offloading mode, enable debugging on the device.
    Args.AddAllArgs(CmdArgs, options::OPT_fopenmp_target_debug_EQ);
    if (Args.hasFlag(options::OPT_fopenmp_target_debug,
                     options::OPT_fno_openmp_target_debug, /*Default=*/false))
      CmdArgs.push_back("-fopenmp-target-debug");

    // When in OpenMP offloading mode, forward assumptions information about
    // thread and team counts in the device.
    if (Args.hasFlag(options::OPT_fopenmp_assume_teams_oversubscription,
                     options::OPT_fno_openmp_assume_teams_oversubscription,
                     /*Default=*/false))
      CmdArgs.push_back("-fopenmp-assume-teams-oversubscription");
    if (Args.hasFlag(options::OPT_fopenmp_assume_threads_oversubscription,
                     options::OPT_fno_openmp_assume_threads_oversubscription,
                     /*Default=*/false))
      CmdArgs.push_back("-fopenmp-assume-threads-oversubscription");
    if (Args.hasArg(options::OPT_fopenmp_assume_no_thread_state))
      CmdArgs.push_back("-fopenmp-assume-no-thread-state");
    if (Args.hasArg(options::OPT_fopenmp_assume_no_nested_parallelism))
      CmdArgs.push_back("-fopenmp-assume-no-nested-parallelism");
    if (Args.hasArg(options::OPT_nogpulib))
      CmdArgs.push_back("-nogpulib");
  }
}

static void addFloatingPointOptions(const Driver &D, const ArgList &Args,
                                    ArgStringList &CmdArgs) {
  StringRef FPContract;
  bool HonorINFs = true;
  bool HonorNaNs = true;
  bool ApproxFunc = false;
  bool SignedZeros = true;
  bool AssociativeMath = false;
  bool ReciprocalMath = false;

  if (const Arg *A = Args.getLastArg(options::OPT_ffp_contract)) {
    const StringRef Val = A->getValue();
    if (Val == "fast" || Val == "off") {
      FPContract = Val;
    } else if (Val == "on") {
      // Warn instead of error because users might have makefiles written for
      // gfortran (which accepts -ffp-contract=on)
      D.Diag(diag::warn_drv_unsupported_option_for_flang)
          << Val << A->getOption().getName() << "off";
      FPContract = "off";
    } else
      // Clang's "fast-honor-pragmas" option is not supported because it is
      // non-standard
      D.Diag(diag::err_drv_unsupported_option_argument)
          << A->getSpelling() << Val;
  }

  for (const Arg *A : Args) {
    auto optId = A->getOption().getID();
    switch (optId) {
    // if this isn't an FP option, skip the claim below
    default:
      continue;

    case options::OPT_fhonor_infinities:
      HonorINFs = true;
      break;
    case options::OPT_fno_honor_infinities:
      HonorINFs = false;
      break;
    case options::OPT_fhonor_nans:
      HonorNaNs = true;
      break;
    case options::OPT_fno_honor_nans:
      HonorNaNs = false;
      break;
    case options::OPT_fapprox_func:
      ApproxFunc = true;
      break;
    case options::OPT_fno_approx_func:
      ApproxFunc = false;
      break;
    case options::OPT_fsigned_zeros:
      SignedZeros = true;
      break;
    case options::OPT_fno_signed_zeros:
      SignedZeros = false;
      break;
    case options::OPT_fassociative_math:
      AssociativeMath = true;
      break;
    case options::OPT_fno_associative_math:
      AssociativeMath = false;
      break;
    case options::OPT_freciprocal_math:
      ReciprocalMath = true;
      break;
    case options::OPT_fno_reciprocal_math:
      ReciprocalMath = false;
      break;
    case options::OPT_Ofast:
      [[fallthrough]];
    case options::OPT_ffast_math:
      HonorINFs = false;
      HonorNaNs = false;
      AssociativeMath = true;
      ReciprocalMath = true;
      ApproxFunc = true;
      SignedZeros = false;
      FPContract = "fast";
      break;
    case options::OPT_fno_fast_math:
      HonorINFs = true;
      HonorNaNs = true;
      AssociativeMath = false;
      ReciprocalMath = false;
      ApproxFunc = false;
      SignedZeros = true;
      // -fno-fast-math should undo -ffast-math so I return FPContract to the
      // default. It is important to check it is "fast" (the default) so that
      // --ffp-contract=off -fno-fast-math --> -ffp-contract=off
      if (FPContract == "fast")
        FPContract = "";
      break;
    }

    // If we handled this option claim it
    A->claim();
  }

  if (!HonorINFs && !HonorNaNs && AssociativeMath && ReciprocalMath &&
      ApproxFunc && !SignedZeros &&
      (FPContract == "fast" || FPContract.empty())) {
    CmdArgs.push_back("-ffast-math");
    return;
  }

  if (!FPContract.empty())
    CmdArgs.push_back(Args.MakeArgString("-ffp-contract=" + FPContract));

  if (!HonorINFs)
    CmdArgs.push_back("-menable-no-infs");

  if (!HonorNaNs)
    CmdArgs.push_back("-menable-no-nans");

  if (ApproxFunc)
    CmdArgs.push_back("-fapprox-func");

  if (!SignedZeros)
    CmdArgs.push_back("-fno-signed-zeros");

  if (AssociativeMath && !SignedZeros)
    CmdArgs.push_back("-mreassociate");

  if (ReciprocalMath)
    CmdArgs.push_back("-freciprocal-math");
}

static void renderRemarksOptions(const ArgList &Args, ArgStringList &CmdArgs,
                                 const InputInfo &Input) {
  StringRef Format = "yaml";
  if (const Arg *A = Args.getLastArg(options::OPT_fsave_optimization_record_EQ))
    Format = A->getValue();

  CmdArgs.push_back("-opt-record-file");

  const Arg *A = Args.getLastArg(options::OPT_foptimization_record_file_EQ);
  if (A) {
    CmdArgs.push_back(A->getValue());
  } else {
    SmallString<128> F;

    if (Args.hasArg(options::OPT_c) || Args.hasArg(options::OPT_S)) {
      if (Arg *FinalOutput = Args.getLastArg(options::OPT_o))
        F = FinalOutput->getValue();
    }

    if (F.empty()) {
      // Use the input filename.
      F = llvm::sys::path::stem(Input.getBaseInput());
    }

    SmallString<32> Extension;
    Extension += "opt.";
    Extension += Format;

    llvm::sys::path::replace_extension(F, Extension);
    CmdArgs.push_back(Args.MakeArgString(F));
  }

  if (const Arg *A =
          Args.getLastArg(options::OPT_foptimization_record_passes_EQ)) {
    CmdArgs.push_back("-opt-record-passes");
    CmdArgs.push_back(A->getValue());
  }

  if (!Format.empty()) {
    CmdArgs.push_back("-opt-record-format");
    CmdArgs.push_back(Format.data());
  }
}

void Flang::ConstructJob(Compilation &C, const JobAction &JA,
                         const InputInfo &Output, const InputInfoList &Inputs,
                         const ArgList &Args, const char *LinkingOutput) const {
  const auto &TC = getToolChain();
  const llvm::Triple &Triple = TC.getEffectiveTriple();
  const std::string &TripleStr = Triple.getTriple();

  const Driver &D = TC.getDriver();
  ArgStringList CmdArgs;
  DiagnosticsEngine &Diags = D.getDiags();

  // Invoke ourselves in -fc1 mode.
  CmdArgs.push_back("-fc1");

  // Add the "effective" target triple.
  CmdArgs.push_back("-triple");
  CmdArgs.push_back(Args.MakeArgString(TripleStr));

  if (isa<PreprocessJobAction>(JA)) {
    CmdArgs.push_back("-E");
    if (Args.getLastArg(options::OPT_dM)) {
      CmdArgs.push_back("-dM");
    }
  } else if (isa<CompileJobAction>(JA) || isa<BackendJobAction>(JA)) {
    if (JA.getType() == types::TY_Nothing) {
      CmdArgs.push_back("-fsyntax-only");
    } else if (JA.getType() == types::TY_AST) {
      CmdArgs.push_back("-emit-ast");
    } else if (JA.getType() == types::TY_LLVM_IR ||
               JA.getType() == types::TY_LTO_IR) {
      CmdArgs.push_back("-emit-llvm");
    } else if (JA.getType() == types::TY_LLVM_BC ||
               JA.getType() == types::TY_LTO_BC) {
      CmdArgs.push_back("-emit-llvm-bc");
    } else if (JA.getType() == types::TY_PP_Asm) {
      CmdArgs.push_back("-S");
    } else {
      assert(false && "Unexpected output type!");
    }
  } else if (isa<AssembleJobAction>(JA)) {
    CmdArgs.push_back("-emit-obj");
  } else {
    assert(false && "Unexpected action class for Flang tool.");
  }

  const InputInfo &Input = Inputs[0];
  types::ID InputType = Input.getType();

  // Add preprocessing options like -I, -D, etc. if we are using the
  // preprocessor (i.e. skip when dealing with e.g. binary files).
  if (types::getPreprocessedType(InputType) != types::TY_INVALID)
    addPreprocessingOptions(Args, CmdArgs);

  addFortranDialectOptions(Args, CmdArgs);

  // Color diagnostics are parsed by the driver directly from argv and later
  // re-parsed to construct this job; claim any possible color diagnostic here
  // to avoid warn_drv_unused_argument.
  Args.getLastArg(options::OPT_fcolor_diagnostics,
                  options::OPT_fno_color_diagnostics);
  if (Diags.getDiagnosticOptions().ShowColors)
    CmdArgs.push_back("-fcolor-diagnostics");

  // LTO mode is parsed by the Clang driver library.
  LTOKind LTOMode = D.getLTOMode(/* IsOffload */ false);
  assert(LTOMode != LTOK_Unknown && "Unknown LTO mode.");
  if (LTOMode == LTOK_Full)
    CmdArgs.push_back("-flto=full");
  else if (LTOMode == LTOK_Thin) {
    Diags.Report(
        Diags.getCustomDiagID(DiagnosticsEngine::Warning,
                              "the option '-flto=thin' is a work in progress"));
    CmdArgs.push_back("-flto=thin");
  }

  // -fPIC and related options.
  addPicOptions(Args, CmdArgs);

  // Floating point related options
  addFloatingPointOptions(D, Args, CmdArgs);

  // Add target args, features, etc.
  addTargetOptions(Args, CmdArgs);

  llvm::Reloc::Model RelocationModel =
      std::get<0>(ParsePICArgs(getToolChain(), Args));
  // Add MCModel information
  addMCModel(D, Args, Triple, RelocationModel, CmdArgs);

  // Add Codegen options
  addCodegenOptions(Args, CmdArgs);

  // Add R Group options
  Args.AddAllArgs(CmdArgs, options::OPT_R_Group);

  // Remarks can be enabled with any of the `-f.*optimization-record.*` flags.
  if (willEmitRemarks(Args))
    renderRemarksOptions(Args, CmdArgs, Input);

  // Add other compile options
  addOtherOptions(Args, CmdArgs);

  // Disable all warnings
  // TODO: Handle interactions between -w, -pedantic, -Wall, -WOption
  Args.AddLastArg(CmdArgs, options::OPT_w);

  // Forward flags for OpenMP. We don't do this if the current action is an
  // device offloading action other than OpenMP.
  if (Args.hasFlag(options::OPT_fopenmp, options::OPT_fopenmp_EQ,
                   options::OPT_fno_openmp, false) &&
      (JA.isDeviceOffloading(Action::OFK_None) ||
       JA.isDeviceOffloading(Action::OFK_OpenMP))) {
    switch (D.getOpenMPRuntime(Args)) {
    case Driver::OMPRT_OMP:
    case Driver::OMPRT_IOMP5:
      // Clang can generate useful OpenMP code for these two runtime libraries.
      CmdArgs.push_back("-fopenmp");
      Args.AddAllArgs(CmdArgs, options::OPT_fopenmp_version_EQ);

      if (Args.hasArg(options::OPT_fopenmp_force_usm))
        CmdArgs.push_back("-fopenmp-force-usm");

      // FIXME: Clang supports a whole bunch more flags here.
      break;
    default:
      // By default, if Clang doesn't know how to generate useful OpenMP code
      // for a specific runtime library, we just don't pass the '-fopenmp' flag
      // down to the actual compilation.
      // FIXME: It would be better to have a mode which *only* omits IR
      // generation based on the OpenMP support so that we get consistent
      // semantic analysis, etc.
      const Arg *A = Args.getLastArg(options::OPT_fopenmp_EQ);
      D.Diag(diag::warn_drv_unsupported_openmp_library)
          << A->getSpelling() << A->getValue();
      break;
    }
  }

  // Pass the path to compiler resource files.
  CmdArgs.push_back("-resource-dir");
  CmdArgs.push_back(D.ResourceDir.c_str());

  // Offloading related options
  addOffloadOptions(C, Inputs, JA, Args, CmdArgs);

  // Forward -Xflang arguments to -fc1
  Args.AddAllArgValues(CmdArgs, options::OPT_Xflang);

  CodeGenOptions::FramePointerKind FPKeepKind =
      getFramePointerKind(Args, Triple);

  const char *FPKeepKindStr = nullptr;
  switch (FPKeepKind) {
  case CodeGenOptions::FramePointerKind::None:
    FPKeepKindStr = "-mframe-pointer=none";
    break;
  case CodeGenOptions::FramePointerKind::Reserved:
    FPKeepKindStr = "-mframe-pointer=reserved";
    break;
  case CodeGenOptions::FramePointerKind::NonLeaf:
    FPKeepKindStr = "-mframe-pointer=non-leaf";
    break;
  case CodeGenOptions::FramePointerKind::All:
    FPKeepKindStr = "-mframe-pointer=all";
    break;
  }
  assert(FPKeepKindStr && "unknown FramePointerKind");
  CmdArgs.push_back(FPKeepKindStr);

  // Forward -mllvm options to the LLVM option parser. In practice, this means
  // forwarding to `-fc1` as that's where the LLVM parser is run.
  for (const Arg *A : Args.filtered(options::OPT_mllvm)) {
    A->claim();
    A->render(Args, CmdArgs);
  }

  for (const Arg *A : Args.filtered(options::OPT_mmlir)) {
    A->claim();
    A->render(Args, CmdArgs);
  }

  // Remove any unsupported gfortran diagnostic options
  for (const Arg *A : Args.filtered(options::OPT_flang_ignored_w_Group)) {
    A->claim();
    D.Diag(diag::warn_drv_unsupported_diag_option_for_flang)
        << A->getOption().getName();
  }

  // Optimization level for CodeGen.
  if (const Arg *A = Args.getLastArg(options::OPT_O_Group)) {
    if (A->getOption().matches(options::OPT_O4)) {
      CmdArgs.push_back("-O3");
      D.Diag(diag::warn_O4_is_O3);
    } else if (A->getOption().matches(options::OPT_Ofast)) {
      CmdArgs.push_back("-O3");
    } else {
      A->render(Args, CmdArgs);
    }
  }

  assert((Output.isFilename() || Output.isNothing()) && "Invalid output.");
  if (Output.isFilename()) {
    CmdArgs.push_back("-o");
    CmdArgs.push_back(Output.getFilename());
  }

  assert(Input.isFilename() && "Invalid input.");

  if (Args.getLastArg(options::OPT_save_temps_EQ))
    Args.AddLastArg(CmdArgs, options::OPT_save_temps_EQ);

  addDashXForInput(Args, Input, CmdArgs);

  CmdArgs.push_back(Input.getFilename());

  // TODO: Replace flang-new with flang once the new driver replaces the
  // throwaway driver
  const char *Exec = Args.MakeArgString(D.GetProgramPath("flang-new", TC));
  C.addCommand(std::make_unique<Command>(JA, *this,
                                         ResponseFileSupport::AtFileUTF8(),
                                         Exec, CmdArgs, Inputs, Output));
}

Flang::Flang(const ToolChain &TC) : Tool("flang-new", "flang frontend", TC) {}

Flang::~Flang() {}
