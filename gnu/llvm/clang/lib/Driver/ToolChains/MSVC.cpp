//===-- MSVC.cpp - MSVC ToolChain Implementations -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MSVC.h"
#include "CommonArgs.h"
#include "Darwin.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/Version.h"
#include "clang/Config/config.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "clang/Driver/SanitizerArgs.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/TargetParser/Host.h"
#include <cstdio>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOGDI
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
#endif

using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

static bool canExecute(llvm::vfs::FileSystem &VFS, StringRef Path) {
  auto Status = VFS.status(Path);
  if (!Status)
    return false;
  return (Status->getPermissions() & llvm::sys::fs::perms::all_exe) != 0;
}

// Try to find Exe from a Visual Studio distribution.  This first tries to find
// an installed copy of Visual Studio and, failing that, looks in the PATH,
// making sure that whatever executable that's found is not a same-named exe
// from clang itself to prevent clang from falling back to itself.
static std::string FindVisualStudioExecutable(const ToolChain &TC,
                                              const char *Exe) {
  const auto &MSVC = static_cast<const toolchains::MSVCToolChain &>(TC);
  SmallString<128> FilePath(
      MSVC.getSubDirectoryPath(llvm::SubDirectoryType::Bin));
  llvm::sys::path::append(FilePath, Exe);
  return std::string(canExecute(TC.getVFS(), FilePath) ? FilePath.str() : Exe);
}

