//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//
// Implements C++ ABI Exception Handling Level 1 as documented at:
//      https://itanium-cxx-abi.github.io/cxx-abi/abi-eh.html
// using libunwind
//
//===----------------------------------------------------------------------===//

// ARM EHABI does not specify _Unwind_{Get,Set}{GR,IP}().  Thus, we are
// defining inline functions to delegate the function calls to
// _Unwind_VRS_{Get,Set}().  However, some applications might declare the
// function protetype directly (instead of including <unwind.h>), thus we need
// to export these functions from libunwind.so as well.
#define _LIBUNWIND_UNWIND_LEVEL1_EXTERNAL_LINKAGE 1

#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cet_unwind.h"
#include "config.h"
#include "libunwind.h"
#include "libunwind_ext.h"
#include "unwind.h"

#if !defined(_LIBUNWIND_ARM_EHABI) && !defined(__USING_SJLJ_EXCEPTIONS__) &&   \
    !defined(__wasm__)

#ifndef _LIBUNWIND_SUPPORT_SEH_UNWIND

// When CET is enabled, each "call" instruction will push return address to
// CET shadow stack, each "ret" instruction will pop current CET shadow stack
// top and compare it with target address which program will return.
// In exception handing, some stack frames will be skipped before jumping to
// landing pad and we must adjust CET shadow stack accordingly.
// _LIBUNWIND_POP_CET_SSP is used to adjust CET shadow stack pointer and we
// directly jump to __libunwind_Registers_x86/x86_64_jumpto instead of using
// a regular function call to avoid pushing to CET shadow stack again.
#if !defined(_LIBUNWIND_USE_CET) && !defined(_LIBUNWIND_USE_GCS)
#define __unw_phase2_resume(cursor, fn)                                        \
  do {                                                                         \
    (void)fn;                                                                  \
    __unw_resume((cursor));                                                    \
  } while (0)
#elif defined(_LIBUNWIND_TARGET_I386)
#define __cet_ss_step_size 4
#define __unw_phase2_resume(cursor, fn)                                        \
  do {                                                                         \
    _LIBUNWIND_POP_CET_SSP((fn));                                              \
    void *cetRegContext = __libunwind_cet_get_registers((cursor));             \
    void *cetJumpAddress = __libunwind_cet_get_jump_target();                  \
    __asm__ volatile("push %%edi\n\t"                                          \
                     "sub $4, %%esp\n\t"                                       \
                     "jmp *%%edx\n\t" :: "D"(cetRegContext),                   \
                     "d"(cetJumpAddress));                                     \
  } while (0)
#elif defined(_LIBUNWIND_TARGET_X86_64)
#define __cet_ss_step_size 8
#define __unw_phase2_resume(cursor, fn)                                        \
  do {                                                                         \
    _LIBUNWIND_POP_CET_SSP((fn));                                              \
    void *cetRegContext = __libunwind_cet_get_registers((cursor));             \
    void *cetJumpAddress = __libunwind_cet_get_jump_target();                  \
    __asm__ volatile("jmpq *%%rdx\n\t" :: "D"(cetRegContext),                  \
                     "d"(cetJumpAddress));                                     \
  } while (0)
#elif defined(_LIBUNWIND_TARGET_AARCH64)
#define __cet_ss_step_size 8
#define __unw_phase2_resume(cursor, fn)                                        \
  do {                                                                         \
    _LIBUNWIND_POP_CET_SSP((fn));                                              \
    void *cetRegContext = __libunwind_cet_get_registers((cursor));             \
    void *cetJumpAddress = __libunwind_cet_get_jump_target();                  \
    __asm__ volatile("mov x0, %0\n\t"                                          \
                     "br %1\n\t"                                               \
                     :                                                         \
                     : "r"(cetRegContext), "r"(cetJumpAddress)                 \
                     : "x0");                                                  \
  } while (0)
#endif

