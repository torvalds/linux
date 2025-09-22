//===--- InterpStack.h - Stack implementation for the VM --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines the upwards-growing stack used by the interpreter.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_INTERP_INTERPSTACK_H
#define LLVM_CLANG_AST_INTERP_INTERPSTACK_H

#include "FunctionPointer.h"
#include "IntegralAP.h"
#include "MemberPointer.h"
#include "PrimType.h"
#include <memory>
#include <vector>

namespace clang {
namespace interp {

/// Stack frame storing temporaries and parameters.
class InterpStack final {
public:
  InterpStack() {}

  /// Destroys the stack, freeing up storage.
  ~InterpStack();

  /// Constructs a value in place on the top of the stack.
  template <typename T, typename... Tys> void push(Tys &&... Args) {
    new (grow(aligned_size<T>())) T(std::forward<Tys>(Args)...);
#ifndef NDEBUG
    ItemTypes.push_back(toPrimType<T>());
#endif
  }

  /// Returns the value from the top of the stack and removes it.
  template <typename T> T pop() {
#ifndef NDEBUG
    assert(!ItemTypes.empty());
    assert(ItemTypes.back() == toPrimType<T>());
    ItemTypes.pop_back();
#endif
    T *Ptr = &peekInternal<T>();
    T Value = std::move(*Ptr);
    shrink(aligned_size<T>());
    return Value;
  }

  /// Discards the top value from the stack.
  template <typename T> void discard() {
#ifndef NDEBUG
    assert(!ItemTypes.empty());
    assert(ItemTypes.back() == toPrimType<T>());
    ItemTypes.pop_back();
#endif
    T *Ptr = &peekInternal<T>();
    Ptr->~T();
    shrink(aligned_size<T>());
  }

  /// Returns a reference to the value on the top of the stack.
  template <typename T> T &peek() const {
#ifndef NDEBUG
    assert(!ItemTypes.empty());
    assert(ItemTypes.back() == toPrimType<T>());
#endif
    return peekInternal<T>();
  }

  template <typename T> T &peek(size_t Offset) const {
    assert(aligned(Offset));
    return *reinterpret_cast<T *>(peekData(Offset));
  }

  /// Returns a pointer to the top object.
  void *top() const { return Chunk ? peekData(0) : nullptr; }

  /// Returns the size of the stack in bytes.
  size_t size() const { return StackSize; }

  /// Clears the stack without calling any destructors.
  void clear();

  /// Returns whether the stack is empty.
  bool empty() const { return StackSize == 0; }

  /// dump the stack contents to stderr.
  void dump() const;

private:
  /// All stack slots are aligned to the native pointer alignment for storage.
  /// The size of an object is rounded up to a pointer alignment multiple.
  template <typename T> constexpr size_t aligned_size() const {
    constexpr size_t PtrAlign = alignof(void *);
    return ((sizeof(T) + PtrAlign - 1) / PtrAlign) * PtrAlign;
  }

  /// Like the public peek(), but without the debug type checks.
  template <typename T> T &peekInternal() const {
    return *reinterpret_cast<T *>(peekData(aligned_size<T>()));
  }

  /// Grows the stack to accommodate a value and returns a pointer to it.
  void *grow(size_t Size);
  /// Returns a pointer from the top of the stack.
  void *peekData(size_t Size) const;
  /// Shrinks the stack.
  void shrink(size_t Size);

  /// Allocate stack space in 1Mb chunks.
  static constexpr size_t ChunkSize = 1024 * 1024;

  /// Metadata for each stack chunk.
  ///
  /// The stack is composed of a linked list of chunks. Whenever an allocation
  /// is out of bounds, a new chunk is linked. When a chunk becomes empty,
  /// it is not immediately freed: a chunk is deallocated only when the
  /// predecessor becomes empty.
  struct StackChunk {
    StackChunk *Next;
    StackChunk *Prev;
    char *End;

    StackChunk(StackChunk *Prev = nullptr)
        : Next(nullptr), Prev(Prev), End(reinterpret_cast<char *>(this + 1)) {}

    /// Returns the size of the chunk, minus the header.
    size_t size() const { return End - start(); }

    /// Returns a pointer to the start of the data region.
    char *start() { return reinterpret_cast<char *>(this + 1); }
    const char *start() const {
      return reinterpret_cast<const char *>(this + 1);
    }
  };
  static_assert(sizeof(StackChunk) < ChunkSize, "Invalid chunk size");

  /// First chunk on the stack.
  StackChunk *Chunk = nullptr;
  /// Total size of the stack.
  size_t StackSize = 0;

#ifndef NDEBUG
  /// vector recording the type of data we pushed into the stack.
  std::vector<PrimType> ItemTypes;

  template <typename T> static constexpr PrimType toPrimType() {
    if constexpr (std::is_same_v<T, Pointer>)
      return PT_Ptr;
    else if constexpr (std::is_same_v<T, bool> ||
                       std::is_same_v<T, Boolean>)
      return PT_Bool;
    else if constexpr (std::is_same_v<T, int8_t> ||
                       std::is_same_v<T, Integral<8, true>>)
      return PT_Sint8;
    else if constexpr (std::is_same_v<T, uint8_t> ||
                       std::is_same_v<T, Integral<8, false>>)
      return PT_Uint8;
    else if constexpr (std::is_same_v<T, int16_t> ||
                       std::is_same_v<T, Integral<16, true>>)
      return PT_Sint16;
    else if constexpr (std::is_same_v<T, uint16_t> ||
                       std::is_same_v<T, Integral<16, false>>)
      return PT_Uint16;
    else if constexpr (std::is_same_v<T, int32_t> ||
                       std::is_same_v<T, Integral<32, true>>)
      return PT_Sint32;
    else if constexpr (std::is_same_v<T, uint32_t> ||
                       std::is_same_v<T, Integral<32, false>>)
      return PT_Uint32;
    else if constexpr (std::is_same_v<T, int64_t> ||
                       std::is_same_v<T, Integral<64, true>>)
      return PT_Sint64;
    else if constexpr (std::is_same_v<T, uint64_t> ||
                       std::is_same_v<T, Integral<64, false>>)
      return PT_Uint64;
    else if constexpr (std::is_same_v<T, Floating>)
      return PT_Float;
    else if constexpr (std::is_same_v<T, FunctionPointer>)
      return PT_FnPtr;
    else if constexpr (std::is_same_v<T, IntegralAP<true>>)
      return PT_IntAP;
    else if constexpr (std::is_same_v<T, IntegralAP<false>>)
      return PT_IntAP;
    else if constexpr (std::is_same_v<T, MemberPointer>)
      return PT_MemberPtr;

    llvm_unreachable("unknown type push()'ed into InterpStack");
  }
#endif
};

} // namespace interp
} // namespace clang

#endif
