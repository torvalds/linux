//===--- CGRecordLayout.h - LLVM Record Layout Information ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_CGRECORDLAYOUT_H
#define LLVM_CLANG_LIB_CODEGEN_CGRECORDLAYOUT_H

#include "clang/AST/CharUnits.h"
#include "clang/AST/DeclCXX.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/DerivedTypes.h"

namespace llvm {
  class StructType;
}

namespace clang {
namespace CodeGen {

/// Structure with information about how a bitfield should be accessed.
///
/// Often we layout a sequence of bitfields as a contiguous sequence of bits.
/// When the AST record layout does this, we represent it in the LLVM IR's type
/// as either a sequence of i8 members or a byte array to reserve the number of
/// bytes touched without forcing any particular alignment beyond the basic
/// character alignment.
///
/// Then accessing a particular bitfield involves converting this byte array
/// into a single integer of that size (i24 or i40 -- may not be power-of-two
/// size), loading it, and shifting and masking to extract the particular
/// subsequence of bits which make up that particular bitfield. This structure
/// encodes the information used to construct the extraction code sequences.
/// The CGRecordLayout also has a field index which encodes which byte-sequence
/// this bitfield falls within. Let's assume the following C struct:
///
///   struct S {
///     char a, b, c;
///     unsigned bits : 3;
///     unsigned more_bits : 4;
///     unsigned still_more_bits : 7;
///   };
///
/// This will end up as the following LLVM type. The first array is the
/// bitfield, and the second is the padding out to a 4-byte alignment.
///
///   %t = type { i8, i8, i8, i8, i8, [3 x i8] }
///
/// When generating code to access more_bits, we'll generate something
/// essentially like this:
///
///   define i32 @foo(%t* %base) {
///     %0 = gep %t* %base, i32 0, i32 3
///     %2 = load i8* %1
///     %3 = lshr i8 %2, 3
///     %4 = and i8 %3, 15
///     %5 = zext i8 %4 to i32
///     ret i32 %i
///   }
///
struct CGBitFieldInfo {
  /// The offset within a contiguous run of bitfields that are represented as
  /// a single "field" within the LLVM struct type. This offset is in bits.
  unsigned Offset : 16;

  /// The total size of the bit-field, in bits.
  unsigned Size : 15;

  /// Whether the bit-field is signed.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsSigned : 1;

  /// The storage size in bits which should be used when accessing this
  /// bitfield.
  unsigned StorageSize;

  /// The offset of the bitfield storage from the start of the struct.
  CharUnits StorageOffset;

  /// The offset within a contiguous run of bitfields that are represented as a
  /// single "field" within the LLVM struct type, taking into account the AAPCS
  /// rules for volatile bitfields. This offset is in bits.
  unsigned VolatileOffset : 16;

  /// The storage size in bits which should be used when accessing this
  /// bitfield.
  unsigned VolatileStorageSize;

  /// The offset of the bitfield storage from the start of the struct.
  CharUnits VolatileStorageOffset;

  CGBitFieldInfo()
      : Offset(), Size(), IsSigned(), StorageSize(), VolatileOffset(),
        VolatileStorageSize() {}

  CGBitFieldInfo(unsigned Offset, unsigned Size, bool IsSigned,
                 unsigned StorageSize, CharUnits StorageOffset)
      : Offset(Offset), Size(Size), IsSigned(IsSigned),
        StorageSize(StorageSize), StorageOffset(StorageOffset) {}

  void print(raw_ostream &OS) const;
  void dump() const;

  /// Given a bit-field decl, build an appropriate helper object for
  /// accessing that field (which is expected to have the given offset and
  /// size).
  static CGBitFieldInfo MakeInfo(class CodeGenTypes &Types,
                                 const FieldDecl *FD,
                                 uint64_t Offset, uint64_t Size,
                                 uint64_t StorageSize,
                                 CharUnits StorageOffset);
};

/// CGRecordLayout - This class handles struct and union layout info while
/// lowering AST types to LLVM types.
///
/// These layout objects are only created on demand as IR generation requires.
class CGRecordLayout {
  friend class CodeGenTypes;

  CGRecordLayout(const CGRecordLayout &) = delete;
  void operator=(const CGRecordLayout &) = delete;

private:
  /// The LLVM type corresponding to this record layout; used when
  /// laying it out as a complete object.
  llvm::StructType *CompleteObjectType;

