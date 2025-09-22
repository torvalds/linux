//===--- Cuda.cpp - Cuda Tool and ToolChain Implementations -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Cuda.h"
#include "CommonArgs.h"
#include "clang/Basic/Cuda.h"
#include "clang/Config/config.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Distro.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/InputInfo.h"
#include "clang/Driver/Options.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatAdapters.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/TargetParser.h"
#include <system_error>

using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

namespace {

CudaVersion getCudaVersion(uint32_t raw_version) {
  if (raw_version < 7050)
    return CudaVersion::CUDA_70;
  if (raw_version < 8000)
    return CudaVersion::CUDA_75;
  if (raw_version < 9000)
    return CudaVersion::CUDA_80;
  if (raw_version < 9010)
    return CudaVersion::CUDA_90;
  if (raw_version < 9020)
    return CudaVersion::CUDA_91;
  if (raw_version < 10000)
    return CudaVersion::CUDA_92;
  if (raw_version < 10010)
    return CudaVersion::CUDA_100;
  if (raw_version < 10020)
    return CudaVersion::CUDA_101;
  if (raw_version < 11000)
    return CudaVersion::CUDA_102;
  if (raw_version < 11010)
    return CudaVersion::CUDA_110;
  if (raw_version < 11020)
    return CudaVersion::CUDA_111;
  if (raw_version < 11030)
    return CudaVersion::CUDA_112;
  if (raw_version < 11040)
    return CudaVersion::CUDA_113;
  if (raw_version < 11050)
    return CudaVersion::CUDA_114;
  if (raw_version < 11060)
    return CudaVersion::CUDA_115;
  if (raw_version < 11070)
    return CudaVersion::CUDA_116;
  if (raw_version < 11080)
    return CudaVersion::CUDA_117;
  if (raw_version < 11090)
    return CudaVersion::CUDA_118;
  if (raw_version < 12010)
    return CudaVersion::CUDA_120;
  if (raw_version < 12020)
    return CudaVersion::CUDA_121;
  if (raw_version < 12030)
    return CudaVersion::CUDA_122;
  if (raw_version < 12040)
    return CudaVersion::CUDA_123;
  if (raw_version < 12050)
    return CudaVersion::CUDA_124;
  if (raw_version < 12060)
    return CudaVersion::CUDA_125;
  return CudaVersion::NEW;
}

CudaVersion parseCudaHFile(llvm::StringRef Input) {
  // Helper lambda which skips the words if the line starts with them or returns
  // std::nullopt otherwise.
  auto StartsWithWords =
      [](llvm::StringRef Line,
         const SmallVector<StringRef, 3> words) -> std::optional<StringRef> {
    for (StringRef word : words) {
      if (!Line.consume_front(word))
        return {};
      Line = Line.ltrim();
    }
    return Line;
  };

  Input = Input.ltrim();
  while (!Input.empty()) {
    if (auto Line =
            StartsWithWords(Input.ltrim(), {"#", "define", "CUDA_VERSION"})) {
      uint32_t RawVersion;
      Line->consumeInteger(10, RawVersion);
      return getCudaVersion(RawVersion);
    }
    // Find next non-empty line.
    Input = Input.drop_front(Input.find_first_of("\n\r")).ltrim();
  }
  return CudaVersion::UNKNOWN;
}
} // namespace

void CudaInstallationDetector::WarnIfUnsupportedVersion() {
  if (Version > CudaVersion::PARTIALLY_SUPPORTED) {
    std::string VersionString = CudaVersionToString(Version);
    if (!VersionString.empty())
      VersionString.insert(0, " ");
    D.Diag(diag::warn_drv_new_cuda_version)
        << VersionString
        << (CudaVersion::PARTIALLY_SUPPORTED != CudaVersion::FULLY_SUPPORTED)
        << CudaVersionToString(CudaVersion::PARTIALLY_SUPPORTED);
  } else if (Version > CudaVersion::FULLY_SUPPORTED)
    D.Diag(diag::warn_drv_partially_supported_cuda_version)
        << CudaVersionToString(Version);
}

