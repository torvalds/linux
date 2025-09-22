//===--- AMDGPU.cpp - AMDGPU ToolChain Implementations ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "CommonArgs.h"
#include "clang/Basic/TargetID.h"
#include "clang/Config/config.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/InputInfo.h"
#include "clang/Driver/Options.h"
#include "clang/Driver/SanitizerArgs.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/TargetParser/Host.h"
#include <optional>
#include <system_error>

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang::driver::toolchains;
using namespace clang;
using namespace llvm::opt;

// Look for sub-directory starts with PackageName under ROCm candidate path.
// If there is one and only one matching sub-directory found, append the
// sub-directory to Path. If there is no matching sub-directory or there are
// more than one matching sub-directories, diagnose them. Returns the full
// path of the package if there is only one matching sub-directory, otherwise
// returns an empty string.
llvm::SmallString<0>
RocmInstallationDetector::findSPACKPackage(const Candidate &Cand,
                                           StringRef PackageName) {
  if (!Cand.isSPACK())
    return {};
  std::error_code EC;
  std::string Prefix = Twine(PackageName + "-" + Cand.SPACKReleaseStr).str();
  llvm::SmallVector<llvm::SmallString<0>> SubDirs;
  for (llvm::vfs::directory_iterator File = D.getVFS().dir_begin(Cand.Path, EC),
                                     FileEnd;
       File != FileEnd && !EC; File.increment(EC)) {
    llvm::StringRef FileName = llvm::sys::path::filename(File->path());
    if (FileName.starts_with(Prefix)) {
      SubDirs.push_back(FileName);
      if (SubDirs.size() > 1)
        break;
    }
  }
  if (SubDirs.size() == 1) {
    auto PackagePath = Cand.Path;
    llvm::sys::path::append(PackagePath, SubDirs[0]);
    return PackagePath;
  }
  if (SubDirs.size() == 0 && Verbose) {
    llvm::errs() << "SPACK package " << Prefix << " not found at " << Cand.Path
                 << '\n';
    return {};
  }

  if (SubDirs.size() > 1 && Verbose) {
    llvm::errs() << "Cannot use SPACK package " << Prefix << " at " << Cand.Path
                 << " due to multiple installations for the same version\n";
  }
  return {};
}

void RocmInstallationDetector::scanLibDevicePath(llvm::StringRef Path) {
  assert(!Path.empty());

  const StringRef Suffix(".bc");
  const StringRef Suffix2(".amdgcn.bc");

  std::error_code EC;
  for (llvm::vfs::directory_iterator LI = D.getVFS().dir_begin(Path, EC), LE;
       !EC && LI != LE; LI = LI.increment(EC)) {
    StringRef FilePath = LI->path();
    StringRef FileName = llvm::sys::path::filename(FilePath);
    if (!FileName.ends_with(Suffix))
      continue;

    StringRef BaseName;
    if (FileName.ends_with(Suffix2))
      BaseName = FileName.drop_back(Suffix2.size());
    else if (FileName.ends_with(Suffix))
      BaseName = FileName.drop_back(Suffix.size());

    const StringRef ABIVersionPrefix = "oclc_abi_version_";
    if (BaseName == "ocml") {
      OCML = FilePath;
    } else if (BaseName == "ockl") {
      OCKL = FilePath;
    } else if (BaseName == "opencl") {
      OpenCL = FilePath;
    } else if (BaseName == "hip") {
      HIP = FilePath;
    } else if (BaseName == "asanrtl") {
      AsanRTL = FilePath;
    } else if (BaseName == "oclc_finite_only_off") {
      FiniteOnly.Off = FilePath;
    } else if (BaseName == "oclc_finite_only_on") {
      FiniteOnly.On = FilePath;
    } else if (BaseName == "oclc_daz_opt_on") {
      DenormalsAreZero.On = FilePath;
    } else if (BaseName == "oclc_daz_opt_off") {
      DenormalsAreZero.Off = FilePath;
    } else if (BaseName == "oclc_correctly_rounded_sqrt_on") {
      CorrectlyRoundedSqrt.On = FilePath;
    } else if (BaseName == "oclc_correctly_rounded_sqrt_off") {
      CorrectlyRoundedSqrt.Off = FilePath;
    } else if (BaseName == "oclc_unsafe_math_on") {
      UnsafeMath.On = FilePath;
    } else if (BaseName == "oclc_unsafe_math_off") {
      UnsafeMath.Off = FilePath;
    } else if (BaseName == "oclc_wavefrontsize64_on") {
      WavefrontSize64.On = FilePath;
    } else if (BaseName == "oclc_wavefrontsize64_off") {
      WavefrontSize64.Off = FilePath;
    } else if (BaseName.starts_with(ABIVersionPrefix)) {
      unsigned ABIVersionNumber;
      if (BaseName.drop_front(ABIVersionPrefix.size())
              .getAsInteger(/*Redex=*/0, ABIVersionNumber))
        continue;
      ABIVersionMap[ABIVersionNumber] = FilePath.str();
    } else {
      // Process all bitcode filenames that look like
      // ocl_isa_version_XXX.amdgcn.bc
      const StringRef DeviceLibPrefix = "oclc_isa_version_";
      if (!BaseName.starts_with(DeviceLibPrefix))
        continue;

      StringRef IsaVersionNumber =
        BaseName.drop_front(DeviceLibPrefix.size());

      llvm::Twine GfxName = Twine("gfx") + IsaVersionNumber;
      SmallString<8> Tmp;
      LibDeviceMap.insert(
        std::make_pair(GfxName.toStringRef(Tmp), FilePath.str()));
    }
  }
}

