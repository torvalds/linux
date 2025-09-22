//===-- QueueItem.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/Queue.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/QueueItem.h"
#include "lldb/Target/SystemRuntime.h"

using namespace lldb;
using namespace lldb_private;

QueueItem::QueueItem(QueueSP queue_sp, ProcessSP process_sp,
                     lldb::addr_t item_ref, lldb_private::Address address)
    : m_queue_wp(), m_process_wp(), m_item_ref(item_ref), m_address(address),
      m_have_fetched_entire_item(false), m_kind(eQueueItemKindUnknown),
      m_item_that_enqueued_this_ref(LLDB_INVALID_ADDRESS),
      m_enqueueing_thread_id(LLDB_INVALID_THREAD_ID),
      m_enqueueing_queue_id(LLDB_INVALID_QUEUE_ID),
      m_target_queue_id(LLDB_INVALID_QUEUE_ID), m_stop_id(0), m_backtrace(),
      m_thread_label(), m_queue_label(), m_target_queue_label() {
  m_queue_wp = queue_sp;
  m_process_wp = process_sp;
}

QueueItem::~QueueItem() = default;

QueueItemKind QueueItem::GetKind() {
  FetchEntireItem();
  return m_kind;
}

void QueueItem::SetKind(QueueItemKind item_kind) { m_kind = item_kind; }

Address &QueueItem::GetAddress() { return m_address; }

void QueueItem::SetAddress(Address addr) { m_address = addr; }

ThreadSP QueueItem::GetExtendedBacktraceThread(ConstString type) {
  FetchEntireItem();
  ThreadSP return_thread;
  QueueSP queue_sp = m_queue_wp.lock();
  if (queue_sp) {
    ProcessSP process_sp = queue_sp->GetProcess();
    if (process_sp && process_sp->GetSystemRuntime()) {
      return_thread =
          process_sp->GetSystemRuntime()->GetExtendedBacktraceForQueueItem(
              this->shared_from_this(), type);
    }
  }
  return return_thread;
}

lldb::addr_t QueueItem::GetItemThatEnqueuedThis() {
  FetchEntireItem();
  return m_item_that_enqueued_this_ref;
}

lldb::tid_t QueueItem::GetEnqueueingThreadID() {
  FetchEntireItem();
  return m_enqueueing_thread_id;
}

lldb::queue_id_t QueueItem::GetEnqueueingQueueID() {
  FetchEntireItem();
  return m_enqueueing_queue_id;
}

uint32_t QueueItem::GetStopID() {
  FetchEntireItem();
  return m_stop_id;
}

std::vector<lldb::addr_t> &QueueItem::GetEnqueueingBacktrace() {
  FetchEntireItem();
  return m_backtrace;
}

std::string QueueItem::GetThreadLabel() {
  FetchEntireItem();
  return m_thread_label;
}

std::string QueueItem::GetQueueLabel() {
  FetchEntireItem();
  return m_queue_label;
}

ProcessSP QueueItem::GetProcessSP() { return m_process_wp.lock(); }

void QueueItem::FetchEntireItem() {
  if (m_have_fetched_entire_item)
    return;
  ProcessSP process_sp = m_process_wp.lock();
  if (process_sp) {
    SystemRuntime *runtime = process_sp->GetSystemRuntime();
    if (runtime) {
      runtime->CompleteQueueItem(this, m_item_ref);
      m_have_fetched_entire_item = true;
    }
  }
}
