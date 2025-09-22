//===-- BreakpointLocation.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Breakpoint/BreakpointID.h"
#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Expression/DiagnosticManager.h"
#include "lldb/Expression/ExpressionVariable.h"
#include "lldb/Expression/UserExpression.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/TypeSystem.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadSpec.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb;
using namespace lldb_private;

BreakpointLocation::BreakpointLocation(break_id_t loc_id, Breakpoint &owner,
                                       const Address &addr, lldb::tid_t tid,
                                       bool hardware, bool check_for_resolver)
    : m_should_resolve_indirect_functions(false), m_is_reexported(false),
      m_is_indirect(false), m_address(addr), m_owner(owner),
      m_condition_hash(0), m_loc_id(loc_id), m_hit_counter() {
  if (check_for_resolver) {
    Symbol *symbol = m_address.CalculateSymbolContextSymbol();
    if (symbol && symbol->IsIndirect()) {
      SetShouldResolveIndirectFunctions(true);
    }
  }

  SetThreadIDInternal(tid);
}

BreakpointLocation::~BreakpointLocation() { ClearBreakpointSite(); }

lldb::addr_t BreakpointLocation::GetLoadAddress() const {
  return m_address.GetOpcodeLoadAddress(&m_owner.GetTarget());
}

const BreakpointOptions &BreakpointLocation::GetOptionsSpecifyingKind(
    BreakpointOptions::OptionKind kind) const {
  if (m_options_up && m_options_up->IsOptionSet(kind))
    return *m_options_up;
  else
    return m_owner.GetOptions();
}

Address &BreakpointLocation::GetAddress() { return m_address; }

Breakpoint &BreakpointLocation::GetBreakpoint() { return m_owner; }

Target &BreakpointLocation::GetTarget() { return m_owner.GetTarget(); }

bool BreakpointLocation::IsEnabled() const {
  if (!m_owner.IsEnabled())
    return false;
  else if (m_options_up != nullptr)
    return m_options_up->IsEnabled();
  else
    return true;
}

void BreakpointLocation::SetEnabled(bool enabled) {
  GetLocationOptions().SetEnabled(enabled);
  if (enabled) {
    ResolveBreakpointSite();
  } else {
    ClearBreakpointSite();
  }
  SendBreakpointLocationChangedEvent(enabled ? eBreakpointEventTypeEnabled
                                             : eBreakpointEventTypeDisabled);
}

bool BreakpointLocation::IsAutoContinue() const {
  if (m_options_up &&
      m_options_up->IsOptionSet(BreakpointOptions::eAutoContinue))
    return m_options_up->IsAutoContinue();
  else
    return m_owner.IsAutoContinue();
}

void BreakpointLocation::SetAutoContinue(bool auto_continue) {
  GetLocationOptions().SetAutoContinue(auto_continue);
  SendBreakpointLocationChangedEvent(eBreakpointEventTypeAutoContinueChanged);
}

void BreakpointLocation::SetThreadID(lldb::tid_t thread_id) {
  SetThreadIDInternal(thread_id);
  SendBreakpointLocationChangedEvent(eBreakpointEventTypeThreadChanged);
}

lldb::tid_t BreakpointLocation::GetThreadID() {
  const ThreadSpec *thread_spec =
      GetOptionsSpecifyingKind(BreakpointOptions::eThreadSpec)
          .GetThreadSpecNoCreate();
  if (thread_spec)
    return thread_spec->GetTID();
  else
    return LLDB_INVALID_THREAD_ID;
}

void BreakpointLocation::SetThreadIndex(uint32_t index) {
  if (index != 0)
    GetLocationOptions().GetThreadSpec()->SetIndex(index);
  else {
    // If we're resetting this to an invalid thread id, then don't make an
    // options pointer just to do that.
    if (m_options_up != nullptr)
      m_options_up->GetThreadSpec()->SetIndex(index);
  }
  SendBreakpointLocationChangedEvent(eBreakpointEventTypeThreadChanged);
}

