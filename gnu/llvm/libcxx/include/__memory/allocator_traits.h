// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MEMORY_ALLOCATOR_TRAITS_H
#define _LIBCPP___MEMORY_ALLOCATOR_TRAITS_H

#include <__config>
#include <__memory/construct_at.h>
#include <__memory/pointer_traits.h>
#include <__type_traits/enable_if.h>
#include <__type_traits/is_constructible.h>
#include <__type_traits/is_empty.h>
#include <__type_traits/is_same.h>
#include <__type_traits/make_unsigned.h>
#include <__type_traits/remove_reference.h>
#include <__type_traits/void_t.h>
#include <__utility/declval.h>
#include <__utility/forward.h>
#include <cstddef>
#include <limits>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#define _LIBCPP_ALLOCATOR_TRAITS_HAS_XXX(NAME, PROPERTY)                                                               \
  template <class _Tp, class = void>                                                                                   \
  struct NAME : false_type {};                                                                                         \
  template <class _Tp>                                                                                                 \
  struct NAME<_Tp, __void_t<typename _Tp::PROPERTY > > : true_type {}

// __pointer
template <class _Tp,
          class _Alloc,
          class _RawAlloc = __libcpp_remove_reference_t<_Alloc>,
          bool            = __has_pointer<_RawAlloc>::value>
struct __pointer {
  using type _LIBCPP_NODEBUG = typename _RawAlloc::pointer;
};
template <class _Tp, class _Alloc, class _RawAlloc>
struct __pointer<_Tp, _Alloc, _RawAlloc, false> {
  using type _LIBCPP_NODEBUG = _Tp*;
};

// __const_pointer
_LIBCPP_ALLOCATOR_TRAITS_HAS_XXX(__has_const_pointer, const_pointer);
template <class _Tp, class _Ptr, class _Alloc, bool = __has_const_pointer<_Alloc>::value>
struct __const_pointer {
  using type _LIBCPP_NODEBUG = typename _Alloc::const_pointer;
};
template <class _Tp, class _Ptr, class _Alloc>
struct __const_pointer<_Tp, _Ptr, _Alloc, false> {
#ifdef _LIBCPP_CXX03_LANG
  using type = typename pointer_traits<_Ptr>::template rebind<const _Tp>::other;
#else
  using type _LIBCPP_NODEBUG = typename pointer_traits<_Ptr>::template rebind<const _Tp>;
#endif
};

// __void_pointer
_LIBCPP_ALLOCATOR_TRAITS_HAS_XXX(__has_void_pointer, void_pointer);
template <class _Ptr, class _Alloc, bool = __has_void_pointer<_Alloc>::value>
struct __void_pointer {
  using type _LIBCPP_NODEBUG = typename _Alloc::void_pointer;
};
template <class _Ptr, class _Alloc>
struct __void_pointer<_Ptr, _Alloc, false> {
#ifdef _LIBCPP_CXX03_LANG
  using type _LIBCPP_NODEBUG = typename pointer_traits<_Ptr>::template rebind<void>::other;
#else
  using type _LIBCPP_NODEBUG = typename pointer_traits<_Ptr>::template rebind<void>;
#endif
};

// __const_void_pointer
_LIBCPP_ALLOCATOR_TRAITS_HAS_XXX(__has_const_void_pointer, const_void_pointer);
template <class _Ptr, class _Alloc, bool = __has_const_void_pointer<_Alloc>::value>
struct __const_void_pointer {
  using type _LIBCPP_NODEBUG = typename _Alloc::const_void_pointer;
};
template <class _Ptr, class _Alloc>
struct __const_void_pointer<_Ptr, _Alloc, false> {
#ifdef _LIBCPP_CXX03_LANG
  using type _LIBCPP_NODEBUG = typename pointer_traits<_Ptr>::template rebind<const void>::other;
#else
  using type _LIBCPP_NODEBUG = typename pointer_traits<_Ptr>::template rebind<const void>;
#endif
};

// __size_type
_LIBCPP_ALLOCATOR_TRAITS_HAS_XXX(__has_size_type, size_type);
template <class _Alloc, class _DiffType, bool = __has_size_type<_Alloc>::value>
struct __size_type : make_unsigned<_DiffType> {};
template <class _Alloc, class _DiffType>
struct __size_type<_Alloc, _DiffType, true> {
  using type _LIBCPP_NODEBUG = typename _Alloc::size_type;
};

