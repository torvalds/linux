//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TUPLE_SFINAE_HELPERS_H
#define _LIBCPP___TUPLE_SFINAE_HELPERS_H

#include <__config>
#include <__fwd/tuple.h>
#include <__tuple/make_tuple_types.h>
#include <__tuple/tuple_element.h>
#include <__tuple/tuple_like_ext.h>
#include <__tuple/tuple_size.h>
#include <__tuple/tuple_types.h>
#include <__type_traits/conjunction.h>
#include <__type_traits/enable_if.h>
#include <__type_traits/integral_constant.h>
#include <__type_traits/is_constructible.h>
#include <__type_traits/is_same.h>
#include <__type_traits/remove_cvref.h>
#include <__type_traits/remove_reference.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#ifndef _LIBCPP_CXX03_LANG

struct __tuple_sfinae_base {
  template <template <class, class...> class _Trait, class... _LArgs, class... _RArgs>
  static auto __do_test(__tuple_types<_LArgs...>,
                        __tuple_types<_RArgs...>) -> __all<__enable_if_t<_Trait<_LArgs, _RArgs>::value, bool>{true}...>;
  template <template <class...> class>
  static auto __do_test(...) -> false_type;

  template <class _FromArgs, class _ToArgs>
  using __constructible = decltype(__do_test<is_constructible>(_ToArgs{}, _FromArgs{}));
};

// __tuple_constructible

template <class _Tp,
          class _Up,
          bool = __tuple_like_ext<__libcpp_remove_reference_t<_Tp> >::value,
          bool = __tuple_like_ext<_Up>::value>
struct __tuple_constructible : public false_type {};

template <class _Tp, class _Up>
struct __tuple_constructible<_Tp, _Up, true, true>
    : public __tuple_sfinae_base::__constructible< typename __make_tuple_types<_Tp>::type,
                                                   typename __make_tuple_types<_Up>::type > {};

template <size_t _Ip, class... _Tp>
struct _LIBCPP_TEMPLATE_VIS tuple_element<_Ip, tuple<_Tp...> > {
  typedef _LIBCPP_NODEBUG typename tuple_element<_Ip, __tuple_types<_Tp...> >::type type;
};

struct _LIBCPP_EXPORTED_FROM_ABI __check_tuple_constructor_fail {
  static _LIBCPP_HIDE_FROM_ABI constexpr bool __enable_explicit_default() { return false; }
  static _LIBCPP_HIDE_FROM_ABI constexpr bool __enable_implicit_default() { return false; }
  template <class...>
  static _LIBCPP_HIDE_FROM_ABI constexpr bool __enable_explicit() {
    return false;
  }
  template <class...>
  static _LIBCPP_HIDE_FROM_ABI constexpr bool __enable_implicit() {
    return false;
  }
  template <class...>
  static _LIBCPP_HIDE_FROM_ABI constexpr bool __enable_assign() {
    return false;
  }
};
#endif // !defined(_LIBCPP_CXX03_LANG)

#if _LIBCPP_STD_VER >= 17

template <bool _CanCopy, bool _CanMove>
struct __sfinae_ctor_base {};
template <>
struct __sfinae_ctor_base<false, false> {
  __sfinae_ctor_base()                                     = default;
  __sfinae_ctor_base(__sfinae_ctor_base const&)            = delete;
  __sfinae_ctor_base(__sfinae_ctor_base&&)                 = delete;
  __sfinae_ctor_base& operator=(__sfinae_ctor_base const&) = default;
  __sfinae_ctor_base& operator=(__sfinae_ctor_base&&)      = default;
};
template <>
struct __sfinae_ctor_base<true, false> {
  __sfinae_ctor_base()                                     = default;
  __sfinae_ctor_base(__sfinae_ctor_base const&)            = default;
  __sfinae_ctor_base(__sfinae_ctor_base&&)                 = delete;
  __sfinae_ctor_base& operator=(__sfinae_ctor_base const&) = default;
  __sfinae_ctor_base& operator=(__sfinae_ctor_base&&)      = default;
};
template <>
struct __sfinae_ctor_base<false, true> {
  __sfinae_ctor_base()                                     = default;
  __sfinae_ctor_base(__sfinae_ctor_base const&)            = delete;
  __sfinae_ctor_base(__sfinae_ctor_base&&)                 = default;
  __sfinae_ctor_base& operator=(__sfinae_ctor_base const&) = default;
  __sfinae_ctor_base& operator=(__sfinae_ctor_base&&)      = default;
};

template <bool _CanCopy, bool _CanMove>
struct __sfinae_assign_base {};
template <>
struct __sfinae_assign_base<false, false> {
  __sfinae_assign_base()                                       = default;
  __sfinae_assign_base(__sfinae_assign_base const&)            = default;
  __sfinae_assign_base(__sfinae_assign_base&&)                 = default;
  __sfinae_assign_base& operator=(__sfinae_assign_base const&) = delete;
  __sfinae_assign_base& operator=(__sfinae_assign_base&&)      = delete;
};
template <>
struct __sfinae_assign_base<true, false> {
  __sfinae_assign_base()                                       = default;
  __sfinae_assign_base(__sfinae_assign_base const&)            = default;
  __sfinae_assign_base(__sfinae_assign_base&&)                 = default;
  __sfinae_assign_base& operator=(__sfinae_assign_base const&) = default;
  __sfinae_assign_base& operator=(__sfinae_assign_base&&)      = delete;
};
template <>
struct __sfinae_assign_base<false, true> {
  __sfinae_assign_base()                                       = default;
  __sfinae_assign_base(__sfinae_assign_base const&)            = default;
  __sfinae_assign_base(__sfinae_assign_base&&)                 = default;
  __sfinae_assign_base& operator=(__sfinae_assign_base const&) = delete;
  __sfinae_assign_base& operator=(__sfinae_assign_base&&)      = default;
};
#endif // _LIBCPP_STD_VER >= 17

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TUPLE_SFINAE_HELPERS_H
