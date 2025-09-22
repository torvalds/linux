//===- SampleContextTracker.cpp - Context-sensitive Profile Tracker -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the SampleContextTracker used by CSSPGO.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/SampleContextTracker.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/ProfileData/SampleProf.h"
#include <map>
#include <queue>
#include <vector>

using namespace llvm;
using namespace sampleprof;

#define DEBUG_TYPE "sample-context-tracker"

namespace llvm {

ContextTrieNode *ContextTrieNode::getChildContext(const LineLocation &CallSite,
                                                  FunctionId CalleeName) {
  if (CalleeName.empty())
    return getHottestChildContext(CallSite);

  uint64_t Hash = FunctionSamples::getCallSiteHash(CalleeName, CallSite);
  auto It = AllChildContext.find(Hash);
  if (It != AllChildContext.end())
    return &It->second;
  return nullptr;
}

ContextTrieNode *
ContextTrieNode::getHottestChildContext(const LineLocation &CallSite) {
  // CSFDO-TODO: This could be slow, change AllChildContext so we can
  // do point look up for child node by call site alone.
  // Retrieve the child node with max count for indirect call
  ContextTrieNode *ChildNodeRet = nullptr;
  uint64_t MaxCalleeSamples = 0;
  for (auto &It : AllChildContext) {
    ContextTrieNode &ChildNode = It.second;
    if (ChildNode.CallSiteLoc != CallSite)
      continue;
    FunctionSamples *Samples = ChildNode.getFunctionSamples();
    if (!Samples)
      continue;
    if (Samples->getTotalSamples() > MaxCalleeSamples) {
      ChildNodeRet = &ChildNode;
      MaxCalleeSamples = Samples->getTotalSamples();
    }
  }

  return ChildNodeRet;
}

ContextTrieNode &
SampleContextTracker::moveContextSamples(ContextTrieNode &ToNodeParent,
                                         const LineLocation &CallSite,
                                         ContextTrieNode &&NodeToMove) {
  uint64_t Hash =
      FunctionSamples::getCallSiteHash(NodeToMove.getFuncName(), CallSite);
  std::map<uint64_t, ContextTrieNode> &AllChildContext =
      ToNodeParent.getAllChildContext();
  assert(!AllChildContext.count(Hash) && "Node to remove must exist");
  AllChildContext[Hash] = NodeToMove;
  ContextTrieNode &NewNode = AllChildContext[Hash];
  NewNode.setCallSiteLoc(CallSite);

  // Walk through nodes in the moved the subtree, and update
  // FunctionSamples' context as for the context promotion.
  // We also need to set new parant link for all children.
  std::queue<ContextTrieNode *> NodeToUpdate;
  NewNode.setParentContext(&ToNodeParent);
  NodeToUpdate.push(&NewNode);

  while (!NodeToUpdate.empty()) {
    ContextTrieNode *Node = NodeToUpdate.front();
    NodeToUpdate.pop();
    FunctionSamples *FSamples = Node->getFunctionSamples();

    if (FSamples) {
      setContextNode(FSamples, Node);
      FSamples->getContext().setState(SyntheticContext);
    }

    for (auto &It : Node->getAllChildContext()) {
      ContextTrieNode *ChildNode = &It.second;
      ChildNode->setParentContext(Node);
      NodeToUpdate.push(ChildNode);
    }
  }

  return NewNode;
}

void ContextTrieNode::removeChildContext(const LineLocation &CallSite,
                                         FunctionId CalleeName) {
  uint64_t Hash = FunctionSamples::getCallSiteHash(CalleeName, CallSite);
  // Note this essentially calls dtor and destroys that child context
  AllChildContext.erase(Hash);
}

std::map<uint64_t, ContextTrieNode> &ContextTrieNode::getAllChildContext() {
  return AllChildContext;
}

FunctionId ContextTrieNode::getFuncName() const { return FuncName; }

FunctionSamples *ContextTrieNode::getFunctionSamples() const {
  return FuncSamples;
}

void ContextTrieNode::setFunctionSamples(FunctionSamples *FSamples) {
  FuncSamples = FSamples;
}

std::optional<uint32_t> ContextTrieNode::getFunctionSize() const {
  return FuncSize;
}

void ContextTrieNode::addFunctionSize(uint32_t FSize) {
  if (!FuncSize)
    FuncSize = 0;

  FuncSize = *FuncSize + FSize;
}

LineLocation ContextTrieNode::getCallSiteLoc() const { return CallSiteLoc; }

ContextTrieNode *ContextTrieNode::getParentContext() const {
  return ParentContext;
}

void ContextTrieNode::setParentContext(ContextTrieNode *Parent) {
  ParentContext = Parent;
}

void ContextTrieNode::setCallSiteLoc(const LineLocation &Loc) {
  CallSiteLoc = Loc;
}

void ContextTrieNode::dumpNode() {
  dbgs() << "Node: " << FuncName << "\n"
         << "  Callsite: " << CallSiteLoc << "\n"
         << "  Size: " << FuncSize << "\n"
         << "  Children:\n";

  for (auto &It : AllChildContext) {
    dbgs() << "    Node: " << It.second.getFuncName() << "\n";
  }
}

void ContextTrieNode::dumpTree() {
  dbgs() << "Context Profile Tree:\n";
  std::queue<ContextTrieNode *> NodeQueue;
  NodeQueue.push(this);

  while (!NodeQueue.empty()) {
    ContextTrieNode *Node = NodeQueue.front();
    NodeQueue.pop();
    Node->dumpNode();

    for (auto &It : Node->getAllChildContext()) {
      ContextTrieNode *ChildNode = &It.second;
      NodeQueue.push(ChildNode);
    }
  }
}

ContextTrieNode *ContextTrieNode::getOrCreateChildContext(
    const LineLocation &CallSite, FunctionId CalleeName, bool AllowCreate) {
  uint64_t Hash = FunctionSamples::getCallSiteHash(CalleeName, CallSite);
  auto It = AllChildContext.find(Hash);
  if (It != AllChildContext.end()) {
    assert(It->second.getFuncName() == CalleeName &&
           "Hash collision for child context node");
    return &It->second;
  }

  if (!AllowCreate)
    return nullptr;

  AllChildContext[Hash] = ContextTrieNode(this, CalleeName, nullptr, CallSite);
  return &AllChildContext[Hash];
}

// Profiler tracker than manages profiles and its associated context
SampleContextTracker::SampleContextTracker(
    SampleProfileMap &Profiles,
    const DenseMap<uint64_t, StringRef> *GUIDToFuncNameMap)
    : GUIDToFuncNameMap(GUIDToFuncNameMap) {
  for (auto &FuncSample : Profiles) {
    FunctionSamples *FSamples = &FuncSample.second;
    SampleContext Context = FuncSample.second.getContext();
    LLVM_DEBUG(dbgs() << "Tracking Context for function: " << Context.toString()
                      << "\n");
    ContextTrieNode *NewNode = getOrCreateContextPath(Context, true);
    assert(!NewNode->getFunctionSamples() &&
           "New node can't have sample profile");
    NewNode->setFunctionSamples(FSamples);
  }
  populateFuncToCtxtMap();
}

void SampleContextTracker::populateFuncToCtxtMap() {
  for (auto *Node : *this) {
    FunctionSamples *FSamples = Node->getFunctionSamples();
    if (FSamples) {
      FSamples->getContext().setState(RawContext);
      setContextNode(FSamples, Node);
      FuncToCtxtProfiles[Node->getFuncName()].push_back(FSamples);
    }
  }
}

FunctionSamples *
SampleContextTracker::getCalleeContextSamplesFor(const CallBase &Inst,
                                                 StringRef CalleeName) {
  LLVM_DEBUG(dbgs() << "Getting callee context for instr: " << Inst << "\n");
  DILocation *DIL = Inst.getDebugLoc();
  if (!DIL)
    return nullptr;

  CalleeName = FunctionSamples::getCanonicalFnName(CalleeName);
  
  FunctionId FName = getRepInFormat(CalleeName);

  // For indirect call, CalleeName will be empty, in which case the context
  // profile for callee with largest total samples will be returned.
  ContextTrieNode *CalleeContext = getCalleeContextFor(DIL, FName);
  if (CalleeContext) {
    FunctionSamples *FSamples = CalleeContext->getFunctionSamples();
    LLVM_DEBUG(if (FSamples) {
      dbgs() << "  Callee context found: " << getContextString(CalleeContext)
             << "\n";
    });
    return FSamples;
  }

  return nullptr;
}

std::vector<const FunctionSamples *>
SampleContextTracker::getIndirectCalleeContextSamplesFor(
    const DILocation *DIL) {
  std::vector<const FunctionSamples *> R;
  if (!DIL)
    return R;

  ContextTrieNode *CallerNode = getContextFor(DIL);
  LineLocation CallSite = FunctionSamples::getCallSiteIdentifier(DIL);
  for (auto &It : CallerNode->getAllChildContext()) {
    ContextTrieNode &ChildNode = It.second;
    if (ChildNode.getCallSiteLoc() != CallSite)
      continue;
    if (FunctionSamples *CalleeSamples = ChildNode.getFunctionSamples())
      R.push_back(CalleeSamples);
  }

  return R;
}

FunctionSamples *
SampleContextTracker::getContextSamplesFor(const DILocation *DIL) {
  assert(DIL && "Expect non-null location");

  ContextTrieNode *ContextNode = getContextFor(DIL);
  if (!ContextNode)
    return nullptr;

  // We may have inlined callees during pre-LTO compilation, in which case
  // we need to rely on the inline stack from !dbg to mark context profile
  // as inlined, instead of `MarkContextSamplesInlined` during inlining.
  // Sample profile loader walks through all instructions to get profile,
  // which calls this function. So once that is done, all previously inlined
  // context profile should be marked properly.
  FunctionSamples *Samples = ContextNode->getFunctionSamples();
  if (Samples && ContextNode->getParentContext() != &RootContext)
    Samples->getContext().setState(InlinedContext);

  return Samples;
}

FunctionSamples *
SampleContextTracker::getContextSamplesFor(const SampleContext &Context) {
  ContextTrieNode *Node = getContextFor(Context);
  if (!Node)
    return nullptr;

  return Node->getFunctionSamples();
}

SampleContextTracker::ContextSamplesTy &
SampleContextTracker::getAllContextSamplesFor(const Function &Func) {
  StringRef CanonName = FunctionSamples::getCanonicalFnName(Func);
  return FuncToCtxtProfiles[getRepInFormat(CanonName)];
}

SampleContextTracker::ContextSamplesTy &
SampleContextTracker::getAllContextSamplesFor(StringRef Name) {
  return FuncToCtxtProfiles[getRepInFormat(Name)];
}

FunctionSamples *SampleContextTracker::getBaseSamplesFor(const Function &Func,
                                                         bool MergeContext) {
  StringRef CanonName = FunctionSamples::getCanonicalFnName(Func);
  return getBaseSamplesFor(getRepInFormat(CanonName), MergeContext);
}

FunctionSamples *SampleContextTracker::getBaseSamplesFor(FunctionId Name,
                                                         bool MergeContext) {
  LLVM_DEBUG(dbgs() << "Getting base profile for function: " << Name << "\n");

  // Base profile is top-level node (child of root node), so try to retrieve
  // existing top-level node for given function first. If it exists, it could be
  // that we've merged base profile before, or there's actually context-less
  // profile from the input (e.g. due to unreliable stack walking).
  ContextTrieNode *Node = getTopLevelContextNode(Name);
  if (MergeContext) {
    LLVM_DEBUG(dbgs() << "  Merging context profile into base profile: " << Name
                      << "\n");

    // We have profile for function under different contexts,
    // create synthetic base profile and merge context profiles
    // into base profile.
    for (auto *CSamples : FuncToCtxtProfiles[Name]) {
      SampleContext &Context = CSamples->getContext();
      // Skip inlined context profile and also don't re-merge any context
      if (Context.hasState(InlinedContext) || Context.hasState(MergedContext))
        continue;

      ContextTrieNode *FromNode = getContextNodeForProfile(CSamples);
      if (FromNode == Node)
        continue;

      ContextTrieNode &ToNode = promoteMergeContextSamplesTree(*FromNode);
      assert((!Node || Node == &ToNode) && "Expect only one base profile");
      Node = &ToNode;
    }
  }

  // Still no profile even after merge/promotion (if allowed)
  if (!Node)
    return nullptr;

  return Node->getFunctionSamples();
}

void SampleContextTracker::markContextSamplesInlined(
    const FunctionSamples *InlinedSamples) {
  assert(InlinedSamples && "Expect non-null inlined samples");
  LLVM_DEBUG(dbgs() << "Marking context profile as inlined: "
                    << getContextString(*InlinedSamples) << "\n");
  InlinedSamples->getContext().setState(InlinedContext);
}

ContextTrieNode &SampleContextTracker::getRootContext() { return RootContext; }

void SampleContextTracker::promoteMergeContextSamplesTree(
    const Instruction &Inst, FunctionId CalleeName) {
  LLVM_DEBUG(dbgs() << "Promoting and merging context tree for instr: \n"
                    << Inst << "\n");
  // Get the caller context for the call instruction, we don't use callee
  // name from call because there can be context from indirect calls too.
  DILocation *DIL = Inst.getDebugLoc();
  ContextTrieNode *CallerNode = getContextFor(DIL);
  if (!CallerNode)
    return;

  // Get the context that needs to be promoted
  LineLocation CallSite = FunctionSamples::getCallSiteIdentifier(DIL);
  // For indirect call, CalleeName will be empty, in which case we need to
  // promote all non-inlined child context profiles.
  if (CalleeName.empty()) {
    for (auto &It : CallerNode->getAllChildContext()) {
      ContextTrieNode *NodeToPromo = &It.second;
      if (CallSite != NodeToPromo->getCallSiteLoc())
        continue;
      FunctionSamples *FromSamples = NodeToPromo->getFunctionSamples();
      if (FromSamples && FromSamples->getContext().hasState(InlinedContext))
        continue;
      promoteMergeContextSamplesTree(*NodeToPromo);
    }
    return;
  }

  // Get the context for the given callee that needs to be promoted
  ContextTrieNode *NodeToPromo =
      CallerNode->getChildContext(CallSite, CalleeName);
  if (!NodeToPromo)
    return;

  promoteMergeContextSamplesTree(*NodeToPromo);
}

ContextTrieNode &SampleContextTracker::promoteMergeContextSamplesTree(
    ContextTrieNode &NodeToPromo) {
  // Promote the input node to be directly under root. This can happen
  // when we decided to not inline a function under context represented
  // by the input node. The promote and merge is then needed to reflect
  // the context profile in the base (context-less) profile.
  FunctionSamples *FromSamples = NodeToPromo.getFunctionSamples();
  assert(FromSamples && "Shouldn't promote a context without profile");
  (void)FromSamples;  // Unused in release build.

  LLVM_DEBUG(dbgs() << "  Found context tree root to promote: "
                    << getContextString(&NodeToPromo) << "\n");

  assert(!FromSamples->getContext().hasState(InlinedContext) &&
         "Shouldn't promote inlined context profile");
  return promoteMergeContextSamplesTree(NodeToPromo, RootContext);
}

#ifndef NDEBUG
std::string
SampleContextTracker::getContextString(const FunctionSamples &FSamples) const {
  return getContextString(getContextNodeForProfile(&FSamples));
}

std::string
SampleContextTracker::getContextString(ContextTrieNode *Node) const {
  SampleContextFrameVector Res;
  if (Node == &RootContext)
    return std::string();
  Res.emplace_back(Node->getFuncName(), LineLocation(0, 0));

  ContextTrieNode *PreNode = Node;
  Node = Node->getParentContext();
  while (Node && Node != &RootContext) {
    Res.emplace_back(Node->getFuncName(), PreNode->getCallSiteLoc());
    PreNode = Node;
    Node = Node->getParentContext();
  }

  std::reverse(Res.begin(), Res.end());

  return SampleContext::getContextString(Res);
}
#endif

void SampleContextTracker::dump() { RootContext.dumpTree(); }

StringRef SampleContextTracker::getFuncNameFor(ContextTrieNode *Node) const {
  if (!FunctionSamples::UseMD5)
    return Node->getFuncName().stringRef();
  assert(GUIDToFuncNameMap && "GUIDToFuncNameMap needs to be populated first");
  return GUIDToFuncNameMap->lookup(Node->getFuncName().getHashCode());
}

ContextTrieNode *
SampleContextTracker::getContextFor(const SampleContext &Context) {
  return getOrCreateContextPath(Context, false);
}

ContextTrieNode *
SampleContextTracker::getCalleeContextFor(const DILocation *DIL,
                                          FunctionId CalleeName) {
  assert(DIL && "Expect non-null location");

  ContextTrieNode *CallContext = getContextFor(DIL);
  if (!CallContext)
    return nullptr;

  // When CalleeName is empty, the child context profile with max
  // total samples will be returned.
  return CallContext->getChildContext(
      FunctionSamples::getCallSiteIdentifier(DIL), CalleeName);
}

ContextTrieNode *SampleContextTracker::getContextFor(const DILocation *DIL) {
  assert(DIL && "Expect non-null location");
  SmallVector<std::pair<LineLocation, FunctionId>, 10> S;

  // Use C++ linkage name if possible.
  const DILocation *PrevDIL = DIL;
  for (DIL = DIL->getInlinedAt(); DIL; DIL = DIL->getInlinedAt()) {
    StringRef Name = PrevDIL->getScope()->getSubprogram()->getLinkageName();
    if (Name.empty())
      Name = PrevDIL->getScope()->getSubprogram()->getName();
    S.push_back(
        std::make_pair(FunctionSamples::getCallSiteIdentifier(DIL),
                       getRepInFormat(Name)));
    PrevDIL = DIL;
  }

  // Push root node, note that root node like main may only
  // a name, but not linkage name.
  StringRef RootName = PrevDIL->getScope()->getSubprogram()->getLinkageName();
  if (RootName.empty())
    RootName = PrevDIL->getScope()->getSubprogram()->getName();
  S.push_back(std::make_pair(LineLocation(0, 0),
                             getRepInFormat(RootName)));

  ContextTrieNode *ContextNode = &RootContext;
  int I = S.size();
  while (--I >= 0 && ContextNode) {
    LineLocation &CallSite = S[I].first;
    FunctionId CalleeName = S[I].second;
    ContextNode = ContextNode->getChildContext(CallSite, CalleeName);
  }

  if (I < 0)
    return ContextNode;

  return nullptr;
}

ContextTrieNode *
SampleContextTracker::getOrCreateContextPath(const SampleContext &Context,
                                             bool AllowCreate) {
  ContextTrieNode *ContextNode = &RootContext;
  LineLocation CallSiteLoc(0, 0);

  for (const auto &Callsite : Context.getContextFrames()) {
    // Create child node at parent line/disc location
    if (AllowCreate) {
      ContextNode =
          ContextNode->getOrCreateChildContext(CallSiteLoc, Callsite.Func);
    } else {
      ContextNode =
          ContextNode->getChildContext(CallSiteLoc, Callsite.Func);
    }
    CallSiteLoc = Callsite.Location;
  }

  assert((!AllowCreate || ContextNode) &&
         "Node must exist if creation is allowed");
  return ContextNode;
}

ContextTrieNode *
SampleContextTracker::getTopLevelContextNode(FunctionId FName) {
  assert(!FName.empty() && "Top level node query must provide valid name");
  return RootContext.getChildContext(LineLocation(0, 0), FName);
}

ContextTrieNode &
SampleContextTracker::addTopLevelContextNode(FunctionId FName) {
  assert(!getTopLevelContextNode(FName) && "Node to add must not exist");
  return *RootContext.getOrCreateChildContext(LineLocation(0, 0), FName);
}

void SampleContextTracker::mergeContextNode(ContextTrieNode &FromNode,
                                            ContextTrieNode &ToNode) {
  FunctionSamples *FromSamples = FromNode.getFunctionSamples();
  FunctionSamples *ToSamples = ToNode.getFunctionSamples();
  if (FromSamples && ToSamples) {
    // Merge/duplicate FromSamples into ToSamples
    ToSamples->merge(*FromSamples);
    ToSamples->getContext().setState(SyntheticContext);
    FromSamples->getContext().setState(MergedContext);
    if (FromSamples->getContext().hasAttribute(ContextShouldBeInlined))
      ToSamples->getContext().setAttribute(ContextShouldBeInlined);
  } else if (FromSamples) {
    // Transfer FromSamples from FromNode to ToNode
    ToNode.setFunctionSamples(FromSamples);
    setContextNode(FromSamples, &ToNode);
    FromSamples->getContext().setState(SyntheticContext);
  }
}

ContextTrieNode &SampleContextTracker::promoteMergeContextSamplesTree(
    ContextTrieNode &FromNode, ContextTrieNode &ToNodeParent) {

  // Ignore call site location if destination is top level under root
  LineLocation NewCallSiteLoc = LineLocation(0, 0);
  LineLocation OldCallSiteLoc = FromNode.getCallSiteLoc();
  ContextTrieNode &FromNodeParent = *FromNode.getParentContext();
  ContextTrieNode *ToNode = nullptr;
  bool MoveToRoot = (&ToNodeParent == &RootContext);
  if (!MoveToRoot) {
    NewCallSiteLoc = OldCallSiteLoc;
  }

  // Locate destination node, create/move if not existing
  ToNode = ToNodeParent.getChildContext(NewCallSiteLoc, FromNode.getFuncName());
  if (!ToNode) {
    // Do not delete node to move from its parent here because
    // caller is iterating over children of that parent node.
    ToNode =
        &moveContextSamples(ToNodeParent, NewCallSiteLoc, std::move(FromNode));
    LLVM_DEBUG({
      dbgs() << "  Context promoted and merged to: " << getContextString(ToNode)
             << "\n";
    });
  } else {
    // Destination node exists, merge samples for the context tree
    mergeContextNode(FromNode, *ToNode);
    LLVM_DEBUG({
      if (ToNode->getFunctionSamples())
        dbgs() << "  Context promoted and merged to: "
               << getContextString(ToNode) << "\n";
    });

    // Recursively promote and merge children
    for (auto &It : FromNode.getAllChildContext()) {
      ContextTrieNode &FromChildNode = It.second;
      promoteMergeContextSamplesTree(FromChildNode, *ToNode);
    }

    // Remove children once they're all merged
    FromNode.getAllChildContext().clear();
  }

  // For root of subtree, remove itself from old parent too
  if (MoveToRoot)
    FromNodeParent.removeChildContext(OldCallSiteLoc, ToNode->getFuncName());

  return *ToNode;
}

void SampleContextTracker::createContextLessProfileMap(
    SampleProfileMap &ContextLessProfiles) {
  for (auto *Node : *this) {
    FunctionSamples *FProfile = Node->getFunctionSamples();
    // Profile's context can be empty, use ContextNode's func name.
    if (FProfile)
      ContextLessProfiles.create(Node->getFuncName()).merge(*FProfile);
  }
}
} // namespace llvm
