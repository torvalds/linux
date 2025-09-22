//===-- Analysis.cpp - CodeGen LLVM IR Analysis Utilities -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines several CodeGen-specific LLVM IR analysis utilities.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/Analysis.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

/// Compute the linearized index of a member in a nested aggregate/struct/array
/// by recursing and accumulating CurIndex as long as there are indices in the
/// index list.
unsigned llvm::ComputeLinearIndex(Type *Ty,
                                  const unsigned *Indices,
                                  const unsigned *IndicesEnd,
                                  unsigned CurIndex) {
  // Base case: We're done.
  if (Indices && Indices == IndicesEnd)
    return CurIndex;

  // Given a struct type, recursively traverse the elements.
  if (StructType *STy = dyn_cast<StructType>(Ty)) {
    for (auto I : llvm::enumerate(STy->elements())) {
      Type *ET = I.value();
      if (Indices && *Indices == I.index())
        return ComputeLinearIndex(ET, Indices + 1, IndicesEnd, CurIndex);
      CurIndex = ComputeLinearIndex(ET, nullptr, nullptr, CurIndex);
    }
    assert(!Indices && "Unexpected out of bound");
    return CurIndex;
  }
  // Given an array type, recursively traverse the elements.
  else if (ArrayType *ATy = dyn_cast<ArrayType>(Ty)) {
    Type *EltTy = ATy->getElementType();
    unsigned NumElts = ATy->getNumElements();
    // Compute the Linear offset when jumping one element of the array
    unsigned EltLinearOffset = ComputeLinearIndex(EltTy, nullptr, nullptr, 0);
    if (Indices) {
      assert(*Indices < NumElts && "Unexpected out of bound");
      // If the indice is inside the array, compute the index to the requested
      // elt and recurse inside the element with the end of the indices list
      CurIndex += EltLinearOffset* *Indices;
      return ComputeLinearIndex(EltTy, Indices+1, IndicesEnd, CurIndex);
    }
    CurIndex += EltLinearOffset*NumElts;
    return CurIndex;
  }
  // We haven't found the type we're looking for, so keep searching.
  return CurIndex + 1;
}

/// ComputeValueVTs - Given an LLVM IR type, compute a sequence of
/// EVTs that represent all the individual underlying
/// non-aggregate types that comprise it.
///
/// If Offsets is non-null, it points to a vector to be filled in
/// with the in-memory offsets of each of the individual values.
///
void llvm::ComputeValueVTs(const TargetLowering &TLI, const DataLayout &DL,
                           Type *Ty, SmallVectorImpl<EVT> &ValueVTs,
                           SmallVectorImpl<EVT> *MemVTs,
                           SmallVectorImpl<TypeSize> *Offsets,
                           TypeSize StartingOffset) {
  assert((Ty->isScalableTy() == StartingOffset.isScalable() ||
          StartingOffset.isZero()) &&
         "Offset/TypeSize mismatch!");
  // Given a struct type, recursively traverse the elements.
  if (StructType *STy = dyn_cast<StructType>(Ty)) {
    // If the Offsets aren't needed, don't query the struct layout. This allows
    // us to support structs with scalable vectors for operations that don't
    // need offsets.
    const StructLayout *SL = Offsets ? DL.getStructLayout(STy) : nullptr;
    for (StructType::element_iterator EB = STy->element_begin(),
                                      EI = EB,
                                      EE = STy->element_end();
         EI != EE; ++EI) {
      // Don't compute the element offset if we didn't get a StructLayout above.
      TypeSize EltOffset =
          SL ? SL->getElementOffset(EI - EB) : TypeSize::getZero();
      ComputeValueVTs(TLI, DL, *EI, ValueVTs, MemVTs, Offsets,
                      StartingOffset + EltOffset);
    }
    return;
  }
  // Given an array type, recursively traverse the elements.
  if (ArrayType *ATy = dyn_cast<ArrayType>(Ty)) {
    Type *EltTy = ATy->getElementType();
    TypeSize EltSize = DL.getTypeAllocSize(EltTy);
    for (unsigned i = 0, e = ATy->getNumElements(); i != e; ++i)
      ComputeValueVTs(TLI, DL, EltTy, ValueVTs, MemVTs, Offsets,
                      StartingOffset + i * EltSize);
    return;
  }
  // Interpret void as zero return values.
  if (Ty->isVoidTy())
    return;
  // Base case: we can get an EVT for this LLVM IR type.
  ValueVTs.push_back(TLI.getValueType(DL, Ty));
  if (MemVTs)
    MemVTs->push_back(TLI.getMemValueType(DL, Ty));
  if (Offsets)
    Offsets->push_back(StartingOffset);
}

