/* Low level interface to ptrace, for the remote server for GDB.
   Copyright 1995, 1996, 1998, 1999, 2000, 2001, 2002, 2003, 2004
   Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "server.h"
#include "fbsd-low.h"

#include <sys/wait.h>
#include <sys/param.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ``all_threads'' is keyed by the LWP ID - it should be the thread ID instead,
   however.  This requires changing the ID in place when we go from !using_threads
   to using_threads, immediately.

   ``all_processes'' is keyed by the process ID - which on Linux is (presently)
   the same as the LWP ID.  */

struct inferior_list all_processes;

/* FIXME this is a bit of a hack, and could be removed.  */
int stopping_threads;

/* FIXME make into a target method?  */
int using_threads;

static void fbsd_resume_one_process (struct inferior_list_entry *entry,
				      int step, int signal);
static void fbsd_resume (struct thread_resume *resume_info);
static void stop_all_processes (void);
static int fbsd_wait_for_event (struct thread_info *child);

struct pending_signals
{
  int signal;
  struct pending_signals *prev;
};

#define PTRACE_ARG3_TYPE caddr_t
#define PTRACE_XFER_TYPE int

int debug_threads = 0;

#define pid_of(proc) ((proc)->head.id)

/* FIXME: Delete eventually.  */
#define inferior_pid (pid_of (get_thread_process (current_inferior)))

/* This function should only be called if the process got a SIGTRAP.
   The SIGTRAP could mean several things.

   On i386, where decr_pc_after_break is non-zero:
   If we were single-stepping this process using PT_STEP,
   we will get only the one SIGTRAP (even if the instruction we
   stepped over was a breakpoint).  The value of $eip will be the
   next instruction.
   If we continue the process using PTRACE_CONT, we will get a
   SIGTRAP when we hit a breakpoint.  The value of $eip will be
   the instruction after the breakpoint (i.e. needs to be
   decremented).  If we report the SIGTRAP to GDB, we must also
   report the undecremented PC.  If we cancel the SIGTRAP, we
   must resume at the decremented PC.

   (Presumably, not yet tested) On a non-decr_pc_after_break machine
   with hardware or kernel single-step:
   If we single-step over a breakpoint instruction, our PC will
   point at the following instruction.  If we continue and hit a
   breakpoint instruction, our PC will point at the breakpoint
   instruction.  */

static CORE_ADDR
get_stop_pc (void)
{
  CORE_ADDR stop_pc = (*the_low_target.get_pc) ();

  if (get_thread_process (current_inferior)->stepping)
    return stop_pc;
  else
    return stop_pc - the_low_target.decr_pc_after_break;
}

static void *
add_process (int pid)
{
  struct process_info *process;

  process = (struct process_info *) malloc (sizeof (*process));
  memset (process, 0, sizeof (*process));

  process->head.id = pid;

  /* Default to tid == lwpid == pid.  */
  process->tid = pid;
  process->lwpid = pid;

  add_inferior_to_list (&all_processes, &process->head);

  return process;
}

/* Start an inferior process and returns its pid.
   ALLARGS is a vector of program-name and args. */

static int
fbsd_create_inferior (char *program, char **allargs)
{
  void *new_process;
  int pid;

  pid = vfork ();
  if (pid < 0)
    perror_with_name ("vfork");

  if (pid == 0)
    {
      ptrace (PT_TRACE_ME, 0, 0, 0);

      setpgid (0, 0);

      execv (program, allargs);

      fprintf (stderr, "Cannot exec %s: %s.\n", program,
	       strerror (errno));
      fflush (stderr);
      _exit (0177);
    }

  new_process = add_process (pid);
  add_thread (pid, new_process);

  return pid;
}

/* Attach to an inferior process.  */

void
fbsd_attach_lwp (int pid, int tid)
{
  struct process_info *new_process;

  if (ptrace (PT_ATTACH, pid, 0, 0) != 0)
    {
      fprintf (stderr, "Cannot attach to process %d: %s (%d)\n", pid,
	       strerror (errno), errno);
      fflush (stderr);

      /* If we fail to attach to an LWP, just return.  */
      if (!using_threads)
	_exit (0177);
      return;
    }

  new_process = (struct process_info *) add_process (pid);
  add_thread (tid, new_process);

  /* The next time we wait for this LWP we'll see a SIGSTOP as PTRACE_ATTACH
     brings it to a halt.  We should ignore that SIGSTOP and resume the process
     (unless this is the first process, in which case the flag will be cleared
     in fbsd_attach).

     On the other hand, if we are currently trying to stop all threads, we
     should treat the new thread as if we had sent it a SIGSTOP.  This works
     because we are guaranteed that add_process added us to the end of the
     list, and so the new thread has not yet reached wait_for_sigstop (but
     will).  */
  if (! stopping_threads)
    new_process->stop_expected = 1;
}

