//===--- Gnu.cpp - Gnu Tool and ToolChain Implementations -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Gnu.h"
#include "Arch/ARM.h"
#include "Arch/CSKY.h"
#include "Arch/LoongArch.h"
#include "Arch/Mips.h"
#include "Arch/PPC.h"
#include "Arch/RISCV.h"
#include "Arch/Sparc.h"
#include "Arch/SystemZ.h"
#include "CommonArgs.h"
#include "Linux.h"
#include "clang/Config/config.h" // for GCC_INSTALL_PREFIX
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/MultilibBuilder.h"
#include "clang/Driver/Options.h"
#include "clang/Driver/Tool.h"
#include "clang/Driver/ToolChain.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/TargetParser/RISCVISAInfo.h"
#include "llvm/TargetParser/TargetParser.h"
#include <system_error>

using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace clang;
using namespace llvm::opt;

using tools::addMultilibFlag;
using tools::addPathIfExists;

static bool forwardToGCC(const Option &O) {
  // LinkerInput options have been forwarded. Don't duplicate.
  if (O.hasFlag(options::LinkerInput))
    return false;
  return O.matches(options::OPT_Link_Group) || O.hasFlag(options::LinkOption);
}

// Switch CPU names not recognized by GNU assembler to a close CPU that it does
// recognize, instead of a lower march from being picked in the absence of a cpu
// flag.
static void normalizeCPUNamesForAssembler(const ArgList &Args,
                                          ArgStringList &CmdArgs) {
  if (Arg *A = Args.getLastArg(options::OPT_mcpu_EQ)) {
    StringRef CPUArg(A->getValue());
    if (CPUArg.equals_insensitive("krait"))
      CmdArgs.push_back("-mcpu=cortex-a15");
    else if (CPUArg.equals_insensitive("kryo"))
      CmdArgs.push_back("-mcpu=cortex-a57");
    else
      Args.AddLastArg(CmdArgs, options::OPT_mcpu_EQ);
  }
}

void tools::gcc::Common::ConstructJob(Compilation &C, const JobAction &JA,
                                      const InputInfo &Output,
                                      const InputInfoList &Inputs,
                                      const ArgList &Args,
                                      const char *LinkingOutput) const {
  const Driver &D = getToolChain().getDriver();
  ArgStringList CmdArgs;

  for (const auto &A : Args) {
    if (forwardToGCC(A->getOption())) {
      // It is unfortunate that we have to claim here, as this means
      // we will basically never report anything interesting for
      // platforms using a generic gcc, even if we are just using gcc
      // to get to the assembler.
      A->claim();

      A->render(Args, CmdArgs);
    }
  }

  RenderExtraToolArgs(JA, CmdArgs);

  // If using a driver, force the arch.
  if (getToolChain().getTriple().isOSDarwin()) {
    CmdArgs.push_back("-arch");
    CmdArgs.push_back(
        Args.MakeArgString(getToolChain().getDefaultUniversalArchName()));
  }

  // Try to force gcc to match the tool chain we want, if we recognize
  // the arch.
  //
  // FIXME: The triple class should directly provide the information we want
  // here.
  switch (getToolChain().getArch()) {
  default:
    break;
  case llvm::Triple::x86:
  case llvm::Triple::ppc:
  case llvm::Triple::ppcle:
    CmdArgs.push_back("-m32");
    break;
  case llvm::Triple::x86_64:
  case llvm::Triple::ppc64:
  case llvm::Triple::ppc64le:
    CmdArgs.push_back("-m64");
    break;
  case llvm::Triple::sparcel:
    CmdArgs.push_back("-EL");
    break;
  }

  assert((Output.isFilename() || Output.isNothing()) && "Invalid output.");
  if (Output.isFilename()) {
    CmdArgs.push_back("-o");
    CmdArgs.push_back(Output.getFilename());
  } else {
    CmdArgs.push_back("-fsyntax-only");
  }

  Args.AddAllArgValues(CmdArgs, options::OPT_Wa_COMMA, options::OPT_Xassembler);

  // Only pass -x if gcc will understand it; otherwise hope gcc
  // understands the suffix correctly. The main use case this would go
  // wrong in is for linker inputs if they happened to have an odd
  // suffix; really the only way to get this to happen is a command
  // like '-x foobar a.c' which will treat a.c like a linker input.
  //
  // FIXME: For the linker case specifically, can we safely convert
  // inputs into '-Wl,' options?
  for (const auto &II : Inputs) {
    // Don't try to pass LLVM or AST inputs to a generic gcc.
    if (types::isLLVMIR(II.getType()))
      D.Diag(clang::diag::err_drv_no_linker_llvm_support)
          << getToolChain().getTripleString();
    else if (II.getType() == types::TY_AST)
      D.Diag(diag::err_drv_no_ast_support) << getToolChain().getTripleString();
    else if (II.getType() == types::TY_ModuleFile)
      D.Diag(diag::err_drv_no_module_support)
          << getToolChain().getTripleString();

    if (types::canTypeBeUserSpecified(II.getType())) {
      CmdArgs.push_back("-x");
      CmdArgs.push_back(types::getTypeName(II.getType()));
    }

    if (II.isFilename())
      CmdArgs.push_back(II.getFilename());
    else {
      const Arg &A = II.getInputArg();

      // Reverse translate some rewritten options.
      if (A.getOption().matches(options::OPT_Z_reserved_lib_stdcxx)) {
        CmdArgs.push_back("-lstdc++");
        continue;
      }

      // Don't render as input, we need gcc to do the translations.
      A.render(Args, CmdArgs);
    }
  }

  const std::string &customGCCName = D.getCCCGenericGCCName();
  const char *GCCName;
  if (!customGCCName.empty())
    GCCName = customGCCName.c_str();
  else if (D.CCCIsCXX()) {
    GCCName = "g++";
  } else
    GCCName = "gcc";

  const char *Exec = Args.MakeArgString(getToolChain().GetProgramPath(GCCName));
  C.addCommand(std::make_unique<Command>(JA, *this,
                                         ResponseFileSupport::AtFileCurCP(),
                                         Exec, CmdArgs, Inputs, Output));
}

void tools::gcc::Preprocessor::RenderExtraToolArgs(
    const JobAction &JA, ArgStringList &CmdArgs) const {
  CmdArgs.push_back("-E");
}

void tools::gcc::Compiler::RenderExtraToolArgs(const JobAction &JA,
                                               ArgStringList &CmdArgs) const {
  const Driver &D = getToolChain().getDriver();

  switch (JA.getType()) {
  // If -flto, etc. are present then make sure not to force assembly output.
  case types::TY_LLVM_IR:
  case types::TY_LTO_IR:
  case types::TY_LLVM_BC:
  case types::TY_LTO_BC:
    CmdArgs.push_back("-c");
    break;
  // We assume we've got an "integrated" assembler in that gcc will produce an
  // object file itself.
  case types::TY_Object:
    CmdArgs.push_back("-c");
    break;
  case types::TY_PP_Asm:
    CmdArgs.push_back("-S");
    break;
  case types::TY_Nothing:
    CmdArgs.push_back("-fsyntax-only");
    break;
  default:
    D.Diag(diag::err_drv_invalid_gcc_output_type) << getTypeName(JA.getType());
  }
}

void tools::gcc::Linker::RenderExtraToolArgs(const JobAction &JA,
                                             ArgStringList &CmdArgs) const {
  // The types are (hopefully) good enough.
}

static const char *getLDMOption(const llvm::Triple &T, const ArgList &Args) {
  switch (T.getArch()) {
  case llvm::Triple::x86:
    if (T.isOSIAMCU())
      return "elf_iamcu";
    return "elf_i386";
  case llvm::Triple::aarch64:
    return "aarch64linux";
  case llvm::Triple::aarch64_be:
    return "aarch64linuxb";
  case llvm::Triple::arm:
  case llvm::Triple::thumb:
  case llvm::Triple::armeb:
  case llvm::Triple::thumbeb:
    return tools::arm::isARMBigEndian(T, Args) ? "armelfb_linux_eabi"
                                               : "armelf_linux_eabi";
  case llvm::Triple::m68k:
    return "m68kelf";
  case llvm::Triple::ppc:
    if (T.isOSLinux())
      return "elf32ppclinux";
    return "elf32ppc";
  case llvm::Triple::ppcle:
    if (T.isOSLinux())
      return "elf32lppclinux";
    return "elf32lppc";
  case llvm::Triple::ppc64:
    return "elf64ppc";
  case llvm::Triple::ppc64le:
    return "elf64lppc";
  case llvm::Triple::riscv32:
    return "elf32lriscv";
  case llvm::Triple::riscv64:
    return "elf64lriscv";
  case llvm::Triple::sparc:
  case llvm::Triple::sparcel:
    return "elf32_sparc";
  case llvm::Triple::sparcv9:
    return "elf64_sparc";
  case llvm::Triple::loongarch32:
    return "elf32loongarch";
  case llvm::Triple::loongarch64:
    return "elf64loongarch";
  case llvm::Triple::mips:
    return "elf32btsmip";
  case llvm::Triple::mipsel:
    return "elf32ltsmip";
  case llvm::Triple::mips64:
    if (tools::mips::hasMipsAbiArg(Args, "n32") ||
        T.getEnvironment() == llvm::Triple::GNUABIN32)
      return "elf32btsmipn32";
    return "elf64btsmip";
  case llvm::Triple::mips64el:
    if (tools::mips::hasMipsAbiArg(Args, "n32") ||
        T.getEnvironment() == llvm::Triple::GNUABIN32)
      return "elf32ltsmipn32";
    return "elf64ltsmip";
  case llvm::Triple::systemz:
    return "elf64_s390";
  case llvm::Triple::x86_64:
    if (T.isX32())
      return "elf32_x86_64";
    return "elf_x86_64";
  case llvm::Triple::ve:
    return "elf64ve";
  case llvm::Triple::csky:
    return "cskyelf_linux";
  default:
    return nullptr;
  }
}

static bool getStaticPIE(const ArgList &Args, const ToolChain &TC) {
  bool HasStaticPIE = Args.hasArg(options::OPT_static_pie);
  if (HasStaticPIE && Args.hasArg(options::OPT_no_pie)) {
    const Driver &D = TC.getDriver();
    const llvm::opt::OptTable &Opts = D.getOpts();
    StringRef StaticPIEName = Opts.getOptionName(options::OPT_static_pie);
    StringRef NoPIEName = Opts.getOptionName(options::OPT_nopie);
    D.Diag(diag::err_drv_cannot_mix_options) << StaticPIEName << NoPIEName;
  }
  return HasStaticPIE;
}

static bool getStatic(const ArgList &Args) {
  return Args.hasArg(options::OPT_static) &&
      !Args.hasArg(options::OPT_static_pie);
}

void tools::gnutools::StaticLibTool::ConstructJob(
    Compilation &C, const JobAction &JA, const InputInfo &Output,
    const InputInfoList &Inputs, const ArgList &Args,
    const char *LinkingOutput) const {
  const Driver &D = getToolChain().getDriver();

  // Silence warning for "clang -g foo.o -o foo"
  Args.ClaimAllArgs(options::OPT_g_Group);
  // and "clang -emit-llvm foo.o -o foo"
  Args.ClaimAllArgs(options::OPT_emit_llvm);
  // and for "clang -w foo.o -o foo". Other warning options are already
  // handled somewhere else.
  Args.ClaimAllArgs(options::OPT_w);
  // Silence warnings when linking C code with a C++ '-stdlib' argument.
  Args.ClaimAllArgs(options::OPT_stdlib_EQ);

  // ar tool command "llvm-ar <options> <output_file> <input_files>".
  ArgStringList CmdArgs;
  // Create and insert file members with a deterministic index.
  CmdArgs.push_back("rcsD");
  CmdArgs.push_back(Output.getFilename());

  for (const auto &II : Inputs) {
    if (II.isFilename()) {
       CmdArgs.push_back(II.getFilename());
    }
  }

  // Delete old output archive file if it already exists before generating a new
  // archive file.
  auto OutputFileName = Output.getFilename();
  if (Output.isFilename() && llvm::sys::fs::exists(OutputFileName)) {
    if (std::error_code EC = llvm::sys::fs::remove(OutputFileName)) {
      D.Diag(diag::err_drv_unable_to_remove_file) << EC.message();
      return;
    }
  }

  const char *Exec = Args.MakeArgString(getToolChain().GetStaticLibToolPath());
  C.addCommand(std::make_unique<Command>(JA, *this,
                                         ResponseFileSupport::AtFileCurCP(),
                                         Exec, CmdArgs, Inputs, Output));
}

