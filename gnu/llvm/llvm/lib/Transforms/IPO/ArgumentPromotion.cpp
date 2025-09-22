//===- ArgumentPromotion.cpp - Promote by-reference arguments -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass promotes "by reference" arguments to be "by value" arguments.  In
// practice, this means looking for internal functions that have pointer
// arguments.  If it can prove, through the use of alias analysis, that an
// argument is *only* loaded, then it can pass the value into the function
// instead of the address of the value.  This can cause recursive simplification
// of code and lead to the elimination of allocas (especially in C++ template
// code like the STL).
//
// This pass also handles aggregate arguments that are passed into a function,
// scalarizing them if the elements of the aggregate are only loaded.  Note that
// by default it refuses to scalarize aggregates which would require passing in
// more than three operands to the function, because passing thousands of
// operands for a large array or structure is unprofitable! This limit can be
// configured or disabled, however.
//
// Note that this transformation could also be done for arguments that are only
// stored to (returning the value instead), but does not currently.  This case
// would be best handled when and if LLVM begins supporting multiple return
// values from functions.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/ArgumentPromotion.h"

#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/Loads.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/NoFolder.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/PromoteMemToReg.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "argpromotion"

STATISTIC(NumArgumentsPromoted, "Number of pointer arguments promoted");
STATISTIC(NumArgumentsDead, "Number of dead pointer args eliminated");

namespace {

struct ArgPart {
  Type *Ty;
  Align Alignment;
  /// A representative guaranteed-executed load or store instruction for use by
  /// metadata transfer.
  Instruction *MustExecInstr;
};

using OffsetAndArgPart = std::pair<int64_t, ArgPart>;

} // end anonymous namespace

static Value *createByteGEP(IRBuilderBase &IRB, const DataLayout &DL,
                            Value *Ptr, Type *ResElemTy, int64_t Offset) {
  if (Offset != 0) {
    APInt APOffset(DL.getIndexTypeSizeInBits(Ptr->getType()), Offset);
    Ptr = IRB.CreatePtrAdd(Ptr, IRB.getInt(APOffset));
  }
  return Ptr;
}

