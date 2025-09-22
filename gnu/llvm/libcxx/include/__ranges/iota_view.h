// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANGES_IOTA_VIEW_H
#define _LIBCPP___RANGES_IOTA_VIEW_H

#include <__assert>
#include <__compare/three_way_comparable.h>
#include <__concepts/arithmetic.h>
#include <__concepts/constructible.h>
#include <__concepts/convertible_to.h>
#include <__concepts/copyable.h>
#include <__concepts/equality_comparable.h>
#include <__concepts/invocable.h>
#include <__concepts/same_as.h>
#include <__concepts/semiregular.h>
#include <__concepts/totally_ordered.h>
#include <__config>
#include <__iterator/concepts.h>
#include <__iterator/incrementable_traits.h>
#include <__iterator/iterator_traits.h>
#include <__iterator/unreachable_sentinel.h>
#include <__ranges/enable_borrowed_range.h>
#include <__ranges/movable_box.h>
#include <__ranges/view_interface.h>
#include <__type_traits/conditional.h>
#include <__type_traits/is_nothrow_constructible.h>
#include <__type_traits/make_unsigned.h>
#include <__type_traits/type_identity.h>
#include <__utility/forward.h>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

namespace ranges {
template <class _Int>
struct __get_wider_signed {
  consteval static auto __call() {
    if constexpr (sizeof(_Int) < sizeof(short))
      return type_identity<short>{};
    else if constexpr (sizeof(_Int) < sizeof(int))
      return type_identity<int>{};
    else if constexpr (sizeof(_Int) < sizeof(long))
      return type_identity<long>{};
    else
      return type_identity<long long>{};

    static_assert(
        sizeof(_Int) <= sizeof(long long), "Found integer-like type that is bigger than largest integer like type.");
  }

  using type = typename decltype(__call())::type;
};

template <class _Start>
using _IotaDiffT =
    typename _If< (!integral<_Start> || sizeof(iter_difference_t<_Start>) > sizeof(_Start)),
                  type_identity<iter_difference_t<_Start>>,
                  __get_wider_signed<_Start> >::type;

template <class _Iter>
concept __decrementable = incrementable<_Iter> && requires(_Iter __i) {
  { --__i } -> same_as<_Iter&>;
  { __i-- } -> same_as<_Iter>;
};

template <class _Iter>
concept __advanceable =
    __decrementable<_Iter> && totally_ordered<_Iter> &&
    requires(_Iter __i, const _Iter __j, const _IotaDiffT<_Iter> __n) {
      { __i += __n } -> same_as<_Iter&>;
      { __i -= __n } -> same_as<_Iter&>;
      _Iter(__j + __n);
      _Iter(__n + __j);
      _Iter(__j - __n);
      { __j - __j } -> convertible_to<_IotaDiffT<_Iter>>;
    };

template <class>
struct __iota_iterator_category {};

template <incrementable _Tp>
struct __iota_iterator_category<_Tp> {
  using iterator_category = input_iterator_tag;
};

template <weakly_incrementable _Start, semiregular _BoundSentinel = unreachable_sentinel_t>
  requires __weakly_equality_comparable_with<_Start, _BoundSentinel> && copyable<_Start>
class iota_view : public view_interface<iota_view<_Start, _BoundSentinel>> {
  struct __iterator : public __iota_iterator_category<_Start> {
    friend class iota_view;

    using iterator_concept =
        _If<__advanceable<_Start>,
            random_access_iterator_tag,
            _If<__decrementable<_Start>,
                bidirectional_iterator_tag,
                _If<incrementable<_Start>,
                    forward_iterator_tag,
                    /*Else*/ input_iterator_tag>>>;

    using value_type      = _Start;
    using difference_type = _IotaDiffT<_Start>;

    _Start __value_ = _Start();

    _LIBCPP_HIDE_FROM_ABI __iterator()
      requires default_initializable<_Start>
    = default;

    _LIBCPP_HIDE_FROM_ABI constexpr explicit __iterator(_Start __value) : __value_(std::move(__value)) {}

    _LIBCPP_HIDE_FROM_ABI constexpr _Start operator*() const noexcept(is_nothrow_copy_constructible_v<_Start>) {
      return __value_;
    }

    _LIBCPP_HIDE_FROM_ABI constexpr __iterator& operator++() {
      ++__value_;
      return *this;
    }

    _LIBCPP_HIDE_FROM_ABI constexpr void operator++(int) { ++*this; }

    _LIBCPP_HIDE_FROM_ABI constexpr __iterator operator++(int)
      requires incrementable<_Start>
    {
      auto __tmp = *this;
      ++*this;
      return __tmp;
    }

    _LIBCPP_HIDE_FROM_ABI constexpr __iterator& operator--()
      requires __decrementable<_Start>
    {
      --__value_;
      return *this;
    }