void visualstudio::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                        const InputInfo &Output,
                                        const InputInfoList &Inputs,
                                        const ArgList &Args,
                                        const char *LinkingOutput) const {
  ArgStringList CmdArgs;

  auto &TC = static_cast<const toolchains::MSVCToolChain &>(getToolChain());

  assert((Output.isFilename() || Output.isNothing()) && "invalid output");
  if (Output.isFilename())
    CmdArgs.push_back(
        Args.MakeArgString(std::string("-out:") + Output.getFilename()));

  if (Args.hasArg(options::OPT_marm64x))
    CmdArgs.push_back("-machine:arm64x");
  else if (TC.getTriple().isWindowsArm64EC())
    CmdArgs.push_back("-machine:arm64ec");

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles) &&
      !C.getDriver().IsCLMode() && !C.getDriver().IsFlangMode()) {
    CmdArgs.push_back("-defaultlib:libcmt");
    CmdArgs.push_back("-defaultlib:oldnames");
  }

  // If the VC environment hasn't been configured (perhaps because the user
  // did not run vcvarsall), try to build a consistent link environment.  If
  // the environment variable is set however, assume the user knows what
  // they're doing. If the user passes /vctoolsdir or /winsdkdir, trust that
  // over env vars.
  if (const Arg *A = Args.getLastArg(options::OPT__SLASH_diasdkdir,
                                     options::OPT__SLASH_winsysroot)) {
    // cl.exe doesn't find the DIA SDK automatically, so this too requires
    // explicit flags and doesn't automatically look in "DIA SDK" relative
    // to the path we found for VCToolChainPath.
    llvm::SmallString<128> DIAPath(A->getValue());
    if (A->getOption().getID() == options::OPT__SLASH_winsysroot)
      llvm::sys::path::append(DIAPath, "DIA SDK");

    // The DIA SDK always uses the legacy vc arch, even in new MSVC versions.
    llvm::sys::path::append(DIAPath, "lib",
                            llvm::archToLegacyVCArch(TC.getArch()));
    CmdArgs.push_back(Args.MakeArgString(Twine("-libpath:") + DIAPath));
  }
  if (!llvm::sys::Process::GetEnv("LIB") ||
      Args.getLastArg(options::OPT__SLASH_vctoolsdir,
                      options::OPT__SLASH_winsysroot)) {
    CmdArgs.push_back(Args.MakeArgString(
        Twine("-libpath:") +
        TC.getSubDirectoryPath(llvm::SubDirectoryType::Lib)));
    CmdArgs.push_back(Args.MakeArgString(
        Twine("-libpath:") +
        TC.getSubDirectoryPath(llvm::SubDirectoryType::Lib, "atlmfc")));
  }
  if (!llvm::sys::Process::GetEnv("LIB") ||
      Args.getLastArg(options::OPT__SLASH_winsdkdir,
                      options::OPT__SLASH_winsysroot)) {
    if (TC.useUniversalCRT()) {
      std::string UniversalCRTLibPath;
      if (TC.getUniversalCRTLibraryPath(Args, UniversalCRTLibPath))
        CmdArgs.push_back(
            Args.MakeArgString(Twine("-libpath:") + UniversalCRTLibPath));
    }
    std::string WindowsSdkLibPath;
    if (TC.getWindowsSDKLibraryPath(Args, WindowsSdkLibPath))
      CmdArgs.push_back(
          Args.MakeArgString(std::string("-libpath:") + WindowsSdkLibPath));
  }

  if (!C.getDriver().IsCLMode() && Args.hasArg(options::OPT_L))
    for (const auto &LibPath : Args.getAllArgValues(options::OPT_L))
      CmdArgs.push_back(Args.MakeArgString("-libpath:" + LibPath));

  if (C.getDriver().IsFlangMode()) {
    addFortranRuntimeLibraryPath(TC, Args, CmdArgs);
    addFortranRuntimeLibs(TC, Args, CmdArgs);

    // Inform the MSVC linker that we're generating a console application, i.e.
    // one with `main` as the "user-defined" entry point. The `main` function is
    // defined in flang's runtime libraries.
    CmdArgs.push_back("/subsystem:console");
  }

  // Add the compiler-rt library directories to libpath if they exist to help
  // the linker find the various sanitizer, builtin, and profiling runtimes.
  for (const auto &LibPath : TC.getLibraryPaths()) {
    if (TC.getVFS().exists(LibPath))
      CmdArgs.push_back(Args.MakeArgString("-libpath:" + LibPath));
  }
  auto CRTPath = TC.getCompilerRTPath();
  if (TC.getVFS().exists(CRTPath))
    CmdArgs.push_back(Args.MakeArgString("-libpath:" + CRTPath));

  CmdArgs.push_back("-nologo");

  if (Args.hasArg(options::OPT_g_Group, options::OPT__SLASH_Z7))
    CmdArgs.push_back("-debug");

  // If we specify /hotpatch, let the linker add padding in front of each
  // function, like MSVC does.
  if (Args.hasArg(options::OPT_fms_hotpatch, options::OPT__SLASH_hotpatch))
    CmdArgs.push_back("-functionpadmin");

  // Pass on /Brepro if it was passed to the compiler.
  // Note that /Brepro maps to -mno-incremental-linker-compatible.
  bool DefaultIncrementalLinkerCompatible =
      C.getDefaultToolChain().getTriple().isWindowsMSVCEnvironment();
  if (!Args.hasFlag(options::OPT_mincremental_linker_compatible,
                    options::OPT_mno_incremental_linker_compatible,
                    DefaultIncrementalLinkerCompatible))
    CmdArgs.push_back("-Brepro");

  bool DLL = Args.hasArg(options::OPT__SLASH_LD, options::OPT__SLASH_LDd,
                         options::OPT_shared);
  if (DLL) {
    CmdArgs.push_back(Args.MakeArgString("-dll"));

    SmallString<128> ImplibName(Output.getFilename());
    llvm::sys::path::replace_extension(ImplibName, "lib");
    CmdArgs.push_back(Args.MakeArgString(std::string("-implib:") + ImplibName));
  }

  if (TC.getSanitizerArgs(Args).needsFuzzer()) {
    if (!Args.hasArg(options::OPT_shared))
      CmdArgs.push_back(
          Args.MakeArgString(std::string("-wholearchive:") +
                             TC.getCompilerRTArgString(Args, "fuzzer")));
    CmdArgs.push_back(Args.MakeArgString("-debug"));
    // Prevent the linker from padding sections we use for instrumentation
    // arrays.
    CmdArgs.push_back(Args.MakeArgString("-incremental:no"));
  }

  if (TC.getSanitizerArgs(Args).needsAsanRt()) {
    CmdArgs.push_back(Args.MakeArgString("-debug"));
    CmdArgs.push_back(Args.MakeArgString("-incremental:no"));
    if (TC.getSanitizerArgs(Args).needsSharedRt() ||
        Args.hasArg(options::OPT__SLASH_MD, options::OPT__SLASH_MDd)) {
      for (const auto &Lib : {"asan_dynamic", "asan_dynamic_runtime_thunk"})
        CmdArgs.push_back(TC.getCompilerRTArgString(Args, Lib));
      // Make sure the dynamic runtime thunk is not optimized out at link time
      // to ensure proper SEH handling.
      CmdArgs.push_back(Args.MakeArgString(
          TC.getArch() == llvm::Triple::x86
              ? "-include:___asan_seh_interceptor"
              : "-include:__asan_seh_interceptor"));
      // Make sure the linker consider all object files from the dynamic runtime
      // thunk.
      CmdArgs.push_back(Args.MakeArgString(std::string("-wholearchive:") +
          TC.getCompilerRT(Args, "asan_dynamic_runtime_thunk")));
    } else if (DLL) {
      CmdArgs.push_back(TC.getCompilerRTArgString(Args, "asan_dll_thunk"));
    } else {
      for (const auto &Lib : {"asan", "asan_cxx"}) {
        CmdArgs.push_back(TC.getCompilerRTArgString(Args, Lib));
        // Make sure the linker consider all object files from the static lib.
        // This is necessary because instrumented dlls need access to all the
        // interface exported by the static lib in the main executable.
        CmdArgs.push_back(Args.MakeArgString(std::string("-wholearchive:") +
            TC.getCompilerRT(Args, Lib)));
      }
    }
  }

  Args.AddAllArgValues(CmdArgs, options::OPT__SLASH_link);

  // Control Flow Guard checks
  for (const Arg *A : Args.filtered(options::OPT__SLASH_guard)) {
    StringRef GuardArgs = A->getValue();
    if (GuardArgs.equals_insensitive("cf") ||
        GuardArgs.equals_insensitive("cf,nochecks")) {
      // MSVC doesn't yet support the "nochecks" modifier.
      CmdArgs.push_back("-guard:cf");
    } else if (GuardArgs.equals_insensitive("cf-")) {
      CmdArgs.push_back("-guard:cf-");
    } else if (GuardArgs.equals_insensitive("ehcont")) {
      CmdArgs.push_back("-guard:ehcont");
    } else if (GuardArgs.equals_insensitive("ehcont-")) {
      CmdArgs.push_back("-guard:ehcont-");
    }
  }

  if (Args.hasFlag(options::OPT_fopenmp, options::OPT_fopenmp_EQ,
                   options::OPT_fno_openmp, false)) {
    CmdArgs.push_back("-nodefaultlib:vcomp.lib");
    CmdArgs.push_back("-nodefaultlib:vcompd.lib");
    CmdArgs.push_back(Args.MakeArgString(std::string("-libpath:") +
                                         TC.getDriver().Dir + "/../lib"));
    switch (TC.getDriver().getOpenMPRuntime(Args)) {
    case Driver::OMPRT_OMP:
      CmdArgs.push_back("-defaultlib:libomp.lib");
      break;
    case Driver::OMPRT_IOMP5:
      CmdArgs.push_back("-defaultlib:libiomp5md.lib");
      break;
    case Driver::OMPRT_GOMP:
      break;
    case Driver::OMPRT_Unknown:
      // Already diagnosed.
      break;
    }
  }

  // Add compiler-rt lib in case if it was explicitly
  // specified as an argument for --rtlib option.
  if (!Args.hasArg(options::OPT_nostdlib)) {
    AddRunTimeLibs(TC, TC.getDriver(), CmdArgs, Args);
  }

  StringRef Linker =
      Args.getLastArgValue(options::OPT_fuse_ld_EQ, CLANG_DEFAULT_LINKER);
  if (Linker.empty())
    Linker = "link";
  // We need to translate 'lld' into 'lld-link'.
  else if (Linker.equals_insensitive("lld"))
    Linker = "lld-link";

  if (Linker == "lld-link") {
    for (Arg *A : Args.filtered(options::OPT_vfsoverlay))
      CmdArgs.push_back(
          Args.MakeArgString(std::string("/vfsoverlay:") + A->getValue()));

    if (C.getDriver().isUsingLTO() &&
        Args.hasFlag(options::OPT_gsplit_dwarf, options::OPT_gno_split_dwarf,
                     false))
      CmdArgs.push_back(Args.MakeArgString(Twine("/dwodir:") +
                                           Output.getFilename() + "_dwo"));
  }

  // Add filenames, libraries, and other linker inputs.
  for (const auto &Input : Inputs) {
    if (Input.isFilename()) {
      CmdArgs.push_back(Input.getFilename());
      continue;
    }

    const Arg &A = Input.getInputArg();

    // Render -l options differently for the MSVC linker.
    if (A.getOption().matches(options::OPT_l)) {
      StringRef Lib = A.getValue();
      const char *LinkLibArg;
      if (Lib.ends_with(".lib"))
        LinkLibArg = Args.MakeArgString(Lib);
      else
        LinkLibArg = Args.MakeArgString(Lib + ".lib");
      CmdArgs.push_back(LinkLibArg);
      continue;
    }

    // Otherwise, this is some other kind of linker input option like -Wl, -z,
    // or -L. Render it, even if MSVC doesn't understand it.
    A.renderAsInput(Args, CmdArgs);
  }

  addHIPRuntimeLibArgs(TC, C, Args, CmdArgs);

  TC.addProfileRTLibs(Args, CmdArgs);

  std::vector<const char *> Environment;

  // We need to special case some linker paths. In the case of the regular msvc
  // linker, we need to use a special search algorithm.
  llvm::SmallString<128> linkPath;
  if (Linker.equals_insensitive("link")) {
    // If we're using the MSVC linker, it's not sufficient to just use link
    // from the program PATH, because other environments like GnuWin32 install
    // their own link.exe which may come first.
    linkPath = FindVisualStudioExecutable(TC, "link.exe");

    if (!TC.FoundMSVCInstall() && !canExecute(TC.getVFS(), linkPath)) {
      llvm::SmallString<128> ClPath;
      ClPath = TC.GetProgramPath("cl.exe");
      if (canExecute(TC.getVFS(), ClPath)) {
        linkPath = llvm::sys::path::parent_path(ClPath);
        llvm::sys::path::append(linkPath, "link.exe");
        if (!canExecute(TC.getVFS(), linkPath))
          C.getDriver().Diag(clang::diag::warn_drv_msvc_not_found);
      } else {
        C.getDriver().Diag(clang::diag::warn_drv_msvc_not_found);
      }
    }

    // Clang handles passing the proper asan libs to the linker, which goes
    // against link.exe's /INFERASANLIBS which automatically finds asan libs.
    if (TC.getSanitizerArgs(Args).needsAsanRt())
      CmdArgs.push_back("/INFERASANLIBS:NO");

#ifdef _WIN32
    // When cross-compiling with VS2017 or newer, link.exe expects to have
    // its containing bin directory at the top of PATH, followed by the
    // native target bin directory.
    // e.g. when compiling for x86 on an x64 host, PATH should start with:
    // /bin/Hostx64/x86;/bin/Hostx64/x64
    // This doesn't attempt to handle llvm::ToolsetLayout::DevDivInternal.
    if (TC.getIsVS2017OrNewer() &&
        llvm::Triple(llvm::sys::getProcessTriple()).getArch() != TC.getArch()) {
      auto HostArch = llvm::Triple(llvm::sys::getProcessTriple()).getArch();

      auto EnvBlockWide =
          std::unique_ptr<wchar_t[], decltype(&FreeEnvironmentStringsW)>(
              GetEnvironmentStringsW(), FreeEnvironmentStringsW);
      if (!EnvBlockWide)
        goto SkipSettingEnvironment;

      size_t EnvCount = 0;
      size_t EnvBlockLen = 0;
      while (EnvBlockWide[EnvBlockLen] != L'\0') {
        ++EnvCount;
        EnvBlockLen += std::wcslen(&EnvBlockWide[EnvBlockLen]) +
                       1 /*string null-terminator*/;
      }
      ++EnvBlockLen; // add the block null-terminator

      std::string EnvBlock;
      if (!llvm::convertUTF16ToUTF8String(
              llvm::ArrayRef<char>(reinterpret_cast<char *>(EnvBlockWide.get()),
                                   EnvBlockLen * sizeof(EnvBlockWide[0])),
              EnvBlock))
        goto SkipSettingEnvironment;

      Environment.reserve(EnvCount);

      // Now loop over each string in the block and copy them into the
      // environment vector, adjusting the PATH variable as needed when we
      // find it.
      for (const char *Cursor = EnvBlock.data(); *Cursor != '\0';) {
        llvm::StringRef EnvVar(Cursor);
        if (EnvVar.starts_with_insensitive("path=")) {
          constexpr size_t PrefixLen = 5; // strlen("path=")
          Environment.push_back(Args.MakeArgString(
              EnvVar.substr(0, PrefixLen) +
              TC.getSubDirectoryPath(llvm::SubDirectoryType::Bin) +
              llvm::Twine(llvm::sys::EnvPathSeparator) +
              TC.getSubDirectoryPath(llvm::SubDirectoryType::Bin, HostArch) +
              (EnvVar.size() > PrefixLen
                   ? llvm::Twine(llvm::sys::EnvPathSeparator) +
                         EnvVar.substr(PrefixLen)
                   : "")));
        } else {
          Environment.push_back(Args.MakeArgString(EnvVar));
        }
        Cursor += EnvVar.size() + 1 /*null-terminator*/;
      }
    }
  SkipSettingEnvironment:;
#endif
  } else {
    linkPath = TC.GetProgramPath(Linker.str().c_str());
  }

  auto LinkCmd = std::make_unique<Command>(
      JA, *this, ResponseFileSupport::AtFileUTF16(),
      Args.MakeArgString(linkPath), CmdArgs, Inputs, Output);
  if (!Environment.empty())
    LinkCmd->setEnvironment(Environment);
  C.addCommand(std::move(LinkCmd));
}

