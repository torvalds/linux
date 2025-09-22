// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MEMORY_COMPRESSED_PAIR_H
#define _LIBCPP___MEMORY_COMPRESSED_PAIR_H

#include <__config>
#include <__fwd/tuple.h>
#include <__tuple/tuple_indices.h>
#include <__type_traits/decay.h>
#include <__type_traits/dependent_type.h>
#include <__type_traits/enable_if.h>
#include <__type_traits/is_constructible.h>
#include <__type_traits/is_empty.h>
#include <__type_traits/is_final.h>
#include <__type_traits/is_same.h>
#include <__type_traits/is_swappable.h>
#include <__utility/forward.h>
#include <__utility/move.h>
#include <__utility/piecewise_construct.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

// Tag used to default initialize one or both of the pair's elements.
struct __default_init_tag {};
struct __value_init_tag {};

template <class _Tp, int _Idx, bool _CanBeEmptyBase = is_empty<_Tp>::value && !__libcpp_is_final<_Tp>::value>
struct __compressed_pair_elem {
  using _ParamT         = _Tp;
  using reference       = _Tp&;
  using const_reference = const _Tp&;

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR explicit __compressed_pair_elem(__default_init_tag) {}
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR explicit __compressed_pair_elem(__value_init_tag) : __value_() {}

  template <class _Up, __enable_if_t<!is_same<__compressed_pair_elem, __decay_t<_Up> >::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR explicit __compressed_pair_elem(_Up&& __u)
      : __value_(std::forward<_Up>(__u)) {}

#ifndef _LIBCPP_CXX03_LANG
  template <class... _Args, size_t... _Indices>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 explicit __compressed_pair_elem(
      piecewise_construct_t, tuple<_Args...> __args, __tuple_indices<_Indices...>)
      : __value_(std::forward<_Args>(std::get<_Indices>(__args))...) {}
#endif

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 reference __get() _NOEXCEPT { return __value_; }
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR const_reference __get() const _NOEXCEPT { return __value_; }

private:
  _Tp __value_;
};

template <class _Tp, int _Idx>
struct __compressed_pair_elem<_Tp, _Idx, true> : private _Tp {
  using _ParamT         = _Tp;
  using reference       = _Tp&;
  using const_reference = const _Tp&;
  using __value_type    = _Tp;

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR explicit __compressed_pair_elem() = default;
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR explicit __compressed_pair_elem(__default_init_tag) {}
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR explicit __compressed_pair_elem(__value_init_tag) : __value_type() {}

  template <class _Up, __enable_if_t<!is_same<__compressed_pair_elem, __decay_t<_Up> >::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR explicit __compressed_pair_elem(_Up&& __u)
      : __value_type(std::forward<_Up>(__u)) {}

#ifndef _LIBCPP_CXX03_LANG
  template <class... _Args, size_t... _Indices>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17
  __compressed_pair_elem(piecewise_construct_t, tuple<_Args...> __args, __tuple_indices<_Indices...>)
      : __value_type(std::forward<_Args>(std::get<_Indices>(__args))...) {}
#endif

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 reference __get() _NOEXCEPT { return *this; }
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR const_reference __get() const _NOEXCEPT { return *this; }
};

template <class _T1, class _T2>
class __compressed_pair : private __compressed_pair_elem<_T1, 0>, private __compressed_pair_elem<_T2, 1> {
public:
  // NOTE: This static assert should never fire because __compressed_pair
  // is *almost never* used in a scenario where it's possible for T1 == T2.
  // (The exception is std::function where it is possible that the function
  //  object and the allocator have the same type).
  static_assert(
      (!is_same<_T1, _T2>::value),
      "__compressed_pair cannot be instantiated when T1 and T2 are the same type; "
      "The current implementation is NOT ABI-compatible with the previous implementation for this configuration");

  using _Base1 _LIBCPP_NODEBUG = __compressed_pair_elem<_T1, 0>;
  using _Base2 _LIBCPP_NODEBUG = __compressed_pair_elem<_T2, 1>;

  template <bool _Dummy         = true,
            __enable_if_t< __dependent_type<is_default_constructible<_T1>, _Dummy>::value &&
                               __dependent_type<is_default_constructible<_T2>, _Dummy>::value,
                           int> = 0>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR explicit __compressed_pair()
      : _Base1(__value_init_tag()), _Base2(__value_init_tag()) {}

  template <class _U1, class _U2>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR explicit __compressed_pair(_U1&& __t1, _U2&& __t2)
      : _Base1(std::forward<_U1>(__t1)), _Base2(std::forward<_U2>(__t2)) {}

#ifndef _LIBCPP_CXX03_LANG
  template <class... _Args1, class... _Args2>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 explicit __compressed_pair(
      piecewise_construct_t __pc, tuple<_Args1...> __first_args, tuple<_Args2...> __second_args)
      : _Base1(__pc, std::move(__first_args), typename __make_tuple_indices<sizeof...(_Args1)>::type()),
        _Base2(__pc, std::move(__second_args), typename __make_tuple_indices<sizeof...(_Args2)>::type()) {}
#endif

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 typename _Base1::reference first() _NOEXCEPT {
    return static_cast<_Base1&>(*this).__get();
  }

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR typename _Base1::const_reference first() const _NOEXCEPT {
    return static_cast<_Base1 const&>(*this).__get();
  }

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 typename _Base2::reference second() _NOEXCEPT {
    return static_cast<_Base2&>(*this).__get();
  }

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR typename _Base2::const_reference second() const _NOEXCEPT {
    return static_cast<_Base2 const&>(*this).__get();
  }

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR static _Base1* __get_first_base(__compressed_pair* __pair) _NOEXCEPT {
    return static_cast<_Base1*>(__pair);
  }
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR static _Base2* __get_second_base(__compressed_pair* __pair) _NOEXCEPT {
    return static_cast<_Base2*>(__pair);
  }

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 void swap(__compressed_pair& __x)
      _NOEXCEPT_(__is_nothrow_swappable_v<_T1>&& __is_nothrow_swappable_v<_T2>) {
    using std::swap;
    swap(first(), __x.first());
    swap(second(), __x.second());
  }
};

template <class _T1, class _T2>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 void
swap(__compressed_pair<_T1, _T2>& __x, __compressed_pair<_T1, _T2>& __y)
    _NOEXCEPT_(__is_nothrow_swappable_v<_T1>&& __is_nothrow_swappable_v<_T2>) {
  __x.swap(__y);
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___MEMORY_COMPRESSED_PAIR_H
