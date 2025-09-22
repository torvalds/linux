//===- ShadowStackGCLowering.cpp - Custom lowering for shadow-stack gc ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the custom lowering code required by the shadow-stack GC
// strategy.
//
// This pass implements the code transformation described in this paper:
//   "Accurate Garbage Collection in an Uncooperative Environment"
//   Fergus Henderson, ISMM, 2002
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/ShadowStackGCLowering.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/CodeGen/GCMetadata.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Transforms/Utils/EscapeEnumerator.h"
#include <cassert>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "shadow-stack-gc-lowering"

namespace {

class ShadowStackGCLoweringImpl {
  /// RootChain - This is the global linked-list that contains the chain of GC
  /// roots.
  GlobalVariable *Head = nullptr;

  /// StackEntryTy - Abstract type of a link in the shadow stack.
  StructType *StackEntryTy = nullptr;
  StructType *FrameMapTy = nullptr;

  /// Roots - GC roots in the current function. Each is a pair of the
  /// intrinsic call and its corresponding alloca.
  std::vector<std::pair<CallInst *, AllocaInst *>> Roots;

public:
  ShadowStackGCLoweringImpl() = default;

  bool doInitialization(Module &M);
  bool runOnFunction(Function &F, DomTreeUpdater *DTU);

private:
  bool IsNullValue(Value *V);
  Constant *GetFrameMap(Function &F);
  Type *GetConcreteStackEntryType(Function &F);
  void CollectRoots(Function &F);

  static GetElementPtrInst *CreateGEP(LLVMContext &Context, IRBuilder<> &B,
                                      Type *Ty, Value *BasePtr, int Idx1,
                                      const char *Name);
  static GetElementPtrInst *CreateGEP(LLVMContext &Context, IRBuilder<> &B,
                                      Type *Ty, Value *BasePtr, int Idx1, int Idx2,
                                      const char *Name);
};

class ShadowStackGCLowering : public FunctionPass {
  ShadowStackGCLoweringImpl Impl;

public:
  static char ID;

  ShadowStackGCLowering();

  bool doInitialization(Module &M) override { return Impl.doInitialization(M); }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addPreserved<DominatorTreeWrapperPass>();
  }
  bool runOnFunction(Function &F) override {
    std::optional<DomTreeUpdater> DTU;
    if (auto *DTWP = getAnalysisIfAvailable<DominatorTreeWrapperPass>())
      DTU.emplace(DTWP->getDomTree(), DomTreeUpdater::UpdateStrategy::Lazy);
    return Impl.runOnFunction(F, DTU ? &*DTU : nullptr);
  }
};

} // end anonymous namespace

PreservedAnalyses ShadowStackGCLoweringPass::run(Module &M,
                                                 ModuleAnalysisManager &MAM) {
  auto &Map = MAM.getResult<CollectorMetadataAnalysis>(M);
  if (Map.StrategyMap.contains("shadow-stack"))
    return PreservedAnalyses::all();

  ShadowStackGCLoweringImpl Impl;
  bool Changed = Impl.doInitialization(M);
  for (auto &F : M) {
    auto &FAM =
        MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
    auto *DT = FAM.getCachedResult<DominatorTreeAnalysis>(F);
    DomTreeUpdater DTU(DT, DomTreeUpdater::UpdateStrategy::Lazy);
    Changed |= Impl.runOnFunction(F, DT ? &DTU : nullptr);
  }

  if (!Changed)
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserve<DominatorTreeAnalysis>();
  return PA;
}

char ShadowStackGCLowering::ID = 0;
char &llvm::ShadowStackGCLoweringID = ShadowStackGCLowering::ID;

INITIALIZE_PASS_BEGIN(ShadowStackGCLowering, DEBUG_TYPE,
                      "Shadow Stack GC Lowering", false, false)
INITIALIZE_PASS_DEPENDENCY(GCModuleInfo)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_END(ShadowStackGCLowering, DEBUG_TYPE,
                    "Shadow Stack GC Lowering", false, false)

FunctionPass *llvm::createShadowStackGCLoweringPass() { return new ShadowStackGCLowering(); }