// Parse and extract version numbers from `.hipVersion`. Return `true` if
// the parsing fails.
bool RocmInstallationDetector::parseHIPVersionFile(llvm::StringRef V) {
  SmallVector<StringRef, 4> VersionParts;
  V.split(VersionParts, '\n');
  unsigned Major = ~0U;
  unsigned Minor = ~0U;
  for (auto Part : VersionParts) {
    auto Splits = Part.rtrim().split('=');
    if (Splits.first == "HIP_VERSION_MAJOR") {
      if (Splits.second.getAsInteger(0, Major))
        return true;
    } else if (Splits.first == "HIP_VERSION_MINOR") {
      if (Splits.second.getAsInteger(0, Minor))
        return true;
    } else if (Splits.first == "HIP_VERSION_PATCH")
      VersionPatch = Splits.second.str();
  }
  if (Major == ~0U || Minor == ~0U)
    return true;
  VersionMajorMinor = llvm::VersionTuple(Major, Minor);
  DetectedVersion =
      (Twine(Major) + "." + Twine(Minor) + "." + VersionPatch).str();
  return false;
}

/// \returns a list of candidate directories for ROCm installation, which is
/// cached and populated only once.
const SmallVectorImpl<RocmInstallationDetector::Candidate> &
RocmInstallationDetector::getInstallationPathCandidates() {

  // Return the cached candidate list if it has already been populated.
  if (!ROCmSearchDirs.empty())
    return ROCmSearchDirs;

  auto DoPrintROCmSearchDirs = [&]() {
    if (PrintROCmSearchDirs)
      for (auto Cand : ROCmSearchDirs) {
        llvm::errs() << "ROCm installation search path";
        if (Cand.isSPACK())
          llvm::errs() << " (Spack " << Cand.SPACKReleaseStr << ")";
        llvm::errs() << ": " << Cand.Path << '\n';
      }
  };

  // For candidate specified by --rocm-path we do not do strict check, i.e.,
  // checking existence of HIP version file and device library files.
  if (!RocmPathArg.empty()) {
    ROCmSearchDirs.emplace_back(RocmPathArg.str());
    DoPrintROCmSearchDirs();
    return ROCmSearchDirs;
  } else if (std::optional<std::string> RocmPathEnv =
                 llvm::sys::Process::GetEnv("ROCM_PATH")) {
    if (!RocmPathEnv->empty()) {
      ROCmSearchDirs.emplace_back(std::move(*RocmPathEnv));
      DoPrintROCmSearchDirs();
      return ROCmSearchDirs;
    }
  }

  // Try to find relative to the compiler binary.
  StringRef InstallDir = D.Dir;

  // Check both a normal Unix prefix position of the clang binary, as well as
  // the Windows-esque layout the ROCm packages use with the host architecture
  // subdirectory of bin.
  auto DeduceROCmPath = [](StringRef ClangPath) {
    // Strip off directory (usually bin)
    StringRef ParentDir = llvm::sys::path::parent_path(ClangPath);
    StringRef ParentName = llvm::sys::path::filename(ParentDir);

    // Some builds use bin/{host arch}, so go up again.
    if (ParentName == "bin") {
      ParentDir = llvm::sys::path::parent_path(ParentDir);
      ParentName = llvm::sys::path::filename(ParentDir);
    }

    // Detect ROCm packages built with SPACK.
    // clang is installed at
    // <rocm_root>/llvm-amdgpu-<rocm_release_string>-<hash>/bin directory.
    // We only consider the parent directory of llvm-amdgpu package as ROCm
    // installation candidate for SPACK.
    if (ParentName.starts_with("llvm-amdgpu-")) {
      auto SPACKPostfix =
          ParentName.drop_front(strlen("llvm-amdgpu-")).split('-');
      auto SPACKReleaseStr = SPACKPostfix.first;
      if (!SPACKReleaseStr.empty()) {
        ParentDir = llvm::sys::path::parent_path(ParentDir);
        return Candidate(ParentDir.str(), /*StrictChecking=*/true,
                         SPACKReleaseStr);
      }
    }

    // Some versions of the rocm llvm package install to /opt/rocm/llvm/bin
    // Some versions of the aomp package install to /opt/rocm/aomp/bin
    if (ParentName == "llvm" || ParentName.starts_with("aomp"))
      ParentDir = llvm::sys::path::parent_path(ParentDir);

    return Candidate(ParentDir.str(), /*StrictChecking=*/true);
  };

  // Deduce ROCm path by the path used to invoke clang. Do not resolve symbolic
  // link of clang itself.
  ROCmSearchDirs.emplace_back(DeduceROCmPath(InstallDir));

  // Deduce ROCm path by the real path of the invoked clang, resolving symbolic
  // link of clang itself.
  llvm::SmallString<256> RealClangPath;
  llvm::sys::fs::real_path(D.getClangProgramPath(), RealClangPath);
  auto ParentPath = llvm::sys::path::parent_path(RealClangPath);
  if (ParentPath != InstallDir)
    ROCmSearchDirs.emplace_back(DeduceROCmPath(ParentPath));

  // Device library may be installed in clang or resource directory.
  auto ClangRoot = llvm::sys::path::parent_path(InstallDir);
  auto RealClangRoot = llvm::sys::path::parent_path(ParentPath);
  ROCmSearchDirs.emplace_back(ClangRoot.str(), /*StrictChecking=*/true);
  if (RealClangRoot != ClangRoot)
    ROCmSearchDirs.emplace_back(RealClangRoot.str(), /*StrictChecking=*/true);
  ROCmSearchDirs.emplace_back(D.ResourceDir,
                              /*StrictChecking=*/true);

  ROCmSearchDirs.emplace_back(D.SysRoot + "/opt/rocm",
                              /*StrictChecking=*/true);

  // Find the latest /opt/rocm-{release} directory.
  std::error_code EC;
  std::string LatestROCm;
  llvm::VersionTuple LatestVer;
  // Get ROCm version from ROCm directory name.
  auto GetROCmVersion = [](StringRef DirName) {
    llvm::VersionTuple V;
    std::string VerStr = DirName.drop_front(strlen("rocm-")).str();
    // The ROCm directory name follows the format of
    // rocm-{major}.{minor}.{subMinor}[-{build}]
    std::replace(VerStr.begin(), VerStr.end(), '-', '.');
    V.tryParse(VerStr);
    return V;
  };
  for (llvm::vfs::directory_iterator
           File = D.getVFS().dir_begin(D.SysRoot + "/opt", EC),
           FileEnd;
       File != FileEnd && !EC; File.increment(EC)) {
    llvm::StringRef FileName = llvm::sys::path::filename(File->path());
    if (!FileName.starts_with("rocm-"))
      continue;
    if (LatestROCm.empty()) {
      LatestROCm = FileName.str();
      LatestVer = GetROCmVersion(LatestROCm);
      continue;
    }
    auto Ver = GetROCmVersion(FileName);
    if (LatestVer < Ver) {
      LatestROCm = FileName.str();
      LatestVer = Ver;
    }
  }
  if (!LatestROCm.empty())
    ROCmSearchDirs.emplace_back(D.SysRoot + "/opt/" + LatestROCm,
                                /*StrictChecking=*/true);

  ROCmSearchDirs.emplace_back(D.SysRoot + "/usr/local",
                              /*StrictChecking=*/true);
  ROCmSearchDirs.emplace_back(D.SysRoot + "/usr",
                              /*StrictChecking=*/true);

  DoPrintROCmSearchDirs();
  return ROCmSearchDirs;
}

