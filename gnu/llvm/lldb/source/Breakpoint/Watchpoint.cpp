//===-- Watchpoint.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Breakpoint/Watchpoint.h"

#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Breakpoint/WatchpointResource.h"
#include "lldb/Core/Value.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectMemory.h"
#include "lldb/DataFormatters/DumpValueObjectOptions.h"
#include "lldb/Expression/UserExpression.h"
#include "lldb/Symbol/TypeSystem.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/ThreadSpec.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

Watchpoint::Watchpoint(Target &target, lldb::addr_t addr, uint32_t size,
                       const CompilerType *type, bool hardware)
    : StoppointSite(0, addr, size, hardware), m_target(target),
      m_enabled(false), m_is_hardware(hardware), m_is_watch_variable(false),
      m_is_ephemeral(false), m_disabled_count(0), m_watch_read(0),
      m_watch_write(0), m_watch_modify(0), m_ignore_count(0) {

  if (type && type->IsValid())
    m_type = *type;
  else {
    // If we don't have a known type, then we force it to unsigned int of the
    // right size.
    auto type_system_or_err =
        target.GetScratchTypeSystemForLanguage(eLanguageTypeC);
    if (auto err = type_system_or_err.takeError()) {
      LLDB_LOG_ERROR(GetLog(LLDBLog::Watchpoints), std::move(err),
                     "Failed to set type: {0}");
    } else {
      if (auto ts = *type_system_or_err) {
        if (size <= target.GetArchitecture().GetAddressByteSize()) {
          m_type =
              ts->GetBuiltinTypeForEncodingAndBitSize(eEncodingUint, 8 * size);
        } else {
          CompilerType clang_uint8_type =
              ts->GetBuiltinTypeForEncodingAndBitSize(eEncodingUint, 8);
          m_type = clang_uint8_type.GetArrayType(size);
        }
      } else
        LLDB_LOG_ERROR(GetLog(LLDBLog::Watchpoints), std::move(err),
                       "Failed to set type: Typesystem is no longer live: {0}");
    }
  }

  // Set the initial value of the watched variable:
  if (m_target.GetProcessSP()) {
    ExecutionContext exe_ctx;
    m_target.GetProcessSP()->CalculateExecutionContext(exe_ctx);
    CaptureWatchedValue(exe_ctx);
  }
}

Watchpoint::~Watchpoint() = default;

// This function is used when "baton" doesn't need to be freed
void Watchpoint::SetCallback(WatchpointHitCallback callback, void *baton,
                             bool is_synchronous) {
  // The default "Baton" class will keep a copy of "baton" and won't free or
  // delete it when it goes out of scope.
  m_options.SetCallback(callback, std::make_shared<UntypedBaton>(baton),
                        is_synchronous);

  SendWatchpointChangedEvent(eWatchpointEventTypeCommandChanged);
}

// This function is used when a baton needs to be freed and therefore is
// contained in a "Baton" subclass.
void Watchpoint::SetCallback(WatchpointHitCallback callback,
                             const BatonSP &callback_baton_sp,
                             bool is_synchronous) {
  m_options.SetCallback(callback, callback_baton_sp, is_synchronous);
  SendWatchpointChangedEvent(eWatchpointEventTypeCommandChanged);
}

