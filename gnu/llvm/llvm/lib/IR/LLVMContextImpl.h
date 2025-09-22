//===- LLVMContextImpl.h - The LLVMContextImpl opaque class -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file declares LLVMContextImpl, the opaque implementation
//  of LLVMContext.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_IR_LLVMCONTEXTIMPL_H
#define LLVM_LIB_IR_LLVMCONTEXTIMPL_H

#include "ConstantsContext.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/TrackingMDRef.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/StringSaver.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

class AttributeImpl;
class AttributeListImpl;
class AttributeSetNode;
class BasicBlock;
class ConstantRangeAttributeImpl;
class ConstantRangeListAttributeImpl;
struct DiagnosticHandler;
class DbgMarker;
class ElementCount;
class Function;
class GlobalObject;
class GlobalValue;
class InlineAsm;
class LLVMRemarkStreamer;
class OptPassGate;
namespace remarks {
class RemarkStreamer;
}
template <typename T> class StringMapEntry;
class StringRef;
class TypedPointerType;
class ValueHandleBase;

template <> struct DenseMapInfo<APFloat> {
  static inline APFloat getEmptyKey() { return APFloat(APFloat::Bogus(), 1); }
  static inline APFloat getTombstoneKey() {
    return APFloat(APFloat::Bogus(), 2);
  }

  static unsigned getHashValue(const APFloat &Key) {
    return static_cast<unsigned>(hash_value(Key));
  }

  static bool isEqual(const APFloat &LHS, const APFloat &RHS) {
    return LHS.bitwiseIsEqual(RHS);
  }
};

struct AnonStructTypeKeyInfo {
  struct KeyTy {
    ArrayRef<Type *> ETypes;
    bool isPacked;

    KeyTy(const ArrayRef<Type *> &E, bool P) : ETypes(E), isPacked(P) {}

    KeyTy(const StructType *ST)
        : ETypes(ST->elements()), isPacked(ST->isPacked()) {}

    bool operator==(const KeyTy &that) const {
      if (isPacked != that.isPacked)
        return false;
      if (ETypes != that.ETypes)
        return false;
      return true;
    }
    bool operator!=(const KeyTy &that) const { return !this->operator==(that); }
  };

  static inline StructType *getEmptyKey() {
    return DenseMapInfo<StructType *>::getEmptyKey();
  }

  static inline StructType *getTombstoneKey() {
    return DenseMapInfo<StructType *>::getTombstoneKey();
  }

  static unsigned getHashValue(const KeyTy &Key) {
    return hash_combine(
        hash_combine_range(Key.ETypes.begin(), Key.ETypes.end()), Key.isPacked);
  }

  static unsigned getHashValue(const StructType *ST) {
    return getHashValue(KeyTy(ST));
  }

  static bool isEqual(const KeyTy &LHS, const StructType *RHS) {
    if (RHS == getEmptyKey() || RHS == getTombstoneKey())
      return false;
    return LHS == KeyTy(RHS);
  }

  static bool isEqual(const StructType *LHS, const StructType *RHS) {
    return LHS == RHS;
  }
};

struct FunctionTypeKeyInfo {
  struct KeyTy {
    const Type *ReturnType;
    ArrayRef<Type *> Params;
    bool isVarArg;

    KeyTy(const Type *R, const ArrayRef<Type *> &P, bool V)
        : ReturnType(R), Params(P), isVarArg(V) {}
    KeyTy(const FunctionType *FT)
        : ReturnType(FT->getReturnType()), Params(FT->params()),
          isVarArg(FT->isVarArg()) {}

    bool operator==(const KeyTy &that) const {
      if (ReturnType != that.ReturnType)
        return false;
      if (isVarArg != that.isVarArg)
        return false;
      if (Params != that.Params)
        return false;
      return true;
    }
    bool operator!=(const KeyTy &that) const { return !this->operator==(that); }
  };

  static inline FunctionType *getEmptyKey() {
    return DenseMapInfo<FunctionType *>::getEmptyKey();
  }

  static inline FunctionType *getTombstoneKey() {
    return DenseMapInfo<FunctionType *>::getTombstoneKey();
  }

  static unsigned getHashValue(const KeyTy &Key) {
    return hash_combine(
        Key.ReturnType,
        hash_combine_range(Key.Params.begin(), Key.Params.end()), Key.isVarArg);
  }

  static unsigned getHashValue(const FunctionType *FT) {
    return getHashValue(KeyTy(FT));
  }

  static bool isEqual(const KeyTy &LHS, const FunctionType *RHS) {
    if (RHS == getEmptyKey() || RHS == getTombstoneKey())
      return false;
    return LHS == KeyTy(RHS);
  }

  static bool isEqual(const FunctionType *LHS, const FunctionType *RHS) {
    return LHS == RHS;
  }
};

struct TargetExtTypeKeyInfo {
  struct KeyTy {
    StringRef Name;
    ArrayRef<Type *> TypeParams;
    ArrayRef<unsigned> IntParams;

    KeyTy(StringRef N, const ArrayRef<Type *> &TP, const ArrayRef<unsigned> &IP)
        : Name(N), TypeParams(TP), IntParams(IP) {}
    KeyTy(const TargetExtType *TT)
        : Name(TT->getName()), TypeParams(TT->type_params()),
          IntParams(TT->int_params()) {}

    bool operator==(const KeyTy &that) const {
      return Name == that.Name && TypeParams == that.TypeParams &&
             IntParams == that.IntParams;
    }
    bool operator!=(const KeyTy &that) const { return !this->operator==(that); }
  };

  static inline TargetExtType *getEmptyKey() {
    return DenseMapInfo<TargetExtType *>::getEmptyKey();
  }

  static inline TargetExtType *getTombstoneKey() {
    return DenseMapInfo<TargetExtType *>::getTombstoneKey();
  }

  static unsigned getHashValue(const KeyTy &Key) {
    return hash_combine(
        Key.Name,
        hash_combine_range(Key.TypeParams.begin(), Key.TypeParams.end()),
        hash_combine_range(Key.IntParams.begin(), Key.IntParams.end()));
  }

  static unsigned getHashValue(const TargetExtType *FT) {
    return getHashValue(KeyTy(FT));
  }

  static bool isEqual(const KeyTy &LHS, const TargetExtType *RHS) {
    if (RHS == getEmptyKey() || RHS == getTombstoneKey())
      return false;
    return LHS == KeyTy(RHS);
  }

  static bool isEqual(const TargetExtType *LHS, const TargetExtType *RHS) {
    return LHS == RHS;
  }
};

/// Structure for hashing arbitrary MDNode operands.
class MDNodeOpsKey {
  ArrayRef<Metadata *> RawOps;
  ArrayRef<MDOperand> Ops;
  unsigned Hash;

protected:
  MDNodeOpsKey(ArrayRef<Metadata *> Ops)
      : RawOps(Ops), Hash(calculateHash(Ops)) {}

  template <class NodeTy>
  MDNodeOpsKey(const NodeTy *N, unsigned Offset = 0)
      : Ops(N->op_begin() + Offset, N->op_end()), Hash(N->getHash()) {}

  template <class NodeTy>
  bool compareOps(const NodeTy *RHS, unsigned Offset = 0) const {
    if (getHash() != RHS->getHash())
      return false;

    assert((RawOps.empty() || Ops.empty()) && "Two sets of operands?");
    return RawOps.empty() ? compareOps(Ops, RHS, Offset)
                          : compareOps(RawOps, RHS, Offset);
  }

  static unsigned calculateHash(MDNode *N, unsigned Offset = 0);

private:
  template <class T>
  static bool compareOps(ArrayRef<T> Ops, const MDNode *RHS, unsigned Offset) {
    if (Ops.size() != RHS->getNumOperands() - Offset)
      return false;
    return std::equal(Ops.begin(), Ops.end(), RHS->op_begin() + Offset);
  }

  static unsigned calculateHash(ArrayRef<Metadata *> Ops);

public:
  unsigned getHash() const { return Hash; }
};

template <class NodeTy> struct MDNodeKeyImpl;

/// Configuration point for MDNodeInfo::isEqual().
template <class NodeTy> struct MDNodeSubsetEqualImpl {
  using KeyTy = MDNodeKeyImpl<NodeTy>;

  static bool isSubsetEqual(const KeyTy &LHS, const NodeTy *RHS) {
    return false;
  }

  static bool isSubsetEqual(const NodeTy *LHS, const NodeTy *RHS) {
    return false;
  }
};

/// DenseMapInfo for MDTuple.
///
/// Note that we don't need the is-function-local bit, since that's implicit in
/// the operands.
template <> struct MDNodeKeyImpl<MDTuple> : MDNodeOpsKey {
  MDNodeKeyImpl(ArrayRef<Metadata *> Ops) : MDNodeOpsKey(Ops) {}
  MDNodeKeyImpl(const MDTuple *N) : MDNodeOpsKey(N) {}

  bool isKeyOf(const MDTuple *RHS) const { return compareOps(RHS); }

  unsigned getHashValue() const { return getHash(); }

  static unsigned calculateHash(MDTuple *N) {
    return MDNodeOpsKey::calculateHash(N);
  }
};