int
fbsd_attach (int pid)
{
  struct process_info *process;

  fbsd_attach_lwp (pid, pid);

  /* Don't ignore the initial SIGSTOP if we just attached to this process.  */
  process = (struct process_info *) find_inferior_id (&all_processes, pid);
  process->stop_expected = 0;

  return 0;
}

/* Kill the inferior process.  Make us have no inferior.  */

static void
fbsd_kill_one_process (struct inferior_list_entry *entry)
{
  struct thread_info *thread = (struct thread_info *) entry;
  struct process_info *process = get_thread_process (thread);
  int wstat;

  do
    {
      ptrace (PT_KILL, pid_of (process), 0, 0);

      /* Make sure it died.  The loop is most likely unnecessary.  */
      wstat = fbsd_wait_for_event (thread);
    } while (WIFSTOPPED (wstat));
}

static void
fbsd_kill (void)
{
  for_each_inferior (&all_threads, fbsd_kill_one_process);
}

static void
fbsd_detach_one_process (struct inferior_list_entry *entry)
{
  struct thread_info *thread = (struct thread_info *) entry;
  struct process_info *process = get_thread_process (thread);

  ptrace (PT_DETACH, pid_of (process), 0, 0);
}

static void
fbsd_detach (void)
{
  for_each_inferior (&all_threads, fbsd_detach_one_process);
}

/* Return nonzero if the given thread is still alive.  */
static int
fbsd_thread_alive (int tid)
{
  if (find_inferior_id (&all_threads, tid) != NULL)
    return 1;
  else
    return 0;
}

/* Return nonzero if this process stopped at a breakpoint which
   no longer appears to be inserted.  Also adjust the PC
   appropriately to resume where the breakpoint used to be.  */
static int
check_removed_breakpoint (struct process_info *event_child)
{
  CORE_ADDR stop_pc;
  struct thread_info *saved_inferior;

  if (event_child->pending_is_breakpoint == 0)
    return 0;

  if (debug_threads)
    fprintf (stderr, "Checking for breakpoint.\n");

  saved_inferior = current_inferior;
  current_inferior = get_process_thread (event_child);

  stop_pc = get_stop_pc ();

  /* If the PC has changed since we stopped, then we shouldn't do
     anything.  This happens if, for instance, GDB handled the
     decr_pc_after_break subtraction itself.  */
  if (stop_pc != event_child->pending_stop_pc)
    {
      if (debug_threads)
	fprintf (stderr, "Ignoring, PC was changed.\n");

      event_child->pending_is_breakpoint = 0;
      current_inferior = saved_inferior;
      return 0;
    }

  /* If the breakpoint is still there, we will report hitting it.  */
  if ((*the_low_target.breakpoint_at) (stop_pc))
    {
      if (debug_threads)
	fprintf (stderr, "Ignoring, breakpoint is still present.\n");
      current_inferior = saved_inferior;
      return 0;
    }

  if (debug_threads)
    fprintf (stderr, "Removed breakpoint.\n");

  /* For decr_pc_after_break targets, here is where we perform the
     decrement.  We go immediately from this function to resuming,
     and can not safely call get_stop_pc () again.  */
  if (the_low_target.set_pc != NULL)
    (*the_low_target.set_pc) (stop_pc);

  /* We consumed the pending SIGTRAP.  */
  event_child->pending_is_breakpoint = 0;
  event_child->status_pending_p = 0;
  event_child->status_pending = 0;

  current_inferior = saved_inferior;
  return 1;
}

/* Return 1 if this process has an interesting status pending.  This function
   may silently resume an inferior process.  */
static int
status_pending_p (struct inferior_list_entry *entry, void *dummy)
{
  struct process_info *process = (struct process_info *) entry;

  if (process->status_pending_p)
    if (check_removed_breakpoint (process))
      {
	/* This thread was stopped at a breakpoint, and the breakpoint
	   is now gone.  We were told to continue (or step...) all threads,
	   so GDB isn't trying to single-step past this breakpoint.
	   So instead of reporting the old SIGTRAP, pretend we got to
	   the breakpoint just after it was removed instead of just
	   before; resume the process.  */
	fbsd_resume_one_process (&process->head, 0, 0);
	return 0;
      }

  return process->status_pending_p;
}

