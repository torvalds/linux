//===- Evaluator.cpp - LLVM IR evaluator ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Function evaluator for LLVM IR.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/Evaluator.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "evaluator"

using namespace llvm;

static inline bool
isSimpleEnoughValueToCommit(Constant *C,
                            SmallPtrSetImpl<Constant *> &SimpleConstants,
                            const DataLayout &DL);

/// Return true if the specified constant can be handled by the code generator.
/// We don't want to generate something like:
///   void *X = &X/42;
/// because the code generator doesn't have a relocation that can handle that.
///
/// This function should be called if C was not found (but just got inserted)
/// in SimpleConstants to avoid having to rescan the same constants all the
/// time.
static bool
isSimpleEnoughValueToCommitHelper(Constant *C,
                                  SmallPtrSetImpl<Constant *> &SimpleConstants,
                                  const DataLayout &DL) {
  // Simple global addresses are supported, do not allow dllimport or
  // thread-local globals.
  if (auto *GV = dyn_cast<GlobalValue>(C))
    return !GV->hasDLLImportStorageClass() && !GV->isThreadLocal();

  // Simple integer, undef, constant aggregate zero, etc are all supported.
  if (C->getNumOperands() == 0 || isa<BlockAddress>(C))
    return true;

  // Aggregate values are safe if all their elements are.
  if (isa<ConstantAggregate>(C)) {
    for (Value *Op : C->operands())
      if (!isSimpleEnoughValueToCommit(cast<Constant>(Op), SimpleConstants, DL))
        return false;
    return true;
  }

  // We don't know exactly what relocations are allowed in constant expressions,
  // so we allow &global+constantoffset, which is safe and uniformly supported
  // across targets.
  ConstantExpr *CE = cast<ConstantExpr>(C);
  switch (CE->getOpcode()) {
  case Instruction::BitCast:
    // Bitcast is fine if the casted value is fine.
    return isSimpleEnoughValueToCommit(CE->getOperand(0), SimpleConstants, DL);

  case Instruction::IntToPtr:
  case Instruction::PtrToInt:
    // int <=> ptr is fine if the int type is the same size as the
    // pointer type.
    if (DL.getTypeSizeInBits(CE->getType()) !=
        DL.getTypeSizeInBits(CE->getOperand(0)->getType()))
      return false;
    return isSimpleEnoughValueToCommit(CE->getOperand(0), SimpleConstants, DL);

  // GEP is fine if it is simple + constant offset.
  case Instruction::GetElementPtr:
    for (unsigned i = 1, e = CE->getNumOperands(); i != e; ++i)
      if (!isa<ConstantInt>(CE->getOperand(i)))
        return false;
    return isSimpleEnoughValueToCommit(CE->getOperand(0), SimpleConstants, DL);

  case Instruction::Add:
    // We allow simple+cst.
    if (!isa<ConstantInt>(CE->getOperand(1)))
      return false;
    return isSimpleEnoughValueToCommit(CE->getOperand(0), SimpleConstants, DL);
  }
  return false;
}

static inline bool
isSimpleEnoughValueToCommit(Constant *C,
                            SmallPtrSetImpl<Constant *> &SimpleConstants,
                            const DataLayout &DL) {
  // If we already checked this constant, we win.
  if (!SimpleConstants.insert(C).second)
    return true;
  // Check the constant.
  return isSimpleEnoughValueToCommitHelper(C, SimpleConstants, DL);
}

void Evaluator::MutableValue::clear() {
  if (auto *Agg = dyn_cast_if_present<MutableAggregate *>(Val))
    delete Agg;
  Val = nullptr;
}