MSVCToolChain::MSVCToolChain(const Driver &D, const llvm::Triple &Triple,
                             const ArgList &Args)
    : ToolChain(D, Triple, Args), CudaInstallation(D, Triple, Args),
      RocmInstallation(D, Triple, Args) {
  getProgramPaths().push_back(getDriver().Dir);

  std::optional<llvm::StringRef> VCToolsDir, VCToolsVersion;
  if (Arg *A = Args.getLastArg(options::OPT__SLASH_vctoolsdir))
    VCToolsDir = A->getValue();
  if (Arg *A = Args.getLastArg(options::OPT__SLASH_vctoolsversion))
    VCToolsVersion = A->getValue();
  if (Arg *A = Args.getLastArg(options::OPT__SLASH_winsdkdir))
    WinSdkDir = A->getValue();
  if (Arg *A = Args.getLastArg(options::OPT__SLASH_winsdkversion))
    WinSdkVersion = A->getValue();
  if (Arg *A = Args.getLastArg(options::OPT__SLASH_winsysroot))
    WinSysRoot = A->getValue();

  // Check the command line first, that's the user explicitly telling us what to
  // use. Check the environment next, in case we're being invoked from a VS
  // command prompt. Failing that, just try to find the newest Visual Studio
  // version we can and use its default VC toolchain.
  llvm::findVCToolChainViaCommandLine(getVFS(), VCToolsDir, VCToolsVersion,
                                      WinSysRoot, VCToolChainPath, VSLayout) ||
      llvm::findVCToolChainViaEnvironment(getVFS(), VCToolChainPath,
                                          VSLayout) ||
      llvm::findVCToolChainViaSetupConfig(getVFS(), VCToolsVersion,
                                          VCToolChainPath, VSLayout) ||
      llvm::findVCToolChainViaRegistry(VCToolChainPath, VSLayout);
}

