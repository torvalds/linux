//===- llvm/Analysis/VectorUtils.h - Vector utilities -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines some vectorizer utilities.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_VECTORUTILS_H
#define LLVM_ANALYSIS_VECTORUTILS_H

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/LoopAccessAnalysis.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/VFABIDemangler.h"
#include "llvm/Support/CheckedArithmetic.h"

namespace llvm {
class TargetLibraryInfo;

/// The Vector Function Database.
///
/// Helper class used to find the vector functions associated to a
/// scalar CallInst.
class VFDatabase {
  /// The Module of the CallInst CI.
  const Module *M;
  /// The CallInst instance being queried for scalar to vector mappings.
  const CallInst &CI;
  /// List of vector functions descriptors associated to the call
  /// instruction.
  const SmallVector<VFInfo, 8> ScalarToVectorMappings;

  /// Retrieve the scalar-to-vector mappings associated to the rule of
  /// a vector Function ABI.
  static void getVFABIMappings(const CallInst &CI,
                               SmallVectorImpl<VFInfo> &Mappings) {
    if (!CI.getCalledFunction())
      return;

    const StringRef ScalarName = CI.getCalledFunction()->getName();

    SmallVector<std::string, 8> ListOfStrings;
    // The check for the vector-function-abi-variant attribute is done when
    // retrieving the vector variant names here.
    VFABI::getVectorVariantNames(CI, ListOfStrings);
    if (ListOfStrings.empty())
      return;
    for (const auto &MangledName : ListOfStrings) {
      const std::optional<VFInfo> Shape =
          VFABI::tryDemangleForVFABI(MangledName, CI.getFunctionType());
      // A match is found via scalar and vector names, and also by
      // ensuring that the variant described in the attribute has a
      // corresponding definition or declaration of the vector
      // function in the Module M.
      if (Shape && (Shape->ScalarName == ScalarName)) {
        assert(CI.getModule()->getFunction(Shape->VectorName) &&
               "Vector function is missing.");
        Mappings.push_back(*Shape);
      }
    }
  }

public:
  /// Retrieve all the VFInfo instances associated to the CallInst CI.
  static SmallVector<VFInfo, 8> getMappings(const CallInst &CI) {
    SmallVector<VFInfo, 8> Ret;

    // Get mappings from the Vector Function ABI variants.
    getVFABIMappings(CI, Ret);

    // Other non-VFABI variants should be retrieved here.

    return Ret;
  }

  static bool hasMaskedVariant(const CallInst &CI,
                               std::optional<ElementCount> VF = std::nullopt) {
    // Check whether we have at least one masked vector version of a scalar
    // function. If no VF is specified then we check for any masked variant,
    // otherwise we look for one that matches the supplied VF.
    auto Mappings = VFDatabase::getMappings(CI);
    for (VFInfo Info : Mappings)
      if (!VF || Info.Shape.VF == *VF)
        if (Info.isMasked())
          return true;

    return false;
  }

  /// Constructor, requires a CallInst instance.
  VFDatabase(CallInst &CI)
      : M(CI.getModule()), CI(CI),
        ScalarToVectorMappings(VFDatabase::getMappings(CI)) {}

