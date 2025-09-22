//===--- DeclID.h - ID number for deserialized declarations  ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines DeclID class family to describe the deserialized
// declarations. The DeclID is widely used in AST via LazyDeclPtr, or calls to
// `ExternalASTSource::getExternalDecl`. It will be helpful for type safety to
// require the use of `DeclID` to explicit.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_DECLID_H
#define LLVM_CLANG_AST_DECLID_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/iterator.h"

#include <climits>

namespace clang {

/// Predefined declaration IDs.
///
/// These declaration IDs correspond to predefined declarations in the AST
/// context, such as the NULL declaration ID. Such declarations are never
/// actually serialized, since they will be built by the AST context when
/// it is created.
enum PredefinedDeclIDs {
  /// The NULL declaration.
  PREDEF_DECL_NULL_ID = 0,

  /// The translation unit.
  PREDEF_DECL_TRANSLATION_UNIT_ID = 1,

  /// The Objective-C 'id' type.
  PREDEF_DECL_OBJC_ID_ID = 2,

  /// The Objective-C 'SEL' type.
  PREDEF_DECL_OBJC_SEL_ID = 3,

  /// The Objective-C 'Class' type.
  PREDEF_DECL_OBJC_CLASS_ID = 4,

  /// The Objective-C 'Protocol' type.
  PREDEF_DECL_OBJC_PROTOCOL_ID = 5,

  /// The signed 128-bit integer type.
  PREDEF_DECL_INT_128_ID = 6,

  /// The unsigned 128-bit integer type.
  PREDEF_DECL_UNSIGNED_INT_128_ID = 7,

  /// The internal 'instancetype' typedef.
  PREDEF_DECL_OBJC_INSTANCETYPE_ID = 8,

  /// The internal '__builtin_va_list' typedef.
  PREDEF_DECL_BUILTIN_VA_LIST_ID = 9,

  /// The internal '__va_list_tag' struct, if any.
  PREDEF_DECL_VA_LIST_TAG = 10,

  /// The internal '__builtin_ms_va_list' typedef.
  PREDEF_DECL_BUILTIN_MS_VA_LIST_ID = 11,

  /// The predeclared '_GUID' struct.
  PREDEF_DECL_BUILTIN_MS_GUID_ID = 12,

  /// The extern "C" context.
  PREDEF_DECL_EXTERN_C_CONTEXT_ID = 13,

  /// The internal '__make_integer_seq' template.
  PREDEF_DECL_MAKE_INTEGER_SEQ_ID = 14,

  /// The internal '__NSConstantString' typedef.
  PREDEF_DECL_CF_CONSTANT_STRING_ID = 15,

  /// The internal '__NSConstantString' tag type.
  PREDEF_DECL_CF_CONSTANT_STRING_TAG_ID = 16,

  /// The internal '__type_pack_element' template.
  PREDEF_DECL_TYPE_PACK_ELEMENT_ID = 17,
};

/// The number of declaration IDs that are predefined.
///
/// For more information about predefined declarations, see the
/// \c PredefinedDeclIDs type and the PREDEF_DECL_*_ID constants.
const unsigned int NUM_PREDEF_DECL_IDS = 18;

/// GlobalDeclID means DeclID in the current ASTContext and LocalDeclID means
/// DeclID specific to a certain ModuleFile. Specially, in ASTWriter, the
/// LocalDeclID to the ModuleFile been writting is equal to the GlobalDeclID.
/// Outside the serializer, all the DeclID been used should be GlobalDeclID.
/// We can translate a LocalDeclID to the GlobalDeclID by
/// `ASTReader::getGlobalDeclID()`.

class DeclIDBase {
public:
  /// An ID number that refers to a declaration in an AST file.
  ///
  /// The ID numbers of declarations are consecutive (in order of
  /// discovery), with values below NUM_PREDEF_DECL_IDS being reserved.
  /// At the start of a chain of precompiled headers, declaration ID 1 is
  /// used for the translation unit declaration.
  ///
  /// DeclID should only be used directly in serialization. All other users
  /// should use LocalDeclID or GlobalDeclID.
  using DeclID = uint64_t;

protected:
  DeclIDBase() : ID(PREDEF_DECL_NULL_ID) {}
  explicit DeclIDBase(DeclID ID) : ID(ID) {}

public:
  DeclID getRawValue() const { return ID; }

  explicit operator DeclID() const { return ID; }

  explicit operator PredefinedDeclIDs() const { return (PredefinedDeclIDs)ID; }

  bool isValid() const { return ID != PREDEF_DECL_NULL_ID; }

  bool isInvalid() const { return ID == PREDEF_DECL_NULL_ID; }

  unsigned getModuleFileIndex() const { return ID >> 32; }

  unsigned getLocalDeclIndex() const;

