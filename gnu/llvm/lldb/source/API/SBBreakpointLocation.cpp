//===-- SBBreakpointLocation.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBBreakpointLocation.h"
#include "lldb/API/SBAddress.h"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBDefines.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBStringList.h"
#include "lldb/API/SBStructuredData.h"
#include "lldb/Utility/Instrumentation.h"

#include "lldb/Breakpoint/Breakpoint.h"
#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/StructuredDataImpl.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/ScriptInterpreter.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/ThreadSpec.h"
#include "lldb/Utility/Stream.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-types.h"

#include "SBBreakpointOptionCommon.h"

using namespace lldb;
using namespace lldb_private;

SBBreakpointLocation::SBBreakpointLocation() { LLDB_INSTRUMENT_VA(this); }

SBBreakpointLocation::SBBreakpointLocation(
    const lldb::BreakpointLocationSP &break_loc_sp)
    : m_opaque_wp(break_loc_sp) {
  LLDB_INSTRUMENT_VA(this, break_loc_sp);
}

SBBreakpointLocation::SBBreakpointLocation(const SBBreakpointLocation &rhs)
    : m_opaque_wp(rhs.m_opaque_wp) {
  LLDB_INSTRUMENT_VA(this, rhs);
}

const SBBreakpointLocation &SBBreakpointLocation::
operator=(const SBBreakpointLocation &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  m_opaque_wp = rhs.m_opaque_wp;
  return *this;
}

SBBreakpointLocation::~SBBreakpointLocation() = default;

BreakpointLocationSP SBBreakpointLocation::GetSP() const {
  return m_opaque_wp.lock();
}

bool SBBreakpointLocation::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBBreakpointLocation::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return bool(GetSP());
}

SBAddress SBBreakpointLocation::GetAddress() {
  LLDB_INSTRUMENT_VA(this);

  BreakpointLocationSP loc_sp = GetSP();
  if (loc_sp) {
    return SBAddress(loc_sp->GetAddress());
  }

  return SBAddress();
}

addr_t SBBreakpointLocation::GetLoadAddress() {
  LLDB_INSTRUMENT_VA(this);

  addr_t ret_addr = LLDB_INVALID_ADDRESS;
  BreakpointLocationSP loc_sp = GetSP();

  if (loc_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        loc_sp->GetTarget().GetAPIMutex());
    ret_addr = loc_sp->GetLoadAddress();
  }

  return ret_addr;
}

void SBBreakpointLocation::SetEnabled(bool enabled) {
  LLDB_INSTRUMENT_VA(this, enabled);

  BreakpointLocationSP loc_sp = GetSP();
  if (loc_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        loc_sp->GetTarget().GetAPIMutex());
    loc_sp->SetEnabled(enabled);
  }
}

bool SBBreakpointLocation::IsEnabled() {
  LLDB_INSTRUMENT_VA(this);

  BreakpointLocationSP loc_sp = GetSP();
  if (loc_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        loc_sp->GetTarget().GetAPIMutex());
    return loc_sp->IsEnabled();
  } else
    return false;
}

uint32_t SBBreakpointLocation::GetHitCount() {
  LLDB_INSTRUMENT_VA(this);

  BreakpointLocationSP loc_sp = GetSP();
  if (loc_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        loc_sp->GetTarget().GetAPIMutex());
    return loc_sp->GetHitCount();
  } else
    return 0;
}

uint32_t SBBreakpointLocation::GetIgnoreCount() {
  LLDB_INSTRUMENT_VA(this);

  BreakpointLocationSP loc_sp = GetSP();
  if (loc_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        loc_sp->GetTarget().GetAPIMutex());
    return loc_sp->GetIgnoreCount();
  } else
    return 0;
}

void SBBreakpointLocation::SetIgnoreCount(uint32_t n) {
  LLDB_INSTRUMENT_VA(this, n);

  BreakpointLocationSP loc_sp = GetSP();
  if (loc_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        loc_sp->GetTarget().GetAPIMutex());
    loc_sp->SetIgnoreCount(n);
  }
}

void SBBreakpointLocation::SetCondition(const char *condition) {
  LLDB_INSTRUMENT_VA(this, condition);

  BreakpointLocationSP loc_sp = GetSP();
  if (loc_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        loc_sp->GetTarget().GetAPIMutex());
    loc_sp->SetCondition(condition);
  }
}

const char *SBBreakpointLocation::GetCondition() {
  LLDB_INSTRUMENT_VA(this);

  BreakpointLocationSP loc_sp = GetSP();
  if (!loc_sp)
    return nullptr;

  std::lock_guard<std::recursive_mutex> guard(
      loc_sp->GetTarget().GetAPIMutex());
  return ConstString(loc_sp->GetConditionText()).GetCString();
}

