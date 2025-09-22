//==- llvm/Analysis/MemoryBuiltins.h - Calls to memory builtins --*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This family of functions identifies calls to builtin functions that allocate
// or free memory.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_MEMORYBUILTINS_H
#define LLVM_ANALYSIS_MEMORYBUILTINS_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/TargetFolder.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/ValueHandle.h"
#include <cstdint>
#include <optional>
#include <utility>

namespace llvm {

class AllocaInst;
class AAResults;
class Argument;
class ConstantPointerNull;
class DataLayout;
class ExtractElementInst;
class ExtractValueInst;
class GEPOperator;
class GlobalAlias;
class GlobalVariable;
class Instruction;
class IntegerType;
class IntrinsicInst;
class IntToPtrInst;
class LLVMContext;
class LoadInst;
class PHINode;
class SelectInst;
class Type;
class UndefValue;
class Value;

/// Tests if a value is a call or invoke to a library function that
/// allocates or reallocates memory (either malloc, calloc, realloc, or strdup
/// like).
bool isAllocationFn(const Value *V, const TargetLibraryInfo *TLI);
bool isAllocationFn(const Value *V,
                    function_ref<const TargetLibraryInfo &(Function &)> GetTLI);

/// Tests if a value is a call or invoke to a library function that
/// allocates memory via new.
bool isNewLikeFn(const Value *V, const TargetLibraryInfo *TLI);

/// Tests if a value is a call or invoke to a library function that
/// allocates memory similar to malloc or calloc.
bool isMallocOrCallocLikeFn(const Value *V, const TargetLibraryInfo *TLI);

/// Tests if a value is a call or invoke to a library function that
/// allocates memory (either malloc, calloc, or strdup like).
bool isAllocLikeFn(const Value *V, const TargetLibraryInfo *TLI);

/// Tests if a function is a call or invoke to a library function that
/// reallocates memory (e.g., realloc).
bool isReallocLikeFn(const Function *F);

/// If this is a call to a realloc function, return the reallocated operand.
Value *getReallocatedOperand(const CallBase *CB);

//===----------------------------------------------------------------------===//
//  free Call Utility Functions.
//

/// isLibFreeFunction - Returns true if the function is a builtin free()
bool isLibFreeFunction(const Function *F, const LibFunc TLIFn);

/// If this if a call to a free function, return the freed operand.
Value *getFreedOperand(const CallBase *CB, const TargetLibraryInfo *TLI);

//===----------------------------------------------------------------------===//
//  Properties of allocation functions
//

/// Return true if this is a call to an allocation function that does not have
/// side effects that we are required to preserve beyond the effect of
/// allocating a new object.
/// Ex: If our allocation routine has a counter for the number of objects
/// allocated, and the program prints it on exit, can the value change due
/// to optimization? Answer is highly language dependent.
/// Note: *Removable* really does mean removable; it does not mean observable.
/// A language (e.g. C++) can allow removing allocations without allowing
/// insertion or speculative execution of allocation routines.
bool isRemovableAlloc(const CallBase *V, const TargetLibraryInfo *TLI);

/// Gets the alignment argument for an aligned_alloc-like function, using either
/// built-in knowledge based on fuction names/signatures or allocalign
/// attributes. Note: the Value returned may not indicate a valid alignment, per
/// the definition of the allocalign attribute.
Value *getAllocAlignment(const CallBase *V, const TargetLibraryInfo *TLI);

/// Return the size of the requested allocation. With a trivial mapper, this is
/// similar to calling getObjectSize(..., Exact), but without looking through
/// calls that return their argument. A mapper function can be used to replace
/// one Value* (operand to the allocation) with another. This is useful when
/// doing abstract interpretation.
std::optional<APInt> getAllocSize(
    const CallBase *CB, const TargetLibraryInfo *TLI,
    function_ref<const Value *(const Value *)> Mapper = [](const Value *V) {
      return V;
    });

/// If this is a call to an allocation function that initializes memory to a
/// fixed value, return said value in the requested type.  Otherwise, return
/// nullptr.
Constant *getInitialValueOfAllocation(const Value *V,
                                      const TargetLibraryInfo *TLI,
                                      Type *Ty);

/// If a function is part of an allocation family (e.g.
/// malloc/realloc/calloc/free), return the identifier for its family
/// of functions.
std::optional<StringRef> getAllocationFamily(const Value *I,
                                             const TargetLibraryInfo *TLI);

//===----------------------------------------------------------------------===//
//  Utility functions to compute size of objects.
//

/// Various options to control the behavior of getObjectSize.
struct ObjectSizeOpts {
  /// Controls how we handle conditional statements with unknown conditions.
  enum class Mode : uint8_t {
    /// All branches must be known and have the same size, starting from the
    /// offset, to be merged.
    ExactSizeFromOffset,
    /// All branches must be known and have the same underlying size and offset
    /// to be merged.
    ExactUnderlyingSizeAndOffset,
    /// Evaluate all branches of an unknown condition. If all evaluations
    /// succeed, pick the minimum size.
    Min,
    /// Same as Min, except we pick the maximum size of all of the branches.
    Max,
  };

