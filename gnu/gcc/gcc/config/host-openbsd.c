/* OpenBSD host-specific hook definitions.
   Copyright (C) 2005 Free Software Foundation, Inc.

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
   Free Software Foundation, 59 Temple Place - Suite 330, Boston,
   MA 02111-1307, USA.  */

#include <limits.h>
#include <unistd.h>

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "hosthooks.h"
#include "hosthooks-def.h"

#undef HOST_HOOKS_GT_PCH_GET_ADDRESS
#define HOST_HOOKS_GT_PCH_GET_ADDRESS openbsd_gt_pch_get_address

#undef HOST_HOOKS_GT_PCH_USE_ADDRESS
#define HOST_HOOKS_GT_PCH_USE_ADDRESS openbsd_gt_pch_use_address

/* Return the address of the PCH address space, if the PCH will fit in it.  */

void *
openbsd_gt_pch_get_address (size_t size, int fd ATTRIBUTE_UNUSED)
{
  void *base, *addr;
  size_t pgsz;

  if (size > INT_MAX)
          return NULL;

  pgsz = sysconf(_SC_PAGESIZE);
  if (pgsz == (size_t)-1)
    return NULL;

  base = sbrk(0);

  /* round up to nearest page */
  base = (void *)(((long)base + (pgsz - 1)) & ~(pgsz - 1));
  if (brk(base) != 0)
    return NULL;

  /* attempt to allocate size */
  addr = sbrk(size);
  if (addr == (void *)-1)
    return NULL;

  /* deallocate the memory */
  if (brk(base) != 0)
    return NULL;

  /* confirm addr is as expected */
  if (addr != base)
    return NULL;

  return base;
}

/* Return 0 if we can reserve the PCH address space. */

int
openbsd_gt_pch_use_address (void *base, size_t size, int fd ATTRIBUTE_UNUSED, size_t off ATTRIBUTE_UNUSED)
{
  void *addr;

  if (size == 0)
    return -1;

  /* sanity check base address */
  addr = sbrk(0);
  if (addr == (void *)-1 || base < addr)
    return -1;

  /* set base for sbrk */
  if (brk(base) != 0)
    return -1;

  /* attempt to get the memory */
  addr = sbrk(size);
  if (addr == (void *)-1)
    return -1;

  /* sanity check the result */
  if (addr != base) {
    brk(base);
    return -1;
  }

  return 0;
}

const struct host_hooks host_hooks = HOST_HOOKS_INITIALIZER;
