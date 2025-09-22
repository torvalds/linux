//===-- MemoryProfileInfo.cpp - memory profile info ------------------------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains utilities to analyze memory profile information.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/MemoryProfileInfo.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;
using namespace llvm::memprof;

#define DEBUG_TYPE "memory-profile-info"

// Upper bound on lifetime access density (accesses per byte per lifetime sec)
// for marking an allocation cold.
cl::opt<float> MemProfLifetimeAccessDensityColdThreshold(
    "memprof-lifetime-access-density-cold-threshold", cl::init(0.05),
    cl::Hidden,
    cl::desc("The threshold the lifetime access density (accesses per byte per "
             "lifetime sec) must be under to consider an allocation cold"));

// Lower bound on lifetime to mark an allocation cold (in addition to accesses
// per byte per sec above). This is to avoid pessimizing short lived objects.
cl::opt<unsigned> MemProfAveLifetimeColdThreshold(
    "memprof-ave-lifetime-cold-threshold", cl::init(200), cl::Hidden,
    cl::desc("The average lifetime (s) for an allocation to be considered "
             "cold"));

// Lower bound on average lifetime accesses density (total life time access
// density / alloc count) for marking an allocation hot.
cl::opt<unsigned> MemProfMinAveLifetimeAccessDensityHotThreshold(
    "memprof-min-ave-lifetime-access-density-hot-threshold", cl::init(1000),
    cl::Hidden,
    cl::desc("The minimum TotalLifetimeAccessDensity / AllocCount for an "
             "allocation to be considered hot"));

cl::opt<bool> MemProfReportHintedSizes(
    "memprof-report-hinted-sizes", cl::init(false), cl::Hidden,
    cl::desc("Report total allocation sizes of hinted allocations"));

AllocationType llvm::memprof::getAllocType(uint64_t TotalLifetimeAccessDensity,
                                           uint64_t AllocCount,
                                           uint64_t TotalLifetime) {
  // The access densities are multiplied by 100 to hold 2 decimal places of
  // precision, so need to divide by 100.
  if (((float)TotalLifetimeAccessDensity) / AllocCount / 100 <
          MemProfLifetimeAccessDensityColdThreshold
      // Lifetime is expected to be in ms, so convert the threshold to ms.
      && ((float)TotalLifetime) / AllocCount >=
             MemProfAveLifetimeColdThreshold * 1000)
    return AllocationType::Cold;

  // The access densities are multiplied by 100 to hold 2 decimal places of
  // precision, so need to divide by 100.
  if (((float)TotalLifetimeAccessDensity) / AllocCount / 100 >
      MemProfMinAveLifetimeAccessDensityHotThreshold)
    return AllocationType::Hot;

  return AllocationType::NotCold;
}

MDNode *llvm::memprof::buildCallstackMetadata(ArrayRef<uint64_t> CallStack,
                                              LLVMContext &Ctx) {
  std::vector<Metadata *> StackVals;
  for (auto Id : CallStack) {
    auto *StackValMD =
        ValueAsMetadata::get(ConstantInt::get(Type::getInt64Ty(Ctx), Id));
    StackVals.push_back(StackValMD);
  }
  return MDNode::get(Ctx, StackVals);
}

MDNode *llvm::memprof::getMIBStackNode(const MDNode *MIB) {
  assert(MIB->getNumOperands() >= 2);
  // The stack metadata is the first operand of each memprof MIB metadata.
  return cast<MDNode>(MIB->getOperand(0));
}

AllocationType llvm::memprof::getMIBAllocType(const MDNode *MIB) {
  assert(MIB->getNumOperands() >= 2);
  // The allocation type is currently the second operand of each memprof
  // MIB metadata. This will need to change as we add additional allocation
  // types that can be applied based on the allocation profile data.
  auto *MDS = dyn_cast<MDString>(MIB->getOperand(1));
  assert(MDS);
  if (MDS->getString() == "cold") {
    return AllocationType::Cold;
  } else if (MDS->getString() == "hot") {
    return AllocationType::Hot;
  }
  return AllocationType::NotCold;
}

uint64_t llvm::memprof::getMIBTotalSize(const MDNode *MIB) {
  if (MIB->getNumOperands() < 3)
    return 0;
  return mdconst::dyn_extract<ConstantInt>(MIB->getOperand(2))->getZExtValue();
}

