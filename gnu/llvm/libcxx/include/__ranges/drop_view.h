// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANGES_DROP_VIEW_H
#define _LIBCPP___RANGES_DROP_VIEW_H

#include <__algorithm/min.h>
#include <__assert>
#include <__concepts/constructible.h>
#include <__concepts/convertible_to.h>
#include <__config>
#include <__functional/bind_back.h>
#include <__fwd/span.h>
#include <__fwd/string_view.h>
#include <__iterator/concepts.h>
#include <__iterator/distance.h>
#include <__iterator/iterator_traits.h>
#include <__iterator/next.h>
#include <__ranges/access.h>
#include <__ranges/all.h>
#include <__ranges/concepts.h>
#include <__ranges/empty_view.h>
#include <__ranges/enable_borrowed_range.h>
#include <__ranges/iota_view.h>
#include <__ranges/non_propagating_cache.h>
#include <__ranges/range_adaptor.h>
#include <__ranges/repeat_view.h>
#include <__ranges/size.h>
#include <__ranges/subrange.h>
#include <__ranges/view_interface.h>
#include <__type_traits/conditional.h>
#include <__type_traits/decay.h>
#include <__type_traits/is_nothrow_constructible.h>
#include <__type_traits/make_unsigned.h>
#include <__type_traits/remove_cvref.h>
#include <__utility/auto_cast.h>
#include <__utility/forward.h>
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
template <view _View>
class drop_view : public view_interface<drop_view<_View>> {
  // We cache begin() whenever ranges::next is not guaranteed O(1) to provide an
  // amortized O(1) begin() method. If this is an input_range, then we cannot cache
  // begin because begin is not equality preserving.
  // Note: drop_view<input-range>::begin() is still trivially amortized O(1) because
  // one can't call begin() on it more than once.
  static constexpr bool _UseCache = forward_range<_View> && !(random_access_range<_View> && sized_range<_View>);
  using _Cache                    = _If<_UseCache, __non_propagating_cache<iterator_t<_View>>, __empty_cache>;
  _LIBCPP_NO_UNIQUE_ADDRESS _Cache __cached_begin_ = _Cache();
  range_difference_t<_View> __count_               = 0;
  _View __base_                                    = _View();

public:
  _LIBCPP_HIDE_FROM_ABI drop_view()
    requires default_initializable<_View>
  = default;

  _LIBCPP_HIDE_FROM_ABI constexpr _LIBCPP_EXPLICIT_SINCE_CXX23
  drop_view(_View __base, range_difference_t<_View> __count)
      : __count_(__count), __base_(std::move(__base)) {
    _LIBCPP_ASSERT_UNCATEGORIZED(__count_ >= 0, "count must be greater than or equal to zero.");
  }

  _LIBCPP_HIDE_FROM_ABI constexpr _View base() const&
    requires copy_constructible<_View>
  {
    return __base_;
  }
  _LIBCPP_HIDE_FROM_ABI constexpr _View base() && { return std::move(__base_); }