/// DoPromotion - This method actually performs the promotion of the specified
/// arguments, and returns the new function.  At this point, we know that it's
/// safe to do so.
static Function *
doPromotion(Function *F, FunctionAnalysisManager &FAM,
            const DenseMap<Argument *, SmallVector<OffsetAndArgPart, 4>>
                &ArgsToPromote) {
  // Start by computing a new prototype for the function, which is the same as
  // the old function, but has modified arguments.
  FunctionType *FTy = F->getFunctionType();
  std::vector<Type *> Params;

  // Attribute - Keep track of the parameter attributes for the arguments
  // that we are *not* promoting. For the ones that we do promote, the parameter
  // attributes are lost
  SmallVector<AttributeSet, 8> ArgAttrVec;
  // Mapping from old to new argument indices. -1 for promoted or removed
  // arguments.
  SmallVector<unsigned> NewArgIndices;
  AttributeList PAL = F->getAttributes();

  // First, determine the new argument list
  unsigned ArgNo = 0, NewArgNo = 0;
  for (Function::arg_iterator I = F->arg_begin(), E = F->arg_end(); I != E;
       ++I, ++ArgNo) {
    if (!ArgsToPromote.count(&*I)) {
      // Unchanged argument
      Params.push_back(I->getType());
      ArgAttrVec.push_back(PAL.getParamAttrs(ArgNo));
      NewArgIndices.push_back(NewArgNo++);
    } else if (I->use_empty()) {
      // Dead argument (which are always marked as promotable)
      ++NumArgumentsDead;
      NewArgIndices.push_back((unsigned)-1);
    } else {
      const auto &ArgParts = ArgsToPromote.find(&*I)->second;
      for (const auto &Pair : ArgParts) {
        Params.push_back(Pair.second.Ty);
        ArgAttrVec.push_back(AttributeSet());
      }
      ++NumArgumentsPromoted;
      NewArgIndices.push_back((unsigned)-1);
      NewArgNo += ArgParts.size();
    }
  }

  Type *RetTy = FTy->getReturnType();

  // Construct the new function type using the new arguments.
  FunctionType *NFTy = FunctionType::get(RetTy, Params, FTy->isVarArg());

  // Create the new function body and insert it into the module.
  Function *NF = Function::Create(NFTy, F->getLinkage(), F->getAddressSpace(),
                                  F->getName());
  NF->copyAttributesFrom(F);
  NF->copyMetadata(F, 0);
  NF->setIsNewDbgInfoFormat(F->IsNewDbgInfoFormat);

  // The new function will have the !dbg metadata copied from the original
  // function. The original function may not be deleted, and dbg metadata need
  // to be unique, so we need to drop it.
  F->setSubprogram(nullptr);

  LLVM_DEBUG(dbgs() << "ARG PROMOTION:  Promoting to:" << *NF << "\n"
                    << "From: " << *F);

  uint64_t LargestVectorWidth = 0;
  for (auto *I : Params)
    if (auto *VT = dyn_cast<llvm::VectorType>(I))
      LargestVectorWidth = std::max(
          LargestVectorWidth, VT->getPrimitiveSizeInBits().getKnownMinValue());

  // Recompute the parameter attributes list based on the new arguments for
  // the function.
  NF->setAttributes(AttributeList::get(F->getContext(), PAL.getFnAttrs(),
                                       PAL.getRetAttrs(), ArgAttrVec));

  // Remap argument indices in allocsize attribute.
  if (auto AllocSize = NF->getAttributes().getFnAttrs().getAllocSizeArgs()) {
    unsigned Arg1 = NewArgIndices[AllocSize->first];
    assert(Arg1 != (unsigned)-1 && "allocsize cannot be promoted argument");
    std::optional<unsigned> Arg2;
    if (AllocSize->second) {
      Arg2 = NewArgIndices[*AllocSize->second];
      assert(Arg2 != (unsigned)-1 && "allocsize cannot be promoted argument");
    }
    NF->addFnAttr(Attribute::getWithAllocSizeArgs(F->getContext(), Arg1, Arg2));
  }

  AttributeFuncs::updateMinLegalVectorWidthAttr(*NF, LargestVectorWidth);
  ArgAttrVec.clear();

  F->getParent()->getFunctionList().insert(F->getIterator(), NF);
  NF->takeName(F);

  // Loop over all the callers of the function, transforming the call sites to
  // pass in the loaded pointers.
  SmallVector<Value *, 16> Args;
  const DataLayout &DL = F->getDataLayout();
  SmallVector<WeakTrackingVH, 16> DeadArgs;

  while (!F->use_empty()) {
    CallBase &CB = cast<CallBase>(*F->user_back());
    assert(CB.getCalledFunction() == F);
    const AttributeList &CallPAL = CB.getAttributes();
    IRBuilder<NoFolder> IRB(&CB);

    // Loop over the operands, inserting GEP and loads in the caller as
    // appropriate.
    auto *AI = CB.arg_begin();
    ArgNo = 0;
    for (Function::arg_iterator I = F->arg_begin(), E = F->arg_end(); I != E;
         ++I, ++AI, ++ArgNo) {
      if (!ArgsToPromote.count(&*I)) {
        Args.push_back(*AI); // Unmodified argument
        ArgAttrVec.push_back(CallPAL.getParamAttrs(ArgNo));
      } else if (!I->use_empty()) {
        Value *V = *AI;
        const auto &ArgParts = ArgsToPromote.find(&*I)->second;
        for (const auto &Pair : ArgParts) {
          LoadInst *LI = IRB.CreateAlignedLoad(
              Pair.second.Ty,
              createByteGEP(IRB, DL, V, Pair.second.Ty, Pair.first),
              Pair.second.Alignment, V->getName() + ".val");
          if (Pair.second.MustExecInstr) {
            LI->setAAMetadata(Pair.second.MustExecInstr->getAAMetadata());
            LI->copyMetadata(*Pair.second.MustExecInstr,
                             {LLVMContext::MD_dereferenceable,
                              LLVMContext::MD_dereferenceable_or_null,
                              LLVMContext::MD_noundef,
                              LLVMContext::MD_nontemporal});
            // Only transfer poison-generating metadata if we also have
            // !noundef.
            // TODO: Without !noundef, we could merge this metadata across
            // all promoted loads.
            if (LI->hasMetadata(LLVMContext::MD_noundef))
              LI->copyMetadata(*Pair.second.MustExecInstr,
                               {LLVMContext::MD_range, LLVMContext::MD_nonnull,
                                LLVMContext::MD_align});
          }
          Args.push_back(LI);
          ArgAttrVec.push_back(AttributeSet());
        }
      } else {
        assert(ArgsToPromote.count(&*I) && I->use_empty());
        DeadArgs.emplace_back(AI->get());
      }
    }

    // Push any varargs arguments on the list.
    for (; AI != CB.arg_end(); ++AI, ++ArgNo) {
      Args.push_back(*AI);
      ArgAttrVec.push_back(CallPAL.getParamAttrs(ArgNo));
    }

    SmallVector<OperandBundleDef, 1> OpBundles;
    CB.getOperandBundlesAsDefs(OpBundles);

    CallBase *NewCS = nullptr;
    if (InvokeInst *II = dyn_cast<InvokeInst>(&CB)) {
      NewCS = InvokeInst::Create(NF, II->getNormalDest(), II->getUnwindDest(),
                                 Args, OpBundles, "", CB.getIterator());
    } else {
      auto *NewCall =
          CallInst::Create(NF, Args, OpBundles, "", CB.getIterator());
      NewCall->setTailCallKind(cast<CallInst>(&CB)->getTailCallKind());
      NewCS = NewCall;
    }
    NewCS->setCallingConv(CB.getCallingConv());
    NewCS->setAttributes(AttributeList::get(F->getContext(),
                                            CallPAL.getFnAttrs(),
                                            CallPAL.getRetAttrs(), ArgAttrVec));
    NewCS->copyMetadata(CB, {LLVMContext::MD_prof, LLVMContext::MD_dbg});
    Args.clear();
    ArgAttrVec.clear();

    AttributeFuncs::updateMinLegalVectorWidthAttr(*CB.getCaller(),
                                                  LargestVectorWidth);

    if (!CB.use_empty()) {
      CB.replaceAllUsesWith(NewCS);
      NewCS->takeName(&CB);
    }

    // Finally, remove the old call from the program, reducing the use-count of
    // F.
    CB.eraseFromParent();
  }

  RecursivelyDeleteTriviallyDeadInstructionsPermissive(DeadArgs);

  // Since we have now created the new function, splice the body of the old
  // function right into the new function, leaving the old rotting hulk of the
  // function empty.
  NF->splice(NF->begin(), F);

  // We will collect all the new created allocas to promote them into registers
  // after the following loop
  SmallVector<AllocaInst *, 4> Allocas;

  // Loop over the argument list, transferring uses of the old arguments over to
  // the new arguments, also transferring over the names as well.
  Function::arg_iterator I2 = NF->arg_begin();
  for (Argument &Arg : F->args()) {
    if (!ArgsToPromote.count(&Arg)) {
      // If this is an unmodified argument, move the name and users over to the
      // new version.
      Arg.replaceAllUsesWith(&*I2);
      I2->takeName(&Arg);
      ++I2;
      continue;
    }

    // There potentially are metadata uses for things like llvm.dbg.value.
    // Replace them with undef, after handling the other regular uses.
    auto RauwUndefMetadata = make_scope_exit(
        [&]() { Arg.replaceAllUsesWith(UndefValue::get(Arg.getType())); });

    if (Arg.use_empty())
      continue;

    // Otherwise, if we promoted this argument, we have to create an alloca in
    // the callee for every promotable part and store each of the new incoming
    // arguments into the corresponding alloca, what lets the old code (the
    // store instructions if they are allowed especially) a chance to work as
    // before.
    assert(Arg.getType()->isPointerTy() &&
           "Only arguments with a pointer type are promotable");

    IRBuilder<NoFolder> IRB(&NF->begin()->front());

    // Add only the promoted elements, so parts from ArgsToPromote
    SmallDenseMap<int64_t, AllocaInst *> OffsetToAlloca;
    for (const auto &Pair : ArgsToPromote.find(&Arg)->second) {
      int64_t Offset = Pair.first;
      const ArgPart &Part = Pair.second;

      Argument *NewArg = I2++;
      NewArg->setName(Arg.getName() + "." + Twine(Offset) + ".val");

      AllocaInst *NewAlloca = IRB.CreateAlloca(
          Part.Ty, nullptr, Arg.getName() + "." + Twine(Offset) + ".allc");
      NewAlloca->setAlignment(Pair.second.Alignment);
      IRB.CreateAlignedStore(NewArg, NewAlloca, Pair.second.Alignment);

      // Collect the alloca to retarget the users to
      OffsetToAlloca.insert({Offset, NewAlloca});
    }

    auto GetAlloca = [&](Value *Ptr) {
      APInt Offset(DL.getIndexTypeSizeInBits(Ptr->getType()), 0);
      Ptr = Ptr->stripAndAccumulateConstantOffsets(DL, Offset,
                                                   /* AllowNonInbounds */ true);
      assert(Ptr == &Arg && "Not constant offset from arg?");
      return OffsetToAlloca.lookup(Offset.getSExtValue());
    };

    // Cleanup the code from the dead instructions: GEPs and BitCasts in between
    // the original argument and its users: loads and stores. Retarget every
    // user to the new created alloca.
    SmallVector<Value *, 16> Worklist;
    SmallVector<Instruction *, 16> DeadInsts;
    append_range(Worklist, Arg.users());
    while (!Worklist.empty()) {
      Value *V = Worklist.pop_back_val();
      if (isa<BitCastInst>(V) || isa<GetElementPtrInst>(V)) {
        DeadInsts.push_back(cast<Instruction>(V));
        append_range(Worklist, V->users());
        continue;
      }

      if (auto *LI = dyn_cast<LoadInst>(V)) {
        Value *Ptr = LI->getPointerOperand();
        LI->setOperand(LoadInst::getPointerOperandIndex(), GetAlloca(Ptr));
        continue;
      }

      if (auto *SI = dyn_cast<StoreInst>(V)) {
        assert(!SI->isVolatile() && "Volatile operations can't be promoted.");
        Value *Ptr = SI->getPointerOperand();
        SI->setOperand(StoreInst::getPointerOperandIndex(), GetAlloca(Ptr));
        continue;
      }

      llvm_unreachable("Unexpected user");
    }

    for (Instruction *I : DeadInsts) {
      I->replaceAllUsesWith(PoisonValue::get(I->getType()));
      I->eraseFromParent();
    }

    // Collect the allocas for promotion
    for (const auto &Pair : OffsetToAlloca) {
      assert(isAllocaPromotable(Pair.second) &&
             "By design, only promotable allocas should be produced.");
      Allocas.push_back(Pair.second);
    }
  }

  LLVM_DEBUG(dbgs() << "ARG PROMOTION: " << Allocas.size()
                    << " alloca(s) are promotable by Mem2Reg\n");

  if (!Allocas.empty()) {
    // And we are able to call the `promoteMemoryToRegister()` function.
    // Our earlier checks have ensured that PromoteMemToReg() will
    // succeed.
    auto &DT = FAM.getResult<DominatorTreeAnalysis>(*NF);
    auto &AC = FAM.getResult<AssumptionAnalysis>(*NF);
    PromoteMemToReg(Allocas, DT, &AC);
  }

  return NF;
}

