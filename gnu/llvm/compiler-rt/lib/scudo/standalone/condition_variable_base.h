//===-- condition_variable_base.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_CONDITION_VARIABLE_BASE_H_
#define SCUDO_CONDITION_VARIABLE_BASE_H_

#include "mutex.h"
#include "thread_annotations.h"

namespace scudo {

template <typename Derived> class ConditionVariableBase {
public:
  constexpr ConditionVariableBase() = default;

  void bindTestOnly(HybridMutex &Mutex) {
#if SCUDO_DEBUG
    boundMutex = &Mutex;
#else
    (void)Mutex;
#endif
  }

  void notifyAll(HybridMutex &M) REQUIRES(M) {
#if SCUDO_DEBUG
    CHECK_EQ(&M, boundMutex);
#endif
    getDerived()->notifyAllImpl(M);
  }

  void wait(HybridMutex &M) REQUIRES(M) {
#if SCUDO_DEBUG
    CHECK_EQ(&M, boundMutex);
#endif
    getDerived()->waitImpl(M);
  }

protected:
  Derived *getDerived() { return static_cast<Derived *>(this); }

#if SCUDO_DEBUG
  // Because thread-safety analysis doesn't support pointer aliasing, we are not
  // able to mark the proper annotations without false positive. Instead, we
  // pass the lock and do the same-lock check separately.
  HybridMutex *boundMutex = nullptr;
#endif
};

} // namespace scudo

#endif // SCUDO_CONDITION_VARIABLE_BASE_H_