  _LIBCPP_HIDE_FROM_ABI constexpr auto begin()
    requires(!(__simple_view<_View> && random_access_range<const _View> && sized_range<const _View>))
  {
    if constexpr (random_access_range<_View> && sized_range<_View>) {
      const auto __dist = std::min(ranges::distance(__base_), __count_);
      return ranges::begin(__base_) + __dist;
    }
    if constexpr (_UseCache)
      if (__cached_begin_.__has_value())
        return *__cached_begin_;

    auto __tmp = ranges::next(ranges::begin(__base_), __count_, ranges::end(__base_));
    if constexpr (_UseCache)
      __cached_begin_.__emplace(__tmp);
    return __tmp;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto begin() const
    requires random_access_range<const _View> && sized_range<const _View>
  {
    const auto __dist = std::min(ranges::distance(__base_), __count_);
    return ranges::begin(__base_) + __dist;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto end()
    requires(!__simple_view<_View>)
  {
    return ranges::end(__base_);
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto end() const
    requires range<const _View>
  {
    return ranges::end(__base_);
  }

  _LIBCPP_HIDE_FROM_ABI static constexpr auto __size(auto& __self) {
    const auto __s = ranges::size(__self.__base_);
    const auto __c = static_cast<decltype(__s)>(__self.__count_);
    return __s < __c ? 0 : __s - __c;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto size()
    requires sized_range<_View>
  {
    return __size(*this);
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto size() const
    requires sized_range<const _View>
  {
    return __size(*this);
  }
};

template <class _Range>
drop_view(_Range&&, range_difference_t<_Range>) -> drop_view<views::all_t<_Range>>;

template <class _Tp>
inline constexpr bool enable_borrowed_range<drop_view<_Tp>> = enable_borrowed_range<_Tp>;

namespace views {
namespace __drop {

template <class _Tp>
inline constexpr bool __is_empty_view = false;

template <class _Tp>
inline constexpr bool __is_empty_view<empty_view<_Tp>> = true;

template <class _Tp>
inline constexpr bool __is_passthrough_specialization = false;

template <class _Tp, size_t _Extent>
inline constexpr bool __is_passthrough_specialization<span<_Tp, _Extent>> = true;

template <class _CharT, class _Traits>
inline constexpr bool __is_passthrough_specialization<basic_string_view<_CharT, _Traits>> = true;

template <class _Np, class _Bound>
inline constexpr bool __is_passthrough_specialization<iota_view<_Np, _Bound>> = true;

template <class _Iter, class _Sent, subrange_kind _Kind>
inline constexpr bool __is_passthrough_specialization<subrange<_Iter, _Sent, _Kind>> =
    !subrange<_Iter, _Sent, _Kind>::_StoreSize;

template <class _Tp>
inline constexpr bool __is_subrange_specialization_with_store_size = false;

template <class _Iter, class _Sent, subrange_kind _Kind>
inline constexpr bool __is_subrange_specialization_with_store_size<subrange<_Iter, _Sent, _Kind>> =
    subrange<_Iter, _Sent, _Kind>::_StoreSize;

template <class _Tp>
struct __passthrough_type;

template <class _Tp, size_t _Extent>
struct __passthrough_type<span<_Tp, _Extent>> {
  using type = span<_Tp>;
};

template <class _CharT, class _Traits>
struct __passthrough_type<basic_string_view<_CharT, _Traits>> {
  using type = basic_string_view<_CharT, _Traits>;
};

template <class _Np, class _Bound>
struct __passthrough_type<iota_view<_Np, _Bound>> {
  using type = iota_view<_Np, _Bound>;
};

template <class _Iter, class _Sent, subrange_kind _Kind>
struct __passthrough_type<subrange<_Iter, _Sent, _Kind>> {
  using type = subrange<_Iter, _Sent, _Kind>;
};

template <class _Tp>
using __passthrough_type_t = typename __passthrough_type<_Tp>::type;

struct __fn {
  // [range.drop.overview]: the `empty_view` case.
  template <class _Range, convertible_to<range_difference_t<_Range>> _Np>
    requires __is_empty_view<remove_cvref_t<_Range>>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Range&& __range, _Np&&) const
      noexcept(noexcept(_LIBCPP_AUTO_CAST(std::forward<_Range>(__range))))
          -> decltype(_LIBCPP_AUTO_CAST(std::forward<_Range>(__range))) {
    return _LIBCPP_AUTO_CAST(std::forward<_Range>(__range));
  }

  // [range.drop.overview]: the `span | basic_string_view | iota_view | subrange (StoreSize == false)` case.
  template <class _Range,
            convertible_to<range_difference_t<_Range>> _Np,
            class _RawRange = remove_cvref_t<_Range>,
            class _Dist     = range_difference_t<_Range>>
    requires(!__is_empty_view<_RawRange> && random_access_range<_RawRange> && sized_range<_RawRange> &&
             __is_passthrough_specialization<_RawRange>)
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Range&& __rng, _Np&& __n) const
      noexcept(noexcept(__passthrough_type_t<_RawRange>(
          ranges::begin(__rng) + std::min<_Dist>(ranges::distance(__rng), std::forward<_Np>(__n)), ranges::end(__rng))))
          -> decltype(__passthrough_type_t<_RawRange>(
              // Note: deliberately not forwarding `__rng` to guard against double moves.
              ranges::begin(__rng) + std::min<_Dist>(ranges::distance(__rng), std::forward<_Np>(__n)),
              ranges::end(__rng))) {
    return __passthrough_type_t<_RawRange>(
        ranges::begin(__rng) + std::min<_Dist>(ranges::distance(__rng), std::forward<_Np>(__n)), ranges::end(__rng));
  }

  // [range.drop.overview]: the `subrange (StoreSize == true)` case.
  template <class _Range,
            convertible_to<range_difference_t<_Range>> _Np,
            class _RawRange = remove_cvref_t<_Range>,
            class _Dist     = range_difference_t<_Range>>
    requires(!__is_empty_view<_RawRange> && random_access_range<_RawRange> && sized_range<_RawRange> &&
             __is_subrange_specialization_with_store_size<_RawRange>)
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Range&& __rng, _Np&& __n) const noexcept(noexcept(
      _RawRange(ranges::begin(__rng) + std::min<_Dist>(ranges::distance(__rng), std::forward<_Np>(__n)),
                ranges::end(__rng),
                std::__to_unsigned_like(ranges::distance(__rng) -
                                        std::min<_Dist>(ranges::distance(__rng), std::forward<_Np>(__n))))))
      -> decltype(_RawRange(
          // Note: deliberately not forwarding `__rng` to guard against double moves.
          ranges::begin(__rng) + std::min<_Dist>(ranges::distance(__rng), std::forward<_Np>(__n)),
          ranges::end(__rng),
          std::__to_unsigned_like(ranges::distance(__rng) -
                                  std::min<_Dist>(ranges::distance(__rng), std::forward<_Np>(__n))))) {
    // Introducing local variables avoids calculating `min` and `distance` twice (at the cost of diverging from the
    // expression used in the `noexcept` clause and the return statement).
    auto __dist    = ranges::distance(__rng);
    auto __clamped = std::min<_Dist>(__dist, std::forward<_Np>(__n));
    return _RawRange(ranges::begin(__rng) + __clamped, ranges::end(__rng), std::__to_unsigned_like(__dist - __clamped));
  }
  // clang-format off
#if _LIBCPP_STD_VER >= 23
  // [range.drop.overview]: the `repeat_view` "_RawRange models sized_range" case.
  template <class _Range,
            convertible_to<range_difference_t<_Range>> _Np,
            class _RawRange = remove_cvref_t<_Range>,
            class _Dist     = range_difference_t<_Range>>
    requires (__is_repeat_specialization<_RawRange> && sized_range<_RawRange>)
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Range&& __range, _Np&& __n) const
    noexcept(noexcept(views::repeat(*__range.__value_, ranges::distance(__range) - std::min<_Dist>(ranges::distance(__range), std::forward<_Np>(__n)))))
    -> decltype(      views::repeat(*__range.__value_, ranges::distance(__range) - std::min<_Dist>(ranges::distance(__range), std::forward<_Np>(__n))))
    { return          views::repeat(*__range.__value_, ranges::distance(__range) - std::min<_Dist>(ranges::distance(__range), std::forward<_Np>(__n))); }

  // [range.drop.overview]: the `repeat_view` "otherwise" case.
  template <class _Range,
            convertible_to<range_difference_t<_Range>> _Np,
            class _RawRange = remove_cvref_t<_Range>,
            class _Dist     = range_difference_t<_Range>>
    requires (__is_repeat_specialization<_RawRange> && !sized_range<_RawRange>)
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI
  constexpr auto operator()(_Range&& __range, _Np&&) const
    noexcept(noexcept(_LIBCPP_AUTO_CAST(std::forward<_Range>(__range))))
    -> decltype(      _LIBCPP_AUTO_CAST(std::forward<_Range>(__range)))
    { return          _LIBCPP_AUTO_CAST(std::forward<_Range>(__range)); }
#endif
  // clang-format on

  // [range.drop.overview]: the "otherwise" case.
  template <class _Range, convertible_to<range_difference_t<_Range>> _Np, class _RawRange = remove_cvref_t<_Range>>
  // Note: without specifically excluding the other cases, GCC sees this overload as ambiguous with the other
  // overloads.
    requires(!(__is_empty_view<_RawRange> ||
#  if _LIBCPP_STD_VER >= 23
               __is_repeat_specialization<_RawRange> ||
#  endif
               (__is_subrange_specialization_with_store_size<_RawRange> && sized_range<_RawRange> &&
                random_access_range<_RawRange>) ||
               (__is_passthrough_specialization<_RawRange> && sized_range<_RawRange> &&
                random_access_range<_RawRange>)))
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Range&& __range, _Np&& __n) const
      noexcept(noexcept(drop_view(std::forward<_Range>(__range), std::forward<_Np>(__n))))
          -> decltype(drop_view(std::forward<_Range>(__range), std::forward<_Np>(__n))) {
    return drop_view(std::forward<_Range>(__range), std::forward<_Np>(__n));
  }

  template <class _Np>
    requires constructible_from<decay_t<_Np>, _Np>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Np&& __n) const
      noexcept(is_nothrow_constructible_v<decay_t<_Np>, _Np>) {
    return __range_adaptor_closure_t(std::__bind_back(*this, std::forward<_Np>(__n)));
  }
};

} // namespace __drop

inline namespace __cpo {
inline constexpr auto drop = __drop::__fn{};
} // namespace __cpo
} // namespace views

} // namespace ranges

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANGES_DROP_VIEW_H