static void
fbsd_wait_for_process (struct process_info **childp, int *wstatp)
{
  int ret;
  int to_wait_for = -1;

  if (*childp != NULL)
    to_wait_for = (*childp)->lwpid;

  while (1)
    {
      ret = waitpid (to_wait_for, wstatp, WNOHANG);

      if (ret == -1)
	{
	  if (errno != ECHILD)
	    perror_with_name ("waitpid");
	}
      else if (ret > 0)
	break;

      usleep (1000);
    }

  if (debug_threads
      && (!WIFSTOPPED (*wstatp)
	  || (WSTOPSIG (*wstatp) != 32
	      && WSTOPSIG (*wstatp) != 33)))
    fprintf (stderr, "Got an event from %d (%x)\n", ret, *wstatp);

  if (to_wait_for == -1)
    *childp = (struct process_info *) find_inferior_id (&all_processes, ret);

  (*childp)->stopped = 1;
  (*childp)->pending_is_breakpoint = 0;

  if (debug_threads
      && WIFSTOPPED (*wstatp))
    {
      current_inferior = (struct thread_info *)
	find_inferior_id (&all_threads, (*childp)->tid);
      /* For testing only; i386_stop_pc prints out a diagnostic.  */
      if (the_low_target.get_pc != NULL)
	get_stop_pc ();
    }
}

