//===--- WebAssembly.cpp - WebAssembly ToolChain Implementation -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "WebAssembly.h"
#include "CommonArgs.h"
#include "Gnu.h"
#include "clang/Basic/Version.h"
#include "clang/Config/config.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/VirtualFileSystem.h"

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang::driver::toolchains;
using namespace clang;
using namespace llvm::opt;

/// Following the conventions in https://wiki.debian.org/Multiarch/Tuples,
/// we remove the vendor field to form the multiarch triple.
std::string WebAssembly::getMultiarchTriple(const Driver &D,
                                            const llvm::Triple &TargetTriple,
                                            StringRef SysRoot) const {
    return (TargetTriple.getArchName() + "-" +
            TargetTriple.getOSAndEnvironmentName()).str();
}

std::string wasm::Linker::getLinkerPath(const ArgList &Args) const {
  const ToolChain &ToolChain = getToolChain();
  if (const Arg* A = Args.getLastArg(options::OPT_fuse_ld_EQ)) {
    StringRef UseLinker = A->getValue();
    if (!UseLinker.empty()) {
      if (llvm::sys::path::is_absolute(UseLinker) &&
          llvm::sys::fs::can_execute(UseLinker))
        return std::string(UseLinker);

      // Interpret 'lld' as explicitly requesting `wasm-ld`, so look for that
      // linker. Note that for `wasm32-wasip2` this overrides the default linker
      // of `wasm-component-ld`.
      if (UseLinker == "lld") {
        return ToolChain.GetProgramPath("wasm-ld");
      }

      // Allow 'ld' as an alias for the default linker
      if (UseLinker != "ld")
        ToolChain.getDriver().Diag(diag::err_drv_invalid_linker_name)
            << A->getAsString(Args);
    }
  }

  return ToolChain.GetProgramPath(ToolChain.getDefaultLinker());
}

