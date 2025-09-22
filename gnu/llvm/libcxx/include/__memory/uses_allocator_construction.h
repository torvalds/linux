//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MEMORY_USES_ALLOCATOR_CONSTRUCTION_H
#define _LIBCPP___MEMORY_USES_ALLOCATOR_CONSTRUCTION_H

#include <__config>
#include <__memory/construct_at.h>
#include <__memory/uses_allocator.h>
#include <__tuple/tuple_like_no_subrange.h>
#include <__type_traits/enable_if.h>
#include <__type_traits/is_same.h>
#include <__type_traits/remove_cv.h>
#include <__utility/declval.h>
#include <__utility/pair.h>
#include <tuple>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 17

template <class _Type>
inline constexpr bool __is_std_pair = false;

template <class _Type1, class _Type2>
inline constexpr bool __is_std_pair<pair<_Type1, _Type2>> = true;

template <class _Tp>
inline constexpr bool __is_cv_std_pair = __is_std_pair<remove_cv_t<_Tp>>;

template <class _Type, class _Alloc, class... _Args, __enable_if_t<!__is_cv_std_pair<_Type>, int> = 0>
_LIBCPP_HIDE_FROM_ABI constexpr auto
__uses_allocator_construction_args(const _Alloc& __alloc, _Args&&... __args) noexcept {
  if constexpr (!uses_allocator_v<remove_cv_t<_Type>, _Alloc> && is_constructible_v<_Type, _Args...>) {
    return std::forward_as_tuple(std::forward<_Args>(__args)...);
  } else if constexpr (uses_allocator_v<remove_cv_t<_Type>, _Alloc> &&
                       is_constructible_v<_Type, allocator_arg_t, const _Alloc&, _Args...>) {
    return tuple<allocator_arg_t, const _Alloc&, _Args&&...>(allocator_arg, __alloc, std::forward<_Args>(__args)...);
  } else if constexpr (uses_allocator_v<remove_cv_t<_Type>, _Alloc> &&
                       is_constructible_v<_Type, _Args..., const _Alloc&>) {
    return std::forward_as_tuple(std::forward<_Args>(__args)..., __alloc);
  } else {
    static_assert(
        sizeof(_Type) + 1 == 0, "If uses_allocator_v<Type> is true, the type has to be allocator-constructible");
  }
}

template <class _Pair, class _Alloc, class _Tuple1, class _Tuple2, __enable_if_t<__is_cv_std_pair<_Pair>, int> = 0>
_LIBCPP_HIDE_FROM_ABI constexpr auto __uses_allocator_construction_args(
    const _Alloc& __alloc, piecewise_construct_t, _Tuple1&& __x, _Tuple2&& __y) noexcept {
  return std::make_tuple(
      piecewise_construct,
      std::apply(
          [&__alloc](auto&&... __args1) {
            return std::__uses_allocator_construction_args<typename _Pair::first_type>(
                __alloc, std::forward<decltype(__args1)>(__args1)...);
          },
          std::forward<_Tuple1>(__x)),
      std::apply(
          [&__alloc](auto&&... __args2) {
            return std::__uses_allocator_construction_args<typename _Pair::second_type>(
                __alloc, std::forward<decltype(__args2)>(__args2)...);
          },
          std::forward<_Tuple2>(__y)));
}

template <class _Pair, class _Alloc, __enable_if_t<__is_cv_std_pair<_Pair>, int> = 0>
_LIBCPP_HIDE_FROM_ABI constexpr auto __uses_allocator_construction_args(const _Alloc& __alloc) noexcept {
  return std::__uses_allocator_construction_args<_Pair>(__alloc, piecewise_construct, tuple<>{}, tuple<>{});
}

template <class _Pair, class _Alloc, class _Up, class _Vp, __enable_if_t<__is_cv_std_pair<_Pair>, int> = 0>
_LIBCPP_HIDE_FROM_ABI constexpr auto
__uses_allocator_construction_args(const _Alloc& __alloc, _Up&& __u, _Vp&& __v) noexcept {
  return std::__uses_allocator_construction_args<_Pair>(
      __alloc,
      piecewise_construct,
      std::forward_as_tuple(std::forward<_Up>(__u)),
      std::forward_as_tuple(std::forward<_Vp>(__v)));
}

