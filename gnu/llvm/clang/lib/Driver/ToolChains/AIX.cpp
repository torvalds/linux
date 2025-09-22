//===--- AIX.cpp - AIX ToolChain Implementations ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AIX.h"
#include "Arch/PPC.h"
#include "CommonArgs.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Options.h"
#include "clang/Driver/SanitizerArgs.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Option/ArgList.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Support/Path.h"

#include <set>

using AIX = clang::driver::toolchains::AIX;
using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang::driver::toolchains;

using namespace llvm::opt;
using namespace llvm::sys;

void aix::Assembler::ConstructJob(Compilation &C, const JobAction &JA,
                                  const InputInfo &Output,
                                  const InputInfoList &Inputs,
                                  const ArgList &Args,
                                  const char *LinkingOutput) const {
  const Driver &D = getToolChain().getDriver();
  ArgStringList CmdArgs;

  const bool IsArch32Bit = getToolChain().getTriple().isArch32Bit();
  const bool IsArch64Bit = getToolChain().getTriple().isArch64Bit();
  // Only support 32 and 64 bit.
  if (!IsArch32Bit && !IsArch64Bit)
    llvm_unreachable("Unsupported bit width value.");

  if (Arg *A = C.getArgs().getLastArg(options::OPT_G)) {
    D.Diag(diag::err_drv_unsupported_opt_for_target)
        << A->getSpelling() << D.getTargetTriple();
  }

  // Specify the mode in which the as(1) command operates.
  if (IsArch32Bit) {
    CmdArgs.push_back("-a32");
  } else {
    // Must be 64-bit, otherwise asserted already.
    CmdArgs.push_back("-a64");
  }

  // Accept any mixture of instructions.
  // On Power for AIX and Linux, this behaviour matches that of GCC for both the
  // user-provided assembler source case and the compiler-produced assembler
  // source case. Yet XL with user-provided assembler source would not add this.
  CmdArgs.push_back("-many");

  Args.AddAllArgValues(CmdArgs, options::OPT_Wa_COMMA, options::OPT_Xassembler);

  // Specify assembler output file.
  assert((Output.isFilename() || Output.isNothing()) && "Invalid output.");
  if (Output.isFilename()) {
    CmdArgs.push_back("-o");
    CmdArgs.push_back(Output.getFilename());
  }

  // Specify assembler input file.
  // The system assembler on AIX takes exactly one input file. The driver is
  // expected to invoke as(1) separately for each assembler source input file.
  if (Inputs.size() != 1)
    llvm_unreachable("Invalid number of input files.");
  const InputInfo &II = Inputs[0];
  assert((II.isFilename() || II.isNothing()) && "Invalid input.");
  if (II.isFilename())
    CmdArgs.push_back(II.getFilename());

  const char *Exec = Args.MakeArgString(getToolChain().GetProgramPath("as"));
  C.addCommand(std::make_unique<Command>(JA, *this, ResponseFileSupport::None(),
                                         Exec, CmdArgs, Inputs, Output));
}

// Determine whether there are any linker options that supply an export list
// (or equivalent information about what to export) being sent to the linker.
static bool hasExportListLinkerOpts(const ArgStringList &CmdArgs) {
  for (size_t i = 0, Size = CmdArgs.size(); i < Size; ++i) {
    llvm::StringRef ArgString(CmdArgs[i]);

    if (ArgString.starts_with("-bE:") || ArgString.starts_with("-bexport:") ||
        ArgString == "-bexpall" || ArgString == "-bexpfull")
      return true;

    // If we split -b option, check the next opt.
    if (ArgString == "-b" && i + 1 < Size) {
      ++i;
      llvm::StringRef ArgNextString(CmdArgs[i]);
      if (ArgNextString.starts_with("E:") ||
          ArgNextString.starts_with("export:") || ArgNextString == "expall" ||
          ArgNextString == "expfull")
        return true;
    }
  }
  return false;
}

