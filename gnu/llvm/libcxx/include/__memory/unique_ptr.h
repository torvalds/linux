// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MEMORY_UNIQUE_PTR_H
#define _LIBCPP___MEMORY_UNIQUE_PTR_H

#include <__compare/compare_three_way.h>
#include <__compare/compare_three_way_result.h>
#include <__compare/three_way_comparable.h>
#include <__config>
#include <__functional/hash.h>
#include <__functional/operations.h>
#include <__memory/allocator_traits.h> // __pointer
#include <__memory/auto_ptr.h>
#include <__memory/compressed_pair.h>
#include <__type_traits/add_lvalue_reference.h>
#include <__type_traits/common_type.h>
#include <__type_traits/conditional.h>
#include <__type_traits/dependent_type.h>
#include <__type_traits/integral_constant.h>
#include <__type_traits/is_array.h>
#include <__type_traits/is_assignable.h>
#include <__type_traits/is_constructible.h>
#include <__type_traits/is_convertible.h>
#include <__type_traits/is_function.h>
#include <__type_traits/is_pointer.h>
#include <__type_traits/is_reference.h>
#include <__type_traits/is_same.h>
#include <__type_traits/is_swappable.h>
#include <__type_traits/is_trivially_relocatable.h>
#include <__type_traits/is_void.h>
#include <__type_traits/remove_extent.h>
#include <__type_traits/remove_pointer.h>
#include <__type_traits/type_identity.h>
#include <__utility/declval.h>
#include <__utility/forward.h>
#include <__utility/move.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#ifndef _LIBCPP_CXX03_LANG

template <class _Ptr>
struct __is_noexcept_deref_or_void {
  static constexpr bool value = noexcept(*std::declval<_Ptr>());
};

template <>
struct __is_noexcept_deref_or_void<void*> : true_type {};
#endif

template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS default_delete {
  static_assert(!is_function<_Tp>::value, "default_delete cannot be instantiated for function types");
#ifndef _LIBCPP_CXX03_LANG
  _LIBCPP_HIDE_FROM_ABI constexpr default_delete() _NOEXCEPT = default;
#else
  _LIBCPP_HIDE_FROM_ABI default_delete() {}
#endif
  template <class _Up, __enable_if_t<is_convertible<_Up*, _Tp*>::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 default_delete(const default_delete<_Up>&) _NOEXCEPT {}

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 void operator()(_Tp* __ptr) const _NOEXCEPT {
    static_assert(sizeof(_Tp) >= 0, "cannot delete an incomplete type");
    static_assert(!is_void<_Tp>::value, "cannot delete an incomplete type");
    delete __ptr;
  }
};

template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS default_delete<_Tp[]> {
private:
  template <class _Up>
  struct _EnableIfConvertible : enable_if<is_convertible<_Up (*)[], _Tp (*)[]>::value> {};

public:
#ifndef _LIBCPP_CXX03_LANG
  _LIBCPP_HIDE_FROM_ABI constexpr default_delete() _NOEXCEPT = default;
#else
  _LIBCPP_HIDE_FROM_ABI default_delete() {}
#endif

  template <class _Up>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23
  default_delete(const default_delete<_Up[]>&, typename _EnableIfConvertible<_Up>::type* = 0) _NOEXCEPT {}

  template <class _Up>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 typename _EnableIfConvertible<_Up>::type
  operator()(_Up* __ptr) const _NOEXCEPT {
    static_assert(sizeof(_Up) >= 0, "cannot delete an incomplete type");
    delete[] __ptr;
  }
};

template <class _Deleter>
struct __unique_ptr_deleter_sfinae {
  static_assert(!is_reference<_Deleter>::value, "incorrect specialization");
  typedef const _Deleter& __lval_ref_type;
  typedef _Deleter&& __good_rval_ref_type;
  typedef true_type __enable_rval_overload;
};