uint32_t BreakpointLocation::GetThreadIndex() const {
  const ThreadSpec *thread_spec =
      GetOptionsSpecifyingKind(BreakpointOptions::eThreadSpec)
          .GetThreadSpecNoCreate();
  if (thread_spec)
    return thread_spec->GetIndex();
  else
    return 0;
}

void BreakpointLocation::SetThreadName(const char *thread_name) {
  if (thread_name != nullptr)
    GetLocationOptions().GetThreadSpec()->SetName(thread_name);
  else {
    // If we're resetting this to an invalid thread id, then don't make an
    // options pointer just to do that.
    if (m_options_up != nullptr)
      m_options_up->GetThreadSpec()->SetName(thread_name);
  }
  SendBreakpointLocationChangedEvent(eBreakpointEventTypeThreadChanged);
}

const char *BreakpointLocation::GetThreadName() const {
  const ThreadSpec *thread_spec =
      GetOptionsSpecifyingKind(BreakpointOptions::eThreadSpec)
          .GetThreadSpecNoCreate();
  if (thread_spec)
    return thread_spec->GetName();
  else
    return nullptr;
}

void BreakpointLocation::SetQueueName(const char *queue_name) {
  if (queue_name != nullptr)
    GetLocationOptions().GetThreadSpec()->SetQueueName(queue_name);
  else {
    // If we're resetting this to an invalid thread id, then don't make an
    // options pointer just to do that.
    if (m_options_up != nullptr)
      m_options_up->GetThreadSpec()->SetQueueName(queue_name);
  }
  SendBreakpointLocationChangedEvent(eBreakpointEventTypeThreadChanged);
}

const char *BreakpointLocation::GetQueueName() const {
  const ThreadSpec *thread_spec =
      GetOptionsSpecifyingKind(BreakpointOptions::eThreadSpec)
          .GetThreadSpecNoCreate();
  if (thread_spec)
    return thread_spec->GetQueueName();
  else
    return nullptr;
}

bool BreakpointLocation::InvokeCallback(StoppointCallbackContext *context) {
  if (m_options_up != nullptr && m_options_up->HasCallback())
    return m_options_up->InvokeCallback(context, m_owner.GetID(), GetID());
  else
    return m_owner.InvokeCallback(context, GetID());
}

bool BreakpointLocation::IsCallbackSynchronous() {
  if (m_options_up != nullptr && m_options_up->HasCallback())
    return m_options_up->IsCallbackSynchronous();
  else
    return m_owner.GetOptions().IsCallbackSynchronous();
}

void BreakpointLocation::SetCallback(BreakpointHitCallback callback,
                                     void *baton, bool is_synchronous) {
  // The default "Baton" class will keep a copy of "baton" and won't free or
  // delete it when it goes out of scope.
  GetLocationOptions().SetCallback(
      callback, std::make_shared<UntypedBaton>(baton), is_synchronous);
  SendBreakpointLocationChangedEvent(eBreakpointEventTypeCommandChanged);
}

void BreakpointLocation::SetCallback(BreakpointHitCallback callback,
                                     const BatonSP &baton_sp,
                                     bool is_synchronous) {
  GetLocationOptions().SetCallback(callback, baton_sp, is_synchronous);
  SendBreakpointLocationChangedEvent(eBreakpointEventTypeCommandChanged);
}

void BreakpointLocation::ClearCallback() {
  GetLocationOptions().ClearCallback();
}

void BreakpointLocation::SetCondition(const char *condition) {
  GetLocationOptions().SetCondition(condition);
  SendBreakpointLocationChangedEvent(eBreakpointEventTypeConditionChanged);
}

const char *BreakpointLocation::GetConditionText(size_t *hash) const {
  return GetOptionsSpecifyingKind(BreakpointOptions::eCondition)
      .GetConditionText(hash);
}

