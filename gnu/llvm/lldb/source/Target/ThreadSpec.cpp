//===-- ThreadSpec.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/ThreadSpec.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/StructuredData.h"

using namespace lldb;
using namespace lldb_private;

const char *ThreadSpec::g_option_names[static_cast<uint32_t>(
    ThreadSpec::OptionNames::LastOptionName)]{"Index", "ID", "Name",
                                              "QueueName"};

ThreadSpec::ThreadSpec() : m_name(), m_queue_name() {}

std::unique_ptr<ThreadSpec> ThreadSpec::CreateFromStructuredData(
    const StructuredData::Dictionary &spec_dict, Status &error) {
  uint32_t index = UINT32_MAX;
  lldb::tid_t tid = LLDB_INVALID_THREAD_ID;
  llvm::StringRef name;
  llvm::StringRef queue_name;

  std::unique_ptr<ThreadSpec> thread_spec_up(new ThreadSpec());
  bool success = spec_dict.GetValueForKeyAsInteger(
      GetKey(OptionNames::ThreadIndex), index);
  if (success)
    thread_spec_up->SetIndex(index);

  success =
      spec_dict.GetValueForKeyAsInteger(GetKey(OptionNames::ThreadID), tid);
  if (success)
    thread_spec_up->SetTID(tid);

  success =
      spec_dict.GetValueForKeyAsString(GetKey(OptionNames::ThreadName), name);
  if (success)
    thread_spec_up->SetName(name);

  success = spec_dict.GetValueForKeyAsString(GetKey(OptionNames::ThreadName),
                                             queue_name);
  if (success)
    thread_spec_up->SetQueueName(queue_name);

  return thread_spec_up;
}

StructuredData::ObjectSP ThreadSpec::SerializeToStructuredData() {
  StructuredData::DictionarySP data_dict_sp(new StructuredData::Dictionary());

  if (m_index != UINT32_MAX)
    data_dict_sp->AddIntegerItem(GetKey(OptionNames::ThreadIndex), m_index);
  if (m_tid != LLDB_INVALID_THREAD_ID)
    data_dict_sp->AddIntegerItem(GetKey(OptionNames::ThreadID), m_tid);
  if (!m_name.empty())
    data_dict_sp->AddStringItem(GetKey(OptionNames::ThreadName), m_name);
  if (!m_queue_name.empty())
    data_dict_sp->AddStringItem(GetKey(OptionNames::QueueName), m_queue_name);

  return data_dict_sp;
}

const char *ThreadSpec::GetName() const {
  return m_name.empty() ? nullptr : m_name.c_str();
}

const char *ThreadSpec::GetQueueName() const {
  return m_queue_name.empty() ? nullptr : m_queue_name.c_str();
}

bool ThreadSpec::TIDMatches(Thread &thread) const {
  if (m_tid == LLDB_INVALID_THREAD_ID)
    return true;

  lldb::tid_t thread_id = thread.GetID();
  return TIDMatches(thread_id);
}

bool ThreadSpec::IndexMatches(Thread &thread) const {
  if (m_index == UINT32_MAX)
    return true;
  uint32_t index = thread.GetIndexID();
  return IndexMatches(index);
}

bool ThreadSpec::NameMatches(Thread &thread) const {
  if (m_name.empty())
    return true;

  const char *name = thread.GetName();
  return NameMatches(name);
}

bool ThreadSpec::QueueNameMatches(Thread &thread) const {
  if (m_queue_name.empty())
    return true;

  const char *queue_name = thread.GetQueueName();
  return QueueNameMatches(queue_name);
}

bool ThreadSpec::ThreadPassesBasicTests(Thread &thread) const {
  if (!HasSpecification())
    return true;

  if (!TIDMatches(thread))
    return false;

  if (!IndexMatches(thread))
    return false;

  if (!NameMatches(thread))
    return false;

  if (!QueueNameMatches(thread))
    return false;

  return true;
}

bool ThreadSpec::HasSpecification() const {
  return (m_index != UINT32_MAX || m_tid != LLDB_INVALID_THREAD_ID ||
          !m_name.empty() || !m_queue_name.empty());
}

void ThreadSpec::GetDescription(Stream *s, lldb::DescriptionLevel level) const {
  if (!HasSpecification()) {
    if (level == eDescriptionLevelBrief) {
      s->PutCString("thread spec: no ");
    }
  } else {
    if (level == eDescriptionLevelBrief) {
      s->PutCString("thread spec: yes ");
    } else {
      if (GetTID() != LLDB_INVALID_THREAD_ID)
        s->Printf("tid: 0x%" PRIx64 " ", GetTID());

      if (GetIndex() != UINT32_MAX)
        s->Printf("index: %d ", GetIndex());

      const char *name = GetName();
      if (name)
        s->Printf("thread name: \"%s\" ", name);

      const char *queue_name = GetQueueName();
      if (queue_name)
        s->Printf("queue name: \"%s\" ", queue_name);
    }
  }
}
