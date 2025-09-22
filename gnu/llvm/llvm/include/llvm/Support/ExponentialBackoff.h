//===- llvm/Support/ExponentialBackoff.h ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a helper class for implementing exponential backoff.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_EXPONENTIALBACKOFF_H
#define LLVM_EXPONENTIALBACKOFF_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Error.h"
#include <chrono>
#include <random>

namespace llvm {

/// A class to help implement exponential backoff.
///
/// Example usage:
/// \code
///   ExponentialBackoff Backoff(10s);
///   do {
///     if (tryToDoSomething())
///       return ItWorked;
///   } while (Backoff.waitForNextAttempt());
///   return Timeout;
/// \endcode
class ExponentialBackoff {
public:
  using duration = std::chrono::steady_clock::duration;
  using time_point = std::chrono::steady_clock::time_point;

  /// \param Timeout the maximum wall time this should run for starting when
  ///        this object is constructed.
  /// \param MinWait the minimum amount of time `waitForNextAttempt` will sleep
  ///        for.
  /// \param MaxWait the maximum amount of time `waitForNextAttempt` will sleep
  ///        for.
  ExponentialBackoff(duration Timeout,
                     duration MinWait = std::chrono::milliseconds(10),
                     duration MaxWait = std::chrono::milliseconds(500))
      : MinWait(MinWait), MaxWait(MaxWait),
        EndTime(std::chrono::steady_clock::now() + Timeout) {}

  /// Blocks while waiting for the next attempt.
  /// \returns true if you should try again, false if the timeout has been
  /// reached.
  bool waitForNextAttempt();

private:
  duration MinWait;
  duration MaxWait;
  time_point EndTime;
  std::random_device RandDev;
  int64_t CurrentMultiplier = 1;
};

} // end namespace llvm

#endif // LLVM_EXPONENTIALBACKOFF_H