void tools::gnutools::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                           const InputInfo &Output,
                                           const InputInfoList &Inputs,
                                           const ArgList &Args,
                                           const char *LinkingOutput) const {
  // FIXME: The Linker class constructor takes a ToolChain and not a
  // Generic_ELF, so the static_cast might return a reference to a invalid
  // instance (see PR45061). Ideally, the Linker constructor needs to take a
  // Generic_ELF instead.
  const auto &ToolChain = static_cast<const Generic_ELF &>(getToolChain());
  const Driver &D = ToolChain.getDriver();

  const llvm::Triple &Triple = getToolChain().getEffectiveTriple();

  const llvm::Triple::ArchType Arch = ToolChain.getArch();
  const bool isOHOSFamily = ToolChain.getTriple().isOHOSFamily();
  const bool isAndroid = ToolChain.getTriple().isAndroid();
  const bool IsIAMCU = ToolChain.getTriple().isOSIAMCU();
  const bool IsVE = ToolChain.getTriple().isVE();
  const bool IsStaticPIE = getStaticPIE(Args, ToolChain);
  const bool IsStatic = getStatic(Args);
  const bool HasCRTBeginEndFiles =
      ToolChain.getTriple().hasEnvironment() ||
      (ToolChain.getTriple().getVendor() != llvm::Triple::MipsTechnologies);

  ArgStringList CmdArgs;

  // Silence warning for "clang -g foo.o -o foo"
  Args.ClaimAllArgs(options::OPT_g_Group);
  // and "clang -emit-llvm foo.o -o foo"
  Args.ClaimAllArgs(options::OPT_emit_llvm);
  // and for "clang -w foo.o -o foo". Other warning options are already
  // handled somewhere else.
  Args.ClaimAllArgs(options::OPT_w);

  if (!D.SysRoot.empty())
    CmdArgs.push_back(Args.MakeArgString("--sysroot=" + D.SysRoot));

  if (Args.hasArg(options::OPT_s))
    CmdArgs.push_back("-s");

  if (Triple.isARM() || Triple.isThumb()) {
    bool IsBigEndian = arm::isARMBigEndian(Triple, Args);
    if (IsBigEndian)
      arm::appendBE8LinkFlag(Args, CmdArgs, Triple);
    CmdArgs.push_back(IsBigEndian ? "-EB" : "-EL");
  } else if (Triple.isAArch64()) {
    CmdArgs.push_back(Arch == llvm::Triple::aarch64_be ? "-EB" : "-EL");
  }

  // Most Android ARM64 targets should enable the linker fix for erratum
  // 843419. Only non-Cortex-A53 devices are allowed to skip this flag.
  if (Arch == llvm::Triple::aarch64 && (isAndroid || isOHOSFamily)) {
    std::string CPU = getCPUName(D, Args, Triple);
    if (CPU.empty() || CPU == "generic" || CPU == "cortex-a53")
      CmdArgs.push_back("--fix-cortex-a53-843419");
  }

  ToolChain.addExtraOpts(CmdArgs);

  CmdArgs.push_back("--eh-frame-hdr");

  if (const char *LDMOption = getLDMOption(ToolChain.getTriple(), Args)) {
    CmdArgs.push_back("-m");
    CmdArgs.push_back(LDMOption);
  } else {
    D.Diag(diag::err_target_unknown_triple) << Triple.str();
    return;
  }

  if (Triple.isRISCV()) {
    CmdArgs.push_back("-X");
    if (Args.hasArg(options::OPT_mno_relax))
      CmdArgs.push_back("--no-relax");
  }

  const bool IsShared = Args.hasArg(options::OPT_shared);
  if (IsShared)
    CmdArgs.push_back("-shared");
  bool IsPIE = false;
  if (IsStaticPIE) {
    CmdArgs.push_back("-static");
    CmdArgs.push_back("-pie");
    CmdArgs.push_back("--no-dynamic-linker");
    CmdArgs.push_back("-z");
    CmdArgs.push_back("text");
  } else if (IsStatic) {
    CmdArgs.push_back("-static");
  } else if (!Args.hasArg(options::OPT_r)) {
    if (Args.hasArg(options::OPT_rdynamic))
      CmdArgs.push_back("-export-dynamic");
    if (!IsShared) {
      IsPIE = Args.hasFlag(options::OPT_pie, options::OPT_no_pie,
                           ToolChain.isPIEDefault(Args));
      if (IsPIE)
        CmdArgs.push_back("-pie");
      CmdArgs.push_back("-dynamic-linker");
      CmdArgs.push_back(Args.MakeArgString(Twine(D.DyldPrefix) +
                                           ToolChain.getDynamicLinker(Args)));
    }
  }

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles,
                   options::OPT_r)) {
    if (!isAndroid && !IsIAMCU) {
      const char *crt1 = nullptr;
      if (!Args.hasArg(options::OPT_shared)) {
        if (Args.hasArg(options::OPT_pg))
          crt1 = "gcrt1.o";
        else if (IsPIE)
          crt1 = "Scrt1.o";
        else if (IsStaticPIE)
          crt1 = "rcrt1.o";
        else
          crt1 = "crt1.o";
      }
      if (crt1)
        CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath(crt1)));

      CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crti.o")));
    }

    if (IsVE) {
      CmdArgs.push_back("-z");
      CmdArgs.push_back("max-page-size=0x4000000");
    }

    if (IsIAMCU)
      CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crt0.o")));
    else if (HasCRTBeginEndFiles) {
      std::string P;
      if (ToolChain.GetRuntimeLibType(Args) == ToolChain::RLT_CompilerRT &&
          !isAndroid) {
        std::string crtbegin = ToolChain.getCompilerRT(Args, "crtbegin",
                                                       ToolChain::FT_Object);
        if (ToolChain.getVFS().exists(crtbegin))
          P = crtbegin;
      }
      if (P.empty()) {
        const char *crtbegin;
        if (Args.hasArg(options::OPT_shared))
          crtbegin = isAndroid ? "crtbegin_so.o" : "crtbeginS.o";
        else if (IsStatic)
          crtbegin = isAndroid ? "crtbegin_static.o" : "crtbeginT.o";
        else if (IsPIE || IsStaticPIE)
          crtbegin = isAndroid ? "crtbegin_dynamic.o" : "crtbeginS.o";
        else
          crtbegin = isAndroid ? "crtbegin_dynamic.o" : "crtbegin.o";
        P = ToolChain.GetFilePath(crtbegin);
      }
      CmdArgs.push_back(Args.MakeArgString(P));
    }

    // Add crtfastmath.o if available and fast math is enabled.
    ToolChain.addFastMathRuntimeIfAvailable(Args, CmdArgs);

    if (isAndroid && Args.hasFlag(options::OPT_fandroid_pad_segment,
                                  options::OPT_fno_android_pad_segment, false))
      CmdArgs.push_back(
          Args.MakeArgString(ToolChain.GetFilePath("crt_pad_segment.o")));
  }

  Args.addAllArgs(CmdArgs, {options::OPT_L, options::OPT_u});

  ToolChain.AddFilePathLibArgs(Args, CmdArgs);

  if (D.isUsingLTO()) {
    assert(!Inputs.empty() && "Must have at least one input.");
    // Find the first filename InputInfo object.
    auto Input = llvm::find_if(
        Inputs, [](const InputInfo &II) -> bool { return II.isFilename(); });
    if (Input == Inputs.end())
      // For a very rare case, all of the inputs to the linker are
      // InputArg. If that happens, just use the first InputInfo.
      Input = Inputs.begin();

    addLTOOptions(ToolChain, Args, CmdArgs, Output, *Input,
                  D.getLTOMode() == LTOK_Thin);
  }

  if (Args.hasArg(options::OPT_Z_Xlinker__no_demangle))
    CmdArgs.push_back("--no-demangle");

  bool NeedsSanitizerDeps = addSanitizerRuntimes(ToolChain, Args, CmdArgs);
  bool NeedsXRayDeps = addXRayRuntime(ToolChain, Args, CmdArgs);
  addLinkerCompressDebugSectionsOption(ToolChain, Args, CmdArgs);
  AddLinkerInputs(ToolChain, Inputs, Args, CmdArgs, JA);

  addHIPRuntimeLibArgs(ToolChain, C, Args, CmdArgs);

  // The profile runtime also needs access to system libraries.
  getToolChain().addProfileRTLibs(Args, CmdArgs);

  if (D.CCCIsCXX() &&
      !Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs,
                   options::OPT_r)) {
    if (ToolChain.ShouldLinkCXXStdlib(Args)) {
      bool OnlyLibstdcxxStatic = Args.hasArg(options::OPT_static_libstdcxx) &&
                                 !Args.hasArg(options::OPT_static);
      if (OnlyLibstdcxxStatic)
        CmdArgs.push_back("-Bstatic");
      ToolChain.AddCXXStdlibLibArgs(Args, CmdArgs);
      if (OnlyLibstdcxxStatic)
        CmdArgs.push_back("-Bdynamic");
    }
    CmdArgs.push_back("-lm");
  }

  // Silence warnings when linking C code with a C++ '-stdlib' argument.
  Args.ClaimAllArgs(options::OPT_stdlib_EQ);

  // Additional linker set-up and flags for Fortran. This is required in order
  // to generate executables. As Fortran runtime depends on the C runtime,
  // these dependencies need to be listed before the C runtime below (i.e.
  // AddRunTimeLibs).
  if (D.IsFlangMode()) {
    addFortranRuntimeLibraryPath(ToolChain, Args, CmdArgs);
    addFortranRuntimeLibs(ToolChain, Args, CmdArgs);
    CmdArgs.push_back("-lm");
  }

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_r)) {
    if (!Args.hasArg(options::OPT_nodefaultlibs)) {
      if (IsStatic || IsStaticPIE)
        CmdArgs.push_back("--start-group");

      if (NeedsSanitizerDeps)
        linkSanitizerRuntimeDeps(ToolChain, Args, CmdArgs);

      if (NeedsXRayDeps)
        linkXRayRuntimeDeps(ToolChain, Args, CmdArgs);

      bool WantPthread = Args.hasArg(options::OPT_pthread) ||
                         Args.hasArg(options::OPT_pthreads);

      // Use the static OpenMP runtime with -static-openmp
      bool StaticOpenMP = Args.hasArg(options::OPT_static_openmp) &&
                          !Args.hasArg(options::OPT_static);

      // FIXME: Only pass GompNeedsRT = true for platforms with libgomp that
      // require librt. Most modern Linux platforms do, but some may not.
      if (addOpenMPRuntime(C, CmdArgs, ToolChain, Args, StaticOpenMP,
                           JA.isHostOffloading(Action::OFK_OpenMP),
                           /* GompNeedsRT= */ true))
        // OpenMP runtimes implies pthreads when using the GNU toolchain.
        // FIXME: Does this really make sense for all GNU toolchains?
        WantPthread = true;

      AddRunTimeLibs(ToolChain, D, CmdArgs, Args);

      // LLVM support for atomics on 32-bit SPARC V8+ is incomplete, so
      // forcibly link with libatomic as a workaround.
      // TODO: Issue #41880 and D118021.
      if (getToolChain().getTriple().getArch() == llvm::Triple::sparc) {
        CmdArgs.push_back("--push-state");
        CmdArgs.push_back("--as-needed");
        CmdArgs.push_back("-latomic");
        CmdArgs.push_back("--pop-state");
      }

      // We don't need libpthread neither for bionic (Android) nor for musl,
      // (used by OHOS as runtime library).
      if (WantPthread && !isAndroid && !isOHOSFamily)
        CmdArgs.push_back("-lpthread");

      if (Args.hasArg(options::OPT_fsplit_stack))
        CmdArgs.push_back("--wrap=pthread_create");

      if (!Args.hasArg(options::OPT_nolibc))
        CmdArgs.push_back("-lc");

      // Add IAMCU specific libs, if needed.
      if (IsIAMCU)
        CmdArgs.push_back("-lgloss");

      if (IsStatic || IsStaticPIE)
        CmdArgs.push_back("--end-group");
      else
        AddRunTimeLibs(ToolChain, D, CmdArgs, Args);

      // Add IAMCU specific libs (outside the group), if needed.
      if (IsIAMCU) {
        CmdArgs.push_back("--as-needed");
        CmdArgs.push_back("-lsoftfp");
        CmdArgs.push_back("--no-as-needed");
      }
    }

    if (!Args.hasArg(options::OPT_nostartfiles) && !IsIAMCU) {
      if (HasCRTBeginEndFiles) {
        std::string P;
        if (ToolChain.GetRuntimeLibType(Args) == ToolChain::RLT_CompilerRT &&
            !isAndroid) {
          std::string crtend = ToolChain.getCompilerRT(Args, "crtend",
                                                       ToolChain::FT_Object);
          if (ToolChain.getVFS().exists(crtend))
            P = crtend;
        }
        if (P.empty()) {
          const char *crtend;
          if (Args.hasArg(options::OPT_shared))
            crtend = isAndroid ? "crtend_so.o" : "crtendS.o";
          else if (IsPIE || IsStaticPIE)
            crtend = isAndroid ? "crtend_android.o" : "crtendS.o";
          else
            crtend = isAndroid ? "crtend_android.o" : "crtend.o";
          P = ToolChain.GetFilePath(crtend);
        }
        CmdArgs.push_back(Args.MakeArgString(P));
      }
      if (!isAndroid)
        CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crtn.o")));
    }
  }

  Args.AddAllArgs(CmdArgs, options::OPT_T);

  const char *Exec = Args.MakeArgString(ToolChain.GetLinkerPath());
  C.addCommand(std::make_unique<Command>(JA, *this,
                                         ResponseFileSupport::AtFileCurCP(),
                                         Exec, CmdArgs, Inputs, Output));
}

void tools::gnutools::Assembler::ConstructJob(Compilation &C,
                                              const JobAction &JA,
                                              const InputInfo &Output,
                                              const InputInfoList &Inputs,
                                              const ArgList &Args,
                                              const char *LinkingOutput) const {
  const auto &D = getToolChain().getDriver();

  claimNoWarnArgs(Args);

  ArgStringList CmdArgs;

  llvm::Reloc::Model RelocationModel;
  unsigned PICLevel;
  bool IsPIE;
  const char *DefaultAssembler = "as";
  // Enforce GNU as on Solaris; the native assembler's input syntax isn't fully
  // compatible.
  if (getToolChain().getTriple().isOSSolaris())
    DefaultAssembler = "gas";
  std::tie(RelocationModel, PICLevel, IsPIE) =
      ParsePICArgs(getToolChain(), Args);

  if (const Arg *A = Args.getLastArg(options::OPT_gz, options::OPT_gz_EQ)) {
    if (A->getOption().getID() == options::OPT_gz) {
      CmdArgs.push_back("--compress-debug-sections");
    } else {
      StringRef Value = A->getValue();
      if (Value == "none" || Value == "zlib" || Value == "zstd") {
        CmdArgs.push_back(
            Args.MakeArgString("--compress-debug-sections=" + Twine(Value)));
      } else {
        D.Diag(diag::err_drv_unsupported_option_argument)
            << A->getSpelling() << Value;
      }
    }
  }

  switch (getToolChain().getArch()) {
  default:
    break;
  // Add --32/--64 to make sure we get the format we want.
  // This is incomplete
  case llvm::Triple::x86:
    CmdArgs.push_back("--32");
    break;
  case llvm::Triple::x86_64:
    if (getToolChain().getTriple().isX32())
      CmdArgs.push_back("--x32");
    else
      CmdArgs.push_back("--64");
    break;
  case llvm::Triple::ppc: {
    CmdArgs.push_back("-a32");
    CmdArgs.push_back("-mppc");
    CmdArgs.push_back("-mbig-endian");
    CmdArgs.push_back(ppc::getPPCAsmModeForCPU(
        getCPUName(D, Args, getToolChain().getTriple())));
    break;
  }
  case llvm::Triple::ppcle: {
    CmdArgs.push_back("-a32");
    CmdArgs.push_back("-mppc");
    CmdArgs.push_back("-mlittle-endian");
    CmdArgs.push_back(ppc::getPPCAsmModeForCPU(
        getCPUName(D, Args, getToolChain().getTriple())));
    break;
  }
  case llvm::Triple::ppc64: {
    CmdArgs.push_back("-a64");
    CmdArgs.push_back("-mppc64");
    CmdArgs.push_back("-mbig-endian");
    CmdArgs.push_back(ppc::getPPCAsmModeForCPU(
        getCPUName(D, Args, getToolChain().getTriple())));
    break;
  }
  case llvm::Triple::ppc64le: {
    CmdArgs.push_back("-a64");
    CmdArgs.push_back("-mppc64");
    CmdArgs.push_back("-mlittle-endian");
    CmdArgs.push_back(ppc::getPPCAsmModeForCPU(
        getCPUName(D, Args, getToolChain().getTriple())));
    break;
  }
  case llvm::Triple::riscv32:
  case llvm::Triple::riscv64: {
    StringRef ABIName = riscv::getRISCVABI(Args, getToolChain().getTriple());
    CmdArgs.push_back("-mabi");
    CmdArgs.push_back(ABIName.data());
    std::string MArchName =
        riscv::getRISCVArch(Args, getToolChain().getTriple());
    CmdArgs.push_back("-march");
    CmdArgs.push_back(Args.MakeArgString(MArchName));
    if (!Args.hasFlag(options::OPT_mrelax, options::OPT_mno_relax, true))
      Args.addOptOutFlag(CmdArgs, options::OPT_mrelax, options::OPT_mno_relax);
    break;
  }
  case llvm::Triple::sparc:
  case llvm::Triple::sparcel: {
    CmdArgs.push_back("-32");
    std::string CPU = getCPUName(D, Args, getToolChain().getTriple());
    CmdArgs.push_back(
        sparc::getSparcAsmModeForCPU(CPU, getToolChain().getTriple()));
    AddAssemblerKPIC(getToolChain(), Args, CmdArgs);
    break;
  }
  case llvm::Triple::sparcv9: {
    CmdArgs.push_back("-64");
    std::string CPU = getCPUName(D, Args, getToolChain().getTriple());
    CmdArgs.push_back(
        sparc::getSparcAsmModeForCPU(CPU, getToolChain().getTriple()));
    AddAssemblerKPIC(getToolChain(), Args, CmdArgs);
    break;
  }
  case llvm::Triple::arm:
  case llvm::Triple::armeb:
  case llvm::Triple::thumb:
  case llvm::Triple::thumbeb: {
    const llvm::Triple &Triple2 = getToolChain().getTriple();
    CmdArgs.push_back(arm::isARMBigEndian(Triple2, Args) ? "-EB" : "-EL");
    switch (Triple2.getSubArch()) {
    case llvm::Triple::ARMSubArch_v7:
      CmdArgs.push_back("-mfpu=neon");
      break;
    case llvm::Triple::ARMSubArch_v8:
      CmdArgs.push_back("-mfpu=crypto-neon-fp-armv8");
      break;
    default:
      break;
    }

    switch (arm::getARMFloatABI(getToolChain(), Args)) {
    case arm::FloatABI::Invalid: llvm_unreachable("must have an ABI!");
    case arm::FloatABI::Soft:
      CmdArgs.push_back(Args.MakeArgString("-mfloat-abi=soft"));
      break;
    case arm::FloatABI::SoftFP:
      CmdArgs.push_back(Args.MakeArgString("-mfloat-abi=softfp"));
      break;
    case arm::FloatABI::Hard:
      CmdArgs.push_back(Args.MakeArgString("-mfloat-abi=hard"));
      break;
    }

    Args.AddLastArg(CmdArgs, options::OPT_march_EQ);
    normalizeCPUNamesForAssembler(Args, CmdArgs);

    Args.AddLastArg(CmdArgs, options::OPT_mfpu_EQ);
    // The integrated assembler doesn't implement e_flags setting behavior for
    // -meabi=gnu (gcc -mabi={apcs-gnu,atpcs} passes -meabi=gnu to gas). For
    // compatibility we accept but warn.
    if (Arg *A = Args.getLastArgNoClaim(options::OPT_mabi_EQ))
      A->ignoreTargetSpecific();
    break;
  }
  case llvm::Triple::aarch64:
  case llvm::Triple::aarch64_be: {
    CmdArgs.push_back(
        getToolChain().getArch() == llvm::Triple::aarch64_be ? "-EB" : "-EL");
    Args.AddLastArg(CmdArgs, options::OPT_march_EQ);
    normalizeCPUNamesForAssembler(Args, CmdArgs);

    break;
  }
  // TODO: handle loongarch32.
  case llvm::Triple::loongarch64: {
    StringRef ABIName =
        loongarch::getLoongArchABI(D, Args, getToolChain().getTriple());
    CmdArgs.push_back(Args.MakeArgString("-mabi=" + ABIName));
    break;
  }
  case llvm::Triple::mips:
  case llvm::Triple::mipsel:
  case llvm::Triple::mips64:
  case llvm::Triple::mips64el: {
    StringRef CPUName;
    StringRef ABIName;
    mips::getMipsCPUAndABI(Args, getToolChain().getTriple(), CPUName, ABIName);
    ABIName = mips::getGnuCompatibleMipsABIName(ABIName);

    CmdArgs.push_back("-march");
    CmdArgs.push_back(CPUName.data());

    CmdArgs.push_back("-mabi");
    CmdArgs.push_back(ABIName.data());

    // -mno-shared should be emitted unless -fpic, -fpie, -fPIC, -fPIE,
    // or -mshared (not implemented) is in effect.
    if (RelocationModel == llvm::Reloc::Static)
      CmdArgs.push_back("-mno-shared");

    // LLVM doesn't support -mplt yet and acts as if it is always given.
    // However, -mplt has no effect with the N64 ABI.
    if (ABIName != "64" && !Args.hasArg(options::OPT_mno_abicalls))
      CmdArgs.push_back("-call_nonpic");

    if (getToolChain().getTriple().isLittleEndian())
      CmdArgs.push_back("-EL");
    else
      CmdArgs.push_back("-EB");

    if (Arg *A = Args.getLastArg(options::OPT_mnan_EQ)) {
      if (StringRef(A->getValue()) == "2008")
        CmdArgs.push_back(Args.MakeArgString("-mnan=2008"));
    }

    // Add the last -mfp32/-mfpxx/-mfp64 or -mfpxx if it is enabled by default.
    if (Arg *A = Args.getLastArg(options::OPT_mfp32, options::OPT_mfpxx,
                                 options::OPT_mfp64)) {
      A->claim();
      A->render(Args, CmdArgs);
    } else if (mips::shouldUseFPXX(
                   Args, getToolChain().getTriple(), CPUName, ABIName,
                   mips::getMipsFloatABI(getToolChain().getDriver(), Args,
                                         getToolChain().getTriple())))
      CmdArgs.push_back("-mfpxx");

    // Pass on -mmips16 or -mno-mips16. However, the assembler equivalent of
    // -mno-mips16 is actually -no-mips16.
    if (Arg *A =
            Args.getLastArg(options::OPT_mips16, options::OPT_mno_mips16)) {
      if (A->getOption().matches(options::OPT_mips16)) {
        A->claim();
        A->render(Args, CmdArgs);
      } else {
        A->claim();
        CmdArgs.push_back("-no-mips16");
      }
    }

    Args.AddLastArg(CmdArgs, options::OPT_mmicromips,
                    options::OPT_mno_micromips);
    Args.AddLastArg(CmdArgs, options::OPT_mdsp, options::OPT_mno_dsp);
    Args.AddLastArg(CmdArgs, options::OPT_mdspr2, options::OPT_mno_dspr2);

    if (Arg *A = Args.getLastArg(options::OPT_mmsa, options::OPT_mno_msa)) {
      // Do not use AddLastArg because not all versions of MIPS assembler
      // support -mmsa / -mno-msa options.
      if (A->getOption().matches(options::OPT_mmsa))
        CmdArgs.push_back(Args.MakeArgString("-mmsa"));
    }

    Args.AddLastArg(CmdArgs, options::OPT_mhard_float,
                    options::OPT_msoft_float);

    Args.AddLastArg(CmdArgs, options::OPT_mdouble_float,
                    options::OPT_msingle_float);

    Args.AddLastArg(CmdArgs, options::OPT_modd_spreg,
                    options::OPT_mno_odd_spreg);

    AddAssemblerKPIC(getToolChain(), Args, CmdArgs);
    break;
  }
  case llvm::Triple::systemz: {
    // Always pass an -march option, since our default of z10 is later
    // than the GNU assembler's default.
    std::string CPUName = systemz::getSystemZTargetCPU(Args);
    CmdArgs.push_back(Args.MakeArgString("-march=" + CPUName));
    break;
  }
  case llvm::Triple::ve:
    DefaultAssembler = "nas";
  }

  for (const Arg *A : Args.filtered(options::OPT_ffile_prefix_map_EQ,
                                    options::OPT_fdebug_prefix_map_EQ)) {
    StringRef Map = A->getValue();
    if (!Map.contains('='))
      D.Diag(diag::err_drv_invalid_argument_to_option)
          << Map << A->getOption().getName();
    else {
      CmdArgs.push_back(Args.MakeArgString("--debug-prefix-map"));
      CmdArgs.push_back(Args.MakeArgString(Map));
    }
    A->claim();
  }

  Args.AddAllArgs(CmdArgs, options::OPT_I);
  Args.AddAllArgValues(CmdArgs, options::OPT_Wa_COMMA, options::OPT_Xassembler);

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  for (const auto &II : Inputs)
    CmdArgs.push_back(II.getFilename());

  if (Arg *A = Args.getLastArg(options::OPT_g_Flag, options::OPT_gN_Group,
                               options::OPT_gdwarf_2, options::OPT_gdwarf_3,
                               options::OPT_gdwarf_4, options::OPT_gdwarf_5,
                               options::OPT_gdwarf))
    if (!A->getOption().matches(options::OPT_g0)) {
      Args.AddLastArg(CmdArgs, options::OPT_g_Flag);

      unsigned DwarfVersion = getDwarfVersion(getToolChain(), Args);
      CmdArgs.push_back(Args.MakeArgString("-gdwarf-" + Twine(DwarfVersion)));
    }

  const char *Exec =
      Args.MakeArgString(getToolChain().GetProgramPath(DefaultAssembler));
  C.addCommand(std::make_unique<Command>(JA, *this,
                                         ResponseFileSupport::AtFileCurCP(),
                                         Exec, CmdArgs, Inputs, Output));

  // Handle the debug info splitting at object creation time if we're
  // creating an object.
  // TODO: Currently only works on linux with newer objcopy.
  if (Args.hasArg(options::OPT_gsplit_dwarf) &&
      getToolChain().getTriple().isOSLinux())
    SplitDebugInfo(getToolChain(), C, *this, JA, Args, Output,
                   SplitDebugName(JA, Args, Inputs[0], Output));
}