/// Return true if we can prove that all callees pass in a valid pointer for the
/// specified function argument.
static bool allCallersPassValidPointerForArgument(
    Argument *Arg, SmallPtrSetImpl<CallBase *> &RecursiveCalls,
    Align NeededAlign, uint64_t NeededDerefBytes) {
  Function *Callee = Arg->getParent();
  const DataLayout &DL = Callee->getDataLayout();
  APInt Bytes(64, NeededDerefBytes);

  // Check if the argument itself is marked dereferenceable and aligned.
  if (isDereferenceableAndAlignedPointer(Arg, NeededAlign, Bytes, DL))
    return true;

  // Look at all call sites of the function.  At this point we know we only have
  // direct callees.
  return all_of(Callee->users(), [&](User *U) {
    CallBase &CB = cast<CallBase>(*U);
    // In case of functions with recursive calls, this check
    // (isDereferenceableAndAlignedPointer) will fail when it tries to look at
    // the first caller of this function. The caller may or may not have a load,
    // incase it doesn't load the pointer being passed, this check will fail.
    // So, it's safe to skip the check incase we know that we are dealing with a
    // recursive call. For example we have a IR given below.
    //
    // def fun(ptr %a) {
    //   ...
    //   %loadres = load i32, ptr %a, align 4
    //   %res = call i32 @fun(ptr %a)
    //   ...
    // }
    //
    // def bar(ptr %x) {
    //   ...
    //   %resbar = call i32 @fun(ptr %x)
    //   ...
    // }
    //
    // Since we record processed recursive calls, we check if the current
    // CallBase has been processed before. If yes it means that it is a
    // recursive call and we can skip the check just for this call. So, just
    // return true.
    if (RecursiveCalls.contains(&CB))
      return true;

    return isDereferenceableAndAlignedPointer(CB.getArgOperand(Arg->getArgNo()),
                                              NeededAlign, Bytes, DL);
  });
}