bool BreakpointLocation::ConditionSaysStop(ExecutionContext &exe_ctx,
                                           Status &error) {
  Log *log = GetLog(LLDBLog::Breakpoints);

  std::lock_guard<std::mutex> guard(m_condition_mutex);

  size_t condition_hash;
  const char *condition_text = GetConditionText(&condition_hash);

  if (!condition_text) {
    m_user_expression_sp.reset();
    return false;
  }

  error.Clear();

  DiagnosticManager diagnostics;

  if (condition_hash != m_condition_hash || !m_user_expression_sp ||
      !m_user_expression_sp->IsParseCacheable() ||
      !m_user_expression_sp->MatchesContext(exe_ctx)) {
    LanguageType language = eLanguageTypeUnknown;
    // See if we can figure out the language from the frame, otherwise use the
    // default language:
    CompileUnit *comp_unit = m_address.CalculateSymbolContextCompileUnit();
    if (comp_unit)
      language = comp_unit->GetLanguage();

    m_user_expression_sp.reset(GetTarget().GetUserExpressionForLanguage(
        condition_text, llvm::StringRef(), language, Expression::eResultTypeAny,
        EvaluateExpressionOptions(), nullptr, error));
    if (error.Fail()) {
      LLDB_LOGF(log, "Error getting condition expression: %s.",
                error.AsCString());
      m_user_expression_sp.reset();
      return true;
    }

    if (!m_user_expression_sp->Parse(diagnostics, exe_ctx,
                                     eExecutionPolicyOnlyWhenNeeded, true,
                                     false)) {
      error.SetErrorStringWithFormat(
          "Couldn't parse conditional expression:\n%s",
          diagnostics.GetString().c_str());
      m_user_expression_sp.reset();
      return true;
    }

    m_condition_hash = condition_hash;
  }

  // We need to make sure the user sees any parse errors in their condition, so
  // we'll hook the constructor errors up to the debugger's Async I/O.

  ValueObjectSP result_value_sp;

  EvaluateExpressionOptions options;
  options.SetUnwindOnError(true);
  options.SetIgnoreBreakpoints(true);
  options.SetTryAllThreads(true);
  options.SetSuppressPersistentResult(
      true); // Don't generate a user variable for condition expressions.

  Status expr_error;

  diagnostics.Clear();

  ExpressionVariableSP result_variable_sp;

  ExpressionResults result_code = m_user_expression_sp->Execute(
      diagnostics, exe_ctx, options, m_user_expression_sp, result_variable_sp);

  bool ret;

  if (result_code == eExpressionCompleted) {
    if (!result_variable_sp) {
      error.SetErrorString("Expression did not return a result");
      return false;
    }

    result_value_sp = result_variable_sp->GetValueObject();

    if (result_value_sp) {
      ret = result_value_sp->IsLogicalTrue(error);
      if (log) {
        if (error.Success()) {
          LLDB_LOGF(log, "Condition successfully evaluated, result is %s.\n",
                    ret ? "true" : "false");
        } else {
          error.SetErrorString(
              "Failed to get an integer result from the expression");
          ret = false;
        }
      }
    } else {
      ret = false;
      error.SetErrorString("Failed to get any result from the expression");
    }
  } else {
    ret = false;
    error.SetErrorStringWithFormat("Couldn't execute expression:\n%s",
                                   diagnostics.GetString().c_str());
  }

  return ret;
}

uint32_t BreakpointLocation::GetIgnoreCount() const {
  return GetOptionsSpecifyingKind(BreakpointOptions::eIgnoreCount)
      .GetIgnoreCount();
}

void BreakpointLocation::SetIgnoreCount(uint32_t n) {
  GetLocationOptions().SetIgnoreCount(n);
  SendBreakpointLocationChangedEvent(eBreakpointEventTypeIgnoreChanged);
}

void BreakpointLocation::DecrementIgnoreCount() {
  if (m_options_up != nullptr) {
    uint32_t loc_ignore = m_options_up->GetIgnoreCount();
    if (loc_ignore != 0)
      m_options_up->SetIgnoreCount(loc_ignore - 1);
  }
}

