//===-- SBBreakpoint.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBBreakpoint.h"
#include "lldb/API/SBBreakpointLocation.h"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBEvent.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBStringList.h"
#include "lldb/API/SBStructuredData.h"
#include "lldb/API/SBThread.h"
#include "lldb/Utility/Instrumentation.h"

#include "lldb/Breakpoint/Breakpoint.h"
#include "lldb/Breakpoint/BreakpointIDList.h"
#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Breakpoint/BreakpointResolver.h"
#include "lldb/Breakpoint/BreakpointResolverScripted.h"
#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Core/Address.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/StructuredDataImpl.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/ScriptInterpreter.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadSpec.h"
#include "lldb/Utility/Stream.h"

#include "SBBreakpointOptionCommon.h"

#include "lldb/lldb-enumerations.h"

#include "llvm/ADT/STLExtras.h"

using namespace lldb;
using namespace lldb_private;

SBBreakpoint::SBBreakpoint() { LLDB_INSTRUMENT_VA(this); }

SBBreakpoint::SBBreakpoint(const SBBreakpoint &rhs)
    : m_opaque_wp(rhs.m_opaque_wp) {
  LLDB_INSTRUMENT_VA(this, rhs);
}

SBBreakpoint::SBBreakpoint(const lldb::BreakpointSP &bp_sp)
    : m_opaque_wp(bp_sp) {
  LLDB_INSTRUMENT_VA(this, bp_sp);
}

SBBreakpoint::~SBBreakpoint() = default;

const SBBreakpoint &SBBreakpoint::operator=(const SBBreakpoint &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  m_opaque_wp = rhs.m_opaque_wp;
  return *this;
}

bool SBBreakpoint::operator==(const lldb::SBBreakpoint &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  return m_opaque_wp.lock() == rhs.m_opaque_wp.lock();
}

bool SBBreakpoint::operator!=(const lldb::SBBreakpoint &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  return m_opaque_wp.lock() != rhs.m_opaque_wp.lock();
}

SBTarget SBBreakpoint::GetTarget() const {
  LLDB_INSTRUMENT_VA(this);

  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp)
    return SBTarget(bkpt_sp->GetTargetSP());

  return SBTarget();
}

break_id_t SBBreakpoint::GetID() const {
  LLDB_INSTRUMENT_VA(this);

  break_id_t break_id = LLDB_INVALID_BREAK_ID;
  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp)
    break_id = bkpt_sp->GetID();

  return break_id;
}

bool SBBreakpoint::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBBreakpoint::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  BreakpointSP bkpt_sp = GetSP();
  if (!bkpt_sp)
    return false;
  else if (bkpt_sp->GetTarget().GetBreakpointByID(bkpt_sp->GetID()))
    return true;
  else
    return false;
}

void SBBreakpoint::ClearAllBreakpointSites() {
  LLDB_INSTRUMENT_VA(this);

  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    bkpt_sp->ClearAllBreakpointSites();
  }
}

SBBreakpointLocation SBBreakpoint::FindLocationByAddress(addr_t vm_addr) {
  LLDB_INSTRUMENT_VA(this, vm_addr);

  SBBreakpointLocation sb_bp_location;

  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    if (vm_addr != LLDB_INVALID_ADDRESS) {
      std::lock_guard<std::recursive_mutex> guard(
          bkpt_sp->GetTarget().GetAPIMutex());
      Address address;
      Target &target = bkpt_sp->GetTarget();
      if (!target.GetSectionLoadList().ResolveLoadAddress(vm_addr, address)) {
        address.SetRawAddress(vm_addr);
      }
      sb_bp_location.SetLocation(bkpt_sp->FindLocationByAddress(address));
    }
  }
  return sb_bp_location;
}

break_id_t SBBreakpoint::FindLocationIDByAddress(addr_t vm_addr) {
  LLDB_INSTRUMENT_VA(this, vm_addr);

  break_id_t break_id = LLDB_INVALID_BREAK_ID;
  BreakpointSP bkpt_sp = GetSP();

  if (bkpt_sp && vm_addr != LLDB_INVALID_ADDRESS) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    Address address;
    Target &target = bkpt_sp->GetTarget();
    if (!target.GetSectionLoadList().ResolveLoadAddress(vm_addr, address)) {
      address.SetRawAddress(vm_addr);
    }
    break_id = bkpt_sp->FindLocationIDByAddress(address);
  }

  return break_id;
}

