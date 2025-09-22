//===-- SBFrame.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <set>
#include <string>

#include "lldb/API/SBFrame.h"

#include "lldb/lldb-types.h"

#include "Utils.h"
#include "lldb/Core/Address.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/ValueObjectRegister.h"
#include "lldb/Core/ValueObjectVariable.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/Expression/ExpressionVariable.h"
#include "lldb/Expression/UserExpression.h"
#include "lldb/Host/Host.h"
#include "lldb/Symbol/Block.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/Variable.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/StackFrameRecognizer.h"
#include "lldb/Target/StackID.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Instrumentation.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Stream.h"

#include "lldb/API/SBAddress.h"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBExpressionOptions.h"
#include "lldb/API/SBFormat.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBSymbolContext.h"
#include "lldb/API/SBThread.h"
#include "lldb/API/SBValue.h"
#include "lldb/API/SBVariablesOptions.h"

#include "llvm/Support/PrettyStackTrace.h"

using namespace lldb;
using namespace lldb_private;

SBFrame::SBFrame() : m_opaque_sp(new ExecutionContextRef()) {
  LLDB_INSTRUMENT_VA(this);
}

SBFrame::SBFrame(const StackFrameSP &lldb_object_sp)
    : m_opaque_sp(new ExecutionContextRef(lldb_object_sp)) {
  LLDB_INSTRUMENT_VA(this, lldb_object_sp);
}

SBFrame::SBFrame(const SBFrame &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  m_opaque_sp = clone(rhs.m_opaque_sp);
}

SBFrame::~SBFrame() = default;

const SBFrame &SBFrame::operator=(const SBFrame &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs)
    m_opaque_sp = clone(rhs.m_opaque_sp);
  return *this;
}

StackFrameSP SBFrame::GetFrameSP() const {
  return (m_opaque_sp ? m_opaque_sp->GetFrameSP() : StackFrameSP());
}

void SBFrame::SetFrameSP(const StackFrameSP &lldb_object_sp) {
  return m_opaque_sp->SetFrameSP(lldb_object_sp);
}

bool SBFrame::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBFrame::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  Target *target = exe_ctx.GetTargetPtr();
  Process *process = exe_ctx.GetProcessPtr();
  if (target && process) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process->GetRunLock()))
      return GetFrameSP().get() != nullptr;
  }

  // Without a target & process we can't have a valid stack frame.
  return false;
}

SBSymbolContext SBFrame::GetSymbolContext(uint32_t resolve_scope) const {
  LLDB_INSTRUMENT_VA(this, resolve_scope);

  SBSymbolContext sb_sym_ctx;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);
  SymbolContextItem scope = static_cast<SymbolContextItem>(resolve_scope);
  Target *target = exe_ctx.GetTargetPtr();
  Process *process = exe_ctx.GetProcessPtr();
  if (target && process) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process->GetRunLock())) {
      if (StackFrame *frame = exe_ctx.GetFramePtr())
        sb_sym_ctx = frame->GetSymbolContext(scope);
    }
  }

  return sb_sym_ctx;
}

SBModule SBFrame::GetModule() const {
  LLDB_INSTRUMENT_VA(this);

  SBModule sb_module;
  ModuleSP module_sp;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  StackFrame *frame = nullptr;
  Target *target = exe_ctx.GetTargetPtr();
  Process *process = exe_ctx.GetProcessPtr();
  if (target && process) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process->GetRunLock())) {
      frame = exe_ctx.GetFramePtr();
      if (frame) {
        module_sp = frame->GetSymbolContext(eSymbolContextModule).module_sp;
        sb_module.SetSP(module_sp);
      }
    }
  }

  return sb_module;
}

SBCompileUnit SBFrame::GetCompileUnit() const {
  LLDB_INSTRUMENT_VA(this);

  SBCompileUnit sb_comp_unit;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  StackFrame *frame = nullptr;
  Target *target = exe_ctx.GetTargetPtr();
  Process *process = exe_ctx.GetProcessPtr();
  if (target && process) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process->GetRunLock())) {
      frame = exe_ctx.GetFramePtr();
      if (frame) {
        sb_comp_unit.reset(
            frame->GetSymbolContext(eSymbolContextCompUnit).comp_unit);
      }
    }
  }

  return sb_comp_unit;
}

SBFunction SBFrame::GetFunction() const {
  LLDB_INSTRUMENT_VA(this);

  SBFunction sb_function;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  StackFrame *frame = nullptr;
  Target *target = exe_ctx.GetTargetPtr();
  Process *process = exe_ctx.GetProcessPtr();
  if (target && process) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process->GetRunLock())) {
      frame = exe_ctx.GetFramePtr();
      if (frame) {
        sb_function.reset(
            frame->GetSymbolContext(eSymbolContextFunction).function);
      }
    }
  }

  return sb_function;
}