CudaInstallationDetector::CudaInstallationDetector(
    const Driver &D, const llvm::Triple &HostTriple,
    const llvm::opt::ArgList &Args)
    : D(D) {
  struct Candidate {
    std::string Path;
    bool StrictChecking;

    Candidate(std::string Path, bool StrictChecking = false)
        : Path(Path), StrictChecking(StrictChecking) {}
  };
  SmallVector<Candidate, 4> Candidates;

  // In decreasing order so we prefer newer versions to older versions.
  std::initializer_list<const char *> Versions = {"8.0", "7.5", "7.0"};
  auto &FS = D.getVFS();

  if (Args.hasArg(clang::driver::options::OPT_cuda_path_EQ)) {
    Candidates.emplace_back(
        Args.getLastArgValue(clang::driver::options::OPT_cuda_path_EQ).str());
  } else if (HostTriple.isOSWindows()) {
    for (const char *Ver : Versions)
      Candidates.emplace_back(
          D.SysRoot + "/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v" +
          Ver);
  } else {
    if (!Args.hasArg(clang::driver::options::OPT_cuda_path_ignore_env)) {
      // Try to find ptxas binary. If the executable is located in a directory
      // called 'bin/', its parent directory might be a good guess for a valid
      // CUDA installation.
      // However, some distributions might installs 'ptxas' to /usr/bin. In that
      // case the candidate would be '/usr' which passes the following checks
      // because '/usr/include' exists as well. To avoid this case, we always
      // check for the directory potentially containing files for libdevice,
      // even if the user passes -nocudalib.
      if (llvm::ErrorOr<std::string> ptxas =
              llvm::sys::findProgramByName("ptxas")) {
        SmallString<256> ptxasAbsolutePath;
        llvm::sys::fs::real_path(*ptxas, ptxasAbsolutePath);

        StringRef ptxasDir = llvm::sys::path::parent_path(ptxasAbsolutePath);
        if (llvm::sys::path::filename(ptxasDir) == "bin")
          Candidates.emplace_back(
              std::string(llvm::sys::path::parent_path(ptxasDir)),
              /*StrictChecking=*/true);
      }
    }

    Candidates.emplace_back(D.SysRoot + "/usr/local/cuda");
    for (const char *Ver : Versions)
      Candidates.emplace_back(D.SysRoot + "/usr/local/cuda-" + Ver);

    Distro Dist(FS, llvm::Triple(llvm::sys::getProcessTriple()));
    if (Dist.IsDebian() || Dist.IsUbuntu())
      // Special case for Debian to have nvidia-cuda-toolkit work
      // out of the box. More info on http://bugs.debian.org/882505
      Candidates.emplace_back(D.SysRoot + "/usr/lib/cuda");
  }

  bool NoCudaLib = Args.hasArg(options::OPT_nogpulib);

  for (const auto &Candidate : Candidates) {
    InstallPath = Candidate.Path;
    if (InstallPath.empty() || !FS.exists(InstallPath))
      continue;

    BinPath = InstallPath + "/bin";
    IncludePath = InstallPath + "/include";
    LibDevicePath = InstallPath + "/nvvm/libdevice";

    if (!(FS.exists(IncludePath) && FS.exists(BinPath)))
      continue;
    bool CheckLibDevice = (!NoCudaLib || Candidate.StrictChecking);
    if (CheckLibDevice && !FS.exists(LibDevicePath))
      continue;

    Version = CudaVersion::UNKNOWN;
    if (auto CudaHFile = FS.getBufferForFile(InstallPath + "/include/cuda.h"))
      Version = parseCudaHFile((*CudaHFile)->getBuffer());
    // As the last resort, make an educated guess between CUDA-7.0, which had
    // old-style libdevice bitcode, and an unknown recent CUDA version.
    if (Version == CudaVersion::UNKNOWN) {
      Version = FS.exists(LibDevicePath + "/libdevice.10.bc")
                    ? CudaVersion::NEW
                    : CudaVersion::CUDA_70;
    }

    if (Version >= CudaVersion::CUDA_90) {
      // CUDA-9+ uses single libdevice file for all GPU variants.
      std::string FilePath = LibDevicePath + "/libdevice.10.bc";
      if (FS.exists(FilePath)) {
        for (int Arch = (int)OffloadArch::SM_30, E = (int)OffloadArch::LAST;
             Arch < E; ++Arch) {
          OffloadArch OA = static_cast<OffloadArch>(Arch);
          if (!IsNVIDIAOffloadArch(OA))
            continue;
          std::string OffloadArchName(OffloadArchToString(OA));
          LibDeviceMap[OffloadArchName] = FilePath;
        }
      }
    } else {
      std::error_code EC;
      for (llvm::vfs::directory_iterator LI = FS.dir_begin(LibDevicePath, EC),
                                         LE;
           !EC && LI != LE; LI = LI.increment(EC)) {
        StringRef FilePath = LI->path();
        StringRef FileName = llvm::sys::path::filename(FilePath);
        // Process all bitcode filenames that look like
        // libdevice.compute_XX.YY.bc
        const StringRef LibDeviceName = "libdevice.";
        if (!(FileName.starts_with(LibDeviceName) && FileName.ends_with(".bc")))
          continue;
        StringRef GpuArch = FileName.slice(
            LibDeviceName.size(), FileName.find('.', LibDeviceName.size()));
        LibDeviceMap[GpuArch] = FilePath.str();
        // Insert map entries for specific devices with this compute
        // capability. NVCC's choice of the libdevice library version is
        // rather peculiar and depends on the CUDA version.
        if (GpuArch == "compute_20") {
          LibDeviceMap["sm_20"] = std::string(FilePath);
          LibDeviceMap["sm_21"] = std::string(FilePath);
          LibDeviceMap["sm_32"] = std::string(FilePath);
        } else if (GpuArch == "compute_30") {
          LibDeviceMap["sm_30"] = std::string(FilePath);
          if (Version < CudaVersion::CUDA_80) {
            LibDeviceMap["sm_50"] = std::string(FilePath);
            LibDeviceMap["sm_52"] = std::string(FilePath);
            LibDeviceMap["sm_53"] = std::string(FilePath);
          }
          LibDeviceMap["sm_60"] = std::string(FilePath);
          LibDeviceMap["sm_61"] = std::string(FilePath);
          LibDeviceMap["sm_62"] = std::string(FilePath);
        } else if (GpuArch == "compute_35") {
          LibDeviceMap["sm_35"] = std::string(FilePath);
          LibDeviceMap["sm_37"] = std::string(FilePath);
        } else if (GpuArch == "compute_50") {
          if (Version >= CudaVersion::CUDA_80) {
            LibDeviceMap["sm_50"] = std::string(FilePath);
            LibDeviceMap["sm_52"] = std::string(FilePath);
            LibDeviceMap["sm_53"] = std::string(FilePath);
          }
        }
      }
    }

    // Check that we have found at least one libdevice that we can link in if
    // -nocudalib hasn't been specified.
    if (LibDeviceMap.empty() && !NoCudaLib)
      continue;

    IsValid = true;
    break;
  }
}