RocmInstallationDetector::RocmInstallationDetector(
    const Driver &D, const llvm::Triple &HostTriple,
    const llvm::opt::ArgList &Args, bool DetectHIPRuntime, bool DetectDeviceLib)
    : D(D) {
  Verbose = Args.hasArg(options::OPT_v);
  RocmPathArg = Args.getLastArgValue(clang::driver::options::OPT_rocm_path_EQ);
  PrintROCmSearchDirs =
      Args.hasArg(clang::driver::options::OPT_print_rocm_search_dirs);
  RocmDeviceLibPathArg =
      Args.getAllArgValues(clang::driver::options::OPT_rocm_device_lib_path_EQ);
  HIPPathArg = Args.getLastArgValue(clang::driver::options::OPT_hip_path_EQ);
  HIPStdParPathArg =
    Args.getLastArgValue(clang::driver::options::OPT_hipstdpar_path_EQ);
  HasHIPStdParLibrary =
    !HIPStdParPathArg.empty() && D.getVFS().exists(HIPStdParPathArg +
                                                   "/hipstdpar_lib.hpp");
  HIPRocThrustPathArg =
    Args.getLastArgValue(clang::driver::options::OPT_hipstdpar_thrust_path_EQ);
  HasRocThrustLibrary = !HIPRocThrustPathArg.empty() &&
                        D.getVFS().exists(HIPRocThrustPathArg + "/thrust");
  HIPRocPrimPathArg =
    Args.getLastArgValue(clang::driver::options::OPT_hipstdpar_prim_path_EQ);
  HasRocPrimLibrary = !HIPRocPrimPathArg.empty() &&
                      D.getVFS().exists(HIPRocPrimPathArg + "/rocprim");

  if (auto *A = Args.getLastArg(clang::driver::options::OPT_hip_version_EQ)) {
    HIPVersionArg = A->getValue();
    unsigned Major = ~0U;
    unsigned Minor = ~0U;
    SmallVector<StringRef, 3> Parts;
    HIPVersionArg.split(Parts, '.');
    if (Parts.size())
      Parts[0].getAsInteger(0, Major);
    if (Parts.size() > 1)
      Parts[1].getAsInteger(0, Minor);
    if (Parts.size() > 2)
      VersionPatch = Parts[2].str();
    if (VersionPatch.empty())
      VersionPatch = "0";
    if (Major != ~0U && Minor == ~0U)
      Minor = 0;
    if (Major == ~0U || Minor == ~0U)
      D.Diag(diag::err_drv_invalid_value)
          << A->getAsString(Args) << HIPVersionArg;

    VersionMajorMinor = llvm::VersionTuple(Major, Minor);
    DetectedVersion =
        (Twine(Major) + "." + Twine(Minor) + "." + VersionPatch).str();
  } else {
    VersionPatch = DefaultVersionPatch;
    VersionMajorMinor =
        llvm::VersionTuple(DefaultVersionMajor, DefaultVersionMinor);
    DetectedVersion = (Twine(DefaultVersionMajor) + "." +
                       Twine(DefaultVersionMinor) + "." + VersionPatch)
                          .str();
  }

  if (DetectHIPRuntime)
    detectHIPRuntime();
  if (DetectDeviceLib)
    detectDeviceLibrary();
}

