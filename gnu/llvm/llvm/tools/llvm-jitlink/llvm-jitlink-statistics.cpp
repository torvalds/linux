//===-- llvm-jitlink-statistics.cpp -- gathers/reports JIT-linking stats --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the code for enabling, gathering and reporting
// llvm-jitlink statistics.
//
//===----------------------------------------------------------------------===//

#include "llvm-jitlink.h"
#include "llvm/Support/CommandLine.h"

#define DEBUG_TYPE "llvm_jitlink"

using namespace llvm;
using namespace llvm::jitlink;
using namespace llvm::orc;

static cl::opt<bool> ShowPrePruneTotalBlockSize(
    "pre-prune-total-block-size",
    cl::desc("Total size of all blocks (including zero-fill) in all "
             "graphs (pre-pruning)"),
    cl::init(false));

static cl::opt<bool> ShowPostFixupTotalBlockSize(
    "post-fixup-total-block-size",
    cl::desc("Total size of all blocks (including zero-fill) in all "
             "graphs (post-fixup)"),
    cl::init(false));

class StatsPlugin : public ObjectLinkingLayer::Plugin {
public:
  static void enableIfNeeded(Session &S, bool UsingOrcRuntime) {
    std::unique_ptr<StatsPlugin> Instance;
    auto GetStats = [&]() -> StatsPlugin & {
      if (!Instance)
        Instance.reset(new StatsPlugin(UsingOrcRuntime));
      return *Instance;
    };

    if (ShowPrePruneTotalBlockSize)
      GetStats().PrePruneTotalBlockSize = 0;

    if (ShowPostFixupTotalBlockSize)
      GetStats().PostFixupTotalBlockSize = 0;

    if (Instance)
      S.ObjLayer.addPlugin(std::move(Instance));
  }

  ~StatsPlugin() { publish(dbgs()); }

  void publish(raw_ostream &OS);

  void modifyPassConfig(MaterializationResponsibility &MR, LinkGraph &G,
                        PassConfiguration &PassConfig) override {
    PassConfig.PrePrunePasses.push_back(
        [this](LinkGraph &G) { return recordPrePruneStats(G); });
    PassConfig.PostFixupPasses.push_back(
        [this](LinkGraph &G) { return recordPostFixupStats(G); });
  }

  Error notifyFailed(MaterializationResponsibility &MR) override {
    return Error::success();
  }

  Error notifyRemovingResources(JITDylib &JD, ResourceKey K) override {
    return Error::success();
  }

  void notifyTransferringResources(JITDylib &JD, ResourceKey DstKey,
                                   ResourceKey SrcKey) override {}

private:
  StatsPlugin(bool UsingOrcRuntime) : UsingOrcRuntime(UsingOrcRuntime) {}
  Error recordPrePruneStats(jitlink::LinkGraph &G);
  Error recordPostFixupStats(jitlink::LinkGraph &G);

  bool UsingOrcRuntime;

  std::mutex M;
  std::optional<uint64_t> PrePruneTotalBlockSize;
  std::optional<uint64_t> PostFixupTotalBlockSize;
  std::optional<DenseMap<size_t, size_t>> EdgeCountDetails;
};

void StatsPlugin::publish(raw_ostream &OS) {

  if (UsingOrcRuntime)
    OS << "Note: Session stats include runtime and entry point lookup, but "
          "not JITDylib initialization/deinitialization.\n";

  OS << "Statistics:\n";
  if (PrePruneTotalBlockSize)
    OS << "  Total size of all blocks before pruning: "
       << *PrePruneTotalBlockSize << "\n";

  if (PostFixupTotalBlockSize)
    OS << "  Total size of all blocks after fixups: "
       << *PostFixupTotalBlockSize << "\n";
}

static uint64_t computeTotalBlockSizes(LinkGraph &G) {
  uint64_t TotalSize = 0;
  for (auto *B : G.blocks())
    TotalSize += B->getSize();
  return TotalSize;
}

Error StatsPlugin::recordPrePruneStats(LinkGraph &G) {
  std::lock_guard<std::mutex> Lock(M);

  if (PrePruneTotalBlockSize)
    *PrePruneTotalBlockSize += computeTotalBlockSizes(G);

  return Error::success();
}

Error StatsPlugin::recordPostFixupStats(LinkGraph &G) {
  std::lock_guard<std::mutex> Lock(M);

  if (PostFixupTotalBlockSize)
    *PostFixupTotalBlockSize += computeTotalBlockSizes(G);
  return Error::success();
}

namespace llvm {
void enableStatistics(Session &S, bool UsingOrcRuntime) {
  StatsPlugin::enableIfNeeded(S, UsingOrcRuntime);
}
} // namespace llvm