SBBreakpointLocation SBBreakpoint::FindLocationByID(break_id_t bp_loc_id) {
  LLDB_INSTRUMENT_VA(this, bp_loc_id);

  SBBreakpointLocation sb_bp_location;
  BreakpointSP bkpt_sp = GetSP();

  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    sb_bp_location.SetLocation(bkpt_sp->FindLocationByID(bp_loc_id));
  }

  return sb_bp_location;
}

SBBreakpointLocation SBBreakpoint::GetLocationAtIndex(uint32_t index) {
  LLDB_INSTRUMENT_VA(this, index);

  SBBreakpointLocation sb_bp_location;
  BreakpointSP bkpt_sp = GetSP();

  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    sb_bp_location.SetLocation(bkpt_sp->GetLocationAtIndex(index));
  }

  return sb_bp_location;
}

void SBBreakpoint::SetEnabled(bool enable) {
  LLDB_INSTRUMENT_VA(this, enable);

  BreakpointSP bkpt_sp = GetSP();

  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    bkpt_sp->SetEnabled(enable);
  }
}

bool SBBreakpoint::IsEnabled() {
  LLDB_INSTRUMENT_VA(this);

  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    return bkpt_sp->IsEnabled();
  } else
    return false;
}

void SBBreakpoint::SetOneShot(bool one_shot) {
  LLDB_INSTRUMENT_VA(this, one_shot);

  BreakpointSP bkpt_sp = GetSP();

  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    bkpt_sp->SetOneShot(one_shot);
  }
}

bool SBBreakpoint::IsOneShot() const {
  LLDB_INSTRUMENT_VA(this);

  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    return bkpt_sp->IsOneShot();
  } else
    return false;
}

bool SBBreakpoint::IsInternal() {
  LLDB_INSTRUMENT_VA(this);

  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    return bkpt_sp->IsInternal();
  } else
    return false;
}

void SBBreakpoint::SetIgnoreCount(uint32_t count) {
  LLDB_INSTRUMENT_VA(this, count);

  BreakpointSP bkpt_sp = GetSP();

  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    bkpt_sp->SetIgnoreCount(count);
  }
}

void SBBreakpoint::SetCondition(const char *condition) {
  LLDB_INSTRUMENT_VA(this, condition);

  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    bkpt_sp->SetCondition(condition);
  }
}

const char *SBBreakpoint::GetCondition() {
  LLDB_INSTRUMENT_VA(this);

  BreakpointSP bkpt_sp = GetSP();
  if (!bkpt_sp)
    return nullptr;

  std::lock_guard<std::recursive_mutex> guard(
      bkpt_sp->GetTarget().GetAPIMutex());
  return ConstString(bkpt_sp->GetConditionText()).GetCString();
}

void SBBreakpoint::SetAutoContinue(bool auto_continue) {
  LLDB_INSTRUMENT_VA(this, auto_continue);

  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    bkpt_sp->SetAutoContinue(auto_continue);
  }
}

bool SBBreakpoint::GetAutoContinue() {
  LLDB_INSTRUMENT_VA(this);

  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    return bkpt_sp->IsAutoContinue();
  }
  return false;
}

uint32_t SBBreakpoint::GetHitCount() const {
  LLDB_INSTRUMENT_VA(this);

  uint32_t count = 0;
  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    count = bkpt_sp->GetHitCount();
  }

  return count;
}

uint32_t SBBreakpoint::GetIgnoreCount() const {
  LLDB_INSTRUMENT_VA(this);

  uint32_t count = 0;
  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    count = bkpt_sp->GetIgnoreCount();
  }

  return count;
}

void SBBreakpoint::SetThreadID(tid_t tid) {
  LLDB_INSTRUMENT_VA(this, tid);

  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    bkpt_sp->SetThreadID(tid);
  }
}

tid_t SBBreakpoint::GetThreadID() {
  LLDB_INSTRUMENT_VA(this);

  tid_t tid = LLDB_INVALID_THREAD_ID;
  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    tid = bkpt_sp->GetThreadID();
  }

  return tid;
}

void SBBreakpoint::SetThreadIndex(uint32_t index) {
  LLDB_INSTRUMENT_VA(this, index);

  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    bkpt_sp->GetOptions().GetThreadSpec()->SetIndex(index);
  }
}