void wasm::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                const InputInfo &Output,
                                const InputInfoList &Inputs,
                                const ArgList &Args,
                                const char *LinkingOutput) const {

  const ToolChain &ToolChain = getToolChain();
  const char *Linker = Args.MakeArgString(getLinkerPath(Args));
  ArgStringList CmdArgs;

  CmdArgs.push_back("-m");
  if (ToolChain.getTriple().isArch64Bit())
    CmdArgs.push_back("wasm64");
  else
    CmdArgs.push_back("wasm32");

  if (Args.hasArg(options::OPT_s))
    CmdArgs.push_back("--strip-all");

  // On `wasip2` the default linker is `wasm-component-ld` which wraps the
  // execution of `wasm-ld`. Find `wasm-ld` and pass it as an argument of where
  // to find it to avoid it needing to hunt and rediscover or search `PATH` for
  // where it is.
  if (llvm::sys::path::stem(Linker).ends_with_insensitive(
          "wasm-component-ld")) {
    CmdArgs.push_back("--wasm-ld-path");
    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetProgramPath("wasm-ld")));
  }

  Args.addAllArgs(CmdArgs, {options::OPT_L, options::OPT_u});

  ToolChain.AddFilePathLibArgs(Args, CmdArgs);

  bool IsCommand = true;
  const char *Crt1;
  const char *Entry = nullptr;

  // When -shared is specified, use the reactor exec model unless
  // specified otherwise.
  if (Args.hasArg(options::OPT_shared))
    IsCommand = false;

  if (const Arg *A = Args.getLastArg(options::OPT_mexec_model_EQ)) {
    StringRef CM = A->getValue();
    if (CM == "command") {
      IsCommand = true;
    } else if (CM == "reactor") {
      IsCommand = false;
    } else {
      ToolChain.getDriver().Diag(diag::err_drv_invalid_argument_to_option)
          << CM << A->getOption().getName();
    }
  }

  if (IsCommand) {
    // If crt1-command.o exists, it supports new-style commands, so use it.
    // Otherwise, use the old crt1.o. This is a temporary transition measure.
    // Once WASI libc no longer needs to support LLVM versions which lack
    // support for new-style command, it can make crt1.o the same as
    // crt1-command.o. And once LLVM no longer needs to support WASI libc
    // versions before that, it can switch to using crt1-command.o.
    Crt1 = "crt1.o";
    if (ToolChain.GetFilePath("crt1-command.o") != "crt1-command.o")
      Crt1 = "crt1-command.o";
  } else {
    Crt1 = "crt1-reactor.o";
    Entry = "_initialize";
  }

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles))
    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath(Crt1)));
  if (Entry) {
    CmdArgs.push_back(Args.MakeArgString("--entry"));
    CmdArgs.push_back(Args.MakeArgString(Entry));
  }

  if (Args.hasArg(options::OPT_shared))
    CmdArgs.push_back(Args.MakeArgString("-shared"));

  AddLinkerInputs(ToolChain, Inputs, Args, CmdArgs, JA);

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs)) {
    if (ToolChain.ShouldLinkCXXStdlib(Args))
      ToolChain.AddCXXStdlibLibArgs(Args, CmdArgs);

    if (Args.hasArg(options::OPT_pthread)) {
      CmdArgs.push_back("-lpthread");
      CmdArgs.push_back("--shared-memory");
    }

    CmdArgs.push_back("-lc");
    AddRunTimeLibs(ToolChain, ToolChain.getDriver(), CmdArgs, Args);
  }

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  if (Args.hasFlag(options::OPT_wasm_opt, options::OPT_no_wasm_opt, true)) {
    // When optimizing, if wasm-opt is available, run it.
    std::string WasmOptPath;
    if (Args.getLastArg(options::OPT_O_Group)) {
      WasmOptPath = ToolChain.GetProgramPath("wasm-opt");
      if (WasmOptPath == "wasm-opt") {
        WasmOptPath = {};
      }
    }

    if (!WasmOptPath.empty()) {
      CmdArgs.push_back("--keep-section=target_features");
    }

    C.addCommand(std::make_unique<Command>(JA, *this,
                                           ResponseFileSupport::AtFileCurCP(),
                                           Linker, CmdArgs, Inputs, Output));

    if (Arg *A = Args.getLastArg(options::OPT_O_Group)) {
      if (!WasmOptPath.empty()) {
        StringRef OOpt = "s";
        if (A->getOption().matches(options::OPT_O4) ||
            A->getOption().matches(options::OPT_Ofast))
          OOpt = "4";
        else if (A->getOption().matches(options::OPT_O0))
          OOpt = "0";
        else if (A->getOption().matches(options::OPT_O))
          OOpt = A->getValue();

        if (OOpt != "0") {
          const char *WasmOpt = Args.MakeArgString(WasmOptPath);
          ArgStringList OptArgs;
          OptArgs.push_back(Output.getFilename());
          OptArgs.push_back(Args.MakeArgString(llvm::Twine("-O") + OOpt));
          OptArgs.push_back("-o");
          OptArgs.push_back(Output.getFilename());
          C.addCommand(std::make_unique<Command>(
              JA, *this, ResponseFileSupport::AtFileCurCP(), WasmOpt, OptArgs,
              Inputs, Output));
        }
      }
    }
  }
}

/// Given a base library directory, append path components to form the
/// LTO directory.
static std::string AppendLTOLibDir(const std::string &Dir) {
    // The version allows the path to be keyed to the specific version of
    // LLVM in used, as the bitcode format is not stable.
    return Dir + "/llvm-lto/" LLVM_VERSION_STRING;
}

WebAssembly::WebAssembly(const Driver &D, const llvm::Triple &Triple,
                         const llvm::opt::ArgList &Args)
    : ToolChain(D, Triple, Args) {

  assert(Triple.isArch32Bit() != Triple.isArch64Bit());

  getProgramPaths().push_back(getDriver().Dir);

  auto SysRoot = getDriver().SysRoot;
  if (getTriple().getOS() == llvm::Triple::UnknownOS) {
    // Theoretically an "unknown" OS should mean no standard libraries, however
    // it could also mean that a custom set of libraries is in use, so just add
    // /lib to the search path. Disable multiarch in this case, to discourage
    // paths containing "unknown" from acquiring meanings.
    getFilePaths().push_back(SysRoot + "/lib");
  } else {
    const std::string MultiarchTriple =
        getMultiarchTriple(getDriver(), Triple, SysRoot);
    if (D.isUsingLTO()) {
      // For LTO, enable use of lto-enabled sysroot libraries too, if available.
      // Note that the directory is keyed to the LLVM revision, as LLVM's
      // bitcode format is not stable.
      auto Dir = AppendLTOLibDir(SysRoot + "/lib/" + MultiarchTriple);
      getFilePaths().push_back(Dir);
    }
    getFilePaths().push_back(SysRoot + "/lib/" + MultiarchTriple);
  }
}

