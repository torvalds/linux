//===------ BPFPreserveStaticOffset.cpp -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// TLDR: replaces llvm.preserve.static.offset + GEP + load / store
//           with llvm.bpf.getelementptr.and.load / store
//
// This file implements BPFPreserveStaticOffsetPass transformation.
// This transformation address two BPF verifier specific issues:
//
// (a) Access to the fields of some structural types is allowed only
//     using load and store instructions with static immediate offsets.
//
//     Examples of such types are `struct __sk_buff` and `struct
//     bpf_sock_ops`.  This is so because offsets of the fields of
//     these structures do not match real offsets in the running
//     kernel. During BPF program load LDX and STX instructions
//     referring to the fields of these types are rewritten so that
//     offsets match real offsets. For this rewrite to happen field
//     offsets have to be encoded as immediate operands of the
//     instructions.
//
//     See kernel/bpf/verifier.c:convert_ctx_access function in the
//     Linux kernel source tree for details.
//
// (b) Pointers to context parameters of BPF programs must not be
//     modified before access.
//
//     During BPF program verification a tag PTR_TO_CTX is tracked for
//     register values. In case if register with such tag is modified
//     BPF program is not allowed to read or write memory using this
//     register. See kernel/bpf/verifier.c:check_mem_access function
//     in the Linux kernel source tree for details.
//
// The following sequence of the IR instructions:
//
//   %x = getelementptr %ptr, %constant_offset
//   %y = load %x
//
// Is translated as a single machine instruction:
//
//   LDW %ptr, %constant_offset
//
// In order for cases (a) and (b) to work the sequence %x-%y above has
// to be preserved by the IR passes.
//
// However, several optimization passes might sink `load` instruction
// or hoist `getelementptr` instruction so that the instructions are
// no longer in sequence. Examples of such passes are:
// SimplifyCFGPass, InstCombinePass, GVNPass.
// After such modification the verifier would reject the BPF program.
//
// To avoid this issue the patterns like (load/store (getelementptr ...))
// are replaced by calls to BPF specific intrinsic functions:
// - llvm.bpf.getelementptr.and.load
// - llvm.bpf.getelementptr.and.store
//
// These calls are lowered back to (load/store (getelementptr ...))
// by BPFCheckAndAdjustIR pass right before the translation from IR to
// machine instructions.
//
// The transformation is split into the following steps:
// - When IR is generated from AST the calls to intrinsic function
//   llvm.preserve.static.offset are inserted.
// - BPFPreserveStaticOffsetPass is executed as early as possible
//   with AllowPatial set to true, this handles marked GEP chains
//   with constant offsets.
// - BPFPreserveStaticOffsetPass is executed at ScalarOptimizerLateEPCallback
//   with AllowPatial set to false, this handles marked GEP chains
//   with offsets that became constant after loop unrolling, e.g.
//   to handle the following code:
//
// struct context { int x[4]; } __attribute__((preserve_static_offset));
//
//   struct context *ctx = ...;
// #pragma clang loop unroll(full)
//   for (int i = 0; i < 4; ++i)
//     foo(ctx->x[i]);
//
// The early BPFPreserveStaticOffsetPass run is necessary to allow
// additional GVN / CSE opportunities after functions inlining.
// The relative order of optimization applied to function:
// - early stage (1)
// - ...
// - function inlining (2)
// - ...
// - loop unrolling
// - ...
// - ScalarOptimizerLateEPCallback (3)
//
// When function A is inlined into function B all optimizations for A
// are already done, while some passes remain for B. In case if
// BPFPreserveStaticOffsetPass is done at (3) but not done at (1)
// the code after (2) would contain a mix of
// (load (gep %p)) and (get.and.load %p) usages:
// - the (load (gep %p)) would come from the calling function;
// - the (get.and.load %p) would come from the callee function.
// Thus clobbering CSE / GVN passes done after inlining.

#include "BPF.h"
#include "BPFCORE.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsBPF.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"

#define DEBUG_TYPE "bpf-preserve-static-offset"

