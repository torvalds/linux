//===-- sanitizer_block_signals.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of sanitizer_common unit tests.
//
//===----------------------------------------------------------------------===//
#include <signal.h>
#include <stdio.h>

#include "gtest/gtest.h"
#include "sanitizer_common/sanitizer_linux.h"

namespace __sanitizer {

#if SANITIZER_LINUX && !SANITIZER_ANDROID
volatile int received_sig = -1;

void signal_handler(int signum) { received_sig = signum; }

TEST(SanitizerCommon, NoBlockSignals) {
  // No signals blocked
  signal(SIGUSR1, signal_handler);
  raise(SIGUSR1);
  EXPECT_EQ(received_sig, SIGUSR1);

  received_sig = -1;
  signal(SIGPIPE, signal_handler);
  raise(SIGPIPE);
  EXPECT_EQ(received_sig, SIGPIPE);
}

TEST(SanitizerCommon, BlockSignalsPlain) {
  // ScopedBlockSignals; SIGUSR1 should be blocked but not SIGPIPE
  {
    __sanitizer_sigset_t sigset = {};
    ScopedBlockSignals block(&sigset);

    received_sig = -1;
    signal(SIGUSR1, signal_handler);
    raise(SIGUSR1);
    EXPECT_EQ(received_sig, -1);

    received_sig = -1;
    signal(SIGPIPE, signal_handler);
    raise(SIGPIPE);
    EXPECT_EQ(received_sig, SIGPIPE);
  }
  EXPECT_EQ(received_sig, SIGUSR1);
}

TEST(SanitizerCommon, BlockSignalsExceptPipe) {
  // Manually block SIGPIPE; ScopedBlockSignals should not unblock this
  sigset_t block_sigset;
  sigemptyset(&block_sigset);
  sigaddset(&block_sigset, SIGPIPE);
  sigprocmask(SIG_BLOCK, &block_sigset, NULL);
  {
    __sanitizer_sigset_t sigset = {};
    ScopedBlockSignals block(&sigset);

    received_sig = -1;
    signal(SIGPIPE, signal_handler);
    raise(SIGPIPE);
    EXPECT_EQ(received_sig, -1);
  }
  sigprocmask(SIG_UNBLOCK, &block_sigset, NULL);
  EXPECT_EQ(received_sig, SIGPIPE);
}
#endif  // SANITIZER_LINUX && !SANITIZER_ANDROID

}  // namespace __sanitizer