SBSymbol SBFrame::GetSymbol() const {
  LLDB_INSTRUMENT_VA(this);

  SBSymbol sb_symbol;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  StackFrame *frame = nullptr;
  Target *target = exe_ctx.GetTargetPtr();
  Process *process = exe_ctx.GetProcessPtr();
  if (target && process) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process->GetRunLock())) {
      frame = exe_ctx.GetFramePtr();
      if (frame) {
        sb_symbol.reset(frame->GetSymbolContext(eSymbolContextSymbol).symbol);
      }
    }
  }

  return sb_symbol;
}

SBBlock SBFrame::GetBlock() const {
  LLDB_INSTRUMENT_VA(this);

  SBBlock sb_block;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  StackFrame *frame = nullptr;
  Target *target = exe_ctx.GetTargetPtr();
  Process *process = exe_ctx.GetProcessPtr();
  if (target && process) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process->GetRunLock())) {
      frame = exe_ctx.GetFramePtr();
      if (frame)
        sb_block.SetPtr(frame->GetSymbolContext(eSymbolContextBlock).block);
    }
  }
  return sb_block;
}

SBBlock SBFrame::GetFrameBlock() const {
  LLDB_INSTRUMENT_VA(this);

  SBBlock sb_block;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  StackFrame *frame = nullptr;
  Target *target = exe_ctx.GetTargetPtr();
  Process *process = exe_ctx.GetProcessPtr();
  if (target && process) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process->GetRunLock())) {
      frame = exe_ctx.GetFramePtr();
      if (frame)
        sb_block.SetPtr(frame->GetFrameBlock());
    }
  }
  return sb_block;
}

SBLineEntry SBFrame::GetLineEntry() const {
  LLDB_INSTRUMENT_VA(this);

  SBLineEntry sb_line_entry;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  StackFrame *frame = nullptr;
  Target *target = exe_ctx.GetTargetPtr();
  Process *process = exe_ctx.GetProcessPtr();
  if (target && process) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process->GetRunLock())) {
      frame = exe_ctx.GetFramePtr();
      if (frame) {
        sb_line_entry.SetLineEntry(
            frame->GetSymbolContext(eSymbolContextLineEntry).line_entry);
      }
    }
  }
  return sb_line_entry;
}

uint32_t SBFrame::GetFrameID() const {
  LLDB_INSTRUMENT_VA(this);

  uint32_t frame_idx = UINT32_MAX;

  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  StackFrame *frame = exe_ctx.GetFramePtr();
  if (frame)
    frame_idx = frame->GetFrameIndex();

  return frame_idx;
}

lldb::addr_t SBFrame::GetCFA() const {
  LLDB_INSTRUMENT_VA(this);

  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  StackFrame *frame = exe_ctx.GetFramePtr();
  if (frame)
    return frame->GetStackID().GetCallFrameAddress();
  return LLDB_INVALID_ADDRESS;
}

addr_t SBFrame::GetPC() const {
  LLDB_INSTRUMENT_VA(this);

  addr_t addr = LLDB_INVALID_ADDRESS;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  StackFrame *frame = nullptr;
  Target *target = exe_ctx.GetTargetPtr();
  Process *process = exe_ctx.GetProcessPtr();
  if (target && process) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process->GetRunLock())) {
      frame = exe_ctx.GetFramePtr();
      if (frame) {
        addr = frame->GetFrameCodeAddress().GetOpcodeLoadAddress(
            target, AddressClass::eCode);
      }
    }
  }

  return addr;
}

bool SBFrame::SetPC(addr_t new_pc) {
  LLDB_INSTRUMENT_VA(this, new_pc);

  bool ret_val = false;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  Target *target = exe_ctx.GetTargetPtr();
  Process *process = exe_ctx.GetProcessPtr();
  if (target && process) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process->GetRunLock())) {
      if (StackFrame *frame = exe_ctx.GetFramePtr()) {
        if (RegisterContextSP reg_ctx_sp = frame->GetRegisterContext()) {
          ret_val = reg_ctx_sp->SetPC(new_pc);
        }
      }
    }
  }

  return ret_val;
}

addr_t SBFrame::GetSP() const {
  LLDB_INSTRUMENT_VA(this);

  addr_t addr = LLDB_INVALID_ADDRESS;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  Target *target = exe_ctx.GetTargetPtr();
  Process *process = exe_ctx.GetProcessPtr();
  if (target && process) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process->GetRunLock())) {
      if (StackFrame *frame = exe_ctx.GetFramePtr()) {
        if (RegisterContextSP reg_ctx_sp = frame->GetRegisterContext()) {
          addr = reg_ctx_sp->GetSP();
        }
      }
    }
  }

  return addr;
}

