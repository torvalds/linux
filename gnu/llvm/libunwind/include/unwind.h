//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//
// C++ ABI Level 1 ABI documented at:
//   https://itanium-cxx-abi.github.io/cxx-abi/abi-eh.html
//
//===----------------------------------------------------------------------===//

#ifndef __UNWIND_H__
#define __UNWIND_H__

#include <__libunwind_config.h>

#include <stdint.h>
#include <stddef.h>

#if defined(__SEH__) && !defined(__USING_SJLJ_EXCEPTIONS__) && defined(_WIN32)
#include <windows.h>
#include <ntverp.h>
#endif

#if defined(__APPLE__)
#define LIBUNWIND_UNAVAIL __attribute__ (( unavailable ))
#else
#define LIBUNWIND_UNAVAIL
#endif

typedef enum {
  _URC_NO_REASON = 0,
  _URC_OK = 0,
  _URC_FOREIGN_EXCEPTION_CAUGHT = 1,
  _URC_FATAL_PHASE2_ERROR = 2,
  _URC_FATAL_PHASE1_ERROR = 3,
  _URC_NORMAL_STOP = 4,
  _URC_END_OF_STACK = 5,
  _URC_HANDLER_FOUND = 6,
  _URC_INSTALL_CONTEXT = 7,
  _URC_CONTINUE_UNWIND = 8,
#if defined(_LIBUNWIND_ARM_EHABI)
  _URC_FAILURE = 9
#endif
} _Unwind_Reason_Code;

typedef enum {
  _UA_SEARCH_PHASE = 1,
  _UA_CLEANUP_PHASE = 2,
  _UA_HANDLER_FRAME = 4,
  _UA_FORCE_UNWIND = 8,
  _UA_END_OF_STACK = 16 // gcc extension to C++ ABI
} _Unwind_Action;

typedef struct _Unwind_Context _Unwind_Context;   // opaque

#if defined(_LIBUNWIND_ARM_EHABI)
#include <unwind_arm_ehabi.h>
#else
#include <unwind_itanium.h>
#endif

typedef _Unwind_Reason_Code (*_Unwind_Stop_Fn)
    (int version,
     _Unwind_Action actions,
     _Unwind_Exception_Class exceptionClass,
     _Unwind_Exception* exceptionObject,
     struct _Unwind_Context* context,
     void* stop_parameter);