bool BreakpointLocation::IgnoreCountShouldStop() {
  uint32_t owner_ignore = GetBreakpoint().GetIgnoreCount();
  uint32_t loc_ignore = 0;
  if (m_options_up != nullptr)
    loc_ignore = m_options_up->GetIgnoreCount();

  if (loc_ignore != 0 || owner_ignore != 0) {
    m_owner.DecrementIgnoreCount();
    DecrementIgnoreCount(); // Have to decrement our owners' ignore count,
                            // since it won't get a chance to.
    return false;
  }
  return true;
}

BreakpointOptions &BreakpointLocation::GetLocationOptions() {
  // If we make the copy we don't copy the callbacks because that is
  // potentially expensive and we don't want to do that for the simple case
  // where someone is just disabling the location.
  if (m_options_up == nullptr)
    m_options_up = std::make_unique<BreakpointOptions>(false);

  return *m_options_up;
}

bool BreakpointLocation::ValidForThisThread(Thread &thread) {
  return thread.MatchesSpec(
      GetOptionsSpecifyingKind(BreakpointOptions::eThreadSpec)
          .GetThreadSpecNoCreate());
}

// RETURNS - true if we should stop at this breakpoint, false if we
// should continue.  Note, we don't check the thread spec for the breakpoint
// here, since if the breakpoint is not for this thread, then the event won't
// even get reported, so the check is redundant.

bool BreakpointLocation::ShouldStop(StoppointCallbackContext *context) {
  bool should_stop = true;
  Log *log = GetLog(LLDBLog::Breakpoints);

  // Do this first, if a location is disabled, it shouldn't increment its hit
  // count.
  if (!IsEnabled())
    return false;

  // We only run synchronous callbacks in ShouldStop:
  context->is_synchronous = true;
  should_stop = InvokeCallback(context);

  if (log) {
    StreamString s;
    GetDescription(&s, lldb::eDescriptionLevelVerbose);
    LLDB_LOGF(log, "Hit breakpoint location: %s, %s.\n", s.GetData(),
              should_stop ? "stopping" : "continuing");
  }

  return should_stop;
}

void BreakpointLocation::BumpHitCount() {
  if (IsEnabled()) {
    // Step our hit count, and also step the hit count of the owner.
    m_hit_counter.Increment();
    m_owner.m_hit_counter.Increment();
  }
}

void BreakpointLocation::UndoBumpHitCount() {
  if (IsEnabled()) {
    // Step our hit count, and also step the hit count of the owner.
    m_hit_counter.Decrement();
    m_owner.m_hit_counter.Decrement();
  }
}

bool BreakpointLocation::IsResolved() const {
  return m_bp_site_sp.get() != nullptr;
}

lldb::BreakpointSiteSP BreakpointLocation::GetBreakpointSite() const {
  return m_bp_site_sp;
}

bool BreakpointLocation::ResolveBreakpointSite() {
  if (m_bp_site_sp)
    return true;

  Process *process = m_owner.GetTarget().GetProcessSP().get();
  if (process == nullptr)
    return false;

  lldb::break_id_t new_id =
      process->CreateBreakpointSite(shared_from_this(), m_owner.IsHardware());

  if (new_id == LLDB_INVALID_BREAK_ID) {
    Log *log = GetLog(LLDBLog::Breakpoints);
    if (log)
      log->Warning("Failed to add breakpoint site at 0x%" PRIx64,
                   m_address.GetOpcodeLoadAddress(&m_owner.GetTarget()));
  }

  return IsResolved();
}

bool BreakpointLocation::SetBreakpointSite(BreakpointSiteSP &bp_site_sp) {
  m_bp_site_sp = bp_site_sp;
  SendBreakpointLocationChangedEvent(eBreakpointEventTypeLocationsResolved);
  return true;
}

