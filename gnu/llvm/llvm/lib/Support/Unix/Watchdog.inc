//===--- Unix/Watchdog.inc - Unix Watchdog Implementation -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides the generic Unix implementation of the Watchdog class.
//
//===----------------------------------------------------------------------===//

#include "llvm/Config/config.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

namespace llvm {
namespace sys {
Watchdog::Watchdog(unsigned int seconds) {
#ifdef HAVE_UNISTD_H
  alarm(seconds);
#endif
}

Watchdog::~Watchdog() {
#ifdef HAVE_UNISTD_H
  alarm(0);
#endif
}
} // namespace sys
} // namespace llvm
