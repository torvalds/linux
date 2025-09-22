//===--- NaCl.cpp - Native Client ToolChain Implementations -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "NaCl.h"
#include "CommonArgs.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/InputInfo.h"
#include "clang/Driver/Options.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/Path.h"

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang::driver::toolchains;
using namespace clang;
using namespace llvm::opt;

// NaCl ARM assembly (inline or standalone) can be written with a set of macros
// for the various SFI requirements like register masking. The assembly tool
// inserts the file containing the macros as an input into all the assembly
// jobs.
void nacltools::AssemblerARM::ConstructJob(Compilation &C, const JobAction &JA,
                                           const InputInfo &Output,
                                           const InputInfoList &Inputs,
                                           const ArgList &Args,
                                           const char *LinkingOutput) const {
  const auto &ToolChain = static_cast<const NaClToolChain &>(getToolChain());
  InputInfo NaClMacros(types::TY_PP_Asm, ToolChain.GetNaClArmMacrosPath(),
                       "nacl-arm-macros.s");
  InputInfoList NewInputs;
  NewInputs.push_back(NaClMacros);
  NewInputs.append(Inputs.begin(), Inputs.end());
  gnutools::Assembler::ConstructJob(C, JA, Output, NewInputs, Args,
                                    LinkingOutput);
}

// This is quite similar to gnutools::Linker::ConstructJob with changes that
// we use static by default, do not yet support sanitizers or LTO, and a few
// others. Eventually we can support more of that and hopefully migrate back
// to gnutools::Linker.
void nacltools::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                     const InputInfo &Output,
                                     const InputInfoList &Inputs,
                                     const ArgList &Args,
                                     const char *LinkingOutput) const {

  const auto &ToolChain = static_cast<const NaClToolChain &>(getToolChain());
  const Driver &D = ToolChain.getDriver();
  const llvm::Triple::ArchType Arch = ToolChain.getArch();
  const bool IsStatic =
      !Args.hasArg(options::OPT_dynamic) && !Args.hasArg(options::OPT_shared);

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

  if (Args.hasArg(options::OPT_rdynamic))
    CmdArgs.push_back("-export-dynamic");

  if (Args.hasArg(options::OPT_s))
    CmdArgs.push_back("-s");

  // NaClToolChain doesn't have ExtraOpts like Linux; the only relevant flag
  // from there is --build-id, which we do want.
  CmdArgs.push_back("--build-id");

  if (!IsStatic)
    CmdArgs.push_back("--eh-frame-hdr");

  CmdArgs.push_back("-m");
  if (Arch == llvm::Triple::x86)
    CmdArgs.push_back("elf_i386_nacl");
  else if (Arch == llvm::Triple::arm)
    CmdArgs.push_back("armelf_nacl");
  else if (Arch == llvm::Triple::x86_64)
    CmdArgs.push_back("elf_x86_64_nacl");
  else if (Arch == llvm::Triple::mipsel)
    CmdArgs.push_back("mipselelf_nacl");
  else
    D.Diag(diag::err_target_unsupported_arch) << ToolChain.getArchName()
                                              << "Native Client";

  if (IsStatic)
    CmdArgs.push_back("-static");
  else if (Args.hasArg(options::OPT_shared))
    CmdArgs.push_back("-shared");

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());
  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles)) {
    if (!Args.hasArg(options::OPT_shared))
      CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crt1.o")));
    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crti.o")));

    const char *crtbegin;
    if (IsStatic)
      crtbegin = "crtbeginT.o";
    else if (Args.hasArg(options::OPT_shared))
      crtbegin = "crtbeginS.o";
    else
      crtbegin = "crtbegin.o";
    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath(crtbegin)));
  }

  Args.addAllArgs(CmdArgs, {options::OPT_L, options::OPT_u});

  ToolChain.AddFilePathLibArgs(Args, CmdArgs);

  if (Args.hasArg(options::OPT_Z_Xlinker__no_demangle))
    CmdArgs.push_back("--no-demangle");

  AddLinkerInputs(ToolChain, Inputs, Args, CmdArgs, JA);

  if (D.CCCIsCXX() &&
      !Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs)) {
    if (ToolChain.ShouldLinkCXXStdlib(Args)) {
      bool OnlyLibstdcxxStatic =
          Args.hasArg(options::OPT_static_libstdcxx) && !IsStatic;
      if (OnlyLibstdcxxStatic)
        CmdArgs.push_back("-Bstatic");
      ToolChain.AddCXXStdlibLibArgs(Args, CmdArgs);
      if (OnlyLibstdcxxStatic)
        CmdArgs.push_back("-Bdynamic");
    }
    CmdArgs.push_back("-lm");
  }

  if (!Args.hasArg(options::OPT_nostdlib)) {
    if (!Args.hasArg(options::OPT_nodefaultlibs)) {
      // Always use groups, since it has no effect on dynamic libraries.
      CmdArgs.push_back("--start-group");
      CmdArgs.push_back("-lc");
      // NaCl's libc++ currently requires libpthread, so just always include it
      // in the group for C++.
      if (Args.hasArg(options::OPT_pthread) ||
          Args.hasArg(options::OPT_pthreads) || D.CCCIsCXX()) {
        // Gold, used by Mips, handles nested groups differently than ld, and
        // without '-lnacl' it prefers symbols from libpthread.a over libnacl.a,
        // which is not a desired behaviour here.
        // See https://sourceware.org/ml/binutils/2015-03/msg00034.html
        if (getToolChain().getArch() == llvm::Triple::mipsel)
          CmdArgs.push_back("-lnacl");

        CmdArgs.push_back("-lpthread");
      }

      CmdArgs.push_back("-lgcc");
      CmdArgs.push_back("--as-needed");
      if (IsStatic)
        CmdArgs.push_back("-lgcc_eh");
      else
        CmdArgs.push_back("-lgcc_s");
      CmdArgs.push_back("--no-as-needed");

      // Mips needs to create and use pnacl_legacy library that contains
      // definitions from bitcode/pnaclmm.c and definitions for
      // __nacl_tp_tls_offset() and __nacl_tp_tdb_offset().
      if (getToolChain().getArch() == llvm::Triple::mipsel)
        CmdArgs.push_back("-lpnacl_legacy");

      CmdArgs.push_back("--end-group");
    }

    if (!Args.hasArg(options::OPT_nostartfiles)) {
      const char *crtend;
      if (Args.hasArg(options::OPT_shared))
        crtend = "crtendS.o";
      else
        crtend = "crtend.o";

      CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath(crtend)));
      CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crtn.o")));
    }
  }

  const char *Exec = Args.MakeArgString(ToolChain.GetLinkerPath());
  C.addCommand(std::make_unique<Command>(JA, *this,
                                         ResponseFileSupport::AtFileCurCP(),
                                         Exec, CmdArgs, Inputs, Output));
}