#ifdef __cplusplus
extern "C" {
#endif

extern uintptr_t _Unwind_GetRegionStart(struct _Unwind_Context *context);
extern uintptr_t
    _Unwind_GetLanguageSpecificData(struct _Unwind_Context *context);
#ifdef __USING_SJLJ_EXCEPTIONS__
extern _Unwind_Reason_Code
    _Unwind_SjLj_ForcedUnwind(_Unwind_Exception *exception_object,
                              _Unwind_Stop_Fn stop, void *stop_parameter);
#else
extern _Unwind_Reason_Code
    _Unwind_ForcedUnwind(_Unwind_Exception *exception_object,
                         _Unwind_Stop_Fn stop, void *stop_parameter);
#endif

#ifdef __USING_SJLJ_EXCEPTIONS__
typedef struct _Unwind_FunctionContext *_Unwind_FunctionContext_t;
extern void _Unwind_SjLj_Register(_Unwind_FunctionContext_t fc);
extern void _Unwind_SjLj_Unregister(_Unwind_FunctionContext_t fc);
#endif

//
// The following are semi-supported extensions to the C++ ABI
//

//
//  called by __cxa_rethrow().
//
#ifdef __USING_SJLJ_EXCEPTIONS__
extern _Unwind_Reason_Code
    _Unwind_SjLj_Resume_or_Rethrow(_Unwind_Exception *exception_object);
#else
extern _Unwind_Reason_Code
    _Unwind_Resume_or_Rethrow(_Unwind_Exception *exception_object);
#endif

// _Unwind_Backtrace() is a gcc extension that walks the stack and calls the
// _Unwind_Trace_Fn once per frame until it reaches the bottom of the stack
// or the _Unwind_Trace_Fn function returns something other than _URC_NO_REASON.
typedef _Unwind_Reason_Code (*_Unwind_Trace_Fn)(struct _Unwind_Context *,
                                                void *);
extern _Unwind_Reason_Code _Unwind_Backtrace(_Unwind_Trace_Fn, void *);

// _Unwind_GetCFA is a gcc extension that can be called from within a
// personality handler to get the CFA (stack pointer before call) of
// current frame.
extern uintptr_t _Unwind_GetCFA(struct _Unwind_Context *);


// _Unwind_GetIPInfo is a gcc extension that can be called from within a
// personality handler.  Similar to _Unwind_GetIP() but also returns in
// *ipBefore a non-zero value if the instruction pointer is at or before the
// instruction causing the unwind. Normally, in a function call, the IP returned
// is the return address which is after the call instruction and may be past the
// end of the function containing the call instruction.
extern uintptr_t _Unwind_GetIPInfo(struct _Unwind_Context *context,
                                   int *ipBefore);


// __register_frame() is used with dynamically generated code to register the
// FDE for a generated (JIT) code.  The FDE must use pc-rel addressing to point
// to its function and optional LSDA.
// __register_frame() has existed in all versions of Mac OS X, but in 10.4 and
// 10.5 it was buggy and did not actually register the FDE with the unwinder.
// In 10.6 and later it does register properly.
extern void __register_frame(const void *fde);
extern void __deregister_frame(const void *fde);

// _Unwind_Find_FDE() will locate the FDE if the pc is in some function that has
// an associated FDE. Note, Mac OS X 10.6 and later, introduces "compact unwind
// info" which the runtime uses in preference to DWARF unwind info.  This
// function will only work if the target function has an FDE but no compact
// unwind info.
struct dwarf_eh_bases {
  uintptr_t tbase;
  uintptr_t dbase;
  uintptr_t func;
};
extern const void *_Unwind_Find_FDE(const void *pc, struct dwarf_eh_bases *);


// This function attempts to find the start (address of first instruction) of
// a function given an address inside the function.  It only works if the
// function has an FDE (DWARF unwind info).
// This function is unimplemented on Mac OS X 10.6 and later.  Instead, use
// _Unwind_Find_FDE() and look at the dwarf_eh_bases.func result.
extern void *_Unwind_FindEnclosingFunction(void *pc);

// Mac OS X does not support text-rel and data-rel addressing so these functions
// are unimplemented.
extern uintptr_t _Unwind_GetDataRelBase(struct _Unwind_Context *context)
    LIBUNWIND_UNAVAIL;
extern uintptr_t _Unwind_GetTextRelBase(struct _Unwind_Context *context)
    LIBUNWIND_UNAVAIL;

// Mac OS X 10.4 and 10.5 had implementations of these functions in
// libgcc_s.dylib, but they never worked.
/// These functions are no longer available on Mac OS X.
extern void __register_frame_info_bases(const void *fde, void *ob, void *tb,
                                        void *db) LIBUNWIND_UNAVAIL;
extern void __register_frame_info(const void *fde, void *ob)
    LIBUNWIND_UNAVAIL;
extern void __register_frame_info_table_bases(const void *fde, void *ob,
                                              void *tb, void *db)
    LIBUNWIND_UNAVAIL;
extern void __register_frame_info_table(const void *fde, void *ob)
    LIBUNWIND_UNAVAIL;
extern void __register_frame_table(const void *fde)
    LIBUNWIND_UNAVAIL;
extern void *__deregister_frame_info(const void *fde)
    LIBUNWIND_UNAVAIL;
extern void *__deregister_frame_info_bases(const void *fde)
    LIBUNWIND_UNAVAIL;

#if defined(__SEH__) && !defined(__USING_SJLJ_EXCEPTIONS__)
#ifndef _WIN32
typedef struct _EXCEPTION_RECORD EXCEPTION_RECORD;
typedef struct _CONTEXT CONTEXT;
typedef struct _DISPATCHER_CONTEXT DISPATCHER_CONTEXT;
#elif !defined(__MINGW32__) && VER_PRODUCTBUILD < 8000
typedef struct _DISPATCHER_CONTEXT DISPATCHER_CONTEXT;
#endif
// This is the common wrapper for GCC-style personality functions with SEH.
extern EXCEPTION_DISPOSITION _GCC_specific_handler(EXCEPTION_RECORD *exc,
                                                   void *frame, CONTEXT *ctx,
                                                   DISPATCHER_CONTEXT *disp,
                                                   _Unwind_Personality_Fn pers);
#endif

#ifdef __cplusplus
}
#endif

#endif // __UNWIND_H__
