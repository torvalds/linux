//===--- Descriptor.h - Types for the constexpr VM --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines descriptors which characterise allocations.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_INTERP_DESCRIPTOR_H
#define LLVM_CLANG_AST_INTERP_DESCRIPTOR_H

#include "PrimType.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"

namespace clang {
namespace interp {
class Block;
class Record;
struct InitMap;
struct Descriptor;
enum PrimType : unsigned;

using DeclTy = llvm::PointerUnion<const Decl *, const Expr *>;
using InitMapPtr = std::optional<std::pair<bool, std::shared_ptr<InitMap>>>;

/// Invoked whenever a block is created. The constructor method fills in the
/// inline descriptors of all fields and array elements. It also initializes
/// all the fields which contain non-trivial types.
using BlockCtorFn = void (*)(Block *Storage, std::byte *FieldPtr, bool IsConst,
                             bool IsMutable, bool IsActive,
                             const Descriptor *FieldDesc);

/// Invoked when a block is destroyed. Invokes the destructors of all
/// non-trivial nested fields of arrays and records.
using BlockDtorFn = void (*)(Block *Storage, std::byte *FieldPtr,
                             const Descriptor *FieldDesc);

/// Invoked when a block with pointers referencing it goes out of scope. Such
/// blocks are persisted: the move function copies all inline descriptors and
/// non-trivial fields, as existing pointers might need to reference those
/// descriptors. Data is not copied since it cannot be legally read.
using BlockMoveFn = void (*)(Block *Storage, const std::byte *SrcFieldPtr,
                             std::byte *DstFieldPtr,
                             const Descriptor *FieldDesc);

enum class GlobalInitState {
  Initialized,
  NoInitializer,
  InitializerFailed,
};

/// Descriptor used for global variables.
struct alignas(void *) GlobalInlineDescriptor {
  GlobalInitState InitState = GlobalInitState::InitializerFailed;
};
static_assert(sizeof(GlobalInlineDescriptor) == sizeof(void *), "");

/// Inline descriptor embedded in structures and arrays.
///
/// Such descriptors precede all composite array elements and structure fields.
/// If the base of a pointer is not zero, the base points to the end of this
/// structure. The offset field is used to traverse the pointer chain up
/// to the root structure which allocated the object.
struct InlineDescriptor {
  /// Offset inside the structure/array.
  unsigned Offset;

  /// Flag indicating if the storage is constant or not.
  /// Relevant for primitive fields.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsConst : 1;
  /// For primitive fields, it indicates if the field was initialized.
  /// Primitive fields in static storage are always initialized.
  /// Arrays are always initialized, even though their elements might not be.
  /// Base classes are initialized after the constructor is invoked.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsInitialized : 1;
  /// Flag indicating if the field is an embedded base class.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsBase : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsVirtualBase : 1;
  /// Flag indicating if the field is the active member of a union.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsActive : 1;
  /// Flag indicating if the field is mutable (if in a record).
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsFieldMutable : 1;

  const Descriptor *Desc;

  InlineDescriptor(const Descriptor *D)
      : Offset(sizeof(InlineDescriptor)), IsConst(false), IsInitialized(false),
        IsBase(false), IsActive(false), IsFieldMutable(false), Desc(D) {}

  void dump() const { dump(llvm::errs()); }
  void dump(llvm::raw_ostream &OS) const;
};
static_assert(sizeof(GlobalInlineDescriptor) != sizeof(InlineDescriptor), "");

/// Describes a memory block created by an allocation site.
struct Descriptor final {
private:
  /// Original declaration, used to emit the error message.
  const DeclTy Source;
  /// Size of an element, in host bytes.
  const unsigned ElemSize;
  /// Size of the storage, in host bytes.
  const unsigned Size;
  /// Size of the metadata.
  const unsigned MDSize;
  /// Size of the allocation (storage + metadata), in host bytes.
  const unsigned AllocSize;

  /// Value to denote arrays of unknown size.
  static constexpr unsigned UnknownSizeMark = (unsigned)-1;

public:
  /// Token to denote structures of unknown size.
  struct UnknownSize {};

  using MetadataSize = std::optional<unsigned>;
  static constexpr MetadataSize InlineDescMD = sizeof(InlineDescriptor);
  static constexpr MetadataSize GlobalMD = sizeof(GlobalInlineDescriptor);

  /// Maximum number of bytes to be used for array elements.
  static constexpr unsigned MaxArrayElemBytes =
      std::numeric_limits<decltype(AllocSize)>::max() - sizeof(InitMapPtr) -
      align(std::max(*InlineDescMD, *GlobalMD));

  /// Pointer to the record, if block contains records.
  const Record *const ElemRecord = nullptr;
  /// Descriptor of the array element.
  const Descriptor *const ElemDesc = nullptr;
  /// The primitive type this descriptor was created for,
  /// or the primitive element type in case this is
  /// a primitive array.
  const std::optional<PrimType> PrimT = std::nullopt;
  /// Flag indicating if the block is mutable.
  const bool IsConst = false;
  /// Flag indicating if a field is mutable.
  const bool IsMutable = false;
  /// Flag indicating if the block is a temporary.
  const bool IsTemporary = false;
  /// Flag indicating if the block is an array.
  const bool IsArray = false;
  /// Flag indicating if this is a dummy descriptor.
  bool IsDummy = false;