void llvm::ComputeValueVTs(const TargetLowering &TLI, const DataLayout &DL,
                           Type *Ty, SmallVectorImpl<EVT> &ValueVTs,
                           SmallVectorImpl<EVT> *MemVTs,
                           SmallVectorImpl<uint64_t> *FixedOffsets,
                           uint64_t StartingOffset) {
  TypeSize Offset = TypeSize::getFixed(StartingOffset);
  if (FixedOffsets) {
    SmallVector<TypeSize, 4> Offsets;
    ComputeValueVTs(TLI, DL, Ty, ValueVTs, MemVTs, &Offsets, Offset);
    for (TypeSize Offset : Offsets)
      FixedOffsets->push_back(Offset.getFixedValue());
  } else {
    ComputeValueVTs(TLI, DL, Ty, ValueVTs, MemVTs, nullptr, Offset);
  }
}

void llvm::computeValueLLTs(const DataLayout &DL, Type &Ty,
                            SmallVectorImpl<LLT> &ValueTys,
                            SmallVectorImpl<uint64_t> *Offsets,
                            uint64_t StartingOffset) {
  // Given a struct type, recursively traverse the elements.
  if (StructType *STy = dyn_cast<StructType>(&Ty)) {
    // If the Offsets aren't needed, don't query the struct layout. This allows
    // us to support structs with scalable vectors for operations that don't
    // need offsets.
    const StructLayout *SL = Offsets ? DL.getStructLayout(STy) : nullptr;
    for (unsigned I = 0, E = STy->getNumElements(); I != E; ++I) {
      uint64_t EltOffset = SL ? SL->getElementOffset(I) : 0;
      computeValueLLTs(DL, *STy->getElementType(I), ValueTys, Offsets,
                       StartingOffset + EltOffset);
    }
    return;
  }
  // Given an array type, recursively traverse the elements.
  if (ArrayType *ATy = dyn_cast<ArrayType>(&Ty)) {
    Type *EltTy = ATy->getElementType();
    uint64_t EltSize = DL.getTypeAllocSize(EltTy).getFixedValue();
    for (unsigned i = 0, e = ATy->getNumElements(); i != e; ++i)
      computeValueLLTs(DL, *EltTy, ValueTys, Offsets,
                       StartingOffset + i * EltSize);
    return;
  }
  // Interpret void as zero return values.
  if (Ty.isVoidTy())
    return;
  // Base case: we can get an LLT for this LLVM IR type.
  ValueTys.push_back(getLLTForType(Ty, DL));
  if (Offsets != nullptr)
    Offsets->push_back(StartingOffset * 8);
}

/// ExtractTypeInfo - Returns the type info, possibly bitcast, encoded in V.
GlobalValue *llvm::ExtractTypeInfo(Value *V) {
  V = V->stripPointerCasts();
  GlobalValue *GV = dyn_cast<GlobalValue>(V);
  GlobalVariable *Var = dyn_cast<GlobalVariable>(V);

  if (Var && Var->getName() == "llvm.eh.catch.all.value") {
    assert(Var->hasInitializer() &&
           "The EH catch-all value must have an initializer");
    Value *Init = Var->getInitializer();
    GV = dyn_cast<GlobalValue>(Init);
    if (!GV) V = cast<ConstantPointerNull>(Init);
  }

  assert((GV || isa<ConstantPointerNull>(V)) &&
         "TypeInfo must be a global variable or NULL");
  return GV;
}

