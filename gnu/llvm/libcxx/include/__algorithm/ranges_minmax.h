//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_RANGES_MINMAX_H
#define _LIBCPP___ALGORITHM_RANGES_MINMAX_H

#include <__algorithm/min_max_result.h>
#include <__algorithm/minmax_element.h>
#include <__assert>
#include <__concepts/copyable.h>
#include <__concepts/same_as.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/invoke.h>
#include <__functional/ranges_operations.h>
#include <__iterator/concepts.h>
#include <__iterator/next.h>
#include <__iterator/projected.h>
#include <__ranges/access.h>
#include <__ranges/concepts.h>
#include <__type_traits/desugars_to.h>
#include <__type_traits/is_reference.h>
#include <__type_traits/is_trivially_copyable.h>
#include <__type_traits/remove_cvref.h>
#include <__utility/forward.h>
#include <__utility/move.h>
#include <__utility/pair.h>
#include <initializer_list>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER >= 20

_LIBCPP_PUSH_MACROS
#  include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

namespace ranges {
template <class _T1>
using minmax_result = min_max_result<_T1>;

namespace __minmax {
struct __fn {
  template <class _Type,
            class _Proj                                                      = identity,
            indirect_strict_weak_order<projected<const _Type*, _Proj>> _Comp = ranges::less>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr ranges::minmax_result<const _Type&>
  operator()(_LIBCPP_LIFETIMEBOUND const _Type& __a,
             _LIBCPP_LIFETIMEBOUND const _Type& __b,
             _Comp __comp = {},
             _Proj __proj = {}) const {
    if (std::invoke(__comp, std::invoke(__proj, __b), std::invoke(__proj, __a)))
      return {__b, __a};
    return {__a, __b};
  }

  template <copyable _Type,
            class _Proj                                                      = identity,
            indirect_strict_weak_order<projected<const _Type*, _Proj>> _Comp = ranges::less>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr ranges::minmax_result<_Type>
  operator()(initializer_list<_Type> __il, _Comp __comp = {}, _Proj __proj = {}) const {
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        __il.begin() != __il.end(), "initializer_list has to contain at least one element");
    auto __iters = std::__minmax_element_impl(__il.begin(), __il.end(), __comp, __proj);
    return ranges::minmax_result<_Type>{*__iters.first, *__iters.second};
  }

  template <input_range _Range,
            class _Proj                                                            = identity,
            indirect_strict_weak_order<projected<iterator_t<_Range>, _Proj>> _Comp = ranges::less>
    requires indirectly_copyable_storable<iterator_t<_Range>, range_value_t<_Range>*>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr ranges::minmax_result<range_value_t<_Range>>
  operator()(_Range&& __r, _Comp __comp = {}, _Proj __proj = {}) const {
    auto __first  = ranges::begin(__r);
    auto __last   = ranges::end(__r);
    using _ValueT = range_value_t<_Range>;

    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(__first != __last, "range has to contain at least one element");

    // This optimiation is not in minmax_element because clang doesn't see through the pointers and as a result doesn't
    // vectorize the code.
    if constexpr (contiguous_range<_Range> && is_integral_v<_ValueT> &&
                  __is_cheap_to_copy<_ValueT> & __is_identity<_Proj>::value &&
                  __desugars_to_v<__less_tag, _Comp, _ValueT, _ValueT>) {
      minmax_result<_ValueT> __result = {__r[0], __r[0]};
      for (auto __e : __r) {
        if (__e < __result.min)
          __result.min = __e;
        if (__result.max < __e)
          __result.max = __e;
      }
      return __result;
    } else if constexpr (forward_range<_Range>) {
      // Special-case the one element case. Avoid repeatedly initializing objects from the result of an iterator
      // dereference when doing so might not be idempotent. The `if constexpr` avoids the extra branch in cases where
      // it's not needed.
      if constexpr (!same_as<remove_cvref_t<range_reference_t<_Range>>, _ValueT> ||
                    is_rvalue_reference_v<range_reference_t<_Range>>) {
        if (ranges::next(__first) == __last) {
          // During initialization, members are allowed to refer to already initialized members
          // (see http://eel.is/c++draft/dcl.init.aggr#6)
          minmax_result<_ValueT> __result = {*__first, __result.min};
          return __result;
        }
      }
      auto __result = std::__minmax_element_impl(__first, __last, __comp, __proj);
      return {*__result.first, *__result.second};
    } else {
      // input_iterators can't be copied, so the implementation for input_iterators has to store
      // the values instead of a pointer to the correct values
      auto __less = [&](auto&& __a, auto&& __b) -> bool {
        return std::invoke(__comp,
                           std::invoke(__proj, std::forward<decltype(__a)>(__a)),
                           std::invoke(__proj, std::forward<decltype(__b)>(__b)));
      };

      // During initialization, members are allowed to refer to already initialized members
      // (see http://eel.is/c++draft/dcl.init.aggr#6)
      ranges::minmax_result<_ValueT> __result = {*__first, __result.min};
      if (__first == __last || ++__first == __last)
        return __result;

      if (__less(*__first, __result.min))
        __result.min = *__first;
      else
        __result.max = *__first;

      while (++__first != __last) {
        _ValueT __i = *__first;
        if (++__first == __last) {
          if (__less(__i, __result.min))
            __result.min = __i;
          else if (!__less(__i, __result.max))
            __result.max = __i;
          return __result;
        }

        if (__less(*__first, __i)) {
          if (__less(*__first, __result.min))
            __result.min = *__first;
          if (!__less(__i, __result.max))
            __result.max = std::move(__i);
        } else {
          if (__less(__i, __result.min))
            __result.min = std::move(__i);
          if (!__less(*__first, __result.max))
            __result.max = *__first;
        }
      }
      return __result;
    }
  }
};
} // namespace __minmax

inline namespace __cpo {
inline constexpr auto minmax = __minmax::__fn{};
} // namespace __cpo
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP_STD_VER >= 20

#endif // _LIBCPP___ALGORITHM_RANGES_MINMAX_H