bool BreakpointLocation::ClearBreakpointSite() {
  if (m_bp_site_sp.get()) {
    ProcessSP process_sp(m_owner.GetTarget().GetProcessSP());
    // If the process exists, get it to remove the owner, it will remove the
    // physical implementation of the breakpoint as well if there are no more
    // owners.  Otherwise just remove this owner.
    if (process_sp)
      process_sp->RemoveConstituentFromBreakpointSite(GetBreakpoint().GetID(),
                                                      GetID(), m_bp_site_sp);
    else
      m_bp_site_sp->RemoveConstituent(GetBreakpoint().GetID(), GetID());

    m_bp_site_sp.reset();
    return true;
  }
  return false;
}

void BreakpointLocation::GetDescription(Stream *s,
                                        lldb::DescriptionLevel level) {
  SymbolContext sc;

  // If the description level is "initial" then the breakpoint is printing out
  // our initial state, and we should let it decide how it wants to print our
  // label.
  if (level != eDescriptionLevelInitial) {
    s->Indent();
    BreakpointID::GetCanonicalReference(s, m_owner.GetID(), GetID());
  }

  if (level == lldb::eDescriptionLevelBrief)
    return;

  if (level != eDescriptionLevelInitial)
    s->PutCString(": ");

  if (level == lldb::eDescriptionLevelVerbose)
    s->IndentMore();

  if (m_address.IsSectionOffset()) {
    m_address.CalculateSymbolContext(&sc);

    if (level == lldb::eDescriptionLevelFull ||
        level == eDescriptionLevelInitial) {
      if (IsReExported())
        s->PutCString("re-exported target = ");
      else
        s->PutCString("where = ");
      sc.DumpStopContext(s, m_owner.GetTarget().GetProcessSP().get(), m_address,
                         false, true, false, true, true, true);
    } else {
      if (sc.module_sp) {
        s->EOL();
        s->Indent("module = ");
        sc.module_sp->GetFileSpec().Dump(s->AsRawOstream());
      }

      if (sc.comp_unit != nullptr) {
        s->EOL();
        s->Indent("compile unit = ");
        sc.comp_unit->GetPrimaryFile().GetFilename().Dump(s);

        if (sc.function != nullptr) {
          s->EOL();
          s->Indent("function = ");
          s->PutCString(sc.function->GetName().AsCString("<unknown>"));
          if (ConstString mangled_name =
                  sc.function->GetMangled().GetMangledName()) {
            s->EOL();
            s->Indent("mangled function = ");
            s->PutCString(mangled_name.AsCString());
          }
        }

        if (sc.line_entry.line > 0) {
          s->EOL();
          s->Indent("location = ");
          sc.line_entry.DumpStopContext(s, true);
        }

      } else {
        // If we don't have a comp unit, see if we have a symbol we can print.
        if (sc.symbol) {
          s->EOL();
          if (IsReExported())
            s->Indent("re-exported target = ");
          else
            s->Indent("symbol = ");
          s->PutCString(sc.symbol->GetName().AsCString("<unknown>"));
        }
      }
    }
  }

  if (level == lldb::eDescriptionLevelVerbose) {
    s->EOL();
    s->Indent();
  }

  if (m_address.IsSectionOffset() &&
      (level == eDescriptionLevelFull || level == eDescriptionLevelInitial))
    s->Printf(", ");
  s->Printf("address = ");

  ExecutionContextScope *exe_scope = nullptr;
  Target *target = &m_owner.GetTarget();
  if (target)
    exe_scope = target->GetProcessSP().get();
  if (exe_scope == nullptr)
    exe_scope = target;

  if (level == eDescriptionLevelInitial)
    m_address.Dump(s, exe_scope, Address::DumpStyleLoadAddress,
                   Address::DumpStyleFileAddress);
  else
    m_address.Dump(s, exe_scope, Address::DumpStyleLoadAddress,
                   Address::DumpStyleModuleWithFileAddress);

  if (IsIndirect() && m_bp_site_sp) {
    Address resolved_address;
    resolved_address.SetLoadAddress(m_bp_site_sp->GetLoadAddress(), target);
    Symbol *resolved_symbol = resolved_address.CalculateSymbolContextSymbol();
    if (resolved_symbol) {
      if (level == eDescriptionLevelFull || level == eDescriptionLevelInitial)
        s->Printf(", ");
      else if (level == lldb::eDescriptionLevelVerbose) {
        s->EOL();
        s->Indent();
      }
      s->Printf("indirect target = %s",
                resolved_symbol->GetName().GetCString());
    }
  }

  bool is_resolved = IsResolved();
  bool is_hardware = is_resolved && m_bp_site_sp->IsHardware();

  if (level == lldb::eDescriptionLevelVerbose) {
    s->EOL();
    s->Indent();
    s->Printf("resolved = %s\n", is_resolved ? "true" : "false");
    s->Indent();
    s->Printf("hardware = %s\n", is_hardware ? "true" : "false");
    s->Indent();
    s->Printf("hit count = %-4u\n", GetHitCount());

    if (m_options_up) {
      s->Indent();
      m_options_up->GetDescription(s, level);
      s->EOL();
    }
    s->IndentLess();
  } else if (level != eDescriptionLevelInitial) {
    s->Printf(", %sresolved, %shit count = %u ", (is_resolved ? "" : "un"),
              (is_hardware ? "hardware, " : ""), GetHitCount());
    if (m_options_up) {
      m_options_up->GetDescription(s, level);
    }
  }
}

