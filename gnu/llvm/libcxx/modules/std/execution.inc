// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifdef _LIBCPP_ENABLE_EXPERIMENTAL
export namespace std {
  // [execpol.type], execution policy type trait
  using std::is_execution_policy;
  using std::is_execution_policy_v;
} // namespace std

export namespace std::execution {
  // [execpol.seq], sequenced execution policy
  using std::execution::sequenced_policy;

  // [execpol.par], parallel execution policy
  using std::execution::parallel_policy;

  // [execpol.parunseq], parallel and unsequenced execution policy
  using std::execution::parallel_unsequenced_policy;

  // [execpol.unseq], unsequenced execution policy
  using std::execution::unsequenced_policy;

  // [execpol.objects], execution policy objects
  using std::execution::par;
  using std::execution::par_unseq;
  using std::execution::seq;
  using std::execution::unseq;
} // namespace std::execution
#endif // _LIBCPP_ENABLE_EXPERIMENTAL
