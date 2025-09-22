//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//
//  Implements setjump-longjump based C++ exceptions
//
//===----------------------------------------------------------------------===//

#include <unwind.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "config.h"

/// With SJLJ based exceptions, any function that has a catch clause or needs to
/// do any clean up when an exception propagates through it, needs to call
/// \c _Unwind_SjLj_Register at the start of the function and
/// \c _Unwind_SjLj_Unregister at the end.  The register function is called with
/// the address of a block of memory in the function's stack frame.  The runtime
/// keeps a linked list (stack) of these blocks - one per thread.  The calling
/// function also sets the personality and lsda fields of the block.

#if defined(_LIBUNWIND_BUILD_SJLJ_APIS)

struct _Unwind_FunctionContext {
  // next function in stack of handlers
  struct _Unwind_FunctionContext *prev;

#if defined(__ve__)
  // VE requires to store 64 bit pointers in the buffer for SjLj exception.
  // We expand the size of values defined here.  This size must be matched
  // to the size returned by TargetMachine::getSjLjDataSize().

  // set by calling function before registering to be the landing pad
  uint64_t                        resumeLocation;

  // set by personality handler to be parameters passed to landing pad function
  uint64_t                        resumeParameters[4];
#else
  // set by calling function before registering to be the landing pad
  uint32_t                        resumeLocation;

  // set by personality handler to be parameters passed to landing pad function
  uint32_t                        resumeParameters[4];
#endif

  // set by calling function before registering
  _Unwind_Personality_Fn personality;          // arm offset=24
  uintptr_t                       lsda;        // arm offset=28

  // variable length array, contains registers to restore
  // 0 = r7, 1 = pc, 2 = sp
  void                           *jbuf[];
};

#if defined(_LIBUNWIND_HAS_NO_THREADS)
# define _LIBUNWIND_THREAD_LOCAL
#else
# if __STDC_VERSION__ >= 201112L
#  define _LIBUNWIND_THREAD_LOCAL _Thread_local
# elif defined(_MSC_VER)
#  define _LIBUNWIND_THREAD_LOCAL __declspec(thread)
# elif defined(__GNUC__) || defined(__clang__)
#  define _LIBUNWIND_THREAD_LOCAL __thread
# else
#  error Unable to create thread local storage
# endif
#endif


#if !defined(FOR_DYLD)

#if defined(__APPLE__)
#include <System/pthread_machdep.h>
#else
static _LIBUNWIND_THREAD_LOCAL struct _Unwind_FunctionContext *stack = NULL;
#endif

static struct _Unwind_FunctionContext *
__Unwind_SjLj_GetTopOfFunctionStack(void) {
#if defined(__APPLE__)
  return _pthread_getspecific_direct(__PTK_LIBC_DYLD_Unwind_SjLj_Key);
#else
  return stack;
#endif
}

static void
__Unwind_SjLj_SetTopOfFunctionStack(struct _Unwind_FunctionContext *fc) {
#if defined(__APPLE__)
  _pthread_setspecific_direct(__PTK_LIBC_DYLD_Unwind_SjLj_Key, fc);
#else
  stack = fc;
#endif
}

#endif


/// Called at start of each function that catches exceptions
_LIBUNWIND_EXPORT void
_Unwind_SjLj_Register(struct _Unwind_FunctionContext *fc) {
  fc->prev = __Unwind_SjLj_GetTopOfFunctionStack();
  __Unwind_SjLj_SetTopOfFunctionStack(fc);
}


/// Called at end of each function that catches exceptions
_LIBUNWIND_EXPORT void
_Unwind_SjLj_Unregister(struct _Unwind_FunctionContext *fc) {
  __Unwind_SjLj_SetTopOfFunctionStack(fc->prev);
}


