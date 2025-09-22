//===-- LLVMUserExpression.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//


#include "lldb/Expression/LLVMUserExpression.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/Expression/DiagnosticManager.h"
#include "lldb/Expression/ExpressionVariable.h"
#include "lldb/Expression/IRExecutionUnit.h"
#include "lldb/Expression/IRInterpreter.h"
#include "lldb/Expression/Materializer.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Symbol/Block.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/SymbolVendor.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/ThreadPlan.h"
#include "lldb/Target/ThreadPlanCallUserExpression.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb;
using namespace lldb_private;

char LLVMUserExpression::ID;

LLVMUserExpression::LLVMUserExpression(ExecutionContextScope &exe_scope,
                                       llvm::StringRef expr,
                                       llvm::StringRef prefix,
                                       SourceLanguage language,
                                       ResultType desired_type,
                                       const EvaluateExpressionOptions &options)
    : UserExpression(exe_scope, expr, prefix, language, desired_type, options),
      m_stack_frame_bottom(LLDB_INVALID_ADDRESS),
      m_stack_frame_top(LLDB_INVALID_ADDRESS), m_allow_cxx(false),
      m_allow_objc(false), m_transformed_text(), m_execution_unit_sp(),
      m_materializer_up(), m_jit_module_wp(), m_target(nullptr),
      m_can_interpret(false), m_materialized_address(LLDB_INVALID_ADDRESS) {}

LLVMUserExpression::~LLVMUserExpression() {
  if (m_target) {
    lldb::ModuleSP jit_module_sp(m_jit_module_wp.lock());
    if (jit_module_sp)
      m_target->GetImages().Remove(jit_module_sp);
  }
}