Tool *MSVCToolChain::buildLinker() const {
  return new tools::visualstudio::Linker(*this);
}

Tool *MSVCToolChain::buildAssembler() const {
  if (getTriple().isOSBinFormatMachO())
    return new tools::darwin::Assembler(*this);
  getDriver().Diag(clang::diag::err_no_external_assembler);
  return nullptr;
}

ToolChain::UnwindTableLevel
MSVCToolChain::getDefaultUnwindTableLevel(const ArgList &Args) const {
  // Don't emit unwind tables by default for MachO targets.
  if (getTriple().isOSBinFormatMachO())
    return UnwindTableLevel::None;

  // All non-x86_32 Windows targets require unwind tables. However, LLVM
  // doesn't know how to generate them for all targets, so only enable
  // the ones that are actually implemented.
  if (getArch() == llvm::Triple::x86_64 || getArch() == llvm::Triple::arm ||
      getArch() == llvm::Triple::thumb || getArch() == llvm::Triple::aarch64)
    return UnwindTableLevel::Asynchronous;

  return UnwindTableLevel::None;
}

bool MSVCToolChain::isPICDefault() const {
  return getArch() == llvm::Triple::x86_64 ||
         getArch() == llvm::Triple::aarch64;
}

bool MSVCToolChain::isPIEDefault(const llvm::opt::ArgList &Args) const {
  return false;
}

bool MSVCToolChain::isPICDefaultForced() const {
  return getArch() == llvm::Triple::x86_64 ||
         getArch() == llvm::Triple::aarch64;
}

void MSVCToolChain::AddCudaIncludeArgs(const ArgList &DriverArgs,
                                       ArgStringList &CC1Args) const {
  CudaInstallation->AddCudaIncludeArgs(DriverArgs, CC1Args);
}

void MSVCToolChain::AddHIPIncludeArgs(const ArgList &DriverArgs,
                                      ArgStringList &CC1Args) const {
  RocmInstallation->AddHIPIncludeArgs(DriverArgs, CC1Args);
}

void MSVCToolChain::AddHIPRuntimeLibArgs(const ArgList &Args,
                                         ArgStringList &CmdArgs) const {
  CmdArgs.append({Args.MakeArgString(StringRef("-libpath:") +
                                     RocmInstallation->getLibPath()),
                  "amdhip64.lib"});
}

void MSVCToolChain::printVerboseInfo(raw_ostream &OS) const {
  CudaInstallation->print(OS);
  RocmInstallation->print(OS);
}