  /// \defgroup VFDatabase query interface.
  ///
  /// @{
  /// Retrieve the Function with VFShape \p Shape.
  Function *getVectorizedFunction(const VFShape &Shape) const {
    if (Shape == VFShape::getScalarShape(CI.getFunctionType()))
      return CI.getCalledFunction();

    for (const auto &Info : ScalarToVectorMappings)
      if (Info.Shape == Shape)
        return M->getFunction(Info.VectorName);

    return nullptr;
  }
  /// @}
};

template <typename T> class ArrayRef;
class DemandedBits;
template <typename InstTy> class InterleaveGroup;
class IRBuilderBase;
class Loop;
class ScalarEvolution;
class TargetTransformInfo;
class Type;
class Value;

namespace Intrinsic {
typedef unsigned ID;
}

/// A helper function for converting Scalar types to vector types. If
/// the incoming type is void, we return void. If the EC represents a
/// scalar, we return the scalar type.
inline Type *ToVectorTy(Type *Scalar, ElementCount EC) {
  if (Scalar->isVoidTy() || Scalar->isMetadataTy() || EC.isScalar())
    return Scalar;
  return VectorType::get(Scalar, EC);
}

inline Type *ToVectorTy(Type *Scalar, unsigned VF) {
  return ToVectorTy(Scalar, ElementCount::getFixed(VF));
}

/// Identify if the intrinsic is trivially vectorizable.
/// This method returns true if the intrinsic's argument types are all scalars
/// for the scalar form of the intrinsic and all vectors (or scalars handled by
/// isVectorIntrinsicWithScalarOpAtArg) for the vector form of the intrinsic.
bool isTriviallyVectorizable(Intrinsic::ID ID);

/// Identifies if the vector form of the intrinsic has a scalar operand.
bool isVectorIntrinsicWithScalarOpAtArg(Intrinsic::ID ID,
                                        unsigned ScalarOpdIdx);

/// Identifies if the vector form of the intrinsic is overloaded on the type of
/// the operand at index \p OpdIdx, or on the return type if \p OpdIdx is -1.
bool isVectorIntrinsicWithOverloadTypeAtArg(Intrinsic::ID ID, int OpdIdx);

/// Returns intrinsic ID for call.
/// For the input call instruction it finds mapping intrinsic and returns
/// its intrinsic ID, in case it does not found it return not_intrinsic.
Intrinsic::ID getVectorIntrinsicIDForCall(const CallInst *CI,
                                          const TargetLibraryInfo *TLI);

/// Given a vector and an element number, see if the scalar value is
/// already around as a register, for example if it were inserted then extracted
/// from the vector.
Value *findScalarElement(Value *V, unsigned EltNo);

/// If all non-negative \p Mask elements are the same value, return that value.
/// If all elements are negative (undefined) or \p Mask contains different
/// non-negative values, return -1.
int getSplatIndex(ArrayRef<int> Mask);

/// Get splat value if the input is a splat vector or return nullptr.
/// The value may be extracted from a splat constants vector or from
/// a sequence of instructions that broadcast a single value into a vector.
Value *getSplatValue(const Value *V);

/// Return true if each element of the vector value \p V is poisoned or equal to
/// every other non-poisoned element. If an index element is specified, either
/// every element of the vector is poisoned or the element at that index is not
/// poisoned and equal to every other non-poisoned element.
/// This may be more powerful than the related getSplatValue() because it is
/// not limited by finding a scalar source value to a splatted vector.
bool isSplatValue(const Value *V, int Index = -1, unsigned Depth = 0);

/// Transform a shuffle mask's output demanded element mask into demanded
/// element masks for the 2 operands, returns false if the mask isn't valid.
/// Both \p DemandedLHS and \p DemandedRHS are initialised to [SrcWidth].
/// \p AllowUndefElts permits "-1" indices to be treated as undef.
bool getShuffleDemandedElts(int SrcWidth, ArrayRef<int> Mask,
                            const APInt &DemandedElts, APInt &DemandedLHS,
                            APInt &DemandedRHS, bool AllowUndefElts = false);

/// Replace each shuffle mask index with the scaled sequential indices for an
/// equivalent mask of narrowed elements. Mask elements that are less than 0
/// (sentinel values) are repeated in the output mask.
///
/// Example with Scale = 4:
///   <4 x i32> <3, 2, 0, -1> -->
///   <16 x i8> <12, 13, 14, 15, 8, 9, 10, 11, 0, 1, 2, 3, -1, -1, -1, -1>
///
/// This is the reverse process of widening shuffle mask elements, but it always
/// succeeds because the indexes can always be multiplied (scaled up) to map to
/// narrower vector elements.
void narrowShuffleMaskElts(int Scale, ArrayRef<int> Mask,
                           SmallVectorImpl<int> &ScaledMask);

/// Try to transform a shuffle mask by replacing elements with the scaled index
/// for an equivalent mask of widened elements. If all mask elements that would
/// map to a wider element of the new mask are the same negative number
/// (sentinel value), that element of the new mask is the same value. If any
/// element in a given slice is negative and some other element in that slice is
/// not the same value, return false (partial matches with sentinel values are
/// not allowed).
///
/// Example with Scale = 4:
///   <16 x i8> <12, 13, 14, 15, 8, 9, 10, 11, 0, 1, 2, 3, -1, -1, -1, -1> -->
///   <4 x i32> <3, 2, 0, -1>
///
/// This is the reverse process of narrowing shuffle mask elements if it
/// succeeds. This transform is not always possible because indexes may not
/// divide evenly (scale down) to map to wider vector elements.
bool widenShuffleMaskElts(int Scale, ArrayRef<int> Mask,
                          SmallVectorImpl<int> &ScaledMask);

/// Attempt to narrow/widen the \p Mask shuffle mask to the \p NumDstElts target
/// width. Internally this will call narrowShuffleMaskElts/widenShuffleMaskElts.
/// This will assert unless NumDstElts is a multiple of Mask.size (or vice-versa).
/// Returns false on failure, and ScaledMask will be in an undefined state.
bool scaleShuffleMaskElts(unsigned NumDstElts, ArrayRef<int> Mask,
                          SmallVectorImpl<int> &ScaledMask);

/// Repetitively apply `widenShuffleMaskElts()` for as long as it succeeds,
/// to get the shuffle mask with widest possible elements.
void getShuffleMaskWithWidestElts(ArrayRef<int> Mask,
                                  SmallVectorImpl<int> &ScaledMask);

/// Splits and processes shuffle mask depending on the number of input and
/// output registers. The function does 2 main things: 1) splits the
/// source/destination vectors into real registers; 2) do the mask analysis to
/// identify which real registers are permuted. Then the function processes
/// resulting registers mask using provided action items. If no input register
/// is defined, \p NoInputAction action is used. If only 1 input register is
/// used, \p SingleInputAction is used, otherwise \p ManyInputsAction is used to
/// process > 2 input registers and masks.
/// \param Mask Original shuffle mask.
/// \param NumOfSrcRegs Number of source registers.
/// \param NumOfDestRegs Number of destination registers.
/// \param NumOfUsedRegs Number of actually used destination registers.
void processShuffleMasks(
    ArrayRef<int> Mask, unsigned NumOfSrcRegs, unsigned NumOfDestRegs,
    unsigned NumOfUsedRegs, function_ref<void()> NoInputAction,
    function_ref<void(ArrayRef<int>, unsigned, unsigned)> SingleInputAction,
    function_ref<void(ArrayRef<int>, unsigned, unsigned)> ManyInputsAction);

/// Compute the demanded elements mask of horizontal binary operations. A
/// horizontal operation combines two adjacent elements in a vector operand.
/// This function returns a mask for the elements that correspond to the first
/// operand of this horizontal combination. For example, for two vectors
/// [X1, X2, X3, X4] and [Y1, Y2, Y3, Y4], the resulting mask can include the
/// elements X1, X3, Y1, and Y3. To get the other operands, simply shift the
/// result of this function to the left by 1.
///
/// \param VectorBitWidth the total bit width of the vector
/// \param DemandedElts   the demanded elements mask for the operation
/// \param DemandedLHS    the demanded elements mask for the left operand
/// \param DemandedRHS    the demanded elements mask for the right operand
void getHorizDemandedEltsForFirstOperand(unsigned VectorBitWidth,
                                         const APInt &DemandedElts,
                                         APInt &DemandedLHS,
                                         APInt &DemandedRHS);

/// Compute a map of integer instructions to their minimum legal type
/// size.
///
/// C semantics force sub-int-sized values (e.g. i8, i16) to be promoted to int
/// type (e.g. i32) whenever arithmetic is performed on them.
///
/// For targets with native i8 or i16 operations, usually InstCombine can shrink
/// the arithmetic type down again. However InstCombine refuses to create
/// illegal types, so for targets without i8 or i16 registers, the lengthening
/// and shrinking remains.
///
/// Most SIMD ISAs (e.g. NEON) however support vectors of i8 or i16 even when
/// their scalar equivalents do not, so during vectorization it is important to
/// remove these lengthens and truncates when deciding the profitability of
/// vectorization.
///
/// This function analyzes the given range of instructions and determines the
/// minimum type size each can be converted to. It attempts to remove or
/// minimize type size changes across each def-use chain, so for example in the
/// following code:
///
///   %1 = load i8, i8*
///   %2 = add i8 %1, 2
///   %3 = load i16, i16*
///   %4 = zext i8 %2 to i32
///   %5 = zext i16 %3 to i32
///   %6 = add i32 %4, %5
///   %7 = trunc i32 %6 to i16
///
/// Instruction %6 must be done at least in i16, so computeMinimumValueSizes
/// will return: {%1: 16, %2: 16, %3: 16, %4: 16, %5: 16, %6: 16, %7: 16}.
///
/// If the optional TargetTransformInfo is provided, this function tries harder
/// to do less work by only looking at illegal types.
MapVector<Instruction*, uint64_t>
computeMinimumValueSizes(ArrayRef<BasicBlock*> Blocks,
                         DemandedBits &DB,
                         const TargetTransformInfo *TTI=nullptr);

/// Compute the union of two access-group lists.
///
/// If the list contains just one access group, it is returned directly. If the
/// list is empty, returns nullptr.
MDNode *uniteAccessGroups(MDNode *AccGroups1, MDNode *AccGroups2);

/// Compute the access-group list of access groups that @p Inst1 and @p Inst2
/// are both in. If either instruction does not access memory at all, it is
/// considered to be in every list.
///
/// If the list contains just one access group, it is returned directly. If the
/// list is empty, returns nullptr.
MDNode *intersectAccessGroups(const Instruction *Inst1,
                              const Instruction *Inst2);

/// Specifically, let Kinds = [MD_tbaa, MD_alias_scope, MD_noalias, MD_fpmath,
/// MD_nontemporal, MD_access_group, MD_mmra].
/// For K in Kinds, we get the MDNode for K from each of the
/// elements of VL, compute their "intersection" (i.e., the most generic
/// metadata value that covers all of the individual values), and set I's
/// metadata for M equal to the intersection value.
///
/// This function always sets a (possibly null) value for each K in Kinds.
Instruction *propagateMetadata(Instruction *I, ArrayRef<Value *> VL);

/// Create a mask that filters the members of an interleave group where there
/// are gaps.
///
/// For example, the mask for \p Group with interleave-factor 3
/// and \p VF 4, that has only its first member present is:
///
///   <1,0,0,1,0,0,1,0,0,1,0,0>
///
/// Note: The result is a mask of 0's and 1's, as opposed to the other
/// create[*]Mask() utilities which create a shuffle mask (mask that
/// consists of indices).
Constant *createBitMaskForGaps(IRBuilderBase &Builder, unsigned VF,
                               const InterleaveGroup<Instruction> &Group);

/// Create a mask with replicated elements.
///
/// This function creates a shuffle mask for replicating each of the \p VF
/// elements in a vector \p ReplicationFactor times. It can be used to
/// transform a mask of \p VF elements into a mask of
/// \p VF * \p ReplicationFactor elements used by a predicated
/// interleaved-group of loads/stores whose Interleaved-factor ==
/// \p ReplicationFactor.
///
/// For example, the mask for \p ReplicationFactor=3 and \p VF=4 is:
///
///   <0,0,0,1,1,1,2,2,2,3,3,3>
llvm::SmallVector<int, 16> createReplicatedMask(unsigned ReplicationFactor,
                                                unsigned VF);

/// Create an interleave shuffle mask.
///
/// This function creates a shuffle mask for interleaving \p NumVecs vectors of
/// vectorization factor \p VF into a single wide vector. The mask is of the
/// form:
///
///   <0, VF, VF * 2, ..., VF * (NumVecs - 1), 1, VF + 1, VF * 2 + 1, ...>
///
/// For example, the mask for VF = 4 and NumVecs = 2 is:
///
///   <0, 4, 1, 5, 2, 6, 3, 7>.
llvm::SmallVector<int, 16> createInterleaveMask(unsigned VF, unsigned NumVecs);

/// Create a stride shuffle mask.
///
/// This function creates a shuffle mask whose elements begin at \p Start and
/// are incremented by \p Stride. The mask can be used to deinterleave an
/// interleaved vector into separate vectors of vectorization factor \p VF. The
/// mask is of the form:
///
///   <Start, Start + Stride, ..., Start + Stride * (VF - 1)>
///
/// For example, the mask for Start = 0, Stride = 2, and VF = 4 is:
///
///   <0, 2, 4, 6>
llvm::SmallVector<int, 16> createStrideMask(unsigned Start, unsigned Stride,
                                            unsigned VF);

/// Create a sequential shuffle mask.
///
/// This function creates shuffle mask whose elements are sequential and begin
/// at \p Start.  The mask contains \p NumInts integers and is padded with \p
/// NumUndefs undef values. The mask is of the form:
///
///   <Start, Start + 1, ... Start + NumInts - 1, undef_1, ... undef_NumUndefs>
///
/// For example, the mask for Start = 0, NumInsts = 4, and NumUndefs = 4 is:
///
///   <0, 1, 2, 3, undef, undef, undef, undef>
llvm::SmallVector<int, 16>
createSequentialMask(unsigned Start, unsigned NumInts, unsigned NumUndefs);

/// Given a shuffle mask for a binary shuffle, create the equivalent shuffle
/// mask assuming both operands are identical. This assumes that the unary
/// shuffle will use elements from operand 0 (operand 1 will be unused).
llvm::SmallVector<int, 16> createUnaryMask(ArrayRef<int> Mask,
                                           unsigned NumElts);

/// Concatenate a list of vectors.
///
/// This function generates code that concatenate the vectors in \p Vecs into a
/// single large vector. The number of vectors should be greater than one, and
/// their element types should be the same. The number of elements in the
/// vectors should also be the same; however, if the last vector has fewer
/// elements, it will be padded with undefs.
Value *concatenateVectors(IRBuilderBase &Builder, ArrayRef<Value *> Vecs);

/// Given a mask vector of i1, Return true if all of the elements of this
/// predicate mask are known to be false or undef.  That is, return true if all
/// lanes can be assumed inactive.
bool maskIsAllZeroOrUndef(Value *Mask);

/// Given a mask vector of i1, Return true if all of the elements of this
/// predicate mask are known to be true or undef.  That is, return true if all
/// lanes can be assumed active.
bool maskIsAllOneOrUndef(Value *Mask);

/// Given a mask vector of i1, Return true if any of the elements of this
/// predicate mask are known to be true or undef.  That is, return true if at
/// least one lane can be assumed active.
bool maskContainsAllOneOrUndef(Value *Mask);

/// Given a mask vector of the form <Y x i1>, return an APInt (of bitwidth Y)
/// for each lane which may be active.
APInt possiblyDemandedEltsInMask(Value *Mask);

/// The group of interleaved loads/stores sharing the same stride and
/// close to each other.
///
/// Each member in this group has an index starting from 0, and the largest
/// index should be less than interleaved factor, which is equal to the absolute
/// value of the access's stride.
///
/// E.g. An interleaved load group of factor 4:
///        for (unsigned i = 0; i < 1024; i+=4) {
///          a = A[i];                           // Member of index 0
///          b = A[i+1];                         // Member of index 1
///          d = A[i+3];                         // Member of index 3
///          ...
///        }
///
///      An interleaved store group of factor 4:
///        for (unsigned i = 0; i < 1024; i+=4) {
///          ...
///          A[i]   = a;                         // Member of index 0
///          A[i+1] = b;                         // Member of index 1
///          A[i+2] = c;                         // Member of index 2
///          A[i+3] = d;                         // Member of index 3
///        }
///
/// Note: the interleaved load group could have gaps (missing members), but
/// the interleaved store group doesn't allow gaps.
template <typename InstTy> class InterleaveGroup {
public:
  InterleaveGroup(uint32_t Factor, bool Reverse, Align Alignment)
      : Factor(Factor), Reverse(Reverse), Alignment(Alignment),
        InsertPos(nullptr) {}

  InterleaveGroup(InstTy *Instr, int32_t Stride, Align Alignment)
      : Alignment(Alignment), InsertPos(Instr) {
    Factor = std::abs(Stride);
    assert(Factor > 1 && "Invalid interleave factor");

    Reverse = Stride < 0;
    Members[0] = Instr;
  }

  bool isReverse() const { return Reverse; }
  uint32_t getFactor() const { return Factor; }
  Align getAlign() const { return Alignment; }
  uint32_t getNumMembers() const { return Members.size(); }

  /// Try to insert a new member \p Instr with index \p Index and
  /// alignment \p NewAlign. The index is related to the leader and it could be
  /// negative if it is the new leader.
  ///
  /// \returns false if the instruction doesn't belong to the group.
  bool insertMember(InstTy *Instr, int32_t Index, Align NewAlign) {
    // Make sure the key fits in an int32_t.
    std::optional<int32_t> MaybeKey = checkedAdd(Index, SmallestKey);
    if (!MaybeKey)
      return false;
    int32_t Key = *MaybeKey;

    // Skip if the key is used for either the tombstone or empty special values.
    if (DenseMapInfo<int32_t>::getTombstoneKey() == Key ||
        DenseMapInfo<int32_t>::getEmptyKey() == Key)
      return false;

    // Skip if there is already a member with the same index.
    if (Members.contains(Key))
      return false;

    if (Key > LargestKey) {
      // The largest index is always less than the interleave factor.
      if (Index >= static_cast<int32_t>(Factor))
        return false;

      LargestKey = Key;
    } else if (Key < SmallestKey) {

      // Make sure the largest index fits in an int32_t.
      std::optional<int32_t> MaybeLargestIndex = checkedSub(LargestKey, Key);
      if (!MaybeLargestIndex)
        return false;

      // The largest index is always less than the interleave factor.
      if (*MaybeLargestIndex >= static_cast<int64_t>(Factor))
        return false;

      SmallestKey = Key;
    }

    // It's always safe to select the minimum alignment.
    Alignment = std::min(Alignment, NewAlign);
    Members[Key] = Instr;
    return true;
  }

  /// Get the member with the given index \p Index
  ///
  /// \returns nullptr if contains no such member.
  InstTy *getMember(uint32_t Index) const {
    int32_t Key = SmallestKey + Index;
    return Members.lookup(Key);
  }

  /// Get the index for the given member. Unlike the key in the member
  /// map, the index starts from 0.
  uint32_t getIndex(const InstTy *Instr) const {
    for (auto I : Members) {
      if (I.second == Instr)
        return I.first - SmallestKey;
    }

    llvm_unreachable("InterleaveGroup contains no such member");
  }

  InstTy *getInsertPos() const { return InsertPos; }
  void setInsertPos(InstTy *Inst) { InsertPos = Inst; }

  /// Add metadata (e.g. alias info) from the instructions in this group to \p
  /// NewInst.
  ///
  /// FIXME: this function currently does not add noalias metadata a'la
  /// addNewMedata.  To do that we need to compute the intersection of the
  /// noalias info from all members.
  void addMetadata(InstTy *NewInst) const;

  /// Returns true if this Group requires a scalar iteration to handle gaps.
  bool requiresScalarEpilogue() const {
    // If the last member of the Group exists, then a scalar epilog is not
    // needed for this group.
    if (getMember(getFactor() - 1))
      return false;

    // We have a group with gaps. It therefore can't be a reversed access,
    // because such groups get invalidated (TODO).
    assert(!isReverse() && "Group should have been invalidated");

    // This is a group of loads, with gaps, and without a last-member
    return true;
  }

private:
  uint32_t Factor; // Interleave Factor.
  bool Reverse;
  Align Alignment;
  DenseMap<int32_t, InstTy *> Members;
  int32_t SmallestKey = 0;
  int32_t LargestKey = 0;

  // To avoid breaking dependences, vectorized instructions of an interleave
  // group should be inserted at either the first load or the last store in
  // program order.
  //
  // E.g. %even = load i32             // Insert Position
  //      %add = add i32 %even         // Use of %even
  //      %odd = load i32
  //
  //      store i32 %even
  //      %odd = add i32               // Def of %odd
  //      store i32 %odd               // Insert Position
  InstTy *InsertPos;
};

/// Drive the analysis of interleaved memory accesses in the loop.
///
/// Use this class to analyze interleaved accesses only when we can vectorize
/// a loop. Otherwise it's meaningless to do analysis as the vectorization
/// on interleaved accesses is unsafe.
///
/// The analysis collects interleave groups and records the relationships
/// between the member and the group in a map.
class InterleavedAccessInfo {
public:
  InterleavedAccessInfo(PredicatedScalarEvolution &PSE, Loop *L,
                        DominatorTree *DT, LoopInfo *LI,
                        const LoopAccessInfo *LAI)
      : PSE(PSE), TheLoop(L), DT(DT), LI(LI), LAI(LAI) {}

