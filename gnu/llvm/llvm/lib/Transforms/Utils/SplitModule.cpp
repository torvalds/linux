//===- SplitModule.cpp - Split a module into partitions -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the function llvm::SplitModule, which splits a module
// into multiple linkable partitions. It can be used to implement parallel code
// generation for link-time optimization.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/SplitModule.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/EquivalenceClasses.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <memory>
#include <queue>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "split-module"

namespace {

using ClusterMapType = EquivalenceClasses<const GlobalValue *>;
using ComdatMembersType = DenseMap<const Comdat *, const GlobalValue *>;
using ClusterIDMapType = DenseMap<const GlobalValue *, unsigned>;

bool compareClusters(const std::pair<unsigned, unsigned> &A,
                     const std::pair<unsigned, unsigned> &B) {
  if (A.second || B.second)
    return A.second > B.second;
  return A.first > B.first;
}

using BalancingQueueType =
    std::priority_queue<std::pair<unsigned, unsigned>,
                        std::vector<std::pair<unsigned, unsigned>>,
                        decltype(compareClusters) *>;

} // end anonymous namespace

static void addNonConstUser(ClusterMapType &GVtoClusterMap,
                            const GlobalValue *GV, const User *U) {
  assert((!isa<Constant>(U) || isa<GlobalValue>(U)) && "Bad user");

  if (const Instruction *I = dyn_cast<Instruction>(U)) {
    const GlobalValue *F = I->getParent()->getParent();
    GVtoClusterMap.unionSets(GV, F);
  } else if (const GlobalValue *GVU = dyn_cast<GlobalValue>(U)) {
    GVtoClusterMap.unionSets(GV, GVU);
  } else {
    llvm_unreachable("Underimplemented use case");
  }
}

// Adds all GlobalValue users of V to the same cluster as GV.
static void addAllGlobalValueUsers(ClusterMapType &GVtoClusterMap,
                                   const GlobalValue *GV, const Value *V) {
  for (const auto *U : V->users()) {
    SmallVector<const User *, 4> Worklist;
    Worklist.push_back(U);
    while (!Worklist.empty()) {
      const User *UU = Worklist.pop_back_val();
      // For each constant that is not a GV (a pure const) recurse.
      if (isa<Constant>(UU) && !isa<GlobalValue>(UU)) {
        Worklist.append(UU->user_begin(), UU->user_end());
        continue;
      }
      addNonConstUser(GVtoClusterMap, GV, UU);
    }
  }
}

static const GlobalObject *getGVPartitioningRoot(const GlobalValue *GV) {
  const GlobalObject *GO = GV->getAliaseeObject();
  if (const auto *GI = dyn_cast_or_null<GlobalIFunc>(GO))
    GO = GI->getResolverFunction();
  return GO;
}

// Find partitions for module in the way that no locals need to be
// globalized.
// Try to balance pack those partitions into N files since this roughly equals
// thread balancing for the backend codegen step.
static void findPartitions(Module &M, ClusterIDMapType &ClusterIDMap,
                           unsigned N) {
  // At this point module should have the proper mix of globals and locals.
  // As we attempt to partition this module, we must not change any
  // locals to globals.
  LLVM_DEBUG(dbgs() << "Partition module with (" << M.size()
                    << ") functions\n");
  ClusterMapType GVtoClusterMap;
  ComdatMembersType ComdatMembers;

  auto recordGVSet = [&GVtoClusterMap, &ComdatMembers](GlobalValue &GV) {
    if (GV.isDeclaration())
      return;

    if (!GV.hasName())
      GV.setName("__llvmsplit_unnamed");

    // Comdat groups must not be partitioned. For comdat groups that contain
    // locals, record all their members here so we can keep them together.
    // Comdat groups that only contain external globals are already handled by
    // the MD5-based partitioning.
    if (const Comdat *C = GV.getComdat()) {
      auto &Member = ComdatMembers[C];
      if (Member)
        GVtoClusterMap.unionSets(Member, &GV);
      else
        Member = &GV;
    }

    // Aliases should not be separated from their aliasees and ifuncs should
    // not be separated from their resolvers regardless of linkage.
    if (const GlobalObject *Root = getGVPartitioningRoot(&GV))
      if (&GV != Root)
        GVtoClusterMap.unionSets(&GV, Root);

    if (const Function *F = dyn_cast<Function>(&GV)) {
      for (const BasicBlock &BB : *F) {
        BlockAddress *BA = BlockAddress::lookup(&BB);
        if (!BA || !BA->isConstantUsed())
          continue;
        addAllGlobalValueUsers(GVtoClusterMap, F, BA);
      }
    }

    if (GV.hasLocalLinkage())
      addAllGlobalValueUsers(GVtoClusterMap, &GV, &GV);
  };

  llvm::for_each(M.functions(), recordGVSet);
  llvm::for_each(M.globals(), recordGVSet);
  llvm::for_each(M.aliases(), recordGVSet);

  // Assigned all GVs to merged clusters while balancing number of objects in
  // each.
  BalancingQueueType BalancingQueue(compareClusters);
  // Pre-populate priority queue with N slot blanks.
  for (unsigned i = 0; i < N; ++i)
    BalancingQueue.push(std::make_pair(i, 0));

  using SortType = std::pair<unsigned, ClusterMapType::iterator>;

  SmallVector<SortType, 64> Sets;
  SmallPtrSet<const GlobalValue *, 32> Visited;

  // To guarantee determinism, we have to sort SCC according to size.
  // When size is the same, use leader's name.
  for (ClusterMapType::iterator I = GVtoClusterMap.begin(),
                                E = GVtoClusterMap.end();
       I != E; ++I)
    if (I->isLeader())
      Sets.push_back(
          std::make_pair(std::distance(GVtoClusterMap.member_begin(I),
                                       GVtoClusterMap.member_end()),
                         I));

  llvm::sort(Sets, [](const SortType &a, const SortType &b) {
    if (a.first == b.first)
      return a.second->getData()->getName() > b.second->getData()->getName();
    else
      return a.first > b.first;
  });

  for (auto &I : Sets) {
    unsigned CurrentClusterID = BalancingQueue.top().first;
    unsigned CurrentClusterSize = BalancingQueue.top().second;
    BalancingQueue.pop();

    LLVM_DEBUG(dbgs() << "Root[" << CurrentClusterID << "] cluster_size("
                      << I.first << ") ----> " << I.second->getData()->getName()
                      << "\n");

    for (ClusterMapType::member_iterator MI =
             GVtoClusterMap.findLeader(I.second);
         MI != GVtoClusterMap.member_end(); ++MI) {
      if (!Visited.insert(*MI).second)
        continue;
      LLVM_DEBUG(dbgs() << "----> " << (*MI)->getName()
                        << ((*MI)->hasLocalLinkage() ? " l " : " e ") << "\n");
      Visited.insert(*MI);
      ClusterIDMap[*MI] = CurrentClusterID;
      CurrentClusterSize++;
    }
    // Add this set size to the number of entries in this cluster.
    BalancingQueue.push(std::make_pair(CurrentClusterID, CurrentClusterSize));
  }
}