void CudaInstallationDetector::AddCudaIncludeArgs(
    const ArgList &DriverArgs, ArgStringList &CC1Args) const {
  if (!DriverArgs.hasArg(options::OPT_nobuiltininc)) {
    // Add cuda_wrappers/* to our system include path.  This lets us wrap
    // standard library headers.
    SmallString<128> P(D.ResourceDir);
    llvm::sys::path::append(P, "include");
    llvm::sys::path::append(P, "cuda_wrappers");
    CC1Args.push_back("-internal-isystem");
    CC1Args.push_back(DriverArgs.MakeArgString(P));
  }

  if (DriverArgs.hasArg(options::OPT_nogpuinc))
    return;

  if (!isValid()) {
    D.Diag(diag::err_drv_no_cuda_installation);
    return;
  }

  CC1Args.push_back("-include");
  CC1Args.push_back("__clang_cuda_runtime_wrapper.h");
}

void CudaInstallationDetector::CheckCudaVersionSupportsArch(
    OffloadArch Arch) const {
  if (Arch == OffloadArch::UNKNOWN || Version == CudaVersion::UNKNOWN ||
      ArchsWithBadVersion[(int)Arch])
    return;

  auto MinVersion = MinVersionForOffloadArch(Arch);
  auto MaxVersion = MaxVersionForOffloadArch(Arch);
  if (Version < MinVersion || Version > MaxVersion) {
    ArchsWithBadVersion[(int)Arch] = true;
    D.Diag(diag::err_drv_cuda_version_unsupported)
        << OffloadArchToString(Arch) << CudaVersionToString(MinVersion)
        << CudaVersionToString(MaxVersion) << InstallPath
        << CudaVersionToString(Version);
  }
}

void CudaInstallationDetector::print(raw_ostream &OS) const {
  if (isValid())
    OS << "Found CUDA installation: " << InstallPath << ", version "
       << CudaVersionToString(Version) << "\n";
}

namespace {
/// Debug info level for the NVPTX devices. We may need to emit different debug
/// info level for the host and for the device itselfi. This type controls
/// emission of the debug info for the devices. It either prohibits disable info
/// emission completely, or emits debug directives only, or emits same debug
/// info as for the host.
enum DeviceDebugInfoLevel {
  DisableDebugInfo,        /// Do not emit debug info for the devices.
  DebugDirectivesOnly,     /// Emit only debug directives.
  EmitSameDebugInfoAsHost, /// Use the same debug info level just like for the
                           /// host.
};
} // anonymous namespace

/// Define debug info level for the NVPTX devices. If the debug info for both
/// the host and device are disabled (-g0/-ggdb0 or no debug options at all). If
/// only debug directives are requested for the both host and device
/// (-gline-directvies-only), or the debug info only for the device is disabled
/// (optimization is on and --cuda-noopt-device-debug was not specified), the
/// debug directves only must be emitted for the device. Otherwise, use the same
/// debug info level just like for the host (with the limitations of only
/// supported DWARF2 standard).
static DeviceDebugInfoLevel mustEmitDebugInfo(const ArgList &Args) {
  const Arg *A = Args.getLastArg(options::OPT_O_Group);
  bool IsDebugEnabled = !A || A->getOption().matches(options::OPT_O0) ||
                        Args.hasFlag(options::OPT_cuda_noopt_device_debug,
                                     options::OPT_no_cuda_noopt_device_debug,
                                     /*Default=*/false);
  if (const Arg *A = Args.getLastArg(options::OPT_g_Group)) {
    const Option &Opt = A->getOption();
    if (Opt.matches(options::OPT_gN_Group)) {
      if (Opt.matches(options::OPT_g0) || Opt.matches(options::OPT_ggdb0))
        return DisableDebugInfo;
      if (Opt.matches(options::OPT_gline_directives_only))
        return DebugDirectivesOnly;
    }
    return IsDebugEnabled ? EmitSameDebugInfoAsHost : DebugDirectivesOnly;
  }
  return willEmitRemarks(Args) ? DebugDirectivesOnly : DisableDebugInfo;
}