addr_t SBFrame::GetFP() const {
  LLDB_INSTRUMENT_VA(this);

  addr_t addr = LLDB_INVALID_ADDRESS;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  Target *target = exe_ctx.GetTargetPtr();
  Process *process = exe_ctx.GetProcessPtr();
  if (target && process) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process->GetRunLock())) {
      if (StackFrame *frame = exe_ctx.GetFramePtr()) {
        if (RegisterContextSP reg_ctx_sp = frame->GetRegisterContext()) {
          addr = reg_ctx_sp->GetFP();
        }
      }
    }
  }

  return addr;
}

SBAddress SBFrame::GetPCAddress() const {
  LLDB_INSTRUMENT_VA(this);

  SBAddress sb_addr;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  StackFrame *frame = exe_ctx.GetFramePtr();
  Target *target = exe_ctx.GetTargetPtr();
  Process *process = exe_ctx.GetProcessPtr();
  if (target && process) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process->GetRunLock())) {
      frame = exe_ctx.GetFramePtr();
      if (frame)
        sb_addr.SetAddress(frame->GetFrameCodeAddress());
    }
  }
  return sb_addr;
}

void SBFrame::Clear() {
  LLDB_INSTRUMENT_VA(this);

  m_opaque_sp->Clear();
}

lldb::SBValue SBFrame::GetValueForVariablePath(const char *var_path) {
  LLDB_INSTRUMENT_VA(this, var_path);

  SBValue sb_value;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  StackFrame *frame = exe_ctx.GetFramePtr();
  Target *target = exe_ctx.GetTargetPtr();
  if (frame && target) {
    lldb::DynamicValueType use_dynamic =
        frame->CalculateTarget()->GetPreferDynamicValue();
    sb_value = GetValueForVariablePath(var_path, use_dynamic);
  }
  return sb_value;
}

lldb::SBValue SBFrame::GetValueForVariablePath(const char *var_path,
                                               DynamicValueType use_dynamic) {
  LLDB_INSTRUMENT_VA(this, var_path, use_dynamic);

  SBValue sb_value;
  if (var_path == nullptr || var_path[0] == '\0') {
    return sb_value;
  }

  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  StackFrame *frame = nullptr;
  Target *target = exe_ctx.GetTargetPtr();
  Process *process = exe_ctx.GetProcessPtr();
  if (target && process) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process->GetRunLock())) {
      frame = exe_ctx.GetFramePtr();
      if (frame) {
        VariableSP var_sp;
        Status error;
        ValueObjectSP value_sp(frame->GetValueForVariableExpressionPath(
            var_path, eNoDynamicValues,
            StackFrame::eExpressionPathOptionCheckPtrVsMember |
                StackFrame::eExpressionPathOptionsAllowDirectIVarAccess,
            var_sp, error));
        sb_value.SetSP(value_sp, use_dynamic);
      }
    }
  }
  return sb_value;
}

SBValue SBFrame::FindVariable(const char *name) {
  LLDB_INSTRUMENT_VA(this, name);

  SBValue value;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  StackFrame *frame = exe_ctx.GetFramePtr();
  Target *target = exe_ctx.GetTargetPtr();
  if (frame && target) {
    lldb::DynamicValueType use_dynamic =
        frame->CalculateTarget()->GetPreferDynamicValue();
    value = FindVariable(name, use_dynamic);
  }
  return value;
}

SBValue SBFrame::FindVariable(const char *name,
                              lldb::DynamicValueType use_dynamic) {
  LLDB_INSTRUMENT_VA(this, name, use_dynamic);

  VariableSP var_sp;
  SBValue sb_value;

  if (name == nullptr || name[0] == '\0') {
    return sb_value;
  }

  ValueObjectSP value_sp;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  StackFrame *frame = nullptr;
  Target *target = exe_ctx.GetTargetPtr();
  Process *process = exe_ctx.GetProcessPtr();
  if (target && process) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process->GetRunLock())) {
      frame = exe_ctx.GetFramePtr();
      if (frame) {
        value_sp = frame->FindVariable(ConstString(name));

        if (value_sp)
          sb_value.SetSP(value_sp, use_dynamic);
      }
    }
  }

  return sb_value;
}

SBValue SBFrame::FindValue(const char *name, ValueType value_type) {
  LLDB_INSTRUMENT_VA(this, name, value_type);

  SBValue value;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  StackFrame *frame = exe_ctx.GetFramePtr();
  Target *target = exe_ctx.GetTargetPtr();
  if (frame && target) {
    lldb::DynamicValueType use_dynamic =
        frame->CalculateTarget()->GetPreferDynamicValue();
    value = FindValue(name, value_type, use_dynamic);
  }
  return value;
}

