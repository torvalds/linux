//===--- Darwin.cpp - Darwin Tool and ToolChain Implementations -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Darwin.h"
#include "Arch/AArch64.h"
#include "Arch/ARM.h"
#include "CommonArgs.h"
#include "clang/Basic/AlignedAllocation.h"
#include "clang/Basic/ObjCRuntime.h"
#include "clang/Config/config.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "clang/Driver/SanitizerArgs.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Option/ArgList.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/TargetParser/TargetParser.h"
#include "llvm/TargetParser/Triple.h"
#include <cstdlib> // ::getenv

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang::driver::toolchains;
using namespace clang;
using namespace llvm::opt;

static VersionTuple minimumMacCatalystDeploymentTarget() {
  return VersionTuple(13, 1);
}

llvm::Triple::ArchType darwin::getArchTypeForMachOArchName(StringRef Str) {
  // See arch(3) and llvm-gcc's driver-driver.c. We don't implement support for
  // archs which Darwin doesn't use.

  // The matching this routine does is fairly pointless, since it is neither the
  // complete architecture list, nor a reasonable subset. The problem is that
  // historically the driver accepts this and also ties its -march=
  // handling to the architecture name, so we need to be careful before removing
  // support for it.

  // This code must be kept in sync with Clang's Darwin specific argument
  // translation.

  return llvm::StringSwitch<llvm::Triple::ArchType>(Str)
      .Cases("i386", "i486", "i486SX", "i586", "i686", llvm::Triple::x86)
      .Cases("pentium", "pentpro", "pentIIm3", "pentIIm5", "pentium4",
             llvm::Triple::x86)
      .Cases("x86_64", "x86_64h", llvm::Triple::x86_64)
      // This is derived from the driver.
      .Cases("arm", "armv4t", "armv5", "armv6", "armv6m", llvm::Triple::arm)
      .Cases("armv7", "armv7em", "armv7k", "armv7m", llvm::Triple::arm)
      .Cases("armv7s", "xscale", llvm::Triple::arm)
      .Cases("arm64", "arm64e", llvm::Triple::aarch64)
      .Case("arm64_32", llvm::Triple::aarch64_32)
      .Case("r600", llvm::Triple::r600)
      .Case("amdgcn", llvm::Triple::amdgcn)
      .Case("nvptx", llvm::Triple::nvptx)
      .Case("nvptx64", llvm::Triple::nvptx64)
      .Case("amdil", llvm::Triple::amdil)
      .Case("spir", llvm::Triple::spir)
      .Default(llvm::Triple::UnknownArch);
}

void darwin::setTripleTypeForMachOArchName(llvm::Triple &T, StringRef Str,
                                           const ArgList &Args) {
  const llvm::Triple::ArchType Arch = getArchTypeForMachOArchName(Str);
  llvm::ARM::ArchKind ArchKind = llvm::ARM::parseArch(Str);
  T.setArch(Arch);
  if (Arch != llvm::Triple::UnknownArch)
    T.setArchName(Str);

  if (ArchKind == llvm::ARM::ArchKind::ARMV6M ||
      ArchKind == llvm::ARM::ArchKind::ARMV7M ||
      ArchKind == llvm::ARM::ArchKind::ARMV7EM) {
    // Don't reject these -version-min= if we have the appropriate triple.
    if (T.getOS() == llvm::Triple::IOS)
      for (Arg *A : Args.filtered(options::OPT_mios_version_min_EQ))
        A->ignoreTargetSpecific();
    if (T.getOS() == llvm::Triple::WatchOS)
      for (Arg *A : Args.filtered(options::OPT_mwatchos_version_min_EQ))
        A->ignoreTargetSpecific();
    if (T.getOS() == llvm::Triple::TvOS)
      for (Arg *A : Args.filtered(options::OPT_mtvos_version_min_EQ))
        A->ignoreTargetSpecific();

    T.setOS(llvm::Triple::UnknownOS);
    T.setObjectFormat(llvm::Triple::MachO);
  }
}

void darwin::Assembler::ConstructJob(Compilation &C, const JobAction &JA,
                                     const InputInfo &Output,
                                     const InputInfoList &Inputs,
                                     const ArgList &Args,
                                     const char *LinkingOutput) const {
  const llvm::Triple &T(getToolChain().getTriple());

  ArgStringList CmdArgs;

  assert(Inputs.size() == 1 && "Unexpected number of inputs.");
  const InputInfo &Input = Inputs[0];

  // Determine the original source input.
  const Action *SourceAction = &JA;
  while (SourceAction->getKind() != Action::InputClass) {
    assert(!SourceAction->getInputs().empty() && "unexpected root action!");
    SourceAction = SourceAction->getInputs()[0];
  }

  // If -fno-integrated-as is used add -Q to the darwin assembler driver to make
  // sure it runs its system assembler not clang's integrated assembler.
  // Applicable to darwin11+ and Xcode 4+.  darwin<10 lacked integrated-as.
  // FIXME: at run-time detect assembler capabilities or rely on version
  // information forwarded by -target-assembler-version.
  if (Args.hasArg(options::OPT_fno_integrated_as)) {
    if (!(T.isMacOSX() && T.isMacOSXVersionLT(10, 7)))
      CmdArgs.push_back("-Q");
  }

  // Forward -g, assuming we are dealing with an actual assembly file.
  if (SourceAction->getType() == types::TY_Asm ||
      SourceAction->getType() == types::TY_PP_Asm) {
    if (Args.hasArg(options::OPT_gstabs))
      CmdArgs.push_back("--gstabs");
    else if (Args.hasArg(options::OPT_g_Group))
      CmdArgs.push_back("-g");
  }

  // Derived from asm spec.
  AddMachOArch(Args, CmdArgs);

  // Use -force_cpusubtype_ALL on x86 by default.
  if (T.isX86() || Args.hasArg(options::OPT_force__cpusubtype__ALL))
    CmdArgs.push_back("-force_cpusubtype_ALL");

  if (getToolChain().getArch() != llvm::Triple::x86_64 &&
      (((Args.hasArg(options::OPT_mkernel) ||
         Args.hasArg(options::OPT_fapple_kext)) &&
        getMachOToolChain().isKernelStatic()) ||
       Args.hasArg(options::OPT_static)))
    CmdArgs.push_back("-static");

  Args.AddAllArgValues(CmdArgs, options::OPT_Wa_COMMA, options::OPT_Xassembler);

  assert(Output.isFilename() && "Unexpected lipo output.");
  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  assert(Input.isFilename() && "Invalid input.");
  CmdArgs.push_back(Input.getFilename());

  // asm_final spec is empty.

  const char *Exec = Args.MakeArgString(getToolChain().GetProgramPath("as"));
  C.addCommand(std::make_unique<Command>(JA, *this, ResponseFileSupport::None(),
                                         Exec, CmdArgs, Inputs, Output));
}

void darwin::MachOTool::anchor() {}

void darwin::MachOTool::AddMachOArch(const ArgList &Args,
                                     ArgStringList &CmdArgs) const {
  StringRef ArchName = getMachOToolChain().getMachOArchName(Args);

  // Derived from darwin_arch spec.
  CmdArgs.push_back("-arch");
  CmdArgs.push_back(Args.MakeArgString(ArchName));

  // FIXME: Is this needed anymore?
  if (ArchName == "arm")
    CmdArgs.push_back("-force_cpusubtype_ALL");
}

bool darwin::Linker::NeedsTempPath(const InputInfoList &Inputs) const {
  // We only need to generate a temp path for LTO if we aren't compiling object
  // files. When compiling source files, we run 'dsymutil' after linking. We
  // don't run 'dsymutil' when compiling object files.
  for (const auto &Input : Inputs)
    if (Input.getType() != types::TY_Object)
      return true;

  return false;
}

/// Pass -no_deduplicate to ld64 under certain conditions:
///
/// - Either -O0 or -O1 is explicitly specified
/// - No -O option is specified *and* this is a compile+link (implicit -O0)
///
/// Also do *not* add -no_deduplicate when no -O option is specified and this
/// is just a link (we can't imply -O0)
static bool shouldLinkerNotDedup(bool IsLinkerOnlyAction, const ArgList &Args) {
  if (Arg *A = Args.getLastArg(options::OPT_O_Group)) {
    if (A->getOption().matches(options::OPT_O0))
      return true;
    if (A->getOption().matches(options::OPT_O))
      return llvm::StringSwitch<bool>(A->getValue())
                    .Case("1", true)
                    .Default(false);
    return false; // OPT_Ofast & OPT_O4
  }

  if (!IsLinkerOnlyAction) // Implicit -O0 for compile+linker only.
    return true;
  return false;
}

void darwin::Linker::AddLinkArgs(Compilation &C, const ArgList &Args,
                                 ArgStringList &CmdArgs,
                                 const InputInfoList &Inputs,
                                 VersionTuple Version, bool LinkerIsLLD,
                                 bool UsePlatformVersion) const {
  const Driver &D = getToolChain().getDriver();
  const toolchains::MachO &MachOTC = getMachOToolChain();

  // Newer linkers support -demangle. Pass it if supported and not disabled by
  // the user.
  if ((Version >= VersionTuple(100) || LinkerIsLLD) &&
      !Args.hasArg(options::OPT_Z_Xlinker__no_demangle))
    CmdArgs.push_back("-demangle");

  if (Args.hasArg(options::OPT_rdynamic) &&
      (Version >= VersionTuple(137) || LinkerIsLLD))
    CmdArgs.push_back("-export_dynamic");

  // If we are using App Extension restrictions, pass a flag to the linker
  // telling it that the compiled code has been audited.
  if (Args.hasFlag(options::OPT_fapplication_extension,
                   options::OPT_fno_application_extension, false))
    CmdArgs.push_back("-application_extension");

  if (D.isUsingLTO() && (Version >= VersionTuple(116) || LinkerIsLLD) &&
      NeedsTempPath(Inputs)) {
    std::string TmpPathName;
    if (D.getLTOMode() == LTOK_Full) {
      // If we are using full LTO, then automatically create a temporary file
      // path for the linker to use, so that it's lifetime will extend past a
      // possible dsymutil step.
      TmpPathName =
          D.GetTemporaryPath("cc", types::getTypeTempSuffix(types::TY_Object));
    } else if (D.getLTOMode() == LTOK_Thin)
      // If we are using thin LTO, then create a directory instead.
      TmpPathName = D.GetTemporaryDirectory("thinlto");

    if (!TmpPathName.empty()) {
      auto *TmpPath = C.getArgs().MakeArgString(TmpPathName);
      C.addTempFile(TmpPath);
      CmdArgs.push_back("-object_path_lto");
      CmdArgs.push_back(TmpPath);
    }
  }

  // Use -lto_library option to specify the libLTO.dylib path. Try to find
  // it in clang installed libraries. ld64 will only look at this argument
  // when it actually uses LTO, so libLTO.dylib only needs to exist at link
  // time if ld64 decides that it needs to use LTO.
  // Since this is passed unconditionally, ld64 will never look for libLTO.dylib
  // next to it. That's ok since ld64 using a libLTO.dylib not matching the
  // clang version won't work anyways.
  // lld is built at the same revision as clang and statically links in
  // LLVM libraries, so it doesn't need libLTO.dylib.
  if (Version >= VersionTuple(133) && !LinkerIsLLD) {
    // Search for libLTO in <InstalledDir>/../lib/libLTO.dylib
    StringRef P = llvm::sys::path::parent_path(D.Dir);
    SmallString<128> LibLTOPath(P);
    llvm::sys::path::append(LibLTOPath, "lib");
    llvm::sys::path::append(LibLTOPath, "libLTO.dylib");
    CmdArgs.push_back("-lto_library");
    CmdArgs.push_back(C.getArgs().MakeArgString(LibLTOPath));
  }

  // ld64 version 262 and above runs the deduplicate pass by default.
  // FIXME: lld doesn't dedup by default. Should we pass `--icf=safe`
  //        if `!shouldLinkerNotDedup()` if LinkerIsLLD here?
  if (Version >= VersionTuple(262) &&
      shouldLinkerNotDedup(C.getJobs().empty(), Args))
    CmdArgs.push_back("-no_deduplicate");

  // Derived from the "link" spec.
  Args.AddAllArgs(CmdArgs, options::OPT_static);
  if (!Args.hasArg(options::OPT_static))
    CmdArgs.push_back("-dynamic");
  if (Args.hasArg(options::OPT_fgnu_runtime)) {
    // FIXME: gcc replaces -lobjc in forward args with -lobjc-gnu
    // here. How do we wish to handle such things?
  }

  if (!Args.hasArg(options::OPT_dynamiclib)) {
    AddMachOArch(Args, CmdArgs);
    // FIXME: Why do this only on this path?
    Args.AddLastArg(CmdArgs, options::OPT_force__cpusubtype__ALL);

    Args.AddLastArg(CmdArgs, options::OPT_bundle);
    Args.AddAllArgs(CmdArgs, options::OPT_bundle__loader);
    Args.AddAllArgs(CmdArgs, options::OPT_client__name);

    Arg *A;
    if ((A = Args.getLastArg(options::OPT_compatibility__version)) ||
        (A = Args.getLastArg(options::OPT_current__version)) ||
        (A = Args.getLastArg(options::OPT_install__name)))
      D.Diag(diag::err_drv_argument_only_allowed_with) << A->getAsString(Args)
                                                       << "-dynamiclib";

    Args.AddLastArg(CmdArgs, options::OPT_force__flat__namespace);
    Args.AddLastArg(CmdArgs, options::OPT_keep__private__externs);
    Args.AddLastArg(CmdArgs, options::OPT_private__bundle);
  } else {
    CmdArgs.push_back("-dylib");

    Arg *A;
    if ((A = Args.getLastArg(options::OPT_bundle)) ||
        (A = Args.getLastArg(options::OPT_bundle__loader)) ||
        (A = Args.getLastArg(options::OPT_client__name)) ||
        (A = Args.getLastArg(options::OPT_force__flat__namespace)) ||
        (A = Args.getLastArg(options::OPT_keep__private__externs)) ||
        (A = Args.getLastArg(options::OPT_private__bundle)))
      D.Diag(diag::err_drv_argument_not_allowed_with) << A->getAsString(Args)
                                                      << "-dynamiclib";

    Args.AddAllArgsTranslated(CmdArgs, options::OPT_compatibility__version,
                              "-dylib_compatibility_version");
    Args.AddAllArgsTranslated(CmdArgs, options::OPT_current__version,
                              "-dylib_current_version");

    AddMachOArch(Args, CmdArgs);

    Args.AddAllArgsTranslated(CmdArgs, options::OPT_install__name,
                              "-dylib_install_name");
  }

  Args.AddLastArg(CmdArgs, options::OPT_all__load);
  Args.AddAllArgs(CmdArgs, options::OPT_allowable__client);
  Args.AddLastArg(CmdArgs, options::OPT_bind__at__load);
  if (MachOTC.isTargetIOSBased())
    Args.AddLastArg(CmdArgs, options::OPT_arch__errors__fatal);
  Args.AddLastArg(CmdArgs, options::OPT_dead__strip);
  Args.AddLastArg(CmdArgs, options::OPT_no__dead__strip__inits__and__terms);
  Args.AddAllArgs(CmdArgs, options::OPT_dylib__file);
  Args.AddLastArg(CmdArgs, options::OPT_dynamic);
  Args.AddAllArgs(CmdArgs, options::OPT_exported__symbols__list);
  Args.AddLastArg(CmdArgs, options::OPT_flat__namespace);
  Args.AddAllArgs(CmdArgs, options::OPT_force__load);
  Args.AddAllArgs(CmdArgs, options::OPT_headerpad__max__install__names);
  Args.AddAllArgs(CmdArgs, options::OPT_image__base);
  Args.AddAllArgs(CmdArgs, options::OPT_init);

  // Add the deployment target.
  if (Version >= VersionTuple(520) || LinkerIsLLD || UsePlatformVersion)
    MachOTC.addPlatformVersionArgs(Args, CmdArgs);
  else
    MachOTC.addMinVersionArgs(Args, CmdArgs);

  Args.AddLastArg(CmdArgs, options::OPT_nomultidefs);
  Args.AddLastArg(CmdArgs, options::OPT_multi__module);
  Args.AddLastArg(CmdArgs, options::OPT_single__module);
  Args.AddAllArgs(CmdArgs, options::OPT_multiply__defined);
  Args.AddAllArgs(CmdArgs, options::OPT_multiply__defined__unused);

  if (const Arg *A =
          Args.getLastArg(options::OPT_fpie, options::OPT_fPIE,
                          options::OPT_fno_pie, options::OPT_fno_PIE)) {
    if (A->getOption().matches(options::OPT_fpie) ||
        A->getOption().matches(options::OPT_fPIE))
      CmdArgs.push_back("-pie");
    else
      CmdArgs.push_back("-no_pie");
  }

  // for embed-bitcode, use -bitcode_bundle in linker command
  if (C.getDriver().embedBitcodeEnabled()) {
    // Check if the toolchain supports bitcode build flow.
    if (MachOTC.SupportsEmbeddedBitcode()) {
      CmdArgs.push_back("-bitcode_bundle");
      // FIXME: Pass this if LinkerIsLLD too, once it implements this flag.
      if (C.getDriver().embedBitcodeMarkerOnly() &&
          Version >= VersionTuple(278)) {
        CmdArgs.push_back("-bitcode_process_mode");
        CmdArgs.push_back("marker");
      }
    } else
      D.Diag(diag::err_drv_bitcode_unsupported_on_toolchain);
  }

  // If GlobalISel is enabled, pass it through to LLVM.
  if (Arg *A = Args.getLastArg(options::OPT_fglobal_isel,
                               options::OPT_fno_global_isel)) {
    if (A->getOption().matches(options::OPT_fglobal_isel)) {
      CmdArgs.push_back("-mllvm");
      CmdArgs.push_back("-global-isel");
      // Disable abort and fall back to SDAG silently.
      CmdArgs.push_back("-mllvm");
      CmdArgs.push_back("-global-isel-abort=0");
    }
  }

  if (Args.hasArg(options::OPT_mkernel) ||
      Args.hasArg(options::OPT_fapple_kext) ||
      Args.hasArg(options::OPT_ffreestanding)) {
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back("-disable-atexit-based-global-dtor-lowering");
  }

  Args.AddLastArg(CmdArgs, options::OPT_prebind);
  Args.AddLastArg(CmdArgs, options::OPT_noprebind);
  Args.AddLastArg(CmdArgs, options::OPT_nofixprebinding);
  Args.AddLastArg(CmdArgs, options::OPT_prebind__all__twolevel__modules);
  Args.AddLastArg(CmdArgs, options::OPT_read__only__relocs);
  Args.AddAllArgs(CmdArgs, options::OPT_sectcreate);
  Args.AddAllArgs(CmdArgs, options::OPT_sectorder);
  Args.AddAllArgs(CmdArgs, options::OPT_seg1addr);
  Args.AddAllArgs(CmdArgs, options::OPT_segprot);
  Args.AddAllArgs(CmdArgs, options::OPT_segaddr);
  Args.AddAllArgs(CmdArgs, options::OPT_segs__read__only__addr);
  Args.AddAllArgs(CmdArgs, options::OPT_segs__read__write__addr);
  Args.AddAllArgs(CmdArgs, options::OPT_seg__addr__table);
  Args.AddAllArgs(CmdArgs, options::OPT_seg__addr__table__filename);
  Args.AddAllArgs(CmdArgs, options::OPT_sub__library);
  Args.AddAllArgs(CmdArgs, options::OPT_sub__umbrella);

  // Give --sysroot= preference, over the Apple specific behavior to also use
  // --isysroot as the syslibroot.
  StringRef sysroot = C.getSysRoot();
  if (sysroot != "") {
    CmdArgs.push_back("-syslibroot");
    CmdArgs.push_back(C.getArgs().MakeArgString(sysroot));
  } else if (const Arg *A = Args.getLastArg(options::OPT_isysroot)) {
    CmdArgs.push_back("-syslibroot");
    CmdArgs.push_back(A->getValue());
  }

  Args.AddLastArg(CmdArgs, options::OPT_twolevel__namespace);
  Args.AddLastArg(CmdArgs, options::OPT_twolevel__namespace__hints);
  Args.AddAllArgs(CmdArgs, options::OPT_umbrella);
  Args.AddAllArgs(CmdArgs, options::OPT_undefined);
  Args.AddAllArgs(CmdArgs, options::OPT_unexported__symbols__list);
  Args.AddAllArgs(CmdArgs, options::OPT_weak__reference__mismatches);
  Args.AddLastArg(CmdArgs, options::OPT_X_Flag);
  Args.AddAllArgs(CmdArgs, options::OPT_y);
  Args.AddLastArg(CmdArgs, options::OPT_w);
  Args.AddAllArgs(CmdArgs, options::OPT_pagezero__size);
  Args.AddAllArgs(CmdArgs, options::OPT_segs__read__);
  Args.AddLastArg(CmdArgs, options::OPT_seglinkedit);
  Args.AddLastArg(CmdArgs, options::OPT_noseglinkedit);
  Args.AddAllArgs(CmdArgs, options::OPT_sectalign);
  Args.AddAllArgs(CmdArgs, options::OPT_sectobjectsymbols);
  Args.AddAllArgs(CmdArgs, options::OPT_segcreate);
  Args.AddLastArg(CmdArgs, options::OPT_why_load);
  Args.AddLastArg(CmdArgs, options::OPT_whatsloaded);
  Args.AddAllArgs(CmdArgs, options::OPT_dylinker__install__name);
  Args.AddLastArg(CmdArgs, options::OPT_dylinker);
  Args.AddLastArg(CmdArgs, options::OPT_Mach);

  if (LinkerIsLLD) {
    if (auto *CSPGOGenerateArg = getLastCSProfileGenerateArg(Args)) {
      SmallString<128> Path(CSPGOGenerateArg->getNumValues() == 0
                                ? ""
                                : CSPGOGenerateArg->getValue());
      llvm::sys::path::append(Path, "default_%m.profraw");
      CmdArgs.push_back("--cs-profile-generate");
      CmdArgs.push_back(Args.MakeArgString(Twine("--cs-profile-path=") + Path));
    } else if (auto *ProfileUseArg = getLastProfileUseArg(Args)) {
      SmallString<128> Path(
          ProfileUseArg->getNumValues() == 0 ? "" : ProfileUseArg->getValue());
      if (Path.empty() || llvm::sys::fs::is_directory(Path))
        llvm::sys::path::append(Path, "default.profdata");
      CmdArgs.push_back(Args.MakeArgString(Twine("--cs-profile-path=") + Path));
    }
  }
}

