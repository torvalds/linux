//===-- xray_recursion_guard.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a dynamic runtime instrumentation system.
//
//===----------------------------------------------------------------------===//
#ifndef XRAY_XRAY_RECURSION_GUARD_H
#define XRAY_XRAY_RECURSION_GUARD_H

#include "sanitizer_common/sanitizer_atomic.h"

namespace __xray {

/// The RecursionGuard is useful for guarding against signal handlers which are
/// also potentially calling XRay-instrumented functions. To use the
/// RecursionGuard, you'll typically need a thread_local atomic_uint8_t:
///
///   thread_local atomic_uint8_t Guard{0};
///
///   // In a handler function:
///   void handleArg0(int32_t F, XRayEntryType T) {
///     RecursionGuard G(Guard);
///     if (!G)
///       return;  // Failed to acquire the guard.
///     ...
///   }
///
class RecursionGuard {
  atomic_uint8_t &Running;
  const bool Valid;

public:
  explicit inline RecursionGuard(atomic_uint8_t &R)
      : Running(R), Valid(!atomic_exchange(&R, 1, memory_order_acq_rel)) {}

  inline RecursionGuard(const RecursionGuard &) = delete;
  inline RecursionGuard(RecursionGuard &&) = delete;
  inline RecursionGuard &operator=(const RecursionGuard &) = delete;
  inline RecursionGuard &operator=(RecursionGuard &&) = delete;

  explicit inline operator bool() const { return Valid; }

  inline ~RecursionGuard() noexcept {
    if (Valid)
      atomic_store(&Running, 0, memory_order_release);
  }
};

} // namespace __xray

#endif // XRAY_XRAY_RECURSION_GUARD_H