static int
fbsd_wait_for_event (struct thread_info *child)
{
  CORE_ADDR stop_pc;
  struct process_info *event_child;
  int wstat;

  /* Check for a process with a pending status.  */
  /* It is possible that the user changed the pending task's registers since
     it stopped.  We correctly handle the change of PC if we hit a breakpoint
     (in check_removed_breakpoint); signals should be reported anyway.  */
  if (child == NULL)
    {
      event_child = (struct process_info *)
	find_inferior (&all_processes, status_pending_p, NULL);
      if (debug_threads && event_child)
	fprintf (stderr, "Got a pending child %d\n", event_child->lwpid);
    }
  else
    {
      event_child = get_thread_process (child);
      if (event_child->status_pending_p
	  && check_removed_breakpoint (event_child))
	event_child = NULL;
    }

  if (event_child != NULL)
    {
      if (event_child->status_pending_p)
	{
	  if (debug_threads)
	    fprintf (stderr, "Got an event from pending child %d (%04x)\n",
		     event_child->lwpid, event_child->status_pending);
	  wstat = event_child->status_pending;
	  event_child->status_pending_p = 0;
	  event_child->status_pending = 0;
	  current_inferior = get_process_thread (event_child);
	  return wstat;
	}
    }

  /* We only enter this loop if no process has a pending wait status.  Thus
     any action taken in response to a wait status inside this loop is
     responding as soon as we detect the status, not after any pending
     events.  */
  while (1)
    {
      if (child == NULL)
	event_child = NULL;
      else
	event_child = get_thread_process (child);

      fbsd_wait_for_process (&event_child, &wstat);

      if (event_child == NULL)
	error ("event from unknown child");

      current_inferior = (struct thread_info *)
	find_inferior_id (&all_threads, event_child->tid);

      if (using_threads)
	{
	  /* Check for thread exit.  */
	  if (! WIFSTOPPED (wstat))
	    {
	      if (debug_threads)
		fprintf (stderr, "Thread %d (LWP %d) exiting\n",
			 event_child->tid, event_child->head.id);

	      /* If the last thread is exiting, just return.  */
	      if (all_threads.head == all_threads.tail)
		return wstat;

	      dead_thread_notify (event_child->tid);

	      remove_inferior (&all_processes, &event_child->head);
	      free (event_child);
	      remove_thread (current_inferior);
	      current_inferior = (struct thread_info *) all_threads.head;

	      /* If we were waiting for this particular child to do something...
		 well, it did something.  */
	      if (child != NULL)
		return wstat;

	      /* Wait for a more interesting event.  */
	      continue;
	    }

	  if (WIFSTOPPED (wstat)
	      && WSTOPSIG (wstat) == SIGSTOP
	      && event_child->stop_expected)
	    {
	      if (debug_threads)
		fprintf (stderr, "Expected stop.\n");
	      event_child->stop_expected = 0;
	      fbsd_resume_one_process (&event_child->head,
					event_child->stepping, 0);
	      continue;
	    }

	  /* FIXME drow/2002-06-09: Get signal numbers from the inferior's
	     thread library?  */
	  if (WIFSTOPPED (wstat))
	    {
	      if (debug_threads)
		fprintf (stderr, "Ignored signal %d for %d (LWP %d).\n",
			 WSTOPSIG (wstat), event_child->tid,
			 event_child->head.id);
	      fbsd_resume_one_process (&event_child->head,
					event_child->stepping,
					WSTOPSIG (wstat));
	      continue;
	    }
	}

      /* If this event was not handled above, and is not a SIGTRAP, report
	 it.  */
      if (!WIFSTOPPED (wstat) || WSTOPSIG (wstat) != SIGTRAP)
	return wstat;

      /* If this target does not support breakpoints, we simply report the
	 SIGTRAP; it's of no concern to us.  */
      if (the_low_target.get_pc == NULL)
	return wstat;

      stop_pc = get_stop_pc ();

      /* bp_reinsert will only be set if we were single-stepping.
	 Notice that we will resume the process after hitting
	 a gdbserver breakpoint; single-stepping to/over one
	 is not supported (yet).  */
      if (event_child->bp_reinsert != 0)
	{
	  if (debug_threads)
	    fprintf (stderr, "Reinserted breakpoint.\n");
	  reinsert_breakpoint (event_child->bp_reinsert);
	  event_child->bp_reinsert = 0;

	  /* Clear the single-stepping flag and SIGTRAP as we resume.  */
	  fbsd_resume_one_process (&event_child->head, 0, 0);
	  continue;
	}

      if (debug_threads)
	fprintf (stderr, "Hit a (non-reinsert) breakpoint.\n");

      if (check_breakpoints (stop_pc) != 0)
	{
	  /* We hit one of our own breakpoints.  We mark it as a pending
	     breakpoint, so that check_removed_breakpoint () will do the PC
	     adjustment for us at the appropriate time.  */
	  event_child->pending_is_breakpoint = 1;
	  event_child->pending_stop_pc = stop_pc;

	  /* Now we need to put the breakpoint back.  We continue in the event
	     loop instead of simply replacing the breakpoint right away,
	     in order to not lose signals sent to the thread that hit the
	     breakpoint.  Unfortunately this increases the window where another
	     thread could sneak past the removed breakpoint.  For the current
	     use of server-side breakpoints (thread creation) this is
	     acceptable; but it needs to be considered before this breakpoint
	     mechanism can be used in more general ways.  For some breakpoints
	     it may be necessary to stop all other threads, but that should
	     be avoided where possible.

	     If breakpoint_reinsert_addr is NULL, that means that we can
	     use PT_STEP on this platform.  Uninsert the breakpoint,
	     mark it for reinsertion, and single-step.

	     Otherwise, call the target function to figure out where we need
	     our temporary breakpoint, create it, and continue executing this
	     process.  */
	  if (the_low_target.breakpoint_reinsert_addr == NULL)
	    {
	      event_child->bp_reinsert = stop_pc;
	      uninsert_breakpoint (stop_pc);
	      fbsd_resume_one_process (&event_child->head, 1, 0);
	    }
	  else
	    {
	      reinsert_breakpoint_by_bp
		(stop_pc, (*the_low_target.breakpoint_reinsert_addr) ());
	      fbsd_resume_one_process (&event_child->head, 0, 0);
	    }

	  continue;
	}

      /* If we were single-stepping, we definitely want to report the
	 SIGTRAP.  The single-step operation has completed, so also
         clear the stepping flag; in general this does not matter,
	 because the SIGTRAP will be reported to the client, which
	 will give us a new action for this thread, but clear it for
	 consistency anyway.  It's safe to clear the stepping flag
         because the only consumer of get_stop_pc () after this point
	 is check_removed_breakpoint, and pending_is_breakpoint is not
	 set.  It might be wiser to use a step_completed flag instead.  */
      if (event_child->stepping)
	{
	  event_child->stepping = 0;
	  return wstat;
	}

      /* A SIGTRAP that we can't explain.  It may have been a breakpoint.
	 Check if it is a breakpoint, and if so mark the process information
	 accordingly.  This will handle both the necessary fiddling with the
	 PC on decr_pc_after_break targets and suppressing extra threads
	 hitting a breakpoint if two hit it at once and then GDB removes it
	 after the first is reported.  Arguably it would be better to report
	 multiple threads hitting breakpoints simultaneously, but the current
	 remote protocol does not allow this.  */
      if ((*the_low_target.breakpoint_at) (stop_pc))
	{
	  event_child->pending_is_breakpoint = 1;
	  event_child->pending_stop_pc = stop_pc;
	}

      return wstat;
    }

  /* NOTREACHED */
  return 0;
}

/* Wait for process, returns status.  */