  ~InterleavedAccessInfo() { invalidateGroups(); }

  /// Analyze the interleaved accesses and collect them in interleave
  /// groups. Substitute symbolic strides using \p Strides.
  /// Consider also predicated loads/stores in the analysis if
  /// \p EnableMaskedInterleavedGroup is true.
  void analyzeInterleaving(bool EnableMaskedInterleavedGroup);

  /// Invalidate groups, e.g., in case all blocks in loop will be predicated
  /// contrary to original assumption. Although we currently prevent group
  /// formation for predicated accesses, we may be able to relax this limitation
  /// in the future once we handle more complicated blocks. Returns true if any
  /// groups were invalidated.
  bool invalidateGroups() {
    if (InterleaveGroups.empty()) {
      assert(
          !RequiresScalarEpilogue &&
          "RequiresScalarEpilog should not be set without interleave groups");
      return false;
    }

    InterleaveGroupMap.clear();
    for (auto *Ptr : InterleaveGroups)
      delete Ptr;
    InterleaveGroups.clear();
    RequiresScalarEpilogue = false;
    return true;
  }

  /// Check if \p Instr belongs to any interleave group.
  bool isInterleaved(Instruction *Instr) const {
    return InterleaveGroupMap.contains(Instr);
  }

  /// Get the interleave group that \p Instr belongs to.
  ///
  /// \returns nullptr if doesn't have such group.
  InterleaveGroup<Instruction> *
  getInterleaveGroup(const Instruction *Instr) const {
    return InterleaveGroupMap.lookup(Instr);
  }

