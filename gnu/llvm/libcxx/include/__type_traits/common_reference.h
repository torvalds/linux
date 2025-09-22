//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_COMMON_REFERENCE_H
#define _LIBCPP___TYPE_TRAITS_COMMON_REFERENCE_H

#include <__config>
#include <__type_traits/common_type.h>
#include <__type_traits/copy_cv.h>
#include <__type_traits/copy_cvref.h>
#include <__type_traits/is_convertible.h>
#include <__type_traits/is_reference.h>
#include <__type_traits/remove_cv.h>
#include <__type_traits/remove_cvref.h>
#include <__type_traits/remove_reference.h>
#include <__utility/declval.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

// common_reference
#if _LIBCPP_STD_VER >= 20
// Let COND_RES(X, Y) be:
template <class _Xp, class _Yp>
using __cond_res = decltype(false ? std::declval<_Xp (&)()>()() : std::declval<_Yp (&)()>()());

// Let `XREF(A)` denote a unary alias template `T` such that `T<U>` denotes the same type as `U`
// with the addition of `A`'s cv and reference qualifiers, for a non-reference cv-unqualified type
// `U`.
// [Note: `XREF(A)` is `__xref<A>::template __apply`]
template <class _Tp>
struct __xref {
  template <class _Up>
  using __apply = __copy_cvref_t<_Tp, _Up>;
};

// Given types A and B, let X be remove_reference_t<A>, let Y be remove_reference_t<B>,
// and let COMMON-REF(A, B) be:
template <class _Ap, class _Bp, class _Xp = remove_reference_t<_Ap>, class _Yp = remove_reference_t<_Bp>>
struct __common_ref;

template <class _Xp, class _Yp>
using __common_ref_t = typename __common_ref<_Xp, _Yp>::__type;

template <class _Xp, class _Yp>
using __cv_cond_res = __cond_res<__copy_cv_t<_Xp, _Yp>&, __copy_cv_t<_Yp, _Xp>&>;

//    If A and B are both lvalue reference types, COMMON-REF(A, B) is
//    COND-RES(COPYCV(X, Y)&, COPYCV(Y, X)&) if that type exists and is a reference type.
// clang-format off
template <class _Ap, class _Bp, class _Xp, class _Yp>
  requires
    requires { typename __cv_cond_res<_Xp, _Yp>; } &&
    is_reference_v<__cv_cond_res<_Xp, _Yp>>
struct __common_ref<_Ap&, _Bp&, _Xp, _Yp> {
  using __type = __cv_cond_res<_Xp, _Yp>;
};
// clang-format on

//    Otherwise, let C be remove_reference_t<COMMON-REF(X&, Y&)>&&. ...
template <class _Xp, class _Yp>
using __common_ref_C = remove_reference_t<__common_ref_t<_Xp&, _Yp&>>&&;

//    .... If A and B are both rvalue reference types, C is well-formed, and
//    is_convertible_v<A, C> && is_convertible_v<B, C> is true, then COMMON-REF(A, B) is C.
// clang-format off
template <class _Ap, class _Bp, class _Xp, class _Yp>
  requires
    requires { typename __common_ref_C<_Xp, _Yp>; } &&
    is_convertible_v<_Ap&&, __common_ref_C<_Xp, _Yp>> &&
    is_convertible_v<_Bp&&, __common_ref_C<_Xp, _Yp>>
struct __common_ref<_Ap&&, _Bp&&, _Xp, _Yp> {
  using __type = __common_ref_C<_Xp, _Yp>;
};
// clang-format on

//    Otherwise, let D be COMMON-REF(const X&, Y&). ...
template <class _Tp, class _Up>
using __common_ref_D = __common_ref_t<const _Tp&, _Up&>;

//    ... If A is an rvalue reference and B is an lvalue reference and D is well-formed and
//    is_convertible_v<A, D> is true, then COMMON-REF(A, B) is D.
// clang-format off
template <class _Ap, class _Bp, class _Xp, class _Yp>
  requires
    requires { typename __common_ref_D<_Xp, _Yp>; } &&
    is_convertible_v<_Ap&&, __common_ref_D<_Xp, _Yp>>