static _Unwind_Reason_Code
unwind_phase1(struct _Unwind_Exception *exception_object) {
  _Unwind_FunctionContext_t c = __Unwind_SjLj_GetTopOfFunctionStack();
  _LIBUNWIND_TRACE_UNWINDING("unwind_phase1: initial function-context=%p",
                             (void *)c);

  // walk each frame looking for a place to stop
  for (bool handlerNotFound = true; handlerNotFound; c = c->prev) {

    // check for no more frames
    if (c == NULL) {
      _LIBUNWIND_TRACE_UNWINDING("unwind_phase1(ex_ojb=%p): reached "
                                 "bottom => _URC_END_OF_STACK",
                                 (void *)exception_object);
      return _URC_END_OF_STACK;
    }

    _LIBUNWIND_TRACE_UNWINDING("unwind_phase1: function-context=%p", (void *)c);
    // if there is a personality routine, ask it if it will want to stop at this
    // frame
    if (c->personality != NULL) {
      _LIBUNWIND_TRACE_UNWINDING("unwind_phase1(ex_ojb=%p): calling "
                                 "personality function %p",
                                 (void *)exception_object,
                                 (void *)c->personality);
      _Unwind_Reason_Code personalityResult = (*c->personality)(
          1, _UA_SEARCH_PHASE, exception_object->exception_class,
          exception_object, (struct _Unwind_Context *)c);
      switch (personalityResult) {
      case _URC_HANDLER_FOUND:
        // found a catch clause or locals that need destructing in this frame
        // stop search and remember function context
        handlerNotFound = false;
        exception_object->private_2 = (uintptr_t) c;
        _LIBUNWIND_TRACE_UNWINDING("unwind_phase1(ex_ojb=%p): "
                                   "_URC_HANDLER_FOUND",
                                   (void *)exception_object);
        return _URC_NO_REASON;

      case _URC_CONTINUE_UNWIND:
        _LIBUNWIND_TRACE_UNWINDING("unwind_phase1(ex_ojb=%p): "
                                   "_URC_CONTINUE_UNWIND",
                                   (void *)exception_object);
        // continue unwinding
        break;

      default:
        // something went wrong
        _LIBUNWIND_TRACE_UNWINDING(
            "unwind_phase1(ex_ojb=%p): _URC_FATAL_PHASE1_ERROR",
            (void *)exception_object);
        return _URC_FATAL_PHASE1_ERROR;
      }
    }
  }
  return _URC_NO_REASON;
}


static _Unwind_Reason_Code
unwind_phase2(struct _Unwind_Exception *exception_object) {
  _LIBUNWIND_TRACE_UNWINDING("unwind_phase2(ex_ojb=%p)",
                             (void *)exception_object);

  // walk each frame until we reach where search phase said to stop
  _Unwind_FunctionContext_t c = __Unwind_SjLj_GetTopOfFunctionStack();
  while (true) {
    _LIBUNWIND_TRACE_UNWINDING("unwind_phase2s(ex_ojb=%p): context=%p",
                               (void *)exception_object, (void *)c);

    // check for no more frames
    if (c == NULL) {
      _LIBUNWIND_TRACE_UNWINDING(
          "unwind_phase2(ex_ojb=%p): __unw_step() reached "
          "bottom => _URC_END_OF_STACK",
          (void *)exception_object);
      return _URC_END_OF_STACK;
    }

    // if there is a personality routine, tell it we are unwinding
    if (c->personality != NULL) {
      _Unwind_Action action = _UA_CLEANUP_PHASE;
      if ((uintptr_t) c == exception_object->private_2)
        action = (_Unwind_Action)(
            _UA_CLEANUP_PHASE |
            _UA_HANDLER_FRAME); // tell personality this was the frame it marked
                                // in phase 1
      _Unwind_Reason_Code personalityResult =
          (*c->personality)(1, action, exception_object->exception_class,
                            exception_object, (struct _Unwind_Context *)c);
      switch (personalityResult) {
      case _URC_CONTINUE_UNWIND:
        // continue unwinding
        _LIBUNWIND_TRACE_UNWINDING(
            "unwind_phase2(ex_ojb=%p): _URC_CONTINUE_UNWIND",
            (void *)exception_object);
        if ((uintptr_t) c == exception_object->private_2) {
          // phase 1 said we would stop at this frame, but we did not...
          _LIBUNWIND_ABORT("during phase1 personality function said it would "
                           "stop here, but now if phase2 it did not stop here");
        }
        break;
      case _URC_INSTALL_CONTEXT:
        _LIBUNWIND_TRACE_UNWINDING("unwind_phase2(ex_ojb=%p): "
                                   "_URC_INSTALL_CONTEXT, will resume at "
                                   "landing pad %p",
                                   (void *)exception_object, c->jbuf[1]);
        // personality routine says to transfer control to landing pad
        // we may get control back if landing pad calls _Unwind_Resume()
        __Unwind_SjLj_SetTopOfFunctionStack(c);
        __builtin_longjmp(c->jbuf, 1);
        // __unw_resume() only returns if there was an error
        return _URC_FATAL_PHASE2_ERROR;
      default:
        // something went wrong
        _LIBUNWIND_DEBUG_LOG("personality function returned unknown result %d",
                      personalityResult);
        return _URC_FATAL_PHASE2_ERROR;
      }
    }
    c = c->prev;
  }

  // clean up phase did not resume at the frame that the search phase said it
  // would
  return _URC_FATAL_PHASE2_ERROR;
}