std::string
MSVCToolChain::getSubDirectoryPath(llvm::SubDirectoryType Type,
                                   llvm::StringRef SubdirParent) const {
  return llvm::getSubDirectoryPath(Type, VSLayout, VCToolChainPath, getArch(),
                                   SubdirParent);
}

std::string
MSVCToolChain::getSubDirectoryPath(llvm::SubDirectoryType Type,
                                   llvm::Triple::ArchType TargetArch) const {
  return llvm::getSubDirectoryPath(Type, VSLayout, VCToolChainPath, TargetArch,
                                   "");
}

// Find the most recent version of Universal CRT or Windows 10 SDK.
// vcvarsqueryregistry.bat from Visual Studio 2015 sorts entries in the include
// directory by name and uses the last one of the list.
// So we compare entry names lexicographically to find the greatest one.
// Gets the library path required to link against the Windows SDK.
bool MSVCToolChain::getWindowsSDKLibraryPath(const ArgList &Args,
                                             std::string &path) const {
  std::string sdkPath;
  int sdkMajor = 0;
  std::string windowsSDKIncludeVersion;
  std::string windowsSDKLibVersion;

  path.clear();
  if (!llvm::getWindowsSDKDir(getVFS(), WinSdkDir, WinSdkVersion, WinSysRoot,
                              sdkPath, sdkMajor, windowsSDKIncludeVersion,
                              windowsSDKLibVersion))
    return false;

  llvm::SmallString<128> libPath(sdkPath);
  llvm::sys::path::append(libPath, "Lib");
  if (sdkMajor >= 10)
    if (!(WinSdkDir.has_value() || WinSysRoot.has_value()) &&
        WinSdkVersion.has_value())
      windowsSDKLibVersion = *WinSdkVersion;
  if (sdkMajor >= 8)
    llvm::sys::path::append(libPath, windowsSDKLibVersion, "um");
  return llvm::appendArchToWindowsSDKLibPath(sdkMajor, libPath, getArch(),
                                             path);
}

bool MSVCToolChain::useUniversalCRT() const {
  return llvm::useUniversalCRT(VSLayout, VCToolChainPath, getArch(), getVFS());
}

bool MSVCToolChain::getUniversalCRTLibraryPath(const ArgList &Args,
                                               std::string &Path) const {
  std::string UniversalCRTSdkPath;
  std::string UCRTVersion;

  Path.clear();
  if (!llvm::getUniversalCRTSdkDir(getVFS(), WinSdkDir, WinSdkVersion,
                                   WinSysRoot, UniversalCRTSdkPath,
                                   UCRTVersion))
    return false;

  if (!(WinSdkDir.has_value() || WinSysRoot.has_value()) &&
      WinSdkVersion.has_value())
    UCRTVersion = *WinSdkVersion;

  StringRef ArchName = llvm::archToWindowsSDKArch(getArch());
  if (ArchName.empty())
    return false;

  llvm::SmallString<128> LibPath(UniversalCRTSdkPath);
  llvm::sys::path::append(LibPath, "Lib", UCRTVersion, "ucrt", ArchName);

  Path = std::string(LibPath);
  return true;
}

static VersionTuple getMSVCVersionFromExe(const std::string &BinDir) {
  VersionTuple Version;
#ifdef _WIN32
  SmallString<128> ClExe(BinDir);
  llvm::sys::path::append(ClExe, "cl.exe");

  std::wstring ClExeWide;
  if (!llvm::ConvertUTF8toWide(ClExe.c_str(), ClExeWide))
    return Version;

  const DWORD VersionSize = ::GetFileVersionInfoSizeW(ClExeWide.c_str(),
                                                      nullptr);
  if (VersionSize == 0)
    return Version;

  SmallVector<uint8_t, 4 * 1024> VersionBlock(VersionSize);
  if (!::GetFileVersionInfoW(ClExeWide.c_str(), 0, VersionSize,
                             VersionBlock.data()))
    return Version;

  VS_FIXEDFILEINFO *FileInfo = nullptr;
  UINT FileInfoSize = 0;
  if (!::VerQueryValueW(VersionBlock.data(), L"\\",
                        reinterpret_cast<LPVOID *>(&FileInfo), &FileInfoSize) ||
      FileInfoSize < sizeof(*FileInfo))
    return Version;

  const unsigned Major = (FileInfo->dwFileVersionMS >> 16) & 0xFFFF;
  const unsigned Minor = (FileInfo->dwFileVersionMS      ) & 0xFFFF;
  const unsigned Micro = (FileInfo->dwFileVersionLS >> 16) & 0xFFFF;

  Version = VersionTuple(Major, Minor, Micro);
#endif
  return Version;
}

void MSVCToolChain::AddSystemIncludeWithSubfolder(
    const ArgList &DriverArgs, ArgStringList &CC1Args,
    const std::string &folder, const Twine &subfolder1, const Twine &subfolder2,
    const Twine &subfolder3) const {
  llvm::SmallString<128> path(folder);
  llvm::sys::path::append(path, subfolder1, subfolder2, subfolder3);
  addSystemInclude(DriverArgs, CC1Args, path);
}