/// NaCl Toolchain
NaClToolChain::NaClToolChain(const Driver &D, const llvm::Triple &Triple,
                             const ArgList &Args)
    : Generic_ELF(D, Triple, Args) {

  // Remove paths added by Generic_GCC. NaCl Toolchain cannot use the
  // default paths, and must instead only use the paths provided
  // with this toolchain based on architecture.
  path_list &file_paths = getFilePaths();
  path_list &prog_paths = getProgramPaths();

  file_paths.clear();
  prog_paths.clear();

  // Path for library files (libc.a, ...)
  std::string FilePath(getDriver().Dir + "/../");

  // Path for tools (clang, ld, etc..)
  std::string ProgPath(getDriver().Dir + "/../");

  // Path for toolchain libraries (libgcc.a, ...)
  std::string ToolPath(getDriver().ResourceDir + "/lib/");

  switch (Triple.getArch()) {
  case llvm::Triple::x86:
    file_paths.push_back(FilePath + "x86_64-nacl/lib32");
    file_paths.push_back(FilePath + "i686-nacl/usr/lib");
    prog_paths.push_back(ProgPath + "x86_64-nacl/bin");
    file_paths.push_back(ToolPath + "i686-nacl");
    break;
  case llvm::Triple::x86_64:
    file_paths.push_back(FilePath + "x86_64-nacl/lib");
    file_paths.push_back(FilePath + "x86_64-nacl/usr/lib");
    prog_paths.push_back(ProgPath + "x86_64-nacl/bin");
    file_paths.push_back(ToolPath + "x86_64-nacl");
    break;
  case llvm::Triple::arm:
    file_paths.push_back(FilePath + "arm-nacl/lib");
    file_paths.push_back(FilePath + "arm-nacl/usr/lib");
    prog_paths.push_back(ProgPath + "arm-nacl/bin");
    file_paths.push_back(ToolPath + "arm-nacl");
    break;
  case llvm::Triple::mipsel:
    file_paths.push_back(FilePath + "mipsel-nacl/lib");
    file_paths.push_back(FilePath + "mipsel-nacl/usr/lib");
    prog_paths.push_back(ProgPath + "bin");
    file_paths.push_back(ToolPath + "mipsel-nacl");
    break;
  default:
    break;
  }

  NaClArmMacrosPath = GetFilePath("nacl-arm-macros.s");
}