static _Unwind_Reason_Code
unwind_phase2_forced(struct _Unwind_Exception *exception_object,
                     _Unwind_Stop_Fn stop, void *stop_parameter) {
  // walk each frame until we reach where search phase said to stop
  _Unwind_FunctionContext_t c = __Unwind_SjLj_GetTopOfFunctionStack();
  while (true) {

    // get next frame (skip over first which is _Unwind_RaiseException)
    if (c == NULL) {
      _LIBUNWIND_TRACE_UNWINDING(
          "unwind_phase2(ex_ojb=%p): __unw_step() reached "
          "bottom => _URC_END_OF_STACK",
          (void *)exception_object);
      return _URC_END_OF_STACK;
    }

    // call stop function at each frame
    _Unwind_Action action =
        (_Unwind_Action)(_UA_FORCE_UNWIND | _UA_CLEANUP_PHASE);
    _Unwind_Reason_Code stopResult =
        (*stop)(1, action, exception_object->exception_class, exception_object,
                (struct _Unwind_Context *)c, stop_parameter);
    _LIBUNWIND_TRACE_UNWINDING("unwind_phase2_forced(ex_ojb=%p): "
                               "stop function returned %d",
                               (void *)exception_object, stopResult);
    if (stopResult != _URC_NO_REASON) {
      _LIBUNWIND_TRACE_UNWINDING("unwind_phase2_forced(ex_ojb=%p): "
                                 "stopped by stop function",
                                 (void *)exception_object);
      return _URC_FATAL_PHASE2_ERROR;
    }

    // if there is a personality routine, tell it we are unwinding
    if (c->personality != NULL) {
      _Unwind_Personality_Fn p = (_Unwind_Personality_Fn)c->personality;
      _LIBUNWIND_TRACE_UNWINDING("unwind_phase2_forced(ex_ojb=%p): "
                                 "calling personality function %p",
                                 (void *)exception_object, (void *)p);
      _Unwind_Reason_Code personalityResult =
          (*p)(1, action, exception_object->exception_class, exception_object,
               (struct _Unwind_Context *)c);
      switch (personalityResult) {
      case _URC_CONTINUE_UNWIND:
        _LIBUNWIND_TRACE_UNWINDING("unwind_phase2_forced(ex_ojb=%p):  "
                                   "personality returned _URC_CONTINUE_UNWIND",
                                   (void *)exception_object);
        // destructors called, continue unwinding
        break;
      case _URC_INSTALL_CONTEXT:
        _LIBUNWIND_TRACE_UNWINDING("unwind_phase2_forced(ex_ojb=%p): "
                                   "personality returned _URC_INSTALL_CONTEXT",
                                   (void *)exception_object);
        // we may get control back if landing pad calls _Unwind_Resume()
        __Unwind_SjLj_SetTopOfFunctionStack(c);
        __builtin_longjmp(c->jbuf, 1);
        break;
      default:
        // something went wrong
        _LIBUNWIND_TRACE_UNWINDING("unwind_phase2_forced(ex_ojb=%p): "
                                   "personality returned %d, "
                                   "_URC_FATAL_PHASE2_ERROR",
                                   (void *)exception_object, personalityResult);
        return _URC_FATAL_PHASE2_ERROR;
      }
    }
    c = c->prev;
  }

  // call stop function one last time and tell it we've reached the end of the
  // stack
  _LIBUNWIND_TRACE_UNWINDING("unwind_phase2_forced(ex_ojb=%p): calling stop "
                             "function with _UA_END_OF_STACK",
                             (void *)exception_object);
  _Unwind_Action lastAction =
      (_Unwind_Action)(_UA_FORCE_UNWIND | _UA_CLEANUP_PHASE | _UA_END_OF_STACK);
  (*stop)(1, lastAction, exception_object->exception_class, exception_object,
          (struct _Unwind_Context *)c, stop_parameter);

  // clean up phase did not resume at the frame that the search phase said it
  // would
  return _URC_FATAL_PHASE2_ERROR;
}


