// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANGES_TAKE_VIEW_H
#define _LIBCPP___RANGES_TAKE_VIEW_H

#include <__algorithm/min.h>
#include <__algorithm/ranges_min.h>
#include <__assert>
#include <__concepts/constructible.h>
#include <__concepts/convertible_to.h>
#include <__config>
#include <__functional/bind_back.h>
#include <__fwd/span.h>
#include <__fwd/string_view.h>
#include <__iterator/concepts.h>
#include <__iterator/counted_iterator.h>
#include <__iterator/default_sentinel.h>
#include <__iterator/distance.h>
#include <__iterator/iterator_traits.h>
#include <__ranges/access.h>
#include <__ranges/all.h>
#include <__ranges/concepts.h>
#include <__ranges/empty_view.h>
#include <__ranges/enable_borrowed_range.h>
#include <__ranges/iota_view.h>
#include <__ranges/range_adaptor.h>
#include <__ranges/repeat_view.h>
#include <__ranges/size.h>
#include <__ranges/subrange.h>
#include <__ranges/view_interface.h>
#include <__type_traits/decay.h>
#include <__type_traits/is_nothrow_constructible.h>
#include <__type_traits/maybe_const.h>
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
class take_view : public view_interface<take_view<_View>> {
  _LIBCPP_NO_UNIQUE_ADDRESS _View __base_ = _View();
  range_difference_t<_View> __count_      = 0;

  template <bool>
  class __sentinel;

public:
  _LIBCPP_HIDE_FROM_ABI take_view()
    requires default_initializable<_View>
  = default;

  _LIBCPP_HIDE_FROM_ABI constexpr _LIBCPP_EXPLICIT_SINCE_CXX23
  take_view(_View __base, range_difference_t<_View> __count)
      : __base_(std::move(__base)), __count_(__count) {
    _LIBCPP_ASSERT_UNCATEGORIZED(__count >= 0, "count has to be greater than or equal to zero");
  }

