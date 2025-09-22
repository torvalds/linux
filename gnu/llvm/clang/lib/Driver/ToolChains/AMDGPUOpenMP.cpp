//===- AMDGPUOpenMP.cpp - AMDGPUOpenMP ToolChain Implementation -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AMDGPUOpenMP.h"
#include "AMDGPU.h"
#include "CommonArgs.h"
#include "ToolChains/ROCm.h"
#include "clang/Basic/DiagnosticDriver.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/InputInfo.h"
#include "clang/Driver/Options.h"
#include "clang/Driver/Tool.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatAdapters.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/Path.h"

using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

AMDGPUOpenMPToolChain::AMDGPUOpenMPToolChain(const Driver &D,
                                             const llvm::Triple &Triple,
                                             const ToolChain &HostTC,
                                             const ArgList &Args)
    : ROCMToolChain(D, Triple, Args), HostTC(HostTC) {
  // Lookup binaries into the driver directory, this is used to
  // discover the 'amdgpu-arch' executable.
  getProgramPaths().push_back(getDriver().Dir);
}

void AMDGPUOpenMPToolChain::addClangTargetOptions(
    const llvm::opt::ArgList &DriverArgs, llvm::opt::ArgStringList &CC1Args,
    Action::OffloadKind DeviceOffloadingKind) const {
  HostTC.addClangTargetOptions(DriverArgs, CC1Args, DeviceOffloadingKind);

  assert(DeviceOffloadingKind == Action::OFK_OpenMP &&
         "Only OpenMP offloading kinds are supported.");

  if (DriverArgs.hasArg(options::OPT_nogpulib))
    return;

  for (auto BCFile : getDeviceLibs(DriverArgs)) {
    CC1Args.push_back(BCFile.ShouldInternalize ? "-mlink-builtin-bitcode"
                                               : "-mlink-bitcode-file");
    CC1Args.push_back(DriverArgs.MakeArgString(BCFile.Path));
  }

  // Link the bitcode library late if we're using device LTO.
  if (getDriver().isUsingLTO(/* IsOffload */ true))
    return;
}

llvm::opt::DerivedArgList *AMDGPUOpenMPToolChain::TranslateArgs(
    const llvm::opt::DerivedArgList &Args, StringRef BoundArch,
    Action::OffloadKind DeviceOffloadKind) const {
  DerivedArgList *DAL =
      HostTC.TranslateArgs(Args, BoundArch, DeviceOffloadKind);
  if (!DAL)
    DAL = new DerivedArgList(Args.getBaseArgs());

  const OptTable &Opts = getDriver().getOpts();

  if (DeviceOffloadKind == Action::OFK_OpenMP) {
    for (Arg *A : Args)
      if (!llvm::is_contained(*DAL, A))
        DAL->append(A);

    if (!DAL->hasArg(options::OPT_march_EQ)) {
      StringRef Arch = BoundArch;
      if (Arch.empty()) {
        auto ArchsOrErr = getSystemGPUArchs(Args);
        if (!ArchsOrErr) {
          std::string ErrMsg =
              llvm::formatv("{0}", llvm::fmt_consume(ArchsOrErr.takeError()));
          getDriver().Diag(diag::err_drv_undetermined_gpu_arch)
              << llvm::Triple::getArchTypeName(getArch()) << ErrMsg << "-march";
          Arch = OffloadArchToString(OffloadArch::HIPDefault);
        } else {
          Arch = Args.MakeArgString(ArchsOrErr->front());
        }
      }
      DAL->AddJoinedArg(nullptr, Opts.getOption(options::OPT_march_EQ), Arch);
    }

    return DAL;
  }

  for (Arg *A : Args) {
    DAL->append(A);
  }

  if (!BoundArch.empty()) {
    DAL->eraseArg(options::OPT_march_EQ);
    DAL->AddJoinedArg(nullptr, Opts.getOption(options::OPT_march_EQ),
                      BoundArch);
  }

  return DAL;
}

void AMDGPUOpenMPToolChain::addClangWarningOptions(
    ArgStringList &CC1Args) const {
  AMDGPUToolChain::addClangWarningOptions(CC1Args);
  HostTC.addClangWarningOptions(CC1Args);
}

ToolChain::CXXStdlibType
AMDGPUOpenMPToolChain::GetCXXStdlibType(const ArgList &Args) const {
  return HostTC.GetCXXStdlibType(Args);
}

void AMDGPUOpenMPToolChain::AddClangSystemIncludeArgs(
    const ArgList &DriverArgs, ArgStringList &CC1Args) const {
  HostTC.AddClangSystemIncludeArgs(DriverArgs, CC1Args);
}

void AMDGPUOpenMPToolChain::AddIAMCUIncludeArgs(const ArgList &Args,
                                                ArgStringList &CC1Args) const {
  HostTC.AddIAMCUIncludeArgs(Args, CC1Args);
}

SanitizerMask AMDGPUOpenMPToolChain::getSupportedSanitizers() const {
  // The AMDGPUOpenMPToolChain only supports sanitizers in the sense that it
  // allows sanitizer arguments on the command line if they are supported by the
  // host toolchain. The AMDGPUOpenMPToolChain will actually ignore any command
  // line arguments for any of these "supported" sanitizers. That means that no
  // sanitization of device code is actually supported at this time.
  //
  // This behavior is necessary because the host and device toolchains
  // invocations often share the command line, so the device toolchain must
  // tolerate flags meant only for the host toolchain.
  return HostTC.getSupportedSanitizers();
}

VersionTuple
AMDGPUOpenMPToolChain::computeMSVCVersion(const Driver *D,
                                          const ArgList &Args) const {
  return HostTC.computeMSVCVersion(D, Args);
}

llvm::SmallVector<ToolChain::BitCodeLibraryInfo, 12>
AMDGPUOpenMPToolChain::getDeviceLibs(const llvm::opt::ArgList &Args) const {
  if (Args.hasArg(options::OPT_nogpulib))
    return {};

  if (!RocmInstallation->hasDeviceLibrary()) {
    getDriver().Diag(diag::err_drv_no_rocm_device_lib) << 0;
    return {};
  }

  StringRef GpuArch = getProcessorFromTargetID(
      getTriple(), Args.getLastArgValue(options::OPT_march_EQ));

  SmallVector<BitCodeLibraryInfo, 12> BCLibs;
  for (auto BCLib : getCommonDeviceLibNames(Args, GpuArch.str(),
                                            /*IsOpenMP=*/true))
    BCLibs.emplace_back(BCLib);

  return BCLibs;
}
