//===-- condition_variable_linux.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_CONDITION_VARIABLE_LINUX_H_
#define SCUDO_CONDITION_VARIABLE_LINUX_H_

#include "platform.h"

#if SCUDO_LINUX

#include "atomic_helpers.h"
#include "condition_variable_base.h"
#include "thread_annotations.h"

namespace scudo {

class ConditionVariableLinux
    : public ConditionVariableBase<ConditionVariableLinux> {
public:
  void notifyAllImpl(HybridMutex &M) REQUIRES(M);

  void waitImpl(HybridMutex &M) REQUIRES(M);

private:
  u32 LastNotifyAll = 0;
  atomic_u32 Counter = {};
};

} // namespace scudo

#endif // SCUDO_LINUX

#endif // SCUDO_CONDITION_VARIABLE_LINUX_H_