/// Determine whether we are linking the ObjC runtime.
static bool isObjCRuntimeLinked(const ArgList &Args) {
  if (isObjCAutoRefCount(Args)) {
    Args.ClaimAllArgs(options::OPT_fobjc_link_runtime);
    return true;
  }
  return Args.hasArg(options::OPT_fobjc_link_runtime);
}

static bool checkRemarksOptions(const Driver &D, const ArgList &Args,
                                const llvm::Triple &Triple) {
  // When enabling remarks, we need to error if:
  // * The remark file is specified but we're targeting multiple architectures,
  // which means more than one remark file is being generated.
  bool hasMultipleInvocations =
      Args.getAllArgValues(options::OPT_arch).size() > 1;
  bool hasExplicitOutputFile =
      Args.getLastArg(options::OPT_foptimization_record_file_EQ);
  if (hasMultipleInvocations && hasExplicitOutputFile) {
    D.Diag(diag::err_drv_invalid_output_with_multiple_archs)
        << "-foptimization-record-file";
    return false;
  }
  return true;
}

static void renderRemarksOptions(const ArgList &Args, ArgStringList &CmdArgs,
                                 const llvm::Triple &Triple,
                                 const InputInfo &Output, const JobAction &JA) {
  StringRef Format = "yaml";
  if (const Arg *A = Args.getLastArg(options::OPT_fsave_optimization_record_EQ))
    Format = A->getValue();

  CmdArgs.push_back("-mllvm");
  CmdArgs.push_back("-lto-pass-remarks-output");
  CmdArgs.push_back("-mllvm");

  const Arg *A = Args.getLastArg(options::OPT_foptimization_record_file_EQ);
  if (A) {
    CmdArgs.push_back(A->getValue());
  } else {
    assert(Output.isFilename() && "Unexpected ld output.");
    SmallString<128> F;
    F = Output.getFilename();
    F += ".opt.";
    F += Format;

    CmdArgs.push_back(Args.MakeArgString(F));
  }

  if (const Arg *A =
          Args.getLastArg(options::OPT_foptimization_record_passes_EQ)) {
    CmdArgs.push_back("-mllvm");
    std::string Passes =
        std::string("-lto-pass-remarks-filter=") + A->getValue();
    CmdArgs.push_back(Args.MakeArgString(Passes));
  }

  if (!Format.empty()) {
    CmdArgs.push_back("-mllvm");
    Twine FormatArg = Twine("-lto-pass-remarks-format=") + Format;
    CmdArgs.push_back(Args.MakeArgString(FormatArg));
  }

  if (getLastProfileUseArg(Args)) {
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back("-lto-pass-remarks-with-hotness");

    if (const Arg *A =
            Args.getLastArg(options::OPT_fdiagnostics_hotness_threshold_EQ)) {
      CmdArgs.push_back("-mllvm");
      std::string Opt =
          std::string("-lto-pass-remarks-hotness-threshold=") + A->getValue();
      CmdArgs.push_back(Args.MakeArgString(Opt));
    }
  }
}

static void AppendPlatformPrefix(SmallString<128> &Path, const llvm::Triple &T);

void darwin::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                  const InputInfo &Output,
                                  const InputInfoList &Inputs,
                                  const ArgList &Args,
                                  const char *LinkingOutput) const {
  assert(Output.getType() == types::TY_Image && "Invalid linker output type.");

  // If the number of arguments surpasses the system limits, we will encode the
  // input files in a separate file, shortening the command line. To this end,
  // build a list of input file names that can be passed via a file with the
  // -filelist linker option.
  llvm::opt::ArgStringList InputFileList;

  // The logic here is derived from gcc's behavior; most of which
  // comes from specs (starting with link_command). Consult gcc for
  // more information.
  ArgStringList CmdArgs;

  /// Hack(tm) to ignore linking errors when we are doing ARC migration.
  if (Args.hasArg(options::OPT_ccc_arcmt_check,
                  options::OPT_ccc_arcmt_migrate)) {
    for (const auto &Arg : Args)
      Arg->claim();
    const char *Exec =
        Args.MakeArgString(getToolChain().GetProgramPath("touch"));
    CmdArgs.push_back(Output.getFilename());
    C.addCommand(std::make_unique<Command>(JA, *this,
                                           ResponseFileSupport::None(), Exec,
                                           CmdArgs, std::nullopt, Output));
    return;
  }

  VersionTuple Version = getMachOToolChain().getLinkerVersion(Args);

  bool LinkerIsLLD;
  const char *Exec =
      Args.MakeArgString(getToolChain().GetLinkerPath(&LinkerIsLLD));

  // xrOS always uses -platform-version.
  bool UsePlatformVersion = getToolChain().getTriple().isXROS();

  // I'm not sure why this particular decomposition exists in gcc, but
  // we follow suite for ease of comparison.
  AddLinkArgs(C, Args, CmdArgs, Inputs, Version, LinkerIsLLD,
              UsePlatformVersion);

  if (willEmitRemarks(Args) &&
      checkRemarksOptions(getToolChain().getDriver(), Args,
                          getToolChain().getTriple()))
    renderRemarksOptions(Args, CmdArgs, getToolChain().getTriple(), Output, JA);

  // Propagate the -moutline flag to the linker in LTO.
  if (Arg *A =
          Args.getLastArg(options::OPT_moutline, options::OPT_mno_outline)) {
    if (A->getOption().matches(options::OPT_moutline)) {
      if (getMachOToolChain().getMachOArchName(Args) == "arm64") {
        CmdArgs.push_back("-mllvm");
        CmdArgs.push_back("-enable-machine-outliner");
      }
    } else {
      // Disable all outlining behaviour if we have mno-outline. We need to do
      // this explicitly, because targets which support default outlining will
      // try to do work if we don't.
      CmdArgs.push_back("-mllvm");
      CmdArgs.push_back("-enable-machine-outliner=never");
    }
  }

  // Outline from linkonceodr functions by default in LTO, whenever the outliner
  // is enabled.  Note that the target may enable the machine outliner
  // independently of -moutline.
  CmdArgs.push_back("-mllvm");
  CmdArgs.push_back("-enable-linkonceodr-outlining");

  // Setup statistics file output.
  SmallString<128> StatsFile =
      getStatsFileName(Args, Output, Inputs[0], getToolChain().getDriver());
  if (!StatsFile.empty()) {
    CmdArgs.push_back("-mllvm");
    CmdArgs.push_back(Args.MakeArgString("-lto-stats-file=" + StatsFile.str()));
  }

  // It seems that the 'e' option is completely ignored for dynamic executables
  // (the default), and with static executables, the last one wins, as expected.
  Args.addAllArgs(CmdArgs, {options::OPT_d_Flag, options::OPT_s, options::OPT_t,
                            options::OPT_Z_Flag, options::OPT_u_Group});

  // Forward -ObjC when either -ObjC or -ObjC++ is used, to force loading
  // members of static archive libraries which implement Objective-C classes or
  // categories.
  if (Args.hasArg(options::OPT_ObjC) || Args.hasArg(options::OPT_ObjCXX))
    CmdArgs.push_back("-ObjC");

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles))
    getMachOToolChain().addStartObjectFileArgs(Args, CmdArgs);

  Args.AddAllArgs(CmdArgs, options::OPT_L);

  AddLinkerInputs(getToolChain(), Inputs, Args, CmdArgs, JA);
  // Build the input file for -filelist (list of linker input files) in case we
  // need it later
  for (const auto &II : Inputs) {
    if (!II.isFilename()) {
      // This is a linker input argument.
      // We cannot mix input arguments and file names in a -filelist input, thus
      // we prematurely stop our list (remaining files shall be passed as
      // arguments).
      if (InputFileList.size() > 0)
        break;

      continue;
    }

    InputFileList.push_back(II.getFilename());
  }

  // Additional linker set-up and flags for Fortran. This is required in order
  // to generate executables.
  if (getToolChain().getDriver().IsFlangMode()) {
    addFortranRuntimeLibraryPath(getToolChain(), Args, CmdArgs);
    addFortranRuntimeLibs(getToolChain(), Args, CmdArgs);
  }

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs))
    addOpenMPRuntime(C, CmdArgs, getToolChain(), Args);

  if (isObjCRuntimeLinked(Args) &&
      !Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs)) {
    // We use arclite library for both ARC and subscripting support.
    getMachOToolChain().AddLinkARCArgs(Args, CmdArgs);

    CmdArgs.push_back("-framework");
    CmdArgs.push_back("Foundation");
    // Link libobj.
    CmdArgs.push_back("-lobjc");
  }

  if (LinkingOutput) {
    CmdArgs.push_back("-arch_multiple");
    CmdArgs.push_back("-final_output");
    CmdArgs.push_back(LinkingOutput);
  }

  if (Args.hasArg(options::OPT_fnested_functions))
    CmdArgs.push_back("-allow_stack_execute");

  getMachOToolChain().addProfileRTLibs(Args, CmdArgs);

  StringRef Parallelism = getLTOParallelism(Args, getToolChain().getDriver());
  if (!Parallelism.empty()) {
    CmdArgs.push_back("-mllvm");
    unsigned NumThreads =
        llvm::get_threadpool_strategy(Parallelism)->compute_thread_count();
    CmdArgs.push_back(Args.MakeArgString("-threads=" + Twine(NumThreads)));
  }

  if (getToolChain().ShouldLinkCXXStdlib(Args))
    getToolChain().AddCXXStdlibLibArgs(Args, CmdArgs);

  bool NoStdOrDefaultLibs =
      Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs);
  bool ForceLinkBuiltins = Args.hasArg(options::OPT_fapple_link_rtlib);
  if (!NoStdOrDefaultLibs || ForceLinkBuiltins) {
    // link_ssp spec is empty.

    // If we have both -nostdlib/nodefaultlibs and -fapple-link-rtlib then
    // we just want to link the builtins, not the other libs like libSystem.
    if (NoStdOrDefaultLibs && ForceLinkBuiltins) {
      getMachOToolChain().AddLinkRuntimeLib(Args, CmdArgs, "builtins");
    } else {
      // Let the tool chain choose which runtime library to link.
      getMachOToolChain().AddLinkRuntimeLibArgs(Args, CmdArgs,
                                                ForceLinkBuiltins);

      // No need to do anything for pthreads. Claim argument to avoid warning.
      Args.ClaimAllArgs(options::OPT_pthread);
      Args.ClaimAllArgs(options::OPT_pthreads);
    }
  }

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles)) {
    // endfile_spec is empty.
  }

  Args.AddAllArgs(CmdArgs, options::OPT_T_Group);
  Args.AddAllArgs(CmdArgs, options::OPT_F);

  // -iframework should be forwarded as -F.
  for (const Arg *A : Args.filtered(options::OPT_iframework))
    CmdArgs.push_back(Args.MakeArgString(std::string("-F") + A->getValue()));

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs)) {
    if (Arg *A = Args.getLastArg(options::OPT_fveclib)) {
      if (A->getValue() == StringRef("Accelerate")) {
        CmdArgs.push_back("-framework");
        CmdArgs.push_back("Accelerate");
      }
    }
  }

  // Add non-standard, platform-specific search paths, e.g., for DriverKit:
  //  -L<sysroot>/System/DriverKit/usr/lib
  //  -F<sysroot>/System/DriverKit/System/Library/Framework
  {
    bool NonStandardSearchPath = false;
    const auto &Triple = getToolChain().getTriple();
    if (Triple.isDriverKit()) {
      // ld64 fixed the implicit -F and -L paths in ld64-605.1+.
      NonStandardSearchPath =
          Version.getMajor() < 605 ||
          (Version.getMajor() == 605 && Version.getMinor().value_or(0) < 1);
    }

    if (NonStandardSearchPath) {
      if (auto *Sysroot = Args.getLastArg(options::OPT_isysroot)) {
        auto AddSearchPath = [&](StringRef Flag, StringRef SearchPath) {
          SmallString<128> P(Sysroot->getValue());
          AppendPlatformPrefix(P, Triple);
          llvm::sys::path::append(P, SearchPath);
          if (getToolChain().getVFS().exists(P)) {
            CmdArgs.push_back(Args.MakeArgString(Flag + P));
          }
        };
        AddSearchPath("-L", "/usr/lib");
        AddSearchPath("-F", "/System/Library/Frameworks");
      }
    }
  }

  ResponseFileSupport ResponseSupport;
  if (Version >= VersionTuple(705) || LinkerIsLLD) {
    ResponseSupport = ResponseFileSupport::AtFileUTF8();
  } else {
    // For older versions of the linker, use the legacy filelist method instead.
    ResponseSupport = {ResponseFileSupport::RF_FileList, llvm::sys::WEM_UTF8,
                       "-filelist"};
  }

  std::unique_ptr<Command> Cmd = std::make_unique<Command>(
      JA, *this, ResponseSupport, Exec, CmdArgs, Inputs, Output);
  Cmd->setInputFileList(std::move(InputFileList));
  C.addCommand(std::move(Cmd));
}

void darwin::StaticLibTool::ConstructJob(Compilation &C, const JobAction &JA,
                                         const InputInfo &Output,
                                         const InputInfoList &Inputs,
                                         const ArgList &Args,
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

  // libtool <options> <output_file> <input_files>
  ArgStringList CmdArgs;
  // Create and insert file members with a deterministic index.
  CmdArgs.push_back("-static");
  CmdArgs.push_back("-D");
  CmdArgs.push_back("-no_warning_for_no_symbols");
  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  for (const auto &II : Inputs) {
    if (II.isFilename()) {
      CmdArgs.push_back(II.getFilename());
    }
  }

  // Delete old output archive file if it already exists before generating a new
  // archive file.
  const auto *OutputFileName = Output.getFilename();
  if (Output.isFilename() && llvm::sys::fs::exists(OutputFileName)) {
    if (std::error_code EC = llvm::sys::fs::remove(OutputFileName)) {
      D.Diag(diag::err_drv_unable_to_remove_file) << EC.message();
      return;
    }
  }

  const char *Exec = Args.MakeArgString(getToolChain().GetStaticLibToolPath());
  C.addCommand(std::make_unique<Command>(JA, *this,
                                         ResponseFileSupport::AtFileUTF8(),
                                         Exec, CmdArgs, Inputs, Output));
}

void darwin::Lipo::ConstructJob(Compilation &C, const JobAction &JA,
                                const InputInfo &Output,
                                const InputInfoList &Inputs,
                                const ArgList &Args,
                                const char *LinkingOutput) const {
  ArgStringList CmdArgs;

  CmdArgs.push_back("-create");
  assert(Output.isFilename() && "Unexpected lipo output.");

  CmdArgs.push_back("-output");
  CmdArgs.push_back(Output.getFilename());

  for (const auto &II : Inputs) {
    assert(II.isFilename() && "Unexpected lipo input.");
    CmdArgs.push_back(II.getFilename());
  }

  const char *Exec = Args.MakeArgString(getToolChain().GetProgramPath("lipo"));
  C.addCommand(std::make_unique<Command>(JA, *this, ResponseFileSupport::None(),
                                         Exec, CmdArgs, Inputs, Output));
}