using namespace llvm;

static const unsigned GepAndLoadFirstIdxArg = 6;
static const unsigned GepAndStoreFirstIdxArg = 7;

static bool isIntrinsicCall(Value *I, Intrinsic::ID Id) {
  if (auto *Call = dyn_cast<CallInst>(I))
    if (Function *Func = Call->getCalledFunction())
      return Func->getIntrinsicID() == Id;
  return false;
}

static bool isPreserveStaticOffsetCall(Value *I) {
  return isIntrinsicCall(I, Intrinsic::preserve_static_offset);
}

static CallInst *isGEPAndLoad(Value *I) {
  if (isIntrinsicCall(I, Intrinsic::bpf_getelementptr_and_load))
    return cast<CallInst>(I);
  return nullptr;
}

static CallInst *isGEPAndStore(Value *I) {
  if (isIntrinsicCall(I, Intrinsic::bpf_getelementptr_and_store))
    return cast<CallInst>(I);
  return nullptr;
}

template <class T = Instruction>
static DILocation *mergeDILocations(SmallVector<T *> &Insns) {
  DILocation *Merged = (*Insns.begin())->getDebugLoc();
  for (T *I : Insns)
    Merged = DILocation::getMergedLocation(Merged, I->getDebugLoc());
  return Merged;
}

static CallInst *makeIntrinsicCall(Module *M,
                                   Intrinsic::BPFIntrinsics Intrinsic,
                                   ArrayRef<Type *> Types,
                                   ArrayRef<Value *> Args) {

  Function *Fn = Intrinsic::getDeclaration(M, Intrinsic, Types);
  return CallInst::Create(Fn, Args);
}

static void setParamElementType(CallInst *Call, unsigned ArgNo, Type *Type) {
  LLVMContext &C = Call->getContext();
  Call->addParamAttr(ArgNo, Attribute::get(C, Attribute::ElementType, Type));
}

static void setParamReadNone(CallInst *Call, unsigned ArgNo) {
  LLVMContext &C = Call->getContext();
  Call->addParamAttr(ArgNo, Attribute::get(C, Attribute::ReadNone));
}

static void setParamReadOnly(CallInst *Call, unsigned ArgNo) {
  LLVMContext &C = Call->getContext();
  Call->addParamAttr(ArgNo, Attribute::get(C, Attribute::ReadOnly));
}

static void setParamWriteOnly(CallInst *Call, unsigned ArgNo) {
  LLVMContext &C = Call->getContext();
  Call->addParamAttr(ArgNo, Attribute::get(C, Attribute::WriteOnly));
}

namespace {
struct GEPChainInfo {
  bool InBounds;
  Type *SourceElementType;
  SmallVector<Value *> Indices;
  SmallVector<GetElementPtrInst *> Members;

  GEPChainInfo() { reset(); }

  void reset() {
    InBounds = true;
    SourceElementType = nullptr;
    Indices.clear();
    Members.clear();
  }
};
} // Anonymous namespace

template <class T = std::disjunction<LoadInst, StoreInst>>
static void fillCommonArgs(LLVMContext &C, SmallVector<Value *> &Args,
                           GEPChainInfo &GEP, T *Insn) {
  Type *Int8Ty = Type::getInt8Ty(C);
  Type *Int1Ty = Type::getInt1Ty(C);
  // Implementation of Align guarantees that ShiftValue < 64
  unsigned AlignShiftValue = Log2_64(Insn->getAlign().value());
  Args.push_back(GEP.Members[0]->getPointerOperand());
  Args.push_back(ConstantInt::get(Int1Ty, Insn->isVolatile()));
  Args.push_back(ConstantInt::get(Int8Ty, (unsigned)Insn->getOrdering()));
  Args.push_back(ConstantInt::get(Int8Ty, (unsigned)Insn->getSyncScopeID()));
  Args.push_back(ConstantInt::get(Int8Ty, AlignShiftValue));
  Args.push_back(ConstantInt::get(Int1Ty, GEP.InBounds));
  Args.append(GEP.Indices.begin(), GEP.Indices.end());
}

