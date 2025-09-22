//===-- ProfiledCallGraph.h - Profiled Call Graph ----------------- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_PROFILEDCALLGRAPH_H
#define LLVM_TRANSFORMS_IPO_PROFILEDCALLGRAPH_H

#include "llvm/ADT/GraphTraits.h"
#include "llvm/ProfileData/SampleProf.h"
#include "llvm/ProfileData/SampleProfReader.h"
#include "llvm/Transforms/IPO/SampleContextTracker.h"
#include <queue>
#include <set>

namespace llvm {
namespace sampleprof {

struct ProfiledCallGraphNode;

struct ProfiledCallGraphEdge {
  ProfiledCallGraphEdge(ProfiledCallGraphNode *Source,
                        ProfiledCallGraphNode *Target, uint64_t Weight)
      : Source(Source), Target(Target), Weight(Weight) {}
  ProfiledCallGraphNode *Source;
  ProfiledCallGraphNode *Target;
  uint64_t Weight;

  // The call destination is the only important data here,
  // allow to transparently unwrap into it.
  operator ProfiledCallGraphNode *() const { return Target; }
};

struct ProfiledCallGraphNode {

  // Sort edges by callee names only since all edges to be compared are from
  // same caller. Edge weights are not considered either because for the same
  // callee only the edge with the largest weight is added to the edge set.
  struct ProfiledCallGraphEdgeComparer {
    bool operator()(const ProfiledCallGraphEdge &L,
                    const ProfiledCallGraphEdge &R) const {
      return L.Target->Name < R.Target->Name;
    }
  };

  using edge = ProfiledCallGraphEdge;
  using edges = std::set<edge, ProfiledCallGraphEdgeComparer>;
  using iterator = edges::iterator;
  using const_iterator = edges::const_iterator;
  
  ProfiledCallGraphNode(FunctionId FName = FunctionId()) : Name(FName)
  {}
  
  FunctionId Name;
  edges Edges;
};

class ProfiledCallGraph {
public:
  using iterator = ProfiledCallGraphNode::iterator;

  // Constructor for non-CS profile.
  ProfiledCallGraph(SampleProfileMap &ProfileMap,
                    uint64_t IgnoreColdCallThreshold = 0) {
    assert(!FunctionSamples::ProfileIsCS &&
           "CS flat profile is not handled here");
    for (const auto &Samples : ProfileMap) {
      addProfiledCalls(Samples.second);
    }

    // Trim edges with weight up to `IgnoreColdCallThreshold`. This aims
    // for a more stable call graph with "determinstic" edges from run to run.
    trimColdEges(IgnoreColdCallThreshold);
  }

  // Constructor for CS profile.
  ProfiledCallGraph(SampleContextTracker &ContextTracker,
                    uint64_t IgnoreColdCallThreshold = 0) {
    // BFS traverse the context profile trie to add call edges for calls shown
    // in context.
    std::queue<ContextTrieNode *> Queue;
    for (auto &Child : ContextTracker.getRootContext().getAllChildContext()) {
      ContextTrieNode *Callee = &Child.second;
      addProfiledFunction(Callee->getFuncName());
      Queue.push(Callee);
    }

    while (!Queue.empty()) {
      ContextTrieNode *Caller = Queue.front();
      Queue.pop();
      FunctionSamples *CallerSamples = Caller->getFunctionSamples();

      // Add calls for context.
      // Note that callsite target samples are completely ignored since they can
      // conflict with the context edges, which are formed by context
      // compression during profile generation, for cyclic SCCs. This may
      // further result in an SCC order incompatible with the purely
      // context-based one, which may in turn block context-based inlining.
      for (auto &Child : Caller->getAllChildContext()) {
        ContextTrieNode *Callee = &Child.second;
        addProfiledFunction(Callee->getFuncName());
        Queue.push(Callee);

        // Fetch edge weight from the profile.
        uint64_t Weight;
        FunctionSamples *CalleeSamples = Callee->getFunctionSamples();
        if (!CalleeSamples || !CallerSamples) {
          Weight = 0;
        } else {
          uint64_t CalleeEntryCount = CalleeSamples->getHeadSamplesEstimate();
          uint64_t CallsiteCount = 0;
          LineLocation Callsite = Callee->getCallSiteLoc();
          if (auto CallTargets = CallerSamples->findCallTargetMapAt(Callsite)) {
            auto It = CallTargets->find(CalleeSamples->getFunction());
            if (It != CallTargets->end())
              CallsiteCount = It->second;
          }
          Weight = std::max(CallsiteCount, CalleeEntryCount);
        }

        addProfiledCall(Caller->getFuncName(), Callee->getFuncName(), Weight);
      }
    }

    // Trim edges with weight up to `IgnoreColdCallThreshold`. This aims
    // for a more stable call graph with "determinstic" edges from run to run.
    trimColdEges(IgnoreColdCallThreshold);
  }

