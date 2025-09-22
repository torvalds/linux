//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___COMPARE_COMMON_COMPARISON_CATEGORY_H
#define _LIBCPP___COMPARE_COMMON_COMPARISON_CATEGORY_H

#include <__compare/ordering.h>
#include <__config>
#include <__type_traits/is_same.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

namespace __comp_detail {

enum _ClassifyCompCategory : unsigned { _None, _PartialOrd, _WeakOrd, _StrongOrd, _CCC_Size };

template <class _Tp>
_LIBCPP_HIDE_FROM_ABI constexpr _ClassifyCompCategory __type_to_enum() noexcept {
  if (is_same_v<_Tp, partial_ordering>)
    return _PartialOrd;
  if (is_same_v<_Tp, weak_ordering>)
    return _WeakOrd;
  if (is_same_v<_Tp, strong_ordering>)
    return _StrongOrd;
  return _None;
}

template <size_t _Size>
_LIBCPP_HIDE_FROM_ABI constexpr _ClassifyCompCategory
__compute_comp_type(const _ClassifyCompCategory (&__types)[_Size]) {
  int __seen[_CCC_Size] = {};
  for (auto __type : __types)
    ++__seen[__type];
  if (__seen[_None])
    return _None;
  if (__seen[_PartialOrd])
    return _PartialOrd;
  if (__seen[_WeakOrd])
    return _WeakOrd;
  return _StrongOrd;
}

template <class... _Ts, bool _False = false>
_LIBCPP_HIDE_FROM_ABI constexpr auto __get_comp_type() {
  using _CCC                    = _ClassifyCompCategory;
  constexpr _CCC __type_kinds[] = {_StrongOrd, __type_to_enum<_Ts>()...};
  constexpr _CCC __cat          = __comp_detail::__compute_comp_type(__type_kinds);
  if constexpr (__cat == _None)
    return void();
  else if constexpr (__cat == _PartialOrd)
    return partial_ordering::equivalent;
  else if constexpr (__cat == _WeakOrd)
    return weak_ordering::equivalent;
  else if constexpr (__cat == _StrongOrd)
    return strong_ordering::equivalent;
  else
    static_assert(_False, "unhandled case");
}
} // namespace __comp_detail

// [cmp.common], common comparison category type
template <class... _Ts>
struct _LIBCPP_TEMPLATE_VIS common_comparison_category {
  using type = decltype(__comp_detail::__get_comp_type<_Ts...>());
};

template <class... _Ts>
using common_comparison_category_t = typename common_comparison_category<_Ts...>::type;

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___COMPARE_COMMON_COMPARISON_CATEGORY_H
