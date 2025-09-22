//===-- timing.cpp ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "timing.h"

namespace scudo {

Timer::~Timer() {
  if (Manager)
    Manager->report(*this);
}

ScopedTimer::ScopedTimer(TimingManager &Manager, const char *Name)
    : Timer(Manager.getOrCreateTimer(Name)) {
  start();
}

ScopedTimer::ScopedTimer(TimingManager &Manager, const Timer &Nest,
                         const char *Name)
    : Timer(Manager.nest(Nest, Name)) {
  start();
}

} // namespace scudo