const char *WebAssembly::getDefaultLinker() const {
  if (getOS() == "wasip2")
    return "wasm-component-ld";
  return "wasm-ld";
}

bool WebAssembly::IsMathErrnoDefault() const { return false; }

bool WebAssembly::IsObjCNonFragileABIDefault() const { return true; }

bool WebAssembly::UseObjCMixedDispatch() const { return true; }

bool WebAssembly::isPICDefault() const { return false; }

bool WebAssembly::isPIEDefault(const llvm::opt::ArgList &Args) const {
  return false;
}

bool WebAssembly::isPICDefaultForced() const { return false; }

bool WebAssembly::hasBlocksRuntime() const { return false; }

// TODO: Support profiling.
bool WebAssembly::SupportsProfiling() const { return false; }

bool WebAssembly::HasNativeLLVMSupport() const { return true; }

void WebAssembly::addClangTargetOptions(const ArgList &DriverArgs,
                                        ArgStringList &CC1Args,
                                        Action::OffloadKind) const {
  if (!DriverArgs.hasFlag(clang::driver::options::OPT_fuse_init_array,
                          options::OPT_fno_use_init_array, true))
    CC1Args.push_back("-fno-use-init-array");

  // '-pthread' implies atomics, bulk-memory, mutable-globals, and sign-ext
  if (DriverArgs.hasFlag(options::OPT_pthread, options::OPT_no_pthread,
                         false)) {
    if (DriverArgs.hasFlag(options::OPT_mno_atomics, options::OPT_matomics,
                           false))
      getDriver().Diag(diag::err_drv_argument_not_allowed_with)
          << "-pthread"
          << "-mno-atomics";
    if (DriverArgs.hasFlag(options::OPT_mno_bulk_memory,
                           options::OPT_mbulk_memory, false))
      getDriver().Diag(diag::err_drv_argument_not_allowed_with)
          << "-pthread"
          << "-mno-bulk-memory";
    if (DriverArgs.hasFlag(options::OPT_mno_mutable_globals,
                           options::OPT_mmutable_globals, false))
      getDriver().Diag(diag::err_drv_argument_not_allowed_with)
          << "-pthread"
          << "-mno-mutable-globals";
    if (DriverArgs.hasFlag(options::OPT_mno_sign_ext, options::OPT_msign_ext,
                           false))
      getDriver().Diag(diag::err_drv_argument_not_allowed_with)
          << "-pthread"
          << "-mno-sign-ext";
    CC1Args.push_back("-target-feature");
    CC1Args.push_back("+atomics");
    CC1Args.push_back("-target-feature");
    CC1Args.push_back("+bulk-memory");
    CC1Args.push_back("-target-feature");
    CC1Args.push_back("+mutable-globals");
    CC1Args.push_back("-target-feature");
    CC1Args.push_back("+sign-ext");
  }

  if (!DriverArgs.hasFlag(options::OPT_mmutable_globals,
                          options::OPT_mno_mutable_globals, false)) {
    // -fPIC implies +mutable-globals because the PIC ABI used by the linker
    // depends on importing and exporting mutable globals.
    llvm::Reloc::Model RelocationModel;
    unsigned PICLevel;
    bool IsPIE;
    std::tie(RelocationModel, PICLevel, IsPIE) =
        ParsePICArgs(*this, DriverArgs);
    if (RelocationModel == llvm::Reloc::PIC_) {
      if (DriverArgs.hasFlag(options::OPT_mno_mutable_globals,
                             options::OPT_mmutable_globals, false)) {
        getDriver().Diag(diag::err_drv_argument_not_allowed_with)
            << "-fPIC"
            << "-mno-mutable-globals";
      }
      CC1Args.push_back("-target-feature");
      CC1Args.push_back("+mutable-globals");
    }
  }

  if (DriverArgs.getLastArg(options::OPT_fwasm_exceptions)) {
    // '-fwasm-exceptions' is not compatible with '-mno-exception-handling'
    if (DriverArgs.hasFlag(options::OPT_mno_exception_handing,
                           options::OPT_mexception_handing, false))
      getDriver().Diag(diag::err_drv_argument_not_allowed_with)
          << "-fwasm-exceptions"
          << "-mno-exception-handling";
    // '-fwasm-exceptions' is not compatible with
    // '-mllvm -enable-emscripten-cxx-exceptions'
    for (const Arg *A : DriverArgs.filtered(options::OPT_mllvm)) {
      if (StringRef(A->getValue(0)) == "-enable-emscripten-cxx-exceptions")
        getDriver().Diag(diag::err_drv_argument_not_allowed_with)
            << "-fwasm-exceptions"
            << "-mllvm -enable-emscripten-cxx-exceptions";
    }
    // '-fwasm-exceptions' implies exception-handling feature
    CC1Args.push_back("-target-feature");
    CC1Args.push_back("+exception-handling");
    // Backend needs -wasm-enable-eh to enable Wasm EH
    CC1Args.push_back("-mllvm");
    CC1Args.push_back("-wasm-enable-eh");

    // New Wasm EH spec (adopted in Oct 2023) requires multivalue and
    // reference-types.
    if (DriverArgs.hasFlag(options::OPT_mno_multivalue,
                           options::OPT_mmultivalue, false)) {
      getDriver().Diag(diag::err_drv_argument_not_allowed_with)
          << "-fwasm-exceptions" << "-mno-multivalue";
    }
    if (DriverArgs.hasFlag(options::OPT_mno_reference_types,
                           options::OPT_mreference_types, false)) {
      getDriver().Diag(diag::err_drv_argument_not_allowed_with)
          << "-fwasm-exceptions" << "-mno-reference-types";
    }
    CC1Args.push_back("-target-feature");
    CC1Args.push_back("+multivalue");
    CC1Args.push_back("-target-feature");
    CC1Args.push_back("+reference-types");
  }

  for (const Arg *A : DriverArgs.filtered(options::OPT_mllvm)) {
    StringRef Opt = A->getValue(0);
    if (Opt.starts_with("-emscripten-cxx-exceptions-allowed")) {
      // '-mllvm -emscripten-cxx-exceptions-allowed' should be used with
      // '-mllvm -enable-emscripten-cxx-exceptions'
      bool EmEHArgExists = false;
      for (const Arg *A : DriverArgs.filtered(options::OPT_mllvm)) {
        if (StringRef(A->getValue(0)) == "-enable-emscripten-cxx-exceptions") {
          EmEHArgExists = true;
          break;
        }
      }
      if (!EmEHArgExists)
        getDriver().Diag(diag::err_drv_argument_only_allowed_with)
            << "-mllvm -emscripten-cxx-exceptions-allowed"
            << "-mllvm -enable-emscripten-cxx-exceptions";

      // Prevent functions specified in -emscripten-cxx-exceptions-allowed list
      // from being inlined before reaching the wasm backend.
      StringRef FuncNamesStr = Opt.split('=').second;
      SmallVector<StringRef, 4> FuncNames;
      FuncNamesStr.split(FuncNames, ',');
      for (auto Name : FuncNames) {
        CC1Args.push_back("-mllvm");
        CC1Args.push_back(DriverArgs.MakeArgString("--force-attribute=" + Name +
                                                   ":noinline"));
      }
    }

    if (Opt.starts_with("-wasm-enable-sjlj")) {
      // '-mllvm -wasm-enable-sjlj' is not compatible with
      // '-mno-exception-handling'
      if (DriverArgs.hasFlag(options::OPT_mno_exception_handing,
                             options::OPT_mexception_handing, false))
        getDriver().Diag(diag::err_drv_argument_not_allowed_with)
            << "-mllvm -wasm-enable-sjlj"
            << "-mno-exception-handling";
      // '-mllvm -wasm-enable-sjlj' is not compatible with
      // '-mllvm -enable-emscripten-cxx-exceptions'
      // because we don't allow Emscripten EH + Wasm SjLj
      for (const Arg *A : DriverArgs.filtered(options::OPT_mllvm)) {
        if (StringRef(A->getValue(0)) == "-enable-emscripten-cxx-exceptions")
          getDriver().Diag(diag::err_drv_argument_not_allowed_with)
              << "-mllvm -wasm-enable-sjlj"
              << "-mllvm -enable-emscripten-cxx-exceptions";
      }
      // '-mllvm -wasm-enable-sjlj' is not compatible with
      // '-mllvm -enable-emscripten-sjlj'
      for (const Arg *A : DriverArgs.filtered(options::OPT_mllvm)) {
        if (StringRef(A->getValue(0)) == "-enable-emscripten-sjlj")
          getDriver().Diag(diag::err_drv_argument_not_allowed_with)
              << "-mllvm -wasm-enable-sjlj"
              << "-mllvm -enable-emscripten-sjlj";
      }
      // '-mllvm -wasm-enable-sjlj' implies exception-handling feature
      CC1Args.push_back("-target-feature");
      CC1Args.push_back("+exception-handling");
      // Backend needs '-exception-model=wasm' to use Wasm EH instructions
      CC1Args.push_back("-exception-model=wasm");

      // New Wasm EH spec (adopted in Oct 2023) requires multivalue and
      // reference-types.
      if (DriverArgs.hasFlag(options::OPT_mno_multivalue,
                             options::OPT_mmultivalue, false)) {
        getDriver().Diag(diag::err_drv_argument_not_allowed_with)
            << "-mllvm -wasm-enable-sjlj" << "-mno-multivalue";
      }
      if (DriverArgs.hasFlag(options::OPT_mno_reference_types,
                             options::OPT_mreference_types, false)) {
        getDriver().Diag(diag::err_drv_argument_not_allowed_with)
            << "-mllvm -wasm-enable-sjlj" << "-mno-reference-types";
      }
      CC1Args.push_back("-target-feature");
      CC1Args.push_back("+multivalue");
      CC1Args.push_back("-target-feature");
      CC1Args.push_back("+reference-types");
    }
  }
}

