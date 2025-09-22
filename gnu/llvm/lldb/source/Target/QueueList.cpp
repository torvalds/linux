//===-- QueueList.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/Queue.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/QueueList.h"

using namespace lldb;
using namespace lldb_private;

QueueList::QueueList(Process *process)
    : m_process(process), m_stop_id(0), m_queues(), m_mutex() {}

QueueList::~QueueList() { Clear(); }

uint32_t QueueList::GetSize() {
  std::lock_guard<std::mutex> guard(m_mutex);
  return m_queues.size();
}

lldb::QueueSP QueueList::GetQueueAtIndex(uint32_t idx) {
  std::lock_guard<std::mutex> guard(m_mutex);
  if (idx < m_queues.size()) {
    return m_queues[idx];
  } else {
    return QueueSP();
  }
}

void QueueList::Clear() {
  std::lock_guard<std::mutex> guard(m_mutex);
  m_queues.clear();
}

void QueueList::AddQueue(QueueSP queue_sp) {
  std::lock_guard<std::mutex> guard(m_mutex);
  if (queue_sp.get()) {
    m_queues.push_back(queue_sp);
  }
}

lldb::QueueSP QueueList::FindQueueByID(lldb::queue_id_t qid) {
  QueueSP ret;
  for (QueueSP queue_sp : Queues()) {
    if (queue_sp->GetID() == qid) {
      ret = queue_sp;
      break;
    }
  }
  return ret;
}

lldb::QueueSP QueueList::FindQueueByIndexID(uint32_t index_id) {
  QueueSP ret;
  for (QueueSP queue_sp : Queues()) {
    if (queue_sp->GetIndexID() == index_id) {
      ret = queue_sp;
      break;
    }
  }
  return ret;
}

std::mutex &QueueList::GetMutex() { return m_mutex; }