static Instruction *makeGEPAndLoad(Module *M, GEPChainInfo &GEP,
                                   LoadInst *Load) {
  SmallVector<Value *> Args;
  fillCommonArgs(M->getContext(), Args, GEP, Load);
  CallInst *Call = makeIntrinsicCall(M, Intrinsic::bpf_getelementptr_and_load,
                                     {Load->getType()}, Args);
  setParamElementType(Call, 0, GEP.SourceElementType);
  Call->applyMergedLocation(mergeDILocations(GEP.Members), Load->getDebugLoc());
  Call->setName((*GEP.Members.rbegin())->getName());
  if (Load->isUnordered()) {
    Call->setOnlyReadsMemory();
    Call->setOnlyAccessesArgMemory();
    setParamReadOnly(Call, 0);
  }
  for (unsigned I = GepAndLoadFirstIdxArg; I < Args.size(); ++I)
    Call->addParamAttr(I, Attribute::ImmArg);
  Call->setAAMetadata(Load->getAAMetadata());
  return Call;
}

static Instruction *makeGEPAndStore(Module *M, GEPChainInfo &GEP,
                                    StoreInst *Store) {
  SmallVector<Value *> Args;
  Args.push_back(Store->getValueOperand());
  fillCommonArgs(M->getContext(), Args, GEP, Store);
  CallInst *Call =
      makeIntrinsicCall(M, Intrinsic::bpf_getelementptr_and_store,
                        {Store->getValueOperand()->getType()}, Args);
  setParamElementType(Call, 1, GEP.SourceElementType);
  if (Store->getValueOperand()->getType()->isPointerTy())
    setParamReadNone(Call, 0);
  Call->applyMergedLocation(mergeDILocations(GEP.Members),
                            Store->getDebugLoc());
  if (Store->isUnordered()) {
    Call->setOnlyWritesMemory();
    Call->setOnlyAccessesArgMemory();
    setParamWriteOnly(Call, 1);
  }
  for (unsigned I = GepAndStoreFirstIdxArg; I < Args.size(); ++I)
    Call->addParamAttr(I, Attribute::ImmArg);
  Call->setAAMetadata(Store->getAAMetadata());
  return Call;
}

static unsigned getOperandAsUnsigned(CallInst *Call, unsigned ArgNo) {
  if (auto *Int = dyn_cast<ConstantInt>(Call->getOperand(ArgNo)))
    return Int->getValue().getZExtValue();
  std::string Report;
  raw_string_ostream ReportS(Report);
  ReportS << "Expecting ConstantInt as argument #" << ArgNo << " of " << *Call
          << "\n";
  report_fatal_error(StringRef(Report));
}

static GetElementPtrInst *reconstructGEP(CallInst *Call, int Delta) {
  SmallVector<Value *> Indices;
  Indices.append(Call->data_operands_begin() + 6 + Delta,
                 Call->data_operands_end());
  Type *GEPPointeeType = Call->getParamElementType(Delta);
  auto *GEP =
      GetElementPtrInst::Create(GEPPointeeType, Call->getOperand(Delta),
                                ArrayRef<Value *>(Indices), Call->getName());
  GEP->setIsInBounds(getOperandAsUnsigned(Call, 5 + Delta));
  return GEP;
}

template <class T = std::disjunction<LoadInst, StoreInst>>
static void reconstructCommon(CallInst *Call, GetElementPtrInst *GEP, T *Insn,
                              int Delta) {
  Insn->setVolatile(getOperandAsUnsigned(Call, 1 + Delta));
  Insn->setOrdering((AtomicOrdering)getOperandAsUnsigned(Call, 2 + Delta));
  Insn->setSyncScopeID(getOperandAsUnsigned(Call, 3 + Delta));
  unsigned AlignShiftValue = getOperandAsUnsigned(Call, 4 + Delta);
  Insn->setAlignment(Align(1ULL << AlignShiftValue));
  GEP->setDebugLoc(Call->getDebugLoc());
  Insn->setDebugLoc(Call->getDebugLoc());
  Insn->setAAMetadata(Call->getAAMetadata());
}

