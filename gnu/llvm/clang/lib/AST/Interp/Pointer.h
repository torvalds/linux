//===--- Pointer.h - Types for the constexpr VM -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines the classes responsible for pointer tracking.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_INTERP_POINTER_H
#define LLVM_CLANG_AST_INTERP_POINTER_H

#include "Descriptor.h"
#include "InterpBlock.h"
#include "clang/AST/ComparisonCategories.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "llvm/Support/raw_ostream.h"

namespace clang {
namespace interp {
class Block;
class DeadBlock;
class Pointer;
class Context;
template <unsigned A, bool B> class Integral;
enum PrimType : unsigned;

class Pointer;
inline llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const Pointer &P);

struct BlockPointer {
  /// The block the pointer is pointing to.
  Block *Pointee;
  /// Start of the current subfield.
  unsigned Base;
};

struct IntPointer {
  const Descriptor *Desc;
  uint64_t Value;
};

enum class Storage { Block, Int };

/// A pointer to a memory block, live or dead.
///
/// This object can be allocated into interpreter stack frames. If pointing to
/// a live block, it is a link in the chain of pointers pointing to the block.
///
/// In the simplest form, a Pointer has a Block* (the pointee) and both Base
/// and Offset are 0, which means it will point to raw data.
///
/// The Base field is used to access metadata about the data. For primitive
/// arrays, the Base is followed by an InitMap. In a variety of cases, the
/// Base is preceded by an InlineDescriptor, which is used to track the
/// initialization state, among other things.
///
/// The Offset field is used to access the actual data. In other words, the
/// data the pointer decribes can be found at
/// Pointee->rawData() + Pointer.Offset.
///
///
/// Pointee                      Offset
/// │                              │
/// │                              │
/// ▼                              ▼
/// ┌───────┬────────────┬─────────┬────────────────────────────┐
/// │ Block │ InlineDesc │ InitMap │ Actual Data                │
/// └───────┴────────────┴─────────┴────────────────────────────┘
///                      ▲
///                      │
///                      │
///                     Base
class Pointer {
private:
  static constexpr unsigned PastEndMark = ~0u;
  static constexpr unsigned RootPtrMark = ~0u;

public:
  Pointer() {
    StorageKind = Storage::Int;
    PointeeStorage.Int.Value = 0;
    PointeeStorage.Int.Desc = nullptr;
  }
  Pointer(Block *B);
  Pointer(Block *B, uint64_t BaseAndOffset);
  Pointer(const Pointer &P);
  Pointer(Pointer &&P);
  Pointer(uint64_t Address, const Descriptor *Desc, uint64_t Offset = 0)
      : Offset(Offset), StorageKind(Storage::Int) {
    PointeeStorage.Int.Value = Address;
    PointeeStorage.Int.Desc = Desc;
  }
  ~Pointer();

  void operator=(const Pointer &P);
  void operator=(Pointer &&P);

  /// Equality operators are just for tests.
  bool operator==(const Pointer &P) const {
    if (P.StorageKind != StorageKind)
      return false;
    if (isIntegralPointer())
      return P.asIntPointer().Value == asIntPointer().Value &&
             Offset == P.Offset;

    assert(isBlockPointer());
    return P.asBlockPointer().Pointee == asBlockPointer().Pointee &&
           P.asBlockPointer().Base == asBlockPointer().Base &&
           Offset == P.Offset;
  }

  bool operator!=(const Pointer &P) const { return !(P == *this); }

  /// Converts the pointer to an APValue.
  APValue toAPValue(const ASTContext &ASTCtx) const;

  /// Converts the pointer to a string usable in diagnostics.
  std::string toDiagnosticString(const ASTContext &Ctx) const;

  uint64_t getIntegerRepresentation() const {
    if (isIntegralPointer())
      return asIntPointer().Value + (Offset * elemSize());
    return reinterpret_cast<uint64_t>(asBlockPointer().Pointee) + Offset;
  }

  /// Converts the pointer to an APValue that is an rvalue.
  std::optional<APValue> toRValue(const Context &Ctx,
                                  QualType ResultType) const;

  /// Offsets a pointer inside an array.
  [[nodiscard]] Pointer atIndex(uint64_t Idx) const {
    if (isIntegralPointer())
      return Pointer(asIntPointer().Value, asIntPointer().Desc, Idx);

    if (asBlockPointer().Base == RootPtrMark)
      return Pointer(asBlockPointer().Pointee, RootPtrMark,
                     getDeclDesc()->getSize());
    uint64_t Off = Idx * elemSize();
    if (getFieldDesc()->ElemDesc)
      Off += sizeof(InlineDescriptor);
    else
      Off += sizeof(InitMapPtr);
    return Pointer(asBlockPointer().Pointee, asBlockPointer().Base,
                   asBlockPointer().Base + Off);
  }