  /// How we want to evaluate this object's size.
  Mode EvalMode = Mode::ExactSizeFromOffset;
  /// Whether to round the result up to the alignment of allocas, byval
  /// arguments, and global variables.
  bool RoundToAlign = false;
  /// If this is true, null pointers in address space 0 will be treated as
  /// though they can't be evaluated. Otherwise, null is always considered to
  /// point to a 0 byte region of memory.
  bool NullIsUnknownSize = false;
  /// If set, used for more accurate evaluation
  AAResults *AA = nullptr;
};

/// Compute the size of the object pointed by Ptr. Returns true and the
/// object size in Size if successful, and false otherwise. In this context, by
/// object we mean the region of memory starting at Ptr to the end of the
/// underlying object pointed to by Ptr.
///
/// WARNING: The object size returned is the allocation size.  This does not
/// imply dereferenceability at site of use since the object may be freeed in
/// between.
bool getObjectSize(const Value *Ptr, uint64_t &Size, const DataLayout &DL,
                   const TargetLibraryInfo *TLI, ObjectSizeOpts Opts = {});

/// Try to turn a call to \@llvm.objectsize into an integer value of the given
/// Type. Returns null on failure. If MustSucceed is true, this function will
/// not return null, and may return conservative values governed by the second
/// argument of the call to objectsize.
Value *lowerObjectSizeCall(IntrinsicInst *ObjectSize, const DataLayout &DL,
                           const TargetLibraryInfo *TLI, bool MustSucceed);
Value *lowerObjectSizeCall(
    IntrinsicInst *ObjectSize, const DataLayout &DL,
    const TargetLibraryInfo *TLI, AAResults *AA, bool MustSucceed,
    SmallVectorImpl<Instruction *> *InsertedInstructions = nullptr);

/// SizeOffsetType - A base template class for the object size visitors. Used
/// here as a self-documenting way to handle the values rather than using a
/// \p std::pair.
template <typename T, class C> struct SizeOffsetType {
public:
  T Size;
  T Offset;

  SizeOffsetType() = default;
  SizeOffsetType(T Size, T Offset)
      : Size(std::move(Size)), Offset(std::move(Offset)) {}

  bool knownSize() const { return C::known(Size); }
  bool knownOffset() const { return C::known(Offset); }
  bool anyKnown() const { return knownSize() || knownOffset(); }
  bool bothKnown() const { return knownSize() && knownOffset(); }

  bool operator==(const SizeOffsetType<T, C> &RHS) const {
    return Size == RHS.Size && Offset == RHS.Offset;
  }
  bool operator!=(const SizeOffsetType<T, C> &RHS) const {
    return !(*this == RHS);
  }
};

/// SizeOffsetAPInt - Used by \p ObjectSizeOffsetVisitor, which works with
/// \p APInts.
struct SizeOffsetAPInt : public SizeOffsetType<APInt, SizeOffsetAPInt> {
  SizeOffsetAPInt() = default;
  SizeOffsetAPInt(APInt Size, APInt Offset)
      : SizeOffsetType(std::move(Size), std::move(Offset)) {}

