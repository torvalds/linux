//===--- Record.h - struct and class metadata for the VM --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A record is part of a program to describe the layout and methods of a struct.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_INTERP_RECORD_H
#define LLVM_CLANG_AST_INTERP_RECORD_H

#include "Descriptor.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"

namespace clang {
namespace interp {
class Program;

/// Structure/Class descriptor.
class Record final {
public:
  /// Describes a record field.
  struct Field {
    const FieldDecl *Decl;
    unsigned Offset;
    const Descriptor *Desc;
    bool isBitField() const { return Decl->isBitField(); }
  };

  /// Describes a base class.
  struct Base {
    const RecordDecl *Decl;
    unsigned Offset;
    const Descriptor *Desc;
    const Record *R;
  };

  /// Mapping from identifiers to field descriptors.
  using FieldList = llvm::SmallVector<Field, 8>;
  /// Mapping from identifiers to base classes.
  using BaseList = llvm::SmallVector<Base, 8>;
  /// List of virtual base classes.
  using VirtualBaseList = llvm::SmallVector<Base, 2>;

public:
  /// Returns the underlying declaration.
  const RecordDecl *getDecl() const { return Decl; }
  /// Returns the name of the underlying declaration.
  const std::string getName() const;
  /// Checks if the record is a union.
  bool isUnion() const { return IsUnion; }
  /// Returns the size of the record.
  unsigned getSize() const { return BaseSize; }
  /// Returns the full size of the record, including records.
  unsigned getFullSize() const { return BaseSize + VirtualSize; }
  /// Returns a field.
  const Field *getField(const FieldDecl *FD) const;
  /// Returns a base descriptor.
  const Base *getBase(const RecordDecl *FD) const;
  /// Returns a base descriptor.
  const Base *getBase(QualType T) const;
  /// Returns a virtual base descriptor.
  const Base *getVirtualBase(const RecordDecl *RD) const;
  /// Returns the destructor of the record, if any.
  const CXXDestructorDecl *getDestructor() const {
    if (const auto *CXXDecl = dyn_cast<CXXRecordDecl>(Decl))
      return CXXDecl->getDestructor();
    return nullptr;
  }

  using const_field_iter = FieldList::const_iterator;
  llvm::iterator_range<const_field_iter> fields() const {
    return llvm::make_range(Fields.begin(), Fields.end());
  }

  unsigned getNumFields() const { return Fields.size(); }
  const Field *getField(unsigned I) const { return &Fields[I]; }

  using const_base_iter = BaseList::const_iterator;
  llvm::iterator_range<const_base_iter> bases() const {
    return llvm::make_range(Bases.begin(), Bases.end());
  }

  unsigned getNumBases() const { return Bases.size(); }
  const Base *getBase(unsigned I) const {
    assert(I < getNumBases());
    return &Bases[I];
  }

  using const_virtual_iter = VirtualBaseList::const_iterator;
  llvm::iterator_range<const_virtual_iter> virtual_bases() const {
    return llvm::make_range(VirtualBases.begin(), VirtualBases.end());
  }

  unsigned getNumVirtualBases() const { return VirtualBases.size(); }
  const Base *getVirtualBase(unsigned I) const { return &VirtualBases[I]; }

  void dump(llvm::raw_ostream &OS, unsigned Indentation = 0,
            unsigned Offset = 0) const;
  void dump() const { dump(llvm::errs()); }

private:
  /// Constructor used by Program to create record descriptors.
  Record(const RecordDecl *, BaseList &&Bases, FieldList &&Fields,
         VirtualBaseList &&VirtualBases, unsigned VirtualSize,
         unsigned BaseSize);

private:
  friend class Program;

  /// Original declaration.
  const RecordDecl *Decl;
  /// List of all base classes.
  BaseList Bases;
  /// List of all the fields in the record.
  FieldList Fields;
  /// List o fall virtual bases.
  VirtualBaseList VirtualBases;

  /// Mapping from declarations to bases.
  llvm::DenseMap<const RecordDecl *, const Base *> BaseMap;
  /// Mapping from field identifiers to descriptors.
  llvm::DenseMap<const FieldDecl *, const Field *> FieldMap;
  /// Mapping from declarations to virtual bases.
  llvm::DenseMap<const RecordDecl *, Base *> VirtualBaseMap;
  /// Size of the structure.
  unsigned BaseSize;
  /// Size of all virtual bases.
  unsigned VirtualSize;
  /// If this record is a union.
  bool IsUnion;
};

} // namespace interp
} // namespace clang

#endif