struct __common_ref<_Ap&&, _Bp&, _Xp, _Yp> {
  using __type = __common_ref_D<_Xp, _Yp>;
};
// clang-format on

//    Otherwise, if A is an lvalue reference and B is an rvalue reference, then
//    COMMON-REF(A, B) is COMMON-REF(B, A).
template <class _Ap, class _Bp, class _Xp, class _Yp>
struct __common_ref<_Ap&, _Bp&&, _Xp, _Yp> : __common_ref<_Bp&&, _Ap&> {};

//    Otherwise, COMMON-REF(A, B) is ill-formed.
template <class _Ap, class _Bp, class _Xp, class _Yp>
struct __common_ref {};

// Note C: For the common_reference trait applied to a parameter pack [...]

template <class...>
struct common_reference;

template <class... _Types>
using common_reference_t = typename common_reference<_Types...>::type;

// bullet 1 - sizeof...(T) == 0
template <>
struct common_reference<> {};

// bullet 2 - sizeof...(T) == 1
template <class _Tp>
struct common_reference<_Tp> {
  using type = _Tp;
};

// bullet 3 - sizeof...(T) == 2
template <class _Tp, class _Up>
struct __common_reference_sub_bullet3;
template <class _Tp, class _Up>
struct __common_reference_sub_bullet2 : __common_reference_sub_bullet3<_Tp, _Up> {};
template <class _Tp, class _Up>
struct __common_reference_sub_bullet1 : __common_reference_sub_bullet2<_Tp, _Up> {};

// sub-bullet 1 - If T1 and T2 are reference types and COMMON-REF(T1, T2) is well-formed, then
// the member typedef `type` denotes that type.
template <class _Tp, class _Up>
struct common_reference<_Tp, _Up> : __common_reference_sub_bullet1<_Tp, _Up> {};

template <class _Tp, class _Up>
  requires is_reference_v<_Tp> && is_reference_v<_Up> && requires { typename __common_ref_t<_Tp, _Up>; }
struct __common_reference_sub_bullet1<_Tp, _Up> {
  using type = __common_ref_t<_Tp, _Up>;
};

// sub-bullet 2 - Otherwise, if basic_common_reference<remove_cvref_t<T1>, remove_cvref_t<T2>, XREF(T1), XREF(T2)>::type
// is well-formed, then the member typedef `type` denotes that type.
template <class, class, template <class> class, template <class> class>
struct basic_common_reference {};

template <class _Tp, class _Up>
using __basic_common_reference_t =
    typename basic_common_reference<remove_cvref_t<_Tp>,
                                    remove_cvref_t<_Up>,
                                    __xref<_Tp>::template __apply,
                                    __xref<_Up>::template __apply>::type;

template <class _Tp, class _Up>
  requires requires { typename __basic_common_reference_t<_Tp, _Up>; }
struct __common_reference_sub_bullet2<_Tp, _Up> {
  using type = __basic_common_reference_t<_Tp, _Up>;
};

// sub-bullet 3 - Otherwise, if COND-RES(T1, T2) is well-formed,
// then the member typedef `type` denotes that type.
template <class _Tp, class _Up>
  requires requires { typename __cond_res<_Tp, _Up>; }
struct __common_reference_sub_bullet3<_Tp, _Up> {
  using type = __cond_res<_Tp, _Up>;
};

// sub-bullet 4 & 5 - Otherwise, if common_type_t<T1, T2> is well-formed,
//                    then the member typedef `type` denotes that type.
//                  - Otherwise, there shall be no member `type`.
template <class _Tp, class _Up>
struct __common_reference_sub_bullet3 : common_type<_Tp, _Up> {};

// bullet 4 - If there is such a type `C`, the member typedef type shall denote the same type, if
//            any, as `common_reference_t<C, Rest...>`.
template <class _Tp, class _Up, class _Vp, class... _Rest>
  requires requires { typename common_reference_t<_Tp, _Up>; }
struct common_reference<_Tp, _Up, _Vp, _Rest...> : common_reference<common_reference_t<_Tp, _Up>, _Vp, _Rest...> {};

// bullet 5 - Otherwise, there shall be no member `type`.
template <class...>
struct common_reference {};

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_COMMON_REFERENCE_H