void MSVCToolChain::AddClangSystemIncludeArgs(const ArgList &DriverArgs,
                                              ArgStringList &CC1Args) const {
  if (DriverArgs.hasArg(options::OPT_nostdinc))
    return;

  if (!DriverArgs.hasArg(options::OPT_nobuiltininc)) {
    AddSystemIncludeWithSubfolder(DriverArgs, CC1Args, getDriver().ResourceDir,
                                  "include");
  }

  // Add %INCLUDE%-like directories from the -imsvc flag.
  for (const auto &Path : DriverArgs.getAllArgValues(options::OPT__SLASH_imsvc))
    addSystemInclude(DriverArgs, CC1Args, Path);

  auto AddSystemIncludesFromEnv = [&](StringRef Var) -> bool {
    if (auto Val = llvm::sys::Process::GetEnv(Var)) {
      SmallVector<StringRef, 8> Dirs;
      StringRef(*Val).split(Dirs, ";", /*MaxSplit=*/-1, /*KeepEmpty=*/false);
      if (!Dirs.empty()) {
        addSystemIncludes(DriverArgs, CC1Args, Dirs);
        return true;
      }
    }
    return false;
  };

  // Add %INCLUDE%-like dirs via /external:env: flags.
  for (const auto &Var :
       DriverArgs.getAllArgValues(options::OPT__SLASH_external_env)) {
    AddSystemIncludesFromEnv(Var);
  }

  // Add DIA SDK include if requested.
  if (const Arg *A = DriverArgs.getLastArg(options::OPT__SLASH_diasdkdir,
                                           options::OPT__SLASH_winsysroot)) {
    // cl.exe doesn't find the DIA SDK automatically, so this too requires
    // explicit flags and doesn't automatically look in "DIA SDK" relative
    // to the path we found for VCToolChainPath.
    llvm::SmallString<128> DIASDKPath(A->getValue());
    if (A->getOption().getID() == options::OPT__SLASH_winsysroot)
      llvm::sys::path::append(DIASDKPath, "DIA SDK");
    AddSystemIncludeWithSubfolder(DriverArgs, CC1Args, std::string(DIASDKPath),
                                  "include");
  }

  if (DriverArgs.hasArg(options::OPT_nostdlibinc))
    return;

  // Honor %INCLUDE% and %EXTERNAL_INCLUDE%. It should have essential search
  // paths set by vcvarsall.bat. Skip if the user expressly set a vctoolsdir.
  if (!DriverArgs.getLastArg(options::OPT__SLASH_vctoolsdir,
                             options::OPT__SLASH_winsysroot)) {
    bool Found = AddSystemIncludesFromEnv("INCLUDE");
    Found |= AddSystemIncludesFromEnv("EXTERNAL_INCLUDE");
    if (Found)
      return;
  }

  // When built with access to the proper Windows APIs, try to actually find
  // the correct include paths first.
  if (!VCToolChainPath.empty()) {
    addSystemInclude(DriverArgs, CC1Args,
                     getSubDirectoryPath(llvm::SubDirectoryType::Include));
    addSystemInclude(
        DriverArgs, CC1Args,
        getSubDirectoryPath(llvm::SubDirectoryType::Include, "atlmfc"));

    if (useUniversalCRT()) {
      std::string UniversalCRTSdkPath;
      std::string UCRTVersion;
      if (llvm::getUniversalCRTSdkDir(getVFS(), WinSdkDir, WinSdkVersion,
                                      WinSysRoot, UniversalCRTSdkPath,
                                      UCRTVersion)) {
        if (!(WinSdkDir.has_value() || WinSysRoot.has_value()) &&
            WinSdkVersion.has_value())
          UCRTVersion = *WinSdkVersion;
        AddSystemIncludeWithSubfolder(DriverArgs, CC1Args, UniversalCRTSdkPath,
                                      "Include", UCRTVersion, "ucrt");
      }
    }

    std::string WindowsSDKDir;
    int major = 0;
    std::string windowsSDKIncludeVersion;
    std::string windowsSDKLibVersion;
    if (llvm::getWindowsSDKDir(getVFS(), WinSdkDir, WinSdkVersion, WinSysRoot,
                               WindowsSDKDir, major, windowsSDKIncludeVersion,
                               windowsSDKLibVersion)) {
      if (major >= 10)
        if (!(WinSdkDir.has_value() || WinSysRoot.has_value()) &&
            WinSdkVersion.has_value())
          windowsSDKIncludeVersion = windowsSDKLibVersion = *WinSdkVersion;
      if (major >= 8) {
        // Note: windowsSDKIncludeVersion is empty for SDKs prior to v10.
        // Anyway, llvm::sys::path::append is able to manage it.
        AddSystemIncludeWithSubfolder(DriverArgs, CC1Args, WindowsSDKDir,
                                      "Include", windowsSDKIncludeVersion,
                                      "shared");
        AddSystemIncludeWithSubfolder(DriverArgs, CC1Args, WindowsSDKDir,
                                      "Include", windowsSDKIncludeVersion,
                                      "um");
        AddSystemIncludeWithSubfolder(DriverArgs, CC1Args, WindowsSDKDir,
                                      "Include", windowsSDKIncludeVersion,
                                      "winrt");
        if (major >= 10) {
          llvm::VersionTuple Tuple;
          if (!Tuple.tryParse(windowsSDKIncludeVersion) &&
              Tuple.getSubminor().value_or(0) >= 17134) {
            AddSystemIncludeWithSubfolder(DriverArgs, CC1Args, WindowsSDKDir,
                                          "Include", windowsSDKIncludeVersion,
                                          "cppwinrt");
          }
        }
      } else {
        AddSystemIncludeWithSubfolder(DriverArgs, CC1Args, WindowsSDKDir,
                                      "Include");
      }
    }

    return;
  }

#if defined(_WIN32)
  // As a fallback, select default install paths.
  // FIXME: Don't guess drives and paths like this on Windows.
  const StringRef Paths[] = {
    "C:/Program Files/Microsoft Visual Studio 10.0/VC/include",
    "C:/Program Files/Microsoft Visual Studio 9.0/VC/include",
    "C:/Program Files/Microsoft Visual Studio 9.0/VC/PlatformSDK/Include",
    "C:/Program Files/Microsoft Visual Studio 8/VC/include",
    "C:/Program Files/Microsoft Visual Studio 8/VC/PlatformSDK/Include"
  };
  addSystemIncludes(DriverArgs, CC1Args, Paths);
#endif
}

