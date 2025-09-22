//===-- AppleGetQueuesHandler.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AppleGetQueuesHandler.h"

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

const char *AppleGetQueuesHandler::g_get_current_queues_function_name =
    "__lldb_backtrace_recording_get_current_queues";
const char *AppleGetQueuesHandler::g_get_current_queues_function_code =
    "                             \n\
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
    typedef void *introspection_dispatch_queue_info_t;                                                          \n\
                                                                                                                \n\
    extern uint64_t __introspection_dispatch_get_queues (queue_list_scope_t scope,                              \n\
                                                 introspection_dispatch_queue_info_t *returned_queues_buffer,   \n\
                                                 uint64_t *returned_queues_buffer_size);                        \n\
    extern int printf(const char *format, ...);                                                                 \n\
                                                                                                                \n\
    /*                                                                                                          \n\
     * return type define                                                                                       \n\
     */                                                                                                         \n\
                                                                                                                \n\
    struct get_current_queues_return_values                                                                     \n\
    {                                                                                                           \n\
        uint64_t queues_buffer_ptr;    /* the address of the queues buffer from libBacktraceRecording */        \n\
        uint64_t queues_buffer_size;   /* the size of the queues buffer from libBacktraceRecording */           \n\
        uint64_t count;                /* the number of queues included in the queues buffer */                 \n\
    };                                                                                                          \n\
                                                                                                                \n\
    void  __lldb_backtrace_recording_get_current_queues                                                         \n\
                                               (struct get_current_queues_return_values *return_buffer,         \n\
                                                int debug,                                                      \n\
                                                void *page_to_free,                                             \n\
                                                uint64_t page_to_free_size)                                     \n\
{                                                                                                               \n\
    if (debug)                                                                                                  \n\
      printf (\"entering get_current_queues with args %p, %d, 0x%p, 0x%llx\\n\", return_buffer, debug, page_to_free, page_to_free_size); \n\
    if (page_to_free != 0)                                                                                      \n\
    {                                                                                                           \n\
        mach_vm_deallocate (mach_task_self(), (mach_vm_address_t) page_to_free, (mach_vm_size_t) page_to_free_size); \n\
    }                                                                                                           \n\
                                                                                                                \n\
    return_buffer->count = __introspection_dispatch_get_queues (                                                \n\
                                                      /* QUEUES_WITH_ANY_ITEMS */ 2,                            \n\
                                                      (void**)&return_buffer->queues_buffer_ptr,                \n\
                                                      &return_buffer->queues_buffer_size);                      \n\
    if (debug)                                                                                                  \n\
        printf(\"result was count %lld\\n\", return_buffer->count);                                             \n\
}                                                                                                               \n\
}                                                                                                               \n\
";

AppleGetQueuesHandler::AppleGetQueuesHandler(Process *process)
    : m_process(process), m_get_queues_impl_code_up(),
      m_get_queues_function_mutex(),
      m_get_queues_return_buffer_addr(LLDB_INVALID_ADDRESS),
      m_get_queues_retbuffer_mutex() {}

AppleGetQueuesHandler::~AppleGetQueuesHandler() = default;

void AppleGetQueuesHandler::Detach() {

  if (m_process && m_process->IsAlive() &&
      m_get_queues_return_buffer_addr != LLDB_INVALID_ADDRESS) {
    std::unique_lock<std::mutex> lock(m_get_queues_retbuffer_mutex,
                                      std::defer_lock);
    (void)lock.try_lock(); // Even if we don't get the lock, deallocate the buffer
    m_process->DeallocateMemory(m_get_queues_return_buffer_addr);
  }
}

// Construct a CompilerType for the structure that
// g_get_current_queues_function_code will return by value so we can extract
// the fields after performing the function call. i.e. we are getting this
// struct returned to us:
//
//    struct get_current_queues_return_values
//    {
//        introspection_dispatch_queue_info_t *queues_buffer;
//        uint64_t queues_buffer_size;
//        uint64_t count;
//    };

// Compile our __lldb_backtrace_recording_get_current_queues() function (from
// the source above in g_get_current_queues_function_code) if we don't find
// that function in the inferior already with USE_BUILTIN_FUNCTION defined.
// (e.g. this would be the case for testing.)
//
// Insert the __lldb_backtrace_recording_get_current_queues into the inferior
// process if needed.
//
// Write the get_queues_arglist into the inferior's memory space to prepare for
// the call.
//
// Returns the address of the arguments written down in the inferior process,
// which can be used to make the function call.

lldb::addr_t
AppleGetQueuesHandler::SetupGetQueuesFunction(Thread &thread,
                                              ValueList &get_queues_arglist) {
  ThreadSP thread_sp(thread.shared_from_this());
  ExecutionContext exe_ctx(thread_sp);

  Address impl_code_address;
  DiagnosticManager diagnostics;
  Log *log = GetLog(LLDBLog::SystemRuntime);
  lldb::addr_t args_addr = LLDB_INVALID_ADDRESS;

  FunctionCaller *get_queues_caller = nullptr;

  // Scope for mutex locker:
  {
    std::lock_guard<std::mutex> guard(m_get_queues_function_mutex);

    // First stage is to make the ClangUtility to hold our injected function:

    if (!m_get_queues_impl_code_up) {
      if (g_get_current_queues_function_code != nullptr) {
        auto utility_fn_or_error = exe_ctx.GetTargetRef().CreateUtilityFunction(
            g_get_current_queues_function_code,
            g_get_current_queues_function_name, eLanguageTypeC, exe_ctx);
        if (!utility_fn_or_error) {
          LLDB_LOG_ERROR(log, utility_fn_or_error.takeError(),
                         "Failed to create UtilityFunction for queues "
                         "introspection: {0}.");
          return args_addr;
        }
        m_get_queues_impl_code_up = std::move(*utility_fn_or_error);
      } else {
        if (log) {
          LLDB_LOGF(log, "No queues introspection code found.");
          diagnostics.Dump(log);
        }
        return LLDB_INVALID_ADDRESS;
      }
    }

    // Next make the runner function for our implementation utility function.
    TypeSystemClangSP scratch_ts_sp =
        ScratchTypeSystemClang::GetForTarget(thread.GetProcess()->GetTarget());
    CompilerType get_queues_return_type =
        scratch_ts_sp->GetBasicType(eBasicTypeVoid).GetPointerType();
    Status error;
    get_queues_caller = m_get_queues_impl_code_up->MakeFunctionCaller(
        get_queues_return_type, get_queues_arglist, thread_sp, error);
    if (error.Fail() || get_queues_caller == nullptr) {
      LLDB_LOGF(log,
                "Could not get function caller for get-queues function: %s.",
                error.AsCString());
      return args_addr;
    }
  }

  diagnostics.Clear();

  // Now write down the argument values for this particular call.  This looks
  // like it might be a race condition if other threads were calling into here,
  // but actually it isn't because we allocate a new args structure for this
  // call by passing args_addr = LLDB_INVALID_ADDRESS...

  if (!get_queues_caller->WriteFunctionArguments(
          exe_ctx, args_addr, get_queues_arglist, diagnostics)) {
    if (log) {
      LLDB_LOGF(log, "Error writing get-queues function arguments.");
      diagnostics.Dump(log);
    }
    return args_addr;
  }

  return args_addr;
}

AppleGetQueuesHandler::GetQueuesReturnInfo
AppleGetQueuesHandler::GetCurrentQueues(Thread &thread, addr_t page_to_free,
                                        uint64_t page_to_free_size,
                                        Status &error) {
  lldb::StackFrameSP thread_cur_frame = thread.GetStackFrameAtIndex(0);
  ProcessSP process_sp(thread.CalculateProcess());
  TargetSP target_sp(thread.CalculateTarget());
  TypeSystemClangSP scratch_ts_sp =
      ScratchTypeSystemClang::GetForTarget(*target_sp);
  Log *log = GetLog(LLDBLog::SystemRuntime);

  GetQueuesReturnInfo return_value;
  return_value.queues_buffer_ptr = LLDB_INVALID_ADDRESS;
  return_value.queues_buffer_size = 0;
  return_value.count = 0;

  error.Clear();

  if (!thread.SafeToCallFunctions()) {
    LLDB_LOGF(log, "Not safe to call functions on thread 0x%" PRIx64,
              thread.GetID());
    error.SetErrorString("Not safe to call functions on this thread.");
    return return_value;
  }

  // Set up the arguments for a call to

  // struct get_current_queues_return_values
  // {
  //    uint64_t queues_buffer_ptr;    /* the address of the queues buffer from
  //    libBacktraceRecording */
  //    uint64_t queues_buffer_size;   /* the size of the queues buffer from
  //    libBacktraceRecording */
  //    uint64_t count;                /* the number of queues included in the
  //    queues buffer */
  // };
  //
  //  void
  //    __lldb_backtrace_recording_get_current_queues
  //                                         (struct
  //                                         get_current_queues_return_values
  //                                         *return_buffer,
  //                                          void *page_to_free,
  //                                          uint64_t page_to_free_size);

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

  Value page_to_free_value;
  page_to_free_value.SetValueType(Value::ValueType::Scalar);
  page_to_free_value.SetCompilerType(clang_void_ptr_type);

  CompilerType clang_uint64_type =
      scratch_ts_sp->GetBasicType(eBasicTypeUnsignedLongLong);
  Value page_to_free_size_value;
  page_to_free_size_value.SetValueType(Value::ValueType::Scalar);
  page_to_free_size_value.SetCompilerType(clang_uint64_type);

  std::lock_guard<std::mutex> guard(m_get_queues_retbuffer_mutex);
  if (m_get_queues_return_buffer_addr == LLDB_INVALID_ADDRESS) {
    addr_t bufaddr = process_sp->AllocateMemory(
        32, ePermissionsReadable | ePermissionsWritable, error);
    if (!error.Success() || bufaddr == LLDB_INVALID_ADDRESS) {
      LLDB_LOGF(log, "Failed to allocate memory for return buffer for get "
                     "current queues func call");
      return return_value;
    }
    m_get_queues_return_buffer_addr = bufaddr;
  }

  ValueList argument_values;

  return_buffer_ptr_value.GetScalar() = m_get_queues_return_buffer_addr;
  argument_values.PushValue(return_buffer_ptr_value);

  debug_value.GetScalar() = 0;
  argument_values.PushValue(debug_value);

  if (page_to_free != LLDB_INVALID_ADDRESS)
    page_to_free_value.GetScalar() = page_to_free;
  else
    page_to_free_value.GetScalar() = 0;
  argument_values.PushValue(page_to_free_value);

  page_to_free_size_value.GetScalar() = page_to_free_size;
  argument_values.PushValue(page_to_free_size_value);

  addr_t args_addr = SetupGetQueuesFunction(thread, argument_values);

  if (!m_get_queues_impl_code_up) {
    error.SetErrorString(
        "Unable to compile __introspection_dispatch_get_queues.");
    return return_value;
  }

  FunctionCaller *get_queues_caller =
      m_get_queues_impl_code_up->GetFunctionCaller();

  if (get_queues_caller == nullptr) {
    error.SetErrorString(
        "Unable to get caller for call __introspection_dispatch_get_queues");
    return return_value;
  }

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
  options.SetTryAllThreads(false);
  options.SetIsForUtilityExpr(true);
  thread.CalculateExecutionContext(exe_ctx);

  ExpressionResults func_call_ret;
  Value results;
  func_call_ret = get_queues_caller->ExecuteFunction(
      exe_ctx, &args_addr, options, diagnostics, results);
  if (func_call_ret != eExpressionCompleted || !error.Success()) {
    LLDB_LOGF(log,
              "Unable to call introspection_get_dispatch_queues(), got "
              "ExpressionResults %d, error contains %s",
              func_call_ret, error.AsCString(""));
    error.SetErrorString("Unable to call introspection_get_dispatch_queues() "
                         "for list of queues");
    return return_value;
  }

  return_value.queues_buffer_ptr = m_process->ReadUnsignedIntegerFromMemory(
      m_get_queues_return_buffer_addr, 8, LLDB_INVALID_ADDRESS, error);
  if (!error.Success() ||
      return_value.queues_buffer_ptr == LLDB_INVALID_ADDRESS) {
    return_value.queues_buffer_ptr = LLDB_INVALID_ADDRESS;
    return return_value;
  }

  return_value.queues_buffer_size = m_process->ReadUnsignedIntegerFromMemory(
      m_get_queues_return_buffer_addr + 8, 8, 0, error);

  if (!error.Success()) {
    return_value.queues_buffer_ptr = LLDB_INVALID_ADDRESS;
    return return_value;
  }

  return_value.count = m_process->ReadUnsignedIntegerFromMemory(
      m_get_queues_return_buffer_addr + 16, 8, 0, error);
  if (!error.Success()) {
    return_value.queues_buffer_ptr = LLDB_INVALID_ADDRESS;
    return return_value;
  }

  LLDB_LOGF(log,
            "AppleGetQueuesHandler called "
            "__introspection_dispatch_get_queues (page_to_free == "
            "0x%" PRIx64 ", size = %" PRId64 "), returned page is at 0x%" PRIx64
            ", size %" PRId64 ", count = %" PRId64,
            page_to_free, page_to_free_size, return_value.queues_buffer_ptr,
            return_value.queues_buffer_size, return_value.count);

  return return_value;
}
