//===-- SBBreakpointName.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBBreakpointName.h"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBError.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBStringList.h"
#include "lldb/API/SBStructuredData.h"
#include "lldb/API/SBTarget.h"
#include "lldb/Utility/Instrumentation.h"

#include "lldb/Breakpoint/BreakpointName.h"
#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/StructuredDataImpl.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/ScriptInterpreter.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/ThreadSpec.h"
#include "lldb/Utility/Stream.h"

#include "SBBreakpointOptionCommon.h"

using namespace lldb;
using namespace lldb_private;

namespace lldb
{
class SBBreakpointNameImpl {
public:
  SBBreakpointNameImpl(TargetSP target_sp, const char *name) {
    if (!name || name[0] == '\0')
      return;
    m_name.assign(name);

    if (!target_sp)
      return;

    m_target_wp = target_sp;
  }

  SBBreakpointNameImpl(SBTarget &sb_target, const char *name);
  bool operator==(const SBBreakpointNameImpl &rhs);
  bool operator!=(const SBBreakpointNameImpl &rhs);

  // For now we take a simple approach and only keep the name, and relook up
  // the location when we need it.

  TargetSP GetTarget() const {
    return m_target_wp.lock();
  }

  const char *GetName() const {
    return m_name.c_str();
  }

  bool IsValid() const {
    return !m_name.empty() && m_target_wp.lock();
  }

  lldb_private::BreakpointName *GetBreakpointName() const;

private:
  TargetWP m_target_wp;
  std::string m_name;
};

SBBreakpointNameImpl::SBBreakpointNameImpl(SBTarget &sb_target,
                                           const char *name) {
  if (!name || name[0] == '\0')
    return;
  m_name.assign(name);

  if (!sb_target.IsValid())
    return;

  TargetSP target_sp = sb_target.GetSP();
  if (!target_sp)
    return;

  m_target_wp = target_sp;
}

bool SBBreakpointNameImpl::operator==(const SBBreakpointNameImpl &rhs) {
  return m_name == rhs.m_name && m_target_wp.lock() == rhs.m_target_wp.lock();
}

bool SBBreakpointNameImpl::operator!=(const SBBreakpointNameImpl &rhs) {
  return m_name != rhs.m_name || m_target_wp.lock() != rhs.m_target_wp.lock();
}

lldb_private::BreakpointName *SBBreakpointNameImpl::GetBreakpointName() const {
  if (!IsValid())
    return nullptr;
  TargetSP target_sp = GetTarget();
  if (!target_sp)
    return nullptr;
  Status error;
  return target_sp->FindBreakpointName(ConstString(m_name), true, error);
}

} // namespace lldb

SBBreakpointName::SBBreakpointName() { LLDB_INSTRUMENT_VA(this); }

SBBreakpointName::SBBreakpointName(SBTarget &sb_target, const char *name) {
  LLDB_INSTRUMENT_VA(this, sb_target, name);

  m_impl_up = std::make_unique<SBBreakpointNameImpl>(sb_target, name);
  // Call FindBreakpointName here to make sure the name is valid, reset if not:
  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    m_impl_up.reset();
}

SBBreakpointName::SBBreakpointName(SBBreakpoint &sb_bkpt, const char *name) {
  LLDB_INSTRUMENT_VA(this, sb_bkpt, name);

  if (!sb_bkpt.IsValid()) {
    m_impl_up.reset();
    return;
  }
  BreakpointSP bkpt_sp = sb_bkpt.GetSP();
  Target &target = bkpt_sp->GetTarget();

  m_impl_up =
      std::make_unique<SBBreakpointNameImpl>(target.shared_from_this(), name);

  // Call FindBreakpointName here to make sure the name is valid, reset if not:
  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name) {
    m_impl_up.reset();
    return;
  }

  // Now copy over the breakpoint's options:
  target.ConfigureBreakpointName(*bp_name, bkpt_sp->GetOptions(),
                                 BreakpointName::Permissions());
}

