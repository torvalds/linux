//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_STRIP_SIGNATURE_H
#define _LIBCPP___TYPE_TRAITS_STRIP_SIGNATURE_H

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER >= 17

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Fp>
struct __strip_signature;

#  if defined(__cpp_static_call_operator) && __cpp_static_call_operator >= 202207L

template <class _Rp, class... _Args>
struct __strip_signature<_Rp (*)(_Args...)> {
  using type = _Rp(_Args...);
};

template <class _Rp, class... _Args>
struct __strip_signature<_Rp (*)(_Args...) noexcept> {
  using type = _Rp(_Args...);
};

#  endif // defined(__cpp_static_call_operator) && __cpp_static_call_operator >= 202207L

// clang-format off
template<class _Rp, class _Gp, class ..._Ap>
struct __strip_signature<_Rp (_Gp::*) (_Ap...)> { using type = _Rp(_Ap...); };
template<class _Rp, class _Gp, class ..._Ap>
struct __strip_signature<_Rp (_Gp::*) (_Ap...) const> { using type = _Rp(_Ap...); };
template<class _Rp, class _Gp, class ..._Ap>
struct __strip_signature<_Rp (_Gp::*) (_Ap...) volatile> { using type = _Rp(_Ap...); };
template<class _Rp, class _Gp, class ..._Ap>
struct __strip_signature<_Rp (_Gp::*) (_Ap...) const volatile> { using type = _Rp(_Ap...); };

template<class _Rp, class _Gp, class ..._Ap>
struct __strip_signature<_Rp (_Gp::*) (_Ap...) &> { using type = _Rp(_Ap...); };
template<class _Rp, class _Gp, class ..._Ap>
struct __strip_signature<_Rp (_Gp::*) (_Ap...) const &> { using type = _Rp(_Ap...); };
template<class _Rp, class _Gp, class ..._Ap>
struct __strip_signature<_Rp (_Gp::*) (_Ap...) volatile &> { using type = _Rp(_Ap...); };
template<class _Rp, class _Gp, class ..._Ap>
struct __strip_signature<_Rp (_Gp::*) (_Ap...) const volatile &> { using type = _Rp(_Ap...); };

template<class _Rp, class _Gp, class ..._Ap>
struct __strip_signature<_Rp (_Gp::*) (_Ap...) noexcept> { using type = _Rp(_Ap...); };
template<class _Rp, class _Gp, class ..._Ap>
struct __strip_signature<_Rp (_Gp::*) (_Ap...) const noexcept> { using type = _Rp(_Ap...); };
template<class _Rp, class _Gp, class ..._Ap>
struct __strip_signature<_Rp (_Gp::*) (_Ap...) volatile noexcept> { using type = _Rp(_Ap...); };
template<class _Rp, class _Gp, class ..._Ap>
struct __strip_signature<_Rp (_Gp::*) (_Ap...) const volatile noexcept> { using type = _Rp(_Ap...); };

template<class _Rp, class _Gp, class ..._Ap>
struct __strip_signature<_Rp (_Gp::*) (_Ap...) & noexcept> { using type = _Rp(_Ap...); };
template<class _Rp, class _Gp, class ..._Ap>
struct __strip_signature<_Rp (_Gp::*) (_Ap...) const & noexcept> { using type = _Rp(_Ap...); };
template<class _Rp, class _Gp, class ..._Ap>
struct __strip_signature<_Rp (_Gp::*) (_Ap...) volatile & noexcept> { using type = _Rp(_Ap...); };
template<class _Rp, class _Gp, class ..._Ap>
struct __strip_signature<_Rp (_Gp::*) (_Ap...) const volatile & noexcept> { using type = _Rp(_Ap...); };
// clang-format on

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 17

#endif // _LIBCPP___TYPE_TRAITS_STRIP_SIGNATURE_H