  iterator_range<SmallPtrSetIterator<llvm::InterleaveGroup<Instruction> *>>
  getInterleaveGroups() {
    return make_range(InterleaveGroups.begin(), InterleaveGroups.end());
  }

  /// Returns true if an interleaved group that may access memory
  /// out-of-bounds requires a scalar epilogue iteration for correctness.
  bool requiresScalarEpilogue() const { return RequiresScalarEpilogue; }

  /// Invalidate groups that require a scalar epilogue (due to gaps). This can
  /// happen when optimizing for size forbids a scalar epilogue, and the gap
  /// cannot be filtered by masking the load/store.
  void invalidateGroupsRequiringScalarEpilogue();

  /// Returns true if we have any interleave groups.
  bool hasGroups() const { return !InterleaveGroups.empty(); }

private:
  /// A wrapper around ScalarEvolution, used to add runtime SCEV checks.
  /// Simplifies SCEV expressions in the context of existing SCEV assumptions.
  /// The interleaved access analysis can also add new predicates (for example
  /// by versioning strides of pointers).
  PredicatedScalarEvolution &PSE;

  Loop *TheLoop;
  DominatorTree *DT;
  LoopInfo *LI;
  const LoopAccessInfo *LAI;

  /// True if the loop may contain non-reversed interleaved groups with
  /// out-of-bounds accesses. We ensure we don't speculatively access memory
  /// out-of-bounds by executing at least one scalar epilogue iteration.
  bool RequiresScalarEpilogue = false;