bool Watchpoint::SetupVariableWatchpointDisabler(StackFrameSP frame_sp) const {
  if (!frame_sp)
    return false;

  ThreadSP thread_sp = frame_sp->GetThread();
  if (!thread_sp)
    return false;

  uint32_t return_frame_index =
      thread_sp->GetSelectedFrameIndex(DoNoSelectMostRelevantFrame) + 1;
  if (return_frame_index >= LLDB_INVALID_FRAME_ID)
    return false;

  StackFrameSP return_frame_sp(
      thread_sp->GetStackFrameAtIndex(return_frame_index));
  if (!return_frame_sp)
    return false;

  ExecutionContext exe_ctx(return_frame_sp);
  TargetSP target_sp = exe_ctx.GetTargetSP();
  if (!target_sp)
    return false;

  Address return_address(return_frame_sp->GetFrameCodeAddress());
  lldb::addr_t return_addr = return_address.GetLoadAddress(target_sp.get());
  if (return_addr == LLDB_INVALID_ADDRESS)
    return false;

  BreakpointSP bp_sp = target_sp->CreateBreakpoint(
      return_addr, /*internal=*/true, /*request_hardware=*/false);
  if (!bp_sp || !bp_sp->HasResolvedLocations())
    return false;

  auto wvc_up = std::make_unique<WatchpointVariableContext>(GetID(), exe_ctx);
  auto baton_sp = std::make_shared<WatchpointVariableBaton>(std::move(wvc_up));
  bp_sp->SetCallback(VariableWatchpointDisabler, baton_sp);
  bp_sp->SetOneShot(true);
  bp_sp->SetBreakpointKind("variable watchpoint disabler");
  return true;
}

bool Watchpoint::VariableWatchpointDisabler(void *baton,
                                            StoppointCallbackContext *context,
                                            user_id_t break_id,
                                            user_id_t break_loc_id) {
  assert(baton && "null baton");
  if (!baton || !context)
    return false;

  Log *log = GetLog(LLDBLog::Watchpoints);

  WatchpointVariableContext *wvc =
      static_cast<WatchpointVariableContext *>(baton);

  LLDB_LOGF(log, "called by breakpoint %" PRIu64 ".%" PRIu64, break_id,
            break_loc_id);

  if (wvc->watch_id == LLDB_INVALID_WATCH_ID)
    return false;

  TargetSP target_sp = context->exe_ctx_ref.GetTargetSP();
  if (!target_sp)
    return false;

  ProcessSP process_sp = target_sp->GetProcessSP();
  if (!process_sp)
    return false;

  WatchpointSP watch_sp =
      target_sp->GetWatchpointList().FindByID(wvc->watch_id);
  if (!watch_sp)
    return false;

  if (wvc->exe_ctx == context->exe_ctx_ref) {
    LLDB_LOGF(log,
              "callback for watchpoint %" PRId32
              " matched internal breakpoint execution context",
              watch_sp->GetID());
    process_sp->DisableWatchpoint(watch_sp);
    return false;
  }
  LLDB_LOGF(log,
            "callback for watchpoint %" PRId32
            " didn't match internal breakpoint execution context",
            watch_sp->GetID());
  return false;
}

void Watchpoint::ClearCallback() {
  m_options.ClearCallback();
  SendWatchpointChangedEvent(eWatchpointEventTypeCommandChanged);
}

void Watchpoint::SetDeclInfo(const std::string &str) { m_decl_str = str; }

std::string Watchpoint::GetWatchSpec() { return m_watch_spec_str; }

void Watchpoint::SetWatchSpec(const std::string &str) {
  m_watch_spec_str = str;
}

bool Watchpoint::IsHardware() const {
  lldbassert(m_is_hardware || !HardwareRequired());
  return m_is_hardware;
}

bool Watchpoint::IsWatchVariable() const { return m_is_watch_variable; }

void Watchpoint::SetWatchVariable(bool val) { m_is_watch_variable = val; }

bool Watchpoint::CaptureWatchedValue(const ExecutionContext &exe_ctx) {
  ConstString g_watch_name("$__lldb__watch_value");
  m_old_value_sp = m_new_value_sp;
  Address watch_address(GetLoadAddress());
  if (!m_type.IsValid()) {
    // Don't know how to report new & old values, since we couldn't make a
    // scalar type for this watchpoint. This works around an assert in
    // ValueObjectMemory::Create.
    // FIXME: This should not happen, but if it does in some case we care about,
    // we can go grab the value raw and print it as unsigned.
    return false;
  }
  m_new_value_sp = ValueObjectMemory::Create(
      exe_ctx.GetBestExecutionContextScope(), g_watch_name.GetStringRef(),
      watch_address, m_type);
  m_new_value_sp = m_new_value_sp->CreateConstantValue(g_watch_name);
  return (m_new_value_sp && m_new_value_sp->GetError().Success());
}

