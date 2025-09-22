/* Mudflap: narrow-pointer bounds-checking by tree rewriting.
   Copyright (C) 2002, 2003 Free Software Foundation, Inc.
   Contributed by Frank Ch. Eigler <fche@redhat.com>
   and Graydon Hoare <graydon@redhat.com>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file into combinations with other programs,
and to distribute those combinations without any restriction coming
from the use of this file.  (The General Public License restrictions
do apply in other respects; for example, they cover modification of
the file, and distribution when not linked into a combine
executable.)

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */


#include "config.h"

#include <stdio.h>

#include "mf-runtime.h"
#include "mf-impl.h"

#ifdef _MUDFLAP
#error "Do not compile this file with -fmudflap!"
#endif


extern char _end[];
extern char ENTRY_POINT[];


/* Run some quick validation of the given region.
   Return -1 / 0 / 1 if the access known-invalid, possibly-valid, or known-valid.
*/
int
__mf_heuristic_check (uintptr_t ptr, uintptr_t ptr_high)
{
  VERBOSE_TRACE ("mf: heuristic check\n");

  /* XXX: Disable the stack bounding check for libmudflapth.  We do
     actually have enough information to track stack bounds (see
     __mf_pthread_info in mf-hooks.c), so with a bit of future work,
     this heuristic can be turned on.  */
#ifndef LIBMUDFLAPTH

  /* The first heuristic is to check stack bounds.  This is a
     transient condition and quick to check. */
  if (__mf_opts.heur_stack_bound)
    {
      uintptr_t stack_top_guess = (uintptr_t)__builtin_frame_address(0);
#if defined(__i386__) && defined (__linux__)
      uintptr_t stack_segment_base = 0xC0000000; /* XXX: Bad assumption. */
#else
      /* Cause tests to fail. */
      uintptr_t stack_segment_base = 0;
#endif

      VERBOSE_TRACE ("mf: stack estimated as %p-%p\n",
		     (void *) stack_top_guess, (void *) stack_segment_base);

      if (ptr_high <= stack_segment_base &&
	  ptr >= stack_top_guess &&
	  ptr_high >= ptr)
	{
	  return 1;
	}
    }
#endif


  /* The second heuristic is to scan the range of memory regions
     listed in /proc/self/maps, a special file provided by the Linux
     kernel.  Its results may be cached, and in fact, a GUESS object
     may as well be recorded for interesting matching sections.  */
  if (__mf_opts.heur_proc_map)
    {
      /* Keep a record of seen records from /proc/self/map.  */
      enum { max_entries = 500 };
      struct proc_self_map_entry
      {
	uintptr_t low;
	uintptr_t high;
      };
      static struct proc_self_map_entry entry [max_entries];
      static unsigned entry_used [max_entries];

      /* Look for a known proc_self_map entry that may cover this
	 region.  If one exists, then this heuristic has already run,
	 and should not be run again.  The check should be allowed to
	 fail.  */
      unsigned i;
      unsigned deja_vu = 0;
      for (i=0; i<max_entries; i++)
	{
	  if (entry_used[i] &&
	      (entry[i].low <= ptr) &&
	      (entry[i].high >= ptr_high))
	    deja_vu = 1;
	}

      if (! deja_vu)
	{
	  /* Time to run the heuristic.  Rescan /proc/self/maps; update the
	     entry[] array; XXX: remove expired entries, add new ones.
	     XXX: Consider entries that have grown (e.g., stack).  */
	  char buf[512];
	  char flags[4];
	  void *low, *high;
	  FILE *fp;

	  fp = fopen ("/proc/self/maps", "r");
	  if (fp)
	    {
	      while (fgets (buf, sizeof(buf), fp))
		{
		  if (sscanf (buf, "%p-%p %4c", &low, &high, flags) == 3)
		    {
		      if ((uintptr_t) low <= ptr &&
			  (uintptr_t) high >= ptr_high)
			{
			  for (i=0; i<max_entries; i++)
			    {
			      if (! entry_used[i])
				{
				  entry[i].low = (uintptr_t) low;
				  entry[i].high = (uintptr_t) high;
				  entry_used[i] = 1;
				  break;
				}
			    }

			  VERBOSE_TRACE ("mf: registering region #%d "
					 "%p-%p given %s",
					 i, (void *) low, (void *) high, buf);

			  __mfu_register ((void *) low, (size_t) (high-low),
					  __MF_TYPE_GUESS,
					  "/proc/self/maps segment");

			  return 0; /* undecided (tending to cachable) */
			}
		    }
		}
	      fclose (fp);
	    }
	}
    }


  /* The third heuristic is to approve all accesses between _start (or its
     equivalent for the given target) and _end, which should include all
     text and initialized data.  */
  if (__mf_opts.heur_start_end)
    if (ptr >= (uintptr_t) & ENTRY_POINT && ptr_high <= (uintptr_t) & _end)
      return 1; /* uncacheable */

  return 0; /* unknown */
}