lldb::ExpressionResults
LLVMUserExpression::DoExecute(DiagnosticManager &diagnostic_manager,
                              ExecutionContext &exe_ctx,
                              const EvaluateExpressionOptions &options,
                              lldb::UserExpressionSP &shared_ptr_to_me,
                              lldb::ExpressionVariableSP &result) {
  // The expression log is quite verbose, and if you're just tracking the
  // execution of the expression, it's quite convenient to have these logs come
  // out with the STEP log as well.
  Log *log(GetLog(LLDBLog::Expressions | LLDBLog::Step));

  if (m_jit_start_addr == LLDB_INVALID_ADDRESS && !m_can_interpret) {
    diagnostic_manager.PutString(
        lldb::eSeverityError,
        "Expression can't be run, because there is no JIT compiled function");
    return lldb::eExpressionSetupError;
  }

  lldb::addr_t struct_address = LLDB_INVALID_ADDRESS;

  if (!PrepareToExecuteJITExpression(diagnostic_manager, exe_ctx,
                                     struct_address)) {
    diagnostic_manager.Printf(
        lldb::eSeverityError,
        "errored out in %s, couldn't PrepareToExecuteJITExpression",
        __FUNCTION__);
    return lldb::eExpressionSetupError;
  }

  lldb::addr_t function_stack_bottom = LLDB_INVALID_ADDRESS;
  lldb::addr_t function_stack_top = LLDB_INVALID_ADDRESS;

  if (m_can_interpret) {
    llvm::Module *module = m_execution_unit_sp->GetModule();
    llvm::Function *function = m_execution_unit_sp->GetFunction();

    if (!module || !function) {
      diagnostic_manager.PutString(
          lldb::eSeverityError, "supposed to interpret, but nothing is there");
      return lldb::eExpressionSetupError;
    }

    Status interpreter_error;

    std::vector<lldb::addr_t> args;

    if (!AddArguments(exe_ctx, args, struct_address, diagnostic_manager)) {
      diagnostic_manager.Printf(lldb::eSeverityError,
                                "errored out in %s, couldn't AddArguments",
                                __FUNCTION__);
      return lldb::eExpressionSetupError;
    }

    function_stack_bottom = m_stack_frame_bottom;
    function_stack_top = m_stack_frame_top;

    IRInterpreter::Interpret(*module, *function, args, *m_execution_unit_sp,
                             interpreter_error, function_stack_bottom,
                             function_stack_top, exe_ctx, options.GetTimeout());

    if (!interpreter_error.Success()) {
      diagnostic_manager.Printf(lldb::eSeverityError,
                                "supposed to interpret, but failed: %s",
                                interpreter_error.AsCString());
      return lldb::eExpressionDiscarded;
    }
  } else {
    if (!exe_ctx.HasThreadScope()) {
      diagnostic_manager.Printf(lldb::eSeverityError,
                                "%s called with no thread selected",
                                __FUNCTION__);
      return lldb::eExpressionSetupError;
    }

    // Store away the thread ID for error reporting, in case it exits
    // during execution:
    lldb::tid_t expr_thread_id = exe_ctx.GetThreadRef().GetID();

    Address wrapper_address(m_jit_start_addr);

    std::vector<lldb::addr_t> args;

    if (!AddArguments(exe_ctx, args, struct_address, diagnostic_manager)) {
      diagnostic_manager.Printf(lldb::eSeverityError,
                                "errored out in %s, couldn't AddArguments",
                                __FUNCTION__);
      return lldb::eExpressionSetupError;
    }

    lldb::ThreadPlanSP call_plan_sp(new ThreadPlanCallUserExpression(
        exe_ctx.GetThreadRef(), wrapper_address, args, options,
        shared_ptr_to_me));

    StreamString ss;
    if (!call_plan_sp || !call_plan_sp->ValidatePlan(&ss)) {
      diagnostic_manager.PutString(lldb::eSeverityError, ss.GetString());
      return lldb::eExpressionSetupError;
    }

    ThreadPlanCallUserExpression *user_expression_plan =
        static_cast<ThreadPlanCallUserExpression *>(call_plan_sp.get());

    lldb::addr_t function_stack_pointer =
        user_expression_plan->GetFunctionStackPointer();

    function_stack_bottom = function_stack_pointer - HostInfo::GetPageSize();
    function_stack_top = function_stack_pointer;

    LLDB_LOGF(log,
              "-- [UserExpression::Execute] Execution of expression begins --");

    if (exe_ctx.GetProcessPtr())
      exe_ctx.GetProcessPtr()->SetRunningUserExpression(true);

    lldb::ExpressionResults execution_result =
        exe_ctx.GetProcessRef().RunThreadPlan(exe_ctx, call_plan_sp, options,
                                              diagnostic_manager);

    if (exe_ctx.GetProcessPtr())
      exe_ctx.GetProcessPtr()->SetRunningUserExpression(false);

    LLDB_LOGF(log, "-- [UserExpression::Execute] Execution of expression "
                   "completed --");

    if (execution_result == lldb::eExpressionInterrupted ||
        execution_result == lldb::eExpressionHitBreakpoint) {
      const char *error_desc = nullptr;

      if (user_expression_plan) {
        if (auto real_stop_info_sp = user_expression_plan->GetRealStopInfo())
          error_desc = real_stop_info_sp->GetDescription();
      }
      if (error_desc)
        diagnostic_manager.Printf(lldb::eSeverityError,
                                  "Execution was interrupted, reason: %s.",
                                  error_desc);
      else
        diagnostic_manager.PutString(lldb::eSeverityError,
                                     "Execution was interrupted.");

      if ((execution_result == lldb::eExpressionInterrupted &&
           options.DoesUnwindOnError()) ||
          (execution_result == lldb::eExpressionHitBreakpoint &&
           options.DoesIgnoreBreakpoints()))
        diagnostic_manager.AppendMessageToDiagnostic(
            "The process has been returned to the state before expression "
            "evaluation.");
      else {
        if (execution_result == lldb::eExpressionHitBreakpoint)
          user_expression_plan->TransferExpressionOwnership();
        diagnostic_manager.AppendMessageToDiagnostic(
            "The process has been left at the point where it was "
            "interrupted, "
            "use \"thread return -x\" to return to the state before "
            "expression evaluation.");
      }

      return execution_result;
    } else if (execution_result == lldb::eExpressionStoppedForDebug) {
      diagnostic_manager.PutString(
          lldb::eSeverityInfo,
          "Execution was halted at the first instruction of the expression "
          "function because \"debug\" was requested.\n"
          "Use \"thread return -x\" to return to the state before expression "
          "evaluation.");
      return execution_result;
    } else if (execution_result == lldb::eExpressionThreadVanished) {
      diagnostic_manager.Printf(
          lldb::eSeverityError,
          "Couldn't complete execution; the thread "
          "on which the expression was being run: 0x%" PRIx64
          " exited during its execution.",
          expr_thread_id);
      return execution_result;
    } else if (execution_result != lldb::eExpressionCompleted) {
      diagnostic_manager.Printf(
          lldb::eSeverityError, "Couldn't execute function; result was %s",
          Process::ExecutionResultAsCString(execution_result));
      return execution_result;
    }
  }

  if (FinalizeJITExecution(diagnostic_manager, exe_ctx, result,
                           function_stack_bottom, function_stack_top)) {
    return lldb::eExpressionCompleted;
  } else {
    return lldb::eExpressionResultUnavailable;
  }
}

