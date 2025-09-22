//===------- COFFVCRuntimeSupport.cpp - VC runtime support in ORC ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/COFFVCRuntimeSupport.h"

#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/LookupAndRecordAddrs.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/WindowsDriver/MSVCPaths.h"

#define DEBUG_TYPE "orc"

using namespace llvm;
using namespace llvm::orc;
using namespace llvm::orc::shared;

Expected<std::unique_ptr<COFFVCRuntimeBootstrapper>>
COFFVCRuntimeBootstrapper::Create(ExecutionSession &ES,
                                  ObjectLinkingLayer &ObjLinkingLayer,
                                  const char *RuntimePath) {
  return std::unique_ptr<COFFVCRuntimeBootstrapper>(
      new COFFVCRuntimeBootstrapper(ES, ObjLinkingLayer, RuntimePath));
}

COFFVCRuntimeBootstrapper::COFFVCRuntimeBootstrapper(
    ExecutionSession &ES, ObjectLinkingLayer &ObjLinkingLayer,
    const char *RuntimePath)
    : ES(ES), ObjLinkingLayer(ObjLinkingLayer) {
  if (RuntimePath)
    this->RuntimePath = RuntimePath;
}

Expected<std::vector<std::string>>
COFFVCRuntimeBootstrapper::loadStaticVCRuntime(JITDylib &JD,
                                               bool DebugVersion) {
  StringRef VCLibs[] = {"libvcruntime.lib", "libcmt.lib", "libcpmt.lib"};
  StringRef UCRTLibs[] = {"libucrt.lib"};
  std::vector<std::string> ImportedLibraries;
  if (auto Err = loadVCRuntime(JD, ImportedLibraries, ArrayRef(VCLibs),
                               ArrayRef(UCRTLibs)))
    return std::move(Err);
  return ImportedLibraries;
}

Expected<std::vector<std::string>>
COFFVCRuntimeBootstrapper::loadDynamicVCRuntime(JITDylib &JD,
                                                bool DebugVersion) {
  StringRef VCLibs[] = {"vcruntime.lib", "msvcrt.lib", "msvcprt.lib"};
  StringRef UCRTLibs[] = {"ucrt.lib"};
  std::vector<std::string> ImportedLibraries;
  if (auto Err = loadVCRuntime(JD, ImportedLibraries, ArrayRef(VCLibs),
                               ArrayRef(UCRTLibs)))
    return std::move(Err);
  return ImportedLibraries;
}

Error COFFVCRuntimeBootstrapper::loadVCRuntime(
    JITDylib &JD, std::vector<std::string> &ImportedLibraries,
    ArrayRef<StringRef> VCLibs, ArrayRef<StringRef> UCRTLibs) {
  MSVCToolchainPath Path;
  if (!RuntimePath.empty()) {
    Path.UCRTSdkLib = RuntimePath;
    Path.VCToolchainLib = RuntimePath;
  } else {
    auto ToolchainPath = getMSVCToolchainPath();
    if (!ToolchainPath)
      return ToolchainPath.takeError();
    Path = *ToolchainPath;
  }
  LLVM_DEBUG({
    dbgs() << "Using VC toolchain pathes\n";
    dbgs() << "  VC toolchain path: " << Path.VCToolchainLib << "\n";
    dbgs() << "  UCRT path: " << Path.UCRTSdkLib << "\n";
  });

  auto LoadLibrary = [&](SmallString<256> LibPath, StringRef LibName) -> Error {
    sys::path::append(LibPath, LibName);

    auto G = StaticLibraryDefinitionGenerator::Load(ObjLinkingLayer,
                                                    LibPath.c_str());
    if (!G)
      return G.takeError();

    for (auto &Lib : (*G)->getImportedDynamicLibraries())
      ImportedLibraries.push_back(Lib);

    JD.addGenerator(std::move(*G));

    return Error::success();
  };
  for (auto &Lib : UCRTLibs)
    if (auto Err = LoadLibrary(Path.UCRTSdkLib, Lib))
      return Err;

  for (auto &Lib : VCLibs)
    if (auto Err = LoadLibrary(Path.VCToolchainLib, Lib))
      return Err;
  ImportedLibraries.push_back("ntdll.dll");
  ImportedLibraries.push_back("Kernel32.dll");

  return Error::success();
}

