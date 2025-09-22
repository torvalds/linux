//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <__verbose_abort>
#include <new>

namespace std { // purposefully not versioned

#ifndef __GLIBCXX__
const nothrow_t nothrow{};
#endif

#ifndef LIBSTDCXX

void __throw_bad_alloc() {
#  ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  throw bad_alloc();
#  else
  _LIBCPP_VERBOSE_ABORT("bad_alloc was thrown in -fno-exceptions mode");
#  endif
}

#endif // !LIBSTDCXX

} // namespace std