void darwin::Dsymutil::ConstructJob(Compilation &C, const JobAction &JA,
                                    const InputInfo &Output,
                                    const InputInfoList &Inputs,
                                    const ArgList &Args,
                                    const char *LinkingOutput) const {
  ArgStringList CmdArgs;

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  assert(Inputs.size() == 1 && "Unable to handle multiple inputs.");
  const InputInfo &Input = Inputs[0];
  assert(Input.isFilename() && "Unexpected dsymutil input.");
  CmdArgs.push_back(Input.getFilename());

  const char *Exec =
      Args.MakeArgString(getToolChain().GetProgramPath("dsymutil"));
  C.addCommand(std::make_unique<Command>(JA, *this, ResponseFileSupport::None(),
                                         Exec, CmdArgs, Inputs, Output));
}

void darwin::VerifyDebug::ConstructJob(Compilation &C, const JobAction &JA,
                                       const InputInfo &Output,
                                       const InputInfoList &Inputs,
                                       const ArgList &Args,
                                       const char *LinkingOutput) const {
  ArgStringList CmdArgs;
  CmdArgs.push_back("--verify");
  CmdArgs.push_back("--debug-info");
  CmdArgs.push_back("--eh-frame");
  CmdArgs.push_back("--quiet");

  assert(Inputs.size() == 1 && "Unable to handle multiple inputs.");
  const InputInfo &Input = Inputs[0];
  assert(Input.isFilename() && "Unexpected verify input");

  // Grabbing the output of the earlier dsymutil run.
  CmdArgs.push_back(Input.getFilename());

  const char *Exec =
      Args.MakeArgString(getToolChain().GetProgramPath("dwarfdump"));
  C.addCommand(std::make_unique<Command>(JA, *this, ResponseFileSupport::None(),
                                         Exec, CmdArgs, Inputs, Output));
}

MachO::MachO(const Driver &D, const llvm::Triple &Triple, const ArgList &Args)
    : ToolChain(D, Triple, Args) {
  // We expect 'as', 'ld', etc. to be adjacent to our install dir.
  getProgramPaths().push_back(getDriver().Dir);
}

/// Darwin - Darwin tool chain for i386 and x86_64.
Darwin::Darwin(const Driver &D, const llvm::Triple &Triple, const ArgList &Args)
    : MachO(D, Triple, Args), TargetInitialized(false),
      CudaInstallation(D, Triple, Args), RocmInstallation(D, Triple, Args) {}

types::ID MachO::LookupTypeForExtension(StringRef Ext) const {
  types::ID Ty = ToolChain::LookupTypeForExtension(Ext);

  // Darwin always preprocesses assembly files (unless -x is used explicitly).
  if (Ty == types::TY_PP_Asm)
    return types::TY_Asm;

  return Ty;
}

bool MachO::HasNativeLLVMSupport() const { return true; }

ToolChain::CXXStdlibType Darwin::GetDefaultCXXStdlibType() const {
  // Always use libc++ by default
  return ToolChain::CST_Libcxx;
}

/// Darwin provides an ARC runtime starting in MacOS X 10.7 and iOS 5.0.
ObjCRuntime Darwin::getDefaultObjCRuntime(bool isNonFragile) const {
  if (isTargetWatchOSBased())
    return ObjCRuntime(ObjCRuntime::WatchOS, TargetVersion);
  if (isTargetIOSBased())
    return ObjCRuntime(ObjCRuntime::iOS, TargetVersion);
  if (isTargetXROS()) {
    // XROS uses the iOS runtime.
    auto T = llvm::Triple(Twine("arm64-apple-") +
                          llvm::Triple::getOSTypeName(llvm::Triple::XROS) +
                          TargetVersion.getAsString());
    return ObjCRuntime(ObjCRuntime::iOS, T.getiOSVersion());
  }
  if (isNonFragile)
    return ObjCRuntime(ObjCRuntime::MacOSX, TargetVersion);
  return ObjCRuntime(ObjCRuntime::FragileMacOSX, TargetVersion);
}

/// Darwin provides a blocks runtime starting in MacOS X 10.6 and iOS 3.2.
bool Darwin::hasBlocksRuntime() const {
  if (isTargetWatchOSBased() || isTargetDriverKit() || isTargetXROS())
    return true;
  else if (isTargetIOSBased())
    return !isIPhoneOSVersionLT(3, 2);
  else {
    assert(isTargetMacOSBased() && "unexpected darwin target");
    return !isMacosxVersionLT(10, 6);
  }
}

void Darwin::AddCudaIncludeArgs(const ArgList &DriverArgs,
                                ArgStringList &CC1Args) const {
  CudaInstallation->AddCudaIncludeArgs(DriverArgs, CC1Args);
}

void Darwin::AddHIPIncludeArgs(const ArgList &DriverArgs,
                               ArgStringList &CC1Args) const {
  RocmInstallation->AddHIPIncludeArgs(DriverArgs, CC1Args);
}

// This is just a MachO name translation routine and there's no
// way to join this into ARMTargetParser without breaking all
// other assumptions. Maybe MachO should consider standardising
// their nomenclature.
static const char *ArmMachOArchName(StringRef Arch) {
  return llvm::StringSwitch<const char *>(Arch)
      .Case("armv6k", "armv6")
      .Case("armv6m", "armv6m")
      .Case("armv5tej", "armv5")
      .Case("xscale", "xscale")
      .Case("armv4t", "armv4t")
      .Case("armv7", "armv7")
      .Cases("armv7a", "armv7-a", "armv7")
      .Cases("armv7r", "armv7-r", "armv7")
      .Cases("armv7em", "armv7e-m", "armv7em")
      .Cases("armv7k", "armv7-k", "armv7k")
      .Cases("armv7m", "armv7-m", "armv7m")
      .Cases("armv7s", "armv7-s", "armv7s")
      .Default(nullptr);
}

static const char *ArmMachOArchNameCPU(StringRef CPU) {
  llvm::ARM::ArchKind ArchKind = llvm::ARM::parseCPUArch(CPU);
  if (ArchKind == llvm::ARM::ArchKind::INVALID)
    return nullptr;
  StringRef Arch = llvm::ARM::getArchName(ArchKind);

  // FIXME: Make sure this MachO triple mangling is really necessary.
  // ARMv5* normalises to ARMv5.
  if (Arch.starts_with("armv5"))
    Arch = Arch.substr(0, 5);
  // ARMv6*, except ARMv6M, normalises to ARMv6.
  else if (Arch.starts_with("armv6") && !Arch.ends_with("6m"))
    Arch = Arch.substr(0, 5);
  // ARMv7A normalises to ARMv7.
  else if (Arch.ends_with("v7a"))
    Arch = Arch.substr(0, 5);
  return Arch.data();
}

StringRef MachO::getMachOArchName(const ArgList &Args) const {
  switch (getTriple().getArch()) {
  default:
    return getDefaultUniversalArchName();

  case llvm::Triple::aarch64_32:
    return "arm64_32";

  case llvm::Triple::aarch64: {
    if (getTriple().isArm64e())
      return "arm64e";
    return "arm64";
  }

  case llvm::Triple::thumb:
  case llvm::Triple::arm:
    if (const Arg *A = Args.getLastArg(clang::driver::options::OPT_march_EQ))
      if (const char *Arch = ArmMachOArchName(A->getValue()))
        return Arch;

    if (const Arg *A = Args.getLastArg(options::OPT_mcpu_EQ))
      if (const char *Arch = ArmMachOArchNameCPU(A->getValue()))
        return Arch;

    return "arm";
  }
}

VersionTuple MachO::getLinkerVersion(const llvm::opt::ArgList &Args) const {
  if (LinkerVersion) {
#ifndef NDEBUG
    VersionTuple NewLinkerVersion;
    if (Arg *A = Args.getLastArg(options::OPT_mlinker_version_EQ))
      (void)NewLinkerVersion.tryParse(A->getValue());
    assert(NewLinkerVersion == LinkerVersion);
#endif
    return *LinkerVersion;
  }

  VersionTuple NewLinkerVersion;
  if (Arg *A = Args.getLastArg(options::OPT_mlinker_version_EQ))
    if (NewLinkerVersion.tryParse(A->getValue()))
      getDriver().Diag(diag::err_drv_invalid_version_number)
        << A->getAsString(Args);

  LinkerVersion = NewLinkerVersion;
  return *LinkerVersion;
}

Darwin::~Darwin() {}

MachO::~MachO() {}

std::string Darwin::ComputeEffectiveClangTriple(const ArgList &Args,
                                                types::ID InputType) const {
  llvm::Triple Triple(ComputeLLVMTriple(Args, InputType));

  // If the target isn't initialized (e.g., an unknown Darwin platform, return
  // the default triple).
  if (!isTargetInitialized())
    return Triple.getTriple();

  SmallString<16> Str;
  if (isTargetWatchOSBased())
    Str += "watchos";
  else if (isTargetTvOSBased())
    Str += "tvos";
  else if (isTargetDriverKit())
    Str += "driverkit";
  else if (isTargetIOSBased() || isTargetMacCatalyst())
    Str += "ios";
  else if (isTargetXROS())
    Str += llvm::Triple::getOSTypeName(llvm::Triple::XROS);
  else
    Str += "macosx";
  Str += getTripleTargetVersion().getAsString();
  Triple.setOSName(Str);

  return Triple.getTriple();
}

Tool *MachO::getTool(Action::ActionClass AC) const {
  switch (AC) {
  case Action::LipoJobClass:
    if (!Lipo)
      Lipo.reset(new tools::darwin::Lipo(*this));
    return Lipo.get();
  case Action::DsymutilJobClass:
    if (!Dsymutil)
      Dsymutil.reset(new tools::darwin::Dsymutil(*this));
    return Dsymutil.get();
  case Action::VerifyDebugInfoJobClass:
    if (!VerifyDebug)
      VerifyDebug.reset(new tools::darwin::VerifyDebug(*this));
    return VerifyDebug.get();
  default:
    return ToolChain::getTool(AC);
  }
}

Tool *MachO::buildLinker() const { return new tools::darwin::Linker(*this); }

Tool *MachO::buildStaticLibTool() const {
  return new tools::darwin::StaticLibTool(*this);
}

Tool *MachO::buildAssembler() const {
  return new tools::darwin::Assembler(*this);
}

DarwinClang::DarwinClang(const Driver &D, const llvm::Triple &Triple,
                         const ArgList &Args)
    : Darwin(D, Triple, Args) {}

void DarwinClang::addClangWarningOptions(ArgStringList &CC1Args) const {
  // Always error about undefined 'TARGET_OS_*' macros.
  CC1Args.push_back("-Wundef-prefix=TARGET_OS_");
  CC1Args.push_back("-Werror=undef-prefix");

  // For modern targets, promote certain warnings to errors.
  if (isTargetWatchOSBased() || getTriple().isArch64Bit()) {
    // Always enable -Wdeprecated-objc-isa-usage and promote it
    // to an error.
    CC1Args.push_back("-Wdeprecated-objc-isa-usage");
    CC1Args.push_back("-Werror=deprecated-objc-isa-usage");

    // For iOS and watchOS, also error about implicit function declarations,
    // as that can impact calling conventions.
    if (!isTargetMacOS())
      CC1Args.push_back("-Werror=implicit-function-declaration");
  }
}

/// Take a path that speculatively points into Xcode and return the
/// `XCODE/Contents/Developer` path if it is an Xcode path, or an empty path
/// otherwise.
static StringRef getXcodeDeveloperPath(StringRef PathIntoXcode) {
  static constexpr llvm::StringLiteral XcodeAppSuffix(
      ".app/Contents/Developer");
  size_t Index = PathIntoXcode.find(XcodeAppSuffix);
  if (Index == StringRef::npos)
    return "";
  return PathIntoXcode.take_front(Index + XcodeAppSuffix.size());
}

void DarwinClang::AddLinkARCArgs(const ArgList &Args,
                                 ArgStringList &CmdArgs) const {
  // Avoid linking compatibility stubs on i386 mac.
  if (isTargetMacOSBased() && getArch() == llvm::Triple::x86)
    return;
  if (isTargetAppleSiliconMac())
    return;
  // ARC runtime is supported everywhere on arm64e.
  if (getTriple().isArm64e())
    return;
  if (isTargetXROS())
    return;

  ObjCRuntime runtime = getDefaultObjCRuntime(/*nonfragile*/ true);

  if ((runtime.hasNativeARC() || !isObjCAutoRefCount(Args)) &&
      runtime.hasSubscripting())
    return;

  SmallString<128> P(getDriver().ClangExecutable);
  llvm::sys::path::remove_filename(P); // 'clang'
  llvm::sys::path::remove_filename(P); // 'bin'
  llvm::sys::path::append(P, "lib", "arc");

  // 'libarclite' usually lives in the same toolchain as 'clang'. However, the
  // Swift open source toolchains for macOS distribute Clang without libarclite.
  // In that case, to allow the linker to find 'libarclite', we point to the
  // 'libarclite' in the XcodeDefault toolchain instead.
  if (!getVFS().exists(P)) {
    auto updatePath = [&](const Arg *A) {
      // Try to infer the path to 'libarclite' in the toolchain from the
      // specified SDK path.
      StringRef XcodePathForSDK = getXcodeDeveloperPath(A->getValue());
      if (XcodePathForSDK.empty())
        return false;

      P = XcodePathForSDK;
      llvm::sys::path::append(P, "Toolchains/XcodeDefault.xctoolchain/usr",
                              "lib", "arc");
      return getVFS().exists(P);
    };

    bool updated = false;
    if (const Arg *A = Args.getLastArg(options::OPT_isysroot))
      updated = updatePath(A);

    if (!updated) {
      if (const Arg *A = Args.getLastArg(options::OPT__sysroot_EQ))
        updatePath(A);
    }
  }

  CmdArgs.push_back("-force_load");
  llvm::sys::path::append(P, "libarclite_");
  // Mash in the platform.
  if (isTargetWatchOSSimulator())
    P += "watchsimulator";
  else if (isTargetWatchOS())
    P += "watchos";
  else if (isTargetTvOSSimulator())
    P += "appletvsimulator";
  else if (isTargetTvOS())
    P += "appletvos";
  else if (isTargetIOSSimulator())
    P += "iphonesimulator";
  else if (isTargetIPhoneOS())
    P += "iphoneos";
  else
    P += "macosx";
  P += ".a";

  if (!getVFS().exists(P))
    getDriver().Diag(clang::diag::err_drv_darwin_sdk_missing_arclite) << P;

  CmdArgs.push_back(Args.MakeArgString(P));
}

unsigned DarwinClang::GetDefaultDwarfVersion() const {
  // Default to use DWARF 2 on OS X 10.10 / iOS 8 and lower.
  if ((isTargetMacOSBased() && isMacosxVersionLT(10, 11)) ||
      (isTargetIOSBased() && isIPhoneOSVersionLT(9)))
    return 2;
  // Default to use DWARF 4 on OS X 10.11 - macOS 14 / iOS 9 - iOS 17.
  if ((isTargetMacOSBased() && isMacosxVersionLT(15)) ||
      (isTargetIOSBased() && isIPhoneOSVersionLT(18)) ||
      (isTargetWatchOSBased() && TargetVersion < llvm::VersionTuple(11)) ||
      (isTargetXROS() && TargetVersion < llvm::VersionTuple(2)) ||
      (isTargetDriverKit() && TargetVersion < llvm::VersionTuple(24)) ||
      (isTargetMacOSBased() &&
       TargetVersion.empty())) // apple-darwin, no version.
    return 4;
  return 5;
}

void MachO::AddLinkRuntimeLib(const ArgList &Args, ArgStringList &CmdArgs,
                              StringRef Component, RuntimeLinkOptions Opts,
                              bool IsShared) const {
  std::string P = getCompilerRT(
      Args, Component, IsShared ? ToolChain::FT_Shared : ToolChain::FT_Static);

  // For now, allow missing resource libraries to support developers who may
  // not have compiler-rt checked out or integrated into their build (unless
  // we explicitly force linking with this library).
  if ((Opts & RLO_AlwaysLink) || getVFS().exists(P)) {
    const char *LibArg = Args.MakeArgString(P);
    CmdArgs.push_back(LibArg);
  }

  // Adding the rpaths might negatively interact when other rpaths are involved,
  // so we should make sure we add the rpaths last, after all user-specified
  // rpaths. This is currently true from this place, but we need to be
  // careful if this function is ever called before user's rpaths are emitted.
  if (Opts & RLO_AddRPath) {
    assert(StringRef(P).ends_with(".dylib") && "must be a dynamic library");

    // Add @executable_path to rpath to support having the dylib copied with
    // the executable.
    CmdArgs.push_back("-rpath");
    CmdArgs.push_back("@executable_path");

    // Add the compiler-rt library's directory to rpath to support using the
    // dylib from the default location without copying.
    CmdArgs.push_back("-rpath");
    CmdArgs.push_back(Args.MakeArgString(llvm::sys::path::parent_path(P)));
  }
}

std::string MachO::getCompilerRT(const ArgList &, StringRef Component,
                                 FileType Type) const {
  assert(Type != ToolChain::FT_Object &&
         "it doesn't make sense to ask for the compiler-rt library name as an "
         "object file");
  SmallString<64> MachOLibName = StringRef("libclang_rt");
  // On MachO, the builtins component is not in the library name
  if (Component != "builtins") {
    MachOLibName += '.';
    MachOLibName += Component;
  }
  MachOLibName += Type == ToolChain::FT_Shared ? "_dynamic.dylib" : ".a";

  SmallString<128> FullPath(getDriver().ResourceDir);
  llvm::sys::path::append(FullPath, "lib", "darwin", "macho_embedded",
                          MachOLibName);
  return std::string(FullPath);
}

std::string Darwin::getCompilerRT(const ArgList &, StringRef Component,
                                  FileType Type) const {
  assert(Type != ToolChain::FT_Object &&
         "it doesn't make sense to ask for the compiler-rt library name as an "
         "object file");
  SmallString<64> DarwinLibName = StringRef("libclang_rt.");
  // On Darwin, the builtins component is not in the library name
  if (Component != "builtins") {
    DarwinLibName += Component;
    DarwinLibName += '_';
  }
  DarwinLibName += getOSLibraryNameSuffix();
  DarwinLibName += Type == ToolChain::FT_Shared ? "_dynamic.dylib" : ".a";

  SmallString<128> FullPath(getDriver().ResourceDir);
  llvm::sys::path::append(FullPath, "lib", "darwin", DarwinLibName);
  return std::string(FullPath);
}

StringRef Darwin::getPlatformFamily() const {
  switch (TargetPlatform) {
    case DarwinPlatformKind::MacOS:
      return "MacOSX";
    case DarwinPlatformKind::IPhoneOS:
      if (TargetEnvironment == MacCatalyst)
        return "MacOSX";
      return "iPhone";
    case DarwinPlatformKind::TvOS:
      return "AppleTV";
    case DarwinPlatformKind::WatchOS:
      return "Watch";
    case DarwinPlatformKind::DriverKit:
      return "DriverKit";
    case DarwinPlatformKind::XROS:
      return "XR";
  }
  llvm_unreachable("Unsupported platform");
}