  /// Storage management methods.
  const BlockCtorFn CtorFn = nullptr;
  const BlockDtorFn DtorFn = nullptr;
  const BlockMoveFn MoveFn = nullptr;

  /// Allocates a descriptor for a primitive.
  Descriptor(const DeclTy &D, PrimType Type, MetadataSize MD, bool IsConst,
             bool IsTemporary, bool IsMutable);

  /// Allocates a descriptor for an array of primitives.
  Descriptor(const DeclTy &D, PrimType Type, MetadataSize MD, size_t NumElems,
             bool IsConst, bool IsTemporary, bool IsMutable);

  /// Allocates a descriptor for an array of primitives of unknown size.
  Descriptor(const DeclTy &D, PrimType Type, MetadataSize MDSize,
             bool IsTemporary, UnknownSize);

  /// Allocates a descriptor for an array of composites.
  Descriptor(const DeclTy &D, const Descriptor *Elem, MetadataSize MD,
             unsigned NumElems, bool IsConst, bool IsTemporary, bool IsMutable);

  /// Allocates a descriptor for an array of composites of unknown size.
  Descriptor(const DeclTy &D, const Descriptor *Elem, MetadataSize MD,
             bool IsTemporary, UnknownSize);

  /// Allocates a descriptor for a record.
  Descriptor(const DeclTy &D, const Record *R, MetadataSize MD, bool IsConst,
             bool IsTemporary, bool IsMutable);

  /// Allocates a dummy descriptor.
  Descriptor(const DeclTy &D);

  /// Make this descriptor a dummy descriptor.
  void makeDummy() { IsDummy = true; }

  QualType getType() const;
  QualType getElemQualType() const;
  SourceLocation getLocation() const;

  const Decl *asDecl() const { return Source.dyn_cast<const Decl *>(); }
  const Expr *asExpr() const { return Source.dyn_cast<const Expr *>(); }
  const DeclTy &getSource() const { return Source; }

  const ValueDecl *asValueDecl() const {
    return dyn_cast_if_present<ValueDecl>(asDecl());
  }

  const VarDecl *asVarDecl() const {
    return dyn_cast_if_present<VarDecl>(asDecl());
  }

  const FieldDecl *asFieldDecl() const {
    return dyn_cast_if_present<FieldDecl>(asDecl());
  }

  const RecordDecl *asRecordDecl() const {
    return dyn_cast_if_present<RecordDecl>(asDecl());
  }

  /// Returns the size of the object without metadata.
  unsigned getSize() const {
    assert(!isUnknownSizeArray() && "Array of unknown size");
    return Size;
  }

  PrimType getPrimType() const {
    assert(isPrimitiveArray() || isPrimitive());
    return *PrimT;
  }

  /// Returns the allocated size, including metadata.
  unsigned getAllocSize() const { return AllocSize; }
  /// returns the size of an element when the structure is viewed as an array.
  unsigned getElemSize()  const { return ElemSize; }
  /// Returns the size of the metadata.
  unsigned getMetadataSize() const { return MDSize; }

  /// Returns the number of elements stored in the block.
  unsigned getNumElems() const {
    return Size == UnknownSizeMark ? 0 : (getSize() / getElemSize());
  }

  /// Checks if the descriptor is of an array of primitives.
  bool isPrimitiveArray() const { return IsArray && !ElemDesc; }
  /// Checks if the descriptor is of an array of composites.
  bool isCompositeArray() const { return IsArray && ElemDesc; }
  /// Checks if the descriptor is of an array of zero size.
  bool isZeroSizeArray() const { return Size == 0; }
  /// Checks if the descriptor is of an array of unknown size.
  bool isUnknownSizeArray() const { return Size == UnknownSizeMark; }

  /// Checks if the descriptor is of a primitive.
  bool isPrimitive() const { return !IsArray && !ElemRecord; }

  /// Checks if the descriptor is of an array.
  bool isArray() const { return IsArray; }
  /// Checks if the descriptor is of a record.
  bool isRecord() const { return !IsArray && ElemRecord; }
  /// Checks if this is a dummy descriptor.
  bool isDummy() const { return IsDummy; }

  void dump() const;
  void dump(llvm::raw_ostream &OS) const;
};

/// Bitfield tracking the initialisation status of elements of primitive arrays.
struct InitMap final {
private:
  /// Type packing bits.
  using T = uint64_t;
  /// Bits stored in a single field.
  static constexpr uint64_t PER_FIELD = sizeof(T) * CHAR_BIT;

public:
  /// Initializes the map with no fields set.
  explicit InitMap(unsigned N);

private:
  friend class Pointer;

  /// Returns a pointer to storage.
  T *data() { return Data.get(); }
  const T *data() const { return Data.get(); }

  /// Initializes an element. Returns true when object if fully initialized.
  bool initializeElement(unsigned I);

  /// Checks if an element was initialized.
  bool isElementInitialized(unsigned I) const;

  static constexpr size_t numFields(unsigned N) {
    return (N + PER_FIELD - 1) / PER_FIELD;
  }
  /// Number of fields not initialized.
  unsigned UninitFields;
  std::unique_ptr<T[]> Data;
};

} // namespace interp
} // namespace clang

#endif