static unsigned char
fbsd_wait (char *status)
{
  int w;
  struct thread_info *child = NULL;

retry:
  /* If we were only supposed to resume one thread, only wait for
     that thread - if it's still alive.  If it died, however - which
     can happen if we're coming from the thread death case below -
     then we need to make sure we restart the other threads.  We could
     pick a thread at random or restart all; restarting all is less
     arbitrary.  */
  if (cont_thread > 0)
    {
      child = (struct thread_info *) find_inferior_id (&all_threads,
						       cont_thread);

      /* No stepping, no signal - unless one is pending already, of course.  */
      if (child == NULL)
	{
	  struct thread_resume resume_info;
	  resume_info.thread = -1;
	  resume_info.step = resume_info.sig = resume_info.leave_stopped = 0;
	  fbsd_resume (&resume_info);
	}
    }

  enable_async_io ();
  unblock_async_io ();
  w = fbsd_wait_for_event (child);
  stop_all_processes ();
  disable_async_io ();

  /* If we are waiting for a particular child, and it exited,
     fbsd_wait_for_event will return its exit status.  Similarly if
     the last child exited.  If this is not the last child, however,
     do not report it as exited until there is a 'thread exited' response
     available in the remote protocol.  Instead, just wait for another event.
     This should be safe, because if the thread crashed we will already
     have reported the termination signal to GDB; that should stop any
     in-progress stepping operations, etc.

     Report the exit status of the last thread to exit.  This matches
     LinuxThreads' behavior.  */

  if (all_threads.head == all_threads.tail)
    {
      if (WIFEXITED (w))
	{
	  fprintf (stderr, "\nChild exited with retcode = %x \n", WEXITSTATUS (w));
	  *status = 'W';
	  clear_inferiors ();
	  free (all_processes.head);
	  all_processes.head = all_processes.tail = NULL;
	  return ((unsigned char) WEXITSTATUS (w));
	}
      else if (!WIFSTOPPED (w))
	{
	  fprintf (stderr, "\nChild terminated with signal = %x \n", WTERMSIG (w));
	  *status = 'X';
	  clear_inferiors ();
	  free (all_processes.head);
	  all_processes.head = all_processes.tail = NULL;
	  return ((unsigned char) WTERMSIG (w));
	}
    }
  else
    {
      if (!WIFSTOPPED (w))
	goto retry;
    }

  *status = 'T';
  return ((unsigned char) WSTOPSIG (w));
}

static void
send_sigstop (struct inferior_list_entry *entry)
{
  struct process_info *process = (struct process_info *) entry;

  if (process->stopped)
    return;

  /* If we already have a pending stop signal for this process, don't
     send another.  */
  if (process->stop_expected)
    {
      process->stop_expected = 0;
      return;
    }

  if (debug_threads)
    fprintf (stderr, "Sending sigstop to process %d\n", process->head.id);

  kill (process->head.id, SIGSTOP);
  process->sigstop_sent = 1;
}

static void
wait_for_sigstop (struct inferior_list_entry *entry)
{
  struct process_info *process = (struct process_info *) entry;
  struct thread_info *saved_inferior, *thread;
  int wstat, saved_tid;

  if (process->stopped)
    return;

  saved_inferior = current_inferior;
  saved_tid = ((struct inferior_list_entry *) saved_inferior)->id;
  thread = (struct thread_info *) find_inferior_id (&all_threads,
						    process->tid);
  wstat = fbsd_wait_for_event (thread);

  /* If we stopped with a non-SIGSTOP signal, save it for later
     and record the pending SIGSTOP.  If the process exited, just
     return.  */
  if (WIFSTOPPED (wstat)
      && WSTOPSIG (wstat) != SIGSTOP)
    {
      if (debug_threads)
	fprintf (stderr, "Stopped with non-sigstop signal\n");
      process->status_pending_p = 1;
      process->status_pending = wstat;
      process->stop_expected = 1;
    }

  if (fbsd_thread_alive (saved_tid))
    current_inferior = saved_inferior;
  else
    {
      if (debug_threads)
	fprintf (stderr, "Previously current thread died.\n");

      /* Set a valid thread as current.  */
      set_desired_inferior (0);
    }
}

static void
stop_all_processes (void)
{
  stopping_threads = 1;
  for_each_inferior (&all_processes, send_sigstop);
  for_each_inferior (&all_processes, wait_for_sigstop);
  stopping_threads = 0;
}

/* Resume execution of the inferior process.
   If STEP is nonzero, single-step it.
   If SIGNAL is nonzero, give it that signal.  */

