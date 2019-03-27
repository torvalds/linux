/*
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */
#pragma once

#include <atomic>
#include <cassert>
#include <stdexcept>
#include <string>

namespace pzstd {

// Coordinates graceful shutdown of the pzstd pipeline
class ErrorHolder {
  std::atomic<bool> error_;
  std::string message_;

 public:
  ErrorHolder() : error_(false) {}

  bool hasError() noexcept {
    return error_.load();
  }

  void setError(std::string message) noexcept {
    // Given multiple possibly concurrent calls, exactly one will ever succeed.
    bool expected = false;
    if (error_.compare_exchange_strong(expected, true)) {
      message_ = std::move(message);
    }
  }

  bool check(bool predicate, std::string message) noexcept {
    if (!predicate) {
      setError(std::move(message));
    }
    return !hasError();
  }

  std::string getError() noexcept {
    error_.store(false);
    return std::move(message_);
  }

  ~ErrorHolder() {
    assert(!hasError());
  }
};
}