namespace {
// Filter to remove Multilibs that don't exist as a suffix to Path
class FilterNonExistent {
  StringRef Base, File;
  llvm::vfs::FileSystem &VFS;

public:
  FilterNonExistent(StringRef Base, StringRef File, llvm::vfs::FileSystem &VFS)
      : Base(Base), File(File), VFS(VFS) {}
  bool operator()(const Multilib &M) {
    return !VFS.exists(Base + M.gccSuffix() + File);
  }
};
} // end anonymous namespace

static bool isSoftFloatABI(const ArgList &Args) {
  Arg *A = Args.getLastArg(options::OPT_msoft_float, options::OPT_mhard_float,
                           options::OPT_mfloat_abi_EQ);
  if (!A)
    return false;

  return A->getOption().matches(options::OPT_msoft_float) ||
         (A->getOption().matches(options::OPT_mfloat_abi_EQ) &&
          A->getValue() == StringRef("soft"));
}

static bool isArmOrThumbArch(llvm::Triple::ArchType Arch) {
  return Arch == llvm::Triple::arm || Arch == llvm::Triple::thumb;
}

static bool isMipsEL(llvm::Triple::ArchType Arch) {
  return Arch == llvm::Triple::mipsel || Arch == llvm::Triple::mips64el;
}

static bool isMips16(const ArgList &Args) {
  Arg *A = Args.getLastArg(options::OPT_mips16, options::OPT_mno_mips16);
  return A && A->getOption().matches(options::OPT_mips16);
}

static bool isMicroMips(const ArgList &Args) {
  Arg *A = Args.getLastArg(options::OPT_mmicromips, options::OPT_mno_micromips);
  return A && A->getOption().matches(options::OPT_mmicromips);
}

static bool isMSP430(llvm::Triple::ArchType Arch) {
  return Arch == llvm::Triple::msp430;
}

static bool findMipsCsMultilibs(const Multilib::flags_list &Flags,
                                FilterNonExistent &NonExistent,
                                DetectedMultilibs &Result) {
  // Check for Code Sourcery toolchain multilibs
  MultilibSet CSMipsMultilibs;
  {
    auto MArchMips16 = MultilibBuilder("/mips16").flag("-m32").flag("-mips16");

    auto MArchMicroMips =
        MultilibBuilder("/micromips").flag("-m32").flag("-mmicromips");

    auto MArchDefault = MultilibBuilder("")
                            .flag("-mips16", /*Disallow=*/true)
                            .flag("-mmicromips", /*Disallow=*/true);

    auto UCLibc = MultilibBuilder("/uclibc").flag("-muclibc");

    auto SoftFloat = MultilibBuilder("/soft-float").flag("-msoft-float");

    auto Nan2008 = MultilibBuilder("/nan2008").flag("-mnan=2008");

    auto DefaultFloat = MultilibBuilder("")
                            .flag("-msoft-float", /*Disallow=*/true)
                            .flag("-mnan=2008", /*Disallow=*/true);

    auto BigEndian =
        MultilibBuilder("").flag("-EB").flag("-EL", /*Disallow=*/true);

    auto LittleEndian =
        MultilibBuilder("/el").flag("-EL").flag("-EB", /*Disallow=*/true);

    // Note that this one's osSuffix is ""
    auto MAbi64 = MultilibBuilder("")
                      .gccSuffix("/64")
                      .includeSuffix("/64")
                      .flag("-mabi=n64")
                      .flag("-mabi=n32", /*Disallow=*/true)
                      .flag("-m32", /*Disallow=*/true);

    CSMipsMultilibs =
        MultilibSetBuilder()
            .Either(MArchMips16, MArchMicroMips, MArchDefault)
            .Maybe(UCLibc)
            .Either(SoftFloat, Nan2008, DefaultFloat)
            .FilterOut("/micromips/nan2008")
            .FilterOut("/mips16/nan2008")
            .Either(BigEndian, LittleEndian)
            .Maybe(MAbi64)
            .FilterOut("/mips16.*/64")
            .FilterOut("/micromips.*/64")
            .makeMultilibSet()
            .FilterOut(NonExistent)
            .setIncludeDirsCallback([](const Multilib &M) {
              std::vector<std::string> Dirs({"/include"});
              if (StringRef(M.includeSuffix()).starts_with("/uclibc"))
                Dirs.push_back(
                    "/../../../../mips-linux-gnu/libc/uclibc/usr/include");
              else
                Dirs.push_back("/../../../../mips-linux-gnu/libc/usr/include");
              return Dirs;
            });
  }

  MultilibSet DebianMipsMultilibs;
  {
    MultilibBuilder MAbiN32 =
        MultilibBuilder().gccSuffix("/n32").includeSuffix("/n32").flag(
            "-mabi=n32");

    MultilibBuilder M64 = MultilibBuilder()
                              .gccSuffix("/64")
                              .includeSuffix("/64")
                              .flag("-m64")
                              .flag("-m32", /*Disallow=*/true)
                              .flag("-mabi=n32", /*Disallow=*/true);

    MultilibBuilder M32 = MultilibBuilder()
                              .gccSuffix("/32")
                              .flag("-m64", /*Disallow=*/true)
                              .flag("-m32")
                              .flag("-mabi=n32", /*Disallow=*/true);

    DebianMipsMultilibs = MultilibSetBuilder()
                              .Either(M32, M64, MAbiN32)
                              .makeMultilibSet()
                              .FilterOut(NonExistent);
  }

  // Sort candidates. Toolchain that best meets the directories tree goes first.
  // Then select the first toolchains matches command line flags.
  MultilibSet *Candidates[] = {&CSMipsMultilibs, &DebianMipsMultilibs};
  if (CSMipsMultilibs.size() < DebianMipsMultilibs.size())
    std::iter_swap(Candidates, Candidates + 1);
  for (const MultilibSet *Candidate : Candidates) {
    if (Candidate->select(Flags, Result.SelectedMultilibs)) {
      if (Candidate == &DebianMipsMultilibs)
        Result.BiarchSibling = Multilib();
      Result.Multilibs = *Candidate;
      return true;
    }
  }
  return false;
}

static bool findMipsAndroidMultilibs(llvm::vfs::FileSystem &VFS, StringRef Path,
                                     const Multilib::flags_list &Flags,
                                     FilterNonExistent &NonExistent,
                                     DetectedMultilibs &Result) {

  MultilibSet AndroidMipsMultilibs =
      MultilibSetBuilder()
          .Maybe(MultilibBuilder("/mips-r2", {}, {}).flag("-march=mips32r2"))
          .Maybe(MultilibBuilder("/mips-r6", {}, {}).flag("-march=mips32r6"))
          .makeMultilibSet()
          .FilterOut(NonExistent);

  MultilibSet AndroidMipselMultilibs =
      MultilibSetBuilder()
          .Either(MultilibBuilder().flag("-march=mips32"),
                  MultilibBuilder("/mips-r2", "", "/mips-r2")
                      .flag("-march=mips32r2"),
                  MultilibBuilder("/mips-r6", "", "/mips-r6")
                      .flag("-march=mips32r6"))
          .makeMultilibSet()
          .FilterOut(NonExistent);

  MultilibSet AndroidMips64elMultilibs =
      MultilibSetBuilder()
          .Either(MultilibBuilder().flag("-march=mips64r6"),
                  MultilibBuilder("/32/mips-r1", "", "/mips-r1")
                      .flag("-march=mips32"),
                  MultilibBuilder("/32/mips-r2", "", "/mips-r2")
                      .flag("-march=mips32r2"),
                  MultilibBuilder("/32/mips-r6", "", "/mips-r6")
                      .flag("-march=mips32r6"))
          .makeMultilibSet()
          .FilterOut(NonExistent);

  MultilibSet *MS = &AndroidMipsMultilibs;
  if (VFS.exists(Path + "/mips-r6"))
    MS = &AndroidMipselMultilibs;
  else if (VFS.exists(Path + "/32"))
    MS = &AndroidMips64elMultilibs;
  if (MS->select(Flags, Result.SelectedMultilibs)) {
    Result.Multilibs = *MS;
    return true;
  }
  return false;
}

static bool findMipsMuslMultilibs(const Multilib::flags_list &Flags,
                                  FilterNonExistent &NonExistent,
                                  DetectedMultilibs &Result) {
  // Musl toolchain multilibs
  MultilibSet MuslMipsMultilibs;
  {
    auto MArchMipsR2 = MultilibBuilder("")
                           .osSuffix("/mips-r2-hard-musl")
                           .flag("-EB")
                           .flag("-EL", /*Disallow=*/true)
                           .flag("-march=mips32r2");

    auto MArchMipselR2 = MultilibBuilder("/mipsel-r2-hard-musl")
                             .flag("-EB", /*Disallow=*/true)
                             .flag("-EL")
                             .flag("-march=mips32r2");

    MuslMipsMultilibs = MultilibSetBuilder()
                            .Either(MArchMipsR2, MArchMipselR2)
                            .makeMultilibSet();

    // Specify the callback that computes the include directories.
    MuslMipsMultilibs.setIncludeDirsCallback([](const Multilib &M) {
      return std::vector<std::string>(
          {"/../sysroot" + M.osSuffix() + "/usr/include"});
    });
  }
  if (MuslMipsMultilibs.select(Flags, Result.SelectedMultilibs)) {
    Result.Multilibs = MuslMipsMultilibs;
    return true;
  }
  return false;
}

static bool findMipsMtiMultilibs(const Multilib::flags_list &Flags,
                                 FilterNonExistent &NonExistent,
                                 DetectedMultilibs &Result) {
  // CodeScape MTI toolchain v1.2 and early.
  MultilibSet MtiMipsMultilibsV1;
  {
    auto MArchMips32 = MultilibBuilder("/mips32")
                           .flag("-m32")
                           .flag("-m64", /*Disallow=*/true)
                           .flag("-mmicromips", /*Disallow=*/true)
                           .flag("-march=mips32");

    auto MArchMicroMips = MultilibBuilder("/micromips")
                              .flag("-m32")
                              .flag("-m64", /*Disallow=*/true)
                              .flag("-mmicromips");

    auto MArchMips64r2 = MultilibBuilder("/mips64r2")
                             .flag("-m32", /*Disallow=*/true)
                             .flag("-m64")
                             .flag("-march=mips64r2");

    auto MArchMips64 = MultilibBuilder("/mips64")
                           .flag("-m32", /*Disallow=*/true)
                           .flag("-m64")
                           .flag("-march=mips64r2", /*Disallow=*/true);

    auto MArchDefault = MultilibBuilder("")
                            .flag("-m32")
                            .flag("-m64", /*Disallow=*/true)
                            .flag("-mmicromips", /*Disallow=*/true)
                            .flag("-march=mips32r2");

    auto Mips16 = MultilibBuilder("/mips16").flag("-mips16");

    auto UCLibc = MultilibBuilder("/uclibc").flag("-muclibc");

    auto MAbi64 = MultilibBuilder("/64")
                      .flag("-mabi=n64")
                      .flag("-mabi=n32", /*Disallow=*/true)
                      .flag("-m32", /*Disallow=*/true);

    auto BigEndian =
        MultilibBuilder("").flag("-EB").flag("-EL", /*Disallow=*/true);

    auto LittleEndian =
        MultilibBuilder("/el").flag("-EL").flag("-EB", /*Disallow=*/true);

    auto SoftFloat = MultilibBuilder("/sof").flag("-msoft-float");

    auto Nan2008 = MultilibBuilder("/nan2008").flag("-mnan=2008");

    MtiMipsMultilibsV1 =
        MultilibSetBuilder()
            .Either(MArchMips32, MArchMicroMips, MArchMips64r2, MArchMips64,
                    MArchDefault)
            .Maybe(UCLibc)
            .Maybe(Mips16)
            .FilterOut("/mips64/mips16")
            .FilterOut("/mips64r2/mips16")
            .FilterOut("/micromips/mips16")
            .Maybe(MAbi64)
            .FilterOut("/micromips/64")
            .FilterOut("/mips32/64")
            .FilterOut("^/64")
            .FilterOut("/mips16/64")
            .Either(BigEndian, LittleEndian)
            .Maybe(SoftFloat)
            .Maybe(Nan2008)
            .FilterOut(".*sof/nan2008")
            .makeMultilibSet()
            .FilterOut(NonExistent)
            .setIncludeDirsCallback([](const Multilib &M) {
              std::vector<std::string> Dirs({"/include"});
              if (StringRef(M.includeSuffix()).starts_with("/uclibc"))
                Dirs.push_back("/../../../../sysroot/uclibc/usr/include");
              else
                Dirs.push_back("/../../../../sysroot/usr/include");
              return Dirs;
            });
  }

  // CodeScape IMG toolchain starting from v1.3.
  MultilibSet MtiMipsMultilibsV2;
  {
    auto BeHard = MultilibBuilder("/mips-r2-hard")
                      .flag("-EB")
                      .flag("-msoft-float", /*Disallow=*/true)
                      .flag("-mnan=2008", /*Disallow=*/true)
                      .flag("-muclibc", /*Disallow=*/true);
    auto BeSoft = MultilibBuilder("/mips-r2-soft")
                      .flag("-EB")
                      .flag("-msoft-float")
                      .flag("-mnan=2008", /*Disallow=*/true);
    auto ElHard = MultilibBuilder("/mipsel-r2-hard")
                      .flag("-EL")
                      .flag("-msoft-float", /*Disallow=*/true)
                      .flag("-mnan=2008", /*Disallow=*/true)
                      .flag("-muclibc", /*Disallow=*/true);
    auto ElSoft = MultilibBuilder("/mipsel-r2-soft")
                      .flag("-EL")
                      .flag("-msoft-float")
                      .flag("-mnan=2008", /*Disallow=*/true)
                      .flag("-mmicromips", /*Disallow=*/true);
    auto BeHardNan = MultilibBuilder("/mips-r2-hard-nan2008")
                         .flag("-EB")
                         .flag("-msoft-float", /*Disallow=*/true)
                         .flag("-mnan=2008")
                         .flag("-muclibc", /*Disallow=*/true);
    auto ElHardNan = MultilibBuilder("/mipsel-r2-hard-nan2008")
                         .flag("-EL")
                         .flag("-msoft-float", /*Disallow=*/true)
                         .flag("-mnan=2008")
                         .flag("-muclibc", /*Disallow=*/true)
                         .flag("-mmicromips", /*Disallow=*/true);
    auto BeHardNanUclibc = MultilibBuilder("/mips-r2-hard-nan2008-uclibc")
                               .flag("-EB")
                               .flag("-msoft-float", /*Disallow=*/true)
                               .flag("-mnan=2008")
                               .flag("-muclibc");
    auto ElHardNanUclibc = MultilibBuilder("/mipsel-r2-hard-nan2008-uclibc")
                               .flag("-EL")
                               .flag("-msoft-float", /*Disallow=*/true)
                               .flag("-mnan=2008")
                               .flag("-muclibc");
    auto BeHardUclibc = MultilibBuilder("/mips-r2-hard-uclibc")
                            .flag("-EB")
                            .flag("-msoft-float", /*Disallow=*/true)
                            .flag("-mnan=2008", /*Disallow=*/true)
                            .flag("-muclibc");
    auto ElHardUclibc = MultilibBuilder("/mipsel-r2-hard-uclibc")
                            .flag("-EL")
                            .flag("-msoft-float", /*Disallow=*/true)
                            .flag("-mnan=2008", /*Disallow=*/true)
                            .flag("-muclibc");
    auto ElMicroHardNan = MultilibBuilder("/micromipsel-r2-hard-nan2008")
                              .flag("-EL")
                              .flag("-msoft-float", /*Disallow=*/true)
                              .flag("-mnan=2008")
                              .flag("-mmicromips");
    auto ElMicroSoft = MultilibBuilder("/micromipsel-r2-soft")
                           .flag("-EL")
                           .flag("-msoft-float")
                           .flag("-mnan=2008", /*Disallow=*/true)
                           .flag("-mmicromips");

    auto O32 = MultilibBuilder("/lib")
                   .osSuffix("")
                   .flag("-mabi=n32", /*Disallow=*/true)
                   .flag("-mabi=n64", /*Disallow=*/true);
    auto N32 = MultilibBuilder("/lib32")
                   .osSuffix("")
                   .flag("-mabi=n32")
                   .flag("-mabi=n64", /*Disallow=*/true);
    auto N64 = MultilibBuilder("/lib64")
                   .osSuffix("")
                   .flag("-mabi=n32", /*Disallow=*/true)
                   .flag("-mabi=n64");

    MtiMipsMultilibsV2 =
        MultilibSetBuilder()
            .Either({BeHard, BeSoft, ElHard, ElSoft, BeHardNan, ElHardNan,
                     BeHardNanUclibc, ElHardNanUclibc, BeHardUclibc,
                     ElHardUclibc, ElMicroHardNan, ElMicroSoft})
            .Either(O32, N32, N64)
            .makeMultilibSet()
            .FilterOut(NonExistent)
            .setIncludeDirsCallback([](const Multilib &M) {
              return std::vector<std::string>({"/../../../../sysroot" +
                                               M.includeSuffix() +
                                               "/../usr/include"});
            })
            .setFilePathsCallback([](const Multilib &M) {
              return std::vector<std::string>(
                  {"/../../../../mips-mti-linux-gnu/lib" + M.gccSuffix()});
            });
  }
  for (auto *Candidate : {&MtiMipsMultilibsV1, &MtiMipsMultilibsV2}) {
    if (Candidate->select(Flags, Result.SelectedMultilibs)) {
      Result.Multilibs = *Candidate;
      return true;
    }
  }
  return false;
}

