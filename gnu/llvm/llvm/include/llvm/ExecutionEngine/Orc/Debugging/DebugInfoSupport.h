//===--- DebugInfoSupport.h ---- Utils for debug info support ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Utilities to preserve and parse debug info from LinkGraphs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_DEBUGINFOSUPPORT_H
#define LLVM_EXECUTIONENGINE_ORC_DEBUGINFOSUPPORT_H

#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h"

#include "llvm/DebugInfo/DWARF/DWARFContext.h"

namespace llvm {

namespace orc {

Error preserveDebugSections(jitlink::LinkGraph &G);
// The backing stringmap is also returned, for memory liftime management.
Expected<std::pair<std::unique_ptr<DWARFContext>,
                   StringMap<std::unique_ptr<MemoryBuffer>>>>
createDWARFContext(jitlink::LinkGraph &G);

// Thin wrapper around preserveDebugSections to be used as a standalone plugin.
class DebugInfoPreservationPlugin : public ObjectLinkingLayer::Plugin {
public:
  void modifyPassConfig(MaterializationResponsibility &MR,
                        jitlink::LinkGraph &LG,
                        jitlink::PassConfiguration &PassConfig) override {
    PassConfig.PrePrunePasses.push_back(preserveDebugSections);
  }

  Error notifyRemovingResources(JITDylib &JD, ResourceKey K) override {
    // Do nothing.
    return Error::success();
  }
  Error notifyFailed(MaterializationResponsibility &MR) override {
    // Do nothing.
    return Error::success();
  }
  void notifyTransferringResources(JITDylib &JD, ResourceKey DstKey,
                                   ResourceKey SrcKey) override {
    // Do nothing.
  }

  static Expected<std::unique_ptr<DebugInfoPreservationPlugin>> Create() {
    return std::make_unique<DebugInfoPreservationPlugin>();
  }
};

} // namespace orc

} // namespace llvm

#endif