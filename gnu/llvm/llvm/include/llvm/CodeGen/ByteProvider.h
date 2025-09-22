//===-- include/llvm/CodeGen/ByteProvider.h - Map bytes ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// \file
// This file implements ByteProvider. The purpose of ByteProvider is to provide
// a map between a target node's byte (byte position is DestOffset) and the
// source (and byte position) that provides it (in Src and SrcOffset
// respectively) See CodeGen/SelectionDAG/DAGCombiner.cpp MatchLoadCombine
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_BYTEPROVIDER_H
#define LLVM_CODEGEN_BYTEPROVIDER_H

#include <optional>
#include <type_traits>

namespace llvm {

/// Represents known origin of an individual byte in combine pattern. The
/// value of the byte is either constant zero, or comes from memory /
/// some other productive instruction (e.g. arithmetic instructions).
/// Bit manipulation instructions like shifts are not ByteProviders, rather
/// are used to extract Bytes.
template <typename ISelOp> class ByteProvider {
private:
  ByteProvider(std::optional<ISelOp> Src, int64_t DestOffset, int64_t SrcOffset)
      : Src(Src), DestOffset(DestOffset), SrcOffset(SrcOffset) {}

  // TODO -- use constraint in c++20
  // Does this type correspond with an operation in selection DAG
  template <typename T> class is_op {
  private:
    using yes = std::true_type;
    using no = std::false_type;

    // Only allow classes with member function getOpcode
    template <typename U>
    static auto test(int) -> decltype(std::declval<U>().getOpcode(), yes());

    template <typename> static no test(...);

  public:
    using remove_pointer_t = typename std::remove_pointer<T>::type;
    static constexpr bool value =
        std::is_same<decltype(test<remove_pointer_t>(0)), yes>::value;
  };

public:
  // For constant zero providers Src is set to nullopt. For actual providers
  // Src represents the node which originally produced the relevant bits.
  std::optional<ISelOp> Src = std::nullopt;
  // DestOffset is the offset of the byte in the dest we are trying to map for.
  int64_t DestOffset = 0;
  // SrcOffset is the offset in the ultimate source node that maps to the
  // DestOffset
  int64_t SrcOffset = 0;

  ByteProvider() = default;

  static ByteProvider getSrc(std::optional<ISelOp> Val, int64_t ByteOffset,
                             int64_t VectorOffset) {
    static_assert(is_op<ISelOp>().value,
                  "ByteProviders must contain an operation in selection DAG.");
    return ByteProvider(Val, ByteOffset, VectorOffset);
  }

  static ByteProvider getConstantZero() {
    return ByteProvider<ISelOp>(std::nullopt, 0, 0);
  }
  bool isConstantZero() const { return !Src; }

  bool hasSrc() const { return Src.has_value(); }

  bool hasSameSrc(const ByteProvider &Other) const { return Other.Src == Src; }

  bool operator==(const ByteProvider &Other) const {
    return Other.Src == Src && Other.DestOffset == DestOffset &&
           Other.SrcOffset == SrcOffset;
  }
};
} // end namespace llvm

#endif // LLVM_CODEGEN_BYTEPROVIDER_H
