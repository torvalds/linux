//===- TypeIndex.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_TYPEINDEX_H
#define LLVM_DEBUGINFO_CODEVIEW_TYPEINDEX_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/Support/Endian.h"
#include <cassert>
#include <cinttypes>

namespace llvm {

class ScopedPrinter;
class StringRef;

namespace codeview {

class TypeCollection;

enum class SimpleTypeKind : uint32_t {
  None = 0x0000,          // uncharacterized type (no type)
  Void = 0x0003,          // void
  NotTranslated = 0x0007, // type not translated by cvpack
  HResult = 0x0008,       // OLE/COM HRESULT

  SignedCharacter = 0x0010,   // 8 bit signed
  UnsignedCharacter = 0x0020, // 8 bit unsigned
  NarrowCharacter = 0x0070,   // really a char
  WideCharacter = 0x0071,     // wide char
  Character16 = 0x007a,       // char16_t
  Character32 = 0x007b,       // char32_t
  Character8 = 0x007c,        // char8_t

  SByte = 0x0068,       // 8 bit signed int
  Byte = 0x0069,        // 8 bit unsigned int
  Int16Short = 0x0011,  // 16 bit signed
  UInt16Short = 0x0021, // 16 bit unsigned
  Int16 = 0x0072,       // 16 bit signed int
  UInt16 = 0x0073,      // 16 bit unsigned int
  Int32Long = 0x0012,   // 32 bit signed
  UInt32Long = 0x0022,  // 32 bit unsigned
  Int32 = 0x0074,       // 32 bit signed int
  UInt32 = 0x0075,      // 32 bit unsigned int
  Int64Quad = 0x0013,   // 64 bit signed
  UInt64Quad = 0x0023,  // 64 bit unsigned
  Int64 = 0x0076,       // 64 bit signed int
  UInt64 = 0x0077,      // 64 bit unsigned int
  Int128Oct = 0x0014,   // 128 bit signed int
  UInt128Oct = 0x0024,  // 128 bit unsigned int
  Int128 = 0x0078,      // 128 bit signed int
  UInt128 = 0x0079,     // 128 bit unsigned int

  Float16 = 0x0046,                 // 16 bit real
  Float32 = 0x0040,                 // 32 bit real
  Float32PartialPrecision = 0x0045, // 32 bit PP real
  Float48 = 0x0044,                 // 48 bit real
  Float64 = 0x0041,                 // 64 bit real
  Float80 = 0x0042,                 // 80 bit real
  Float128 = 0x0043,                // 128 bit real

  Complex16 = 0x0056,                 // 16 bit complex
  Complex32 = 0x0050,                 // 32 bit complex
  Complex32PartialPrecision = 0x0055, // 32 bit PP complex
  Complex48 = 0x0054,                 // 48 bit complex
  Complex64 = 0x0051,                 // 64 bit complex
  Complex80 = 0x0052,                 // 80 bit complex
  Complex128 = 0x0053,                // 128 bit complex

  Boolean8 = 0x0030,   // 8 bit boolean
  Boolean16 = 0x0031,  // 16 bit boolean
  Boolean32 = 0x0032,  // 32 bit boolean
  Boolean64 = 0x0033,  // 64 bit boolean
  Boolean128 = 0x0034, // 128 bit boolean
};

enum class SimpleTypeMode : uint32_t {
  Direct = 0x00000000,        // Not a pointer
  NearPointer = 0x00000100,   // Near pointer
  FarPointer = 0x00000200,    // Far pointer
  HugePointer = 0x00000300,   // Huge pointer
  NearPointer32 = 0x00000400, // 32 bit near pointer
  FarPointer32 = 0x00000500,  // 32 bit far pointer
  NearPointer64 = 0x00000600, // 64 bit near pointer
  NearPointer128 = 0x00000700 // 128 bit near pointer
};

/// A 32-bit type reference. Types are indexed by their order of appearance in
/// .debug$T plus 0x1000. Type indices less than 0x1000 are "simple" types,
/// composed of a SimpleTypeMode byte followed by a SimpleTypeKind byte.
class TypeIndex {
public:
  static const uint32_t FirstNonSimpleIndex = 0x1000;
  static const uint32_t SimpleKindMask = 0x000000ff;
  static const uint32_t SimpleModeMask = 0x00000700;
  static const uint32_t DecoratedItemIdMask = 0x80000000;

public:
  TypeIndex() : Index(static_cast<uint32_t>(SimpleTypeKind::None)) {}
  explicit TypeIndex(uint32_t Index) : Index(Index) {}
  explicit TypeIndex(SimpleTypeKind Kind)
      : Index(static_cast<uint32_t>(Kind)) {}
  TypeIndex(SimpleTypeKind Kind, SimpleTypeMode Mode)
      : Index(static_cast<uint32_t>(Kind) | static_cast<uint32_t>(Mode)) {}