StringRef Darwin::getSDKName(StringRef isysroot) {
  // Assume SDK has path: SOME_PATH/SDKs/PlatformXX.YY.sdk
  auto BeginSDK = llvm::sys::path::rbegin(isysroot);
  auto EndSDK = llvm::sys::path::rend(isysroot);
  for (auto IT = BeginSDK; IT != EndSDK; ++IT) {
    StringRef SDK = *IT;
    if (SDK.ends_with(".sdk"))
        return SDK.slice(0, SDK.size() - 4);
  }
  return "";
}

StringRef Darwin::getOSLibraryNameSuffix(bool IgnoreSim) const {
  switch (TargetPlatform) {
  case DarwinPlatformKind::MacOS:
    return "osx";
  case DarwinPlatformKind::IPhoneOS:
    if (TargetEnvironment == MacCatalyst)
      return "osx";
    return TargetEnvironment == NativeEnvironment || IgnoreSim ? "ios"
                                                               : "iossim";
  case DarwinPlatformKind::TvOS:
    return TargetEnvironment == NativeEnvironment || IgnoreSim ? "tvos"
                                                               : "tvossim";
  case DarwinPlatformKind::WatchOS:
    return TargetEnvironment == NativeEnvironment || IgnoreSim ? "watchos"
                                                               : "watchossim";
  case DarwinPlatformKind::XROS:
    return TargetEnvironment == NativeEnvironment || IgnoreSim ? "xros"
                                                               : "xrossim";
  case DarwinPlatformKind::DriverKit:
    return "driverkit";
  }
  llvm_unreachable("Unsupported platform");
}

/// Check if the link command contains a symbol export directive.
static bool hasExportSymbolDirective(const ArgList &Args) {
  for (Arg *A : Args) {
    if (A->getOption().matches(options::OPT_exported__symbols__list))
      return true;
    if (!A->getOption().matches(options::OPT_Wl_COMMA) &&
        !A->getOption().matches(options::OPT_Xlinker))
      continue;
    if (A->containsValue("-exported_symbols_list") ||
        A->containsValue("-exported_symbol"))
      return true;
  }
  return false;
}

/// Add an export directive for \p Symbol to the link command.
static void addExportedSymbol(ArgStringList &CmdArgs, const char *Symbol) {
  CmdArgs.push_back("-exported_symbol");
  CmdArgs.push_back(Symbol);
}

/// Add a sectalign directive for \p Segment and \p Section to the maximum
/// expected page size for Darwin.
///
/// On iPhone 6+ the max supported page size is 16K. On macOS, the max is 4K.
/// Use a common alignment constant (16K) for now, and reduce the alignment on
/// macOS if it proves important.
static void addSectalignToPage(const ArgList &Args, ArgStringList &CmdArgs,
                               StringRef Segment, StringRef Section) {
  for (const char *A : {"-sectalign", Args.MakeArgString(Segment),
                        Args.MakeArgString(Section), "0x4000"})
    CmdArgs.push_back(A);
}

void Darwin::addProfileRTLibs(const ArgList &Args,
                              ArgStringList &CmdArgs) const {
  if (!needsProfileRT(Args) && !needsGCovInstrumentation(Args))
    return;

  AddLinkRuntimeLib(Args, CmdArgs, "profile",
                    RuntimeLinkOptions(RLO_AlwaysLink));

  bool ForGCOV = needsGCovInstrumentation(Args);

  // If we have a symbol export directive and we're linking in the profile
  // runtime, automatically export symbols necessary to implement some of the
  // runtime's functionality.
  if (hasExportSymbolDirective(Args) && ForGCOV) {
    addExportedSymbol(CmdArgs, "___gcov_dump");
    addExportedSymbol(CmdArgs, "___gcov_reset");
    addExportedSymbol(CmdArgs, "_writeout_fn_list");
    addExportedSymbol(CmdArgs, "_reset_fn_list");
  }

  // Align __llvm_prf_{cnts,bits,data} sections to the maximum expected page
  // alignment. This allows profile counters to be mmap()'d to disk. Note that
  // it's not enough to just page-align __llvm_prf_cnts: the following section
  // must also be page-aligned so that its data is not clobbered by mmap().
  //
  // The section alignment is only needed when continuous profile sync is
  // enabled, but this is expected to be the default in Xcode. Specifying the
  // extra alignment also allows the same binary to be used with/without sync
  // enabled.
  if (!ForGCOV) {
    for (auto IPSK : {llvm::IPSK_cnts, llvm::IPSK_bitmap, llvm::IPSK_data}) {
      addSectalignToPage(
          Args, CmdArgs, "__DATA",
          llvm::getInstrProfSectionName(IPSK, llvm::Triple::MachO,
                                        /*AddSegmentInfo=*/false));
    }
  }
}

void DarwinClang::AddLinkSanitizerLibArgs(const ArgList &Args,
                                          ArgStringList &CmdArgs,
                                          StringRef Sanitizer,
                                          bool Shared) const {
  auto RLO = RuntimeLinkOptions(RLO_AlwaysLink | (Shared ? RLO_AddRPath : 0U));
  AddLinkRuntimeLib(Args, CmdArgs, Sanitizer, RLO, Shared);
}

ToolChain::RuntimeLibType DarwinClang::GetRuntimeLibType(
    const ArgList &Args) const {
  if (Arg* A = Args.getLastArg(options::OPT_rtlib_EQ)) {
    StringRef Value = A->getValue();
    if (Value != "compiler-rt" && Value != "platform")
      getDriver().Diag(clang::diag::err_drv_unsupported_rtlib_for_platform)
          << Value << "darwin";
  }

  return ToolChain::RLT_CompilerRT;
}

void DarwinClang::AddLinkRuntimeLibArgs(const ArgList &Args,
                                        ArgStringList &CmdArgs,
                                        bool ForceLinkBuiltinRT) const {
  // Call once to ensure diagnostic is printed if wrong value was specified
  GetRuntimeLibType(Args);

  // Darwin doesn't support real static executables, don't link any runtime
  // libraries with -static.
  if (Args.hasArg(options::OPT_static) ||
      Args.hasArg(options::OPT_fapple_kext) ||
      Args.hasArg(options::OPT_mkernel)) {
    if (ForceLinkBuiltinRT)
      AddLinkRuntimeLib(Args, CmdArgs, "builtins");
    return;
  }

  // Reject -static-libgcc for now, we can deal with this when and if someone
  // cares. This is useful in situations where someone wants to statically link
  // something like libstdc++, and needs its runtime support routines.
  if (const Arg *A = Args.getLastArg(options::OPT_static_libgcc)) {
    getDriver().Diag(diag::err_drv_unsupported_opt) << A->getAsString(Args);
    return;
  }

  const SanitizerArgs &Sanitize = getSanitizerArgs(Args);

  if (!Sanitize.needsSharedRt()) {
    const char *sanitizer = nullptr;
    if (Sanitize.needsUbsanRt()) {
      sanitizer = "UndefinedBehaviorSanitizer";
    } else if (Sanitize.needsAsanRt()) {
      sanitizer = "AddressSanitizer";
    } else if (Sanitize.needsTsanRt()) {
      sanitizer = "ThreadSanitizer";
    }
    if (sanitizer) {
      getDriver().Diag(diag::err_drv_unsupported_static_sanitizer_darwin)
          << sanitizer;
      return;
    }
  }

  if (Sanitize.linkRuntimes()) {
    if (Sanitize.needsAsanRt()) {
      if (Sanitize.needsStableAbi()) {
        AddLinkSanitizerLibArgs(Args, CmdArgs, "asan_abi", /*shared=*/false);
      } else {
        assert(Sanitize.needsSharedRt() &&
               "Static sanitizer runtimes not supported");
        AddLinkSanitizerLibArgs(Args, CmdArgs, "asan");
      }
    }
    if (Sanitize.needsLsanRt())
      AddLinkSanitizerLibArgs(Args, CmdArgs, "lsan");
    if (Sanitize.needsUbsanRt()) {
      assert(Sanitize.needsSharedRt() &&
             "Static sanitizer runtimes not supported");
      AddLinkSanitizerLibArgs(
          Args, CmdArgs,
          Sanitize.requiresMinimalRuntime() ? "ubsan_minimal" : "ubsan");
    }
    if (Sanitize.needsTsanRt()) {
      assert(Sanitize.needsSharedRt() &&
             "Static sanitizer runtimes not supported");
      AddLinkSanitizerLibArgs(Args, CmdArgs, "tsan");
    }
    if (Sanitize.needsFuzzer() && !Args.hasArg(options::OPT_dynamiclib)) {
      AddLinkSanitizerLibArgs(Args, CmdArgs, "fuzzer", /*shared=*/false);

        // Libfuzzer is written in C++ and requires libcxx.
        AddCXXStdlibLibArgs(Args, CmdArgs);
    }
    if (Sanitize.needsStatsRt()) {
      AddLinkRuntimeLib(Args, CmdArgs, "stats_client", RLO_AlwaysLink);
      AddLinkSanitizerLibArgs(Args, CmdArgs, "stats");
    }
  }

  const XRayArgs &XRay = getXRayArgs();
  if (XRay.needsXRayRt()) {
    AddLinkRuntimeLib(Args, CmdArgs, "xray");
    AddLinkRuntimeLib(Args, CmdArgs, "xray-basic");
    AddLinkRuntimeLib(Args, CmdArgs, "xray-fdr");
  }

  if (isTargetDriverKit() && !Args.hasArg(options::OPT_nodriverkitlib)) {
    CmdArgs.push_back("-framework");
    CmdArgs.push_back("DriverKit");
  }

  // Otherwise link libSystem, then the dynamic runtime library, and finally any
  // target specific static runtime library.
  if (!isTargetDriverKit())
    CmdArgs.push_back("-lSystem");

  // Select the dynamic runtime library and the target specific static library.
  if (isTargetIOSBased()) {
    // If we are compiling as iOS / simulator, don't attempt to link libgcc_s.1,
    // it never went into the SDK.
    // Linking against libgcc_s.1 isn't needed for iOS 5.0+
    if (isIPhoneOSVersionLT(5, 0) && !isTargetIOSSimulator() &&
        getTriple().getArch() != llvm::Triple::aarch64)
      CmdArgs.push_back("-lgcc_s.1");
  }
  AddLinkRuntimeLib(Args, CmdArgs, "builtins");
}

/// Returns the most appropriate macOS target version for the current process.
///
/// If the macOS SDK version is the same or earlier than the system version,
/// then the SDK version is returned. Otherwise the system version is returned.
static std::string getSystemOrSDKMacOSVersion(StringRef MacOSSDKVersion) {
  llvm::Triple SystemTriple(llvm::sys::getProcessTriple());
  if (!SystemTriple.isMacOSX())
    return std::string(MacOSSDKVersion);
  VersionTuple SystemVersion;
  SystemTriple.getMacOSXVersion(SystemVersion);

  unsigned Major, Minor, Micro;
  bool HadExtra;
  if (!Driver::GetReleaseVersion(MacOSSDKVersion, Major, Minor, Micro,
                                 HadExtra))
    return std::string(MacOSSDKVersion);
  VersionTuple SDKVersion(Major, Minor, Micro);

  if (SDKVersion > SystemVersion)
    return SystemVersion.getAsString();
  return std::string(MacOSSDKVersion);
}

namespace {

/// The Darwin OS that was selected or inferred from arguments / environment.
struct DarwinPlatform {
  enum SourceKind {
    /// The OS was specified using the -target argument.
    TargetArg,
    /// The OS was specified using the -mtargetos= argument.
    MTargetOSArg,
    /// The OS was specified using the -m<os>-version-min argument.
    OSVersionArg,
    /// The OS was specified using the OS_DEPLOYMENT_TARGET environment.
    DeploymentTargetEnv,
    /// The OS was inferred from the SDK.
    InferredFromSDK,
    /// The OS was inferred from the -arch.
    InferredFromArch
  };

  using DarwinPlatformKind = Darwin::DarwinPlatformKind;
  using DarwinEnvironmentKind = Darwin::DarwinEnvironmentKind;

  DarwinPlatformKind getPlatform() const { return Platform; }

  DarwinEnvironmentKind getEnvironment() const { return Environment; }

  void setEnvironment(DarwinEnvironmentKind Kind) {
    Environment = Kind;
    InferSimulatorFromArch = false;
  }

  StringRef getOSVersion() const {
    if (Kind == OSVersionArg)
      return Argument->getValue();
    return OSVersion;
  }

  void setOSVersion(StringRef S) {
    assert(Kind == TargetArg && "Unexpected kind!");
    OSVersion = std::string(S);
  }

  bool hasOSVersion() const { return HasOSVersion; }

  VersionTuple getNativeTargetVersion() const {
    assert(Environment == DarwinEnvironmentKind::MacCatalyst &&
           "native target version is specified only for Mac Catalyst");
    return NativeTargetVersion;
  }

  /// Returns true if the target OS was explicitly specified.
  bool isExplicitlySpecified() const { return Kind <= DeploymentTargetEnv; }

  /// Returns true if the simulator environment can be inferred from the arch.
  bool canInferSimulatorFromArch() const { return InferSimulatorFromArch; }

  const std::optional<llvm::Triple> &getTargetVariantTriple() const {
    return TargetVariantTriple;
  }

  /// Adds the -m<os>-version-min argument to the compiler invocation.
  void addOSVersionMinArgument(DerivedArgList &Args, const OptTable &Opts) {
    if (Argument)
      return;
    assert(Kind != TargetArg && Kind != MTargetOSArg && Kind != OSVersionArg &&
           "Invalid kind");
    options::ID Opt;
    switch (Platform) {
    case DarwinPlatformKind::MacOS:
      Opt = options::OPT_mmacos_version_min_EQ;
      break;
    case DarwinPlatformKind::IPhoneOS:
      Opt = options::OPT_mios_version_min_EQ;
      break;
    case DarwinPlatformKind::TvOS:
      Opt = options::OPT_mtvos_version_min_EQ;
      break;
    case DarwinPlatformKind::WatchOS:
      Opt = options::OPT_mwatchos_version_min_EQ;
      break;
    case DarwinPlatformKind::XROS:
      // xrOS always explicitly provides a version in the triple.
      return;
    case DarwinPlatformKind::DriverKit:
      // DriverKit always explicitly provides a version in the triple.
      return;
    }
    Argument = Args.MakeJoinedArg(nullptr, Opts.getOption(Opt), OSVersion);
    Args.append(Argument);
  }

  /// Returns the OS version with the argument / environment variable that
  /// specified it.
  std::string getAsString(DerivedArgList &Args, const OptTable &Opts) {
    switch (Kind) {
    case TargetArg:
    case MTargetOSArg:
    case OSVersionArg:
    case InferredFromSDK:
    case InferredFromArch:
      assert(Argument && "OS version argument not yet inferred");
      return Argument->getAsString(Args);
    case DeploymentTargetEnv:
      return (llvm::Twine(EnvVarName) + "=" + OSVersion).str();
    }
    llvm_unreachable("Unsupported Darwin Source Kind");
  }

  void setEnvironment(llvm::Triple::EnvironmentType EnvType,
                      const VersionTuple &OSVersion,
                      const std::optional<DarwinSDKInfo> &SDKInfo) {
    switch (EnvType) {
    case llvm::Triple::Simulator:
      Environment = DarwinEnvironmentKind::Simulator;
      break;
    case llvm::Triple::MacABI: {
      Environment = DarwinEnvironmentKind::MacCatalyst;
      // The minimum native macOS target for MacCatalyst is macOS 10.15.
      NativeTargetVersion = VersionTuple(10, 15);
      if (HasOSVersion && SDKInfo) {
        if (const auto *MacCatalystToMacOSMapping = SDKInfo->getVersionMapping(
                DarwinSDKInfo::OSEnvPair::macCatalystToMacOSPair())) {
          if (auto MacOSVersion = MacCatalystToMacOSMapping->map(
                  OSVersion, NativeTargetVersion, std::nullopt)) {
            NativeTargetVersion = *MacOSVersion;
          }
        }
      }
      // In a zippered build, we could be building for a macOS target that's
      // lower than the version that's implied by the OS version. In that case
      // we need to use the minimum version as the native target version.
      if (TargetVariantTriple) {
        auto TargetVariantVersion = TargetVariantTriple->getOSVersion();
        if (TargetVariantVersion.getMajor()) {
          if (TargetVariantVersion < NativeTargetVersion)
            NativeTargetVersion = TargetVariantVersion;
        }
      }
      break;
    }
    default:
      break;
    }
  }

  static DarwinPlatform
  createFromTarget(const llvm::Triple &TT, StringRef OSVersion, Arg *A,
                   std::optional<llvm::Triple> TargetVariantTriple,
                   const std::optional<DarwinSDKInfo> &SDKInfo) {
    DarwinPlatform Result(TargetArg, getPlatformFromOS(TT.getOS()), OSVersion,
                          A);
    VersionTuple OsVersion = TT.getOSVersion();
    if (OsVersion.getMajor() == 0)
      Result.HasOSVersion = false;
    Result.TargetVariantTriple = TargetVariantTriple;
    Result.setEnvironment(TT.getEnvironment(), OsVersion, SDKInfo);
    return Result;
  }
  static DarwinPlatform
  createFromMTargetOS(llvm::Triple::OSType OS, VersionTuple OSVersion,
                      llvm::Triple::EnvironmentType Environment, Arg *A,
                      const std::optional<DarwinSDKInfo> &SDKInfo) {
    DarwinPlatform Result(MTargetOSArg, getPlatformFromOS(OS),
                          OSVersion.getAsString(), A);
    Result.InferSimulatorFromArch = false;
    Result.setEnvironment(Environment, OSVersion, SDKInfo);
    return Result;
  }
  static DarwinPlatform createOSVersionArg(DarwinPlatformKind Platform, Arg *A,
                                           bool IsSimulator) {
    DarwinPlatform Result{OSVersionArg, Platform, A};
    if (IsSimulator)
      Result.Environment = DarwinEnvironmentKind::Simulator;
    return Result;
  }
  static DarwinPlatform createDeploymentTargetEnv(DarwinPlatformKind Platform,
                                                  StringRef EnvVarName,
                                                  StringRef Value) {
    DarwinPlatform Result(DeploymentTargetEnv, Platform, Value);
    Result.EnvVarName = EnvVarName;
    return Result;
  }
  static DarwinPlatform createFromSDK(DarwinPlatformKind Platform,
                                      StringRef Value,
                                      bool IsSimulator = false) {
    DarwinPlatform Result(InferredFromSDK, Platform, Value);
    if (IsSimulator)
      Result.Environment = DarwinEnvironmentKind::Simulator;
    Result.InferSimulatorFromArch = false;
    return Result;
  }
  static DarwinPlatform createFromArch(llvm::Triple::OSType OS,
                                       StringRef Value) {
    return DarwinPlatform(InferredFromArch, getPlatformFromOS(OS), Value);
  }

