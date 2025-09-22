//===-- MSVCPaths.h - MSVC path-parsing helpers -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_WINDOWSDRIVER_MSVCPATHS_H
#define LLVM_WINDOWSDRIVER_MSVCPATHS_H

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/TargetParser/Triple.h"
#include <optional>
#include <string>

namespace llvm {

namespace vfs {
class FileSystem;
}

enum class SubDirectoryType {
  Bin,
  Include,
  Lib,
};

enum class ToolsetLayout {
  OlderVS,
  VS2017OrNewer,
  DevDivInternal,
};

// Windows SDKs and VC Toolchains group their contents into subdirectories based
// on the target architecture. This function converts an llvm::Triple::ArchType
// to the corresponding subdirectory name.
const char *archToWindowsSDKArch(llvm::Triple::ArchType Arch);

// Similar to the above function, but for Visual Studios before VS2017.
const char *archToLegacyVCArch(llvm::Triple::ArchType Arch);

// Similar to the above function, but for DevDiv internal builds.
const char *archToDevDivInternalArch(llvm::Triple::ArchType Arch);

bool appendArchToWindowsSDKLibPath(int SDKMajor, llvm::SmallString<128> LibPath,
                                   llvm::Triple::ArchType Arch,
                                   std::string &path);

// Get the path to a specific subdirectory in the current toolchain for
// a given target architecture.
// VS2017 changed the VC toolchain layout, so this should be used instead
// of hardcoding paths.
std::string getSubDirectoryPath(SubDirectoryType Type, ToolsetLayout VSLayout,
                                const std::string &VCToolChainPath,
                                llvm::Triple::ArchType TargetArch,
                                llvm::StringRef SubdirParent = "");

// Check if the Include path of a specified version of Visual Studio contains
// specific header files. If not, they are probably shipped with Universal CRT.
bool useUniversalCRT(ToolsetLayout VSLayout, const std::string &VCToolChainPath,
                     llvm::Triple::ArchType TargetArch,
                     llvm::vfs::FileSystem &VFS);

/// Get Windows SDK installation directory.
bool getWindowsSDKDir(vfs::FileSystem &VFS,
                      std::optional<llvm::StringRef> WinSdkDir,
                      std::optional<llvm::StringRef> WinSdkVersion,
                      std::optional<llvm::StringRef> WinSysRoot,
                      std::string &Path, int &Major,
                      std::string &WindowsSDKIncludeVersion,
                      std::string &WindowsSDKLibVersion);

bool getUniversalCRTSdkDir(vfs::FileSystem &VFS,
                           std::optional<llvm::StringRef> WinSdkDir,
                           std::optional<llvm::StringRef> WinSdkVersion,
                           std::optional<llvm::StringRef> WinSysRoot,
                           std::string &Path, std::string &UCRTVersion);

// Check command line arguments to try and find a toolchain.
bool findVCToolChainViaCommandLine(
    vfs::FileSystem &VFS, std::optional<llvm::StringRef> VCToolsDir,
    std::optional<llvm::StringRef> VCToolsVersion,
    std::optional<llvm::StringRef> WinSysRoot, std::string &Path,
    ToolsetLayout &VSLayout);

// Check various environment variables to try and find a toolchain.
bool findVCToolChainViaEnvironment(vfs::FileSystem &VFS, std::string &Path,
                                   ToolsetLayout &VSLayout);

// Query the Setup Config server for installs, then pick the newest version
// and find its default VC toolchain. If `VCToolsVersion` is specified, that
// version is preferred over the latest version.
//
// This is the preferred way to discover new Visual Studios, as they're no
// longer listed in the registry.
bool
findVCToolChainViaSetupConfig(vfs::FileSystem &VFS,
                              std::optional<llvm::StringRef> VCToolsVersion,
                              std::string &Path, ToolsetLayout &VSLayout);

// Look in the registry for Visual Studio installs, and use that to get
// a toolchain path. VS2017 and newer don't get added to the registry.
// So if we find something here, we know that it's an older version.
bool findVCToolChainViaRegistry(std::string &Path, ToolsetLayout &VSLayout);

} // namespace llvm

#endif