/// Determine that this argument is safe to promote, and find the argument
/// parts it can be promoted into.
static bool findArgParts(Argument *Arg, const DataLayout &DL, AAResults &AAR,
                         unsigned MaxElements, bool IsRecursive,
                         SmallVectorImpl<OffsetAndArgPart> &ArgPartsVec) {
  // Quick exit for unused arguments
  if (Arg->use_empty())
    return true;

  // We can only promote this argument if all the uses are loads at known
  // offsets.
  //
  // Promoting the argument causes it to be loaded in the caller
  // unconditionally. This is only safe if we can prove that either the load
  // would have happened in the callee anyway (ie, there is a load in the entry
  // block) or the pointer passed in at every call site is guaranteed to be
  // valid.
  // In the former case, invalid loads can happen, but would have happened
  // anyway, in the latter case, invalid loads won't happen. This prevents us
  // from introducing an invalid load that wouldn't have happened in the
  // original code.

  SmallDenseMap<int64_t, ArgPart, 4> ArgParts;
  Align NeededAlign(1);
  uint64_t NeededDerefBytes = 0;

  // And if this is a byval argument we also allow to have store instructions.
  // Only handle in such way arguments with specified alignment;
  // if it's unspecified, the actual alignment of the argument is
  // target-specific.
  bool AreStoresAllowed = Arg->getParamByValType() && Arg->getParamAlign();

  // An end user of a pointer argument is a load or store instruction.
  // Returns std::nullopt if this load or store is not based on the argument.
  // Return true if we can promote the instruction, false otherwise.
  auto HandleEndUser = [&](auto *I, Type *Ty,
                           bool GuaranteedToExecute) -> std::optional<bool> {
    // Don't promote volatile or atomic instructions.
    if (!I->isSimple())
      return false;

    Value *Ptr = I->getPointerOperand();
    APInt Offset(DL.getIndexTypeSizeInBits(Ptr->getType()), 0);
    Ptr = Ptr->stripAndAccumulateConstantOffsets(DL, Offset,
                                                 /* AllowNonInbounds */ true);
    if (Ptr != Arg)
      return std::nullopt;

    if (Offset.getSignificantBits() >= 64)
      return false;

    TypeSize Size = DL.getTypeStoreSize(Ty);
    // Don't try to promote scalable types.
    if (Size.isScalable())
      return false;

    // If this is a recursive function and one of the types is a pointer,
    // then promoting it might lead to recursive promotion.
    if (IsRecursive && Ty->isPointerTy())
      return false;

    int64_t Off = Offset.getSExtValue();
    auto Pair = ArgParts.try_emplace(
        Off, ArgPart{Ty, I->getAlign(), GuaranteedToExecute ? I : nullptr});
    ArgPart &Part = Pair.first->second;
    bool OffsetNotSeenBefore = Pair.second;

    // We limit promotion to only promoting up to a fixed number of elements of
    // the aggregate.
    if (MaxElements > 0 && ArgParts.size() > MaxElements) {
      LLVM_DEBUG(dbgs() << "ArgPromotion of " << *Arg << " failed: "
                        << "more than " << MaxElements << " parts\n");
      return false;
    }

    // For now, we only support loading/storing one specific type at a given
    // offset.
    if (Part.Ty != Ty) {
      LLVM_DEBUG(dbgs() << "ArgPromotion of " << *Arg << " failed: "
                        << "accessed as both " << *Part.Ty << " and " << *Ty
                        << " at offset " << Off << "\n");
      return false;
    }

    // If this instruction is not guaranteed to execute, and we haven't seen a
    // load or store at this offset before (or it had lower alignment), then we
    // need to remember that requirement.
    // Note that skipping instructions of previously seen offsets is only
    // correct because we only allow a single type for a given offset, which
    // also means that the number of accessed bytes will be the same.
    if (!GuaranteedToExecute &&
        (OffsetNotSeenBefore || Part.Alignment < I->getAlign())) {
      // We won't be able to prove dereferenceability for negative offsets.
      if (Off < 0)
        return false;

      // If the offset is not aligned, an aligned base pointer won't help.
      if (!isAligned(I->getAlign(), Off))
        return false;

      NeededDerefBytes = std::max(NeededDerefBytes, Off + Size.getFixedValue());
      NeededAlign = std::max(NeededAlign, I->getAlign());
    }

    Part.Alignment = std::max(Part.Alignment, I->getAlign());
    return true;
  };

  // Look for loads and stores that are guaranteed to execute on entry.
  for (Instruction &I : Arg->getParent()->getEntryBlock()) {
    std::optional<bool> Res{};
    if (LoadInst *LI = dyn_cast<LoadInst>(&I))
      Res = HandleEndUser(LI, LI->getType(), /* GuaranteedToExecute */ true);
    else if (StoreInst *SI = dyn_cast<StoreInst>(&I))
      Res = HandleEndUser(SI, SI->getValueOperand()->getType(),
                          /* GuaranteedToExecute */ true);
    if (Res && !*Res)
      return false;

    if (!isGuaranteedToTransferExecutionToSuccessor(&I))
      break;
  }

  // Now look at all loads of the argument. Remember the load instructions
  // for the aliasing check below.
  SmallVector<const Use *, 16> Worklist;
  SmallPtrSet<const Use *, 16> Visited;
  SmallVector<LoadInst *, 16> Loads;
  SmallPtrSet<CallBase *, 4> RecursiveCalls;
  auto AppendUses = [&](const Value *V) {
    for (const Use &U : V->uses())
      if (Visited.insert(&U).second)
        Worklist.push_back(&U);
  };
  AppendUses(Arg);
  while (!Worklist.empty()) {
    const Use *U = Worklist.pop_back_val();
    Value *V = U->getUser();
    if (isa<BitCastInst>(V)) {
      AppendUses(V);
      continue;
    }

    if (auto *GEP = dyn_cast<GetElementPtrInst>(V)) {
      if (!GEP->hasAllConstantIndices())
        return false;
      AppendUses(V);
      continue;
    }

    if (auto *LI = dyn_cast<LoadInst>(V)) {
      if (!*HandleEndUser(LI, LI->getType(), /* GuaranteedToExecute */ false))
        return false;
      Loads.push_back(LI);
      continue;
    }

    // Stores are allowed for byval arguments
    auto *SI = dyn_cast<StoreInst>(V);
    if (AreStoresAllowed && SI &&
        U->getOperandNo() == StoreInst::getPointerOperandIndex()) {
      if (!*HandleEndUser(SI, SI->getValueOperand()->getType(),
                          /* GuaranteedToExecute */ false))
        return false;
      continue;
      // Only stores TO the argument is allowed, all the other stores are
      // unknown users
    }

    auto *CB = dyn_cast<CallBase>(V);
    Value *PtrArg = U->get();
    if (CB && CB->getCalledFunction() == CB->getFunction()) {
      if (PtrArg != Arg) {
        LLVM_DEBUG(dbgs() << "ArgPromotion of " << *Arg << " failed: "
                          << "pointer offset is not equal to zero\n");
        return false;
      }

      unsigned int ArgNo = Arg->getArgNo();
      if (U->getOperandNo() != ArgNo) {
        LLVM_DEBUG(dbgs() << "ArgPromotion of " << *Arg << " failed: "
                          << "arg position is different in callee\n");
        return false;
      }

      // We limit promotion to only promoting up to a fixed number of elements
      // of the aggregate.
      if (MaxElements > 0 && ArgParts.size() > MaxElements) {
        LLVM_DEBUG(dbgs() << "ArgPromotion of " << *Arg << " failed: "
                          << "more than " << MaxElements << " parts\n");
        return false;
      }

      RecursiveCalls.insert(CB);
      continue;
    }
    // Unknown user.
    LLVM_DEBUG(dbgs() << "ArgPromotion of " << *Arg << " failed: "
                      << "unknown user " << *V << "\n");
    return false;
  }

  if (NeededDerefBytes || NeededAlign > 1) {
    // Try to prove a required deref / aligned requirement.
    if (!allCallersPassValidPointerForArgument(Arg, RecursiveCalls, NeededAlign,
                                               NeededDerefBytes)) {
      LLVM_DEBUG(dbgs() << "ArgPromotion of " << *Arg << " failed: "
                        << "not dereferenceable or aligned\n");
      return false;
    }
  }

  if (ArgParts.empty())
    return true; // No users, this is a dead argument.

  // Sort parts by offset.
  append_range(ArgPartsVec, ArgParts);
  sort(ArgPartsVec, llvm::less_first());

  // Make sure the parts are non-overlapping.
  int64_t Offset = ArgPartsVec[0].first;
  for (const auto &Pair : ArgPartsVec) {
    if (Pair.first < Offset)
      return false; // Overlap with previous part.

    Offset = Pair.first + DL.getTypeStoreSize(Pair.second.Ty);
  }

  // If store instructions are allowed, the path from the entry of the function
  // to each load may be not free of instructions that potentially invalidate
  // the load, and this is an admissible situation.
  if (AreStoresAllowed)
    return true;

  // Okay, now we know that the argument is only used by load instructions, and
  // it is safe to unconditionally perform all of them. Use alias analysis to
  // check to see if the pointer is guaranteed to not be modified from entry of
  // the function to each of the load instructions.

  for (LoadInst *Load : Loads) {
    // Check to see if the load is invalidated from the start of the block to
    // the load itself.
    BasicBlock *BB = Load->getParent();

    MemoryLocation Loc = MemoryLocation::get(Load);
    if (AAR.canInstructionRangeModRef(BB->front(), *Load, Loc, ModRefInfo::Mod))
      return false; // Pointer is invalidated!

    // Now check every path from the entry block to the load for transparency.
    // To do this, we perform a depth first search on the inverse CFG from the
    // loading block.
    for (BasicBlock *P : predecessors(BB)) {
      for (BasicBlock *TranspBB : inverse_depth_first(P))
        if (AAR.canBasicBlockModify(*TranspBB, Loc))
          return false;
    }
  }

  // If the path from the entry of the function to each load is free of
  // instructions that potentially invalidate the load, we can make the
  // transformation!
  return true;
}