void RocmInstallationDetector::detectDeviceLibrary() {
  assert(LibDevicePath.empty());

  if (!RocmDeviceLibPathArg.empty())
    LibDevicePath = RocmDeviceLibPathArg[RocmDeviceLibPathArg.size() - 1];
  else if (std::optional<std::string> LibPathEnv =
               llvm::sys::Process::GetEnv("HIP_DEVICE_LIB_PATH"))
    LibDevicePath = std::move(*LibPathEnv);

  auto &FS = D.getVFS();
  if (!LibDevicePath.empty()) {
    // Maintain compatability with HIP flag/envvar pointing directly at the
    // bitcode library directory. This points directly at the library path instead
    // of the rocm root installation.
    if (!FS.exists(LibDevicePath))
      return;

    scanLibDevicePath(LibDevicePath);
    HasDeviceLibrary = allGenericLibsValid() && !LibDeviceMap.empty();
    return;
  }

  // Check device library exists at the given path.
  auto CheckDeviceLib = [&](StringRef Path, bool StrictChecking) {
    bool CheckLibDevice = (!NoBuiltinLibs || StrictChecking);
    if (CheckLibDevice && !FS.exists(Path))
      return false;

    scanLibDevicePath(Path);

    if (!NoBuiltinLibs) {
      // Check that the required non-target libraries are all available.
      if (!allGenericLibsValid())
        return false;

      // Check that we have found at least one libdevice that we can link in
      // if -nobuiltinlib hasn't been specified.
      if (LibDeviceMap.empty())
        return false;
    }
    return true;
  };

  // Find device libraries in <LLVM_DIR>/lib/clang/<ver>/lib/amdgcn/bitcode
  LibDevicePath = D.ResourceDir;
  llvm::sys::path::append(LibDevicePath, CLANG_INSTALL_LIBDIR_BASENAME,
                          "amdgcn", "bitcode");
  HasDeviceLibrary = CheckDeviceLib(LibDevicePath, true);
  if (HasDeviceLibrary)
    return;

  // Find device libraries in a legacy ROCm directory structure
  // ${ROCM_ROOT}/amdgcn/bitcode/*
  auto &ROCmDirs = getInstallationPathCandidates();
  for (const auto &Candidate : ROCmDirs) {
    LibDevicePath = Candidate.Path;
    llvm::sys::path::append(LibDevicePath, "amdgcn", "bitcode");
    HasDeviceLibrary = CheckDeviceLib(LibDevicePath, Candidate.StrictChecking);
    if (HasDeviceLibrary)
      return;
  }
}

void RocmInstallationDetector::detectHIPRuntime() {
  SmallVector<Candidate, 4> HIPSearchDirs;
  if (!HIPPathArg.empty())
    HIPSearchDirs.emplace_back(HIPPathArg.str());
  else if (std::optional<std::string> HIPPathEnv =
               llvm::sys::Process::GetEnv("HIP_PATH")) {
    if (!HIPPathEnv->empty())
      HIPSearchDirs.emplace_back(std::move(*HIPPathEnv));
  }
  if (HIPSearchDirs.empty())
    HIPSearchDirs.append(getInstallationPathCandidates());
  auto &FS = D.getVFS();

  for (const auto &Candidate : HIPSearchDirs) {
    InstallPath = Candidate.Path;
    if (InstallPath.empty() || !FS.exists(InstallPath))
      continue;
    // HIP runtime built by SPACK is installed to
    // <rocm_root>/hip-<rocm_release_string>-<hash> directory.
    auto SPACKPath = findSPACKPackage(Candidate, "hip");
    InstallPath = SPACKPath.empty() ? InstallPath : SPACKPath;

    BinPath = InstallPath;
    llvm::sys::path::append(BinPath, "bin");
    IncludePath = InstallPath;
    llvm::sys::path::append(IncludePath, "include");
    LibPath = InstallPath;
    llvm::sys::path::append(LibPath, "lib");
    SharePath = InstallPath;
    llvm::sys::path::append(SharePath, "share");

    // Get parent of InstallPath and append "share"
    SmallString<0> ParentSharePath = llvm::sys::path::parent_path(InstallPath);
    llvm::sys::path::append(ParentSharePath, "share");

    auto Append = [](SmallString<0> &path, const Twine &a, const Twine &b = "",
                     const Twine &c = "", const Twine &d = "") {
      SmallString<0> newpath = path;
      llvm::sys::path::append(newpath, a, b, c, d);
      return newpath;
    };
    // If HIP version file can be found and parsed, use HIP version from there.
    std::vector<SmallString<0>> VersionFilePaths = {
        Append(SharePath, "hip", "version"),
        InstallPath != D.SysRoot + "/usr/local"
            ? Append(ParentSharePath, "hip", "version")
            : SmallString<0>(),
        Append(BinPath, ".hipVersion")};

    for (const auto &VersionFilePath : VersionFilePaths) {
      if (VersionFilePath.empty())
        continue;
      llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> VersionFile =
          FS.getBufferForFile(VersionFilePath);
      if (!VersionFile)
        continue;
      if (HIPVersionArg.empty() && VersionFile)
        if (parseHIPVersionFile((*VersionFile)->getBuffer()))
          continue;

      HasHIPRuntime = true;
      return;
    }
    // Otherwise, if -rocm-path is specified (no strict checking), use the
    // default HIP version or specified by --hip-version.
    if (!Candidate.StrictChecking) {
      HasHIPRuntime = true;
      return;
    }
  }
  HasHIPRuntime = false;
}

void RocmInstallationDetector::print(raw_ostream &OS) const {
  if (hasHIPRuntime())
    OS << "Found HIP installation: " << InstallPath << ", version "
       << DetectedVersion << '\n';
}

