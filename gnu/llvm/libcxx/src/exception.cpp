//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <exception>
#include <new>
#include <typeinfo>

#if defined(LIBCXXRT) || defined(LIBCXX_BUILDING_LIBCXXABI)
#  include <cxxabi.h>
using namespace __cxxabiv1;
#  define HAVE_DEPENDENT_EH_ABI 1
#endif

#if defined(_LIBCPP_ABI_MICROSOFT)
#  include "support/runtime/exception_msvc.ipp"
#  include "support/runtime/exception_pointer_msvc.ipp"
#elif defined(_LIBCPPABI_VERSION)
#  include "support/runtime/exception_libcxxabi.ipp"
#  include "support/runtime/exception_pointer_cxxabi.ipp"
#elif defined(LIBCXXRT)
#  include "support/runtime/exception_libcxxrt.ipp"
#  include "support/runtime/exception_pointer_cxxabi.ipp"
#elif defined(__GLIBCXX__)
#  include "support/runtime/exception_glibcxx.ipp"
#  include "support/runtime/exception_pointer_glibcxx.ipp"
#else
#  include "include/atomic_support.h"
#  include "support/runtime/exception_fallback.ipp"
#  include "support/runtime/exception_pointer_unimplemented.ipp"
#endif
