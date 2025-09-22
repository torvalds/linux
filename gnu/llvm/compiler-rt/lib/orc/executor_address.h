//===------ ExecutorAddress.h - Executing process address -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Represents an address in the executing program.
//
// This file was derived from
// llvm/include/llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h.
//
//===----------------------------------------------------------------------===//

#ifndef ORC_RT_EXECUTOR_ADDRESS_H
#define ORC_RT_EXECUTOR_ADDRESS_H

#include "adt.h"
#include "simple_packed_serialization.h"

#include <cassert>
#include <type_traits>

namespace __orc_rt {

using ExecutorAddrDiff = uint64_t;

/// Represents an address in the executor process.
class ExecutorAddr {
public:
  /// A wrap/unwrap function that leaves pointers unmodified.
  template <typename T> using rawPtr = __orc_rt::identity<T *>;

  /// Default wrap function to use on this host.
  template <typename T> using defaultWrap = rawPtr<T>;

  /// Default unwrap function to use on this host.
  template <typename T> using defaultUnwrap = rawPtr<T>;

  /// Merges a tag into the raw address value:
  ///   P' = P | (TagValue << TagOffset).
  class Tag {
  public:
    constexpr Tag(uintptr_t TagValue, uintptr_t TagOffset)
        : TagMask(TagValue << TagOffset) {}

    template <typename T> constexpr T *operator()(T *P) {
      return reinterpret_cast<T *>(reinterpret_cast<uintptr_t>(P) | TagMask);
    }

  private:
    uintptr_t TagMask;
  };

  /// Strips a tag of the given length from the given offset within the pointer:
  /// P' = P & ~(((1 << TagLen) -1) << TagOffset)
  class Untag {
  public:
    constexpr Untag(uintptr_t TagLen, uintptr_t TagOffset)
        : UntagMask(~(((uintptr_t(1) << TagLen) - 1) << TagOffset)) {}

    template <typename T> constexpr T *operator()(T *P) {
      return reinterpret_cast<T *>(reinterpret_cast<uintptr_t>(P) & UntagMask);
    }

  private:
    uintptr_t UntagMask;
  };

  ExecutorAddr() = default;
  explicit ExecutorAddr(uint64_t Addr) : Addr(Addr) {}

  /// Create an ExecutorAddr from the given pointer.
  template <typename T, typename UnwrapFn = defaultUnwrap<T>>
  static ExecutorAddr fromPtr(T *Ptr, UnwrapFn &&Unwrap = UnwrapFn()) {
    return ExecutorAddr(
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(Unwrap(Ptr))));
  }

  /// Cast this ExecutorAddr to a pointer of the given type.
  template <typename T, typename WrapFn = defaultWrap<std::remove_pointer_t<T>>>
  std::enable_if_t<std::is_pointer<T>::value, T>
  toPtr(WrapFn &&Wrap = WrapFn()) const {
    uintptr_t IntPtr = static_cast<uintptr_t>(Addr);
    assert(IntPtr == Addr && "ExecutorAddr value out of range for uintptr_t");
    return Wrap(reinterpret_cast<T>(IntPtr));
  }

  /// Cast this ExecutorAddr to a pointer of the given function type.
  template <typename T, typename WrapFn = defaultWrap<T>>
  std::enable_if_t<std::is_function<T>::value, T *>
  toPtr(WrapFn &&Wrap = WrapFn()) const {
    uintptr_t IntPtr = static_cast<uintptr_t>(Addr);
    assert(IntPtr == Addr && "ExecutorAddr value out of range for uintptr_t");
    return Wrap(reinterpret_cast<T *>(IntPtr));
  }

  uint64_t getValue() const { return Addr; }
  void setValue(uint64_t Addr) { this->Addr = Addr; }
  bool isNull() const { return Addr == 0; }

  explicit operator bool() const { return Addr != 0; }

  friend bool operator==(const ExecutorAddr &LHS, const ExecutorAddr &RHS) {
    return LHS.Addr == RHS.Addr;
  }

  friend bool operator!=(const ExecutorAddr &LHS, const ExecutorAddr &RHS) {
    return LHS.Addr != RHS.Addr;
  }

  friend bool operator<(const ExecutorAddr &LHS, const ExecutorAddr &RHS) {
    return LHS.Addr < RHS.Addr;
  }

  friend bool operator<=(const ExecutorAddr &LHS, const ExecutorAddr &RHS) {
    return LHS.Addr <= RHS.Addr;
  }

  friend bool operator>(const ExecutorAddr &LHS, const ExecutorAddr &RHS) {
    return LHS.Addr > RHS.Addr;
  }

  friend bool operator>=(const ExecutorAddr &LHS, const ExecutorAddr &RHS) {
    return LHS.Addr >= RHS.Addr;
  }