void NVPTX::Assembler::ConstructJob(Compilation &C, const JobAction &JA,
                                    const InputInfo &Output,
                                    const InputInfoList &Inputs,
                                    const ArgList &Args,
                                    const char *LinkingOutput) const {
  const auto &TC =
      static_cast<const toolchains::NVPTXToolChain &>(getToolChain());
  assert(TC.getTriple().isNVPTX() && "Wrong platform");

  StringRef GPUArchName;
  // If this is a CUDA action we need to extract the device architecture
  // from the Job's associated architecture, otherwise use the -march=arch
  // option. This option may come from -Xopenmp-target flag or the default
  // value.
  if (JA.isDeviceOffloading(Action::OFK_Cuda)) {
    GPUArchName = JA.getOffloadingArch();
  } else {
    GPUArchName = Args.getLastArgValue(options::OPT_march_EQ);
    if (GPUArchName.empty()) {
      C.getDriver().Diag(diag::err_drv_offload_missing_gpu_arch)
          << getToolChain().getArchName() << getShortName();
      return;
    }
  }

  // Obtain architecture from the action.
  OffloadArch gpu_arch = StringToOffloadArch(GPUArchName);
  assert(gpu_arch != OffloadArch::UNKNOWN &&
         "Device action expected to have an architecture.");

  // Check that our installation's ptxas supports gpu_arch.
  if (!Args.hasArg(options::OPT_no_cuda_version_check)) {
    TC.CudaInstallation.CheckCudaVersionSupportsArch(gpu_arch);
  }

  ArgStringList CmdArgs;
  CmdArgs.push_back(TC.getTriple().isArch64Bit() ? "-m64" : "-m32");
  DeviceDebugInfoLevel DIKind = mustEmitDebugInfo(Args);
  if (DIKind == EmitSameDebugInfoAsHost) {
    // ptxas does not accept -g option if optimization is enabled, so
    // we ignore the compiler's -O* options if we want debug info.
    CmdArgs.push_back("-g");
    CmdArgs.push_back("--dont-merge-basicblocks");
    CmdArgs.push_back("--return-at-end");
  } else if (Arg *A = Args.getLastArg(options::OPT_O_Group)) {
    // Map the -O we received to -O{0,1,2,3}.
    //
    // TODO: Perhaps we should map host -O2 to ptxas -O3. -O3 is ptxas's
    // default, so it may correspond more closely to the spirit of clang -O2.

    // -O3 seems like the least-bad option when -Osomething is specified to
    // clang but it isn't handled below.
    StringRef OOpt = "3";
    if (A->getOption().matches(options::OPT_O4) ||
        A->getOption().matches(options::OPT_Ofast))
      OOpt = "3";
    else if (A->getOption().matches(options::OPT_O0))
      OOpt = "0";
    else if (A->getOption().matches(options::OPT_O)) {
      // -Os, -Oz, and -O(anything else) map to -O2, for lack of better options.
      OOpt = llvm::StringSwitch<const char *>(A->getValue())
                 .Case("1", "1")
                 .Case("2", "2")
                 .Case("3", "3")
                 .Case("s", "2")
                 .Case("z", "2")
                 .Default("2");
    }
    CmdArgs.push_back(Args.MakeArgString(llvm::Twine("-O") + OOpt));
  } else {
    // If no -O was passed, pass -O0 to ptxas -- no opt flag should correspond
    // to no optimizations, but ptxas's default is -O3.
    CmdArgs.push_back("-O0");
  }
  if (DIKind == DebugDirectivesOnly)
    CmdArgs.push_back("-lineinfo");

  // Pass -v to ptxas if it was passed to the driver.
  if (Args.hasArg(options::OPT_v))
    CmdArgs.push_back("-v");

  CmdArgs.push_back("--gpu-name");
  CmdArgs.push_back(Args.MakeArgString(OffloadArchToString(gpu_arch)));
  CmdArgs.push_back("--output-file");
  std::string OutputFileName = TC.getInputFilename(Output);

  if (Output.isFilename() && OutputFileName != Output.getFilename())
    C.addTempFile(Args.MakeArgString(OutputFileName));

  CmdArgs.push_back(Args.MakeArgString(OutputFileName));
  for (const auto &II : Inputs)
    CmdArgs.push_back(Args.MakeArgString(II.getFilename()));

  for (const auto &A : Args.getAllArgValues(options::OPT_Xcuda_ptxas))
    CmdArgs.push_back(Args.MakeArgString(A));

  bool Relocatable;
  if (JA.isOffloading(Action::OFK_OpenMP))
    // In OpenMP we need to generate relocatable code.
    Relocatable = Args.hasFlag(options::OPT_fopenmp_relocatable_target,
                               options::OPT_fnoopenmp_relocatable_target,
                               /*Default=*/true);
  else if (JA.isOffloading(Action::OFK_Cuda))
    // In CUDA we generate relocatable code by default.
    Relocatable = Args.hasFlag(options::OPT_fgpu_rdc, options::OPT_fno_gpu_rdc,
                               /*Default=*/false);
  else
    // Otherwise, we are compiling directly and should create linkable output.
    Relocatable = true;

  if (Relocatable)
    CmdArgs.push_back("-c");

  const char *Exec;
  if (Arg *A = Args.getLastArg(options::OPT_ptxas_path_EQ))
    Exec = A->getValue();
  else
    Exec = Args.MakeArgString(TC.GetProgramPath("ptxas"));
  C.addCommand(std::make_unique<Command>(
      JA, *this,
      ResponseFileSupport{ResponseFileSupport::RF_Full, llvm::sys::WEM_UTF8,
                          "--options-file"},
      Exec, CmdArgs, Inputs, Output));
}

static bool shouldIncludePTX(const ArgList &Args, StringRef InputArch) {
  // The new driver does not include PTX by default to avoid overhead.
  bool includePTX = !Args.hasFlag(options::OPT_offload_new_driver,
                                  options::OPT_no_offload_new_driver, false);
  for (Arg *A : Args.filtered(options::OPT_cuda_include_ptx_EQ,
                              options::OPT_no_cuda_include_ptx_EQ)) {
    A->claim();
    const StringRef ArchStr = A->getValue();
    if (A->getOption().matches(options::OPT_cuda_include_ptx_EQ) &&
        (ArchStr == "all" || ArchStr == InputArch))
      includePTX = true;
    else if (A->getOption().matches(options::OPT_no_cuda_include_ptx_EQ) &&
             (ArchStr == "all" || ArchStr == InputArch))
      includePTX = false;
  }
  return includePTX;
}