void NaClToolChain::AddClangSystemIncludeArgs(const ArgList &DriverArgs,
                                              ArgStringList &CC1Args) const {
  const Driver &D = getDriver();
  if (DriverArgs.hasArg(clang::driver::options::OPT_nostdinc))
    return;

  if (!DriverArgs.hasArg(options::OPT_nobuiltininc)) {
    SmallString<128> P(D.ResourceDir);
    llvm::sys::path::append(P, "include");
    addSystemInclude(DriverArgs, CC1Args, P.str());
  }

  if (DriverArgs.hasArg(options::OPT_nostdlibinc))
    return;

  SmallString<128> P(D.Dir + "/../");
  switch (getTriple().getArch()) {
  case llvm::Triple::x86:
    // x86 is special because multilib style uses x86_64-nacl/include for libc
    // headers but the SDK wants i686-nacl/usr/include. The other architectures
    // have the same substring.
    llvm::sys::path::append(P, "i686-nacl/usr/include");
    addSystemInclude(DriverArgs, CC1Args, P.str());
    llvm::sys::path::remove_filename(P);
    llvm::sys::path::remove_filename(P);
    llvm::sys::path::remove_filename(P);
    llvm::sys::path::append(P, "x86_64-nacl/include");
    addSystemInclude(DriverArgs, CC1Args, P.str());
    return;
  case llvm::Triple::arm:
    llvm::sys::path::append(P, "arm-nacl/usr/include");
    break;
  case llvm::Triple::x86_64:
    llvm::sys::path::append(P, "x86_64-nacl/usr/include");
    break;
  case llvm::Triple::mipsel:
    llvm::sys::path::append(P, "mipsel-nacl/usr/include");
    break;
  default:
    return;
  }

  addSystemInclude(DriverArgs, CC1Args, P.str());
  llvm::sys::path::remove_filename(P);
  llvm::sys::path::remove_filename(P);
  llvm::sys::path::append(P, "include");
  addSystemInclude(DriverArgs, CC1Args, P.str());
}

void NaClToolChain::AddCXXStdlibLibArgs(const ArgList &Args,
                                        ArgStringList &CmdArgs) const {
  // Check for -stdlib= flags. We only support libc++ but this consumes the arg
  // if the value is libc++, and emits an error for other values.
  GetCXXStdlibType(Args);
  CmdArgs.push_back("-lc++");
  if (Args.hasArg(options::OPT_fexperimental_library))
    CmdArgs.push_back("-lc++experimental");
}

void NaClToolChain::addLibCxxIncludePaths(
    const llvm::opt::ArgList &DriverArgs,
    llvm::opt::ArgStringList &CC1Args) const {
  const Driver &D = getDriver();

  SmallString<128> P(D.Dir + "/../");
  switch (getTriple().getArch()) {
  default:
    break;
  case llvm::Triple::arm:
    llvm::sys::path::append(P, "arm-nacl/include/c++/v1");
    addSystemInclude(DriverArgs, CC1Args, P.str());
    break;
  case llvm::Triple::x86:
    llvm::sys::path::append(P, "x86_64-nacl/include/c++/v1");
    addSystemInclude(DriverArgs, CC1Args, P.str());
    break;
  case llvm::Triple::x86_64:
    llvm::sys::path::append(P, "x86_64-nacl/include/c++/v1");
    addSystemInclude(DriverArgs, CC1Args, P.str());
    break;
  case llvm::Triple::mipsel:
    llvm::sys::path::append(P, "mipsel-nacl/include/c++/v1");
    addSystemInclude(DriverArgs, CC1Args, P.str());
    break;
  }
}

ToolChain::CXXStdlibType
NaClToolChain::GetCXXStdlibType(const ArgList &Args) const {
  if (Arg *A = Args.getLastArg(options::OPT_stdlib_EQ)) {
    StringRef Value = A->getValue();
    if (Value == "libc++")
      return ToolChain::CST_Libcxx;
    getDriver().Diag(clang::diag::err_drv_invalid_stdlib_name)
        << A->getAsString(Args);
  }

  return ToolChain::CST_Libcxx;
}

std::string
NaClToolChain::ComputeEffectiveClangTriple(const ArgList &Args,
                                           types::ID InputType) const {
  llvm::Triple TheTriple(ComputeLLVMTriple(Args, InputType));
  if (TheTriple.getArch() == llvm::Triple::arm &&
      TheTriple.getEnvironment() == llvm::Triple::UnknownEnvironment)
    TheTriple.setEnvironment(llvm::Triple::GNUEABIHF);
  return TheTriple.getTriple();
}

Tool *NaClToolChain::buildLinker() const {
  return new tools::nacltools::Linker(*this);
}

Tool *NaClToolChain::buildAssembler() const {
  if (getTriple().getArch() == llvm::Triple::arm)
    return new tools::nacltools::AssemblerARM(*this);
  return new tools::gnutools::Assembler(*this);
}
