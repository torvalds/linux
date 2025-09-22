//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Define ~condition_variable.
//
// On some platforms ~condition_variable has been made trivial and the
// definition is only provided for ABI compatibility.

#include <__config>
#include <__thread/support.h>

#if _LIBCPP_ABI_VERSION == 1 || !defined(_LIBCPP_HAS_TRIVIAL_CONDVAR_DESTRUCTION)
#  define NEEDS_CONDVAR_DESTRUCTOR
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#ifdef NEEDS_CONDVAR_DESTRUCTOR

class _LIBCPP_EXPORTED_FROM_ABI condition_variable {
  __libcpp_condvar_t __cv_ = _LIBCPP_CONDVAR_INITIALIZER;

public:
  _LIBCPP_HIDE_FROM_ABI constexpr condition_variable() noexcept = default;

  ~condition_variable();

  condition_variable(const condition_variable&)            = delete;
  condition_variable& operator=(const condition_variable&) = delete;
};

condition_variable::~condition_variable() { __libcpp_condvar_destroy(&__cv_); }
#endif

_LIBCPP_END_NAMESPACE_STD
