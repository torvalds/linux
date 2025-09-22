//===--- DebugerSupportPlugin.h -- Utils for debugger support ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generates debug objects and registers them using the jit-loader-gdb protocol.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_DEBUGGERSUPPORTPLUGIN_H
#define LLVM_EXECUTIONENGINE_ORC_DEBUGGERSUPPORTPLUGIN_H

#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/EPCDebugObjectRegistrar.h"
#include "llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h"

namespace llvm {
namespace orc {

/// For each object containing debug info, installs JITLink passes to synthesize
/// a debug object and then register it via the GDB JIT-registration interface.
///
/// Currently MachO only. For ELF use DebugObjectManagerPlugin. These two
/// plugins will be merged in the near future.
class GDBJITDebugInfoRegistrationPlugin : public ObjectLinkingLayer::Plugin {
public:
  class DebugSectionSynthesizer {
  public:
    virtual ~DebugSectionSynthesizer() = default;
    virtual Error startSynthesis() = 0;
    virtual Error completeSynthesisAndRegister() = 0;
  };

  static Expected<std::unique_ptr<GDBJITDebugInfoRegistrationPlugin>>
  Create(ExecutionSession &ES, JITDylib &ProcessJD, const Triple &TT);

  GDBJITDebugInfoRegistrationPlugin(ExecutorAddr RegisterActionAddr)
      : RegisterActionAddr(RegisterActionAddr) {}

  Error notifyFailed(MaterializationResponsibility &MR) override;
  Error notifyRemovingResources(JITDylib &JD, ResourceKey K) override;

  void notifyTransferringResources(JITDylib &JD, ResourceKey DstKey,
                                   ResourceKey SrcKey) override;

  void modifyPassConfig(MaterializationResponsibility &MR,
                        jitlink::LinkGraph &LG,
                        jitlink::PassConfiguration &PassConfig) override;

private:
  void modifyPassConfigForMachO(MaterializationResponsibility &MR,
                                jitlink::LinkGraph &LG,
                                jitlink::PassConfiguration &PassConfig);

  ExecutorAddr RegisterActionAddr;
};

} // namespace orc
} // namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_DEBUGGERSUPPORTPLUGIN_H