/// DenseMapInfo for DILocation.
template <> struct MDNodeKeyImpl<DILocation> {
  unsigned Line;
  unsigned Column;
  Metadata *Scope;
  Metadata *InlinedAt;
  bool ImplicitCode;

  MDNodeKeyImpl(unsigned Line, unsigned Column, Metadata *Scope,
                Metadata *InlinedAt, bool ImplicitCode)
      : Line(Line), Column(Column), Scope(Scope), InlinedAt(InlinedAt),
        ImplicitCode(ImplicitCode) {}
  MDNodeKeyImpl(const DILocation *L)
      : Line(L->getLine()), Column(L->getColumn()), Scope(L->getRawScope()),
        InlinedAt(L->getRawInlinedAt()), ImplicitCode(L->isImplicitCode()) {}

  bool isKeyOf(const DILocation *RHS) const {
    return Line == RHS->getLine() && Column == RHS->getColumn() &&
           Scope == RHS->getRawScope() && InlinedAt == RHS->getRawInlinedAt() &&
           ImplicitCode == RHS->isImplicitCode();
  }

  unsigned getHashValue() const {
    return hash_combine(Line, Column, Scope, InlinedAt, ImplicitCode);
  }
};

/// DenseMapInfo for GenericDINode.
template <> struct MDNodeKeyImpl<GenericDINode> : MDNodeOpsKey {
  unsigned Tag;
  MDString *Header;

  MDNodeKeyImpl(unsigned Tag, MDString *Header, ArrayRef<Metadata *> DwarfOps)
      : MDNodeOpsKey(DwarfOps), Tag(Tag), Header(Header) {}
  MDNodeKeyImpl(const GenericDINode *N)
      : MDNodeOpsKey(N, 1), Tag(N->getTag()), Header(N->getRawHeader()) {}

  bool isKeyOf(const GenericDINode *RHS) const {
    return Tag == RHS->getTag() && Header == RHS->getRawHeader() &&
           compareOps(RHS, 1);
  }

  unsigned getHashValue() const { return hash_combine(getHash(), Tag, Header); }

  static unsigned calculateHash(GenericDINode *N) {
    return MDNodeOpsKey::calculateHash(N, 1);
  }
};

template <> struct MDNodeKeyImpl<DISubrange> {
  Metadata *CountNode;
  Metadata *LowerBound;
  Metadata *UpperBound;
  Metadata *Stride;

  MDNodeKeyImpl(Metadata *CountNode, Metadata *LowerBound, Metadata *UpperBound,
                Metadata *Stride)
      : CountNode(CountNode), LowerBound(LowerBound), UpperBound(UpperBound),
        Stride(Stride) {}
  MDNodeKeyImpl(const DISubrange *N)
      : CountNode(N->getRawCountNode()), LowerBound(N->getRawLowerBound()),
        UpperBound(N->getRawUpperBound()), Stride(N->getRawStride()) {}

  bool isKeyOf(const DISubrange *RHS) const {
    auto BoundsEqual = [=](Metadata *Node1, Metadata *Node2) -> bool {
      if (Node1 == Node2)
        return true;

      ConstantAsMetadata *MD1 = dyn_cast_or_null<ConstantAsMetadata>(Node1);
      ConstantAsMetadata *MD2 = dyn_cast_or_null<ConstantAsMetadata>(Node2);
      if (MD1 && MD2) {
        ConstantInt *CV1 = cast<ConstantInt>(MD1->getValue());
        ConstantInt *CV2 = cast<ConstantInt>(MD2->getValue());
        if (CV1->getSExtValue() == CV2->getSExtValue())
          return true;
      }
      return false;
    };

    return BoundsEqual(CountNode, RHS->getRawCountNode()) &&
           BoundsEqual(LowerBound, RHS->getRawLowerBound()) &&
           BoundsEqual(UpperBound, RHS->getRawUpperBound()) &&
           BoundsEqual(Stride, RHS->getRawStride());
  }

  unsigned getHashValue() const {
    if (CountNode)
      if (auto *MD = dyn_cast<ConstantAsMetadata>(CountNode))
        return hash_combine(cast<ConstantInt>(MD->getValue())->getSExtValue(),
                            LowerBound, UpperBound, Stride);
    return hash_combine(CountNode, LowerBound, UpperBound, Stride);
  }
};

template <> struct MDNodeKeyImpl<DIGenericSubrange> {
  Metadata *CountNode;
  Metadata *LowerBound;
  Metadata *UpperBound;
  Metadata *Stride;

  MDNodeKeyImpl(Metadata *CountNode, Metadata *LowerBound, Metadata *UpperBound,
                Metadata *Stride)
      : CountNode(CountNode), LowerBound(LowerBound), UpperBound(UpperBound),
        Stride(Stride) {}
  MDNodeKeyImpl(const DIGenericSubrange *N)
      : CountNode(N->getRawCountNode()), LowerBound(N->getRawLowerBound()),
        UpperBound(N->getRawUpperBound()), Stride(N->getRawStride()) {}

  bool isKeyOf(const DIGenericSubrange *RHS) const {
    return (CountNode == RHS->getRawCountNode()) &&
           (LowerBound == RHS->getRawLowerBound()) &&
           (UpperBound == RHS->getRawUpperBound()) &&
           (Stride == RHS->getRawStride());
  }

  unsigned getHashValue() const {
    auto *MD = dyn_cast_or_null<ConstantAsMetadata>(CountNode);
    if (CountNode && MD)
      return hash_combine(cast<ConstantInt>(MD->getValue())->getSExtValue(),
                          LowerBound, UpperBound, Stride);
    return hash_combine(CountNode, LowerBound, UpperBound, Stride);
  }
};

template <> struct MDNodeKeyImpl<DIEnumerator> {
  APInt Value;
  MDString *Name;
  bool IsUnsigned;

  MDNodeKeyImpl(APInt Value, bool IsUnsigned, MDString *Name)
      : Value(std::move(Value)), Name(Name), IsUnsigned(IsUnsigned) {}
  MDNodeKeyImpl(int64_t Value, bool IsUnsigned, MDString *Name)
      : Value(APInt(64, Value, !IsUnsigned)), Name(Name),
        IsUnsigned(IsUnsigned) {}
  MDNodeKeyImpl(const DIEnumerator *N)
      : Value(N->getValue()), Name(N->getRawName()),
        IsUnsigned(N->isUnsigned()) {}

  bool isKeyOf(const DIEnumerator *RHS) const {
    return Value.getBitWidth() == RHS->getValue().getBitWidth() &&
           Value == RHS->getValue() && IsUnsigned == RHS->isUnsigned() &&
           Name == RHS->getRawName();
  }

  unsigned getHashValue() const { return hash_combine(Value, Name); }
};

template <> struct MDNodeKeyImpl<DIBasicType> {
  unsigned Tag;
  MDString *Name;
  uint64_t SizeInBits;
  uint32_t AlignInBits;
  unsigned Encoding;
  unsigned Flags;

  MDNodeKeyImpl(unsigned Tag, MDString *Name, uint64_t SizeInBits,
                uint32_t AlignInBits, unsigned Encoding, unsigned Flags)
      : Tag(Tag), Name(Name), SizeInBits(SizeInBits), AlignInBits(AlignInBits),
        Encoding(Encoding), Flags(Flags) {}
  MDNodeKeyImpl(const DIBasicType *N)
      : Tag(N->getTag()), Name(N->getRawName()), SizeInBits(N->getSizeInBits()),
        AlignInBits(N->getAlignInBits()), Encoding(N->getEncoding()),
        Flags(N->getFlags()) {}

  bool isKeyOf(const DIBasicType *RHS) const {
    return Tag == RHS->getTag() && Name == RHS->getRawName() &&
           SizeInBits == RHS->getSizeInBits() &&
           AlignInBits == RHS->getAlignInBits() &&
           Encoding == RHS->getEncoding() && Flags == RHS->getFlags();
  }

  unsigned getHashValue() const {
    return hash_combine(Tag, Name, SizeInBits, AlignInBits, Encoding);
  }
};

template <> struct MDNodeKeyImpl<DIStringType> {
  unsigned Tag;
  MDString *Name;
  Metadata *StringLength;
  Metadata *StringLengthExp;
  Metadata *StringLocationExp;
  uint64_t SizeInBits;
  uint32_t AlignInBits;
  unsigned Encoding;

  MDNodeKeyImpl(unsigned Tag, MDString *Name, Metadata *StringLength,
                Metadata *StringLengthExp, Metadata *StringLocationExp,
                uint64_t SizeInBits, uint32_t AlignInBits, unsigned Encoding)
      : Tag(Tag), Name(Name), StringLength(StringLength),
        StringLengthExp(StringLengthExp), StringLocationExp(StringLocationExp),
        SizeInBits(SizeInBits), AlignInBits(AlignInBits), Encoding(Encoding) {}
  MDNodeKeyImpl(const DIStringType *N)
      : Tag(N->getTag()), Name(N->getRawName()),
        StringLength(N->getRawStringLength()),
        StringLengthExp(N->getRawStringLengthExp()),
        StringLocationExp(N->getRawStringLocationExp()),
        SizeInBits(N->getSizeInBits()), AlignInBits(N->getAlignInBits()),
        Encoding(N->getEncoding()) {}

  bool isKeyOf(const DIStringType *RHS) const {
    return Tag == RHS->getTag() && Name == RHS->getRawName() &&
           StringLength == RHS->getRawStringLength() &&
           StringLengthExp == RHS->getRawStringLengthExp() &&
           StringLocationExp == RHS->getRawStringLocationExp() &&
           SizeInBits == RHS->getSizeInBits() &&
           AlignInBits == RHS->getAlignInBits() &&
           Encoding == RHS->getEncoding();
  }
  unsigned getHashValue() const {
    // Intentionally computes the hash on a subset of the operands for
    // performance reason. The subset has to be significant enough to avoid
    // collision "most of the time". There is no correctness issue in case of
    // collision because of the full check above.
    return hash_combine(Tag, Name, StringLength, Encoding);
  }
};