void RocmInstallationDetector::AddHIPIncludeArgs(const ArgList &DriverArgs,
                                                 ArgStringList &CC1Args) const {
  bool UsesRuntimeWrapper = VersionMajorMinor > llvm::VersionTuple(3, 5) &&
                            !DriverArgs.hasArg(options::OPT_nohipwrapperinc);
  bool HasHipStdPar = DriverArgs.hasArg(options::OPT_hipstdpar);

  if (!DriverArgs.hasArg(options::OPT_nobuiltininc)) {
    // HIP header includes standard library wrapper headers under clang
    // cuda_wrappers directory. Since these wrapper headers include_next
    // standard C++ headers, whereas libc++ headers include_next other clang
    // headers. The include paths have to follow this order:
    // - wrapper include path
    // - standard C++ include path
    // - other clang include path
    // Since standard C++ and other clang include paths are added in other
    // places after this function, here we only need to make sure wrapper
    // include path is added.
    //
    // ROCm 3.5 does not fully support the wrapper headers. Therefore it needs
    // a workaround.
    SmallString<128> P(D.ResourceDir);
    if (UsesRuntimeWrapper)
      llvm::sys::path::append(P, "include", "cuda_wrappers");
    CC1Args.push_back("-internal-isystem");
    CC1Args.push_back(DriverArgs.MakeArgString(P));
  }

  const auto HandleHipStdPar = [=, &DriverArgs, &CC1Args]() {
    StringRef Inc = getIncludePath();
    auto &FS = D.getVFS();

    if (!hasHIPStdParLibrary())
      if (!HIPStdParPathArg.empty() ||
          !FS.exists(Inc + "/thrust/system/hip/hipstdpar/hipstdpar_lib.hpp")) {
        D.Diag(diag::err_drv_no_hipstdpar_lib);
        return;
      }
    if (!HasRocThrustLibrary && !FS.exists(Inc + "/thrust")) {
      D.Diag(diag::err_drv_no_hipstdpar_thrust_lib);
      return;
    }
    if (!HasRocPrimLibrary && !FS.exists(Inc + "/rocprim")) {
      D.Diag(diag::err_drv_no_hipstdpar_prim_lib);
      return;
    }
    const char *ThrustPath;
    if (HasRocThrustLibrary)
      ThrustPath = DriverArgs.MakeArgString(HIPRocThrustPathArg);
    else
      ThrustPath = DriverArgs.MakeArgString(Inc + "/thrust");

    const char *HIPStdParPath;
    if (hasHIPStdParLibrary())
      HIPStdParPath = DriverArgs.MakeArgString(HIPStdParPathArg);
    else
      HIPStdParPath = DriverArgs.MakeArgString(StringRef(ThrustPath) +
                                               "/system/hip/hipstdpar");

    const char *PrimPath;
    if (HasRocPrimLibrary)
      PrimPath = DriverArgs.MakeArgString(HIPRocPrimPathArg);
    else
      PrimPath = DriverArgs.MakeArgString(getIncludePath() + "/rocprim");

    CC1Args.append({"-idirafter", ThrustPath, "-idirafter", PrimPath,
                    "-idirafter", HIPStdParPath, "-include",
                    "hipstdpar_lib.hpp"});
  };

  if (DriverArgs.hasArg(options::OPT_nogpuinc)) {
    if (HasHipStdPar)
      HandleHipStdPar();

    return;
  }

  if (!hasHIPRuntime()) {
    D.Diag(diag::err_drv_no_hip_runtime);
    return;
  }

  CC1Args.push_back("-idirafter");
  CC1Args.push_back(DriverArgs.MakeArgString(getIncludePath()));
  if (UsesRuntimeWrapper)
    CC1Args.append({"-include", "__clang_hip_runtime_wrapper.h"});
  if (HasHipStdPar)
    HandleHipStdPar();
}

void amdgpu::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                  const InputInfo &Output,
                                  const InputInfoList &Inputs,
                                  const ArgList &Args,
                                  const char *LinkingOutput) const {
  std::string Linker = getToolChain().GetLinkerPath();
  ArgStringList CmdArgs;
  CmdArgs.push_back("--no-undefined");
  CmdArgs.push_back("-shared");

  addLinkerCompressDebugSectionsOption(getToolChain(), Args, CmdArgs);
  Args.AddAllArgs(CmdArgs, options::OPT_L);
  getToolChain().AddFilePathLibArgs(Args, CmdArgs);
  AddLinkerInputs(getToolChain(), Inputs, Args, CmdArgs, JA);
  if (C.getDriver().isUsingLTO())
    addLTOOptions(getToolChain(), Args, CmdArgs, Output, Inputs[0],
                  C.getDriver().getLTOMode() == LTOK_Thin);
  else if (Args.hasArg(options::OPT_mcpu_EQ))
    CmdArgs.push_back(Args.MakeArgString(
        "-plugin-opt=mcpu=" + Args.getLastArgValue(options::OPT_mcpu_EQ)));
  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());
  C.addCommand(std::make_unique<Command>(
      JA, *this, ResponseFileSupport::AtFileCurCP(), Args.MakeArgString(Linker),
      CmdArgs, Inputs, Output));
}

void amdgpu::getAMDGPUTargetFeatures(const Driver &D,
                                     const llvm::Triple &Triple,
                                     const llvm::opt::ArgList &Args,
                                     std::vector<StringRef> &Features) {
  // Add target ID features to -target-feature options. No diagnostics should
  // be emitted here since invalid target ID is diagnosed at other places.
  StringRef TargetID;
  if (Args.hasArg(options::OPT_mcpu_EQ))
    TargetID = Args.getLastArgValue(options::OPT_mcpu_EQ);
  else if (Args.hasArg(options::OPT_march_EQ))
    TargetID = Args.getLastArgValue(options::OPT_march_EQ);
  if (!TargetID.empty()) {
    llvm::StringMap<bool> FeatureMap;
    auto OptionalGpuArch = parseTargetID(Triple, TargetID, &FeatureMap);
    if (OptionalGpuArch) {
      StringRef GpuArch = *OptionalGpuArch;
      // Iterate through all possible target ID features for the given GPU.
      // If it is mapped to true, add +feature.
      // If it is mapped to false, add -feature.
      // If it is not in the map (default), do not add it
      for (auto &&Feature : getAllPossibleTargetIDFeatures(Triple, GpuArch)) {
        auto Pos = FeatureMap.find(Feature);
        if (Pos == FeatureMap.end())
          continue;
        Features.push_back(Args.MakeArgStringRef(
            (Twine(Pos->second ? "+" : "-") + Feature).str()));
      }
    }
  }

  if (Args.hasFlag(options::OPT_mwavefrontsize64,
                   options::OPT_mno_wavefrontsize64, false))
    Features.push_back("+wavefrontsize64");

  if (Args.hasFlag(options::OPT_mamdgpu_precise_memory_op,
                   options::OPT_mno_amdgpu_precise_memory_op, false))
    Features.push_back("+precise-memory");

  handleTargetFeaturesGroup(D, Triple, Args, Features,
                            options::OPT_m_amdgpu_Features_Group);
}