template <class _Deleter>
struct __unique_ptr_deleter_sfinae<_Deleter const&> {
  typedef const _Deleter& __lval_ref_type;
  typedef const _Deleter&& __bad_rval_ref_type;
  typedef false_type __enable_rval_overload;
};

template <class _Deleter>
struct __unique_ptr_deleter_sfinae<_Deleter&> {
  typedef _Deleter& __lval_ref_type;
  typedef _Deleter&& __bad_rval_ref_type;
  typedef false_type __enable_rval_overload;
};

#if defined(_LIBCPP_ABI_ENABLE_UNIQUE_PTR_TRIVIAL_ABI)
#  define _LIBCPP_UNIQUE_PTR_TRIVIAL_ABI __attribute__((__trivial_abi__))
#else
#  define _LIBCPP_UNIQUE_PTR_TRIVIAL_ABI
#endif

template <class _Tp, class _Dp = default_delete<_Tp> >
class _LIBCPP_UNIQUE_PTR_TRIVIAL_ABI _LIBCPP_TEMPLATE_VIS unique_ptr {
public:
  typedef _Tp element_type;
  typedef _Dp deleter_type;
  typedef _LIBCPP_NODEBUG typename __pointer<_Tp, deleter_type>::type pointer;

  static_assert(!is_rvalue_reference<deleter_type>::value, "the specified deleter type cannot be an rvalue reference");

  // A unique_ptr contains the following members which may be trivially relocatable:
  // - pointer : this may be trivially relocatable, so it's checked
  // - deleter_type: this may be trivially relocatable, so it's checked
  //
  // This unique_ptr implementation only contains a pointer to the unique object and a deleter, so there are no
  // references to itself. This means that the entire structure is trivially relocatable if its members are.
  using __trivially_relocatable = __conditional_t<
      __libcpp_is_trivially_relocatable<pointer>::value && __libcpp_is_trivially_relocatable<deleter_type>::value,
      unique_ptr,
      void>;

private:
  __compressed_pair<pointer, deleter_type> __ptr_;

  typedef _LIBCPP_NODEBUG __unique_ptr_deleter_sfinae<_Dp> _DeleterSFINAE;

  template <bool _Dummy>
  using _LValRefType _LIBCPP_NODEBUG = typename __dependent_type<_DeleterSFINAE, _Dummy>::__lval_ref_type;

  template <bool _Dummy>
  using _GoodRValRefType _LIBCPP_NODEBUG = typename __dependent_type<_DeleterSFINAE, _Dummy>::__good_rval_ref_type;

  template <bool _Dummy>
  using _BadRValRefType _LIBCPP_NODEBUG = typename __dependent_type<_DeleterSFINAE, _Dummy>::__bad_rval_ref_type;

  template <bool _Dummy, class _Deleter = typename __dependent_type< __type_identity<deleter_type>, _Dummy>::type>
  using _EnableIfDeleterDefaultConstructible _LIBCPP_NODEBUG =
      __enable_if_t<is_default_constructible<_Deleter>::value && !is_pointer<_Deleter>::value>;

  template <class _ArgType>
  using _EnableIfDeleterConstructible _LIBCPP_NODEBUG = __enable_if_t<is_constructible<deleter_type, _ArgType>::value>;

  template <class _UPtr, class _Up>
  using _EnableIfMoveConvertible _LIBCPP_NODEBUG =
      __enable_if_t< is_convertible<typename _UPtr::pointer, pointer>::value && !is_array<_Up>::value >;

  template <class _UDel>
  using _EnableIfDeleterConvertible _LIBCPP_NODEBUG =
      __enable_if_t< (is_reference<_Dp>::value && is_same<_Dp, _UDel>::value) ||
                     (!is_reference<_Dp>::value && is_convertible<_UDel, _Dp>::value) >;

