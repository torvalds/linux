//===-- AppleGetItemInfoHandler.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AppleGetItemInfoHandler.h"

#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/Value.h"
#include "lldb/Expression/DiagnosticManager.h"
#include "lldb/Expression/FunctionCaller.h"
#include "lldb/Expression/UtilityFunction.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb;
using namespace lldb_private;

const char *AppleGetItemInfoHandler::g_get_item_info_function_name =
    "__lldb_backtrace_recording_get_item_info";
const char *AppleGetItemInfoHandler::g_get_item_info_function_code =
    "                                  \n\
extern \"C\"                                                                                                    \n\
{                                                                                                               \n\
    /*                                                                                                          \n\
     * mach defines                                                                                             \n\
     */                                                                                                         \n\
                                                                                                                \n\
    typedef unsigned int uint32_t;                                                                              \n\
    typedef unsigned long long uint64_t;                                                                        \n\
    typedef uint32_t mach_port_t;                                                                               \n\
    typedef mach_port_t vm_map_t;                                                                               \n\
    typedef int kern_return_t;                                                                                  \n\
    typedef uint64_t mach_vm_address_t;                                                                         \n\
    typedef uint64_t mach_vm_size_t;                                                                            \n\
                                                                                                                \n\
    mach_port_t mach_task_self ();                                                                              \n\
    kern_return_t mach_vm_deallocate (vm_map_t target, mach_vm_address_t address, mach_vm_size_t size);         \n\
                                                                                                                \n\
    /*                                                                                                          \n\
     * libBacktraceRecording defines                                                                            \n\
     */                                                                                                         \n\
                                                                                                                \n\
    typedef uint32_t queue_list_scope_t;                                                                        \n\
    typedef void *dispatch_queue_t;                                                                             \n\
    typedef void *introspection_dispatch_queue_info_t;                                                          \n\
    typedef void *introspection_dispatch_item_info_ref;                                                         \n\
                                                                                                                \n\
    extern uint64_t __introspection_dispatch_queue_item_get_info (introspection_dispatch_item_info_ref item_info_ref, \n\
                                                 introspection_dispatch_item_info_ref *returned_queues_buffer,  \n\
                                                 uint64_t *returned_queues_buffer_size);                        \n\
    extern int printf(const char *format, ...);                                                                 \n\
                                                                                                                \n\
    /*                                                                                                          \n\
     * return type define                                                                                       \n\
     */                                                                                                         \n\
                                                                                                                \n\
    struct get_item_info_return_values                                                                      \n\
    {                                                                                                           \n\
        uint64_t item_info_buffer_ptr;    /* the address of the items buffer from libBacktraceRecording */  \n\
        uint64_t item_info_buffer_size;   /* the size of the items buffer from libBacktraceRecording */     \n\
    };                                                                                                          \n\
                                                                                                                \n\
    void  __lldb_backtrace_recording_get_item_info                                                          \n\
                                               (struct get_item_info_return_values *return_buffer,          \n\
                                                int debug,                                                      \n\
                                                uint64_t /* introspection_dispatch_item_info_ref item_info_ref */ item, \n\
                                                void *page_to_free,                                             \n\
                                                uint64_t page_to_free_size)                                     \n\
{                                                                                                               \n\
    if (debug)                                                                                                  \n\
      printf (\"entering get_item_info with args return_buffer == %p, debug == %d, item == 0x%llx, page_to_free == %p, page_to_free_size == 0x%llx\\n\", return_buffer, debug, item, page_to_free, page_to_free_size); \n\
    if (page_to_free != 0)                                                                                      \n\
    {                                                                                                           \n\
        mach_vm_deallocate (mach_task_self(), (mach_vm_address_t) page_to_free, (mach_vm_size_t) page_to_free_size); \n\
    }                                                                                                           \n\
                                                                                                                \n\
    __introspection_dispatch_queue_item_get_info ((void*) item,                                                 \n\
                                                  (void**)&return_buffer->item_info_buffer_ptr,                 \n\
                                                  &return_buffer->item_info_buffer_size);                       \n\
}                                                                                                               \n\
}                                                                                                               \n\
";

