//===-- PThreadEvent.h ------------------------------------------*- C++ -*-===//
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

#ifndef LLDB_TOOLS_DEBUGSERVER_SOURCE_PTHREADEVENT_H
#define LLDB_TOOLS_DEBUGSERVER_SOURCE_PTHREADEVENT_H
#include "PThreadCondition.h"
#include "PThreadMutex.h"
#include <cstdint>
#include <ctime>

class PThreadEvent {
public:
  PThreadEvent(uint32_t bits = 0, uint32_t validBits = 0);
  ~PThreadEvent();

  uint32_t NewEventBit();
  void FreeEventBits(const uint32_t mask);

  void ReplaceEventBits(const uint32_t bits);
  uint32_t GetEventBits() const;
  void SetEvents(const uint32_t mask);
  void ResetEvents(const uint32_t mask);
  // Wait for events to be set or reset. These functions take an optional
  // timeout value. If timeout is NULL an infinite timeout will be used.
  uint32_t
  WaitForSetEvents(const uint32_t mask,
                   const struct timespec *timeout_abstime = NULL) const;
  uint32_t
  WaitForEventsToReset(const uint32_t mask,
                       const struct timespec *timeout_abstime = NULL) const;

  uint32_t GetResetAckMask() const { return m_reset_ack_mask; }
  uint32_t SetResetAckMask(uint32_t mask) { return m_reset_ack_mask = mask; }
  uint32_t WaitForResetAck(const uint32_t mask,
                           const struct timespec *timeout_abstime = NULL) const;

protected:
  // pthread condition and mutex variable to control access and allow
  // blocking between the main thread and the spotlight index thread.
  mutable PThreadMutex m_mutex;
  mutable PThreadCondition m_set_condition;
  mutable PThreadCondition m_reset_condition;
  uint32_t m_bits;
  uint32_t m_validBits;
  uint32_t m_reset_ack_mask;

private:
  PThreadEvent(const PThreadEvent &) = delete;
  PThreadEvent &operator=(const PThreadEvent &rhs) = delete;
};

#endif // LLDB_TOOLS_DEBUGSERVER_SOURCE_PTHREADEVENT_H