// __alloc_traits_difference_type
_LIBCPP_ALLOCATOR_TRAITS_HAS_XXX(__has_alloc_traits_difference_type, difference_type);
template <class _Alloc, class _Ptr, bool = __has_alloc_traits_difference_type<_Alloc>::value>
struct __alloc_traits_difference_type {
  using type _LIBCPP_NODEBUG = typename pointer_traits<_Ptr>::difference_type;
};
template <class _Alloc, class _Ptr>
struct __alloc_traits_difference_type<_Alloc, _Ptr, true> {
  using type _LIBCPP_NODEBUG = typename _Alloc::difference_type;
};

// __propagate_on_container_copy_assignment
_LIBCPP_ALLOCATOR_TRAITS_HAS_XXX(__has_propagate_on_container_copy_assignment, propagate_on_container_copy_assignment);
template <class _Alloc, bool = __has_propagate_on_container_copy_assignment<_Alloc>::value>
struct __propagate_on_container_copy_assignment : false_type {};
template <class _Alloc>
struct __propagate_on_container_copy_assignment<_Alloc, true> {
  using type _LIBCPP_NODEBUG = typename _Alloc::propagate_on_container_copy_assignment;
};

// __propagate_on_container_move_assignment
_LIBCPP_ALLOCATOR_TRAITS_HAS_XXX(__has_propagate_on_container_move_assignment, propagate_on_container_move_assignment);
template <class _Alloc, bool = __has_propagate_on_container_move_assignment<_Alloc>::value>
struct __propagate_on_container_move_assignment : false_type {};
template <class _Alloc>
struct __propagate_on_container_move_assignment<_Alloc, true> {
  using type _LIBCPP_NODEBUG = typename _Alloc::propagate_on_container_move_assignment;
};

// __propagate_on_container_swap
_LIBCPP_ALLOCATOR_TRAITS_HAS_XXX(__has_propagate_on_container_swap, propagate_on_container_swap);
template <class _Alloc, bool = __has_propagate_on_container_swap<_Alloc>::value>
struct __propagate_on_container_swap : false_type {};
template <class _Alloc>
struct __propagate_on_container_swap<_Alloc, true> {
  using type _LIBCPP_NODEBUG = typename _Alloc::propagate_on_container_swap;
};

// __is_always_equal
_LIBCPP_ALLOCATOR_TRAITS_HAS_XXX(__has_is_always_equal, is_always_equal);
template <class _Alloc, bool = __has_is_always_equal<_Alloc>::value>
struct __is_always_equal : is_empty<_Alloc> {};
template <class _Alloc>
struct __is_always_equal<_Alloc, true> {
  using type _LIBCPP_NODEBUG = typename _Alloc::is_always_equal;
};

// __allocator_traits_rebind
_LIBCPP_SUPPRESS_DEPRECATED_PUSH
template <class _Tp, class _Up, class = void>
struct __has_rebind_other : false_type {};
template <class _Tp, class _Up>
struct __has_rebind_other<_Tp, _Up, __void_t<typename _Tp::template rebind<_Up>::other> > : true_type {};

template <class _Tp, class _Up, bool = __has_rebind_other<_Tp, _Up>::value>
struct __allocator_traits_rebind {
  static_assert(__has_rebind_other<_Tp, _Up>::value, "This allocator has to implement rebind");
  using type _LIBCPP_NODEBUG = typename _Tp::template rebind<_Up>::other;
};
template <template <class, class...> class _Alloc, class _Tp, class... _Args, class _Up>
struct __allocator_traits_rebind<_Alloc<_Tp, _Args...>, _Up, true> {
  using type _LIBCPP_NODEBUG = typename _Alloc<_Tp, _Args...>::template rebind<_Up>::other;
};
template <template <class, class...> class _Alloc, class _Tp, class... _Args, class _Up>
struct __allocator_traits_rebind<_Alloc<_Tp, _Args...>, _Up, false> {
  using type _LIBCPP_NODEBUG = _Alloc<_Up, _Args...>;
};
_LIBCPP_SUPPRESS_DEPRECATED_POP

template <class _Alloc, class _Tp>
using __allocator_traits_rebind_t = typename __allocator_traits_rebind<_Alloc, _Tp>::type;

_LIBCPP_SUPPRESS_DEPRECATED_PUSH

// __has_allocate_hint
template <class _Alloc, class _SizeType, class _ConstVoidPtr, class = void>
struct __has_allocate_hint : false_type {};

template <class _Alloc, class _SizeType, class _ConstVoidPtr>
struct __has_allocate_hint<
    _Alloc,
    _SizeType,
    _ConstVoidPtr,
    decltype((void)std::declval<_Alloc>().allocate(std::declval<_SizeType>(), std::declval<_ConstVoidPtr>()))>
    : true_type {};

// __has_construct
template <class, class _Alloc, class... _Args>
struct __has_construct_impl : false_type {};