  /// Constructs an inferred SDKInfo value based on the version inferred from
  /// the SDK path itself. Only works for values that were created by inferring
  /// the platform from the SDKPath.
  DarwinSDKInfo inferSDKInfo() {
    assert(Kind == InferredFromSDK && "can infer SDK info only");
    llvm::VersionTuple Version;
    bool IsValid = !Version.tryParse(OSVersion);
    (void)IsValid;
    assert(IsValid && "invalid SDK version");
    return DarwinSDKInfo(
        Version,
        /*MaximumDeploymentTarget=*/VersionTuple(Version.getMajor(), 0, 99));
  }

private:
  DarwinPlatform(SourceKind Kind, DarwinPlatformKind Platform, Arg *Argument)
      : Kind(Kind), Platform(Platform), Argument(Argument) {}
  DarwinPlatform(SourceKind Kind, DarwinPlatformKind Platform, StringRef Value,
                 Arg *Argument = nullptr)
      : Kind(Kind), Platform(Platform), OSVersion(Value), Argument(Argument) {}

  static DarwinPlatformKind getPlatformFromOS(llvm::Triple::OSType OS) {
    switch (OS) {
    case llvm::Triple::Darwin:
    case llvm::Triple::MacOSX:
      return DarwinPlatformKind::MacOS;
    case llvm::Triple::IOS:
      return DarwinPlatformKind::IPhoneOS;
    case llvm::Triple::TvOS:
      return DarwinPlatformKind::TvOS;
    case llvm::Triple::WatchOS:
      return DarwinPlatformKind::WatchOS;
    case llvm::Triple::XROS:
      return DarwinPlatformKind::XROS;
    case llvm::Triple::DriverKit:
      return DarwinPlatformKind::DriverKit;
    default:
      llvm_unreachable("Unable to infer Darwin variant");
    }
  }

  SourceKind Kind;
  DarwinPlatformKind Platform;
  DarwinEnvironmentKind Environment = DarwinEnvironmentKind::NativeEnvironment;
  VersionTuple NativeTargetVersion;
  std::string OSVersion;
  bool HasOSVersion = true, InferSimulatorFromArch = true;
  Arg *Argument;
  StringRef EnvVarName;
  std::optional<llvm::Triple> TargetVariantTriple;
};

/// Returns the deployment target that's specified using the -m<os>-version-min
/// argument.
std::optional<DarwinPlatform>
getDeploymentTargetFromOSVersionArg(DerivedArgList &Args,
                                    const Driver &TheDriver) {
  Arg *macOSVersion = Args.getLastArg(options::OPT_mmacos_version_min_EQ);
  Arg *iOSVersion = Args.getLastArg(options::OPT_mios_version_min_EQ,
                                    options::OPT_mios_simulator_version_min_EQ);
  Arg *TvOSVersion =
      Args.getLastArg(options::OPT_mtvos_version_min_EQ,
                      options::OPT_mtvos_simulator_version_min_EQ);
  Arg *WatchOSVersion =
      Args.getLastArg(options::OPT_mwatchos_version_min_EQ,
                      options::OPT_mwatchos_simulator_version_min_EQ);
  if (macOSVersion) {
    if (iOSVersion || TvOSVersion || WatchOSVersion) {
      TheDriver.Diag(diag::err_drv_argument_not_allowed_with)
          << macOSVersion->getAsString(Args)
          << (iOSVersion ? iOSVersion
                         : TvOSVersion ? TvOSVersion : WatchOSVersion)
                 ->getAsString(Args);
    }
    return DarwinPlatform::createOSVersionArg(Darwin::MacOS, macOSVersion,
                                              /*IsSimulator=*/false);
  } else if (iOSVersion) {
    if (TvOSVersion || WatchOSVersion) {
      TheDriver.Diag(diag::err_drv_argument_not_allowed_with)
          << iOSVersion->getAsString(Args)
          << (TvOSVersion ? TvOSVersion : WatchOSVersion)->getAsString(Args);
    }
    return DarwinPlatform::createOSVersionArg(
        Darwin::IPhoneOS, iOSVersion,
        iOSVersion->getOption().getID() ==
            options::OPT_mios_simulator_version_min_EQ);
  } else if (TvOSVersion) {
    if (WatchOSVersion) {
      TheDriver.Diag(diag::err_drv_argument_not_allowed_with)
          << TvOSVersion->getAsString(Args)
          << WatchOSVersion->getAsString(Args);
    }
    return DarwinPlatform::createOSVersionArg(
        Darwin::TvOS, TvOSVersion,
        TvOSVersion->getOption().getID() ==
            options::OPT_mtvos_simulator_version_min_EQ);
  } else if (WatchOSVersion)
    return DarwinPlatform::createOSVersionArg(
        Darwin::WatchOS, WatchOSVersion,
        WatchOSVersion->getOption().getID() ==
            options::OPT_mwatchos_simulator_version_min_EQ);
  return std::nullopt;
}

/// Returns the deployment target that's specified using the
/// OS_DEPLOYMENT_TARGET environment variable.
std::optional<DarwinPlatform>
getDeploymentTargetFromEnvironmentVariables(const Driver &TheDriver,
                                            const llvm::Triple &Triple) {
  std::string Targets[Darwin::LastDarwinPlatform + 1];
  const char *EnvVars[] = {
      "MACOSX_DEPLOYMENT_TARGET",
      "IPHONEOS_DEPLOYMENT_TARGET",
      "TVOS_DEPLOYMENT_TARGET",
      "WATCHOS_DEPLOYMENT_TARGET",
      "DRIVERKIT_DEPLOYMENT_TARGET",
      "XROS_DEPLOYMENT_TARGET"
  };
  static_assert(std::size(EnvVars) == Darwin::LastDarwinPlatform + 1,
                "Missing platform");
  for (const auto &I : llvm::enumerate(llvm::ArrayRef(EnvVars))) {
    if (char *Env = ::getenv(I.value()))
      Targets[I.index()] = Env;
  }

  // Allow conflicts among OSX and iOS for historical reasons, but choose the
  // default platform.
  if (!Targets[Darwin::MacOS].empty() &&
      (!Targets[Darwin::IPhoneOS].empty() ||
       !Targets[Darwin::WatchOS].empty() || !Targets[Darwin::TvOS].empty() ||
       !Targets[Darwin::XROS].empty())) {
    if (Triple.getArch() == llvm::Triple::arm ||
        Triple.getArch() == llvm::Triple::aarch64 ||
        Triple.getArch() == llvm::Triple::thumb)
      Targets[Darwin::MacOS] = "";
    else
      Targets[Darwin::IPhoneOS] = Targets[Darwin::WatchOS] =
          Targets[Darwin::TvOS] = Targets[Darwin::XROS] = "";
  } else {
    // Don't allow conflicts in any other platform.
    unsigned FirstTarget = std::size(Targets);
    for (unsigned I = 0; I != std::size(Targets); ++I) {
      if (Targets[I].empty())
        continue;
      if (FirstTarget == std::size(Targets))
        FirstTarget = I;
      else
        TheDriver.Diag(diag::err_drv_conflicting_deployment_targets)
            << Targets[FirstTarget] << Targets[I];
    }
  }

  for (const auto &Target : llvm::enumerate(llvm::ArrayRef(Targets))) {
    if (!Target.value().empty())
      return DarwinPlatform::createDeploymentTargetEnv(
          (Darwin::DarwinPlatformKind)Target.index(), EnvVars[Target.index()],
          Target.value());
  }
  return std::nullopt;
}

/// Returns the SDK name without the optional prefix that ends with a '.' or an
/// empty string otherwise.
static StringRef dropSDKNamePrefix(StringRef SDKName) {
  size_t PrefixPos = SDKName.find('.');
  if (PrefixPos == StringRef::npos)
    return "";
  return SDKName.substr(PrefixPos + 1);
}

/// Tries to infer the deployment target from the SDK specified by -isysroot
/// (or SDKROOT). Uses the version specified in the SDKSettings.json file if
/// it's available.
std::optional<DarwinPlatform>
inferDeploymentTargetFromSDK(DerivedArgList &Args,
                             const std::optional<DarwinSDKInfo> &SDKInfo) {
  const Arg *A = Args.getLastArg(options::OPT_isysroot);
  if (!A)
    return std::nullopt;
  StringRef isysroot = A->getValue();
  StringRef SDK = Darwin::getSDKName(isysroot);
  if (!SDK.size())
    return std::nullopt;

  std::string Version;
  if (SDKInfo) {
    // Get the version from the SDKSettings.json if it's available.
    Version = SDKInfo->getVersion().getAsString();
  } else {
    // Slice the version number out.
    // Version number is between the first and the last number.
    size_t StartVer = SDK.find_first_of("0123456789");
    size_t EndVer = SDK.find_last_of("0123456789");
    if (StartVer != StringRef::npos && EndVer > StartVer)
      Version = std::string(SDK.slice(StartVer, EndVer + 1));
  }
  if (Version.empty())
    return std::nullopt;

  auto CreatePlatformFromSDKName =
      [&](StringRef SDK) -> std::optional<DarwinPlatform> {
    if (SDK.starts_with("iPhoneOS") || SDK.starts_with("iPhoneSimulator"))
      return DarwinPlatform::createFromSDK(
          Darwin::IPhoneOS, Version,
          /*IsSimulator=*/SDK.starts_with("iPhoneSimulator"));
    else if (SDK.starts_with("MacOSX"))
      return DarwinPlatform::createFromSDK(Darwin::MacOS,
                                           getSystemOrSDKMacOSVersion(Version));
    else if (SDK.starts_with("WatchOS") || SDK.starts_with("WatchSimulator"))
      return DarwinPlatform::createFromSDK(
          Darwin::WatchOS, Version,
          /*IsSimulator=*/SDK.starts_with("WatchSimulator"));
    else if (SDK.starts_with("AppleTVOS") ||
             SDK.starts_with("AppleTVSimulator"))
      return DarwinPlatform::createFromSDK(
          Darwin::TvOS, Version,
          /*IsSimulator=*/SDK.starts_with("AppleTVSimulator"));
    else if (SDK.starts_with("XR"))
      return DarwinPlatform::createFromSDK(
          Darwin::XROS, Version,
          /*IsSimulator=*/SDK.contains("Simulator"));
    else if (SDK.starts_with("DriverKit"))
      return DarwinPlatform::createFromSDK(Darwin::DriverKit, Version);
    return std::nullopt;
  };
  if (auto Result = CreatePlatformFromSDKName(SDK))
    return Result;
  // The SDK can be an SDK variant with a name like `<prefix>.<platform>`.
  return CreatePlatformFromSDKName(dropSDKNamePrefix(SDK));
}

std::string getOSVersion(llvm::Triple::OSType OS, const llvm::Triple &Triple,
                         const Driver &TheDriver) {
  VersionTuple OsVersion;
  llvm::Triple SystemTriple(llvm::sys::getProcessTriple());
  switch (OS) {
  case llvm::Triple::Darwin:
  case llvm::Triple::MacOSX:
    // If there is no version specified on triple, and both host and target are
    // macos, use the host triple to infer OS version.
    if (Triple.isMacOSX() && SystemTriple.isMacOSX() &&
        !Triple.getOSMajorVersion())
      SystemTriple.getMacOSXVersion(OsVersion);
    else if (!Triple.getMacOSXVersion(OsVersion))
      TheDriver.Diag(diag::err_drv_invalid_darwin_version)
          << Triple.getOSName();
    break;
  case llvm::Triple::IOS:
    if (Triple.isMacCatalystEnvironment() && !Triple.getOSMajorVersion()) {
      OsVersion = VersionTuple(13, 1);
    } else
      OsVersion = Triple.getiOSVersion();
    break;
  case llvm::Triple::TvOS:
    OsVersion = Triple.getOSVersion();
    break;
  case llvm::Triple::WatchOS:
    OsVersion = Triple.getWatchOSVersion();
    break;
  case llvm::Triple::XROS:
    OsVersion = Triple.getOSVersion();
    if (!OsVersion.getMajor())
      OsVersion = OsVersion.withMajorReplaced(1);
    break;
  case llvm::Triple::DriverKit:
    OsVersion = Triple.getDriverKitVersion();
    break;
  default:
    llvm_unreachable("Unexpected OS type");
    break;
  }

  std::string OSVersion;
  llvm::raw_string_ostream(OSVersion)
      << OsVersion.getMajor() << '.' << OsVersion.getMinor().value_or(0) << '.'
      << OsVersion.getSubminor().value_or(0);
  return OSVersion;
}

/// Tries to infer the target OS from the -arch.
std::optional<DarwinPlatform>
inferDeploymentTargetFromArch(DerivedArgList &Args, const Darwin &Toolchain,
                              const llvm::Triple &Triple,
                              const Driver &TheDriver) {
  llvm::Triple::OSType OSTy = llvm::Triple::UnknownOS;

  StringRef MachOArchName = Toolchain.getMachOArchName(Args);
  if (MachOArchName == "arm64" || MachOArchName == "arm64e")
    OSTy = llvm::Triple::MacOSX;
  else if (MachOArchName == "armv7" || MachOArchName == "armv7s")
    OSTy = llvm::Triple::IOS;
  else if (MachOArchName == "armv7k" || MachOArchName == "arm64_32")
    OSTy = llvm::Triple::WatchOS;
  else if (MachOArchName != "armv6m" && MachOArchName != "armv7m" &&
           MachOArchName != "armv7em")
    OSTy = llvm::Triple::MacOSX;
  if (OSTy == llvm::Triple::UnknownOS)
    return std::nullopt;
  return DarwinPlatform::createFromArch(OSTy,
                                        getOSVersion(OSTy, Triple, TheDriver));
}

/// Returns the deployment target that's specified using the -target option.
std::optional<DarwinPlatform> getDeploymentTargetFromTargetArg(
    DerivedArgList &Args, const llvm::Triple &Triple, const Driver &TheDriver,
    const std::optional<DarwinSDKInfo> &SDKInfo) {
  if (!Args.hasArg(options::OPT_target))
    return std::nullopt;
  if (Triple.getOS() == llvm::Triple::Darwin ||
      Triple.getOS() == llvm::Triple::UnknownOS)
    return std::nullopt;
  std::string OSVersion = getOSVersion(Triple.getOS(), Triple, TheDriver);
  std::optional<llvm::Triple> TargetVariantTriple;
  for (const Arg *A : Args.filtered(options::OPT_darwin_target_variant)) {
    llvm::Triple TVT(A->getValue());
    // Find a matching <arch>-<vendor> target variant triple that can be used.
    if ((Triple.getArch() == llvm::Triple::aarch64 ||
         TVT.getArchName() == Triple.getArchName()) &&
        TVT.getArch() == Triple.getArch() &&
        TVT.getSubArch() == Triple.getSubArch() &&
        TVT.getVendor() == Triple.getVendor()) {
      if (TargetVariantTriple)
        continue;
      A->claim();
      // Accept a -target-variant triple when compiling code that may run on
      // macOS or Mac Catalyst.
      if ((Triple.isMacOSX() && TVT.getOS() == llvm::Triple::IOS &&
           TVT.isMacCatalystEnvironment()) ||
          (TVT.isMacOSX() && Triple.getOS() == llvm::Triple::IOS &&
           Triple.isMacCatalystEnvironment())) {
        TargetVariantTriple = TVT;
        continue;
      }
      TheDriver.Diag(diag::err_drv_target_variant_invalid)
          << A->getSpelling() << A->getValue();
    }
  }
  return DarwinPlatform::createFromTarget(Triple, OSVersion,
                                          Args.getLastArg(options::OPT_target),
                                          TargetVariantTriple, SDKInfo);
}

/// Returns the deployment target that's specified using the -mtargetos option.
std::optional<DarwinPlatform> getDeploymentTargetFromMTargetOSArg(
    DerivedArgList &Args, const Driver &TheDriver,
    const std::optional<DarwinSDKInfo> &SDKInfo) {
  auto *A = Args.getLastArg(options::OPT_mtargetos_EQ);
  if (!A)
    return std::nullopt;
  llvm::Triple TT(llvm::Twine("unknown-apple-") + A->getValue());
  switch (TT.getOS()) {
  case llvm::Triple::MacOSX:
  case llvm::Triple::IOS:
  case llvm::Triple::TvOS:
  case llvm::Triple::WatchOS:
  case llvm::Triple::XROS:
    break;
  default:
    TheDriver.Diag(diag::err_drv_invalid_os_in_arg)
        << TT.getOSName() << A->getAsString(Args);
    return std::nullopt;
  }

  VersionTuple Version = TT.getOSVersion();
  if (!Version.getMajor()) {
    TheDriver.Diag(diag::err_drv_invalid_version_number)
        << A->getAsString(Args);
    return std::nullopt;
  }
  return DarwinPlatform::createFromMTargetOS(TT.getOS(), Version,
                                             TT.getEnvironment(), A, SDKInfo);
}

std::optional<DarwinSDKInfo> parseSDKSettings(llvm::vfs::FileSystem &VFS,
                                              const ArgList &Args,
                                              const Driver &TheDriver) {
  const Arg *A = Args.getLastArg(options::OPT_isysroot);
  if (!A)
    return std::nullopt;
  StringRef isysroot = A->getValue();
  auto SDKInfoOrErr = parseDarwinSDKInfo(VFS, isysroot);
  if (!SDKInfoOrErr) {
    llvm::consumeError(SDKInfoOrErr.takeError());
    TheDriver.Diag(diag::warn_drv_darwin_sdk_invalid_settings);
    return std::nullopt;
  }
  return *SDKInfoOrErr;
}

} // namespace