  template <class _UDel>
  using _EnableIfDeleterAssignable = __enable_if_t< is_assignable<_Dp&, _UDel&&>::value >;

public:
  template <bool _Dummy = true, class = _EnableIfDeleterDefaultConstructible<_Dummy> >
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR unique_ptr() _NOEXCEPT : __ptr_(__value_init_tag(), __value_init_tag()) {}

  template <bool _Dummy = true, class = _EnableIfDeleterDefaultConstructible<_Dummy> >
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR unique_ptr(nullptr_t) _NOEXCEPT
      : __ptr_(__value_init_tag(), __value_init_tag()) {}

  template <bool _Dummy = true, class = _EnableIfDeleterDefaultConstructible<_Dummy> >
  _LIBCPP_HIDE_FROM_ABI
  _LIBCPP_CONSTEXPR_SINCE_CXX23 explicit unique_ptr(pointer __p) _NOEXCEPT : __ptr_(__p, __value_init_tag()) {}

  template <bool _Dummy = true, class = _EnableIfDeleterConstructible<_LValRefType<_Dummy> > >
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 unique_ptr(pointer __p, _LValRefType<_Dummy> __d) _NOEXCEPT
      : __ptr_(__p, __d) {}

  template <bool _Dummy = true, class = _EnableIfDeleterConstructible<_GoodRValRefType<_Dummy> > >
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 unique_ptr(pointer __p, _GoodRValRefType<_Dummy> __d) _NOEXCEPT
      : __ptr_(__p, std::move(__d)) {
    static_assert(!is_reference<deleter_type>::value, "rvalue deleter bound to reference");
  }

  template <bool _Dummy = true, class = _EnableIfDeleterConstructible<_BadRValRefType<_Dummy> > >
  _LIBCPP_HIDE_FROM_ABI unique_ptr(pointer __p, _BadRValRefType<_Dummy> __d) = delete;

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 unique_ptr(unique_ptr&& __u) _NOEXCEPT
      : __ptr_(__u.release(), std::forward<deleter_type>(__u.get_deleter())) {}

  template <class _Up,
            class _Ep,
            class = _EnableIfMoveConvertible<unique_ptr<_Up, _Ep>, _Up>,
            class = _EnableIfDeleterConvertible<_Ep> >
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 unique_ptr(unique_ptr<_Up, _Ep>&& __u) _NOEXCEPT
      : __ptr_(__u.release(), std::forward<_Ep>(__u.get_deleter())) {}

#if _LIBCPP_STD_VER <= 14 || defined(_LIBCPP_ENABLE_CXX17_REMOVED_AUTO_PTR)
  template <class _Up,
            __enable_if_t<is_convertible<_Up*, _Tp*>::value && is_same<_Dp, default_delete<_Tp> >::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI unique_ptr(auto_ptr<_Up>&& __p) _NOEXCEPT : __ptr_(__p.release(), __value_init_tag()) {}
#endif

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 unique_ptr& operator=(unique_ptr&& __u) _NOEXCEPT {
    reset(__u.release());
    __ptr_.second() = std::forward<deleter_type>(__u.get_deleter());
    return *this;
  }

