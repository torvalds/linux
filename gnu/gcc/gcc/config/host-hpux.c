/* HP-UX host-specific hook definitions.
   Copyright (C) 2004, 2005 Free Software Foundation, Inc.

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
#include <unistd.h>
#include "hosthooks.h"
#include "hosthooks-def.h"

#ifndef MAP_FAILED
#define MAP_FAILED (void *)-1L
#endif

static void *hpux_gt_pch_get_address (size_t, int);
static int hpux_gt_pch_use_address (void *, size_t, int, size_t);

#undef HOST_HOOKS_GT_PCH_GET_ADDRESS
#define HOST_HOOKS_GT_PCH_GET_ADDRESS hpux_gt_pch_get_address
#undef HOST_HOOKS_GT_PCH_USE_ADDRESS
#define HOST_HOOKS_GT_PCH_USE_ADDRESS hpux_gt_pch_use_address

/* For various ports, try to guess a fixed spot in the vm space
   that's probably free.  */
#if (defined(__hppa__) || defined(__ia64__)) && defined(__LP64__)
# define TRY_EMPTY_VM_SPACE	0x8000000000000000
#elif defined(__hppa__) || defined(__ia64__)
# define TRY_EMPTY_VM_SPACE	0x60000000
#else
# define TRY_EMPTY_VM_SPACE	0
#endif

/* Determine a location where we might be able to reliably allocate
   SIZE bytes.  FD is the PCH file, though we should return with the
   file unmapped.  */

static void *
hpux_gt_pch_get_address (size_t size, int fd)
{
  void *addr;

  addr = mmap ((void *)TRY_EMPTY_VM_SPACE, size, PROT_READ | PROT_WRITE,
	       MAP_PRIVATE, fd, 0);

  /* If we failed the map, that means there's *no* free space.  */
  if (addr == (void *) MAP_FAILED)
    return NULL;
  /* Unmap the area before returning.  */
  munmap (addr, size);

  return addr;
}

/* Map SIZE bytes of FD+OFFSET at BASE.  Return 1 if we succeeded at
   mapping the data at BASE, -1 if we couldn't.

   It's not possibly to reliably mmap a file using MAP_PRIVATE to
   a specific START address on either hpux or linux.  First we see
   if mmap with MAP_PRIVATE works.  If it does, we are off to the
   races.  If it doesn't, we try an anonymous private mmap since the
   kernel is more likely to honor the BASE address in anonymous maps.
   We then copy the data to the anonymous private map.  This assumes
   of course that we don't need to change the data in the PCH file
   after it is created.

   This approach obviously causes a performance penalty but there is
   little else we can do given the current PCH implementation.  */

static int
hpux_gt_pch_use_address (void *base, size_t size, int fd, size_t offset)
{
  void *addr;

  /* We're called with size == 0 if we're not planning to load a PCH
     file at all.  This allows the hook to free any static space that
     we might have allocated at link time.  */
  if (size == 0)
    return -1;

  /* Try to map the file with MAP_PRIVATE.  */
  addr = mmap (base, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, offset);

  if (addr == base)
    return 1;

  if (addr != (void *) MAP_FAILED)
    munmap (addr, size);

  /* Try to make an anonymous private mmap at the desired location.  */
  addr = mmap (base, size, PROT_READ | PROT_WRITE,
	       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  if (addr != base)
    {
      if (addr != (void *) MAP_FAILED)
        munmap (addr, size);
      return -1;
    }

  if (lseek (fd, offset, SEEK_SET) == (off_t)-1)
    return -1;

  while (size)
    {
      ssize_t nbytes;

      nbytes = read (fd, base, MIN (size, SSIZE_MAX));
      if (nbytes <= 0)
        return -1;
      base = (char *) base + nbytes;
      size -= nbytes;
    }

  return 1;
}


const struct host_hooks host_hooks = HOST_HOOKS_INITIALIZER;