/// getFCmpCondCode - Return the ISD condition code corresponding to
/// the given LLVM IR floating-point condition code.  This includes
/// consideration of global floating-point math flags.
///
ISD::CondCode llvm::getFCmpCondCode(FCmpInst::Predicate Pred) {
  switch (Pred) {
  case FCmpInst::FCMP_FALSE: return ISD::SETFALSE;
  case FCmpInst::FCMP_OEQ:   return ISD::SETOEQ;
  case FCmpInst::FCMP_OGT:   return ISD::SETOGT;
  case FCmpInst::FCMP_OGE:   return ISD::SETOGE;
  case FCmpInst::FCMP_OLT:   return ISD::SETOLT;
  case FCmpInst::FCMP_OLE:   return ISD::SETOLE;
  case FCmpInst::FCMP_ONE:   return ISD::SETONE;
  case FCmpInst::FCMP_ORD:   return ISD::SETO;
  case FCmpInst::FCMP_UNO:   return ISD::SETUO;
  case FCmpInst::FCMP_UEQ:   return ISD::SETUEQ;
  case FCmpInst::FCMP_UGT:   return ISD::SETUGT;
  case FCmpInst::FCMP_UGE:   return ISD::SETUGE;
  case FCmpInst::FCMP_ULT:   return ISD::SETULT;
  case FCmpInst::FCMP_ULE:   return ISD::SETULE;
  case FCmpInst::FCMP_UNE:   return ISD::SETUNE;
  case FCmpInst::FCMP_TRUE:  return ISD::SETTRUE;
  default: llvm_unreachable("Invalid FCmp predicate opcode!");
  }
}

ISD::CondCode llvm::getFCmpCodeWithoutNaN(ISD::CondCode CC) {
  switch (CC) {
    case ISD::SETOEQ: case ISD::SETUEQ: return ISD::SETEQ;
    case ISD::SETONE: case ISD::SETUNE: return ISD::SETNE;
    case ISD::SETOLT: case ISD::SETULT: return ISD::SETLT;
    case ISD::SETOLE: case ISD::SETULE: return ISD::SETLE;
    case ISD::SETOGT: case ISD::SETUGT: return ISD::SETGT;
    case ISD::SETOGE: case ISD::SETUGE: return ISD::SETGE;
    default: return CC;
  }
}

ISD::CondCode llvm::getICmpCondCode(ICmpInst::Predicate Pred) {
  switch (Pred) {
  case ICmpInst::ICMP_EQ:  return ISD::SETEQ;
  case ICmpInst::ICMP_NE:  return ISD::SETNE;
  case ICmpInst::ICMP_SLE: return ISD::SETLE;
  case ICmpInst::ICMP_ULE: return ISD::SETULE;
  case ICmpInst::ICMP_SGE: return ISD::SETGE;
  case ICmpInst::ICMP_UGE: return ISD::SETUGE;
  case ICmpInst::ICMP_SLT: return ISD::SETLT;
  case ICmpInst::ICMP_ULT: return ISD::SETULT;
  case ICmpInst::ICMP_SGT: return ISD::SETGT;
  case ICmpInst::ICMP_UGT: return ISD::SETUGT;
  default:
    llvm_unreachable("Invalid ICmp predicate opcode!");
  }
}

ICmpInst::Predicate llvm::getICmpCondCode(ISD::CondCode Pred) {
  switch (Pred) {
  case ISD::SETEQ:
    return ICmpInst::ICMP_EQ;
  case ISD::SETNE:
    return ICmpInst::ICMP_NE;
  case ISD::SETLE:
    return ICmpInst::ICMP_SLE;
  case ISD::SETULE:
    return ICmpInst::ICMP_ULE;
  case ISD::SETGE:
    return ICmpInst::ICMP_SGE;
  case ISD::SETUGE:
    return ICmpInst::ICMP_UGE;
  case ISD::SETLT:
    return ICmpInst::ICMP_SLT;
  case ISD::SETULT:
    return ICmpInst::ICMP_ULT;
  case ISD::SETGT:
    return ICmpInst::ICMP_SGT;
  case ISD::SETUGT:
    return ICmpInst::ICMP_UGT;
  default:
    llvm_unreachable("Invalid ISD integer condition code!");
  }
}

static bool isNoopBitcast(Type *T1, Type *T2,
                          const TargetLoweringBase& TLI) {
  return T1 == T2 || (T1->isPointerTy() && T2->isPointerTy()) ||
         (isa<VectorType>(T1) && isa<VectorType>(T2) &&
          TLI.isTypeLegal(EVT::getEVT(T1)) && TLI.isTypeLegal(EVT::getEVT(T2)));
}

