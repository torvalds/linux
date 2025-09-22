//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "__cxxabi_config.h"
#include "cxxabi.h"

// Tell the implementation that we're building the actual implementation
// (and not testing it)
#define BUILDING_CXA_GUARD
#include "cxa_guard_impl.h"

/*
    This implementation must be careful to not call code external to this file
    which will turn around and try to call __cxa_guard_acquire reentrantly.
    For this reason, the headers of this file are as restricted as possible.
    Previous implementations of this code for __APPLE__ have used
    std::__libcpp_mutex_lock and the abort_message utility without problem. This
    implementation also uses std::__libcpp_condvar_wait which has tested
    to not be a problem.
*/

namespace __cxxabiv1 {

#if defined(_LIBCXXABI_GUARD_ABI_ARM)
using guard_type = uint32_t;
#else
using guard_type = uint64_t;
#endif

extern "C"
{
_LIBCXXABI_FUNC_VIS int __cxa_guard_acquire(guard_type* raw_guard_object) {
  SelectedImplementation imp(raw_guard_object);
  return static_cast<int>(imp.cxa_guard_acquire());
}

_LIBCXXABI_FUNC_VIS void __cxa_guard_release(guard_type *raw_guard_object) {
  SelectedImplementation imp(raw_guard_object);
  imp.cxa_guard_release();
}

_LIBCXXABI_FUNC_VIS void __cxa_guard_abort(guard_type *raw_guard_object) {
  SelectedImplementation imp(raw_guard_object);
  imp.cxa_guard_abort();
}
} // extern "C"

}  // __cxxabiv1
