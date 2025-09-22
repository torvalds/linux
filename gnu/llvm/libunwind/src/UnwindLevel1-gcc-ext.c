//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//
//  Implements gcc extensions to the C++ ABI Exception Handling Level 1.
//
//===----------------------------------------------------------------------===//

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "libunwind_ext.h"
#include "libunwind.h"
#include "Unwind-EHABI.h"
#include "unwind.h"

#if defined(_AIX)
#include <sys/debug.h>
#endif

#if defined(_LIBUNWIND_BUILD_ZERO_COST_APIS)

#if defined(_LIBUNWIND_SUPPORT_SEH_UNWIND)
#define PRIVATE_1 private_[0]
#elif defined(_LIBUNWIND_ARM_EHABI)
#define PRIVATE_1 unwinder_cache.reserved1
#else
#define PRIVATE_1 private_1
#endif

///  Called by __cxa_rethrow().
_LIBUNWIND_EXPORT _Unwind_Reason_Code
_Unwind_Resume_or_Rethrow(_Unwind_Exception *exception_object) {
  _LIBUNWIND_TRACE_API(
      "_Unwind_Resume_or_Rethrow(ex_obj=%p), private_1=%" PRIdPTR,
      (void *)exception_object, (intptr_t)exception_object->PRIVATE_1);

  // If this is non-forced and a stopping place was found, then this is a
  // re-throw.
  // Call _Unwind_RaiseException() as if this was a new exception
  if (exception_object->PRIVATE_1 == 0) {
    return _Unwind_RaiseException(exception_object);
    // Will return if there is no catch clause, so that __cxa_rethrow can call
    // std::terminate().
  }

  // Call through to _Unwind_Resume() which distinguishes between forced and
  // regular exceptions.
  _Unwind_Resume(exception_object);
  _LIBUNWIND_ABORT("_Unwind_Resume_or_Rethrow() called _Unwind_RaiseException()"
                   " which unexpectedly returned");
}

/// Called by personality handler during phase 2 to get base address for data
/// relative encodings.
_LIBUNWIND_EXPORT uintptr_t
_Unwind_GetDataRelBase(struct _Unwind_Context *context) {
  _LIBUNWIND_TRACE_API("_Unwind_GetDataRelBase(context=%p)", (void *)context);
#if defined(_AIX)
  return unw_get_data_rel_base((unw_cursor_t *)context);
#else
  (void)context;
  _LIBUNWIND_ABORT("_Unwind_GetDataRelBase() not implemented");
#endif
}

/// Called by personality handler during phase 2 to get base address for text
/// relative encodings.
_LIBUNWIND_EXPORT uintptr_t
_Unwind_GetTextRelBase(struct _Unwind_Context *context) {
  (void)context;
  _LIBUNWIND_TRACE_API("_Unwind_GetTextRelBase(context=%p)", (void *)context);
  _LIBUNWIND_ABORT("_Unwind_GetTextRelBase() not implemented");
}


/// Scans unwind information to find the function that contains the
/// specified code address "pc".
_LIBUNWIND_EXPORT void *_Unwind_FindEnclosingFunction(void *pc) {
  _LIBUNWIND_TRACE_API("_Unwind_FindEnclosingFunction(pc=%p)", pc);
#if defined(_AIX)
  if (pc == NULL)
    return NULL;

  // Get the start address of the enclosing function from the function's
  // traceback table.
  uint32_t *p = (uint32_t *)pc;

  // Keep looking forward until a word of 0 is found. The traceback
  // table starts at the following word.
  while (*p)
    ++p;
  struct tbtable *TBTable = (struct tbtable *)(p + 1);

  // Get the address of the traceback table extension.
  p = (uint32_t *)&TBTable->tb_ext;

  // Skip field parminfo if it exists.
  if (TBTable->tb.fixedparms || TBTable->tb.floatparms)
    ++p;

  if (TBTable->tb.has_tboff)
    // *p contains the offset from the function start to traceback table.
    return (void *)((uintptr_t)TBTable - *p - sizeof(uint32_t));
  return NULL;
#else
  // This is slow, but works.
  // We create an unwind cursor then alter the IP to be pc
  unw_cursor_t cursor;
  unw_context_t uc;
  unw_proc_info_t info;
  __unw_getcontext(&uc);
  __unw_init_local(&cursor, &uc);
  __unw_set_reg(&cursor, UNW_REG_IP, (unw_word_t)(intptr_t)pc);
  if (__unw_get_proc_info(&cursor, &info) == UNW_ESUCCESS)
    return (void *)(intptr_t) info.start_ip;
  else
    return NULL;
#endif
}

