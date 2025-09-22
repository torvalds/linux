//===- AttributeImpl.h - Attribute Internals --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines various helper methods and classes used by
/// LLVMContextImpl for creating and managing attributes.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_IR_ATTRIBUTEIMPL_H
#define LLVM_LIB_IR_ATTRIBUTEIMPL_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/ConstantRangeList.h"
#include "llvm/Support/TrailingObjects.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace llvm {

class LLVMContext;
class Type;

//===----------------------------------------------------------------------===//
/// \class
/// This class represents a single, uniqued attribute. That attribute
/// could be a single enum, a tuple, or a string.
class AttributeImpl : public FoldingSetNode {
  unsigned char KindID; ///< Holds the AttrEntryKind of the attribute

protected:
  enum AttrEntryKind {
    EnumAttrEntry,
    IntAttrEntry,
    StringAttrEntry,
    TypeAttrEntry,
    ConstantRangeAttrEntry,
    ConstantRangeListAttrEntry,
  };

  AttributeImpl(AttrEntryKind KindID) : KindID(KindID) {}

public:
  // AttributesImpl is uniqued, these should not be available.
  AttributeImpl(const AttributeImpl &) = delete;
  AttributeImpl &operator=(const AttributeImpl &) = delete;

  bool isEnumAttribute() const { return KindID == EnumAttrEntry; }
  bool isIntAttribute() const { return KindID == IntAttrEntry; }
  bool isStringAttribute() const { return KindID == StringAttrEntry; }
  bool isTypeAttribute() const { return KindID == TypeAttrEntry; }
  bool isConstantRangeAttribute() const {
    return KindID == ConstantRangeAttrEntry;
  }
  bool isConstantRangeListAttribute() const {
    return KindID == ConstantRangeListAttrEntry;
  }

  bool hasAttribute(Attribute::AttrKind A) const;
  bool hasAttribute(StringRef Kind) const;

  Attribute::AttrKind getKindAsEnum() const;
  uint64_t getValueAsInt() const;
  bool getValueAsBool() const;

  StringRef getKindAsString() const;
  StringRef getValueAsString() const;

  Type *getValueAsType() const;

  const ConstantRange &getValueAsConstantRange() const;

  ArrayRef<ConstantRange> getValueAsConstantRangeList() const;

  /// Used when sorting the attributes.
  bool operator<(const AttributeImpl &AI) const;

  void Profile(FoldingSetNodeID &ID) const {
    if (isEnumAttribute())
      Profile(ID, getKindAsEnum());
    else if (isIntAttribute())
      Profile(ID, getKindAsEnum(), getValueAsInt());
    else if (isStringAttribute())
      Profile(ID, getKindAsString(), getValueAsString());
    else if (isTypeAttribute())
      Profile(ID, getKindAsEnum(), getValueAsType());
    else if (isConstantRangeAttribute())
      Profile(ID, getKindAsEnum(), getValueAsConstantRange());
    else
      Profile(ID, getKindAsEnum(), getValueAsConstantRangeList());
  }

  static void Profile(FoldingSetNodeID &ID, Attribute::AttrKind Kind) {
    assert(Attribute::isEnumAttrKind(Kind) && "Expected enum attribute");
    ID.AddInteger(Kind);
  }

  static void Profile(FoldingSetNodeID &ID, Attribute::AttrKind Kind,
                      uint64_t Val) {
    assert(Attribute::isIntAttrKind(Kind) && "Expected int attribute");
    ID.AddInteger(Kind);
    ID.AddInteger(Val);
  }

  static void Profile(FoldingSetNodeID &ID, StringRef Kind, StringRef Values) {
    ID.AddString(Kind);
    if (!Values.empty()) ID.AddString(Values);
  }

  static void Profile(FoldingSetNodeID &ID, Attribute::AttrKind Kind,
                      Type *Ty) {
    ID.AddInteger(Kind);
    ID.AddPointer(Ty);
  }

  static void Profile(FoldingSetNodeID &ID, Attribute::AttrKind Kind,
                      const ConstantRange &CR) {
    ID.AddInteger(Kind);
    CR.getLower().Profile(ID);
    CR.getUpper().Profile(ID);
  }

  static void Profile(FoldingSetNodeID &ID, Attribute::AttrKind Kind,
                      ArrayRef<ConstantRange> Val) {
    ID.AddInteger(Kind);
    ID.AddInteger(Val.size());
    for (auto &CR : Val) {
      CR.getLower().Profile(ID);
      CR.getUpper().Profile(ID);
    }
  }
};

static_assert(std::is_trivially_destructible<AttributeImpl>::value,
              "AttributeImpl should be trivially destructible");

//===----------------------------------------------------------------------===//
/// \class
/// A set of classes that contain the value of the
/// attribute object. There are three main categories: enum attribute entries,
/// represented by Attribute::AttrKind; alignment attribute entries; and string
/// attribute enties, which are for target-dependent attributes.