SBValue SBFrame::FindValue(const char *name, ValueType value_type,
                           lldb::DynamicValueType use_dynamic) {
  LLDB_INSTRUMENT_VA(this, name, value_type, use_dynamic);

  SBValue sb_value;

  if (name == nullptr || name[0] == '\0') {
    return sb_value;
  }

  ValueObjectSP value_sp;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  StackFrame *frame = nullptr;
  Target *target = exe_ctx.GetTargetPtr();
  Process *process = exe_ctx.GetProcessPtr();
  if (target && process) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process->GetRunLock())) {
      frame = exe_ctx.GetFramePtr();
      if (frame) {
        VariableList variable_list;

        switch (value_type) {
        case eValueTypeVariableGlobal:      // global variable
        case eValueTypeVariableStatic:      // static variable
        case eValueTypeVariableArgument:    // function argument variables
        case eValueTypeVariableLocal:       // function local variables
        case eValueTypeVariableThreadLocal: // thread local variables
        {
          SymbolContext sc(frame->GetSymbolContext(eSymbolContextBlock));

          const bool can_create = true;
          const bool get_parent_variables = true;
          const bool stop_if_block_is_inlined_function = true;

          if (sc.block)
            sc.block->AppendVariables(
                can_create, get_parent_variables,
                stop_if_block_is_inlined_function,
                [frame](Variable *v) { return v->IsInScope(frame); },
                &variable_list);
          if (value_type == eValueTypeVariableGlobal 
              || value_type == eValueTypeVariableStatic) {
            const bool get_file_globals = true;
            VariableList *frame_vars = frame->GetVariableList(get_file_globals,
                                                              nullptr);
            if (frame_vars)
              frame_vars->AppendVariablesIfUnique(variable_list);
          }
          ConstString const_name(name);
          VariableSP variable_sp(
              variable_list.FindVariable(const_name, value_type));
          if (variable_sp) {
            value_sp = frame->GetValueObjectForFrameVariable(variable_sp,
                                                             eNoDynamicValues);
            sb_value.SetSP(value_sp, use_dynamic);
          }
        } break;

        case eValueTypeRegister: // stack frame register value
        {
          RegisterContextSP reg_ctx(frame->GetRegisterContext());
          if (reg_ctx) {
            if (const RegisterInfo *reg_info =
                    reg_ctx->GetRegisterInfoByName(name)) {
              value_sp = ValueObjectRegister::Create(frame, reg_ctx, reg_info);
              sb_value.SetSP(value_sp);
            }
          }
        } break;

        case eValueTypeRegisterSet: // A collection of stack frame register
                                    // values
        {
          RegisterContextSP reg_ctx(frame->GetRegisterContext());
          if (reg_ctx) {
            const uint32_t num_sets = reg_ctx->GetRegisterSetCount();
            for (uint32_t set_idx = 0; set_idx < num_sets; ++set_idx) {
              const RegisterSet *reg_set = reg_ctx->GetRegisterSet(set_idx);
              if (reg_set &&
                  (llvm::StringRef(reg_set->name).equals_insensitive(name) ||
                   llvm::StringRef(reg_set->short_name)
                       .equals_insensitive(name))) {
                value_sp =
                    ValueObjectRegisterSet::Create(frame, reg_ctx, set_idx);
                sb_value.SetSP(value_sp);
                break;
              }
            }
          }
        } break;

        case eValueTypeConstResult: // constant result variables
        {
          ConstString const_name(name);
          ExpressionVariableSP expr_var_sp(
              target->GetPersistentVariable(const_name));
          if (expr_var_sp) {
            value_sp = expr_var_sp->GetValueObject();
            sb_value.SetSP(value_sp, use_dynamic);
          }
        } break;

        default:
          break;
        }
      }
    }
  }

  return sb_value;
}

bool SBFrame::IsEqual(const SBFrame &that) const {
  LLDB_INSTRUMENT_VA(this, that);

  lldb::StackFrameSP this_sp = GetFrameSP();
  lldb::StackFrameSP that_sp = that.GetFrameSP();
  return (this_sp && that_sp && this_sp->GetStackID() == that_sp->GetStackID());
}

bool SBFrame::operator==(const SBFrame &rhs) const {
  LLDB_INSTRUMENT_VA(this, rhs);

  return IsEqual(rhs);
}

bool SBFrame::operator!=(const SBFrame &rhs) const {
  LLDB_INSTRUMENT_VA(this, rhs);

  return !IsEqual(rhs);
}

