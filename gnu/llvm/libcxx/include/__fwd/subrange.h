//===---------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//

#ifndef _LIBCPP___FWD_SUBRANGE_H
#define _LIBCPP___FWD_SUBRANGE_H

#include <__concepts/copyable.h>
#include <__config>
#include <__iterator/concepts.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

namespace ranges {

enum class subrange_kind : bool { unsized, sized };

template <input_or_output_iterator _Iter, sentinel_for<_Iter> _Sent, subrange_kind _Kind>
  requires(_Kind == subrange_kind::sized || !sized_sentinel_for<_Sent, _Iter>)
class _LIBCPP_TEMPLATE_VIS subrange;

template <size_t _Index, class _Iter, class _Sent, subrange_kind _Kind>
  requires((_Index == 0 && copyable<_Iter>) || _Index == 1)
_LIBCPP_HIDE_FROM_ABI constexpr auto get(const subrange<_Iter, _Sent, _Kind>&);

template <size_t _Index, class _Iter, class _Sent, subrange_kind _Kind>
  requires(_Index < 2)
_LIBCPP_HIDE_FROM_ABI constexpr auto get(subrange<_Iter, _Sent, _Kind>&&);

} // namespace ranges

using ranges::get;

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

#endif // _LIBCPP___FWD_SUBRANGE_H