std::pair<GetElementPtrInst *, LoadInst *>
BPFPreserveStaticOffsetPass::reconstructLoad(CallInst *Call) {
  GetElementPtrInst *GEP = reconstructGEP(Call, 0);
  Type *ReturnType = Call->getFunctionType()->getReturnType();
  auto *Load = new LoadInst(ReturnType, GEP, "",
                            /* These would be set in reconstructCommon */
                            false, Align(1));
  reconstructCommon(Call, GEP, Load, 0);
  return std::pair{GEP, Load};
}

std::pair<GetElementPtrInst *, StoreInst *>
BPFPreserveStaticOffsetPass::reconstructStore(CallInst *Call) {
  GetElementPtrInst *GEP = reconstructGEP(Call, 1);
  auto *Store = new StoreInst(Call->getOperand(0), GEP,
                              /* These would be set in reconstructCommon */
                              false, Align(1));
  reconstructCommon(Call, GEP, Store, 1);
  return std::pair{GEP, Store};
}

static bool isZero(Value *V) {
  auto *CI = dyn_cast<ConstantInt>(V);
  return CI && CI->isZero();
}

// Given a chain of GEP instructions collect information necessary to
// merge this chain as a single GEP instruction of form:
//   getelementptr %<type>, ptr %p, i32 0, <field_idx1>, <field_idx2>, ...
static bool foldGEPChainAsStructAccess(SmallVector<GetElementPtrInst *> &GEPs,
                                       GEPChainInfo &Info) {
  if (GEPs.empty())
    return false;

  if (!all_of(GEPs, [=](GetElementPtrInst *GEP) {
        return GEP->hasAllConstantIndices();
      }))
    return false;

  GetElementPtrInst *First = GEPs[0];
  Info.InBounds = First->isInBounds();
  Info.SourceElementType = First->getSourceElementType();
  Type *ResultElementType = First->getResultElementType();
  Info.Indices.append(First->idx_begin(), First->idx_end());
  Info.Members.push_back(First);

  for (auto *Iter = GEPs.begin() + 1; Iter != GEPs.end(); ++Iter) {
    GetElementPtrInst *GEP = *Iter;
    if (!isZero(*GEP->idx_begin())) {
      Info.reset();
      return false;
    }
    if (!GEP->getSourceElementType() ||
        GEP->getSourceElementType() != ResultElementType) {
      Info.reset();
      return false;
    }
    Info.InBounds &= GEP->isInBounds();
    Info.Indices.append(GEP->idx_begin() + 1, GEP->idx_end());
    Info.Members.push_back(GEP);
    ResultElementType = GEP->getResultElementType();
  }

  return true;
}

// Given a chain of GEP instructions collect information necessary to
// merge this chain as a single GEP instruction of form:
//   getelementptr i8, ptr %p, i64 %offset
static bool foldGEPChainAsU8Access(SmallVector<GetElementPtrInst *> &GEPs,
                                   GEPChainInfo &Info) {
  if (GEPs.empty())
    return false;

  GetElementPtrInst *First = GEPs[0];
  const DataLayout &DL = First->getDataLayout();
  LLVMContext &C = First->getContext();
  Type *PtrTy = First->getType()->getScalarType();
  APInt Offset(DL.getIndexTypeSizeInBits(PtrTy), 0);
  for (GetElementPtrInst *GEP : GEPs) {
    if (!GEP->accumulateConstantOffset(DL, Offset)) {
      Info.reset();
      return false;
    }
    Info.InBounds &= GEP->isInBounds();
    Info.Members.push_back(GEP);
  }
  Info.SourceElementType = Type::getInt8Ty(C);
  Info.Indices.push_back(ConstantInt::get(C, Offset));

  return true;
}