SBThread SBFrame::GetThread() const {
  LLDB_INSTRUMENT_VA(this);

  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  ThreadSP thread_sp(exe_ctx.GetThreadSP());
  SBThread sb_thread(thread_sp);

  return sb_thread;
}

const char *SBFrame::Disassemble() const {
  LLDB_INSTRUMENT_VA(this);

  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);
  Target *target = exe_ctx.GetTargetPtr();
  Process *process = exe_ctx.GetProcessPtr();
  if (!target || !process)
    return nullptr;

  Process::StopLocker stop_locker;
  if (stop_locker.TryLock(&process->GetRunLock())) {
    if (auto *frame = exe_ctx.GetFramePtr())
      return ConstString(frame->Disassemble()).GetCString();
  }

  return nullptr;
}

SBValueList SBFrame::GetVariables(bool arguments, bool locals, bool statics,
                                  bool in_scope_only) {
  LLDB_INSTRUMENT_VA(this, arguments, locals, statics, in_scope_only);

  SBValueList value_list;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  StackFrame *frame = exe_ctx.GetFramePtr();
  Target *target = exe_ctx.GetTargetPtr();
  if (frame && target) {
    lldb::DynamicValueType use_dynamic =
        frame->CalculateTarget()->GetPreferDynamicValue();
    const bool include_runtime_support_values =
        target->GetDisplayRuntimeSupportValues();

    SBVariablesOptions options;
    options.SetIncludeArguments(arguments);
    options.SetIncludeLocals(locals);
    options.SetIncludeStatics(statics);
    options.SetInScopeOnly(in_scope_only);
    options.SetIncludeRuntimeSupportValues(include_runtime_support_values);
    options.SetUseDynamic(use_dynamic);

    value_list = GetVariables(options);
  }
  return value_list;
}

lldb::SBValueList SBFrame::GetVariables(bool arguments, bool locals,
                                        bool statics, bool in_scope_only,
                                        lldb::DynamicValueType use_dynamic) {
  LLDB_INSTRUMENT_VA(this, arguments, locals, statics, in_scope_only,
                     use_dynamic);

  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  Target *target = exe_ctx.GetTargetPtr();
  const bool include_runtime_support_values =
      target ? target->GetDisplayRuntimeSupportValues() : false;
  SBVariablesOptions options;
  options.SetIncludeArguments(arguments);
  options.SetIncludeLocals(locals);
  options.SetIncludeStatics(statics);
  options.SetInScopeOnly(in_scope_only);
  options.SetIncludeRuntimeSupportValues(include_runtime_support_values);
  options.SetUseDynamic(use_dynamic);
  return GetVariables(options);
}

SBValueList SBFrame::GetVariables(const lldb::SBVariablesOptions &options) {
  LLDB_INSTRUMENT_VA(this, options);

  SBValueList value_list;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  StackFrame *frame = nullptr;
  Target *target = exe_ctx.GetTargetPtr();

  const bool statics = options.GetIncludeStatics();
  const bool arguments = options.GetIncludeArguments();
  const bool recognized_arguments =
        options.GetIncludeRecognizedArguments(SBTarget(exe_ctx.GetTargetSP()));
  const bool locals = options.GetIncludeLocals();
  const bool in_scope_only = options.GetInScopeOnly();
  const bool include_runtime_support_values =
      options.GetIncludeRuntimeSupportValues();
  const lldb::DynamicValueType use_dynamic = options.GetUseDynamic();


  std::set<VariableSP> variable_set;
  Process *process = exe_ctx.GetProcessPtr();
  if (target && process) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process->GetRunLock())) {
      frame = exe_ctx.GetFramePtr();
      if (frame) {
        Debugger &dbg = process->GetTarget().GetDebugger();
        VariableList *variable_list = nullptr;
        Status var_error;
        variable_list = frame->GetVariableList(true, &var_error);
        if (var_error.Fail())
          value_list.SetError(var_error);
        if (variable_list) {
          const size_t num_variables = variable_list->GetSize();
          if (num_variables) {
            size_t num_produced = 0;
            for (const VariableSP &variable_sp : *variable_list) {
              if (INTERRUPT_REQUESTED(dbg, 
                    "Interrupted getting frame variables with {0} of {1} "
                    "produced.", num_produced, num_variables))
                return {};

              if (variable_sp) {
                bool add_variable = false;
                switch (variable_sp->GetScope()) {
                case eValueTypeVariableGlobal:
                case eValueTypeVariableStatic:
                case eValueTypeVariableThreadLocal:
                  add_variable = statics;
                  break;

                case eValueTypeVariableArgument:
                  add_variable = arguments;
                  break;

                case eValueTypeVariableLocal:
                  add_variable = locals;
                  break;

                default:
                  break;
                }
                if (add_variable) {
                  // Only add variables once so we don't end up with duplicates
                  if (variable_set.find(variable_sp) == variable_set.end())
                    variable_set.insert(variable_sp);
                  else
                    continue;

                  if (in_scope_only && !variable_sp->IsInScope(frame))
                    continue;

                  ValueObjectSP valobj_sp(frame->GetValueObjectForFrameVariable(
                      variable_sp, eNoDynamicValues));

                  if (!include_runtime_support_values && valobj_sp != nullptr &&
                      valobj_sp->IsRuntimeSupportValue())
                    continue;

                  SBValue value_sb;
                  value_sb.SetSP(valobj_sp, use_dynamic);
                  value_list.Append(value_sb);
                }
              }
            }
            num_produced++;
          }
        }
        if (recognized_arguments) {
          auto recognized_frame = frame->GetRecognizedFrame();
          if (recognized_frame) {
            ValueObjectListSP recognized_arg_list =
                recognized_frame->GetRecognizedArguments();
            if (recognized_arg_list) {
              for (auto &rec_value_sp : recognized_arg_list->GetObjects()) {
                SBValue value_sb;
                value_sb.SetSP(rec_value_sp, use_dynamic);
                value_list.Append(value_sb);
              }
            }
          }
        }
      }
    }
  }

  return value_list;
}