  template <class _Up,
            class _Ep,
            class = _EnableIfMoveConvertible<unique_ptr<_Up, _Ep>, _Up>,
            class = _EnableIfDeleterAssignable<_Ep> >
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 unique_ptr& operator=(unique_ptr<_Up, _Ep>&& __u) _NOEXCEPT {
    reset(__u.release());
    __ptr_.second() = std::forward<_Ep>(__u.get_deleter());
    return *this;
  }

#if _LIBCPP_STD_VER <= 14 || defined(_LIBCPP_ENABLE_CXX17_REMOVED_AUTO_PTR)
  template <class _Up,
            __enable_if_t<is_convertible<_Up*, _Tp*>::value && is_same<_Dp, default_delete<_Tp> >::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI unique_ptr& operator=(auto_ptr<_Up> __p) {
    reset(__p.release());
    return *this;
  }
#endif

#ifdef _LIBCPP_CXX03_LANG
  unique_ptr(unique_ptr const&)            = delete;
  unique_ptr& operator=(unique_ptr const&) = delete;
#endif

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 ~unique_ptr() { reset(); }

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 unique_ptr& operator=(nullptr_t) _NOEXCEPT {
    reset();
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 __add_lvalue_reference_t<_Tp> operator*() const
      _NOEXCEPT_(__is_noexcept_deref_or_void<pointer>::value) {
    return *__ptr_.first();
  }
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 pointer operator->() const _NOEXCEPT { return __ptr_.first(); }
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 pointer get() const _NOEXCEPT { return __ptr_.first(); }
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 deleter_type& get_deleter() _NOEXCEPT { return __ptr_.second(); }
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 const deleter_type& get_deleter() const _NOEXCEPT {
    return __ptr_.second();
  }
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 explicit operator bool() const _NOEXCEPT {
    return __ptr_.first() != nullptr;
  }

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 pointer release() _NOEXCEPT {
    pointer __t    = __ptr_.first();
    __ptr_.first() = pointer();
    return __t;
  }

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 void reset(pointer __p = pointer()) _NOEXCEPT {
    pointer __tmp  = __ptr_.first();
    __ptr_.first() = __p;
    if (__tmp)
      __ptr_.second()(__tmp);
  }

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 void swap(unique_ptr& __u) _NOEXCEPT { __ptr_.swap(__u.__ptr_); }
};

template <class _Tp, class _Dp>
class _LIBCPP_UNIQUE_PTR_TRIVIAL_ABI _LIBCPP_TEMPLATE_VIS unique_ptr<_Tp[], _Dp> {
public:
  typedef _Tp element_type;
  typedef _Dp deleter_type;
  typedef typename __pointer<_Tp, deleter_type>::type pointer;

  // A unique_ptr contains the following members which may be trivially relocatable:
  // - pointer : this may be trivially relocatable, so it's checked
  // - deleter_type: this may be trivially relocatable, so it's checked
  //
  // This unique_ptr implementation only contains a pointer to the unique object and a deleter, so there are no
  // references to itself. This means that the entire structure is trivially relocatable if its members are.
  using __trivially_relocatable = __conditional_t<
      __libcpp_is_trivially_relocatable<pointer>::value && __libcpp_is_trivially_relocatable<deleter_type>::value,
      unique_ptr,
      void>;

private:
  __compressed_pair<pointer, deleter_type> __ptr_;

  template <class _From>
  struct _CheckArrayPointerConversion : is_same<_From, pointer> {};

  template <class _FromElem>
  struct _CheckArrayPointerConversion<_FromElem*>
      : integral_constant<bool,
                          is_same<_FromElem*, pointer>::value ||
                              (is_same<pointer, element_type*>::value &&
                               is_convertible<_FromElem (*)[], element_type (*)[]>::value) > {};

  typedef __unique_ptr_deleter_sfinae<_Dp> _DeleterSFINAE;

  template <bool _Dummy>
  using _LValRefType _LIBCPP_NODEBUG = typename __dependent_type<_DeleterSFINAE, _Dummy>::__lval_ref_type;

  template <bool _Dummy>
  using _GoodRValRefType _LIBCPP_NODEBUG = typename __dependent_type<_DeleterSFINAE, _Dummy>::__good_rval_ref_type;

  template <bool _Dummy>
  using _BadRValRefType _LIBCPP_NODEBUG = typename __dependent_type<_DeleterSFINAE, _Dummy>::__bad_rval_ref_type;

  template <bool _Dummy, class _Deleter = typename __dependent_type< __type_identity<deleter_type>, _Dummy>::type>
  using _EnableIfDeleterDefaultConstructible _LIBCPP_NODEBUG =
      __enable_if_t<is_default_constructible<_Deleter>::value && !is_pointer<_Deleter>::value>;