class EnumAttributeImpl : public AttributeImpl {
  Attribute::AttrKind Kind;

protected:
  EnumAttributeImpl(AttrEntryKind ID, Attribute::AttrKind Kind)
      : AttributeImpl(ID), Kind(Kind) {}

public:
  EnumAttributeImpl(Attribute::AttrKind Kind)
      : AttributeImpl(EnumAttrEntry), Kind(Kind) {
    assert(Kind != Attribute::AttrKind::None &&
           "Can't create a None attribute!");
  }

  Attribute::AttrKind getEnumKind() const { return Kind; }
};

class IntAttributeImpl : public EnumAttributeImpl {
  uint64_t Val;

public:
  IntAttributeImpl(Attribute::AttrKind Kind, uint64_t Val)
      : EnumAttributeImpl(IntAttrEntry, Kind), Val(Val) {
    assert(Attribute::isIntAttrKind(Kind) &&
           "Wrong kind for int attribute!");
  }

  uint64_t getValue() const { return Val; }
};

class StringAttributeImpl final
    : public AttributeImpl,
      private TrailingObjects<StringAttributeImpl, char> {
  friend TrailingObjects;

  unsigned KindSize;
  unsigned ValSize;
  size_t numTrailingObjects(OverloadToken<char>) const {
    return KindSize + 1 + ValSize + 1;
  }

public:
  StringAttributeImpl(StringRef Kind, StringRef Val = StringRef())
      : AttributeImpl(StringAttrEntry), KindSize(Kind.size()),
        ValSize(Val.size()) {
    char *TrailingString = getTrailingObjects<char>();
    // Some users rely on zero-termination.
    llvm::copy(Kind, TrailingString);
    TrailingString[KindSize] = '\0';
    llvm::copy(Val, &TrailingString[KindSize + 1]);
    TrailingString[KindSize + 1 + ValSize] = '\0';
  }

  StringRef getStringKind() const {
    return StringRef(getTrailingObjects<char>(), KindSize);
  }
  StringRef getStringValue() const {
    return StringRef(getTrailingObjects<char>() + KindSize + 1, ValSize);
  }

  static size_t totalSizeToAlloc(StringRef Kind, StringRef Val) {
    return TrailingObjects::totalSizeToAlloc<char>(Kind.size() + 1 +
                                                   Val.size() + 1);
  }
};

class TypeAttributeImpl : public EnumAttributeImpl {
  Type *Ty;

public:
  TypeAttributeImpl(Attribute::AttrKind Kind, Type *Ty)
      : EnumAttributeImpl(TypeAttrEntry, Kind), Ty(Ty) {}

  Type *getTypeValue() const { return Ty; }
};

class ConstantRangeAttributeImpl : public EnumAttributeImpl {
  ConstantRange CR;

public:
  ConstantRangeAttributeImpl(Attribute::AttrKind Kind, const ConstantRange &CR)
      : EnumAttributeImpl(ConstantRangeAttrEntry, Kind), CR(CR) {}

  const ConstantRange &getConstantRangeValue() const { return CR; }
};

class ConstantRangeListAttributeImpl final
    : public EnumAttributeImpl,
      private TrailingObjects<ConstantRangeListAttributeImpl, ConstantRange> {
  friend TrailingObjects;

  unsigned Size;
  size_t numTrailingObjects(OverloadToken<ConstantRange>) const { return Size; }

public:
  ConstantRangeListAttributeImpl(Attribute::AttrKind Kind,
                                 ArrayRef<ConstantRange> Val)
      : EnumAttributeImpl(ConstantRangeListAttrEntry, Kind), Size(Val.size()) {
    assert(Size > 0);
    ConstantRange *TrailingCR = getTrailingObjects<ConstantRange>();
    std::uninitialized_copy(Val.begin(), Val.end(), TrailingCR);
  }

  ~ConstantRangeListAttributeImpl() {
    ConstantRange *TrailingCR = getTrailingObjects<ConstantRange>();
    for (unsigned I = 0; I != Size; ++I)
      TrailingCR[I].~ConstantRange();
  }

  ArrayRef<ConstantRange> getConstantRangeListValue() const {
    return ArrayRef(getTrailingObjects<ConstantRange>(), Size);
  }

  static size_t totalSizeToAlloc(ArrayRef<ConstantRange> Val) {
    return TrailingObjects::totalSizeToAlloc<ConstantRange>(Val.size());
  }
};

class AttributeBitSet {
  /// Bitset with a bit for each available attribute Attribute::AttrKind.
  uint8_t AvailableAttrs[12] = {};
  static_assert(Attribute::EndAttrKinds <= sizeof(AvailableAttrs) * CHAR_BIT,
                "Too many attributes");

public:
  bool hasAttribute(Attribute::AttrKind Kind) const {
    return AvailableAttrs[Kind / 8] & (1 << (Kind % 8));
  }

  void addAttribute(Attribute::AttrKind Kind) {
    AvailableAttrs[Kind / 8] |= 1 << (Kind % 8);
  }
};