static _Unwind_Reason_Code
unwind_phase1(unw_context_t *uc, unw_cursor_t *cursor, _Unwind_Exception *exception_object) {
  __unw_init_local(cursor, uc);

  // Walk each frame looking for a place to stop.
  while (true) {
    // Ask libunwind to get next frame (skip over first which is
    // _Unwind_RaiseException).
    int stepResult = __unw_step(cursor);
    if (stepResult == 0) {
      _LIBUNWIND_TRACE_UNWINDING(
          "unwind_phase1(ex_obj=%p): __unw_step() reached "
          "bottom => _URC_END_OF_STACK",
          (void *)exception_object);
      return _URC_END_OF_STACK;
    } else if (stepResult < 0) {
      _LIBUNWIND_TRACE_UNWINDING(
          "unwind_phase1(ex_obj=%p): __unw_step failed => "
          "_URC_FATAL_PHASE1_ERROR",
          (void *)exception_object);
      return _URC_FATAL_PHASE1_ERROR;
    }

    // See if frame has code to run (has personality routine).
    unw_proc_info_t frameInfo;
    unw_word_t sp;
    if (__unw_get_proc_info(cursor, &frameInfo) != UNW_ESUCCESS) {
      _LIBUNWIND_TRACE_UNWINDING(
          "unwind_phase1(ex_obj=%p): __unw_get_proc_info "
          "failed => _URC_FATAL_PHASE1_ERROR",
          (void *)exception_object);
      return _URC_FATAL_PHASE1_ERROR;
    }

#ifndef NDEBUG
    // When tracing, print state information.
    if (_LIBUNWIND_TRACING_UNWINDING) {
      char functionBuf[512];
      const char *functionName = functionBuf;
      unw_word_t offset;
      if ((__unw_get_proc_name(cursor, functionBuf, sizeof(functionBuf),
                               &offset) != UNW_ESUCCESS) ||
          (frameInfo.start_ip + offset > frameInfo.end_ip))
        functionName = ".anonymous.";
      unw_word_t pc;
      __unw_get_reg(cursor, UNW_REG_IP, &pc);
      _LIBUNWIND_TRACE_UNWINDING(
          "unwind_phase1(ex_obj=%p): pc=0x%" PRIxPTR ", start_ip=0x%" PRIxPTR
          ", func=%s, lsda=0x%" PRIxPTR ", personality=0x%" PRIxPTR "",
          (void *)exception_object, pc, frameInfo.start_ip, functionName,
          frameInfo.lsda, frameInfo.handler);
    }
#endif

    // If there is a personality routine, ask it if it will want to stop at
    // this frame.
    if (frameInfo.handler != 0) {
      _Unwind_Personality_Fn p =
          (_Unwind_Personality_Fn)(uintptr_t)(frameInfo.handler);
      _LIBUNWIND_TRACE_UNWINDING(
          "unwind_phase1(ex_obj=%p): calling personality function %p",
          (void *)exception_object, (void *)(uintptr_t)p);
      _Unwind_Reason_Code personalityResult =
          (*p)(1, _UA_SEARCH_PHASE, exception_object->exception_class,
               exception_object, (struct _Unwind_Context *)(cursor));
      switch (personalityResult) {
      case _URC_HANDLER_FOUND:
        // found a catch clause or locals that need destructing in this frame
        // stop search and remember stack pointer at the frame
        __unw_get_reg(cursor, UNW_REG_SP, &sp);
        exception_object->private_2 = (uintptr_t)sp;
        _LIBUNWIND_TRACE_UNWINDING(
            "unwind_phase1(ex_obj=%p): _URC_HANDLER_FOUND",
            (void *)exception_object);
        return _URC_NO_REASON;

      case _URC_CONTINUE_UNWIND:
        _LIBUNWIND_TRACE_UNWINDING(
            "unwind_phase1(ex_obj=%p): _URC_CONTINUE_UNWIND",
            (void *)exception_object);
        // continue unwinding
        break;

      default:
        // something went wrong
        _LIBUNWIND_TRACE_UNWINDING(
            "unwind_phase1(ex_obj=%p): _URC_FATAL_PHASE1_ERROR",
            (void *)exception_object);
        return _URC_FATAL_PHASE1_ERROR;
      }
    }
  }
  return _URC_NO_REASON;
}
extern int __unw_step_stage2(unw_cursor_t *);

