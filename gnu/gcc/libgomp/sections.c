/* Copyright (C) 2005, 2007 Free Software Foundation, Inc.
   Contributed by Richard Henderson <rth@redhat.com>.

   This file is part of the GNU OpenMP Library (libgomp).

   Libgomp is free software; you can redistribute it and/or modify it
   under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation; either version 2.1 of the License, or
   (at your option) any later version.

   Libgomp is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
   FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
   more details.

   You should have received a copy of the GNU Lesser General Public License 
   along with libgomp; see the file COPYING.LIB.  If not, write to the
   Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
   MA 02110-1301, USA.  */

/* As a special exception, if you link this library with other files, some
   of which are compiled with GCC, to produce an executable, this library
   does not by itself cause the resulting executable to be covered by the
   GNU General Public License.  This exception does not however invalidate
   any other reasons why the executable file might be covered by the GNU
   General Public License.  */

/* This file handles the SECTIONS construct.  */

#include "libgomp.h"


/* Initialize the given work share construct from the given arguments.  */

static inline void
gomp_sections_init (struct gomp_work_share *ws, unsigned count)
{
  ws->sched = GFS_DYNAMIC;
  ws->chunk_size = 1;
  ws->end = count + 1;
  ws->incr = 1;
  ws->next = 1;
}

/* This routine is called when first encountering a sections construct
   that is not bound directly to a parallel construct.  The first thread 
   that arrives will create the work-share construct; subsequent threads
   will see the construct exists and allocate work from it.

   COUNT is the number of sections in this construct.

   Returns the 1-based section number for this thread to perform, or 0 if
   all work was assigned to other threads prior to this thread's arrival.  */

unsigned
GOMP_sections_start (unsigned count)
{
  struct gomp_thread *thr = gomp_thread ();
  long s, e, ret;

  if (gomp_work_share_start (false))
    gomp_sections_init (thr->ts.work_share, count);

  if (gomp_iter_dynamic_next_locked (&s, &e))
    ret = s;
  else
    ret = 0;

  gomp_mutex_unlock (&thr->ts.work_share->lock);

  return ret;
}

/* This routine is called when the thread completes processing of the
   section currently assigned to it.  If the work-share construct is
   bound directly to a parallel construct, then the construct may have
   been set up before the parallel.  In which case, this may be the
   first iteration for the thread.

   Returns the 1-based section number for this thread to perform, or 0 if
   all work was assigned to other threads prior to this thread's arrival.  */

unsigned
GOMP_sections_next (void)
{
  struct gomp_thread *thr = gomp_thread ();
  long s, e, ret;

  gomp_mutex_lock (&thr->ts.work_share->lock);
  if (gomp_iter_dynamic_next_locked (&s, &e))
    ret = s;
  else
    ret = 0;
  gomp_mutex_unlock (&thr->ts.work_share->lock);

  return ret;
}

/* This routine pre-initializes a work-share construct to avoid one
   synchronization once we get into the loop.  */

void
GOMP_parallel_sections_start (void (*fn) (void *), void *data,
			      unsigned num_threads, unsigned count)
{
  struct gomp_work_share *ws;

  num_threads = gomp_resolve_num_threads (num_threads);
  if (gomp_dyn_var && num_threads > count)
    num_threads = count;

  ws = gomp_new_work_share (false, num_threads);
  gomp_sections_init (ws, count);
  gomp_team_start (fn, data, num_threads, ws);
}

/* The GOMP_section_end* routines are called after the thread is told
   that all sections are complete.  This first version synchronizes
   all threads; the nowait version does not.  */

void
GOMP_sections_end (void)
{
  gomp_work_share_end ();
}

void
GOMP_sections_end_nowait (void)
{
  gomp_work_share_end_nowait ();
}