/// Look through operations that will be free to find the earliest source of
/// this value.
///
/// @param ValLoc If V has aggregate type, we will be interested in a particular
/// scalar component. This records its address; the reverse of this list gives a
/// sequence of indices appropriate for an extractvalue to locate the important
/// value. This value is updated during the function and on exit will indicate
/// similar information for the Value returned.
///
/// @param DataBits If this function looks through truncate instructions, this
/// will record the smallest size attained.
static const Value *getNoopInput(const Value *V,
                                 SmallVectorImpl<unsigned> &ValLoc,
                                 unsigned &DataBits,
                                 const TargetLoweringBase &TLI,
                                 const DataLayout &DL) {
  while (true) {
    // Try to look through V1; if V1 is not an instruction, it can't be looked
    // through.
    const Instruction *I = dyn_cast<Instruction>(V);
    if (!I || I->getNumOperands() == 0) return V;
    const Value *NoopInput = nullptr;

    Value *Op = I->getOperand(0);
    if (isa<BitCastInst>(I)) {
      // Look through truly no-op bitcasts.
      if (isNoopBitcast(Op->getType(), I->getType(), TLI))
        NoopInput = Op;
    } else if (isa<GetElementPtrInst>(I)) {
      // Look through getelementptr
      if (cast<GetElementPtrInst>(I)->hasAllZeroIndices())
        NoopInput = Op;
    } else if (isa<IntToPtrInst>(I)) {
      // Look through inttoptr.
      // Make sure this isn't a truncating or extending cast.  We could
      // support this eventually, but don't bother for now.
      if (!isa<VectorType>(I->getType()) &&
          DL.getPointerSizeInBits() ==
              cast<IntegerType>(Op->getType())->getBitWidth())
        NoopInput = Op;
    } else if (isa<PtrToIntInst>(I)) {
      // Look through ptrtoint.
      // Make sure this isn't a truncating or extending cast.  We could
      // support this eventually, but don't bother for now.
      if (!isa<VectorType>(I->getType()) &&
          DL.getPointerSizeInBits() ==
              cast<IntegerType>(I->getType())->getBitWidth())
        NoopInput = Op;
    } else if (isa<TruncInst>(I) &&
               TLI.allowTruncateForTailCall(Op->getType(), I->getType())) {
      DataBits =
          std::min((uint64_t)DataBits,
                   I->getType()->getPrimitiveSizeInBits().getFixedValue());
      NoopInput = Op;
    } else if (auto *CB = dyn_cast<CallBase>(I)) {
      const Value *ReturnedOp = CB->getReturnedArgOperand();
      if (ReturnedOp && isNoopBitcast(ReturnedOp->getType(), I->getType(), TLI))
        NoopInput = ReturnedOp;
    } else if (const InsertValueInst *IVI = dyn_cast<InsertValueInst>(V)) {
      // Value may come from either the aggregate or the scalar
      ArrayRef<unsigned> InsertLoc = IVI->getIndices();
      if (ValLoc.size() >= InsertLoc.size() &&
          std::equal(InsertLoc.begin(), InsertLoc.end(), ValLoc.rbegin())) {
        // The type being inserted is a nested sub-type of the aggregate; we
        // have to remove those initial indices to get the location we're
        // interested in for the operand.
        ValLoc.resize(ValLoc.size() - InsertLoc.size());
        NoopInput = IVI->getInsertedValueOperand();
      } else {
        // The struct we're inserting into has the value we're interested in, no
        // change of address.
        NoopInput = Op;
      }
    } else if (const ExtractValueInst *EVI = dyn_cast<ExtractValueInst>(V)) {
      // The part we're interested in will inevitably be some sub-section of the
      // previous aggregate. Combine the two paths to obtain the true address of
      // our element.
      ArrayRef<unsigned> ExtractLoc = EVI->getIndices();
      ValLoc.append(ExtractLoc.rbegin(), ExtractLoc.rend());
      NoopInput = Op;
    }
    // Terminate if we couldn't find anything to look through.
    if (!NoopInput)
      return V;

    V = NoopInput;
  }
}

