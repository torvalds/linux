/* mingw32 host-specific hook definitions.
   Copyright (C) 2004 Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 2, or (at your
   option) any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to the
   Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
   MA 02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "hosthooks.h"
#include "hosthooks-def.h"
#include "toplev.h"
#include "diagnostic.h"


#define WIN32_LEAN_AND_MEAN  /* Not so important if we have windows.h.gch.  */
#include <windows.h>

static void * mingw32_gt_pch_get_address (size_t, int);
static int mingw32_gt_pch_use_address (void *, size_t, int, size_t);
static size_t mingw32_gt_pch_alloc_granularity (void);

#undef HOST_HOOKS_GT_PCH_GET_ADDRESS
#define HOST_HOOKS_GT_PCH_GET_ADDRESS mingw32_gt_pch_get_address
#undef HOST_HOOKS_GT_PCH_USE_ADDRESS
#define HOST_HOOKS_GT_PCH_USE_ADDRESS mingw32_gt_pch_use_address
#undef HOST_HOOKS_GT_PCH_ALLOC_GRANULARITY
#define HOST_HOOKS_GT_PCH_ALLOC_GRANULARITY mingw32_gt_pch_alloc_granularity

static inline void w32_error(const char*, const char*, int, const char*);

/* FIXME: Is this big enough?  */
static const size_t pch_VA_max_size  = 128 * 1024 * 1024;

/* Granularity for reserving address space.  */
static const size_t va_granularity = 0x10000;

/* Print out the GetLastError() translation.  */ 
static inline void
w32_error (const char* function, const char* file, int line,
	   const char* my_msg)
{
  LPSTR w32_msgbuf;
  FormatMessageA (FORMAT_MESSAGE_ALLOCATE_BUFFER
		  | FORMAT_MESSAGE_FROM_SYSTEM
		  | FORMAT_MESSAGE_IGNORE_INSERTS
		  | FORMAT_MESSAGE_MAX_WIDTH_MASK,
    		  NULL, GetLastError(),
		  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		  (LPSTR) &w32_msgbuf, 0, NULL);
  fprintf(stderr, "internal error in %s, at %s:%d: %s: %s\n",
	  function, trim_filename (file), line, my_msg, w32_msgbuf);
  LocalFree ((HLOCAL)w32_msgbuf);
}

/* Granularity for reserving address space.  */
static size_t mingw32_gt_pch_alloc_granularity (void)
{
  return va_granularity;
}

/* Identify an address that's likely to be free in a subsequent invocation
   of the compiler.  The area should be able to hold SIZE bytes.  FD is an
   open file descriptor if the host would like to probe with mmap.  */

static void *
mingw32_gt_pch_get_address (size_t size, int fd  ATTRIBUTE_UNUSED)
{
  void* res;
  size = (size + va_granularity - 1) & ~(va_granularity - 1);
  if (size > pch_VA_max_size)
    return NULL;

  /* FIXME: We let system determine base by setting first arg to NULL.
     Allocating at top of available address space avoids unnecessary
     fragmentation of "ordinary" (malloc's)  address space but may not be safe
     with delayed load of system dll's. Preferred addresses for NT system
     dlls is in 0x70000000 to 0x78000000 range.
     If we allocate at bottom we need to reserve the address as early as possible
     and at the same point in each invocation. */
 
  res = VirtualAlloc (NULL, pch_VA_max_size,
		      MEM_RESERVE | MEM_TOP_DOWN,
		      PAGE_NOACCESS);
  if (!res)
    w32_error (__FUNCTION__, __FILE__, __LINE__, "VirtualAlloc");
  else
    /* We do not need the address space for now, so free it.  */
    VirtualFree (res, 0, MEM_RELEASE);

  return res; 
}

/* ADDR is an address returned by gt_pch_get_address.  Attempt to allocate
   SIZE bytes at the same address and load it with the data from FD at 
   OFFSET.  Return -1 if we couldn't allocate memory at ADDR, return 0
   if the memory is allocated but the data not loaded, return 1 if done.  */

static int
mingw32_gt_pch_use_address (void *addr, size_t size, int fd,
			    size_t offset)
{
  void * mmap_addr;
  static HANDLE mmap_handle;

  if (size == 0)
    return 0;
  
  /* Offset must be also be a multiple of allocation granularity for
     this to work.  We can't change the offset. */ 
  if ((offset & (va_granularity - 1)) != 0 || size > pch_VA_max_size)
    return -1;

  mmap_handle = CreateFileMapping ((HANDLE) _get_osfhandle (fd),
				   NULL, PAGE_WRITECOPY | SEC_COMMIT,
				   0, 0,  NULL);
  if (mmap_handle == NULL)
    {
      w32_error (__FUNCTION__,  __FILE__, __LINE__, "CreateFileMapping");
      return -1; 
    }
  mmap_addr = MapViewOfFileEx (mmap_handle, FILE_MAP_COPY, 0, offset,
			       size, addr);
  if (mmap_addr != addr)
    {
      w32_error (__FUNCTION__, __FILE__, __LINE__, "MapViewOfFileEx");
      CloseHandle(mmap_handle);
      return  -1;
    }

  return 1;
}

const struct host_hooks host_hooks = HOST_HOOKS_INITIALIZER;