  /// Holds the relationships between the members and the interleave group.
  DenseMap<Instruction *, InterleaveGroup<Instruction> *> InterleaveGroupMap;

  SmallPtrSet<InterleaveGroup<Instruction> *, 4> InterleaveGroups;

  /// Holds dependences among the memory accesses in the loop. It maps a source
  /// access to a set of dependent sink accesses.
  DenseMap<Instruction *, SmallPtrSet<Instruction *, 2>> Dependences;

  /// The descriptor for a strided memory access.
  struct StrideDescriptor {
    StrideDescriptor() = default;
    StrideDescriptor(int64_t Stride, const SCEV *Scev, uint64_t Size,
                     Align Alignment)
        : Stride(Stride), Scev(Scev), Size(Size), Alignment(Alignment) {}

    // The access's stride. It is negative for a reverse access.
    int64_t Stride = 0;

    // The scalar expression of this access.
    const SCEV *Scev = nullptr;

    // The size of the memory object.
    uint64_t Size = 0;

    // The alignment of this access.
    Align Alignment;
  };

  /// A type for holding instructions and their stride descriptors.
  using StrideEntry = std::pair<Instruction *, StrideDescriptor>;

  /// Create a new interleave group with the given instruction \p Instr,
  /// stride \p Stride and alignment \p Align.
  ///
  /// \returns the newly created interleave group.
  InterleaveGroup<Instruction> *
  createInterleaveGroup(Instruction *Instr, int Stride, Align Alignment) {
    assert(!InterleaveGroupMap.count(Instr) &&
           "Already in an interleaved access group");
    InterleaveGroupMap[Instr] =
        new InterleaveGroup<Instruction>(Instr, Stride, Alignment);
    InterleaveGroups.insert(InterleaveGroupMap[Instr]);
    return InterleaveGroupMap[Instr];
  }