SBBreakpointName::SBBreakpointName(const SBBreakpointName &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (!rhs.m_impl_up)
    return;
  else
    m_impl_up = std::make_unique<SBBreakpointNameImpl>(
        rhs.m_impl_up->GetTarget(), rhs.m_impl_up->GetName());
}

SBBreakpointName::~SBBreakpointName() = default;

const SBBreakpointName &SBBreakpointName::
operator=(const SBBreakpointName &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (!rhs.m_impl_up) {
    m_impl_up.reset();
    return *this;
  }

  m_impl_up = std::make_unique<SBBreakpointNameImpl>(rhs.m_impl_up->GetTarget(),
                                                     rhs.m_impl_up->GetName());
  return *this;
}

bool SBBreakpointName::operator==(const lldb::SBBreakpointName &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  return *m_impl_up == *rhs.m_impl_up;
}

bool SBBreakpointName::operator!=(const lldb::SBBreakpointName &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  return *m_impl_up != *rhs.m_impl_up;
}

bool SBBreakpointName::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBBreakpointName::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  if (!m_impl_up)
    return false;
  return m_impl_up->IsValid();
}

const char *SBBreakpointName::GetName() const {
  LLDB_INSTRUMENT_VA(this);

  if (!m_impl_up)
    return "<Invalid Breakpoint Name Object>";
  return ConstString(m_impl_up->GetName()).GetCString();
}

void SBBreakpointName::SetEnabled(bool enable) {
  LLDB_INSTRUMENT_VA(this, enable);

  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return;

  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  bp_name->GetOptions().SetEnabled(enable);
}

void SBBreakpointName::UpdateName(BreakpointName &bp_name) {
  if (!IsValid())
    return;

  TargetSP target_sp = m_impl_up->GetTarget();
  if (!target_sp)
    return;
  target_sp->ApplyNameToBreakpoints(bp_name);

}

bool SBBreakpointName::IsEnabled() {
  LLDB_INSTRUMENT_VA(this);

  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return false;

  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  return bp_name->GetOptions().IsEnabled();
}

void SBBreakpointName::SetOneShot(bool one_shot) {
  LLDB_INSTRUMENT_VA(this, one_shot);

  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return;

  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  bp_name->GetOptions().SetOneShot(one_shot);
  UpdateName(*bp_name);
}

bool SBBreakpointName::IsOneShot() const {
  LLDB_INSTRUMENT_VA(this);

  const BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return false;

  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  return bp_name->GetOptions().IsOneShot();
}

void SBBreakpointName::SetIgnoreCount(uint32_t count) {
  LLDB_INSTRUMENT_VA(this, count);

  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return;

  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  bp_name->GetOptions().SetIgnoreCount(count);
  UpdateName(*bp_name);
}

uint32_t SBBreakpointName::GetIgnoreCount() const {
  LLDB_INSTRUMENT_VA(this);

  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return false;

  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  return bp_name->GetOptions().GetIgnoreCount();
}

void SBBreakpointName::SetCondition(const char *condition) {
  LLDB_INSTRUMENT_VA(this, condition);

  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return;

  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  bp_name->GetOptions().SetCondition(condition);
  UpdateName(*bp_name);
}

const char *SBBreakpointName::GetCondition() {
  LLDB_INSTRUMENT_VA(this);

  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return nullptr;

  std::lock_guard<std::recursive_mutex> guard(
      m_impl_up->GetTarget()->GetAPIMutex());

  return ConstString(bp_name->GetOptions().GetConditionText()).GetCString();
}

void SBBreakpointName::SetAutoContinue(bool auto_continue) {
  LLDB_INSTRUMENT_VA(this, auto_continue);

  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return;

  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  bp_name->GetOptions().SetAutoContinue(auto_continue);
  UpdateName(*bp_name);
}