template <class _Alloc, class... _Args>
struct __has_construct_impl<decltype((void)std::declval<_Alloc>().construct(std::declval<_Args>()...)),
                            _Alloc,
                            _Args...> : true_type {};

template <class _Alloc, class... _Args>
struct __has_construct : __has_construct_impl<void, _Alloc, _Args...> {};

// __has_destroy
template <class _Alloc, class _Pointer, class = void>
struct __has_destroy : false_type {};

template <class _Alloc, class _Pointer>
struct __has_destroy<_Alloc, _Pointer, decltype((void)std::declval<_Alloc>().destroy(std::declval<_Pointer>()))>
    : true_type {};

// __has_max_size
template <class _Alloc, class = void>
struct __has_max_size : false_type {};

template <class _Alloc>
struct __has_max_size<_Alloc, decltype((void)std::declval<_Alloc&>().max_size())> : true_type {};

// __has_select_on_container_copy_construction
template <class _Alloc, class = void>
struct __has_select_on_container_copy_construction : false_type {};

template <class _Alloc>
struct __has_select_on_container_copy_construction<
    _Alloc,
    decltype((void)std::declval<_Alloc>().select_on_container_copy_construction())> : true_type {};

_LIBCPP_SUPPRESS_DEPRECATED_POP

#if _LIBCPP_STD_VER >= 23

template <class _Pointer, class _SizeType = size_t>
struct allocation_result {
  _Pointer ptr;
  _SizeType count;
};
_LIBCPP_CTAD_SUPPORTED_FOR_TYPE(allocation_result);

#endif // _LIBCPP_STD_VER

template <class _Alloc>
struct _LIBCPP_TEMPLATE_VIS allocator_traits {
  using allocator_type     = _Alloc;
  using value_type         = typename allocator_type::value_type;
  using pointer            = typename __pointer<value_type, allocator_type>::type;
  using const_pointer      = typename __const_pointer<value_type, pointer, allocator_type>::type;
  using void_pointer       = typename __void_pointer<pointer, allocator_type>::type;
  using const_void_pointer = typename __const_void_pointer<pointer, allocator_type>::type;
  using difference_type    = typename __alloc_traits_difference_type<allocator_type, pointer>::type;
  using size_type          = typename __size_type<allocator_type, difference_type>::type;
  using propagate_on_container_copy_assignment =
      typename __propagate_on_container_copy_assignment<allocator_type>::type;
  using propagate_on_container_move_assignment =
      typename __propagate_on_container_move_assignment<allocator_type>::type;
  using propagate_on_container_swap = typename __propagate_on_container_swap<allocator_type>::type;
  using is_always_equal             = typename __is_always_equal<allocator_type>::type;

#ifndef _LIBCPP_CXX03_LANG
  template <class _Tp>
  using rebind_alloc = __allocator_traits_rebind_t<allocator_type, _Tp>;
  template <class _Tp>
  using rebind_traits = allocator_traits<rebind_alloc<_Tp> >;
#else  // _LIBCPP_CXX03_LANG
  template <class _Tp>
  struct rebind_alloc {
    using other = __allocator_traits_rebind_t<allocator_type, _Tp>;
  };
  template <class _Tp>
  struct rebind_traits {
    using other = allocator_traits<typename rebind_alloc<_Tp>::other>;
  };
#endif // _LIBCPP_CXX03_LANG

  _LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 static pointer
  allocate(allocator_type& __a, size_type __n) {
    return __a.allocate(__n);
  }

  template <class _Ap = _Alloc, __enable_if_t<__has_allocate_hint<_Ap, size_type, const_void_pointer>::value, int> = 0>
  _LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 static pointer
  allocate(allocator_type& __a, size_type __n, const_void_pointer __hint) {
    _LIBCPP_SUPPRESS_DEPRECATED_PUSH
    return __a.allocate(__n, __hint);
    _LIBCPP_SUPPRESS_DEPRECATED_POP
  }
  template <class _Ap                                                                           = _Alloc,
            class                                                                               = void,
            __enable_if_t<!__has_allocate_hint<_Ap, size_type, const_void_pointer>::value, int> = 0>
  _LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 static pointer
  allocate(allocator_type& __a, size_type __n, const_void_pointer) {
    return __a.allocate(__n);
  }

#if _LIBCPP_STD_VER >= 23
  template <class _Ap = _Alloc>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI static constexpr allocation_result<pointer, size_type>
  allocate_at_least(_Ap& __alloc, size_type __n) {
    if constexpr (requires { __alloc.allocate_at_least(__n); }) {
      return __alloc.allocate_at_least(__n);
    } else {
      return {__alloc.allocate(__n), __n};
    }
  }
#endif

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 static void
  deallocate(allocator_type& __a, pointer __p, size_type __n) _NOEXCEPT {
    __a.deallocate(__p, __n);
  }

