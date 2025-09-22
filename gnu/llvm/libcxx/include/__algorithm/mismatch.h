// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_MISMATCH_H
#define _LIBCPP___ALGORITHM_MISMATCH_H

#include <__algorithm/comp.h>
#include <__algorithm/min.h>
#include <__algorithm/simd_utils.h>
#include <__algorithm/unwrap_iter.h>
#include <__config>
#include <__functional/identity.h>
#include <__iterator/aliasing_iterator.h>
#include <__type_traits/desugars_to.h>
#include <__type_traits/invoke.h>
#include <__type_traits/is_constant_evaluated.h>
#include <__type_traits/is_equality_comparable.h>
#include <__type_traits/is_integral.h>
#include <__utility/move.h>
#include <__utility/pair.h>
#include <__utility/unreachable.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Iter1, class _Sent1, class _Iter2, class _Pred, class _Proj1, class _Proj2>
_LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 pair<_Iter1, _Iter2>
__mismatch_loop(_Iter1 __first1, _Sent1 __last1, _Iter2 __first2, _Pred& __pred, _Proj1& __proj1, _Proj2& __proj2) {
  while (__first1 != __last1) {
    if (!std::__invoke(__pred, std::__invoke(__proj1, *__first1), std::__invoke(__proj2, *__first2)))
      break;
    ++__first1;
    ++__first2;
  }
  return std::make_pair(std::move(__first1), std::move(__first2));
}

template <class _Iter1, class _Sent1, class _Iter2, class _Pred, class _Proj1, class _Proj2>
_LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 pair<_Iter1, _Iter2>
__mismatch(_Iter1 __first1, _Sent1 __last1, _Iter2 __first2, _Pred& __pred, _Proj1& __proj1, _Proj2& __proj2) {
  return std::__mismatch_loop(__first1, __last1, __first2, __pred, __proj1, __proj2);
}

#if _LIBCPP_VECTORIZE_ALGORITHMS

template <class _Iter>
_LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 pair<_Iter, _Iter>
__mismatch_vectorized(_Iter __first1, _Iter __last1, _Iter __first2) {
  using __value_type              = __iter_value_type<_Iter>;
  constexpr size_t __unroll_count = 4;
  constexpr size_t __vec_size     = __native_vector_size<__value_type>;
  using __vec                     = __simd_vector<__value_type, __vec_size>;

  if (!__libcpp_is_constant_evaluated()) {
    auto __orig_first1 = __first1;
    auto __last2       = __first2 + (__last1 - __first1);
    while (static_cast<size_t>(__last1 - __first1) >= __unroll_count * __vec_size) [[__unlikely__]] {
      __vec __lhs[__unroll_count];
      __vec __rhs[__unroll_count];

      for (size_t __i = 0; __i != __unroll_count; ++__i) {
        __lhs[__i] = std::__load_vector<__vec>(__first1 + __i * __vec_size);
        __rhs[__i] = std::__load_vector<__vec>(__first2 + __i * __vec_size);
      }

      for (size_t __i = 0; __i != __unroll_count; ++__i) {
        if (auto __cmp_res = __lhs[__i] == __rhs[__i]; !std::__all_of(__cmp_res)) {
          auto __offset = __i * __vec_size + std::__find_first_not_set(__cmp_res);
          return {__first1 + __offset, __first2 + __offset};
        }
      }

      __first1 += __unroll_count * __vec_size;
      __first2 += __unroll_count * __vec_size;
    }

    // check the remaining 0-3 vectors
    while (static_cast<size_t>(__last1 - __first1) >= __vec_size) {
      if (auto __cmp_res = std::__load_vector<__vec>(__first1) == std::__load_vector<__vec>(__first2);
          !std::__all_of(__cmp_res)) {
        auto __offset = std::__find_first_not_set(__cmp_res);
        return {__first1 + __offset, __first2 + __offset};
      }
      __first1 += __vec_size;
      __first2 += __vec_size;
    }

    if (__last1 - __first1 == 0)
      return {__first1, __first2};

    // Check if we can load elements in front of the current pointer. If that's the case load a vector at
    // (last - vector_size) to check the remaining elements
    if (static_cast<size_t>(__first1 - __orig_first1) >= __vec_size) {
      __first1 = __last1 - __vec_size;
      __first2 = __last2 - __vec_size;
      auto __offset =
          std::__find_first_not_set(std::__load_vector<__vec>(__first1) == std::__load_vector<__vec>(__first2));
      return {__first1 + __offset, __first2 + __offset};
    } // else loop over the elements individually
  }

  __equal_to __pred;
  __identity __proj;
  return std::__mismatch_loop(__first1, __last1, __first2, __pred, __proj, __proj);
}

template <class _Tp,
          class _Pred,
          class _Proj1,
          class _Proj2,
          __enable_if_t<is_integral<_Tp>::value && __desugars_to_v<__equal_tag, _Pred, _Tp, _Tp> &&
                            __is_identity<_Proj1>::value && __is_identity<_Proj2>::value,
                        int> = 0>
_LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 pair<_Tp*, _Tp*>
__mismatch(_Tp* __first1, _Tp* __last1, _Tp* __first2, _Pred&, _Proj1&, _Proj2&) {
  return std::__mismatch_vectorized(__first1, __last1, __first2);
}

template <class _Tp,
          class _Pred,
          class _Proj1,
          class _Proj2,
          __enable_if_t<!is_integral<_Tp>::value && __desugars_to_v<__equal_tag, _Pred, _Tp, _Tp> &&
                            __is_identity<_Proj1>::value && __is_identity<_Proj2>::value &&
                            __can_map_to_integer_v<_Tp> && __libcpp_is_trivially_equality_comparable<_Tp, _Tp>::value,
                        int> = 0>
_LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 pair<_Tp*, _Tp*>
__mismatch(_Tp* __first1, _Tp* __last1, _Tp* __first2, _Pred& __pred, _Proj1& __proj1, _Proj2& __proj2) {
  if (__libcpp_is_constant_evaluated()) {
    return std::__mismatch_loop(__first1, __last1, __first2, __pred, __proj1, __proj2);
  } else {
    using _Iter = __aliasing_iterator<_Tp*, __get_as_integer_type_t<_Tp>>;
    auto __ret  = std::__mismatch_vectorized(_Iter(__first1), _Iter(__last1), _Iter(__first2));
    return {__ret.first.__base(), __ret.second.__base()};
  }
}
#endif // _LIBCPP_VECTORIZE_ALGORITHMS

template <class _InputIterator1, class _InputIterator2, class _BinaryPredicate>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 pair<_InputIterator1, _InputIterator2>
mismatch(_InputIterator1 __first1, _InputIterator1 __last1, _InputIterator2 __first2, _BinaryPredicate __pred) {
  __identity __proj;
  auto __res = std::__mismatch(
      std::__unwrap_iter(__first1), std::__unwrap_iter(__last1), std::__unwrap_iter(__first2), __pred, __proj, __proj);
  return std::make_pair(std::__rewrap_iter(__first1, __res.first), std::__rewrap_iter(__first2, __res.second));
}

template <class _InputIterator1, class _InputIterator2>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 pair<_InputIterator1, _InputIterator2>
mismatch(_InputIterator1 __first1, _InputIterator1 __last1, _InputIterator2 __first2) {
  return std::mismatch(__first1, __last1, __first2, __equal_to());
}

#if _LIBCPP_STD_VER >= 14
template <class _Iter1, class _Sent1, class _Iter2, class _Sent2, class _Pred, class _Proj1, class _Proj2>
_LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 pair<_Iter1, _Iter2> __mismatch(
    _Iter1 __first1, _Sent1 __last1, _Iter2 __first2, _Sent2 __last2, _Pred& __pred, _Proj1& __proj1, _Proj2& __proj2) {
  while (__first1 != __last1 && __first2 != __last2) {
    if (!std::__invoke(__pred, std::__invoke(__proj1, *__first1), std::__invoke(__proj2, *__first2)))
      break;
    ++__first1;
    ++__first2;
  }
  return {std::move(__first1), std::move(__first2)};
}

template <class _Tp, class _Pred, class _Proj1, class _Proj2>
_LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 pair<_Tp*, _Tp*>
__mismatch(_Tp* __first1, _Tp* __last1, _Tp* __first2, _Tp* __last2, _Pred& __pred, _Proj1& __proj1, _Proj2& __proj2) {
  auto __len = std::min(__last1 - __first1, __last2 - __first2);
  return std::__mismatch(__first1, __first1 + __len, __first2, __pred, __proj1, __proj2);
}

template <class _InputIterator1, class _InputIterator2, class _BinaryPredicate>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 pair<_InputIterator1, _InputIterator2>
mismatch(_InputIterator1 __first1,
         _InputIterator1 __last1,
         _InputIterator2 __first2,
         _InputIterator2 __last2,
         _BinaryPredicate __pred) {
  __identity __proj;
  auto __res = std::__mismatch(
      std::__unwrap_iter(__first1),
      std::__unwrap_iter(__last1),
      std::__unwrap_iter(__first2),
      std::__unwrap_iter(__last2),
      __pred,
      __proj,
      __proj);
  return {std::__rewrap_iter(__first1, __res.first), std::__rewrap_iter(__first2, __res.second)};
}

template <class _InputIterator1, class _InputIterator2>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 pair<_InputIterator1, _InputIterator2>
mismatch(_InputIterator1 __first1, _InputIterator1 __last1, _InputIterator2 __first2, _InputIterator2 __last2) {
  return std::mismatch(__first1, __last1, __first2, __last2, __equal_to());
}
#endif

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_MISMATCH_H
