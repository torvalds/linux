//===- ExternalPreprocessorSource.h - Abstract Macro Interface --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ExternalPreprocessorSource interface, which enables
//  construction of macro definitions from some external source.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_LEX_EXTERNALPREPROCESSORSOURCE_H
#define LLVM_CLANG_LEX_EXTERNALPREPROCESSORSOURCE_H

#include <cassert>
#include <cstdint>
  
namespace clang {

class IdentifierInfo;
class Module;

/// Abstract interface for external sources of preprocessor
/// information.
///
/// This abstract class allows an external sources (such as the \c ASTReader)
/// to provide additional preprocessing information.
class ExternalPreprocessorSource {
public:
  virtual ~ExternalPreprocessorSource();

  /// Read the set of macros defined by this external macro source.
  virtual void ReadDefinedMacros() = 0;

  /// Update an out-of-date identifier.
  virtual void updateOutOfDateIdentifier(const IdentifierInfo &II) = 0;

  /// Return the identifier associated with the given ID number.
  ///
  /// The ID 0 is associated with the NULL identifier.
  virtual IdentifierInfo *GetIdentifier(uint64_t ID) = 0;

  /// Map a module ID to a module.
  virtual Module *getModule(unsigned ModuleID) = 0;
};

// Either a pointer to an IdentifierInfo of the controlling macro or the ID
// number of the controlling macro.
class LazyIdentifierInfoPtr {
  // If the low bit is clear, a pointer to the IdentifierInfo. If the low
  // bit is set, the upper 63 bits are the ID number.
  mutable uint64_t Ptr = 0;

public:
  LazyIdentifierInfoPtr() = default;

  explicit LazyIdentifierInfoPtr(const IdentifierInfo *Ptr)
      : Ptr(reinterpret_cast<uint64_t>(Ptr)) {}

  explicit LazyIdentifierInfoPtr(uint64_t ID) : Ptr((ID << 1) | 0x01) {
    assert((ID << 1 >> 1) == ID && "ID must require < 63 bits");
    if (ID == 0)
      Ptr = 0;
  }

  LazyIdentifierInfoPtr &operator=(const IdentifierInfo *Ptr) {
    this->Ptr = reinterpret_cast<uint64_t>(Ptr);
    return *this;
  }

  LazyIdentifierInfoPtr &operator=(uint64_t ID) {
    assert((ID << 1 >> 1) == ID && "IDs must require < 63 bits");
    if (ID == 0)
      Ptr = 0;
    else
      Ptr = (ID << 1) | 0x01;

    return *this;
  }

  /// Whether this pointer is non-NULL.
  ///
  /// This operation does not require the AST node to be deserialized.
  bool isValid() const { return Ptr != 0; }

  /// Whether this pointer is currently stored as ID.
  bool isID() const { return Ptr & 0x01; }

  IdentifierInfo *getPtr() const {
    assert(!isID());
    return reinterpret_cast<IdentifierInfo *>(Ptr);
  }

  uint64_t getID() const {
    assert(isID());
    return Ptr >> 1;
  }
};
}

#endif