void MSVCToolChain::AddClangCXXStdlibIncludeArgs(const ArgList &DriverArgs,
                                                 ArgStringList &CC1Args) const {
  // FIXME: There should probably be logic here to find libc++ on Windows.
}

VersionTuple MSVCToolChain::computeMSVCVersion(const Driver *D,
                                               const ArgList &Args) const {
  bool IsWindowsMSVC = getTriple().isWindowsMSVCEnvironment();
  VersionTuple MSVT = ToolChain::computeMSVCVersion(D, Args);
  if (MSVT.empty())
    MSVT = getTriple().getEnvironmentVersion();
  if (MSVT.empty() && IsWindowsMSVC)
    MSVT =
        getMSVCVersionFromExe(getSubDirectoryPath(llvm::SubDirectoryType::Bin));
  if (MSVT.empty() &&
      Args.hasFlag(options::OPT_fms_extensions, options::OPT_fno_ms_extensions,
                   IsWindowsMSVC)) {
    // -fms-compatibility-version=19.33 is default, aka 2022, 17.3
    // NOTE: when changing this value, also update
    // clang/docs/CommandGuide/clang.rst and clang/docs/UsersManual.rst
    // accordingly.
    MSVT = VersionTuple(19, 33);
  }
  return MSVT;
}

std::string
MSVCToolChain::ComputeEffectiveClangTriple(const ArgList &Args,
                                           types::ID InputType) const {
  // The MSVC version doesn't care about the architecture, even though it
  // may look at the triple internally.
  VersionTuple MSVT = computeMSVCVersion(/*D=*/nullptr, Args);
  MSVT = VersionTuple(MSVT.getMajor(), MSVT.getMinor().value_or(0),
                      MSVT.getSubminor().value_or(0));

  // For the rest of the triple, however, a computed architecture name may
  // be needed.
  llvm::Triple Triple(ToolChain::ComputeEffectiveClangTriple(Args, InputType));
  if (Triple.getEnvironment() == llvm::Triple::MSVC) {
    StringRef ObjFmt = Triple.getEnvironmentName().split('-').second;
    if (ObjFmt.empty())
      Triple.setEnvironmentName((Twine("msvc") + MSVT.getAsString()).str());
    else
      Triple.setEnvironmentName(
          (Twine("msvc") + MSVT.getAsString() + Twine('-') + ObjFmt).str());
  }
  return Triple.getTriple();
}

SanitizerMask MSVCToolChain::getSupportedSanitizers() const {
  SanitizerMask Res = ToolChain::getSupportedSanitizers();
  Res |= SanitizerKind::Address;
  Res |= SanitizerKind::PointerCompare;
  Res |= SanitizerKind::PointerSubtract;
  Res |= SanitizerKind::Fuzzer;
  Res |= SanitizerKind::FuzzerNoLink;
  Res &= ~SanitizerKind::CFIMFCall;
  return Res;
}

static void TranslateOptArg(Arg *A, llvm::opt::DerivedArgList &DAL,
                            bool SupportsForcingFramePointer,
                            const char *ExpandChar, const OptTable &Opts) {
  assert(A->getOption().matches(options::OPT__SLASH_O));

  StringRef OptStr = A->getValue();
  for (size_t I = 0, E = OptStr.size(); I != E; ++I) {
    const char &OptChar = *(OptStr.data() + I);
    switch (OptChar) {
    default:
      break;
    case '1':
    case '2':
    case 'x':
    case 'd':
      // Ignore /O[12xd] flags that aren't the last one on the command line.
      // Only the last one gets expanded.
      if (&OptChar != ExpandChar) {
        A->claim();
        break;
      }
      if (OptChar == 'd') {
        DAL.AddFlagArg(A, Opts.getOption(options::OPT_O0));
      } else {
        if (OptChar == '1') {
          DAL.AddJoinedArg(A, Opts.getOption(options::OPT_O), "s");
        } else if (OptChar == '2' || OptChar == 'x') {
          DAL.AddFlagArg(A, Opts.getOption(options::OPT_fbuiltin));
          DAL.AddJoinedArg(A, Opts.getOption(options::OPT_O), "3");
        }
        if (SupportsForcingFramePointer &&
            !DAL.hasArgNoClaim(options::OPT_fno_omit_frame_pointer))
          DAL.AddFlagArg(A, Opts.getOption(options::OPT_fomit_frame_pointer));
        if (OptChar == '1' || OptChar == '2')
          DAL.AddFlagArg(A, Opts.getOption(options::OPT_ffunction_sections));
      }
      break;
    case 'b':
      if (I + 1 != E && isdigit(OptStr[I + 1])) {
        switch (OptStr[I + 1]) {
        case '0':
          DAL.AddFlagArg(A, Opts.getOption(options::OPT_fno_inline));
          break;
        case '1':
          DAL.AddFlagArg(A, Opts.getOption(options::OPT_finline_hint_functions));
          break;
        case '2':
        case '3':
          DAL.AddFlagArg(A, Opts.getOption(options::OPT_finline_functions));
          break;
        }
        ++I;
      }
      break;
    case 'g':
      A->claim();
      break;
    case 'i':
      if (I + 1 != E && OptStr[I + 1] == '-') {
        ++I;
        DAL.AddFlagArg(A, Opts.getOption(options::OPT_fno_builtin));
      } else {
        DAL.AddFlagArg(A, Opts.getOption(options::OPT_fbuiltin));
      }
      break;
    case 's':
      DAL.AddJoinedArg(A, Opts.getOption(options::OPT_O), "s");
      break;
    case 't':
      DAL.AddJoinedArg(A, Opts.getOption(options::OPT_O), "3");
      break;
    case 'y': {
      bool OmitFramePointer = true;
      if (I + 1 != E && OptStr[I + 1] == '-') {
        OmitFramePointer = false;
        ++I;
      }
      if (SupportsForcingFramePointer) {
        if (OmitFramePointer)
          DAL.AddFlagArg(A,
                         Opts.getOption(options::OPT_fomit_frame_pointer));
        else
          DAL.AddFlagArg(
              A, Opts.getOption(options::OPT_fno_omit_frame_pointer));
      } else {
        // Don't warn about /Oy- in x86-64 builds (where
        // SupportsForcingFramePointer is false).  The flag having no effect
        // there is a compiler-internal optimization, and people shouldn't have
        // to special-case their build files for x86-64 clang-cl.
        A->claim();
      }
      break;
    }
    }
  }
}