bool SBBreakpointName::GetAutoContinue() {
  LLDB_INSTRUMENT_VA(this);

  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return false;

  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  return bp_name->GetOptions().IsAutoContinue();
}

void SBBreakpointName::SetThreadID(tid_t tid) {
  LLDB_INSTRUMENT_VA(this, tid);

  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return;

  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  bp_name->GetOptions().SetThreadID(tid);
  UpdateName(*bp_name);
}

tid_t SBBreakpointName::GetThreadID() {
  LLDB_INSTRUMENT_VA(this);

  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return LLDB_INVALID_THREAD_ID;

  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  return bp_name->GetOptions().GetThreadSpec()->GetTID();
}

void SBBreakpointName::SetThreadIndex(uint32_t index) {
  LLDB_INSTRUMENT_VA(this, index);

  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return;

  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  bp_name->GetOptions().GetThreadSpec()->SetIndex(index);
  UpdateName(*bp_name);
}

uint32_t SBBreakpointName::GetThreadIndex() const {
  LLDB_INSTRUMENT_VA(this);

  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return LLDB_INVALID_THREAD_ID;

  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  return bp_name->GetOptions().GetThreadSpec()->GetIndex();
}

void SBBreakpointName::SetThreadName(const char *thread_name) {
  LLDB_INSTRUMENT_VA(this, thread_name);

  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return;

  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  bp_name->GetOptions().GetThreadSpec()->SetName(thread_name);
  UpdateName(*bp_name);
}

const char *SBBreakpointName::GetThreadName() const {
  LLDB_INSTRUMENT_VA(this);

  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return nullptr;

  std::lock_guard<std::recursive_mutex> guard(
      m_impl_up->GetTarget()->GetAPIMutex());

  return ConstString(bp_name->GetOptions().GetThreadSpec()->GetName())
      .GetCString();
}

void SBBreakpointName::SetQueueName(const char *queue_name) {
  LLDB_INSTRUMENT_VA(this, queue_name);

  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return;

  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  bp_name->GetOptions().GetThreadSpec()->SetQueueName(queue_name);
  UpdateName(*bp_name);
}

const char *SBBreakpointName::GetQueueName() const {
  LLDB_INSTRUMENT_VA(this);

  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return nullptr;

  std::lock_guard<std::recursive_mutex> guard(
      m_impl_up->GetTarget()->GetAPIMutex());

  return ConstString(bp_name->GetOptions().GetThreadSpec()->GetQueueName())
      .GetCString();
}

void SBBreakpointName::SetCommandLineCommands(SBStringList &commands) {
  LLDB_INSTRUMENT_VA(this, commands);

  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return;
  if (commands.GetSize() == 0)
    return;


  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());
  std::unique_ptr<BreakpointOptions::CommandData> cmd_data_up(
      new BreakpointOptions::CommandData(*commands, eScriptLanguageNone));

  bp_name->GetOptions().SetCommandDataCallback(cmd_data_up);
  UpdateName(*bp_name);
}

bool SBBreakpointName::GetCommandLineCommands(SBStringList &commands) {
  LLDB_INSTRUMENT_VA(this, commands);

  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return false;

  StringList command_list;
  bool has_commands =
      bp_name->GetOptions().GetCommandLineCallbacks(command_list);
  if (has_commands)
    commands.AppendList(command_list);
  return has_commands;
}

const char *SBBreakpointName::GetHelpString() const {
  LLDB_INSTRUMENT_VA(this);

  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return "";

  return ConstString(bp_name->GetHelp()).GetCString();
}

void SBBreakpointName::SetHelpString(const char *help_string) {
  LLDB_INSTRUMENT_VA(this, help_string);

  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return;


  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());
  bp_name->SetHelp(help_string);
}

bool SBBreakpointName::GetDescription(SBStream &s) {
  LLDB_INSTRUMENT_VA(this, s);

  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
  {
    s.Printf("No value");
    return false;
  }

  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());
  bp_name->GetDescription(s.get(), eDescriptionLevelFull);
  return true;
}