Constant *Evaluator::MutableValue::read(Type *Ty, APInt Offset,
                                        const DataLayout &DL) const {
  TypeSize TySize = DL.getTypeStoreSize(Ty);
  const MutableValue *V = this;
  while (const auto *Agg = dyn_cast_if_present<MutableAggregate *>(V->Val)) {
    Type *AggTy = Agg->Ty;
    std::optional<APInt> Index = DL.getGEPIndexForOffset(AggTy, Offset);
    if (!Index || Index->uge(Agg->Elements.size()) ||
        !TypeSize::isKnownLE(TySize, DL.getTypeStoreSize(AggTy)))
      return nullptr;

    V = &Agg->Elements[Index->getZExtValue()];
  }

  return ConstantFoldLoadFromConst(cast<Constant *>(V->Val), Ty, Offset, DL);
}

bool Evaluator::MutableValue::makeMutable() {
  Constant *C = cast<Constant *>(Val);
  Type *Ty = C->getType();
  unsigned NumElements;
  if (auto *VT = dyn_cast<FixedVectorType>(Ty)) {
    NumElements = VT->getNumElements();
  } else if (auto *AT = dyn_cast<ArrayType>(Ty))
    NumElements = AT->getNumElements();
  else if (auto *ST = dyn_cast<StructType>(Ty))
    NumElements = ST->getNumElements();
  else
    return false;

  MutableAggregate *MA = new MutableAggregate(Ty);
  MA->Elements.reserve(NumElements);
  for (unsigned I = 0; I < NumElements; ++I)
    MA->Elements.push_back(C->getAggregateElement(I));
  Val = MA;
  return true;
}

bool Evaluator::MutableValue::write(Constant *V, APInt Offset,
                                    const DataLayout &DL) {
  Type *Ty = V->getType();
  TypeSize TySize = DL.getTypeStoreSize(Ty);
  MutableValue *MV = this;
  while (Offset != 0 ||
         !CastInst::isBitOrNoopPointerCastable(Ty, MV->getType(), DL)) {
    if (isa<Constant *>(MV->Val) && !MV->makeMutable())
      return false;

    MutableAggregate *Agg = cast<MutableAggregate *>(MV->Val);
    Type *AggTy = Agg->Ty;
    std::optional<APInt> Index = DL.getGEPIndexForOffset(AggTy, Offset);
    if (!Index || Index->uge(Agg->Elements.size()) ||
        !TypeSize::isKnownLE(TySize, DL.getTypeStoreSize(AggTy)))
      return false;

    MV = &Agg->Elements[Index->getZExtValue()];
  }

  Type *MVType = MV->getType();
  MV->clear();
  if (Ty->isIntegerTy() && MVType->isPointerTy())
    MV->Val = ConstantExpr::getIntToPtr(V, MVType);
  else if (Ty->isPointerTy() && MVType->isIntegerTy())
    MV->Val = ConstantExpr::getPtrToInt(V, MVType);
  else if (Ty != MVType)
    MV->Val = ConstantExpr::getBitCast(V, MVType);
  else
    MV->Val = V;
  return true;
}

Constant *Evaluator::MutableAggregate::toConstant() const {
  SmallVector<Constant *, 32> Consts;
  for (const MutableValue &MV : Elements)
    Consts.push_back(MV.toConstant());

  if (auto *ST = dyn_cast<StructType>(Ty))
    return ConstantStruct::get(ST, Consts);
  if (auto *AT = dyn_cast<ArrayType>(Ty))
    return ConstantArray::get(AT, Consts);
  assert(isa<FixedVectorType>(Ty) && "Must be vector");
  return ConstantVector::get(Consts);
}

/// Return the value that would be computed by a load from P after the stores
/// reflected by 'memory' have been performed.  If we can't decide, return null.
Constant *Evaluator::ComputeLoadResult(Constant *P, Type *Ty) {
  APInt Offset(DL.getIndexTypeSizeInBits(P->getType()), 0);
  P = cast<Constant>(P->stripAndAccumulateConstantOffsets(
      DL, Offset, /* AllowNonInbounds */ true));
  Offset = Offset.sextOrTrunc(DL.getIndexTypeSizeInBits(P->getType()));
  if (auto *GV = dyn_cast<GlobalVariable>(P))
    return ComputeLoadResult(GV, Ty, Offset);
  return nullptr;
}