/// AMDGPU Toolchain
AMDGPUToolChain::AMDGPUToolChain(const Driver &D, const llvm::Triple &Triple,
                                 const ArgList &Args)
    : Generic_ELF(D, Triple, Args),
      OptionsDefault(
          {{options::OPT_O, "3"}, {options::OPT_cl_std_EQ, "CL1.2"}}) {
  // Check code object version options. Emit warnings for legacy options
  // and errors for the last invalid code object version options.
  // It is done here to avoid repeated warning or error messages for
  // each tool invocation.
  checkAMDGPUCodeObjectVersion(D, Args);
}

Tool *AMDGPUToolChain::buildLinker() const {
  return new tools::amdgpu::Linker(*this);
}

DerivedArgList *
AMDGPUToolChain::TranslateArgs(const DerivedArgList &Args, StringRef BoundArch,
                               Action::OffloadKind DeviceOffloadKind) const {

  DerivedArgList *DAL =
      Generic_ELF::TranslateArgs(Args, BoundArch, DeviceOffloadKind);

  const OptTable &Opts = getDriver().getOpts();

  if (!DAL)
    DAL = new DerivedArgList(Args.getBaseArgs());

  for (Arg *A : Args)
    DAL->append(A);

  // Replace -mcpu=native with detected GPU.
  Arg *LastMCPUArg = DAL->getLastArg(options::OPT_mcpu_EQ);
  if (LastMCPUArg && StringRef(LastMCPUArg->getValue()) == "native") {
    DAL->eraseArg(options::OPT_mcpu_EQ);
    auto GPUsOrErr = getSystemGPUArchs(Args);
    if (!GPUsOrErr) {
      getDriver().Diag(diag::err_drv_undetermined_gpu_arch)
          << llvm::Triple::getArchTypeName(getArch())
          << llvm::toString(GPUsOrErr.takeError()) << "-mcpu";
    } else {
      auto &GPUs = *GPUsOrErr;
      if (GPUs.size() > 1) {
        getDriver().Diag(diag::warn_drv_multi_gpu_arch)
            << llvm::Triple::getArchTypeName(getArch())
            << llvm::join(GPUs, ", ") << "-mcpu";
      }
      DAL->AddJoinedArg(nullptr, Opts.getOption(options::OPT_mcpu_EQ),
                        Args.MakeArgString(GPUs.front()));
    }
  }

  checkTargetID(*DAL);

  if (Args.getLastArgValue(options::OPT_x) != "cl")
    return DAL;

  // Phase 1 (.cl -> .bc)
  if (Args.hasArg(options::OPT_c) && Args.hasArg(options::OPT_emit_llvm)) {
    DAL->AddFlagArg(nullptr, Opts.getOption(getTriple().isArch64Bit()
                                                ? options::OPT_m64
                                                : options::OPT_m32));

    // Have to check OPT_O4, OPT_O0 & OPT_Ofast separately
    // as they defined that way in Options.td
    if (!Args.hasArg(options::OPT_O, options::OPT_O0, options::OPT_O4,
                     options::OPT_Ofast))
      DAL->AddJoinedArg(nullptr, Opts.getOption(options::OPT_O),
                        getOptionDefault(options::OPT_O));
  }

  return DAL;
}

bool AMDGPUToolChain::getDefaultDenormsAreZeroForTarget(
    llvm::AMDGPU::GPUKind Kind) {

  // Assume nothing without a specific target.
  if (Kind == llvm::AMDGPU::GK_NONE)
    return false;

  const unsigned ArchAttr = llvm::AMDGPU::getArchAttrAMDGCN(Kind);

  // Default to enabling f32 denormals by default on subtargets where fma is
  // fast with denormals
  const bool BothDenormAndFMAFast =
      (ArchAttr & llvm::AMDGPU::FEATURE_FAST_FMA_F32) &&
      (ArchAttr & llvm::AMDGPU::FEATURE_FAST_DENORMAL_F32);
  return !BothDenormAndFMAFast;
}

llvm::DenormalMode AMDGPUToolChain::getDefaultDenormalModeForType(
    const llvm::opt::ArgList &DriverArgs, const JobAction &JA,
    const llvm::fltSemantics *FPType) const {
  // Denormals should always be enabled for f16 and f64.
  if (!FPType || FPType != &llvm::APFloat::IEEEsingle())
    return llvm::DenormalMode::getIEEE();

  if (JA.getOffloadingDeviceKind() == Action::OFK_HIP ||
      JA.getOffloadingDeviceKind() == Action::OFK_Cuda) {
    auto Arch = getProcessorFromTargetID(getTriple(), JA.getOffloadingArch());
    auto Kind = llvm::AMDGPU::parseArchAMDGCN(Arch);
    if (FPType && FPType == &llvm::APFloat::IEEEsingle() &&
        DriverArgs.hasFlag(options::OPT_fgpu_flush_denormals_to_zero,
                           options::OPT_fno_gpu_flush_denormals_to_zero,
                           getDefaultDenormsAreZeroForTarget(Kind)))
      return llvm::DenormalMode::getPreserveSign();

    return llvm::DenormalMode::getIEEE();
  }

  const StringRef GpuArch = getGPUArch(DriverArgs);
  auto Kind = llvm::AMDGPU::parseArchAMDGCN(GpuArch);

  // TODO: There are way too many flags that change this. Do we need to check
  // them all?
  bool DAZ = DriverArgs.hasArg(options::OPT_cl_denorms_are_zero) ||
             getDefaultDenormsAreZeroForTarget(Kind);

  // Outputs are flushed to zero (FTZ), preserving sign. Denormal inputs are
  // also implicit treated as zero (DAZ).
  return DAZ ? llvm::DenormalMode::getPreserveSign() :
               llvm::DenormalMode::getIEEE();
}

