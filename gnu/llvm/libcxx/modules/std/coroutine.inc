// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {

  // [coroutine.traits], coroutine traits
  using std::coroutine_traits;

  // [coroutine.handle], coroutine handle
  using std::coroutine_handle;

  // [coroutine.handle.compare], comparison operators
  using std::operator==;
  using std::operator<=>;

  // [coroutine.handle.hash], hash support
  using std::hash;

  // [coroutine.noop], no-op coroutines
  using std::noop_coroutine;
  using std::noop_coroutine_handle;
  using std::noop_coroutine_promise;

  // [coroutine.trivial.awaitables], trivial awaitables
  using std::suspend_always;
  using std::suspend_never;
} // namespace std
