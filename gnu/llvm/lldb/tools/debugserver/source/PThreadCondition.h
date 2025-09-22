//===-- PThreadCondition.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 6/16/07.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_DEBUGSERVER_SOURCE_PTHREADCONDITION_H
#define LLDB_TOOLS_DEBUGSERVER_SOURCE_PTHREADCONDITION_H

#include <pthread.h>

class PThreadCondition {
public:
  PThreadCondition() { ::pthread_cond_init(&m_condition, NULL); }

  ~PThreadCondition() { ::pthread_cond_destroy(&m_condition); }

  pthread_cond_t *Condition() { return &m_condition; }

  int Broadcast() { return ::pthread_cond_broadcast(&m_condition); }

  int Signal() { return ::pthread_cond_signal(&m_condition); }

protected:
  pthread_cond_t m_condition;
};

#endif
