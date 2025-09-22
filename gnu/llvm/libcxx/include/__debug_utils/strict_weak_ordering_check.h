//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___LIBCXX_DEBUG_STRICT_WEAK_ORDERING_CHECK
#define _LIBCPP___LIBCXX_DEBUG_STRICT_WEAK_ORDERING_CHECK

#include <__config>

#include <__algorithm/comp_ref_type.h>
#include <__algorithm/is_sorted.h>
#include <__assert>
#include <__iterator/iterator_traits.h>
#include <__type_traits/is_constant_evaluated.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _RandomAccessIterator, class _Comp>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 void
__check_strict_weak_ordering_sorted(_RandomAccessIterator __first, _RandomAccessIterator __last, _Comp& __comp) {
#if _LIBCPP_HARDENING_MODE == _LIBCPP_HARDENING_MODE_DEBUG
  using __diff_t  = __iter_diff_t<_RandomAccessIterator>;
  using _Comp_ref = __comp_ref_type<_Comp>;
  if (!__libcpp_is_constant_evaluated()) {
    // Check if the range is actually sorted.
    _LIBCPP_ASSERT_SEMANTIC_REQUIREMENT(
        (std::is_sorted<_RandomAccessIterator, _Comp_ref>(__first, __last, _Comp_ref(__comp))),
        "The range is not sorted after the sort, your comparator is not a valid strict-weak ordering");
    // Limit the number of elements we need to check.
    __diff_t __size = __last - __first > __diff_t(100) ? __diff_t(100) : __last - __first;
    __diff_t __p    = 0;
    while (__p < __size) {
      __diff_t __q = __p + __diff_t(1);
      // Find first element that is greater than *(__first+__p).
      while (__q < __size && !__comp(*(__first + __p), *(__first + __q))) {
        ++__q;
      }
      // Check that the elements from __p to __q are equal between each other.
      for (__diff_t __b = __p; __b < __q; ++__b) {
        for (__diff_t __a = __p; __a <= __b; ++__a) {
          _LIBCPP_ASSERT_SEMANTIC_REQUIREMENT(
              !__comp(*(__first + __a), *(__first + __b)), "Your comparator is not a valid strict-weak ordering");
          _LIBCPP_ASSERT_SEMANTIC_REQUIREMENT(
              !__comp(*(__first + __b), *(__first + __a)), "Your comparator is not a valid strict-weak ordering");
        }
      }
      // Check that elements between __p and __q are less than between __q and __size.
      for (__diff_t __a = __p; __a < __q; ++__a) {
        for (__diff_t __b = __q; __b < __size; ++__b) {
          _LIBCPP_ASSERT_SEMANTIC_REQUIREMENT(
              __comp(*(__first + __a), *(__first + __b)), "Your comparator is not a valid strict-weak ordering");
          _LIBCPP_ASSERT_SEMANTIC_REQUIREMENT(
              !__comp(*(__first + __b), *(__first + __a)), "Your comparator is not a valid strict-weak ordering");
        }
      }
      // Skip these equal elements.
      __p = __q;
    }
  }
#else
  (void)__first;
  (void)__last;
  (void)__comp;
#endif // _LIBCPP_HARDENING_MODE == _LIBCPP_HARDENING_MODE_DEBUG
}

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___LIBCXX_DEBUG_STRICT_WEAK_ORDERING_CHECK