static void externalize(GlobalValue *GV) {
  if (GV->hasLocalLinkage()) {
    GV->setLinkage(GlobalValue::ExternalLinkage);
    GV->setVisibility(GlobalValue::HiddenVisibility);
  }

  // Unnamed entities must be named consistently between modules. setName will
  // give a distinct name to each such entity.
  if (!GV->hasName())
    GV->setName("__llvmsplit_unnamed");
}

// Returns whether GV should be in partition (0-based) I of N.
static bool isInPartition(const GlobalValue *GV, unsigned I, unsigned N) {
  if (const GlobalObject *Root = getGVPartitioningRoot(GV))
    GV = Root;

  StringRef Name;
  if (const Comdat *C = GV->getComdat())
    Name = C->getName();
  else
    Name = GV->getName();

  // Partition by MD5 hash. We only need a few bits for evenness as the number
  // of partitions will generally be in the 1-2 figure range; the low 16 bits
  // are enough.
  MD5 H;
  MD5::MD5Result R;
  H.update(Name);
  H.final(R);
  return (R[0] | (R[1] << 8)) % N == I;
}

void llvm::SplitModule(
    Module &M, unsigned N,
    function_ref<void(std::unique_ptr<Module> MPart)> ModuleCallback,
    bool PreserveLocals, bool RoundRobin) {
  if (!PreserveLocals) {
    for (Function &F : M)
      externalize(&F);
    for (GlobalVariable &GV : M.globals())
      externalize(&GV);
    for (GlobalAlias &GA : M.aliases())
      externalize(&GA);
    for (GlobalIFunc &GIF : M.ifuncs())
      externalize(&GIF);
  }

  // This performs splitting without a need for externalization, which might not
  // always be possible.
  ClusterIDMapType ClusterIDMap;
  findPartitions(M, ClusterIDMap, N);

  // Find functions not mapped to modules in ClusterIDMap and count functions
  // per module. Map unmapped functions using round-robin so that they skip
  // being distributed by isInPartition() based on function name hashes below.
  // This provides better uniformity of distribution of functions to modules
  // in some cases - for example when the number of functions equals to N.
  if (RoundRobin) {
    DenseMap<unsigned, unsigned> ModuleFunctionCount;
    SmallVector<const GlobalValue *> UnmappedFunctions;
    for (const auto &F : M.functions()) {
      if (F.isDeclaration() ||
          F.getLinkage() != GlobalValue::LinkageTypes::ExternalLinkage)
        continue;
      auto It = ClusterIDMap.find(&F);
      if (It == ClusterIDMap.end())
        UnmappedFunctions.push_back(&F);
      else
        ++ModuleFunctionCount[It->second];
    }
    BalancingQueueType BalancingQueue(compareClusters);
    for (unsigned I = 0; I < N; ++I) {
      if (auto It = ModuleFunctionCount.find(I);
          It != ModuleFunctionCount.end())
        BalancingQueue.push(*It);
      else
        BalancingQueue.push({I, 0});
    }
    for (const auto *const F : UnmappedFunctions) {
      const unsigned I = BalancingQueue.top().first;
      const unsigned Count = BalancingQueue.top().second;
      BalancingQueue.pop();
      ClusterIDMap.insert({F, I});
      BalancingQueue.push({I, Count + 1});
    }
  }

  // FIXME: We should be able to reuse M as the last partition instead of
  // cloning it. Note that the callers at the moment expect the module to
  // be preserved, so will need some adjustments as well.
  for (unsigned I = 0; I < N; ++I) {
    ValueToValueMapTy VMap;
    std::unique_ptr<Module> MPart(
        CloneModule(M, VMap, [&](const GlobalValue *GV) {
          if (auto It = ClusterIDMap.find(GV); It != ClusterIDMap.end())
            return It->second == I;
          else
            return isInPartition(GV, I, N);
        }));
    if (I != 0)
      MPart->setModuleInlineAsm("");
    ModuleCallback(std::move(MPart));
  }
}
