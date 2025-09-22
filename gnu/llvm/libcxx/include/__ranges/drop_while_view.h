// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANGES_DROP_WHILE_VIEW_H
#define _LIBCPP___RANGES_DROP_WHILE_VIEW_H

#include <__algorithm/ranges_find_if_not.h>
#include <__assert>
#include <__concepts/constructible.h>
#include <__config>
#include <__functional/bind_back.h>
#include <__functional/reference_wrapper.h>
#include <__iterator/concepts.h>
#include <__ranges/access.h>
#include <__ranges/all.h>
#include <__ranges/concepts.h>
#include <__ranges/enable_borrowed_range.h>
#include <__ranges/movable_box.h>
#include <__ranges/non_propagating_cache.h>
#include <__ranges/range_adaptor.h>
#include <__ranges/view_interface.h>
#include <__type_traits/conditional.h>
#include <__type_traits/decay.h>
#include <__type_traits/is_nothrow_constructible.h>
#include <__type_traits/is_object.h>
#include <__utility/forward.h>
#include <__utility/in_place.h>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

namespace ranges {

template <view _View, class _Pred>
  requires input_range<_View> && is_object_v<_Pred> && indirect_unary_predicate<const _Pred, iterator_t<_View>>
class _LIBCPP_ABI_LLVM18_NO_UNIQUE_ADDRESS drop_while_view : public view_interface<drop_while_view<_View, _Pred>> {
public:
  _LIBCPP_HIDE_FROM_ABI drop_while_view()
    requires default_initializable<_View> && default_initializable<_Pred>
  = default;

  _LIBCPP_HIDE_FROM_ABI constexpr _LIBCPP_EXPLICIT_SINCE_CXX23 drop_while_view(_View __base, _Pred __pred)
      : __base_(std::move(__base)), __pred_(std::in_place, std::move(__pred)) {}

  _LIBCPP_HIDE_FROM_ABI constexpr _View base() const&
    requires copy_constructible<_View>
  {
    return __base_;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr _View base() && { return std::move(__base_); }

  _LIBCPP_HIDE_FROM_ABI constexpr const _Pred& pred() const { return *__pred_; }

  _LIBCPP_HIDE_FROM_ABI constexpr auto begin() {
    // Note: this duplicates a check in `optional` but provides a better error message.
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        __pred_.__has_value(),
        "drop_while_view needs to have a non-empty predicate before calling begin() -- did a previous "
        "assignment to this drop_while_view fail?");
    if constexpr (_UseCache) {
      if (!__cached_begin_.__has_value()) {
        __cached_begin_.__emplace(ranges::find_if_not(__base_, std::cref(*__pred_)));
      }
      return *__cached_begin_;
    } else {
      return ranges::find_if_not(__base_, std::cref(*__pred_));
    }
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto end() { return ranges::end(__base_); }

private:
  _LIBCPP_NO_UNIQUE_ADDRESS _View __base_ = _View();
  _LIBCPP_NO_UNIQUE_ADDRESS __movable_box<_Pred> __pred_;

  static constexpr bool _UseCache = forward_range<_View>;
  using _Cache                    = _If<_UseCache, __non_propagating_cache<iterator_t<_View>>, __empty_cache>;
  _LIBCPP_NO_UNIQUE_ADDRESS _Cache __cached_begin_ = _Cache();
};

template <class _View, class _Pred>
inline constexpr bool enable_borrowed_range<drop_while_view<_View, _Pred>> = enable_borrowed_range<_View>;

template <class _Range, class _Pred>
drop_while_view(_Range&&, _Pred) -> drop_while_view<views::all_t<_Range>, _Pred>;

namespace views {
namespace __drop_while {

struct __fn {
  template <class _Range, class _Pred>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Range&& __range, _Pred&& __pred) const
      noexcept(noexcept(/**/ drop_while_view(std::forward<_Range>(__range), std::forward<_Pred>(__pred))))
          -> decltype(/*--*/ drop_while_view(std::forward<_Range>(__range), std::forward<_Pred>(__pred))) {
    return /*-------------*/ drop_while_view(std::forward<_Range>(__range), std::forward<_Pred>(__pred));
  }

  template <class _Pred>
    requires constructible_from<decay_t<_Pred>, _Pred>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Pred&& __pred) const
      noexcept(is_nothrow_constructible_v<decay_t<_Pred>, _Pred>) {
    return __range_adaptor_closure_t(std::__bind_back(*this, std::forward<_Pred>(__pred)));
  }
};

} // namespace __drop_while

inline namespace __cpo {
inline constexpr auto drop_while = __drop_while::__fn{};
} // namespace __cpo
} // namespace views
} // namespace ranges

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANGES_DROP_WHILE_VIEW_H