/// Called by __cxa_throw.  Only returns if there is a fatal error
_LIBUNWIND_EXPORT _Unwind_Reason_Code
_Unwind_SjLj_RaiseException(struct _Unwind_Exception *exception_object) {
  _LIBUNWIND_TRACE_API("_Unwind_SjLj_RaiseException(ex_obj=%p)",
                       (void *)exception_object);

  // mark that this is a non-forced unwind, so _Unwind_Resume() can do the right
  // thing
  exception_object->private_1 = 0;
  exception_object->private_2 = 0;

  // phase 1: the search phase
  _Unwind_Reason_Code phase1 = unwind_phase1(exception_object);
  if (phase1 != _URC_NO_REASON)
    return phase1;

  // phase 2: the clean up phase
  return unwind_phase2(exception_object);
}



/// When _Unwind_RaiseException() is in phase2, it hands control
/// to the personality function at each frame.  The personality
/// may force a jump to a landing pad in that function, the landing
/// pad code may then call _Unwind_Resume() to continue with the
/// unwinding.  Note: the call to _Unwind_Resume() is from compiler
/// generated user code.  All other _Unwind_* routines are called
/// by the C++ runtime __cxa_* routines.
///
/// Re-throwing an exception is implemented by having the code call
/// __cxa_rethrow() which in turn calls _Unwind_Resume_or_Rethrow()
_LIBUNWIND_EXPORT void
_Unwind_SjLj_Resume(struct _Unwind_Exception *exception_object) {
  _LIBUNWIND_TRACE_API("_Unwind_SjLj_Resume(ex_obj=%p)",
                       (void *)exception_object);

  if (exception_object->private_1 != 0)
    unwind_phase2_forced(exception_object,
                         (_Unwind_Stop_Fn) exception_object->private_1,
                         (void *)exception_object->private_2);
  else
    unwind_phase2(exception_object);

  // clients assume _Unwind_Resume() does not return, so all we can do is abort.
  _LIBUNWIND_ABORT("_Unwind_SjLj_Resume() can't return");
}


///  Called by __cxa_rethrow().
_LIBUNWIND_EXPORT _Unwind_Reason_Code
_Unwind_SjLj_Resume_or_Rethrow(struct _Unwind_Exception *exception_object) {
  _LIBUNWIND_TRACE_API("__Unwind_SjLj_Resume_or_Rethrow(ex_obj=%p), "
                       "private_1=%" PRIuPTR,
                       (void *)exception_object, exception_object->private_1);
  // If this is non-forced and a stopping place was found, then this is a
  // re-throw.
  // Call _Unwind_RaiseException() as if this was a new exception.
  if (exception_object->private_1 == 0) {
    return _Unwind_SjLj_RaiseException(exception_object);
    // should return if there is no catch clause, so that __cxa_rethrow can call
    // std::terminate()
  }

  // Call through to _Unwind_Resume() which distinguishes between forced and
  // regular exceptions.
  _Unwind_SjLj_Resume(exception_object);
  _LIBUNWIND_ABORT("__Unwind_SjLj_Resume_or_Rethrow() called "
                    "_Unwind_SjLj_Resume() which unexpectedly returned");
}


/// Called by personality handler during phase 2 to get LSDA for current frame.
_LIBUNWIND_EXPORT uintptr_t
_Unwind_GetLanguageSpecificData(struct _Unwind_Context *context) {
  _Unwind_FunctionContext_t ufc = (_Unwind_FunctionContext_t) context;
  _LIBUNWIND_TRACE_API("_Unwind_GetLanguageSpecificData(context=%p) "
                       "=> 0x%" PRIuPTR,
                       (void *)context, ufc->lsda);
  return ufc->lsda;
}


/// Called by personality handler during phase 2 to get register values.
_LIBUNWIND_EXPORT uintptr_t _Unwind_GetGR(struct _Unwind_Context *context,
                                          int index) {
  _LIBUNWIND_TRACE_API("_Unwind_GetGR(context=%p, reg=%d)", (void *)context,
                       index);
  _Unwind_FunctionContext_t ufc = (_Unwind_FunctionContext_t) context;
  return ufc->resumeParameters[index];
}


/// Called by personality handler during phase 2 to alter register values.
_LIBUNWIND_EXPORT void _Unwind_SetGR(struct _Unwind_Context *context, int index,
                                     uintptr_t new_value) {
  _LIBUNWIND_TRACE_API("_Unwind_SetGR(context=%p, reg=%d, value=0x%" PRIxPTR
                       ")",
                       (void *)context, index, new_value);
  _Unwind_FunctionContext_t ufc = (_Unwind_FunctionContext_t) context;
  ufc->resumeParameters[index] = new_value;
}