static bool findMipsImgMultilibs(const Multilib::flags_list &Flags,
                                 FilterNonExistent &NonExistent,
                                 DetectedMultilibs &Result) {
  // CodeScape IMG toolchain v1.2 and early.
  MultilibSet ImgMultilibsV1;
  {
    auto Mips64r6 = MultilibBuilder("/mips64r6")
                        .flag("-m64")
                        .flag("-m32", /*Disallow=*/true);

    auto LittleEndian =
        MultilibBuilder("/el").flag("-EL").flag("-EB", /*Disallow=*/true);

    auto MAbi64 = MultilibBuilder("/64")
                      .flag("-mabi=n64")
                      .flag("-mabi=n32", /*Disallow=*/true)
                      .flag("-m32", /*Disallow=*/true);

    ImgMultilibsV1 =
        MultilibSetBuilder()
            .Maybe(Mips64r6)
            .Maybe(MAbi64)
            .Maybe(LittleEndian)
            .makeMultilibSet()
            .FilterOut(NonExistent)
            .setIncludeDirsCallback([](const Multilib &M) {
              return std::vector<std::string>(
                  {"/include", "/../../../../sysroot/usr/include"});
            });
  }

  // CodeScape IMG toolchain starting from v1.3.
  MultilibSet ImgMultilibsV2;
  {
    auto BeHard = MultilibBuilder("/mips-r6-hard")
                      .flag("-EB")
                      .flag("-msoft-float", /*Disallow=*/true)
                      .flag("-mmicromips", /*Disallow=*/true);
    auto BeSoft = MultilibBuilder("/mips-r6-soft")
                      .flag("-EB")
                      .flag("-msoft-float")
                      .flag("-mmicromips", /*Disallow=*/true);
    auto ElHard = MultilibBuilder("/mipsel-r6-hard")
                      .flag("-EL")
                      .flag("-msoft-float", /*Disallow=*/true)
                      .flag("-mmicromips", /*Disallow=*/true);
    auto ElSoft = MultilibBuilder("/mipsel-r6-soft")
                      .flag("-EL")
                      .flag("-msoft-float")
                      .flag("-mmicromips", /*Disallow=*/true);
    auto BeMicroHard = MultilibBuilder("/micromips-r6-hard")
                           .flag("-EB")
                           .flag("-msoft-float", /*Disallow=*/true)
                           .flag("-mmicromips");
    auto BeMicroSoft = MultilibBuilder("/micromips-r6-soft")
                           .flag("-EB")
                           .flag("-msoft-float")
                           .flag("-mmicromips");
    auto ElMicroHard = MultilibBuilder("/micromipsel-r6-hard")
                           .flag("-EL")
                           .flag("-msoft-float", /*Disallow=*/true)
                           .flag("-mmicromips");
    auto ElMicroSoft = MultilibBuilder("/micromipsel-r6-soft")
                           .flag("-EL")
                           .flag("-msoft-float")
                           .flag("-mmicromips");

    auto O32 = MultilibBuilder("/lib")
                   .osSuffix("")
                   .flag("-mabi=n32", /*Disallow=*/true)
                   .flag("-mabi=n64", /*Disallow=*/true);
    auto N32 = MultilibBuilder("/lib32")
                   .osSuffix("")
                   .flag("-mabi=n32")
                   .flag("-mabi=n64", /*Disallow=*/true);
    auto N64 = MultilibBuilder("/lib64")
                   .osSuffix("")
                   .flag("-mabi=n32", /*Disallow=*/true)
                   .flag("-mabi=n64");

    ImgMultilibsV2 =
        MultilibSetBuilder()
            .Either({BeHard, BeSoft, ElHard, ElSoft, BeMicroHard, BeMicroSoft,
                     ElMicroHard, ElMicroSoft})
            .Either(O32, N32, N64)
            .makeMultilibSet()
            .FilterOut(NonExistent)
            .setIncludeDirsCallback([](const Multilib &M) {
              return std::vector<std::string>({"/../../../../sysroot" +
                                               M.includeSuffix() +
                                               "/../usr/include"});
            })
            .setFilePathsCallback([](const Multilib &M) {
              return std::vector<std::string>(
                  {"/../../../../mips-img-linux-gnu/lib" + M.gccSuffix()});
            });
  }
  for (auto *Candidate : {&ImgMultilibsV1, &ImgMultilibsV2}) {
    if (Candidate->select(Flags, Result.SelectedMultilibs)) {
      Result.Multilibs = *Candidate;
      return true;
    }
  }
  return false;
}

bool clang::driver::findMIPSMultilibs(const Driver &D,
                                      const llvm::Triple &TargetTriple,
                                      StringRef Path, const ArgList &Args,
                                      DetectedMultilibs &Result) {
  FilterNonExistent NonExistent(Path, "/crtbegin.o", D.getVFS());

  StringRef CPUName;
  StringRef ABIName;
  tools::mips::getMipsCPUAndABI(Args, TargetTriple, CPUName, ABIName);

  llvm::Triple::ArchType TargetArch = TargetTriple.getArch();

  Multilib::flags_list Flags;
  addMultilibFlag(TargetTriple.isMIPS32(), "-m32", Flags);
  addMultilibFlag(TargetTriple.isMIPS64(), "-m64", Flags);
  addMultilibFlag(isMips16(Args), "-mips16", Flags);
  addMultilibFlag(CPUName == "mips32", "-march=mips32", Flags);
  addMultilibFlag(CPUName == "mips32r2" || CPUName == "mips32r3" ||
                      CPUName == "mips32r5" || CPUName == "p5600",
                  "-march=mips32r2", Flags);
  addMultilibFlag(CPUName == "mips32r6", "-march=mips32r6", Flags);
  addMultilibFlag(CPUName == "mips64", "-march=mips64", Flags);
  addMultilibFlag(CPUName == "mips64r2" || CPUName == "mips64r3" ||
                      CPUName == "mips64r5" || CPUName == "octeon" ||
                      CPUName == "octeon+",
                  "-march=mips64r2", Flags);
  addMultilibFlag(CPUName == "mips64r6", "-march=mips64r6", Flags);
  addMultilibFlag(isMicroMips(Args), "-mmicromips", Flags);
  addMultilibFlag(tools::mips::isUCLibc(Args), "-muclibc", Flags);
  addMultilibFlag(tools::mips::isNaN2008(D, Args, TargetTriple), "-mnan=2008",
                  Flags);
  addMultilibFlag(ABIName == "n32", "-mabi=n32", Flags);
  addMultilibFlag(ABIName == "n64", "-mabi=n64", Flags);
  addMultilibFlag(isSoftFloatABI(Args), "-msoft-float", Flags);
  addMultilibFlag(!isSoftFloatABI(Args), "-mhard-float", Flags);
  addMultilibFlag(isMipsEL(TargetArch), "-EL", Flags);
  addMultilibFlag(!isMipsEL(TargetArch), "-EB", Flags);

  if (TargetTriple.isAndroid())
    return findMipsAndroidMultilibs(D.getVFS(), Path, Flags, NonExistent,
                                    Result);

  if (TargetTriple.getVendor() == llvm::Triple::MipsTechnologies &&
      TargetTriple.getOS() == llvm::Triple::Linux &&
      TargetTriple.getEnvironment() == llvm::Triple::UnknownEnvironment)
    return findMipsMuslMultilibs(Flags, NonExistent, Result);

  if (TargetTriple.getVendor() == llvm::Triple::MipsTechnologies &&
      TargetTriple.getOS() == llvm::Triple::Linux &&
      TargetTriple.isGNUEnvironment())
    return findMipsMtiMultilibs(Flags, NonExistent, Result);

  if (TargetTriple.getVendor() == llvm::Triple::ImaginationTechnologies &&
      TargetTriple.getOS() == llvm::Triple::Linux &&
      TargetTriple.isGNUEnvironment())
    return findMipsImgMultilibs(Flags, NonExistent, Result);

  if (findMipsCsMultilibs(Flags, NonExistent, Result))
    return true;

  // Fallback to the regular toolchain-tree structure.
  Multilib Default;
  Result.Multilibs.push_back(Default);
  Result.Multilibs.FilterOut(NonExistent);

  if (Result.Multilibs.select(Flags, Result.SelectedMultilibs)) {
    Result.BiarchSibling = Multilib();
    return true;
  }

  return false;
}

static void findAndroidArmMultilibs(const Driver &D,
                                    const llvm::Triple &TargetTriple,
                                    StringRef Path, const ArgList &Args,
                                    DetectedMultilibs &Result) {
  // Find multilibs with subdirectories like armv7-a, thumb, armv7-a/thumb.
  FilterNonExistent NonExistent(Path, "/crtbegin.o", D.getVFS());
  MultilibBuilder ArmV7Multilib = MultilibBuilder("/armv7-a")
                                      .flag("-march=armv7-a")
                                      .flag("-mthumb", /*Disallow=*/true);
  MultilibBuilder ThumbMultilib = MultilibBuilder("/thumb")
                                      .flag("-march=armv7-a", /*Disallow=*/true)
                                      .flag("-mthumb");
  MultilibBuilder ArmV7ThumbMultilib =
      MultilibBuilder("/armv7-a/thumb").flag("-march=armv7-a").flag("-mthumb");
  MultilibBuilder DefaultMultilib =
      MultilibBuilder("")
          .flag("-march=armv7-a", /*Disallow=*/true)
          .flag("-mthumb", /*Disallow=*/true);
  MultilibSet AndroidArmMultilibs =
      MultilibSetBuilder()
          .Either(ThumbMultilib, ArmV7Multilib, ArmV7ThumbMultilib,
                  DefaultMultilib)
          .makeMultilibSet()
          .FilterOut(NonExistent);

  Multilib::flags_list Flags;
  llvm::StringRef Arch = Args.getLastArgValue(options::OPT_march_EQ);
  bool IsArmArch = TargetTriple.getArch() == llvm::Triple::arm;
  bool IsThumbArch = TargetTriple.getArch() == llvm::Triple::thumb;
  bool IsV7SubArch = TargetTriple.getSubArch() == llvm::Triple::ARMSubArch_v7;
  bool IsThumbMode = IsThumbArch ||
      Args.hasFlag(options::OPT_mthumb, options::OPT_mno_thumb, false) ||
      (IsArmArch && llvm::ARM::parseArchISA(Arch) == llvm::ARM::ISAKind::THUMB);
  bool IsArmV7Mode = (IsArmArch || IsThumbArch) &&
      (llvm::ARM::parseArchVersion(Arch) == 7 ||
       (IsArmArch && Arch == "" && IsV7SubArch));
  addMultilibFlag(IsArmV7Mode, "-march=armv7-a", Flags);
  addMultilibFlag(IsThumbMode, "-mthumb", Flags);

  if (AndroidArmMultilibs.select(Flags, Result.SelectedMultilibs))
    Result.Multilibs = AndroidArmMultilibs;
}

static bool findMSP430Multilibs(const Driver &D,
                                const llvm::Triple &TargetTriple,
                                StringRef Path, const ArgList &Args,
                                DetectedMultilibs &Result) {
  FilterNonExistent NonExistent(Path, "/crtbegin.o", D.getVFS());
  MultilibBuilder WithoutExceptions =
      MultilibBuilder("/430").flag("-exceptions", /*Disallow=*/true);
  MultilibBuilder WithExceptions =
      MultilibBuilder("/430/exceptions").flag("-exceptions");

  // FIXME: when clang starts to support msp430x ISA additional logic
  // to select between multilib must be implemented
  // MultilibBuilder MSP430xMultilib = MultilibBuilder("/large");

  Result.Multilibs.push_back(WithoutExceptions.makeMultilib());
  Result.Multilibs.push_back(WithExceptions.makeMultilib());
  Result.Multilibs.FilterOut(NonExistent);

  Multilib::flags_list Flags;
  addMultilibFlag(Args.hasFlag(options::OPT_fexceptions,
                               options::OPT_fno_exceptions, false),
                  "-exceptions", Flags);
  if (Result.Multilibs.select(Flags, Result.SelectedMultilibs))
    return true;

  return false;
}

static void findCSKYMultilibs(const Driver &D, const llvm::Triple &TargetTriple,
                              StringRef Path, const ArgList &Args,
                              DetectedMultilibs &Result) {
  FilterNonExistent NonExistent(Path, "/crtbegin.o", D.getVFS());

  tools::csky::FloatABI TheFloatABI = tools::csky::getCSKYFloatABI(D, Args);
  std::optional<llvm::StringRef> Res =
      tools::csky::getCSKYArchName(D, Args, TargetTriple);

  if (!Res)
    return;
  auto ARCHName = *Res;

  Multilib::flags_list Flags;
  addMultilibFlag(TheFloatABI == tools::csky::FloatABI::Hard, "-hard-fp",
                  Flags);
  addMultilibFlag(TheFloatABI == tools::csky::FloatABI::SoftFP, "-soft-fp",
                  Flags);
  addMultilibFlag(TheFloatABI == tools::csky::FloatABI::Soft, "-soft", Flags);
  addMultilibFlag(ARCHName == "ck801", "-march=ck801", Flags);
  addMultilibFlag(ARCHName == "ck802", "-march=ck802", Flags);
  addMultilibFlag(ARCHName == "ck803", "-march=ck803", Flags);
  addMultilibFlag(ARCHName == "ck804", "-march=ck804", Flags);
  addMultilibFlag(ARCHName == "ck805", "-march=ck805", Flags);
  addMultilibFlag(ARCHName == "ck807", "-march=ck807", Flags);
  addMultilibFlag(ARCHName == "ck810", "-march=ck810", Flags);
  addMultilibFlag(ARCHName == "ck810v", "-march=ck810v", Flags);
  addMultilibFlag(ARCHName == "ck860", "-march=ck860", Flags);
  addMultilibFlag(ARCHName == "ck860v", "-march=ck860v", Flags);

  bool isBigEndian = false;
  if (Arg *A = Args.getLastArg(options::OPT_mlittle_endian,
                               options::OPT_mbig_endian))
    isBigEndian = !A->getOption().matches(options::OPT_mlittle_endian);
  addMultilibFlag(isBigEndian, "-EB", Flags);

  auto HardFloat = MultilibBuilder("/hard-fp").flag("-hard-fp");
  auto SoftFpFloat = MultilibBuilder("/soft-fp").flag("-soft-fp");
  auto SoftFloat = MultilibBuilder("").flag("-soft");
  auto Arch801 = MultilibBuilder("/ck801").flag("-march=ck801");
  auto Arch802 = MultilibBuilder("/ck802").flag("-march=ck802");
  auto Arch803 = MultilibBuilder("/ck803").flag("-march=ck803");
  // CK804 use the same library as CK803
  auto Arch804 = MultilibBuilder("/ck803").flag("-march=ck804");
  auto Arch805 = MultilibBuilder("/ck805").flag("-march=ck805");
  auto Arch807 = MultilibBuilder("/ck807").flag("-march=ck807");
  auto Arch810 = MultilibBuilder("").flag("-march=ck810");
  auto Arch810v = MultilibBuilder("/ck810v").flag("-march=ck810v");
  auto Arch860 = MultilibBuilder("/ck860").flag("-march=ck860");
  auto Arch860v = MultilibBuilder("/ck860v").flag("-march=ck860v");
  auto BigEndian = MultilibBuilder("/big").flag("-EB");

  MultilibSet CSKYMultilibs =
      MultilibSetBuilder()
          .Maybe(BigEndian)
          .Either({Arch801, Arch802, Arch803, Arch804, Arch805, Arch807,
                   Arch810, Arch810v, Arch860, Arch860v})
          .Either(HardFloat, SoftFpFloat, SoftFloat)
          .makeMultilibSet()
          .FilterOut(NonExistent);

  if (CSKYMultilibs.select(Flags, Result.SelectedMultilibs))
    Result.Multilibs = CSKYMultilibs;
}

/// Extend the multi-lib re-use selection mechanism for RISC-V.
/// This function will try to re-use multi-lib if they are compatible.
/// Definition of compatible:
///   - ABI must be the same.
///   - multi-lib is a subset of current arch, e.g. multi-lib=march=rv32im
///     is a subset of march=rv32imc.
///   - march that contains atomic extension can't reuse multi-lib that
///     doesn't have atomic, vice versa. e.g. multi-lib=march=rv32im and
///     march=rv32ima are not compatible, because software and hardware
///     atomic operation can't work together correctly.
static bool
selectRISCVMultilib(const MultilibSet &RISCVMultilibSet, StringRef Arch,
                    const Multilib::flags_list &Flags,
                    llvm::SmallVectorImpl<Multilib> &SelectedMultilibs) {
  // Try to find the perfect matching multi-lib first.
  if (RISCVMultilibSet.select(Flags, SelectedMultilibs))
    return true;

  Multilib::flags_list NewFlags;
  std::vector<MultilibBuilder> NewMultilibs;

  llvm::Expected<std::unique_ptr<llvm::RISCVISAInfo>> ParseResult =
      llvm::RISCVISAInfo::parseArchString(
          Arch, /*EnableExperimentalExtension=*/true,
          /*ExperimentalExtensionVersionCheck=*/false);
  // Ignore any error here, we assume it will be handled in another place.
  if (llvm::errorToBool(ParseResult.takeError()))
    return false;

  auto &ISAInfo = *ParseResult;

  addMultilibFlag(ISAInfo->getXLen() == 32, "-m32", NewFlags);
  addMultilibFlag(ISAInfo->getXLen() == 64, "-m64", NewFlags);

  // Collect all flags except march=*
  for (StringRef Flag : Flags) {
    if (Flag.starts_with("!march=") || Flag.starts_with("-march="))
      continue;

    NewFlags.push_back(Flag.str());
  }

  llvm::StringSet<> AllArchExts;
  // Reconstruct multi-lib list, and break march option into separated
  // extension. e.g. march=rv32im -> +i +m
  for (const auto &M : RISCVMultilibSet) {
    bool Skip = false;

    MultilibBuilder NewMultilib =
        MultilibBuilder(M.gccSuffix(), M.osSuffix(), M.includeSuffix());
    for (StringRef Flag : M.flags()) {
      // Add back all flags except -march.
      if (!Flag.consume_front("-march=")) {
        NewMultilib.flag(Flag);
        continue;
      }

      // Break down -march into individual extension.
      llvm::Expected<std::unique_ptr<llvm::RISCVISAInfo>> MLConfigParseResult =
          llvm::RISCVISAInfo::parseArchString(
              Flag, /*EnableExperimentalExtension=*/true,
              /*ExperimentalExtensionVersionCheck=*/false);
      // Ignore any error here, we assume it will handled in another place.
      if (llvm::errorToBool(MLConfigParseResult.takeError())) {
        // We might get a parsing error if rv32e in the list, we could just skip
        // that and process the rest of multi-lib configs.
        Skip = true;
        continue;
      }
      auto &MLConfigISAInfo = *MLConfigParseResult;

      for (auto &MLConfigArchExt : MLConfigISAInfo->getExtensions()) {
        auto ExtName = MLConfigArchExt.first;
        NewMultilib.flag(Twine("-", ExtName).str());

        if (AllArchExts.insert(ExtName).second) {
          addMultilibFlag(ISAInfo->hasExtension(ExtName),
                          Twine("-", ExtName).str(), NewFlags);
        }
      }

      // Check the XLEN explicitly.
      if (MLConfigISAInfo->getXLen() == 32) {
        NewMultilib.flag("-m32");
        NewMultilib.flag("-m64", /*Disallow*/ true);
      } else {
        NewMultilib.flag("-m32", /*Disallow*/ true);
        NewMultilib.flag("-m64");
      }

      // Atomic extension must be explicitly checked, soft and hard atomic
      // operation never co-work correctly.
      if (!MLConfigISAInfo->hasExtension("a"))
        NewMultilib.flag("-a", /*Disallow*/ true);
    }

    if (Skip)
      continue;

    NewMultilibs.emplace_back(NewMultilib);
  }

  // Build an internal used only multi-lib list, used for checking any
  // compatible multi-lib.
  MultilibSet NewRISCVMultilibs =
      MultilibSetBuilder().Either(NewMultilibs).makeMultilibSet();

  if (NewRISCVMultilibs.select(NewFlags, SelectedMultilibs))
    for (const Multilib &NewSelectedM : SelectedMultilibs)
      for (const auto &M : RISCVMultilibSet)
        // Look up the corresponding multi-lib entry in original multi-lib set.
        if (M.gccSuffix() == NewSelectedM.gccSuffix())
          return true;

  return false;
}