static void
fbsd_resume_one_process (struct inferior_list_entry *entry,
			  int step, int signal)
{
  struct process_info *process = (struct process_info *) entry;
  struct thread_info *saved_inferior;

  if (process->stopped == 0)
    return;

  /* If we have pending signals or status, and a new signal, enqueue the
     signal.  Also enqueue the signal if we are waiting to reinsert a
     breakpoint; it will be picked up again below.  */
  if (signal != 0
      && (process->status_pending_p || process->pending_signals != NULL
	  || process->bp_reinsert != 0))
    {
      struct pending_signals *p_sig;
      p_sig = malloc (sizeof (*p_sig));
      p_sig->prev = process->pending_signals;
      p_sig->signal = signal;
      process->pending_signals = p_sig;
    }

  if (process->status_pending_p && !check_removed_breakpoint (process))
    return;

  saved_inferior = current_inferior;
  current_inferior = get_process_thread (process);

  if (debug_threads)
    fprintf (stderr, "Resuming process %d (%s, signal %d, stop %s)\n", inferior_pid,
	     step ? "step" : "continue", signal,
	     process->stop_expected ? "expected" : "not expected");

  /* This bit needs some thinking about.  If we get a signal that
     we must report while a single-step reinsert is still pending,
     we often end up resuming the thread.  It might be better to
     (ew) allow a stack of pending events; then we could be sure that
     the reinsert happened right away and not lose any signals.

     Making this stack would also shrink the window in which breakpoints are
     uninserted (see comment in fbsd_wait_for_process) but not enough for
     complete correctness, so it won't solve that problem.  It may be
     worthwhile just to solve this one, however.  */
  if (process->bp_reinsert != 0)
    {
      if (debug_threads)
	fprintf (stderr, "  pending reinsert at %08lx", (long)process->bp_reinsert);
      if (step == 0)
	fprintf (stderr, "BAD - reinserting but not stepping.\n");
      step = 1;

      /* Postpone any pending signal.  It was enqueued above.  */
      signal = 0;
    }

  check_removed_breakpoint (process);

  if (debug_threads && the_low_target.get_pc != NULL)
    {
      fprintf (stderr, "  ");
      (long) (*the_low_target.get_pc) ();
    }

  /* If we have pending signals, consume one unless we are trying to reinsert
     a breakpoint.  */
  if (process->pending_signals != NULL && process->bp_reinsert == 0)
    {
      struct pending_signals **p_sig;

      p_sig = &process->pending_signals;
      while ((*p_sig)->prev != NULL)
	p_sig = &(*p_sig)->prev;

      signal = (*p_sig)->signal;
      free (*p_sig);
      *p_sig = NULL;
    }

  regcache_invalidate_one ((struct inferior_list_entry *)
			   get_process_thread (process));
  errno = 0;
  process->stopped = 0;
  process->stepping = step;
  ptrace (step ? PT_STEP : PT_CONTINUE, process->lwpid, (PTRACE_ARG3_TYPE) 1, signal);

  current_inferior = saved_inferior;
  if (errno)
    perror_with_name ("ptrace");
}

static struct thread_resume *resume_ptr;

/* This function is called once per thread.  We look up the thread
   in RESUME_PTR, and mark the thread with a pointer to the appropriate
   resume request.

   This algorithm is O(threads * resume elements), but resume elements
   is small (and will remain small at least until GDB supports thread
   suspension).  */
static void
fbsd_set_resume_request (struct inferior_list_entry *entry)
{
  struct process_info *process;
  struct thread_info *thread;
  int ndx;

  thread = (struct thread_info *) entry;
  process = get_thread_process (thread);

  ndx = 0;
  while (resume_ptr[ndx].thread != -1 && resume_ptr[ndx].thread != entry->id)
    ndx++;

  process->resume = &resume_ptr[ndx];
}

/* This function is called once per thread.  We check the thread's resume
   request, which will tell us whether to resume, step, or leave the thread
   stopped; and what signal, if any, it should be sent.  For threads which
   we aren't explicitly told otherwise, we preserve the stepping flag; this
   is used for stepping over gdbserver-placed breakpoints.  */

static void
fbsd_continue_one_thread (struct inferior_list_entry *entry)
{
  struct process_info *process;
  struct thread_info *thread;
  int step;

  thread = (struct thread_info *) entry;
  process = get_thread_process (thread);

  if (process->resume->leave_stopped)
    return;

  if (process->resume->thread == -1)
    step = process->stepping || process->resume->step;
  else
    step = process->resume->step;

  fbsd_resume_one_process (&process->head, step, process->resume->sig);

  process->resume = NULL;
}

/* This function is called once per thread.  We check the thread's resume
   request, which will tell us whether to resume, step, or leave the thread
   stopped; and what signal, if any, it should be sent.  We queue any needed
   signals, since we won't actually resume.  We already have a pending event
   to report, so we don't need to preserve any step requests; they should
   be re-issued if necessary.  */

