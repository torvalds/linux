//===--- Watchdog.h - Watchdog timer ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file declares the llvm::sys::Watchdog class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_WATCHDOG_H
#define LLVM_SUPPORT_WATCHDOG_H

#include "llvm/Support/Compiler.h"

namespace llvm {
  namespace sys {

    /// This class provides an abstraction for a timeout around an operation
    /// that must complete in a given amount of time. Failure to complete before
    /// the timeout is an unrecoverable situation and no mechanisms to attempt
    /// to handle it are provided.
    class Watchdog {
    public:
      Watchdog(unsigned int seconds);
      ~Watchdog();
    private:
      // Noncopyable.
      Watchdog(const Watchdog &other) = delete;
      Watchdog &operator=(const Watchdog &other) = delete;
    };
  }
}

#endif
