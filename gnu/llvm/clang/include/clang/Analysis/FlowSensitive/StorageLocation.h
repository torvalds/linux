//===-- StorageLocation.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines classes that represent elements of the local variable store
// and of the heap during dataflow analysis.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_STORAGELOCATION_H
#define LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_STORAGELOCATION_H

#include "clang/AST/Decl.h"
#include "clang/AST/Type.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/Debug.h"
#include <cassert>

#define DEBUG_TYPE "dataflow"

namespace clang {
namespace dataflow {

/// Base class for elements of the local variable store and of the heap.
///
/// Each storage location holds a value. The mapping from storage locations to
/// values is stored in the environment.
class StorageLocation {
public:
  enum class Kind {
    Scalar,
    Record,
  };

  StorageLocation(Kind LocKind, QualType Type) : LocKind(LocKind), Type(Type) {
    assert(Type.isNull() || !Type->isReferenceType());
  }

  // Non-copyable because addresses of storage locations are used as their
  // identities throughout framework and user code. The framework is responsible
  // for construction and destruction of storage locations.
  StorageLocation(const StorageLocation &) = delete;
  StorageLocation &operator=(const StorageLocation &) = delete;

  virtual ~StorageLocation() = default;

  Kind getKind() const { return LocKind; }

  QualType getType() const { return Type; }

private:
  Kind LocKind;
  QualType Type;
};

/// A storage location that is not subdivided further for the purposes of
/// abstract interpretation. For example: `int`, `int*`, `int&`.
class ScalarStorageLocation final : public StorageLocation {
public:
  explicit ScalarStorageLocation(QualType Type)
      : StorageLocation(Kind::Scalar, Type) {}

  static bool classof(const StorageLocation *Loc) {
    return Loc->getKind() == Kind::Scalar;
  }
};

/// A storage location for a record (struct, class, or union).
///
/// Contains storage locations for all modeled fields of the record (also
/// referred to as "children"). The child map is flat, so accessible members of
/// the base class are directly accessible as children of this location.
///
/// Record storage locations may also contain so-called synthetic fields. These
/// are typically used to model the internal state of a class (e.g. the value
/// stored in a `std::optional`) without having to depend on that class's
/// implementation details. All `RecordStorageLocation`s of a given type should
/// have the same synthetic fields.
///
/// The storage location for a field of reference type may be null. This
/// typically occurs in one of two situations:
/// - The record has not been fully initialized.
/// - The maximum depth for modelling a self-referential data structure has been
///   reached.
/// Storage locations for fields of all other types must be non-null.
///
/// FIXME: Currently, the storage location of unions is modelled the same way as
/// that of structs or classes. Eventually, we need to change this modelling so
/// that all of the members of a given union have the same storage location.
class RecordStorageLocation final : public StorageLocation {
public:
  using FieldToLoc = llvm::DenseMap<const ValueDecl *, StorageLocation *>;
  using SyntheticFieldMap = llvm::StringMap<StorageLocation *>;

  RecordStorageLocation(QualType Type, FieldToLoc TheChildren,
                        SyntheticFieldMap TheSyntheticFields)
      : StorageLocation(Kind::Record, Type), Children(std::move(TheChildren)),
        SyntheticFields(std::move(TheSyntheticFields)) {
    assert(!Type.isNull());
    assert(Type->isRecordType());
    assert([this] {
      for (auto [Field, Loc] : Children) {
        if (!Field->getType()->isReferenceType() && Loc == nullptr)
          return false;
      }
      return true;
    }());
  }

  static bool classof(const StorageLocation *Loc) {
    return Loc->getKind() == Kind::Record;
  }

  /// Returns the child storage location for `D`.
  ///
  /// May return null if `D` has reference type; guaranteed to return non-null
  /// in all other cases.
  ///
  /// Note that it is an error to call this with a field that does not exist.
  /// The function does not return null in this case.
  StorageLocation *getChild(const ValueDecl &D) const {
    auto It = Children.find(&D);
    LLVM_DEBUG({
      if (It == Children.end()) {
        llvm::dbgs() << "Couldn't find child " << D.getNameAsString()
                     << " on StorageLocation " << this << " of type "
                     << getType() << "\n";
        llvm::dbgs() << "Existing children:\n";
        for ([[maybe_unused]] auto [Field, Loc] : Children) {
          llvm::dbgs() << Field->getNameAsString() << "\n";
        }
      }
    });
    assert(It != Children.end());
    return It->second;
  }

  /// Returns the storage location for the synthetic field `Name`.
  /// The synthetic field must exist.
  StorageLocation &getSyntheticField(llvm::StringRef Name) const {
    StorageLocation *Loc = SyntheticFields.lookup(Name);
    assert(Loc != nullptr);
    return *Loc;
  }

  llvm::iterator_range<SyntheticFieldMap::const_iterator>
  synthetic_fields() const {
    return {SyntheticFields.begin(), SyntheticFields.end()};
  }

  /// Changes the child storage location for a field `D` of reference type.
  /// All other fields cannot change their storage location and always retain
  /// the storage location passed to the `RecordStorageLocation` constructor.
  ///
  /// Requirements:
  ///
  ///  `D` must have reference type.
  void setChild(const ValueDecl &D, StorageLocation *Loc) {
    assert(D.getType()->isReferenceType());
    Children[&D] = Loc;
  }

  llvm::iterator_range<FieldToLoc::const_iterator> children() const {
    return {Children.begin(), Children.end()};
  }

private:
  FieldToLoc Children;
  SyntheticFieldMap SyntheticFields;
};

} // namespace dataflow
} // namespace clang

#undef DEBUG_TYPE

#endif // LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_STORAGELOCATION_H