static void findRISCVBareMetalMultilibs(const Driver &D,
                                        const llvm::Triple &TargetTriple,
                                        StringRef Path, const ArgList &Args,
                                        DetectedMultilibs &Result) {
  FilterNonExistent NonExistent(Path, "/crtbegin.o", D.getVFS());
  struct RiscvMultilib {
    StringRef march;
    StringRef mabi;
  };
  // currently only support the set of multilibs like riscv-gnu-toolchain does.
  // TODO: support MULTILIB_REUSE
  constexpr RiscvMultilib RISCVMultilibSet[] = {
      {"rv32i", "ilp32"},     {"rv32im", "ilp32"},     {"rv32iac", "ilp32"},
      {"rv32imac", "ilp32"},  {"rv32imafc", "ilp32f"}, {"rv64imac", "lp64"},
      {"rv64imafdc", "lp64d"}};

  std::vector<MultilibBuilder> Ms;
  for (auto Element : RISCVMultilibSet) {
    // multilib path rule is ${march}/${mabi}
    Ms.emplace_back(
        MultilibBuilder(
            (Twine(Element.march) + "/" + Twine(Element.mabi)).str())
            .flag(Twine("-march=", Element.march).str())
            .flag(Twine("-mabi=", Element.mabi).str()));
  }
  MultilibSet RISCVMultilibs =
      MultilibSetBuilder()
          .Either(Ms)
          .makeMultilibSet()
          .FilterOut(NonExistent)
          .setFilePathsCallback([](const Multilib &M) {
            return std::vector<std::string>(
                {M.gccSuffix(),
                 "/../../../../riscv64-unknown-elf/lib" + M.gccSuffix(),
                 "/../../../../riscv32-unknown-elf/lib" + M.gccSuffix()});
          });

  Multilib::flags_list Flags;
  llvm::StringSet<> Added_ABIs;
  StringRef ABIName = tools::riscv::getRISCVABI(Args, TargetTriple);
  std::string MArch = tools::riscv::getRISCVArch(Args, TargetTriple);
  for (auto Element : RISCVMultilibSet) {
    addMultilibFlag(MArch == Element.march,
                    Twine("-march=", Element.march).str().c_str(), Flags);
    if (!Added_ABIs.count(Element.mabi)) {
      Added_ABIs.insert(Element.mabi);
      addMultilibFlag(ABIName == Element.mabi,
                      Twine("-mabi=", Element.mabi).str().c_str(), Flags);
    }
  }

  if (selectRISCVMultilib(RISCVMultilibs, MArch, Flags,
                          Result.SelectedMultilibs))
    Result.Multilibs = RISCVMultilibs;
}

static void findRISCVMultilibs(const Driver &D,
                               const llvm::Triple &TargetTriple, StringRef Path,
                               const ArgList &Args, DetectedMultilibs &Result) {
  if (TargetTriple.getOS() == llvm::Triple::UnknownOS)
    return findRISCVBareMetalMultilibs(D, TargetTriple, Path, Args, Result);

  FilterNonExistent NonExistent(Path, "/crtbegin.o", D.getVFS());
  MultilibBuilder Ilp32 =
      MultilibBuilder("lib32/ilp32").flag("-m32").flag("-mabi=ilp32");
  MultilibBuilder Ilp32f =
      MultilibBuilder("lib32/ilp32f").flag("-m32").flag("-mabi=ilp32f");
  MultilibBuilder Ilp32d =
      MultilibBuilder("lib32/ilp32d").flag("-m32").flag("-mabi=ilp32d");
  MultilibBuilder Lp64 =
      MultilibBuilder("lib64/lp64").flag("-m64").flag("-mabi=lp64");
  MultilibBuilder Lp64f =
      MultilibBuilder("lib64/lp64f").flag("-m64").flag("-mabi=lp64f");
  MultilibBuilder Lp64d =
      MultilibBuilder("lib64/lp64d").flag("-m64").flag("-mabi=lp64d");
  MultilibSet RISCVMultilibs =
      MultilibSetBuilder()
          .Either({Ilp32, Ilp32f, Ilp32d, Lp64, Lp64f, Lp64d})
          .makeMultilibSet()
          .FilterOut(NonExistent);

  Multilib::flags_list Flags;
  bool IsRV64 = TargetTriple.getArch() == llvm::Triple::riscv64;
  StringRef ABIName = tools::riscv::getRISCVABI(Args, TargetTriple);

  addMultilibFlag(!IsRV64, "-m32", Flags);
  addMultilibFlag(IsRV64, "-m64", Flags);
  addMultilibFlag(ABIName == "ilp32", "-mabi=ilp32", Flags);
  addMultilibFlag(ABIName == "ilp32f", "-mabi=ilp32f", Flags);
  addMultilibFlag(ABIName == "ilp32d", "-mabi=ilp32d", Flags);
  addMultilibFlag(ABIName == "lp64", "-mabi=lp64", Flags);
  addMultilibFlag(ABIName == "lp64f", "-mabi=lp64f", Flags);
  addMultilibFlag(ABIName == "lp64d", "-mabi=lp64d", Flags);

  if (RISCVMultilibs.select(Flags, Result.SelectedMultilibs))
    Result.Multilibs = RISCVMultilibs;
}

static bool findBiarchMultilibs(const Driver &D,
                                const llvm::Triple &TargetTriple,
                                StringRef Path, const ArgList &Args,
                                bool NeedsBiarchSuffix,
                                DetectedMultilibs &Result) {
  MultilibBuilder DefaultBuilder;

  // Some versions of SUSE and Fedora on ppc64 put 32-bit libs
  // in what would normally be GCCInstallPath and put the 64-bit
  // libs in a subdirectory named 64. The simple logic we follow is that
  // *if* there is a subdirectory of the right name with crtbegin.o in it,
  // we use that. If not, and if not a biarch triple alias, we look for
  // crtbegin.o without the subdirectory.

  StringRef Suff64 = "/64";
  // Solaris uses platform-specific suffixes instead of /64.
  if (TargetTriple.isOSSolaris()) {
    switch (TargetTriple.getArch()) {
    case llvm::Triple::x86:
    case llvm::Triple::x86_64:
      Suff64 = "/amd64";
      break;
    case llvm::Triple::sparc:
    case llvm::Triple::sparcv9:
      Suff64 = "/sparcv9";
      break;
    default:
      break;
    }
  }

  Multilib Alt64 = MultilibBuilder()
                       .gccSuffix(Suff64)
                       .includeSuffix(Suff64)
                       .flag("-m32", /*Disallow=*/true)
                       .flag("-m64")
                       .flag("-mx32", /*Disallow=*/true)
                       .makeMultilib();
  Multilib Alt32 = MultilibBuilder()
                       .gccSuffix("/32")
                       .includeSuffix("/32")
                       .flag("-m32")
                       .flag("-m64", /*Disallow=*/true)
                       .flag("-mx32", /*Disallow=*/true)
                       .makeMultilib();
  Multilib Altx32 = MultilibBuilder()
                        .gccSuffix("/x32")
                        .includeSuffix("/x32")
                        .flag("-m32", /*Disallow=*/true)
                        .flag("-m64", /*Disallow=*/true)
                        .flag("-mx32")
                        .makeMultilib();
  Multilib Alt32sparc = MultilibBuilder()
                            .gccSuffix("/sparcv8plus")
                            .includeSuffix("/sparcv8plus")
                            .flag("-m32")
                            .flag("-m64", /*Disallow=*/true)
                            .makeMultilib();

  // GCC toolchain for IAMCU doesn't have crtbegin.o, so look for libgcc.a.
  FilterNonExistent NonExistent(
      Path, TargetTriple.isOSIAMCU() ? "/libgcc.a" : "/crtbegin.o", D.getVFS());

  // Determine default multilib from: 32, 64, x32
  // Also handle cases such as 64 on 32, 32 on 64, etc.
  enum { UNKNOWN, WANT32, WANT64, WANTX32 } Want = UNKNOWN;
  const bool IsX32 = TargetTriple.isX32();
  if (TargetTriple.isArch32Bit() && !NonExistent(Alt32))
    Want = WANT64;
  if (TargetTriple.isArch32Bit() && !NonExistent(Alt32sparc))
    Want = WANT64;
  else if (TargetTriple.isArch64Bit() && IsX32 && !NonExistent(Altx32))
    Want = WANT64;
  else if (TargetTriple.isArch64Bit() && !IsX32 && !NonExistent(Alt64))
    Want = WANT32;
  else if (TargetTriple.isArch64Bit() && !NonExistent(Alt32sparc))
    Want = WANT64;
  else {
    if (TargetTriple.isArch32Bit())
      Want = NeedsBiarchSuffix ? WANT64 : WANT32;
    else if (IsX32)
      Want = NeedsBiarchSuffix ? WANT64 : WANTX32;
    else
      Want = NeedsBiarchSuffix ? WANT32 : WANT64;
  }

  if (Want == WANT32)
    DefaultBuilder.flag("-m32")
        .flag("-m64", /*Disallow=*/true)
        .flag("-mx32", /*Disallow=*/true);
  else if (Want == WANT64)
    DefaultBuilder.flag("-m32", /*Disallow=*/true)
        .flag("-m64")
        .flag("-mx32", /*Disallow=*/true);
  else if (Want == WANTX32)
    DefaultBuilder.flag("-m32", /*Disallow=*/true)
        .flag("-m64", /*Disallow=*/true)
        .flag("-mx32");
  else
    return false;

  Multilib Default = DefaultBuilder.makeMultilib();

  Result.Multilibs.push_back(Default);
  Result.Multilibs.push_back(Alt64);
  Result.Multilibs.push_back(Alt32);
  Result.Multilibs.push_back(Altx32);
  Result.Multilibs.push_back(Alt32sparc);

  Result.Multilibs.FilterOut(NonExistent);

  Multilib::flags_list Flags;
  addMultilibFlag(TargetTriple.isArch64Bit() && !IsX32, "-m64", Flags);
  addMultilibFlag(TargetTriple.isArch32Bit(), "-m32", Flags);
  addMultilibFlag(TargetTriple.isArch64Bit() && IsX32, "-mx32", Flags);

  if (!Result.Multilibs.select(Flags, Result.SelectedMultilibs))
    return false;

  if (Result.SelectedMultilibs.back() == Alt64 ||
      Result.SelectedMultilibs.back() == Alt32 ||
      Result.SelectedMultilibs.back() == Altx32 ||
      Result.SelectedMultilibs.back() == Alt32sparc)
    Result.BiarchSibling = Default;

  return true;
}

/// Generic_GCC - A tool chain using the 'gcc' command to perform
/// all subcommands; this relies on gcc translating the majority of
/// command line options.

/// Less-than for GCCVersion, implementing a Strict Weak Ordering.
bool Generic_GCC::GCCVersion::isOlderThan(int RHSMajor, int RHSMinor,
                                          int RHSPatch,
                                          StringRef RHSPatchSuffix) const {
  if (Major != RHSMajor)
    return Major < RHSMajor;
  if (Minor != RHSMinor) {
    // Note that versions without a specified minor sort higher than those with
    // a minor.
    if (RHSMinor == -1)
      return true;
    if (Minor == -1)
      return false;
    return Minor < RHSMinor;
  }
  if (Patch != RHSPatch) {
    // Note that versions without a specified patch sort higher than those with
    // a patch.
    if (RHSPatch == -1)
      return true;
    if (Patch == -1)
      return false;

    // Otherwise just sort on the patch itself.
    return Patch < RHSPatch;
  }
  if (PatchSuffix != RHSPatchSuffix) {
    // Sort empty suffixes higher.
    if (RHSPatchSuffix.empty())
      return true;
    if (PatchSuffix.empty())
      return false;

    // Provide a lexicographic sort to make this a total ordering.
    return PatchSuffix < RHSPatchSuffix;
  }

  // The versions are equal.
  return false;
}

/// Parse a GCCVersion object out of a string of text.
///
/// This is the primary means of forming GCCVersion objects.
/*static*/
Generic_GCC::GCCVersion Generic_GCC::GCCVersion::Parse(StringRef VersionText) {
  const GCCVersion BadVersion = {VersionText.str(), -1, -1, -1, "", "", ""};
  std::pair<StringRef, StringRef> First = VersionText.split('.');
  std::pair<StringRef, StringRef> Second = First.second.split('.');

  StringRef MajorStr = First.first;
  StringRef MinorStr = Second.first;
  StringRef PatchStr = Second.second;

  GCCVersion GoodVersion = {VersionText.str(), -1, -1, -1, "", "", ""};

  // Parse version number strings such as:
  //   5
  //   4.4
  //   4.4-patched
  //   4.4.0
  //   4.4.x
  //   4.4.2-rc4
  //   4.4.x-patched
  //   10-win32
  // Split on '.', handle 1, 2 or 3 such segments. Each segment must contain
  // purely a number, except for the last one, where a non-number suffix
  // is stored in PatchSuffix. The third segment is allowed to not contain
  // a number at all.

  auto TryParseLastNumber = [&](StringRef Segment, int &Number,
                                std::string &OutStr) -> bool {
    // Look for a number prefix and parse that, and split out any trailing
    // string into GoodVersion.PatchSuffix.

    if (size_t EndNumber = Segment.find_first_not_of("0123456789")) {
      StringRef NumberStr = Segment.slice(0, EndNumber);
      if (NumberStr.getAsInteger(10, Number) || Number < 0)
        return false;
      OutStr = NumberStr;
      GoodVersion.PatchSuffix = Segment.substr(EndNumber);
      return true;
    }
    return false;
  };
  auto TryParseNumber = [](StringRef Segment, int &Number) -> bool {
    if (Segment.getAsInteger(10, Number) || Number < 0)
      return false;
    return true;
  };

  if (MinorStr.empty()) {
    // If no minor string, major is the last segment
    if (!TryParseLastNumber(MajorStr, GoodVersion.Major, GoodVersion.MajorStr))
      return BadVersion;
    return GoodVersion;
  }

  if (!TryParseNumber(MajorStr, GoodVersion.Major))
    return BadVersion;
  GoodVersion.MajorStr = MajorStr;

  if (PatchStr.empty()) {
    // If no patch string, minor is the last segment
    if (!TryParseLastNumber(MinorStr, GoodVersion.Minor, GoodVersion.MinorStr))
      return BadVersion;
    return GoodVersion;
  }

  if (!TryParseNumber(MinorStr, GoodVersion.Minor))
    return BadVersion;
  GoodVersion.MinorStr = MinorStr;

  // For the last segment, tolerate a missing number.
  std::string DummyStr;
  TryParseLastNumber(PatchStr, GoodVersion.Patch, DummyStr);
  return GoodVersion;
}

static llvm::StringRef getGCCToolchainDir(const ArgList &Args,
                                          llvm::StringRef SysRoot) {
  const Arg *A = Args.getLastArg(clang::driver::options::OPT_gcc_toolchain);
  if (A)
    return A->getValue();

  // If we have a SysRoot, ignore GCC_INSTALL_PREFIX.
  // GCC_INSTALL_PREFIX specifies the gcc installation for the default
  // sysroot and is likely not valid with a different sysroot.
  if (!SysRoot.empty())
    return "";

  return GCC_INSTALL_PREFIX;
}

/// Initialize a GCCInstallationDetector from the driver.
///
/// This performs all of the autodetection and sets up the various paths.
/// Once constructed, a GCCInstallationDetector is essentially immutable.
///
/// FIXME: We shouldn't need an explicit TargetTriple parameter here, and
/// should instead pull the target out of the driver. This is currently
/// necessary because the driver doesn't store the final version of the target
/// triple.
void Generic_GCC::GCCInstallationDetector::init(
    const llvm::Triple &TargetTriple, const ArgList &Args) {
  llvm::Triple BiarchVariantTriple = TargetTriple.isArch32Bit()
                                         ? TargetTriple.get64BitArchVariant()
                                         : TargetTriple.get32BitArchVariant();
  // The library directories which may contain GCC installations.
  SmallVector<StringRef, 4> CandidateLibDirs, CandidateBiarchLibDirs;
  // The compatible GCC triples for this particular architecture.
  SmallVector<StringRef, 16> CandidateTripleAliases;
  SmallVector<StringRef, 16> CandidateBiarchTripleAliases;
  // Add some triples that we want to check first.
  CandidateTripleAliases.push_back(TargetTriple.str());
  std::string TripleNoVendor, BiarchTripleNoVendor;
  if (TargetTriple.getVendor() == llvm::Triple::UnknownVendor) {
    StringRef OSEnv = TargetTriple.getOSAndEnvironmentName();
    if (TargetTriple.getEnvironment() == llvm::Triple::GNUX32)
      OSEnv = "linux-gnu";
    TripleNoVendor = (TargetTriple.getArchName().str() + '-' + OSEnv).str();
    CandidateTripleAliases.push_back(TripleNoVendor);
    if (BiarchVariantTriple.getArch() != llvm::Triple::UnknownArch) {
      BiarchTripleNoVendor =
          (BiarchVariantTriple.getArchName().str() + '-' + OSEnv).str();
      CandidateBiarchTripleAliases.push_back(BiarchTripleNoVendor);
    }
  }

  CollectLibDirsAndTriples(TargetTriple, BiarchVariantTriple, CandidateLibDirs,
                           CandidateTripleAliases, CandidateBiarchLibDirs,
                           CandidateBiarchTripleAliases);

  // If --gcc-install-dir= is specified, skip filesystem detection.
  if (const Arg *A =
          Args.getLastArg(clang::driver::options::OPT_gcc_install_dir_EQ);
      A && A->getValue()[0]) {
    StringRef InstallDir = A->getValue();
    if (!ScanGCCForMultilibs(TargetTriple, Args, InstallDir, false)) {
      D.Diag(diag::err_drv_invalid_gcc_install_dir) << InstallDir;
    } else {
      (void)InstallDir.consume_back("/");
      StringRef VersionText = llvm::sys::path::filename(InstallDir);
      StringRef TripleText =
          llvm::sys::path::filename(llvm::sys::path::parent_path(InstallDir));

      Version = GCCVersion::Parse(VersionText);
      GCCTriple.setTriple(TripleText);
      GCCInstallPath = std::string(InstallDir);
      GCCParentLibPath = GCCInstallPath + "/../../..";
      IsValid = true;
    }
    return;
  }

  // If --gcc-triple is specified use this instead of trying to
  // auto-detect a triple.
  if (const Arg *A =
          Args.getLastArg(clang::driver::options::OPT_gcc_triple_EQ)) {
    StringRef GCCTriple = A->getValue();
    CandidateTripleAliases.clear();
    CandidateTripleAliases.push_back(GCCTriple);
  }

  // Compute the set of prefixes for our search.
  SmallVector<std::string, 8> Prefixes;
  StringRef GCCToolchainDir = getGCCToolchainDir(Args, D.SysRoot);
  if (GCCToolchainDir != "") {
    if (GCCToolchainDir.back() == '/')
      GCCToolchainDir = GCCToolchainDir.drop_back(); // remove the /

    Prefixes.push_back(std::string(GCCToolchainDir));
  } else {
    // If we have a SysRoot, try that first.
    if (!D.SysRoot.empty()) {
      Prefixes.push_back(D.SysRoot);
      AddDefaultGCCPrefixes(TargetTriple, Prefixes, D.SysRoot);
    }

    // Then look for gcc installed alongside clang.
    Prefixes.push_back(D.Dir + "/..");

    // Next, look for prefix(es) that correspond to distribution-supplied gcc
    // installations.
    if (D.SysRoot.empty()) {
      // Typically /usr.
      AddDefaultGCCPrefixes(TargetTriple, Prefixes, D.SysRoot);
    }

    // Try to respect gcc-config on Gentoo if --gcc-toolchain is not provided.
    // This avoids accidentally enforcing the system GCC version when using a
    // custom toolchain.
    SmallVector<StringRef, 16> GentooTestTriples;
    // Try to match an exact triple as target triple first.
    // e.g. crossdev -S x86_64-gentoo-linux-gnu will install gcc libs for
    // x86_64-gentoo-linux-gnu. But "clang -target x86_64-gentoo-linux-gnu"
    // may pick the libraries for x86_64-pc-linux-gnu even when exact matching
    // triple x86_64-gentoo-linux-gnu is present.
    GentooTestTriples.push_back(TargetTriple.str());
    GentooTestTriples.append(CandidateTripleAliases.begin(),
                             CandidateTripleAliases.end());
    if (ScanGentooConfigs(TargetTriple, Args, GentooTestTriples,
                          CandidateBiarchTripleAliases))
      return;
  }

  // Loop over the various components which exist and select the best GCC
  // installation available. GCC installs are ranked by version number.
  const GCCVersion VersionZero = GCCVersion::Parse("0.0.0");
  Version = VersionZero;
  for (const std::string &Prefix : Prefixes) {
    auto &VFS = D.getVFS();
    if (!VFS.exists(Prefix))
      continue;
    for (StringRef Suffix : CandidateLibDirs) {
      const std::string LibDir = concat(Prefix, Suffix);
      if (!VFS.exists(LibDir))
        continue;
      // Maybe filter out <libdir>/gcc and <libdir>/gcc-cross.
      bool GCCDirExists = VFS.exists(LibDir + "/gcc");
      bool GCCCrossDirExists = VFS.exists(LibDir + "/gcc-cross");
      for (StringRef Candidate : CandidateTripleAliases)
        ScanLibDirForGCCTriple(TargetTriple, Args, LibDir, Candidate, false,
                               GCCDirExists, GCCCrossDirExists);
    }
    for (StringRef Suffix : CandidateBiarchLibDirs) {
      const std::string LibDir = Prefix + Suffix.str();
      if (!VFS.exists(LibDir))
        continue;
      bool GCCDirExists = VFS.exists(LibDir + "/gcc");
      bool GCCCrossDirExists = VFS.exists(LibDir + "/gcc-cross");
      for (StringRef Candidate : CandidateBiarchTripleAliases)
        ScanLibDirForGCCTriple(TargetTriple, Args, LibDir, Candidate, true,
                               GCCDirExists, GCCCrossDirExists);
    }

    // Skip other prefixes once a GCC installation is found.
    if (Version > VersionZero)
      break;
  }
}