void SBBreakpointLocation::SetAutoContinue(bool auto_continue) {
  LLDB_INSTRUMENT_VA(this, auto_continue);

  BreakpointLocationSP loc_sp = GetSP();
  if (loc_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        loc_sp->GetTarget().GetAPIMutex());
    loc_sp->SetAutoContinue(auto_continue);
  }
}

bool SBBreakpointLocation::GetAutoContinue() {
  LLDB_INSTRUMENT_VA(this);

  BreakpointLocationSP loc_sp = GetSP();
  if (loc_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        loc_sp->GetTarget().GetAPIMutex());
    return loc_sp->IsAutoContinue();
  }
  return false;
}

void SBBreakpointLocation::SetCallback(SBBreakpointHitCallback callback,
                                       void *baton) {
  LLDB_INSTRUMENT_VA(this, callback, baton);

  BreakpointLocationSP loc_sp = GetSP();

  if (loc_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        loc_sp->GetTarget().GetAPIMutex());
    BatonSP baton_sp(new SBBreakpointCallbackBaton(callback, baton));
    loc_sp->SetCallback(SBBreakpointCallbackBaton::PrivateBreakpointHitCallback,
                        baton_sp, false);
  }
}

void SBBreakpointLocation::SetScriptCallbackFunction(
  const char *callback_function_name) {
  LLDB_INSTRUMENT_VA(this, callback_function_name);
}

SBError SBBreakpointLocation::SetScriptCallbackFunction(
    const char *callback_function_name,
    SBStructuredData &extra_args) {
  LLDB_INSTRUMENT_VA(this, callback_function_name, extra_args);
  SBError sb_error;
  BreakpointLocationSP loc_sp = GetSP();

  if (loc_sp) {
    Status error;
    std::lock_guard<std::recursive_mutex> guard(
        loc_sp->GetTarget().GetAPIMutex());
    BreakpointOptions &bp_options = loc_sp->GetLocationOptions();
    error = loc_sp->GetBreakpoint()
        .GetTarget()
        .GetDebugger()
        .GetScriptInterpreter()
        ->SetBreakpointCommandCallbackFunction(bp_options,
                                               callback_function_name,
                                               extra_args.m_impl_up
                                                   ->GetObjectSP());
      sb_error.SetError(error);
    } else
      sb_error.SetErrorString("invalid breakpoint");

    return sb_error;
}

SBError
SBBreakpointLocation::SetScriptCallbackBody(const char *callback_body_text) {
  LLDB_INSTRUMENT_VA(this, callback_body_text);

  BreakpointLocationSP loc_sp = GetSP();

  SBError sb_error;
  if (loc_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        loc_sp->GetTarget().GetAPIMutex());
    BreakpointOptions &bp_options = loc_sp->GetLocationOptions();
    Status error =
        loc_sp->GetBreakpoint()
            .GetTarget()
            .GetDebugger()
            .GetScriptInterpreter()
            ->SetBreakpointCommandCallback(bp_options, callback_body_text,
                                           /*is_callback=*/false);
    sb_error.SetError(error);
  } else
    sb_error.SetErrorString("invalid breakpoint");

  return sb_error;
}

void SBBreakpointLocation::SetCommandLineCommands(SBStringList &commands) {
  LLDB_INSTRUMENT_VA(this, commands);

  BreakpointLocationSP loc_sp = GetSP();
  if (!loc_sp)
    return;
  if (commands.GetSize() == 0)
    return;

  std::lock_guard<std::recursive_mutex> guard(
      loc_sp->GetTarget().GetAPIMutex());
  std::unique_ptr<BreakpointOptions::CommandData> cmd_data_up(
      new BreakpointOptions::CommandData(*commands, eScriptLanguageNone));

  loc_sp->GetLocationOptions().SetCommandDataCallback(cmd_data_up);
}

bool SBBreakpointLocation::GetCommandLineCommands(SBStringList &commands) {
  LLDB_INSTRUMENT_VA(this, commands);

  BreakpointLocationSP loc_sp = GetSP();
  if (!loc_sp)
    return false;
  StringList command_list;
  bool has_commands =
      loc_sp->GetLocationOptions().GetCommandLineCallbacks(command_list);
  if (has_commands)
    commands.AppendList(command_list);
  return has_commands;
}