uint32_t SBBreakpoint::GetThreadIndex() const {
  LLDB_INSTRUMENT_VA(this);

  uint32_t thread_idx = UINT32_MAX;
  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    const ThreadSpec *thread_spec =
        bkpt_sp->GetOptions().GetThreadSpecNoCreate();
    if (thread_spec != nullptr)
      thread_idx = thread_spec->GetIndex();
  }

  return thread_idx;
}

void SBBreakpoint::SetThreadName(const char *thread_name) {
  LLDB_INSTRUMENT_VA(this, thread_name);

  BreakpointSP bkpt_sp = GetSP();

  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    bkpt_sp->GetOptions().GetThreadSpec()->SetName(thread_name);
  }
}

const char *SBBreakpoint::GetThreadName() const {
  LLDB_INSTRUMENT_VA(this);

  BreakpointSP bkpt_sp = GetSP();
  if (!bkpt_sp)
    return nullptr;

  std::lock_guard<std::recursive_mutex> guard(
      bkpt_sp->GetTarget().GetAPIMutex());
  if (const ThreadSpec *thread_spec =
          bkpt_sp->GetOptions().GetThreadSpecNoCreate())
    return ConstString(thread_spec->GetName()).GetCString();

  return nullptr;
}

void SBBreakpoint::SetQueueName(const char *queue_name) {
  LLDB_INSTRUMENT_VA(this, queue_name);

  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    bkpt_sp->GetOptions().GetThreadSpec()->SetQueueName(queue_name);
  }
}

const char *SBBreakpoint::GetQueueName() const {
  LLDB_INSTRUMENT_VA(this);

  BreakpointSP bkpt_sp = GetSP();
  if (!bkpt_sp)
    return nullptr;

  std::lock_guard<std::recursive_mutex> guard(
      bkpt_sp->GetTarget().GetAPIMutex());
  if (const ThreadSpec *thread_spec =
          bkpt_sp->GetOptions().GetThreadSpecNoCreate())
    return ConstString(thread_spec->GetQueueName()).GetCString();

  return nullptr;
}

size_t SBBreakpoint::GetNumResolvedLocations() const {
  LLDB_INSTRUMENT_VA(this);

  size_t num_resolved = 0;
  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    num_resolved = bkpt_sp->GetNumResolvedLocations();
  }
  return num_resolved;
}

size_t SBBreakpoint::GetNumLocations() const {
  LLDB_INSTRUMENT_VA(this);

  BreakpointSP bkpt_sp = GetSP();
  size_t num_locs = 0;
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    num_locs = bkpt_sp->GetNumLocations();
  }
  return num_locs;
}

void SBBreakpoint::SetCommandLineCommands(SBStringList &commands) {
  LLDB_INSTRUMENT_VA(this, commands);

  BreakpointSP bkpt_sp = GetSP();
  if (!bkpt_sp)
    return;
  if (commands.GetSize() == 0)
    return;

  std::lock_guard<std::recursive_mutex> guard(
      bkpt_sp->GetTarget().GetAPIMutex());
  std::unique_ptr<BreakpointOptions::CommandData> cmd_data_up(
      new BreakpointOptions::CommandData(*commands, eScriptLanguageNone));

  bkpt_sp->GetOptions().SetCommandDataCallback(cmd_data_up);
}

bool SBBreakpoint::GetCommandLineCommands(SBStringList &commands) {
  LLDB_INSTRUMENT_VA(this, commands);

  BreakpointSP bkpt_sp = GetSP();
  if (!bkpt_sp)
    return false;
  StringList command_list;
  bool has_commands =
      bkpt_sp->GetOptions().GetCommandLineCallbacks(command_list);
  if (has_commands)
    commands.AppendList(command_list);
  return has_commands;
}

bool SBBreakpoint::GetDescription(SBStream &s) {
  LLDB_INSTRUMENT_VA(this, s);

  return GetDescription(s, true);
}

bool SBBreakpoint::GetDescription(SBStream &s, bool include_locations) {
  LLDB_INSTRUMENT_VA(this, s, include_locations);

  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    s.Printf("SBBreakpoint: id = %i, ", bkpt_sp->GetID());
    bkpt_sp->GetResolverDescription(s.get());
    bkpt_sp->GetFilterDescription(s.get());
    if (include_locations) {
      const size_t num_locations = bkpt_sp->GetNumLocations();
      s.Printf(", locations = %" PRIu64, (uint64_t)num_locations);
    }
    return true;
  }
  s.Printf("No value");
  return false;
}