std::string llvm::memprof::getAllocTypeAttributeString(AllocationType Type) {
  switch (Type) {
  case AllocationType::NotCold:
    return "notcold";
    break;
  case AllocationType::Cold:
    return "cold";
    break;
  case AllocationType::Hot:
    return "hot";
    break;
  default:
    assert(false && "Unexpected alloc type");
  }
  llvm_unreachable("invalid alloc type");
}

static void addAllocTypeAttribute(LLVMContext &Ctx, CallBase *CI,
                                  AllocationType AllocType) {
  auto AllocTypeString = getAllocTypeAttributeString(AllocType);
  auto A = llvm::Attribute::get(Ctx, "memprof", AllocTypeString);
  CI->addFnAttr(A);
}

bool llvm::memprof::hasSingleAllocType(uint8_t AllocTypes) {
  const unsigned NumAllocTypes = llvm::popcount(AllocTypes);
  assert(NumAllocTypes != 0);
  return NumAllocTypes == 1;
}

void CallStackTrie::addCallStack(AllocationType AllocType,
                                 ArrayRef<uint64_t> StackIds,
                                 uint64_t TotalSize) {
  bool First = true;
  CallStackTrieNode *Curr = nullptr;
  for (auto StackId : StackIds) {
    // If this is the first stack frame, add or update alloc node.
    if (First) {
      First = false;
      if (Alloc) {
        assert(AllocStackId == StackId);
        Alloc->AllocTypes |= static_cast<uint8_t>(AllocType);
        Alloc->TotalSize += TotalSize;
      } else {
        AllocStackId = StackId;
        Alloc = new CallStackTrieNode(AllocType, TotalSize);
      }
      Curr = Alloc;
      continue;
    }
    // Update existing caller node if it exists.
    auto Next = Curr->Callers.find(StackId);
    if (Next != Curr->Callers.end()) {
      Curr = Next->second;
      Curr->AllocTypes |= static_cast<uint8_t>(AllocType);
      Curr->TotalSize += TotalSize;
      continue;
    }
    // Otherwise add a new caller node.
    auto *New = new CallStackTrieNode(AllocType, TotalSize);
    Curr->Callers[StackId] = New;
    Curr = New;
  }
  assert(Curr);
}

void CallStackTrie::addCallStack(MDNode *MIB) {
  MDNode *StackMD = getMIBStackNode(MIB);
  assert(StackMD);
  std::vector<uint64_t> CallStack;
  CallStack.reserve(StackMD->getNumOperands());
  for (const auto &MIBStackIter : StackMD->operands()) {
    auto *StackId = mdconst::dyn_extract<ConstantInt>(MIBStackIter);
    assert(StackId);
    CallStack.push_back(StackId->getZExtValue());
  }
  addCallStack(getMIBAllocType(MIB), CallStack, getMIBTotalSize(MIB));
}

static MDNode *createMIBNode(LLVMContext &Ctx,
                             std::vector<uint64_t> &MIBCallStack,
                             AllocationType AllocType, uint64_t TotalSize) {
  std::vector<Metadata *> MIBPayload(
      {buildCallstackMetadata(MIBCallStack, Ctx)});
  MIBPayload.push_back(
      MDString::get(Ctx, getAllocTypeAttributeString(AllocType)));
  if (TotalSize)
    MIBPayload.push_back(ValueAsMetadata::get(
        ConstantInt::get(Type::getInt64Ty(Ctx), TotalSize)));
  return MDNode::get(Ctx, MIBPayload);
}