#if defined(_LIBUNWIND_USE_GCS)
// Enable the GCS target feature to permit gcspop instructions to be used.
__attribute__((target("gcs")))
#endif
static _Unwind_Reason_Code
unwind_phase2(unw_context_t *uc, unw_cursor_t *cursor, _Unwind_Exception *exception_object) {
  __unw_init_local(cursor, uc);

  _LIBUNWIND_TRACE_UNWINDING("unwind_phase2(ex_obj=%p)",
                             (void *)exception_object);

  // uc is initialized by __unw_getcontext in the parent frame. The first stack
  // frame walked is unwind_phase2.
  unsigned framesWalked = 1;
#if defined(_LIBUNWIND_USE_CET)
  unsigned long shadowStackTop = _get_ssp();
#elif defined(_LIBUNWIND_USE_GCS)
  unsigned long shadowStackTop = 0;
  if (__chkfeat(_CHKFEAT_GCS))
    shadowStackTop = (unsigned long)__gcspr();
#endif
  // Walk each frame until we reach where search phase said to stop.
  while (true) {

    // Ask libunwind to get next frame (skip over first which is
    // _Unwind_RaiseException).
    int stepResult = __unw_step_stage2(cursor);
    if (stepResult == 0) {
      _LIBUNWIND_TRACE_UNWINDING(
          "unwind_phase2(ex_obj=%p): __unw_step_stage2() reached "
          "bottom => _URC_END_OF_STACK",
          (void *)exception_object);
      return _URC_END_OF_STACK;
    } else if (stepResult < 0) {
      _LIBUNWIND_TRACE_UNWINDING(
          "unwind_phase2(ex_obj=%p): __unw_step_stage2 failed => "
          "_URC_FATAL_PHASE1_ERROR",
          (void *)exception_object);
      return _URC_FATAL_PHASE2_ERROR;
    }

    // Get info about this frame.
    unw_word_t sp;
    unw_proc_info_t frameInfo;
    __unw_get_reg(cursor, UNW_REG_SP, &sp);
    if (__unw_get_proc_info(cursor, &frameInfo) != UNW_ESUCCESS) {
      _LIBUNWIND_TRACE_UNWINDING(
          "unwind_phase2(ex_obj=%p): __unw_get_proc_info "
          "failed => _URC_FATAL_PHASE1_ERROR",
          (void *)exception_object);
      return _URC_FATAL_PHASE2_ERROR;
    }

#ifndef NDEBUG
    // When tracing, print state information.
    if (_LIBUNWIND_TRACING_UNWINDING) {
      char functionBuf[512];
      const char *functionName = functionBuf;
      unw_word_t offset;
      if ((__unw_get_proc_name(cursor, functionBuf, sizeof(functionBuf),
                               &offset) != UNW_ESUCCESS) ||
          (frameInfo.start_ip + offset > frameInfo.end_ip))
        functionName = ".anonymous.";
      _LIBUNWIND_TRACE_UNWINDING("unwind_phase2(ex_obj=%p): start_ip=0x%" PRIxPTR
                                 ", func=%s, sp=0x%" PRIxPTR ", lsda=0x%" PRIxPTR
                                 ", personality=0x%" PRIxPTR,
                                 (void *)exception_object, frameInfo.start_ip,
                                 functionName, sp, frameInfo.lsda,
                                 frameInfo.handler);
    }
#endif

// In CET enabled environment, we check return address stored in normal stack
// against return address stored in CET shadow stack, if the 2 addresses don't
// match, it means return address in normal stack has been corrupted, we return
// _URC_FATAL_PHASE2_ERROR.
#if defined(_LIBUNWIND_USE_CET) || defined(_LIBUNWIND_USE_GCS)
    if (shadowStackTop != 0) {
      unw_word_t retInNormalStack;
      __unw_get_reg(cursor, UNW_REG_IP, &retInNormalStack);
      unsigned long retInShadowStack = *(
          unsigned long *)(shadowStackTop + __cet_ss_step_size * framesWalked);
      if (retInNormalStack != retInShadowStack)
        return _URC_FATAL_PHASE2_ERROR;
    }
#endif
    ++framesWalked;
    // If there is a personality routine, tell it we are unwinding.
    if (frameInfo.handler != 0) {
      _Unwind_Personality_Fn p =
          (_Unwind_Personality_Fn)(uintptr_t)(frameInfo.handler);
      _Unwind_Action action = _UA_CLEANUP_PHASE;
      if (sp == exception_object->private_2) {
        // Tell personality this was the frame it marked in phase 1.
        action = (_Unwind_Action)(_UA_CLEANUP_PHASE | _UA_HANDLER_FRAME);
      }
       _Unwind_Reason_Code personalityResult =
          (*p)(1, action, exception_object->exception_class, exception_object,
               (struct _Unwind_Context *)(cursor));
      switch (personalityResult) {
      case _URC_CONTINUE_UNWIND:
        // Continue unwinding
        _LIBUNWIND_TRACE_UNWINDING(
            "unwind_phase2(ex_obj=%p): _URC_CONTINUE_UNWIND",
            (void *)exception_object);
        if (sp == exception_object->private_2) {
          // Phase 1 said we would stop at this frame, but we did not...
          _LIBUNWIND_ABORT("during phase1 personality function said it would "
                           "stop here, but now in phase2 it did not stop here");
        }
        break;
      case _URC_INSTALL_CONTEXT:
        _LIBUNWIND_TRACE_UNWINDING(
            "unwind_phase2(ex_obj=%p): _URC_INSTALL_CONTEXT",
            (void *)exception_object);
        // Personality routine says to transfer control to landing pad.
        // We may get control back if landing pad calls _Unwind_Resume().
        if (_LIBUNWIND_TRACING_UNWINDING) {
          unw_word_t pc;
          __unw_get_reg(cursor, UNW_REG_IP, &pc);
          __unw_get_reg(cursor, UNW_REG_SP, &sp);
          _LIBUNWIND_TRACE_UNWINDING("unwind_phase2(ex_obj=%p): re-entering "
                                     "user code with ip=0x%" PRIxPTR
                                     ", sp=0x%" PRIxPTR,
                                     (void *)exception_object, pc, sp);
        }

        __unw_phase2_resume(cursor, framesWalked);
        // __unw_phase2_resume() only returns if there was an error.
        return _URC_FATAL_PHASE2_ERROR;
      default:
        // Personality routine returned an unknown result code.
        _LIBUNWIND_DEBUG_LOG("personality function returned unknown result %d",
                             personalityResult);
        return _URC_FATAL_PHASE2_ERROR;
      }
    }
  }

  // Clean up phase did not resume at the frame that the search phase
  // said it would...
  return _URC_FATAL_PHASE2_ERROR;
}