#  if _LIBCPP_STD_VER >= 23
template <class _Pair, class _Alloc, class _Up, class _Vp, __enable_if_t<__is_cv_std_pair<_Pair>, int> = 0>
_LIBCPP_HIDE_FROM_ABI constexpr auto
__uses_allocator_construction_args(const _Alloc& __alloc, pair<_Up, _Vp>& __pair) noexcept {
  return std::__uses_allocator_construction_args<_Pair>(
      __alloc, piecewise_construct, std::forward_as_tuple(__pair.first), std::forward_as_tuple(__pair.second));
}
#  endif

template <class _Pair, class _Alloc, class _Up, class _Vp, __enable_if_t<__is_cv_std_pair<_Pair>, int> = 0>
_LIBCPP_HIDE_FROM_ABI constexpr auto
__uses_allocator_construction_args(const _Alloc& __alloc, const pair<_Up, _Vp>& __pair) noexcept {
  return std::__uses_allocator_construction_args<_Pair>(
      __alloc, piecewise_construct, std::forward_as_tuple(__pair.first), std::forward_as_tuple(__pair.second));
}

template <class _Pair, class _Alloc, class _Up, class _Vp, __enable_if_t<__is_cv_std_pair<_Pair>, int> = 0>
_LIBCPP_HIDE_FROM_ABI constexpr auto
__uses_allocator_construction_args(const _Alloc& __alloc, pair<_Up, _Vp>&& __pair) noexcept {
  return std::__uses_allocator_construction_args<_Pair>(
      __alloc,
      piecewise_construct,
      std::forward_as_tuple(std::get<0>(std::move(__pair))),
      std::forward_as_tuple(std::get<1>(std::move(__pair))));
}

#  if _LIBCPP_STD_VER >= 23
template <class _Pair, class _Alloc, class _Up, class _Vp, __enable_if_t<__is_cv_std_pair<_Pair>, int> = 0>
_LIBCPP_HIDE_FROM_ABI constexpr auto
__uses_allocator_construction_args(const _Alloc& __alloc, const pair<_Up, _Vp>&& __pair) noexcept {
  return std::__uses_allocator_construction_args<_Pair>(
      __alloc,
      piecewise_construct,
      std::forward_as_tuple(std::get<0>(std::move(__pair))),
      std::forward_as_tuple(std::get<1>(std::move(__pair))));
}

template <class _Pair, class _Alloc, __pair_like_no_subrange _PairLike, __enable_if_t<__is_cv_std_pair<_Pair>, int> = 0>
_LIBCPP_HIDE_FROM_ABI constexpr auto
__uses_allocator_construction_args(const _Alloc& __alloc, _PairLike&& __p) noexcept {
  return std::__uses_allocator_construction_args<_Pair>(
      __alloc,
      piecewise_construct,
      std::forward_as_tuple(std::get<0>(std::forward<_PairLike>(__p))),
      std::forward_as_tuple(std::get<1>(std::forward<_PairLike>(__p))));
}
#  endif

namespace __uses_allocator_detail {

template <class _Ap, class _Bp>
void __fun(const pair<_Ap, _Bp>&);

template <class _Tp>
decltype(__uses_allocator_detail::__fun(std::declval<_Tp>()), true_type()) __convertible_to_const_pair_ref_impl(int);

template <class>
false_type __convertible_to_const_pair_ref_impl(...);

template <class _Tp>
inline constexpr bool __convertible_to_const_pair_ref =
    decltype(__uses_allocator_detail::__convertible_to_const_pair_ref_impl<_Tp>(0))::value;

#  if _LIBCPP_STD_VER >= 23
template <class _Tp, class _Up>
inline constexpr bool __uses_allocator_constraints =
    __is_cv_std_pair<_Tp> && !__pair_like_no_subrange<_Up> && !__convertible_to_const_pair_ref<_Up>;
#  else
template <class _Tp, class _Up>
inline constexpr bool __uses_allocator_constraints = __is_cv_std_pair<_Tp> && !__convertible_to_const_pair_ref<_Up>;
#  endif

} // namespace __uses_allocator_detail