SBError SBBreakpoint::AddLocation(SBAddress &address) {
  LLDB_INSTRUMENT_VA(this, address);

  BreakpointSP bkpt_sp = GetSP();
  SBError error;

  if (!address.IsValid()) {
    error.SetErrorString("Can't add an invalid address.");
    return error;
  }

  if (!bkpt_sp) {
    error.SetErrorString("No breakpoint to add a location to.");
    return error;
  }

  if (!llvm::isa<BreakpointResolverScripted>(bkpt_sp->GetResolver().get())) {
    error.SetErrorString("Only a scripted resolver can add locations.");
    return error;
  }

  if (bkpt_sp->GetSearchFilter()->AddressPasses(address.ref()))
    bkpt_sp->AddLocation(address.ref());
  else {
    StreamString s;
    address.get()->Dump(&s, &bkpt_sp->GetTarget(),
                        Address::DumpStyleModuleWithFileAddress);
    error.SetErrorStringWithFormat("Address: %s didn't pass the filter.",
                                   s.GetData());
  }
  return error;
}

SBStructuredData SBBreakpoint::SerializeToStructuredData() {
  LLDB_INSTRUMENT_VA(this);

  SBStructuredData data;
  BreakpointSP bkpt_sp = GetSP();

  if (!bkpt_sp)
    return data;

  StructuredData::ObjectSP bkpt_dict = bkpt_sp->SerializeToStructuredData();
  data.m_impl_up->SetObjectSP(bkpt_dict);
  return data;
}

void SBBreakpoint::SetCallback(SBBreakpointHitCallback callback, void *baton) {
  LLDB_INSTRUMENT_VA(this, callback, baton);

  BreakpointSP bkpt_sp = GetSP();

  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    BatonSP baton_sp(new SBBreakpointCallbackBaton(callback, baton));
    bkpt_sp->SetCallback(SBBreakpointCallbackBaton
      ::PrivateBreakpointHitCallback, baton_sp,
                         false);
  }
}

void SBBreakpoint::SetScriptCallbackFunction(
  const char *callback_function_name) {
  LLDB_INSTRUMENT_VA(this, callback_function_name);
  SBStructuredData empty_args;
  SetScriptCallbackFunction(callback_function_name, empty_args);
}

SBError SBBreakpoint::SetScriptCallbackFunction(
    const char *callback_function_name,
    SBStructuredData &extra_args) {
  LLDB_INSTRUMENT_VA(this, callback_function_name, extra_args);
  SBError sb_error;
  BreakpointSP bkpt_sp = GetSP();

  if (bkpt_sp) {
    Status error;
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    BreakpointOptions &bp_options = bkpt_sp->GetOptions();
    error = bkpt_sp->GetTarget()
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

SBError SBBreakpoint::SetScriptCallbackBody(const char *callback_body_text) {
  LLDB_INSTRUMENT_VA(this, callback_body_text);

  BreakpointSP bkpt_sp = GetSP();

  SBError sb_error;
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    BreakpointOptions &bp_options = bkpt_sp->GetOptions();
    Status error =
        bkpt_sp->GetTarget()
            .GetDebugger()
            .GetScriptInterpreter()
            ->SetBreakpointCommandCallback(bp_options, callback_body_text,
                                           /*is_callback=*/false);
    sb_error.SetError(error);
  } else
    sb_error.SetErrorString("invalid breakpoint");

  return sb_error;
}

bool SBBreakpoint::AddName(const char *new_name) {
  LLDB_INSTRUMENT_VA(this, new_name);

  SBError status = AddNameWithErrorHandling(new_name);
  return status.Success();
}

SBError SBBreakpoint::AddNameWithErrorHandling(const char *new_name) {
  LLDB_INSTRUMENT_VA(this, new_name);

  BreakpointSP bkpt_sp = GetSP();

  SBError status;
  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    Status error;
    bkpt_sp->GetTarget().AddNameToBreakpoint(bkpt_sp, new_name, error);
    status.SetError(error);
  } else {
    status.SetErrorString("invalid breakpoint");
  }

  return status;
}