ShadowStackGCLowering::ShadowStackGCLowering() : FunctionPass(ID) {
  initializeShadowStackGCLoweringPass(*PassRegistry::getPassRegistry());
}

Constant *ShadowStackGCLoweringImpl::GetFrameMap(Function &F) {
  // doInitialization creates the abstract type of this value.
  Type *VoidPtr = PointerType::getUnqual(F.getContext());

  // Truncate the ShadowStackDescriptor if some metadata is null.
  unsigned NumMeta = 0;
  SmallVector<Constant *, 16> Metadata;
  for (unsigned I = 0; I != Roots.size(); ++I) {
    Constant *C = cast<Constant>(Roots[I].first->getArgOperand(1));
    if (!C->isNullValue())
      NumMeta = I + 1;
    Metadata.push_back(C);
  }
  Metadata.resize(NumMeta);

  Type *Int32Ty = Type::getInt32Ty(F.getContext());

  Constant *BaseElts[] = {
      ConstantInt::get(Int32Ty, Roots.size(), false),
      ConstantInt::get(Int32Ty, NumMeta, false),
  };

  Constant *DescriptorElts[] = {
      ConstantStruct::get(FrameMapTy, BaseElts),
      ConstantArray::get(ArrayType::get(VoidPtr, NumMeta), Metadata)};

  Type *EltTys[] = {DescriptorElts[0]->getType(), DescriptorElts[1]->getType()};
  StructType *STy = StructType::create(EltTys, "gc_map." + utostr(NumMeta));

  Constant *FrameMap = ConstantStruct::get(STy, DescriptorElts);

  // FIXME: Is this actually dangerous as WritingAnLLVMPass.html claims? Seems
  //        that, short of multithreaded LLVM, it should be safe; all that is
  //        necessary is that a simple Module::iterator loop not be invalidated.
  //        Appending to the GlobalVariable list is safe in that sense.
  //
  //        All of the output passes emit globals last. The ExecutionEngine
  //        explicitly supports adding globals to the module after
  //        initialization.
  //
  //        Still, if it isn't deemed acceptable, then this transformation needs
  //        to be a ModulePass (which means it cannot be in the 'llc' pipeline
  //        (which uses a FunctionPassManager (which segfaults (not asserts) if
  //        provided a ModulePass))).
  Constant *GV = new GlobalVariable(*F.getParent(), FrameMap->getType(), true,
                                    GlobalVariable::InternalLinkage, FrameMap,
                                    "__gc_" + F.getName());

  Constant *GEPIndices[2] = {
      ConstantInt::get(Type::getInt32Ty(F.getContext()), 0),
      ConstantInt::get(Type::getInt32Ty(F.getContext()), 0)};
  return ConstantExpr::getGetElementPtr(FrameMap->getType(), GV, GEPIndices);
}

Type *ShadowStackGCLoweringImpl::GetConcreteStackEntryType(Function &F) {
  // doInitialization creates the generic version of this type.
  std::vector<Type *> EltTys;
  EltTys.push_back(StackEntryTy);
  for (const std::pair<CallInst *, AllocaInst *> &Root : Roots)
    EltTys.push_back(Root.second->getAllocatedType());

  return StructType::create(EltTys, ("gc_stackentry." + F.getName()).str());
}

