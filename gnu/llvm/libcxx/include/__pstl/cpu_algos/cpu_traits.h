//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___PSTL_CPU_ALGOS_CPU_TRAITS_H
#define _LIBCPP___PSTL_CPU_ALGOS_CPU_TRAITS_H

#include <__config>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD
namespace __pstl {

// __cpu_traits
//
// This traits class encapsulates the basis operations for a CPU-based implementation of the PSTL.
// All the operations in the PSTL can be implemented from these basis operations, so a pure CPU backend
// only needs to customize these traits in order to get an implementation of the whole PSTL.
//
// Basis operations
// ================
//
//  template <class _RandomAccessIterator, class _Functor>
//  optional<__empty> __for_each(_RandomAccessIterator __first, _RandomAccessIterator __last, _Functor __func);
//    - __func must take a subrange of [__first, __last) that should be executed in serial
//
//  template <class _Iterator, class _UnaryOp, class _Tp, class _BinaryOp, class _Reduction>
//  optional<_Tp> __transform_reduce(_Iterator __first, _Iterator __last, _UnaryOp, _Tp __init, _BinaryOp, _Reduction);
//
//  template <class _RandomAccessIterator1,
//            class _RandomAccessIterator2,
//            class _RandomAccessIterator3,
//            class _Compare,
//            class _LeafMerge>
//  optional<_RandomAccessIterator3> __merge(_RandomAccessIterator1 __first1,
//                                           _RandomAccessIterator1 __last1,
//                                           _RandomAccessIterator2 __first2,
//                                           _RandomAccessIterator2 __last2,
//                                           _RandomAccessIterator3 __outit,
//                                           _Compare __comp,
//                                           _LeafMerge __leaf_merge);
//
//  template <class _RandomAccessIterator, class _Comp, class _LeafSort>
//  optional<__empty> __stable_sort(_RandomAccessIterator __first,
//                                  _RandomAccessIterator __last,
//                                  _Comp __comp,
//                                  _LeafSort __leaf_sort);
//
//   void __cancel_execution();
//      Cancel the execution of other jobs - they aren't needed anymore. This is not a binding request,
//      some backends may not actually be able to cancel jobs.
//
//   constexpr size_t __lane_size;
//      Size of SIMD lanes.
//      TODO: Merge this with __native_vector_size from __algorithm/simd_utils.h
//
//
// Exception handling
// ==================
//
// CPU backends are expected to report errors (i.e. failure to allocate) by returning a disengaged `optional` from their
// implementation. Exceptions shouldn't be used to report an internal failure-to-allocate, since all exceptions are
// turned into a program termination at the front-end level. When a backend returns a disengaged `optional` to the
// frontend, the frontend will turn that into a call to `std::__throw_bad_alloc();` to report the internal failure to
// the user.

template <class _Backend>
struct __cpu_traits;

} // namespace __pstl
_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___PSTL_CPU_ALGOS_CPU_TRAITS_H