/// Check if callers and callee agree on how promoted arguments would be
/// passed.
static bool areTypesABICompatible(ArrayRef<Type *> Types, const Function &F,
                                  const TargetTransformInfo &TTI) {
  return all_of(F.uses(), [&](const Use &U) {
    CallBase *CB = dyn_cast<CallBase>(U.getUser());
    if (!CB)
      return false;

    const Function *Caller = CB->getCaller();
    const Function *Callee = CB->getCalledFunction();
    return TTI.areTypesABICompatible(Caller, Callee, Types);
  });
}

/// PromoteArguments - This method checks the specified function to see if there
/// are any promotable arguments and if it is safe to promote the function (for
/// example, all callers are direct).  If safe to promote some arguments, it
/// calls the DoPromotion method.
static Function *promoteArguments(Function *F, FunctionAnalysisManager &FAM,
                                  unsigned MaxElements, bool IsRecursive) {
  // Don't perform argument promotion for naked functions; otherwise we can end
  // up removing parameters that are seemingly 'not used' as they are referred
  // to in the assembly.
  if (F->hasFnAttribute(Attribute::Naked))
    return nullptr;

  // Make sure that it is local to this module.
  if (!F->hasLocalLinkage())
    return nullptr;

  // Don't promote arguments for variadic functions. Adding, removing, or
  // changing non-pack parameters can change the classification of pack
  // parameters. Frontends encode that classification at the call site in the
  // IR, while in the callee the classification is determined dynamically based
  // on the number of registers consumed so far.
  if (F->isVarArg())
    return nullptr;

  // Don't transform functions that receive inallocas, as the transformation may
  // not be safe depending on calling convention.
  if (F->getAttributes().hasAttrSomewhere(Attribute::InAlloca))
    return nullptr;

  // First check: see if there are any pointer arguments!  If not, quick exit.
  SmallVector<Argument *, 16> PointerArgs;
  for (Argument &I : F->args())
    if (I.getType()->isPointerTy())
      PointerArgs.push_back(&I);
  if (PointerArgs.empty())
    return nullptr;

  // Second check: make sure that all callers are direct callers.  We can't
  // transform functions that have indirect callers.  Also see if the function
  // is self-recursive.
  for (Use &U : F->uses()) {
    CallBase *CB = dyn_cast<CallBase>(U.getUser());
    // Must be a direct call.
    if (CB == nullptr || !CB->isCallee(&U) ||
        CB->getFunctionType() != F->getFunctionType())
      return nullptr;

    // Can't change signature of musttail callee
    if (CB->isMustTailCall())
      return nullptr;

    if (CB->getFunction() == F)
      IsRecursive = true;
  }

  // Can't change signature of musttail caller
  // FIXME: Support promoting whole chain of musttail functions
  for (BasicBlock &BB : *F)
    if (BB.getTerminatingMustTailCall())
      return nullptr;

  const DataLayout &DL = F->getDataLayout();
  auto &AAR = FAM.getResult<AAManager>(*F);
  const auto &TTI = FAM.getResult<TargetIRAnalysis>(*F);

  // Check to see which arguments are promotable.  If an argument is promotable,
  // add it to ArgsToPromote.
  DenseMap<Argument *, SmallVector<OffsetAndArgPart, 4>> ArgsToPromote;
  unsigned NumArgsAfterPromote = F->getFunctionType()->getNumParams();
  for (Argument *PtrArg : PointerArgs) {
    // Replace sret attribute with noalias. This reduces register pressure by
    // avoiding a register copy.
    if (PtrArg->hasStructRetAttr()) {
      unsigned ArgNo = PtrArg->getArgNo();
      F->removeParamAttr(ArgNo, Attribute::StructRet);
      F->addParamAttr(ArgNo, Attribute::NoAlias);
      for (Use &U : F->uses()) {
        CallBase &CB = cast<CallBase>(*U.getUser());
        CB.removeParamAttr(ArgNo, Attribute::StructRet);
        CB.addParamAttr(ArgNo, Attribute::NoAlias);
      }
    }

    // If we can promote the pointer to its value.
    SmallVector<OffsetAndArgPart, 4> ArgParts;

    if (findArgParts(PtrArg, DL, AAR, MaxElements, IsRecursive, ArgParts)) {
      SmallVector<Type *, 4> Types;
      for (const auto &Pair : ArgParts)
        Types.push_back(Pair.second.Ty);

      if (areTypesABICompatible(Types, *F, TTI)) {
        NumArgsAfterPromote += ArgParts.size() - 1;
        ArgsToPromote.insert({PtrArg, std::move(ArgParts)});
      }
    }
  }

  // No promotable pointer arguments.
  if (ArgsToPromote.empty())
    return nullptr;

  if (NumArgsAfterPromote > TTI.getMaxNumArgs())
    return nullptr;

  return doPromotion(F, FAM, ArgsToPromote);
}

