//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_COMMON_TYPE_H
#define _LIBCPP___TYPE_TRAITS_COMMON_TYPE_H

#include <__config>
#include <__type_traits/conditional.h>
#include <__type_traits/decay.h>
#include <__type_traits/is_same.h>
#include <__type_traits/remove_cvref.h>
#include <__type_traits/void_t.h>
#include <__utility/declval.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20
// Let COND_RES(X, Y) be:
template <class _Tp, class _Up>
using __cond_type = decltype(false ? std::declval<_Tp>() : std::declval<_Up>());

template <class _Tp, class _Up, class = void>
struct __common_type3 {};

// sub-bullet 4 - "if COND_RES(CREF(D1), CREF(D2)) denotes a type..."
template <class _Tp, class _Up>
struct __common_type3<_Tp, _Up, void_t<__cond_type<const _Tp&, const _Up&>>> {
  using type = remove_cvref_t<__cond_type<const _Tp&, const _Up&>>;
};

template <class _Tp, class _Up, class = void>
struct __common_type2_imp : __common_type3<_Tp, _Up> {};
#else
template <class _Tp, class _Up, class = void>
struct __common_type2_imp {};
#endif

// sub-bullet 3 - "if decay_t<decltype(false ? declval<D1>() : declval<D2>())> ..."
template <class _Tp, class _Up>
struct __common_type2_imp<_Tp, _Up, __void_t<decltype(true ? std::declval<_Tp>() : std::declval<_Up>())> > {
  typedef _LIBCPP_NODEBUG __decay_t<decltype(true ? std::declval<_Tp>() : std::declval<_Up>())> type;
};

template <class, class = void>
struct __common_type_impl {};

template <class... _Tp>
struct __common_types;
template <class... _Tp>
struct _LIBCPP_TEMPLATE_VIS common_type;

template <class _Tp, class _Up>
struct __common_type_impl< __common_types<_Tp, _Up>, __void_t<typename common_type<_Tp, _Up>::type> > {
  typedef typename common_type<_Tp, _Up>::type type;
};

template <class _Tp, class _Up, class _Vp, class... _Rest>
struct __common_type_impl<__common_types<_Tp, _Up, _Vp, _Rest...>, __void_t<typename common_type<_Tp, _Up>::type> >
    : __common_type_impl<__common_types<typename common_type<_Tp, _Up>::type, _Vp, _Rest...> > {};

// bullet 1 - sizeof...(Tp) == 0

template <>
struct _LIBCPP_TEMPLATE_VIS common_type<> {};

// bullet 2 - sizeof...(Tp) == 1

template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS common_type<_Tp> : public common_type<_Tp, _Tp> {};

// bullet 3 - sizeof...(Tp) == 2

// sub-bullet 1 - "If is_same_v<T1, D1> is false or ..."
template <class _Tp, class _Up>
struct _LIBCPP_TEMPLATE_VIS common_type<_Tp, _Up>
    : __conditional_t<_IsSame<_Tp, __decay_t<_Tp> >::value && _IsSame<_Up, __decay_t<_Up> >::value,
                      __common_type2_imp<_Tp, _Up>,
                      common_type<__decay_t<_Tp>, __decay_t<_Up> > > {};

// bullet 4 - sizeof...(Tp) > 2

template <class _Tp, class _Up, class _Vp, class... _Rest>
struct _LIBCPP_TEMPLATE_VIS common_type<_Tp, _Up, _Vp, _Rest...>
    : __common_type_impl<__common_types<_Tp, _Up, _Vp, _Rest...> > {};

#if _LIBCPP_STD_VER >= 14
template <class... _Tp>
using common_type_t = typename common_type<_Tp...>::type;
#endif

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_COMMON_TYPE_H
