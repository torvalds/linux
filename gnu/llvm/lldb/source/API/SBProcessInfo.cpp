//===-- SBProcessInfo.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBProcessInfo.h"
#include "Utils.h"
#include "lldb/API/SBFileSpec.h"
#include "lldb/Utility/Instrumentation.h"
#include "lldb/Utility/ProcessInfo.h"

using namespace lldb;
using namespace lldb_private;

SBProcessInfo::SBProcessInfo() { LLDB_INSTRUMENT_VA(this); }

SBProcessInfo::SBProcessInfo(const SBProcessInfo &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  m_opaque_up = clone(rhs.m_opaque_up);
}

SBProcessInfo::~SBProcessInfo() = default;

SBProcessInfo &SBProcessInfo::operator=(const SBProcessInfo &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs)
    m_opaque_up = clone(rhs.m_opaque_up);
  return *this;
}

ProcessInstanceInfo &SBProcessInfo::ref() {
  if (m_opaque_up == nullptr) {
    m_opaque_up = std::make_unique<ProcessInstanceInfo>();
  }
  return *m_opaque_up;
}

void SBProcessInfo::SetProcessInfo(const ProcessInstanceInfo &proc_info_ref) {
  ref() = proc_info_ref;
}

bool SBProcessInfo::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBProcessInfo::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up != nullptr;
}

const char *SBProcessInfo::GetName() {
  LLDB_INSTRUMENT_VA(this);

  if (!m_opaque_up)
    return nullptr;

  return ConstString(m_opaque_up->GetName()).GetCString();
}

SBFileSpec SBProcessInfo::GetExecutableFile() {
  LLDB_INSTRUMENT_VA(this);

  SBFileSpec file_spec;
  if (m_opaque_up) {
    file_spec.SetFileSpec(m_opaque_up->GetExecutableFile());
  }
  return file_spec;
}

lldb::pid_t SBProcessInfo::GetProcessID() {
  LLDB_INSTRUMENT_VA(this);

  lldb::pid_t proc_id = LLDB_INVALID_PROCESS_ID;
  if (m_opaque_up) {
    proc_id = m_opaque_up->GetProcessID();
  }
  return proc_id;
}

uint32_t SBProcessInfo::GetUserID() {
  LLDB_INSTRUMENT_VA(this);

  uint32_t user_id = UINT32_MAX;
  if (m_opaque_up) {
    user_id = m_opaque_up->GetUserID();
  }
  return user_id;
}

uint32_t SBProcessInfo::GetGroupID() {
  LLDB_INSTRUMENT_VA(this);

  uint32_t group_id = UINT32_MAX;
  if (m_opaque_up) {
    group_id = m_opaque_up->GetGroupID();
  }
  return group_id;
}

bool SBProcessInfo::UserIDIsValid() {
  LLDB_INSTRUMENT_VA(this);

  bool is_valid = false;
  if (m_opaque_up) {
    is_valid = m_opaque_up->UserIDIsValid();
  }
  return is_valid;
}

bool SBProcessInfo::GroupIDIsValid() {
  LLDB_INSTRUMENT_VA(this);

  bool is_valid = false;
  if (m_opaque_up) {
    is_valid = m_opaque_up->GroupIDIsValid();
  }
  return is_valid;
}

uint32_t SBProcessInfo::GetEffectiveUserID() {
  LLDB_INSTRUMENT_VA(this);

  uint32_t user_id = UINT32_MAX;
  if (m_opaque_up) {
    user_id = m_opaque_up->GetEffectiveUserID();
  }
  return user_id;
}

uint32_t SBProcessInfo::GetEffectiveGroupID() {
  LLDB_INSTRUMENT_VA(this);

  uint32_t group_id = UINT32_MAX;
  if (m_opaque_up) {
    group_id = m_opaque_up->GetEffectiveGroupID();
  }
  return group_id;
}

bool SBProcessInfo::EffectiveUserIDIsValid() {
  LLDB_INSTRUMENT_VA(this);

  bool is_valid = false;
  if (m_opaque_up) {
    is_valid = m_opaque_up->EffectiveUserIDIsValid();
  }
  return is_valid;
}

bool SBProcessInfo::EffectiveGroupIDIsValid() {
  LLDB_INSTRUMENT_VA(this);

  bool is_valid = false;
  if (m_opaque_up) {
    is_valid = m_opaque_up->EffectiveGroupIDIsValid();
  }
  return is_valid;
}

lldb::pid_t SBProcessInfo::GetParentProcessID() {
  LLDB_INSTRUMENT_VA(this);

  lldb::pid_t proc_id = LLDB_INVALID_PROCESS_ID;
  if (m_opaque_up) {
    proc_id = m_opaque_up->GetParentProcessID();
  }
  return proc_id;
}

const char *SBProcessInfo::GetTriple() {
  LLDB_INSTRUMENT_VA(this);

  if (!m_opaque_up)
    return nullptr;

  const auto &arch = m_opaque_up->GetArchitecture();
  if (!arch.IsValid())
    return nullptr;

  return ConstString(arch.GetTriple().getTriple().c_str()).GetCString();
}