  /// The LLVM type for the non-virtual part of this record layout;
  /// used when laying it out as a base subobject.
  llvm::StructType *BaseSubobjectType;

  /// Map from (non-bit-field) struct field to the corresponding llvm struct
  /// type field no. This info is populated by record builder.
  llvm::DenseMap<const FieldDecl *, unsigned> FieldInfo;

  /// Map from (bit-field) struct field to the corresponding llvm struct type
  /// field no. This info is populated by record builder.
  llvm::DenseMap<const FieldDecl *, CGBitFieldInfo> BitFields;

  // FIXME: Maybe we could use a CXXBaseSpecifier as the key and use a single
  // map for both virtual and non-virtual bases.
  llvm::DenseMap<const CXXRecordDecl *, unsigned> NonVirtualBases;

  /// Map from virtual bases to their field index in the complete object.
  llvm::DenseMap<const CXXRecordDecl *, unsigned> CompleteObjectVirtualBases;

  /// False if any direct or indirect subobject of this class, when
  /// considered as a complete object, requires a non-zero bitpattern
  /// when zero-initialized.
  bool IsZeroInitializable : 1;

  /// False if any direct or indirect subobject of this class, when
  /// considered as a base subobject, requires a non-zero bitpattern
  /// when zero-initialized.
  bool IsZeroInitializableAsBase : 1;

public:
  CGRecordLayout(llvm::StructType *CompleteObjectType,
                 llvm::StructType *BaseSubobjectType,
                 bool IsZeroInitializable,
                 bool IsZeroInitializableAsBase)
    : CompleteObjectType(CompleteObjectType),
      BaseSubobjectType(BaseSubobjectType),
      IsZeroInitializable(IsZeroInitializable),
      IsZeroInitializableAsBase(IsZeroInitializableAsBase) {}

  /// Return the "complete object" LLVM type associated with
  /// this record.
  llvm::StructType *getLLVMType() const {
    return CompleteObjectType;
  }

  /// Return the "base subobject" LLVM type associated with
  /// this record.
  llvm::StructType *getBaseSubobjectLLVMType() const {
    return BaseSubobjectType;
  }

  /// Check whether this struct can be C++ zero-initialized
  /// with a zeroinitializer.
  bool isZeroInitializable() const {
    return IsZeroInitializable;
  }

  /// Check whether this struct can be C++ zero-initialized
  /// with a zeroinitializer when considered as a base subobject.
  bool isZeroInitializableAsBase() const {
    return IsZeroInitializableAsBase;
  }

  bool containsFieldDecl(const FieldDecl *FD) const {
    return FieldInfo.count(FD) != 0;
  }

  /// Return llvm::StructType element number that corresponds to the
  /// field FD.
  unsigned getLLVMFieldNo(const FieldDecl *FD) const {
    FD = FD->getCanonicalDecl();
    assert(FieldInfo.count(FD) && "Invalid field for record!");
    return FieldInfo.lookup(FD);
  }

  // Return whether the following non virtual base has a corresponding
  // entry in the LLVM struct.
  bool hasNonVirtualBaseLLVMField(const CXXRecordDecl *RD) const {
    return NonVirtualBases.count(RD);
  }

  unsigned getNonVirtualBaseLLVMFieldNo(const CXXRecordDecl *RD) const {
    assert(NonVirtualBases.count(RD) && "Invalid non-virtual base!");
    return NonVirtualBases.lookup(RD);
  }

  /// Return the LLVM field index corresponding to the given
  /// virtual base.  Only valid when operating on the complete object.
  unsigned getVirtualBaseIndex(const CXXRecordDecl *base) const {
    assert(CompleteObjectVirtualBases.count(base) && "Invalid virtual base!");
    return CompleteObjectVirtualBases.lookup(base);
  }

  /// Return the BitFieldInfo that corresponds to the field FD.
  const CGBitFieldInfo &getBitFieldInfo(const FieldDecl *FD) const {
    FD = FD->getCanonicalDecl();
    assert(FD->isBitField() && "Invalid call for non-bit-field decl!");
    llvm::DenseMap<const FieldDecl *, CGBitFieldInfo>::const_iterator
      it = BitFields.find(FD);
    assert(it != BitFields.end() && "Unable to find bitfield info");
    return it->second;
  }

  void print(raw_ostream &OS) const;
  void dump() const;
};

}  // end namespace CodeGen
}  // end namespace clang

#endif