  /// Creates a pointer to a field.
  [[nodiscard]] Pointer atField(unsigned Off) const {
    unsigned Field = Offset + Off;
    if (isIntegralPointer())
      return Pointer(asIntPointer().Value + Field, asIntPointer().Desc);
    return Pointer(asBlockPointer().Pointee, Field, Field);
  }

  /// Subtract the given offset from the current Base and Offset
  /// of the pointer.
  [[nodiscard]]  Pointer atFieldSub(unsigned Off) const {
    assert(Offset >= Off);
    unsigned O = Offset - Off;
    return Pointer(asBlockPointer().Pointee, O, O);
  }

  /// Restricts the scope of an array element pointer.
  [[nodiscard]] Pointer narrow() const {
    if (!isBlockPointer())
      return *this;
    assert(isBlockPointer());
    // Null pointers cannot be narrowed.
    if (isZero() || isUnknownSizeArray())
      return *this;

    // Pointer to an array of base types - enter block.
    if (asBlockPointer().Base == RootPtrMark)
      return Pointer(asBlockPointer().Pointee, sizeof(InlineDescriptor),
                     Offset == 0 ? Offset : PastEndMark);

    // Pointer is one past end - magic offset marks that.
    if (isOnePastEnd())
      return Pointer(asBlockPointer().Pointee, asBlockPointer().Base,
                     PastEndMark);

    // Primitive arrays are a bit special since they do not have inline
    // descriptors. If Offset != Base, then the pointer already points to
    // an element and there is nothing to do. Otherwise, the pointer is
    // adjusted to the first element of the array.
    if (inPrimitiveArray()) {
      if (Offset != asBlockPointer().Base)
        return *this;
      return Pointer(asBlockPointer().Pointee, asBlockPointer().Base,
                     Offset + sizeof(InitMapPtr));
    }

    // Pointer is to a field or array element - enter it.
    if (Offset != asBlockPointer().Base)
      return Pointer(asBlockPointer().Pointee, Offset, Offset);

    // Enter the first element of an array.
    if (!getFieldDesc()->isArray())
      return *this;

    const unsigned NewBase = asBlockPointer().Base + sizeof(InlineDescriptor);
    return Pointer(asBlockPointer().Pointee, NewBase, NewBase);
  }

  /// Expands a pointer to the containing array, undoing narrowing.
  [[nodiscard]] Pointer expand() const {
    assert(isBlockPointer());
    Block *Pointee = asBlockPointer().Pointee;

    if (isElementPastEnd()) {
      // Revert to an outer one-past-end pointer.
      unsigned Adjust;
      if (inPrimitiveArray())
        Adjust = sizeof(InitMapPtr);
      else
        Adjust = sizeof(InlineDescriptor);
      return Pointer(Pointee, asBlockPointer().Base,
                     asBlockPointer().Base + getSize() + Adjust);
    }

    // Do not step out of array elements.
    if (asBlockPointer().Base != Offset)
      return *this;

    // If at base, point to an array of base types.
    if (isRoot())
      return Pointer(Pointee, RootPtrMark, 0);

    // Step into the containing array, if inside one.
    unsigned Next = asBlockPointer().Base - getInlineDesc()->Offset;
    const Descriptor *Desc =
        (Next == Pointee->getDescriptor()->getMetadataSize())
            ? getDeclDesc()
            : getDescriptor(Next)->Desc;
    if (!Desc->IsArray)
      return *this;
    return Pointer(Pointee, Next, Offset);
  }

  /// Checks if the pointer is null.
  bool isZero() const {
    if (isBlockPointer())
      return asBlockPointer().Pointee == nullptr;
    assert(isIntegralPointer());
    return asIntPointer().Value == 0 && Offset == 0;
  }
  /// Checks if the pointer is live.
  bool isLive() const {
    if (isIntegralPointer())
      return true;
    return asBlockPointer().Pointee && !asBlockPointer().Pointee->IsDead;
  }
  /// Checks if the item is a field in an object.
  bool isField() const {
    if (isIntegralPointer())
      return false;

    return !isRoot() && getFieldDesc()->asDecl();
  }

  /// Accessor for information about the declaration site.
  const Descriptor *getDeclDesc() const {
    if (isIntegralPointer())
      return asIntPointer().Desc;

    assert(isBlockPointer());
    assert(asBlockPointer().Pointee);
    return asBlockPointer().Pointee->Desc;
  }
  SourceLocation getDeclLoc() const { return getDeclDesc()->getLocation(); }

