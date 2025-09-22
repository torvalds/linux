//===- Transforms/IPO/SampleContextTracker.h --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file provides the interface for context-sensitive profile tracker used
/// by CSSPGO.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_SAMPLECONTEXTTRACKER_H
#define LLVM_TRANSFORMS_IPO_SAMPLECONTEXTTRACKER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ProfileData/SampleProf.h"
#include <map>
#include <queue>
#include <vector>

namespace llvm {
class CallBase;
class DILocation;
class Function;
class Instruction;

// Internal trie tree representation used for tracking context tree and sample
// profiles. The path from root node to a given node represents the context of
// that nodes' profile.
class ContextTrieNode {
public:
  ContextTrieNode(ContextTrieNode *Parent = nullptr,
                  FunctionId FName = FunctionId(),
                  FunctionSamples *FSamples = nullptr,
                  LineLocation CallLoc = {0, 0})
      : ParentContext(Parent), FuncName(FName), FuncSamples(FSamples),
        CallSiteLoc(CallLoc){};
  ContextTrieNode *getChildContext(const LineLocation &CallSite,
                                   FunctionId ChildName);
  ContextTrieNode *getHottestChildContext(const LineLocation &CallSite);
  ContextTrieNode *getOrCreateChildContext(const LineLocation &CallSite,
                                           FunctionId ChildName,
                                           bool AllowCreate = true);
  void removeChildContext(const LineLocation &CallSite, FunctionId ChildName);
  std::map<uint64_t, ContextTrieNode> &getAllChildContext();
  FunctionId getFuncName() const;
  FunctionSamples *getFunctionSamples() const;
  void setFunctionSamples(FunctionSamples *FSamples);
  std::optional<uint32_t> getFunctionSize() const;
  void addFunctionSize(uint32_t FSize);
  LineLocation getCallSiteLoc() const;
  ContextTrieNode *getParentContext() const;
  void setParentContext(ContextTrieNode *Parent);
  void setCallSiteLoc(const LineLocation &Loc);
  void dumpNode();
  void dumpTree();

private:
  // Map line+discriminator location to child context
  std::map<uint64_t, ContextTrieNode> AllChildContext;

  // Link to parent context node
  ContextTrieNode *ParentContext;

  // Function name for current context
  FunctionId FuncName;

  // Function Samples for current context
  FunctionSamples *FuncSamples;

  // Function size for current context
  std::optional<uint32_t> FuncSize;

  // Callsite location in parent context
  LineLocation CallSiteLoc;
};

// Profile tracker that manages profiles and its associated context. It
// provides interfaces used by sample profile loader to query context profile or
// base profile for given function or location; it also manages context tree
// manipulation that is needed to accommodate inline decisions so we have
// accurate post-inline profile for functions. Internally context profiles
// are organized in a trie, with each node representing profile for specific
// calling context and the context is identified by path from root to the node.
class SampleContextTracker {
public:
  using ContextSamplesTy = std::vector<FunctionSamples *>;

  SampleContextTracker() = default;
  SampleContextTracker(SampleProfileMap &Profiles,
                       const DenseMap<uint64_t, StringRef> *GUIDToFuncNameMap);
  // Populate the FuncToCtxtProfiles map after the trie is built.
  void populateFuncToCtxtMap();
  // Query context profile for a specific callee with given name at a given
  // call-site. The full context is identified by location of call instruction.
  FunctionSamples *getCalleeContextSamplesFor(const CallBase &Inst,
                                              StringRef CalleeName);
  // Get samples for indirect call targets for call site at given location.
  std::vector<const FunctionSamples *>
  getIndirectCalleeContextSamplesFor(const DILocation *DIL);
  // Query context profile for a given location. The full context
  // is identified by input DILocation.
  FunctionSamples *getContextSamplesFor(const DILocation *DIL);
  // Query context profile for a given sample contxt of a function.
  FunctionSamples *getContextSamplesFor(const SampleContext &Context);
  // Get all context profile for given function.
  ContextSamplesTy &getAllContextSamplesFor(const Function &Func);
  ContextSamplesTy &getAllContextSamplesFor(StringRef Name);
  ContextTrieNode *getOrCreateContextPath(const SampleContext &Context,
                                          bool AllowCreate);
  // Query base profile for a given function. A base profile is a merged view
  // of all context profiles for contexts that are not inlined.
  FunctionSamples *getBaseSamplesFor(const Function &Func,
                                     bool MergeContext = true);
  // Query base profile for a given function by name.
  FunctionSamples *getBaseSamplesFor(FunctionId Name,
                                     bool MergeContext = true);
  // Retrieve the context trie node for given profile context
  ContextTrieNode *getContextFor(const SampleContext &Context);
  // Get real function name for a given trie node.
  StringRef getFuncNameFor(ContextTrieNode *Node) const;
  // Mark a context profile as inlined when function is inlined.
  // This makes sure that inlined context profile will be excluded in
  // function's base profile.
  void markContextSamplesInlined(const FunctionSamples *InlinedSamples);
  ContextTrieNode &getRootContext();
  void promoteMergeContextSamplesTree(const Instruction &Inst,
                                      FunctionId CalleeName);

