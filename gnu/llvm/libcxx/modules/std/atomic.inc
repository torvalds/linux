// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {

  // [atomics.order], order and consistency
  using std::memory_order _LIBCPP_USING_IF_EXISTS;
  using std::memory_order_acq_rel _LIBCPP_USING_IF_EXISTS;
  using std::memory_order_acquire _LIBCPP_USING_IF_EXISTS;
  using std::memory_order_consume _LIBCPP_USING_IF_EXISTS;
  using std::memory_order_relaxed _LIBCPP_USING_IF_EXISTS;
  using std::memory_order_release _LIBCPP_USING_IF_EXISTS;
  using std::memory_order_seq_cst _LIBCPP_USING_IF_EXISTS;

  using std::kill_dependency _LIBCPP_USING_IF_EXISTS;

  // [atomics.ref.generic], class template atomic_ref
  // [atomics.ref.pointer], partial specialization for pointers
  using std::atomic_ref _LIBCPP_USING_IF_EXISTS;

  // [atomics.types.generic], class template atomic
  using std::atomic _LIBCPP_USING_IF_EXISTS;

  // [atomics.nonmembers], non-member functions
  using std::atomic_compare_exchange_strong _LIBCPP_USING_IF_EXISTS;
  using std::atomic_compare_exchange_strong_explicit _LIBCPP_USING_IF_EXISTS;
  using std::atomic_compare_exchange_weak _LIBCPP_USING_IF_EXISTS;
  using std::atomic_compare_exchange_weak_explicit _LIBCPP_USING_IF_EXISTS;
  using std::atomic_exchange _LIBCPP_USING_IF_EXISTS;
  using std::atomic_exchange_explicit _LIBCPP_USING_IF_EXISTS;
  using std::atomic_is_lock_free _LIBCPP_USING_IF_EXISTS;
  using std::atomic_load _LIBCPP_USING_IF_EXISTS;
  using std::atomic_load_explicit _LIBCPP_USING_IF_EXISTS;
  using std::atomic_store _LIBCPP_USING_IF_EXISTS;
  using std::atomic_store_explicit _LIBCPP_USING_IF_EXISTS;

  using std::atomic_fetch_add _LIBCPP_USING_IF_EXISTS;
  using std::atomic_fetch_add_explicit _LIBCPP_USING_IF_EXISTS;
  using std::atomic_fetch_and _LIBCPP_USING_IF_EXISTS;
  using std::atomic_fetch_and_explicit _LIBCPP_USING_IF_EXISTS;
  using std::atomic_fetch_or _LIBCPP_USING_IF_EXISTS;
  using std::atomic_fetch_or_explicit _LIBCPP_USING_IF_EXISTS;
  using std::atomic_fetch_sub _LIBCPP_USING_IF_EXISTS;
  using std::atomic_fetch_sub_explicit _LIBCPP_USING_IF_EXISTS;
  using std::atomic_fetch_xor _LIBCPP_USING_IF_EXISTS;
  using std::atomic_fetch_xor_explicit _LIBCPP_USING_IF_EXISTS;
  using std::atomic_notify_all _LIBCPP_USING_IF_EXISTS;
  using std::atomic_notify_one _LIBCPP_USING_IF_EXISTS;
  using std::atomic_wait _LIBCPP_USING_IF_EXISTS;
  using std::atomic_wait_explicit _LIBCPP_USING_IF_EXISTS;

  // [atomics.alias], type aliases
  using std::atomic_bool _LIBCPP_USING_IF_EXISTS;
  using std::atomic_char _LIBCPP_USING_IF_EXISTS;
  using std::atomic_char16_t _LIBCPP_USING_IF_EXISTS;
  using std::atomic_char32_t _LIBCPP_USING_IF_EXISTS;
#ifndef _LIBCPP_HAS_NO_CHAR8_T
  using std::atomic_char8_t _LIBCPP_USING_IF_EXISTS;