template <> struct MDNodeKeyImpl<DIDerivedType> {
  unsigned Tag;
  MDString *Name;
  Metadata *File;
  unsigned Line;
  Metadata *Scope;
  Metadata *BaseType;
  uint64_t SizeInBits;
  uint64_t OffsetInBits;
  uint32_t AlignInBits;
  std::optional<unsigned> DWARFAddressSpace;
  std::optional<DIDerivedType::PtrAuthData> PtrAuthData;
  unsigned Flags;
  Metadata *ExtraData;
  Metadata *Annotations;

  MDNodeKeyImpl(unsigned Tag, MDString *Name, Metadata *File, unsigned Line,
                Metadata *Scope, Metadata *BaseType, uint64_t SizeInBits,
                uint32_t AlignInBits, uint64_t OffsetInBits,
                std::optional<unsigned> DWARFAddressSpace,
                std::optional<DIDerivedType::PtrAuthData> PtrAuthData,
                unsigned Flags, Metadata *ExtraData, Metadata *Annotations)
      : Tag(Tag), Name(Name), File(File), Line(Line), Scope(Scope),
        BaseType(BaseType), SizeInBits(SizeInBits), OffsetInBits(OffsetInBits),
        AlignInBits(AlignInBits), DWARFAddressSpace(DWARFAddressSpace),
        PtrAuthData(PtrAuthData), Flags(Flags), ExtraData(ExtraData),
        Annotations(Annotations) {}
  MDNodeKeyImpl(const DIDerivedType *N)
      : Tag(N->getTag()), Name(N->getRawName()), File(N->getRawFile()),
        Line(N->getLine()), Scope(N->getRawScope()),
        BaseType(N->getRawBaseType()), SizeInBits(N->getSizeInBits()),
        OffsetInBits(N->getOffsetInBits()), AlignInBits(N->getAlignInBits()),
        DWARFAddressSpace(N->getDWARFAddressSpace()),
        PtrAuthData(N->getPtrAuthData()), Flags(N->getFlags()),
        ExtraData(N->getRawExtraData()), Annotations(N->getRawAnnotations()) {}

  bool isKeyOf(const DIDerivedType *RHS) const {
    return Tag == RHS->getTag() && Name == RHS->getRawName() &&
           File == RHS->getRawFile() && Line == RHS->getLine() &&
           Scope == RHS->getRawScope() && BaseType == RHS->getRawBaseType() &&
           SizeInBits == RHS->getSizeInBits() &&
           AlignInBits == RHS->getAlignInBits() &&
           OffsetInBits == RHS->getOffsetInBits() &&
           DWARFAddressSpace == RHS->getDWARFAddressSpace() &&
           PtrAuthData == RHS->getPtrAuthData() && Flags == RHS->getFlags() &&
           ExtraData == RHS->getRawExtraData() &&
           Annotations == RHS->getRawAnnotations();
  }

  unsigned getHashValue() const {
    // If this is a member inside an ODR type, only hash the type and the name.
    // Otherwise the hash will be stronger than
    // MDNodeSubsetEqualImpl::isODRMember().
    if (Tag == dwarf::DW_TAG_member && Name)
      if (auto *CT = dyn_cast_or_null<DICompositeType>(Scope))
        if (CT->getRawIdentifier())
          return hash_combine(Name, Scope);

    // Intentionally computes the hash on a subset of the operands for
    // performance reason. The subset has to be significant enough to avoid
    // collision "most of the time". There is no correctness issue in case of
    // collision because of the full check above.
    return hash_combine(Tag, Name, File, Line, Scope, BaseType, Flags);
  }
};

template <> struct MDNodeSubsetEqualImpl<DIDerivedType> {
  using KeyTy = MDNodeKeyImpl<DIDerivedType>;

  static bool isSubsetEqual(const KeyTy &LHS, const DIDerivedType *RHS) {
    return isODRMember(LHS.Tag, LHS.Scope, LHS.Name, RHS);
  }

  static bool isSubsetEqual(const DIDerivedType *LHS,
                            const DIDerivedType *RHS) {
    return isODRMember(LHS->getTag(), LHS->getRawScope(), LHS->getRawName(),
                       RHS);
  }

  /// Subprograms compare equal if they declare the same function in an ODR
  /// type.
  static bool isODRMember(unsigned Tag, const Metadata *Scope,
                          const MDString *Name, const DIDerivedType *RHS) {
    // Check whether the LHS is eligible.
    if (Tag != dwarf::DW_TAG_member || !Name)
      return false;

    auto *CT = dyn_cast_or_null<DICompositeType>(Scope);
    if (!CT || !CT->getRawIdentifier())
      return false;

    // Compare to the RHS.
    return Tag == RHS->getTag() && Name == RHS->getRawName() &&
           Scope == RHS->getRawScope();
  }
};

template <> struct MDNodeKeyImpl<DICompositeType> {
  unsigned Tag;
  MDString *Name;
  Metadata *File;
  unsigned Line;
  Metadata *Scope;
  Metadata *BaseType;
  uint64_t SizeInBits;
  uint64_t OffsetInBits;
  uint32_t AlignInBits;
  unsigned Flags;
  Metadata *Elements;
  unsigned RuntimeLang;
  Metadata *VTableHolder;
  Metadata *TemplateParams;
  MDString *Identifier;
  Metadata *Discriminator;
  Metadata *DataLocation;
  Metadata *Associated;
  Metadata *Allocated;
  Metadata *Rank;
  Metadata *Annotations;

  MDNodeKeyImpl(unsigned Tag, MDString *Name, Metadata *File, unsigned Line,
                Metadata *Scope, Metadata *BaseType, uint64_t SizeInBits,
                uint32_t AlignInBits, uint64_t OffsetInBits, unsigned Flags,
                Metadata *Elements, unsigned RuntimeLang,
                Metadata *VTableHolder, Metadata *TemplateParams,
                MDString *Identifier, Metadata *Discriminator,
                Metadata *DataLocation, Metadata *Associated,
                Metadata *Allocated, Metadata *Rank, Metadata *Annotations)
      : Tag(Tag), Name(Name), File(File), Line(Line), Scope(Scope),
        BaseType(BaseType), SizeInBits(SizeInBits), OffsetInBits(OffsetInBits),
        AlignInBits(AlignInBits), Flags(Flags), Elements(Elements),
        RuntimeLang(RuntimeLang), VTableHolder(VTableHolder),
        TemplateParams(TemplateParams), Identifier(Identifier),
        Discriminator(Discriminator), DataLocation(DataLocation),
        Associated(Associated), Allocated(Allocated), Rank(Rank),
        Annotations(Annotations) {}
  MDNodeKeyImpl(const DICompositeType *N)
      : Tag(N->getTag()), Name(N->getRawName()), File(N->getRawFile()),
        Line(N->getLine()), Scope(N->getRawScope()),
        BaseType(N->getRawBaseType()), SizeInBits(N->getSizeInBits()),
        OffsetInBits(N->getOffsetInBits()), AlignInBits(N->getAlignInBits()),
        Flags(N->getFlags()), Elements(N->getRawElements()),
        RuntimeLang(N->getRuntimeLang()), VTableHolder(N->getRawVTableHolder()),
        TemplateParams(N->getRawTemplateParams()),
        Identifier(N->getRawIdentifier()),
        Discriminator(N->getRawDiscriminator()),
        DataLocation(N->getRawDataLocation()),
        Associated(N->getRawAssociated()), Allocated(N->getRawAllocated()),
        Rank(N->getRawRank()), Annotations(N->getRawAnnotations()) {}

  bool isKeyOf(const DICompositeType *RHS) const {
    return Tag == RHS->getTag() && Name == RHS->getRawName() &&
           File == RHS->getRawFile() && Line == RHS->getLine() &&
           Scope == RHS->getRawScope() && BaseType == RHS->getRawBaseType() &&
           SizeInBits == RHS->getSizeInBits() &&
           AlignInBits == RHS->getAlignInBits() &&
           OffsetInBits == RHS->getOffsetInBits() && Flags == RHS->getFlags() &&
           Elements == RHS->getRawElements() &&
           RuntimeLang == RHS->getRuntimeLang() &&
           VTableHolder == RHS->getRawVTableHolder() &&
           TemplateParams == RHS->getRawTemplateParams() &&
           Identifier == RHS->getRawIdentifier() &&
           Discriminator == RHS->getRawDiscriminator() &&
           DataLocation == RHS->getRawDataLocation() &&
           Associated == RHS->getRawAssociated() &&
           Allocated == RHS->getRawAllocated() && Rank == RHS->getRawRank() &&
           Annotations == RHS->getRawAnnotations();
  }

  unsigned getHashValue() const {
    // Intentionally computes the hash on a subset of the operands for
    // performance reason. The subset has to be significant enough to avoid
    // collision "most of the time". There is no correctness issue in case of
    // collision because of the full check above.
    return hash_combine(Name, File, Line, BaseType, Scope, Elements,
                        TemplateParams, Annotations);
  }
};