void BreakpointLocation::Dump(Stream *s) const {
  if (s == nullptr)
    return;

  bool is_resolved = IsResolved();
  bool is_hardware = is_resolved && m_bp_site_sp->IsHardware();

  lldb::tid_t tid = GetOptionsSpecifyingKind(BreakpointOptions::eThreadSpec)
                        .GetThreadSpecNoCreate()
                        ->GetTID();
  s->Printf("BreakpointLocation %u: tid = %4.4" PRIx64
            "  load addr = 0x%8.8" PRIx64 "  state = %s  type = %s breakpoint  "
            "hit_count = %-4u  ignore_count = %-4u",
            GetID(), tid,
            (uint64_t)m_address.GetOpcodeLoadAddress(&m_owner.GetTarget()),
            (m_options_up ? m_options_up->IsEnabled() : m_owner.IsEnabled())
                ? "enabled "
                : "disabled",
            is_hardware ? "hardware" : "software", GetHitCount(),
            GetOptionsSpecifyingKind(BreakpointOptions::eIgnoreCount)
                .GetIgnoreCount());
}

void BreakpointLocation::SendBreakpointLocationChangedEvent(
    lldb::BreakpointEventType eventKind) {
  if (!m_owner.IsInternal() && m_owner.GetTarget().EventTypeHasListeners(
                                   Target::eBroadcastBitBreakpointChanged)) {
    auto data_sp = std::make_shared<Breakpoint::BreakpointEventData>(
        eventKind, m_owner.shared_from_this());
    data_sp->GetBreakpointLocationCollection().Add(shared_from_this());
    m_owner.GetTarget().BroadcastEvent(Target::eBroadcastBitBreakpointChanged,
                                       data_sp);
  }
}

void BreakpointLocation::SwapLocation(BreakpointLocationSP swap_from) {
  m_address = swap_from->m_address;
  m_should_resolve_indirect_functions =
      swap_from->m_should_resolve_indirect_functions;
  m_is_reexported = swap_from->m_is_reexported;
  m_is_indirect = swap_from->m_is_indirect;
  m_user_expression_sp.reset();
}

void BreakpointLocation::SetThreadIDInternal(lldb::tid_t thread_id) {
  if (thread_id != LLDB_INVALID_THREAD_ID)
    GetLocationOptions().SetThreadID(thread_id);
  else {
    // If we're resetting this to an invalid thread id, then don't make an
    // options pointer just to do that.
    if (m_options_up != nullptr)
      m_options_up->SetThreadID(thread_id);
  }
}
