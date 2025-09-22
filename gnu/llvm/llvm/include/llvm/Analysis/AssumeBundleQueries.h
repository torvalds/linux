//===- AssumeBundleQueries.h - utilis to query assume bundles ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contain tools to query into assume bundles. assume bundles can be
// built using utilities from Transform/Utils/AssumeBundleBuilder.h
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_ASSUMEBUNDLEQUERIES_H
#define LLVM_ANALYSIS_ASSUMEBUNDLEQUERIES_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/IntrinsicInst.h"

namespace llvm {
class AssumptionCache;
class DominatorTree;
class Instruction;
class Value;

/// Index of elements in the operand bundle.
/// If the element exist it is guaranteed to be what is specified in this enum
/// but it may not exist.
enum AssumeBundleArg {
  ABA_WasOn = 0,
  ABA_Argument = 1,
};

/// Query the operand bundle of an llvm.assume to find a single attribute of
/// the specified kind applied on a specified Value.
///
/// This has a non-constant complexity. It should only be used when a single
/// attribute is going to be queried.
///
/// Return true iff the queried attribute was found.
/// If ArgVal is set. the argument will be stored to ArgVal.
bool hasAttributeInAssume(AssumeInst &Assume, Value *IsOn, StringRef AttrName,
                          uint64_t *ArgVal = nullptr);
inline bool hasAttributeInAssume(AssumeInst &Assume, Value *IsOn,
                                 Attribute::AttrKind Kind,
                                 uint64_t *ArgVal = nullptr) {
  return hasAttributeInAssume(Assume, IsOn,
                              Attribute::getNameFromAttrKind(Kind), ArgVal);
}

template<> struct DenseMapInfo<Attribute::AttrKind> {
  static Attribute::AttrKind getEmptyKey() {
    return Attribute::EmptyKey;
  }
  static Attribute::AttrKind getTombstoneKey() {
    return Attribute::TombstoneKey;
  }
  static unsigned getHashValue(Attribute::AttrKind AK) {
    return hash_combine(AK);
  }
  static bool isEqual(Attribute::AttrKind LHS, Attribute::AttrKind RHS) {
    return LHS == RHS;
  }
};

/// The map Key contains the Value on for which the attribute is valid and
/// the Attribute that is valid for that value.
/// If the Attribute is not on any value, the Value is nullptr.
using RetainedKnowledgeKey = std::pair<Value *, Attribute::AttrKind>;

struct MinMax {
  uint64_t Min;
  uint64_t Max;
};

/// A mapping from intrinsics (=`llvm.assume` calls) to a value range
/// (=knowledge) that is encoded in them. How the value range is interpreted
/// depends on the RetainedKnowledgeKey that was used to get this out of the
/// RetainedKnowledgeMap.
using Assume2KnowledgeMap = DenseMap<AssumeInst *, MinMax>;

using RetainedKnowledgeMap =
    DenseMap<RetainedKnowledgeKey, Assume2KnowledgeMap>;

/// Insert into the map all the informations contained in the operand bundles of
/// the llvm.assume. This should be used instead of hasAttributeInAssume when
/// many queries are going to be made on the same llvm.assume.
/// String attributes are not inserted in the map.
/// If the IR changes the map will be outdated.
void fillMapFromAssume(AssumeInst &Assume, RetainedKnowledgeMap &Result);

/// Represent one information held inside an operand bundle of an llvm.assume.
/// AttrKind is the property that holds.
/// WasOn if not null is that Value for which AttrKind holds.
/// ArgValue is optionally an argument of the attribute.
/// For example if we know that %P has an alignment of at least four:
///  - AttrKind will be Attribute::Alignment.
///  - WasOn will be %P.
///  - ArgValue will be 4.
struct RetainedKnowledge {
  Attribute::AttrKind AttrKind = Attribute::None;
  uint64_t ArgValue = 0;
  Value *WasOn = nullptr;
  bool operator==(RetainedKnowledge Other) const {
    return AttrKind == Other.AttrKind && WasOn == Other.WasOn &&
           ArgValue == Other.ArgValue;
  }
  bool operator!=(RetainedKnowledge Other) const { return !(*this == Other); }
  /// This is only intended for use in std::min/std::max between attribute that
  /// only differ in ArgValue.
  bool operator<(RetainedKnowledge Other) const {
    assert(((AttrKind == Other.AttrKind && WasOn == Other.WasOn) ||
            AttrKind == Attribute::None || Other.AttrKind == Attribute::None) &&
           "This is only intend for use in min/max to select the best for "
           "RetainedKnowledge that is otherwise equal");
    return ArgValue < Other.ArgValue;
  }
  operator bool() const { return AttrKind != Attribute::None; }
  static RetainedKnowledge none() { return RetainedKnowledge{}; }
};

/// Retreive the information help by Assume on the operand at index Idx.
/// Assume should be an llvm.assume and Idx should be in the operand bundle.
RetainedKnowledge getKnowledgeFromOperandInAssume(AssumeInst &Assume,
                                                  unsigned Idx);

/// Retreive the information help by the Use U of an llvm.assume. the use should
/// be in the operand bundle.
inline RetainedKnowledge getKnowledgeFromUseInAssume(const Use *U) {
  return getKnowledgeFromOperandInAssume(*cast<AssumeInst>(U->getUser()),
                                         U->getOperandNo());
}

/// Tag in operand bundle indicating that this bundle should be ignored.
constexpr StringRef IgnoreBundleTag = "ignore";

/// Return true iff the operand bundles of the provided llvm.assume doesn't
/// contain any valuable information. This is true when:
///  - The operand bundle is empty
///  - The operand bundle only contains information about dropped values or
///    constant folded values.
///
/// the argument to the call of llvm.assume may still be useful even if the
/// function returned true.
bool isAssumeWithEmptyBundle(const AssumeInst &Assume);

/// Return a valid Knowledge associated to the Use U if its Attribute kind is
/// in AttrKinds.
RetainedKnowledge getKnowledgeFromUse(const Use *U,
                                      ArrayRef<Attribute::AttrKind> AttrKinds);

/// Return a valid Knowledge associated to the Value V if its Attribute kind is
/// in AttrKinds and it matches the Filter.
RetainedKnowledge getKnowledgeForValue(
    const Value *V, ArrayRef<Attribute::AttrKind> AttrKinds,
    AssumptionCache *AC = nullptr,
    function_ref<bool(RetainedKnowledge, Instruction *,
                            const CallBase::BundleOpInfo *)>
        Filter = [](auto...) { return true; });

/// Return a valid Knowledge associated to the Value V if its Attribute kind is
/// in AttrKinds and the knowledge is suitable to be used in the context of
/// CtxI.
RetainedKnowledge getKnowledgeValidInContext(
    const Value *V, ArrayRef<Attribute::AttrKind> AttrKinds,
    const Instruction *CtxI, const DominatorTree *DT = nullptr,
    AssumptionCache *AC = nullptr);

/// This extracts the Knowledge from an element of an operand bundle.
/// This is mostly for use in the assume builder.
RetainedKnowledge getKnowledgeFromBundle(AssumeInst &Assume,
                                         const CallBase::BundleOpInfo &BOI);

} // namespace llvm

#endif