template <> struct MDNodeKeyImpl<DISubroutineType> {
  unsigned Flags;
  uint8_t CC;
  Metadata *TypeArray;

  MDNodeKeyImpl(unsigned Flags, uint8_t CC, Metadata *TypeArray)
      : Flags(Flags), CC(CC), TypeArray(TypeArray) {}
  MDNodeKeyImpl(const DISubroutineType *N)
      : Flags(N->getFlags()), CC(N->getCC()), TypeArray(N->getRawTypeArray()) {}

  bool isKeyOf(const DISubroutineType *RHS) const {
    return Flags == RHS->getFlags() && CC == RHS->getCC() &&
           TypeArray == RHS->getRawTypeArray();
  }

  unsigned getHashValue() const { return hash_combine(Flags, CC, TypeArray); }
};

template <> struct MDNodeKeyImpl<DIFile> {
  MDString *Filename;
  MDString *Directory;
  std::optional<DIFile::ChecksumInfo<MDString *>> Checksum;
  MDString *Source;

  MDNodeKeyImpl(MDString *Filename, MDString *Directory,
                std::optional<DIFile::ChecksumInfo<MDString *>> Checksum,
                MDString *Source)
      : Filename(Filename), Directory(Directory), Checksum(Checksum),
        Source(Source) {}
  MDNodeKeyImpl(const DIFile *N)
      : Filename(N->getRawFilename()), Directory(N->getRawDirectory()),
        Checksum(N->getRawChecksum()), Source(N->getRawSource()) {}

  bool isKeyOf(const DIFile *RHS) const {
    return Filename == RHS->getRawFilename() &&
           Directory == RHS->getRawDirectory() &&
           Checksum == RHS->getRawChecksum() && Source == RHS->getRawSource();
  }

  unsigned getHashValue() const {
    return hash_combine(Filename, Directory, Checksum ? Checksum->Kind : 0,
                        Checksum ? Checksum->Value : nullptr, Source);
  }
};

template <> struct MDNodeKeyImpl<DISubprogram> {
  Metadata *Scope;
  MDString *Name;
  MDString *LinkageName;
  Metadata *File;
  unsigned Line;
  Metadata *Type;
  unsigned ScopeLine;
  Metadata *ContainingType;
  unsigned VirtualIndex;
  int ThisAdjustment;
  unsigned Flags;
  unsigned SPFlags;
  Metadata *Unit;
  Metadata *TemplateParams;
  Metadata *Declaration;
  Metadata *RetainedNodes;
  Metadata *ThrownTypes;
  Metadata *Annotations;
  MDString *TargetFuncName;

  MDNodeKeyImpl(Metadata *Scope, MDString *Name, MDString *LinkageName,
                Metadata *File, unsigned Line, Metadata *Type,
                unsigned ScopeLine, Metadata *ContainingType,
                unsigned VirtualIndex, int ThisAdjustment, unsigned Flags,
                unsigned SPFlags, Metadata *Unit, Metadata *TemplateParams,
                Metadata *Declaration, Metadata *RetainedNodes,
                Metadata *ThrownTypes, Metadata *Annotations,
                MDString *TargetFuncName)
      : Scope(Scope), Name(Name), LinkageName(LinkageName), File(File),
        Line(Line), Type(Type), ScopeLine(ScopeLine),
        ContainingType(ContainingType), VirtualIndex(VirtualIndex),
        ThisAdjustment(ThisAdjustment), Flags(Flags), SPFlags(SPFlags),
        Unit(Unit), TemplateParams(TemplateParams), Declaration(Declaration),
        RetainedNodes(RetainedNodes), ThrownTypes(ThrownTypes),
        Annotations(Annotations), TargetFuncName(TargetFuncName) {}
  MDNodeKeyImpl(const DISubprogram *N)
      : Scope(N->getRawScope()), Name(N->getRawName()),
        LinkageName(N->getRawLinkageName()), File(N->getRawFile()),
        Line(N->getLine()), Type(N->getRawType()), ScopeLine(N->getScopeLine()),
        ContainingType(N->getRawContainingType()),
        VirtualIndex(N->getVirtualIndex()),
        ThisAdjustment(N->getThisAdjustment()), Flags(N->getFlags()),
        SPFlags(N->getSPFlags()), Unit(N->getRawUnit()),
        TemplateParams(N->getRawTemplateParams()),
        Declaration(N->getRawDeclaration()),
        RetainedNodes(N->getRawRetainedNodes()),
        ThrownTypes(N->getRawThrownTypes()),
        Annotations(N->getRawAnnotations()),
        TargetFuncName(N->getRawTargetFuncName()) {}

  bool isKeyOf(const DISubprogram *RHS) const {
    return Scope == RHS->getRawScope() && Name == RHS->getRawName() &&
           LinkageName == RHS->getRawLinkageName() &&
           File == RHS->getRawFile() && Line == RHS->getLine() &&
           Type == RHS->getRawType() && ScopeLine == RHS->getScopeLine() &&
           ContainingType == RHS->getRawContainingType() &&
           VirtualIndex == RHS->getVirtualIndex() &&
           ThisAdjustment == RHS->getThisAdjustment() &&
           Flags == RHS->getFlags() && SPFlags == RHS->getSPFlags() &&
           Unit == RHS->getUnit() &&
           TemplateParams == RHS->getRawTemplateParams() &&
           Declaration == RHS->getRawDeclaration() &&
           RetainedNodes == RHS->getRawRetainedNodes() &&
           ThrownTypes == RHS->getRawThrownTypes() &&
           Annotations == RHS->getRawAnnotations() &&
           TargetFuncName == RHS->getRawTargetFuncName();
  }

  bool isDefinition() const { return SPFlags & DISubprogram::SPFlagDefinition; }

  unsigned getHashValue() const {
    // Use the Scope's linkage name instead of using the scope directly, as the
    // scope may be a temporary one which can replaced, which would produce a
    // different hash for the same DISubprogram.
    llvm::StringRef ScopeLinkageName;
    if (auto *CT = dyn_cast_or_null<DICompositeType>(Scope))
      if (auto *ID = CT->getRawIdentifier())
        ScopeLinkageName = ID->getString();

    // If this is a declaration inside an ODR type, only hash the type and the
    // name.  Otherwise the hash will be stronger than
    // MDNodeSubsetEqualImpl::isDeclarationOfODRMember().
    if (!isDefinition() && LinkageName &&
        isa_and_nonnull<DICompositeType>(Scope))
      return hash_combine(LinkageName, ScopeLinkageName);

    // Intentionally computes the hash on a subset of the operands for
    // performance reason. The subset has to be significant enough to avoid
    // collision "most of the time". There is no correctness issue in case of
    // collision because of the full check above.
    return hash_combine(Name, ScopeLinkageName, File, Type, Line);
  }
};

template <> struct MDNodeSubsetEqualImpl<DISubprogram> {
  using KeyTy = MDNodeKeyImpl<DISubprogram>;

  static bool isSubsetEqual(const KeyTy &LHS, const DISubprogram *RHS) {
    return isDeclarationOfODRMember(LHS.isDefinition(), LHS.Scope,
                                    LHS.LinkageName, LHS.TemplateParams, RHS);
  }

  static bool isSubsetEqual(const DISubprogram *LHS, const DISubprogram *RHS) {
    return isDeclarationOfODRMember(LHS->isDefinition(), LHS->getRawScope(),
                                    LHS->getRawLinkageName(),
                                    LHS->getRawTemplateParams(), RHS);
  }

  /// Subprograms compare equal if they declare the same function in an ODR
  /// type.
  static bool isDeclarationOfODRMember(bool IsDefinition, const Metadata *Scope,
                                       const MDString *LinkageName,
                                       const Metadata *TemplateParams,
                                       const DISubprogram *RHS) {
    // Check whether the LHS is eligible.
    if (IsDefinition || !Scope || !LinkageName)
      return false;

    auto *CT = dyn_cast_or_null<DICompositeType>(Scope);
    if (!CT || !CT->getRawIdentifier())
      return false;

    // Compare to the RHS.
    // FIXME: We need to compare template parameters here to avoid incorrect
    // collisions in mapMetadata when RF_ReuseAndMutateDistinctMDs and a
    // ODR-DISubprogram has a non-ODR template parameter (i.e., a
    // DICompositeType that does not have an identifier). Eventually we should
    // decouple ODR logic from uniquing logic.
    return IsDefinition == RHS->isDefinition() && Scope == RHS->getRawScope() &&
           LinkageName == RHS->getRawLinkageName() &&
           TemplateParams == RHS->getRawTemplateParams();
  }
};