static void
fbsd_queue_one_thread (struct inferior_list_entry *entry)
{
  struct process_info *process;
  struct thread_info *thread;

  thread = (struct thread_info *) entry;
  process = get_thread_process (thread);

  if (process->resume->leave_stopped)
    return;

  /* If we have a new signal, enqueue the signal.  */
  if (process->resume->sig != 0)
    {
      struct pending_signals *p_sig;
      p_sig = malloc (sizeof (*p_sig));
      p_sig->prev = process->pending_signals;
      p_sig->signal = process->resume->sig;
      process->pending_signals = p_sig;
    }

  process->resume = NULL;
}

/* Set DUMMY if this process has an interesting status pending.  */
static int
resume_status_pending_p (struct inferior_list_entry *entry, void *flag_p)
{
  struct process_info *process = (struct process_info *) entry;

  /* Processes which will not be resumed are not interesting, because
     we might not wait for them next time through fbsd_wait.  */
  if (process->resume->leave_stopped)
    return 0;

  /* If this thread has a removed breakpoint, we won't have any
     events to report later, so check now.  check_removed_breakpoint
     may clear status_pending_p.  We avoid calling check_removed_breakpoint
     for any thread that we are not otherwise going to resume - this
     lets us preserve stopped status when two threads hit a breakpoint.
     GDB removes the breakpoint to single-step a particular thread
     past it, then re-inserts it and resumes all threads.  We want
     to report the second thread without resuming it in the interim.  */
  if (process->status_pending_p)
    check_removed_breakpoint (process);

  if (process->status_pending_p)
    * (int *) flag_p = 1;

  return 0;
}

static void
fbsd_resume (struct thread_resume *resume_info)
{
  int pending_flag;

  /* Yes, the use of a global here is rather ugly.  */
  resume_ptr = resume_info;

  for_each_inferior (&all_threads, fbsd_set_resume_request);

  /* If there is a thread which would otherwise be resumed, which
     has a pending status, then don't resume any threads - we can just
     report the pending status.  Make sure to queue any signals
     that would otherwise be sent.  */
  pending_flag = 0;
  find_inferior (&all_processes, resume_status_pending_p, &pending_flag);

  if (debug_threads)
    {
      if (pending_flag)
	fprintf (stderr, "Not resuming, pending status\n");
      else
	fprintf (stderr, "Resuming, no pending status\n");
    }

  if (pending_flag)
    for_each_inferior (&all_threads, fbsd_queue_one_thread);
  else
    {
      block_async_io ();
      enable_async_io ();
      for_each_inferior (&all_threads, fbsd_continue_one_thread);
    }
}


static int
regsets_fetch_inferior_registers ()
{
  struct regset_info *regset;

  regset = target_regsets;

  while (regset->size >= 0)
    {
      void *buf;
      int res;

      if (regset->size == 0)
	{
	  regset ++;
	  continue;
	}

      buf = malloc (regset->size);
      res = ptrace (regset->get_request, inferior_pid, (PTRACE_ARG3_TYPE) buf, 0);
      if (res < 0)
	{
	  char s[256];
	  sprintf (s, "ptrace(regsets_fetch_inferior_registers) PID=%d",
		   inferior_pid);
	  perror (s);
	}
      regset->store_function (buf);
      regset ++;
    }
  return 0;
}

static int
regsets_store_inferior_registers ()
{
  struct regset_info *regset;

  regset = target_regsets;

  while (regset->size >= 0)
    {
      void *buf;
      int res;

      if (regset->size == 0)
	{
	  regset ++;
	  continue;
	}

      buf = malloc (regset->size);
      regset->fill_function (buf);
      res = ptrace (regset->set_request, inferior_pid, (PTRACE_ARG3_TYPE) buf, 0);
      if (res < 0)
	{
	  perror ("Warning: ptrace(regsets_store_inferior_registers)");
	}
      regset ++;
      free (buf);
    }
  return 0;
}

void
fbsd_fetch_registers (int regno)
{
      regsets_fetch_inferior_registers ();
}

void
fbsd_store_registers (int regno)
{
      regsets_store_inferior_registers ();
}


/* Copy LEN bytes from inferior's memory starting at MEMADDR
   to debugger memory starting at MYADDR.  */

static int
fbsd_read_memory (CORE_ADDR memaddr, char *myaddr, int len)
{
  register int i;
  /* Round starting address down to longword boundary.  */
  register CORE_ADDR addr = memaddr & -(CORE_ADDR) sizeof (PTRACE_XFER_TYPE);
  /* Round ending address up; get number of longwords that makes.  */
  register int count
    = (((memaddr + len) - addr) + sizeof (PTRACE_XFER_TYPE) - 1)
      / sizeof (PTRACE_XFER_TYPE);
  /* Allocate buffer of that many longwords.  */
  register PTRACE_XFER_TYPE *buffer
    = (PTRACE_XFER_TYPE *) alloca (count * sizeof (PTRACE_XFER_TYPE));

  /* Read all the longwords */
  for (i = 0; i < count; i++, addr += sizeof (PTRACE_XFER_TYPE))
    {
      errno = 0;
      buffer[i] = ptrace (PT_READ_D, inferior_pid, (PTRACE_ARG3_TYPE) (intptr_t)addr, 0);
      if (errno)
	return errno;
    }

  /* Copy appropriate bytes out of the buffer.  */
  memcpy (myaddr, (char *) buffer + (memaddr & (sizeof (PTRACE_XFER_TYPE) - 1)), len);

  return 0;
}

