//===--- HIPSPV.cpp - HIPSPV ToolChain Implementation -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HIPSPV.h"
#include "CommonArgs.h"
#include "HIPUtility.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/InputInfo.h"
#include "clang/Driver/Options.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

// Convenience function for creating temporary file for both modes of
// isSaveTempsEnabled().
static const char *getTempFile(Compilation &C, StringRef Prefix,
                               StringRef Extension) {
  if (C.getDriver().isSaveTempsEnabled()) {
    return C.getArgs().MakeArgString(Prefix + "." + Extension);
  }
  auto TmpFile = C.getDriver().GetTemporaryPath(Prefix, Extension);
  return C.addTempFile(C.getArgs().MakeArgString(TmpFile));
}

// Locates HIP pass plugin.
static std::string findPassPlugin(const Driver &D,
                                  const llvm::opt::ArgList &Args) {
  StringRef Path = Args.getLastArgValue(options::OPT_hipspv_pass_plugin_EQ);
  if (!Path.empty()) {
    if (llvm::sys::fs::exists(Path))
      return Path.str();
    D.Diag(diag::err_drv_no_such_file) << Path;
  }

  StringRef hipPath = Args.getLastArgValue(options::OPT_hip_path_EQ);
  if (!hipPath.empty()) {
    SmallString<128> PluginPath(hipPath);
    llvm::sys::path::append(PluginPath, "lib", "libLLVMHipSpvPasses.so");
    if (llvm::sys::fs::exists(PluginPath))
      return PluginPath.str().str();
    PluginPath.assign(hipPath);
    llvm::sys::path::append(PluginPath, "lib", "llvm",
                            "libLLVMHipSpvPasses.so");
    if (llvm::sys::fs::exists(PluginPath))
      return PluginPath.str().str();
  }

  return std::string();
}

void HIPSPV::Linker::constructLinkAndEmitSpirvCommand(
    Compilation &C, const JobAction &JA, const InputInfoList &Inputs,
    const InputInfo &Output, const llvm::opt::ArgList &Args) const {

  assert(!Inputs.empty() && "Must have at least one input.");
  std::string Name = std::string(llvm::sys::path::stem(Output.getFilename()));
  const char *TempFile = getTempFile(C, Name + "-link", "bc");

  // Link LLVM bitcode.
  ArgStringList LinkArgs{};
  for (auto Input : Inputs)
    LinkArgs.push_back(Input.getFilename());
  LinkArgs.append({"-o", TempFile});
  const char *LlvmLink =
      Args.MakeArgString(getToolChain().GetProgramPath("llvm-link"));
  C.addCommand(std::make_unique<Command>(JA, *this, ResponseFileSupport::None(),
                                         LlvmLink, LinkArgs, Inputs, Output));

  // Post-link HIP lowering.

  // Run LLVM IR passes to lower/expand/emulate HIP code that does not translate
  // to SPIR-V (E.g. dynamic shared memory).
  auto PassPluginPath = findPassPlugin(C.getDriver(), Args);
  if (!PassPluginPath.empty()) {
    const char *PassPathCStr = C.getArgs().MakeArgString(PassPluginPath);
    const char *OptOutput = getTempFile(C, Name + "-lower", "bc");
    ArgStringList OptArgs{TempFile,     "-load-pass-plugin",
                          PassPathCStr, "-passes=hip-post-link-passes",
                          "-o",         OptOutput};
    const char *Opt = Args.MakeArgString(getToolChain().GetProgramPath("opt"));
    C.addCommand(std::make_unique<Command>(
        JA, *this, ResponseFileSupport::None(), Opt, OptArgs, Inputs, Output));
    TempFile = OptOutput;
  }

  // Emit SPIR-V binary.

  llvm::opt::ArgStringList TrArgs{"--spirv-max-version=1.1",
                                  "--spirv-ext=+all"};
  InputInfo TrInput = InputInfo(types::TY_LLVM_BC, TempFile, "");
  SPIRV::constructTranslateCommand(C, *this, JA, Output, TrInput, TrArgs);
}

