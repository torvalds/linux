//===------------ BPFCheckAndAdjustIR.cpp - Check and Adjust IR -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Check IR and adjust IR for verifier friendly codes.
// The following are done for IR checking:
//   - no relocation globals in PHI node.
// The following are done for IR adjustment:
//   - remove __builtin_bpf_passthrough builtins. Target independent IR
//     optimizations are done and those builtins can be removed.
//   - remove llvm.bpf.getelementptr.and.load builtins.
//   - remove llvm.bpf.getelementptr.and.store builtins.
//   - for loads and stores with base addresses from non-zero address space
//     cast base address to zero address space (support for BPF address spaces).
//
//===----------------------------------------------------------------------===//

#include "BPF.h"
#include "BPFCORE.h"
#include "BPFTargetMachine.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicsBPF.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#define DEBUG_TYPE "bpf-check-and-opt-ir"

using namespace llvm;

namespace {

class BPFCheckAndAdjustIR final : public ModulePass {
  bool runOnModule(Module &F) override;

public:
  static char ID;
  BPFCheckAndAdjustIR() : ModulePass(ID) {}
  virtual void getAnalysisUsage(AnalysisUsage &AU) const override;

private:
  void checkIR(Module &M);
  bool adjustIR(Module &M);
  bool removePassThroughBuiltin(Module &M);
  bool removeCompareBuiltin(Module &M);
  bool sinkMinMax(Module &M);
  bool removeGEPBuiltins(Module &M);
  bool insertASpaceCasts(Module &M);
};
} // End anonymous namespace

char BPFCheckAndAdjustIR::ID = 0;
INITIALIZE_PASS(BPFCheckAndAdjustIR, DEBUG_TYPE, "BPF Check And Adjust IR",
                false, false)

ModulePass *llvm::createBPFCheckAndAdjustIR() {
  return new BPFCheckAndAdjustIR();
}

void BPFCheckAndAdjustIR::checkIR(Module &M) {
  // Ensure relocation global won't appear in PHI node
  // This may happen if the compiler generated the following code:
  //   B1:
  //      g1 = @llvm.skb_buff:0:1...
  //      ...
  //      goto B_COMMON
  //   B2:
  //      g2 = @llvm.skb_buff:0:2...
  //      ...
  //      goto B_COMMON
  //   B_COMMON:
  //      g = PHI(g1, g2)
  //      x = load g
  //      ...
  // If anything likes the above "g = PHI(g1, g2)", issue a fatal error.
  for (Function &F : M)
    for (auto &BB : F)
      for (auto &I : BB) {
        PHINode *PN = dyn_cast<PHINode>(&I);
        if (!PN || PN->use_empty())
          continue;
        for (int i = 0, e = PN->getNumIncomingValues(); i < e; ++i) {
          auto *GV = dyn_cast<GlobalVariable>(PN->getIncomingValue(i));
          if (!GV)
            continue;
          if (GV->hasAttribute(BPFCoreSharedInfo::AmaAttr) ||
              GV->hasAttribute(BPFCoreSharedInfo::TypeIdAttr))
            report_fatal_error("relocation global in PHI node");
        }
      }
}

bool BPFCheckAndAdjustIR::removePassThroughBuiltin(Module &M) {
  // Remove __builtin_bpf_passthrough()'s which are used to prevent
  // certain IR optimizations. Now major IR optimizations are done,
  // remove them.
  bool Changed = false;
  CallInst *ToBeDeleted = nullptr;
  for (Function &F : M)
    for (auto &BB : F)
      for (auto &I : BB) {
        if (ToBeDeleted) {
          ToBeDeleted->eraseFromParent();
          ToBeDeleted = nullptr;
        }

        auto *Call = dyn_cast<CallInst>(&I);
        if (!Call)
          continue;
        auto *GV = dyn_cast<GlobalValue>(Call->getCalledOperand());
        if (!GV)
          continue;
        if (!GV->getName().starts_with("llvm.bpf.passthrough"))
          continue;
        Changed = true;
        Value *Arg = Call->getArgOperand(1);
        Call->replaceAllUsesWith(Arg);
        ToBeDeleted = Call;
      }
  return Changed;
}