// Recursive helper to trim contexts and create metadata nodes.
// Caller should have pushed Node's loc to MIBCallStack. Doing this in the
// caller makes it simpler to handle the many early returns in this method.
bool CallStackTrie::buildMIBNodes(CallStackTrieNode *Node, LLVMContext &Ctx,
                                  std::vector<uint64_t> &MIBCallStack,
                                  std::vector<Metadata *> &MIBNodes,
                                  bool CalleeHasAmbiguousCallerContext) {
  // Trim context below the first node in a prefix with a single alloc type.
  // Add an MIB record for the current call stack prefix.
  if (hasSingleAllocType(Node->AllocTypes)) {
    MIBNodes.push_back(createMIBNode(
        Ctx, MIBCallStack, (AllocationType)Node->AllocTypes, Node->TotalSize));
    return true;
  }

  // We don't have a single allocation for all the contexts sharing this prefix,
  // so recursively descend into callers in trie.
  if (!Node->Callers.empty()) {
    bool NodeHasAmbiguousCallerContext = Node->Callers.size() > 1;
    bool AddedMIBNodesForAllCallerContexts = true;
    for (auto &Caller : Node->Callers) {
      MIBCallStack.push_back(Caller.first);
      AddedMIBNodesForAllCallerContexts &=
          buildMIBNodes(Caller.second, Ctx, MIBCallStack, MIBNodes,
                        NodeHasAmbiguousCallerContext);
      // Remove Caller.
      MIBCallStack.pop_back();
    }
    if (AddedMIBNodesForAllCallerContexts)
      return true;
    // We expect that the callers should be forced to add MIBs to disambiguate
    // the context in this case (see below).
    assert(!NodeHasAmbiguousCallerContext);
  }

  // If we reached here, then this node does not have a single allocation type,
  // and we didn't add metadata for a longer call stack prefix including any of
  // Node's callers. That means we never hit a single allocation type along all
  // call stacks with this prefix. This can happen due to recursion collapsing
  // or the stack being deeper than tracked by the profiler runtime, leading to
  // contexts with different allocation types being merged. In that case, we
  // trim the context just below the deepest context split, which is this
  // node if the callee has an ambiguous caller context (multiple callers),
  // since the recursive calls above returned false. Conservatively give it
  // non-cold allocation type.
  if (!CalleeHasAmbiguousCallerContext)
    return false;
  MIBNodes.push_back(createMIBNode(Ctx, MIBCallStack, AllocationType::NotCold,
                                   Node->TotalSize));
  return true;
}

// Build and attach the minimal necessary MIB metadata. If the alloc has a
// single allocation type, add a function attribute instead. Returns true if
// memprof metadata attached, false if not (attribute added).
bool CallStackTrie::buildAndAttachMIBMetadata(CallBase *CI) {
  auto &Ctx = CI->getContext();
  if (hasSingleAllocType(Alloc->AllocTypes)) {
    addAllocTypeAttribute(Ctx, CI, (AllocationType)Alloc->AllocTypes);
    if (MemProfReportHintedSizes) {
      assert(Alloc->TotalSize);
      errs() << "Total size for allocation with location hash " << AllocStackId
             << " and single alloc type "
             << getAllocTypeAttributeString((AllocationType)Alloc->AllocTypes)
             << ": " << Alloc->TotalSize << "\n";
    }
    return false;
  }
  std::vector<uint64_t> MIBCallStack;
  MIBCallStack.push_back(AllocStackId);
  std::vector<Metadata *> MIBNodes;
  assert(!Alloc->Callers.empty() && "addCallStack has not been called yet");
  // The last parameter is meant to say whether the callee of the given node
  // has more than one caller. Here the node being passed in is the alloc
  // and it has no callees. So it's false.
  if (buildMIBNodes(Alloc, Ctx, MIBCallStack, MIBNodes, false)) {
    assert(MIBCallStack.size() == 1 &&
           "Should only be left with Alloc's location in stack");
    CI->setMetadata(LLVMContext::MD_memprof, MDNode::get(Ctx, MIBNodes));
    return true;
  }
  // If there exists corner case that CallStackTrie has one chain to leaf
  // and all node in the chain have multi alloc type, conservatively give
  // it non-cold allocation type.
  // FIXME: Avoid this case before memory profile created.
  addAllocTypeAttribute(Ctx, CI, AllocationType::NotCold);
  return false;
}

template <>
CallStack<MDNode, MDNode::op_iterator>::CallStackIterator::CallStackIterator(
    const MDNode *N, bool End)
    : N(N) {
  if (!N)
    return;
  Iter = End ? N->op_end() : N->op_begin();
}

template <>
uint64_t
CallStack<MDNode, MDNode::op_iterator>::CallStackIterator::operator*() {
  assert(Iter != N->op_end());
  ConstantInt *StackIdCInt = mdconst::dyn_extract<ConstantInt>(*Iter);
  assert(StackIdCInt);
  return StackIdCInt->getZExtValue();
}

template <> uint64_t CallStack<MDNode, MDNode::op_iterator>::back() const {
  assert(N);
  return mdconst::dyn_extract<ConstantInt>(N->operands().back())
      ->getZExtValue();
}