    _LIBCPP_HIDE_FROM_ABI constexpr __iterator operator--(int)
      requires __decrementable<_Start>
    {
      auto __tmp = *this;
      --*this;
      return __tmp;
    }

    _LIBCPP_HIDE_FROM_ABI constexpr __iterator& operator+=(difference_type __n)
      requires __advanceable<_Start>
    {
      if constexpr (__integer_like<_Start> && !__signed_integer_like<_Start>) {
        if (__n >= difference_type(0)) {
          __value_ += static_cast<_Start>(__n);
        } else {
          __value_ -= static_cast<_Start>(-__n);
        }
      } else {
        __value_ += __n;
      }
      return *this;
    }

    _LIBCPP_HIDE_FROM_ABI constexpr __iterator& operator-=(difference_type __n)
      requires __advanceable<_Start>
    {
      if constexpr (__integer_like<_Start> && !__signed_integer_like<_Start>) {
        if (__n >= difference_type(0)) {
          __value_ -= static_cast<_Start>(__n);
        } else {
          __value_ += static_cast<_Start>(-__n);
        }
      } else {
        __value_ -= __n;
      }
      return *this;
    }

    _LIBCPP_HIDE_FROM_ABI constexpr _Start operator[](difference_type __n) const
      requires __advanceable<_Start>
    {
      return _Start(__value_ + __n);
    }

    _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(const __iterator& __x, const __iterator& __y)
      requires equality_comparable<_Start>
    {
      return __x.__value_ == __y.__value_;
    }

    _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator<(const __iterator& __x, const __iterator& __y)
      requires totally_ordered<_Start>
    {
      return __x.__value_ < __y.__value_;
    }

    _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator>(const __iterator& __x, const __iterator& __y)
      requires totally_ordered<_Start>
    {
      return __y < __x;
    }

    _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator<=(const __iterator& __x, const __iterator& __y)
      requires totally_ordered<_Start>
    {
      return !(__y < __x);
    }

    _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator>=(const __iterator& __x, const __iterator& __y)
      requires totally_ordered<_Start>
    {
      return !(__x < __y);
    }

    _LIBCPP_HIDE_FROM_ABI friend constexpr auto operator<=>(const __iterator& __x, const __iterator& __y)
      requires totally_ordered<_Start> && three_way_comparable<_Start>
    {
      return __x.__value_ <=> __y.__value_;
    }

    _LIBCPP_HIDE_FROM_ABI friend constexpr __iterator operator+(__iterator __i, difference_type __n)
      requires __advanceable<_Start>
    {
      __i += __n;
      return __i;
    }

    _LIBCPP_HIDE_FROM_ABI friend constexpr __iterator operator+(difference_type __n, __iterator __i)
      requires __advanceable<_Start>
    {
      return __i + __n;
    }

    _LIBCPP_HIDE_FROM_ABI friend constexpr __iterator operator-(__iterator __i, difference_type __n)
      requires __advanceable<_Start>
    {
      __i -= __n;
      return __i;
    }

    _LIBCPP_HIDE_FROM_ABI friend constexpr difference_type operator-(const __iterator& __x, const __iterator& __y)
      requires __advanceable<_Start>
    {
      if constexpr (__integer_like<_Start>) {
        if constexpr (__signed_integer_like<_Start>) {
          return difference_type(difference_type(__x.__value_) - difference_type(__y.__value_));
        }
        if (__y.__value_ > __x.__value_) {
          return difference_type(-difference_type(__y.__value_ - __x.__value_));
        }
        return difference_type(__x.__value_ - __y.__value_);
      }
      return __x.__value_ - __y.__value_;
    }
  };

  struct __sentinel {
    friend class iota_view;

  private:
    _BoundSentinel __bound_sentinel_ = _BoundSentinel();

  public:
    _LIBCPP_HIDE_FROM_ABI __sentinel() = default;
    _LIBCPP_HIDE_FROM_ABI constexpr explicit __sentinel(_BoundSentinel __bound_sentinel)
        : __bound_sentinel_(std::move(__bound_sentinel)) {}

    _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(const __iterator& __x, const __sentinel& __y) {
      return __x.__value_ == __y.__bound_sentinel_;
    }

    _LIBCPP_HIDE_FROM_ABI friend constexpr iter_difference_t<_Start>
    operator-(const __iterator& __x, const __sentinel& __y)
      requires sized_sentinel_for<_BoundSentinel, _Start>
    {
      return __x.__value_ - __y.__bound_sentinel_;
    }

    _LIBCPP_HIDE_FROM_ABI friend constexpr iter_difference_t<_Start>
    operator-(const __sentinel& __x, const __iterator& __y)
      requires sized_sentinel_for<_BoundSentinel, _Start>
    {
      return -(__y - __x);
    }
  };

  _Start __value_                  = _Start();
  _BoundSentinel __bound_sentinel_ = _BoundSentinel();

public:
  _LIBCPP_HIDE_FROM_ABI iota_view()
    requires default_initializable<_Start>
  = default;