bool AMDGPUToolChain::isWave64(const llvm::opt::ArgList &DriverArgs,
                               llvm::AMDGPU::GPUKind Kind) {
  const unsigned ArchAttr = llvm::AMDGPU::getArchAttrAMDGCN(Kind);
  bool HasWave32 = (ArchAttr & llvm::AMDGPU::FEATURE_WAVE32);

  return !HasWave32 || DriverArgs.hasFlag(
    options::OPT_mwavefrontsize64, options::OPT_mno_wavefrontsize64, false);
}


/// ROCM Toolchain
ROCMToolChain::ROCMToolChain(const Driver &D, const llvm::Triple &Triple,
                             const ArgList &Args)
    : AMDGPUToolChain(D, Triple, Args) {
  RocmInstallation->detectDeviceLibrary();
}

void AMDGPUToolChain::addClangTargetOptions(
    const llvm::opt::ArgList &DriverArgs,
    llvm::opt::ArgStringList &CC1Args,
    Action::OffloadKind DeviceOffloadingKind) const {
  // Default to "hidden" visibility, as object level linking will not be
  // supported for the foreseeable future.
  if (!DriverArgs.hasArg(options::OPT_fvisibility_EQ,
                         options::OPT_fvisibility_ms_compat)) {
    CC1Args.push_back("-fvisibility=hidden");
    CC1Args.push_back("-fapply-global-visibility-to-externs");
  }
}

void AMDGPUToolChain::addClangWarningOptions(ArgStringList &CC1Args) const {
  // AMDGPU does not support atomic lib call. Treat atomic alignment
  // warnings as errors.
  CC1Args.push_back("-Werror=atomic-alignment");
}

StringRef
AMDGPUToolChain::getGPUArch(const llvm::opt::ArgList &DriverArgs) const {
  return getProcessorFromTargetID(
      getTriple(), DriverArgs.getLastArgValue(options::OPT_mcpu_EQ));
}

AMDGPUToolChain::ParsedTargetIDType
AMDGPUToolChain::getParsedTargetID(const llvm::opt::ArgList &DriverArgs) const {
  StringRef TargetID = DriverArgs.getLastArgValue(options::OPT_mcpu_EQ);
  if (TargetID.empty())
    return {std::nullopt, std::nullopt, std::nullopt};

  llvm::StringMap<bool> FeatureMap;
  auto OptionalGpuArch = parseTargetID(getTriple(), TargetID, &FeatureMap);
  if (!OptionalGpuArch)
    return {TargetID.str(), std::nullopt, std::nullopt};

  return {TargetID.str(), OptionalGpuArch->str(), FeatureMap};
}

void AMDGPUToolChain::checkTargetID(
    const llvm::opt::ArgList &DriverArgs) const {
  auto PTID = getParsedTargetID(DriverArgs);
  if (PTID.OptionalTargetID && !PTID.OptionalGPUArch) {
    getDriver().Diag(clang::diag::err_drv_bad_target_id)
        << *PTID.OptionalTargetID;
  }
}

Expected<SmallVector<std::string>>
AMDGPUToolChain::getSystemGPUArchs(const ArgList &Args) const {
  // Detect AMD GPUs availible on the system.
  std::string Program;
  if (Arg *A = Args.getLastArg(options::OPT_amdgpu_arch_tool_EQ))
    Program = A->getValue();
  else
    Program = GetProgramPath("amdgpu-arch");

  auto StdoutOrErr = executeToolChainProgram(Program, /*SecondsToWait=*/10);
  if (!StdoutOrErr)
    return StdoutOrErr.takeError();

  SmallVector<std::string, 1> GPUArchs;
  for (StringRef Arch : llvm::split((*StdoutOrErr)->getBuffer(), "\n"))
    if (!Arch.empty())
      GPUArchs.push_back(Arch.str());

  if (GPUArchs.empty())
    return llvm::createStringError(std::error_code(),
                                   "No AMD GPU detected in the system");

  return std::move(GPUArchs);
}

void ROCMToolChain::addClangTargetOptions(
    const llvm::opt::ArgList &DriverArgs, llvm::opt::ArgStringList &CC1Args,
    Action::OffloadKind DeviceOffloadingKind) const {
  AMDGPUToolChain::addClangTargetOptions(DriverArgs, CC1Args,
                                         DeviceOffloadingKind);

  // For the OpenCL case where there is no offload target, accept -nostdlib to
  // disable bitcode linking.
  if (DeviceOffloadingKind == Action::OFK_None &&
      DriverArgs.hasArg(options::OPT_nostdlib))
    return;

  if (DriverArgs.hasArg(options::OPT_nogpulib))
    return;

  // Get the device name and canonicalize it
  const StringRef GpuArch = getGPUArch(DriverArgs);
  auto Kind = llvm::AMDGPU::parseArchAMDGCN(GpuArch);
  const StringRef CanonArch = llvm::AMDGPU::getArchNameAMDGCN(Kind);
  StringRef LibDeviceFile = RocmInstallation->getLibDeviceFile(CanonArch);
  auto ABIVer = DeviceLibABIVersion::fromCodeObjectVersion(
      getAMDGPUCodeObjectVersion(getDriver(), DriverArgs));
  if (!RocmInstallation->checkCommonBitcodeLibs(CanonArch, LibDeviceFile,
                                                ABIVer))
    return;

  bool Wave64 = isWave64(DriverArgs, Kind);

  // TODO: There are way too many flags that change this. Do we need to check
  // them all?
  bool DAZ = DriverArgs.hasArg(options::OPT_cl_denorms_are_zero) ||
             getDefaultDenormsAreZeroForTarget(Kind);
  bool FiniteOnly = DriverArgs.hasArg(options::OPT_cl_finite_math_only);

  bool UnsafeMathOpt =
      DriverArgs.hasArg(options::OPT_cl_unsafe_math_optimizations);
  bool FastRelaxedMath = DriverArgs.hasArg(options::OPT_cl_fast_relaxed_math);
  bool CorrectSqrt =
      DriverArgs.hasArg(options::OPT_cl_fp32_correctly_rounded_divide_sqrt);

  // Add the OpenCL specific bitcode library.
  llvm::SmallVector<std::string, 12> BCLibs;
  BCLibs.push_back(RocmInstallation->getOpenCLPath().str());

  // Add the generic set of libraries.
  BCLibs.append(RocmInstallation->getCommonBitcodeLibs(
      DriverArgs, LibDeviceFile, Wave64, DAZ, FiniteOnly, UnsafeMathOpt,
      FastRelaxedMath, CorrectSqrt, ABIVer, false));

  if (getSanitizerArgs(DriverArgs).needsAsanRt()) {
    CC1Args.push_back("-mlink-bitcode-file");
    CC1Args.push_back(
        DriverArgs.MakeArgString(RocmInstallation->getAsanRTLPath()));
  }
  for (StringRef BCFile : BCLibs) {
    CC1Args.push_back("-mlink-builtin-bitcode");
    CC1Args.push_back(DriverArgs.MakeArgString(BCFile));
  }
}