static void reportNonStaticGEPChain(Instruction *Insn) {
  auto Msg = DiagnosticInfoUnsupported(
      *Insn->getFunction(),
      Twine("Non-constant offset in access to a field of a type marked "
            "with preserve_static_offset might be rejected by BPF verifier")
          .concat(Insn->getDebugLoc()
                      ? ""
                      : " (pass -g option to get exact location)"),
      Insn->getDebugLoc(), DS_Warning);
  Insn->getContext().diagnose(Msg);
}

static bool allZeroIndices(SmallVector<GetElementPtrInst *> &GEPs) {
  return GEPs.empty() || all_of(GEPs, [=](GetElementPtrInst *GEP) {
           return GEP->hasAllZeroIndices();
         });
}

static bool tryToReplaceWithGEPBuiltin(Instruction *LoadOrStoreTemplate,
                                       SmallVector<GetElementPtrInst *> &GEPs,
                                       Instruction *InsnToReplace) {
  GEPChainInfo GEPChain;
  if (!foldGEPChainAsStructAccess(GEPs, GEPChain) &&
      !foldGEPChainAsU8Access(GEPs, GEPChain)) {
    return false;
  }
  Module *M = InsnToReplace->getModule();
  if (auto *Load = dyn_cast<LoadInst>(LoadOrStoreTemplate)) {
    Instruction *Replacement = makeGEPAndLoad(M, GEPChain, Load);
    Replacement->insertBefore(InsnToReplace);
    InsnToReplace->replaceAllUsesWith(Replacement);
  }
  if (auto *Store = dyn_cast<StoreInst>(LoadOrStoreTemplate)) {
    Instruction *Replacement = makeGEPAndStore(M, GEPChain, Store);
    Replacement->insertBefore(InsnToReplace);
  }
  return true;
}

// Check if U->getPointerOperand() == I
static bool isPointerOperand(Value *I, User *U) {
  if (auto *L = dyn_cast<LoadInst>(U))
    return L->getPointerOperand() == I;
  if (auto *S = dyn_cast<StoreInst>(U))
    return S->getPointerOperand() == I;
  if (auto *GEP = dyn_cast<GetElementPtrInst>(U))
    return GEP->getPointerOperand() == I;
  if (auto *Call = isGEPAndLoad(U))
    return Call->getArgOperand(0) == I;
  if (auto *Call = isGEPAndStore(U))
    return Call->getArgOperand(1) == I;
  return false;
}

static bool isInlineableCall(User *U) {
  if (auto *Call = dyn_cast<CallInst>(U))
    return Call->hasFnAttr(Attribute::InlineHint);
  return false;
}

static void rewriteAccessChain(Instruction *Insn,
                               SmallVector<GetElementPtrInst *> &GEPs,
                               SmallVector<Instruction *> &Visited,
                               bool AllowPatial, bool &StillUsed);

static void rewriteUses(Instruction *Insn,
                        SmallVector<GetElementPtrInst *> &GEPs,
                        SmallVector<Instruction *> &Visited, bool AllowPatial,
                        bool &StillUsed) {
  for (User *U : Insn->users()) {
    auto *UI = dyn_cast<Instruction>(U);
    if (UI && (isPointerOperand(Insn, UI) || isPreserveStaticOffsetCall(UI) ||
               isInlineableCall(UI)))
      rewriteAccessChain(UI, GEPs, Visited, AllowPatial, StillUsed);
    else
      LLVM_DEBUG({
        llvm::dbgs() << "unsupported usage in BPFPreserveStaticOffsetPass:\n";
        llvm::dbgs() << "  Insn: " << *Insn << "\n";
        llvm::dbgs() << "  User: " << *U << "\n";
      });
  }
}

