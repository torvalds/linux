/* Backward compatibility unwind routines.
   Copyright (C) 2004, 2005, 2006
   Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   In addition to the permissions in the GNU General Public License, the
   Free Software Foundation gives you unlimited permission to link the
   compiled version of this file into combinations with other programs,
   and to distribute those combinations without any restriction coming
   from the use of this file.  (The General Public License restrictions
   do apply in other respects; for example, they cover modification of
   the file, and distribution when not linked into a combined
   executable.)

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.  */

#if defined (USE_GAS_SYMVER) && defined (USE_LIBUNWIND_EXCEPTIONS)
#include "tconfig.h"
#include "tsystem.h"
#include "unwind.h"
#include "unwind-dw2-fde.h"
#include "unwind-compat.h"

extern _Unwind_Reason_Code __libunwind_Unwind_Backtrace
  (_Unwind_Trace_Fn, void *);

_Unwind_Reason_Code
_Unwind_Backtrace (_Unwind_Trace_Fn trace, void *trace_argument)
{
  return __libunwind_Unwind_Backtrace (trace, trace_argument);
}
symver (_Unwind_Backtrace, GCC_3.3);

extern void __libunwind_Unwind_DeleteException
  (struct _Unwind_Exception *);

void
_Unwind_DeleteException (struct _Unwind_Exception *exc)
{
  return __libunwind_Unwind_DeleteException (exc);
}
symver (_Unwind_DeleteException, GCC_3.0);

extern void * __libunwind_Unwind_FindEnclosingFunction (void *);

void *
_Unwind_FindEnclosingFunction (void *pc)
{
  return __libunwind_Unwind_FindEnclosingFunction (pc);
}
symver (_Unwind_FindEnclosingFunction, GCC_3.3);

extern _Unwind_Reason_Code __libunwind_Unwind_ForcedUnwind
  (struct _Unwind_Exception *, _Unwind_Stop_Fn, void *);

_Unwind_Reason_Code
_Unwind_ForcedUnwind (struct _Unwind_Exception *exc,
		      _Unwind_Stop_Fn stop, void * stop_argument)
{
  return __libunwind_Unwind_ForcedUnwind (exc, stop, stop_argument);
}
symver (_Unwind_ForcedUnwind, GCC_3.0);

extern _Unwind_Word __libunwind_Unwind_GetCFA
  (struct _Unwind_Context *);

_Unwind_Word
_Unwind_GetCFA (struct _Unwind_Context *context)
{
  return __libunwind_Unwind_GetCFA (context);
}
symver (_Unwind_GetCFA, GCC_3.3);

#ifdef __ia64__
extern _Unwind_Word __libunwind_Unwind_GetBSP
  (struct _Unwind_Context *);

_Unwind_Word
_Unwind_GetBSP (struct _Unwind_Context * context)
{
  return __libunwind_Unwind_GetBSP (context);
}
symver (_Unwind_GetBSP, GCC_3.3.2);
#else
extern _Unwind_Ptr __libunwind_Unwind_GetDataRelBase
  (struct _Unwind_Context *);

_Unwind_Ptr
_Unwind_GetDataRelBase (struct _Unwind_Context *context)
{
  return __libunwind_Unwind_GetDataRelBase (context);
}
symver (_Unwind_GetDataRelBase, GCC_3.0);

extern _Unwind_Ptr __libunwind_Unwind_GetTextRelBase
  (struct _Unwind_Context *);

_Unwind_Ptr
_Unwind_GetTextRelBase (struct _Unwind_Context *context)
{
  return __libunwind_Unwind_GetTextRelBase (context);
}
symver (_Unwind_GetTextRelBase, GCC_3.0);
#endif

extern _Unwind_Word __libunwind_Unwind_GetGR
  (struct _Unwind_Context *, int );

_Unwind_Word
_Unwind_GetGR (struct _Unwind_Context *context, int index)
{
  return __libunwind_Unwind_GetGR (context, index);
}
symver (_Unwind_GetGR, GCC_3.0);

extern _Unwind_Ptr __libunwind_Unwind_GetIP (struct _Unwind_Context *);

_Unwind_Ptr
_Unwind_GetIP (struct _Unwind_Context *context)
{
  return __libunwind_Unwind_GetIP (context);
}
symver (_Unwind_GetIP, GCC_3.0);

_Unwind_Ptr
_Unwind_GetIPInfo (struct _Unwind_Context *context, int *ip_before_insn)
{
  *ip_before_insn = 0;
  return __libunwind_Unwind_GetIP (context);
}

extern void *__libunwind_Unwind_GetLanguageSpecificData
  (struct _Unwind_Context *);

void *
_Unwind_GetLanguageSpecificData (struct _Unwind_Context *context)
{
  return __libunwind_Unwind_GetLanguageSpecificData (context);
}
symver (_Unwind_GetLanguageSpecificData, GCC_3.0);

extern _Unwind_Ptr __libunwind_Unwind_GetRegionStart
  (struct _Unwind_Context *);

_Unwind_Ptr
_Unwind_GetRegionStart (struct _Unwind_Context *context)
{
  return __libunwind_Unwind_GetRegionStart (context);
}
symver (_Unwind_GetRegionStart, GCC_3.0);

extern _Unwind_Reason_Code __libunwind_Unwind_RaiseException
  (struct _Unwind_Exception *);

_Unwind_Reason_Code
_Unwind_RaiseException(struct _Unwind_Exception *exc)
{
  return __libunwind_Unwind_RaiseException (exc);
}
symver (_Unwind_RaiseException, GCC_3.0);

extern void __libunwind_Unwind_Resume (struct _Unwind_Exception *);

void
_Unwind_Resume (struct _Unwind_Exception *exc)
{
  __libunwind_Unwind_Resume (exc);
}
symver (_Unwind_Resume, GCC_3.0);

extern _Unwind_Reason_Code __libunwind_Unwind_Resume_or_Rethrow
   (struct _Unwind_Exception *);

_Unwind_Reason_Code
_Unwind_Resume_or_Rethrow (struct _Unwind_Exception *exc)
{
  return __libunwind_Unwind_Resume_or_Rethrow (exc);
}
symver (_Unwind_Resume_or_Rethrow, GCC_3.3);

extern void __libunwind_Unwind_SetGR
  (struct _Unwind_Context *, int, _Unwind_Word);

void
_Unwind_SetGR (struct _Unwind_Context *context, int index,
	       _Unwind_Word val) 
{
  __libunwind_Unwind_SetGR (context, index, val);
}
symver (_Unwind_SetGR, GCC_3.0);

extern void __libunwind_Unwind_SetIP
  (struct _Unwind_Context *, _Unwind_Ptr);

void
_Unwind_SetIP (struct _Unwind_Context *context, _Unwind_Ptr val)
{
  return __libunwind_Unwind_SetIP (context, val);
}
symver (_Unwind_SetIP, GCC_3.0);
#endif
