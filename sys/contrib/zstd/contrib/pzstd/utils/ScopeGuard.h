/*
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */
#pragma once

#include <utility>

namespace pzstd {

/**
 * Dismissable scope guard.
 * `Function` must be callable and take no parameters.
 * Unless `dissmiss()` is called, the callable is executed upon destruction of
 * `ScopeGuard`.
 *
 * Example:
 *
 *   auto guard = makeScopeGuard([&] { cleanup(); });
 */
template <typename Function>
class ScopeGuard {
  Function function;
  bool dismissed;

 public:
  explicit ScopeGuard(Function&& function)
      : function(std::move(function)), dismissed(false) {}

  void dismiss() {
    dismissed = true;
  }

  ~ScopeGuard() noexcept {
    if (!dismissed) {
      function();
    }
  }
};

/// Creates a scope guard from `function`.
template <typename Function>
ScopeGuard<Function> makeScopeGuard(Function&& function) {
  return ScopeGuard<Function>(std::forward<Function>(function));
}
}