void aix::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                               const InputInfo &Output,
                               const InputInfoList &Inputs, const ArgList &Args,
                               const char *LinkingOutput) const {
  const AIX &ToolChain = static_cast<const AIX &>(getToolChain());
  const Driver &D = ToolChain.getDriver();
  ArgStringList CmdArgs;

  const bool IsArch32Bit = ToolChain.getTriple().isArch32Bit();
  const bool IsArch64Bit = ToolChain.getTriple().isArch64Bit();
  // Only support 32 and 64 bit.
  if (!(IsArch32Bit || IsArch64Bit))
    llvm_unreachable("Unsupported bit width value.");

  if (Arg *A = C.getArgs().getLastArg(options::OPT_G)) {
    D.Diag(diag::err_drv_unsupported_opt_for_target)
        << A->getSpelling() << D.getTargetTriple();
  }

  // Force static linking when "-static" is present.
  if (Args.hasArg(options::OPT_static))
    CmdArgs.push_back("-bnso");

  // Add options for shared libraries.
  if (Args.hasArg(options::OPT_shared)) {
    CmdArgs.push_back("-bM:SRE");
    CmdArgs.push_back("-bnoentry");
  }

  if (Args.hasFlag(options::OPT_mxcoff_roptr, options::OPT_mno_xcoff_roptr,
                   false)) {
    if (Args.hasArg(options::OPT_shared))
      D.Diag(diag::err_roptr_cannot_build_shared);

    // The `-mxcoff-roptr` option places constants in RO sections as much as
    // possible. Then `-bforceimprw` changes such sections to RW if they contain
    // imported symbols that need to be resolved.
    CmdArgs.push_back("-bforceimprw");
  }

  // PGO instrumentation generates symbols belonging to special sections, and
  // the linker needs to place all symbols in a particular section together in
  // memory; the AIX linker does that under an option.
  if (Args.hasFlag(options::OPT_fprofile_arcs, options::OPT_fno_profile_arcs,
                    false) ||
       Args.hasFlag(options::OPT_fprofile_generate,
                    options::OPT_fno_profile_generate, false) ||
       Args.hasFlag(options::OPT_fprofile_generate_EQ,
                    options::OPT_fno_profile_generate, false) ||
       Args.hasFlag(options::OPT_fprofile_instr_generate,
                    options::OPT_fno_profile_instr_generate, false) ||
       Args.hasFlag(options::OPT_fprofile_instr_generate_EQ,
                    options::OPT_fno_profile_instr_generate, false) ||
       Args.hasFlag(options::OPT_fcs_profile_generate,
                    options::OPT_fno_profile_generate, false) ||
       Args.hasFlag(options::OPT_fcs_profile_generate_EQ,
                    options::OPT_fno_profile_generate, false) ||
       Args.hasArg(options::OPT_fcreate_profile) ||
       Args.hasArg(options::OPT_coverage))
    CmdArgs.push_back("-bdbg:namedsects:ss");

  if (Arg *A =
          Args.getLastArg(clang::driver::options::OPT_mxcoff_build_id_EQ)) {
    StringRef BuildId = A->getValue();
    if (BuildId[0] != '0' || BuildId[1] != 'x' ||
        BuildId.find_if_not(llvm::isHexDigit, 2) != StringRef::npos)
      ToolChain.getDriver().Diag(diag::err_drv_unsupported_option_argument)
          << A->getSpelling() << BuildId;
    else {
      std::string LinkerFlag = "-bdbg:ldrinfo:xcoff_binary_id:0x";
      if (BuildId.size() % 2) // Prepend a 0 if odd number of digits.
        LinkerFlag += "0";
      LinkerFlag += BuildId.drop_front(2).lower();
      CmdArgs.push_back(Args.MakeArgString(LinkerFlag));
    }
  }

  // Specify linker output file.
  assert((Output.isFilename() || Output.isNothing()) && "Invalid output.");
  if (Output.isFilename()) {
    CmdArgs.push_back("-o");
    CmdArgs.push_back(Output.getFilename());
  }

  // Set linking mode (i.e., 32/64-bit) and the address of
  // text and data sections based on arch bit width.
  if (IsArch32Bit) {
    CmdArgs.push_back("-b32");
    CmdArgs.push_back("-bpT:0x10000000");
    CmdArgs.push_back("-bpD:0x20000000");
  } else {
    // Must be 64-bit, otherwise asserted already.
    CmdArgs.push_back("-b64");
    CmdArgs.push_back("-bpT:0x100000000");
    CmdArgs.push_back("-bpD:0x110000000");
  }

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles,
                   options::OPT_shared, options::OPT_r)) {
    auto getCrt0Basename = [&Args, IsArch32Bit] {
      if (Arg *A = Args.getLastArgNoClaim(options::OPT_p, options::OPT_pg)) {
        // Enable gprofiling when "-pg" is specified.
        if (A->getOption().matches(options::OPT_pg))
          return IsArch32Bit ? "gcrt0.o" : "gcrt0_64.o";
        // Enable profiling when "-p" is specified.
        return IsArch32Bit ? "mcrt0.o" : "mcrt0_64.o";
      }
      return IsArch32Bit ? "crt0.o" : "crt0_64.o";
    };

    CmdArgs.push_back(
        Args.MakeArgString(ToolChain.GetFilePath(getCrt0Basename())));

    CmdArgs.push_back(Args.MakeArgString(
        ToolChain.GetFilePath(IsArch32Bit ? "crti.o" : "crti_64.o")));
  }

  // Collect all static constructor and destructor functions in both C and CXX
  // language link invocations. This has to come before AddLinkerInputs as the
  // implied option needs to precede any other '-bcdtors' settings or
  // '-bnocdtors' that '-Wl' might forward.
  CmdArgs.push_back("-bcdtors:all:0:s");

  // Specify linker input file(s).
  AddLinkerInputs(ToolChain, Inputs, Args, CmdArgs, JA);

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

  if (Args.hasArg(options::OPT_shared) && !hasExportListLinkerOpts(CmdArgs)) {

    const char *CreateExportListExec = Args.MakeArgString(
        path::parent_path(ToolChain.getDriver().ClangExecutable) +
        "/llvm-nm");
    ArgStringList CreateExportCmdArgs;

    std::string CreateExportListPath =
        C.getDriver().GetTemporaryPath("CreateExportList", "exp");
    const char *ExportList =
        C.addTempFile(C.getArgs().MakeArgString(CreateExportListPath));

    for (const auto &II : Inputs)
      if (II.isFilename())
        CreateExportCmdArgs.push_back(II.getFilename());

    CreateExportCmdArgs.push_back("--export-symbols");
    CreateExportCmdArgs.push_back("-X");
    if (IsArch32Bit) {
      CreateExportCmdArgs.push_back("32");
    } else {
      // Must be 64-bit, otherwise asserted already.
      CreateExportCmdArgs.push_back("64");
    }

    auto ExpCommand = std::make_unique<Command>(
        JA, *this, ResponseFileSupport::None(), CreateExportListExec,
        CreateExportCmdArgs, Inputs, Output);
    ExpCommand->setRedirectFiles(
        {std::nullopt, std::string(ExportList), std::nullopt});
    C.addCommand(std::move(ExpCommand));
    CmdArgs.push_back(Args.MakeArgString(llvm::Twine("-bE:") + ExportList));
  }

  // Add directory to library search path.
  Args.AddAllArgs(CmdArgs, options::OPT_L);
  if (!Args.hasArg(options::OPT_r)) {
    ToolChain.AddFilePathLibArgs(Args, CmdArgs);
    ToolChain.addProfileRTLibs(Args, CmdArgs);

    if (getToolChain().ShouldLinkCXXStdlib(Args))
      getToolChain().AddCXXStdlibLibArgs(Args, CmdArgs);

    if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs)) {
      AddRunTimeLibs(ToolChain, D, CmdArgs, Args);

      // Add OpenMP runtime if -fopenmp is specified.
      if (Args.hasFlag(options::OPT_fopenmp, options::OPT_fopenmp_EQ,
                       options::OPT_fno_openmp, false)) {
        switch (ToolChain.getDriver().getOpenMPRuntime(Args)) {
        case Driver::OMPRT_OMP:
          CmdArgs.push_back("-lomp");
          break;
        case Driver::OMPRT_IOMP5:
          CmdArgs.push_back("-liomp5");
          break;
        case Driver::OMPRT_GOMP:
          CmdArgs.push_back("-lgomp");
          break;
        case Driver::OMPRT_Unknown:
          // Already diagnosed.
          break;
        }
      }

      // Support POSIX threads if "-pthreads" or "-pthread" is present.
      if (Args.hasArg(options::OPT_pthreads, options::OPT_pthread))
        CmdArgs.push_back("-lpthreads");

      if (D.CCCIsCXX())
        CmdArgs.push_back("-lm");

      CmdArgs.push_back("-lc");

      if (Args.hasArgNoClaim(options::OPT_p, options::OPT_pg)) {
        CmdArgs.push_back(Args.MakeArgString((llvm::Twine("-L") + D.SysRoot) +
                                             "/lib/profiled"));
        CmdArgs.push_back(Args.MakeArgString((llvm::Twine("-L") + D.SysRoot) +
                                             "/usr/lib/profiled"));
      }
    }
  }

  if (D.IsFlangMode()) {
    addFortranRuntimeLibraryPath(ToolChain, Args, CmdArgs);
    addFortranRuntimeLibs(ToolChain, Args, CmdArgs);
    CmdArgs.push_back("-lm");
    CmdArgs.push_back("-lpthread");
  }
  const char *Exec = Args.MakeArgString(ToolChain.GetLinkerPath());
  C.addCommand(std::make_unique<Command>(JA, *this, ResponseFileSupport::None(),
                                         Exec, CmdArgs, Inputs, Output));
}