  /// Returns the expression or declaration the pointer has been created for.
  DeclTy getSource() const {
    if (isBlockPointer())
      return getDeclDesc()->getSource();

    assert(isIntegralPointer());
    return asIntPointer().Desc ? asIntPointer().Desc->getSource() : DeclTy();
  }

  /// Returns a pointer to the object of which this pointer is a field.
  [[nodiscard]] Pointer getBase() const {
    if (asBlockPointer().Base == RootPtrMark) {
      assert(Offset == PastEndMark && "cannot get base of a block");
      return Pointer(asBlockPointer().Pointee, asBlockPointer().Base, 0);
    }
    unsigned NewBase = asBlockPointer().Base - getInlineDesc()->Offset;
    return Pointer(asBlockPointer().Pointee, NewBase, NewBase);
  }
  /// Returns the parent array.
  [[nodiscard]] Pointer getArray() const {
    if (asBlockPointer().Base == RootPtrMark) {
      assert(Offset != 0 && Offset != PastEndMark && "not an array element");
      return Pointer(asBlockPointer().Pointee, asBlockPointer().Base, 0);
    }
    assert(Offset != asBlockPointer().Base && "not an array element");
    return Pointer(asBlockPointer().Pointee, asBlockPointer().Base,
                   asBlockPointer().Base);
  }

  /// Accessors for information about the innermost field.
  const Descriptor *getFieldDesc() const {
    if (isIntegralPointer())
      return asIntPointer().Desc;

    if (isRoot())
      return getDeclDesc();
    return getInlineDesc()->Desc;
  }

  /// Returns the type of the innermost field.
  QualType getType() const {
    if (inPrimitiveArray() && Offset != asBlockPointer().Base) {
      // Unfortunately, complex and vector types are not array types in clang,
      // but they are for us.
      if (const auto *AT = getFieldDesc()->getType()->getAsArrayTypeUnsafe())
        return AT->getElementType();
      if (const auto *CT = getFieldDesc()->getType()->getAs<ComplexType>())
        return CT->getElementType();
      if (const auto *CT = getFieldDesc()->getType()->getAs<VectorType>())
        return CT->getElementType();
    }
    return getFieldDesc()->getType();
  }

  [[nodiscard]] Pointer getDeclPtr() const {
    return Pointer(asBlockPointer().Pointee);
  }

  /// Returns the element size of the innermost field.
  size_t elemSize() const {
    if (isIntegralPointer()) {
      if (!asIntPointer().Desc)
        return 1;
      return asIntPointer().Desc->getElemSize();
    }

    if (asBlockPointer().Base == RootPtrMark)
      return getDeclDesc()->getSize();
    return getFieldDesc()->getElemSize();
  }
  /// Returns the total size of the innermost field.
  size_t getSize() const {
    assert(isBlockPointer());
    return getFieldDesc()->getSize();
  }

  /// Returns the offset into an array.
  unsigned getOffset() const {
    assert(Offset != PastEndMark && "invalid offset");
    if (asBlockPointer().Base == RootPtrMark)
      return Offset;

    unsigned Adjust = 0;
    if (Offset != asBlockPointer().Base) {
      if (getFieldDesc()->ElemDesc)
        Adjust = sizeof(InlineDescriptor);
      else
        Adjust = sizeof(InitMapPtr);
    }
    return Offset - asBlockPointer().Base - Adjust;
  }

  /// Whether this array refers to an array, but not
  /// to the first element.
  bool isArrayRoot() const {
    return inArray() && Offset == asBlockPointer().Base;
  }

  /// Checks if the innermost field is an array.
  bool inArray() const {
    if (isBlockPointer())
      return getFieldDesc()->IsArray;
    return false;
  }
  /// Checks if the structure is a primitive array.
  bool inPrimitiveArray() const {
    if (isBlockPointer())
      return getFieldDesc()->isPrimitiveArray();
    return false;
  }
  /// Checks if the structure is an array of unknown size.
  bool isUnknownSizeArray() const {
    if (!isBlockPointer())
      return false;
    return getFieldDesc()->isUnknownSizeArray();
  }
  /// Checks if the pointer points to an array.
  bool isArrayElement() const {
    if (isBlockPointer())
      return inArray() && asBlockPointer().Base != Offset;
    return false;
  }
  /// Pointer points directly to a block.
  bool isRoot() const {
    if (isZero() || isIntegralPointer())
      return true;
    return (asBlockPointer().Base ==
                asBlockPointer().Pointee->getDescriptor()->getMetadataSize() ||
            asBlockPointer().Base == 0);
  }
  /// If this pointer has an InlineDescriptor we can use to initialize.
  bool canBeInitialized() const {
    if (!isBlockPointer())
      return false;

    return asBlockPointer().Pointee && asBlockPointer().Base > 0;
  }

