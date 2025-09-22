//===-- atomic_test.cpp -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "tests/scudo_unit_test.h"

#include "atomic_helpers.h"

namespace scudo {

template <typename T> struct ValAndMagic {
  typename T::Type Magic0;
  T A;
  typename T::Type Magic1;

  static ValAndMagic<T> *Sink;
};

template <typename T> ValAndMagic<T> *ValAndMagic<T>::Sink;

template <typename T, memory_order LoadMO, memory_order StoreMO>
void checkStoreLoad() {
  typedef typename T::Type Type;
  ValAndMagic<T> Val;
  // Prevent the compiler from scalarizing the struct.
  ValAndMagic<T>::Sink = &Val;
  // Ensure that surrounding memory is not overwritten.
  Val.Magic0 = Val.Magic1 = (Type)-3;
  for (u64 I = 0; I < 100; I++) {
    // Generate A value that occupies all bytes of the variable.
    u64 V = I;
    V |= V << 8;
    V |= V << 16;
    V |= V << 32;
    Val.A.ValDoNotUse = (Type)V;
    EXPECT_EQ(atomic_load(&Val.A, LoadMO), (Type)V);
    Val.A.ValDoNotUse = (Type)-1;
    atomic_store(&Val.A, (Type)V, StoreMO);
    EXPECT_EQ(Val.A.ValDoNotUse, (Type)V);
  }
  EXPECT_EQ(Val.Magic0, (Type)-3);
  EXPECT_EQ(Val.Magic1, (Type)-3);
}

TEST(ScudoAtomicTest, AtomicStoreLoad) {
  checkStoreLoad<atomic_u8, memory_order_relaxed, memory_order_relaxed>();
  checkStoreLoad<atomic_u8, memory_order_consume, memory_order_relaxed>();
  checkStoreLoad<atomic_u8, memory_order_acquire, memory_order_relaxed>();
  checkStoreLoad<atomic_u8, memory_order_relaxed, memory_order_release>();
  checkStoreLoad<atomic_u8, memory_order_seq_cst, memory_order_seq_cst>();

  checkStoreLoad<atomic_u16, memory_order_relaxed, memory_order_relaxed>();
  checkStoreLoad<atomic_u16, memory_order_consume, memory_order_relaxed>();
  checkStoreLoad<atomic_u16, memory_order_acquire, memory_order_relaxed>();
  checkStoreLoad<atomic_u16, memory_order_relaxed, memory_order_release>();
  checkStoreLoad<atomic_u16, memory_order_seq_cst, memory_order_seq_cst>();

  checkStoreLoad<atomic_u32, memory_order_relaxed, memory_order_relaxed>();
  checkStoreLoad<atomic_u32, memory_order_consume, memory_order_relaxed>();
  checkStoreLoad<atomic_u32, memory_order_acquire, memory_order_relaxed>();
  checkStoreLoad<atomic_u32, memory_order_relaxed, memory_order_release>();
  checkStoreLoad<atomic_u32, memory_order_seq_cst, memory_order_seq_cst>();

  checkStoreLoad<atomic_u64, memory_order_relaxed, memory_order_relaxed>();
  checkStoreLoad<atomic_u64, memory_order_consume, memory_order_relaxed>();
  checkStoreLoad<atomic_u64, memory_order_acquire, memory_order_relaxed>();
  checkStoreLoad<atomic_u64, memory_order_relaxed, memory_order_release>();
  checkStoreLoad<atomic_u64, memory_order_seq_cst, memory_order_seq_cst>();

  checkStoreLoad<atomic_uptr, memory_order_relaxed, memory_order_relaxed>();
  checkStoreLoad<atomic_uptr, memory_order_consume, memory_order_relaxed>();
  checkStoreLoad<atomic_uptr, memory_order_acquire, memory_order_relaxed>();
  checkStoreLoad<atomic_uptr, memory_order_relaxed, memory_order_release>();
  checkStoreLoad<atomic_uptr, memory_order_seq_cst, memory_order_seq_cst>();
}

template <typename T> void checkAtomicCompareExchange() {
  typedef typename T::Type Type;
  Type OldVal = 42;
  Type NewVal = 24;
  Type V = OldVal;
  EXPECT_TRUE(atomic_compare_exchange_strong(reinterpret_cast<T *>(&V), &OldVal,
                                             NewVal, memory_order_relaxed));
  EXPECT_FALSE(atomic_compare_exchange_strong(
      reinterpret_cast<T *>(&V), &OldVal, NewVal, memory_order_relaxed));
  EXPECT_EQ(NewVal, OldVal);
}

TEST(ScudoAtomicTest, AtomicCompareExchangeTest) {
  checkAtomicCompareExchange<atomic_u8>();
  checkAtomicCompareExchange<atomic_u16>();
  checkAtomicCompareExchange<atomic_u32>();
  checkAtomicCompareExchange<atomic_u64>();
  checkAtomicCompareExchange<atomic_uptr>();
}

} // namespace scudo