SBValueList SBFrame::GetRegisters() {
  LLDB_INSTRUMENT_VA(this);

  SBValueList value_list;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  StackFrame *frame = nullptr;
  Target *target = exe_ctx.GetTargetPtr();
  Process *process = exe_ctx.GetProcessPtr();
  if (target && process) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process->GetRunLock())) {
      frame = exe_ctx.GetFramePtr();
      if (frame) {
        RegisterContextSP reg_ctx(frame->GetRegisterContext());
        if (reg_ctx) {
          const uint32_t num_sets = reg_ctx->GetRegisterSetCount();
          for (uint32_t set_idx = 0; set_idx < num_sets; ++set_idx) {
            value_list.Append(
                ValueObjectRegisterSet::Create(frame, reg_ctx, set_idx));
          }
        }
      }
    }
  }

  return value_list;
}

SBValue SBFrame::FindRegister(const char *name) {
  LLDB_INSTRUMENT_VA(this, name);

  SBValue result;
  ValueObjectSP value_sp;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  StackFrame *frame = nullptr;
  Target *target = exe_ctx.GetTargetPtr();
  Process *process = exe_ctx.GetProcessPtr();
  if (target && process) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process->GetRunLock())) {
      frame = exe_ctx.GetFramePtr();
      if (frame) {
        RegisterContextSP reg_ctx(frame->GetRegisterContext());
        if (reg_ctx) {
          if (const RegisterInfo *reg_info =
                  reg_ctx->GetRegisterInfoByName(name)) {
            value_sp = ValueObjectRegister::Create(frame, reg_ctx, reg_info);
            result.SetSP(value_sp);
          }
        }
      }
    }
  }

  return result;
}

SBError SBFrame::GetDescriptionWithFormat(const SBFormat &format,
                                          SBStream &output) {
  Stream &strm = output.ref();

  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  StackFrame *frame = nullptr;
  Target *target = exe_ctx.GetTargetPtr();
  Process *process = exe_ctx.GetProcessPtr();
  SBError error;

  if (!format) {
    error.SetErrorString("The provided SBFormat object is invalid");
    return error;
  }

  if (target && process) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process->GetRunLock())) {
      frame = exe_ctx.GetFramePtr();
      if (frame &&
          frame->DumpUsingFormat(strm, format.GetFormatEntrySP().get())) {
        return error;
      }
    }
  }
  error.SetErrorStringWithFormat(
      "It was not possible to generate a frame "
      "description with the given format string '%s'",
      format.GetFormatEntrySP()->string.c_str());
  return error;
}

bool SBFrame::GetDescription(SBStream &description) {
  LLDB_INSTRUMENT_VA(this, description);

  Stream &strm = description.ref();

  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  StackFrame *frame;
  Target *target = exe_ctx.GetTargetPtr();
  Process *process = exe_ctx.GetProcessPtr();
  if (target && process) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process->GetRunLock())) {
      frame = exe_ctx.GetFramePtr();
      if (frame) {
        frame->DumpUsingSettingsFormat(&strm);
      }
    }

  } else
    strm.PutCString("No value");

  return true;
}