//===----------------------------------------------------------------------===//
/// \class
/// This class represents a group of attributes that apply to one
/// element: function, return type, or parameter.
class AttributeSetNode final
    : public FoldingSetNode,
      private TrailingObjects<AttributeSetNode, Attribute> {
  friend TrailingObjects;

  unsigned NumAttrs; ///< Number of attributes in this node.
  AttributeBitSet AvailableAttrs; ///< Available enum attributes.

  DenseMap<StringRef, Attribute> StringAttrs;

  AttributeSetNode(ArrayRef<Attribute> Attrs);

  static AttributeSetNode *getSorted(LLVMContext &C,
                                     ArrayRef<Attribute> SortedAttrs);
  std::optional<Attribute> findEnumAttribute(Attribute::AttrKind Kind) const;

public:
  // AttributesSetNode is uniqued, these should not be available.
  AttributeSetNode(const AttributeSetNode &) = delete;
  AttributeSetNode &operator=(const AttributeSetNode &) = delete;

  void operator delete(void *p) { ::operator delete(p); }

  static AttributeSetNode *get(LLVMContext &C, const AttrBuilder &B);

  static AttributeSetNode *get(LLVMContext &C, ArrayRef<Attribute> Attrs);

  /// Return the number of attributes this AttributeList contains.
  unsigned getNumAttributes() const { return NumAttrs; }

  bool hasAttribute(Attribute::AttrKind Kind) const {
    return AvailableAttrs.hasAttribute(Kind);
  }
  bool hasAttribute(StringRef Kind) const;
  bool hasAttributes() const { return NumAttrs != 0; }

  Attribute getAttribute(Attribute::AttrKind Kind) const;
  Attribute getAttribute(StringRef Kind) const;

  MaybeAlign getAlignment() const;
  MaybeAlign getStackAlignment() const;
  uint64_t getDereferenceableBytes() const;
  uint64_t getDereferenceableOrNullBytes() const;
  std::optional<std::pair<unsigned, std::optional<unsigned>>> getAllocSizeArgs()
      const;
  unsigned getVScaleRangeMin() const;
  std::optional<unsigned> getVScaleRangeMax() const;
  UWTableKind getUWTableKind() const;
  AllocFnKind getAllocKind() const;
  MemoryEffects getMemoryEffects() const;
  FPClassTest getNoFPClass() const;
  std::string getAsString(bool InAttrGrp) const;
  Type *getAttributeType(Attribute::AttrKind Kind) const;

  using iterator = const Attribute *;

  iterator begin() const { return getTrailingObjects<Attribute>(); }
  iterator end() const { return begin() + NumAttrs; }

  void Profile(FoldingSetNodeID &ID) const {
    Profile(ID, ArrayRef(begin(), end()));
  }

  static void Profile(FoldingSetNodeID &ID, ArrayRef<Attribute> AttrList) {
    for (const auto &Attr : AttrList)
      Attr.Profile(ID);
  }
};

//===----------------------------------------------------------------------===//
/// \class
/// This class represents a set of attributes that apply to the function,
/// return type, and parameters.
class AttributeListImpl final
    : public FoldingSetNode,
      private TrailingObjects<AttributeListImpl, AttributeSet> {
  friend class AttributeList;
  friend TrailingObjects;

private:
  unsigned NumAttrSets; ///< Number of entries in this set.
  /// Available enum function attributes.
  AttributeBitSet AvailableFunctionAttrs;
  /// Union of enum attributes available at any index.
  AttributeBitSet AvailableSomewhereAttrs;

  // Helper fn for TrailingObjects class.
  size_t numTrailingObjects(OverloadToken<AttributeSet>) { return NumAttrSets; }

public:
  AttributeListImpl(ArrayRef<AttributeSet> Sets);

  // AttributesSetImpt is uniqued, these should not be available.
  AttributeListImpl(const AttributeListImpl &) = delete;
  AttributeListImpl &operator=(const AttributeListImpl &) = delete;

  /// Return true if the AttributeSet or the FunctionIndex has an
  /// enum attribute of the given kind.
  bool hasFnAttribute(Attribute::AttrKind Kind) const {
    return AvailableFunctionAttrs.hasAttribute(Kind);
  }

  /// Return true if the specified attribute is set for at least one
  /// parameter or for the return value. If Index is not nullptr, the index
  /// of a parameter with the specified attribute is provided.
  bool hasAttrSomewhere(Attribute::AttrKind Kind,
                        unsigned *Index = nullptr) const;

  using iterator = const AttributeSet *;

  iterator begin() const { return getTrailingObjects<AttributeSet>(); }
  iterator end() const { return begin() + NumAttrSets; }

  void Profile(FoldingSetNodeID &ID) const;
  static void Profile(FoldingSetNodeID &ID, ArrayRef<AttributeSet> Nodes);

  void dump() const;
};

static_assert(std::is_trivially_destructible<AttributeListImpl>::value,
              "AttributeListImpl should be trivially destructible");

} // end namespace llvm

#endif // LLVM_LIB_IR_ATTRIBUTEIMPL_H
