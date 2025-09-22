//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <new>

#include "include/atomic_support.h"

#if defined(_LIBCPP_ABI_MICROSOFT)
#  if !defined(_LIBCPP_ABI_VCRUNTIME)
#    define _LIBPCPP_DEFINE_NEW_HANDLER
#  endif
#elif defined(LIBCXX_BUILDING_LIBCXXABI)
// nothing to do, we use the one from libc++abi
#elif defined(LIBCXXRT)
#  define _LIBPCPP_DEFINE_NEW_HANDLER
#elif defined(__GLIBCXX__)
// nothing to do, we use the one from libstdc++/libsupc++
#else
#  define _LIBPCPP_DEFINE_NEW_HANDLER
#endif

#if defined(_LIBPCPP_DEFINE_NEW_HANDLER)

namespace std { // purposefully not versioned

static constinit std::new_handler __new_handler = nullptr;

new_handler set_new_handler(new_handler handler) noexcept { return __libcpp_atomic_exchange(&__new_handler, handler); }

new_handler get_new_handler() noexcept { return __libcpp_atomic_load(&__new_handler); }

} // namespace std

#endif // _LIBPCPP_DEFINE_NEW_HANDLER
