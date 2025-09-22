// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANGES_SUBRANGE_H
#define _LIBCPP___RANGES_SUBRANGE_H

#include <__assert>
#include <__concepts/constructible.h>
#include <__concepts/convertible_to.h>
#include <__concepts/copyable.h>
#include <__concepts/derived_from.h>
#include <__concepts/different_from.h>
#include <__config>
#include <__fwd/subrange.h>
#include <__iterator/advance.h>
#include <__iterator/concepts.h>
#include <__iterator/incrementable_traits.h>
#include <__iterator/iterator_traits.h>
#include <__ranges/access.h>
#include <__ranges/concepts.h>
#include <__ranges/dangling.h>
#include <__ranges/enable_borrowed_range.h>
#include <__ranges/size.h>
#include <__ranges/view_interface.h>
#include <__tuple/tuple_element.h>
#include <__tuple/tuple_like_no_subrange.h>
#include <__tuple/tuple_size.h>
#include <__type_traits/conditional.h>
#include <__type_traits/decay.h>
#include <__type_traits/is_pointer.h>
#include <__type_traits/is_reference.h>
#include <__type_traits/make_unsigned.h>
#include <__type_traits/remove_const.h>
#include <__type_traits/remove_pointer.h>
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
template <class _From, class _To>
concept __uses_nonqualification_pointer_conversion =
    is_pointer_v<_From> && is_pointer_v<_To> &&
    !convertible_to<remove_pointer_t<_From> (*)[], remove_pointer_t<_To> (*)[]>;

template <class _From, class _To>
concept __convertible_to_non_slicing =
    convertible_to<_From, _To> && !__uses_nonqualification_pointer_conversion<decay_t<_From>, decay_t<_To>>;

template <class _Pair, class _Iter, class _Sent>
concept __pair_like_convertible_from =
    !range<_Pair> && __pair_like_no_subrange<_Pair> && constructible_from<_Pair, _Iter, _Sent> &&
    __convertible_to_non_slicing<_Iter, tuple_element_t<0, _Pair>> && convertible_to<_Sent, tuple_element_t<1, _Pair>>;

template <input_or_output_iterator _Iter,
          sentinel_for<_Iter> _Sent = _Iter,
          subrange_kind _Kind       = sized_sentinel_for<_Sent, _Iter> ? subrange_kind::sized : subrange_kind::unsized>
  requires(_Kind == subrange_kind::sized || !sized_sentinel_for<_Sent, _Iter>)
class _LIBCPP_TEMPLATE_VIS subrange : public view_interface<subrange<_Iter, _Sent, _Kind>> {
public:
  // Note: this is an internal implementation detail that is public only for internal usage.
  static constexpr bool _StoreSize = (_Kind == subrange_kind::sized && !sized_sentinel_for<_Sent, _Iter>);

private:
  static constexpr bool _MustProvideSizeAtConstruction = !_StoreSize; // just to improve compiler diagnostics
  struct _Empty {
    _LIBCPP_HIDE_FROM_ABI constexpr _Empty(auto) noexcept {}
  };
  using _Size = conditional_t<_StoreSize, make_unsigned_t<iter_difference_t<_Iter>>, _Empty>;
  _LIBCPP_NO_UNIQUE_ADDRESS _Iter __begin_ = _Iter();
  _LIBCPP_NO_UNIQUE_ADDRESS _Sent __end_   = _Sent();
  _LIBCPP_NO_UNIQUE_ADDRESS _Size __size_  = 0;

public:
  _LIBCPP_HIDE_FROM_ABI subrange()
    requires default_initializable<_Iter>
  = default;

  _LIBCPP_HIDE_FROM_ABI constexpr subrange(__convertible_to_non_slicing<_Iter> auto __iter, _Sent __sent)
    requires _MustProvideSizeAtConstruction
      : __begin_(std::move(__iter)), __end_(std::move(__sent)) {}

  _LIBCPP_HIDE_FROM_ABI constexpr subrange(
      __convertible_to_non_slicing<_Iter> auto __iter, _Sent __sent, make_unsigned_t<iter_difference_t<_Iter>> __n)
    requires(_Kind == subrange_kind::sized)
      : __begin_(std::move(__iter)), __end_(std::move(__sent)), __size_(__n) {
    if constexpr (sized_sentinel_for<_Sent, _Iter>)
      _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS((__end_ - __begin_) == static_cast<iter_difference_t<_Iter>>(__n),
                                          "std::ranges::subrange was passed an invalid size hint");
  }

  template <__different_from<subrange> _Range>
    requires borrowed_range<_Range> && __convertible_to_non_slicing<iterator_t<_Range>, _Iter> &&
             convertible_to<sentinel_t<_Range>, _Sent>
             _LIBCPP_HIDE_FROM_ABI constexpr subrange(_Range&& __range)
               requires(!_StoreSize)
      : subrange(ranges::begin(__range), ranges::end(__range)) {}

  template <__different_from<subrange> _Range>
    requires borrowed_range<_Range> && __convertible_to_non_slicing<iterator_t<_Range>, _Iter> &&
             convertible_to<sentinel_t<_Range>, _Sent>
             _LIBCPP_HIDE_FROM_ABI constexpr subrange(_Range&& __range)
               requires _StoreSize && sized_range<_Range>
      : subrange(__range, ranges::size(__range)) {}