Constant *Evaluator::ComputeLoadResult(GlobalVariable *GV, Type *Ty,
                                       const APInt &Offset) {
  auto It = MutatedMemory.find(GV);
  if (It != MutatedMemory.end())
    return It->second.read(Ty, Offset, DL);

  if (!GV->hasDefinitiveInitializer())
    return nullptr;
  return ConstantFoldLoadFromConst(GV->getInitializer(), Ty, Offset, DL);
}

static Function *getFunction(Constant *C) {
  if (auto *Fn = dyn_cast<Function>(C))
    return Fn;

  if (auto *Alias = dyn_cast<GlobalAlias>(C))
    if (auto *Fn = dyn_cast<Function>(Alias->getAliasee()))
      return Fn;
  return nullptr;
}

Function *
Evaluator::getCalleeWithFormalArgs(CallBase &CB,
                                   SmallVectorImpl<Constant *> &Formals) {
  auto *V = CB.getCalledOperand()->stripPointerCasts();
  if (auto *Fn = getFunction(getVal(V)))
    return getFormalParams(CB, Fn, Formals) ? Fn : nullptr;
  return nullptr;
}

bool Evaluator::getFormalParams(CallBase &CB, Function *F,
                                SmallVectorImpl<Constant *> &Formals) {
  if (!F)
    return false;

  auto *FTy = F->getFunctionType();
  if (FTy->getNumParams() > CB.arg_size()) {
    LLVM_DEBUG(dbgs() << "Too few arguments for function.\n");
    return false;
  }

  auto ArgI = CB.arg_begin();
  for (Type *PTy : FTy->params()) {
    auto *ArgC = ConstantFoldLoadThroughBitcast(getVal(*ArgI), PTy, DL);
    if (!ArgC) {
      LLVM_DEBUG(dbgs() << "Can not convert function argument.\n");
      return false;
    }
    Formals.push_back(ArgC);
    ++ArgI;
  }
  return true;
}

/// If call expression contains bitcast then we may need to cast
/// evaluated return value to a type of the call expression.
Constant *Evaluator::castCallResultIfNeeded(Type *ReturnType, Constant *RV) {
  if (!RV || RV->getType() == ReturnType)
    return RV;

  RV = ConstantFoldLoadThroughBitcast(RV, ReturnType, DL);
  if (!RV)
    LLVM_DEBUG(dbgs() << "Failed to fold bitcast call expr\n");
  return RV;
}