PreservedAnalyses ArgumentPromotionPass::run(LazyCallGraph::SCC &C,
                                             CGSCCAnalysisManager &AM,
                                             LazyCallGraph &CG,
                                             CGSCCUpdateResult &UR) {
  bool Changed = false, LocalChange;

  // Iterate until we stop promoting from this SCC.
  do {
    LocalChange = false;

    FunctionAnalysisManager &FAM =
        AM.getResult<FunctionAnalysisManagerCGSCCProxy>(C, CG).getManager();

    bool IsRecursive = C.size() > 1;
    for (LazyCallGraph::Node &N : C) {
      Function &OldF = N.getFunction();
      Function *NewF = promoteArguments(&OldF, FAM, MaxElements, IsRecursive);
      if (!NewF)
        continue;
      LocalChange = true;

      // Directly substitute the functions in the call graph. Note that this
      // requires the old function to be completely dead and completely
      // replaced by the new function. It does no call graph updates, it merely
      // swaps out the particular function mapped to a particular node in the
      // graph.
      C.getOuterRefSCC().replaceNodeFunction(N, *NewF);
      FAM.clear(OldF, OldF.getName());
      OldF.eraseFromParent();

      PreservedAnalyses FuncPA;
      FuncPA.preserveSet<CFGAnalyses>();
      for (auto *U : NewF->users()) {
        auto *UserF = cast<CallBase>(U)->getFunction();
        FAM.invalidate(*UserF, FuncPA);
      }
    }

    Changed |= LocalChange;
  } while (LocalChange);

  if (!Changed)
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  // We've cleared out analyses for deleted functions.
  PA.preserve<FunctionAnalysisManagerCGSCCProxy>();
  // We've manually invalidated analyses for functions we've modified.
  PA.preserveSet<AllAnalysesOn<Function>>();
  return PA;
}