// All inputs to this linker must be from CudaDeviceActions, as we need to look
// at the Inputs' Actions in order to figure out which GPU architecture they
// correspond to.
void NVPTX::FatBinary::ConstructJob(Compilation &C, const JobAction &JA,
                                    const InputInfo &Output,
                                    const InputInfoList &Inputs,
                                    const ArgList &Args,
                                    const char *LinkingOutput) const {
  const auto &TC =
      static_cast<const toolchains::CudaToolChain &>(getToolChain());
  assert(TC.getTriple().isNVPTX() && "Wrong platform");

  ArgStringList CmdArgs;
  if (TC.CudaInstallation.version() <= CudaVersion::CUDA_100)
    CmdArgs.push_back("--cuda");
  CmdArgs.push_back(TC.getTriple().isArch64Bit() ? "-64" : "-32");
  CmdArgs.push_back(Args.MakeArgString("--create"));
  CmdArgs.push_back(Args.MakeArgString(Output.getFilename()));
  if (mustEmitDebugInfo(Args) == EmitSameDebugInfoAsHost)
    CmdArgs.push_back("-g");

  for (const auto &II : Inputs) {
    auto *A = II.getAction();
    assert(A->getInputs().size() == 1 &&
           "Device offload action is expected to have a single input");
    const char *gpu_arch_str = A->getOffloadingArch();
    assert(gpu_arch_str &&
           "Device action expected to have associated a GPU architecture!");
    OffloadArch gpu_arch = StringToOffloadArch(gpu_arch_str);

    if (II.getType() == types::TY_PP_Asm &&
        !shouldIncludePTX(Args, gpu_arch_str))
      continue;
    // We need to pass an Arch of the form "sm_XX" for cubin files and
    // "compute_XX" for ptx.
    const char *Arch = (II.getType() == types::TY_PP_Asm)
                           ? OffloadArchToVirtualArchString(gpu_arch)
                           : gpu_arch_str;
    CmdArgs.push_back(
        Args.MakeArgString(llvm::Twine("--image=profile=") + Arch +
                           ",file=" + getToolChain().getInputFilename(II)));
  }

  for (const auto &A : Args.getAllArgValues(options::OPT_Xcuda_fatbinary))
    CmdArgs.push_back(Args.MakeArgString(A));

  const char *Exec = Args.MakeArgString(TC.GetProgramPath("fatbinary"));
  C.addCommand(std::make_unique<Command>(
      JA, *this,
      ResponseFileSupport{ResponseFileSupport::RF_Full, llvm::sys::WEM_UTF8,
                          "--options-file"},
      Exec, CmdArgs, Inputs, Output));
}

void NVPTX::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                 const InputInfo &Output,
                                 const InputInfoList &Inputs,
                                 const ArgList &Args,
                                 const char *LinkingOutput) const {
  const auto &TC =
      static_cast<const toolchains::NVPTXToolChain &>(getToolChain());
  ArgStringList CmdArgs;

  assert(TC.getTriple().isNVPTX() && "Wrong platform");

  assert((Output.isFilename() || Output.isNothing()) && "Invalid output.");
  if (Output.isFilename()) {
    CmdArgs.push_back("-o");
    CmdArgs.push_back(Output.getFilename());
  }

  if (mustEmitDebugInfo(Args) == EmitSameDebugInfoAsHost)
    CmdArgs.push_back("-g");

  if (Args.hasArg(options::OPT_v))
    CmdArgs.push_back("-v");

  StringRef GPUArch = Args.getLastArgValue(options::OPT_march_EQ);
  if (GPUArch.empty()) {
    C.getDriver().Diag(diag::err_drv_offload_missing_gpu_arch)
        << getToolChain().getArchName() << getShortName();
    return;
  }

  CmdArgs.push_back("-arch");
  CmdArgs.push_back(Args.MakeArgString(GPUArch));

  if (Args.hasArg(options::OPT_ptxas_path_EQ))
    CmdArgs.push_back(Args.MakeArgString(
        "--pxtas-path=" + Args.getLastArgValue(options::OPT_ptxas_path_EQ)));

  if (Args.hasArg(options::OPT_cuda_path_EQ))
    CmdArgs.push_back(Args.MakeArgString(
        "--cuda-path=" + Args.getLastArgValue(options::OPT_cuda_path_EQ)));

  // Add paths specified in LIBRARY_PATH environment variable as -L options.
  addDirectoryList(Args, CmdArgs, "-L", "LIBRARY_PATH");

  // Add standard library search paths passed on the command line.
  Args.AddAllArgs(CmdArgs, options::OPT_L);
  getToolChain().AddFilePathLibArgs(Args, CmdArgs);
  AddLinkerInputs(getToolChain(), Inputs, Args, CmdArgs, JA);

  if (C.getDriver().isUsingLTO())
    addLTOOptions(getToolChain(), Args, CmdArgs, Output, Inputs[0],
                  C.getDriver().getLTOMode() == LTOK_Thin);

  // Add paths for the default clang library path.
  SmallString<256> DefaultLibPath =
      llvm::sys::path::parent_path(TC.getDriver().Dir);
  llvm::sys::path::append(DefaultLibPath, CLANG_INSTALL_LIBDIR_BASENAME);
  CmdArgs.push_back(Args.MakeArgString(Twine("-L") + DefaultLibPath));

  C.addCommand(std::make_unique<Command>(
      JA, *this,
      ResponseFileSupport{ResponseFileSupport::RF_Full, llvm::sys::WEM_UTF8,
                          "--options-file"},
      Args.MakeArgString(getToolChain().GetProgramPath("clang-nvlink-wrapper")),
      CmdArgs, Inputs, Output));
}