void SBBreakpointLocation::SetThreadID(tid_t thread_id) {
  LLDB_INSTRUMENT_VA(this, thread_id);

  BreakpointLocationSP loc_sp = GetSP();
  if (loc_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        loc_sp->GetTarget().GetAPIMutex());
    loc_sp->SetThreadID(thread_id);
  }
}

tid_t SBBreakpointLocation::GetThreadID() {
  LLDB_INSTRUMENT_VA(this);

  tid_t tid = LLDB_INVALID_THREAD_ID;
  BreakpointLocationSP loc_sp = GetSP();
  if (loc_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        loc_sp->GetTarget().GetAPIMutex());
    return loc_sp->GetThreadID();
  }
  return tid;
}

void SBBreakpointLocation::SetThreadIndex(uint32_t index) {
  LLDB_INSTRUMENT_VA(this, index);

  BreakpointLocationSP loc_sp = GetSP();
  if (loc_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        loc_sp->GetTarget().GetAPIMutex());
    loc_sp->SetThreadIndex(index);
  }
}

uint32_t SBBreakpointLocation::GetThreadIndex() const {
  LLDB_INSTRUMENT_VA(this);

  uint32_t thread_idx = UINT32_MAX;
  BreakpointLocationSP loc_sp = GetSP();
  if (loc_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        loc_sp->GetTarget().GetAPIMutex());
    return loc_sp->GetThreadIndex();
  }
  return thread_idx;
}

void SBBreakpointLocation::SetThreadName(const char *thread_name) {
  LLDB_INSTRUMENT_VA(this, thread_name);

  BreakpointLocationSP loc_sp = GetSP();
  if (loc_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        loc_sp->GetTarget().GetAPIMutex());
    loc_sp->SetThreadName(thread_name);
  }
}

const char *SBBreakpointLocation::GetThreadName() const {
  LLDB_INSTRUMENT_VA(this);

  BreakpointLocationSP loc_sp = GetSP();
  if (!loc_sp)
    return nullptr;

  std::lock_guard<std::recursive_mutex> guard(
      loc_sp->GetTarget().GetAPIMutex());
  return ConstString(loc_sp->GetThreadName()).GetCString();
}

void SBBreakpointLocation::SetQueueName(const char *queue_name) {
  LLDB_INSTRUMENT_VA(this, queue_name);

  BreakpointLocationSP loc_sp = GetSP();
  if (loc_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        loc_sp->GetTarget().GetAPIMutex());
    loc_sp->SetQueueName(queue_name);
  }
}

const char *SBBreakpointLocation::GetQueueName() const {
  LLDB_INSTRUMENT_VA(this);

  BreakpointLocationSP loc_sp = GetSP();
  if (!loc_sp)
    return nullptr;

  std::lock_guard<std::recursive_mutex> guard(
      loc_sp->GetTarget().GetAPIMutex());
  return ConstString(loc_sp->GetQueueName()).GetCString();
}

bool SBBreakpointLocation::IsResolved() {
  LLDB_INSTRUMENT_VA(this);

  BreakpointLocationSP loc_sp = GetSP();
  if (loc_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        loc_sp->GetTarget().GetAPIMutex());
    return loc_sp->IsResolved();
  }
  return false;
}

void SBBreakpointLocation::SetLocation(
    const lldb::BreakpointLocationSP &break_loc_sp) {
  // Uninstall the callbacks?
  m_opaque_wp = break_loc_sp;
}

bool SBBreakpointLocation::GetDescription(SBStream &description,
                                          DescriptionLevel level) {
  LLDB_INSTRUMENT_VA(this, description, level);

  Stream &strm = description.ref();
  BreakpointLocationSP loc_sp = GetSP();

  if (loc_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        loc_sp->GetTarget().GetAPIMutex());
    loc_sp->GetDescription(&strm, level);
    strm.EOL();
  } else
    strm.PutCString("No value");

  return true;
}

break_id_t SBBreakpointLocation::GetID() {
  LLDB_INSTRUMENT_VA(this);

  BreakpointLocationSP loc_sp = GetSP();
  if (loc_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        loc_sp->GetTarget().GetAPIMutex());
    return loc_sp->GetID();
  } else
    return LLDB_INVALID_BREAK_ID;
}

SBBreakpoint SBBreakpointLocation::GetBreakpoint() {
  LLDB_INSTRUMENT_VA(this);

  BreakpointLocationSP loc_sp = GetSP();

  SBBreakpoint sb_bp;
  if (loc_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        loc_sp->GetTarget().GetAPIMutex());
    sb_bp = loc_sp->GetBreakpoint().shared_from_this();
  }

  return sb_bp;
}