SBValue SBFrame::EvaluateExpression(const char *expr) {
  LLDB_INSTRUMENT_VA(this, expr);

  SBValue result;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  StackFrame *frame = exe_ctx.GetFramePtr();
  Target *target = exe_ctx.GetTargetPtr();
  if (frame && target) {
    SBExpressionOptions options;
    lldb::DynamicValueType fetch_dynamic_value =
        frame->CalculateTarget()->GetPreferDynamicValue();
    options.SetFetchDynamicValue(fetch_dynamic_value);
    options.SetUnwindOnError(true);
    options.SetIgnoreBreakpoints(true);
    SourceLanguage language = target->GetLanguage();
    if (!language)
      language = frame->GetLanguage();
    options.SetLanguage((SBSourceLanguageName)language.name, language.version);
    return EvaluateExpression(expr, options);
  } else {
    Status error;
    error.SetErrorString("can't evaluate expressions when the "
                           "process is running.");
    ValueObjectSP error_val_sp = ValueObjectConstResult::Create(nullptr, error);
    result.SetSP(error_val_sp, false);
  }
  return result;
}

SBValue
SBFrame::EvaluateExpression(const char *expr,
                            lldb::DynamicValueType fetch_dynamic_value) {
  LLDB_INSTRUMENT_VA(this, expr, fetch_dynamic_value);

  SBExpressionOptions options;
  options.SetFetchDynamicValue(fetch_dynamic_value);
  options.SetUnwindOnError(true);
  options.SetIgnoreBreakpoints(true);
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  StackFrame *frame = exe_ctx.GetFramePtr();
  Target *target = exe_ctx.GetTargetPtr();
  SourceLanguage language;
  if (target)
    language = target->GetLanguage();
  if (!language && frame)
    language = frame->GetLanguage();
  options.SetLanguage((SBSourceLanguageName)language.name, language.version);
  return EvaluateExpression(expr, options);
}

SBValue SBFrame::EvaluateExpression(const char *expr,
                                    lldb::DynamicValueType fetch_dynamic_value,
                                    bool unwind_on_error) {
  LLDB_INSTRUMENT_VA(this, expr, fetch_dynamic_value, unwind_on_error);

  SBExpressionOptions options;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  options.SetFetchDynamicValue(fetch_dynamic_value);
  options.SetUnwindOnError(unwind_on_error);
  options.SetIgnoreBreakpoints(true);
  StackFrame *frame = exe_ctx.GetFramePtr();
  Target *target = exe_ctx.GetTargetPtr();
  SourceLanguage language;
  if (target)
    language = target->GetLanguage();
  if (!language && frame)
    language = frame->GetLanguage();
  options.SetLanguage((SBSourceLanguageName)language.name, language.version);
  return EvaluateExpression(expr, options);
}

lldb::SBValue SBFrame::EvaluateExpression(const char *expr,
                                          const SBExpressionOptions &options) {
  LLDB_INSTRUMENT_VA(this, expr, options);

  Log *expr_log = GetLog(LLDBLog::Expressions);

  SBValue expr_result;

  if (expr == nullptr || expr[0] == '\0') {
    return expr_result;
  }

  ValueObjectSP expr_value_sp;

  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  StackFrame *frame = nullptr;
  Target *target = exe_ctx.GetTargetPtr();
  Process *process = exe_ctx.GetProcessPtr();

  if (target && process) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process->GetRunLock())) {
      frame = exe_ctx.GetFramePtr();
      if (frame) {
        std::unique_ptr<llvm::PrettyStackTraceFormat> stack_trace;
        if (target->GetDisplayExpressionsInCrashlogs()) {
          StreamString frame_description;
          frame->DumpUsingSettingsFormat(&frame_description);
          stack_trace = std::make_unique<llvm::PrettyStackTraceFormat>(
              "SBFrame::EvaluateExpression (expr = \"%s\", fetch_dynamic_value "
              "= %u) %s",
              expr, options.GetFetchDynamicValue(),
              frame_description.GetData());
        }

        target->EvaluateExpression(expr, frame, expr_value_sp, options.ref());
        expr_result.SetSP(expr_value_sp, options.GetFetchDynamicValue());
      }
    } else {
      Status error;
      error.SetErrorString("can't evaluate expressions when the "
                           "process is running.");
      expr_value_sp = ValueObjectConstResult::Create(nullptr, error);
      expr_result.SetSP(expr_value_sp, false);
    }
  } else {
      Status error;
      error.SetErrorString("sbframe object is not valid.");
      expr_value_sp = ValueObjectConstResult::Create(nullptr, error);
      expr_result.SetSP(expr_value_sp, false);
  }

  if (expr_result.GetError().Success())
    LLDB_LOGF(expr_log,
              "** [SBFrame::EvaluateExpression] Expression result is "
              "%s, summary %s **",
              expr_result.GetValue(), expr_result.GetSummary());
  else
    LLDB_LOGF(expr_log,
              "** [SBFrame::EvaluateExpression] Expression evaluation failed: "
              "%s **",
              expr_result.GetError().GetCString());

  return expr_result;
}