  _LIBCPP_HIDE_FROM_ABI constexpr _View base() const&
    requires copy_constructible<_View>
  {
    return __base_;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr _View base() && { return std::move(__base_); }

  _LIBCPP_HIDE_FROM_ABI constexpr auto begin()
    requires(!__simple_view<_View>)
  {
    if constexpr (sized_range<_View>) {
      if constexpr (random_access_range<_View>) {
        return ranges::begin(__base_);
      } else {
        using _DifferenceT = range_difference_t<_View>;
        auto __size        = size();
        return counted_iterator(ranges::begin(__base_), static_cast<_DifferenceT>(__size));
      }
    } else {
      return counted_iterator(ranges::begin(__base_), __count_);
    }
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto begin() const
    requires range<const _View>
  {
    if constexpr (sized_range<const _View>) {
      if constexpr (random_access_range<const _View>) {
        return ranges::begin(__base_);
      } else {
        using _DifferenceT = range_difference_t<const _View>;
        auto __size        = size();
        return counted_iterator(ranges::begin(__base_), static_cast<_DifferenceT>(__size));
      }
    } else {
      return counted_iterator(ranges::begin(__base_), __count_);
    }
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto end()
    requires(!__simple_view<_View>)
  {
    if constexpr (sized_range<_View>) {
      if constexpr (random_access_range<_View>) {
        return ranges::begin(__base_) + size();
      } else {
        return default_sentinel;
      }
    } else {
      return __sentinel<false>{ranges::end(__base_)};
    }
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto end() const
    requires range<const _View>
  {
    if constexpr (sized_range<const _View>) {
      if constexpr (random_access_range<const _View>) {
        return ranges::begin(__base_) + size();
      } else {
        return default_sentinel;
      }
    } else {
      return __sentinel<true>{ranges::end(__base_)};
    }
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto size()
    requires sized_range<_View>
  {
    auto __n = ranges::size(__base_);
    return ranges::min(__n, static_cast<decltype(__n)>(__count_));
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto size() const
    requires sized_range<const _View>
  {
    auto __n = ranges::size(__base_);
    return ranges::min(__n, static_cast<decltype(__n)>(__count_));
  }
};

template <view _View>
template <bool _Const>
class take_view<_View>::__sentinel {
  using _Base = __maybe_const<_Const, _View>;
  template <bool _OtherConst>
  using _Iter                                        = counted_iterator<iterator_t<__maybe_const<_OtherConst, _View>>>;
  _LIBCPP_NO_UNIQUE_ADDRESS sentinel_t<_Base> __end_ = sentinel_t<_Base>();

  template <bool>
  friend class take_view<_View>::__sentinel;

public:
  _LIBCPP_HIDE_FROM_ABI __sentinel() = default;

  _LIBCPP_HIDE_FROM_ABI constexpr explicit __sentinel(sentinel_t<_Base> __end) : __end_(std::move(__end)) {}

  _LIBCPP_HIDE_FROM_ABI constexpr __sentinel(__sentinel<!_Const> __s)
    requires _Const && convertible_to<sentinel_t<_View>, sentinel_t<_Base>>
      : __end_(std::move(__s.__end_)) {}

  _LIBCPP_HIDE_FROM_ABI constexpr sentinel_t<_Base> base() const { return __end_; }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(const _Iter<_Const>& __lhs, const __sentinel& __rhs) {
    return __lhs.count() == 0 || __lhs.base() == __rhs.__end_;
  }

  template <bool _OtherConst = !_Const>
    requires sentinel_for<sentinel_t<_Base>, iterator_t<__maybe_const<_OtherConst, _View>>>
  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(const _Iter<_OtherConst>& __lhs, const __sentinel& __rhs) {
    return __lhs.count() == 0 || __lhs.base() == __rhs.__end_;
  }
};

template <class _Range>
take_view(_Range&&, range_difference_t<_Range>) -> take_view<views::all_t<_Range>>;

template <class _Tp>
inline constexpr bool enable_borrowed_range<take_view<_Tp>> = enable_borrowed_range<_Tp>;

namespace views {
namespace __take {

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

template <class _Iter, class _Sent, subrange_kind _Kind>
inline constexpr bool __is_passthrough_specialization<subrange<_Iter, _Sent, _Kind>> = true;

template <class _Tp>
inline constexpr bool __is_iota_specialization = false;

template <class _Np, class _Bound>
inline constexpr bool __is_iota_specialization<iota_view<_Np, _Bound>> = true;

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

template <class _Iter, class _Sent, subrange_kind _Kind>
  requires requires { typename subrange<_Iter>; }
struct __passthrough_type<subrange<_Iter, _Sent, _Kind>> {
  using type = subrange<_Iter>;
};

template <class _Tp>
using __passthrough_type_t = typename __passthrough_type<_Tp>::type;

struct __fn {
  // [range.take.overview]: the `empty_view` case.
  template <class _Range, convertible_to<range_difference_t<_Range>> _Np>
    requires __is_empty_view<remove_cvref_t<_Range>>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Range&& __range, _Np&&) const
      noexcept(noexcept(_LIBCPP_AUTO_CAST(std::forward<_Range>(__range))))
          -> decltype(_LIBCPP_AUTO_CAST(std::forward<_Range>(__range))) {
    return _LIBCPP_AUTO_CAST(std::forward<_Range>(__range));
  }

  // [range.take.overview]: the `span | basic_string_view | subrange` case.
  template <class _Range,
            convertible_to<range_difference_t<_Range>> _Np,
            class _RawRange = remove_cvref_t<_Range>,
            class _Dist     = range_difference_t<_Range>>
    requires(!__is_empty_view<_RawRange> && random_access_range<_RawRange> && sized_range<_RawRange> &&
             __is_passthrough_specialization<_RawRange>)
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto
  operator()(_Range&& __rng, _Np&& __n) const noexcept(noexcept(__passthrough_type_t<_RawRange>(
      ranges::begin(__rng), ranges::begin(__rng) + std::min<_Dist>(ranges::distance(__rng), std::forward<_Np>(__n)))))
      -> decltype(__passthrough_type_t<_RawRange>(
          // Note: deliberately not forwarding `__rng` to guard against double moves.
          ranges::begin(__rng),
          ranges::begin(__rng) + std::min<_Dist>(ranges::distance(__rng), std::forward<_Np>(__n)))) {
    return __passthrough_type_t<_RawRange>(
        ranges::begin(__rng), ranges::begin(__rng) + std::min<_Dist>(ranges::distance(__rng), std::forward<_Np>(__n)));
  }

  // [range.take.overview]: the `iota_view` case.
  // clang-format off
  template <class _Range,
            convertible_to<range_difference_t<_Range>> _Np,
            class _RawRange = remove_cvref_t<_Range>,
            class _Dist     = range_difference_t<_Range>>
    requires (!__is_empty_view<_RawRange> &&
              random_access_range<_RawRange> &&
              sized_range<_RawRange> &&
              __is_iota_specialization<_RawRange>)
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI
  constexpr auto operator()(_Range&& __rng, _Np&& __n) const
    noexcept(noexcept(ranges::iota_view(
                              *ranges::begin(__rng),
                              *(ranges::begin(__rng) + std::min<_Dist>(ranges::distance(__rng), std::forward<_Np>(__n)))
                              )))
    -> decltype(      ranges::iota_view(
                              // Note: deliberately not forwarding `__rng` to guard against double moves.
                              *ranges::begin(__rng),
                              *(ranges::begin(__rng) + std::min<_Dist>(ranges::distance(__rng), std::forward<_Np>(__n)))
                              ))
    { return          ranges::iota_view(
                              *ranges::begin(__rng),
                              *(ranges::begin(__rng) + std::min<_Dist>(ranges::distance(__rng), std::forward<_Np>(__n)))
                              ); }

#if _LIBCPP_STD_VER >= 23
  // [range.take.overview]: the `repeat_view` "_RawRange models sized_range" case.
  template <class _Range,
            convertible_to<range_difference_t<_Range>> _Np,
            class _RawRange = remove_cvref_t<_Range>,
            class _Dist     = range_difference_t<_Range>>
    requires(__is_repeat_specialization<_RawRange> && sized_range<_RawRange>)
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Range&& __range, _Np&& __n) const
    noexcept(noexcept(views::repeat(*__range.__value_, std::min<_Dist>(ranges::distance(__range), std::forward<_Np>(__n)))))
    -> decltype(      views::repeat(*__range.__value_, std::min<_Dist>(ranges::distance(__range), std::forward<_Np>(__n))))
    { return          views::repeat(*__range.__value_, std::min<_Dist>(ranges::distance(__range), std::forward<_Np>(__n))); }

  // [range.take.overview]: the `repeat_view` "otherwise" case.
  template <class _Range,
            convertible_to<range_difference_t<_Range>> _Np,
            class _RawRange = remove_cvref_t<_Range>,
            class _Dist     = range_difference_t<_Range>>
    requires(__is_repeat_specialization<_RawRange> && !sized_range<_RawRange>)
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Range&& __range, _Np&& __n) const
    noexcept(noexcept(views::repeat(*__range.__value_, static_cast<_Dist>(__n))))
    -> decltype(      views::repeat(*__range.__value_, static_cast<_Dist>(__n)))
    { return          views::repeat(*__range.__value_, static_cast<_Dist>(__n)); }
#endif
  // clang-format on

  // [range.take.overview]: the "otherwise" case.
  template <class _Range, convertible_to<range_difference_t<_Range>> _Np, class _RawRange = remove_cvref_t<_Range>>
  // Note: without specifically excluding the other cases, GCC sees this overload as ambiguous with the other
  // overloads.
    requires(!(__is_empty_view<_RawRange> ||
#  if _LIBCPP_STD_VER >= 23
               __is_repeat_specialization<_RawRange> ||
#  endif
               (__is_iota_specialization<_RawRange> && sized_range<_RawRange> && random_access_range<_RawRange>) ||
               (__is_passthrough_specialization<_RawRange> && sized_range<_RawRange> &&
                random_access_range<_RawRange>)))
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Range&& __range, _Np&& __n) const
      noexcept(noexcept(take_view(std::forward<_Range>(__range), std::forward<_Np>(__n))))
          -> decltype(take_view(std::forward<_Range>(__range), std::forward<_Np>(__n))) {
    return take_view(std::forward<_Range>(__range), std::forward<_Np>(__n));
  }

  template <class _Np>
    requires constructible_from<decay_t<_Np>, _Np>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Np&& __n) const
      noexcept(is_nothrow_constructible_v<decay_t<_Np>, _Np>) {
    return __range_adaptor_closure_t(std::__bind_back(*this, std::forward<_Np>(__n)));
  }
};

} // namespace __take

inline namespace __cpo {
inline constexpr auto take = __take::__fn{};
} // namespace __cpo
} // namespace views

} // namespace ranges

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANGES_TAKE_VIEW_H