void NVPTX::getNVPTXTargetFeatures(const Driver &D, const llvm::Triple &Triple,
                                   const llvm::opt::ArgList &Args,
                                   std::vector<StringRef> &Features) {
  if (Args.hasArg(options::OPT_cuda_feature_EQ)) {
    StringRef PtxFeature =
        Args.getLastArgValue(options::OPT_cuda_feature_EQ, "+ptx42");
    Features.push_back(Args.MakeArgString(PtxFeature));
    return;
  }
  CudaInstallationDetector CudaInstallation(D, Triple, Args);

  // New CUDA versions often introduce new instructions that are only supported
  // by new PTX version, so we need to raise PTX level to enable them in NVPTX
  // back-end.
  const char *PtxFeature = nullptr;
  switch (CudaInstallation.version()) {
#define CASE_CUDA_VERSION(CUDA_VER, PTX_VER)                                   \
  case CudaVersion::CUDA_##CUDA_VER:                                           \
    PtxFeature = "+ptx" #PTX_VER;                                              \
    break;
    CASE_CUDA_VERSION(125, 85);
    CASE_CUDA_VERSION(124, 84);
    CASE_CUDA_VERSION(123, 83);
    CASE_CUDA_VERSION(122, 82);
    CASE_CUDA_VERSION(121, 81);
    CASE_CUDA_VERSION(120, 80);
    CASE_CUDA_VERSION(118, 78);
    CASE_CUDA_VERSION(117, 77);
    CASE_CUDA_VERSION(116, 76);
    CASE_CUDA_VERSION(115, 75);
    CASE_CUDA_VERSION(114, 74);
    CASE_CUDA_VERSION(113, 73);
    CASE_CUDA_VERSION(112, 72);
    CASE_CUDA_VERSION(111, 71);
    CASE_CUDA_VERSION(110, 70);
    CASE_CUDA_VERSION(102, 65);
    CASE_CUDA_VERSION(101, 64);
    CASE_CUDA_VERSION(100, 63);
    CASE_CUDA_VERSION(92, 61);
    CASE_CUDA_VERSION(91, 61);
    CASE_CUDA_VERSION(90, 60);
#undef CASE_CUDA_VERSION
  default:
    PtxFeature = "+ptx42";
  }
  Features.push_back(PtxFeature);
}

/// NVPTX toolchain. Our assembler is ptxas, and our linker is nvlink. This
/// operates as a stand-alone version of the NVPTX tools without the host
/// toolchain.
NVPTXToolChain::NVPTXToolChain(const Driver &D, const llvm::Triple &Triple,
                               const llvm::Triple &HostTriple,
                               const ArgList &Args, bool Freestanding = false)
    : ToolChain(D, Triple, Args), CudaInstallation(D, HostTriple, Args),
      Freestanding(Freestanding) {
  if (CudaInstallation.isValid())
    getProgramPaths().push_back(std::string(CudaInstallation.getBinPath()));
  // Lookup binaries into the driver directory, this is used to
  // discover the 'nvptx-arch' executable.
  getProgramPaths().push_back(getDriver().Dir);
}

/// We only need the host triple to locate the CUDA binary utilities, use the
/// system's default triple if not provided.
NVPTXToolChain::NVPTXToolChain(const Driver &D, const llvm::Triple &Triple,
                               const ArgList &Args)
    : NVPTXToolChain(D, Triple, llvm::Triple(LLVM_HOST_TRIPLE), Args,
                     /*Freestanding=*/true) {}

llvm::opt::DerivedArgList *
NVPTXToolChain::TranslateArgs(const llvm::opt::DerivedArgList &Args,
                              StringRef BoundArch,
                              Action::OffloadKind OffloadKind) const {
  DerivedArgList *DAL = ToolChain::TranslateArgs(Args, BoundArch, OffloadKind);
  if (!DAL)
    DAL = new DerivedArgList(Args.getBaseArgs());

  const OptTable &Opts = getDriver().getOpts();

  for (Arg *A : Args)
    if (!llvm::is_contained(*DAL, A))
      DAL->append(A);

  if (!DAL->hasArg(options::OPT_march_EQ) && OffloadKind != Action::OFK_None) {
    DAL->AddJoinedArg(nullptr, Opts.getOption(options::OPT_march_EQ),
                      OffloadArchToString(OffloadArch::CudaDefault));
  } else if (DAL->getLastArgValue(options::OPT_march_EQ) == "generic" &&
             OffloadKind == Action::OFK_None) {
    DAL->eraseArg(options::OPT_march_EQ);
  } else if (DAL->getLastArgValue(options::OPT_march_EQ) == "native") {
    auto GPUsOrErr = getSystemGPUArchs(Args);
    if (!GPUsOrErr) {
      getDriver().Diag(diag::err_drv_undetermined_gpu_arch)
          << getArchName() << llvm::toString(GPUsOrErr.takeError()) << "-march";
    } else {
      if (GPUsOrErr->size() > 1)
        getDriver().Diag(diag::warn_drv_multi_gpu_arch)
            << getArchName() << llvm::join(*GPUsOrErr, ", ") << "-march";
      DAL->AddJoinedArg(nullptr, Opts.getOption(options::OPT_march_EQ),
                        Args.MakeArgString(GPUsOrErr->front()));
    }
  }

  return DAL;
}

void NVPTXToolChain::addClangTargetOptions(
    const llvm::opt::ArgList &DriverArgs, llvm::opt::ArgStringList &CC1Args,
    Action::OffloadKind DeviceOffloadingKind) const {
  // If we are compiling with a standalone NVPTX toolchain we want to try to
  // mimic a standard environment as much as possible. So we enable lowering
  // ctor / dtor functions to global symbols that can be registered.
  if (Freestanding)
    CC1Args.append({"-mllvm", "--nvptx-lower-global-ctor-dtor"});
}