  // Create a merged conext-less profile map.
  void createContextLessProfileMap(SampleProfileMap &ContextLessProfiles);
  ContextTrieNode *
  getContextNodeForProfile(const FunctionSamples *FSamples) const {
    auto I = ProfileToNodeMap.find(FSamples);
    if (I == ProfileToNodeMap.end())
      return nullptr;
    return I->second;
  }
  HashKeyMap<std::unordered_map, FunctionId, ContextSamplesTy>
      &getFuncToCtxtProfiles() {
    return FuncToCtxtProfiles;
  }

  class Iterator : public llvm::iterator_facade_base<
                       Iterator, std::forward_iterator_tag, ContextTrieNode *,
                       std::ptrdiff_t, ContextTrieNode **, ContextTrieNode *> {
    std::queue<ContextTrieNode *> NodeQueue;

  public:
    explicit Iterator() = default;
    explicit Iterator(ContextTrieNode *Node) { NodeQueue.push(Node); }
    Iterator &operator++() {
      assert(!NodeQueue.empty() && "Iterator already at the end");
      ContextTrieNode *Node = NodeQueue.front();
      NodeQueue.pop();
      for (auto &It : Node->getAllChildContext())
        NodeQueue.push(&It.second);
      return *this;
    }

    bool operator==(const Iterator &Other) const {
      if (NodeQueue.empty() && Other.NodeQueue.empty())
        return true;
      if (NodeQueue.empty() || Other.NodeQueue.empty())
        return false;
      return NodeQueue.front() == Other.NodeQueue.front();
    }

    ContextTrieNode *operator*() const {
      assert(!NodeQueue.empty() && "Invalid access to end iterator");
      return NodeQueue.front();
    }
  };

  Iterator begin() { return Iterator(&RootContext); }
  Iterator end() { return Iterator(); }

#ifndef NDEBUG
  // Get a context string from root to current node.
  std::string getContextString(const FunctionSamples &FSamples) const;
  std::string getContextString(ContextTrieNode *Node) const;
#endif
  // Dump the internal context profile trie.
  void dump();

private:
  ContextTrieNode *getContextFor(const DILocation *DIL);
  ContextTrieNode *getCalleeContextFor(const DILocation *DIL,
                                       FunctionId CalleeName);
  ContextTrieNode *getTopLevelContextNode(FunctionId FName);
  ContextTrieNode &addTopLevelContextNode(FunctionId FName);
  ContextTrieNode &promoteMergeContextSamplesTree(ContextTrieNode &NodeToPromo);
  void mergeContextNode(ContextTrieNode &FromNode, ContextTrieNode &ToNode);
  ContextTrieNode &
  promoteMergeContextSamplesTree(ContextTrieNode &FromNode,
                                 ContextTrieNode &ToNodeParent);
  ContextTrieNode &moveContextSamples(ContextTrieNode &ToNodeParent,
                                      const LineLocation &CallSite,
                                      ContextTrieNode &&NodeToMove);
  void setContextNode(const FunctionSamples *FSample, ContextTrieNode *Node) {
    ProfileToNodeMap[FSample] = Node;
  }
  // Map from function name to context profiles (excluding base profile)
  HashKeyMap<std::unordered_map, FunctionId, ContextSamplesTy>
      FuncToCtxtProfiles;

  // Map from current FunctionSample to the belonged context trie.
  std::unordered_map<const FunctionSamples *, ContextTrieNode *>
      ProfileToNodeMap;

  // Map from function guid to real function names. Only used in md5 mode.
  const DenseMap<uint64_t, StringRef> *GUIDToFuncNameMap;

  // Root node for context trie tree
  ContextTrieNode RootContext;
};

} // end namespace llvm
#endif // LLVM_TRANSFORMS_IPO_SAMPLECONTEXTTRACKER_H