/// Return true if this scalar return value only has bits discarded on its path
/// from the "tail call" to the "ret". This includes the obvious noop
/// instructions handled by getNoopInput above as well as free truncations (or
/// extensions prior to the call).
static bool slotOnlyDiscardsData(const Value *RetVal, const Value *CallVal,
                                 SmallVectorImpl<unsigned> &RetIndices,
                                 SmallVectorImpl<unsigned> &CallIndices,
                                 bool AllowDifferingSizes,
                                 const TargetLoweringBase &TLI,
                                 const DataLayout &DL) {

  // Trace the sub-value needed by the return value as far back up the graph as
  // possible, in the hope that it will intersect with the value produced by the
  // call. In the simple case with no "returned" attribute, the hope is actually
  // that we end up back at the tail call instruction itself.
  unsigned BitsRequired = UINT_MAX;
  RetVal = getNoopInput(RetVal, RetIndices, BitsRequired, TLI, DL);

  // If this slot in the value returned is undef, it doesn't matter what the
  // call puts there, it'll be fine.
  if (isa<UndefValue>(RetVal))
    return true;

  // Now do a similar search up through the graph to find where the value
  // actually returned by the "tail call" comes from. In the simple case without
  // a "returned" attribute, the search will be blocked immediately and the loop
  // a Noop.
  unsigned BitsProvided = UINT_MAX;
  CallVal = getNoopInput(CallVal, CallIndices, BitsProvided, TLI, DL);

  // There's no hope if we can't actually trace them to (the same part of!) the
  // same value.
  if (CallVal != RetVal || CallIndices != RetIndices)
    return false;

  // However, intervening truncates may have made the call non-tail. Make sure
  // all the bits that are needed by the "ret" have been provided by the "tail
  // call". FIXME: with sufficiently cunning bit-tracking, we could look through
  // extensions too.
  if (BitsProvided < BitsRequired ||
      (!AllowDifferingSizes && BitsProvided != BitsRequired))
    return false;

  return true;
}

/// For an aggregate type, determine whether a given index is within bounds or
/// not.
static bool indexReallyValid(Type *T, unsigned Idx) {
  if (ArrayType *AT = dyn_cast<ArrayType>(T))
    return Idx < AT->getNumElements();

  return Idx < cast<StructType>(T)->getNumElements();
}

/// Move the given iterators to the next leaf type in depth first traversal.
///
/// Performs a depth-first traversal of the type as specified by its arguments,
/// stopping at the next leaf node (which may be a legitimate scalar type or an
/// empty struct or array).
///
/// @param SubTypes List of the partial components making up the type from
/// outermost to innermost non-empty aggregate. The element currently
/// represented is SubTypes.back()->getTypeAtIndex(Path.back() - 1).
///
/// @param Path Set of extractvalue indices leading from the outermost type
/// (SubTypes[0]) to the leaf node currently represented.
///
/// @returns true if a new type was found, false otherwise. Calling this
/// function again on a finished iterator will repeatedly return
/// false. SubTypes.back()->getTypeAtIndex(Path.back()) is either an empty
/// aggregate or a non-aggregate
static bool advanceToNextLeafType(SmallVectorImpl<Type *> &SubTypes,
                                  SmallVectorImpl<unsigned> &Path) {
  // First march back up the tree until we can successfully increment one of the
  // coordinates in Path.
  while (!Path.empty() && !indexReallyValid(SubTypes.back(), Path.back() + 1)) {
    Path.pop_back();
    SubTypes.pop_back();
  }

  // If we reached the top, then the iterator is done.
  if (Path.empty())
    return false;

  // We know there's *some* valid leaf now, so march back down the tree picking
  // out the left-most element at each node.
  ++Path.back();
  Type *DeeperType =
      ExtractValueInst::getIndexedType(SubTypes.back(), Path.back());
  while (DeeperType->isAggregateType()) {
    if (!indexReallyValid(DeeperType, 0))
      return true;

    SubTypes.push_back(DeeperType);
    Path.push_back(0);

    DeeperType = ExtractValueInst::getIndexedType(DeeperType, 0);
  }

  return true;
}

/// Find the first non-empty, scalar-like type in Next and setup the iterator
/// components.
///
/// Assuming Next is an aggregate of some kind, this function will traverse the
/// tree from left to right (i.e. depth-first) looking for the first
/// non-aggregate type which will play a role in function return.
///
/// For example, if Next was {[0 x i64], {{}, i32, {}}, i32} then we would setup
/// Path as [1, 1] and SubTypes as [Next, {{}, i32, {}}] to represent the first
/// i32 in that type.
static bool firstRealType(Type *Next, SmallVectorImpl<Type *> &SubTypes,
                          SmallVectorImpl<unsigned> &Path) {
  // First initialise the iterator components to the first "leaf" node
  // (i.e. node with no valid sub-type at any index, so {} does count as a leaf
  // despite nominally being an aggregate).
  while (Type *FirstInner = ExtractValueInst::getIndexedType(Next, 0)) {
    SubTypes.push_back(Next);
    Path.push_back(0);
    Next = FirstInner;
  }

  // If there's no Path now, Next was originally scalar already (or empty
  // leaf). We're done.
  if (Path.empty())
    return true;

  // Otherwise, use normal iteration to keep looking through the tree until we
  // find a non-aggregate type.
  while (ExtractValueInst::getIndexedType(SubTypes.back(), Path.back())
             ->isAggregateType()) {
    if (!advanceToNextLeafType(SubTypes, Path))
      return false;
  }

  return true;
}