/// Walk every frame and call trace function at each one.  If trace function
/// returns anything other than _URC_NO_REASON, then walk is terminated.
_LIBUNWIND_EXPORT _Unwind_Reason_Code
_Unwind_Backtrace(_Unwind_Trace_Fn callback, void *ref) {
  unw_cursor_t cursor;
  unw_context_t uc;
  __unw_getcontext(&uc);
  __unw_init_local(&cursor, &uc);

  _LIBUNWIND_TRACE_API("_Unwind_Backtrace(callback=%p)",
                       (void *)(uintptr_t)callback);

#if defined(_LIBUNWIND_ARM_EHABI)
  // Create a mock exception object for force unwinding.
  _Unwind_Exception ex;
  memset(&ex, '\0', sizeof(ex));
  memcpy(&ex.exception_class, "CLNGUNW", sizeof(ex.exception_class));
#endif

  // walk each frame
  while (true) {
    _Unwind_Reason_Code result;

#if !defined(_LIBUNWIND_ARM_EHABI)
    // ask libunwind to get next frame (skip over first frame which is
    // _Unwind_Backtrace())
    if (__unw_step(&cursor) <= 0) {
      _LIBUNWIND_TRACE_UNWINDING(" _backtrace: ended because cursor reached "
                                 "bottom of stack, returning %d",
                                 _URC_END_OF_STACK);
      return _URC_END_OF_STACK;
    }
#else
    // Get the information for this frame.
    unw_proc_info_t frameInfo;
    if (__unw_get_proc_info(&cursor, &frameInfo) != UNW_ESUCCESS) {
      return _URC_END_OF_STACK;
    }

    // Update the pr_cache in the mock exception object.
    uint32_t *unwindInfo = (uint32_t *)frameInfo.unwind_info;
    ex.pr_cache.fnstart = frameInfo.start_ip;
    ex.pr_cache.ehtp = (_Unwind_EHT_Header *) unwindInfo;
    ex.pr_cache.additional= frameInfo.flags;

    struct _Unwind_Context *context = (struct _Unwind_Context *)&cursor;
    // Get and call the personality function to unwind the frame.
    _Unwind_Personality_Fn handler = (_Unwind_Personality_Fn)frameInfo.handler;
    if (handler == NULL) {
      return _URC_END_OF_STACK;
    }
    if (handler(_US_VIRTUAL_UNWIND_FRAME | _US_FORCE_UNWIND, &ex, context) !=
            _URC_CONTINUE_UNWIND) {
      return _URC_END_OF_STACK;
    }
#endif // defined(_LIBUNWIND_ARM_EHABI)

    // debugging
    if (_LIBUNWIND_TRACING_UNWINDING) {
      char functionName[512];
      unw_proc_info_t frame;
      unw_word_t offset;
      __unw_get_proc_name(&cursor, functionName, 512, &offset);
      __unw_get_proc_info(&cursor, &frame);
      _LIBUNWIND_TRACE_UNWINDING(
          " _backtrace: start_ip=0x%" PRIxPTR ", func=%s, lsda=0x%" PRIxPTR ", context=%p",
          frame.start_ip, functionName, frame.lsda,
          (void *)&cursor);
    }

    // call trace function with this frame
    result = (*callback)((struct _Unwind_Context *)(&cursor), ref);
    if (result != _URC_NO_REASON) {
      _LIBUNWIND_TRACE_UNWINDING(
          " _backtrace: ended because callback returned %d", result);
      return result;
    }
  }
}


/// Find DWARF unwind info for an address 'pc' in some function.
_LIBUNWIND_EXPORT const void *_Unwind_Find_FDE(const void *pc,
                                               struct dwarf_eh_bases *bases) {
  // This is slow, but works.
  // We create an unwind cursor then alter the IP to be pc
  unw_cursor_t cursor;
  unw_context_t uc;
  unw_proc_info_t info;
  __unw_getcontext(&uc);
  __unw_init_local(&cursor, &uc);
  __unw_set_reg(&cursor, UNW_REG_IP, (unw_word_t)(intptr_t)pc);
  __unw_get_proc_info(&cursor, &info);
  bases->tbase = (uintptr_t)info.extra;
  bases->dbase = 0; // dbase not used on Mac OS X
  bases->func = (uintptr_t)info.start_ip;
  _LIBUNWIND_TRACE_API("_Unwind_Find_FDE(pc=%p) => %p", pc,
                  (void *)(intptr_t) info.unwind_info);
  return (void *)(intptr_t) info.unwind_info;
}

/// Returns the CFA (call frame area, or stack pointer at start of function)
/// for the current context.
_LIBUNWIND_EXPORT uintptr_t _Unwind_GetCFA(struct _Unwind_Context *context) {
  unw_cursor_t *cursor = (unw_cursor_t *)context;
  unw_word_t result;
  __unw_get_reg(cursor, UNW_REG_SP, &result);
  _LIBUNWIND_TRACE_API("_Unwind_GetCFA(context=%p) => 0x%" PRIxPTR,
                       (void *)context, result);
  return (uintptr_t)result;
}


