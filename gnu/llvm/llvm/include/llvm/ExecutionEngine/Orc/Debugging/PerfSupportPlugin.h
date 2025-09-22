//===----- PerfSupportPlugin.h ----- Utils for perf support -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Handles support for registering code with perf
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_PERFSUPPORTPLUGIN_H
#define LLVM_EXECUTIONENGINE_ORC_PERFSUPPORTPLUGIN_H

#include "llvm/ExecutionEngine/Orc/Shared/PerfSharedStructs.h"

#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h"

namespace llvm {
namespace orc {

/// Log perf jitdump events for each object (see
/// https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/tools/perf/Documentation/jitdump-specification.txt).
/// Currently has support for dumping code load records and unwind info records.
class PerfSupportPlugin : public ObjectLinkingLayer::Plugin {
public:
  PerfSupportPlugin(ExecutorProcessControl &EPC,
                    ExecutorAddr RegisterPerfStartAddr,
                    ExecutorAddr RegisterPerfEndAddr,
                    ExecutorAddr RegisterPerfImplAddr, bool EmitDebugInfo,
                    bool EmitUnwindInfo);
  ~PerfSupportPlugin();

  void modifyPassConfig(MaterializationResponsibility &MR,
                        jitlink::LinkGraph &G,
                        jitlink::PassConfiguration &Config) override;

  Error notifyFailed(MaterializationResponsibility &MR) override {
    return Error::success();
  }

  Error notifyRemovingResources(JITDylib &JD, ResourceKey K) override {
    return Error::success();
  }

  void notifyTransferringResources(JITDylib &JD, ResourceKey DstKey,
                                   ResourceKey SrcKey) override {}

  static Expected<std::unique_ptr<PerfSupportPlugin>>
  Create(ExecutorProcessControl &EPC, JITDylib &JD, bool EmitDebugInfo,
         bool EmitUnwindInfo);

private:
  ExecutorProcessControl &EPC;
  ExecutorAddr RegisterPerfStartAddr;
  ExecutorAddr RegisterPerfEndAddr;
  ExecutorAddr RegisterPerfImplAddr;
  std::atomic<uint64_t> CodeIndex;
  bool EmitDebugInfo;
  bool EmitUnwindInfo;
};

} // namespace orc
} // namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_PERFSUPPORTPLUGIN_H