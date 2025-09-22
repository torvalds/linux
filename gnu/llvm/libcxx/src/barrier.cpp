//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <barrier>
#include <thread>

_LIBCPP_BEGIN_NAMESPACE_STD

#if !defined(_LIBCPP_HAS_NO_TREE_BARRIER)

class __barrier_algorithm_base {
public:
  struct alignas(64) /* naturally-align the heap state */ __state_t {
    struct {
      __atomic_base<__barrier_phase_t> __phase{0};
    } __tickets[64];
  };

  ptrdiff_t& __expected_;
  unique_ptr<__state_t[]> __state_;

  _LIBCPP_HIDDEN __barrier_algorithm_base(ptrdiff_t& __expected) : __expected_(__expected) {
    size_t const __count = (__expected + 1) >> 1;
    __state_             = unique_ptr<__state_t[]>(new __state_t[__count]);
  }
  _LIBCPP_HIDDEN bool __arrive(__barrier_phase_t __old_phase) {
    __barrier_phase_t const __half_step = __old_phase + 1, __full_step = __old_phase + 2;
    size_t __current_expected = __expected_,
           __current          = hash<thread::id>()(this_thread::get_id()) % ((__expected_ + 1) >> 1);
    for (int __round = 0;; ++__round) {
      if (__current_expected <= 1)
        return true;
      size_t const __end_node = ((__current_expected + 1) >> 1), __last_node = __end_node - 1;
      for (;; ++__current) {
        if (__current == __end_node)
          __current = 0;
        __barrier_phase_t expect = __old_phase;
        if (__current == __last_node && (__current_expected & 1)) {
          if (__state_[__current].__tickets[__round].__phase.compare_exchange_strong(
                  expect, __full_step, memory_order_acq_rel))
            break; // I'm 1 in 1, go to next __round
        } else if (__state_[__current].__tickets[__round].__phase.compare_exchange_strong(
                       expect, __half_step, memory_order_acq_rel)) {
          return false; // I'm 1 in 2, done with arrival
        } else if (expect == __half_step) {
          if (__state_[__current].__tickets[__round].__phase.compare_exchange_strong(
                  expect, __full_step, memory_order_acq_rel))
            break; // I'm 2 in 2, go to next __round
        }
      }
      __current_expected = __last_node + 1;
      __current >>= 1;
    }
  }
};

_LIBCPP_EXPORTED_FROM_ABI __barrier_algorithm_base* __construct_barrier_algorithm_base(ptrdiff_t& __expected) {
  return new __barrier_algorithm_base(__expected);
}
_LIBCPP_EXPORTED_FROM_ABI bool
__arrive_barrier_algorithm_base(__barrier_algorithm_base* __barrier, __barrier_phase_t __old_phase) noexcept {
  return __barrier->__arrive(__old_phase);
}
_LIBCPP_EXPORTED_FROM_ABI void __destroy_barrier_algorithm_base(__barrier_algorithm_base* __barrier) noexcept {
  delete __barrier;
}

#endif // !defined(_LIBCPP_HAS_NO_TREE_BARRIER)

_LIBCPP_END_NAMESPACE_STD