/// AIX - AIX tool chain which can call as(1) and ld(1) directly.
AIX::AIX(const Driver &D, const llvm::Triple &Triple, const ArgList &Args)
    : ToolChain(D, Triple, Args) {
  getProgramPaths().push_back(getDriver().Dir);

  ParseInlineAsmUsingAsmParser = Args.hasFlag(
      options::OPT_fintegrated_as, options::OPT_fno_integrated_as, true);
  getLibraryPaths().push_back(getDriver().SysRoot + "/usr/lib");
}

// Returns the effective header sysroot path to use.
// This comes from either -isysroot or --sysroot.
llvm::StringRef
AIX::GetHeaderSysroot(const llvm::opt::ArgList &DriverArgs) const {
  if (DriverArgs.hasArg(options::OPT_isysroot))
    return DriverArgs.getLastArgValue(options::OPT_isysroot);
  if (!getDriver().SysRoot.empty())
    return getDriver().SysRoot;
  return "/";
}

void AIX::AddOpenMPIncludeArgs(const ArgList &DriverArgs,
                               ArgStringList &CC1Args) const {
  // Add OpenMP include paths if -fopenmp is specified.
  if (DriverArgs.hasFlag(options::OPT_fopenmp, options::OPT_fopenmp_EQ,
                         options::OPT_fno_openmp, false)) {
    SmallString<128> PathOpenMP;
    switch (getDriver().getOpenMPRuntime(DriverArgs)) {
    case Driver::OMPRT_OMP:
      PathOpenMP = GetHeaderSysroot(DriverArgs);
      llvm::sys::path::append(PathOpenMP, "opt/IBM/openxlCSDK", "include",
                              "openmp");
      addSystemInclude(DriverArgs, CC1Args, PathOpenMP.str());
      break;
    case Driver::OMPRT_IOMP5:
    case Driver::OMPRT_GOMP:
    case Driver::OMPRT_Unknown:
      // Unknown / unsupported include paths.
      break;
    }
  }
}