/// doInitialization - If this module uses the GC intrinsics, find them now. If
/// not, exit fast.
bool ShadowStackGCLoweringImpl::doInitialization(Module &M) {
  bool Active = false;
  for (Function &F : M) {
    if (F.hasGC() && F.getGC() == "shadow-stack") {
      Active = true;
      break;
    }
  }
  if (!Active)
    return false;

  // struct FrameMap {
  //   int32_t NumRoots; // Number of roots in stack frame.
  //   int32_t NumMeta;  // Number of metadata descriptors. May be < NumRoots.
  //   void *Meta[];     // May be absent for roots without metadata.
  // };
  std::vector<Type *> EltTys;
  // 32 bits is ok up to a 32GB stack frame. :)
  EltTys.push_back(Type::getInt32Ty(M.getContext()));
  // Specifies length of variable length array.
  EltTys.push_back(Type::getInt32Ty(M.getContext()));
  FrameMapTy = StructType::create(EltTys, "gc_map");
  PointerType *FrameMapPtrTy = PointerType::getUnqual(FrameMapTy);

  // struct StackEntry {
  //   ShadowStackEntry *Next; // Caller's stack entry.
  //   FrameMap *Map;          // Pointer to constant FrameMap.
  //   void *Roots[];          // Stack roots (in-place array, so we pretend).
  // };

  StackEntryTy = StructType::create(M.getContext(), "gc_stackentry");

  EltTys.clear();
  EltTys.push_back(PointerType::getUnqual(StackEntryTy));
  EltTys.push_back(FrameMapPtrTy);
  StackEntryTy->setBody(EltTys);
  PointerType *StackEntryPtrTy = PointerType::getUnqual(StackEntryTy);

  // Get the root chain if it already exists.
  Head = M.getGlobalVariable("llvm_gc_root_chain");
  if (!Head) {
    // If the root chain does not exist, insert a new one with linkonce
    // linkage!
    Head = new GlobalVariable(
        M, StackEntryPtrTy, false, GlobalValue::LinkOnceAnyLinkage,
        Constant::getNullValue(StackEntryPtrTy), "llvm_gc_root_chain");
  } else if (Head->hasExternalLinkage() && Head->isDeclaration()) {
    Head->setInitializer(Constant::getNullValue(StackEntryPtrTy));
    Head->setLinkage(GlobalValue::LinkOnceAnyLinkage);
  }

  return true;
}

bool ShadowStackGCLoweringImpl::IsNullValue(Value *V) {
  if (Constant *C = dyn_cast<Constant>(V))
    return C->isNullValue();
  return false;
}

void ShadowStackGCLoweringImpl::CollectRoots(Function &F) {
  // FIXME: Account for original alignment. Could fragment the root array.
  //   Approach 1: Null initialize empty slots at runtime. Yuck.
  //   Approach 2: Emit a map of the array instead of just a count.

  assert(Roots.empty() && "Not cleaned up?");

  SmallVector<std::pair<CallInst *, AllocaInst *>, 16> MetaRoots;

  for (BasicBlock &BB : F)
    for (Instruction &I : BB)
      if (IntrinsicInst *CI = dyn_cast<IntrinsicInst>(&I))
        if (Function *F = CI->getCalledFunction())
          if (F->getIntrinsicID() == Intrinsic::gcroot) {
            std::pair<CallInst *, AllocaInst *> Pair = std::make_pair(
                CI,
                cast<AllocaInst>(CI->getArgOperand(0)->stripPointerCasts()));
            if (IsNullValue(CI->getArgOperand(1)))
              Roots.push_back(Pair);
            else
              MetaRoots.push_back(Pair);
          }

  // Number roots with metadata (usually empty) at the beginning, so that the
  // FrameMap::Meta array can be elided.
  Roots.insert(Roots.begin(), MetaRoots.begin(), MetaRoots.end());
}

GetElementPtrInst *
ShadowStackGCLoweringImpl::CreateGEP(LLVMContext &Context, IRBuilder<> &B,
                                     Type *Ty, Value *BasePtr, int Idx,
                                     int Idx2, const char *Name) {
  Value *Indices[] = {ConstantInt::get(Type::getInt32Ty(Context), 0),
                      ConstantInt::get(Type::getInt32Ty(Context), Idx),
                      ConstantInt::get(Type::getInt32Ty(Context), Idx2)};
  Value *Val = B.CreateGEP(Ty, BasePtr, Indices, Name);

  assert(isa<GetElementPtrInst>(Val) && "Unexpected folded constant");

  return dyn_cast<GetElementPtrInst>(Val);
}

GetElementPtrInst *ShadowStackGCLoweringImpl::CreateGEP(LLVMContext &Context,
                                                        IRBuilder<> &B,
                                                        Type *Ty,
                                                        Value *BasePtr, int Idx,
                                                        const char *Name) {
  Value *Indices[] = {ConstantInt::get(Type::getInt32Ty(Context), 0),
                      ConstantInt::get(Type::getInt32Ty(Context), Idx)};
  Value *Val = B.CreateGEP(Ty, BasePtr, Indices, Name);

  assert(isa<GetElementPtrInst>(Val) && "Unexpected folded constant");

  return dyn_cast<GetElementPtrInst>(Val);
}