void Darwin::AddDeploymentTarget(DerivedArgList &Args) const {
  const OptTable &Opts = getDriver().getOpts();

  // Support allowing the SDKROOT environment variable used by xcrun and other
  // Xcode tools to define the default sysroot, by making it the default for
  // isysroot.
  if (const Arg *A = Args.getLastArg(options::OPT_isysroot)) {
    // Warn if the path does not exist.
    if (!getVFS().exists(A->getValue()))
      getDriver().Diag(clang::diag::warn_missing_sysroot) << A->getValue();
  } else {
    if (char *env = ::getenv("SDKROOT")) {
      // We only use this value as the default if it is an absolute path,
      // exists, and it is not the root path.
      if (llvm::sys::path::is_absolute(env) && getVFS().exists(env) &&
          StringRef(env) != "/") {
        Args.append(Args.MakeSeparateArg(
            nullptr, Opts.getOption(options::OPT_isysroot), env));
      }
    }
  }

  // Read the SDKSettings.json file for more information, like the SDK version
  // that we can pass down to the compiler.
  SDKInfo = parseSDKSettings(getVFS(), Args, getDriver());

  // The OS and the version can be specified using the -target argument.
  std::optional<DarwinPlatform> OSTarget =
      getDeploymentTargetFromTargetArg(Args, getTriple(), getDriver(), SDKInfo);
  if (OSTarget) {
    // Disallow mixing -target and -mtargetos=.
    if (const auto *MTargetOSArg = Args.getLastArg(options::OPT_mtargetos_EQ)) {
      std::string TargetArgStr = OSTarget->getAsString(Args, Opts);
      std::string MTargetOSArgStr = MTargetOSArg->getAsString(Args);
      getDriver().Diag(diag::err_drv_cannot_mix_options)
          << TargetArgStr << MTargetOSArgStr;
    }
    std::optional<DarwinPlatform> OSVersionArgTarget =
        getDeploymentTargetFromOSVersionArg(Args, getDriver());
    if (OSVersionArgTarget) {
      unsigned TargetMajor, TargetMinor, TargetMicro;
      bool TargetExtra;
      unsigned ArgMajor, ArgMinor, ArgMicro;
      bool ArgExtra;
      if (OSTarget->getPlatform() != OSVersionArgTarget->getPlatform() ||
          (Driver::GetReleaseVersion(OSTarget->getOSVersion(), TargetMajor,
                                     TargetMinor, TargetMicro, TargetExtra) &&
           Driver::GetReleaseVersion(OSVersionArgTarget->getOSVersion(),
                                     ArgMajor, ArgMinor, ArgMicro, ArgExtra) &&
           (VersionTuple(TargetMajor, TargetMinor, TargetMicro) !=
                VersionTuple(ArgMajor, ArgMinor, ArgMicro) ||
            TargetExtra != ArgExtra))) {
        // Select the OS version from the -m<os>-version-min argument when
        // the -target does not include an OS version.
        if (OSTarget->getPlatform() == OSVersionArgTarget->getPlatform() &&
            !OSTarget->hasOSVersion()) {
          OSTarget->setOSVersion(OSVersionArgTarget->getOSVersion());
        } else {
          // Warn about -m<os>-version-min that doesn't match the OS version
          // that's specified in the target.
          std::string OSVersionArg =
              OSVersionArgTarget->getAsString(Args, Opts);
          std::string TargetArg = OSTarget->getAsString(Args, Opts);
          getDriver().Diag(clang::diag::warn_drv_overriding_option)
              << OSVersionArg << TargetArg;
        }
      }
    }
  } else if ((OSTarget = getDeploymentTargetFromMTargetOSArg(Args, getDriver(),
                                                             SDKInfo))) {
    // The OS target can be specified using the -mtargetos= argument.
    // Disallow mixing -mtargetos= and -m<os>version-min=.
    std::optional<DarwinPlatform> OSVersionArgTarget =
        getDeploymentTargetFromOSVersionArg(Args, getDriver());
    if (OSVersionArgTarget) {
      std::string MTargetOSArgStr = OSTarget->getAsString(Args, Opts);
      std::string OSVersionArgStr = OSVersionArgTarget->getAsString(Args, Opts);
      getDriver().Diag(diag::err_drv_cannot_mix_options)
          << MTargetOSArgStr << OSVersionArgStr;
    }
  } else {
    // The OS target can be specified using the -m<os>version-min argument.
    OSTarget = getDeploymentTargetFromOSVersionArg(Args, getDriver());
    // If no deployment target was specified on the command line, check for
    // environment defines.
    if (!OSTarget) {
      OSTarget =
          getDeploymentTargetFromEnvironmentVariables(getDriver(), getTriple());
      if (OSTarget) {
        // Don't infer simulator from the arch when the SDK is also specified.
        std::optional<DarwinPlatform> SDKTarget =
            inferDeploymentTargetFromSDK(Args, SDKInfo);
        if (SDKTarget)
          OSTarget->setEnvironment(SDKTarget->getEnvironment());
      }
    }
    // If there is no command-line argument to specify the Target version and
    // no environment variable defined, see if we can set the default based
    // on -isysroot using SDKSettings.json if it exists.
    if (!OSTarget) {
      OSTarget = inferDeploymentTargetFromSDK(Args, SDKInfo);
      /// If the target was successfully constructed from the SDK path, try to
      /// infer the SDK info if the SDK doesn't have it.
      if (OSTarget && !SDKInfo)
        SDKInfo = OSTarget->inferSDKInfo();
    }
    // If no OS targets have been specified, try to guess platform from -target
    // or arch name and compute the version from the triple.
    if (!OSTarget)
      OSTarget =
          inferDeploymentTargetFromArch(Args, *this, getTriple(), getDriver());
  }

  assert(OSTarget && "Unable to infer Darwin variant");
  OSTarget->addOSVersionMinArgument(Args, Opts);
  DarwinPlatformKind Platform = OSTarget->getPlatform();

  unsigned Major, Minor, Micro;
  bool HadExtra;
  // The major version should not be over this number.
  const unsigned MajorVersionLimit = 1000;
  // Set the tool chain target information.
  if (Platform == MacOS) {
    if (!Driver::GetReleaseVersion(OSTarget->getOSVersion(), Major, Minor,
                                   Micro, HadExtra) ||
        HadExtra || Major < 10 || Major >= MajorVersionLimit || Minor >= 100 ||
        Micro >= 100)
      getDriver().Diag(diag::err_drv_invalid_version_number)
          << OSTarget->getAsString(Args, Opts);
  } else if (Platform == IPhoneOS) {
    if (!Driver::GetReleaseVersion(OSTarget->getOSVersion(), Major, Minor,
                                   Micro, HadExtra) ||
        HadExtra || Major >= MajorVersionLimit || Minor >= 100 || Micro >= 100)
      getDriver().Diag(diag::err_drv_invalid_version_number)
          << OSTarget->getAsString(Args, Opts);
    ;
    if (OSTarget->getEnvironment() == MacCatalyst &&
        (Major < 13 || (Major == 13 && Minor < 1))) {
      getDriver().Diag(diag::err_drv_invalid_version_number)
          << OSTarget->getAsString(Args, Opts);
      Major = 13;
      Minor = 1;
      Micro = 0;
    }
    // For 32-bit targets, the deployment target for iOS has to be earlier than
    // iOS 11.
    if (getTriple().isArch32Bit() && Major >= 11) {
      // If the deployment target is explicitly specified, print a diagnostic.
      if (OSTarget->isExplicitlySpecified()) {
        if (OSTarget->getEnvironment() == MacCatalyst)
          getDriver().Diag(diag::err_invalid_macos_32bit_deployment_target);
        else
          getDriver().Diag(diag::warn_invalid_ios_deployment_target)
              << OSTarget->getAsString(Args, Opts);
        // Otherwise, set it to 10.99.99.
      } else {
        Major = 10;
        Minor = 99;
        Micro = 99;
      }
    }
  } else if (Platform == TvOS) {
    if (!Driver::GetReleaseVersion(OSTarget->getOSVersion(), Major, Minor,
                                   Micro, HadExtra) ||
        HadExtra || Major >= MajorVersionLimit || Minor >= 100 || Micro >= 100)
      getDriver().Diag(diag::err_drv_invalid_version_number)
          << OSTarget->getAsString(Args, Opts);
  } else if (Platform == WatchOS) {
    if (!Driver::GetReleaseVersion(OSTarget->getOSVersion(), Major, Minor,
                                   Micro, HadExtra) ||
        HadExtra || Major >= MajorVersionLimit || Minor >= 100 || Micro >= 100)
      getDriver().Diag(diag::err_drv_invalid_version_number)
          << OSTarget->getAsString(Args, Opts);
  } else if (Platform == DriverKit) {
    if (!Driver::GetReleaseVersion(OSTarget->getOSVersion(), Major, Minor,
                                   Micro, HadExtra) ||
        HadExtra || Major < 19 || Major >= MajorVersionLimit || Minor >= 100 ||
        Micro >= 100)
      getDriver().Diag(diag::err_drv_invalid_version_number)
          << OSTarget->getAsString(Args, Opts);
  } else if (Platform == XROS) {
    if (!Driver::GetReleaseVersion(OSTarget->getOSVersion(), Major, Minor,
                                   Micro, HadExtra) ||
        HadExtra || Major < 1 || Major >= MajorVersionLimit || Minor >= 100 ||
        Micro >= 100)
      getDriver().Diag(diag::err_drv_invalid_version_number)
          << OSTarget->getAsString(Args, Opts);
  } else
    llvm_unreachable("unknown kind of Darwin platform");

  DarwinEnvironmentKind Environment = OSTarget->getEnvironment();
  // Recognize iOS targets with an x86 architecture as the iOS simulator.
  if (Environment == NativeEnvironment && Platform != MacOS &&
      Platform != DriverKit && OSTarget->canInferSimulatorFromArch() &&
      getTriple().isX86())
    Environment = Simulator;

  VersionTuple NativeTargetVersion;
  if (Environment == MacCatalyst)
    NativeTargetVersion = OSTarget->getNativeTargetVersion();
  setTarget(Platform, Environment, Major, Minor, Micro, NativeTargetVersion);
  TargetVariantTriple = OSTarget->getTargetVariantTriple();

  if (const Arg *A = Args.getLastArg(options::OPT_isysroot)) {
    StringRef SDK = getSDKName(A->getValue());
    if (SDK.size() > 0) {
      size_t StartVer = SDK.find_first_of("0123456789");
      StringRef SDKName = SDK.slice(0, StartVer);
      if (!SDKName.starts_with(getPlatformFamily()) &&
          !dropSDKNamePrefix(SDKName).starts_with(getPlatformFamily()))
        getDriver().Diag(diag::warn_incompatible_sysroot)
            << SDKName << getPlatformFamily();
    }
  }
}

// For certain platforms/environments almost all resources (e.g., headers) are
// located in sub-directories, e.g., for DriverKit they live in
// <SYSROOT>/System/DriverKit/usr/include (instead of <SYSROOT>/usr/include).
static void AppendPlatformPrefix(SmallString<128> &Path,
                                 const llvm::Triple &T) {
  if (T.isDriverKit()) {
    llvm::sys::path::append(Path, "System", "DriverKit");
  }
}

// Returns the effective sysroot from either -isysroot or --sysroot, plus the
// platform prefix (if any).
llvm::SmallString<128>
DarwinClang::GetEffectiveSysroot(const llvm::opt::ArgList &DriverArgs) const {
  llvm::SmallString<128> Path("/");
  if (DriverArgs.hasArg(options::OPT_isysroot))
    Path = DriverArgs.getLastArgValue(options::OPT_isysroot);
  else if (!getDriver().SysRoot.empty())
    Path = getDriver().SysRoot;

  if (hasEffectiveTriple()) {
    AppendPlatformPrefix(Path, getEffectiveTriple());
  }
  return Path;
}

void DarwinClang::AddClangSystemIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                                            llvm::opt::ArgStringList &CC1Args) const {
  const Driver &D = getDriver();

  llvm::SmallString<128> Sysroot = GetEffectiveSysroot(DriverArgs);

  bool NoStdInc = DriverArgs.hasArg(options::OPT_nostdinc);
  bool NoStdlibInc = DriverArgs.hasArg(options::OPT_nostdlibinc);
  bool NoBuiltinInc = DriverArgs.hasFlag(
      options::OPT_nobuiltininc, options::OPT_ibuiltininc, /*Default=*/false);
  bool ForceBuiltinInc = DriverArgs.hasFlag(
      options::OPT_ibuiltininc, options::OPT_nobuiltininc, /*Default=*/false);

  // Add <sysroot>/usr/local/include
  if (!NoStdInc && !NoStdlibInc) {
      SmallString<128> P(Sysroot);
      llvm::sys::path::append(P, "usr", "local", "include");
      addSystemInclude(DriverArgs, CC1Args, P);
  }

  // Add the Clang builtin headers (<resource>/include)
  if (!(NoStdInc && !ForceBuiltinInc) && !NoBuiltinInc) {
    SmallString<128> P(D.ResourceDir);
    llvm::sys::path::append(P, "include");
    addSystemInclude(DriverArgs, CC1Args, P);
  }

  if (NoStdInc || NoStdlibInc)
    return;

  // Check for configure-time C include directories.
  llvm::StringRef CIncludeDirs(C_INCLUDE_DIRS);
  if (!CIncludeDirs.empty()) {
    llvm::SmallVector<llvm::StringRef, 5> dirs;
    CIncludeDirs.split(dirs, ":");
    for (llvm::StringRef dir : dirs) {
      llvm::StringRef Prefix =
          llvm::sys::path::is_absolute(dir) ? "" : llvm::StringRef(Sysroot);
      addExternCSystemInclude(DriverArgs, CC1Args, Prefix + dir);
    }
  } else {
    // Otherwise, add <sysroot>/usr/include.
    SmallString<128> P(Sysroot);
    llvm::sys::path::append(P, "usr", "include");
    addExternCSystemInclude(DriverArgs, CC1Args, P.str());
  }
}

bool DarwinClang::AddGnuCPlusPlusIncludePaths(const llvm::opt::ArgList &DriverArgs,
                                              llvm::opt::ArgStringList &CC1Args,
                                              llvm::SmallString<128> Base,
                                              llvm::StringRef Version,
                                              llvm::StringRef ArchDir,
                                              llvm::StringRef BitDir) const {
  llvm::sys::path::append(Base, Version);

  // Add the base dir
  addSystemInclude(DriverArgs, CC1Args, Base);

  // Add the multilib dirs
  {
    llvm::SmallString<128> P = Base;
    if (!ArchDir.empty())
      llvm::sys::path::append(P, ArchDir);
    if (!BitDir.empty())
      llvm::sys::path::append(P, BitDir);
    addSystemInclude(DriverArgs, CC1Args, P);
  }

  // Add the backward dir
  {
    llvm::SmallString<128> P = Base;
    llvm::sys::path::append(P, "backward");
    addSystemInclude(DriverArgs, CC1Args, P);
  }

  return getVFS().exists(Base);
}

void DarwinClang::AddClangCXXStdlibIncludeArgs(
    const llvm::opt::ArgList &DriverArgs,
    llvm::opt::ArgStringList &CC1Args) const {
  // The implementation from a base class will pass through the -stdlib to
  // CC1Args.
  // FIXME: this should not be necessary, remove usages in the frontend
  //        (e.g. HeaderSearchOptions::UseLibcxx) and don't pipe -stdlib.
  //        Also check whether this is used for setting library search paths.
  ToolChain::AddClangCXXStdlibIncludeArgs(DriverArgs, CC1Args);

  if (DriverArgs.hasArg(options::OPT_nostdinc, options::OPT_nostdlibinc,
                        options::OPT_nostdincxx))
    return;

  llvm::SmallString<128> Sysroot = GetEffectiveSysroot(DriverArgs);

  switch (GetCXXStdlibType(DriverArgs)) {
  case ToolChain::CST_Libcxx: {
    // On Darwin, libc++ can be installed in one of the following places:
    // 1. Alongside the compiler in <clang-executable-folder>/../include/c++/v1
    // 2. In a SDK (or a custom sysroot) in <sysroot>/usr/include/c++/v1
    //
    // The precedence of paths is as listed above, i.e. we take the first path
    // that exists. Note that we never include libc++ twice -- we take the first
    // path that exists and don't send the other paths to CC1 (otherwise
    // include_next could break).

    // Check for (1)
    // Get from '<install>/bin' to '<install>/include/c++/v1'.
    // Note that InstallBin can be relative, so we use '..' instead of
    // parent_path.
    llvm::SmallString<128> InstallBin(getDriver().Dir); // <install>/bin
    llvm::sys::path::append(InstallBin, "..", "include", "c++", "v1");
    if (getVFS().exists(InstallBin)) {
      addSystemInclude(DriverArgs, CC1Args, InstallBin);
      return;
    } else if (DriverArgs.hasArg(options::OPT_v)) {
      llvm::errs() << "ignoring nonexistent directory \"" << InstallBin
                   << "\"\n";
    }

    // Otherwise, check for (2)
    llvm::SmallString<128> SysrootUsr = Sysroot;
    llvm::sys::path::append(SysrootUsr, "usr", "include", "c++", "v1");
    if (getVFS().exists(SysrootUsr)) {
      addSystemInclude(DriverArgs, CC1Args, SysrootUsr);
      return;
    } else if (DriverArgs.hasArg(options::OPT_v)) {
      llvm::errs() << "ignoring nonexistent directory \"" << SysrootUsr
                   << "\"\n";
    }

    // Otherwise, don't add any path.
    break;
  }

  case ToolChain::CST_Libstdcxx:
    llvm::SmallString<128> UsrIncludeCxx = Sysroot;
    llvm::sys::path::append(UsrIncludeCxx, "usr", "include", "c++");

    llvm::Triple::ArchType arch = getTriple().getArch();
    bool IsBaseFound = true;
    switch (arch) {
    default: break;

    case llvm::Triple::x86:
    case llvm::Triple::x86_64:
      IsBaseFound = AddGnuCPlusPlusIncludePaths(DriverArgs, CC1Args, UsrIncludeCxx,
                                                "4.2.1",
                                                "i686-apple-darwin10",
                                                arch == llvm::Triple::x86_64 ? "x86_64" : "");
      IsBaseFound |= AddGnuCPlusPlusIncludePaths(DriverArgs, CC1Args, UsrIncludeCxx,
                                                "4.0.0", "i686-apple-darwin8",
                                                 "");
      break;

    case llvm::Triple::arm:
    case llvm::Triple::thumb:
      IsBaseFound = AddGnuCPlusPlusIncludePaths(DriverArgs, CC1Args, UsrIncludeCxx,
                                                "4.2.1",
                                                "arm-apple-darwin10",
                                                "v7");
      IsBaseFound |= AddGnuCPlusPlusIncludePaths(DriverArgs, CC1Args, UsrIncludeCxx,
                                                "4.2.1",
                                                "arm-apple-darwin10",
                                                 "v6");
      break;

    case llvm::Triple::aarch64:
      IsBaseFound = AddGnuCPlusPlusIncludePaths(DriverArgs, CC1Args, UsrIncludeCxx,
                                                "4.2.1",
                                                "arm64-apple-darwin10",
                                                "");
      break;
    }

    if (!IsBaseFound) {
      getDriver().Diag(diag::warn_drv_libstdcxx_not_found);
    }

    break;
  }
}