Error COFFVCRuntimeBootstrapper::initializeStaticVCRuntime(JITDylib &JD) {
  ExecutorAddr jit_scrt_initialize, jit_scrt_dllmain_before_initialize_c,
      jit_scrt_initialize_type_info,
      jit_scrt_initialize_default_local_stdio_options;
  if (auto Err = lookupAndRecordAddrs(
          ES, LookupKind::Static, makeJITDylibSearchOrder(&JD),
          {{ES.intern("__scrt_initialize_crt"), &jit_scrt_initialize},
           {ES.intern("__scrt_dllmain_before_initialize_c"),
            &jit_scrt_dllmain_before_initialize_c},
           {ES.intern("?__scrt_initialize_type_info@@YAXXZ"),
            &jit_scrt_initialize_type_info},
           {ES.intern("__scrt_initialize_default_local_stdio_options"),
            &jit_scrt_initialize_default_local_stdio_options}}))
    return Err;

  auto RunVoidInitFunc = [&](ExecutorAddr Addr) -> Error {
    if (auto Res = ES.getExecutorProcessControl().runAsVoidFunction(Addr))
      return Error::success();
    else
      return Res.takeError();
  };

  auto R =
      ES.getExecutorProcessControl().runAsIntFunction(jit_scrt_initialize, 0);
  if (!R)
    return R.takeError();

  if (auto Err = RunVoidInitFunc(jit_scrt_dllmain_before_initialize_c))
    return Err;

  if (auto Err = RunVoidInitFunc(jit_scrt_initialize_type_info))
    return Err;

  if (auto Err =
          RunVoidInitFunc(jit_scrt_initialize_default_local_stdio_options))
    return Err;

  SymbolAliasMap Alias;
  Alias[ES.intern("__run_after_c_init")] = {
      ES.intern("__scrt_dllmain_after_initialize_c"), JITSymbolFlags::Exported};
  if (auto Err = JD.define(symbolAliases(Alias)))
    return Err;

  return Error::success();
}

Expected<COFFVCRuntimeBootstrapper::MSVCToolchainPath>
COFFVCRuntimeBootstrapper::getMSVCToolchainPath() {
  std::string VCToolChainPath;
  ToolsetLayout VSLayout;
  IntrusiveRefCntPtr<vfs::FileSystem> VFS = vfs::getRealFileSystem();
  if (!findVCToolChainViaCommandLine(*VFS, std::nullopt, std::nullopt,
                                     std::nullopt, VCToolChainPath, VSLayout) &&
      !findVCToolChainViaEnvironment(*VFS, VCToolChainPath, VSLayout) &&
      !findVCToolChainViaSetupConfig(*VFS, {}, VCToolChainPath, VSLayout) &&
      !findVCToolChainViaRegistry(VCToolChainPath, VSLayout))
    return make_error<StringError>("Couldn't find msvc toolchain.",
                                   inconvertibleErrorCode());

  std::string UniversalCRTSdkPath;
  std::string UCRTVersion;
  if (!getUniversalCRTSdkDir(*VFS, std::nullopt, std::nullopt, std::nullopt,
                             UniversalCRTSdkPath, UCRTVersion))
    return make_error<StringError>("Couldn't find universal sdk.",
                                   inconvertibleErrorCode());

  MSVCToolchainPath ToolchainPath;
  SmallString<256> VCToolchainLib(VCToolChainPath);
  sys::path::append(VCToolchainLib, "lib", "x64");
  ToolchainPath.VCToolchainLib = VCToolchainLib;

  SmallString<256> UCRTSdkLib(UniversalCRTSdkPath);
  sys::path::append(UCRTSdkLib, "Lib", UCRTVersion, "ucrt", "x64");
  ToolchainPath.UCRTSdkLib = UCRTSdkLib;
  return ToolchainPath;
}