bool Watchpoint::WatchedValueReportable(const ExecutionContext &exe_ctx) {
  if (!m_watch_modify || m_watch_read)
    return true;
  if (!m_type.IsValid())
    return true;

  ConstString g_watch_name("$__lldb__watch_value");
  Address watch_address(GetLoadAddress());
  ValueObjectSP newest_valueobj_sp = ValueObjectMemory::Create(
      exe_ctx.GetBestExecutionContextScope(), g_watch_name.GetStringRef(),
      watch_address, m_type);
  newest_valueobj_sp = newest_valueobj_sp->CreateConstantValue(g_watch_name);
  Status error;

  DataExtractor new_data;
  DataExtractor old_data;

  newest_valueobj_sp->GetData(new_data, error);
  if (error.Fail())
    return true;
  m_new_value_sp->GetData(old_data, error);
  if (error.Fail())
    return true;

  if (new_data.GetByteSize() != old_data.GetByteSize() ||
      new_data.GetByteSize() == 0)
    return true;

  if (memcmp(new_data.GetDataStart(), old_data.GetDataStart(),
             old_data.GetByteSize()) == 0)
    return false; // Value has not changed, user requested modify watchpoint

  return true;
}

// RETURNS - true if we should stop at this breakpoint, false if we
// should continue.

bool Watchpoint::ShouldStop(StoppointCallbackContext *context) {
  m_hit_counter.Increment();

  return IsEnabled();
}

void Watchpoint::GetDescription(Stream *s, lldb::DescriptionLevel level) {
  DumpWithLevel(s, level);
}

void Watchpoint::Dump(Stream *s) const {
  DumpWithLevel(s, lldb::eDescriptionLevelBrief);
}

// If prefix is nullptr, we display the watch id and ignore the prefix
// altogether.
bool Watchpoint::DumpSnapshots(Stream *s, const char *prefix) const {
  bool printed_anything = false;

  // For read watchpoints, don't display any before/after value changes.
  if (m_watch_read && !m_watch_modify && !m_watch_write)
    return printed_anything;

  s->Printf("\n");
  s->Printf("Watchpoint %u hit:\n", GetID());

  StreamString values_ss;
  if (prefix)
    values_ss.Indent(prefix);

  if (m_old_value_sp) {
    if (auto *old_value_cstr = m_old_value_sp->GetValueAsCString()) {
      values_ss.Printf("old value: %s", old_value_cstr);
    } else {
      if (auto *old_summary_cstr = m_old_value_sp->GetSummaryAsCString())
        values_ss.Printf("old value: %s", old_summary_cstr);
      else {
        StreamString strm;
        DumpValueObjectOptions options;
        options.SetUseDynamicType(eNoDynamicValues)
            .SetHideRootType(true)
            .SetHideRootName(true)
            .SetHideName(true);
        if (llvm::Error error = m_old_value_sp->Dump(strm, options))
          strm << "error: " << toString(std::move(error));

        if (strm.GetData())
          values_ss.Printf("old value: %s", strm.GetData());
      }
    }
  }

  if (m_new_value_sp) {
    if (values_ss.GetSize())
      values_ss.Printf("\n");

    if (auto *new_value_cstr = m_new_value_sp->GetValueAsCString())
      values_ss.Printf("new value: %s", new_value_cstr);
    else {
      if (auto *new_summary_cstr = m_new_value_sp->GetSummaryAsCString())
        values_ss.Printf("new value: %s", new_summary_cstr);
      else {
        StreamString strm;
        DumpValueObjectOptions options;
        options.SetUseDynamicType(eNoDynamicValues)
            .SetHideRootType(true)
            .SetHideRootName(true)
            .SetHideName(true);
        if (llvm::Error error = m_new_value_sp->Dump(strm, options))
          strm << "error: " << toString(std::move(error));

        if (strm.GetData())
          values_ss.Printf("new value: %s", strm.GetData());
      }
    }
  }

  if (values_ss.GetSize()) {
    s->Printf("%s", values_ss.GetData());
    printed_anything = true;
  }

  return printed_anything;
}