bool NVPTXToolChain::supportsDebugInfoOption(const llvm::opt::Arg *A) const {
  const Option &O = A->getOption();
  return (O.matches(options::OPT_gN_Group) &&
          !O.matches(options::OPT_gmodules)) ||
         O.matches(options::OPT_g_Flag) ||
         O.matches(options::OPT_ggdbN_Group) || O.matches(options::OPT_ggdb) ||
         O.matches(options::OPT_gdwarf) || O.matches(options::OPT_gdwarf_2) ||
         O.matches(options::OPT_gdwarf_3) || O.matches(options::OPT_gdwarf_4) ||
         O.matches(options::OPT_gdwarf_5) ||
         O.matches(options::OPT_gcolumn_info);
}

void NVPTXToolChain::adjustDebugInfoKind(
    llvm::codegenoptions::DebugInfoKind &DebugInfoKind,
    const ArgList &Args) const {
  switch (mustEmitDebugInfo(Args)) {
  case DisableDebugInfo:
    DebugInfoKind = llvm::codegenoptions::NoDebugInfo;
    break;
  case DebugDirectivesOnly:
    DebugInfoKind = llvm::codegenoptions::DebugDirectivesOnly;
    break;
  case EmitSameDebugInfoAsHost:
    // Use same debug info level as the host.
    break;
  }
}

Expected<SmallVector<std::string>>
NVPTXToolChain::getSystemGPUArchs(const ArgList &Args) const {
  // Detect NVIDIA GPUs availible on the system.
  std::string Program;
  if (Arg *A = Args.getLastArg(options::OPT_nvptx_arch_tool_EQ))
    Program = A->getValue();
  else
    Program = GetProgramPath("nvptx-arch");

  auto StdoutOrErr = executeToolChainProgram(Program, /*SecondsToWait=*/10);
  if (!StdoutOrErr)
    return StdoutOrErr.takeError();

  SmallVector<std::string, 1> GPUArchs;
  for (StringRef Arch : llvm::split((*StdoutOrErr)->getBuffer(), "\n"))
    if (!Arch.empty())
      GPUArchs.push_back(Arch.str());

  if (GPUArchs.empty())
    return llvm::createStringError(std::error_code(),
                                   "No NVIDIA GPU detected in the system");

  return std::move(GPUArchs);
}

/// CUDA toolchain.  Our assembler is ptxas, and our "linker" is fatbinary,
/// which isn't properly a linker but nonetheless performs the step of stitching
/// together object files from the assembler into a single blob.

CudaToolChain::CudaToolChain(const Driver &D, const llvm::Triple &Triple,
                             const ToolChain &HostTC, const ArgList &Args)
    : NVPTXToolChain(D, Triple, HostTC.getTriple(), Args), HostTC(HostTC) {}

void CudaToolChain::addClangTargetOptions(
    const llvm::opt::ArgList &DriverArgs, llvm::opt::ArgStringList &CC1Args,
    Action::OffloadKind DeviceOffloadingKind) const {
  HostTC.addClangTargetOptions(DriverArgs, CC1Args, DeviceOffloadingKind);

  StringRef GpuArch = DriverArgs.getLastArgValue(options::OPT_march_EQ);
  assert(!GpuArch.empty() && "Must have an explicit GPU arch.");
  assert((DeviceOffloadingKind == Action::OFK_OpenMP ||
          DeviceOffloadingKind == Action::OFK_Cuda) &&
         "Only OpenMP or CUDA offloading kinds are supported for NVIDIA GPUs.");

  if (DeviceOffloadingKind == Action::OFK_Cuda) {
    CC1Args.append(
        {"-fcuda-is-device", "-mllvm", "-enable-memcpyopt-without-libcalls"});

    // Unsized function arguments used for variadics were introduced in CUDA-9.0
    // We still do not support generating code that actually uses variadic
    // arguments yet, but we do need to allow parsing them as recent CUDA
    // headers rely on that. https://github.com/llvm/llvm-project/issues/58410
    if (CudaInstallation.version() >= CudaVersion::CUDA_90)
      CC1Args.push_back("-fcuda-allow-variadic-functions");
  }

  if (DriverArgs.hasArg(options::OPT_nogpulib))
    return;

  if (DeviceOffloadingKind == Action::OFK_OpenMP &&
      DriverArgs.hasArg(options::OPT_S))
    return;

  std::string LibDeviceFile = CudaInstallation.getLibDeviceFile(GpuArch);
  if (LibDeviceFile.empty()) {
    getDriver().Diag(diag::err_drv_no_cuda_libdevice) << GpuArch;
    return;
  }

  CC1Args.push_back("-mlink-builtin-bitcode");
  CC1Args.push_back(DriverArgs.MakeArgString(LibDeviceFile));

  clang::CudaVersion CudaInstallationVersion = CudaInstallation.version();

  if (DriverArgs.hasFlag(options::OPT_fcuda_short_ptr,
                         options::OPT_fno_cuda_short_ptr, false))
    CC1Args.append({"-mllvm", "--nvptx-short-ptr"});

  if (CudaInstallationVersion >= CudaVersion::UNKNOWN)
    CC1Args.push_back(
        DriverArgs.MakeArgString(Twine("-target-sdk-version=") +
                                 CudaVersionToString(CudaInstallationVersion)));

  if (DeviceOffloadingKind == Action::OFK_OpenMP) {
    if (CudaInstallationVersion < CudaVersion::CUDA_92) {
      getDriver().Diag(
          diag::err_drv_omp_offload_target_cuda_version_not_support)
          << CudaVersionToString(CudaInstallationVersion);
      return;
    }

    // Link the bitcode library late if we're using device LTO.
    if (getDriver().isUsingLTO(/* IsOffload */ true))
      return;

    addOpenMPDeviceRTL(getDriver(), DriverArgs, CC1Args, GpuArch.str(),
                       getTriple(), HostTC);
  }
}