AppleGetItemInfoHandler::AppleGetItemInfoHandler(Process *process)
    : m_process(process), m_get_item_info_impl_code(),
      m_get_item_info_function_mutex(),
      m_get_item_info_return_buffer_addr(LLDB_INVALID_ADDRESS),
      m_get_item_info_retbuffer_mutex() {}

AppleGetItemInfoHandler::~AppleGetItemInfoHandler() = default;

void AppleGetItemInfoHandler::Detach() {

  if (m_process && m_process->IsAlive() &&
      m_get_item_info_return_buffer_addr != LLDB_INVALID_ADDRESS) {
    std::unique_lock<std::mutex> lock(m_get_item_info_retbuffer_mutex,
                                      std::defer_lock);
    (void)lock.try_lock(); // Even if we don't get the lock, deallocate the buffer
    m_process->DeallocateMemory(m_get_item_info_return_buffer_addr);
  }
}

// Compile our __lldb_backtrace_recording_get_item_info() function (from the
// source above in g_get_item_info_function_code) if we don't find that
// function in the inferior already with USE_BUILTIN_FUNCTION defined.  (e.g.
// this would be the case for testing.)
//
// Insert the __lldb_backtrace_recording_get_item_info into the inferior
// process if needed.
//
// Write the get_item_info_arglist into the inferior's memory space to prepare
// for the call.
//
// Returns the address of the arguments written down in the inferior process,
// which can be used to make the function call.

lldb::addr_t AppleGetItemInfoHandler::SetupGetItemInfoFunction(
    Thread &thread, ValueList &get_item_info_arglist) {
  ExecutionContext exe_ctx(thread.shared_from_this());
  DiagnosticManager diagnostics;
  Log *log = GetLog(LLDBLog::SystemRuntime);
  lldb::addr_t args_addr = LLDB_INVALID_ADDRESS;
  FunctionCaller *get_item_info_caller = nullptr;

  // Scope for mutex locker:
  {
    std::lock_guard<std::mutex> guard(m_get_item_info_function_mutex);

    // First stage is to make the UtilityFunction to hold our injected
    // function:

    if (!m_get_item_info_impl_code) {
      if (g_get_item_info_function_code != nullptr) {
        auto utility_fn_or_error = exe_ctx.GetTargetRef().CreateUtilityFunction(
            g_get_item_info_function_code, g_get_item_info_function_name,
            eLanguageTypeObjC, exe_ctx);
        if (!utility_fn_or_error) {
          LLDB_LOG_ERROR(log, utility_fn_or_error.takeError(),
                         "Failed to create utility function: {0}");
        }
        m_get_item_info_impl_code = std::move(*utility_fn_or_error);
      } else {
        LLDB_LOGF(log, "No get-item-info introspection code found.");
        return LLDB_INVALID_ADDRESS;
      }

      // Next make the runner function for our implementation utility function.
      auto type_system_or_err =
          thread.GetProcess()->GetTarget().GetScratchTypeSystemForLanguage(
              eLanguageTypeC);
      if (auto err = type_system_or_err.takeError()) {
        LLDB_LOG_ERROR(log, std::move(err),
                       "Error inserting get-item-info function: {0}");
        return args_addr;
      }
      auto ts = *type_system_or_err;
      if (!ts)
        return args_addr;

      CompilerType get_item_info_return_type =
          ts->GetBasicTypeFromAST(eBasicTypeVoid)
              .GetPointerType();

      Status error;
      get_item_info_caller = m_get_item_info_impl_code->MakeFunctionCaller(
          get_item_info_return_type, get_item_info_arglist,
          thread.shared_from_this(), error);
      if (error.Fail() || get_item_info_caller == nullptr) {
        LLDB_LOGF(log, "Error inserting get-item-info function: \"%s\".",
                  error.AsCString());
        return args_addr;
      }
    } else {
      // If it's already made, then we can just retrieve the caller:
      get_item_info_caller = m_get_item_info_impl_code->GetFunctionCaller();
      if (!get_item_info_caller) {
        LLDB_LOGF(log, "Failed to get get-item-info introspection caller.");
        m_get_item_info_impl_code.reset();
        return args_addr;
      }
    }
  }

  diagnostics.Clear();

  // Now write down the argument values for this particular call.  This looks
  // like it might be a race condition if other threads were calling into here,
  // but actually it isn't because we allocate a new args structure for this
  // call by passing args_addr = LLDB_INVALID_ADDRESS...

  if (!get_item_info_caller->WriteFunctionArguments(
          exe_ctx, args_addr, get_item_info_arglist, diagnostics)) {
    if (log) {
      LLDB_LOGF(log, "Error writing get-item-info function arguments.");
      diagnostics.Dump(log);
    }

    return args_addr;
  }

  return args_addr;
}