  iterator begin() { return Root.Edges.begin(); }
  iterator end() { return Root.Edges.end(); }
  ProfiledCallGraphNode *getEntryNode() { return &Root; }
  
  void addProfiledFunction(FunctionId Name) {
    if (!ProfiledFunctions.count(Name)) {
      // Link to synthetic root to make sure every node is reachable
      // from root. This does not affect SCC order.
      // Store the pointer of the node because the map can be rehashed.
      auto &Node =
          ProfiledCallGraphNodeList.emplace_back(ProfiledCallGraphNode(Name));
      ProfiledFunctions[Name] = &Node;
      Root.Edges.emplace(&Root, ProfiledFunctions[Name], 0);
    }
  }

private:
  void addProfiledCall(FunctionId CallerName, FunctionId CalleeName,
                       uint64_t Weight = 0) {
    assert(ProfiledFunctions.count(CallerName));
    auto CalleeIt = ProfiledFunctions.find(CalleeName);
    if (CalleeIt == ProfiledFunctions.end())
      return;
    ProfiledCallGraphEdge Edge(ProfiledFunctions[CallerName],
                               CalleeIt->second, Weight);
    auto &Edges = ProfiledFunctions[CallerName]->Edges;
    auto EdgeIt = Edges.find(Edge);
    if (EdgeIt == Edges.end()) {
      Edges.insert(Edge);
    } else {
      // Accumulate weight to the existing edge.
      Edge.Weight += EdgeIt->Weight;
      Edges.erase(EdgeIt);
      Edges.insert(Edge);
    }
  }

  void addProfiledCalls(const FunctionSamples &Samples) {
    addProfiledFunction(Samples.getFunction());

    for (const auto &Sample : Samples.getBodySamples()) {
      for (const auto &[Target, Frequency] : Sample.second.getCallTargets()) {
        addProfiledFunction(Target);
        addProfiledCall(Samples.getFunction(), Target, Frequency);
      }
    }

    for (const auto &CallsiteSamples : Samples.getCallsiteSamples()) {
      for (const auto &InlinedSamples : CallsiteSamples.second) {
        addProfiledFunction(InlinedSamples.first);
        addProfiledCall(Samples.getFunction(), InlinedSamples.first,
                        InlinedSamples.second.getHeadSamplesEstimate());
        addProfiledCalls(InlinedSamples.second);
      }
    }
  }

  // Trim edges with weight up to `Threshold`. Do not trim anything if
  // `Threshold` is zero.
  void trimColdEges(uint64_t Threshold = 0) {
    if (!Threshold)
      return;

    for (auto &Node : ProfiledFunctions) {
      auto &Edges = Node.second->Edges;
      auto I = Edges.begin();
      while (I != Edges.end()) {
        if (I->Weight <= Threshold)
          I = Edges.erase(I);
        else
          I++;
      }
    }
  }

  ProfiledCallGraphNode Root;
  // backing buffer for ProfiledCallGraphNodes.
  std::list<ProfiledCallGraphNode> ProfiledCallGraphNodeList;
  HashKeyMap<llvm::DenseMap, FunctionId, ProfiledCallGraphNode*>
      ProfiledFunctions;
};

} // end namespace sampleprof

template <> struct GraphTraits<ProfiledCallGraphNode *> {
  using NodeType = ProfiledCallGraphNode;
  using NodeRef = ProfiledCallGraphNode *;
  using EdgeType = NodeType::edge;
  using ChildIteratorType = NodeType::const_iterator;

  static NodeRef getEntryNode(NodeRef PCGN) { return PCGN; }
  static ChildIteratorType child_begin(NodeRef N) { return N->Edges.begin(); }
  static ChildIteratorType child_end(NodeRef N) { return N->Edges.end(); }
};

template <>
struct GraphTraits<ProfiledCallGraph *>
    : public GraphTraits<ProfiledCallGraphNode *> {
  static NodeRef getEntryNode(ProfiledCallGraph *PCG) {
    return PCG->getEntryNode();
  }

  static ChildIteratorType nodes_begin(ProfiledCallGraph *PCG) {
    return PCG->begin();
  }

  static ChildIteratorType nodes_end(ProfiledCallGraph *PCG) {
    return PCG->end();
  }
};

} // end namespace llvm

#endif
