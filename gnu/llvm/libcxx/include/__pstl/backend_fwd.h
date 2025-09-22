//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___PSTL_BACKEND_FWD_H
#define _LIBCPP___PSTL_BACKEND_FWD_H

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

//
// This header declares available PSTL backends and the functions that must be implemented in order for the
// PSTL algorithms to be provided.
//
// Backends often do not implement the full set of functions themselves -- a configuration of the PSTL is
// usually a set of backends "stacked" together which each implement some algorithms under some execution
// policies. It is only necessary for the "stack" of backends to implement all algorithms under all execution
// policies, but a single backend is not required to implement everything on its own.
//
// The signatures used by each backend function are documented below.
//
// Exception handling
// ==================
//
// PSTL backends are expected to report errors (i.e. failure to allocate) by returning a disengaged `optional` from
// their implementation. Exceptions shouldn't be used to report an internal failure-to-allocate, since all exceptions
// are turned into a program termination at the front-end level. When a backend returns a disengaged `optional` to the
// frontend, the frontend will turn that into a call to `std::__throw_bad_alloc();` to report the internal failure to
// the user.
//

_LIBCPP_BEGIN_NAMESPACE_STD
namespace __pstl {

template <class... _Backends>
struct __backend_configuration;

struct __default_backend_tag;
struct __libdispatch_backend_tag;
struct __serial_backend_tag;
struct __std_thread_backend_tag;

#if defined(_LIBCPP_PSTL_BACKEND_SERIAL)
using __current_configuration = __backend_configuration<__serial_backend_tag, __default_backend_tag>;
#elif defined(_LIBCPP_PSTL_BACKEND_STD_THREAD)
using __current_configuration = __backend_configuration<__std_thread_backend_tag, __default_backend_tag>;
#elif defined(_LIBCPP_PSTL_BACKEND_LIBDISPATCH)
using __current_configuration = __backend_configuration<__libdispatch_backend_tag, __default_backend_tag>;
#else

// ...New vendors can add parallel backends here...

#  error "Invalid PSTL backend configuration"
#endif

template <class _Backend, class _ExecutionPolicy>
struct __find_if;
// template <class _Policy, class _ForwardIterator, class _Predicate>
// optional<_ForwardIterator>
// operator()(_Policy&&, _ForwardIterator __first, _ForwardIterator __last, _Predicate __pred) const noexcept;

template <class _Backend, class _ExecutionPolicy>
struct __find_if_not;
// template <class _Policy, class _ForwardIterator, class _Predicate>
// optional<_ForwardIterator>
// operator()(_Policy&&, _ForwardIterator __first, _ForwardIterator __last, _Predicate __pred) const noexcept;

template <class _Backend, class _ExecutionPolicy>
struct __find;
// template <class _Policy, class _ForwardIterator, class _Tp>
// optional<_ForwardIterator>
// operator()(_Policy&&, _ForwardIterator __first, _ForwardIterator __last, const _Tp& __value) const noexcept;

template <class _Backend, class _ExecutionPolicy>
struct __any_of;
// template <class _Policy, class _ForwardIterator, class _Predicate>
// optional<bool>
// operator()(_Policy&&, _ForwardIterator __first, _ForwardIterator __last, _Predicate __pred) const noexcept;

template <class _Backend, class _ExecutionPolicy>
struct __all_of;
// template <class _Policy, class _ForwardIterator, class _Predicate>
// optional<bool>
// operator()(_Policy&&, _ForwardIterator __first, _ForwardIterator __last, _Predicate __pred) const noexcept;

template <class _Backend, class _ExecutionPolicy>
struct __none_of;
// template <class _Policy, class _ForwardIterator, class _Predicate>
// optional<bool>
// operator()(_Policy&&, _ForwardIterator __first, _ForwardIterator __last, _Predicate __pred) const noexcept;

template <class _Backend, class _ExecutionPolicy>
struct __is_partitioned;
// template <class _Policy, class _ForwardIterator, class _Predicate>
// optional<bool>
// operator()(_Policy&&, _ForwardIterator __first, _ForwardIterator __last, _Predicate __pred) const noexcept;

template <class _Backend, class _ExecutionPolicy>
struct __for_each;
// template <class _Policy, class _ForwardIterator, class _Function>
// optional<__empty>
// operator()(_Policy&&, _ForwardIterator __first, _ForwardIterator __last, _Function __func) const noexcept;

template <class _Backend, class _ExecutionPolicy>
struct __for_each_n;
// template <class _Policy, class _ForwardIterator, class _Size, class _Function>
// optional<__empty>
// operator()(_Policy&&, _ForwardIterator __first, _Size __size, _Function __func) const noexcept;

template <class _Backend, class _ExecutionPolicy>
struct __fill;
// template <class _Policy, class _ForwardIterator, class _Tp>
// optional<__empty>
// operator()(_Policy&&, _ForwardIterator __first, _ForwardIterator __last, _Tp const& __value) const noexcept;

template <class _Backend, class _ExecutionPolicy>
struct __fill_n;
// template <class _Policy, class _ForwardIterator, class _Size, class _Tp>
// optional<__empty>
// operator()(_Policy&&, _ForwardIterator __first, _Size __n, _Tp const& __value) const noexcept;

template <class _Backend, class _ExecutionPolicy>
struct __replace;
// template <class _Policy, class _ForwardIterator, class _Tp>
// optional<__empty>
// operator()(_Policy&&, _ForwardIterator __first, _ForwardIterator __last,
//                       _Tp const& __old, _Tp const& __new) const noexcept;

template <class _Backend, class _ExecutionPolicy>
struct __replace_if;
// template <class _Policy, class _ForwardIterator, class _Predicate, class _Tp>
// optional<__empty>
// operator()(_Policy&&, _ForwardIterator __first, _ForwardIterator __last,
//                       _Predicate __pred, _Tp const& __new_value) const noexcept;

template <class _Backend, class _ExecutionPolicy>
struct __generate;
// template <class _Policy, class _ForwardIterator, class _Generator>
// optional<__empty>
// operator()(_Policy&&, _ForwardIterator __first, _ForwardIterator __last, _Generator __gen) const noexcept;

template <class _Backend, class _ExecutionPolicy>
struct __generate_n;
// template <class _Policy, class _ForwardIterator, class _Size, class _Generator>
// optional<__empty>
// operator()(_Policy&&, _ForwardIterator __first, _Size __n, _Generator __gen) const noexcept;

template <class _Backend, class _ExecutionPolicy>
struct __merge;
// template <class _Policy, class _ForwardIterator1, class _ForwardIterator2, class _ForwardOutIterator, class _Comp>
// optional<_ForwardOutIterator>
// operator()(_Policy&&, _ForwardIterator1 __first1, _ForwardIterator1 __last1,
//                       _ForwardIterator2 __first2, _ForwardIterator2 __last2,
//                       _ForwardOutIterator __result, _Comp __comp) const noexcept;

template <class _Backend, class _ExecutionPolicy>
struct __stable_sort;
// template <class _Policy, class _RandomAccessIterator, class _Comp>
// optional<__empty>
// operator()(_Policy&&, _RandomAccessIterator __first, _RandomAccessIterator __last, _Comp __comp) const noexcept;

template <class _Backend, class _ExecutionPolicy>
struct __sort;
// template <class _Policy, class _RandomAccessIterator, class _Comp>
// optional<__empty>
// operator()(_Policy&&, _RandomAccessIterator __first, _RandomAccessIterator __last, _Comp __comp) const noexcept;

template <class _Backend, class _ExecutionPolicy>
struct __transform;
// template <class _Policy, class _ForwardIterator, class _ForwardOutIterator, class _UnaryOperation>
// optional<_ForwardOutIterator>
// operator()(_Policy&&, _ForwardIterator __first, _ForwardIterator __last,
//                       _ForwardOutIterator __result,
//                       _UnaryOperation __op) const noexcept;

template <class _Backend, class _ExecutionPolicy>
struct __transform_binary;
// template <class _Policy, class _ForwardIterator1, class _ForwardIterator2,
//                          class _ForwardOutIterator,
//                          class _BinaryOperation>
// optional<_ForwardOutIterator>
// operator()(_Policy&&, _ForwardIterator1 __first1, _ForwardIterator1 __last1,
//                       _ForwardIterator2 __first2,
//                       _ForwardOutIterator __result,
//                       _BinaryOperation __op) const noexcept;

template <class _Backend, class _ExecutionPolicy>
struct __replace_copy_if;
// template <class _Policy, class _ForwardIterator, class _ForwardOutIterator, class _Predicate, class _Tp>
// optional<__empty>
// operator()(_Policy&&, _ForwardIterator __first, _ForwardIterator __last,
//                       _ForwardOutIterator __out_it,
//                       _Predicate __pred,
//                       _Tp const& __new_value) const noexcept;

template <class _Backend, class _ExecutionPolicy>
struct __replace_copy;
// template <class _Policy, class _ForwardIterator, class _ForwardOutIterator, class _Tp>
// optional<__empty>
// operator()(_Policy&&, _ForwardIterator __first, _ForwardIterator __last,
//                       _ForwardOutIterator __out_it,
//                       _Tp const& __old_value,
//                       _Tp const& __new_value) const noexcept;

template <class _Backend, class _ExecutionPolicy>
struct __move;
// template <class _Policy, class _ForwardIterator, class _ForwardOutIterator>
// optional<_ForwardOutIterator>
// operator()(_Policy&&, _ForwardIterator __first, _ForwardIterator __last,
//                       _ForwardOutIterator __out_it) const noexcept;

template <class _Backend, class _ExecutionPolicy>
struct __copy;
// template <class _Policy, class _ForwardIterator, class _ForwardOutIterator>
// optional<_ForwardOutIterator>
// operator()(_Policy&&, _ForwardIterator __first, _ForwardIterator __last,
//                       _ForwardOutIterator __out_it) const noexcept;

template <class _Backend, class _ExecutionPolicy>
struct __copy_n;
// template <class _Policy, class _ForwardIterator, class _Size, class _ForwardOutIterator>
// optional<_ForwardOutIterator>
// operator()(_Policy&&, _ForwardIterator __first, _Size __n, _ForwardOutIterator __out_it) const noexcept;

template <class _Backend, class _ExecutionPolicy>
struct __rotate_copy;
// template <class _Policy, class _ForwardIterator, class _ForwardOutIterator>
// optional<_ForwardOutIterator>
// operator()(_Policy&&, _ForwardIterator __first, _ForwardIterator __middle, _ForwardIterator __last,
//                       _ForwardOutIterator __out_it) const noexcept;

template <class _Backend, class _ExecutionPolicy>
struct __transform_reduce;
// template <class _Policy, class _ForwardIterator, class _Tp, class _BinaryOperation, class _UnaryOperation>
// optional<_Tp>
// operator()(_Policy&&, _ForwardIterator __first, _ForwardIterator __last,
//                       _Tp __init,
//                       _BinaryOperation __reduce,
//                       _UnaryOperation __transform) const noexcept;

template <class _Backend, class _ExecutionPolicy>
struct __transform_reduce_binary;
// template <class _Policy, class _ForwardIterator1, class _ForwardIterator2,
//           class _Tp, class _BinaryOperation1, class _BinaryOperation2>
// optional<_Tp> operator()(_Policy&&, _ForwardIterator1 __first1, _ForwardIterator1 __last1,
//                                     _ForwardIterator2 __first2,
//                                     _Tp __init,
//                                     _BinaryOperation1 __reduce,
//                                     _BinaryOperation2 __transform) const noexcept;

template <class _Backend, class _ExecutionPolicy>
struct __count_if;
// template <class _Policy, class _ForwardIterator, class _Predicate>
// optional<__iter_diff_t<_ForwardIterator>>
// operator()(_Policy&&, _ForwardIterator __first, _ForwardIterator __last, _Predicate __pred) const noexcept;

template <class _Backend, class _ExecutionPolicy>
struct __count;
// template <class _Policy, class _ForwardIterator, class _Tp>
// optional<__iter_diff_t<_ForwardIterator>>
// operator()(_Policy&&, _ForwardIterator __first, _ForwardIterator __last, _Tp const& __value) const noexcept;

template <class _Backend, class _ExecutionPolicy>
struct __equal_3leg;
// template <class _Policy, class _ForwardIterator1, class _ForwardIterator2, class _Predicate>
// optional<bool>
// operator()(_Policy&&, _ForwardIterator1 __first1, _ForwardIterator1 __last1,
//                       _ForwardIterator2 __first2,
//                       _Predicate __pred) const noexcept;

template <class _Backend, class _ExecutionPolicy>
struct __equal;
// template <class _Policy, class _ForwardIterator1, class _ForwardIterator2, class _Predicate>
// optional<bool>
// operator()(_Policy&&, _ForwardIterator1 __first1, _ForwardIterator1 __last1,
//                       _ForwardIterator2 __first2, _ForwardIterator2 __last2,
//                       _Predicate __pred) const noexcept;

template <class _Backend, class _ExecutionPolicy>
struct __reduce;
// template <class _Policy, class _ForwardIterator, class _Tp, class _BinaryOperation>
// optional<_Tp>
// operator()(_Policy&&, _ForwardIterator __first, _ForwardIterator __last,
//                       _Tp __init, _BinaryOperation __op) const noexcept;

} // namespace __pstl
_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___PSTL_BACKEND_FWD_H