void SBBreakpointName::SetCallback(SBBreakpointHitCallback callback,
                                   void *baton) {
  LLDB_INSTRUMENT_VA(this, callback, baton);

  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return;
  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  BatonSP baton_sp(new SBBreakpointCallbackBaton(callback, baton));
  bp_name->GetOptions().SetCallback(SBBreakpointCallbackBaton
                                       ::PrivateBreakpointHitCallback,
                                    baton_sp,
                                    false);
  UpdateName(*bp_name);
}

void SBBreakpointName::SetScriptCallbackFunction(
  const char *callback_function_name) {
  LLDB_INSTRUMENT_VA(this, callback_function_name);
  SBStructuredData empty_args;
  SetScriptCallbackFunction(callback_function_name, empty_args);
}

SBError SBBreakpointName::SetScriptCallbackFunction(
    const char *callback_function_name, 
    SBStructuredData &extra_args) {
  LLDB_INSTRUMENT_VA(this, callback_function_name, extra_args);
  SBError sb_error;
  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name) {
    sb_error.SetErrorString("unrecognized breakpoint name");
    return sb_error;
  }

  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  BreakpointOptions &bp_options = bp_name->GetOptions();
  Status error;
  error = m_impl_up->GetTarget()
              ->GetDebugger()
              .GetScriptInterpreter()
              ->SetBreakpointCommandCallbackFunction(
                  bp_options, callback_function_name,
                  extra_args.m_impl_up->GetObjectSP());
  sb_error.SetError(error);
  UpdateName(*bp_name);
  return sb_error;
}

SBError
SBBreakpointName::SetScriptCallbackBody(const char *callback_body_text) {
  LLDB_INSTRUMENT_VA(this, callback_body_text);

  SBError sb_error;
  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return sb_error;

  std::lock_guard<std::recursive_mutex> guard(
        m_impl_up->GetTarget()->GetAPIMutex());

  BreakpointOptions &bp_options = bp_name->GetOptions();
  Status error = m_impl_up->GetTarget()
                     ->GetDebugger()
                     .GetScriptInterpreter()
                     ->SetBreakpointCommandCallback(
                         bp_options, callback_body_text, /*is_callback=*/false);
  sb_error.SetError(error);
  if (!sb_error.Fail())
    UpdateName(*bp_name);

  return sb_error;
}

bool SBBreakpointName::GetAllowList() const {
  LLDB_INSTRUMENT_VA(this);

  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return false;
  return bp_name->GetPermissions().GetAllowList();
}

void SBBreakpointName::SetAllowList(bool value) {
  LLDB_INSTRUMENT_VA(this, value);

  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return;
  bp_name->GetPermissions().SetAllowList(value);
}

bool SBBreakpointName::GetAllowDelete() {
  LLDB_INSTRUMENT_VA(this);

  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return false;
  return bp_name->GetPermissions().GetAllowDelete();
}

void SBBreakpointName::SetAllowDelete(bool value) {
  LLDB_INSTRUMENT_VA(this, value);

  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return;
  bp_name->GetPermissions().SetAllowDelete(value);
}

bool SBBreakpointName::GetAllowDisable() {
  LLDB_INSTRUMENT_VA(this);

  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return false;
  return bp_name->GetPermissions().GetAllowDisable();
}

void SBBreakpointName::SetAllowDisable(bool value) {
  LLDB_INSTRUMENT_VA(this, value);

  BreakpointName *bp_name = GetBreakpointName();
  if (!bp_name)
    return;
  bp_name->GetPermissions().SetAllowDisable(value);
}

lldb_private::BreakpointName *SBBreakpointName::GetBreakpointName() const
{
  if (!IsValid())
    return nullptr;
  return m_impl_up->GetBreakpointName();
}