/// Evaluate all instructions in block BB, returning true if successful, false
/// if we can't evaluate it.  NewBB returns the next BB that control flows into,
/// or null upon return. StrippedPointerCastsForAliasAnalysis is set to true if
/// we looked through pointer casts to evaluate something.
bool Evaluator::EvaluateBlock(BasicBlock::iterator CurInst, BasicBlock *&NextBB,
                              bool &StrippedPointerCastsForAliasAnalysis) {
  // This is the main evaluation loop.
  while (true) {
    Constant *InstResult = nullptr;

    LLVM_DEBUG(dbgs() << "Evaluating Instruction: " << *CurInst << "\n");

    if (StoreInst *SI = dyn_cast<StoreInst>(CurInst)) {
      if (SI->isVolatile()) {
        LLVM_DEBUG(dbgs() << "Store is volatile! Can not evaluate.\n");
        return false;  // no volatile accesses.
      }
      Constant *Ptr = getVal(SI->getOperand(1));
      Constant *FoldedPtr = ConstantFoldConstant(Ptr, DL, TLI);
      if (Ptr != FoldedPtr) {
        LLVM_DEBUG(dbgs() << "Folding constant ptr expression: " << *Ptr);
        Ptr = FoldedPtr;
        LLVM_DEBUG(dbgs() << "; To: " << *Ptr << "\n");
      }

      APInt Offset(DL.getIndexTypeSizeInBits(Ptr->getType()), 0);
      Ptr = cast<Constant>(Ptr->stripAndAccumulateConstantOffsets(
          DL, Offset, /* AllowNonInbounds */ true));
      Offset = Offset.sextOrTrunc(DL.getIndexTypeSizeInBits(Ptr->getType()));
      auto *GV = dyn_cast<GlobalVariable>(Ptr);
      if (!GV || !GV->hasUniqueInitializer()) {
        LLVM_DEBUG(dbgs() << "Store is not to global with unique initializer: "
                          << *Ptr << "\n");
        return false;
      }

      // If this might be too difficult for the backend to handle (e.g. the addr
      // of one global variable divided by another) then we can't commit it.
      Constant *Val = getVal(SI->getOperand(0));
      if (!isSimpleEnoughValueToCommit(Val, SimpleConstants, DL)) {
        LLVM_DEBUG(dbgs() << "Store value is too complex to evaluate store. "
                          << *Val << "\n");
        return false;
      }

      auto Res = MutatedMemory.try_emplace(GV, GV->getInitializer());
      if (!Res.first->second.write(Val, Offset, DL))
        return false;
    } else if (LoadInst *LI = dyn_cast<LoadInst>(CurInst)) {
      if (LI->isVolatile()) {
        LLVM_DEBUG(
            dbgs() << "Found a Load! Volatile load, can not evaluate.\n");
        return false;  // no volatile accesses.
      }

      Constant *Ptr = getVal(LI->getOperand(0));
      Constant *FoldedPtr = ConstantFoldConstant(Ptr, DL, TLI);
      if (Ptr != FoldedPtr) {
        Ptr = FoldedPtr;
        LLVM_DEBUG(dbgs() << "Found a constant pointer expression, constant "
                             "folding: "
                          << *Ptr << "\n");
      }
      InstResult = ComputeLoadResult(Ptr, LI->getType());
      if (!InstResult) {
        LLVM_DEBUG(
            dbgs() << "Failed to compute load result. Can not evaluate load."
                      "\n");
        return false; // Could not evaluate load.
      }

      LLVM_DEBUG(dbgs() << "Evaluated load: " << *InstResult << "\n");
    } else if (AllocaInst *AI = dyn_cast<AllocaInst>(CurInst)) {
      if (AI->isArrayAllocation()) {
        LLVM_DEBUG(dbgs() << "Found an array alloca. Can not evaluate.\n");
        return false;  // Cannot handle array allocs.
      }
      Type *Ty = AI->getAllocatedType();
      AllocaTmps.push_back(std::make_unique<GlobalVariable>(
          Ty, false, GlobalValue::InternalLinkage, UndefValue::get(Ty),
          AI->getName(), /*TLMode=*/GlobalValue::NotThreadLocal,
          AI->getType()->getPointerAddressSpace()));
      InstResult = AllocaTmps.back().get();
      LLVM_DEBUG(dbgs() << "Found an alloca. Result: " << *InstResult << "\n");
    } else if (isa<CallInst>(CurInst) || isa<InvokeInst>(CurInst)) {
      CallBase &CB = *cast<CallBase>(&*CurInst);

      // Debug info can safely be ignored here.
      if (isa<DbgInfoIntrinsic>(CB)) {
        LLVM_DEBUG(dbgs() << "Ignoring debug info.\n");
        ++CurInst;
        continue;
      }

      // Cannot handle inline asm.
      if (CB.isInlineAsm()) {
        LLVM_DEBUG(dbgs() << "Found inline asm, can not evaluate.\n");
        return false;
      }

      if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(&CB)) {
        if (MemSetInst *MSI = dyn_cast<MemSetInst>(II)) {
          if (MSI->isVolatile()) {
            LLVM_DEBUG(dbgs() << "Can not optimize a volatile memset "
                              << "intrinsic.\n");
            return false;
          }

          auto *LenC = dyn_cast<ConstantInt>(getVal(MSI->getLength()));
          if (!LenC) {
            LLVM_DEBUG(dbgs() << "Memset with unknown length.\n");
            return false;
          }

          Constant *Ptr = getVal(MSI->getDest());
          APInt Offset(DL.getIndexTypeSizeInBits(Ptr->getType()), 0);
          Ptr = cast<Constant>(Ptr->stripAndAccumulateConstantOffsets(
              DL, Offset, /* AllowNonInbounds */ true));
          auto *GV = dyn_cast<GlobalVariable>(Ptr);
          if (!GV) {
            LLVM_DEBUG(dbgs() << "Memset with unknown base.\n");
            return false;
          }

          Constant *Val = getVal(MSI->getValue());
          // Avoid the byte-per-byte scan if we're memseting a zeroinitializer
          // to zero.
          if (!Val->isNullValue() || MutatedMemory.contains(GV) ||
              !GV->hasDefinitiveInitializer() ||
              !GV->getInitializer()->isNullValue()) {
            APInt Len = LenC->getValue();
            if (Len.ugt(64 * 1024)) {
              LLVM_DEBUG(dbgs() << "Not evaluating large memset of size "
                                << Len << "\n");
              return false;
            }

            while (Len != 0) {
              Constant *DestVal = ComputeLoadResult(GV, Val->getType(), Offset);
              if (DestVal != Val) {
                LLVM_DEBUG(dbgs() << "Memset is not a no-op at offset "
                                  << Offset << " of " << *GV << ".\n");
                return false;
              }
              ++Offset;
              --Len;
            }
          }

          LLVM_DEBUG(dbgs() << "Ignoring no-op memset.\n");
          ++CurInst;
          continue;
        }

        if (II->isLifetimeStartOrEnd()) {
          LLVM_DEBUG(dbgs() << "Ignoring lifetime intrinsic.\n");
          ++CurInst;
          continue;
        }

        if (II->getIntrinsicID() == Intrinsic::invariant_start) {
          // We don't insert an entry into Values, as it doesn't have a
          // meaningful return value.
          if (!II->use_empty()) {
            LLVM_DEBUG(dbgs()
                       << "Found unused invariant_start. Can't evaluate.\n");
            return false;
          }
          ConstantInt *Size = cast<ConstantInt>(II->getArgOperand(0));
          Value *PtrArg = getVal(II->getArgOperand(1));
          Value *Ptr = PtrArg->stripPointerCasts();
          if (GlobalVariable *GV = dyn_cast<GlobalVariable>(Ptr)) {
            Type *ElemTy = GV->getValueType();
            if (!Size->isMinusOne() &&
                Size->getValue().getLimitedValue() >=
                    DL.getTypeStoreSize(ElemTy)) {
              Invariants.insert(GV);
              LLVM_DEBUG(dbgs() << "Found a global var that is an invariant: "
                                << *GV << "\n");
            } else {
              LLVM_DEBUG(dbgs()
                         << "Found a global var, but can not treat it as an "
                            "invariant.\n");
            }
          }
          // Continue even if we do nothing.
          ++CurInst;
          continue;
        } else if (II->getIntrinsicID() == Intrinsic::assume) {
          LLVM_DEBUG(dbgs() << "Skipping assume intrinsic.\n");
          ++CurInst;
          continue;
        } else if (II->getIntrinsicID() == Intrinsic::sideeffect) {
          LLVM_DEBUG(dbgs() << "Skipping sideeffect intrinsic.\n");
          ++CurInst;
          continue;
        } else if (II->getIntrinsicID() == Intrinsic::pseudoprobe) {
          LLVM_DEBUG(dbgs() << "Skipping pseudoprobe intrinsic.\n");
          ++CurInst;
          continue;
        } else {
          Value *Stripped = CurInst->stripPointerCastsForAliasAnalysis();
          // Only attempt to getVal() if we've actually managed to strip
          // anything away, or else we'll call getVal() on the current
          // instruction.
          if (Stripped != &*CurInst) {
            InstResult = getVal(Stripped);
          }
          if (InstResult) {
            LLVM_DEBUG(dbgs()
                       << "Stripped pointer casts for alias analysis for "
                          "intrinsic call.\n");
            StrippedPointerCastsForAliasAnalysis = true;
            InstResult = ConstantExpr::getBitCast(InstResult, II->getType());
          } else {
            LLVM_DEBUG(dbgs() << "Unknown intrinsic. Cannot evaluate.\n");
            return false;
          }
        }
      }

      if (!InstResult) {
        // Resolve function pointers.
        SmallVector<Constant *, 8> Formals;
        Function *Callee = getCalleeWithFormalArgs(CB, Formals);
        if (!Callee || Callee->isInterposable()) {
          LLVM_DEBUG(dbgs() << "Can not resolve function pointer.\n");
          return false; // Cannot resolve.
        }

        if (Callee->isDeclaration()) {
          // If this is a function we can constant fold, do it.
          if (Constant *C = ConstantFoldCall(&CB, Callee, Formals, TLI)) {
            InstResult = castCallResultIfNeeded(CB.getType(), C);
            if (!InstResult)
              return false;
            LLVM_DEBUG(dbgs() << "Constant folded function call. Result: "
                              << *InstResult << "\n");
          } else {
            LLVM_DEBUG(dbgs() << "Can not constant fold function call.\n");
            return false;
          }
        } else {
          if (Callee->getFunctionType()->isVarArg()) {
            LLVM_DEBUG(dbgs()
                       << "Can not constant fold vararg function call.\n");
            return false;
          }

          Constant *RetVal = nullptr;
          // Execute the call, if successful, use the return value.
          ValueStack.emplace_back();
          if (!EvaluateFunction(Callee, RetVal, Formals)) {
            LLVM_DEBUG(dbgs() << "Failed to evaluate function.\n");
            return false;
          }
          ValueStack.pop_back();
          InstResult = castCallResultIfNeeded(CB.getType(), RetVal);
          if (RetVal && !InstResult)
            return false;

          if (InstResult) {
            LLVM_DEBUG(dbgs() << "Successfully evaluated function. Result: "
                              << *InstResult << "\n\n");
          } else {
            LLVM_DEBUG(dbgs()
                       << "Successfully evaluated function. Result: 0\n\n");
          }
        }
      }
    } else if (CurInst->isTerminator()) {
      LLVM_DEBUG(dbgs() << "Found a terminator instruction.\n");

      if (BranchInst *BI = dyn_cast<BranchInst>(CurInst)) {
        if (BI->isUnconditional()) {
          NextBB = BI->getSuccessor(0);
        } else {
          ConstantInt *Cond =
            dyn_cast<ConstantInt>(getVal(BI->getCondition()));
          if (!Cond) return false;  // Cannot determine.

          NextBB = BI->getSuccessor(!Cond->getZExtValue());
        }
      } else if (SwitchInst *SI = dyn_cast<SwitchInst>(CurInst)) {
        ConstantInt *Val =
          dyn_cast<ConstantInt>(getVal(SI->getCondition()));
        if (!Val) return false;  // Cannot determine.
        NextBB = SI->findCaseValue(Val)->getCaseSuccessor();
      } else if (IndirectBrInst *IBI = dyn_cast<IndirectBrInst>(CurInst)) {
        Value *Val = getVal(IBI->getAddress())->stripPointerCasts();
        if (BlockAddress *BA = dyn_cast<BlockAddress>(Val))
          NextBB = BA->getBasicBlock();
        else
          return false;  // Cannot determine.
      } else if (isa<ReturnInst>(CurInst)) {
        NextBB = nullptr;
      } else {
        // invoke, unwind, resume, unreachable.
        LLVM_DEBUG(dbgs() << "Can not handle terminator.");
        return false;  // Cannot handle this terminator.
      }

      // We succeeded at evaluating this block!
      LLVM_DEBUG(dbgs() << "Successfully evaluated block.\n");
      return true;
    } else {
      SmallVector<Constant *> Ops;
      for (Value *Op : CurInst->operands())
        Ops.push_back(getVal(Op));
      InstResult = ConstantFoldInstOperands(&*CurInst, Ops, DL, TLI);
      if (!InstResult) {
        LLVM_DEBUG(dbgs() << "Cannot fold instruction: " << *CurInst << "\n");
        return false;
      }
      LLVM_DEBUG(dbgs() << "Folded instruction " << *CurInst << " to "
                        << *InstResult << "\n");
    }

    if (!CurInst->use_empty()) {
      InstResult = ConstantFoldConstant(InstResult, DL, TLI);
      setVal(&*CurInst, InstResult);
    }

    // If we just processed an invoke, we finished evaluating the block.
    if (InvokeInst *II = dyn_cast<InvokeInst>(CurInst)) {
      NextBB = II->getNormalDest();
      LLVM_DEBUG(dbgs() << "Found an invoke instruction. Finished Block.\n\n");
      return true;
    }

    // Advance program counter.
    ++CurInst;
  }
}