void Generic_GCC::GCCInstallationDetector::print(raw_ostream &OS) const {
  for (const auto &InstallPath : CandidateGCCInstallPaths)
    OS << "Found candidate GCC installation: " << InstallPath << "\n";

  if (!GCCInstallPath.empty())
    OS << "Selected GCC installation: " << GCCInstallPath << "\n";

  for (const auto &Multilib : Multilibs)
    OS << "Candidate multilib: " << Multilib << "\n";

  if (Multilibs.size() != 0 || !SelectedMultilib.isDefault())
    OS << "Selected multilib: " << SelectedMultilib << "\n";
}

bool Generic_GCC::GCCInstallationDetector::getBiarchSibling(Multilib &M) const {
  if (BiarchSibling) {
    M = *BiarchSibling;
    return true;
  }
  return false;
}

void Generic_GCC::GCCInstallationDetector::AddDefaultGCCPrefixes(
    const llvm::Triple &TargetTriple, SmallVectorImpl<std::string> &Prefixes,
    StringRef SysRoot) {

  if (TargetTriple.isOSHaiku()) {
    Prefixes.push_back(concat(SysRoot, "/boot/system/develop/tools"));
    return;
  }

  if (TargetTriple.isOSSolaris()) {
    // Solaris is a special case.
    // The GCC installation is under
    //   /usr/gcc/<major>.<minor>/lib/gcc/<triple>/<major>.<minor>.<patch>/
    // so we need to find those /usr/gcc/*/lib/gcc libdirs and go with
    // /usr/gcc/<version> as a prefix.

    SmallVector<std::pair<GCCVersion, std::string>, 8> SolarisPrefixes;
    std::string PrefixDir = concat(SysRoot, "/usr/gcc");
    std::error_code EC;
    for (llvm::vfs::directory_iterator LI = D.getVFS().dir_begin(PrefixDir, EC),
                                       LE;
         !EC && LI != LE; LI = LI.increment(EC)) {
      StringRef VersionText = llvm::sys::path::filename(LI->path());
      GCCVersion CandidateVersion = GCCVersion::Parse(VersionText);

      // Filter out obviously bad entries.
      if (CandidateVersion.Major == -1 || CandidateVersion.isOlderThan(4, 1, 1))
        continue;

      std::string CandidatePrefix = PrefixDir + "/" + VersionText.str();
      std::string CandidateLibPath = CandidatePrefix + "/lib/gcc";
      if (!D.getVFS().exists(CandidateLibPath))
        continue;

      SolarisPrefixes.emplace_back(
          std::make_pair(CandidateVersion, CandidatePrefix));
    }
    // Sort in reverse order so GCCInstallationDetector::init picks the latest.
    std::sort(SolarisPrefixes.rbegin(), SolarisPrefixes.rend());
    for (auto p : SolarisPrefixes)
      Prefixes.emplace_back(p.second);
    return;
  }

  // For Linux, if --sysroot is not specified, look for RHEL/CentOS devtoolsets
  // and gcc-toolsets.
  if (SysRoot.empty() && TargetTriple.getOS() == llvm::Triple::Linux &&
      D.getVFS().exists("/opt/rh")) {
    // TODO: We may want to remove this, since the functionality
    //   can be achieved using config files.
    Prefixes.push_back("/opt/rh/gcc-toolset-12/root/usr");
    Prefixes.push_back("/opt/rh/gcc-toolset-11/root/usr");
    Prefixes.push_back("/opt/rh/gcc-toolset-10/root/usr");
    Prefixes.push_back("/opt/rh/devtoolset-12/root/usr");
    Prefixes.push_back("/opt/rh/devtoolset-11/root/usr");
    Prefixes.push_back("/opt/rh/devtoolset-10/root/usr");
    Prefixes.push_back("/opt/rh/devtoolset-9/root/usr");
    Prefixes.push_back("/opt/rh/devtoolset-8/root/usr");
    Prefixes.push_back("/opt/rh/devtoolset-7/root/usr");
    Prefixes.push_back("/opt/rh/devtoolset-6/root/usr");
    Prefixes.push_back("/opt/rh/devtoolset-4/root/usr");
    Prefixes.push_back("/opt/rh/devtoolset-3/root/usr");
    Prefixes.push_back("/opt/rh/devtoolset-2/root/usr");
  }

  // Fall back to /usr which is used by most non-Solaris systems.
  Prefixes.push_back(concat(SysRoot, "/usr"));
}