void DarwinClang::AddCXXStdlibLibArgs(const ArgList &Args,
                                      ArgStringList &CmdArgs) const {
  CXXStdlibType Type = GetCXXStdlibType(Args);

  switch (Type) {
  case ToolChain::CST_Libcxx:
    CmdArgs.push_back("-lc++");
    if (Args.hasArg(options::OPT_fexperimental_library))
      CmdArgs.push_back("-lc++experimental");
    break;

  case ToolChain::CST_Libstdcxx:
    // Unfortunately, -lstdc++ doesn't always exist in the standard search path;
    // it was previously found in the gcc lib dir. However, for all the Darwin
    // platforms we care about it was -lstdc++.6, so we search for that
    // explicitly if we can't see an obvious -lstdc++ candidate.

    // Check in the sysroot first.
    if (const Arg *A = Args.getLastArg(options::OPT_isysroot)) {
      SmallString<128> P(A->getValue());
      llvm::sys::path::append(P, "usr", "lib", "libstdc++.dylib");

      if (!getVFS().exists(P)) {
        llvm::sys::path::remove_filename(P);
        llvm::sys::path::append(P, "libstdc++.6.dylib");
        if (getVFS().exists(P)) {
          CmdArgs.push_back(Args.MakeArgString(P));
          return;
        }
      }
    }

    // Otherwise, look in the root.
    // FIXME: This should be removed someday when we don't have to care about
    // 10.6 and earlier, where /usr/lib/libstdc++.dylib does not exist.
    if (!getVFS().exists("/usr/lib/libstdc++.dylib") &&
        getVFS().exists("/usr/lib/libstdc++.6.dylib")) {
      CmdArgs.push_back("/usr/lib/libstdc++.6.dylib");
      return;
    }

    // Otherwise, let the linker search.
    CmdArgs.push_back("-lstdc++");
    break;
  }
}

void DarwinClang::AddCCKextLibArgs(const ArgList &Args,
                                   ArgStringList &CmdArgs) const {
  // For Darwin platforms, use the compiler-rt-based support library
  // instead of the gcc-provided one (which is also incidentally
  // only present in the gcc lib dir, which makes it hard to find).

  SmallString<128> P(getDriver().ResourceDir);
  llvm::sys::path::append(P, "lib", "darwin");

  // Use the newer cc_kext for iOS ARM after 6.0.
  if (isTargetWatchOS()) {
    llvm::sys::path::append(P, "libclang_rt.cc_kext_watchos.a");
  } else if (isTargetTvOS()) {
    llvm::sys::path::append(P, "libclang_rt.cc_kext_tvos.a");
  } else if (isTargetIPhoneOS()) {
    llvm::sys::path::append(P, "libclang_rt.cc_kext_ios.a");
  } else if (isTargetDriverKit()) {
    // DriverKit doesn't want extra runtime support.
  } else if (isTargetXROSDevice()) {
    llvm::sys::path::append(
        P, llvm::Twine("libclang_rt.cc_kext_") +
               llvm::Triple::getOSTypeName(llvm::Triple::XROS) + ".a");
  } else {
    llvm::sys::path::append(P, "libclang_rt.cc_kext.a");
  }

  // For now, allow missing resource libraries to support developers who may
  // not have compiler-rt checked out or integrated into their build.
  if (getVFS().exists(P))
    CmdArgs.push_back(Args.MakeArgString(P));
}

DerivedArgList *MachO::TranslateArgs(const DerivedArgList &Args,
                                     StringRef BoundArch,
                                     Action::OffloadKind) const {
  DerivedArgList *DAL = new DerivedArgList(Args.getBaseArgs());
  const OptTable &Opts = getDriver().getOpts();

  // FIXME: We really want to get out of the tool chain level argument
  // translation business, as it makes the driver functionality much
  // more opaque. For now, we follow gcc closely solely for the
  // purpose of easily achieving feature parity & testability. Once we
  // have something that works, we should reevaluate each translation
  // and try to push it down into tool specific logic.

  for (Arg *A : Args) {
    if (A->getOption().matches(options::OPT_Xarch__)) {
      // Skip this argument unless the architecture matches either the toolchain
      // triple arch, or the arch being bound.
      StringRef XarchArch = A->getValue(0);
      if (!(XarchArch == getArchName() ||
            (!BoundArch.empty() && XarchArch == BoundArch)))
        continue;

      Arg *OriginalArg = A;
      TranslateXarchArgs(Args, A, DAL);

      // Linker input arguments require custom handling. The problem is that we
      // have already constructed the phase actions, so we can not treat them as
      // "input arguments".
      if (A->getOption().hasFlag(options::LinkerInput)) {
        // Convert the argument into individual Zlinker_input_args.
        for (const char *Value : A->getValues()) {
          DAL->AddSeparateArg(
              OriginalArg, Opts.getOption(options::OPT_Zlinker_input), Value);
        }
        continue;
      }
    }

    // Sob. These is strictly gcc compatible for the time being. Apple
    // gcc translates options twice, which means that self-expanding
    // options add duplicates.
    switch ((options::ID)A->getOption().getID()) {
    default:
      DAL->append(A);
      break;

    case options::OPT_mkernel:
    case options::OPT_fapple_kext:
      DAL->append(A);
      DAL->AddFlagArg(A, Opts.getOption(options::OPT_static));
      break;

    case options::OPT_dependency_file:
      DAL->AddSeparateArg(A, Opts.getOption(options::OPT_MF), A->getValue());
      break;

    case options::OPT_gfull:
      DAL->AddFlagArg(A, Opts.getOption(options::OPT_g_Flag));
      DAL->AddFlagArg(
          A, Opts.getOption(options::OPT_fno_eliminate_unused_debug_symbols));
      break;

    case options::OPT_gused:
      DAL->AddFlagArg(A, Opts.getOption(options::OPT_g_Flag));
      DAL->AddFlagArg(
          A, Opts.getOption(options::OPT_feliminate_unused_debug_symbols));
      break;

    case options::OPT_shared:
      DAL->AddFlagArg(A, Opts.getOption(options::OPT_dynamiclib));
      break;

    case options::OPT_fconstant_cfstrings:
      DAL->AddFlagArg(A, Opts.getOption(options::OPT_mconstant_cfstrings));
      break;

    case options::OPT_fno_constant_cfstrings:
      DAL->AddFlagArg(A, Opts.getOption(options::OPT_mno_constant_cfstrings));
      break;

    case options::OPT_Wnonportable_cfstrings:
      DAL->AddFlagArg(A,
                      Opts.getOption(options::OPT_mwarn_nonportable_cfstrings));
      break;

    case options::OPT_Wno_nonportable_cfstrings:
      DAL->AddFlagArg(
          A, Opts.getOption(options::OPT_mno_warn_nonportable_cfstrings));
      break;
    }
  }

  // Add the arch options based on the particular spelling of -arch, to match
  // how the driver works.
  if (!BoundArch.empty()) {
    StringRef Name = BoundArch;
    const Option MCpu = Opts.getOption(options::OPT_mcpu_EQ);
    const Option MArch = Opts.getOption(clang::driver::options::OPT_march_EQ);

    // This code must be kept in sync with LLVM's getArchTypeForDarwinArch,
    // which defines the list of which architectures we accept.
    if (Name == "ppc")
      ;
    else if (Name == "ppc601")
      DAL->AddJoinedArg(nullptr, MCpu, "601");
    else if (Name == "ppc603")
      DAL->AddJoinedArg(nullptr, MCpu, "603");
    else if (Name == "ppc604")
      DAL->AddJoinedArg(nullptr, MCpu, "604");
    else if (Name == "ppc604e")
      DAL->AddJoinedArg(nullptr, MCpu, "604e");
    else if (Name == "ppc750")
      DAL->AddJoinedArg(nullptr, MCpu, "750");
    else if (Name == "ppc7400")
      DAL->AddJoinedArg(nullptr, MCpu, "7400");
    else if (Name == "ppc7450")
      DAL->AddJoinedArg(nullptr, MCpu, "7450");
    else if (Name == "ppc970")
      DAL->AddJoinedArg(nullptr, MCpu, "970");

    else if (Name == "ppc64" || Name == "ppc64le")
      DAL->AddFlagArg(nullptr, Opts.getOption(options::OPT_m64));

    else if (Name == "i386")
      ;
    else if (Name == "i486")
      DAL->AddJoinedArg(nullptr, MArch, "i486");
    else if (Name == "i586")
      DAL->AddJoinedArg(nullptr, MArch, "i586");
    else if (Name == "i686")
      DAL->AddJoinedArg(nullptr, MArch, "i686");
    else if (Name == "pentium")
      DAL->AddJoinedArg(nullptr, MArch, "pentium");
    else if (Name == "pentium2")
      DAL->AddJoinedArg(nullptr, MArch, "pentium2");
    else if (Name == "pentpro")
      DAL->AddJoinedArg(nullptr, MArch, "pentiumpro");
    else if (Name == "pentIIm3")
      DAL->AddJoinedArg(nullptr, MArch, "pentium2");

    else if (Name == "x86_64" || Name == "x86_64h")
      DAL->AddFlagArg(nullptr, Opts.getOption(options::OPT_m64));

    else if (Name == "arm")
      DAL->AddJoinedArg(nullptr, MArch, "armv4t");
    else if (Name == "armv4t")
      DAL->AddJoinedArg(nullptr, MArch, "armv4t");
    else if (Name == "armv5")
      DAL->AddJoinedArg(nullptr, MArch, "armv5tej");
    else if (Name == "xscale")
      DAL->AddJoinedArg(nullptr, MArch, "xscale");
    else if (Name == "armv6")
      DAL->AddJoinedArg(nullptr, MArch, "armv6k");
    else if (Name == "armv6m")
      DAL->AddJoinedArg(nullptr, MArch, "armv6m");
    else if (Name == "armv7")
      DAL->AddJoinedArg(nullptr, MArch, "armv7a");
    else if (Name == "armv7em")
      DAL->AddJoinedArg(nullptr, MArch, "armv7em");
    else if (Name == "armv7k")
      DAL->AddJoinedArg(nullptr, MArch, "armv7k");
    else if (Name == "armv7m")
      DAL->AddJoinedArg(nullptr, MArch, "armv7m");
    else if (Name == "armv7s")
      DAL->AddJoinedArg(nullptr, MArch, "armv7s");
  }

  return DAL;
}

void MachO::AddLinkRuntimeLibArgs(const ArgList &Args,
                                  ArgStringList &CmdArgs,
                                  bool ForceLinkBuiltinRT) const {
  // Embedded targets are simple at the moment, not supporting sanitizers and
  // with different libraries for each member of the product { static, PIC } x
  // { hard-float, soft-float }
  llvm::SmallString<32> CompilerRT = StringRef("");
  CompilerRT +=
      (tools::arm::getARMFloatABI(*this, Args) == tools::arm::FloatABI::Hard)
          ? "hard"
          : "soft";
  CompilerRT += Args.hasArg(options::OPT_fPIC) ? "_pic" : "_static";

  AddLinkRuntimeLib(Args, CmdArgs, CompilerRT, RLO_IsEmbedded);
}

bool Darwin::isAlignedAllocationUnavailable() const {
  llvm::Triple::OSType OS;

  if (isTargetMacCatalyst())
    return TargetVersion < alignedAllocMinVersion(llvm::Triple::MacOSX);
  switch (TargetPlatform) {
  case MacOS: // Earlier than 10.13.
    OS = llvm::Triple::MacOSX;
    break;
  case IPhoneOS:
    OS = llvm::Triple::IOS;
    break;
  case TvOS: // Earlier than 11.0.
    OS = llvm::Triple::TvOS;
    break;
  case WatchOS: // Earlier than 4.0.
    OS = llvm::Triple::WatchOS;
    break;
  case XROS: // Always available.
    return false;
  case DriverKit: // Always available.
    return false;
  }

  return TargetVersion < alignedAllocMinVersion(OS);
}

static bool sdkSupportsBuiltinModules(
    const Darwin::DarwinPlatformKind &TargetPlatform,
    const Darwin::DarwinEnvironmentKind &TargetEnvironment,
    const std::optional<DarwinSDKInfo> &SDKInfo) {
  if (TargetEnvironment == Darwin::NativeEnvironment ||
      TargetEnvironment == Darwin::Simulator ||
      TargetEnvironment == Darwin::MacCatalyst) {
    // Standard xnu/Mach/Darwin based environments
    // depend on the SDK version.
  } else {
    // All other environments support builtin modules from the start.
    return true;
  }

  if (!SDKInfo)
    // If there is no SDK info, assume this is building against a
    // pre-SDK version of macOS (i.e. before Mac OS X 10.4). Those
    // don't support modules anyway, but the headers definitely
    // don't support builtin modules either. It might also be some
    // kind of degenerate build environment, err on the side of
    // the old behavior which is to not use builtin modules.
    return false;

  VersionTuple SDKVersion = SDKInfo->getVersion();
  switch (TargetPlatform) {
  // Existing SDKs added support for builtin modules in the fall
  // 2024 major releases.
  case Darwin::MacOS:
    return SDKVersion >= VersionTuple(15U);
  case Darwin::IPhoneOS:
    switch (TargetEnvironment) {
    case Darwin::MacCatalyst:
      // Mac Catalyst uses `-target arm64-apple-ios18.0-macabi` so the platform
      // is iOS, but it builds with the macOS SDK, so it's the macOS SDK version
      // that's relevant.
      return SDKVersion >= VersionTuple(15U);
    default:
      return SDKVersion >= VersionTuple(18U);
    }
  case Darwin::TvOS:
    return SDKVersion >= VersionTuple(18U);
  case Darwin::WatchOS:
    return SDKVersion >= VersionTuple(11U);
  case Darwin::XROS:
    return SDKVersion >= VersionTuple(2U);

  // New SDKs support builtin modules from the start.
  default:
    return true;
  }
}

static inline llvm::VersionTuple
sizedDeallocMinVersion(llvm::Triple::OSType OS) {
  switch (OS) {
  default:
    break;
  case llvm::Triple::Darwin:
  case llvm::Triple::MacOSX: // Earliest supporting version is 10.12.
    return llvm::VersionTuple(10U, 12U);
  case llvm::Triple::IOS:
  case llvm::Triple::TvOS: // Earliest supporting version is 10.0.0.
    return llvm::VersionTuple(10U);
  case llvm::Triple::WatchOS: // Earliest supporting version is 3.0.0.
    return llvm::VersionTuple(3U);
  }

  llvm_unreachable("Unexpected OS");
}

bool Darwin::isSizedDeallocationUnavailable() const {
  llvm::Triple::OSType OS;

  if (isTargetMacCatalyst())
    return TargetVersion < sizedDeallocMinVersion(llvm::Triple::MacOSX);
  switch (TargetPlatform) {
  case MacOS: // Earlier than 10.12.
    OS = llvm::Triple::MacOSX;
    break;
  case IPhoneOS:
    OS = llvm::Triple::IOS;
    break;
  case TvOS: // Earlier than 10.0.
    OS = llvm::Triple::TvOS;
    break;
  case WatchOS: // Earlier than 3.0.
    OS = llvm::Triple::WatchOS;
    break;
  case DriverKit:
  case XROS:
    // Always available.
    return false;
  }

  return TargetVersion < sizedDeallocMinVersion(OS);
}

void Darwin::addClangTargetOptions(
    const llvm::opt::ArgList &DriverArgs, llvm::opt::ArgStringList &CC1Args,
    Action::OffloadKind DeviceOffloadKind) const {
  // Pass "-faligned-alloc-unavailable" only when the user hasn't manually
  // enabled or disabled aligned allocations.
  if (!DriverArgs.hasArgNoClaim(options::OPT_faligned_allocation,
                                options::OPT_fno_aligned_allocation) &&
      isAlignedAllocationUnavailable())
    CC1Args.push_back("-faligned-alloc-unavailable");

  // Pass "-fno-sized-deallocation" only when the user hasn't manually enabled
  // or disabled sized deallocations.
  if (!DriverArgs.hasArgNoClaim(options::OPT_fsized_deallocation,
                                options::OPT_fno_sized_deallocation) &&
      isSizedDeallocationUnavailable())
    CC1Args.push_back("-fno-sized-deallocation");

  addClangCC1ASTargetOptions(DriverArgs, CC1Args);

  // Enable compatibility mode for NSItemProviderCompletionHandler in
  // Foundation/NSItemProvider.h.
  CC1Args.push_back("-fcompatibility-qualified-id-block-type-checking");

  // Give static local variables in inline functions hidden visibility when
  // -fvisibility-inlines-hidden is enabled.
  if (!DriverArgs.getLastArgNoClaim(
          options::OPT_fvisibility_inlines_hidden_static_local_var,
          options::OPT_fno_visibility_inlines_hidden_static_local_var))
    CC1Args.push_back("-fvisibility-inlines-hidden-static-local-var");

  // Earlier versions of the darwin SDK have the C standard library headers
  // all together in the Darwin module. That leads to module cycles with
  // the _Builtin_ modules. e.g. <inttypes.h> on darwin includes <stdint.h>.
  // The builtin <stdint.h> include-nexts <stdint.h>. When both of those
  // darwin headers are in the Darwin module, there's a module cycle Darwin ->
  // _Builtin_stdint -> Darwin (i.e. inttypes.h (darwin) -> stdint.h (builtin) ->
  // stdint.h (darwin)). This is fixed in later versions of the darwin SDK,
  // but until then, the builtin headers need to join the system modules.
  // i.e. when the builtin stdint.h is in the Darwin module too, the cycle
  // goes away. Note that -fbuiltin-headers-in-system-modules does nothing
  // to fix the same problem with C++ headers, and is generally fragile.
  if (!sdkSupportsBuiltinModules(TargetPlatform, TargetEnvironment, SDKInfo))
    CC1Args.push_back("-fbuiltin-headers-in-system-modules");

  if (!DriverArgs.hasArgNoClaim(options::OPT_fdefine_target_os_macros,
                                options::OPT_fno_define_target_os_macros))
    CC1Args.push_back("-fdefine-target-os-macros");
}

void Darwin::addClangCC1ASTargetOptions(
    const llvm::opt::ArgList &Args, llvm::opt::ArgStringList &CC1ASArgs) const {
  if (TargetVariantTriple) {
    CC1ASArgs.push_back("-darwin-target-variant-triple");
    CC1ASArgs.push_back(Args.MakeArgString(TargetVariantTriple->getTriple()));
  }

  if (SDKInfo) {
    /// Pass the SDK version to the compiler when the SDK information is
    /// available.
    auto EmitTargetSDKVersionArg = [&](const VersionTuple &V) {
      std::string Arg;
      llvm::raw_string_ostream OS(Arg);
      OS << "-target-sdk-version=" << V;
      CC1ASArgs.push_back(Args.MakeArgString(Arg));
    };

    if (isTargetMacCatalyst()) {
      if (const auto *MacOStoMacCatalystMapping = SDKInfo->getVersionMapping(
              DarwinSDKInfo::OSEnvPair::macOStoMacCatalystPair())) {
        std::optional<VersionTuple> SDKVersion = MacOStoMacCatalystMapping->map(
            SDKInfo->getVersion(), minimumMacCatalystDeploymentTarget(),
            std::nullopt);
        EmitTargetSDKVersionArg(
            SDKVersion ? *SDKVersion : minimumMacCatalystDeploymentTarget());
      }
    } else {
      EmitTargetSDKVersionArg(SDKInfo->getVersion());
    }

    /// Pass the target variant SDK version to the compiler when the SDK
    /// information is available and is required for target variant.
    if (TargetVariantTriple) {
      if (isTargetMacCatalyst()) {
        std::string Arg;
        llvm::raw_string_ostream OS(Arg);
        OS << "-darwin-target-variant-sdk-version=" << SDKInfo->getVersion();
        CC1ASArgs.push_back(Args.MakeArgString(Arg));
      } else if (const auto *MacOStoMacCatalystMapping =
                     SDKInfo->getVersionMapping(
                         DarwinSDKInfo::OSEnvPair::macOStoMacCatalystPair())) {
        if (std::optional<VersionTuple> SDKVersion =
                MacOStoMacCatalystMapping->map(
                    SDKInfo->getVersion(), minimumMacCatalystDeploymentTarget(),
                    std::nullopt)) {
          std::string Arg;
          llvm::raw_string_ostream OS(Arg);
          OS << "-darwin-target-variant-sdk-version=" << *SDKVersion;
          CC1ASArgs.push_back(Args.MakeArgString(Arg));
        }
      }
    }
  }
}