void HIPSPV::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                  const InputInfo &Output,
                                  const InputInfoList &Inputs,
                                  const ArgList &Args,
                                  const char *LinkingOutput) const {
  if (Inputs.size() > 0 && Inputs[0].getType() == types::TY_Image &&
      JA.getType() == types::TY_Object)
    return HIP::constructGenerateObjFileFromHIPFatBinary(C, Output, Inputs,
                                                         Args, JA, *this);

  if (JA.getType() == types::TY_HIP_FATBIN)
    return HIP::constructHIPFatbinCommand(C, JA, Output.getFilename(), Inputs,
                                          Args, *this);

  constructLinkAndEmitSpirvCommand(C, JA, Inputs, Output, Args);
}

HIPSPVToolChain::HIPSPVToolChain(const Driver &D, const llvm::Triple &Triple,
                                 const ToolChain &HostTC, const ArgList &Args)
    : ToolChain(D, Triple, Args), HostTC(HostTC) {
  // Lookup binaries into the driver directory, this is used to
  // discover the clang-offload-bundler executable.
  getProgramPaths().push_back(getDriver().Dir);
}

void HIPSPVToolChain::addClangTargetOptions(
    const llvm::opt::ArgList &DriverArgs, llvm::opt::ArgStringList &CC1Args,
    Action::OffloadKind DeviceOffloadingKind) const {
  HostTC.addClangTargetOptions(DriverArgs, CC1Args, DeviceOffloadingKind);

  assert(DeviceOffloadingKind == Action::OFK_HIP &&
         "Only HIP offloading kinds are supported for GPUs.");

  CC1Args.append(
      {"-fcuda-is-device", "-fcuda-allow-variadic-functions",
       // A crude workaround for llvm-spirv which does not handle the
       // autovectorized code well (vector reductions, non-i{8,16,32,64} types).
       // TODO: Allow autovectorization when SPIR-V backend arrives.
       "-mllvm", "-vectorize-loops=false", "-mllvm", "-vectorize-slp=false"});

  // Default to "hidden" visibility, as object level linking will not be
  // supported for the foreseeable future.
  if (!DriverArgs.hasArg(options::OPT_fvisibility_EQ,
                         options::OPT_fvisibility_ms_compat))
    CC1Args.append(
        {"-fvisibility=hidden", "-fapply-global-visibility-to-externs"});

  llvm::for_each(getDeviceLibs(DriverArgs),
                 [&](const BitCodeLibraryInfo &BCFile) {
                   CC1Args.append({"-mlink-builtin-bitcode",
                                   DriverArgs.MakeArgString(BCFile.Path)});
                 });
}

Tool *HIPSPVToolChain::buildLinker() const {
  assert(getTriple().getArch() == llvm::Triple::spirv64);
  return new tools::HIPSPV::Linker(*this);
}

void HIPSPVToolChain::addClangWarningOptions(ArgStringList &CC1Args) const {
  HostTC.addClangWarningOptions(CC1Args);
}

ToolChain::CXXStdlibType
HIPSPVToolChain::GetCXXStdlibType(const ArgList &Args) const {
  return HostTC.GetCXXStdlibType(Args);
}

void HIPSPVToolChain::AddClangSystemIncludeArgs(const ArgList &DriverArgs,
                                                ArgStringList &CC1Args) const {
  HostTC.AddClangSystemIncludeArgs(DriverArgs, CC1Args);
}

void HIPSPVToolChain::AddClangCXXStdlibIncludeArgs(
    const ArgList &Args, ArgStringList &CC1Args) const {
  HostTC.AddClangCXXStdlibIncludeArgs(Args, CC1Args);
}

void HIPSPVToolChain::AddIAMCUIncludeArgs(const ArgList &Args,
                                          ArgStringList &CC1Args) const {
  HostTC.AddIAMCUIncludeArgs(Args, CC1Args);
}

void HIPSPVToolChain::AddHIPIncludeArgs(const ArgList &DriverArgs,
                                        ArgStringList &CC1Args) const {
  if (DriverArgs.hasArg(options::OPT_nogpuinc))
    return;

  StringRef hipPath = DriverArgs.getLastArgValue(options::OPT_hip_path_EQ);
  if (hipPath.empty()) {
    getDriver().Diag(diag::err_drv_hipspv_no_hip_path);
    return;
  }
  SmallString<128> P(hipPath);
  llvm::sys::path::append(P, "include");
  CC1Args.append({"-isystem", DriverArgs.MakeArgString(P)});
}