#endif
  using std::atomic_int _LIBCPP_USING_IF_EXISTS;
  using std::atomic_llong _LIBCPP_USING_IF_EXISTS;
  using std::atomic_long _LIBCPP_USING_IF_EXISTS;
  using std::atomic_schar _LIBCPP_USING_IF_EXISTS;
  using std::atomic_short _LIBCPP_USING_IF_EXISTS;
  using std::atomic_uchar _LIBCPP_USING_IF_EXISTS;
  using std::atomic_uint _LIBCPP_USING_IF_EXISTS;
  using std::atomic_ullong _LIBCPP_USING_IF_EXISTS;
  using std::atomic_ulong _LIBCPP_USING_IF_EXISTS;
  using std::atomic_ushort _LIBCPP_USING_IF_EXISTS;
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  using std::atomic_wchar_t _LIBCPP_USING_IF_EXISTS;
#endif

  using std::atomic_int16_t _LIBCPP_USING_IF_EXISTS;
  using std::atomic_int32_t _LIBCPP_USING_IF_EXISTS;
  using std::atomic_int64_t _LIBCPP_USING_IF_EXISTS;
  using std::atomic_int8_t _LIBCPP_USING_IF_EXISTS;
  using std::atomic_uint16_t _LIBCPP_USING_IF_EXISTS;
  using std::atomic_uint32_t _LIBCPP_USING_IF_EXISTS;
  using std::atomic_uint64_t _LIBCPP_USING_IF_EXISTS;
  using std::atomic_uint8_t _LIBCPP_USING_IF_EXISTS;

  using std::atomic_int_least16_t _LIBCPP_USING_IF_EXISTS;
  using std::atomic_int_least32_t _LIBCPP_USING_IF_EXISTS;
  using std::atomic_int_least64_t _LIBCPP_USING_IF_EXISTS;
  using std::atomic_int_least8_t _LIBCPP_USING_IF_EXISTS;
  using std::atomic_uint_least16_t _LIBCPP_USING_IF_EXISTS;
  using std::atomic_uint_least32_t _LIBCPP_USING_IF_EXISTS;
  using std::atomic_uint_least64_t _LIBCPP_USING_IF_EXISTS;
  using std::atomic_uint_least8_t _LIBCPP_USING_IF_EXISTS;

  using std::atomic_int_fast16_t _LIBCPP_USING_IF_EXISTS;
  using std::atomic_int_fast32_t _LIBCPP_USING_IF_EXISTS;
  using std::atomic_int_fast64_t _LIBCPP_USING_IF_EXISTS;
  using std::atomic_int_fast8_t _LIBCPP_USING_IF_EXISTS;
  using std::atomic_uint_fast16_t _LIBCPP_USING_IF_EXISTS;
  using std::atomic_uint_fast32_t _LIBCPP_USING_IF_EXISTS;
  using std::atomic_uint_fast64_t _LIBCPP_USING_IF_EXISTS;
  using std::atomic_uint_fast8_t _LIBCPP_USING_IF_EXISTS;

  using std::atomic_intmax_t _LIBCPP_USING_IF_EXISTS;
  using std::atomic_intptr_t _LIBCPP_USING_IF_EXISTS;
  using std::atomic_ptrdiff_t _LIBCPP_USING_IF_EXISTS;
  using std::atomic_size_t _LIBCPP_USING_IF_EXISTS;
  using std::atomic_uintmax_t _LIBCPP_USING_IF_EXISTS;
  using std::atomic_uintptr_t _LIBCPP_USING_IF_EXISTS;

#ifndef _LIBCPP_NO_LOCK_FREE_TYPES
  using std::atomic_signed_lock_free _LIBCPP_USING_IF_EXISTS;
  using std::atomic_unsigned_lock_free _LIBCPP_USING_IF_EXISTS;
#endif

  // [atomics.flag], flag type and operations
  using std::atomic_flag _LIBCPP_USING_IF_EXISTS;

  using std::atomic_flag_clear _LIBCPP_USING_IF_EXISTS;
  using std::atomic_flag_clear_explicit _LIBCPP_USING_IF_EXISTS;
  using std::atomic_flag_test _LIBCPP_USING_IF_EXISTS;
  using std::atomic_flag_test_and_set _LIBCPP_USING_IF_EXISTS;
  using std::atomic_flag_test_and_set_explicit _LIBCPP_USING_IF_EXISTS;
  using std::atomic_flag_test_explicit _LIBCPP_USING_IF_EXISTS;

  using std::atomic_flag_notify_all _LIBCPP_USING_IF_EXISTS;
  using std::atomic_flag_notify_one _LIBCPP_USING_IF_EXISTS;
  using std::atomic_flag_wait _LIBCPP_USING_IF_EXISTS;
  using std::atomic_flag_wait_explicit _LIBCPP_USING_IF_EXISTS;

  // [atomics.fences], fences
  using std::atomic_signal_fence _LIBCPP_USING_IF_EXISTS;
  using std::atomic_thread_fence _LIBCPP_USING_IF_EXISTS;

  // [depr.atomics.nonmembers]
  using std::atomic_init _LIBCPP_USING_IF_EXISTS;

} // namespace std