template <> struct MDNodeKeyImpl<DILexicalBlock> {
  Metadata *Scope;
  Metadata *File;
  unsigned Line;
  unsigned Column;

  MDNodeKeyImpl(Metadata *Scope, Metadata *File, unsigned Line, unsigned Column)
      : Scope(Scope), File(File), Line(Line), Column(Column) {}
  MDNodeKeyImpl(const DILexicalBlock *N)
      : Scope(N->getRawScope()), File(N->getRawFile()), Line(N->getLine()),
        Column(N->getColumn()) {}

  bool isKeyOf(const DILexicalBlock *RHS) const {
    return Scope == RHS->getRawScope() && File == RHS->getRawFile() &&
           Line == RHS->getLine() && Column == RHS->getColumn();
  }

  unsigned getHashValue() const {
    return hash_combine(Scope, File, Line, Column);
  }
};

template <> struct MDNodeKeyImpl<DILexicalBlockFile> {
  Metadata *Scope;
  Metadata *File;
  unsigned Discriminator;

  MDNodeKeyImpl(Metadata *Scope, Metadata *File, unsigned Discriminator)
      : Scope(Scope), File(File), Discriminator(Discriminator) {}
  MDNodeKeyImpl(const DILexicalBlockFile *N)
      : Scope(N->getRawScope()), File(N->getRawFile()),
        Discriminator(N->getDiscriminator()) {}

  bool isKeyOf(const DILexicalBlockFile *RHS) const {
    return Scope == RHS->getRawScope() && File == RHS->getRawFile() &&
           Discriminator == RHS->getDiscriminator();
  }

  unsigned getHashValue() const {
    return hash_combine(Scope, File, Discriminator);
  }
};

template <> struct MDNodeKeyImpl<DINamespace> {
  Metadata *Scope;
  MDString *Name;
  bool ExportSymbols;

  MDNodeKeyImpl(Metadata *Scope, MDString *Name, bool ExportSymbols)
      : Scope(Scope), Name(Name), ExportSymbols(ExportSymbols) {}
  MDNodeKeyImpl(const DINamespace *N)
      : Scope(N->getRawScope()), Name(N->getRawName()),
        ExportSymbols(N->getExportSymbols()) {}

  bool isKeyOf(const DINamespace *RHS) const {
    return Scope == RHS->getRawScope() && Name == RHS->getRawName() &&
           ExportSymbols == RHS->getExportSymbols();
  }

  unsigned getHashValue() const { return hash_combine(Scope, Name); }
};

template <> struct MDNodeKeyImpl<DICommonBlock> {
  Metadata *Scope;
  Metadata *Decl;
  MDString *Name;
  Metadata *File;
  unsigned LineNo;

  MDNodeKeyImpl(Metadata *Scope, Metadata *Decl, MDString *Name, Metadata *File,
                unsigned LineNo)
      : Scope(Scope), Decl(Decl), Name(Name), File(File), LineNo(LineNo) {}
  MDNodeKeyImpl(const DICommonBlock *N)
      : Scope(N->getRawScope()), Decl(N->getRawDecl()), Name(N->getRawName()),
        File(N->getRawFile()), LineNo(N->getLineNo()) {}

  bool isKeyOf(const DICommonBlock *RHS) const {
    return Scope == RHS->getRawScope() && Decl == RHS->getRawDecl() &&
           Name == RHS->getRawName() && File == RHS->getRawFile() &&
           LineNo == RHS->getLineNo();
  }

  unsigned getHashValue() const {
    return hash_combine(Scope, Decl, Name, File, LineNo);
  }
};

template <> struct MDNodeKeyImpl<DIModule> {
  Metadata *File;
  Metadata *Scope;
  MDString *Name;
  MDString *ConfigurationMacros;
  MDString *IncludePath;
  MDString *APINotesFile;
  unsigned LineNo;
  bool IsDecl;

  MDNodeKeyImpl(Metadata *File, Metadata *Scope, MDString *Name,
                MDString *ConfigurationMacros, MDString *IncludePath,
                MDString *APINotesFile, unsigned LineNo, bool IsDecl)
      : File(File), Scope(Scope), Name(Name),
        ConfigurationMacros(ConfigurationMacros), IncludePath(IncludePath),
        APINotesFile(APINotesFile), LineNo(LineNo), IsDecl(IsDecl) {}
  MDNodeKeyImpl(const DIModule *N)
      : File(N->getRawFile()), Scope(N->getRawScope()), Name(N->getRawName()),
        ConfigurationMacros(N->getRawConfigurationMacros()),
        IncludePath(N->getRawIncludePath()),
        APINotesFile(N->getRawAPINotesFile()), LineNo(N->getLineNo()),
        IsDecl(N->getIsDecl()) {}

  bool isKeyOf(const DIModule *RHS) const {
    return Scope == RHS->getRawScope() && Name == RHS->getRawName() &&
           ConfigurationMacros == RHS->getRawConfigurationMacros() &&
           IncludePath == RHS->getRawIncludePath() &&
           APINotesFile == RHS->getRawAPINotesFile() &&
           File == RHS->getRawFile() && LineNo == RHS->getLineNo() &&
           IsDecl == RHS->getIsDecl();
  }

  unsigned getHashValue() const {
    return hash_combine(Scope, Name, ConfigurationMacros, IncludePath);
  }
};

template <> struct MDNodeKeyImpl<DITemplateTypeParameter> {
  MDString *Name;
  Metadata *Type;
  bool IsDefault;

  MDNodeKeyImpl(MDString *Name, Metadata *Type, bool IsDefault)
      : Name(Name), Type(Type), IsDefault(IsDefault) {}
  MDNodeKeyImpl(const DITemplateTypeParameter *N)
      : Name(N->getRawName()), Type(N->getRawType()),
        IsDefault(N->isDefault()) {}

  bool isKeyOf(const DITemplateTypeParameter *RHS) const {
    return Name == RHS->getRawName() && Type == RHS->getRawType() &&
           IsDefault == RHS->isDefault();
  }

  unsigned getHashValue() const { return hash_combine(Name, Type, IsDefault); }
};

template <> struct MDNodeKeyImpl<DITemplateValueParameter> {
  unsigned Tag;
  MDString *Name;
  Metadata *Type;
  bool IsDefault;
  Metadata *Value;

  MDNodeKeyImpl(unsigned Tag, MDString *Name, Metadata *Type, bool IsDefault,
                Metadata *Value)
      : Tag(Tag), Name(Name), Type(Type), IsDefault(IsDefault), Value(Value) {}
  MDNodeKeyImpl(const DITemplateValueParameter *N)
      : Tag(N->getTag()), Name(N->getRawName()), Type(N->getRawType()),
        IsDefault(N->isDefault()), Value(N->getValue()) {}

  bool isKeyOf(const DITemplateValueParameter *RHS) const {
    return Tag == RHS->getTag() && Name == RHS->getRawName() &&
           Type == RHS->getRawType() && IsDefault == RHS->isDefault() &&
           Value == RHS->getValue();
  }

  unsigned getHashValue() const {
    return hash_combine(Tag, Name, Type, IsDefault, Value);
  }
};

template <> struct MDNodeKeyImpl<DIGlobalVariable> {
  Metadata *Scope;
  MDString *Name;
  MDString *LinkageName;
  Metadata *File;
  unsigned Line;
  Metadata *Type;
  bool IsLocalToUnit;
  bool IsDefinition;
  Metadata *StaticDataMemberDeclaration;
  Metadata *TemplateParams;
  uint32_t AlignInBits;
  Metadata *Annotations;

  MDNodeKeyImpl(Metadata *Scope, MDString *Name, MDString *LinkageName,
                Metadata *File, unsigned Line, Metadata *Type,
                bool IsLocalToUnit, bool IsDefinition,
                Metadata *StaticDataMemberDeclaration, Metadata *TemplateParams,
                uint32_t AlignInBits, Metadata *Annotations)
      : Scope(Scope), Name(Name), LinkageName(LinkageName), File(File),
        Line(Line), Type(Type), IsLocalToUnit(IsLocalToUnit),
        IsDefinition(IsDefinition),
        StaticDataMemberDeclaration(StaticDataMemberDeclaration),
        TemplateParams(TemplateParams), AlignInBits(AlignInBits),
        Annotations(Annotations) {}
  MDNodeKeyImpl(const DIGlobalVariable *N)
      : Scope(N->getRawScope()), Name(N->getRawName()),
        LinkageName(N->getRawLinkageName()), File(N->getRawFile()),
        Line(N->getLine()), Type(N->getRawType()),
        IsLocalToUnit(N->isLocalToUnit()), IsDefinition(N->isDefinition()),
        StaticDataMemberDeclaration(N->getRawStaticDataMemberDeclaration()),
        TemplateParams(N->getRawTemplateParams()),
        AlignInBits(N->getAlignInBits()), Annotations(N->getRawAnnotations()) {}

  bool isKeyOf(const DIGlobalVariable *RHS) const {
    return Scope == RHS->getRawScope() && Name == RHS->getRawName() &&
           LinkageName == RHS->getRawLinkageName() &&
           File == RHS->getRawFile() && Line == RHS->getLine() &&
           Type == RHS->getRawType() && IsLocalToUnit == RHS->isLocalToUnit() &&
           IsDefinition == RHS->isDefinition() &&
           StaticDataMemberDeclaration ==
               RHS->getRawStaticDataMemberDeclaration() &&
           TemplateParams == RHS->getRawTemplateParams() &&
           AlignInBits == RHS->getAlignInBits() &&
           Annotations == RHS->getRawAnnotations();
  }

  unsigned getHashValue() const {
    // We do not use AlignInBits in hashing function here on purpose:
    // in most cases this param for local variable is zero (for function param
    // it is always zero). This leads to lots of hash collisions and errors on
    // cases with lots of similar variables.
    // clang/test/CodeGen/debug-info-257-args.c is an example of this problem,
    // generated IR is random for each run and test fails with Align included.
    // TODO: make hashing work fine with such situations
    return hash_combine(Scope, Name, LinkageName, File, Line, Type,
                        IsLocalToUnit, IsDefinition, /* AlignInBits, */
                        StaticDataMemberDeclaration, Annotations);
  }
};