llvm::SmallVector<ToolChain::BitCodeLibraryInfo, 12>
HIPSPVToolChain::getDeviceLibs(const llvm::opt::ArgList &DriverArgs) const {
  llvm::SmallVector<ToolChain::BitCodeLibraryInfo, 12> BCLibs;
  if (DriverArgs.hasArg(options::OPT_nogpulib))
    return {};

  ArgStringList LibraryPaths;
  // Find device libraries in --hip-device-lib-path and HIP_DEVICE_LIB_PATH.
  auto HipDeviceLibPathArgs = DriverArgs.getAllArgValues(
      // --hip-device-lib-path is alias to this option.
      clang::driver::options::OPT_rocm_device_lib_path_EQ);
  for (auto Path : HipDeviceLibPathArgs)
    LibraryPaths.push_back(DriverArgs.MakeArgString(Path));

  StringRef HipPath = DriverArgs.getLastArgValue(options::OPT_hip_path_EQ);
  if (!HipPath.empty()) {
    SmallString<128> Path(HipPath);
    llvm::sys::path::append(Path, "lib", "hip-device-lib");
    LibraryPaths.push_back(DriverArgs.MakeArgString(Path));
  }

  addDirectoryList(DriverArgs, LibraryPaths, "", "HIP_DEVICE_LIB_PATH");

  // Maintain compatability with --hip-device-lib.
  auto BCLibArgs = DriverArgs.getAllArgValues(options::OPT_hip_device_lib_EQ);
  if (!BCLibArgs.empty()) {
    llvm::for_each(BCLibArgs, [&](StringRef BCName) {
      StringRef FullName;
      for (std::string LibraryPath : LibraryPaths) {
        SmallString<128> Path(LibraryPath);
        llvm::sys::path::append(Path, BCName);
        FullName = Path;
        if (llvm::sys::fs::exists(FullName)) {
          BCLibs.emplace_back(FullName.str());
          return;
        }
      }
      getDriver().Diag(diag::err_drv_no_such_file) << BCName;
    });
  } else {
    // Search device library named as 'hipspv-<triple>.bc'.
    auto TT = getTriple().normalize();
    std::string BCName = "hipspv-" + TT + ".bc";
    for (auto *LibPath : LibraryPaths) {
      SmallString<128> Path(LibPath);
      llvm::sys::path::append(Path, BCName);
      if (llvm::sys::fs::exists(Path)) {
        BCLibs.emplace_back(Path.str().str());
        return BCLibs;
      }
    }
    getDriver().Diag(diag::err_drv_no_hipspv_device_lib)
        << 1 << ("'" + TT + "' target");
    return {};
  }

  return BCLibs;
}

SanitizerMask HIPSPVToolChain::getSupportedSanitizers() const {
  // The HIPSPVToolChain only supports sanitizers in the sense that it allows
  // sanitizer arguments on the command line if they are supported by the host
  // toolchain. The HIPSPVToolChain will actually ignore any command line
  // arguments for any of these "supported" sanitizers. That means that no
  // sanitization of device code is actually supported at this time.
  //
  // This behavior is necessary because the host and device toolchains
  // invocations often share the command line, so the device toolchain must
  // tolerate flags meant only for the host toolchain.
  return HostTC.getSupportedSanitizers();
}

VersionTuple HIPSPVToolChain::computeMSVCVersion(const Driver *D,
                                                 const ArgList &Args) const {
  return HostTC.computeMSVCVersion(D, Args);
}

void HIPSPVToolChain::adjustDebugInfoKind(
    llvm::codegenoptions::DebugInfoKind &DebugInfoKind,
    const llvm::opt::ArgList &Args) const {
  // Debug info generation is disabled for SPIRV-LLVM-Translator
  // which currently aborts on the presence of DW_OP_LLVM_convert.
  // TODO: Enable debug info when the SPIR-V backend arrives.
  DebugInfoKind = llvm::codegenoptions::NoDebugInfo;
}