static void TranslateDArg(Arg *A, llvm::opt::DerivedArgList &DAL,
                          const OptTable &Opts) {
  assert(A->getOption().matches(options::OPT_D));

  StringRef Val = A->getValue();
  size_t Hash = Val.find('#');
  if (Hash == StringRef::npos || Hash > Val.find('=')) {
    DAL.append(A);
    return;
  }

  std::string NewVal = std::string(Val);
  NewVal[Hash] = '=';
  DAL.AddJoinedArg(A, Opts.getOption(options::OPT_D), NewVal);
}

static void TranslatePermissive(Arg *A, llvm::opt::DerivedArgList &DAL,
                                const OptTable &Opts) {
  DAL.AddFlagArg(A, Opts.getOption(options::OPT__SLASH_Zc_twoPhase_));
  DAL.AddFlagArg(A, Opts.getOption(options::OPT_fno_operator_names));
}

static void TranslatePermissiveMinus(Arg *A, llvm::opt::DerivedArgList &DAL,
                                     const OptTable &Opts) {
  DAL.AddFlagArg(A, Opts.getOption(options::OPT__SLASH_Zc_twoPhase));
  DAL.AddFlagArg(A, Opts.getOption(options::OPT_foperator_names));
}

llvm::opt::DerivedArgList *
MSVCToolChain::TranslateArgs(const llvm::opt::DerivedArgList &Args,
                             StringRef BoundArch,
                             Action::OffloadKind OFK) const {
  DerivedArgList *DAL = new DerivedArgList(Args.getBaseArgs());
  const OptTable &Opts = getDriver().getOpts();

  // /Oy and /Oy- don't have an effect on X86-64
  bool SupportsForcingFramePointer = getArch() != llvm::Triple::x86_64;

  // The -O[12xd] flag actually expands to several flags.  We must desugar the
  // flags so that options embedded can be negated.  For example, the '-O2' flag
  // enables '-Oy'.  Expanding '-O2' into its constituent flags allows us to
  // correctly handle '-O2 -Oy-' where the trailing '-Oy-' disables a single
  // aspect of '-O2'.
  //
  // Note that this expansion logic only applies to the *last* of '[12xd]'.

  // First step is to search for the character we'd like to expand.
  const char *ExpandChar = nullptr;
  for (Arg *A : Args.filtered(options::OPT__SLASH_O)) {
    StringRef OptStr = A->getValue();
    for (size_t I = 0, E = OptStr.size(); I != E; ++I) {
      char OptChar = OptStr[I];
      char PrevChar = I > 0 ? OptStr[I - 1] : '0';
      if (PrevChar == 'b') {
        // OptChar does not expand; it's an argument to the previous char.
        continue;
      }
      if (OptChar == '1' || OptChar == '2' || OptChar == 'x' || OptChar == 'd')
        ExpandChar = OptStr.data() + I;
    }
  }

  for (Arg *A : Args) {
    if (A->getOption().matches(options::OPT__SLASH_O)) {
      // The -O flag actually takes an amalgam of other options.  For example,
      // '/Ogyb2' is equivalent to '/Og' '/Oy' '/Ob2'.
      TranslateOptArg(A, *DAL, SupportsForcingFramePointer, ExpandChar, Opts);
    } else if (A->getOption().matches(options::OPT_D)) {
      // Translate -Dfoo#bar into -Dfoo=bar.
      TranslateDArg(A, *DAL, Opts);
    } else if (A->getOption().matches(options::OPT__SLASH_permissive)) {
      // Expand /permissive
      TranslatePermissive(A, *DAL, Opts);
    } else if (A->getOption().matches(options::OPT__SLASH_permissive_)) {
      // Expand /permissive-
      TranslatePermissiveMinus(A, *DAL, Opts);
    } else if (OFK != Action::OFK_HIP) {
      // HIP Toolchain translates input args by itself.
      DAL->append(A);
    }
  }

  return DAL;
}

void MSVCToolChain::addClangTargetOptions(
    const ArgList &DriverArgs, ArgStringList &CC1Args,
    Action::OffloadKind DeviceOffloadKind) const {
  // MSVC STL kindly allows removing all usages of typeid by defining
  // _HAS_STATIC_RTTI to 0. Do so, when compiling with -fno-rtti
  if (DriverArgs.hasFlag(options::OPT_fno_rtti, options::OPT_frtti,
                         /*Default=*/false))
    CC1Args.push_back("-D_HAS_STATIC_RTTI=0");

  if (Arg *A = DriverArgs.getLastArgNoClaim(options::OPT_marm64x))
    A->ignoreTargetSpecific();
}