  uint32_t getIndex() const { return Index; }
  void setIndex(uint32_t I) { Index = I; }
  bool isSimple() const { return Index < FirstNonSimpleIndex; }
  bool isDecoratedItemId() const { return !!(Index & DecoratedItemIdMask); }

  bool isNoneType() const { return *this == None(); }

  uint32_t toArrayIndex() const {
    assert(!isSimple());
    return (getIndex() & ~DecoratedItemIdMask) - FirstNonSimpleIndex;
  }

  static TypeIndex fromArrayIndex(uint32_t Index) {
    return TypeIndex(Index + FirstNonSimpleIndex);
  }

  static TypeIndex fromDecoratedArrayIndex(bool IsItem, uint32_t Index) {
    return TypeIndex((Index + FirstNonSimpleIndex) |
                     (IsItem ? DecoratedItemIdMask : 0));
  }

  TypeIndex removeDecoration() {
    return TypeIndex(Index & ~DecoratedItemIdMask);
  }

  SimpleTypeKind getSimpleKind() const {
    assert(isSimple());
    return static_cast<SimpleTypeKind>(Index & SimpleKindMask);
  }

  SimpleTypeMode getSimpleMode() const {
    assert(isSimple());
    return static_cast<SimpleTypeMode>(Index & SimpleModeMask);
  }

  TypeIndex makeDirect() const { return TypeIndex{getSimpleKind()}; }

  static TypeIndex None() { return TypeIndex(SimpleTypeKind::None); }
  static TypeIndex Void() { return TypeIndex(SimpleTypeKind::Void); }
  static TypeIndex VoidPointer32() {
    return TypeIndex(SimpleTypeKind::Void, SimpleTypeMode::NearPointer32);
  }
  static TypeIndex VoidPointer64() {
    return TypeIndex(SimpleTypeKind::Void, SimpleTypeMode::NearPointer64);
  }

  static TypeIndex NullptrT() {
    // std::nullptr_t uses the pointer mode that doesn't indicate bit-width,
    // presumably because std::nullptr_t is intended to be compatible with any
    // pointer type.
    return TypeIndex(SimpleTypeKind::Void, SimpleTypeMode::NearPointer);
  }

  static TypeIndex SignedCharacter() {
    return TypeIndex(SimpleTypeKind::SignedCharacter);
  }
  static TypeIndex UnsignedCharacter() {
    return TypeIndex(SimpleTypeKind::UnsignedCharacter);
  }
  static TypeIndex NarrowCharacter() {
    return TypeIndex(SimpleTypeKind::NarrowCharacter);
  }
  static TypeIndex WideCharacter() {
    return TypeIndex(SimpleTypeKind::WideCharacter);
  }
  static TypeIndex Int16Short() {
    return TypeIndex(SimpleTypeKind::Int16Short);
  }
  static TypeIndex UInt16Short() {
    return TypeIndex(SimpleTypeKind::UInt16Short);
  }
  static TypeIndex Int32() { return TypeIndex(SimpleTypeKind::Int32); }
  static TypeIndex UInt32() { return TypeIndex(SimpleTypeKind::UInt32); }
  static TypeIndex Int32Long() { return TypeIndex(SimpleTypeKind::Int32Long); }
  static TypeIndex UInt32Long() {
    return TypeIndex(SimpleTypeKind::UInt32Long);
  }
  static TypeIndex Int64() { return TypeIndex(SimpleTypeKind::Int64); }
  static TypeIndex UInt64() { return TypeIndex(SimpleTypeKind::UInt64); }
  static TypeIndex Int64Quad() { return TypeIndex(SimpleTypeKind::Int64Quad); }
  static TypeIndex UInt64Quad() {
    return TypeIndex(SimpleTypeKind::UInt64Quad);
  }