void Watchpoint::DumpWithLevel(Stream *s,
                               lldb::DescriptionLevel description_level) const {
  if (s == nullptr)
    return;

  assert(description_level >= lldb::eDescriptionLevelBrief &&
         description_level <= lldb::eDescriptionLevelVerbose);

  s->Printf("Watchpoint %u: addr = 0x%8.8" PRIx64
            " size = %u state = %s type = %s%s%s",
            GetID(), GetLoadAddress(), m_byte_size,
            IsEnabled() ? "enabled" : "disabled", m_watch_read ? "r" : "",
            m_watch_write ? "w" : "", m_watch_modify ? "m" : "");

  if (description_level >= lldb::eDescriptionLevelFull) {
    if (!m_decl_str.empty())
      s->Printf("\n    declare @ '%s'", m_decl_str.c_str());
    if (!m_watch_spec_str.empty())
      s->Printf("\n    watchpoint spec = '%s'", m_watch_spec_str.c_str());
    if (IsEnabled()) {
      if (ProcessSP process_sp = m_target.GetProcessSP()) {
        auto &resourcelist = process_sp->GetWatchpointResourceList();
        size_t idx = 0;
        s->Printf("\n    watchpoint resources:");
        for (WatchpointResourceSP &wpres : resourcelist.Sites()) {
          if (wpres->ConstituentsContains(this)) {
            s->Printf("\n       #%zu: ", idx);
            wpres->Dump(s);
          }
          idx++;
        }
      }
    }

    // Dump the snapshots we have taken.
    DumpSnapshots(s, "    ");

    if (GetConditionText())
      s->Printf("\n    condition = '%s'", GetConditionText());
    m_options.GetCallbackDescription(s, description_level);
  }

  if (description_level >= lldb::eDescriptionLevelVerbose) {
    s->Printf("\n    hit_count = %-4u  ignore_count = %-4u", GetHitCount(),
              GetIgnoreCount());
  }
}

bool Watchpoint::IsEnabled() const { return m_enabled; }

// Within StopInfo.cpp, we purposely turn on the ephemeral mode right before
// temporarily disable the watchpoint in order to perform possible watchpoint
// actions without triggering further watchpoint events. After the temporary
// disabled watchpoint is enabled, we then turn off the ephemeral mode.

void Watchpoint::TurnOnEphemeralMode() { m_is_ephemeral = true; }

void Watchpoint::TurnOffEphemeralMode() {
  m_is_ephemeral = false;
  // Leaving ephemeral mode, reset the m_disabled_count!
  m_disabled_count = 0;
}

bool Watchpoint::IsDisabledDuringEphemeralMode() {
  return m_disabled_count > 1 && m_is_ephemeral;
}

void Watchpoint::SetEnabled(bool enabled, bool notify) {
  if (!enabled) {
    if (m_is_ephemeral)
      ++m_disabled_count;

    // Don't clear the snapshots for now.
    // Within StopInfo.cpp, we purposely do disable/enable watchpoint while
    // performing watchpoint actions.
  }
  bool changed = enabled != m_enabled;
  m_enabled = enabled;
  if (notify && !m_is_ephemeral && changed)
    SendWatchpointChangedEvent(enabled ? eWatchpointEventTypeEnabled
                                       : eWatchpointEventTypeDisabled);
}

void Watchpoint::SetWatchpointType(uint32_t type, bool notify) {
  int old_watch_read = m_watch_read;
  int old_watch_write = m_watch_write;
  int old_watch_modify = m_watch_modify;
  m_watch_read = (type & LLDB_WATCH_TYPE_READ) != 0;
  m_watch_write = (type & LLDB_WATCH_TYPE_WRITE) != 0;
  m_watch_modify = (type & LLDB_WATCH_TYPE_MODIFY) != 0;
  if (notify &&
      (old_watch_read != m_watch_read || old_watch_write != m_watch_write ||
       old_watch_modify != m_watch_modify))
    SendWatchpointChangedEvent(eWatchpointEventTypeTypeChanged);
}