  static bool known(const APInt &V) { return V.getBitWidth() > 1; }
};

/// Evaluate the size and offset of an object pointed to by a Value*
/// statically. Fails if size or offset are not known at compile time.
class ObjectSizeOffsetVisitor
    : public InstVisitor<ObjectSizeOffsetVisitor, SizeOffsetAPInt> {
  const DataLayout &DL;
  const TargetLibraryInfo *TLI;
  ObjectSizeOpts Options;
  unsigned IntTyBits;
  APInt Zero;
  SmallDenseMap<Instruction *, SizeOffsetAPInt, 8> SeenInsts;
  unsigned InstructionsVisited;

  APInt align(APInt Size, MaybeAlign Align);

  static SizeOffsetAPInt unknown() { return SizeOffsetAPInt(); }

public:
  ObjectSizeOffsetVisitor(const DataLayout &DL, const TargetLibraryInfo *TLI,
                          LLVMContext &Context, ObjectSizeOpts Options = {});

  SizeOffsetAPInt compute(Value *V);

  // These are "private", except they can't actually be made private. Only
  // compute() should be used by external users.
  SizeOffsetAPInt visitAllocaInst(AllocaInst &I);
  SizeOffsetAPInt visitArgument(Argument &A);
  SizeOffsetAPInt visitCallBase(CallBase &CB);
  SizeOffsetAPInt visitConstantPointerNull(ConstantPointerNull &);
  SizeOffsetAPInt visitExtractElementInst(ExtractElementInst &I);
  SizeOffsetAPInt visitExtractValueInst(ExtractValueInst &I);
  SizeOffsetAPInt visitGlobalAlias(GlobalAlias &GA);
  SizeOffsetAPInt visitGlobalVariable(GlobalVariable &GV);
  SizeOffsetAPInt visitIntToPtrInst(IntToPtrInst &);
  SizeOffsetAPInt visitLoadInst(LoadInst &I);
  SizeOffsetAPInt visitPHINode(PHINode &);
  SizeOffsetAPInt visitSelectInst(SelectInst &I);
  SizeOffsetAPInt visitUndefValue(UndefValue &);
  SizeOffsetAPInt visitInstruction(Instruction &I);

private:
  SizeOffsetAPInt findLoadSizeOffset(
      LoadInst &LoadFrom, BasicBlock &BB, BasicBlock::iterator From,
      SmallDenseMap<BasicBlock *, SizeOffsetAPInt, 8> &VisitedBlocks,
      unsigned &ScannedInstCount);
  SizeOffsetAPInt combineSizeOffset(SizeOffsetAPInt LHS, SizeOffsetAPInt RHS);
  SizeOffsetAPInt computeImpl(Value *V);
  SizeOffsetAPInt computeValue(Value *V);
  bool CheckedZextOrTrunc(APInt &I);
};

/// SizeOffsetValue - Used by \p ObjectSizeOffsetEvaluator, which works with
/// \p Values.
struct SizeOffsetWeakTrackingVH;
struct SizeOffsetValue : public SizeOffsetType<Value *, SizeOffsetValue> {
  SizeOffsetValue() : SizeOffsetType(nullptr, nullptr) {}
  SizeOffsetValue(Value *Size, Value *Offset) : SizeOffsetType(Size, Offset) {}
  SizeOffsetValue(const SizeOffsetWeakTrackingVH &SOT);

  static bool known(Value *V) { return V != nullptr; }
};

/// SizeOffsetWeakTrackingVH - Used by \p ObjectSizeOffsetEvaluator in a
/// \p DenseMap.
struct SizeOffsetWeakTrackingVH
    : public SizeOffsetType<WeakTrackingVH, SizeOffsetWeakTrackingVH> {
  SizeOffsetWeakTrackingVH() : SizeOffsetType(nullptr, nullptr) {}
  SizeOffsetWeakTrackingVH(Value *Size, Value *Offset)
      : SizeOffsetType(Size, Offset) {}
  SizeOffsetWeakTrackingVH(const SizeOffsetValue &SOV)
      : SizeOffsetType(SOV.Size, SOV.Offset) {}

  static bool known(WeakTrackingVH V) { return V.pointsToAliveValue(); }
};

/// Evaluate the size and offset of an object pointed to by a Value*.
/// May create code to compute the result at run-time.
class ObjectSizeOffsetEvaluator
    : public InstVisitor<ObjectSizeOffsetEvaluator, SizeOffsetValue> {
  using BuilderTy = IRBuilder<TargetFolder, IRBuilderCallbackInserter>;
  using WeakEvalType = SizeOffsetWeakTrackingVH;
  using CacheMapTy = DenseMap<const Value *, WeakEvalType>;
  using PtrSetTy = SmallPtrSet<const Value *, 8>;

  const DataLayout &DL;
  const TargetLibraryInfo *TLI;
  LLVMContext &Context;
  BuilderTy Builder;
  IntegerType *IntTy;
  Value *Zero;
  CacheMapTy CacheMap;
  PtrSetTy SeenVals;
  ObjectSizeOpts EvalOpts;
  SmallPtrSet<Instruction *, 8> InsertedInstructions;

  SizeOffsetValue compute_(Value *V);

public:
  ObjectSizeOffsetEvaluator(const DataLayout &DL, const TargetLibraryInfo *TLI,
                            LLVMContext &Context, ObjectSizeOpts EvalOpts = {});

  static SizeOffsetValue unknown() { return SizeOffsetValue(); }

  SizeOffsetValue compute(Value *V);

  // The individual instruction visitors should be treated as private.
  SizeOffsetValue visitAllocaInst(AllocaInst &I);
  SizeOffsetValue visitCallBase(CallBase &CB);
  SizeOffsetValue visitExtractElementInst(ExtractElementInst &I);
  SizeOffsetValue visitExtractValueInst(ExtractValueInst &I);
  SizeOffsetValue visitGEPOperator(GEPOperator &GEP);
  SizeOffsetValue visitIntToPtrInst(IntToPtrInst &);
  SizeOffsetValue visitLoadInst(LoadInst &I);
  SizeOffsetValue visitPHINode(PHINode &PHI);
  SizeOffsetValue visitSelectInst(SelectInst &I);
  SizeOffsetValue visitInstruction(Instruction &I);
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_MEMORYBUILTINS_H