void SBBreakpoint::RemoveName(const char *name_to_remove) {
  LLDB_INSTRUMENT_VA(this, name_to_remove);

  BreakpointSP bkpt_sp = GetSP();

  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    bkpt_sp->GetTarget().RemoveNameFromBreakpoint(bkpt_sp,
                                                  ConstString(name_to_remove));
  }
}

bool SBBreakpoint::MatchesName(const char *name) {
  LLDB_INSTRUMENT_VA(this, name);

  BreakpointSP bkpt_sp = GetSP();

  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    return bkpt_sp->MatchesName(name);
  }

  return false;
}

void SBBreakpoint::GetNames(SBStringList &names) {
  LLDB_INSTRUMENT_VA(this, names);

  BreakpointSP bkpt_sp = GetSP();

  if (bkpt_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        bkpt_sp->GetTarget().GetAPIMutex());
    std::vector<std::string> names_vec;
    bkpt_sp->GetNames(names_vec);
    for (const std::string &name : names_vec) {
      names.AppendString(name.c_str());
    }
  }
}

bool SBBreakpoint::EventIsBreakpointEvent(const lldb::SBEvent &event) {
  LLDB_INSTRUMENT_VA(event);

  return Breakpoint::BreakpointEventData::GetEventDataFromEvent(event.get()) !=
         nullptr;
}

BreakpointEventType
SBBreakpoint::GetBreakpointEventTypeFromEvent(const SBEvent &event) {
  LLDB_INSTRUMENT_VA(event);

  if (event.IsValid())
    return Breakpoint::BreakpointEventData::GetBreakpointEventTypeFromEvent(
        event.GetSP());
  return eBreakpointEventTypeInvalidType;
}

SBBreakpoint SBBreakpoint::GetBreakpointFromEvent(const lldb::SBEvent &event) {
  LLDB_INSTRUMENT_VA(event);

  if (event.IsValid())
    return SBBreakpoint(
        Breakpoint::BreakpointEventData::GetBreakpointFromEvent(event.GetSP()));
  return SBBreakpoint();
}

SBBreakpointLocation
SBBreakpoint::GetBreakpointLocationAtIndexFromEvent(const lldb::SBEvent &event,
                                                    uint32_t loc_idx) {
  LLDB_INSTRUMENT_VA(event, loc_idx);

  SBBreakpointLocation sb_breakpoint_loc;
  if (event.IsValid())
    sb_breakpoint_loc.SetLocation(
        Breakpoint::BreakpointEventData::GetBreakpointLocationAtIndexFromEvent(
            event.GetSP(), loc_idx));
  return sb_breakpoint_loc;
}

uint32_t
SBBreakpoint::GetNumBreakpointLocationsFromEvent(const lldb::SBEvent &event) {
  LLDB_INSTRUMENT_VA(event);

  uint32_t num_locations = 0;
  if (event.IsValid())
    num_locations =
        (Breakpoint::BreakpointEventData::GetNumBreakpointLocationsFromEvent(
            event.GetSP()));
  return num_locations;
}

bool SBBreakpoint::IsHardware() const {
  LLDB_INSTRUMENT_VA(this);

  BreakpointSP bkpt_sp = GetSP();
  if (bkpt_sp)
    return bkpt_sp->IsHardware();
  return false;
}

BreakpointSP SBBreakpoint::GetSP() const { return m_opaque_wp.lock(); }

// This is simple collection of breakpoint id's and their target.
class SBBreakpointListImpl {
public:
  SBBreakpointListImpl(lldb::TargetSP target_sp) {
    if (target_sp && target_sp->IsValid())
      m_target_wp = target_sp;
  }

  ~SBBreakpointListImpl() = default;

  size_t GetSize() { return m_break_ids.size(); }

  BreakpointSP GetBreakpointAtIndex(size_t idx) {
    if (idx >= m_break_ids.size())
      return BreakpointSP();
    TargetSP target_sp = m_target_wp.lock();
    if (!target_sp)
      return BreakpointSP();
    lldb::break_id_t bp_id = m_break_ids[idx];
    return target_sp->GetBreakpointList().FindBreakpointByID(bp_id);
  }