bool BPFCheckAndAdjustIR::removeCompareBuiltin(Module &M) {
  // Remove __builtin_bpf_compare()'s which are used to prevent
  // certain IR optimizations. Now major IR optimizations are done,
  // remove them.
  bool Changed = false;
  CallInst *ToBeDeleted = nullptr;
  for (Function &F : M)
    for (auto &BB : F)
      for (auto &I : BB) {
        if (ToBeDeleted) {
          ToBeDeleted->eraseFromParent();
          ToBeDeleted = nullptr;
        }

        auto *Call = dyn_cast<CallInst>(&I);
        if (!Call)
          continue;
        auto *GV = dyn_cast<GlobalValue>(Call->getCalledOperand());
        if (!GV)
          continue;
        if (!GV->getName().starts_with("llvm.bpf.compare"))
          continue;

        Changed = true;
        Value *Arg0 = Call->getArgOperand(0);
        Value *Arg1 = Call->getArgOperand(1);
        Value *Arg2 = Call->getArgOperand(2);

        auto OpVal = cast<ConstantInt>(Arg0)->getValue().getZExtValue();
        CmpInst::Predicate Opcode = (CmpInst::Predicate)OpVal;

        auto *ICmp = new ICmpInst(Opcode, Arg1, Arg2);
        ICmp->insertBefore(Call);

        Call->replaceAllUsesWith(ICmp);
        ToBeDeleted = Call;
      }
  return Changed;
}

struct MinMaxSinkInfo {
  ICmpInst *ICmp;
  Value *Other;
  ICmpInst::Predicate Predicate;
  CallInst *MinMax;
  ZExtInst *ZExt;
  SExtInst *SExt;

  MinMaxSinkInfo(ICmpInst *ICmp, Value *Other, ICmpInst::Predicate Predicate)
      : ICmp(ICmp), Other(Other), Predicate(Predicate), MinMax(nullptr),
        ZExt(nullptr), SExt(nullptr) {}
};