  template <class _Tp, class... _Args, __enable_if_t<__has_construct<allocator_type, _Tp*, _Args...>::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 static void
  construct(allocator_type& __a, _Tp* __p, _Args&&... __args) {
    _LIBCPP_SUPPRESS_DEPRECATED_PUSH
    __a.construct(__p, std::forward<_Args>(__args)...);
    _LIBCPP_SUPPRESS_DEPRECATED_POP
  }
  template <class _Tp,
            class... _Args,
            class                                                                       = void,
            __enable_if_t<!__has_construct<allocator_type, _Tp*, _Args...>::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 static void
  construct(allocator_type&, _Tp* __p, _Args&&... __args) {
    std::__construct_at(__p, std::forward<_Args>(__args)...);
  }

  template <class _Tp, __enable_if_t<__has_destroy<allocator_type, _Tp*>::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 static void destroy(allocator_type& __a, _Tp* __p) {
    _LIBCPP_SUPPRESS_DEPRECATED_PUSH
    __a.destroy(__p);
    _LIBCPP_SUPPRESS_DEPRECATED_POP
  }
  template <class _Tp, class = void, __enable_if_t<!__has_destroy<allocator_type, _Tp*>::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 static void destroy(allocator_type&, _Tp* __p) {
    std::__destroy_at(__p);
  }

  template <class _Ap = _Alloc, __enable_if_t<__has_max_size<const _Ap>::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 static size_type max_size(const allocator_type& __a) _NOEXCEPT {
    _LIBCPP_SUPPRESS_DEPRECATED_PUSH
    return __a.max_size();
    _LIBCPP_SUPPRESS_DEPRECATED_POP
  }
  template <class _Ap = _Alloc, class = void, __enable_if_t<!__has_max_size<const _Ap>::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 static size_type max_size(const allocator_type&) _NOEXCEPT {
    return numeric_limits<size_type>::max() / sizeof(value_type);
  }

  template <class _Ap = _Alloc, __enable_if_t<__has_select_on_container_copy_construction<const _Ap>::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 static allocator_type
  select_on_container_copy_construction(const allocator_type& __a) {
    return __a.select_on_container_copy_construction();
  }
  template <class _Ap                                                                          = _Alloc,
            class                                                                              = void,
            __enable_if_t<!__has_select_on_container_copy_construction<const _Ap>::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 static allocator_type
  select_on_container_copy_construction(const allocator_type& __a) {
    return __a;
  }
};

#ifndef _LIBCPP_CXX03_LANG
template <class _Traits, class _Tp>
using __rebind_alloc _LIBCPP_NODEBUG = typename _Traits::template rebind_alloc<_Tp>;
#else
template <class _Traits, class _Tp>
using __rebind_alloc = typename _Traits::template rebind_alloc<_Tp>::other;
#endif

template <class _Alloc>
struct __check_valid_allocator : true_type {
  using _Traits = std::allocator_traits<_Alloc>;
  static_assert(is_same<_Alloc, __rebind_alloc<_Traits, typename _Traits::value_type> >::value,
                "[allocator.requirements] states that rebinding an allocator to the same type should result in the "
                "original allocator");
};

// __is_default_allocator
template <class _Tp>
struct __is_default_allocator : false_type {};

template <class>
class allocator;

template <class _Tp>
struct __is_default_allocator<allocator<_Tp> > : true_type {};

// __is_cpp17_move_insertable
template <class _Alloc, class = void>
struct __is_cpp17_move_insertable : is_move_constructible<typename _Alloc::value_type> {};

template <class _Alloc>
struct __is_cpp17_move_insertable<
    _Alloc,
    __enable_if_t< !__is_default_allocator<_Alloc>::value &&
                   __has_construct<_Alloc, typename _Alloc::value_type*, typename _Alloc::value_type&&>::value > >
    : true_type {};

// __is_cpp17_copy_insertable
template <class _Alloc, class = void>
struct __is_cpp17_copy_insertable
    : integral_constant<bool,
                        is_copy_constructible<typename _Alloc::value_type>::value &&
                            __is_cpp17_move_insertable<_Alloc>::value > {};

template <class _Alloc>
struct __is_cpp17_copy_insertable<
    _Alloc,
    __enable_if_t< !__is_default_allocator<_Alloc>::value &&
                   __has_construct<_Alloc, typename _Alloc::value_type*, const typename _Alloc::value_type&>::value > >
    : __is_cpp17_move_insertable<_Alloc> {};

#undef _LIBCPP_ALLOCATOR_TRAITS_HAS_XXX

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___MEMORY_ALLOCATOR_TRAITS_H