template <> struct MDNodeKeyImpl<DILocalVariable> {
  Metadata *Scope;
  MDString *Name;
  Metadata *File;
  unsigned Line;
  Metadata *Type;
  unsigned Arg;
  unsigned Flags;
  uint32_t AlignInBits;
  Metadata *Annotations;

  MDNodeKeyImpl(Metadata *Scope, MDString *Name, Metadata *File, unsigned Line,
                Metadata *Type, unsigned Arg, unsigned Flags,
                uint32_t AlignInBits, Metadata *Annotations)
      : Scope(Scope), Name(Name), File(File), Line(Line), Type(Type), Arg(Arg),
        Flags(Flags), AlignInBits(AlignInBits), Annotations(Annotations) {}
  MDNodeKeyImpl(const DILocalVariable *N)
      : Scope(N->getRawScope()), Name(N->getRawName()), File(N->getRawFile()),
        Line(N->getLine()), Type(N->getRawType()), Arg(N->getArg()),
        Flags(N->getFlags()), AlignInBits(N->getAlignInBits()),
        Annotations(N->getRawAnnotations()) {}

  bool isKeyOf(const DILocalVariable *RHS) const {
    return Scope == RHS->getRawScope() && Name == RHS->getRawName() &&
           File == RHS->getRawFile() && Line == RHS->getLine() &&
           Type == RHS->getRawType() && Arg == RHS->getArg() &&
           Flags == RHS->getFlags() && AlignInBits == RHS->getAlignInBits() &&
           Annotations == RHS->getRawAnnotations();
  }

  unsigned getHashValue() const {
    // We do not use AlignInBits in hashing function here on purpose:
    // in most cases this param for local variable is zero (for function param
    // it is always zero). This leads to lots of hash collisions and errors on
    // cases with lots of similar variables.
    // clang/test/CodeGen/debug-info-257-args.c is an example of this problem,
    // generated IR is random for each run and test fails with Align included.
    // TODO: make hashing work fine with such situations
    return hash_combine(Scope, Name, File, Line, Type, Arg, Flags, Annotations);
  }
};

template <> struct MDNodeKeyImpl<DILabel> {
  Metadata *Scope;
  MDString *Name;
  Metadata *File;
  unsigned Line;

  MDNodeKeyImpl(Metadata *Scope, MDString *Name, Metadata *File, unsigned Line)
      : Scope(Scope), Name(Name), File(File), Line(Line) {}
  MDNodeKeyImpl(const DILabel *N)
      : Scope(N->getRawScope()), Name(N->getRawName()), File(N->getRawFile()),
        Line(N->getLine()) {}

  bool isKeyOf(const DILabel *RHS) const {
    return Scope == RHS->getRawScope() && Name == RHS->getRawName() &&
           File == RHS->getRawFile() && Line == RHS->getLine();
  }

  /// Using name and line to get hash value. It should already be mostly unique.
  unsigned getHashValue() const { return hash_combine(Scope, Name, Line); }
};

template <> struct MDNodeKeyImpl<DIExpression> {
  ArrayRef<uint64_t> Elements;

  MDNodeKeyImpl(ArrayRef<uint64_t> Elements) : Elements(Elements) {}
  MDNodeKeyImpl(const DIExpression *N) : Elements(N->getElements()) {}

  bool isKeyOf(const DIExpression *RHS) const {
    return Elements == RHS->getElements();
  }

  unsigned getHashValue() const {
    return hash_combine_range(Elements.begin(), Elements.end());
  }
};

template <> struct MDNodeKeyImpl<DIGlobalVariableExpression> {
  Metadata *Variable;
  Metadata *Expression;

  MDNodeKeyImpl(Metadata *Variable, Metadata *Expression)
      : Variable(Variable), Expression(Expression) {}
  MDNodeKeyImpl(const DIGlobalVariableExpression *N)
      : Variable(N->getRawVariable()), Expression(N->getRawExpression()) {}

  bool isKeyOf(const DIGlobalVariableExpression *RHS) const {
    return Variable == RHS->getRawVariable() &&
           Expression == RHS->getRawExpression();
  }

  unsigned getHashValue() const { return hash_combine(Variable, Expression); }
};

template <> struct MDNodeKeyImpl<DIObjCProperty> {
  MDString *Name;
  Metadata *File;
  unsigned Line;
  MDString *GetterName;
  MDString *SetterName;
  unsigned Attributes;
  Metadata *Type;

  MDNodeKeyImpl(MDString *Name, Metadata *File, unsigned Line,
                MDString *GetterName, MDString *SetterName, unsigned Attributes,
                Metadata *Type)
      : Name(Name), File(File), Line(Line), GetterName(GetterName),
        SetterName(SetterName), Attributes(Attributes), Type(Type) {}
  MDNodeKeyImpl(const DIObjCProperty *N)
      : Name(N->getRawName()), File(N->getRawFile()), Line(N->getLine()),
        GetterName(N->getRawGetterName()), SetterName(N->getRawSetterName()),
        Attributes(N->getAttributes()), Type(N->getRawType()) {}

  bool isKeyOf(const DIObjCProperty *RHS) const {
    return Name == RHS->getRawName() && File == RHS->getRawFile() &&
           Line == RHS->getLine() && GetterName == RHS->getRawGetterName() &&
           SetterName == RHS->getRawSetterName() &&
           Attributes == RHS->getAttributes() && Type == RHS->getRawType();
  }

  unsigned getHashValue() const {
    return hash_combine(Name, File, Line, GetterName, SetterName, Attributes,
                        Type);
  }
};

template <> struct MDNodeKeyImpl<DIImportedEntity> {
  unsigned Tag;
  Metadata *Scope;
  Metadata *Entity;
  Metadata *File;
  unsigned Line;
  MDString *Name;
  Metadata *Elements;

  MDNodeKeyImpl(unsigned Tag, Metadata *Scope, Metadata *Entity, Metadata *File,
                unsigned Line, MDString *Name, Metadata *Elements)
      : Tag(Tag), Scope(Scope), Entity(Entity), File(File), Line(Line),
        Name(Name), Elements(Elements) {}
  MDNodeKeyImpl(const DIImportedEntity *N)
      : Tag(N->getTag()), Scope(N->getRawScope()), Entity(N->getRawEntity()),
        File(N->getRawFile()), Line(N->getLine()), Name(N->getRawName()),
        Elements(N->getRawElements()) {}

  bool isKeyOf(const DIImportedEntity *RHS) const {
    return Tag == RHS->getTag() && Scope == RHS->getRawScope() &&
           Entity == RHS->getRawEntity() && File == RHS->getFile() &&
           Line == RHS->getLine() && Name == RHS->getRawName() &&
           Elements == RHS->getRawElements();
  }

  unsigned getHashValue() const {
    return hash_combine(Tag, Scope, Entity, File, Line, Name, Elements);
  }
};

template <> struct MDNodeKeyImpl<DIMacro> {
  unsigned MIType;
  unsigned Line;
  MDString *Name;
  MDString *Value;

  MDNodeKeyImpl(unsigned MIType, unsigned Line, MDString *Name, MDString *Value)
      : MIType(MIType), Line(Line), Name(Name), Value(Value) {}
  MDNodeKeyImpl(const DIMacro *N)
      : MIType(N->getMacinfoType()), Line(N->getLine()), Name(N->getRawName()),
        Value(N->getRawValue()) {}

  bool isKeyOf(const DIMacro *RHS) const {
    return MIType == RHS->getMacinfoType() && Line == RHS->getLine() &&
           Name == RHS->getRawName() && Value == RHS->getRawValue();
  }

  unsigned getHashValue() const {
    return hash_combine(MIType, Line, Name, Value);
  }
};