DerivedArgList *
Darwin::TranslateArgs(const DerivedArgList &Args, StringRef BoundArch,
                      Action::OffloadKind DeviceOffloadKind) const {
  // First get the generic Apple args, before moving onto Darwin-specific ones.
  DerivedArgList *DAL =
      MachO::TranslateArgs(Args, BoundArch, DeviceOffloadKind);

  // If no architecture is bound, none of the translations here are relevant.
  if (BoundArch.empty())
    return DAL;

  // Add an explicit version min argument for the deployment target. We do this
  // after argument translation because -Xarch_ arguments may add a version min
  // argument.
  AddDeploymentTarget(*DAL);

  // For iOS 6, undo the translation to add -static for -mkernel/-fapple-kext.
  // FIXME: It would be far better to avoid inserting those -static arguments,
  // but we can't check the deployment target in the translation code until
  // it is set here.
  if (isTargetWatchOSBased() || isTargetDriverKit() || isTargetXROS() ||
      (isTargetIOSBased() && !isIPhoneOSVersionLT(6, 0))) {
    for (ArgList::iterator it = DAL->begin(), ie = DAL->end(); it != ie; ) {
      Arg *A = *it;
      ++it;
      if (A->getOption().getID() != options::OPT_mkernel &&
          A->getOption().getID() != options::OPT_fapple_kext)
        continue;
      assert(it != ie && "unexpected argument translation");
      A = *it;
      assert(A->getOption().getID() == options::OPT_static &&
             "missing expected -static argument");
      *it = nullptr;
      ++it;
    }
  }

  auto Arch = tools::darwin::getArchTypeForMachOArchName(BoundArch);
  if ((Arch == llvm::Triple::arm || Arch == llvm::Triple::thumb)) {
    if (Args.hasFlag(options::OPT_fomit_frame_pointer,
                     options::OPT_fno_omit_frame_pointer, false))
      getDriver().Diag(clang::diag::warn_drv_unsupported_opt_for_target)
          << "-fomit-frame-pointer" << BoundArch;
  }

  return DAL;
}

ToolChain::UnwindTableLevel MachO::getDefaultUnwindTableLevel(const ArgList &Args) const {
  // Unwind tables are not emitted if -fno-exceptions is supplied (except when
  // targeting x86_64).
  if (getArch() == llvm::Triple::x86_64 ||
      (GetExceptionModel(Args) != llvm::ExceptionHandling::SjLj &&
       Args.hasFlag(options::OPT_fexceptions, options::OPT_fno_exceptions,
                    true)))
    return (getArch() == llvm::Triple::aarch64 ||
            getArch() == llvm::Triple::aarch64_32)
               ? UnwindTableLevel::Synchronous
               : UnwindTableLevel::Asynchronous;

  return UnwindTableLevel::None;
}

bool MachO::UseDwarfDebugFlags() const {
  if (const char *S = ::getenv("RC_DEBUG_OPTIONS"))
    return S[0] != '\0';
  return false;
}

std::string MachO::GetGlobalDebugPathRemapping() const {
  if (const char *S = ::getenv("RC_DEBUG_PREFIX_MAP"))
    return S;
  return {};
}

llvm::ExceptionHandling Darwin::GetExceptionModel(const ArgList &Args) const {
  // Darwin uses SjLj exceptions on ARM.
  if (getTriple().getArch() != llvm::Triple::arm &&
      getTriple().getArch() != llvm::Triple::thumb)
    return llvm::ExceptionHandling::None;

  // Only watchOS uses the new DWARF/Compact unwinding method.
  llvm::Triple Triple(ComputeLLVMTriple(Args));
  if (Triple.isWatchABI())
    return llvm::ExceptionHandling::DwarfCFI;

  return llvm::ExceptionHandling::SjLj;
}

bool Darwin::SupportsEmbeddedBitcode() const {
  assert(TargetInitialized && "Target not initialized!");
  if (isTargetIPhoneOS() && isIPhoneOSVersionLT(6, 0))
    return false;
  return true;
}

bool MachO::isPICDefault() const { return true; }

bool MachO::isPIEDefault(const llvm::opt::ArgList &Args) const { return false; }

bool MachO::isPICDefaultForced() const {
  return (getArch() == llvm::Triple::x86_64 ||
          getArch() == llvm::Triple::aarch64);
}

bool MachO::SupportsProfiling() const {
  // Profiling instrumentation is only supported on x86.
  return getTriple().isX86();
}

void Darwin::addMinVersionArgs(const ArgList &Args,
                               ArgStringList &CmdArgs) const {
  VersionTuple TargetVersion = getTripleTargetVersion();

  assert(!isTargetXROS() && "xrOS always uses -platform-version");

  if (isTargetWatchOS())
    CmdArgs.push_back("-watchos_version_min");
  else if (isTargetWatchOSSimulator())
    CmdArgs.push_back("-watchos_simulator_version_min");
  else if (isTargetTvOS())
    CmdArgs.push_back("-tvos_version_min");
  else if (isTargetTvOSSimulator())
    CmdArgs.push_back("-tvos_simulator_version_min");
  else if (isTargetDriverKit())
    CmdArgs.push_back("-driverkit_version_min");
  else if (isTargetIOSSimulator())
    CmdArgs.push_back("-ios_simulator_version_min");
  else if (isTargetIOSBased())
    CmdArgs.push_back("-iphoneos_version_min");
  else if (isTargetMacCatalyst())
    CmdArgs.push_back("-maccatalyst_version_min");
  else {
    assert(isTargetMacOS() && "unexpected target");
    CmdArgs.push_back("-macosx_version_min");
  }

  VersionTuple MinTgtVers = getEffectiveTriple().getMinimumSupportedOSVersion();
  if (!MinTgtVers.empty() && MinTgtVers > TargetVersion)
    TargetVersion = MinTgtVers;
  CmdArgs.push_back(Args.MakeArgString(TargetVersion.getAsString()));
  if (TargetVariantTriple) {
    assert(isTargetMacOSBased() && "unexpected target");
    VersionTuple VariantTargetVersion;
    if (TargetVariantTriple->isMacOSX()) {
      CmdArgs.push_back("-macosx_version_min");
      TargetVariantTriple->getMacOSXVersion(VariantTargetVersion);
    } else {
      assert(TargetVariantTriple->isiOS() &&
             TargetVariantTriple->isMacCatalystEnvironment() &&
             "unexpected target variant triple");
      CmdArgs.push_back("-maccatalyst_version_min");
      VariantTargetVersion = TargetVariantTriple->getiOSVersion();
    }
    VersionTuple MinTgtVers =
        TargetVariantTriple->getMinimumSupportedOSVersion();
    if (MinTgtVers.getMajor() && MinTgtVers > VariantTargetVersion)
      VariantTargetVersion = MinTgtVers;
    CmdArgs.push_back(Args.MakeArgString(VariantTargetVersion.getAsString()));
  }
}

static const char *getPlatformName(Darwin::DarwinPlatformKind Platform,
                                   Darwin::DarwinEnvironmentKind Environment) {
  switch (Platform) {
  case Darwin::MacOS:
    return "macos";
  case Darwin::IPhoneOS:
    if (Environment == Darwin::MacCatalyst)
      return "mac catalyst";
    return "ios";
  case Darwin::TvOS:
    return "tvos";
  case Darwin::WatchOS:
    return "watchos";
  case Darwin::XROS:
    return "xros";
  case Darwin::DriverKit:
    return "driverkit";
  }
  llvm_unreachable("invalid platform");
}

void Darwin::addPlatformVersionArgs(const llvm::opt::ArgList &Args,
                                    llvm::opt::ArgStringList &CmdArgs) const {
  auto EmitPlatformVersionArg =
      [&](const VersionTuple &TV, Darwin::DarwinPlatformKind TargetPlatform,
          Darwin::DarwinEnvironmentKind TargetEnvironment,
          const llvm::Triple &TT) {
        // -platform_version <platform> <target_version> <sdk_version>
        // Both the target and SDK version support only up to 3 components.
        CmdArgs.push_back("-platform_version");
        std::string PlatformName =
            getPlatformName(TargetPlatform, TargetEnvironment);
        if (TargetEnvironment == Darwin::Simulator)
          PlatformName += "-simulator";
        CmdArgs.push_back(Args.MakeArgString(PlatformName));
        VersionTuple TargetVersion = TV.withoutBuild();
        if ((TargetPlatform == Darwin::IPhoneOS ||
             TargetPlatform == Darwin::TvOS) &&
            getTriple().getArchName() == "arm64e" &&
            TargetVersion.getMajor() < 14) {
          // arm64e slice is supported on iOS/tvOS 14+ only.
          TargetVersion = VersionTuple(14, 0);
        }
        VersionTuple MinTgtVers = TT.getMinimumSupportedOSVersion();
        if (!MinTgtVers.empty() && MinTgtVers > TargetVersion)
          TargetVersion = MinTgtVers;
        CmdArgs.push_back(Args.MakeArgString(TargetVersion.getAsString()));

        if (TargetPlatform == IPhoneOS && TargetEnvironment == MacCatalyst) {
          // Mac Catalyst programs must use the appropriate iOS SDK version
          // that corresponds to the macOS SDK version used for the compilation.
          std::optional<VersionTuple> iOSSDKVersion;
          if (SDKInfo) {
            if (const auto *MacOStoMacCatalystMapping =
                    SDKInfo->getVersionMapping(
                        DarwinSDKInfo::OSEnvPair::macOStoMacCatalystPair())) {
              iOSSDKVersion = MacOStoMacCatalystMapping->map(
                  SDKInfo->getVersion().withoutBuild(),
                  minimumMacCatalystDeploymentTarget(), std::nullopt);
            }
          }
          CmdArgs.push_back(Args.MakeArgString(
              (iOSSDKVersion ? *iOSSDKVersion
                             : minimumMacCatalystDeploymentTarget())
                  .getAsString()));
          return;
        }

        if (SDKInfo) {
          VersionTuple SDKVersion = SDKInfo->getVersion().withoutBuild();
          if (!SDKVersion.getMinor())
            SDKVersion = VersionTuple(SDKVersion.getMajor(), 0);
          CmdArgs.push_back(Args.MakeArgString(SDKVersion.getAsString()));
        } else {
          // Use an SDK version that's matching the deployment target if the SDK
          // version is missing. This is preferred over an empty SDK version
          // (0.0.0) as the system's runtime might expect the linked binary to
          // contain a valid SDK version in order for the binary to work
          // correctly. It's reasonable to use the deployment target version as
          // a proxy for the SDK version because older SDKs don't guarantee
          // support for deployment targets newer than the SDK versions, so that
          // rules out using some predetermined older SDK version, which leaves
          // the deployment target version as the only reasonable choice.
          CmdArgs.push_back(Args.MakeArgString(TargetVersion.getAsString()));
        }
      };
  EmitPlatformVersionArg(getTripleTargetVersion(), TargetPlatform,
                         TargetEnvironment, getEffectiveTriple());
  if (!TargetVariantTriple)
    return;
  Darwin::DarwinPlatformKind Platform;
  Darwin::DarwinEnvironmentKind Environment;
  VersionTuple TargetVariantVersion;
  if (TargetVariantTriple->isMacOSX()) {
    TargetVariantTriple->getMacOSXVersion(TargetVariantVersion);
    Platform = Darwin::MacOS;
    Environment = Darwin::NativeEnvironment;
  } else {
    assert(TargetVariantTriple->isiOS() &&
           TargetVariantTriple->isMacCatalystEnvironment() &&
           "unexpected target variant triple");
    TargetVariantVersion = TargetVariantTriple->getiOSVersion();
    Platform = Darwin::IPhoneOS;
    Environment = Darwin::MacCatalyst;
  }
  EmitPlatformVersionArg(TargetVariantVersion, Platform, Environment,
                         *TargetVariantTriple);
}

// Add additional link args for the -dynamiclib option.
static void addDynamicLibLinkArgs(const Darwin &D, const ArgList &Args,
                                  ArgStringList &CmdArgs) {
  // Derived from darwin_dylib1 spec.
  if (D.isTargetIPhoneOS()) {
    if (D.isIPhoneOSVersionLT(3, 1))
      CmdArgs.push_back("-ldylib1.o");
    return;
  }

  if (!D.isTargetMacOS())
    return;
  if (D.isMacosxVersionLT(10, 5))
    CmdArgs.push_back("-ldylib1.o");
  else if (D.isMacosxVersionLT(10, 6))
    CmdArgs.push_back("-ldylib1.10.5.o");
}

// Add additional link args for the -bundle option.
static void addBundleLinkArgs(const Darwin &D, const ArgList &Args,
                              ArgStringList &CmdArgs) {
  if (Args.hasArg(options::OPT_static))
    return;
  // Derived from darwin_bundle1 spec.
  if ((D.isTargetIPhoneOS() && D.isIPhoneOSVersionLT(3, 1)) ||
      (D.isTargetMacOS() && D.isMacosxVersionLT(10, 6)))
    CmdArgs.push_back("-lbundle1.o");
}

// Add additional link args for the -pg option.
static void addPgProfilingLinkArgs(const Darwin &D, const ArgList &Args,
                                   ArgStringList &CmdArgs) {
  if (D.isTargetMacOS() && D.isMacosxVersionLT(10, 9)) {
    if (Args.hasArg(options::OPT_static) || Args.hasArg(options::OPT_object) ||
        Args.hasArg(options::OPT_preload)) {
      CmdArgs.push_back("-lgcrt0.o");
    } else {
      CmdArgs.push_back("-lgcrt1.o");

      // darwin_crt2 spec is empty.
    }
    // By default on OS X 10.8 and later, we don't link with a crt1.o
    // file and the linker knows to use _main as the entry point.  But,
    // when compiling with -pg, we need to link with the gcrt1.o file,
    // so pass the -no_new_main option to tell the linker to use the
    // "start" symbol as the entry point.
    if (!D.isMacosxVersionLT(10, 8))
      CmdArgs.push_back("-no_new_main");
  } else {
    D.getDriver().Diag(diag::err_drv_clang_unsupported_opt_pg_darwin)
        << D.isTargetMacOSBased();
  }
}

static void addDefaultCRTLinkArgs(const Darwin &D, const ArgList &Args,
                                  ArgStringList &CmdArgs) {
  // Derived from darwin_crt1 spec.
  if (D.isTargetIPhoneOS()) {
    if (D.getArch() == llvm::Triple::aarch64)
      ; // iOS does not need any crt1 files for arm64
    else if (D.isIPhoneOSVersionLT(3, 1))
      CmdArgs.push_back("-lcrt1.o");
    else if (D.isIPhoneOSVersionLT(6, 0))
      CmdArgs.push_back("-lcrt1.3.1.o");
    return;
  }

  if (!D.isTargetMacOS())
    return;
  if (D.isMacosxVersionLT(10, 5))
    CmdArgs.push_back("-lcrt1.o");
  else if (D.isMacosxVersionLT(10, 6))
    CmdArgs.push_back("-lcrt1.10.5.o");
  else if (D.isMacosxVersionLT(10, 8))
    CmdArgs.push_back("-lcrt1.10.6.o");
  // darwin_crt2 spec is empty.
}

void Darwin::addStartObjectFileArgs(const ArgList &Args,
                                    ArgStringList &CmdArgs) const {
  // Derived from startfile spec.
  if (Args.hasArg(options::OPT_dynamiclib))
    addDynamicLibLinkArgs(*this, Args, CmdArgs);
  else if (Args.hasArg(options::OPT_bundle))
    addBundleLinkArgs(*this, Args, CmdArgs);
  else if (Args.hasArg(options::OPT_pg) && SupportsProfiling())
    addPgProfilingLinkArgs(*this, Args, CmdArgs);
  else if (Args.hasArg(options::OPT_static) ||
           Args.hasArg(options::OPT_object) ||
           Args.hasArg(options::OPT_preload))
    CmdArgs.push_back("-lcrt0.o");
  else
    addDefaultCRTLinkArgs(*this, Args, CmdArgs);

  if (isTargetMacOS() && Args.hasArg(options::OPT_shared_libgcc) &&
      isMacosxVersionLT(10, 5)) {
    const char *Str = Args.MakeArgString(GetFilePath("crt3.o"));
    CmdArgs.push_back(Str);
  }
}

void Darwin::CheckObjCARC() const {
  if (isTargetIOSBased() || isTargetWatchOSBased() || isTargetXROS() ||
      (isTargetMacOSBased() && !isMacosxVersionLT(10, 6)))
    return;
  getDriver().Diag(diag::err_arc_unsupported_on_toolchain);
}

SanitizerMask Darwin::getSupportedSanitizers() const {
  const bool IsX86_64 = getTriple().getArch() == llvm::Triple::x86_64;
  const bool IsAArch64 = getTriple().getArch() == llvm::Triple::aarch64;
  SanitizerMask Res = ToolChain::getSupportedSanitizers();
  Res |= SanitizerKind::Address;
  Res |= SanitizerKind::PointerCompare;
  Res |= SanitizerKind::PointerSubtract;
  Res |= SanitizerKind::Leak;
  Res |= SanitizerKind::Fuzzer;
  Res |= SanitizerKind::FuzzerNoLink;
  Res |= SanitizerKind::ObjCCast;

  // Prior to 10.9, macOS shipped a version of the C++ standard library without
  // C++11 support. The same is true of iOS prior to version 5. These OS'es are
  // incompatible with -fsanitize=vptr.
  if (!(isTargetMacOSBased() && isMacosxVersionLT(10, 9)) &&
      !(isTargetIPhoneOS() && isIPhoneOSVersionLT(5, 0)))
    Res |= SanitizerKind::Vptr;

  if ((IsX86_64 || IsAArch64) &&
      (isTargetMacOSBased() || isTargetIOSSimulator() ||
       isTargetTvOSSimulator() || isTargetWatchOSSimulator())) {
    Res |= SanitizerKind::Thread;
  }

  if (IsX86_64)
    Res |= SanitizerKind::NumericalStability;

  return Res;
}

void Darwin::printVerboseInfo(raw_ostream &OS) const {
  CudaInstallation->print(OS);
  RocmInstallation->print(OS);
}
