// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FUNCTIONAL_MEM_FUN_REF_H
#define _LIBCPP___FUNCTIONAL_MEM_FUN_REF_H

#include <__config>
#include <__functional/binary_function.h>
#include <__functional/unary_function.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER <= 14 || defined(_LIBCPP_ENABLE_CXX17_REMOVED_BINDERS)

template <class _Sp, class _Tp>
class _LIBCPP_TEMPLATE_VIS _LIBCPP_DEPRECATED_IN_CXX11 mem_fun_t : public __unary_function<_Tp*, _Sp> {
  _Sp (_Tp::*__p_)();

public:
  _LIBCPP_HIDE_FROM_ABI explicit mem_fun_t(_Sp (_Tp::*__p)()) : __p_(__p) {}
  _LIBCPP_HIDE_FROM_ABI _Sp operator()(_Tp* __p) const { return (__p->*__p_)(); }
};

template <class _Sp, class _Tp, class _Ap>
class _LIBCPP_TEMPLATE_VIS _LIBCPP_DEPRECATED_IN_CXX11 mem_fun1_t : public __binary_function<_Tp*, _Ap, _Sp> {
  _Sp (_Tp::*__p_)(_Ap);

public:
  _LIBCPP_HIDE_FROM_ABI explicit mem_fun1_t(_Sp (_Tp::*__p)(_Ap)) : __p_(__p) {}
  _LIBCPP_HIDE_FROM_ABI _Sp operator()(_Tp* __p, _Ap __x) const { return (__p->*__p_)(__x); }
};

template <class _Sp, class _Tp>
_LIBCPP_DEPRECATED_IN_CXX11 inline _LIBCPP_HIDE_FROM_ABI mem_fun_t<_Sp, _Tp> mem_fun(_Sp (_Tp::*__f)()) {
  return mem_fun_t<_Sp, _Tp>(__f);
}

template <class _Sp, class _Tp, class _Ap>
_LIBCPP_DEPRECATED_IN_CXX11 inline _LIBCPP_HIDE_FROM_ABI mem_fun1_t<_Sp, _Tp, _Ap> mem_fun(_Sp (_Tp::*__f)(_Ap)) {
  return mem_fun1_t<_Sp, _Tp, _Ap>(__f);
}

template <class _Sp, class _Tp>
class _LIBCPP_TEMPLATE_VIS _LIBCPP_DEPRECATED_IN_CXX11 mem_fun_ref_t : public __unary_function<_Tp, _Sp> {
  _Sp (_Tp::*__p_)();

public:
  _LIBCPP_HIDE_FROM_ABI explicit mem_fun_ref_t(_Sp (_Tp::*__p)()) : __p_(__p) {}
  _LIBCPP_HIDE_FROM_ABI _Sp operator()(_Tp& __p) const { return (__p.*__p_)(); }
};

template <class _Sp, class _Tp, class _Ap>
class _LIBCPP_TEMPLATE_VIS _LIBCPP_DEPRECATED_IN_CXX11 mem_fun1_ref_t : public __binary_function<_Tp, _Ap, _Sp> {
  _Sp (_Tp::*__p_)(_Ap);

public:
  _LIBCPP_HIDE_FROM_ABI explicit mem_fun1_ref_t(_Sp (_Tp::*__p)(_Ap)) : __p_(__p) {}
  _LIBCPP_HIDE_FROM_ABI _Sp operator()(_Tp& __p, _Ap __x) const { return (__p.*__p_)(__x); }
};

template <class _Sp, class _Tp>
_LIBCPP_DEPRECATED_IN_CXX11 inline _LIBCPP_HIDE_FROM_ABI mem_fun_ref_t<_Sp, _Tp> mem_fun_ref(_Sp (_Tp::*__f)()) {
  return mem_fun_ref_t<_Sp, _Tp>(__f);
}

template <class _Sp, class _Tp, class _Ap>
_LIBCPP_DEPRECATED_IN_CXX11 inline _LIBCPP_HIDE_FROM_ABI mem_fun1_ref_t<_Sp, _Tp, _Ap>
mem_fun_ref(_Sp (_Tp::*__f)(_Ap)) {
  return mem_fun1_ref_t<_Sp, _Tp, _Ap>(__f);
}