ToolChain::RuntimeLibType WebAssembly::GetDefaultRuntimeLibType() const {
  return ToolChain::RLT_CompilerRT;
}

ToolChain::CXXStdlibType
WebAssembly::GetCXXStdlibType(const ArgList &Args) const {
  if (Arg *A = Args.getLastArg(options::OPT_stdlib_EQ)) {
    StringRef Value = A->getValue();
    if (Value == "libc++")
      return ToolChain::CST_Libcxx;
    else if (Value == "libstdc++")
      return ToolChain::CST_Libstdcxx;
    else
      getDriver().Diag(diag::err_drv_invalid_stdlib_name)
          << A->getAsString(Args);
  }
  return ToolChain::CST_Libcxx;
}

void WebAssembly::AddClangSystemIncludeArgs(const ArgList &DriverArgs,
                                            ArgStringList &CC1Args) const {
  if (DriverArgs.hasArg(clang::driver::options::OPT_nostdinc))
    return;

  const Driver &D = getDriver();

  if (!DriverArgs.hasArg(options::OPT_nobuiltininc)) {
    SmallString<128> P(D.ResourceDir);
    llvm::sys::path::append(P, "include");
    addSystemInclude(DriverArgs, CC1Args, P);
  }

  if (DriverArgs.hasArg(options::OPT_nostdlibinc))
    return;

  // Check for configure-time C include directories.
  StringRef CIncludeDirs(C_INCLUDE_DIRS);
  if (CIncludeDirs != "") {
    SmallVector<StringRef, 5> dirs;
    CIncludeDirs.split(dirs, ":");
    for (StringRef dir : dirs) {
      StringRef Prefix =
          llvm::sys::path::is_absolute(dir) ? "" : StringRef(D.SysRoot);
      addExternCSystemInclude(DriverArgs, CC1Args, Prefix + dir);
    }
    return;
  }

  if (getTriple().getOS() != llvm::Triple::UnknownOS) {
    const std::string MultiarchTriple =
        getMultiarchTriple(D, getTriple(), D.SysRoot);
    addSystemInclude(DriverArgs, CC1Args, D.SysRoot + "/include/" + MultiarchTriple);
  }
  addSystemInclude(DriverArgs, CC1Args, D.SysRoot + "/include");
}

