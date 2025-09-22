//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//
//  Implements Wasm exception handling proposal
//  (https://github.com/WebAssembly/exception-handling) based C++ exceptions
//
//===----------------------------------------------------------------------===//

#include <stdbool.h>

#include "config.h"

#ifdef __WASM_EXCEPTIONS__

#include "unwind.h"
#include <threads.h>

_Unwind_Reason_Code __gxx_personality_wasm0(int version, _Unwind_Action actions,
                                            uint64_t exceptionClass,
                                            _Unwind_Exception *unwind_exception,
                                            _Unwind_Context *context);

struct _Unwind_LandingPadContext {
  // Input information to personality function
  uintptr_t lpad_index; // landing pad index
  uintptr_t lsda;       // LSDA address

  // Output information computed by personality function
  uintptr_t selector; // selector value
};

// Communication channel between compiler-generated user code and personality
// function
thread_local struct _Unwind_LandingPadContext __wasm_lpad_context;

/// Calls to this function is in landing pads in compiler-generated user code.
/// In other EH schemes, stack unwinding is done by libunwind library, which
/// calls the personality function for each each frame it lands. On the other
/// hand, WebAssembly stack unwinding process is performed by a VM, and the
/// personality function cannot be called from there. So the compiler inserts
/// a call to this function in landing pads in the user code, which in turn
/// calls the personality function.
_Unwind_Reason_Code _Unwind_CallPersonality(void *exception_ptr) {
  struct _Unwind_Exception *exception_object =
      (struct _Unwind_Exception *)exception_ptr;
  _LIBUNWIND_TRACE_API("_Unwind_CallPersonality(exception_object=%p)",
                       (void *)exception_object);

  // Reset the selector.
  __wasm_lpad_context.selector = 0;

  // Call personality function. Wasm does not have two-phase unwinding, so we
  // only do the cleanup phase.
  return __gxx_personality_wasm0(
      1, _UA_SEARCH_PHASE, exception_object->exception_class, exception_object,
      (struct _Unwind_Context *)&__wasm_lpad_context);
}

/// Called by __cxa_throw.
_LIBUNWIND_EXPORT _Unwind_Reason_Code
_Unwind_RaiseException(_Unwind_Exception *exception_object) {
  _LIBUNWIND_TRACE_API("_Unwind_RaiseException(exception_object=%p)",
                       (void *)exception_object);
  // Use Wasm EH's 'throw' instruction.
  __builtin_wasm_throw(0, exception_object);
}

/// Called by __cxa_end_catch.
_LIBUNWIND_EXPORT void
_Unwind_DeleteException(_Unwind_Exception *exception_object) {
  _LIBUNWIND_TRACE_API("_Unwind_DeleteException(ex_obj=%p)",
                       (void *)(exception_object));
  if (exception_object->exception_cleanup != NULL)
    (*exception_object->exception_cleanup)(_URC_FOREIGN_EXCEPTION_CAUGHT,
                                           exception_object);
}

/// Called by personality handler to alter register values.
_LIBUNWIND_EXPORT void _Unwind_SetGR(struct _Unwind_Context *context, int index,
                                     uintptr_t value) {
  _LIBUNWIND_TRACE_API("_Unwind_SetGR(context=%p, index=%d, value=%lu)",
                       (void *)context, index, value);
  // We only use this function to set __wasm_lpad_context.selector field, which
  // is index 1 in the personality function.
  if (index == 1)
    ((struct _Unwind_LandingPadContext *)context)->selector = value;
}

/// Called by personality handler to get instruction pointer.
_LIBUNWIND_EXPORT uintptr_t _Unwind_GetIP(struct _Unwind_Context *context) {
  // The result will be used as an 1-based index after decrementing 1, so we
  // increment 2 here
  uintptr_t result =
      ((struct _Unwind_LandingPadContext *)context)->lpad_index + 2;
  _LIBUNWIND_TRACE_API("_Unwind_GetIP(context=%p) => %lu", (void *)context,
                       result);
  return result;
}

/// Not used in Wasm.
_LIBUNWIND_EXPORT void _Unwind_SetIP(struct _Unwind_Context *context,
                                     uintptr_t value) {}

/// Called by personality handler to get LSDA for current frame.
_LIBUNWIND_EXPORT uintptr_t
_Unwind_GetLanguageSpecificData(struct _Unwind_Context *context) {
  uintptr_t result = ((struct _Unwind_LandingPadContext *)context)->lsda;
  _LIBUNWIND_TRACE_API("_Unwind_GetLanguageSpecificData(context=%p) => 0x%lx",
                       (void *)context, result);
  return result;
}

/// Not used in Wasm.
_LIBUNWIND_EXPORT uintptr_t
_Unwind_GetRegionStart(struct _Unwind_Context *context) {
  return 0;
}

#endif // defined(__WASM_EXCEPTIONS__)