AppleGetItemInfoHandler::GetItemInfoReturnInfo
AppleGetItemInfoHandler::GetItemInfo(Thread &thread, uint64_t item,
                                     addr_t page_to_free,
                                     uint64_t page_to_free_size,
                                     Status &error) {
  lldb::StackFrameSP thread_cur_frame = thread.GetStackFrameAtIndex(0);
  ProcessSP process_sp(thread.CalculateProcess());
  TargetSP target_sp(thread.CalculateTarget());
  TypeSystemClangSP scratch_ts_sp =
      ScratchTypeSystemClang::GetForTarget(*target_sp);
  Log *log = GetLog(LLDBLog::SystemRuntime);

  GetItemInfoReturnInfo return_value;
  return_value.item_buffer_ptr = LLDB_INVALID_ADDRESS;
  return_value.item_buffer_size = 0;

  error.Clear();

  if (!thread.SafeToCallFunctions()) {
    LLDB_LOGF(log, "Not safe to call functions on thread 0x%" PRIx64,
              thread.GetID());
    error.SetErrorString("Not safe to call functions on this thread.");
    return return_value;
  }

  // Set up the arguments for a call to

  // struct get_item_info_return_values
  // {
  //     uint64_t item_info_buffer_ptr;    /* the address of the items buffer
  //     from libBacktraceRecording */
  //     uint64_t item_info_buffer_size;   /* the size of the items buffer from
  //     libBacktraceRecording */
  // };
  //
  // void  __lldb_backtrace_recording_get_item_info
  //                                            (struct
  //                                            get_item_info_return_values
  //                                            *return_buffer,
  //                                             int debug,
  //                                             uint64_t item,
  //                                             void *page_to_free,
  //                                             uint64_t page_to_free_size)

  // Where the return_buffer argument points to a 24 byte region of memory
  // already allocated by lldb in the inferior process.

  CompilerType clang_void_ptr_type =
      scratch_ts_sp->GetBasicType(eBasicTypeVoid).GetPointerType();
  Value return_buffer_ptr_value;
  return_buffer_ptr_value.SetValueType(Value::ValueType::Scalar);
  return_buffer_ptr_value.SetCompilerType(clang_void_ptr_type);

  CompilerType clang_int_type = scratch_ts_sp->GetBasicType(eBasicTypeInt);
  Value debug_value;
  debug_value.SetValueType(Value::ValueType::Scalar);
  debug_value.SetCompilerType(clang_int_type);

  CompilerType clang_uint64_type =
      scratch_ts_sp->GetBasicType(eBasicTypeUnsignedLongLong);
  Value item_value;
  item_value.SetValueType(Value::ValueType::Scalar);
  item_value.SetCompilerType(clang_uint64_type);

  Value page_to_free_value;
  page_to_free_value.SetValueType(Value::ValueType::Scalar);
  page_to_free_value.SetCompilerType(clang_void_ptr_type);

  Value page_to_free_size_value;
  page_to_free_size_value.SetValueType(Value::ValueType::Scalar);
  page_to_free_size_value.SetCompilerType(clang_uint64_type);

  std::lock_guard<std::mutex> guard(m_get_item_info_retbuffer_mutex);
  if (m_get_item_info_return_buffer_addr == LLDB_INVALID_ADDRESS) {
    addr_t bufaddr = process_sp->AllocateMemory(
        32, ePermissionsReadable | ePermissionsWritable, error);
    if (!error.Success() || bufaddr == LLDB_INVALID_ADDRESS) {
      LLDB_LOGF(log, "Failed to allocate memory for return buffer for get "
                     "current queues func call");
      return return_value;
    }
    m_get_item_info_return_buffer_addr = bufaddr;
  }

  ValueList argument_values;

  return_buffer_ptr_value.GetScalar() = m_get_item_info_return_buffer_addr;
  argument_values.PushValue(return_buffer_ptr_value);

  debug_value.GetScalar() = 0;
  argument_values.PushValue(debug_value);

  item_value.GetScalar() = item;
  argument_values.PushValue(item_value);

  if (page_to_free != LLDB_INVALID_ADDRESS)
    page_to_free_value.GetScalar() = page_to_free;
  else
    page_to_free_value.GetScalar() = 0;
  argument_values.PushValue(page_to_free_value);

  page_to_free_size_value.GetScalar() = page_to_free_size;
  argument_values.PushValue(page_to_free_size_value);

  addr_t args_addr = SetupGetItemInfoFunction(thread, argument_values);

  DiagnosticManager diagnostics;
  ExecutionContext exe_ctx;
  EvaluateExpressionOptions options;
  options.SetUnwindOnError(true);
  options.SetIgnoreBreakpoints(true);
  options.SetStopOthers(true);
#if __has_feature(address_sanitizer)
  options.SetTimeout(process_sp->GetUtilityExpressionTimeout());
#else
  options.SetTimeout(std::chrono::milliseconds(500));
#endif
  options.SetTimeout(process_sp->GetUtilityExpressionTimeout());
  options.SetTryAllThreads(false);
  options.SetIsForUtilityExpr(true);
  thread.CalculateExecutionContext(exe_ctx);

  if (!m_get_item_info_impl_code) {
    error.SetErrorString("Unable to compile function to call "
                         "__introspection_dispatch_queue_item_get_info");
    return return_value;
  }

  ExpressionResults func_call_ret;
  Value results;
  FunctionCaller *func_caller = m_get_item_info_impl_code->GetFunctionCaller();
  if (!func_caller) {
    LLDB_LOGF(log, "Could not retrieve function caller for "
                   "__introspection_dispatch_queue_item_get_info.");
    error.SetErrorString("Could not retrieve function caller for "
                         "__introspection_dispatch_queue_item_get_info.");
    return return_value;
  }

  func_call_ret = func_caller->ExecuteFunction(exe_ctx, &args_addr, options,
                                               diagnostics, results);
  if (func_call_ret != eExpressionCompleted || !error.Success()) {
    LLDB_LOGF(log,
              "Unable to call "
              "__introspection_dispatch_queue_item_get_info(), got "
              "ExpressionResults %d, error contains %s",
              func_call_ret, error.AsCString(""));
    error.SetErrorString("Unable to call "
                         "__introspection_dispatch_queue_get_item_info() for "
                         "list of queues");
    return return_value;
  }

  return_value.item_buffer_ptr = m_process->ReadUnsignedIntegerFromMemory(
      m_get_item_info_return_buffer_addr, 8, LLDB_INVALID_ADDRESS, error);
  if (!error.Success() ||
      return_value.item_buffer_ptr == LLDB_INVALID_ADDRESS) {
    return_value.item_buffer_ptr = LLDB_INVALID_ADDRESS;
    return return_value;
  }

  return_value.item_buffer_size = m_process->ReadUnsignedIntegerFromMemory(
      m_get_item_info_return_buffer_addr + 8, 8, 0, error);

  if (!error.Success()) {
    return_value.item_buffer_ptr = LLDB_INVALID_ADDRESS;
    return return_value;
  }
  LLDB_LOGF(log,
            "AppleGetItemInfoHandler called "
            "__introspection_dispatch_queue_item_get_info (page_to_free == "
            "0x%" PRIx64 ", size = %" PRId64 "), returned page is at 0x%" PRIx64
            ", size %" PRId64,
            page_to_free, page_to_free_size, return_value.item_buffer_ptr,
            return_value.item_buffer_size);

  return return_value;
}