/// Evaluate a call to function F, returning true if successful, false if we
/// can't evaluate it.  ActualArgs contains the formal arguments for the
/// function.
bool Evaluator::EvaluateFunction(Function *F, Constant *&RetVal,
                                 const SmallVectorImpl<Constant*> &ActualArgs) {
  assert(ActualArgs.size() == F->arg_size() && "wrong number of arguments");

  // Check to see if this function is already executing (recursion).  If so,
  // bail out.  TODO: we might want to accept limited recursion.
  if (is_contained(CallStack, F))
    return false;

  CallStack.push_back(F);

  // Initialize arguments to the incoming values specified.
  for (const auto &[ArgNo, Arg] : llvm::enumerate(F->args()))
    setVal(&Arg, ActualArgs[ArgNo]);

  // ExecutedBlocks - We only handle non-looping, non-recursive code.  As such,
  // we can only evaluate any one basic block at most once.  This set keeps
  // track of what we have executed so we can detect recursive cases etc.
  SmallPtrSet<BasicBlock*, 32> ExecutedBlocks;

  // CurBB - The current basic block we're evaluating.
  BasicBlock *CurBB = &F->front();

  BasicBlock::iterator CurInst = CurBB->begin();

  while (true) {
    BasicBlock *NextBB = nullptr; // Initialized to avoid compiler warnings.
    LLVM_DEBUG(dbgs() << "Trying to evaluate BB: " << *CurBB << "\n");

    bool StrippedPointerCastsForAliasAnalysis = false;

    if (!EvaluateBlock(CurInst, NextBB, StrippedPointerCastsForAliasAnalysis))
      return false;

    if (!NextBB) {
      // Successfully running until there's no next block means that we found
      // the return.  Fill it the return value and pop the call stack.
      ReturnInst *RI = cast<ReturnInst>(CurBB->getTerminator());
      if (RI->getNumOperands()) {
        // The Evaluator can look through pointer casts as long as alias
        // analysis holds because it's just a simple interpreter and doesn't
        // skip memory accesses due to invariant group metadata, but we can't
        // let users of Evaluator use a value that's been gleaned looking
        // through stripping pointer casts.
        if (StrippedPointerCastsForAliasAnalysis &&
            !RI->getReturnValue()->getType()->isVoidTy()) {
          return false;
        }
        RetVal = getVal(RI->getOperand(0));
      }
      CallStack.pop_back();
      return true;
    }

    // Okay, we succeeded in evaluating this control flow.  See if we have
    // executed the new block before.  If so, we have a looping function,
    // which we cannot evaluate in reasonable time.
    if (!ExecutedBlocks.insert(NextBB).second)
      return false;  // looped!

    // Okay, we have never been in this block before.  Check to see if there
    // are any PHI nodes.  If so, evaluate them with information about where
    // we came from.
    PHINode *PN = nullptr;
    for (CurInst = NextBB->begin();
         (PN = dyn_cast<PHINode>(CurInst)); ++CurInst)
      setVal(PN, getVal(PN->getIncomingValueForBlock(CurBB)));

    // Advance to the next block.
    CurBB = NextBB;
  }
}