  [[nodiscard]] const BlockPointer &asBlockPointer() const {
    assert(isBlockPointer());
    return PointeeStorage.BS;
  }
  [[nodiscard]] const IntPointer &asIntPointer() const {
    assert(isIntegralPointer());
    return PointeeStorage.Int;
  }
  bool isBlockPointer() const { return StorageKind == Storage::Block; }
  bool isIntegralPointer() const { return StorageKind == Storage::Int; }

  /// Returns the record descriptor of a class.
  const Record *getRecord() const { return getFieldDesc()->ElemRecord; }
  /// Returns the element record type, if this is a non-primive array.
  const Record *getElemRecord() const {
    const Descriptor *ElemDesc = getFieldDesc()->ElemDesc;
    return ElemDesc ? ElemDesc->ElemRecord : nullptr;
  }
  /// Returns the field information.
  const FieldDecl *getField() const { return getFieldDesc()->asFieldDecl(); }

  /// Checks if the object is a union.
  bool isUnion() const;

  /// Checks if the storage is extern.
  bool isExtern() const {
    if (isBlockPointer())
      return asBlockPointer().Pointee && asBlockPointer().Pointee->isExtern();
    return false;
  }
  /// Checks if the storage is static.
  bool isStatic() const {
    if (isIntegralPointer())
      return true;
    assert(asBlockPointer().Pointee);
    return asBlockPointer().Pointee->isStatic();
  }
  /// Checks if the storage is temporary.
  bool isTemporary() const {
    if (isBlockPointer()) {
      assert(asBlockPointer().Pointee);
      return asBlockPointer().Pointee->isTemporary();
    }
    return false;
  }
  /// Checks if the storage is a static temporary.
  bool isStaticTemporary() const { return isStatic() && isTemporary(); }

  /// Checks if the field is mutable.
  bool isMutable() const {
    if (!isBlockPointer())
      return false;
    return !isRoot() && getInlineDesc()->IsFieldMutable;
  }

  bool isWeak() const {
    if (isIntegralPointer())
      return false;

    assert(isBlockPointer());
    if (const ValueDecl *VD = getDeclDesc()->asValueDecl())
      return VD->isWeak();
    return false;
  }
  /// Checks if an object was initialized.
  bool isInitialized() const;
  /// Checks if the object is active.
  bool isActive() const {
    if (!isBlockPointer())
      return true;
    return isRoot() || getInlineDesc()->IsActive;
  }
  /// Checks if a structure is a base class.
  bool isBaseClass() const { return isField() && getInlineDesc()->IsBase; }
  bool isVirtualBaseClass() const {
    return isField() && getInlineDesc()->IsVirtualBase;
  }
  /// Checks if the pointer points to a dummy value.
  bool isDummy() const {
    if (!isBlockPointer())
      return false;

    if (!asBlockPointer().Pointee)
      return false;

    return getDeclDesc()->isDummy();
  }

  /// Checks if an object or a subfield is mutable.
  bool isConst() const {
    if (isIntegralPointer())
      return true;
    return isRoot() ? getDeclDesc()->IsConst : getInlineDesc()->IsConst;
  }

  /// Returns the declaration ID.
  std::optional<unsigned> getDeclID() const {
    if (isBlockPointer()) {
      assert(asBlockPointer().Pointee);
      return asBlockPointer().Pointee->getDeclID();
    }
    return std::nullopt;
  }

  /// Returns the byte offset from the start.
  unsigned getByteOffset() const {
    if (isIntegralPointer())
      return asIntPointer().Value + Offset;
    if (isOnePastEnd())
      return PastEndMark;
    return Offset;
  }

  /// Returns the number of elements.
  unsigned getNumElems() const {
    if (isIntegralPointer())
      return ~unsigned(0);
    return getSize() / elemSize();
  }

  const Block *block() const { return asBlockPointer().Pointee; }

  /// Returns the index into an array.
  int64_t getIndex() const {
    if (!isBlockPointer())
      return 0;

    if (isZero())
      return 0;

    // narrow()ed element in a composite array.
    if (asBlockPointer().Base > sizeof(InlineDescriptor) &&
        asBlockPointer().Base == Offset)
      return 0;

    if (auto ElemSize = elemSize())
      return getOffset() / ElemSize;
    return 0;
  }