static bool sinkMinMaxInBB(BasicBlock &BB,
                           const std::function<bool(Instruction *)> &Filter) {
  // Check if V is:
  //   (fn %a %b) or (ext (fn %a %b))
  // Where:
  //   ext := sext | zext
  //   fn  := smin | umin | smax | umax
  auto IsMinMaxCall = [=](Value *V, MinMaxSinkInfo &Info) {
    if (auto *ZExt = dyn_cast<ZExtInst>(V)) {
      V = ZExt->getOperand(0);
      Info.ZExt = ZExt;
    } else if (auto *SExt = dyn_cast<SExtInst>(V)) {
      V = SExt->getOperand(0);
      Info.SExt = SExt;
    }

    auto *Call = dyn_cast<CallInst>(V);
    if (!Call)
      return false;

    auto *Called = dyn_cast<Function>(Call->getCalledOperand());
    if (!Called)
      return false;

    switch (Called->getIntrinsicID()) {
    case Intrinsic::smin:
    case Intrinsic::umin:
    case Intrinsic::smax:
    case Intrinsic::umax:
      break;
    default:
      return false;
    }

    if (!Filter(Call))
      return false;

    Info.MinMax = Call;

    return true;
  };

  auto ZeroOrSignExtend = [](IRBuilder<> &Builder, Value *V,
                             MinMaxSinkInfo &Info) {
    if (Info.SExt) {
      if (Info.SExt->getType() == V->getType())
        return V;
      return Builder.CreateSExt(V, Info.SExt->getType());
    }
    if (Info.ZExt) {
      if (Info.ZExt->getType() == V->getType())
        return V;
      return Builder.CreateZExt(V, Info.ZExt->getType());
    }
    return V;
  };

  bool Changed = false;
  SmallVector<MinMaxSinkInfo, 2> SinkList;

  // Check BB for instructions like:
  //   insn := (icmp %a (fn ...)) | (icmp (fn ...)  %a)
  //
  // Where:
  //   fn := min | max | (sext (min ...)) | (sext (max ...))
  //
  // Put such instructions to SinkList.
  for (Instruction &I : BB) {
    ICmpInst *ICmp = dyn_cast<ICmpInst>(&I);
    if (!ICmp)
      continue;
    if (!ICmp->isRelational())
      continue;
    MinMaxSinkInfo First(ICmp, ICmp->getOperand(1),
                         ICmpInst::getSwappedPredicate(ICmp->getPredicate()));
    MinMaxSinkInfo Second(ICmp, ICmp->getOperand(0), ICmp->getPredicate());
    bool FirstMinMax = IsMinMaxCall(ICmp->getOperand(0), First);
    bool SecondMinMax = IsMinMaxCall(ICmp->getOperand(1), Second);
    if (!(FirstMinMax ^ SecondMinMax))
      continue;
    SinkList.push_back(FirstMinMax ? First : Second);
  }

  // Iterate SinkList and replace each (icmp ...) with corresponding
  // `x < a && x < b` or similar expression.
  for (auto &Info : SinkList) {
    ICmpInst *ICmp = Info.ICmp;
    CallInst *MinMax = Info.MinMax;
    Intrinsic::ID IID = MinMax->getCalledFunction()->getIntrinsicID();
    ICmpInst::Predicate P = Info.Predicate;
    if (ICmpInst::isSigned(P) && IID != Intrinsic::smin &&
        IID != Intrinsic::smax)
      continue;

    IRBuilder<> Builder(ICmp);
    Value *X = Info.Other;
    Value *A = ZeroOrSignExtend(Builder, MinMax->getArgOperand(0), Info);
    Value *B = ZeroOrSignExtend(Builder, MinMax->getArgOperand(1), Info);
    bool IsMin = IID == Intrinsic::smin || IID == Intrinsic::umin;
    bool IsMax = IID == Intrinsic::smax || IID == Intrinsic::umax;
    bool IsLess = ICmpInst::isLE(P) || ICmpInst::isLT(P);
    bool IsGreater = ICmpInst::isGE(P) || ICmpInst::isGT(P);
    assert(IsMin ^ IsMax);
    assert(IsLess ^ IsGreater);

    Value *Replacement;
    Value *LHS = Builder.CreateICmp(P, X, A);
    Value *RHS = Builder.CreateICmp(P, X, B);
    if ((IsLess && IsMin) || (IsGreater && IsMax))
      // x < min(a, b) -> x < a && x < b
      // x > max(a, b) -> x > a && x > b
      Replacement = Builder.CreateLogicalAnd(LHS, RHS);
    else
      // x > min(a, b) -> x > a || x > b
      // x < max(a, b) -> x < a || x < b
      Replacement = Builder.CreateLogicalOr(LHS, RHS);

    ICmp->replaceAllUsesWith(Replacement);

    Instruction *ToRemove[] = {ICmp, Info.ZExt, Info.SExt, MinMax};
    for (Instruction *I : ToRemove)
      if (I && I->use_empty())
        I->eraseFromParent();

    Changed = true;
  }

  return Changed;
}