bool LLVMUserExpression::FinalizeJITExecution(
    DiagnosticManager &diagnostic_manager, ExecutionContext &exe_ctx,
    lldb::ExpressionVariableSP &result, lldb::addr_t function_stack_bottom,
    lldb::addr_t function_stack_top) {
  Log *log = GetLog(LLDBLog::Expressions);

  LLDB_LOGF(log, "-- [UserExpression::FinalizeJITExecution] Dematerializing "
                 "after execution --");

  if (!m_dematerializer_sp) {
    diagnostic_manager.Printf(lldb::eSeverityError,
                              "Couldn't apply expression side effects : no "
                              "dematerializer is present");
    return false;
  }

  Status dematerialize_error;

  m_dematerializer_sp->Dematerialize(dematerialize_error, function_stack_bottom,
                                     function_stack_top);

  if (!dematerialize_error.Success()) {
    diagnostic_manager.Printf(lldb::eSeverityError,
                              "Couldn't apply expression side effects : %s",
                              dematerialize_error.AsCString("unknown error"));
    return false;
  }

  result =
      GetResultAfterDematerialization(exe_ctx.GetBestExecutionContextScope());

  if (result)
    result->TransferAddress();

  m_dematerializer_sp.reset();

  return true;
}

bool LLVMUserExpression::PrepareToExecuteJITExpression(
    DiagnosticManager &diagnostic_manager, ExecutionContext &exe_ctx,
    lldb::addr_t &struct_address) {
  lldb::TargetSP target;
  lldb::ProcessSP process;
  lldb::StackFrameSP frame;

  if (!LockAndCheckContext(exe_ctx, target, process, frame)) {
    diagnostic_manager.PutString(
        lldb::eSeverityError,
        "The context has changed before we could JIT the expression!");
    return false;
  }

  if (m_jit_start_addr != LLDB_INVALID_ADDRESS || m_can_interpret) {
    if (m_materialized_address == LLDB_INVALID_ADDRESS) {
      Status alloc_error;

      IRMemoryMap::AllocationPolicy policy =
          m_can_interpret ? IRMemoryMap::eAllocationPolicyHostOnly
                          : IRMemoryMap::eAllocationPolicyMirror;

      const bool zero_memory = false;

      m_materialized_address = m_execution_unit_sp->Malloc(
          m_materializer_up->GetStructByteSize(),
          m_materializer_up->GetStructAlignment(),
          lldb::ePermissionsReadable | lldb::ePermissionsWritable, policy,
          zero_memory, alloc_error);

      if (!alloc_error.Success()) {
        diagnostic_manager.Printf(
            lldb::eSeverityError,
            "Couldn't allocate space for materialized struct: %s",
            alloc_error.AsCString());
        return false;
      }
    }

    struct_address = m_materialized_address;

    if (m_can_interpret && m_stack_frame_bottom == LLDB_INVALID_ADDRESS) {
      Status alloc_error;

      size_t stack_frame_size = target->GetExprAllocSize();
      if (stack_frame_size == 0) {
        ABISP abi_sp;
        if (process && (abi_sp = process->GetABI()))
          stack_frame_size = abi_sp->GetStackFrameSize();
        else
          stack_frame_size = 512 * 1024;
      }

      const bool zero_memory = false;

      m_stack_frame_bottom = m_execution_unit_sp->Malloc(
          stack_frame_size, 8,
          lldb::ePermissionsReadable | lldb::ePermissionsWritable,
          IRMemoryMap::eAllocationPolicyHostOnly, zero_memory, alloc_error);

      m_stack_frame_top = m_stack_frame_bottom + stack_frame_size;

      if (!alloc_error.Success()) {
        diagnostic_manager.Printf(
            lldb::eSeverityError,
            "Couldn't allocate space for the stack frame: %s",
            alloc_error.AsCString());
        return false;
      }
    }

    Status materialize_error;

    m_dematerializer_sp = m_materializer_up->Materialize(
        frame, *m_execution_unit_sp, struct_address, materialize_error);

    if (!materialize_error.Success()) {
      diagnostic_manager.Printf(lldb::eSeverityError,
                                "Couldn't materialize: %s",
                                materialize_error.AsCString());
      return false;
    }
  }
  return true;
}

