// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANGES_EMPTY_VIEW_H
#define _LIBCPP___RANGES_EMPTY_VIEW_H

#include <__config>
#include <__ranges/enable_borrowed_range.h>
#include <__ranges/view_interface.h>
#include <__type_traits/is_object.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

namespace ranges {
template <class _Tp>
  requires is_object_v<_Tp>
class empty_view : public view_interface<empty_view<_Tp>> {
public:
  _LIBCPP_HIDE_FROM_ABI static constexpr _Tp* begin() noexcept { return nullptr; }
  _LIBCPP_HIDE_FROM_ABI static constexpr _Tp* end() noexcept { return nullptr; }
  _LIBCPP_HIDE_FROM_ABI static constexpr _Tp* data() noexcept { return nullptr; }
  _LIBCPP_HIDE_FROM_ABI static constexpr size_t size() noexcept { return 0; }
  _LIBCPP_HIDE_FROM_ABI static constexpr bool empty() noexcept { return true; }
};

template <class _Tp>
inline constexpr bool enable_borrowed_range<empty_view<_Tp>> = true;

namespace views {

template <class _Tp>
inline constexpr empty_view<_Tp> empty{};

} // namespace views
} // namespace ranges

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___RANGES_EMPTY_VIEW_H