  static TypeIndex Float32() { return TypeIndex(SimpleTypeKind::Float32); }
  static TypeIndex Float64() { return TypeIndex(SimpleTypeKind::Float64); }

  TypeIndex &operator+=(unsigned N) {
    Index += N;
    return *this;
  }

  TypeIndex &operator++() {
    Index += 1;
    return *this;
  }

  TypeIndex operator++(int) {
    TypeIndex Copy = *this;
    operator++();
    return Copy;
  }

  TypeIndex &operator-=(unsigned N) {
    assert(Index >= N);
    Index -= N;
    return *this;
  }

  TypeIndex &operator--() {
    Index -= 1;
    return *this;
  }

  TypeIndex operator--(int) {
    TypeIndex Copy = *this;
    operator--();
    return Copy;
  }

  friend inline bool operator==(const TypeIndex &A, const TypeIndex &B) {
    return A.getIndex() == B.getIndex();
  }

  friend inline bool operator!=(const TypeIndex &A, const TypeIndex &B) {
    return A.getIndex() != B.getIndex();
  }

  friend inline bool operator<(const TypeIndex &A, const TypeIndex &B) {
    return A.getIndex() < B.getIndex();
  }

  friend inline bool operator<=(const TypeIndex &A, const TypeIndex &B) {
    return A.getIndex() <= B.getIndex();
  }

  friend inline bool operator>(const TypeIndex &A, const TypeIndex &B) {
    return A.getIndex() > B.getIndex();
  }

  friend inline bool operator>=(const TypeIndex &A, const TypeIndex &B) {
    return A.getIndex() >= B.getIndex();
  }

  friend inline TypeIndex operator+(const TypeIndex &A, uint32_t N) {
    TypeIndex Result(A);
    Result += N;
    return Result;
  }

  friend inline TypeIndex operator-(const TypeIndex &A, uint32_t N) {
    assert(A.getIndex() >= N);
    TypeIndex Result(A);
    Result -= N;
    return Result;
  }

  friend inline uint32_t operator-(const TypeIndex &A, const TypeIndex &B) {
    assert(A >= B);
    return A.toArrayIndex() - B.toArrayIndex();
  }

  static StringRef simpleTypeName(TypeIndex TI);

private:
  support::ulittle32_t Index;
};

// Used for pseudo-indexing an array of type records.  An array of such records
// sorted by TypeIndex can allow log(N) lookups even though such a type record
// stream does not provide random access.
struct TypeIndexOffset {
  TypeIndex Type;
  support::ulittle32_t Offset;
};

void printTypeIndex(ScopedPrinter &Printer, StringRef FieldName, TypeIndex TI,
                    TypeCollection &Types);
}

template <> struct DenseMapInfo<codeview::TypeIndex> {
  static inline codeview::TypeIndex getEmptyKey() {
    return codeview::TypeIndex{DenseMapInfo<uint32_t>::getEmptyKey()};
  }
  static inline codeview::TypeIndex getTombstoneKey() {
    return codeview::TypeIndex{DenseMapInfo<uint32_t>::getTombstoneKey()};
  }
  static unsigned getHashValue(const codeview::TypeIndex &TI) {
    return DenseMapInfo<uint32_t>::getHashValue(TI.getIndex());
  }
  static bool isEqual(const codeview::TypeIndex &LHS,
                      const codeview::TypeIndex &RHS) {
    return LHS == RHS;
  }
};

} // namespace llvm

#endif