void AIX::AddClangSystemIncludeArgs(const ArgList &DriverArgs,
                                    ArgStringList &CC1Args) const {
  // Return if -nostdinc is specified as a driver option.
  if (DriverArgs.hasArg(options::OPT_nostdinc))
    return;

  llvm::StringRef Sysroot = GetHeaderSysroot(DriverArgs);
  const Driver &D = getDriver();

  if (!DriverArgs.hasArg(options::OPT_nobuiltininc)) {
    SmallString<128> P(D.ResourceDir);
    // Add the PowerPC intrinsic headers (<resource>/include/ppc_wrappers)
    path::append(P, "include", "ppc_wrappers");
    addSystemInclude(DriverArgs, CC1Args, P);
    // Add the Clang builtin headers (<resource>/include)
    addSystemInclude(DriverArgs, CC1Args, path::parent_path(P.str()));
  }

  // Add the include directory containing omp.h. This needs to be before
  // adding the system include directory because other compilers put their
  // omp.h in /usr/include.
  AddOpenMPIncludeArgs(DriverArgs, CC1Args);

  // Return if -nostdlibinc is specified as a driver option.
  if (DriverArgs.hasArg(options::OPT_nostdlibinc))
    return;

  // Add <sysroot>/usr/include.
  SmallString<128> UP(Sysroot);
  path::append(UP, "/usr/include");
  addSystemInclude(DriverArgs, CC1Args, UP.str());
}