  _LIBCPP_HIDE_FROM_ABI constexpr explicit iota_view(_Start __value) : __value_(std::move(__value)) {}

  _LIBCPP_HIDE_FROM_ABI constexpr _LIBCPP_EXPLICIT_SINCE_CXX23
  iota_view(type_identity_t<_Start> __value, type_identity_t<_BoundSentinel> __bound_sentinel)
      : __value_(std::move(__value)), __bound_sentinel_(std::move(__bound_sentinel)) {
    // Validate the precondition if possible.
    if constexpr (totally_ordered_with<_Start, _BoundSentinel>) {
      _LIBCPP_ASSERT_VALID_INPUT_RANGE(
          bool(__value_ <= __bound_sentinel_), "iota_view: bound must be reachable from value");
    }
  }

  _LIBCPP_HIDE_FROM_ABI constexpr _LIBCPP_EXPLICIT_SINCE_CXX23 iota_view(__iterator __first, __iterator __last)
    requires same_as<_Start, _BoundSentinel>
      : iota_view(std::move(__first.__value_), std::move(__last.__value_)) {}

  _LIBCPP_HIDE_FROM_ABI constexpr _LIBCPP_EXPLICIT_SINCE_CXX23 iota_view(__iterator __first, _BoundSentinel __last)
    requires same_as<_BoundSentinel, unreachable_sentinel_t>
      : iota_view(std::move(__first.__value_), std::move(__last)) {}

  _LIBCPP_HIDE_FROM_ABI constexpr _LIBCPP_EXPLICIT_SINCE_CXX23 iota_view(__iterator __first, __sentinel __last)
    requires(!same_as<_Start, _BoundSentinel> && !same_as<_BoundSentinel, unreachable_sentinel_t>)
      : iota_view(std::move(__first.__value_), std::move(__last.__bound_sentinel_)) {}

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator begin() const { return __iterator{__value_}; }

  _LIBCPP_HIDE_FROM_ABI constexpr auto end() const {
    if constexpr (same_as<_BoundSentinel, unreachable_sentinel_t>)
      return unreachable_sentinel;
    else
      return __sentinel{__bound_sentinel_};
  }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator end() const
    requires same_as<_Start, _BoundSentinel>
  {
    return __iterator{__bound_sentinel_};
  }

  _LIBCPP_HIDE_FROM_ABI constexpr bool empty() const { return __value_ == __bound_sentinel_; }

  _LIBCPP_HIDE_FROM_ABI constexpr auto size() const
    requires(same_as<_Start, _BoundSentinel> && __advanceable<_Start>) ||
            (integral<_Start> && integral<_BoundSentinel>) || sized_sentinel_for<_BoundSentinel, _Start>
  {
    if constexpr (__integer_like<_Start> && __integer_like<_BoundSentinel>) {
      return (__value_ < 0)
               ? ((__bound_sentinel_ < 0)
                      ? std::__to_unsigned_like(-__value_) - std::__to_unsigned_like(-__bound_sentinel_)
                      : std::__to_unsigned_like(__bound_sentinel_) + std::__to_unsigned_like(-__value_))
               : std::__to_unsigned_like(__bound_sentinel_) - std::__to_unsigned_like(__value_);
    } else {
      return std::__to_unsigned_like(__bound_sentinel_ - __value_);
    }
  }
};

template <class _Start, class _BoundSentinel>
  requires(!__integer_like<_Start> || !__integer_like<_BoundSentinel> ||
           (__signed_integer_like<_Start> == __signed_integer_like<_BoundSentinel>))
iota_view(_Start, _BoundSentinel) -> iota_view<_Start, _BoundSentinel>;

template <class _Start, class _BoundSentinel>
inline constexpr bool enable_borrowed_range<iota_view<_Start, _BoundSentinel>> = true;

namespace views {
namespace __iota {
struct __fn {
  template <class _Start>
  _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Start&& __start) const
      noexcept(noexcept(ranges::iota_view(std::forward<_Start>(__start))))
          -> decltype(ranges::iota_view(std::forward<_Start>(__start))) {
    return ranges::iota_view(std::forward<_Start>(__start));
  }

  template <class _Start, class _BoundSentinel>
  _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Start&& __start, _BoundSentinel&& __bound_sentinel) const noexcept(
      noexcept(ranges::iota_view(std::forward<_Start>(__start), std::forward<_BoundSentinel>(__bound_sentinel))))
      -> decltype(ranges::iota_view(std::forward<_Start>(__start), std::forward<_BoundSentinel>(__bound_sentinel))) {
    return ranges::iota_view(std::forward<_Start>(__start), std::forward<_BoundSentinel>(__bound_sentinel));
  }
};
} // namespace __iota

inline namespace __cpo {
inline constexpr auto iota = __iota::__fn{};
} // namespace __cpo
} // namespace views
} // namespace ranges

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANGES_IOTA_VIEW_H