/// Set the iterator data-structures to the next non-empty, non-aggregate
/// subtype.
static bool nextRealType(SmallVectorImpl<Type *> &SubTypes,
                         SmallVectorImpl<unsigned> &Path) {
  do {
    if (!advanceToNextLeafType(SubTypes, Path))
      return false;

    assert(!Path.empty() && "found a leaf but didn't set the path?");
  } while (ExtractValueInst::getIndexedType(SubTypes.back(), Path.back())
               ->isAggregateType());

  return true;
}


/// Test if the given instruction is in a position to be optimized
/// with a tail-call. This roughly means that it's in a block with
/// a return and there's nothing that needs to be scheduled
/// between it and the return.
///
/// This function only tests target-independent requirements.
bool llvm::isInTailCallPosition(const CallBase &Call, const TargetMachine &TM,
                                bool ReturnsFirstArg) {
  const BasicBlock *ExitBB = Call.getParent();
  const Instruction *Term = ExitBB->getTerminator();
  const ReturnInst *Ret = dyn_cast<ReturnInst>(Term);

  // The block must end in a return statement or unreachable.
  //
  // FIXME: Decline tailcall if it's not guaranteed and if the block ends in
  // an unreachable, for now. The way tailcall optimization is currently
  // implemented means it will add an epilogue followed by a jump. That is
  // not profitable. Also, if the callee is a special function (e.g.
  // longjmp on x86), it can end up causing miscompilation that has not
  // been fully understood.
  if (!Ret && ((!TM.Options.GuaranteedTailCallOpt &&
                Call.getCallingConv() != CallingConv::Tail &&
                Call.getCallingConv() != CallingConv::SwiftTail) ||
               !isa<UnreachableInst>(Term)))
    return false;

  // If I will have a chain, make sure no other instruction that will have a
  // chain interposes between I and the return.
  // Check for all calls including speculatable functions.
  for (BasicBlock::const_iterator BBI = std::prev(ExitBB->end(), 2);; --BBI) {
    if (&*BBI == &Call)
      break;
    // Debug info intrinsics do not get in the way of tail call optimization.
    // Pseudo probe intrinsics do not block tail call optimization either.
    if (BBI->isDebugOrPseudoInst())
      continue;
    // A lifetime end, assume or noalias.decl intrinsic should not stop tail
    // call optimization.
    if (const IntrinsicInst *II = dyn_cast<IntrinsicInst>(BBI))
      if (II->getIntrinsicID() == Intrinsic::lifetime_end ||
          II->getIntrinsicID() == Intrinsic::assume ||
          II->getIntrinsicID() == Intrinsic::experimental_noalias_scope_decl)
        continue;
    if (BBI->mayHaveSideEffects() || BBI->mayReadFromMemory() ||
        !isSafeToSpeculativelyExecute(&*BBI))
      return false;
  }

  const Function *F = ExitBB->getParent();
  return returnTypeIsEligibleForTailCall(
      F, &Call, Ret, *TM.getSubtargetImpl(*F)->getTargetLowering(),
      ReturnsFirstArg);
}

