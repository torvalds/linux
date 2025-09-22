//===----- COFFVCRuntimeSupport.h -- VC runtime support in ORC --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Utilities for loading and initializaing vc runtime in Orc.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_COFFCRUNTIMESUPPORT_H
#define LLVM_EXECUTIONENGINE_ORC_COFFCRUNTIMESUPPORT_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
#include "llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"

#include <future>
#include <memory>
#include <thread>
#include <vector>

namespace llvm {
namespace orc {

/// Bootstraps the vc runtime within jitdylibs.
class COFFVCRuntimeBootstrapper {
public:
  /// Try to create a COFFVCRuntimeBootstrapper instance. An optional
  /// RuntimePath can be given to specify the location of directory that
  /// contains all vc runtime library files such as ucrt.lib and msvcrt.lib. If
  /// no path was given, it will try to search the MSVC toolchain and Windows
  /// SDK installation and use the found library files automatically.
  ///
  /// Note that depending on the build setting, a different library
  /// file must be used. In general, if vc runtime was statically linked to the
  /// object file that is to be jit-linked, LoadStaticVCRuntime and
  /// InitializeStaticVCRuntime must be used with libcmt.lib, libucrt.lib,
  /// libvcruntimelib. If vc runtime was dynamically linked LoadDynamicVCRuntime
  /// must be used along with msvcrt.lib, ucrt.lib, vcruntime.lib.
  ///
  /// More information is on:
  /// https://docs.microsoft.com/en-us/cpp/c-runtime-library/crt-library-features
  static Expected<std::unique_ptr<COFFVCRuntimeBootstrapper>>
  Create(ExecutionSession &ES, ObjectLinkingLayer &ObjLinkingLayer,
         const char *RuntimePath = nullptr);

  /// Adds symbol definitions of static version of msvc runtime libraries.
  Expected<std::vector<std::string>>
  loadStaticVCRuntime(JITDylib &JD, bool DebugVersion = false);

  /// Runs the initializer of static version of msvc runtime libraries.
  /// This must be called before calling any functions requiring c runtime (e.g.
  /// printf) within the jit session. Note that proper initialization of vc
  /// runtime requires ability of running static initializers. Cosider setting
  /// up COFFPlatform.
  Error initializeStaticVCRuntime(JITDylib &JD);

  /// Adds symbol definitions of dynamic version of msvc runtime libraries.
  Expected<std::vector<std::string>>
  loadDynamicVCRuntime(JITDylib &JD, bool DebugVersion = false);

private:
  COFFVCRuntimeBootstrapper(ExecutionSession &ES,
                            ObjectLinkingLayer &ObjLinkingLayer,
                            const char *RuntimePath);

  ExecutionSession &ES;
  ObjectLinkingLayer &ObjLinkingLayer;
  std::string RuntimePath;

  struct MSVCToolchainPath {
    SmallString<256> VCToolchainLib;
    SmallString<256> UCRTSdkLib;
  };

  static Expected<MSVCToolchainPath> getMSVCToolchainPath();
  Error loadVCRuntime(JITDylib &JD, std::vector<std::string> &ImportedLibraries,
                      ArrayRef<StringRef> VCLibs, ArrayRef<StringRef> UCRTLibs);
};

} // namespace orc
} // namespace llvm

#endif