  /// Release the group and remove all the relationships.
  void releaseGroup(InterleaveGroup<Instruction> *Group) {
    InterleaveGroups.erase(Group);
    releaseGroupWithoutRemovingFromSet(Group);
  }

  /// Do everything necessary to release the group, apart from removing it from
  /// the InterleaveGroups set.
  void releaseGroupWithoutRemovingFromSet(InterleaveGroup<Instruction> *Group) {
    for (unsigned i = 0; i < Group->getFactor(); i++)
      if (Instruction *Member = Group->getMember(i))
        InterleaveGroupMap.erase(Member);

    delete Group;
  }

  /// Collect all the accesses with a constant stride in program order.
  void collectConstStrideAccesses(
      MapVector<Instruction *, StrideDescriptor> &AccessStrideInfo,
      const DenseMap<Value *, const SCEV *> &Strides);

  /// Returns true if \p Stride is allowed in an interleaved group.
  static bool isStrided(int Stride);

  /// Returns true if \p BB is a predicated block.
  bool isPredicated(BasicBlock *BB) const {
    return LoopAccessInfo::blockNeedsPredication(BB, TheLoop, DT);
  }

  /// Returns true if LoopAccessInfo can be used for dependence queries.
  bool areDependencesValid() const {
    return LAI && LAI->getDepChecker().getDependences();
  }

