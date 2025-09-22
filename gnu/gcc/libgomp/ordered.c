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

/* This file handles the ORDERED construct.  */

#include "libgomp.h"


/* This function is called when first allocating an iteration block.  That
   is, the thread is not currently on the queue.  The work-share lock must
   be held on entry.  */

void
gomp_ordered_first (void)
{
  struct gomp_thread *thr = gomp_thread ();
  struct gomp_team *team = thr->ts.team;
  struct gomp_work_share *ws = thr->ts.work_share;
  unsigned index;

  /* Work share constructs can be orphaned.  */
  if (team == NULL || team->nthreads == 1)
    return;

  index = ws->ordered_cur + ws->ordered_num_used;
  if (index >= team->nthreads)
    index -= team->nthreads;
  ws->ordered_team_ids[index] = thr->ts.team_id;

  /* If this is the first and only thread in the queue, then there is
     no one to release us when we get to our ordered section.  Post to
     our own release queue now so that we won't block later.  */
  if (ws->ordered_num_used++ == 0)
    gomp_sem_post (team->ordered_release[thr->ts.team_id]);
}

/* This function is called when completing the last iteration block.  That
   is, there are no more iterations to perform and so the thread should be
   removed from the queue entirely.  Because of the way ORDERED blocks are
   managed, it follows that we currently own access to the ORDERED block,
   and should now pass it on to the next thread.  The work-share lock must
   be held on entry.  */

void
gomp_ordered_last (void)
{
  struct gomp_thread *thr = gomp_thread ();
  struct gomp_team *team = thr->ts.team;
  struct gomp_work_share *ws = thr->ts.work_share;
  unsigned next_id;

  /* Work share constructs can be orphaned.  */
  if (team == NULL || team->nthreads == 1)
    return;

  /* We're no longer the owner.  */
  ws->ordered_owner = -1;

  /* If we're not the last thread in the queue, then wake the next.  */
  if (--ws->ordered_num_used > 0)
    {
      unsigned next = ws->ordered_cur + 1;
      if (next == team->nthreads)
	next = 0;
      ws->ordered_cur = next;

      next_id = ws->ordered_team_ids[next];
      gomp_sem_post (team->ordered_release[next_id]);
    }
}


/* This function is called when allocating a subsequent allocation block.
   That is, we're done with the current iteration block and we're allocating
   another.  This is the logical combination of a call to gomp_ordered_last
   followed by a call to gomp_ordered_first.  The work-share lock must be
   held on entry. */

void
gomp_ordered_next (void)
{
  struct gomp_thread *thr = gomp_thread ();
  struct gomp_team *team = thr->ts.team;
  struct gomp_work_share *ws = thr->ts.work_share;
  unsigned index, next_id;

  /* Work share constructs can be orphaned.  */
  if (team == NULL || team->nthreads == 1)
    return;

  /* We're no longer the owner.  */
  ws->ordered_owner = -1;

  /* If there's only one thread in the queue, that must be us.  */
  if (ws->ordered_num_used == 1)
    {
      /* We have a similar situation as in gomp_ordered_first
	 where we need to post to our own release semaphore.  */
      gomp_sem_post (team->ordered_release[thr->ts.team_id]);
      return;
    }

  /* If the queue is entirely full, then we move ourself to the end of 
     the queue merely by incrementing ordered_cur.  Only if it's not 
     full do we have to write our id.  */
  if (ws->ordered_num_used < team->nthreads)
    {
      index = ws->ordered_cur + ws->ordered_num_used;
      if (index >= team->nthreads)
	index -= team->nthreads;
      ws->ordered_team_ids[index] = thr->ts.team_id;
    }

  index = ws->ordered_cur + 1;
  if (index == team->nthreads)
    index = 0;
  ws->ordered_cur = index;

  next_id = ws->ordered_team_ids[index];
  gomp_sem_post (team->ordered_release[next_id]);
}


/* This function is called when a statically scheduled loop is first
   being created.  */

void
gomp_ordered_static_init (void)
{
  struct gomp_thread *thr = gomp_thread ();
  struct gomp_team *team = thr->ts.team;

  if (team == NULL || team->nthreads == 1)
    return;

  gomp_sem_post (team->ordered_release[0]);
}

/* This function is called when a statically scheduled loop is moving to
   the next allocation block.  Static schedules are not first come first
   served like the others, so we're to move to the numerically next thread,
   not the next thread on a list.  The work-share lock should *not* be held
   on entry.  */

void
gomp_ordered_static_next (void)
{
  struct gomp_thread *thr = gomp_thread ();
  struct gomp_team *team = thr->ts.team;
  struct gomp_work_share *ws = thr->ts.work_share;
  unsigned id = thr->ts.team_id;

  if (team == NULL || team->nthreads == 1)
    return;

  ws->ordered_owner = -1;

  /* This thread currently owns the lock.  Increment the owner.  */
  if (++id == team->nthreads)
    id = 0;
  ws->ordered_team_ids[0] = id;
  gomp_sem_post (team->ordered_release[id]);
}

/* This function is called when we need to assert that the thread owns the
   ordered section.  Due to the problem of posted-but-not-waited semaphores,
   this needs to happen before completing a loop iteration.  */

void
gomp_ordered_sync (void)
{
  struct gomp_thread *thr = gomp_thread ();
  struct gomp_team *team = thr->ts.team;
  struct gomp_work_share *ws = thr->ts.work_share;

  /* Work share constructs can be orphaned.  But this clearly means that
     we are the only thread, and so we automatically own the section.  */
  if (team == NULL || team->nthreads == 1)
    return;

  /* ??? I believe it to be safe to access this data without taking the
     ws->lock.  The only presumed race condition is with the previous
     thread on the queue incrementing ordered_cur such that it points
     to us, concurrently with our check below.  But our team_id is
     already present in the queue, and the other thread will always
     post to our release semaphore.  So the two cases are that we will
     either win the race an momentarily block on the semaphore, or lose
     the race and find the semaphore already unlocked and so not block.
     Either way we get correct results.  */

  if (ws->ordered_owner != thr->ts.team_id)
    {
      gomp_sem_wait (team->ordered_release[thr->ts.team_id]);
      ws->ordered_owner = thr->ts.team_id;
    }
}

/* This function is called by user code when encountering the start of an
   ORDERED block.  We must check to see if the current thread is at the
   head of the queue, and if not, block.  */

#ifdef HAVE_ATTRIBUTE_ALIAS
extern void GOMP_ordered_start (void)
	__attribute__((alias ("gomp_ordered_sync")));
#else
void
GOMP_ordered_start (void)
{
  gomp_ordered_sync ();
}
#endif

/* This function is called by user code when encountering the end of an
   ORDERED block.  With the current ORDERED implementation there's nothing
   for us to do.

   However, the current implementation has a flaw in that it does not allow
   the next thread into the ORDERED section immediately after the current
   thread exits the ORDERED section in its last iteration.  The existance
   of this function allows the implementation to change.  */

void
GOMP_ordered_end (void)
{
}