  template <class _ArgType>
  using _EnableIfDeleterConstructible _LIBCPP_NODEBUG = __enable_if_t<is_constructible<deleter_type, _ArgType>::value>;

  template <class _Pp>
  using _EnableIfPointerConvertible _LIBCPP_NODEBUG = __enable_if_t< _CheckArrayPointerConversion<_Pp>::value >;

  template <class _UPtr, class _Up, class _ElemT = typename _UPtr::element_type>
  using _EnableIfMoveConvertible _LIBCPP_NODEBUG =
      __enable_if_t< is_array<_Up>::value && is_same<pointer, element_type*>::value &&
                     is_same<typename _UPtr::pointer, _ElemT*>::value &&
                     is_convertible<_ElemT (*)[], element_type (*)[]>::value >;

  template <class _UDel>
  using _EnableIfDeleterConvertible _LIBCPP_NODEBUG =
      __enable_if_t< (is_reference<_Dp>::value && is_same<_Dp, _UDel>::value) ||
                     (!is_reference<_Dp>::value && is_convertible<_UDel, _Dp>::value) >;

  template <class _UDel>
  using _EnableIfDeleterAssignable _LIBCPP_NODEBUG = __enable_if_t< is_assignable<_Dp&, _UDel&&>::value >;

public:
  template <bool _Dummy = true, class = _EnableIfDeleterDefaultConstructible<_Dummy> >
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR unique_ptr() _NOEXCEPT : __ptr_(__value_init_tag(), __value_init_tag()) {}

  template <bool _Dummy = true, class = _EnableIfDeleterDefaultConstructible<_Dummy> >
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR unique_ptr(nullptr_t) _NOEXCEPT
      : __ptr_(__value_init_tag(), __value_init_tag()) {}

  template <class _Pp,
            bool _Dummy = true,
            class       = _EnableIfDeleterDefaultConstructible<_Dummy>,
            class       = _EnableIfPointerConvertible<_Pp> >
  _LIBCPP_HIDE_FROM_ABI
  _LIBCPP_CONSTEXPR_SINCE_CXX23 explicit unique_ptr(_Pp __p) _NOEXCEPT : __ptr_(__p, __value_init_tag()) {}

  template <class _Pp,
            bool _Dummy = true,
            class       = _EnableIfDeleterConstructible<_LValRefType<_Dummy> >,
            class       = _EnableIfPointerConvertible<_Pp> >
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 unique_ptr(_Pp __p, _LValRefType<_Dummy> __d) _NOEXCEPT
      : __ptr_(__p, __d) {}

  template <bool _Dummy = true, class = _EnableIfDeleterConstructible<_LValRefType<_Dummy> > >
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 unique_ptr(nullptr_t, _LValRefType<_Dummy> __d) _NOEXCEPT
      : __ptr_(nullptr, __d) {}

  template <class _Pp,
            bool _Dummy = true,
            class       = _EnableIfDeleterConstructible<_GoodRValRefType<_Dummy> >,
            class       = _EnableIfPointerConvertible<_Pp> >
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 unique_ptr(_Pp __p, _GoodRValRefType<_Dummy> __d) _NOEXCEPT
      : __ptr_(__p, std::move(__d)) {
    static_assert(!is_reference<deleter_type>::value, "rvalue deleter bound to reference");
  }

  template <bool _Dummy = true, class = _EnableIfDeleterConstructible<_GoodRValRefType<_Dummy> > >
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 unique_ptr(nullptr_t, _GoodRValRefType<_Dummy> __d) _NOEXCEPT
      : __ptr_(nullptr, std::move(__d)) {
    static_assert(!is_reference<deleter_type>::value, "rvalue deleter bound to reference");
  }