// Do the following transformation:
//
//   x < min(a, b) -> x < a && x < b
//   x > min(a, b) -> x > a || x > b
//   x < max(a, b) -> x < a || x < b
//   x > max(a, b) -> x > a && x > b
//
// Such patterns are introduced by LICM.cpp:hoistMinMax()
// transformation and might lead to BPF verification failures for
// older kernels.
//
// To minimize "collateral" changes only do it for icmp + min/max
// calls when icmp is inside a loop and min/max is outside of that
// loop.
//
// Verification failure happens when:
// - RHS operand of some `icmp LHS, RHS` is replaced by some RHS1;
// - verifier can recognize RHS as a constant scalar in some context;
// - verifier can't recognize RHS1 as a constant scalar in the same
//   context;
//
// The "constant scalar" is not a compile time constant, but a register
// that holds a scalar value known to verifier at some point in time
// during abstract interpretation.
//
// See also:
//   https://lore.kernel.org/bpf/20230406164505.1046801-1-yhs@fb.com/
bool BPFCheckAndAdjustIR::sinkMinMax(Module &M) {
  bool Changed = false;

  for (Function &F : M) {
    if (F.isDeclaration())
      continue;

    LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>(F).getLoopInfo();
    for (Loop *L : LI)
      for (BasicBlock *BB : L->blocks()) {
        // Filter out instructions coming from the same loop
        Loop *BBLoop = LI.getLoopFor(BB);
        auto OtherLoopFilter = [&](Instruction *I) {
          return LI.getLoopFor(I->getParent()) != BBLoop;
        };
        Changed |= sinkMinMaxInBB(*BB, OtherLoopFilter);
      }
  }

  return Changed;
}

void BPFCheckAndAdjustIR::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<LoopInfoWrapperPass>();
}

static void unrollGEPLoad(CallInst *Call) {
  auto [GEP, Load] = BPFPreserveStaticOffsetPass::reconstructLoad(Call);
  GEP->insertBefore(Call);
  Load->insertBefore(Call);
  Call->replaceAllUsesWith(Load);
  Call->eraseFromParent();
}

static void unrollGEPStore(CallInst *Call) {
  auto [GEP, Store] = BPFPreserveStaticOffsetPass::reconstructStore(Call);
  GEP->insertBefore(Call);
  Store->insertBefore(Call);
  Call->eraseFromParent();
}

static bool removeGEPBuiltinsInFunc(Function &F) {
  SmallVector<CallInst *> GEPLoads;
  SmallVector<CallInst *> GEPStores;
  for (auto &BB : F)
    for (auto &Insn : BB)
      if (auto *Call = dyn_cast<CallInst>(&Insn))
        if (auto *Called = Call->getCalledFunction())
          switch (Called->getIntrinsicID()) {
          case Intrinsic::bpf_getelementptr_and_load:
            GEPLoads.push_back(Call);
            break;
          case Intrinsic::bpf_getelementptr_and_store:
            GEPStores.push_back(Call);
            break;
          }

  if (GEPLoads.empty() && GEPStores.empty())
    return false;

  for_each(GEPLoads, unrollGEPLoad);
  for_each(GEPStores, unrollGEPStore);

  return true;
}

// Rewrites the following builtins:
// - llvm.bpf.getelementptr.and.load
// - llvm.bpf.getelementptr.and.store
// As (load (getelementptr ...)) or (store (getelementptr ...)).
bool BPFCheckAndAdjustIR::removeGEPBuiltins(Module &M) {
  bool Changed = false;
  for (auto &F : M)
    Changed = removeGEPBuiltinsInFunc(F) || Changed;
  return Changed;
}

// Wrap ToWrap with cast to address space zero:
// - if ToWrap is a getelementptr,
//   wrap it's base pointer instead and return a copy;
// - if ToWrap is Instruction, insert address space cast
//   immediately after ToWrap;
// - if ToWrap is not an Instruction (function parameter
//   or a global value), insert address space cast at the
//   beginning of the Function F;
// - use Cache to avoid inserting too many casts;
static Value *aspaceWrapValue(DenseMap<Value *, Value *> &Cache, Function *F,
                              Value *ToWrap) {
  auto It = Cache.find(ToWrap);
  if (It != Cache.end())
    return It->getSecond();

  if (auto *GEP = dyn_cast<GetElementPtrInst>(ToWrap)) {
    Value *Ptr = GEP->getPointerOperand();
    Value *WrappedPtr = aspaceWrapValue(Cache, F, Ptr);
    auto *GEPTy = cast<PointerType>(GEP->getType());
    auto *NewGEP = GEP->clone();
    NewGEP->insertAfter(GEP);
    NewGEP->mutateType(GEPTy->getPointerTo(0));
    NewGEP->setOperand(GEP->getPointerOperandIndex(), WrappedPtr);
    NewGEP->setName(GEP->getName());
    Cache[ToWrap] = NewGEP;
    return NewGEP;
  }

  IRBuilder IB(F->getContext());
  if (Instruction *InsnPtr = dyn_cast<Instruction>(ToWrap))
    IB.SetInsertPoint(*InsnPtr->getInsertionPointAfterDef());
  else
    IB.SetInsertPoint(F->getEntryBlock().getFirstInsertionPt());
  auto *PtrTy = cast<PointerType>(ToWrap->getType());
  auto *ASZeroPtrTy = PtrTy->getPointerTo(0);
  auto *ACast = IB.CreateAddrSpaceCast(ToWrap, ASZeroPtrTy, ToWrap->getName());
  Cache[ToWrap] = ACast;
  return ACast;
}