  /// Checks if the index is one past end.
  bool isOnePastEnd() const {
    if (isIntegralPointer())
      return false;

    if (!asBlockPointer().Pointee)
      return false;

    if (isUnknownSizeArray())
      return false;

    return isElementPastEnd() || isPastEnd() ||
           (getSize() == getOffset() && !isZeroSizeArray());
  }

  /// Checks if the pointer points past the end of the object.
  bool isPastEnd() const {
    if (isIntegralPointer())
      return false;

    return !isZero() && Offset > PointeeStorage.BS.Pointee->getSize();
  }

  /// Checks if the pointer is an out-of-bounds element pointer.
  bool isElementPastEnd() const { return Offset == PastEndMark; }

  /// Checks if the pointer is pointing to a zero-size array.
  bool isZeroSizeArray() const { return getFieldDesc()->isZeroSizeArray(); }

  /// Dereferences the pointer, if it's live.
  template <typename T> T &deref() const {
    assert(isLive() && "Invalid pointer");
    assert(isBlockPointer());
    assert(asBlockPointer().Pointee);
    assert(isDereferencable());
    assert(Offset + sizeof(T) <=
           asBlockPointer().Pointee->getDescriptor()->getAllocSize());

    if (isArrayRoot())
      return *reinterpret_cast<T *>(asBlockPointer().Pointee->rawData() +
                                    asBlockPointer().Base + sizeof(InitMapPtr));

    return *reinterpret_cast<T *>(asBlockPointer().Pointee->rawData() + Offset);
  }

  /// Dereferences a primitive element.
  template <typename T> T &elem(unsigned I) const {
    assert(I < getNumElems());
    assert(isBlockPointer());
    assert(asBlockPointer().Pointee);
    return reinterpret_cast<T *>(asBlockPointer().Pointee->data() +
                                 sizeof(InitMapPtr))[I];
  }

  /// Whether this block can be read from at all. This is only true for
  /// block pointers that point to a valid location inside that block.
  bool isDereferencable() const {
    if (!isBlockPointer())
      return false;
    if (isPastEnd())
      return false;

    return true;
  }

  /// Initializes a field.
  void initialize() const;
  /// Activats a field.
  void activate() const;
  /// Deactivates an entire strurcutre.
  void deactivate() const;

  /// Compare two pointers.
  ComparisonCategoryResult compare(const Pointer &Other) const {
    if (!hasSameBase(*this, Other))
      return ComparisonCategoryResult::Unordered;

    if (Offset < Other.Offset)
      return ComparisonCategoryResult::Less;
    else if (Offset > Other.Offset)
      return ComparisonCategoryResult::Greater;

    return ComparisonCategoryResult::Equal;
  }

  /// Checks if two pointers are comparable.
  static bool hasSameBase(const Pointer &A, const Pointer &B);
  /// Checks if two pointers can be subtracted.
  static bool hasSameArray(const Pointer &A, const Pointer &B);

  /// Prints the pointer.
  void print(llvm::raw_ostream &OS) const;

private:
  friend class Block;
  friend class DeadBlock;
  friend class MemberPointer;
  friend class InterpState;
  friend struct InitMap;
  friend class DynamicAllocator;

  Pointer(Block *Pointee, unsigned Base, uint64_t Offset);

  /// Returns the embedded descriptor preceding a field.
  InlineDescriptor *getInlineDesc() const {
    assert(asBlockPointer().Base != sizeof(GlobalInlineDescriptor));
    assert(asBlockPointer().Base <= asBlockPointer().Pointee->getSize());
    return getDescriptor(asBlockPointer().Base);
  }

  /// Returns a descriptor at a given offset.
  InlineDescriptor *getDescriptor(unsigned Offset) const {
    assert(Offset != 0 && "Not a nested pointer");
    assert(isBlockPointer());
    assert(!isZero());
    return reinterpret_cast<InlineDescriptor *>(
               asBlockPointer().Pointee->rawData() + Offset) -
           1;
  }

  /// Returns a reference to the InitMapPtr which stores the initialization map.
  InitMapPtr &getInitMap() const {
    assert(isBlockPointer());
    assert(!isZero());
    return *reinterpret_cast<InitMapPtr *>(asBlockPointer().Pointee->rawData() +
                                           asBlockPointer().Base);
  }

  /// Offset into the storage.
  uint64_t Offset = 0;

  /// Previous link in the pointer chain.
  Pointer *Prev = nullptr;
  /// Next link in the pointer chain.
  Pointer *Next = nullptr;

  union {
    BlockPointer BS;
    IntPointer Int;
  } PointeeStorage;
  Storage StorageKind = Storage::Int;
};

inline llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const Pointer &P) {
  P.print(OS);
  return OS;
}

} // namespace interp
} // namespace clang

#endif