#if defined(_LIBUNWIND_USE_GCS)
// Enable the GCS target feature to permit gcspop instructions to be used.
__attribute__((target("gcs")))
#endif
static _Unwind_Reason_Code
unwind_phase2_forced(unw_context_t *uc, unw_cursor_t *cursor,
                     _Unwind_Exception *exception_object,
                     _Unwind_Stop_Fn stop, void *stop_parameter) {
  __unw_init_local(cursor, uc);

  // uc is initialized by __unw_getcontext in the parent frame. The first stack
  // frame walked is unwind_phase2_forced.
  unsigned framesWalked = 1;
  // Walk each frame until we reach where search phase said to stop
  while (__unw_step_stage2(cursor) > 0) {

    // Update info about this frame.
    unw_proc_info_t frameInfo;
    if (__unw_get_proc_info(cursor, &frameInfo) != UNW_ESUCCESS) {
      _LIBUNWIND_TRACE_UNWINDING(
          "unwind_phase2_forced(ex_obj=%p): __unw_get_proc_info "
          "failed => _URC_END_OF_STACK",
          (void *)exception_object);
      return _URC_FATAL_PHASE2_ERROR;
    }

#ifndef NDEBUG
    // When tracing, print state information.
    if (_LIBUNWIND_TRACING_UNWINDING) {
      char functionBuf[512];
      const char *functionName = functionBuf;
      unw_word_t offset;
      if ((__unw_get_proc_name(cursor, functionBuf, sizeof(functionBuf),
                               &offset) != UNW_ESUCCESS) ||
          (frameInfo.start_ip + offset > frameInfo.end_ip))
        functionName = ".anonymous.";
      _LIBUNWIND_TRACE_UNWINDING(
          "unwind_phase2_forced(ex_obj=%p): start_ip=0x%" PRIxPTR
          ", func=%s, lsda=0x%" PRIxPTR ", personality=0x%" PRIxPTR,
          (void *)exception_object, frameInfo.start_ip, functionName,
          frameInfo.lsda, frameInfo.handler);
    }
#endif

    // Call stop function at each frame.
    _Unwind_Action action =
        (_Unwind_Action)(_UA_FORCE_UNWIND | _UA_CLEANUP_PHASE);
    _Unwind_Reason_Code stopResult =
        (*stop)(1, action, exception_object->exception_class, exception_object,
                (struct _Unwind_Context *)(cursor), stop_parameter);
    _LIBUNWIND_TRACE_UNWINDING(
        "unwind_phase2_forced(ex_obj=%p): stop function returned %d",
        (void *)exception_object, stopResult);
    if (stopResult != _URC_NO_REASON) {
      _LIBUNWIND_TRACE_UNWINDING(
          "unwind_phase2_forced(ex_obj=%p): stopped by stop function",
          (void *)exception_object);
      return _URC_FATAL_PHASE2_ERROR;
    }

    ++framesWalked;
    // If there is a personality routine, tell it we are unwinding.
    if (frameInfo.handler != 0) {
      _Unwind_Personality_Fn p =
          (_Unwind_Personality_Fn)(intptr_t)(frameInfo.handler);
      _LIBUNWIND_TRACE_UNWINDING(
          "unwind_phase2_forced(ex_obj=%p): calling personality function %p",
          (void *)exception_object, (void *)(uintptr_t)p);
      _Unwind_Reason_Code personalityResult =
          (*p)(1, action, exception_object->exception_class, exception_object,
               (struct _Unwind_Context *)(cursor));
      switch (personalityResult) {
      case _URC_CONTINUE_UNWIND:
        _LIBUNWIND_TRACE_UNWINDING("unwind_phase2_forced(ex_obj=%p): "
                                   "personality returned "
                                   "_URC_CONTINUE_UNWIND",
                                   (void *)exception_object);
        // Destructors called, continue unwinding
        break;
      case _URC_INSTALL_CONTEXT:
        _LIBUNWIND_TRACE_UNWINDING("unwind_phase2_forced(ex_obj=%p): "
                                   "personality returned "
                                   "_URC_INSTALL_CONTEXT",
                                   (void *)exception_object);
        // We may get control back if landing pad calls _Unwind_Resume().
        __unw_phase2_resume(cursor, framesWalked);
        break;
      default:
        // Personality routine returned an unknown result code.
        _LIBUNWIND_TRACE_UNWINDING("unwind_phase2_forced(ex_obj=%p): "
                                   "personality returned %d, "
                                   "_URC_FATAL_PHASE2_ERROR",
                                   (void *)exception_object, personalityResult);
        return _URC_FATAL_PHASE2_ERROR;
      }
    }
  }

  // Call stop function one last time and tell it we've reached the end
  // of the stack.
  _LIBUNWIND_TRACE_UNWINDING("unwind_phase2_forced(ex_obj=%p): calling stop "
                             "function with _UA_END_OF_STACK",
                             (void *)exception_object);
  _Unwind_Action lastAction =
      (_Unwind_Action)(_UA_FORCE_UNWIND | _UA_CLEANUP_PHASE | _UA_END_OF_STACK);
  (*stop)(1, lastAction, exception_object->exception_class, exception_object,
          (struct _Unwind_Context *)(cursor), stop_parameter);

  // Clean up phase did not resume at the frame that the search phase said it
  // would.
  return _URC_FATAL_PHASE2_ERROR;
}