  BreakpointSP FindBreakpointByID(lldb::break_id_t desired_id) {
    TargetSP target_sp = m_target_wp.lock();
    if (!target_sp)
      return BreakpointSP();

    for (lldb::break_id_t &break_id : m_break_ids) {
      if (break_id == desired_id)
        return target_sp->GetBreakpointList().FindBreakpointByID(break_id);
    }
    return BreakpointSP();
  }

  bool Append(BreakpointSP bkpt) {
    TargetSP target_sp = m_target_wp.lock();
    if (!target_sp || !bkpt)
      return false;
    if (bkpt->GetTargetSP() != target_sp)
      return false;
    m_break_ids.push_back(bkpt->GetID());
    return true;
  }

  bool AppendIfUnique(BreakpointSP bkpt) {
    TargetSP target_sp = m_target_wp.lock();
    if (!target_sp || !bkpt)
      return false;
    if (bkpt->GetTargetSP() != target_sp)
      return false;
    lldb::break_id_t bp_id = bkpt->GetID();
    if (!llvm::is_contained(m_break_ids, bp_id))
      return false;

    m_break_ids.push_back(bkpt->GetID());
    return true;
  }

  bool AppendByID(lldb::break_id_t id) {
    TargetSP target_sp = m_target_wp.lock();
    if (!target_sp)
      return false;
    if (id == LLDB_INVALID_BREAK_ID)
      return false;
    m_break_ids.push_back(id);
    return true;
  }

  void Clear() { m_break_ids.clear(); }

  void CopyToBreakpointIDList(lldb_private::BreakpointIDList &bp_list) {
    for (lldb::break_id_t id : m_break_ids) {
      bp_list.AddBreakpointID(BreakpointID(id));
    }
  }

  TargetSP GetTarget() { return m_target_wp.lock(); }

private:
  std::vector<lldb::break_id_t> m_break_ids;
  TargetWP m_target_wp;
};

SBBreakpointList::SBBreakpointList(SBTarget &target)
    : m_opaque_sp(new SBBreakpointListImpl(target.GetSP())) {
  LLDB_INSTRUMENT_VA(this, target);
}

SBBreakpointList::~SBBreakpointList() = default;

size_t SBBreakpointList::GetSize() const {
  LLDB_INSTRUMENT_VA(this);

  if (!m_opaque_sp)
    return 0;
  else
    return m_opaque_sp->GetSize();
}

SBBreakpoint SBBreakpointList::GetBreakpointAtIndex(size_t idx) {
  LLDB_INSTRUMENT_VA(this, idx);

  if (!m_opaque_sp)
    return SBBreakpoint();

  BreakpointSP bkpt_sp = m_opaque_sp->GetBreakpointAtIndex(idx);
  return SBBreakpoint(bkpt_sp);
}

SBBreakpoint SBBreakpointList::FindBreakpointByID(lldb::break_id_t id) {
  LLDB_INSTRUMENT_VA(this, id);

  if (!m_opaque_sp)
    return SBBreakpoint();
  BreakpointSP bkpt_sp = m_opaque_sp->FindBreakpointByID(id);
  return SBBreakpoint(bkpt_sp);
}

void SBBreakpointList::Append(const SBBreakpoint &sb_bkpt) {
  LLDB_INSTRUMENT_VA(this, sb_bkpt);

  if (!sb_bkpt.IsValid())
    return;
  if (!m_opaque_sp)
    return;
  m_opaque_sp->Append(sb_bkpt.m_opaque_wp.lock());
}

void SBBreakpointList::AppendByID(lldb::break_id_t id) {
  LLDB_INSTRUMENT_VA(this, id);

  if (!m_opaque_sp)
    return;
  m_opaque_sp->AppendByID(id);
}

bool SBBreakpointList::AppendIfUnique(const SBBreakpoint &sb_bkpt) {
  LLDB_INSTRUMENT_VA(this, sb_bkpt);

  if (!sb_bkpt.IsValid())
    return false;
  if (!m_opaque_sp)
    return false;
  return m_opaque_sp->AppendIfUnique(sb_bkpt.GetSP());
}

void SBBreakpointList::Clear() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_sp)
    m_opaque_sp->Clear();
}

void SBBreakpointList::CopyToBreakpointIDList(
    lldb_private::BreakpointIDList &bp_id_list) {
  if (m_opaque_sp)
    m_opaque_sp->CopyToBreakpointIDList(bp_id_list);
}