// Wrap a pointer operand OpNum of instruction I
// with cast to address space zero
static void aspaceWrapOperand(DenseMap<Value *, Value *> &Cache, Instruction *I,
                              unsigned OpNum) {
  Value *OldOp = I->getOperand(OpNum);
  if (OldOp->getType()->getPointerAddressSpace() == 0)
    return;

  Value *NewOp = aspaceWrapValue(Cache, I->getFunction(), OldOp);
  I->setOperand(OpNum, NewOp);
  // Check if there are any remaining users of old GEP,
  // delete those w/o users
  for (;;) {
    auto *OldGEP = dyn_cast<GetElementPtrInst>(OldOp);
    if (!OldGEP)
      break;
    if (!OldGEP->use_empty())
      break;
    OldOp = OldGEP->getPointerOperand();
    OldGEP->eraseFromParent();
  }
}

// Support for BPF address spaces:
// - for each function in the module M, update pointer operand of
//   each memory access instruction (load/store/cmpxchg/atomicrmw)
//   by casting it from non-zero address space to zero address space, e.g:
//
//   (load (ptr addrspace (N) %p) ...)
//     -> (load (addrspacecast ptr addrspace (N) %p to ptr))
//
// - assign section with name .addr_space.N for globals defined in
//   non-zero address space N
bool BPFCheckAndAdjustIR::insertASpaceCasts(Module &M) {
  bool Changed = false;
  for (Function &F : M) {
    DenseMap<Value *, Value *> CastsCache;
    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        unsigned PtrOpNum;

        if (auto *LD = dyn_cast<LoadInst>(&I))
          PtrOpNum = LD->getPointerOperandIndex();
        else if (auto *ST = dyn_cast<StoreInst>(&I))
          PtrOpNum = ST->getPointerOperandIndex();
        else if (auto *CmpXchg = dyn_cast<AtomicCmpXchgInst>(&I))
          PtrOpNum = CmpXchg->getPointerOperandIndex();
        else if (auto *RMW = dyn_cast<AtomicRMWInst>(&I))
          PtrOpNum = RMW->getPointerOperandIndex();
        else
          continue;

        aspaceWrapOperand(CastsCache, &I, PtrOpNum);
      }
    }
    Changed |= !CastsCache.empty();
  }
  // Merge all globals within same address space into single
  // .addr_space.<addr space no> section
  for (GlobalVariable &G : M.globals()) {
    if (G.getAddressSpace() == 0 || G.hasSection())
      continue;
    SmallString<16> SecName;
    raw_svector_ostream OS(SecName);
    OS << ".addr_space." << G.getAddressSpace();
    G.setSection(SecName);
    // Prevent having separate section for constants
    G.setConstant(false);
  }
  return Changed;
}

bool BPFCheckAndAdjustIR::adjustIR(Module &M) {
  bool Changed = removePassThroughBuiltin(M);
  Changed = removeCompareBuiltin(M) || Changed;
  Changed = sinkMinMax(M) || Changed;
  Changed = removeGEPBuiltins(M) || Changed;
  Changed = insertASpaceCasts(M) || Changed;
  return Changed;
}

bool BPFCheckAndAdjustIR::runOnModule(Module &M) {
  checkIR(M);
  return adjustIR(M);
}
