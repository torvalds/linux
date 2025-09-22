/* Solaris host-specific hook definitions.
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
#include <sys/mman.h>
#include "hosthooks.h"
#include "hosthooks-def.h"


#undef HOST_HOOKS_GT_PCH_USE_ADDRESS
#define HOST_HOOKS_GT_PCH_USE_ADDRESS sol_gt_pch_use_address

/* Map SIZE bytes of FD+OFFSET at BASE.  Return 1 if we succeeded at 
   mapping the data at BASE, -1 if we couldn't.  */

static int
sol_gt_pch_use_address (void *base, size_t size, int fd, size_t offset)
{
  void *addr;

  /* We're called with size == 0 if we're not planning to load a PCH
     file at all.  This allows the hook to free any static space that
     we might have allocated at link time.  */
  if (size == 0)
    return -1;

  addr = mmap (base, size, PROT_READ | PROT_WRITE, MAP_PRIVATE,
	       fd, offset);

  /* Solaris isn't good about honoring the mmap START parameter
     without MAP_FIXED set.  Before we give up, search the desired
     address space with mincore to see if the space is really free.  */
  if (addr != base)
    {
      size_t page_size = getpagesize();
      char one_byte;
      size_t i;

      if (addr != (void *) MAP_FAILED)
	munmap (addr, size);

      errno = 0;
      for (i = 0; i < size; i += page_size)
	if (mincore ((char *)base + i, page_size, (void *)&one_byte) == -1
	    && errno == ENOMEM)
	  continue; /* The page is not mapped.  */
	else
	  break;

      if (i >= size)
	addr = mmap (base, size, 
		     PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED,
		     fd, offset);
    }

  return addr == base ? 1 : -1;
}


const struct host_hooks host_hooks = HOST_HOOKS_INITIALIZER;