/// Called by __cxa_throw.  Only returns if there is a fatal error.
_LIBUNWIND_EXPORT _Unwind_Reason_Code
_Unwind_RaiseException(_Unwind_Exception *exception_object) {
  _LIBUNWIND_TRACE_API("_Unwind_RaiseException(ex_obj=%p)",
                       (void *)exception_object);
  unw_context_t uc;
  unw_cursor_t cursor;
  __unw_getcontext(&uc);

  // Mark that this is a non-forced unwind, so _Unwind_Resume()
  // can do the right thing.
  exception_object->private_1 = 0;
  exception_object->private_2 = 0;

  // phase 1: the search phase
  _Unwind_Reason_Code phase1 = unwind_phase1(&uc, &cursor, exception_object);
  if (phase1 != _URC_NO_REASON)
    return phase1;

  // phase 2: the clean up phase
  return unwind_phase2(&uc, &cursor, exception_object);
}



/// When _Unwind_RaiseException() is in phase2, it hands control
/// to the personality function at each frame.  The personality
/// may force a jump to a landing pad in that function, the landing
/// pad code may then call _Unwind_Resume() to continue with the
/// unwinding.  Note: the call to _Unwind_Resume() is from compiler
/// generated user code.  All other _Unwind_* routines are called
/// by the C++ runtime __cxa_* routines.
///
/// Note: re-throwing an exception (as opposed to continuing the unwind)
/// is implemented by having the code call __cxa_rethrow() which
/// in turn calls _Unwind_Resume_or_Rethrow().
_LIBUNWIND_EXPORT void
_Unwind_Resume(_Unwind_Exception *exception_object) {
  _LIBUNWIND_TRACE_API("_Unwind_Resume(ex_obj=%p)", (void *)exception_object);
  unw_context_t uc;
  unw_cursor_t cursor;
  __unw_getcontext(&uc);

  if (exception_object->private_1 != 0)
    unwind_phase2_forced(&uc, &cursor, exception_object,
                         (_Unwind_Stop_Fn) exception_object->private_1,
                         (void *)exception_object->private_2);
  else
    unwind_phase2(&uc, &cursor, exception_object);

  // Clients assume _Unwind_Resume() does not return, so all we can do is abort.
  _LIBUNWIND_ABORT("_Unwind_Resume() can't return");
}



