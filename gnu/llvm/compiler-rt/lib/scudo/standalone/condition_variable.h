//===-- condition_variable.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_CONDITION_VARIABLE_H_
#define SCUDO_CONDITION_VARIABLE_H_

#include "condition_variable_base.h"

#include "common.h"
#include "platform.h"

#include "condition_variable_linux.h"

namespace scudo {

// A default implementation of default condition variable. It doesn't do a real
// `wait`, instead it spins a short amount of time only.
class ConditionVariableDummy
    : public ConditionVariableBase<ConditionVariableDummy> {
public:
  void notifyAllImpl(UNUSED HybridMutex &M) REQUIRES(M) {}

  void waitImpl(UNUSED HybridMutex &M) REQUIRES(M) {
    M.unlock();

    constexpr u32 SpinTimes = 64;
    volatile u32 V = 0;
    for (u32 I = 0; I < SpinTimes; ++I) {
      u32 Tmp = V + 1;
      V = Tmp;
    }

    M.lock();
  }
};

} // namespace scudo

#endif // SCUDO_CONDITION_VARIABLE_H_
