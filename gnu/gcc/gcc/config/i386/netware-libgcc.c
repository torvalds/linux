/* Startup code for libgcc_s.nlm, necessary because we can't allow
   libgcc_s to use libc's malloc & Co., which associate allocations
   with the NLM owning the current (application) thread.
   Contributed by Jan Beulich (jbeulich@novell.com)
   Copyright (C) 2004 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#include <netware.h>
#include <stddef.h>
#include <stdlib.h>
#include <windows.h>

static rtag_t allocRTag;

BOOL
DllMain (HINSTANCE libraryId __attribute__ ((__unused__)),
	 DWORD reason, void *hModule)
{
  switch (reason)
    {
    case DLL_NLM_STARTUP:
      allocRTag = AllocateResourceTag (hModule,
				       "libgcc memory", AllocSignature);
      return allocRTag != NULL;
    case DLL_NLM_SHUTDOWN:
      /* This does not recover resources associated with the tag...
         ReturnResourceTag (allocRTag, 0); */
      break;
    }
  return 1;
}

void *
malloc (size_t size)
{
  return AllocSleepOK (size, allocRTag, NULL);
}

void
free (void *ptr)
{
  Free (ptr);
}