/// Not used by C++.
/// Unwinds stack, calling "stop" function at each frame.
/// Could be used to implement longjmp().
_LIBUNWIND_EXPORT _Unwind_Reason_Code
_Unwind_ForcedUnwind(_Unwind_Exception *exception_object,
                     _Unwind_Stop_Fn stop, void *stop_parameter) {
  _LIBUNWIND_TRACE_API("_Unwind_ForcedUnwind(ex_obj=%p, stop=%p)",
                       (void *)exception_object, (void *)(uintptr_t)stop);
  unw_context_t uc;
  unw_cursor_t cursor;
  __unw_getcontext(&uc);

  // Mark that this is a forced unwind, so _Unwind_Resume() can do
  // the right thing.
  exception_object->private_1 = (uintptr_t) stop;
  exception_object->private_2 = (uintptr_t) stop_parameter;

  // do it
  return unwind_phase2_forced(&uc, &cursor, exception_object, stop, stop_parameter);
}


/// Called by personality handler during phase 2 to get LSDA for current frame.
_LIBUNWIND_EXPORT uintptr_t
_Unwind_GetLanguageSpecificData(struct _Unwind_Context *context) {
  unw_cursor_t *cursor = (unw_cursor_t *)context;
  unw_proc_info_t frameInfo;
  uintptr_t result = 0;
  if (__unw_get_proc_info(cursor, &frameInfo) == UNW_ESUCCESS)
    result = (uintptr_t)frameInfo.lsda;
  _LIBUNWIND_TRACE_API(
      "_Unwind_GetLanguageSpecificData(context=%p) => 0x%" PRIxPTR,
      (void *)context, result);
#if !defined(_LIBUNWIND_SUPPORT_TBTAB_UNWIND)
  if (result != 0) {
    if (*((uint8_t *)result) != 0xFF)
      _LIBUNWIND_DEBUG_LOG("lsda at 0x%" PRIxPTR " does not start with 0xFF",
                           result);
  }
#endif
  return result;
}