bool RocmInstallationDetector::checkCommonBitcodeLibs(
    StringRef GPUArch, StringRef LibDeviceFile,
    DeviceLibABIVersion ABIVer) const {
  if (!hasDeviceLibrary()) {
    D.Diag(diag::err_drv_no_rocm_device_lib) << 0;
    return false;
  }
  if (LibDeviceFile.empty()) {
    D.Diag(diag::err_drv_no_rocm_device_lib) << 1 << GPUArch;
    return false;
  }
  if (ABIVer.requiresLibrary() && getABIVersionPath(ABIVer).empty()) {
    D.Diag(diag::err_drv_no_rocm_device_lib) << 2 << ABIVer.toString();
    return false;
  }
  return true;
}

llvm::SmallVector<std::string, 12>
RocmInstallationDetector::getCommonBitcodeLibs(
    const llvm::opt::ArgList &DriverArgs, StringRef LibDeviceFile, bool Wave64,
    bool DAZ, bool FiniteOnly, bool UnsafeMathOpt, bool FastRelaxedMath,
    bool CorrectSqrt, DeviceLibABIVersion ABIVer, bool isOpenMP = false) const {
  llvm::SmallVector<std::string, 12> BCLibs;

  auto AddBCLib = [&](StringRef BCFile) { BCLibs.push_back(BCFile.str()); };

  AddBCLib(getOCMLPath());
  if (!isOpenMP)
    AddBCLib(getOCKLPath());
  AddBCLib(getDenormalsAreZeroPath(DAZ));
  AddBCLib(getUnsafeMathPath(UnsafeMathOpt || FastRelaxedMath));
  AddBCLib(getFiniteOnlyPath(FiniteOnly || FastRelaxedMath));
  AddBCLib(getCorrectlyRoundedSqrtPath(CorrectSqrt));
  AddBCLib(getWavefrontSize64Path(Wave64));
  AddBCLib(LibDeviceFile);
  auto ABIVerPath = getABIVersionPath(ABIVer);
  if (!ABIVerPath.empty())
    AddBCLib(ABIVerPath);

  return BCLibs;
}

llvm::SmallVector<std::string, 12>
ROCMToolChain::getCommonDeviceLibNames(const llvm::opt::ArgList &DriverArgs,
                                       const std::string &GPUArch,
                                       bool isOpenMP) const {
  auto Kind = llvm::AMDGPU::parseArchAMDGCN(GPUArch);
  const StringRef CanonArch = llvm::AMDGPU::getArchNameAMDGCN(Kind);

  StringRef LibDeviceFile = RocmInstallation->getLibDeviceFile(CanonArch);
  auto ABIVer = DeviceLibABIVersion::fromCodeObjectVersion(
      getAMDGPUCodeObjectVersion(getDriver(), DriverArgs));
  if (!RocmInstallation->checkCommonBitcodeLibs(CanonArch, LibDeviceFile,
                                                ABIVer))
    return {};

  // If --hip-device-lib is not set, add the default bitcode libraries.
  // TODO: There are way too many flags that change this. Do we need to check
  // them all?
  bool DAZ = DriverArgs.hasFlag(options::OPT_fgpu_flush_denormals_to_zero,
                                options::OPT_fno_gpu_flush_denormals_to_zero,
                                getDefaultDenormsAreZeroForTarget(Kind));
  bool FiniteOnly = DriverArgs.hasFlag(
      options::OPT_ffinite_math_only, options::OPT_fno_finite_math_only, false);
  bool UnsafeMathOpt =
      DriverArgs.hasFlag(options::OPT_funsafe_math_optimizations,
                         options::OPT_fno_unsafe_math_optimizations, false);
  bool FastRelaxedMath = DriverArgs.hasFlag(options::OPT_ffast_math,
                                            options::OPT_fno_fast_math, false);
  bool CorrectSqrt = DriverArgs.hasFlag(
      options::OPT_fhip_fp32_correctly_rounded_divide_sqrt,
      options::OPT_fno_hip_fp32_correctly_rounded_divide_sqrt, true);
  bool Wave64 = isWave64(DriverArgs, Kind);

  return RocmInstallation->getCommonBitcodeLibs(
      DriverArgs, LibDeviceFile, Wave64, DAZ, FiniteOnly, UnsafeMathOpt,
      FastRelaxedMath, CorrectSqrt, ABIVer, isOpenMP);
}