  template <borrowed_range _Range>
    requires __convertible_to_non_slicing<iterator_t<_Range>, _Iter> &&
             convertible_to<sentinel_t<_Range>, _Sent>
             _LIBCPP_HIDE_FROM_ABI constexpr subrange(_Range&& __range, make_unsigned_t<iter_difference_t<_Iter>> __n)
               requires(_Kind == subrange_kind::sized)
      : subrange(ranges::begin(__range), ranges::end(__range), __n) {}

  template <__pair_like_convertible_from<const _Iter&, const _Sent&> _Pair>
  _LIBCPP_HIDE_FROM_ABI constexpr operator _Pair() const {
    return _Pair(__begin_, __end_);
  }

  _LIBCPP_HIDE_FROM_ABI constexpr _Iter begin() const
    requires copyable<_Iter>
  {
    return __begin_;
  }

  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr _Iter begin()
    requires(!copyable<_Iter>)
  {
    return std::move(__begin_);
  }

  _LIBCPP_HIDE_FROM_ABI constexpr _Sent end() const { return __end_; }

  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr bool empty() const { return __begin_ == __end_; }

  _LIBCPP_HIDE_FROM_ABI constexpr make_unsigned_t<iter_difference_t<_Iter>> size() const
    requires(_Kind == subrange_kind::sized)
  {
    if constexpr (_StoreSize)
      return __size_;
    else
      return std::__to_unsigned_like(__end_ - __begin_);
  }

  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr subrange next(iter_difference_t<_Iter> __n = 1) const&
    requires forward_iterator<_Iter>
  {
    auto __tmp = *this;
    __tmp.advance(__n);
    return __tmp;
  }

  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr subrange next(iter_difference_t<_Iter> __n = 1) && {
    advance(__n);
    return std::move(*this);
  }

  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr subrange prev(iter_difference_t<_Iter> __n = 1) const
    requires bidirectional_iterator<_Iter>
  {
    auto __tmp = *this;
    __tmp.advance(-__n);
    return __tmp;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr subrange& advance(iter_difference_t<_Iter> __n) {
    if constexpr (bidirectional_iterator<_Iter>) {
      if (__n < 0) {
        ranges::advance(__begin_, __n);
        if constexpr (_StoreSize)
          __size_ += std::__to_unsigned_like(-__n);
        return *this;
      }
    }

    auto __d = __n - ranges::advance(__begin_, __n, __end_);
    if constexpr (_StoreSize)
      __size_ -= std::__to_unsigned_like(__d);
    return *this;
  }
};

template <input_or_output_iterator _Iter, sentinel_for<_Iter> _Sent>
subrange(_Iter, _Sent) -> subrange<_Iter, _Sent>;

template <input_or_output_iterator _Iter, sentinel_for<_Iter> _Sent>
subrange(_Iter, _Sent, make_unsigned_t<iter_difference_t<_Iter>>) -> subrange<_Iter, _Sent, subrange_kind::sized>;

template <borrowed_range _Range>
subrange(_Range&&) -> subrange<iterator_t<_Range>,
                               sentinel_t<_Range>,
                               (sized_range<_Range> || sized_sentinel_for<sentinel_t<_Range>, iterator_t<_Range>>)
                                   ? subrange_kind::sized
                                   : subrange_kind::unsized>;

template <borrowed_range _Range>
subrange(_Range&&, make_unsigned_t<range_difference_t<_Range>>)
    -> subrange<iterator_t<_Range>, sentinel_t<_Range>, subrange_kind::sized>;

template <size_t _Index, class _Iter, class _Sent, subrange_kind _Kind>
  requires((_Index == 0 && copyable<_Iter>) || _Index == 1)
_LIBCPP_HIDE_FROM_ABI constexpr auto get(const subrange<_Iter, _Sent, _Kind>& __subrange) {
  if constexpr (_Index == 0)
    return __subrange.begin();
  else
    return __subrange.end();
}

template <size_t _Index, class _Iter, class _Sent, subrange_kind _Kind>
  requires(_Index < 2)
_LIBCPP_HIDE_FROM_ABI constexpr auto get(subrange<_Iter, _Sent, _Kind>&& __subrange) {
  if constexpr (_Index == 0)
    return __subrange.begin();
  else
    return __subrange.end();
}

template <class _Ip, class _Sp, subrange_kind _Kp>
inline constexpr bool enable_borrowed_range<subrange<_Ip, _Sp, _Kp>> = true;

template <range _Rp>
using borrowed_subrange_t = _If<borrowed_range<_Rp>, subrange<iterator_t<_Rp>>, dangling>;
} // namespace ranges

// [range.subrange.general]

using ranges::get;

// [ranges.syn]

template <class _Ip, class _Sp, ranges::subrange_kind _Kp>
struct tuple_size<ranges::subrange<_Ip, _Sp, _Kp>> : integral_constant<size_t, 2> {};

template <class _Ip, class _Sp, ranges::subrange_kind _Kp>
struct tuple_element<0, ranges::subrange<_Ip, _Sp, _Kp>> {
  using type = _Ip;
};

template <class _Ip, class _Sp, ranges::subrange_kind _Kp>
struct tuple_element<1, ranges::subrange<_Ip, _Sp, _Kp>> {
  using type = _Sp;
};

template <class _Ip, class _Sp, ranges::subrange_kind _Kp>
struct tuple_element<0, const ranges::subrange<_Ip, _Sp, _Kp>> {
  using type = _Ip;
};

template <class _Ip, class _Sp, ranges::subrange_kind _Kp>
struct tuple_element<1, const ranges::subrange<_Ip, _Sp, _Kp>> {
  using type = _Sp;
};

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANGES_SUBRANGE_H
