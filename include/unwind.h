/* $FreeBSD$ */

/*-
   libunwind - a platform-independent unwind library

   SPDX-License-Identifier: ISC

   Copyright (C) 2003 Hewlett-Packard Co
	Contributed by David Mosberger-Tang <davidm@hpl.hp.com>

This file is part of libunwind.

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.  */

#ifndef _UNWIND_H
#define _UNWIND_H

#include <sys/_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Minimal interface as per C++ ABI draft standard:

	http://www.codesourcery.com/cxx-abi/abi-eh.html */

typedef enum
  {
    _URC_NO_REASON = 0,
    _URC_FOREIGN_EXCEPTION_CAUGHT = 1,
    _URC_FATAL_PHASE2_ERROR = 2,
    _URC_FATAL_PHASE1_ERROR = 3,
    _URC_NORMAL_STOP = 4,
    _URC_END_OF_STACK = 5,
    _URC_HANDLER_FOUND = 6,
    _URC_INSTALL_CONTEXT = 7,
    _URC_CONTINUE_UNWIND = 8
  }
_Unwind_Reason_Code;

typedef int _Unwind_Action;

#define _UA_SEARCH_PHASE	1
#define _UA_CLEANUP_PHASE	2
#define _UA_HANDLER_FRAME	4
#define _UA_FORCE_UNWIND	8

struct _Unwind_Context;		/* opaque data-structure */
struct _Unwind_Exception;	/* forward-declaration */

typedef void (*_Unwind_Exception_Cleanup_Fn) (_Unwind_Reason_Code,
					      struct _Unwind_Exception *);

typedef _Unwind_Reason_Code (*_Unwind_Stop_Fn) (int, _Unwind_Action,
						__int64_t,
						struct _Unwind_Exception *,
						struct _Unwind_Context *,
						void *);

/* The C++ ABI requires exception_class, private_1, and private_2 to
   be of type uint64 and the entire structure to be
   double-word-aligned, but that seems a bit overly IA-64-specific.
   Using "unsigned long" instead should give us the desired effect on
   IA-64, while being more general.  */
struct _Unwind_Exception
  {
    __int64_t exception_class;
    _Unwind_Exception_Cleanup_Fn exception_cleanup;
    unsigned long private_1;
    unsigned long private_2;
  };

extern _Unwind_Reason_Code _Unwind_RaiseException (struct _Unwind_Exception *);
extern _Unwind_Reason_Code _Unwind_ForcedUnwind (struct _Unwind_Exception *,
						 _Unwind_Stop_Fn, void *);
extern void _Unwind_Resume (struct _Unwind_Exception *);
extern void _Unwind_DeleteException (struct _Unwind_Exception *);
extern unsigned long _Unwind_GetGR (struct _Unwind_Context *, int);
extern void _Unwind_SetGR (struct _Unwind_Context *, int, unsigned long);
extern unsigned long _Unwind_GetIP (struct _Unwind_Context *);
extern unsigned long _Unwind_GetIPInfo (struct _Unwind_Context *, int *);
extern void _Unwind_SetIP (struct _Unwind_Context *, unsigned long);
extern unsigned long _Unwind_GetLanguageSpecificData (struct _Unwind_Context*);
extern unsigned long _Unwind_GetRegionStart (struct _Unwind_Context *);

#if defined(_GNU_SOURCE) || defined(_BSD_SOURCE)

/* Callback for _Unwind_Backtrace().  The backtrace stops immediately
   if the callback returns any value other than _URC_NO_REASON. */
typedef _Unwind_Reason_Code (*_Unwind_Trace_Fn) (struct _Unwind_Context *,
						 void *);

/* See http://gcc.gnu.org/ml/gcc-patches/2001-09/msg00082.html for why
   _UA_END_OF_STACK exists.  */
# define _UA_END_OF_STACK	16

/* If the unwind was initiated due to a forced unwind, resume that
   operation, else re-raise the exception.  This is used by
   __cxa_rethrow().  */
extern _Unwind_Reason_Code
	  _Unwind_Resume_or_Rethrow (struct _Unwind_Exception *);

/* See http://gcc.gnu.org/ml/gcc-patches/2003-09/msg00154.html for why
   _Unwind_GetBSP() exists.  */
extern unsigned long _Unwind_GetBSP (struct _Unwind_Context *);

/* Return the "canonical frame address" for the given context.
   This is used by NPTL... */
extern unsigned long _Unwind_GetCFA (struct _Unwind_Context *);

/* Return the base-address for data references.  */
extern unsigned long _Unwind_GetDataRelBase (struct _Unwind_Context *);

/* Return the base-address for text references.  */
extern unsigned long _Unwind_GetTextRelBase (struct _Unwind_Context *);

/* Call _Unwind_Trace_Fn once for each stack-frame, without doing any
   cleanup.  The first frame for which the callback is invoked is the
   one for the caller of _Unwind_Backtrace().  _Unwind_Backtrace()
   returns _URC_END_OF_STACK when the backtrace stopped due to
   reaching the end of the call-chain or _URC_FATAL_PHASE1_ERROR if it
   stops for any other reason.  */
extern _Unwind_Reason_Code _Unwind_Backtrace (_Unwind_Trace_Fn, void *);

/* Find the start-address of the procedure containing the specified IP
   or NULL if it cannot be found (e.g., because the function has no
   unwind info).  Note: there is not necessarily a one-to-one
   correspondence between source-level functions and procedures: some
   functions don't have unwind-info and others are split into multiple
   procedures.  */
extern void *_Unwind_FindEnclosingFunction (void *);

/* See also Linux Standard Base Spec:
    http://www.linuxbase.org/spec/refspecs/LSB_1.3.0/gLSB/gLSB/libgcc-s.html */

#endif /* _GNU_SOURCE || _BSD_SOURCE */

#ifdef __cplusplus
};
#endif

#endif /* _UNWIND_H */