  // The DeclID may be compared with predefined decl ID.
  friend bool operator==(const DeclIDBase &LHS, const DeclID &RHS) {
    return LHS.ID == RHS;
  }
  friend bool operator!=(const DeclIDBase &LHS, const DeclID &RHS) {
    return !operator==(LHS, RHS);
  }
  friend bool operator<(const DeclIDBase &LHS, const DeclID &RHS) {
    return LHS.ID < RHS;
  }
  friend bool operator<=(const DeclIDBase &LHS, const DeclID &RHS) {
    return LHS.ID <= RHS;
  }
  friend bool operator>(const DeclIDBase &LHS, const DeclID &RHS) {
    return LHS.ID > RHS;
  }
  friend bool operator>=(const DeclIDBase &LHS, const DeclID &RHS) {
    return LHS.ID >= RHS;
  }

  friend bool operator==(const DeclIDBase &LHS, const DeclIDBase &RHS) {
    return LHS.ID == RHS.ID;
  }
  friend bool operator!=(const DeclIDBase &LHS, const DeclIDBase &RHS) {
    return LHS.ID != RHS.ID;
  }

  // We may sort the decl ID.
  friend bool operator<(const DeclIDBase &LHS, const DeclIDBase &RHS) {
    return LHS.ID < RHS.ID;
  }
  friend bool operator>(const DeclIDBase &LHS, const DeclIDBase &RHS) {
    return LHS.ID > RHS.ID;
  }
  friend bool operator<=(const DeclIDBase &LHS, const DeclIDBase &RHS) {
    return LHS.ID <= RHS.ID;
  }
  friend bool operator>=(const DeclIDBase &LHS, const DeclIDBase &RHS) {
    return LHS.ID >= RHS.ID;
  }

protected:
  DeclID ID;
};

class ASTWriter;
class ASTReader;
namespace serialization {
class ModuleFile;
} // namespace serialization

class LocalDeclID : public DeclIDBase {
  using Base = DeclIDBase;

  LocalDeclID(PredefinedDeclIDs ID) : Base(ID) {}
  explicit LocalDeclID(DeclID ID) : Base(ID) {}

  // Every Decl ID is a local decl ID to the module being writing in ASTWriter.
  friend class ASTWriter;
  friend class GlobalDeclID;

public:
  LocalDeclID() : Base() {}

  static LocalDeclID get(ASTReader &Reader, serialization::ModuleFile &MF,
                         DeclID ID);
  static LocalDeclID get(ASTReader &Reader, serialization::ModuleFile &MF,
                         unsigned ModuleFileIndex, unsigned LocalDeclID);

  LocalDeclID &operator++() {
    ++ID;
    return *this;
  }

  LocalDeclID operator++(int) {
    LocalDeclID Ret = *this;
    ++(*this);
    return Ret;
  }
};

class GlobalDeclID : public DeclIDBase {
  using Base = DeclIDBase;

public:
  GlobalDeclID() : Base() {}
  explicit GlobalDeclID(DeclID ID) : Base(ID) {}

  explicit GlobalDeclID(unsigned ModuleFileIndex, unsigned LocalID)
      : Base((DeclID)ModuleFileIndex << 32 | (DeclID)LocalID) {}

  // For DeclIDIterator<GlobalDeclID> to be able to convert a GlobalDeclID
  // to a LocalDeclID.
  explicit operator LocalDeclID() const { return LocalDeclID(this->ID); }
};

/// A helper iterator adaptor to convert the iterators to
/// `SmallVector<SomeDeclID>` to the iterators to `SmallVector<OtherDeclID>`.
template <class FromTy, class ToTy>
class DeclIDIterator
    : public llvm::iterator_adaptor_base<DeclIDIterator<FromTy, ToTy>,
                                         const FromTy *,
                                         std::forward_iterator_tag, ToTy> {
public:
  DeclIDIterator() : DeclIDIterator::iterator_adaptor_base(nullptr) {}

  DeclIDIterator(const FromTy *ID)
      : DeclIDIterator::iterator_adaptor_base(ID) {}

  ToTy operator*() const { return ToTy(*this->I); }

  bool operator==(const DeclIDIterator &RHS) const { return this->I == RHS.I; }
};

} // namespace clang

namespace llvm {
template <> struct DenseMapInfo<clang::GlobalDeclID> {
  using GlobalDeclID = clang::GlobalDeclID;
  using DeclID = GlobalDeclID::DeclID;

  static GlobalDeclID getEmptyKey() {
    return GlobalDeclID(DenseMapInfo<DeclID>::getEmptyKey());
  }

  static GlobalDeclID getTombstoneKey() {
    return GlobalDeclID(DenseMapInfo<DeclID>::getTombstoneKey());
  }

  static unsigned getHashValue(const GlobalDeclID &Key) {
    return DenseMapInfo<DeclID>::getHashValue(Key.getRawValue());
  }

  static bool isEqual(const GlobalDeclID &L, const GlobalDeclID &R) {
    return L == R;
  }
};

} // namespace llvm

#endif