  ExecutorAddr &operator++() {
    ++Addr;
    return *this;
  }
  ExecutorAddr &operator--() {
    --Addr;
    return *this;
  }
  ExecutorAddr operator++(int) { return ExecutorAddr(Addr++); }
  ExecutorAddr operator--(int) { return ExecutorAddr(Addr++); }

  ExecutorAddr &operator+=(const ExecutorAddrDiff Delta) {
    Addr += Delta;
    return *this;
  }

  ExecutorAddr &operator-=(const ExecutorAddrDiff Delta) {
    Addr -= Delta;
    return *this;
  }

private:
  uint64_t Addr = 0;
};

/// Subtracting two addresses yields an offset.
inline ExecutorAddrDiff operator-(const ExecutorAddr &LHS,
                                  const ExecutorAddr &RHS) {
  return ExecutorAddrDiff(LHS.getValue() - RHS.getValue());
}

/// Adding an offset and an address yields an address.
inline ExecutorAddr operator+(const ExecutorAddr &LHS,
                              const ExecutorAddrDiff &RHS) {
  return ExecutorAddr(LHS.getValue() + RHS);
}

/// Adding an address and an offset yields an address.
inline ExecutorAddr operator+(const ExecutorAddrDiff &LHS,
                              const ExecutorAddr &RHS) {
  return ExecutorAddr(LHS + RHS.getValue());
}

/// Represents an address range in the exceutor process.
struct ExecutorAddrRange {
  ExecutorAddrRange() = default;
  ExecutorAddrRange(ExecutorAddr Start, ExecutorAddr End)
      : Start(Start), End(End) {}
  ExecutorAddrRange(ExecutorAddr Start, ExecutorAddrDiff Size)
      : Start(Start), End(Start + Size) {}

  bool empty() const { return Start == End; }
  ExecutorAddrDiff size() const { return End - Start; }

  friend bool operator==(const ExecutorAddrRange &LHS,
                         const ExecutorAddrRange &RHS) {
    return LHS.Start == RHS.Start && LHS.End == RHS.End;
  }
  friend bool operator!=(const ExecutorAddrRange &LHS,
                         const ExecutorAddrRange &RHS) {
    return !(LHS == RHS);
  }
  bool contains(ExecutorAddr Addr) const { return Start <= Addr && Addr < End; }
  bool overlaps(const ExecutorAddrRange &Other) {
    return !(Other.End <= Start || End <= Other.Start);
  }

  template <typename T> span<T> toSpan() const {
    assert(size() % sizeof(T) == 0 &&
           "AddressRange is not a multiple of sizeof(T)");
    return span<T>(Start.toPtr<T *>(), size() / sizeof(T));
  }

  ExecutorAddr Start;
  ExecutorAddr End;
};

/// SPS serializatior for ExecutorAddr.
template <> class SPSSerializationTraits<SPSExecutorAddr, ExecutorAddr> {
public:
  static size_t size(const ExecutorAddr &EA) {
    return SPSArgList<uint64_t>::size(EA.getValue());
  }

  static bool serialize(SPSOutputBuffer &BOB, const ExecutorAddr &EA) {
    return SPSArgList<uint64_t>::serialize(BOB, EA.getValue());
  }

  static bool deserialize(SPSInputBuffer &BIB, ExecutorAddr &EA) {
    uint64_t Tmp;
    if (!SPSArgList<uint64_t>::deserialize(BIB, Tmp))
      return false;
    EA = ExecutorAddr(Tmp);
    return true;
  }
};

using SPSExecutorAddrRange = SPSTuple<SPSExecutorAddr, SPSExecutorAddr>;

/// Serialization traits for address ranges.
template <>
class SPSSerializationTraits<SPSExecutorAddrRange, ExecutorAddrRange> {
public:
  static size_t size(const ExecutorAddrRange &Value) {
    return SPSArgList<SPSExecutorAddr, SPSExecutorAddr>::size(Value.Start,
                                                              Value.End);
  }

  static bool serialize(SPSOutputBuffer &BOB, const ExecutorAddrRange &Value) {
    return SPSArgList<SPSExecutorAddr, SPSExecutorAddr>::serialize(
        BOB, Value.Start, Value.End);
  }

  static bool deserialize(SPSInputBuffer &BIB, ExecutorAddrRange &Value) {
    return SPSArgList<SPSExecutorAddr, SPSExecutorAddr>::deserialize(
        BIB, Value.Start, Value.End);
  }
};

using SPSExecutorAddrRangeSequence = SPSSequence<SPSExecutorAddrRange>;

} // End namespace __orc_rt

namespace std {

// Make ExecutorAddr hashable.
template <> struct hash<__orc_rt::ExecutorAddr> {
  size_t operator()(const __orc_rt::ExecutorAddr &A) const {
    return hash<uint64_t>()(A.getValue());
  }
};

} // namespace std

#endif // ORC_RT_EXECUTOR_ADDRESS_H