/// Called by personality handler during phase 2 to get instruction pointer.
/// ipBefore is a boolean that says if IP is already adjusted to be the call
/// site address.  Normally IP is the return address.
_LIBUNWIND_EXPORT uintptr_t _Unwind_GetIPInfo(struct _Unwind_Context *context,
                                              int *ipBefore) {
  _LIBUNWIND_TRACE_API("_Unwind_GetIPInfo(context=%p)", (void *)context);
  int isSignalFrame = __unw_is_signal_frame((unw_cursor_t *)context);
  // Negative means some kind of error (probably UNW_ENOINFO), but we have no
  // good way to report that, and this maintains backward compatibility with the
  // implementation that hard-coded zero in every case, even signal frames.
  if (isSignalFrame <= 0)
    *ipBefore = 0;
  else
    *ipBefore = 1;
  return _Unwind_GetIP(context);
}

#if defined(_LIBUNWIND_SUPPORT_DWARF_UNWIND)

/// Called by programs with dynamic code generators that want
/// to register a dynamically generated FDE.
/// This function has existed on Mac OS X since 10.4, but
/// was broken until 10.6.
_LIBUNWIND_EXPORT void __register_frame(const void *fde) {
  _LIBUNWIND_TRACE_API("__register_frame(%p)", fde);
  __unw_add_dynamic_fde((unw_word_t)(uintptr_t)fde);
}


/// Called by programs with dynamic code generators that want
/// to unregister a dynamically generated FDE.
/// This function has existed on Mac OS X since 10.4, but
/// was broken until 10.6.
_LIBUNWIND_EXPORT void __deregister_frame(const void *fde) {
  _LIBUNWIND_TRACE_API("__deregister_frame(%p)", fde);
  __unw_remove_dynamic_fde((unw_word_t)(uintptr_t)fde);
}


// The following register/deregister functions are gcc extensions.
// They have existed on Mac OS X, but have never worked because Mac OS X
// before 10.6 used keymgr to track known FDEs, but these functions
// never got updated to use keymgr.
// For now, we implement these as do-nothing functions to keep any existing
// applications working.  We also add the not in 10.6 symbol so that nwe
// application won't be able to use them.

#if defined(_LIBUNWIND_SUPPORT_FRAME_APIS)
_LIBUNWIND_EXPORT void __register_frame_info_bases(const void *fde, void *ob,
                                                   void *tb, void *db) {
  (void)fde;
  (void)ob;
  (void)tb;
  (void)db;
 _LIBUNWIND_TRACE_API("__register_frame_info_bases(%p,%p, %p, %p)",
                            fde, ob, tb, db);
  // do nothing, this function never worked in Mac OS X
}

_LIBUNWIND_EXPORT void __register_frame_info(const void *fde, void *ob) {
  (void)fde;
  (void)ob;
  _LIBUNWIND_TRACE_API("__register_frame_info(%p, %p)", fde, ob);
  // do nothing, this function never worked in Mac OS X
}

_LIBUNWIND_EXPORT void __register_frame_info_table_bases(const void *fde,
                                                         void *ob, void *tb,
                                                         void *db) {
  (void)fde;
  (void)ob;
  (void)tb;
  (void)db;
  _LIBUNWIND_TRACE_API("__register_frame_info_table_bases"
                             "(%p,%p, %p, %p)", fde, ob, tb, db);
  // do nothing, this function never worked in Mac OS X
}

_LIBUNWIND_EXPORT void __register_frame_info_table(const void *fde, void *ob) {
  (void)fde;
  (void)ob;
  _LIBUNWIND_TRACE_API("__register_frame_info_table(%p, %p)", fde, ob);
  // do nothing, this function never worked in Mac OS X
}

_LIBUNWIND_EXPORT void __register_frame_table(const void *fde) {
  (void)fde;
  _LIBUNWIND_TRACE_API("__register_frame_table(%p)", fde);
  // do nothing, this function never worked in Mac OS X
}

_LIBUNWIND_EXPORT void *__deregister_frame_info(const void *fde) {
  (void)fde;
  _LIBUNWIND_TRACE_API("__deregister_frame_info(%p)", fde);
  // do nothing, this function never worked in Mac OS X
  return NULL;
}

_LIBUNWIND_EXPORT void *__deregister_frame_info_bases(const void *fde) {
  (void)fde;
  _LIBUNWIND_TRACE_API("__deregister_frame_info_bases(%p)", fde);
  // do nothing, this function never worked in Mac OS X
  return NULL;
}
#endif // defined(_LIBUNWIND_SUPPORT_FRAME_APIS)

#endif // defined(_LIBUNWIND_SUPPORT_DWARF_UNWIND)

#endif // defined(_LIBUNWIND_BUILD_ZERO_COST_APIS)
