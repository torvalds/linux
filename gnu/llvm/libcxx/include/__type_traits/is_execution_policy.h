//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_IS_EXECUTION_POLICY_H
#define _LIBCPP___TYPE_TRAITS_IS_EXECUTION_POLICY_H

#include <__config>
#include <__type_traits/remove_cvref.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER >= 17

_LIBCPP_BEGIN_NAMESPACE_STD

template <class>
inline constexpr bool is_execution_policy_v = false;

template <class>
inline constexpr bool __is_unsequenced_execution_policy_impl = false;

template <class _Tp>
inline constexpr bool __is_unsequenced_execution_policy_v =
    __is_unsequenced_execution_policy_impl<__remove_cvref_t<_Tp>>;

template <class>
inline constexpr bool __is_parallel_execution_policy_impl = false;

template <class _Tp>
inline constexpr bool __is_parallel_execution_policy_v = __is_parallel_execution_policy_impl<__remove_cvref_t<_Tp>>;

namespace execution {
struct __disable_user_instantiations_tag {
  explicit __disable_user_instantiations_tag() = default;
};
} // namespace execution

// TODO: Remove default argument once algorithms are using the new backend dispatching
template <class _ExecutionPolicy>
_LIBCPP_HIDE_FROM_ABI auto
__remove_parallel_policy(const _ExecutionPolicy& = _ExecutionPolicy{execution::__disable_user_instantiations_tag{}});

// Removes the "parallel" part of an execution policy.
// For example, turns par_unseq into unseq, and par into seq.
template <class _ExecutionPolicy>
using __remove_parallel_policy_t = decltype(std::__remove_parallel_policy<_ExecutionPolicy>());

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 17

#endif // _LIBCPP___TYPE_TRAITS_IS_EXECUTION_POLICY_H
