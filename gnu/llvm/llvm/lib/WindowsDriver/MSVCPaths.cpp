//===-- MSVCPaths.cpp - MSVC path-parsing helpers -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/WindowsDriver/MSVCPaths.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"
#include <optional>
#include <string>

#ifdef _WIN32
#include "llvm/Support/ConvertUTF.h"
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifdef _MSC_VER
// Don't support SetupApi on MinGW.
#define USE_MSVC_SETUP_API

// Make sure this comes before MSVCSetupApi.h
#include <comdef.h>

#include "llvm/Support/COM.h"
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnon-virtual-dtor"
#endif
#include "llvm/WindowsDriver/MSVCSetupApi.h"
#ifdef __clang__
#pragma clang diagnostic pop
#endif
_COM_SMARTPTR_TYPEDEF(ISetupConfiguration, __uuidof(ISetupConfiguration));
_COM_SMARTPTR_TYPEDEF(ISetupConfiguration2, __uuidof(ISetupConfiguration2));
_COM_SMARTPTR_TYPEDEF(ISetupHelper, __uuidof(ISetupHelper));
_COM_SMARTPTR_TYPEDEF(IEnumSetupInstances, __uuidof(IEnumSetupInstances));
_COM_SMARTPTR_TYPEDEF(ISetupInstance, __uuidof(ISetupInstance));
_COM_SMARTPTR_TYPEDEF(ISetupInstance2, __uuidof(ISetupInstance2));
#endif

static std::string
getHighestNumericTupleInDirectory(llvm::vfs::FileSystem &VFS,
                                  llvm::StringRef Directory) {
  std::string Highest;
  llvm::VersionTuple HighestTuple;

  std::error_code EC;
  for (llvm::vfs::directory_iterator DirIt = VFS.dir_begin(Directory, EC),
                                     DirEnd;
       !EC && DirIt != DirEnd; DirIt.increment(EC)) {
    auto Status = VFS.status(DirIt->path());
    if (!Status || !Status->isDirectory())
      continue;
    llvm::StringRef CandidateName = llvm::sys::path::filename(DirIt->path());
    llvm::VersionTuple Tuple;
    if (Tuple.tryParse(CandidateName)) // tryParse() returns true on error.
      continue;
    if (Tuple > HighestTuple) {
      HighestTuple = Tuple;
      Highest = CandidateName.str();
    }
  }

  return Highest;
}

static bool getWindows10SDKVersionFromPath(llvm::vfs::FileSystem &VFS,
                                           const std::string &SDKPath,
                                           std::string &SDKVersion) {
  llvm::SmallString<128> IncludePath(SDKPath);
  llvm::sys::path::append(IncludePath, "Include");
  SDKVersion = getHighestNumericTupleInDirectory(VFS, IncludePath);
  return !SDKVersion.empty();
}

static bool getWindowsSDKDirViaCommandLine(
    llvm::vfs::FileSystem &VFS, std::optional<llvm::StringRef> WinSdkDir,
    std::optional<llvm::StringRef> WinSdkVersion,
    std::optional<llvm::StringRef> WinSysRoot, std::string &Path, int &Major,
    std::string &Version) {
  if (WinSdkDir || WinSysRoot) {
    // Don't validate the input; trust the value supplied by the user.
    // The motivation is to prevent unnecessary file and registry access.
    llvm::VersionTuple SDKVersion;
    if (WinSdkVersion)
      SDKVersion.tryParse(*WinSdkVersion);

    if (WinSysRoot) {
      llvm::SmallString<128> SDKPath(*WinSysRoot);
      llvm::sys::path::append(SDKPath, "Windows Kits");
      if (!SDKVersion.empty())
        llvm::sys::path::append(SDKPath, llvm::Twine(SDKVersion.getMajor()));
      else
        llvm::sys::path::append(
            SDKPath, getHighestNumericTupleInDirectory(VFS, SDKPath));
      Path = std::string(SDKPath);
    } else {
      Path = WinSdkDir->str();
    }

    if (!SDKVersion.empty()) {
      Major = SDKVersion.getMajor();
      Version = SDKVersion.getAsString();
    } else if (getWindows10SDKVersionFromPath(VFS, Path, Version)) {
      Major = 10;
    }
    return true;
  }
  return false;
}