template <class _Sp, class _Tp>
class _LIBCPP_TEMPLATE_VIS _LIBCPP_DEPRECATED_IN_CXX11 const_mem_fun_t : public __unary_function<const _Tp*, _Sp> {
  _Sp (_Tp::*__p_)() const;

public:
  _LIBCPP_HIDE_FROM_ABI explicit const_mem_fun_t(_Sp (_Tp::*__p)() const) : __p_(__p) {}
  _LIBCPP_HIDE_FROM_ABI _Sp operator()(const _Tp* __p) const { return (__p->*__p_)(); }
};

template <class _Sp, class _Tp, class _Ap>
class _LIBCPP_TEMPLATE_VIS
_LIBCPP_DEPRECATED_IN_CXX11 const_mem_fun1_t : public __binary_function<const _Tp*, _Ap, _Sp> {
  _Sp (_Tp::*__p_)(_Ap) const;

public:
  _LIBCPP_HIDE_FROM_ABI explicit const_mem_fun1_t(_Sp (_Tp::*__p)(_Ap) const) : __p_(__p) {}
  _LIBCPP_HIDE_FROM_ABI _Sp operator()(const _Tp* __p, _Ap __x) const { return (__p->*__p_)(__x); }
};

template <class _Sp, class _Tp>
_LIBCPP_DEPRECATED_IN_CXX11 inline _LIBCPP_HIDE_FROM_ABI const_mem_fun_t<_Sp, _Tp> mem_fun(_Sp (_Tp::*__f)() const) {
  return const_mem_fun_t<_Sp, _Tp>(__f);
}

template <class _Sp, class _Tp, class _Ap>
_LIBCPP_DEPRECATED_IN_CXX11 inline _LIBCPP_HIDE_FROM_ABI const_mem_fun1_t<_Sp, _Tp, _Ap>
mem_fun(_Sp (_Tp::*__f)(_Ap) const) {
  return const_mem_fun1_t<_Sp, _Tp, _Ap>(__f);
}

template <class _Sp, class _Tp>
class _LIBCPP_TEMPLATE_VIS _LIBCPP_DEPRECATED_IN_CXX11 const_mem_fun_ref_t : public __unary_function<_Tp, _Sp> {
  _Sp (_Tp::*__p_)() const;

public:
  _LIBCPP_HIDE_FROM_ABI explicit const_mem_fun_ref_t(_Sp (_Tp::*__p)() const) : __p_(__p) {}
  _LIBCPP_HIDE_FROM_ABI _Sp operator()(const _Tp& __p) const { return (__p.*__p_)(); }
};

template <class _Sp, class _Tp, class _Ap>
class _LIBCPP_TEMPLATE_VIS _LIBCPP_DEPRECATED_IN_CXX11 const_mem_fun1_ref_t : public __binary_function<_Tp, _Ap, _Sp> {
  _Sp (_Tp::*__p_)(_Ap) const;

public:
  _LIBCPP_HIDE_FROM_ABI explicit const_mem_fun1_ref_t(_Sp (_Tp::*__p)(_Ap) const) : __p_(__p) {}
  _LIBCPP_HIDE_FROM_ABI _Sp operator()(const _Tp& __p, _Ap __x) const { return (__p.*__p_)(__x); }
};

template <class _Sp, class _Tp>
_LIBCPP_DEPRECATED_IN_CXX11 inline _LIBCPP_HIDE_FROM_ABI const_mem_fun_ref_t<_Sp, _Tp>
mem_fun_ref(_Sp (_Tp::*__f)() const) {
  return const_mem_fun_ref_t<_Sp, _Tp>(__f);
}

template <class _Sp, class _Tp, class _Ap>
_LIBCPP_DEPRECATED_IN_CXX11 inline _LIBCPP_HIDE_FROM_ABI const_mem_fun1_ref_t<_Sp, _Tp, _Ap>
mem_fun_ref(_Sp (_Tp::*__f)(_Ap) const) {
  return const_mem_fun1_ref_t<_Sp, _Tp, _Ap>(__f);
}

#endif // _LIBCPP_STD_VER <= 14 || defined(_LIBCPP_ENABLE_CXX17_REMOVED_BINDERS)

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FUNCTIONAL_MEM_FUN_REF_H