bool SBFrame::IsInlined() {
  LLDB_INSTRUMENT_VA(this);

  return static_cast<const SBFrame *>(this)->IsInlined();
}

bool SBFrame::IsInlined() const {
  LLDB_INSTRUMENT_VA(this);

  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  StackFrame *frame = nullptr;
  Target *target = exe_ctx.GetTargetPtr();
  Process *process = exe_ctx.GetProcessPtr();
  if (target && process) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process->GetRunLock())) {
      frame = exe_ctx.GetFramePtr();
      if (frame) {

        Block *block = frame->GetSymbolContext(eSymbolContextBlock).block;
        if (block)
          return block->GetContainingInlinedBlock() != nullptr;
      }
    }
  }
  return false;
}

bool SBFrame::IsArtificial() {
  LLDB_INSTRUMENT_VA(this);

  return static_cast<const SBFrame *>(this)->IsArtificial();
}

bool SBFrame::IsArtificial() const {
  LLDB_INSTRUMENT_VA(this);

  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  StackFrame *frame = exe_ctx.GetFramePtr();
  if (frame)
    return frame->IsArtificial();

  return false;
}

const char *SBFrame::GetFunctionName() {
  LLDB_INSTRUMENT_VA(this);

  return static_cast<const SBFrame *>(this)->GetFunctionName();
}

lldb::LanguageType SBFrame::GuessLanguage() const {
  LLDB_INSTRUMENT_VA(this);

  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  StackFrame *frame = nullptr;
  Target *target = exe_ctx.GetTargetPtr();
  Process *process = exe_ctx.GetProcessPtr();
  if (target && process) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process->GetRunLock())) {
      frame = exe_ctx.GetFramePtr();
      if (frame) {
        return frame->GuessLanguage().AsLanguageType();
      }
    }
  }
  return eLanguageTypeUnknown;
}

const char *SBFrame::GetFunctionName() const {
  LLDB_INSTRUMENT_VA(this);

  const char *name = nullptr;
  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  StackFrame *frame = nullptr;
  Target *target = exe_ctx.GetTargetPtr();
  Process *process = exe_ctx.GetProcessPtr();
  if (target && process) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process->GetRunLock())) {
      frame = exe_ctx.GetFramePtr();
      if (frame) {
        SymbolContext sc(frame->GetSymbolContext(eSymbolContextFunction |
                                                 eSymbolContextBlock |
                                                 eSymbolContextSymbol));
        if (sc.block) {
          Block *inlined_block = sc.block->GetContainingInlinedBlock();
          if (inlined_block) {
            const InlineFunctionInfo *inlined_info =
                inlined_block->GetInlinedFunctionInfo();
            name = inlined_info->GetName().AsCString();
          }
        }

        if (name == nullptr) {
          if (sc.function)
            name = sc.function->GetName().GetCString();
        }

        if (name == nullptr) {
          if (sc.symbol)
            name = sc.symbol->GetName().GetCString();
        }
      }
    }
  }
  return name;
}

const char *SBFrame::GetDisplayFunctionName() {
  LLDB_INSTRUMENT_VA(this);

  const char *name = nullptr;

  std::unique_lock<std::recursive_mutex> lock;
  ExecutionContext exe_ctx(m_opaque_sp.get(), lock);

  StackFrame *frame = nullptr;
  Target *target = exe_ctx.GetTargetPtr();
  Process *process = exe_ctx.GetProcessPtr();
  if (target && process) {
    Process::StopLocker stop_locker;
    if (stop_locker.TryLock(&process->GetRunLock())) {
      frame = exe_ctx.GetFramePtr();
      if (frame) {
        SymbolContext sc(frame->GetSymbolContext(eSymbolContextFunction |
                                                 eSymbolContextBlock |
                                                 eSymbolContextSymbol));
        if (sc.block) {
          Block *inlined_block = sc.block->GetContainingInlinedBlock();
          if (inlined_block) {
            const InlineFunctionInfo *inlined_info =
                inlined_block->GetInlinedFunctionInfo();
            name = inlined_info->GetDisplayName().AsCString();
          }
        }

        if (name == nullptr) {
          if (sc.function)
            name = sc.function->GetDisplayName().GetCString();
        }

        if (name == nullptr) {
          if (sc.symbol)
            name = sc.symbol->GetDisplayName().GetCString();
        }
      }
    }
  }
  return name;
}