/// Called by personality handler during phase 2 to get instruction pointer.
_LIBUNWIND_EXPORT uintptr_t _Unwind_GetIP(struct _Unwind_Context *context) {
  _Unwind_FunctionContext_t ufc = (_Unwind_FunctionContext_t) context;
  _LIBUNWIND_TRACE_API("_Unwind_GetIP(context=%p) => 0x%" PRIxPTR,
                       (void *)context, ufc->resumeLocation + 1);
  return ufc->resumeLocation + 1;
}


/// Called by personality handler during phase 2 to get instruction pointer.
/// ipBefore is a boolean that says if IP is already adjusted to be the call
/// site address.  Normally IP is the return address.
_LIBUNWIND_EXPORT uintptr_t _Unwind_GetIPInfo(struct _Unwind_Context *context,
                                              int *ipBefore) {
  _Unwind_FunctionContext_t ufc = (_Unwind_FunctionContext_t) context;
  *ipBefore = 0;
  _LIBUNWIND_TRACE_API("_Unwind_GetIPInfo(context=%p, %p) => 0x%" PRIxPTR,
                       (void *)context, (void *)ipBefore,
                       ufc->resumeLocation + 1);
  return ufc->resumeLocation + 1;
}


/// Called by personality handler during phase 2 to alter instruction pointer.
_LIBUNWIND_EXPORT void _Unwind_SetIP(struct _Unwind_Context *context,
                                     uintptr_t new_value) {
  _LIBUNWIND_TRACE_API("_Unwind_SetIP(context=%p, value=0x%" PRIxPTR ")",
                       (void *)context, new_value);
  _Unwind_FunctionContext_t ufc = (_Unwind_FunctionContext_t) context;
  ufc->resumeLocation = new_value - 1;
}


/// Called by personality handler during phase 2 to find the start of the
/// function.
_LIBUNWIND_EXPORT uintptr_t
_Unwind_GetRegionStart(struct _Unwind_Context *context) {
  // Not supported or needed for sjlj based unwinding
  (void)context;
  _LIBUNWIND_TRACE_API("_Unwind_GetRegionStart(context=%p)", (void *)context);
  return 0;
}


/// Called by personality handler during phase 2 if a foreign exception
/// is caught.
_LIBUNWIND_EXPORT void
_Unwind_DeleteException(struct _Unwind_Exception *exception_object) {
  _LIBUNWIND_TRACE_API("_Unwind_DeleteException(ex_obj=%p)",
                       (void *)exception_object);
  if (exception_object->exception_cleanup != NULL)
    (*exception_object->exception_cleanup)(_URC_FOREIGN_EXCEPTION_CAUGHT,
                                           exception_object);
}



/// Called by personality handler during phase 2 to get base address for data
/// relative encodings.
_LIBUNWIND_EXPORT uintptr_t
_Unwind_GetDataRelBase(struct _Unwind_Context *context) {
  // Not supported or needed for sjlj based unwinding
  (void)context;
  _LIBUNWIND_TRACE_API("_Unwind_GetDataRelBase(context=%p)", (void *)context);
  _LIBUNWIND_ABORT("_Unwind_GetDataRelBase() not implemented");
}


/// Called by personality handler during phase 2 to get base address for text
/// relative encodings.
_LIBUNWIND_EXPORT uintptr_t
_Unwind_GetTextRelBase(struct _Unwind_Context *context) {
  // Not supported or needed for sjlj based unwinding
  (void)context;
  _LIBUNWIND_TRACE_API("_Unwind_GetTextRelBase(context=%p)", (void *)context);
  _LIBUNWIND_ABORT("_Unwind_GetTextRelBase() not implemented");
}


/// Called by personality handler to get "Call Frame Area" for current frame.
_LIBUNWIND_EXPORT uintptr_t _Unwind_GetCFA(struct _Unwind_Context *context) {
  _LIBUNWIND_TRACE_API("_Unwind_GetCFA(context=%p)", (void *)context);
  if (context != NULL) {
    _Unwind_FunctionContext_t ufc = (_Unwind_FunctionContext_t) context;
    // Setjmp/longjmp based exceptions don't have a true CFA.
    // Instead, the SP in the jmpbuf is the closest approximation.
    return (uintptr_t) ufc->jbuf[2];
  }
  return 0;
}

#endif // defined(_LIBUNWIND_BUILD_SJLJ_APIS)