template <> struct MDNodeKeyImpl<DIMacroFile> {
  unsigned MIType;
  unsigned Line;
  Metadata *File;
  Metadata *Elements;

  MDNodeKeyImpl(unsigned MIType, unsigned Line, Metadata *File,
                Metadata *Elements)
      : MIType(MIType), Line(Line), File(File), Elements(Elements) {}
  MDNodeKeyImpl(const DIMacroFile *N)
      : MIType(N->getMacinfoType()), Line(N->getLine()), File(N->getRawFile()),
        Elements(N->getRawElements()) {}

  bool isKeyOf(const DIMacroFile *RHS) const {
    return MIType == RHS->getMacinfoType() && Line == RHS->getLine() &&
           File == RHS->getRawFile() && Elements == RHS->getRawElements();
  }

  unsigned getHashValue() const {
    return hash_combine(MIType, Line, File, Elements);
  }
};

// DIArgLists are not MDNodes, but we still want to unique them in a DenseSet
// based on a hash of their arguments.
struct DIArgListKeyInfo {
  ArrayRef<ValueAsMetadata *> Args;

  DIArgListKeyInfo(ArrayRef<ValueAsMetadata *> Args) : Args(Args) {}
  DIArgListKeyInfo(const DIArgList *N) : Args(N->getArgs()) {}

  bool isKeyOf(const DIArgList *RHS) const { return Args == RHS->getArgs(); }

  unsigned getHashValue() const {
    return hash_combine_range(Args.begin(), Args.end());
  }
};

/// DenseMapInfo for DIArgList.
struct DIArgListInfo {
  using KeyTy = DIArgListKeyInfo;

  static inline DIArgList *getEmptyKey() {
    return DenseMapInfo<DIArgList *>::getEmptyKey();
  }

  static inline DIArgList *getTombstoneKey() {
    return DenseMapInfo<DIArgList *>::getTombstoneKey();
  }

  static unsigned getHashValue(const KeyTy &Key) { return Key.getHashValue(); }

  static unsigned getHashValue(const DIArgList *N) {
    return KeyTy(N).getHashValue();
  }

  static bool isEqual(const KeyTy &LHS, const DIArgList *RHS) {
    if (RHS == getEmptyKey() || RHS == getTombstoneKey())
      return false;
    return LHS.isKeyOf(RHS);
  }

  static bool isEqual(const DIArgList *LHS, const DIArgList *RHS) {
    return LHS == RHS;
  }
};

/// DenseMapInfo for MDNode subclasses.
template <class NodeTy> struct MDNodeInfo {
  using KeyTy = MDNodeKeyImpl<NodeTy>;
  using SubsetEqualTy = MDNodeSubsetEqualImpl<NodeTy>;

  static inline NodeTy *getEmptyKey() {
    return DenseMapInfo<NodeTy *>::getEmptyKey();
  }

  static inline NodeTy *getTombstoneKey() {
    return DenseMapInfo<NodeTy *>::getTombstoneKey();
  }

  static unsigned getHashValue(const KeyTy &Key) { return Key.getHashValue(); }

  static unsigned getHashValue(const NodeTy *N) {
    return KeyTy(N).getHashValue();
  }

  static bool isEqual(const KeyTy &LHS, const NodeTy *RHS) {
    if (RHS == getEmptyKey() || RHS == getTombstoneKey())
      return false;
    return SubsetEqualTy::isSubsetEqual(LHS, RHS) || LHS.isKeyOf(RHS);
  }

  static bool isEqual(const NodeTy *LHS, const NodeTy *RHS) {
    if (LHS == RHS)
      return true;
    if (RHS == getEmptyKey() || RHS == getTombstoneKey())
      return false;
    return SubsetEqualTy::isSubsetEqual(LHS, RHS);
  }
};

#define HANDLE_MDNODE_LEAF(CLASS) using CLASS##Info = MDNodeInfo<CLASS>;
#include "llvm/IR/Metadata.def"

/// Multimap-like storage for metadata attachments.
class MDAttachments {
public:
  struct Attachment {
    unsigned MDKind;
    TrackingMDNodeRef Node;
  };

private:
  SmallVector<Attachment, 1> Attachments;

public:
  bool empty() const { return Attachments.empty(); }
  size_t size() const { return Attachments.size(); }

  /// Returns the first attachment with the given ID or nullptr if no such
  /// attachment exists.
  MDNode *lookup(unsigned ID) const;

  /// Appends all attachments with the given ID to \c Result in insertion order.
  /// If the global has no attachments with the given ID, or if ID is invalid,
  /// leaves Result unchanged.
  void get(unsigned ID, SmallVectorImpl<MDNode *> &Result) const;

  /// Appends all attachments for the global to \c Result, sorting by attachment
  /// ID. Attachments with the same ID appear in insertion order. This function
  /// does \em not clear \c Result.
  void getAll(SmallVectorImpl<std::pair<unsigned, MDNode *>> &Result) const;

  /// Set an attachment to a particular node.
  ///
  /// Set the \c ID attachment to \c MD, replacing the current attachments at \c
  /// ID (if anyway).
  void set(unsigned ID, MDNode *MD);

  /// Adds an attachment to a particular node.
  void insert(unsigned ID, MDNode &MD);

  /// Remove attachments with the given ID.
  ///
  /// Remove the attachments at \c ID, if any.
  bool erase(unsigned ID);

  /// Erase matching attachments.
  ///
  /// Erases all attachments matching the \c shouldRemove predicate.
  template <class PredTy> void remove_if(PredTy shouldRemove) {
    llvm::erase_if(Attachments, shouldRemove);
  }
};

class LLVMContextImpl {
public:
  /// OwnedModules - The set of modules instantiated in this context, and which
  /// will be automatically deleted if this context is deleted.
  SmallPtrSet<Module *, 4> OwnedModules;

  /// MachineFunctionNums - Keep the next available unique number available for
  /// a MachineFunction in given module. Module must in OwnedModules.
  DenseMap<Module *, unsigned> MachineFunctionNums;

  /// The main remark streamer used by all the other streamers (e.g. IR, MIR,
  /// frontends, etc.). This should only be used by the specific streamers, and
  /// never directly.
  std::unique_ptr<remarks::RemarkStreamer> MainRemarkStreamer;

  std::unique_ptr<DiagnosticHandler> DiagHandler;
  bool RespectDiagnosticFilters = false;
  bool DiagnosticsHotnessRequested = false;
  /// The minimum hotness value a diagnostic needs in order to be included in
  /// optimization diagnostics.
  ///
  /// The threshold is an Optional value, which maps to one of the 3 states:
  /// 1). 0            => threshold disabled. All emarks will be printed.
  /// 2). positive int => manual threshold by user. Remarks with hotness exceed
  ///                     threshold will be printed.
  /// 3). None         => 'auto' threshold by user. The actual value is not
  ///                     available at command line, but will be synced with
  ///                     hotness threhold from profile summary during
  ///                     compilation.
  ///
  /// State 1 and 2 are considered as terminal states. State transition is
  /// only allowed from 3 to 2, when the threshold is first synced with profile
  /// summary. This ensures that the threshold is set only once and stays
  /// constant.
  ///
  /// If threshold option is not specified, it is disabled (0) by default.
  std::optional<uint64_t> DiagnosticsHotnessThreshold = 0;

  /// The percentage of difference between profiling branch weights and
  /// llvm.expect branch weights to tolerate when emiting MisExpect diagnostics
  std::optional<uint32_t> DiagnosticsMisExpectTolerance = 0;
  bool MisExpectWarningRequested = false;

  /// The specialized remark streamer used by LLVM's OptimizationRemarkEmitter.
  std::unique_ptr<LLVMRemarkStreamer> LLVMRS;

  LLVMContext::YieldCallbackTy YieldCallback = nullptr;
  void *YieldOpaqueHandle = nullptr;

  DenseMap<const Value *, ValueName *> ValueNames;

  DenseMap<unsigned, std::unique_ptr<ConstantInt>> IntZeroConstants;
  DenseMap<unsigned, std::unique_ptr<ConstantInt>> IntOneConstants;
  DenseMap<APInt, std::unique_ptr<ConstantInt>> IntConstants;
  DenseMap<std::pair<ElementCount, APInt>, std::unique_ptr<ConstantInt>>
      IntSplatConstants;

  DenseMap<APFloat, std::unique_ptr<ConstantFP>> FPConstants;
  DenseMap<std::pair<ElementCount, APFloat>, std::unique_ptr<ConstantFP>>
      FPSplatConstants;

  FoldingSet<AttributeImpl> AttrsSet;
  FoldingSet<AttributeListImpl> AttrsLists;
  FoldingSet<AttributeSetNode> AttrsSetNodes;

  StringMap<MDString, BumpPtrAllocator> MDStringCache;
  DenseMap<Value *, ValueAsMetadata *> ValuesAsMetadata;
  DenseMap<Metadata *, MetadataAsValue *> MetadataAsValues;
  DenseSet<DIArgList *, DIArgListInfo> DIArgLists;

#define HANDLE_MDNODE_LEAF_UNIQUABLE(CLASS)                                    \
  DenseSet<CLASS *, CLASS##Info> CLASS##s;
#include "llvm/IR/Metadata.def"

  // Optional map for looking up composite types by identifier.
  std::optional<DenseMap<const MDString *, DICompositeType *>> DITypeMap;

  // MDNodes may be uniqued or not uniqued.  When they're not uniqued, they
  // aren't in the MDNodeSet, but they're still shared between objects, so no
  // one object can destroy them.  Keep track of them here so we can delete
  // them on context teardown.
  std::vector<MDNode *> DistinctMDNodes;