void AIX::AddClangCXXStdlibIncludeArgs(
    const llvm::opt::ArgList &DriverArgs,
    llvm::opt::ArgStringList &CC1Args) const {

  if (DriverArgs.hasArg(options::OPT_nostdinc) ||
      DriverArgs.hasArg(options::OPT_nostdincxx) ||
      DriverArgs.hasArg(options::OPT_nostdlibinc))
    return;

  switch (GetCXXStdlibType(DriverArgs)) {
  case ToolChain::CST_Libstdcxx:
    llvm::report_fatal_error(
        "picking up libstdc++ headers is unimplemented on AIX");
  case ToolChain::CST_Libcxx: {
    llvm::StringRef Sysroot = GetHeaderSysroot(DriverArgs);
    SmallString<128> PathCPP(Sysroot);
    llvm::sys::path::append(PathCPP, "opt/IBM/openxlCSDK", "include", "c++",
                            "v1");
    addSystemInclude(DriverArgs, CC1Args, PathCPP.str());
    // Required in order to suppress conflicting C++ overloads in the system
    // libc headers that were used by XL C++.
    CC1Args.push_back("-D__LIBC_NO_CPP_MATH_OVERLOADS__");
    return;
  }
  }

  llvm_unreachable("Unexpected C++ library type; only libc++ is supported.");
}

void AIX::AddCXXStdlibLibArgs(const llvm::opt::ArgList &Args,
                              llvm::opt::ArgStringList &CmdArgs) const {
  switch (GetCXXStdlibType(Args)) {
  case ToolChain::CST_Libstdcxx:
    llvm::report_fatal_error("linking libstdc++ unimplemented on AIX");
  case ToolChain::CST_Libcxx:
    CmdArgs.push_back("-lc++");
    if (Args.hasArg(options::OPT_fexperimental_library))
      CmdArgs.push_back("-lc++experimental");
    CmdArgs.push_back("-lc++abi");
    return;
  }

  llvm_unreachable("Unexpected C++ library type; only libc++ is supported.");
}