void WebAssembly::AddClangCXXStdlibIncludeArgs(const ArgList &DriverArgs,
                                               ArgStringList &CC1Args) const {

  if (DriverArgs.hasArg(options::OPT_nostdlibinc, options::OPT_nostdinc,
                        options::OPT_nostdincxx))
    return;

  switch (GetCXXStdlibType(DriverArgs)) {
  case ToolChain::CST_Libcxx:
    addLibCxxIncludePaths(DriverArgs, CC1Args);
    break;
  case ToolChain::CST_Libstdcxx:
    addLibStdCXXIncludePaths(DriverArgs, CC1Args);
    break;
  }
}

void WebAssembly::AddCXXStdlibLibArgs(const llvm::opt::ArgList &Args,
                                      llvm::opt::ArgStringList &CmdArgs) const {

  switch (GetCXXStdlibType(Args)) {
  case ToolChain::CST_Libcxx:
    CmdArgs.push_back("-lc++");
    if (Args.hasArg(options::OPT_fexperimental_library))
      CmdArgs.push_back("-lc++experimental");
    CmdArgs.push_back("-lc++abi");
    break;
  case ToolChain::CST_Libstdcxx:
    CmdArgs.push_back("-lstdc++");
    break;
  }
}

SanitizerMask WebAssembly::getSupportedSanitizers() const {
  SanitizerMask Res = ToolChain::getSupportedSanitizers();
  if (getTriple().isOSEmscripten()) {
    Res |= SanitizerKind::Vptr | SanitizerKind::Leak | SanitizerKind::Address;
  }
  // -fsanitize=function places two words before the function label, which are
  // -unsupported.
  Res &= ~SanitizerKind::Function;
  return Res;
}