bool Watchpoint::WatchpointRead() const { return m_watch_read != 0; }

bool Watchpoint::WatchpointWrite() const { return m_watch_write != 0; }

bool Watchpoint::WatchpointModify() const { return m_watch_modify != 0; }

uint32_t Watchpoint::GetIgnoreCount() const { return m_ignore_count; }

void Watchpoint::SetIgnoreCount(uint32_t n) {
  bool changed = m_ignore_count != n;
  m_ignore_count = n;
  if (changed)
    SendWatchpointChangedEvent(eWatchpointEventTypeIgnoreChanged);
}

bool Watchpoint::InvokeCallback(StoppointCallbackContext *context) {
  return m_options.InvokeCallback(context, GetID());
}

void Watchpoint::SetCondition(const char *condition) {
  if (condition == nullptr || condition[0] == '\0') {
    if (m_condition_up)
      m_condition_up.reset();
  } else {
    // Pass nullptr for expr_prefix (no translation-unit level definitions).
    Status error;
    m_condition_up.reset(m_target.GetUserExpressionForLanguage(
        condition, {}, {}, UserExpression::eResultTypeAny,
        EvaluateExpressionOptions(), nullptr, error));
    if (error.Fail()) {
      // FIXME: Log something...
      m_condition_up.reset();
    }
  }
  SendWatchpointChangedEvent(eWatchpointEventTypeConditionChanged);
}

const char *Watchpoint::GetConditionText() const {
  if (m_condition_up)
    return m_condition_up->GetUserText();
  else
    return nullptr;
}

void Watchpoint::SendWatchpointChangedEvent(
    lldb::WatchpointEventType eventKind) {
  if (GetTarget().EventTypeHasListeners(
          Target::eBroadcastBitWatchpointChanged)) {
    auto data_sp =
        std::make_shared<WatchpointEventData>(eventKind, shared_from_this());
    GetTarget().BroadcastEvent(Target::eBroadcastBitWatchpointChanged, data_sp);
  }
}

Watchpoint::WatchpointEventData::WatchpointEventData(
    WatchpointEventType sub_type, const WatchpointSP &new_watchpoint_sp)
    : m_watchpoint_event(sub_type), m_new_watchpoint_sp(new_watchpoint_sp) {}

Watchpoint::WatchpointEventData::~WatchpointEventData() = default;

llvm::StringRef Watchpoint::WatchpointEventData::GetFlavorString() {
  return "Watchpoint::WatchpointEventData";
}

llvm::StringRef Watchpoint::WatchpointEventData::GetFlavor() const {
  return WatchpointEventData::GetFlavorString();
}

WatchpointSP &Watchpoint::WatchpointEventData::GetWatchpoint() {
  return m_new_watchpoint_sp;
}

WatchpointEventType
Watchpoint::WatchpointEventData::GetWatchpointEventType() const {
  return m_watchpoint_event;
}

void Watchpoint::WatchpointEventData::Dump(Stream *s) const {}

const Watchpoint::WatchpointEventData *
Watchpoint::WatchpointEventData::GetEventDataFromEvent(const Event *event) {
  if (event) {
    const EventData *event_data = event->GetData();
    if (event_data &&
        event_data->GetFlavor() == WatchpointEventData::GetFlavorString())
      return static_cast<const WatchpointEventData *>(event->GetData());
  }
  return nullptr;
}

WatchpointEventType
Watchpoint::WatchpointEventData::GetWatchpointEventTypeFromEvent(
    const EventSP &event_sp) {
  const WatchpointEventData *data = GetEventDataFromEvent(event_sp.get());

  if (data == nullptr)
    return eWatchpointEventTypeInvalidType;
  else
    return data->GetWatchpointEventType();
}

WatchpointSP Watchpoint::WatchpointEventData::GetWatchpointFromEvent(
    const EventSP &event_sp) {
  WatchpointSP wp_sp;

  const WatchpointEventData *data = GetEventDataFromEvent(event_sp.get());
  if (data)
    wp_sp = data->m_new_watchpoint_sp;

  return wp_sp;
}