// This function processes all the mtocdata options to build the final
// simplified toc data options to pass to CC1.
static void addTocDataOptions(const llvm::opt::ArgList &Args,
                              llvm::opt::ArgStringList &CC1Args,
                              const Driver &D) {

  // Check the global toc-data setting. The default is -mno-tocdata.
  // To enable toc-data globally, -mtocdata must be specified.
  // Additionally, it must be last to take effect.
  const bool TOCDataGloballyinEffect = [&Args]() {
    if (const Arg *LastArg =
            Args.getLastArg(options::OPT_mtocdata, options::OPT_mno_tocdata))
      return LastArg->getOption().matches(options::OPT_mtocdata);
    else
      return false;
  }();

  enum TOCDataSetting {
    AddressInTOC = 0, // Address of the symbol stored in the TOC.
    DataInTOC = 1     // Symbol defined in the TOC.
  };

  const TOCDataSetting DefaultTocDataSetting =
      TOCDataGloballyinEffect ? DataInTOC : AddressInTOC;

  // Process the list of variables in the explicitly specified options
  // -mtocdata= and -mno-tocdata= to see which variables are opposite to
  // the global setting of tocdata in TOCDataGloballyinEffect.
  // Those that have the opposite setting to TOCDataGloballyinEffect, are added
  // to ExplicitlySpecifiedGlobals.
  std::set<llvm::StringRef> ExplicitlySpecifiedGlobals;
  for (const auto Arg :
       Args.filtered(options::OPT_mtocdata_EQ, options::OPT_mno_tocdata_EQ)) {
    TOCDataSetting ArgTocDataSetting =
        Arg->getOption().matches(options::OPT_mtocdata_EQ) ? DataInTOC
                                                           : AddressInTOC;

    if (ArgTocDataSetting != DefaultTocDataSetting)
      for (const char *Val : Arg->getValues())
        ExplicitlySpecifiedGlobals.insert(Val);
    else
      for (const char *Val : Arg->getValues())
        ExplicitlySpecifiedGlobals.erase(Val);
  }

  auto buildExceptionList = [](const std::set<llvm::StringRef> &ExplicitValues,
                               const char *OptionSpelling) {
    std::string Option(OptionSpelling);
    bool IsFirst = true;
    for (const auto &E : ExplicitValues) {
      if (!IsFirst)
        Option += ",";

      IsFirst = false;
      Option += E.str();
    }
    return Option;
  };

  // Pass the final tocdata options to CC1 consisting of the default
  // tocdata option (-mtocdata/-mno-tocdata) along with the list
  // option (-mno-tocdata=/-mtocdata=) if there are any explicitly specified
  // variables which would be exceptions to the default setting.
  const char *TocDataGlobalOption =
      TOCDataGloballyinEffect ? "-mtocdata" : "-mno-tocdata";
  CC1Args.push_back(TocDataGlobalOption);

  const char *TocDataListOption =
      TOCDataGloballyinEffect ? "-mno-tocdata=" : "-mtocdata=";
  if (!ExplicitlySpecifiedGlobals.empty())
    CC1Args.push_back(Args.MakeArgString(llvm::Twine(
        buildExceptionList(ExplicitlySpecifiedGlobals, TocDataListOption))));
}

void AIX::addClangTargetOptions(
    const llvm::opt::ArgList &Args, llvm::opt::ArgStringList &CC1Args,
    Action::OffloadKind DeviceOffloadingKind) const {
  Args.AddLastArg(CC1Args, options::OPT_mignore_xcoff_visibility);
  Args.AddLastArg(CC1Args, options::OPT_mdefault_visibility_export_mapping_EQ);
  Args.addOptInFlag(CC1Args, options::OPT_mxcoff_roptr, options::OPT_mno_xcoff_roptr);

  // Forward last mtocdata/mno_tocdata options to -cc1.
  if (Args.hasArg(options::OPT_mtocdata_EQ, options::OPT_mno_tocdata_EQ,
                  options::OPT_mtocdata))
    addTocDataOptions(Args, CC1Args, getDriver());

  if (Args.hasFlag(options::OPT_fxl_pragma_pack,
                   options::OPT_fno_xl_pragma_pack, true))
    CC1Args.push_back("-fxl-pragma-pack");

  // Pass "-fno-sized-deallocation" only when the user hasn't manually enabled
  // or disabled sized deallocations.
  if (!Args.getLastArgNoClaim(options::OPT_fsized_deallocation,
                              options::OPT_fno_sized_deallocation))
    CC1Args.push_back("-fno-sized-deallocation");
}

void AIX::addProfileRTLibs(const llvm::opt::ArgList &Args,
                           llvm::opt::ArgStringList &CmdArgs) const {
  if (needsProfileRT(Args)) {
    // Add linker option -u__llvm_profile_runtime to cause runtime
    // initialization to occur.
    CmdArgs.push_back(Args.MakeArgString(
        Twine("-u", llvm::getInstrProfRuntimeHookVarName())));

    if (const auto *A =
            Args.getLastArgNoClaim(options::OPT_fprofile_update_EQ)) {
      StringRef Val = A->getValue();
      if (Val == "atomic" || Val == "prefer-atomic")
        CmdArgs.push_back("-latomic");
    }
  }

  ToolChain::addProfileRTLibs(Args, CmdArgs);
}

ToolChain::CXXStdlibType AIX::GetDefaultCXXStdlibType() const {
  return ToolChain::CST_Libcxx;
}

ToolChain::RuntimeLibType AIX::GetDefaultRuntimeLibType() const {
  return ToolChain::RLT_CompilerRT;
}

auto AIX::buildAssembler() const -> Tool * { return new aix::Assembler(*this); }

auto AIX::buildLinker() const -> Tool * { return new aix::Linker(*this); }