llvm::DenormalMode CudaToolChain::getDefaultDenormalModeForType(
    const llvm::opt::ArgList &DriverArgs, const JobAction &JA,
    const llvm::fltSemantics *FPType) const {
  if (JA.getOffloadingDeviceKind() == Action::OFK_Cuda) {
    if (FPType && FPType == &llvm::APFloat::IEEEsingle() &&
        DriverArgs.hasFlag(options::OPT_fgpu_flush_denormals_to_zero,
                           options::OPT_fno_gpu_flush_denormals_to_zero, false))
      return llvm::DenormalMode::getPreserveSign();
  }

  assert(JA.getOffloadingDeviceKind() != Action::OFK_Host);
  return llvm::DenormalMode::getIEEE();
}

void CudaToolChain::AddCudaIncludeArgs(const ArgList &DriverArgs,
                                       ArgStringList &CC1Args) const {
  // Check our CUDA version if we're going to include the CUDA headers.
  if (!DriverArgs.hasArg(options::OPT_nogpuinc) &&
      !DriverArgs.hasArg(options::OPT_no_cuda_version_check)) {
    StringRef Arch = DriverArgs.getLastArgValue(options::OPT_march_EQ);
    assert(!Arch.empty() && "Must have an explicit GPU arch.");
    CudaInstallation.CheckCudaVersionSupportsArch(StringToOffloadArch(Arch));
  }
  CudaInstallation.AddCudaIncludeArgs(DriverArgs, CC1Args);
}

std::string CudaToolChain::getInputFilename(const InputInfo &Input) const {
  // Only object files are changed, for example assembly files keep their .s
  // extensions. If the user requested device-only compilation don't change it.
  if (Input.getType() != types::TY_Object || getDriver().offloadDeviceOnly())
    return ToolChain::getInputFilename(Input);

  return ToolChain::getInputFilename(Input);
}

llvm::opt::DerivedArgList *
CudaToolChain::TranslateArgs(const llvm::opt::DerivedArgList &Args,
                             StringRef BoundArch,
                             Action::OffloadKind DeviceOffloadKind) const {
  DerivedArgList *DAL =
      HostTC.TranslateArgs(Args, BoundArch, DeviceOffloadKind);
  if (!DAL)
    DAL = new DerivedArgList(Args.getBaseArgs());

  const OptTable &Opts = getDriver().getOpts();

  // For OpenMP device offloading, append derived arguments. Make sure
  // flags are not duplicated.
  // Also append the compute capability.
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
          Arch = OffloadArchToString(OffloadArch::CudaDefault);
        } else {
          Arch = Args.MakeArgString(ArchsOrErr->front());
        }
      }
      DAL->AddJoinedArg(nullptr, Opts.getOption(options::OPT_march_EQ), Arch);
    }

    return DAL;
  }

  for (Arg *A : Args) {
    // Make sure flags are not duplicated.
    if (!llvm::is_contained(*DAL, A)) {
      DAL->append(A);
    }
  }

  if (!BoundArch.empty()) {
    DAL->eraseArg(options::OPT_march_EQ);
    DAL->AddJoinedArg(nullptr, Opts.getOption(options::OPT_march_EQ),
                      BoundArch);
  }
  return DAL;
}

Tool *NVPTXToolChain::buildAssembler() const {
  return new tools::NVPTX::Assembler(*this);
}

Tool *NVPTXToolChain::buildLinker() const {
  return new tools::NVPTX::Linker(*this);
}

Tool *CudaToolChain::buildAssembler() const {
  return new tools::NVPTX::Assembler(*this);
}

Tool *CudaToolChain::buildLinker() const {
  return new tools::NVPTX::FatBinary(*this);
}

void CudaToolChain::addClangWarningOptions(ArgStringList &CC1Args) const {
  HostTC.addClangWarningOptions(CC1Args);
}

ToolChain::CXXStdlibType
CudaToolChain::GetCXXStdlibType(const ArgList &Args) const {
  return HostTC.GetCXXStdlibType(Args);
}

void CudaToolChain::AddClangSystemIncludeArgs(const ArgList &DriverArgs,
                                              ArgStringList &CC1Args) const {
  HostTC.AddClangSystemIncludeArgs(DriverArgs, CC1Args);

  if (!DriverArgs.hasArg(options::OPT_nogpuinc) && CudaInstallation.isValid())
    CC1Args.append(
        {"-internal-isystem",
         DriverArgs.MakeArgString(CudaInstallation.getIncludePath())});
}

void CudaToolChain::AddClangCXXStdlibIncludeArgs(const ArgList &Args,
                                                 ArgStringList &CC1Args) const {
  HostTC.AddClangCXXStdlibIncludeArgs(Args, CC1Args);
}

void CudaToolChain::AddIAMCUIncludeArgs(const ArgList &Args,
                                        ArgStringList &CC1Args) const {
  HostTC.AddIAMCUIncludeArgs(Args, CC1Args);
}

SanitizerMask CudaToolChain::getSupportedSanitizers() const {
  // The CudaToolChain only supports sanitizers in the sense that it allows
  // sanitizer arguments on the command line if they are supported by the host
  // toolchain. The CudaToolChain will actually ignore any command line
  // arguments for any of these "supported" sanitizers. That means that no
  // sanitization of device code is actually supported at this time.
  //
  // This behavior is necessary because the host and device toolchains
  // invocations often share the command line, so the device toolchain must
  // tolerate flags meant only for the host toolchain.
  return HostTC.getSupportedSanitizers();
}

VersionTuple CudaToolChain::computeMSVCVersion(const Driver *D,
                                               const ArgList &Args) const {
  return HostTC.computeMSVCVersion(D, Args);
}