bool llvm::attributesPermitTailCall(const Function *F, const Instruction *I,
                                    const ReturnInst *Ret,
                                    const TargetLoweringBase &TLI,
                                    bool *AllowDifferingSizes) {
  // ADS may be null, so don't write to it directly.
  bool DummyADS;
  bool &ADS = AllowDifferingSizes ? *AllowDifferingSizes : DummyADS;
  ADS = true;

  AttrBuilder CallerAttrs(F->getContext(), F->getAttributes().getRetAttrs());
  AttrBuilder CalleeAttrs(F->getContext(),
                          cast<CallInst>(I)->getAttributes().getRetAttrs());

  // Following attributes are completely benign as far as calling convention
  // goes, they shouldn't affect whether the call is a tail call.
  for (const auto &Attr :
       {Attribute::Alignment, Attribute::Dereferenceable,
        Attribute::DereferenceableOrNull, Attribute::NoAlias,
        Attribute::NonNull, Attribute::NoUndef, Attribute::Range}) {
    CallerAttrs.removeAttribute(Attr);
    CalleeAttrs.removeAttribute(Attr);
  }

  if (CallerAttrs.contains(Attribute::ZExt)) {
    if (!CalleeAttrs.contains(Attribute::ZExt))
      return false;

    ADS = false;
    CallerAttrs.removeAttribute(Attribute::ZExt);
    CalleeAttrs.removeAttribute(Attribute::ZExt);
  } else if (CallerAttrs.contains(Attribute::SExt)) {
    if (!CalleeAttrs.contains(Attribute::SExt))
      return false;

    ADS = false;
    CallerAttrs.removeAttribute(Attribute::SExt);
    CalleeAttrs.removeAttribute(Attribute::SExt);
  }

  // Drop sext and zext return attributes if the result is not used.
  // This enables tail calls for code like:
  //
  // define void @caller() {
  // entry:
  //   %unused_result = tail call zeroext i1 @callee()
  //   br label %retlabel
  // retlabel:
  //   ret void
  // }
  if (I->use_empty()) {
    CalleeAttrs.removeAttribute(Attribute::SExt);
    CalleeAttrs.removeAttribute(Attribute::ZExt);
  }

  // If they're still different, there's some facet we don't understand
  // (currently only "inreg", but in future who knows). It may be OK but the
  // only safe option is to reject the tail call.
  return CallerAttrs == CalleeAttrs;
}

bool llvm::returnTypeIsEligibleForTailCall(const Function *F,
                                           const Instruction *I,
                                           const ReturnInst *Ret,
                                           const TargetLoweringBase &TLI,
                                           bool ReturnsFirstArg) {
  // If the block ends with a void return or unreachable, it doesn't matter
  // what the call's return type is.
  if (!Ret || Ret->getNumOperands() == 0) return true;

  // If the return value is undef, it doesn't matter what the call's
  // return type is.
  if (isa<UndefValue>(Ret->getOperand(0))) return true;

  // Make sure the attributes attached to each return are compatible.
  bool AllowDifferingSizes;
  if (!attributesPermitTailCall(F, I, Ret, TLI, &AllowDifferingSizes))
    return false;

  // If the return value is the first argument of the call.
  if (ReturnsFirstArg)
    return true;

  const Value *RetVal = Ret->getOperand(0), *CallVal = I;
  SmallVector<unsigned, 4> RetPath, CallPath;
  SmallVector<Type *, 4> RetSubTypes, CallSubTypes;

  bool RetEmpty = !firstRealType(RetVal->getType(), RetSubTypes, RetPath);
  bool CallEmpty = !firstRealType(CallVal->getType(), CallSubTypes, CallPath);

  // Nothing's actually returned, it doesn't matter what the callee put there
  // it's a valid tail call.
  if (RetEmpty)
    return true;

  // Iterate pairwise through each of the value types making up the tail call
  // and the corresponding return. For each one we want to know whether it's
  // essentially going directly from the tail call to the ret, via operations
  // that end up not generating any code.
  //
  // We allow a certain amount of covariance here. For example it's permitted
  // for the tail call to define more bits than the ret actually cares about
  // (e.g. via a truncate).
  do {
    if (CallEmpty) {
      // We've exhausted the values produced by the tail call instruction, the
      // rest are essentially undef. The type doesn't really matter, but we need
      // *something*.
      Type *SlotType =
          ExtractValueInst::getIndexedType(RetSubTypes.back(), RetPath.back());
      CallVal = UndefValue::get(SlotType);
    }

    // The manipulations performed when we're looking through an insertvalue or
    // an extractvalue would happen at the front of the RetPath list, so since
    // we have to copy it anyway it's more efficient to create a reversed copy.
    SmallVector<unsigned, 4> TmpRetPath(llvm::reverse(RetPath));
    SmallVector<unsigned, 4> TmpCallPath(llvm::reverse(CallPath));

    // Finally, we can check whether the value produced by the tail call at this
    // index is compatible with the value we return.
    if (!slotOnlyDiscardsData(RetVal, CallVal, TmpRetPath, TmpCallPath,
                              AllowDifferingSizes, TLI,
                              F->getDataLayout()))
      return false;

    CallEmpty  = !nextRealType(CallSubTypes, CallPath);
  } while(nextRealType(RetSubTypes, RetPath));

  return true;
}