/*static*/ void Generic_GCC::GCCInstallationDetector::CollectLibDirsAndTriples(
    const llvm::Triple &TargetTriple, const llvm::Triple &BiarchTriple,
    SmallVectorImpl<StringRef> &LibDirs,
    SmallVectorImpl<StringRef> &TripleAliases,
    SmallVectorImpl<StringRef> &BiarchLibDirs,
    SmallVectorImpl<StringRef> &BiarchTripleAliases) {
  // Declare a bunch of static data sets that we'll select between below. These
  // are specifically designed to always refer to string literals to avoid any
  // lifetime or initialization issues.
  //
  // The *Triples variables hard code some triples so that, for example,
  // --target=aarch64 (incomplete triple) can detect lib/aarch64-linux-gnu.
  // They are not needed when the user has correct LLVM_DEFAULT_TARGET_TRIPLE
  // and always uses the full --target (e.g. --target=aarch64-linux-gnu).  The
  // lists should shrink over time. Please don't add more elements to *Triples.
  static const char *const AArch64LibDirs[] = {"/lib64", "/lib"};
  static const char *const AArch64Triples[] = {
      "aarch64-none-linux-gnu", "aarch64-linux-gnu", "aarch64-redhat-linux",
      "aarch64-suse-linux"};
  static const char *const AArch64beLibDirs[] = {"/lib"};
  static const char *const AArch64beTriples[] = {"aarch64_be-none-linux-gnu"};

  static const char *const ARMLibDirs[] = {"/lib"};
  static const char *const ARMTriples[] = {"arm-linux-gnueabi"};
  static const char *const ARMHFTriples[] = {"arm-linux-gnueabihf",
                                             "armv7hl-redhat-linux-gnueabi",
                                             "armv6hl-suse-linux-gnueabi",
                                             "armv7hl-suse-linux-gnueabi"};
  static const char *const ARMebLibDirs[] = {"/lib"};
  static const char *const ARMebTriples[] = {"armeb-linux-gnueabi"};
  static const char *const ARMebHFTriples[] = {
      "armeb-linux-gnueabihf", "armebv7hl-redhat-linux-gnueabi"};

  static const char *const AVRLibDirs[] = {"/lib"};
  static const char *const AVRTriples[] = {"avr"};

  static const char *const CSKYLibDirs[] = {"/lib"};
  static const char *const CSKYTriples[] = {
      "csky-linux-gnuabiv2", "csky-linux-uclibcabiv2", "csky-elf-noneabiv2"};

  static const char *const X86_64LibDirs[] = {"/lib64", "/lib"};
  static const char *const X86_64Triples[] = {
      "x86_64-linux-gnu",       "x86_64-unknown-linux-gnu",
      "x86_64-pc-linux-gnu",    "x86_64-redhat-linux6E",
      "x86_64-redhat-linux",    "x86_64-suse-linux",
      "x86_64-manbo-linux-gnu", "x86_64-slackware-linux",
      "x86_64-unknown-linux",   "x86_64-amazon-linux"};
  static const char *const X32Triples[] = {"x86_64-linux-gnux32",
                                           "x86_64-pc-linux-gnux32"};
  static const char *const X32LibDirs[] = {"/libx32", "/lib"};
  static const char *const X86LibDirs[] = {"/lib32", "/lib"};
  static const char *const X86Triples[] = {
      "i586-linux-gnu",      "i686-linux-gnu",        "i686-pc-linux-gnu",
      "i386-redhat-linux6E", "i686-redhat-linux",     "i386-redhat-linux",
      "i586-suse-linux",     "i686-montavista-linux",
  };

  static const char *const LoongArch64LibDirs[] = {"/lib64", "/lib"};
  static const char *const LoongArch64Triples[] = {
      "loongarch64-linux-gnu", "loongarch64-unknown-linux-gnu"};

  static const char *const M68kLibDirs[] = {"/lib"};
  static const char *const M68kTriples[] = {"m68k-unknown-linux-gnu",
                                            "m68k-suse-linux"};

  static const char *const MIPSLibDirs[] = {"/libo32", "/lib"};
  static const char *const MIPSTriples[] = {
      "mips-linux-gnu", "mips-mti-linux", "mips-mti-linux-gnu",
      "mips-img-linux-gnu", "mipsisa32r6-linux-gnu"};
  static const char *const MIPSELLibDirs[] = {"/libo32", "/lib"};
  static const char *const MIPSELTriples[] = {"mipsel-linux-gnu",
                                              "mips-img-linux-gnu"};

  static const char *const MIPS64LibDirs[] = {"/lib64", "/lib"};
  static const char *const MIPS64Triples[] = {
      "mips-mti-linux-gnu", "mips-img-linux-gnu", "mips64-linux-gnuabi64",
      "mipsisa64r6-linux-gnu", "mipsisa64r6-linux-gnuabi64"};
  static const char *const MIPS64ELLibDirs[] = {"/lib64", "/lib"};
  static const char *const MIPS64ELTriples[] = {
      "mips-mti-linux-gnu", "mips-img-linux-gnu", "mips64el-linux-gnuabi64",
      "mipsisa64r6el-linux-gnu", "mipsisa64r6el-linux-gnuabi64"};

  static const char *const MIPSN32LibDirs[] = {"/lib32"};
  static const char *const MIPSN32Triples[] = {"mips64-linux-gnuabin32",
                                               "mipsisa64r6-linux-gnuabin32"};
  static const char *const MIPSN32ELLibDirs[] = {"/lib32"};
  static const char *const MIPSN32ELTriples[] = {
      "mips64el-linux-gnuabin32", "mipsisa64r6el-linux-gnuabin32"};

  static const char *const MSP430LibDirs[] = {"/lib"};
  static const char *const MSP430Triples[] = {"msp430-elf"};

  static const char *const PPCLibDirs[] = {"/lib32", "/lib"};
  static const char *const PPCTriples[] = {
      "powerpc-unknown-linux-gnu",
      // On 32-bit PowerPC systems running SUSE Linux, gcc is configured as a
      // 64-bit compiler which defaults to "-m32", hence "powerpc64-suse-linux".
      "powerpc64-suse-linux", "powerpc-montavista-linuxspe"};
  static const char *const PPCLELibDirs[] = {"/lib32", "/lib"};
  static const char *const PPCLETriples[] = {"powerpcle-unknown-linux-gnu",
                                             "powerpcle-linux-musl"};

  static const char *const PPC64LibDirs[] = {"/lib64", "/lib"};
  static const char *const PPC64Triples[] = {"powerpc64-unknown-linux-gnu",
                                             "powerpc64-suse-linux",
                                             "ppc64-redhat-linux"};
  static const char *const PPC64LELibDirs[] = {"/lib64", "/lib"};
  static const char *const PPC64LETriples[] = {
      "powerpc64le-unknown-linux-gnu", "powerpc64le-none-linux-gnu",
      "powerpc64le-suse-linux", "ppc64le-redhat-linux"};

  static const char *const RISCV32LibDirs[] = {"/lib32", "/lib"};
  static const char *const RISCV32Triples[] = {"riscv32-unknown-linux-gnu",
                                               "riscv32-unknown-elf"};
  static const char *const RISCV64LibDirs[] = {"/lib64", "/lib"};
  static const char *const RISCV64Triples[] = {"riscv64-unknown-linux-gnu",
                                               "riscv64-unknown-elf"};

  static const char *const SPARCv8LibDirs[] = {"/lib32", "/lib"};
  static const char *const SPARCv8Triples[] = {"sparc-linux-gnu",
                                               "sparcv8-linux-gnu"};
  static const char *const SPARCv9LibDirs[] = {"/lib64", "/lib"};
  static const char *const SPARCv9Triples[] = {"sparc64-linux-gnu",
                                               "sparcv9-linux-gnu"};

  static const char *const SystemZLibDirs[] = {"/lib64", "/lib"};
  static const char *const SystemZTriples[] = {
      "s390x-unknown-linux-gnu", "s390x-ibm-linux-gnu", "s390x-suse-linux",
      "s390x-redhat-linux"};

  using std::begin;
  using std::end;

  if (TargetTriple.isOSSolaris()) {
    static const char *const SolarisLibDirs[] = {"/lib"};
    static const char *const SolarisSparcV8Triples[] = {
        "sparc-sun-solaris2.11"};
    static const char *const SolarisSparcV9Triples[] = {
        "sparcv9-sun-solaris2.11"};
    static const char *const SolarisX86Triples[] = {"i386-pc-solaris2.11"};
    static const char *const SolarisX86_64Triples[] = {"x86_64-pc-solaris2.11"};
    LibDirs.append(begin(SolarisLibDirs), end(SolarisLibDirs));
    BiarchLibDirs.append(begin(SolarisLibDirs), end(SolarisLibDirs));
    switch (TargetTriple.getArch()) {
    case llvm::Triple::x86:
      TripleAliases.append(begin(SolarisX86Triples), end(SolarisX86Triples));
      BiarchTripleAliases.append(begin(SolarisX86_64Triples),
                                 end(SolarisX86_64Triples));
      break;
    case llvm::Triple::x86_64:
      TripleAliases.append(begin(SolarisX86_64Triples),
                           end(SolarisX86_64Triples));
      BiarchTripleAliases.append(begin(SolarisX86Triples),
                                 end(SolarisX86Triples));
      break;
    case llvm::Triple::sparc:
      TripleAliases.append(begin(SolarisSparcV8Triples),
                           end(SolarisSparcV8Triples));
      BiarchTripleAliases.append(begin(SolarisSparcV9Triples),
                                 end(SolarisSparcV9Triples));
      break;
    case llvm::Triple::sparcv9:
      TripleAliases.append(begin(SolarisSparcV9Triples),
                           end(SolarisSparcV9Triples));
      BiarchTripleAliases.append(begin(SolarisSparcV8Triples),
                                 end(SolarisSparcV8Triples));
      break;
    default:
      break;
    }
    return;
  }

  // Android targets should not use GNU/Linux tools or libraries.
  if (TargetTriple.isAndroid()) {
    static const char *const AArch64AndroidTriples[] = {
        "aarch64-linux-android"};
    static const char *const ARMAndroidTriples[] = {"arm-linux-androideabi"};
    static const char *const X86AndroidTriples[] = {"i686-linux-android"};
    static const char *const X86_64AndroidTriples[] = {"x86_64-linux-android"};

    switch (TargetTriple.getArch()) {
    case llvm::Triple::aarch64:
      LibDirs.append(begin(AArch64LibDirs), end(AArch64LibDirs));
      TripleAliases.append(begin(AArch64AndroidTriples),
                           end(AArch64AndroidTriples));
      break;
    case llvm::Triple::arm:
    case llvm::Triple::thumb:
      LibDirs.append(begin(ARMLibDirs), end(ARMLibDirs));
      TripleAliases.append(begin(ARMAndroidTriples), end(ARMAndroidTriples));
      break;
    case llvm::Triple::x86_64:
      LibDirs.append(begin(X86_64LibDirs), end(X86_64LibDirs));
      TripleAliases.append(begin(X86_64AndroidTriples),
                           end(X86_64AndroidTriples));
      BiarchLibDirs.append(begin(X86LibDirs), end(X86LibDirs));
      BiarchTripleAliases.append(begin(X86AndroidTriples),
                                 end(X86AndroidTriples));
      break;
    case llvm::Triple::x86:
      LibDirs.append(begin(X86LibDirs), end(X86LibDirs));
      TripleAliases.append(begin(X86AndroidTriples), end(X86AndroidTriples));
      BiarchLibDirs.append(begin(X86_64LibDirs), end(X86_64LibDirs));
      BiarchTripleAliases.append(begin(X86_64AndroidTriples),
                                 end(X86_64AndroidTriples));
      break;
    default:
      break;
    }

    return;
  }

  if (TargetTriple.isOSHurd()) {
    switch (TargetTriple.getArch()) {
    case llvm::Triple::x86_64:
      LibDirs.append(begin(X86_64LibDirs), end(X86_64LibDirs));
      TripleAliases.push_back("x86_64-gnu");
      break;
    case llvm::Triple::x86:
      LibDirs.append(begin(X86LibDirs), end(X86LibDirs));
      TripleAliases.push_back("i686-gnu");
      break;
    default:
      break;
    }

    return;
  }

  switch (TargetTriple.getArch()) {
  case llvm::Triple::aarch64:
    LibDirs.append(begin(AArch64LibDirs), end(AArch64LibDirs));
    TripleAliases.append(begin(AArch64Triples), end(AArch64Triples));
    BiarchLibDirs.append(begin(AArch64LibDirs), end(AArch64LibDirs));
    BiarchTripleAliases.append(begin(AArch64Triples), end(AArch64Triples));
    break;
  case llvm::Triple::aarch64_be:
    LibDirs.append(begin(AArch64beLibDirs), end(AArch64beLibDirs));
    TripleAliases.append(begin(AArch64beTriples), end(AArch64beTriples));
    BiarchLibDirs.append(begin(AArch64beLibDirs), end(AArch64beLibDirs));
    BiarchTripleAliases.append(begin(AArch64beTriples), end(AArch64beTriples));
    break;
  case llvm::Triple::arm:
  case llvm::Triple::thumb:
    LibDirs.append(begin(ARMLibDirs), end(ARMLibDirs));
    if (TargetTriple.getEnvironment() == llvm::Triple::GNUEABIHF ||
        TargetTriple.getEnvironment() == llvm::Triple::GNUEABIHFT64 ||
        TargetTriple.getEnvironment() == llvm::Triple::MuslEABIHF ||
        TargetTriple.getEnvironment() == llvm::Triple::EABIHF) {
      TripleAliases.append(begin(ARMHFTriples), end(ARMHFTriples));
    } else {
      TripleAliases.append(begin(ARMTriples), end(ARMTriples));
    }
    break;
  case llvm::Triple::armeb:
  case llvm::Triple::thumbeb:
    LibDirs.append(begin(ARMebLibDirs), end(ARMebLibDirs));
    if (TargetTriple.getEnvironment() == llvm::Triple::GNUEABIHF ||
        TargetTriple.getEnvironment() == llvm::Triple::GNUEABIHFT64 ||
        TargetTriple.getEnvironment() == llvm::Triple::MuslEABIHF ||
        TargetTriple.getEnvironment() == llvm::Triple::EABIHF) {
      TripleAliases.append(begin(ARMebHFTriples), end(ARMebHFTriples));
    } else {
      TripleAliases.append(begin(ARMebTriples), end(ARMebTriples));
    }
    break;
  case llvm::Triple::avr:
    LibDirs.append(begin(AVRLibDirs), end(AVRLibDirs));
    TripleAliases.append(begin(AVRTriples), end(AVRTriples));
    break;
  case llvm::Triple::csky:
    LibDirs.append(begin(CSKYLibDirs), end(CSKYLibDirs));
    TripleAliases.append(begin(CSKYTriples), end(CSKYTriples));
    break;
  case llvm::Triple::x86_64:
    if (TargetTriple.isX32()) {
      LibDirs.append(begin(X32LibDirs), end(X32LibDirs));
      TripleAliases.append(begin(X32Triples), end(X32Triples));
      BiarchLibDirs.append(begin(X86_64LibDirs), end(X86_64LibDirs));
      BiarchTripleAliases.append(begin(X86_64Triples), end(X86_64Triples));
    } else {
      LibDirs.append(begin(X86_64LibDirs), end(X86_64LibDirs));
      TripleAliases.append(begin(X86_64Triples), end(X86_64Triples));
      BiarchLibDirs.append(begin(X32LibDirs), end(X32LibDirs));
      BiarchTripleAliases.append(begin(X32Triples), end(X32Triples));
    }
    BiarchLibDirs.append(begin(X86LibDirs), end(X86LibDirs));
    BiarchTripleAliases.append(begin(X86Triples), end(X86Triples));
    break;
  case llvm::Triple::x86:
    LibDirs.append(begin(X86LibDirs), end(X86LibDirs));
    // MCU toolchain is 32 bit only and its triple alias is TargetTriple
    // itself, which will be appended below.
    if (!TargetTriple.isOSIAMCU()) {
      TripleAliases.append(begin(X86Triples), end(X86Triples));
      BiarchLibDirs.append(begin(X86_64LibDirs), end(X86_64LibDirs));
      BiarchTripleAliases.append(begin(X86_64Triples), end(X86_64Triples));
      BiarchLibDirs.append(begin(X32LibDirs), end(X32LibDirs));
      BiarchTripleAliases.append(begin(X32Triples), end(X32Triples));
    }
    break;
  // TODO: Handle loongarch32.
  case llvm::Triple::loongarch64:
    LibDirs.append(begin(LoongArch64LibDirs), end(LoongArch64LibDirs));
    TripleAliases.append(begin(LoongArch64Triples), end(LoongArch64Triples));
    break;
  case llvm::Triple::m68k:
    LibDirs.append(begin(M68kLibDirs), end(M68kLibDirs));
    TripleAliases.append(begin(M68kTriples), end(M68kTriples));
    break;
  case llvm::Triple::mips:
    LibDirs.append(begin(MIPSLibDirs), end(MIPSLibDirs));
    TripleAliases.append(begin(MIPSTriples), end(MIPSTriples));
    BiarchLibDirs.append(begin(MIPS64LibDirs), end(MIPS64LibDirs));
    BiarchTripleAliases.append(begin(MIPS64Triples), end(MIPS64Triples));
    BiarchLibDirs.append(begin(MIPSN32LibDirs), end(MIPSN32LibDirs));
    BiarchTripleAliases.append(begin(MIPSN32Triples), end(MIPSN32Triples));
    break;
  case llvm::Triple::mipsel:
    LibDirs.append(begin(MIPSELLibDirs), end(MIPSELLibDirs));
    TripleAliases.append(begin(MIPSELTriples), end(MIPSELTriples));
    TripleAliases.append(begin(MIPSTriples), end(MIPSTriples));
    BiarchLibDirs.append(begin(MIPS64ELLibDirs), end(MIPS64ELLibDirs));
    BiarchTripleAliases.append(begin(MIPS64ELTriples), end(MIPS64ELTriples));
    BiarchLibDirs.append(begin(MIPSN32ELLibDirs), end(MIPSN32ELLibDirs));
    BiarchTripleAliases.append(begin(MIPSN32ELTriples), end(MIPSN32ELTriples));
    break;
  case llvm::Triple::mips64:
    LibDirs.append(begin(MIPS64LibDirs), end(MIPS64LibDirs));
    TripleAliases.append(begin(MIPS64Triples), end(MIPS64Triples));
    BiarchLibDirs.append(begin(MIPSLibDirs), end(MIPSLibDirs));
    BiarchTripleAliases.append(begin(MIPSTriples), end(MIPSTriples));
    BiarchLibDirs.append(begin(MIPSN32LibDirs), end(MIPSN32LibDirs));
    BiarchTripleAliases.append(begin(MIPSN32Triples), end(MIPSN32Triples));
    break;
  case llvm::Triple::mips64el:
    LibDirs.append(begin(MIPS64ELLibDirs), end(MIPS64ELLibDirs));
    TripleAliases.append(begin(MIPS64ELTriples), end(MIPS64ELTriples));
    BiarchLibDirs.append(begin(MIPSELLibDirs), end(MIPSELLibDirs));
    BiarchTripleAliases.append(begin(MIPSELTriples), end(MIPSELTriples));
    BiarchLibDirs.append(begin(MIPSN32ELLibDirs), end(MIPSN32ELLibDirs));
    BiarchTripleAliases.append(begin(MIPSN32ELTriples), end(MIPSN32ELTriples));
    BiarchTripleAliases.append(begin(MIPSTriples), end(MIPSTriples));
    break;
  case llvm::Triple::msp430:
    LibDirs.append(begin(MSP430LibDirs), end(MSP430LibDirs));
    TripleAliases.append(begin(MSP430Triples), end(MSP430Triples));
    break;
  case llvm::Triple::ppc:
    LibDirs.append(begin(PPCLibDirs), end(PPCLibDirs));
    TripleAliases.append(begin(PPCTriples), end(PPCTriples));
    BiarchLibDirs.append(begin(PPC64LibDirs), end(PPC64LibDirs));
    BiarchTripleAliases.append(begin(PPC64Triples), end(PPC64Triples));
    break;
  case llvm::Triple::ppcle:
    LibDirs.append(begin(PPCLELibDirs), end(PPCLELibDirs));
    TripleAliases.append(begin(PPCLETriples), end(PPCLETriples));
    BiarchLibDirs.append(begin(PPC64LELibDirs), end(PPC64LELibDirs));
    BiarchTripleAliases.append(begin(PPC64LETriples), end(PPC64LETriples));
    break;
  case llvm::Triple::ppc64:
    LibDirs.append(begin(PPC64LibDirs), end(PPC64LibDirs));
    TripleAliases.append(begin(PPC64Triples), end(PPC64Triples));
    BiarchLibDirs.append(begin(PPCLibDirs), end(PPCLibDirs));
    BiarchTripleAliases.append(begin(PPCTriples), end(PPCTriples));
    break;
  case llvm::Triple::ppc64le:
    LibDirs.append(begin(PPC64LELibDirs), end(PPC64LELibDirs));
    TripleAliases.append(begin(PPC64LETriples), end(PPC64LETriples));
    BiarchLibDirs.append(begin(PPCLELibDirs), end(PPCLELibDirs));
    BiarchTripleAliases.append(begin(PPCLETriples), end(PPCLETriples));
    break;
  case llvm::Triple::riscv32:
    LibDirs.append(begin(RISCV32LibDirs), end(RISCV32LibDirs));
    TripleAliases.append(begin(RISCV32Triples), end(RISCV32Triples));
    BiarchLibDirs.append(begin(RISCV64LibDirs), end(RISCV64LibDirs));
    BiarchTripleAliases.append(begin(RISCV64Triples), end(RISCV64Triples));
    break;
  case llvm::Triple::riscv64:
    LibDirs.append(begin(RISCV64LibDirs), end(RISCV64LibDirs));
    TripleAliases.append(begin(RISCV64Triples), end(RISCV64Triples));
    BiarchLibDirs.append(begin(RISCV32LibDirs), end(RISCV32LibDirs));
    BiarchTripleAliases.append(begin(RISCV32Triples), end(RISCV32Triples));
    break;
  case llvm::Triple::sparc:
  case llvm::Triple::sparcel:
    LibDirs.append(begin(SPARCv8LibDirs), end(SPARCv8LibDirs));
    TripleAliases.append(begin(SPARCv8Triples), end(SPARCv8Triples));
    BiarchLibDirs.append(begin(SPARCv9LibDirs), end(SPARCv9LibDirs));
    BiarchTripleAliases.append(begin(SPARCv9Triples), end(SPARCv9Triples));
    break;
  case llvm::Triple::sparcv9:
    LibDirs.append(begin(SPARCv9LibDirs), end(SPARCv9LibDirs));
    TripleAliases.append(begin(SPARCv9Triples), end(SPARCv9Triples));
    BiarchLibDirs.append(begin(SPARCv8LibDirs), end(SPARCv8LibDirs));
    BiarchTripleAliases.append(begin(SPARCv8Triples), end(SPARCv8Triples));
    break;
  case llvm::Triple::systemz:
    LibDirs.append(begin(SystemZLibDirs), end(SystemZLibDirs));
    TripleAliases.append(begin(SystemZTriples), end(SystemZTriples));
    break;
  default:
    // By default, just rely on the standard lib directories and the original
    // triple.
    break;
  }

  // Also include the multiarch variant if it's different.
  if (TargetTriple.str() != BiarchTriple.str())
    BiarchTripleAliases.push_back(BiarchTriple.str());
}

bool Generic_GCC::GCCInstallationDetector::ScanGCCForMultilibs(
    const llvm::Triple &TargetTriple, const ArgList &Args,
    StringRef Path, bool NeedsBiarchSuffix) {
  llvm::Triple::ArchType TargetArch = TargetTriple.getArch();
  DetectedMultilibs Detected;

  // Android standalone toolchain could have multilibs for ARM and Thumb.
  // Debian mips multilibs behave more like the rest of the biarch ones,
  // so handle them there
  if (isArmOrThumbArch(TargetArch) && TargetTriple.isAndroid()) {
    // It should also work without multilibs in a simplified toolchain.
    findAndroidArmMultilibs(D, TargetTriple, Path, Args, Detected);
  } else if (TargetTriple.isCSKY()) {
    findCSKYMultilibs(D, TargetTriple, Path, Args, Detected);
  } else if (TargetTriple.isMIPS()) {
    if (!findMIPSMultilibs(D, TargetTriple, Path, Args, Detected))
      return false;
  } else if (TargetTriple.isRISCV()) {
    findRISCVMultilibs(D, TargetTriple, Path, Args, Detected);
  } else if (isMSP430(TargetArch)) {
    findMSP430Multilibs(D, TargetTriple, Path, Args, Detected);
  } else if (TargetArch == llvm::Triple::avr) {
    // AVR has no multilibs.
  } else if (!findBiarchMultilibs(D, TargetTriple, Path, Args,
                                  NeedsBiarchSuffix, Detected)) {
    return false;
  }

  Multilibs = Detected.Multilibs;
  SelectedMultilib = Detected.SelectedMultilibs.empty()
                         ? Multilib()
                         : Detected.SelectedMultilibs.back();
  BiarchSibling = Detected.BiarchSibling;

  return true;
}

void Generic_GCC::GCCInstallationDetector::ScanLibDirForGCCTriple(
    const llvm::Triple &TargetTriple, const ArgList &Args,
    const std::string &LibDir, StringRef CandidateTriple,
    bool NeedsBiarchSuffix, bool GCCDirExists, bool GCCCrossDirExists) {
  // Locations relative to the system lib directory where GCC's triple-specific
  // directories might reside.
  struct GCCLibSuffix {
    // Path from system lib directory to GCC triple-specific directory.
    std::string LibSuffix;
    // Path from GCC triple-specific directory back to system lib directory.
    // This is one '..' component per component in LibSuffix.
    StringRef ReversePath;
    // Whether this library suffix is relevant for the triple.
    bool Active;
  } Suffixes[] = {
      // This is the normal place.
      {"gcc/" + CandidateTriple.str(), "../..", GCCDirExists},

      // Debian puts cross-compilers in gcc-cross.
      {"gcc-cross/" + CandidateTriple.str(), "../..", GCCCrossDirExists},

      // The Freescale PPC SDK has the gcc libraries in
      // <sysroot>/usr/lib/<triple>/x.y.z so have a look there as well. Only do
      // this on Freescale triples, though, since some systems put a *lot* of
      // files in that location, not just GCC installation data.
      {CandidateTriple.str(), "..",
       TargetTriple.getVendor() == llvm::Triple::Freescale ||
           TargetTriple.getVendor() == llvm::Triple::OpenEmbedded}};

  for (auto &Suffix : Suffixes) {
    if (!Suffix.Active)
      continue;

    StringRef LibSuffix = Suffix.LibSuffix;
    std::error_code EC;
    for (llvm::vfs::directory_iterator
             LI = D.getVFS().dir_begin(LibDir + "/" + LibSuffix, EC),
             LE;
         !EC && LI != LE; LI = LI.increment(EC)) {
      StringRef VersionText = llvm::sys::path::filename(LI->path());
      GCCVersion CandidateVersion = GCCVersion::Parse(VersionText);
      if (CandidateVersion.Major != -1) // Filter obviously bad entries.
        if (!CandidateGCCInstallPaths.insert(std::string(LI->path())).second)
          continue; // Saw this path before; no need to look at it again.
      if (CandidateVersion.isOlderThan(4, 1, 1))
        continue;
      if (CandidateVersion <= Version)
        continue;

      if (!ScanGCCForMultilibs(TargetTriple, Args, LI->path(),
                               NeedsBiarchSuffix))
        continue;

      Version = CandidateVersion;
      GCCTriple.setTriple(CandidateTriple);
      // FIXME: We hack together the directory name here instead of
      // using LI to ensure stable path separators across Windows and
      // Linux.
      GCCInstallPath = (LibDir + "/" + LibSuffix + "/" + VersionText).str();
      GCCParentLibPath = (GCCInstallPath + "/../" + Suffix.ReversePath).str();
      IsValid = true;
    }
  }
}

bool Generic_GCC::GCCInstallationDetector::ScanGentooConfigs(
    const llvm::Triple &TargetTriple, const ArgList &Args,
    const SmallVectorImpl<StringRef> &CandidateTriples,
    const SmallVectorImpl<StringRef> &CandidateBiarchTriples) {
  if (!D.getVFS().exists(concat(D.SysRoot, GentooConfigDir)))
    return false;

  for (StringRef CandidateTriple : CandidateTriples) {
    if (ScanGentooGccConfig(TargetTriple, Args, CandidateTriple))
      return true;
  }

  for (StringRef CandidateTriple : CandidateBiarchTriples) {
    if (ScanGentooGccConfig(TargetTriple, Args, CandidateTriple, true))
      return true;
  }
  return false;
}

bool Generic_GCC::GCCInstallationDetector::ScanGentooGccConfig(
    const llvm::Triple &TargetTriple, const ArgList &Args,
    StringRef CandidateTriple, bool NeedsBiarchSuffix) {
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> File =
      D.getVFS().getBufferForFile(concat(D.SysRoot, GentooConfigDir,
                                         "/config-" + CandidateTriple.str()));
  if (File) {
    SmallVector<StringRef, 2> Lines;
    File.get()->getBuffer().split(Lines, "\n");
    for (StringRef Line : Lines) {
      Line = Line.trim();
      // CURRENT=triple-version
      if (!Line.consume_front("CURRENT="))
        continue;
      // Process the config file pointed to by CURRENT.
      llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> ConfigFile =
          D.getVFS().getBufferForFile(
              concat(D.SysRoot, GentooConfigDir, "/" + Line));
      std::pair<StringRef, StringRef> ActiveVersion = Line.rsplit('-');
      // List of paths to scan for libraries.
      SmallVector<StringRef, 4> GentooScanPaths;
      // Scan the Config file to find installed GCC libraries path.
      // Typical content of the GCC config file:
      // LDPATH="/usr/lib/gcc/x86_64-pc-linux-gnu/4.9.x:/usr/lib/gcc/
      // (continued from previous line) x86_64-pc-linux-gnu/4.9.x/32"
      // MANPATH="/usr/share/gcc-data/x86_64-pc-linux-gnu/4.9.x/man"
      // INFOPATH="/usr/share/gcc-data/x86_64-pc-linux-gnu/4.9.x/info"
      // STDCXX_INCDIR="/usr/lib/gcc/x86_64-pc-linux-gnu/4.9.x/include/g++-v4"
      // We are looking for the paths listed in LDPATH=... .
      if (ConfigFile) {
        SmallVector<StringRef, 2> ConfigLines;
        ConfigFile.get()->getBuffer().split(ConfigLines, "\n");
        for (StringRef ConfLine : ConfigLines) {
          ConfLine = ConfLine.trim();
          if (ConfLine.consume_front("LDPATH=")) {
            // Drop '"' from front and back if present.
            ConfLine.consume_back("\"");
            ConfLine.consume_front("\"");
            // Get all paths sperated by ':'
            ConfLine.split(GentooScanPaths, ':', -1, /*AllowEmpty*/ false);
          }
        }
      }
      // Test the path based on the version in /etc/env.d/gcc/config-{tuple}.
      std::string basePath = "/usr/lib/gcc/" + ActiveVersion.first.str() + "/"
          + ActiveVersion.second.str();
      GentooScanPaths.push_back(StringRef(basePath));

      // Scan all paths for GCC libraries.
      for (const auto &GentooScanPath : GentooScanPaths) {
        std::string GentooPath = concat(D.SysRoot, GentooScanPath);
        if (D.getVFS().exists(GentooPath + "/crtbegin.o")) {
          if (!ScanGCCForMultilibs(TargetTriple, Args, GentooPath,
                                   NeedsBiarchSuffix))
            continue;

          Version = GCCVersion::Parse(ActiveVersion.second);
          GCCInstallPath = GentooPath;
          GCCParentLibPath = GentooPath + std::string("/../../..");
          GCCTriple.setTriple(ActiveVersion.first);
          IsValid = true;
          return true;
        }
      }
    }
  }

  return false;
}

