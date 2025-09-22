// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANGES_SINGLE_VIEW_H
#define _LIBCPP___RANGES_SINGLE_VIEW_H

#include <__concepts/constructible.h>
#include <__config>
#include <__ranges/movable_box.h>
#include <__ranges/range_adaptor.h>
#include <__ranges/view_interface.h>
#include <__type_traits/decay.h>
#include <__type_traits/is_object.h>
#include <__utility/forward.h>
#include <__utility/in_place.h>
#include <__utility/move.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

namespace ranges {
#  if _LIBCPP_STD_VER >= 23
template <move_constructible _Tp>
#  else
template <copy_constructible _Tp>
#  endif
  requires is_object_v<_Tp>
class _LIBCPP_ABI_LLVM18_NO_UNIQUE_ADDRESS single_view : public view_interface<single_view<_Tp>> {
  _LIBCPP_NO_UNIQUE_ADDRESS __movable_box<_Tp> __value_;

public:
  _LIBCPP_HIDE_FROM_ABI single_view()
    requires default_initializable<_Tp>
  = default;

  _LIBCPP_HIDE_FROM_ABI constexpr explicit single_view(const _Tp& __t)
#  if _LIBCPP_STD_VER >= 23
    requires copy_constructible<_Tp>
#  endif
      : __value_(in_place, __t) {
  }

  _LIBCPP_HIDE_FROM_ABI constexpr explicit single_view(_Tp&& __t) : __value_(in_place, std::move(__t)) {}

  template <class... _Args>
    requires constructible_from<_Tp, _Args...>
  _LIBCPP_HIDE_FROM_ABI constexpr explicit single_view(in_place_t, _Args&&... __args)
      : __value_{in_place, std::forward<_Args>(__args)...} {}

  _LIBCPP_HIDE_FROM_ABI constexpr _Tp* begin() noexcept { return data(); }

  _LIBCPP_HIDE_FROM_ABI constexpr const _Tp* begin() const noexcept { return data(); }

  _LIBCPP_HIDE_FROM_ABI constexpr _Tp* end() noexcept { return data() + 1; }

  _LIBCPP_HIDE_FROM_ABI constexpr const _Tp* end() const noexcept { return data() + 1; }

  _LIBCPP_HIDE_FROM_ABI static constexpr bool empty() noexcept { return false; }

  _LIBCPP_HIDE_FROM_ABI static constexpr size_t size() noexcept { return 1; }

  _LIBCPP_HIDE_FROM_ABI constexpr _Tp* data() noexcept { return __value_.operator->(); }

  _LIBCPP_HIDE_FROM_ABI constexpr const _Tp* data() const noexcept { return __value_.operator->(); }
};

template <class _Tp>
single_view(_Tp) -> single_view<_Tp>;

namespace views {
namespace __single_view {

struct __fn : __range_adaptor_closure<__fn> {
  template <class _Range>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Range&& __range) const
      noexcept(noexcept(single_view<decay_t<_Range&&>>(std::forward<_Range>(__range))))
          -> decltype(single_view<decay_t<_Range&&>>(std::forward<_Range>(__range))) {
    return single_view<decay_t<_Range&&>>(std::forward<_Range>(__range));
  }
};
} // namespace __single_view

inline namespace __cpo {
inline constexpr auto single = __single_view::__fn{};
} // namespace __cpo

} // namespace views
} // namespace ranges

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANGES_SINGLE_VIEW_H
