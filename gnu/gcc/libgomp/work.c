/* Copyright (C) 2005 Free Software Foundation, Inc.
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

/* This file contains routines to manage the work-share queue for a team
   of threads.  */

#include "libgomp.h"
#include <stdlib.h>
#include <string.h>


/* Create a new work share structure.  */

struct gomp_work_share *
gomp_new_work_share (bool ordered, unsigned nthreads)
{
  struct gomp_work_share *ws;
  size_t size;

  size = sizeof (*ws);
  if (ordered)
    size += nthreads * sizeof (ws->ordered_team_ids[0]);

  ws = gomp_malloc_cleared (size);
  gomp_mutex_init (&ws->lock);
  ws->ordered_owner = -1;

  return ws;
}


/* Free a work share structure.  */

static void
free_work_share (struct gomp_work_share *ws)
{
  gomp_mutex_destroy (&ws->lock);
  free (ws);
}


/* The current thread is ready to begin the next work sharing construct.
   In all cases, thr->ts.work_share is updated to point to the new
   structure.  In all cases the work_share lock is locked.  Return true
   if this was the first thread to reach this point.  */

bool
gomp_work_share_start (bool ordered)
{
  struct gomp_thread *thr = gomp_thread ();
  struct gomp_team *team = thr->ts.team;
  struct gomp_work_share *ws;
  unsigned ws_index, ws_gen;

  /* Work sharing constructs can be orphaned.  */
  if (team == NULL)
    {
      ws = gomp_new_work_share (ordered, 1);
      thr->ts.work_share = ws;
      thr->ts.static_trip = 0;
      gomp_mutex_lock (&ws->lock);
      return true;
    }

  gomp_mutex_lock (&team->work_share_lock);

  /* This thread is beginning its next generation.  */
  ws_gen = ++thr->ts.work_share_generation;

  /* If this next generation is not newer than any other generation in
     the team, then simply reference the existing construct.  */
  if (ws_gen - team->oldest_live_gen < team->num_live_gen)
    {
      ws_index = ws_gen & team->generation_mask;
      ws = team->work_shares[ws_index];
      thr->ts.work_share = ws;
      thr->ts.static_trip = 0;

      gomp_mutex_lock (&ws->lock);
      gomp_mutex_unlock (&team->work_share_lock);

      return false;
    }

  /* Resize the work shares queue if we've run out of space.  */
  if (team->num_live_gen++ == team->generation_mask)
    {
      team->work_shares = gomp_realloc (team->work_shares,
					2 * team->num_live_gen
					* sizeof (*team->work_shares));

      /* Unless oldest_live_gen is zero, the sequence of live elements
	 wraps around the end of the array.  If we do nothing, we break
	 lookup of the existing elements.  Fix that by unwrapping the
	 data from the front to the end.  */
      if (team->oldest_live_gen > 0)
	memcpy (team->work_shares + team->num_live_gen,
		team->work_shares,
		(team->oldest_live_gen & team->generation_mask)
		* sizeof (*team->work_shares));

      team->generation_mask = team->generation_mask * 2 + 1;
    }

  ws_index = ws_gen & team->generation_mask;
  ws = gomp_new_work_share (ordered, team->nthreads);
  thr->ts.work_share = ws;
  thr->ts.static_trip = 0;
  team->work_shares[ws_index] = ws;

  gomp_mutex_lock (&ws->lock);
  gomp_mutex_unlock (&team->work_share_lock);

  return true;
}


/* The current thread is done with its current work sharing construct.
   This version does imply a barrier at the end of the work-share.  */

void
gomp_work_share_end (void)
{
  struct gomp_thread *thr = gomp_thread ();
  struct gomp_team *team = thr->ts.team;
  struct gomp_work_share *ws = thr->ts.work_share;
  bool last;

  thr->ts.work_share = NULL;

  /* Work sharing constructs can be orphaned.  */
  if (team == NULL)
    {
      free_work_share (ws);
      return;
    }

  last = gomp_barrier_wait_start (&team->barrier);

  if (last)
    {
      unsigned ws_index;

      ws_index = thr->ts.work_share_generation & team->generation_mask;
      team->work_shares[ws_index] = NULL;
      team->oldest_live_gen++;
      team->num_live_gen = 0;

      free_work_share (ws);
    }

  gomp_barrier_wait_end (&team->barrier, last);
}


/* The current thread is done with its current work sharing construct.
   This version does NOT imply a barrier at the end of the work-share.  */

void
gomp_work_share_end_nowait (void)
{
  struct gomp_thread *thr = gomp_thread ();
  struct gomp_team *team = thr->ts.team;
  struct gomp_work_share *ws = thr->ts.work_share;
  unsigned completed;

  thr->ts.work_share = NULL;

  /* Work sharing constructs can be orphaned.  */
  if (team == NULL)
    {
      free_work_share (ws);
      return;
    }

#ifdef HAVE_SYNC_BUILTINS
  completed = __sync_add_and_fetch (&ws->threads_completed, 1);
#else
  gomp_mutex_lock (&ws->lock);
  completed = ++ws->threads_completed;
  gomp_mutex_unlock (&ws->lock);
#endif

  if (completed == team->nthreads)
    {
      unsigned ws_index;

      gomp_mutex_lock (&team->work_share_lock);

      ws_index = thr->ts.work_share_generation & team->generation_mask;
      team->work_shares[ws_index] = NULL;
      team->oldest_live_gen++;
      team->num_live_gen--;

      gomp_mutex_unlock (&team->work_share_lock);

      free_work_share (ws);
    }
}