  // ConstantRangeListAttributeImpl is a TrailingObjects/ArrayRef of
  // ConstantRange. Since this is a dynamically sized class, it's not
  // possible to use SpecificBumpPtrAllocator. Instead, we use normal Alloc
  // for allocation and record all allocated pointers in this vector. In the
  // LLVMContext destructor, call the destuctors of everything in the vector.
  std::vector<ConstantRangeListAttributeImpl *> ConstantRangeListAttributes;

  DenseMap<Type *, std::unique_ptr<ConstantAggregateZero>> CAZConstants;

  using ArrayConstantsTy = ConstantUniqueMap<ConstantArray>;
  ArrayConstantsTy ArrayConstants;

  using StructConstantsTy = ConstantUniqueMap<ConstantStruct>;
  StructConstantsTy StructConstants;

  using VectorConstantsTy = ConstantUniqueMap<ConstantVector>;
  VectorConstantsTy VectorConstants;

  DenseMap<PointerType *, std::unique_ptr<ConstantPointerNull>> CPNConstants;

  DenseMap<TargetExtType *, std::unique_ptr<ConstantTargetNone>> CTNConstants;

  DenseMap<Type *, std::unique_ptr<UndefValue>> UVConstants;

  DenseMap<Type *, std::unique_ptr<PoisonValue>> PVConstants;

  StringMap<std::unique_ptr<ConstantDataSequential>> CDSConstants;

  DenseMap<std::pair<const Function *, const BasicBlock *>, BlockAddress *>
      BlockAddresses;

  DenseMap<const GlobalValue *, DSOLocalEquivalent *> DSOLocalEquivalents;

  DenseMap<const GlobalValue *, NoCFIValue *> NoCFIValues;

  ConstantUniqueMap<ConstantPtrAuth> ConstantPtrAuths;

  ConstantUniqueMap<ConstantExpr> ExprConstants;

  ConstantUniqueMap<InlineAsm> InlineAsms;

  ConstantInt *TheTrueVal = nullptr;
  ConstantInt *TheFalseVal = nullptr;

  // Basic type instances.
  Type VoidTy, LabelTy, HalfTy, BFloatTy, FloatTy, DoubleTy, MetadataTy,
      TokenTy;
  Type X86_FP80Ty, FP128Ty, PPC_FP128Ty, X86_MMXTy, X86_AMXTy;
  IntegerType Int1Ty, Int8Ty, Int16Ty, Int32Ty, Int64Ty, Int128Ty;

  std::unique_ptr<ConstantTokenNone> TheNoneToken;

  BumpPtrAllocator Alloc;
  UniqueStringSaver Saver{Alloc};
  SpecificBumpPtrAllocator<ConstantRangeAttributeImpl>
      ConstantRangeAttributeAlloc;

  DenseMap<unsigned, IntegerType *> IntegerTypes;

  using FunctionTypeSet = DenseSet<FunctionType *, FunctionTypeKeyInfo>;
  FunctionTypeSet FunctionTypes;
  using StructTypeSet = DenseSet<StructType *, AnonStructTypeKeyInfo>;
  StructTypeSet AnonStructTypes;
  StringMap<StructType *> NamedStructTypes;
  unsigned NamedStructTypesUniqueID = 0;

  using TargetExtTypeSet = DenseSet<TargetExtType *, TargetExtTypeKeyInfo>;
  TargetExtTypeSet TargetExtTypes;

  DenseMap<std::pair<Type *, uint64_t>, ArrayType *> ArrayTypes;
  DenseMap<std::pair<Type *, ElementCount>, VectorType *> VectorTypes;
  PointerType *AS0PointerType = nullptr; // AddrSpace = 0
  DenseMap<unsigned, PointerType *> PointerTypes;
  DenseMap<std::pair<Type *, unsigned>, PointerType *> LegacyPointerTypes;
  DenseMap<std::pair<Type *, unsigned>, TypedPointerType *> ASTypedPointerTypes;

  /// ValueHandles - This map keeps track of all of the value handles that are
  /// watching a Value*.  The Value::HasValueHandle bit is used to know
  /// whether or not a value has an entry in this map.
  using ValueHandlesTy = DenseMap<Value *, ValueHandleBase *>;
  ValueHandlesTy ValueHandles;

  /// CustomMDKindNames - Map to hold the metadata string to ID mapping.
  StringMap<unsigned> CustomMDKindNames;

  /// Collection of metadata used in this context.
  DenseMap<const Value *, MDAttachments> ValueMetadata;

  /// Map DIAssignID -> Instructions with that attachment.
  /// Managed by Instruction via Instruction::updateDIAssignIDMapping.
  /// Query using the at:: functions defined in DebugInfo.h.
  DenseMap<DIAssignID *, SmallVector<Instruction *, 1>> AssignmentIDToInstrs;

  /// Collection of per-GlobalObject sections used in this context.
  DenseMap<const GlobalObject *, StringRef> GlobalObjectSections;

  /// Collection of per-GlobalValue partitions used in this context.
  DenseMap<const GlobalValue *, StringRef> GlobalValuePartitions;

  DenseMap<const GlobalValue *, GlobalValue::SanitizerMetadata>
      GlobalValueSanitizerMetadata;

  /// DiscriminatorTable - This table maps file:line locations to an
  /// integer representing the next DWARF path discriminator to assign to
  /// instructions in different blocks at the same location.
  DenseMap<std::pair<const char *, unsigned>, unsigned> DiscriminatorTable;

  /// A set of interned tags for operand bundles.  The StringMap maps
  /// bundle tags to their IDs.
  ///
  /// \see LLVMContext::getOperandBundleTagID
  StringMap<uint32_t> BundleTagCache;

  StringMapEntry<uint32_t> *getOrInsertBundleTag(StringRef Tag);
  void getOperandBundleTags(SmallVectorImpl<StringRef> &Tags) const;
  uint32_t getOperandBundleTagID(StringRef Tag) const;

  /// A set of interned synchronization scopes.  The StringMap maps
  /// synchronization scope names to their respective synchronization scope IDs.
  StringMap<SyncScope::ID> SSC;

  /// getOrInsertSyncScopeID - Maps synchronization scope name to
  /// synchronization scope ID.  Every synchronization scope registered with
  /// LLVMContext has unique ID except pre-defined ones.
  SyncScope::ID getOrInsertSyncScopeID(StringRef SSN);

  /// getSyncScopeNames - Populates client supplied SmallVector with
  /// synchronization scope names registered with LLVMContext.  Synchronization
  /// scope names are ordered by increasing synchronization scope IDs.
  void getSyncScopeNames(SmallVectorImpl<StringRef> &SSNs) const;

  /// Maintain the GC name for each function.
  ///
  /// This saves allocating an additional word in Function for programs which
  /// do not use GC (i.e., most programs) at the cost of increased overhead for
  /// clients which do use GC.
  DenseMap<const Function *, std::string> GCNames;

  /// Flag to indicate if Value (other than GlobalValue) retains their name or
  /// not.
  bool DiscardValueNames = false;

  LLVMContextImpl(LLVMContext &C);
  ~LLVMContextImpl();

  /// Destroy the ConstantArrays if they are not used.
  void dropTriviallyDeadConstantArrays();

  mutable OptPassGate *OPG = nullptr;

  /// Access the object which can disable optional passes and individual
  /// optimizations at compile time.
  OptPassGate &getOptPassGate() const;

  /// Set the object which can disable optional passes and individual
  /// optimizations at compile time.
  ///
  /// The lifetime of the object must be guaranteed to extend as long as the
  /// LLVMContext is used by compilation.
  void setOptPassGate(OptPassGate &);

  /// Mapping of blocks to collections of "trailing" DbgVariableRecords. As part
  /// of the "RemoveDIs" project, debug-info variable location records are going
  /// to cease being instructions... which raises the problem of where should
  /// they be recorded when we remove the terminator of a blocks, such as:
  ///
  ///    %foo = add i32 0, 0
  ///    br label %bar
  ///
  /// If the branch is removed, a legitimate transient state while editing a
  /// block, any debug-records between those two instructions will not have a
  /// location. Each block thus records any DbgVariableRecord records that
  /// "trail" in such a way. These are stored in LLVMContext because typically
  /// LLVM only edits a small number of blocks at a time, so there's no need to
  /// bloat BasicBlock with such a data structure.
  SmallDenseMap<BasicBlock *, DbgMarker *> TrailingDbgRecords;

  // Set, get and delete operations for TrailingDbgRecords.
  void setTrailingDbgRecords(BasicBlock *B, DbgMarker *M) {
    assert(!TrailingDbgRecords.count(B));
    TrailingDbgRecords[B] = M;
  }

  DbgMarker *getTrailingDbgRecords(BasicBlock *B) {
    return TrailingDbgRecords.lookup(B);
  }

  void deleteTrailingDbgRecords(BasicBlock *B) { TrailingDbgRecords.erase(B); }

  std::string DefaultTargetCPU;
  std::string DefaultTargetFeatures;
};

} // end namespace llvm

#endif // LLVM_LIB_IR_LLVMCONTEXTIMPL_H
