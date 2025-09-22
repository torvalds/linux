//===-- sanitizer_atomic_test.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer/AddressSanitizer runtime.
//
//===----------------------------------------------------------------------===//
#include "sanitizer_common/sanitizer_atomic.h"
#include "gtest/gtest.h"

#ifndef __has_extension
#define __has_extension(x) 0
#endif

#ifndef ATOMIC_LLONG_LOCK_FREE
#  if __has_extension(c_atomic) || __has_extension(cxx_atomic)
#    define ATOMIC_LLONG_LOCK_FREE __CLANG_ATOMIC_LLONG_LOCK_FREE
#  elif __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7)
#    define ATOMIC_LLONG_LOCK_FREE __GCC_ATOMIC_LLONG_LOCK_FREE
#  else
#    error Unsupported compiler.
#  endif
#endif

namespace __sanitizer {

template<typename T>
struct ValAndMagic {
  typename T::Type magic0;
  T a;
  typename T::Type magic1;

  static ValAndMagic<T> *sink;
};

template<typename T>
ValAndMagic<T> *ValAndMagic<T>::sink;

template<typename T, memory_order load_mo, memory_order store_mo>
void CheckStoreLoad() {
  typedef typename T::Type Type;
  ValAndMagic<T> val;
  // Prevent the compiler from scalarizing the struct.
  ValAndMagic<T>::sink = &val;
  // Ensure that surrounding memory is not overwritten.
  val.magic0 = val.magic1 = (Type)-3;
  for (u64 i = 0; i < 100; i++) {
    // Generate a value that occupies all bytes of the variable.
    u64 v = i;
    v |= v << 8;
    v |= v << 16;
    v |= v << 32;
    val.a.val_dont_use = (Type)v;
    EXPECT_EQ(atomic_load(&val.a, load_mo), (Type)v);
    val.a.val_dont_use = (Type)-1;
    atomic_store(&val.a, (Type)v, store_mo);
    EXPECT_EQ(val.a.val_dont_use, (Type)v);
  }
  EXPECT_EQ(val.magic0, (Type)-3);
  EXPECT_EQ(val.magic1, (Type)-3);
}

TEST(SanitizerCommon, AtomicStoreLoad) {
  CheckStoreLoad<atomic_uint8_t, memory_order_relaxed, memory_order_relaxed>();
  CheckStoreLoad<atomic_uint8_t, memory_order_consume, memory_order_relaxed>();
  CheckStoreLoad<atomic_uint8_t, memory_order_acquire, memory_order_relaxed>();
  CheckStoreLoad<atomic_uint8_t, memory_order_relaxed, memory_order_release>();
  CheckStoreLoad<atomic_uint8_t, memory_order_seq_cst, memory_order_seq_cst>();

  CheckStoreLoad<atomic_uint16_t, memory_order_relaxed, memory_order_relaxed>();
  CheckStoreLoad<atomic_uint16_t, memory_order_consume, memory_order_relaxed>();
  CheckStoreLoad<atomic_uint16_t, memory_order_acquire, memory_order_relaxed>();
  CheckStoreLoad<atomic_uint16_t, memory_order_relaxed, memory_order_release>();
  CheckStoreLoad<atomic_uint16_t, memory_order_seq_cst, memory_order_seq_cst>();

  CheckStoreLoad<atomic_uint32_t, memory_order_relaxed, memory_order_relaxed>();
  CheckStoreLoad<atomic_uint32_t, memory_order_consume, memory_order_relaxed>();
  CheckStoreLoad<atomic_uint32_t, memory_order_acquire, memory_order_relaxed>();
  CheckStoreLoad<atomic_uint32_t, memory_order_relaxed, memory_order_release>();
  CheckStoreLoad<atomic_uint32_t, memory_order_seq_cst, memory_order_seq_cst>();

  // Avoid fallbacking to software emulated compiler atomics, that are usually
  // provided by libatomic, which is not always present.
#if ATOMIC_LLONG_LOCK_FREE == 2
  CheckStoreLoad<atomic_uint64_t, memory_order_relaxed, memory_order_relaxed>();
  CheckStoreLoad<atomic_uint64_t, memory_order_consume, memory_order_relaxed>();
  CheckStoreLoad<atomic_uint64_t, memory_order_acquire, memory_order_relaxed>();
  CheckStoreLoad<atomic_uint64_t, memory_order_relaxed, memory_order_release>();
  CheckStoreLoad<atomic_uint64_t, memory_order_seq_cst, memory_order_seq_cst>();
#endif

  CheckStoreLoad<atomic_uintptr_t, memory_order_relaxed, memory_order_relaxed>
      ();
  CheckStoreLoad<atomic_uintptr_t, memory_order_consume, memory_order_relaxed>
      ();
  CheckStoreLoad<atomic_uintptr_t, memory_order_acquire, memory_order_relaxed>
      ();
  CheckStoreLoad<atomic_uintptr_t, memory_order_relaxed, memory_order_release>
      ();
  CheckStoreLoad<atomic_uintptr_t, memory_order_seq_cst, memory_order_seq_cst>
      ();
}

// Clang crashes while compiling this test for Android:
// http://llvm.org/bugs/show_bug.cgi?id=15587
#if !SANITIZER_ANDROID
template<typename T>
void CheckAtomicCompareExchange() {
  typedef typename T::Type Type;
  {
    Type old_val = 42;
    Type new_val = 24;
    Type var = old_val;
    EXPECT_TRUE(atomic_compare_exchange_strong((T*)&var, &old_val, new_val,
                                               memory_order_relaxed));
    EXPECT_FALSE(atomic_compare_exchange_strong((T*)&var, &old_val, new_val,
                                                memory_order_relaxed));
    EXPECT_EQ(new_val, old_val);
  }
  {
    Type old_val = 42;
    Type new_val = 24;
    Type var = old_val;
    EXPECT_TRUE(atomic_compare_exchange_weak((T*)&var, &old_val, new_val,
                                             memory_order_relaxed));
    EXPECT_FALSE(atomic_compare_exchange_weak((T*)&var, &old_val, new_val,
                                              memory_order_relaxed));
    EXPECT_EQ(new_val, old_val);
  }
}

TEST(SanitizerCommon, AtomicCompareExchangeTest) {
  CheckAtomicCompareExchange<atomic_uint8_t>();
  CheckAtomicCompareExchange<atomic_uint16_t>();
  CheckAtomicCompareExchange<atomic_uint32_t>();
#if ATOMIC_LLONG_LOCK_FREE == 2
  CheckAtomicCompareExchange<atomic_uint64_t>();
#endif
  CheckAtomicCompareExchange<atomic_uintptr_t>();
}
#endif  //!SANITIZER_ANDROID

}  // namespace __sanitizer