  /// Returns true if memory accesses \p A and \p B can be reordered, if
  /// necessary, when constructing interleaved groups.
  ///
  /// \p A must precede \p B in program order. We return false if reordering is
  /// not necessary or is prevented because \p A and \p B may be dependent.
  bool canReorderMemAccessesForInterleavedGroups(StrideEntry *A,
                                                 StrideEntry *B) const {
    // Code motion for interleaved accesses can potentially hoist strided loads
    // and sink strided stores. The code below checks the legality of the
    // following two conditions:
    //
    // 1. Potentially moving a strided load (B) before any store (A) that
    //    precedes B, or
    //
    // 2. Potentially moving a strided store (A) after any load or store (B)
    //    that A precedes.
    //
    // It's legal to reorder A and B if we know there isn't a dependence from A
    // to B. Note that this determination is conservative since some
    // dependences could potentially be reordered safely.

    // A is potentially the source of a dependence.
    auto *Src = A->first;
    auto SrcDes = A->second;

    // B is potentially the sink of a dependence.
    auto *Sink = B->first;
    auto SinkDes = B->second;

    // Code motion for interleaved accesses can't violate WAR dependences.
    // Thus, reordering is legal if the source isn't a write.
    if (!Src->mayWriteToMemory())
      return true;

    // At least one of the accesses must be strided.
    if (!isStrided(SrcDes.Stride) && !isStrided(SinkDes.Stride))
      return true;

    // If dependence information is not available from LoopAccessInfo,
    // conservatively assume the instructions can't be reordered.
    if (!areDependencesValid())
      return false;

    // If we know there is a dependence from source to sink, assume the
    // instructions can't be reordered. Otherwise, reordering is legal.
    return !Dependences.contains(Src) || !Dependences.lookup(Src).count(Sink);
  }

  /// Collect the dependences from LoopAccessInfo.
  ///
  /// We process the dependences once during the interleaved access analysis to
  /// enable constant-time dependence queries.
  void collectDependences() {
    if (!areDependencesValid())
      return;
    const auto &DepChecker = LAI->getDepChecker();
    auto *Deps = DepChecker.getDependences();
    for (auto Dep : *Deps)
      Dependences[Dep.getSource(DepChecker)].insert(
          Dep.getDestination(DepChecker));
  }
};

} // llvm namespace

#endif
