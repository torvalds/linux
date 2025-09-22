//===- SymbolicFile.h - Interface that only provides symbols ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the SymbolicFile interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_SYMBOLICFILE_H
#define LLVM_OBJECT_SYMBOLICFILE_H

#include "llvm/ADT/iterator_range.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Object/Binary.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MemoryBufferRef.h"
#include <cinttypes>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>

namespace llvm {

class LLVMContext;
class raw_ostream;

namespace object {

union DataRefImpl {
  // This entire union should probably be a
  // char[max(8, sizeof(uintptr_t))] and require the impl to cast.
  struct {
    uint32_t a, b;
  } d;
  uintptr_t p;

  DataRefImpl() { std::memset(this, 0, sizeof(DataRefImpl)); }
};

template <typename OStream>
OStream& operator<<(OStream &OS, const DataRefImpl &D) {
  OS << "(" << format("0x%08" PRIxPTR, D.p) << " (" << format("0x%08x", D.d.a)
     << ", " << format("0x%08x", D.d.b) << "))";
  return OS;
}

inline bool operator==(const DataRefImpl &a, const DataRefImpl &b) {
  // Check bitwise identical. This is the only legal way to compare a union w/o
  // knowing which member is in use.
  return std::memcmp(&a, &b, sizeof(DataRefImpl)) == 0;
}

inline bool operator!=(const DataRefImpl &a, const DataRefImpl &b) {
  return !operator==(a, b);
}

inline bool operator<(const DataRefImpl &a, const DataRefImpl &b) {
  // Check bitwise identical. This is the only legal way to compare a union w/o
  // knowing which member is in use.
  return std::memcmp(&a, &b, sizeof(DataRefImpl)) < 0;
}

template <class content_type> class content_iterator {
  content_type Current;

public:
  using iterator_category = std::forward_iterator_tag;
  using value_type = content_type;
  using difference_type = std::ptrdiff_t;
  using pointer = value_type *;
  using reference = value_type &;

  content_iterator(content_type symb) : Current(std::move(symb)) {}

  const content_type *operator->() const { return &Current; }

  const content_type &operator*() const { return Current; }

  bool operator==(const content_iterator &other) const {
    return Current == other.Current;
  }

  bool operator!=(const content_iterator &other) const {
    return !(*this == other);
  }

  content_iterator &operator++() { // preincrement
    Current.moveNext();
    return *this;
  }
};

class SymbolicFile;

/// This is a value type class that represents a single symbol in the list of
/// symbols in the object file.
class BasicSymbolRef {
  DataRefImpl SymbolPimpl;
  const SymbolicFile *OwningObject = nullptr;

public:
  enum Flags : unsigned {
    SF_None = 0,
    SF_Undefined = 1U << 0,      // Symbol is defined in another object file
    SF_Global = 1U << 1,         // Global symbol
    SF_Weak = 1U << 2,           // Weak symbol
    SF_Absolute = 1U << 3,       // Absolute symbol
    SF_Common = 1U << 4,         // Symbol has common linkage
    SF_Indirect = 1U << 5,       // Symbol is an alias to another symbol
    SF_Exported = 1U << 6,       // Symbol is visible to other DSOs
    SF_FormatSpecific = 1U << 7, // Specific to the object file format
                                 // (e.g. section symbols)
    SF_Thumb = 1U << 8,          // Thumb symbol in a 32-bit ARM binary
    SF_Hidden = 1U << 9,         // Symbol has hidden visibility
    SF_Const = 1U << 10,         // Symbol value is constant
    SF_Executable = 1U << 11,    // Symbol points to an executable section
                                 // (IR only)
  };

  BasicSymbolRef() = default;
  BasicSymbolRef(DataRefImpl SymbolP, const SymbolicFile *Owner);

  bool operator==(const BasicSymbolRef &Other) const;
  bool operator<(const BasicSymbolRef &Other) const;

  void moveNext();

  Error printName(raw_ostream &OS) const;

  /// Get symbol flags (bitwise OR of SymbolRef::Flags)
  Expected<uint32_t> getFlags() const;

  DataRefImpl getRawDataRefImpl() const;
  const SymbolicFile *getObject() const;
};

using basic_symbol_iterator = content_iterator<BasicSymbolRef>;

class SymbolicFile : public Binary {
public:
  SymbolicFile(unsigned int Type, MemoryBufferRef Source);
  ~SymbolicFile() override;

  // virtual interface.
  virtual void moveSymbolNext(DataRefImpl &Symb) const = 0;

  virtual Error printSymbolName(raw_ostream &OS, DataRefImpl Symb) const = 0;

  virtual Expected<uint32_t> getSymbolFlags(DataRefImpl Symb) const = 0;

  virtual basic_symbol_iterator symbol_begin() const = 0;

  virtual basic_symbol_iterator symbol_end() const = 0;

  virtual bool is64Bit() const = 0;

  // convenience wrappers.
  using basic_symbol_iterator_range = iterator_range<basic_symbol_iterator>;
  basic_symbol_iterator_range symbols() const {
    return basic_symbol_iterator_range(symbol_begin(), symbol_end());
  }

  // construction aux.
  static Expected<std::unique_ptr<SymbolicFile>>
  createSymbolicFile(MemoryBufferRef Object, llvm::file_magic Type,
                     LLVMContext *Context, bool InitContent = true);

  static Expected<std::unique_ptr<SymbolicFile>>
  createSymbolicFile(MemoryBufferRef Object) {
    return createSymbolicFile(Object, llvm::file_magic::unknown, nullptr);
  }

  static bool classof(const Binary *v) {
    return v->isSymbolic();
  }

  static bool isSymbolicFile(file_magic Type, const LLVMContext *Context);
};

inline BasicSymbolRef::BasicSymbolRef(DataRefImpl SymbolP,
                                      const SymbolicFile *Owner)
    : SymbolPimpl(SymbolP), OwningObject(Owner) {}

inline bool BasicSymbolRef::operator==(const BasicSymbolRef &Other) const {
  return SymbolPimpl == Other.SymbolPimpl;
}

inline bool BasicSymbolRef::operator<(const BasicSymbolRef &Other) const {
  return SymbolPimpl < Other.SymbolPimpl;
}

inline void BasicSymbolRef::moveNext() {
  return OwningObject->moveSymbolNext(SymbolPimpl);
}

inline Error BasicSymbolRef::printName(raw_ostream &OS) const {
  return OwningObject->printSymbolName(OS, SymbolPimpl);
}

inline Expected<uint32_t> BasicSymbolRef::getFlags() const {
  return OwningObject->getSymbolFlags(SymbolPimpl);
}

inline DataRefImpl BasicSymbolRef::getRawDataRefImpl() const {
  return SymbolPimpl;
}

inline const SymbolicFile *BasicSymbolRef::getObject() const {
  return OwningObject;
}

} // end namespace object
} // end namespace llvm

#endif // LLVM_OBJECT_SYMBOLICFILE_H