/// Called by personality handler during phase 2 to find the start of the
/// function.
_LIBUNWIND_EXPORT uintptr_t
_Unwind_GetRegionStart(struct _Unwind_Context *context) {
  unw_cursor_t *cursor = (unw_cursor_t *)context;
  unw_proc_info_t frameInfo;
  uintptr_t result = 0;
  if (__unw_get_proc_info(cursor, &frameInfo) == UNW_ESUCCESS)
    result = (uintptr_t)frameInfo.start_ip;
  _LIBUNWIND_TRACE_API("_Unwind_GetRegionStart(context=%p) => 0x%" PRIxPTR,
                       (void *)context, result);
  return result;
}

#endif // !_LIBUNWIND_SUPPORT_SEH_UNWIND

/// Called by personality handler during phase 2 if a foreign exception
// is caught.
_LIBUNWIND_EXPORT void
_Unwind_DeleteException(_Unwind_Exception *exception_object) {
  _LIBUNWIND_TRACE_API("_Unwind_DeleteException(ex_obj=%p)",
                       (void *)exception_object);
  if (exception_object->exception_cleanup != NULL)
    (*exception_object->exception_cleanup)(_URC_FOREIGN_EXCEPTION_CAUGHT,
                                           exception_object);
}

/// Called by personality handler during phase 2 to get register values.
_LIBUNWIND_EXPORT uintptr_t
_Unwind_GetGR(struct _Unwind_Context *context, int index) {
  unw_cursor_t *cursor = (unw_cursor_t *)context;
  unw_word_t result;
  __unw_get_reg(cursor, index, &result);
  _LIBUNWIND_TRACE_API("_Unwind_GetGR(context=%p, reg=%d) => 0x%" PRIxPTR,
                       (void *)context, index, result);
  return (uintptr_t)result;
}

/// Called by personality handler during phase 2 to alter register values.
_LIBUNWIND_EXPORT void _Unwind_SetGR(struct _Unwind_Context *context, int index,
                                     uintptr_t value) {
  _LIBUNWIND_TRACE_API("_Unwind_SetGR(context=%p, reg=%d, value=0x%0" PRIxPTR
                       ")",
                       (void *)context, index, value);
  unw_cursor_t *cursor = (unw_cursor_t *)context;
  __unw_set_reg(cursor, index, value);
}

/// Called by personality handler during phase 2 to get instruction pointer.
_LIBUNWIND_EXPORT uintptr_t _Unwind_GetIP(struct _Unwind_Context *context) {
  unw_cursor_t *cursor = (unw_cursor_t *)context;
  unw_word_t result;
  __unw_get_reg(cursor, UNW_REG_IP, &result);
  _LIBUNWIND_TRACE_API("_Unwind_GetIP(context=%p) => 0x%" PRIxPTR,
                       (void *)context, result);
  return (uintptr_t)result;
}

/// Called by personality handler during phase 2 to alter instruction pointer,
/// such as setting where the landing pad is, so _Unwind_Resume() will
/// start executing in the landing pad.
_LIBUNWIND_EXPORT void _Unwind_SetIP(struct _Unwind_Context *context,
                                     uintptr_t value) {
  _LIBUNWIND_TRACE_API("_Unwind_SetIP(context=%p, value=0x%0" PRIxPTR ")",
                       (void *)context, value);
  unw_cursor_t *cursor = (unw_cursor_t *)context;
  __unw_set_reg(cursor, UNW_REG_IP, value);
}

#endif // !defined(_LIBUNWIND_ARM_EHABI) && !defined(__USING_SJLJ_EXCEPTIONS__)