/* Copy LEN bytes of data from debugger memory at MYADDR
   to inferior's memory at MEMADDR.
   On failure (cannot write the inferior)
   returns the value of errno.  */

static int
fbsd_write_memory (CORE_ADDR memaddr, const char *myaddr, int len)
{
  register int i;
  /* Round starting address down to longword boundary.  */
  register CORE_ADDR addr = memaddr & -(CORE_ADDR) sizeof (PTRACE_XFER_TYPE);
  /* Round ending address up; get number of longwords that makes.  */
  register int count
  = (((memaddr + len) - addr) + sizeof (PTRACE_XFER_TYPE) - 1) / sizeof (PTRACE_XFER_TYPE);
  /* Allocate buffer of that many longwords.  */
  register PTRACE_XFER_TYPE *buffer = (PTRACE_XFER_TYPE *) alloca (count * sizeof (PTRACE_XFER_TYPE));
  extern int errno;

  if (debug_threads)
    {
      fprintf (stderr, "Writing %02x to %08lx\n", (unsigned)myaddr[0], (long)memaddr);
    }

  /* Fill start and end extra bytes of buffer with existing memory data.  */

  buffer[0] = ptrace (PT_READ_D, inferior_pid,
		      (PTRACE_ARG3_TYPE) (intptr_t)addr, 0);

  if (count > 1)
    {
      buffer[count - 1]
	= ptrace (PT_READ_D, inferior_pid,
		  (PTRACE_ARG3_TYPE) (intptr_t) (addr + (count - 1)
				      * sizeof (PTRACE_XFER_TYPE)),
		  0);
    }

  /* Copy data to be written over corresponding part of buffer */

  memcpy ((char *) buffer + (memaddr & (sizeof (PTRACE_XFER_TYPE) - 1)), myaddr, len);

  /* Write the entire buffer.  */

  for (i = 0; i < count; i++, addr += sizeof (PTRACE_XFER_TYPE))
    {
      errno = 0;
      ptrace (PT_WRITE_D, inferior_pid, (PTRACE_ARG3_TYPE) (intptr_t)addr, buffer[i]);
      if (errno)
	return errno;
    }

  return 0;
}

static void
fbsd_look_up_symbols (void)
{
#ifdef USE_THREAD_DB
  if (using_threads)
    return;

  using_threads = thread_db_init ();
#endif
}

static void
fbsd_send_signal (int signum)
{
  extern int signal_pid;

  if (cont_thread > 0)
    {
      struct process_info *process;

      process = get_thread_process (current_inferior);
      kill (process->lwpid, signum);
    }
  else
    kill (signal_pid, signum);
}

/* Copy LEN bytes from inferior's auxiliary vector starting at OFFSET
   to debugger memory starting at MYADDR.  */

static int
fbsd_read_auxv (CORE_ADDR offset, char *myaddr, unsigned int len)
{
  char filename[PATH_MAX];
  int fd, n;

  snprintf (filename, sizeof filename, "/proc/%d/auxv", inferior_pid);

  fd = open (filename, O_RDONLY);
  if (fd < 0)
    return -1;

  if (offset != (CORE_ADDR) 0
      && lseek (fd, (off_t) offset, SEEK_SET) != (off_t) offset)
    n = -1;
  else
    n = read (fd, myaddr, len);

  close (fd);

  return n;
}


static struct target_ops fbsd_target_ops = {
  fbsd_create_inferior,
  fbsd_attach,
  fbsd_kill,
  fbsd_detach,
  fbsd_thread_alive,
  fbsd_resume,
  fbsd_wait,
  fbsd_fetch_registers,
  fbsd_store_registers,
  fbsd_read_memory,
  fbsd_write_memory,
  fbsd_look_up_symbols,
  fbsd_send_signal,
  fbsd_read_auxv,
};

static void
fbsd_init_signals ()
{
}

void
initialize_low (void)
{
  using_threads = 0;
  set_target_ops (&fbsd_target_ops);
  set_breakpoint_data (the_low_target.breakpoint,
		       the_low_target.breakpoint_len);
  init_registers ();
  fbsd_init_signals ();
}