  template <class _Pp,
            bool _Dummy = true,
            class       = _EnableIfDeleterConstructible<_BadRValRefType<_Dummy> >,
            class       = _EnableIfPointerConvertible<_Pp> >
  _LIBCPP_HIDE_FROM_ABI unique_ptr(_Pp __p, _BadRValRefType<_Dummy> __d) = delete;

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 unique_ptr(unique_ptr&& __u) _NOEXCEPT
      : __ptr_(__u.release(), std::forward<deleter_type>(__u.get_deleter())) {}

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 unique_ptr& operator=(unique_ptr&& __u) _NOEXCEPT {
    reset(__u.release());
    __ptr_.second() = std::forward<deleter_type>(__u.get_deleter());
    return *this;
  }

  template <class _Up,
            class _Ep,
            class = _EnableIfMoveConvertible<unique_ptr<_Up, _Ep>, _Up>,
            class = _EnableIfDeleterConvertible<_Ep> >
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 unique_ptr(unique_ptr<_Up, _Ep>&& __u) _NOEXCEPT
      : __ptr_(__u.release(), std::forward<_Ep>(__u.get_deleter())) {}

  template <class _Up,
            class _Ep,
            class = _EnableIfMoveConvertible<unique_ptr<_Up, _Ep>, _Up>,
            class = _EnableIfDeleterAssignable<_Ep> >
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 unique_ptr& operator=(unique_ptr<_Up, _Ep>&& __u) _NOEXCEPT {
    reset(__u.release());
    __ptr_.second() = std::forward<_Ep>(__u.get_deleter());
    return *this;
  }

#ifdef _LIBCPP_CXX03_LANG
  unique_ptr(unique_ptr const&)            = delete;
  unique_ptr& operator=(unique_ptr const&) = delete;
#endif

public:
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 ~unique_ptr() { reset(); }

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 unique_ptr& operator=(nullptr_t) _NOEXCEPT {
    reset();
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 __add_lvalue_reference_t<_Tp> operator[](size_t __i) const {
    return __ptr_.first()[__i];
  }
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 pointer get() const _NOEXCEPT { return __ptr_.first(); }

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 deleter_type& get_deleter() _NOEXCEPT { return __ptr_.second(); }

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 const deleter_type& get_deleter() const _NOEXCEPT {
    return __ptr_.second();
  }
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 explicit operator bool() const _NOEXCEPT {
    return __ptr_.first() != nullptr;
  }

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 pointer release() _NOEXCEPT {
    pointer __t    = __ptr_.first();
    __ptr_.first() = pointer();
    return __t;
  }

  template <class _Pp, __enable_if_t<_CheckArrayPointerConversion<_Pp>::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 void reset(_Pp __p) _NOEXCEPT {
    pointer __tmp  = __ptr_.first();
    __ptr_.first() = __p;
    if (__tmp)
      __ptr_.second()(__tmp);
  }

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 void reset(nullptr_t = nullptr) _NOEXCEPT {
    pointer __tmp  = __ptr_.first();
    __ptr_.first() = nullptr;
    if (__tmp)
      __ptr_.second()(__tmp);
  }

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 void swap(unique_ptr& __u) _NOEXCEPT { __ptr_.swap(__u.__ptr_); }
};

template <class _Tp, class _Dp, __enable_if_t<__is_swappable_v<_Dp>, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 void
swap(unique_ptr<_Tp, _Dp>& __x, unique_ptr<_Tp, _Dp>& __y) _NOEXCEPT {
  __x.swap(__y);
}

template <class _T1, class _D1, class _T2, class _D2>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 bool
operator==(const unique_ptr<_T1, _D1>& __x, const unique_ptr<_T2, _D2>& __y) {
  return __x.get() == __y.get();
}

#if _LIBCPP_STD_VER <= 17
template <class _T1, class _D1, class _T2, class _D2>
inline _LIBCPP_HIDE_FROM_ABI bool operator!=(const unique_ptr<_T1, _D1>& __x, const unique_ptr<_T2, _D2>& __y) {
  return !(__x == __y);
}
#endif

template <class _T1, class _D1, class _T2, class _D2>
inline _LIBCPP_HIDE_FROM_ABI bool operator<(const unique_ptr<_T1, _D1>& __x, const unique_ptr<_T2, _D2>& __y) {
  typedef typename unique_ptr<_T1, _D1>::pointer _P1;
  typedef typename unique_ptr<_T2, _D2>::pointer _P2;
  typedef typename common_type<_P1, _P2>::type _Vp;
  return less<_Vp>()(__x.get(), __y.get());
}

template <class _T1, class _D1, class _T2, class _D2>
inline _LIBCPP_HIDE_FROM_ABI bool operator>(const unique_ptr<_T1, _D1>& __x, const unique_ptr<_T2, _D2>& __y) {
  return __y < __x;
}

template <class _T1, class _D1, class _T2, class _D2>
inline _LIBCPP_HIDE_FROM_ABI bool operator<=(const unique_ptr<_T1, _D1>& __x, const unique_ptr<_T2, _D2>& __y) {
  return !(__y < __x);
}

template <class _T1, class _D1, class _T2, class _D2>
inline _LIBCPP_HIDE_FROM_ABI bool operator>=(const unique_ptr<_T1, _D1>& __x, const unique_ptr<_T2, _D2>& __y) {
  return !(__x < __y);
}

#if _LIBCPP_STD_VER >= 20
template <class _T1, class _D1, class _T2, class _D2>
  requires three_way_comparable_with<typename unique_ptr<_T1, _D1>::pointer, typename unique_ptr<_T2, _D2>::pointer>
_LIBCPP_HIDE_FROM_ABI
compare_three_way_result_t<typename unique_ptr<_T1, _D1>::pointer, typename unique_ptr<_T2, _D2>::pointer>
operator<=>(const unique_ptr<_T1, _D1>& __x, const unique_ptr<_T2, _D2>& __y) {
  return compare_three_way()(__x.get(), __y.get());
}
#endif

template <class _T1, class _D1>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 bool
operator==(const unique_ptr<_T1, _D1>& __x, nullptr_t) _NOEXCEPT {
  return !__x;
}

#if _LIBCPP_STD_VER <= 17
template <class _T1, class _D1>
inline _LIBCPP_HIDE_FROM_ABI bool operator==(nullptr_t, const unique_ptr<_T1, _D1>& __x) _NOEXCEPT {
  return !__x;
}

template <class _T1, class _D1>
inline _LIBCPP_HIDE_FROM_ABI bool operator!=(const unique_ptr<_T1, _D1>& __x, nullptr_t) _NOEXCEPT {
  return static_cast<bool>(__x);
}

template <class _T1, class _D1>
inline _LIBCPP_HIDE_FROM_ABI bool operator!=(nullptr_t, const unique_ptr<_T1, _D1>& __x) _NOEXCEPT {
  return static_cast<bool>(__x);
}
#endif // _LIBCPP_STD_VER <= 17

template <class _T1, class _D1>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 bool operator<(const unique_ptr<_T1, _D1>& __x, nullptr_t) {
  typedef typename unique_ptr<_T1, _D1>::pointer _P1;
  return less<_P1>()(__x.get(), nullptr);
}

template <class _T1, class _D1>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 bool operator<(nullptr_t, const unique_ptr<_T1, _D1>& __x) {
  typedef typename unique_ptr<_T1, _D1>::pointer _P1;
  return less<_P1>()(nullptr, __x.get());
}

template <class _T1, class _D1>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 bool operator>(const unique_ptr<_T1, _D1>& __x, nullptr_t) {
  return nullptr < __x;
}

template <class _T1, class _D1>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 bool operator>(nullptr_t, const unique_ptr<_T1, _D1>& __x) {
  return __x < nullptr;
}

template <class _T1, class _D1>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 bool operator<=(const unique_ptr<_T1, _D1>& __x, nullptr_t) {
  return !(nullptr < __x);
}

template <class _T1, class _D1>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 bool operator<=(nullptr_t, const unique_ptr<_T1, _D1>& __x) {
  return !(__x < nullptr);
}

template <class _T1, class _D1>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 bool operator>=(const unique_ptr<_T1, _D1>& __x, nullptr_t) {
  return !(__x < nullptr);
}

template <class _T1, class _D1>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 bool operator>=(nullptr_t, const unique_ptr<_T1, _D1>& __x) {
  return !(nullptr < __x);
}

#if _LIBCPP_STD_VER >= 20
template <class _T1, class _D1>
  requires three_way_comparable< typename unique_ptr<_T1, _D1>::pointer>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 compare_three_way_result_t<typename unique_ptr<_T1, _D1>::pointer>
operator<=>(const unique_ptr<_T1, _D1>& __x, nullptr_t) {
  return compare_three_way()(__x.get(), static_cast<typename unique_ptr<_T1, _D1>::pointer>(nullptr));
}
#endif

#if _LIBCPP_STD_VER >= 14

template <class _Tp>
struct __unique_if {
  typedef unique_ptr<_Tp> __unique_single;
};

template <class _Tp>
struct __unique_if<_Tp[]> {
  typedef unique_ptr<_Tp[]> __unique_array_unknown_bound;
};

template <class _Tp, size_t _Np>
struct __unique_if<_Tp[_Np]> {
  typedef void __unique_array_known_bound;
};

template <class _Tp, class... _Args>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 typename __unique_if<_Tp>::__unique_single
make_unique(_Args&&... __args) {
  return unique_ptr<_Tp>(new _Tp(std::forward<_Args>(__args)...));
}

template <class _Tp>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 typename __unique_if<_Tp>::__unique_array_unknown_bound
make_unique(size_t __n) {
  typedef __remove_extent_t<_Tp> _Up;
  return unique_ptr<_Tp>(new _Up[__n]());
}

template <class _Tp, class... _Args>
typename __unique_if<_Tp>::__unique_array_known_bound make_unique(_Args&&...) = delete;

#endif // _LIBCPP_STD_VER >= 14

#if _LIBCPP_STD_VER >= 20

template <class _Tp>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 typename __unique_if<_Tp>::__unique_single
make_unique_for_overwrite() {
  return unique_ptr<_Tp>(new _Tp);
}

template <class _Tp>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX23 typename __unique_if<_Tp>::__unique_array_unknown_bound
make_unique_for_overwrite(size_t __n) {
  return unique_ptr<_Tp>(new __remove_extent_t<_Tp>[__n]);
}

template <class _Tp, class... _Args>
typename __unique_if<_Tp>::__unique_array_known_bound make_unique_for_overwrite(_Args&&...) = delete;

#endif // _LIBCPP_STD_VER >= 20

template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS hash;

template <class _Tp, class _Dp>
#ifdef _LIBCPP_CXX03_LANG
struct _LIBCPP_TEMPLATE_VIS hash<unique_ptr<_Tp, _Dp> >
#else
struct _LIBCPP_TEMPLATE_VIS hash<__enable_hash_helper< unique_ptr<_Tp, _Dp>, typename unique_ptr<_Tp, _Dp>::pointer> >
#endif
{
#if _LIBCPP_STD_VER <= 17 || defined(_LIBCPP_ENABLE_CXX20_REMOVED_BINDER_TYPEDEFS)
  _LIBCPP_DEPRECATED_IN_CXX17 typedef unique_ptr<_Tp, _Dp> argument_type;
  _LIBCPP_DEPRECATED_IN_CXX17 typedef size_t result_type;
#endif

  _LIBCPP_HIDE_FROM_ABI size_t operator()(const unique_ptr<_Tp, _Dp>& __ptr) const {
    typedef typename unique_ptr<_Tp, _Dp>::pointer pointer;
    return hash<pointer>()(__ptr.get());
  }
};

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___MEMORY_UNIQUE_PTR_H