#ifdef _WIN32
static bool readFullStringValue(HKEY hkey, const char *valueName,
                                std::string &value) {
  std::wstring WideValueName;
  if (!llvm::ConvertUTF8toWide(valueName, WideValueName))
    return false;

  DWORD result = 0;
  DWORD valueSize = 0;
  DWORD type = 0;
  // First just query for the required size.
  result = RegQueryValueExW(hkey, WideValueName.c_str(), NULL, &type, NULL,
                            &valueSize);
  if (result != ERROR_SUCCESS || type != REG_SZ || !valueSize)
    return false;
  std::vector<BYTE> buffer(valueSize);
  result = RegQueryValueExW(hkey, WideValueName.c_str(), NULL, NULL, &buffer[0],
                            &valueSize);
  if (result == ERROR_SUCCESS) {
    std::wstring WideValue(reinterpret_cast<const wchar_t *>(buffer.data()),
                           valueSize / sizeof(wchar_t));
    if (valueSize && WideValue.back() == L'\0') {
      WideValue.pop_back();
    }
    // The destination buffer must be empty as an invariant of the conversion
    // function; but this function is sometimes called in a loop that passes in
    // the same buffer, however. Simply clear it out so we can overwrite it.
    value.clear();
    return llvm::convertWideToUTF8(WideValue, value);
  }
  return false;
}
#endif

/// Read registry string.
/// This also supports a means to look for high-versioned keys by use
/// of a $VERSION placeholder in the key path.
/// $VERSION in the key path is a placeholder for the version number,
/// causing the highest value path to be searched for and used.
/// I.e. "SOFTWARE\\Microsoft\\VisualStudio\\$VERSION".
/// There can be additional characters in the component.  Only the numeric
/// characters are compared.  This function only searches HKLM.
static bool getSystemRegistryString(const char *keyPath, const char *valueName,
                                    std::string &value, std::string *phValue) {
#ifndef _WIN32
  return false;
#else
  HKEY hRootKey = HKEY_LOCAL_MACHINE;
  HKEY hKey = NULL;
  long lResult;
  bool returnValue = false;

  const char *placeHolder = strstr(keyPath, "$VERSION");
  std::string bestName;
  // If we have a $VERSION placeholder, do the highest-version search.
  if (placeHolder) {
    const char *keyEnd = placeHolder - 1;
    const char *nextKey = placeHolder;
    // Find end of previous key.
    while ((keyEnd > keyPath) && (*keyEnd != '\\'))
      keyEnd--;
    // Find end of key containing $VERSION.
    while (*nextKey && (*nextKey != '\\'))
      nextKey++;
    size_t partialKeyLength = keyEnd - keyPath;
    char partialKey[256];
    if (partialKeyLength >= sizeof(partialKey))
      partialKeyLength = sizeof(partialKey) - 1;
    strncpy(partialKey, keyPath, partialKeyLength);
    partialKey[partialKeyLength] = '\0';
    HKEY hTopKey = NULL;
    lResult = RegOpenKeyExA(hRootKey, partialKey, 0, KEY_READ | KEY_WOW64_32KEY,
                            &hTopKey);
    if (lResult == ERROR_SUCCESS) {
      char keyName[256];
      double bestValue = 0.0;
      DWORD index, size = sizeof(keyName) - 1;
      for (index = 0; RegEnumKeyExA(hTopKey, index, keyName, &size, NULL, NULL,
                                    NULL, NULL) == ERROR_SUCCESS;
           index++) {
        const char *sp = keyName;
        while (*sp && !llvm::isDigit(*sp))
          sp++;
        if (!*sp)
          continue;
        const char *ep = sp + 1;
        while (*ep && (llvm::isDigit(*ep) || (*ep == '.')))
          ep++;
        char numBuf[32];
        strncpy(numBuf, sp, sizeof(numBuf) - 1);
        numBuf[sizeof(numBuf) - 1] = '\0';
        double dvalue = strtod(numBuf, NULL);
        if (dvalue > bestValue) {
          // Test that InstallDir is indeed there before keeping this index.
          // Open the chosen key path remainder.
          bestName = keyName;
          // Append rest of key.
          bestName.append(nextKey);
          lResult = RegOpenKeyExA(hTopKey, bestName.c_str(), 0,
                                  KEY_READ | KEY_WOW64_32KEY, &hKey);
          if (lResult == ERROR_SUCCESS) {
            if (readFullStringValue(hKey, valueName, value)) {
              bestValue = dvalue;
              if (phValue)
                *phValue = bestName;
              returnValue = true;
            }
            RegCloseKey(hKey);
          }
        }
        size = sizeof(keyName) - 1;
      }
      RegCloseKey(hTopKey);
    }
  } else {
    lResult =
        RegOpenKeyExA(hRootKey, keyPath, 0, KEY_READ | KEY_WOW64_32KEY, &hKey);
    if (lResult == ERROR_SUCCESS) {
      if (readFullStringValue(hKey, valueName, value))
        returnValue = true;
      if (phValue)
        phValue->clear();
      RegCloseKey(hKey);
    }
  }
  return returnValue;
#endif // _WIN32
}