template < class _Pair,
           class _Alloc,
           class _Type,
           __enable_if_t<__uses_allocator_detail::__uses_allocator_constraints<_Pair, _Type>, int> = 0>
_LIBCPP_HIDE_FROM_ABI constexpr auto
__uses_allocator_construction_args(const _Alloc& __alloc, _Type&& __value) noexcept;

template <class _Type, class _Alloc, class... _Args>
_LIBCPP_HIDE_FROM_ABI constexpr _Type __make_obj_using_allocator(const _Alloc& __alloc, _Args&&... __args);

template < class _Pair,
           class _Alloc,
           class _Type,
           __enable_if_t< __uses_allocator_detail::__uses_allocator_constraints<_Pair, _Type>, int>>
_LIBCPP_HIDE_FROM_ABI constexpr auto
__uses_allocator_construction_args(const _Alloc& __alloc, _Type&& __value) noexcept {
  struct __pair_constructor {
    using _PairMutable = remove_cv_t<_Pair>;

    _LIBCPP_HIDDEN constexpr auto __do_construct(const _PairMutable& __pair) const {
      return std::__make_obj_using_allocator<_PairMutable>(__alloc_, __pair);
    }

    _LIBCPP_HIDDEN constexpr auto __do_construct(_PairMutable&& __pair) const {
      return std::__make_obj_using_allocator<_PairMutable>(__alloc_, std::move(__pair));
    }

    const _Alloc& __alloc_;
    _Type& __value_;

    _LIBCPP_HIDDEN constexpr operator _PairMutable() const { return __do_construct(std::forward<_Type>(__value_)); }
  };

  return std::make_tuple(__pair_constructor{__alloc, __value});
}

template <class _Type, class _Alloc, class... _Args>
_LIBCPP_HIDE_FROM_ABI constexpr _Type __make_obj_using_allocator(const _Alloc& __alloc, _Args&&... __args) {
  return std::make_from_tuple<_Type>(
      std::__uses_allocator_construction_args<_Type>(__alloc, std::forward<_Args>(__args)...));
}

template <class _Type, class _Alloc, class... _Args>
_LIBCPP_HIDE_FROM_ABI constexpr _Type*
__uninitialized_construct_using_allocator(_Type* __ptr, const _Alloc& __alloc, _Args&&... __args) {
  return std::apply(
      [&__ptr](auto&&... __xs) { return std::__construct_at(__ptr, std::forward<decltype(__xs)>(__xs)...); },
      std::__uses_allocator_construction_args<_Type>(__alloc, std::forward<_Args>(__args)...));
}

#endif // _LIBCPP_STD_VER >= 17

#if _LIBCPP_STD_VER >= 20

template <class _Type, class _Alloc, class... _Args>
_LIBCPP_HIDE_FROM_ABI constexpr auto uses_allocator_construction_args(const _Alloc& __alloc, _Args&&... __args) noexcept
    -> decltype(std::__uses_allocator_construction_args<_Type>(__alloc, std::forward<_Args>(__args)...)) {
  return /*--*/ std::__uses_allocator_construction_args<_Type>(__alloc, std::forward<_Args>(__args)...);
}

template <class _Type, class _Alloc, class... _Args>
_LIBCPP_HIDE_FROM_ABI constexpr auto make_obj_using_allocator(const _Alloc& __alloc, _Args&&... __args)
    -> decltype(std::__make_obj_using_allocator<_Type>(__alloc, std::forward<_Args>(__args)...)) {
  return /*--*/ std::__make_obj_using_allocator<_Type>(__alloc, std::forward<_Args>(__args)...);
}

template <class _Type, class _Alloc, class... _Args>
_LIBCPP_HIDE_FROM_ABI constexpr auto
uninitialized_construct_using_allocator(_Type* __ptr, const _Alloc& __alloc, _Args&&... __args)
    -> decltype(std::__uninitialized_construct_using_allocator(__ptr, __alloc, std::forward<_Args>(__args)...)) {
  return /*--*/ std::__uninitialized_construct_using_allocator(__ptr, __alloc, std::forward<_Args>(__args)...);
}

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___MEMORY_USES_ALLOCATOR_CONSTRUCTION_H