/// runOnFunction - Insert code to maintain the shadow stack.
bool ShadowStackGCLoweringImpl::runOnFunction(Function &F,
                                              DomTreeUpdater *DTU) {
  // Quick exit for functions that do not use the shadow stack GC.
  if (!F.hasGC() || F.getGC() != "shadow-stack")
    return false;

  LLVMContext &Context = F.getContext();

  // Find calls to llvm.gcroot.
  CollectRoots(F);

  // If there are no roots in this function, then there is no need to add a
  // stack map entry for it.
  if (Roots.empty())
    return false;

  // Build the constant map and figure the type of the shadow stack entry.
  Value *FrameMap = GetFrameMap(F);
  Type *ConcreteStackEntryTy = GetConcreteStackEntryType(F);

  // Build the shadow stack entry at the very start of the function.
  BasicBlock::iterator IP = F.getEntryBlock().begin();
  IRBuilder<> AtEntry(IP->getParent(), IP);

  Instruction *StackEntry =
      AtEntry.CreateAlloca(ConcreteStackEntryTy, nullptr, "gc_frame");

  AtEntry.SetInsertPointPastAllocas(&F);
  IP = AtEntry.GetInsertPoint();

  // Initialize the map pointer and load the current head of the shadow stack.
  Instruction *CurrentHead =
      AtEntry.CreateLoad(AtEntry.getPtrTy(), Head, "gc_currhead");
  Instruction *EntryMapPtr = CreateGEP(Context, AtEntry, ConcreteStackEntryTy,
                                       StackEntry, 0, 1, "gc_frame.map");
  AtEntry.CreateStore(FrameMap, EntryMapPtr);

  // After all the allocas...
  for (unsigned I = 0, E = Roots.size(); I != E; ++I) {
    // For each root, find the corresponding slot in the aggregate...
    Value *SlotPtr = CreateGEP(Context, AtEntry, ConcreteStackEntryTy,
                               StackEntry, 1 + I, "gc_root");

    // And use it in lieu of the alloca.
    AllocaInst *OriginalAlloca = Roots[I].second;
    SlotPtr->takeName(OriginalAlloca);
    OriginalAlloca->replaceAllUsesWith(SlotPtr);
  }

  // Move past the original stores inserted by GCStrategy::InitRoots. This isn't
  // really necessary (the collector would never see the intermediate state at
  // runtime), but it's nicer not to push the half-initialized entry onto the
  // shadow stack.
  while (isa<StoreInst>(IP))
    ++IP;
  AtEntry.SetInsertPoint(IP->getParent(), IP);

  // Push the entry onto the shadow stack.
  Instruction *EntryNextPtr = CreateGEP(Context, AtEntry, ConcreteStackEntryTy,
                                        StackEntry, 0, 0, "gc_frame.next");
  Instruction *NewHeadVal = CreateGEP(Context, AtEntry, ConcreteStackEntryTy,
                                      StackEntry, 0, "gc_newhead");
  AtEntry.CreateStore(CurrentHead, EntryNextPtr);
  AtEntry.CreateStore(NewHeadVal, Head);

  // For each instruction that escapes...
  EscapeEnumerator EE(F, "gc_cleanup", /*HandleExceptions=*/true, DTU);
  while (IRBuilder<> *AtExit = EE.Next()) {
    // Pop the entry from the shadow stack. Don't reuse CurrentHead from
    // AtEntry, since that would make the value live for the entire function.
    Instruction *EntryNextPtr2 =
        CreateGEP(Context, *AtExit, ConcreteStackEntryTy, StackEntry, 0, 0,
                  "gc_frame.next");
    Value *SavedHead =
        AtExit->CreateLoad(AtExit->getPtrTy(), EntryNextPtr2, "gc_savedhead");
    AtExit->CreateStore(SavedHead, Head);
  }

  // Delete the original allocas (which are no longer used) and the intrinsic
  // calls (which are no longer valid). Doing this last avoids invalidating
  // iterators.
  for (std::pair<CallInst *, AllocaInst *> &Root : Roots) {
    Root.first->eraseFromParent();
    Root.second->eraseFromParent();
  }

  Roots.clear();
  return true;
}