bool llvm::funcReturnsFirstArgOfCall(const CallInst &CI) {
  const ReturnInst *Ret = dyn_cast<ReturnInst>(CI.getParent()->getTerminator());
  Value *RetVal = Ret ? Ret->getReturnValue() : nullptr;
  bool ReturnsFirstArg = false;
  if (RetVal && ((RetVal == CI.getArgOperand(0))))
    ReturnsFirstArg = true;
  return ReturnsFirstArg;
}

static void collectEHScopeMembers(
    DenseMap<const MachineBasicBlock *, int> &EHScopeMembership, int EHScope,
    const MachineBasicBlock *MBB) {
  SmallVector<const MachineBasicBlock *, 16> Worklist = {MBB};
  while (!Worklist.empty()) {
    const MachineBasicBlock *Visiting = Worklist.pop_back_val();
    // Don't follow blocks which start new scopes.
    if (Visiting->isEHPad() && Visiting != MBB)
      continue;

    // Add this MBB to our scope.
    auto P = EHScopeMembership.insert(std::make_pair(Visiting, EHScope));

    // Don't revisit blocks.
    if (!P.second) {
      assert(P.first->second == EHScope && "MBB is part of two scopes!");
      continue;
    }

    // Returns are boundaries where scope transfer can occur, don't follow
    // successors.
    if (Visiting->isEHScopeReturnBlock())
      continue;

    append_range(Worklist, Visiting->successors());
  }
}

DenseMap<const MachineBasicBlock *, int>
llvm::getEHScopeMembership(const MachineFunction &MF) {
  DenseMap<const MachineBasicBlock *, int> EHScopeMembership;

  // We don't have anything to do if there aren't any EH pads.
  if (!MF.hasEHScopes())
    return EHScopeMembership;

  int EntryBBNumber = MF.front().getNumber();
  bool IsSEH = isAsynchronousEHPersonality(
      classifyEHPersonality(MF.getFunction().getPersonalityFn()));

  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  SmallVector<const MachineBasicBlock *, 16> EHScopeBlocks;
  SmallVector<const MachineBasicBlock *, 16> UnreachableBlocks;
  SmallVector<const MachineBasicBlock *, 16> SEHCatchPads;
  SmallVector<std::pair<const MachineBasicBlock *, int>, 16> CatchRetSuccessors;
  for (const MachineBasicBlock &MBB : MF) {
    if (MBB.isEHScopeEntry()) {
      EHScopeBlocks.push_back(&MBB);
    } else if (IsSEH && MBB.isEHPad()) {
      SEHCatchPads.push_back(&MBB);
    } else if (MBB.pred_empty()) {
      UnreachableBlocks.push_back(&MBB);
    }

    MachineBasicBlock::const_iterator MBBI = MBB.getFirstTerminator();

    // CatchPads are not scopes for SEH so do not consider CatchRet to
    // transfer control to another scope.
    if (MBBI == MBB.end() || MBBI->getOpcode() != TII->getCatchReturnOpcode())
      continue;

    // FIXME: SEH CatchPads are not necessarily in the parent function:
    // they could be inside a finally block.
    const MachineBasicBlock *Successor = MBBI->getOperand(0).getMBB();
    const MachineBasicBlock *SuccessorColor = MBBI->getOperand(1).getMBB();
    CatchRetSuccessors.push_back(
        {Successor, IsSEH ? EntryBBNumber : SuccessorColor->getNumber()});
  }

  // We don't have anything to do if there aren't any EH pads.
  if (EHScopeBlocks.empty())
    return EHScopeMembership;

  // Identify all the basic blocks reachable from the function entry.
  collectEHScopeMembers(EHScopeMembership, EntryBBNumber, &MF.front());
  // All blocks not part of a scope are in the parent function.
  for (const MachineBasicBlock *MBB : UnreachableBlocks)
    collectEHScopeMembers(EHScopeMembership, EntryBBNumber, MBB);
  // Next, identify all the blocks inside the scopes.
  for (const MachineBasicBlock *MBB : EHScopeBlocks)
    collectEHScopeMembers(EHScopeMembership, MBB->getNumber(), MBB);
  // SEH CatchPads aren't really scopes, handle them separately.
  for (const MachineBasicBlock *MBB : SEHCatchPads)
    collectEHScopeMembers(EHScopeMembership, EntryBBNumber, MBB);
  // Finally, identify all the targets of a catchret.
  for (std::pair<const MachineBasicBlock *, int> CatchRetPair :
       CatchRetSuccessors)
    collectEHScopeMembers(EHScopeMembership, CatchRetPair.second,
                          CatchRetPair.first);
  return EHScopeMembership;
}