Generic_GCC::Generic_GCC(const Driver &D, const llvm::Triple &Triple,
                         const ArgList &Args)
    : ToolChain(D, Triple, Args), GCCInstallation(D),
      CudaInstallation(D, Triple, Args), RocmInstallation(D, Triple, Args) {
  getProgramPaths().push_back(getDriver().Dir);
}

Generic_GCC::~Generic_GCC() {}

Tool *Generic_GCC::getTool(Action::ActionClass AC) const {
  switch (AC) {
  case Action::PreprocessJobClass:
    if (!Preprocess)
      Preprocess.reset(new clang::driver::tools::gcc::Preprocessor(*this));
    return Preprocess.get();
  case Action::CompileJobClass:
    if (!Compile)
      Compile.reset(new tools::gcc::Compiler(*this));
    return Compile.get();
  default:
    return ToolChain::getTool(AC);
  }
}

Tool *Generic_GCC::buildAssembler() const {
  return new tools::gnutools::Assembler(*this);
}

Tool *Generic_GCC::buildLinker() const { return new tools::gcc::Linker(*this); }

void Generic_GCC::printVerboseInfo(raw_ostream &OS) const {
  // Print the information about how we detected the GCC installation.
  GCCInstallation.print(OS);
  CudaInstallation->print(OS);
  RocmInstallation->print(OS);
}

ToolChain::UnwindTableLevel
Generic_GCC::getDefaultUnwindTableLevel(const ArgList &Args) const {
  switch (getArch()) {
  case llvm::Triple::aarch64:
  case llvm::Triple::aarch64_be:
  case llvm::Triple::ppc:
  case llvm::Triple::ppcle:
  case llvm::Triple::ppc64:
  case llvm::Triple::ppc64le:
  case llvm::Triple::riscv32:
  case llvm::Triple::riscv64:
  case llvm::Triple::x86:
  case llvm::Triple::x86_64:
    return UnwindTableLevel::Asynchronous;
  default:
    return UnwindTableLevel::None;
  }
}

bool Generic_GCC::isPICDefault() const {
  switch (getArch()) {
  case llvm::Triple::x86_64:
    return getTriple().isOSWindows();
  case llvm::Triple::mips64:
  case llvm::Triple::mips64el:
    return true;
  default:
    return false;
  }
}

bool Generic_GCC::isPIEDefault(const llvm::opt::ArgList &Args) const {
  return false;
}

bool Generic_GCC::isPICDefaultForced() const {
  return getArch() == llvm::Triple::x86_64 && getTriple().isOSWindows();
}

bool Generic_GCC::IsIntegratedAssemblerDefault() const {
  switch (getTriple().getArch()) {
  case llvm::Triple::nvptx:
  case llvm::Triple::nvptx64:
  case llvm::Triple::xcore:
    return false;
  default:
    return true;
  }
}

void Generic_GCC::PushPPaths(ToolChain::path_list &PPaths) {
  // Cross-compiling binutils and GCC installations (vanilla and openSUSE at
  // least) put various tools in a triple-prefixed directory off of the parent
  // of the GCC installation. We use the GCC triple here to ensure that we end
  // up with tools that support the same amount of cross compiling as the
  // detected GCC installation. For example, if we find a GCC installation
  // targeting x86_64, but it is a bi-arch GCC installation, it can also be
  // used to target i386.
  if (GCCInstallation.isValid()) {
    PPaths.push_back(Twine(GCCInstallation.getParentLibPath() + "/../" +
                           GCCInstallation.getTriple().str() + "/bin")
                         .str());
  }
}

void Generic_GCC::AddMultilibPaths(const Driver &D,
                                   const std::string &SysRoot,
                                   const std::string &OSLibDir,
                                   const std::string &MultiarchTriple,
                                   path_list &Paths) {
  // Add the multilib suffixed paths where they are available.
  if (GCCInstallation.isValid()) {
    assert(!SelectedMultilibs.empty());
    const llvm::Triple &GCCTriple = GCCInstallation.getTriple();
    const std::string &LibPath =
        std::string(GCCInstallation.getParentLibPath());

    // Sourcery CodeBench MIPS toolchain holds some libraries under
    // a biarch-like suffix of the GCC installation.
    if (const auto &PathsCallback = Multilibs.filePathsCallback())
      for (const auto &Path : PathsCallback(SelectedMultilibs.back()))
        addPathIfExists(D, GCCInstallation.getInstallPath() + Path, Paths);

    // Add lib/gcc/$triple/$version, with an optional /multilib suffix.
    addPathIfExists(D,
                    GCCInstallation.getInstallPath() +
                        SelectedMultilibs.back().gccSuffix(),
                    Paths);

    // Add lib/gcc/$triple/$libdir
    // For GCC built with --enable-version-specific-runtime-libs.
    addPathIfExists(D, GCCInstallation.getInstallPath() + "/../" + OSLibDir,
                    Paths);

    // GCC cross compiling toolchains will install target libraries which ship
    // as part of the toolchain under <prefix>/<triple>/<libdir> rather than as
    // any part of the GCC installation in
    // <prefix>/<libdir>/gcc/<triple>/<version>. This decision is somewhat
    // debatable, but is the reality today. We need to search this tree even
    // when we have a sysroot somewhere else. It is the responsibility of
    // whomever is doing the cross build targeting a sysroot using a GCC
    // installation that is *not* within the system root to ensure two things:
    //
    //  1) Any DSOs that are linked in from this tree or from the install path
    //     above must be present on the system root and found via an
    //     appropriate rpath.
    //  2) There must not be libraries installed into
    //     <prefix>/<triple>/<libdir> unless they should be preferred over
    //     those within the system root.
    //
    // Note that this matches the GCC behavior. See the below comment for where
    // Clang diverges from GCC's behavior.
    addPathIfExists(D,
                    LibPath + "/../" + GCCTriple.str() + "/lib/../" + OSLibDir +
                        SelectedMultilibs.back().osSuffix(),
                    Paths);

    // If the GCC installation we found is inside of the sysroot, we want to
    // prefer libraries installed in the parent prefix of the GCC installation.
    // It is important to *not* use these paths when the GCC installation is
    // outside of the system root as that can pick up unintended libraries.
    // This usually happens when there is an external cross compiler on the
    // host system, and a more minimal sysroot available that is the target of
    // the cross. Note that GCC does include some of these directories in some
    // configurations but this seems somewhere between questionable and simply
    // a bug.
    if (StringRef(LibPath).starts_with(SysRoot))
      addPathIfExists(D, LibPath + "/../" + OSLibDir, Paths);
  }
}

void Generic_GCC::AddMultiarchPaths(const Driver &D,
                                    const std::string &SysRoot,
                                    const std::string &OSLibDir,
                                    path_list &Paths) {
  if (GCCInstallation.isValid()) {
    const std::string &LibPath =
        std::string(GCCInstallation.getParentLibPath());
    const llvm::Triple &GCCTriple = GCCInstallation.getTriple();
    const Multilib &Multilib = GCCInstallation.getMultilib();
    addPathIfExists(
        D, LibPath + "/../" + GCCTriple.str() + "/lib" + Multilib.osSuffix(),
                    Paths);
  }
}

void Generic_GCC::AddMultilibIncludeArgs(const ArgList &DriverArgs,
                                         ArgStringList &CC1Args) const {
  // Add include directories specific to the selected multilib set and multilib.
  if (!GCCInstallation.isValid())
    return;
  // gcc TOOL_INCLUDE_DIR.
  const llvm::Triple &GCCTriple = GCCInstallation.getTriple();
  std::string LibPath(GCCInstallation.getParentLibPath());
  addSystemInclude(DriverArgs, CC1Args,
                   Twine(LibPath) + "/../" + GCCTriple.str() + "/include");

  const auto &Callback = Multilibs.includeDirsCallback();
  if (Callback) {
    for (const auto &Path : Callback(GCCInstallation.getMultilib()))
      addExternCSystemIncludeIfExists(DriverArgs, CC1Args,
                                      GCCInstallation.getInstallPath() + Path);
  }
}

void Generic_GCC::AddClangCXXStdlibIncludeArgs(const ArgList &DriverArgs,
                                               ArgStringList &CC1Args) const {
  if (DriverArgs.hasArg(options::OPT_nostdinc, options::OPT_nostdincxx,
                        options::OPT_nostdlibinc))
    return;

  switch (GetCXXStdlibType(DriverArgs)) {
  case ToolChain::CST_Libcxx:
    addLibCxxIncludePaths(DriverArgs, CC1Args);
    break;

  case ToolChain::CST_Libstdcxx:
    addLibStdCxxIncludePaths(DriverArgs, CC1Args);
    break;
  }
}

void
Generic_GCC::addLibCxxIncludePaths(const llvm::opt::ArgList &DriverArgs,
                                   llvm::opt::ArgStringList &CC1Args) const {
  const Driver &D = getDriver();
  std::string SysRoot = computeSysRoot();
  if (SysRoot.empty())
    SysRoot = llvm::sys::path::get_separator();

  auto AddIncludePath = [&](StringRef Path, bool TargetDirRequired = false) {
    std::string Version = detectLibcxxVersion(Path);
    if (Version.empty())
      return false;

    // First add the per-target include path if it exists.
    bool TargetDirExists = false;
    std::optional<std::string> TargetIncludeDir = getTargetSubDirPath(Path);
    if (TargetIncludeDir) {
      SmallString<128> TargetDir(*TargetIncludeDir);
      llvm::sys::path::append(TargetDir, "c++", Version);
      if (D.getVFS().exists(TargetDir)) {
        addSystemInclude(DriverArgs, CC1Args, TargetDir);
        TargetDirExists = true;
      }
    }
    if (TargetDirRequired && !TargetDirExists)
      return false;

    // Second add the generic one.
    SmallString<128> GenericDir(Path);
    llvm::sys::path::append(GenericDir, "c++", Version);
    addSystemInclude(DriverArgs, CC1Args, GenericDir);
    return true;
  };

  // Android only uses the libc++ headers installed alongside the toolchain if
  // they contain an Android-specific target include path, otherwise they're
  // incompatible with the NDK libraries.
  SmallString<128> DriverIncludeDir(getDriver().Dir);
  llvm::sys::path::append(DriverIncludeDir, "..", "include");
  if (AddIncludePath(DriverIncludeDir,
                     /*TargetDirRequired=*/getTriple().isAndroid()))
    return;
  // If this is a development, non-installed, clang, libcxx will
  // not be found at ../include/c++ but it likely to be found at
  // one of the following two locations:
  SmallString<128> UsrLocalIncludeDir(SysRoot);
  llvm::sys::path::append(UsrLocalIncludeDir, "usr", "local", "include");
  if (AddIncludePath(UsrLocalIncludeDir))
    return;
  SmallString<128> UsrIncludeDir(SysRoot);
  llvm::sys::path::append(UsrIncludeDir, "usr", "include");
  if (AddIncludePath(UsrIncludeDir))
    return;
}

bool Generic_GCC::addLibStdCXXIncludePaths(Twine IncludeDir, StringRef Triple,
                                           Twine IncludeSuffix,
                                           const llvm::opt::ArgList &DriverArgs,
                                           llvm::opt::ArgStringList &CC1Args,
                                           bool DetectDebian) const {
  if (!getVFS().exists(IncludeDir))
    return false;

  // Debian native gcc uses g++-multiarch-incdir.diff which uses
  // include/x86_64-linux-gnu/c++/10$IncludeSuffix instead of
  // include/c++/10/x86_64-linux-gnu$IncludeSuffix.
  std::string Dir = IncludeDir.str();
  StringRef Include =
      llvm::sys::path::parent_path(llvm::sys::path::parent_path(Dir));
  std::string Path =
      (Include + "/" + Triple + Dir.substr(Include.size()) + IncludeSuffix)
          .str();
  if (DetectDebian && !getVFS().exists(Path))
    return false;

  // GPLUSPLUS_INCLUDE_DIR
  addSystemInclude(DriverArgs, CC1Args, IncludeDir);
  // GPLUSPLUS_TOOL_INCLUDE_DIR. If Triple is not empty, add a target-dependent
  // include directory.
  if (DetectDebian)
    addSystemInclude(DriverArgs, CC1Args, Path);
  else if (!Triple.empty())
    addSystemInclude(DriverArgs, CC1Args,
                     IncludeDir + "/" + Triple + IncludeSuffix);
  // GPLUSPLUS_BACKWARD_INCLUDE_DIR
  addSystemInclude(DriverArgs, CC1Args, IncludeDir + "/backward");
  return true;
}

bool Generic_GCC::addGCCLibStdCxxIncludePaths(
    const llvm::opt::ArgList &DriverArgs, llvm::opt::ArgStringList &CC1Args,
    StringRef DebianMultiarch) const {
  assert(GCCInstallation.isValid());

  // By default, look for the C++ headers in an include directory adjacent to
  // the lib directory of the GCC installation. Note that this is expect to be
  // equivalent to '/usr/include/c++/X.Y' in almost all cases.
  StringRef LibDir = GCCInstallation.getParentLibPath();
  StringRef InstallDir = GCCInstallation.getInstallPath();
  StringRef TripleStr = GCCInstallation.getTriple().str();
  const Multilib &Multilib = GCCInstallation.getMultilib();
  const GCCVersion &Version = GCCInstallation.getVersion();

  // Try /../$triple/include/c++/$version (gcc --print-multiarch is not empty).
  if (addLibStdCXXIncludePaths(
          LibDir.str() + "/../" + TripleStr + "/include/c++/" + Version.Text,
          TripleStr, Multilib.includeSuffix(), DriverArgs, CC1Args))
    return true;

  // Try /gcc/$triple/$version/include/c++/ (gcc --print-multiarch is not
  // empty). Like above but for GCC built with
  // --enable-version-specific-runtime-libs.
  if (addLibStdCXXIncludePaths(LibDir.str() + "/gcc/" + TripleStr + "/" +
                                   Version.Text + "/include/c++/",
                               TripleStr, Multilib.includeSuffix(), DriverArgs,
                               CC1Args))
    return true;

  // Detect Debian g++-multiarch-incdir.diff.
  if (addLibStdCXXIncludePaths(LibDir.str() + "/../include/c++/" + Version.Text,
                               DebianMultiarch, Multilib.includeSuffix(),
                               DriverArgs, CC1Args, /*Debian=*/true))
    return true;

  // Try /../include/c++/$version (gcc --print-multiarch is empty).
  if (addLibStdCXXIncludePaths(LibDir.str() + "/../include/c++/" + Version.Text,
                               TripleStr, Multilib.includeSuffix(), DriverArgs,
                               CC1Args))
    return true;

  // Otherwise, fall back on a bunch of options which don't use multiarch
  // layouts for simplicity.
  const std::string LibStdCXXIncludePathCandidates[] = {
      // Gentoo is weird and places its headers inside the GCC install,
      // so if the first attempt to find the headers fails, try these patterns.
      InstallDir.str() + "/include/g++-v" + Version.Text,
      InstallDir.str() + "/include/g++-v" + Version.MajorStr + "." +
          Version.MinorStr,
      InstallDir.str() + "/include/g++-v" + Version.MajorStr,
  };

  for (const auto &IncludePath : LibStdCXXIncludePathCandidates) {
    if (addLibStdCXXIncludePaths(IncludePath, TripleStr,
                                 Multilib.includeSuffix(), DriverArgs, CC1Args))
      return true;
  }
  return false;
}

void
Generic_GCC::addLibStdCxxIncludePaths(const llvm::opt::ArgList &DriverArgs,
                                      llvm::opt::ArgStringList &CC1Args) const {
  if (GCCInstallation.isValid()) {
    addGCCLibStdCxxIncludePaths(DriverArgs, CC1Args,
                                GCCInstallation.getTriple().str());
  }
}

llvm::opt::DerivedArgList *
Generic_GCC::TranslateArgs(const llvm::opt::DerivedArgList &Args, StringRef,
                           Action::OffloadKind DeviceOffloadKind) const {

  // If this tool chain is used for an OpenMP offloading device we have to make
  // sure we always generate a shared library regardless of the commands the
  // user passed to the host. This is required because the runtime library
  // is required to load the device image dynamically at run time.
  if (DeviceOffloadKind == Action::OFK_OpenMP) {
    DerivedArgList *DAL = new DerivedArgList(Args.getBaseArgs());
    const OptTable &Opts = getDriver().getOpts();

    // Request the shared library. Given that these options are decided
    // implicitly, they do not refer to any base argument.
    DAL->AddFlagArg(/*BaseArg=*/nullptr, Opts.getOption(options::OPT_shared));
    DAL->AddFlagArg(/*BaseArg=*/nullptr, Opts.getOption(options::OPT_fPIC));

    // Filter all the arguments we don't care passing to the offloading
    // toolchain as they can mess up with the creation of a shared library.
    for (auto *A : Args) {
      switch ((options::ID)A->getOption().getID()) {
      default:
        DAL->append(A);
        break;
      case options::OPT_shared:
      case options::OPT_dynamic:
      case options::OPT_static:
      case options::OPT_fPIC:
      case options::OPT_fno_PIC:
      case options::OPT_fpic:
      case options::OPT_fno_pic:
      case options::OPT_fPIE:
      case options::OPT_fno_PIE:
      case options::OPT_fpie:
      case options::OPT_fno_pie:
        break;
      }
    }
    return DAL;
  }
  return nullptr;
}

void Generic_ELF::anchor() {}

void Generic_ELF::addClangTargetOptions(const ArgList &DriverArgs,
                                        ArgStringList &CC1Args,
                                        Action::OffloadKind) const {
  if (!DriverArgs.hasFlag(options::OPT_fuse_init_array,
                          options::OPT_fno_use_init_array, true))
    CC1Args.push_back("-fno-use-init-array");
}
