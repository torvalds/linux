// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
#if 0
#  if _LIBCPP_STD_VER >= 23
  // 2.2.3, class template rcu_obj_base using std::rcu_obj_base;
  // 2.2.4, class rcu_domain
  using std::rcu_domain;
  using std::rcu_default_domain();
  using std::rcu_barrier;
  using std::rcu_retire;
  using std::rcu_synchronize;
#  endif // _LIBCPP_STD_VER >= 23
#endif
} // namespace std