// A DFS traversal of GEP chain trees starting from Root.
//
// Recursion descends through GEP instructions and
// llvm.preserve.static.offset calls. Recursion stops at any other
// instruction. If load or store instruction is reached it is replaced
// by a call to `llvm.bpf.getelementptr.and.load` or
// `llvm.bpf.getelementptr.and.store` intrinsic.
// If `llvm.bpf.getelementptr.and.load/store` is reached the accumulated
// GEPs are merged into the intrinsic call.
// If nested calls to `llvm.preserve.static.offset` are encountered these
// calls are marked for deletion.
//
// Parameters description:
// - Insn - current position in the tree
// - GEPs - GEP instructions for the current branch
// - Visited - a list of visited instructions in DFS order,
//   order is important for unused instruction deletion.
// - AllowPartial - when true GEP chains that can't be folded are
//   not reported, otherwise diagnostic message is show for such chains.
// - StillUsed - set to true if one of the GEP chains could not be
//   folded, makes sense when AllowPartial is false, means that root
//   preserve.static.offset call is still in use and should remain
//   until the next run of this pass.
static void rewriteAccessChain(Instruction *Insn,
                               SmallVector<GetElementPtrInst *> &GEPs,
                               SmallVector<Instruction *> &Visited,
                               bool AllowPatial, bool &StillUsed) {
  auto MarkAndTraverseUses = [&]() {
    Visited.push_back(Insn);
    rewriteUses(Insn, GEPs, Visited, AllowPatial, StillUsed);
  };
  auto TryToReplace = [&](Instruction *LoadOrStore) {
    // Do nothing for (preserve.static.offset (load/store ..)) or for
    // GEPs with zero indices. Such constructs lead to zero offset and
    // are simplified by other passes.
    if (allZeroIndices(GEPs))
      return;
    if (tryToReplaceWithGEPBuiltin(LoadOrStore, GEPs, Insn)) {
      Visited.push_back(Insn);
      return;
    }
    if (!AllowPatial)
      reportNonStaticGEPChain(Insn);
    StillUsed = true;
  };
  if (isa<LoadInst>(Insn) || isa<StoreInst>(Insn)) {
    TryToReplace(Insn);
  } else if (isGEPAndLoad(Insn)) {
    auto [GEP, Load] =
        BPFPreserveStaticOffsetPass::reconstructLoad(cast<CallInst>(Insn));
    GEPs.push_back(GEP);
    TryToReplace(Load);
    GEPs.pop_back();
    delete Load;
    delete GEP;
  } else if (isGEPAndStore(Insn)) {
    // This  case can't be merged with the above because
    // `delete Load` / `delete Store` wants a concrete type,
    // destructor of Instruction is protected.
    auto [GEP, Store] =
        BPFPreserveStaticOffsetPass::reconstructStore(cast<CallInst>(Insn));
    GEPs.push_back(GEP);
    TryToReplace(Store);
    GEPs.pop_back();
    delete Store;
    delete GEP;
  } else if (auto *GEP = dyn_cast<GetElementPtrInst>(Insn)) {
    GEPs.push_back(GEP);
    MarkAndTraverseUses();
    GEPs.pop_back();
  } else if (isPreserveStaticOffsetCall(Insn)) {
    MarkAndTraverseUses();
  } else if (isInlineableCall(Insn)) {
    // Preserve preserve.static.offset call for parameters of
    // functions that might be inlined. These would be removed on a
    // second pass after inlining.
    // Might happen when a pointer to a preserve_static_offset
    // structure is passed as parameter of a function that would be
    // inlined inside a loop that would be unrolled.
    if (AllowPatial)
      StillUsed = true;
  } else {
    SmallString<128> Buf;
    raw_svector_ostream BufStream(Buf);
    BufStream << *Insn;
    report_fatal_error(
        Twine("Unexpected rewriteAccessChain Insn = ").concat(Buf));
  }
}

static void removeMarkerCall(Instruction *Marker) {
  Marker->replaceAllUsesWith(Marker->getOperand(0));
  Marker->eraseFromParent();
}