namespace llvm {

const char *archToWindowsSDKArch(Triple::ArchType Arch) {
  switch (Arch) {
  case Triple::ArchType::x86:
    return "x86";
  case Triple::ArchType::x86_64:
    return "x64";
  case Triple::ArchType::arm:
  case Triple::ArchType::thumb:
    return "arm";
  case Triple::ArchType::aarch64:
    return "arm64";
  default:
    return "";
  }
}

const char *archToLegacyVCArch(Triple::ArchType Arch) {
  switch (Arch) {
  case Triple::ArchType::x86:
    // x86 is default in legacy VC toolchains.
    // e.g. x86 libs are directly in /lib as opposed to /lib/x86.
    return "";
  case Triple::ArchType::x86_64:
    return "amd64";
  case Triple::ArchType::arm:
  case Triple::ArchType::thumb:
    return "arm";
  case Triple::ArchType::aarch64:
    return "arm64";
  default:
    return "";
  }
}

const char *archToDevDivInternalArch(Triple::ArchType Arch) {
  switch (Arch) {
  case Triple::ArchType::x86:
    return "i386";
  case Triple::ArchType::x86_64:
    return "amd64";
  case Triple::ArchType::arm:
  case Triple::ArchType::thumb:
    return "arm";
  case Triple::ArchType::aarch64:
    return "arm64";
  default:
    return "";
  }
}

bool appendArchToWindowsSDKLibPath(int SDKMajor, SmallString<128> LibPath,
                                   Triple::ArchType Arch, std::string &path) {
  if (SDKMajor >= 8) {
    sys::path::append(LibPath, archToWindowsSDKArch(Arch));
  } else {
    switch (Arch) {
    // In Windows SDK 7.x, x86 libraries are directly in the Lib folder.
    case Triple::x86:
      break;
    case Triple::x86_64:
      sys::path::append(LibPath, "x64");
      break;
    case Triple::arm:
    case Triple::thumb:
      // It is not necessary to link against Windows SDK 7.x when targeting ARM.
      return false;
    default:
      return false;
    }
  }

  path = std::string(LibPath);
  return true;
}

std::string getSubDirectoryPath(SubDirectoryType Type, ToolsetLayout VSLayout,
                                const std::string &VCToolChainPath,
                                Triple::ArchType TargetArch,
                                StringRef SubdirParent) {
  const char *SubdirName;
  const char *IncludeName;
  switch (VSLayout) {
  case ToolsetLayout::OlderVS:
    SubdirName = archToLegacyVCArch(TargetArch);
    IncludeName = "include";
    break;
  case ToolsetLayout::VS2017OrNewer:
    SubdirName = archToWindowsSDKArch(TargetArch);
    IncludeName = "include";
    break;
  case ToolsetLayout::DevDivInternal:
    SubdirName = archToDevDivInternalArch(TargetArch);
    IncludeName = "inc";
    break;
  }

  SmallString<256> Path(VCToolChainPath);
  if (!SubdirParent.empty())
    sys::path::append(Path, SubdirParent);

  switch (Type) {
  case SubDirectoryType::Bin:
    if (VSLayout == ToolsetLayout::VS2017OrNewer) {
      // MSVC ships with two linkers: a 32-bit x86 and 64-bit x86 linker.
      // On x86, pick the linker that corresponds to the current process.
      // On ARM64, pick the 32-bit x86 linker; the 64-bit one doesn't run
      // on Windows 10.
      //
      // FIXME: Consider using IsWow64GuestMachineSupported to figure out
      // if we can invoke the 64-bit linker. It's generally preferable
      // because it won't run out of address-space.
      const bool HostIsX64 =
          Triple(sys::getProcessTriple()).getArch() == Triple::x86_64;
      const char *const HostName = HostIsX64 ? "Hostx64" : "Hostx86";
      sys::path::append(Path, "bin", HostName, SubdirName);
    } else { // OlderVS or DevDivInternal
      sys::path::append(Path, "bin", SubdirName);
    }
    break;
  case SubDirectoryType::Include:
    sys::path::append(Path, IncludeName);
    break;
  case SubDirectoryType::Lib:
    sys::path::append(Path, "lib", SubdirName);
    break;
  }
  return std::string(Path);
}

bool useUniversalCRT(ToolsetLayout VSLayout, const std::string &VCToolChainPath,
                     Triple::ArchType TargetArch, vfs::FileSystem &VFS) {
  SmallString<128> TestPath(getSubDirectoryPath(
      SubDirectoryType::Include, VSLayout, VCToolChainPath, TargetArch));
  sys::path::append(TestPath, "stdlib.h");
  return !VFS.exists(TestPath);
}

bool getWindowsSDKDir(vfs::FileSystem &VFS, std::optional<StringRef> WinSdkDir,
                      std::optional<StringRef> WinSdkVersion,
                      std::optional<StringRef> WinSysRoot, std::string &Path,
                      int &Major, std::string &WindowsSDKIncludeVersion,
                      std::string &WindowsSDKLibVersion) {
  // Trust /winsdkdir and /winsdkversion if present.
  if (getWindowsSDKDirViaCommandLine(VFS, WinSdkDir, WinSdkVersion, WinSysRoot,
                                     Path, Major, WindowsSDKIncludeVersion)) {
    WindowsSDKLibVersion = WindowsSDKIncludeVersion;
    return true;
  }

  // FIXME: Try env vars (%WindowsSdkDir%, %UCRTVersion%) before going to
  // registry.

  // Try the Windows registry.
  std::string RegistrySDKVersion;
  if (!getSystemRegistryString(
          "SOFTWARE\\Microsoft\\Microsoft SDKs\\Windows\\$VERSION",
          "InstallationFolder", Path, &RegistrySDKVersion))
    return false;
  if (Path.empty() || RegistrySDKVersion.empty())
    return false;

  WindowsSDKIncludeVersion.clear();
  WindowsSDKLibVersion.clear();
  Major = 0;
  std::sscanf(RegistrySDKVersion.c_str(), "v%d.", &Major);
  if (Major <= 7)
    return true;
  if (Major == 8) {
    // Windows SDK 8.x installs libraries in a folder whose names depend on the
    // version of the OS you're targeting.  By default choose the newest, which
    // usually corresponds to the version of the OS you've installed the SDK on.
    const char *Tests[] = {"winv6.3", "win8", "win7"};
    for (const char *Test : Tests) {
      SmallString<128> TestPath(Path);
      sys::path::append(TestPath, "Lib", Test);
      if (VFS.exists(TestPath)) {
        WindowsSDKLibVersion = Test;
        break;
      }
    }
    return !WindowsSDKLibVersion.empty();
  }
  if (Major == 10) {
    if (!getWindows10SDKVersionFromPath(VFS, Path, WindowsSDKIncludeVersion))
      return false;
    WindowsSDKLibVersion = WindowsSDKIncludeVersion;
    return true;
  }
  // Unsupported SDK version
  return false;
}

bool getUniversalCRTSdkDir(vfs::FileSystem &VFS,
                           std::optional<StringRef> WinSdkDir,
                           std::optional<StringRef> WinSdkVersion,
                           std::optional<StringRef> WinSysRoot,
                           std::string &Path, std::string &UCRTVersion) {
  // If /winsdkdir is passed, use it as location for the UCRT too.
  // FIXME: Should there be a dedicated /ucrtdir to override /winsdkdir?
  int Major;
  if (getWindowsSDKDirViaCommandLine(VFS, WinSdkDir, WinSdkVersion, WinSysRoot,
                                     Path, Major, UCRTVersion))
    return true;

  // FIXME: Try env vars (%UniversalCRTSdkDir%, %UCRTVersion%) before going to
  // registry.

  // vcvarsqueryregistry.bat for Visual Studio 2015 queries the registry
  // for the specific key "KitsRoot10". So do we.
  if (!getSystemRegistryString(
          "SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots", "KitsRoot10",
          Path, nullptr))
    return false;

  return getWindows10SDKVersionFromPath(VFS, Path, UCRTVersion);
}

bool findVCToolChainViaCommandLine(vfs::FileSystem &VFS,
                                   std::optional<StringRef> VCToolsDir,
                                   std::optional<StringRef> VCToolsVersion,
                                   std::optional<StringRef> WinSysRoot,
                                   std::string &Path, ToolsetLayout &VSLayout) {
  // Don't validate the input; trust the value supplied by the user.
  // The primary motivation is to prevent unnecessary file and registry access.
  if (VCToolsDir || WinSysRoot) {
    if (WinSysRoot) {
      SmallString<128> ToolsPath(*WinSysRoot);
      sys::path::append(ToolsPath, "VC", "Tools", "MSVC");
      std::string ToolsVersion;
      if (VCToolsVersion)
        ToolsVersion = VCToolsVersion->str();
      else
        ToolsVersion = getHighestNumericTupleInDirectory(VFS, ToolsPath);
      sys::path::append(ToolsPath, ToolsVersion);
      Path = std::string(ToolsPath);
    } else {
      Path = VCToolsDir->str();
    }
    VSLayout = ToolsetLayout::VS2017OrNewer;
    return true;
  }
  return false;
}

bool findVCToolChainViaEnvironment(vfs::FileSystem &VFS, std::string &Path,
                                   ToolsetLayout &VSLayout) {
  // These variables are typically set by vcvarsall.bat
  // when launching a developer command prompt.
  if (std::optional<std::string> VCToolsInstallDir =
          sys::Process::GetEnv("VCToolsInstallDir")) {
    // This is only set by newer Visual Studios, and it leads straight to
    // the toolchain directory.
    Path = std::move(*VCToolsInstallDir);
    VSLayout = ToolsetLayout::VS2017OrNewer;
    return true;
  }
  if (std::optional<std::string> VCInstallDir =
          sys::Process::GetEnv("VCINSTALLDIR")) {
    // If the previous variable isn't set but this one is, then we've found
    // an older Visual Studio. This variable is set by newer Visual Studios too,
    // so this check has to appear second.
    // In older Visual Studios, the VC directory is the toolchain.
    Path = std::move(*VCInstallDir);
    VSLayout = ToolsetLayout::OlderVS;
    return true;
  }

  // We couldn't find any VC environment variables. Let's walk through PATH and
  // see if it leads us to a VC toolchain bin directory. If it does, pick the
  // first one that we find.
  if (std::optional<std::string> PathEnv = sys::Process::GetEnv("PATH")) {
    SmallVector<StringRef, 8> PathEntries;
    StringRef(*PathEnv).split(PathEntries, sys::EnvPathSeparator);
    for (StringRef PathEntry : PathEntries) {
      if (PathEntry.empty())
        continue;

      SmallString<256> ExeTestPath;

      // If cl.exe doesn't exist, then this definitely isn't a VC toolchain.
      ExeTestPath = PathEntry;
      sys::path::append(ExeTestPath, "cl.exe");
      if (!VFS.exists(ExeTestPath))
        continue;

      // cl.exe existing isn't a conclusive test for a VC toolchain; clang also
      // has a cl.exe. So let's check for link.exe too.
      ExeTestPath = PathEntry;
      sys::path::append(ExeTestPath, "link.exe");
      if (!VFS.exists(ExeTestPath))
        continue;

      // whatever/VC/bin --> old toolchain, VC dir is toolchain dir.
      StringRef TestPath = PathEntry;
      bool IsBin = sys::path::filename(TestPath).equals_insensitive("bin");
      if (!IsBin) {
        // Strip any architecture subdir like "amd64".
        TestPath = sys::path::parent_path(TestPath);
        IsBin = sys::path::filename(TestPath).equals_insensitive("bin");
      }
      if (IsBin) {
        StringRef ParentPath = sys::path::parent_path(TestPath);
        StringRef ParentFilename = sys::path::filename(ParentPath);
        if (ParentFilename.equals_insensitive("VC")) {
          Path = std::string(ParentPath);
          VSLayout = ToolsetLayout::OlderVS;
          return true;
        }
        if (ParentFilename.equals_insensitive("x86ret") ||
            ParentFilename.equals_insensitive("x86chk") ||
            ParentFilename.equals_insensitive("amd64ret") ||
            ParentFilename.equals_insensitive("amd64chk")) {
          Path = std::string(ParentPath);
          VSLayout = ToolsetLayout::DevDivInternal;
          return true;
        }

      } else {
        // This could be a new (>=VS2017) toolchain. If it is, we should find
        // path components with these prefixes when walking backwards through
        // the path.
        // Note: empty strings match anything.
        StringRef ExpectedPrefixes[] = {"",     "Host",  "bin", "",
                                        "MSVC", "Tools", "VC"};

        auto It = sys::path::rbegin(PathEntry);
        auto End = sys::path::rend(PathEntry);
        for (StringRef Prefix : ExpectedPrefixes) {
          if (It == End)
            goto NotAToolChain;
          if (!It->starts_with_insensitive(Prefix))
            goto NotAToolChain;
          ++It;
        }

        // We've found a new toolchain!
        // Back up 3 times (/bin/Host/arch) to get the root path.
        StringRef ToolChainPath(PathEntry);
        for (int i = 0; i < 3; ++i)
          ToolChainPath = sys::path::parent_path(ToolChainPath);

        Path = std::string(ToolChainPath);
        VSLayout = ToolsetLayout::VS2017OrNewer;
        return true;
      }

    NotAToolChain:
      continue;
    }
  }
  return false;
}

bool findVCToolChainViaSetupConfig(vfs::FileSystem &VFS,
                                   std::optional<StringRef> VCToolsVersion,
                                   std::string &Path, ToolsetLayout &VSLayout) {
#if !defined(USE_MSVC_SETUP_API)
  return false;
#else
  // FIXME: This really should be done once in the top-level program's main
  // function, as it may have already been initialized with a different
  // threading model otherwise.
  sys::InitializeCOMRAII COM(sys::COMThreadingMode::SingleThreaded);
  HRESULT HR;

  // _com_ptr_t will throw a _com_error if a COM calls fail.
  // The LLVM coding standards forbid exception handling, so we'll have to
  // stop them from being thrown in the first place.
  // The destructor will put the regular error handler back when we leave
  // this scope.
  struct SuppressCOMErrorsRAII {
    static void __stdcall handler(HRESULT hr, IErrorInfo *perrinfo) {}

    SuppressCOMErrorsRAII() { _set_com_error_handler(handler); }

    ~SuppressCOMErrorsRAII() { _set_com_error_handler(_com_raise_error); }

  } COMErrorSuppressor;

  ISetupConfigurationPtr Query;
  HR = Query.CreateInstance(__uuidof(SetupConfiguration));
  if (FAILED(HR))
    return false;

  IEnumSetupInstancesPtr EnumInstances;
  HR = ISetupConfiguration2Ptr(Query)->EnumAllInstances(&EnumInstances);
  if (FAILED(HR))
    return false;

  ISetupInstancePtr Instance;
  HR = EnumInstances->Next(1, &Instance, nullptr);
  if (HR != S_OK)
    return false;

  ISetupInstancePtr NewestInstance;
  std::optional<uint64_t> NewestVersionNum;
  do {
    bstr_t VersionString;
    uint64_t VersionNum;
    HR = Instance->GetInstallationVersion(VersionString.GetAddress());
    if (FAILED(HR))
      continue;
    HR = ISetupHelperPtr(Query)->ParseVersion(VersionString, &VersionNum);
    if (FAILED(HR))
      continue;
    if (!NewestVersionNum || (VersionNum > NewestVersionNum)) {
      NewestInstance = Instance;
      NewestVersionNum = VersionNum;
    }
  } while ((HR = EnumInstances->Next(1, &Instance, nullptr)) == S_OK);

  if (!NewestInstance)
    return false;

  bstr_t VCPathWide;
  HR = NewestInstance->ResolvePath(L"VC", VCPathWide.GetAddress());
  if (FAILED(HR))
    return false;

  std::string VCRootPath;
  convertWideToUTF8(std::wstring(VCPathWide), VCRootPath);

  std::string ToolsVersion;
  if (VCToolsVersion.has_value()) {
    ToolsVersion = *VCToolsVersion;
  } else {
    SmallString<256> ToolsVersionFilePath(VCRootPath);
    sys::path::append(ToolsVersionFilePath, "Auxiliary", "Build",
                      "Microsoft.VCToolsVersion.default.txt");

    auto ToolsVersionFile = MemoryBuffer::getFile(ToolsVersionFilePath);
    if (!ToolsVersionFile)
      return false;

    ToolsVersion = ToolsVersionFile->get()->getBuffer().rtrim();
  }


  SmallString<256> ToolchainPath(VCRootPath);
  sys::path::append(ToolchainPath, "Tools", "MSVC", ToolsVersion);
  auto Status = VFS.status(ToolchainPath);
  if (!Status || !Status->isDirectory())
    return false;

  Path = std::string(ToolchainPath.str());
  VSLayout = ToolsetLayout::VS2017OrNewer;
  return true;
#endif
}

bool findVCToolChainViaRegistry(std::string &Path, ToolsetLayout &VSLayout) {
  std::string VSInstallPath;
  if (getSystemRegistryString(R"(SOFTWARE\Microsoft\VisualStudio\$VERSION)",
                              "InstallDir", VSInstallPath, nullptr) ||
      getSystemRegistryString(R"(SOFTWARE\Microsoft\VCExpress\$VERSION)",
                              "InstallDir", VSInstallPath, nullptr)) {
    if (!VSInstallPath.empty()) {
      auto pos = VSInstallPath.find(R"(\Common7\IDE)");
      if (pos == std::string::npos)
        return false;
      SmallString<256> VCPath(StringRef(VSInstallPath.c_str(), pos));
      sys::path::append(VCPath, "VC");

      Path = std::string(VCPath);
      VSLayout = ToolsetLayout::OlderVS;
      return true;
    }
  }
  return false;
}

} // namespace llvm