Tool *WebAssembly::buildLinker() const {
  return new tools::wasm::Linker(*this);
}

void WebAssembly::addLibCxxIncludePaths(
    const llvm::opt::ArgList &DriverArgs,
    llvm::opt::ArgStringList &CC1Args) const {
  const Driver &D = getDriver();
  std::string SysRoot = computeSysRoot();
  std::string LibPath = SysRoot + "/include";
  const std::string MultiarchTriple =
      getMultiarchTriple(D, getTriple(), SysRoot);
  bool IsKnownOs = (getTriple().getOS() != llvm::Triple::UnknownOS);

  std::string Version = detectLibcxxVersion(LibPath);
  if (Version.empty())
    return;

  // First add the per-target include path if the OS is known.
  if (IsKnownOs) {
    std::string TargetDir = LibPath + "/" + MultiarchTriple + "/c++/" + Version;
    addSystemInclude(DriverArgs, CC1Args, TargetDir);
  }

  // Second add the generic one.
  addSystemInclude(DriverArgs, CC1Args, LibPath + "/c++/" + Version);
}

void WebAssembly::addLibStdCXXIncludePaths(
    const llvm::opt::ArgList &DriverArgs,
    llvm::opt::ArgStringList &CC1Args) const {
  // We cannot use GCCInstallationDetector here as the sysroot usually does
  // not contain a full GCC installation.
  // Instead, we search the given sysroot for /usr/include/xx, similar
  // to how we do it for libc++.
  const Driver &D = getDriver();
  std::string SysRoot = computeSysRoot();
  std::string LibPath = SysRoot + "/include";
  const std::string MultiarchTriple =
      getMultiarchTriple(D, getTriple(), SysRoot);
  bool IsKnownOs = (getTriple().getOS() != llvm::Triple::UnknownOS);

  // This is similar to detectLibcxxVersion()
  std::string Version;
  {
    std::error_code EC;
    Generic_GCC::GCCVersion MaxVersion =
        Generic_GCC::GCCVersion::Parse("0.0.0");
    SmallString<128> Path(LibPath);
    llvm::sys::path::append(Path, "c++");
    for (llvm::vfs::directory_iterator LI = getVFS().dir_begin(Path, EC), LE;
         !EC && LI != LE; LI = LI.increment(EC)) {
      StringRef VersionText = llvm::sys::path::filename(LI->path());
      if (VersionText[0] != 'v') {
        auto Version = Generic_GCC::GCCVersion::Parse(VersionText);
        if (Version > MaxVersion)
          MaxVersion = Version;
      }
    }
    if (MaxVersion.Major > 0)
      Version = MaxVersion.Text;
  }

  if (Version.empty())
    return;

  // First add the per-target include path if the OS is known.
  if (IsKnownOs) {
    std::string TargetDir = LibPath + "/c++/" + Version + "/" + MultiarchTriple;
    addSystemInclude(DriverArgs, CC1Args, TargetDir);
  }

  // Second add the generic one.
  addSystemInclude(DriverArgs, CC1Args, LibPath + "/c++/" + Version);
  // Third the backward one.
  addSystemInclude(DriverArgs, CC1Args, LibPath + "/c++/" + Version + "/backward");
}