static bool rewriteAccessChain(Instruction *Marker, bool AllowPatial,
                               SmallPtrSetImpl<Instruction *> &RemovedMarkers) {
  SmallVector<GetElementPtrInst *> GEPs;
  SmallVector<Instruction *> Visited;
  bool StillUsed = false;
  rewriteUses(Marker, GEPs, Visited, AllowPatial, StillUsed);
  // Check if Visited instructions could be removed, iterate in
  // reverse to unblock instructions higher in the chain.
  for (auto V = Visited.rbegin(); V != Visited.rend(); ++V) {
    if (isPreserveStaticOffsetCall(*V)) {
      removeMarkerCall(*V);
      RemovedMarkers.insert(*V);
    } else if ((*V)->use_empty()) {
      (*V)->eraseFromParent();
    }
  }
  return StillUsed;
}

static std::vector<Instruction *>
collectPreserveStaticOffsetCalls(Function &F) {
  std::vector<Instruction *> Calls;
  for (Instruction &Insn : instructions(F))
    if (isPreserveStaticOffsetCall(&Insn))
      Calls.push_back(&Insn);
  return Calls;
}

bool isPreserveArrayIndex(Value *V) {
  return isIntrinsicCall(V, Intrinsic::preserve_array_access_index);
}

bool isPreserveStructIndex(Value *V) {
  return isIntrinsicCall(V, Intrinsic::preserve_struct_access_index);
}

bool isPreserveUnionIndex(Value *V) {
  return isIntrinsicCall(V, Intrinsic::preserve_union_access_index);
}

static void removePAICalls(Instruction *Marker) {
  auto IsPointerOperand = [](Value *Op, User *U) {
    if (auto *GEP = dyn_cast<GetElementPtrInst>(U))
      return GEP->getPointerOperand() == Op;
    if (isPreserveStaticOffsetCall(U) || isPreserveArrayIndex(U) ||
        isPreserveStructIndex(U) || isPreserveUnionIndex(U))
      return cast<CallInst>(U)->getArgOperand(0) == Op;
    return false;
  };

  SmallVector<Value *, 32> WorkList;
  WorkList.push_back(Marker);
  do {
    Value *V = WorkList.pop_back_val();
    for (User *U : V->users())
      if (IsPointerOperand(V, U))
        WorkList.push_back(U);
    auto *Call = dyn_cast<CallInst>(V);
    if (!Call)
      continue;
    if (isPreserveArrayIndex(V))
      BPFCoreSharedInfo::removeArrayAccessCall(Call);
    else if (isPreserveStructIndex(V))
      BPFCoreSharedInfo::removeStructAccessCall(Call);
    else if (isPreserveUnionIndex(V))
      BPFCoreSharedInfo::removeUnionAccessCall(Call);
  } while (!WorkList.empty());
}

// Look for sequences:
// - llvm.preserve.static.offset -> getelementptr... -> load
// - llvm.preserve.static.offset -> getelementptr... -> store
// And replace those with calls to intrinsics:
// - llvm.bpf.getelementptr.and.load
// - llvm.bpf.getelementptr.and.store
static bool rewriteFunction(Function &F, bool AllowPartial) {
  LLVM_DEBUG(dbgs() << "********** BPFPreserveStaticOffsetPass (AllowPartial="
                    << AllowPartial << ") ************\n");

  auto MarkerCalls = collectPreserveStaticOffsetCalls(F);
  SmallPtrSet<Instruction *, 16> RemovedMarkers;

  LLVM_DEBUG(dbgs() << "There are " << MarkerCalls.size()
                    << " preserve.static.offset calls\n");

  if (MarkerCalls.empty())
    return false;

  for (auto *Call : MarkerCalls)
    removePAICalls(Call);

  for (auto *Call : MarkerCalls) {
    if (RemovedMarkers.contains(Call))
      continue;
    bool StillUsed = rewriteAccessChain(Call, AllowPartial, RemovedMarkers);
    if (!StillUsed || !AllowPartial)
      removeMarkerCall(Call);
  }

  return true;
}

PreservedAnalyses
llvm::BPFPreserveStaticOffsetPass::run(Function &F,
                                       FunctionAnalysisManager &AM) {
  return rewriteFunction(F, AllowPartial) ? PreservedAnalyses::none()
                                          : PreservedAnalyses::all();
}
