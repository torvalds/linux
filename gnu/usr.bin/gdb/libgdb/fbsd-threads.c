/* $FreeBSD$ */
/* FreeBSD libthread_db assisted debugging support.
   Copyright 1999, 2000, 2001 Free Software Foundation, Inc.

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

#include <dlfcn.h>
#include <sys/types.h>
#include <sys/ptrace.h>
#include <signal.h>

#include "proc_service.h"
#include "thread_db.h"

#include "defs.h"
#include "bfd.h"
#include "elf-bfd.h"
#include "gdb_assert.h"
#include "gdbcore.h"
#include "gdbthread.h"
#include "inferior.h"
#include "objfiles.h"
#include "regcache.h"
#include "symfile.h"
#include "symtab.h"
#include "target.h"
#include "gdbcmd.h"
#include "solib-svr4.h"

#include "gregset.h"
#ifdef PT_GETXMMREGS
#include "i387-tdep.h"
#endif

#define LIBTHREAD_DB_SO "libthread_db.so"

struct ps_prochandle
{
  pid_t pid;
};

extern int child_suppress_run;

extern struct target_ops child_ops;

/* This module's target vectors.  */
static struct target_ops fbsd_thread_ops;
static struct target_ops fbsd_core_ops;

/* Saved copy of orignal core_ops. */
static struct target_ops orig_core_ops;
extern struct target_ops core_ops;

/* Pointer to the next function on the objfile event chain.  */
static void (*target_new_objfile_chain) (struct objfile *objfile);

/* Non-zero if there is a thread module */
static int fbsd_thread_present;

/* Non-zero if we're using this module's target vector.  */
static int fbsd_thread_active;

/* Non-zero if core_open is called */
static int fbsd_thread_core = 0;

/* Non-zero if we have to keep this module's target vector active
   across re-runs.  */
static int keep_thread_db;

/* Structure that identifies the child process for the
   <proc_service.h> interface.  */
static struct ps_prochandle proc_handle;

/* Connection to the libthread_db library.  */
static td_thragent_t *thread_agent;

/* The last thread we are single stepping */
static ptid_t last_single_step_thread;

/* Pointers to the libthread_db functions.  */

static td_err_e (*td_init_p) (void);

static td_err_e (*td_ta_new_p) (struct ps_prochandle *ps, td_thragent_t **ta);
static td_err_e (*td_ta_delete_p) (td_thragent_t *);
static td_err_e (*td_ta_map_id2thr_p) (const td_thragent_t *ta, thread_t pt,
				       td_thrhandle_t *__th);
static td_err_e (*td_ta_map_lwp2thr_p) (const td_thragent_t *ta, lwpid_t lwpid,
					td_thrhandle_t *th);
static td_err_e (*td_ta_thr_iter_p) (const td_thragent_t *ta,
				     td_thr_iter_f *callback,
				     void *cbdata_p, td_thr_state_e state,
				     int ti_pri, sigset_t *ti_sigmask_p,
				     unsigned int ti_user_flags);
static td_err_e (*td_ta_event_addr_p) (const td_thragent_t *ta,
				       td_event_e event, td_notify_t *ptr);
static td_err_e (*td_ta_set_event_p) (const td_thragent_t *ta,
				      td_thr_events_t *event);
static td_err_e (*td_ta_event_getmsg_p) (const td_thragent_t *ta,
					 td_event_msg_t *msg);
static td_err_e (*td_thr_get_info_p) (const td_thrhandle_t *th,
				      td_thrinfo_t *infop);
#ifdef PT_GETXMMREGS
static td_err_e (*td_thr_getxmmregs_p) (const td_thrhandle_t *th,
					char *regset);
#endif
static td_err_e (*td_thr_getfpregs_p) (const td_thrhandle_t *th,
				       prfpregset_t *regset);
static td_err_e (*td_thr_getgregs_p) (const td_thrhandle_t *th,
				      prgregset_t gregs);
#ifdef PT_GETXMMREGS
static td_err_e (*td_thr_setxmmregs_p) (const td_thrhandle_t *th,
					const char *fpregs);
#endif
static td_err_e (*td_thr_setfpregs_p) (const td_thrhandle_t *th,
				       const prfpregset_t *fpregs);
static td_err_e (*td_thr_setgregs_p) (const td_thrhandle_t *th,
				      prgregset_t gregs);
static td_err_e (*td_thr_event_enable_p) (const td_thrhandle_t *th, int event);

static td_err_e (*td_thr_sstep_p) (td_thrhandle_t *th, int step);

static td_err_e (*td_ta_tsd_iter_p) (const td_thragent_t *ta,
				 td_key_iter_f *func, void *data);
static td_err_e (*td_thr_tls_get_addr_p) (const td_thrhandle_t *th,
                                          void *map_address,
                                          size_t offset, void **address);
static td_err_e (*td_thr_dbsuspend_p) (const td_thrhandle_t *);
static td_err_e (*td_thr_dbresume_p) (const td_thrhandle_t *);

static CORE_ADDR td_create_bp_addr;

/* Location of the thread death event breakpoint.  */
static CORE_ADDR td_death_bp_addr;

/* Prototypes for local functions.  */
static void fbsd_thread_find_new_threads (void);
static int fbsd_thread_alive (ptid_t ptid);
static void attach_thread (ptid_t ptid, const td_thrhandle_t *th_p,
               const td_thrinfo_t *ti_p, int verbose);
static void fbsd_thread_detach (char *args, int from_tty);

/* Building process ids.  */

#define GET_PID(ptid)		ptid_get_pid (ptid)
#define GET_LWP(ptid)		ptid_get_lwp (ptid)
#define GET_THREAD(ptid)	ptid_get_tid (ptid)

#define IS_LWP(ptid)		(GET_LWP (ptid) != 0)
#define IS_THREAD(ptid)		(GET_THREAD (ptid) != 0)

#define BUILD_LWP(lwp, pid)	ptid_build (pid, lwp, 0)
#define BUILD_THREAD(tid, pid)	ptid_build (pid, 0, tid)

static char *
thread_db_err_str (td_err_e err)
{
  static char buf[64];

  switch (err)
    {
    case TD_OK:
      return "generic 'call succeeded'";
    case TD_ERR:
      return "generic error";
    case TD_NOTHR:
      return "no thread to satisfy query";
    case TD_NOSV:
      return "no sync handle to satisfy query";
    case TD_NOLWP:
      return "no LWP to satisfy query";
    case TD_BADPH:
      return "invalid process handle";
    case TD_BADTH:
      return "invalid thread handle";
    case TD_BADSH:
      return "invalid synchronization handle";
    case TD_BADTA:
      return "invalid thread agent";
    case TD_BADKEY:
      return "invalid key";
    case TD_NOMSG:
      return "no event message for getmsg";
    case TD_NOFPREGS:
      return "FPU register set not available";
    case TD_NOLIBTHREAD:
      return "application not linked with libthread";
    case TD_NOEVENT:
      return "requested event is not supported";
    case TD_NOCAPAB:
      return "capability not available";
    case TD_DBERR:
      return "debugger service failed";
    case TD_NOAPLIC:
      return "operation not applicable to";
    case TD_NOTSD:
      return "no thread-specific data for this thread";
    case TD_MALLOC:
      return "malloc failed";
    case TD_PARTIALREG:
      return "only part of register set was written/read";
    case TD_NOXREGS:
      return "X register set not available for this thread";
    default:
      snprintf (buf, sizeof (buf), "unknown thread_db error '%d'", err);
      return buf;
    }
}

static char *
thread_db_state_str (td_thr_state_e state)
{
  static char buf[64];

  switch (state)
    {
    case TD_THR_STOPPED:
      return "stopped by debugger";
    case TD_THR_RUN:
      return "runnable";
    case TD_THR_ACTIVE:
      return "active";
    case TD_THR_ZOMBIE:
      return "zombie";
    case TD_THR_SLEEP:
      return "sleeping";
    case TD_THR_STOPPED_ASLEEP:
      return "stopped by debugger AND blocked";
    default:
      snprintf (buf, sizeof (buf), "unknown thread_db state %d", state);
      return buf;
    }
}

/* Convert LWP to user-level thread id. */
static ptid_t
thread_from_lwp (ptid_t ptid, td_thrhandle_t *th, td_thrinfo_t *ti)
{
  td_err_e err;
 
  gdb_assert (IS_LWP (ptid));

  if (fbsd_thread_active)
    {
      err = td_ta_map_lwp2thr_p (thread_agent, GET_LWP (ptid), th);
      if (err == TD_OK)
        {
          err = td_thr_get_info_p (th, ti);
          if (err != TD_OK)
            error ("Cannot get thread info: %s", thread_db_err_str (err));
          return BUILD_THREAD (ti->ti_tid, GET_PID (ptid));
        }
    }

  /* the LWP is not mapped to user thread */  
  return BUILD_LWP (GET_LWP (ptid), GET_PID (ptid));
}

static void
fbsd_core_get_first_lwp (bfd *abfd, asection *asect, void *obj)
{
  if (strncmp (bfd_section_name (abfd, asect), ".reg/", 5) != 0)
    return;

  if (*(lwpid_t *)obj != 0)
    return;

  *(lwpid_t *)obj = atoi (bfd_section_name (abfd, asect) + 5);
}

static long
get_current_lwp (int pid)
{
  struct ptrace_lwpinfo pl;
  lwpid_t lwpid;

  if (!target_has_execution)
    {
      lwpid = 0;
      bfd_map_over_sections (core_bfd, fbsd_core_get_first_lwp, &lwpid);
      return lwpid;
    }
  if (ptrace (PT_LWPINFO, pid, (caddr_t)&pl, sizeof(pl)))
    perror_with_name("PT_LWPINFO");

  return (long)pl.pl_lwpid;
}

static void
get_current_thread ()
{
  td_thrhandle_t th;
  td_thrinfo_t ti;
  long lwp;
  ptid_t tmp, ptid;

  lwp = get_current_lwp (proc_handle.pid);
  tmp = BUILD_LWP (lwp, proc_handle.pid);
  ptid = thread_from_lwp (tmp, &th, &ti);
  if (!in_thread_list (ptid))
    {
      attach_thread (ptid, &th, &ti, 1);
    }
  inferior_ptid = ptid;
}

static td_err_e
enable_thread_event (td_thragent_t *thread_agent, int event, CORE_ADDR *bp)
{
  td_notify_t notify;
  td_err_e err;

  /* Get the breakpoint address for thread EVENT.  */
  err = td_ta_event_addr_p (thread_agent, event, &notify);
  if (err != TD_OK)
    return err;

  /* Set up the breakpoint.  */
  (*bp) = gdbarch_convert_from_func_ptr_addr (current_gdbarch,
            extract_typed_address(&notify.u.bptaddr, builtin_type_void_func_ptr),
            &current_target);
  create_thread_event_breakpoint ((*bp));

  return TD_OK;
}

static void
enable_thread_event_reporting (void)
{
  td_thr_events_t events;
  td_notify_t notify;
  td_err_e err;

  /* We cannot use the thread event reporting facility if these
     functions aren't available.  */
  if (td_ta_event_addr_p == NULL || td_ta_set_event_p == NULL
      || td_ta_event_getmsg_p == NULL || td_thr_event_enable_p == NULL)
    return;

  /* Set the process wide mask saying which events we're interested in.  */
  td_event_emptyset (&events);
  td_event_addset (&events, TD_CREATE);
  td_event_addset (&events, TD_DEATH);

  err = td_ta_set_event_p (thread_agent, &events);
  if (err != TD_OK)
    {
      warning ("Unable to set global thread event mask: %s",
	       thread_db_err_str (err));
      return;
    }

  /* Delete previous thread event breakpoints, if any.  */
  remove_thread_event_breakpoints ();
  td_create_bp_addr = 0;
  td_death_bp_addr = 0;

  /* Set up the thread creation event.  */
  err = enable_thread_event (thread_agent, TD_CREATE, &td_create_bp_addr);
  if (err != TD_OK)
    {
      warning ("Unable to get location for thread creation breakpoint: %s",
	       thread_db_err_str (err));
      return;
    }

  /* Set up the thread death event.  */
  err = enable_thread_event (thread_agent, TD_DEATH, &td_death_bp_addr);
  if (err != TD_OK)
    {
      warning ("Unable to get location for thread death breakpoint: %s",
	       thread_db_err_str (err));
      return;
    }
}

static void
disable_thread_event_reporting (void)
{
  td_thr_events_t events;

  /* Set the process wide mask saying we aren't interested in any
     events anymore.  */
  td_event_emptyset (&events);
  td_ta_set_event_p (thread_agent, &events);

  /* Delete thread event breakpoints, if any.  */
  remove_thread_event_breakpoints ();
  td_create_bp_addr = 0;
  td_death_bp_addr = 0;
}

static void
fbsd_thread_activate (void)
{
  fbsd_thread_active = 1;
  init_thread_list();
  if (fbsd_thread_core == 0)
    enable_thread_event_reporting ();
  fbsd_thread_find_new_threads ();
  get_current_thread ();
}

static void
fbsd_thread_deactivate (void)
{
  if (fbsd_thread_core == 0)
    disable_thread_event_reporting();
  td_ta_delete_p (thread_agent);

  inferior_ptid = pid_to_ptid (proc_handle.pid);
  proc_handle.pid = 0;
  fbsd_thread_active = 0;
  fbsd_thread_present = 0;
  init_thread_list ();
}

static char * 
fbsd_thread_get_name (lwpid_t lwpid)
{
  static char last_thr_name[MAXCOMLEN + 1];
  char section_name[32];
  struct ptrace_lwpinfo lwpinfo;
  bfd_size_type size;
  struct bfd_section *section;

  if (target_has_execution)
    {
      if (ptrace (PT_LWPINFO, lwpid, (caddr_t)&lwpinfo, sizeof (lwpinfo)) == -1)
        goto fail;
      strncpy (last_thr_name, lwpinfo.pl_tdname, sizeof (last_thr_name) - 1);
    }
  else
    {
      snprintf (section_name, sizeof (section_name), ".tname/%u", lwpid);
      section = bfd_get_section_by_name (core_bfd, section_name);
      if (! section)
        goto fail;

      /* Section size fix-up. */
      size = bfd_section_size (core_bfd, section);
      if (size > sizeof (last_thr_name))
        size = sizeof (last_thr_name);

      if (! bfd_get_section_contents (core_bfd, section, last_thr_name,
	       (file_ptr)0, size))
        goto fail;
      if (last_thr_name[0] == '\0')
        goto fail;
    }
    last_thr_name[sizeof (last_thr_name) - 1] = '\0';
    return last_thr_name;
fail:
     strcpy (last_thr_name, "<unknown>");
     return last_thr_name;
}

static void
fbsd_thread_new_objfile (struct objfile *objfile)
{
  td_err_e err;

  if (objfile == NULL)
    {
      /* All symbols have been discarded.  If the thread_db target is
         active, deactivate it now.  */
      if (fbsd_thread_active)
        {
          gdb_assert (proc_handle.pid == 0);
          fbsd_thread_active = 0;
        }

      goto quit;
    }

  if (!child_suppress_run)
    goto quit;

  /* Nothing to do.  The thread library was already detected and the
     target vector was already activated.  */
  if (fbsd_thread_active)
    goto quit;

  /* Initialize the structure that identifies the child process.  Note
     that at this point there is no guarantee that we actually have a
     child process.  */
  proc_handle.pid = GET_PID (inferior_ptid);
  
  /* Now attempt to open a connection to the thread library.  */
  err = td_ta_new_p (&proc_handle, &thread_agent);
  switch (err)
    {
    case TD_NOLIBTHREAD:
      /* No thread library was detected.  */
      break;

    case TD_OK:
      /* The thread library was detected.  Activate the thread_db target.  */
      fbsd_thread_present = 1;

      /* We can only poke around if there actually is a child process.
         If there is no child process alive, postpone the steps below
         until one has been created.  */
      if (fbsd_thread_core == 0 && proc_handle.pid != 0)
        {
          push_target(&fbsd_thread_ops);
          fbsd_thread_activate();
        }
      else
        {
          td_ta_delete_p(thread_agent);
          thread_agent = NULL;
        }
      break;

    default:
      warning ("Cannot initialize thread debugging library: %s",
               thread_db_err_str (err));
      break;
    }

 quit:
  if (target_new_objfile_chain)
    target_new_objfile_chain (objfile);
}

static void
fbsd_thread_attach (char *args, int from_tty)
{
  fbsd_thread_core = 0;

  child_ops.to_attach (args, from_tty);

  /* Must get symbols from solibs before libthread_db can run! */
  SOLIB_ADD ((char *) 0, from_tty, (struct target_ops *) 0, auto_solib_add);

  if (fbsd_thread_present && !fbsd_thread_active)
    push_target(&fbsd_thread_ops);
}

static void
fbsd_thread_post_attach (int pid)
{
  child_ops.to_post_attach (pid);

  if (fbsd_thread_present && !fbsd_thread_active)
    {
      proc_handle.pid = GET_PID (inferior_ptid);
      fbsd_thread_activate ();
    }
}

static void
fbsd_thread_detach (char *args, int from_tty)
{
  fbsd_thread_deactivate ();
  unpush_target (&fbsd_thread_ops);

  /* Clear gdb solib information and symbol file
     cache, so that after detach and re-attach, new_objfile
     hook will be called */

  clear_solib();
  symbol_file_clear(0);
  proc_handle.pid = 0;
  child_ops.to_detach (args, from_tty);
}

static int
suspend_thread_callback (const td_thrhandle_t *th_p, void *data)
{
  int err = td_thr_dbsuspend_p (th_p);
  if (err != 0)
	fprintf_filtered(gdb_stderr, "%s %s\n", __func__, thread_db_err_str (err));
  return (err);
}

static int
resume_thread_callback (const td_thrhandle_t *th_p, void *data)
{
  int err = td_thr_dbresume_p (th_p);
  if (err != 0)
	fprintf_filtered(gdb_stderr, "%s %s\n", __func__, thread_db_err_str (err));
  return (err);
}

static void
fbsd_thread_resume (ptid_t ptid, int step, enum target_signal signo)
{
  td_thrhandle_t th;
  td_thrinfo_t ti;
  ptid_t work_ptid;
  int resume_all, ret;
  long lwp, thvalid = 0;

  if (!fbsd_thread_active)
    {
      child_ops.to_resume (ptid, step, signo);
      return;
    }

  if (GET_PID(ptid) != -1 && step != 0)
    {
      resume_all = 0;
      work_ptid = ptid;
    }
  else
    {
      resume_all = 1;
      work_ptid = inferior_ptid;
    }

  lwp = GET_LWP (work_ptid);
  if (lwp == 0)
    {
      /* check user thread */
      ret = td_ta_map_id2thr_p (thread_agent, GET_THREAD(work_ptid), &th);
      if (ret)
        error (thread_db_err_str (ret));

      /* For M:N thread, we need to tell UTS to set/unset single step
         flag at context switch time, the flag will be written into
         thread mailbox. This becauses some architecture may not have
         machine single step flag in ucontext, so we put the flag in mailbox,
         when the thread switches back, kse_switchin restores the single step
         state.  */
      ret = td_thr_sstep_p (&th, step);
      if (ret)
        error (thread_db_err_str (ret));
      ret = td_thr_get_info_p (&th, &ti);
      if (ret)
        error (thread_db_err_str (ret));
      thvalid = 1;
      lwp = ti.ti_lid;
    }

  if (lwp)
    {
      int req = step ? PT_SETSTEP : PT_CLEARSTEP;
      if (ptrace (req, (pid_t) lwp, (caddr_t) 1, target_signal_to_host(signo)))
        perror_with_name ("PT_SETSTEP/PT_CLEARSTEP");
    }

  if (!ptid_equal (last_single_step_thread, null_ptid))
    {
       ret = td_ta_thr_iter_p (thread_agent, resume_thread_callback, NULL,
          TD_THR_ANY_STATE, TD_THR_LOWEST_PRIORITY,
          TD_SIGNO_MASK, TD_THR_ANY_USER_FLAGS);
      if (ret != TD_OK)
        error ("resume error: %s", thread_db_err_str (ret));
    }

  if (!resume_all)
    {
      ret = td_ta_thr_iter_p (thread_agent, suspend_thread_callback, NULL,
          TD_THR_ANY_STATE, TD_THR_LOWEST_PRIORITY,
          TD_SIGNO_MASK, TD_THR_ANY_USER_FLAGS);
      if (ret != TD_OK)
        error ("suspend error: %s", thread_db_err_str (ret));
      last_single_step_thread = work_ptid;
    }
  else
    last_single_step_thread = null_ptid;

  if (thvalid)
    {
      ret = td_thr_dbresume_p (&th);
      if (ret != TD_OK)
        error ("resume error: %s", thread_db_err_str (ret));
    }
  else
    {
      /* it is not necessary, put it here for completness */
      ret = ptrace(PT_RESUME, lwp, 0, 0);
    }

  /* now continue the process, suspended thread wont run */
  if (ptrace (PT_CONTINUE, proc_handle.pid , (caddr_t)1,
	      target_signal_to_host(signo)))
    perror_with_name ("PT_CONTINUE");
}

static void
attach_thread (ptid_t ptid, const td_thrhandle_t *th_p,
               const td_thrinfo_t *ti_p, int verbose)
{
  td_err_e err;

  /* Add the thread to GDB's thread list.  */
  if (!in_thread_list (ptid)) {
    add_thread (ptid);
    if (verbose)
      printf_unfiltered ("[New %s]\n", target_pid_to_str (ptid));
  }

  if (ti_p->ti_state == TD_THR_UNKNOWN || ti_p->ti_state == TD_THR_ZOMBIE)
    return;                     /* A zombie thread -- do not attach.  */

  if (! IS_THREAD(ptid))
    return;
  if (fbsd_thread_core != 0)
    return;
  /* Enable thread event reporting for this thread. */
  err = td_thr_event_enable_p (th_p, 1);
  if (err != TD_OK)
    error ("Cannot enable thread event reporting for %s: %s",
           target_pid_to_str (ptid), thread_db_err_str (err));
}

static void
detach_thread (ptid_t ptid, int verbose)
{
  if (verbose)
    printf_unfiltered ("[%s exited]\n", target_pid_to_str (ptid));
}

static void
check_event (ptid_t ptid)
{
  td_event_msg_t msg;
  td_thrinfo_t ti;
  td_err_e err;
  CORE_ADDR stop_pc;
  int loop = 0;

  /* Bail out early if we're not at a thread event breakpoint.  */
  stop_pc = read_pc_pid (ptid) - DECR_PC_AFTER_BREAK;
  if (stop_pc != td_create_bp_addr && stop_pc != td_death_bp_addr)
    return;
  loop = 1;

  do
    {
      err = td_ta_event_getmsg_p (thread_agent, &msg);
      if (err != TD_OK)
        {
	  if (err == TD_NOMSG)
	    return;
          error ("Cannot get thread event message: %s",
		 thread_db_err_str (err));
        }
      err = td_thr_get_info_p ((void *)(uintptr_t)msg.th_p, &ti);
      if (err != TD_OK)
        error ("Cannot get thread info: %s", thread_db_err_str (err));
      ptid = BUILD_THREAD (ti.ti_tid, GET_PID (ptid));
      switch (msg.event)
        {
        case TD_CREATE:
          /* We may already know about this thread, for instance when the
             user has issued the `info threads' command before the SIGTRAP
             for hitting the thread creation breakpoint was reported.  */
          attach_thread (ptid, (void *)(uintptr_t)msg.th_p, &ti, 1);
          break;
       case TD_DEATH:
         if (!in_thread_list (ptid))
           error ("Spurious thread death event.");
         detach_thread (ptid, 1);
         break;
       default:
          error ("Spurious thread event.");
       }
    }
  while (loop);
}

static ptid_t
fbsd_thread_wait (ptid_t ptid, struct target_waitstatus *ourstatus)
{
  ptid_t ret;
  long lwp;
  CORE_ADDR stop_pc;
  td_thrhandle_t th;
  td_thrinfo_t ti;

  ret = child_ops.to_wait (ptid, ourstatus);
  if (GET_PID(ret) >= 0 && ourstatus->kind == TARGET_WAITKIND_STOPPED)
    {
      lwp = get_current_lwp (GET_PID(ret));
      ret = thread_from_lwp (BUILD_LWP(lwp, GET_PID(ret)),
         &th, &ti);
      if (!in_thread_list(ret)) {
        /*
         * We have to enable event reporting for initial thread
         * which was not mapped before.
	 */
        attach_thread(ret, &th, &ti, 1);
      }
      if (ourstatus->value.sig == TARGET_SIGNAL_TRAP)
        check_event(ret);
      /* this is a hack, if an event won't cause gdb to stop, for example,
         SIGARLM, gdb resumes the process immediatly without setting
         inferior_ptid to the new thread returned here, this is a bug
         because inferior_ptid may already not exist there, and passing
         a none existing thread to fbsd_thread_resume causes error. */
      if (!fbsd_thread_alive (inferior_ptid))
        {
          delete_thread (inferior_ptid);
          inferior_ptid = ret;
        }
    }

  return (ret);
}

static int
fbsd_thread_xfer_memory (CORE_ADDR memaddr, char *myaddr, int len, int write,
                        struct mem_attrib *attrib, struct target_ops *target)
{
  int err;

  if (target_has_execution)
    err = child_ops.to_xfer_memory (memaddr, myaddr, len, write, attrib,
	target);
  else
    err = orig_core_ops.to_xfer_memory (memaddr, myaddr, len, write, attrib,
	target);

  return (err);
}

static void
fbsd_lwp_fetch_registers (int regno)
{
  gregset_t gregs;
  fpregset_t fpregs;
  lwpid_t lwp;
#ifdef PT_GETXMMREGS
  char xmmregs[512];
#endif

  if (!target_has_execution)
    {
      orig_core_ops.to_fetch_registers (-1);
      return;
    }

  /* XXX: We've replaced the pid with the lwpid for GDB's benefit. */
  lwp = GET_PID (inferior_ptid);

  if (ptrace (PT_GETREGS, lwp, (caddr_t) &gregs, 0) == -1)
    error ("Cannot get lwp %d registers: %s\n", lwp, safe_strerror (errno));
  supply_gregset (&gregs);
  
#ifdef PT_GETXMMREGS
  if (ptrace (PT_GETXMMREGS, lwp, xmmregs, 0) == 0)
    {
      i387_supply_fxsave (current_regcache, -1, xmmregs);
    }
  else
    {
#endif
      if (ptrace (PT_GETFPREGS, lwp, (caddr_t) &fpregs, 0) == -1)
	error ("Cannot get lwp %d registers: %s\n ", lwp, safe_strerror (errno));
      supply_fpregset (&fpregs);
#ifdef PT_GETXMMREGS
    }
#endif
}

static void
fbsd_thread_fetch_registers (int regno)
{
  prgregset_t gregset;
  prfpregset_t fpregset;
  td_thrhandle_t th;
  td_err_e err;
#ifdef PT_GETXMMREGS
  char xmmregs[512];
#endif

  if (!IS_THREAD (inferior_ptid))
    {
      fbsd_lwp_fetch_registers (regno);
      return;
    }

  err = td_ta_map_id2thr_p (thread_agent, GET_THREAD (inferior_ptid), &th);
  if (err != TD_OK)
    error ("Cannot find thread %d: Thread ID=%ld, %s",
           pid_to_thread_id (inferior_ptid),           
           GET_THREAD (inferior_ptid), thread_db_err_str (err));

  err = td_thr_getgregs_p (&th, gregset);
  if (err != TD_OK)
    error ("Cannot fetch general-purpose registers for thread %d: Thread ID=%ld, %s",
           pid_to_thread_id (inferior_ptid),
           GET_THREAD (inferior_ptid), thread_db_err_str (err));
#ifdef PT_GETXMMREGS
  err = td_thr_getxmmregs_p (&th, xmmregs);
  if (err == TD_OK)
    {
      i387_supply_fxsave (current_regcache, -1, xmmregs);
    }
  else
    {
#endif
      err = td_thr_getfpregs_p (&th, &fpregset);
      if (err != TD_OK)
	error ("Cannot get floating-point registers for thread %d: Thread ID=%ld, %s",
	       pid_to_thread_id (inferior_ptid),
	       GET_THREAD (inferior_ptid), thread_db_err_str (err));
      supply_fpregset (&fpregset);
#ifdef PT_GETXMMREGS
    }
#endif

  supply_gregset (gregset);
}

static void
fbsd_lwp_store_registers (int regno)
{
  gregset_t gregs;
  fpregset_t fpregs;
  lwpid_t lwp;
#ifdef PT_GETXMMREGS
  char xmmregs[512];
#endif

  /* FIXME, is it possible ? */
  if (!IS_LWP (inferior_ptid))
    {
      child_ops.to_store_registers (regno);
      return ;
    }

  lwp = GET_LWP (inferior_ptid);
  if (regno != -1)
    if (ptrace (PT_GETREGS, lwp, (caddr_t) &gregs, 0) == -1)
      error ("Cannot get lwp %d registers: %s\n", lwp, safe_strerror (errno));

  fill_gregset (&gregs, regno);
  if (ptrace (PT_SETREGS, lwp, (caddr_t) &gregs, 0) == -1)
      error ("Cannot set lwp %d registers: %s\n", lwp, safe_strerror (errno));

#ifdef PT_GETXMMREGS
  if (regno != -1)
    if (ptrace (PT_GETXMMREGS, lwp, xmmregs, 0) == -1)
      goto noxmm;

  i387_fill_fxsave (xmmregs, regno);
  if (ptrace (PT_SETXMMREGS, lwp, xmmregs, 0) == -1)
    goto noxmm;

  return;

noxmm:
#endif

  if (regno != -1)
    if (ptrace (PT_GETFPREGS, lwp, (caddr_t) &fpregs, 0) == -1)
      error ("Cannot get lwp %d float registers: %s\n", lwp,
             safe_strerror (errno));

  fill_fpregset (&fpregs, regno);
  if (ptrace (PT_SETFPREGS, lwp, (caddr_t) &fpregs, 0) == -1)
     error ("Cannot set lwp %d float registers: %s\n", lwp,
            safe_strerror (errno));
}

static void
fbsd_thread_store_registers (int regno)
{
  prgregset_t gregset;
  prfpregset_t fpregset;
  td_thrhandle_t th;
  td_err_e err;
#ifdef PT_GETXMMREGS
  char xmmregs[512];
#endif

  if (!IS_THREAD (inferior_ptid))
    {
      fbsd_lwp_store_registers (regno);
      return;
    }

  err = td_ta_map_id2thr_p (thread_agent, GET_THREAD (inferior_ptid), &th);
  if (err != TD_OK)
    error ("Cannot find thread %d: Thread ID=%ld, %s",
           pid_to_thread_id (inferior_ptid),
           GET_THREAD (inferior_ptid),
           thread_db_err_str (err));

  if (regno != -1)
    {
      char old_value[MAX_REGISTER_SIZE];

      regcache_collect (regno, old_value);
      err = td_thr_getgregs_p (&th, gregset);
      if (err != TD_OK)
        error ("%s: td_thr_getgregs %s", __func__, thread_db_err_str (err));
#ifdef PT_GETXMMREGS
      err = td_thr_getxmmregs_p (&th, xmmregs);
      if (err != TD_OK)
        {
#endif
          err = td_thr_getfpregs_p (&th, &fpregset);
          if (err != TD_OK)
            error ("%s: td_thr_getfpgregs %s", __func__, thread_db_err_str (err));
#ifdef PT_GETXMMREGS
        }
#endif
      supply_register (regno, old_value);
    }

  fill_gregset (gregset, regno);
  err = td_thr_setgregs_p (&th, gregset);
  if (err != TD_OK)
    error ("Cannot store general-purpose registers for thread %d: Thread ID=%d, %s",
           pid_to_thread_id (inferior_ptid), GET_THREAD (inferior_ptid),
           thread_db_err_str (err));

#ifdef PT_GETXMMREGS
  i387_fill_fxsave (xmmregs, regno);
  err = td_thr_setxmmregs_p (&th, xmmregs);
  if (err == TD_OK)
    return;
#endif

  fill_fpregset (&fpregset, regno);
  err = td_thr_setfpregs_p (&th, &fpregset);
  if (err != TD_OK)
    error ("Cannot store floating-point registers for thread %d: Thread ID=%d, %s",
           pid_to_thread_id (inferior_ptid), GET_THREAD (inferior_ptid),
           thread_db_err_str (err));
}

static void
fbsd_thread_kill (void)
{
  child_ops.to_kill();
}

static int
fbsd_thread_can_run (void)
{
  return child_suppress_run;
}

static void
fbsd_thread_create_inferior (char *exec_file, char *allargs, char **env)
{
  if (fbsd_thread_present && !fbsd_thread_active)
    push_target(&fbsd_thread_ops);

  child_ops.to_create_inferior (exec_file, allargs, env);
}

static void
fbsd_thread_post_startup_inferior (ptid_t ptid)
{
  if (fbsd_thread_present && !fbsd_thread_active)
    {
      /* The child process is now the actual multi-threaded
         program.  Snatch its process ID... */
      proc_handle.pid = GET_PID (ptid);
      td_ta_new_p (&proc_handle, &thread_agent);
      fbsd_thread_activate();
    }
}

static void
fbsd_thread_mourn_inferior (void)
{
  if (fbsd_thread_active)
    fbsd_thread_deactivate ();

  unpush_target (&fbsd_thread_ops);

  child_ops.to_mourn_inferior ();
}

static void
fbsd_core_check_lwp (bfd *abfd, asection *asect, void *obj)
{
  lwpid_t lwp;

  if (strncmp (bfd_section_name (abfd, asect), ".reg/", 5) != 0)
    return;

  /* already found */
  if (*(lwpid_t *)obj == 0)
    return;

  lwp = atoi (bfd_section_name (abfd, asect) + 5);
  if (*(lwpid_t *)obj == lwp)
    *(lwpid_t *)obj = 0;
}

static int
fbsd_thread_alive (ptid_t ptid)
{
  td_thrhandle_t th;
  td_thrinfo_t ti;
  td_err_e err;
  gregset_t gregs;
  lwpid_t lwp;

  if (IS_THREAD (ptid))
    {
      err = td_ta_map_id2thr_p (thread_agent, GET_THREAD (ptid), &th);
      if (err != TD_OK)
        return 0;

      err = td_thr_get_info_p (&th, &ti);
      if (err != TD_OK)
        return 0;

      /* A zombie thread. */
      if (ti.ti_state == TD_THR_UNKNOWN || ti.ti_state == TD_THR_ZOMBIE)
        return 0;

      return 1;
    }
  else if (GET_LWP (ptid) == 0)
    {
      /* we sometimes are called with lwp == 0 */
      return 1;
    }

  if (fbsd_thread_active)
    {
      err = td_ta_map_lwp2thr_p (thread_agent, GET_LWP (ptid), &th);

      /*
       * if the lwp was already mapped to user thread, don't use it
       * directly, please use user thread id instead.
       */
      if (err == TD_OK)
        return 0;
    }

  if (!target_has_execution)
    {
      lwp = GET_LWP (ptid);
      bfd_map_over_sections (core_bfd, fbsd_core_check_lwp, &lwp);
      return (lwp == 0); 
    }

  /* check lwp in kernel */
  return ptrace (PT_GETREGS, GET_LWP (ptid), (caddr_t)&gregs, 0) == 0;
}

static void
fbsd_thread_files_info (struct target_ops *ignore)
{
  child_ops.to_files_info (ignore);
}

static int
find_new_threads_callback (const td_thrhandle_t *th_p, void *data)
{
  td_thrinfo_t ti;
  td_err_e err;
  ptid_t ptid;

  err = td_thr_get_info_p (th_p, &ti);
  if (err != TD_OK)
    error ("Cannot get thread info: %s", thread_db_err_str (err));

  /* Ignore zombie */
  if (ti.ti_state == TD_THR_UNKNOWN || ti.ti_state == TD_THR_ZOMBIE)
    return 0;

  ptid = BUILD_THREAD (ti.ti_tid, proc_handle.pid);
  attach_thread (ptid, th_p, &ti, 1);
  return 0;
}

static void
fbsd_thread_find_new_threads (void)
{
  td_err_e err;

  if (!fbsd_thread_active)
    return;

  /* Iterate over all user-space threads to discover new threads. */
  err = td_ta_thr_iter_p (thread_agent, find_new_threads_callback, NULL,
          TD_THR_ANY_STATE, TD_THR_LOWEST_PRIORITY,
          TD_SIGNO_MASK, TD_THR_ANY_USER_FLAGS);
  if (err != TD_OK)
    error ("Cannot find new threads: %s", thread_db_err_str (err));
}

static char *
fbsd_thread_pid_to_str (ptid_t ptid)
{
  static char buf[64 + MAXCOMLEN];

  if (IS_THREAD (ptid))
    {
      td_thrhandle_t th;
      td_thrinfo_t ti;
      td_err_e err;

      err = td_ta_map_id2thr_p (thread_agent, GET_THREAD (ptid), &th);
      if (err != TD_OK)
        error ("Cannot find thread, Thread ID=%ld, %s",
                GET_THREAD (ptid), thread_db_err_str (err));

      err = td_thr_get_info_p (&th, &ti);
      if (err != TD_OK)
        error ("Cannot get thread info, Thread ID=%ld, %s",
               GET_THREAD (ptid), thread_db_err_str (err));

      if (ti.ti_lid != 0)
        {
          snprintf (buf, sizeof (buf), "Thread %llx (LWP %d/%s)",
                    (unsigned long long)th.th_thread, ti.ti_lid,
                    fbsd_thread_get_name (ti.ti_lid));
        }
      else
        {
          snprintf (buf, sizeof (buf), "Thread %llx (%s)",
		    (unsigned long long)th.th_thread,
		    thread_db_state_str (ti.ti_state));
        }

      return buf;
    }
  else if (IS_LWP (ptid))
    {
      snprintf (buf, sizeof (buf), "LWP %d", (int) GET_LWP (ptid));
      return buf;
    }
  return normal_pid_to_str (ptid);
}

CORE_ADDR
fbsd_thread_get_local_address(ptid_t ptid, struct objfile *objfile,
                              CORE_ADDR offset)
{
  td_thrhandle_t th;
  void *address;
  CORE_ADDR lm;
  void *lm2;
  int ret, is_library = (objfile->flags & OBJF_SHARED);

  if (IS_THREAD (ptid))
    {
      if (!td_thr_tls_get_addr_p)
        error ("Cannot find thread-local interface in thread_db library.");

      /* Get the address of the link map for this objfile. */
      lm = svr4_fetch_objfile_link_map (objfile);

      /* Couldn't find link map. Bail out. */
      if (!lm)
        {
          if (is_library)
            error ("Cannot find shared library `%s' link_map in dynamic"
                   " linker's module list", objfile->name);
          else
            error ("Cannot find executable file `%s' link_map in dynamic"
                   " linker's module list", objfile->name);
        }

      ret = td_ta_map_id2thr_p (thread_agent, GET_THREAD(ptid), &th);

      /* get the address of the variable. */
      store_typed_address(&lm2, builtin_type_void_data_ptr, lm);
      ret = td_thr_tls_get_addr_p (&th, lm2, offset, &address);

      if (ret != TD_OK)
        {
          if (is_library)
            error ("Cannot find thread-local storage for thread %ld, "
                   "shared library %s:\n%s",
                   (long) GET_THREAD (ptid),
                   objfile->name, thread_db_err_str (ret));
          else
            error ("Cannot find thread-local storage for thread %ld, "
                   "executable file %s:\n%s",
                   (long) GET_THREAD (ptid),
                   objfile->name, thread_db_err_str (ret));
        }

      /* Cast assuming host == target. */
      return extract_typed_address(&address, builtin_type_void_data_ptr);
    }
  return (0);
}

static int
tsd_cb (thread_key_t key, void (*destructor)(void *), void *ignore)
{
  struct minimal_symbol *ms;
  char *name;

  ms = lookup_minimal_symbol_by_pc (
	extract_typed_address(&destructor, builtin_type_void_func_ptr));
  if (!ms)
    name = "???";
  else
    name = DEPRECATED_SYMBOL_NAME (ms);

  printf_filtered ("Key %d, destructor %p <%s>\n", key, destructor, name);
  return 0;
}

static void
fbsd_thread_tsd_cmd (char *exp, int from_tty)
{
  if (fbsd_thread_active)
    td_ta_tsd_iter_p (thread_agent, tsd_cb, NULL);
}

static void
fbsd_print_sigset (sigset_t *set)
{
  int i;

  for (i = 1; i <= _SIG_MAXSIG; ++i) {
     if (sigismember(set, i)) {
       if (i < sizeof(sys_signame)/sizeof(sys_signame[0]))
         printf_filtered("%s ", sys_signame[i]);
       else
         printf_filtered("sig%d ", i);
     }
  }
  printf_filtered("\n");
}

static void
fbsd_thread_signal_cmd (char *exp, int from_tty)
{
  td_thrhandle_t th;
  td_thrinfo_t ti;
  td_err_e err;
  const char *code;

  if (!fbsd_thread_active || !IS_THREAD(inferior_ptid))
    return;

  err = td_ta_map_id2thr_p (thread_agent, GET_THREAD (inferior_ptid), &th);
  if (err != TD_OK)
    return;

  err = td_thr_get_info_p (&th, &ti);
  if (err != TD_OK)
    return;

  printf_filtered("signal mask:\n");
  fbsd_print_sigset(&ti.ti_sigmask);
  printf_filtered("signal pending:\n");
  fbsd_print_sigset(&ti.ti_pending);
  if (ti.ti_siginfo.si_signo != 0) {
   printf_filtered("si_signo %d si_errno %d", ti.ti_siginfo.si_signo,
     ti.ti_siginfo.si_errno);
   if (ti.ti_siginfo.si_errno != 0)
    printf_filtered(" (%s)", strerror(ti.ti_siginfo.si_errno));
   printf_filtered("\n");
   switch (ti.ti_siginfo.si_code) {
   case SI_NOINFO:
	code = "NOINFO";
	break;
    case SI_USER:
	code = "USER";
	break;
    case SI_QUEUE:
	code = "QUEUE";
	break;
    case SI_TIMER:
	code = "TIMER";
	break;
    case SI_ASYNCIO:
	code = "ASYNCIO";
	break;
    case SI_MESGQ:
	code = "MESGQ";
	break;
    case SI_KERNEL:
	code = "KERNEL";
	break;
    default:
	code = "UNKNOWN";
	break;
    }
    printf_filtered("si_code %s (%d) si_pid %d si_uid %d si_status %x "
      "si_addr %p\n",
      code, ti.ti_siginfo.si_code, ti.ti_siginfo.si_pid, ti.ti_siginfo.si_uid,
      ti.ti_siginfo.si_status, ti.ti_siginfo.si_addr);
  }
}

static int
ignore (CORE_ADDR addr, char *contents)
{
  return 0;
}

static void
fbsd_core_open (char *filename, int from_tty)
{
  int err;

  fbsd_thread_core = 1;

  orig_core_ops.to_open (filename, from_tty);

  if (fbsd_thread_present)
    {
      err = td_ta_new_p (&proc_handle, &thread_agent);
      if (err == TD_OK)
        {
          proc_handle.pid = elf_tdata (core_bfd)->core_pid;
          fbsd_thread_activate ();
        }
      else
        error ("fbsd_core_open: td_ta_new: %s", thread_db_err_str (err));
    }
}

static void
fbsd_core_close (int quitting)
{
  orig_core_ops.to_close (quitting);
}

static void
fbsd_core_detach (char *args, int from_tty)
{
  if (fbsd_thread_active)
    fbsd_thread_deactivate ();
  unpush_target (&fbsd_thread_ops);
  orig_core_ops.to_detach (args, from_tty);
 
  /* Clear gdb solib information and symbol file
     cache, so that after detach and re-attach, new_objfile
     hook will be called */
  clear_solib();
  symbol_file_clear(0);
}

static void
fbsd_core_files_info (struct target_ops *ignore)
{
  orig_core_ops.to_files_info (ignore);
}

static void
init_fbsd_core_ops (void)
{
  fbsd_core_ops.to_shortname = "FreeBSD-core";
  fbsd_core_ops.to_longname = "FreeBSD multithreaded core dump file";
  fbsd_core_ops.to_doc =
    "Use a core file as a target.  Specify the filename of the core file.";
  fbsd_core_ops.to_open = fbsd_core_open;
  fbsd_core_ops.to_close = fbsd_core_close;
  fbsd_core_ops.to_attach = 0;
  fbsd_core_ops.to_post_attach = 0;
  fbsd_core_ops.to_detach = fbsd_core_detach;
  /* fbsd_core_ops.to_resume  = 0; */
  /* fbsd_core_ops.to_wait  = 0;  */
  fbsd_core_ops.to_fetch_registers = fbsd_thread_fetch_registers;
  /* fbsd_core_ops.to_store_registers  = 0; */
  /* fbsd_core_ops.to_prepare_to_store  = 0; */
  fbsd_core_ops.to_xfer_memory = fbsd_thread_xfer_memory;
  fbsd_core_ops.to_files_info = fbsd_core_files_info;
  fbsd_core_ops.to_insert_breakpoint = ignore;
  fbsd_core_ops.to_remove_breakpoint = ignore;
  /* fbsd_core_ops.to_lookup_symbol  = 0; */
  fbsd_core_ops.to_create_inferior = fbsd_thread_create_inferior;
  fbsd_core_ops.to_stratum = core_stratum;
  fbsd_core_ops.to_has_all_memory = 0;
  fbsd_core_ops.to_has_memory = 1;
  fbsd_core_ops.to_has_stack = 1;
  fbsd_core_ops.to_has_registers = 1;
  fbsd_core_ops.to_has_execution = 0;
  fbsd_core_ops.to_has_thread_control = tc_none;
  fbsd_core_ops.to_thread_alive = fbsd_thread_alive;
  fbsd_core_ops.to_pid_to_str = fbsd_thread_pid_to_str;
  fbsd_core_ops.to_find_new_threads = fbsd_thread_find_new_threads;
  fbsd_core_ops.to_sections = 0;
  fbsd_core_ops.to_sections_end = 0;
  fbsd_core_ops.to_magic = OPS_MAGIC;
}

static void
init_fbsd_thread_ops (void)
{
  fbsd_thread_ops.to_shortname = "freebsd-threads";
  fbsd_thread_ops.to_longname = "FreeBSD multithreaded child process.";
  fbsd_thread_ops.to_doc = "FreeBSD threads support.";
  fbsd_thread_ops.to_attach = fbsd_thread_attach;
  fbsd_thread_ops.to_detach = fbsd_thread_detach;
  fbsd_thread_ops.to_post_attach = fbsd_thread_post_attach;
  fbsd_thread_ops.to_resume = fbsd_thread_resume;
  fbsd_thread_ops.to_wait = fbsd_thread_wait;
  fbsd_thread_ops.to_fetch_registers = fbsd_thread_fetch_registers;
  fbsd_thread_ops.to_store_registers = fbsd_thread_store_registers;
  fbsd_thread_ops.to_xfer_memory = fbsd_thread_xfer_memory;
  fbsd_thread_ops.to_files_info = fbsd_thread_files_info;
  fbsd_thread_ops.to_kill = fbsd_thread_kill;
  fbsd_thread_ops.to_create_inferior = fbsd_thread_create_inferior;
  fbsd_thread_ops.to_post_startup_inferior = fbsd_thread_post_startup_inferior;
  fbsd_thread_ops.to_mourn_inferior = fbsd_thread_mourn_inferior;
  fbsd_thread_ops.to_can_run = fbsd_thread_can_run;
  fbsd_thread_ops.to_thread_alive = fbsd_thread_alive;
  fbsd_thread_ops.to_find_new_threads = fbsd_thread_find_new_threads;
  fbsd_thread_ops.to_pid_to_str = fbsd_thread_pid_to_str;
  fbsd_thread_ops.to_stratum = thread_stratum;
  fbsd_thread_ops.to_has_thread_control = tc_none;
  fbsd_thread_ops.to_has_all_memory = 1;
  fbsd_thread_ops.to_has_memory = 1;
  fbsd_thread_ops.to_has_stack = 1;
  fbsd_thread_ops.to_has_registers = 1;
  fbsd_thread_ops.to_has_execution = 1;
  fbsd_thread_ops.to_insert_breakpoint = memory_insert_breakpoint;
  fbsd_thread_ops.to_remove_breakpoint = memory_remove_breakpoint;
  fbsd_thread_ops.to_get_thread_local_address = fbsd_thread_get_local_address;
  fbsd_thread_ops.to_magic = OPS_MAGIC;
}

static int
thread_db_load (void)
{
  void *handle;
  td_err_e err;

  handle = dlopen (LIBTHREAD_DB_SO, RTLD_NOW);
  if (handle == NULL)
      return 0;

#define resolve(X)			\
 if (!(X##_p = dlsym (handle, #X)))	\
   return 0;

  resolve(td_init);
  resolve(td_ta_new);
  resolve(td_ta_delete);
  resolve(td_ta_map_id2thr);
  resolve(td_ta_map_lwp2thr);
  resolve(td_ta_thr_iter);
  resolve(td_thr_get_info);
#ifdef PT_GETXMMREGS
  resolve(td_thr_getxmmregs);
#endif
  resolve(td_thr_getfpregs);
  resolve(td_thr_getgregs);
#ifdef PT_GETXMMREGS
  resolve(td_thr_setxmmregs);
#endif
  resolve(td_thr_setfpregs);
  resolve(td_thr_setgregs);
  resolve(td_thr_sstep);
  resolve(td_ta_tsd_iter);
  resolve(td_thr_dbsuspend);
  resolve(td_thr_dbresume);
  resolve(td_thr_tls_get_addr);

  /* Initialize the library.  */
  err = td_init_p ();
  if (err != TD_OK)
    {
      warning ("Cannot initialize libthread_db: %s", thread_db_err_str (err));
      return 0;
    }

  /* These are not essential.  */
  td_ta_event_addr_p = dlsym (handle, "td_ta_event_addr");
  td_ta_set_event_p = dlsym (handle, "td_ta_set_event");
  td_ta_event_getmsg_p = dlsym (handle, "td_ta_event_getmsg");
  td_thr_event_enable_p = dlsym (handle, "td_thr_event_enable");
  td_thr_tls_get_addr_p = dlsym (handle, "td_thr_tls_get_addr");
  
  return 1;
}

/* we suppress the call to add_target of core_ops in corelow because
   if there are two targets in the stratum core_stratum, find_core_target
   won't know which one to return.  see corelow.c for an additonal
   comment on coreops_suppress_target. */

int coreops_suppress_target = 1;

/* similarly we allow this target to be completely skipped.  This is used
   by kgdb which uses its own core target. */

int fbsdcoreops_suppress_target;

void
_initialize_thread_db (void)
{

  if (fbsdcoreops_suppress_target)
    return;
  init_fbsd_thread_ops ();
  init_fbsd_core_ops ();

  if (thread_db_load ())
    {
      add_target (&fbsd_thread_ops);

      /* "thread tsd" command */
      add_cmd ("tsd", class_run, fbsd_thread_tsd_cmd,
            "Show the thread-specific data keys and destructors "
            "for the process.\n",
           &thread_cmd_list);

      add_cmd ("signal", class_run, fbsd_thread_signal_cmd,
            "Show the thread signal info.\n",
           &thread_cmd_list);

      memcpy (&orig_core_ops, &core_ops, sizeof (struct target_ops));
      memcpy (&core_ops, &fbsd_core_ops, sizeof (struct target_ops));
      add_target (&core_ops);

      /* Add ourselves to objfile event chain. */
      target_new_objfile_chain = target_new_objfile_hook;
      target_new_objfile_hook = fbsd_thread_new_objfile;

      child_suppress_run = 1;
    }
  else
    {
      fprintf_unfiltered (gdb_stderr,
        "[GDB will not be able to debug user-mode threads: %s]\n", dlerror());
     
      /* allow the user to debug non-threaded core files */
      add_target (&core_ops);
    }
}

/* proc service functions */
void
ps_plog (const char *fmt, ...)
{
  va_list args;

  va_start (args, fmt);
  vfprintf_filtered (gdb_stderr, fmt, args);
  va_end (args);
}

ps_err_e
ps_pglobal_lookup (struct ps_prochandle *ph, const char *obj,
   const char *name, psaddr_t *sym_addr)
{
  struct minimal_symbol *ms;
  CORE_ADDR addr;

  ms = lookup_minimal_symbol (name, NULL, NULL);
  if (ms == NULL)
    return PS_NOSYM;

  addr = SYMBOL_VALUE_ADDRESS (ms);
  store_typed_address(sym_addr, builtin_type_void_data_ptr, addr);
  return PS_OK;
}

ps_err_e
ps_pread (struct ps_prochandle *ph, psaddr_t addr, void *buf, size_t len)
{
  int err = target_read_memory (
    extract_typed_address(&addr, builtin_type_void_data_ptr), buf, len);
  return (err == 0 ? PS_OK : PS_ERR);
}

ps_err_e
ps_pwrite (struct ps_prochandle *ph, psaddr_t addr, const void *buf,
            size_t len)
{
  int err = target_write_memory (
    extract_typed_address(&addr, builtin_type_void_data_ptr), (void *)buf, len);
  return (err == 0 ? PS_OK : PS_ERR);
}

ps_err_e
ps_lgetregs (struct ps_prochandle *ph, lwpid_t lwpid, prgregset_t gregset)
{
  struct cleanup *old_chain;

  old_chain = save_inferior_ptid ();

  /* XXX: Target operation isn't lwp aware: replace pid with lwp */
  inferior_ptid = BUILD_LWP (0, lwpid);

  target_fetch_registers (-1);
  fill_gregset (gregset, -1);
  do_cleanups (old_chain);
  return PS_OK;
}

ps_err_e
ps_lsetregs (struct ps_prochandle *ph, lwpid_t lwpid, const prgregset_t gregset)
{
  struct cleanup *old_chain;

  old_chain = save_inferior_ptid ();
  inferior_ptid = BUILD_LWP (lwpid, PIDGET (inferior_ptid));
  supply_gregset ((gdb_gregset_t *) gregset);
  target_store_registers (-1);
  do_cleanups (old_chain);
  return PS_OK;
}

ps_err_e
ps_lgetfpregs (struct ps_prochandle *ph, lwpid_t lwpid, prfpregset_t *fpregset)
{
  struct cleanup *old_chain;

  old_chain = save_inferior_ptid ();
  inferior_ptid = BUILD_LWP (lwpid, PIDGET (inferior_ptid));
  target_fetch_registers (-1);
  fill_fpregset (fpregset, -1);
  do_cleanups (old_chain);
  return PS_OK;
}

ps_err_e
ps_lsetfpregs (struct ps_prochandle *ph, lwpid_t lwpid,
               const prfpregset_t *fpregset)
{
  struct cleanup *old_chain;

  old_chain = save_inferior_ptid ();
  inferior_ptid = BUILD_LWP (lwpid, PIDGET (inferior_ptid));
  supply_fpregset ((gdb_fpregset_t *) fpregset);
  target_store_registers (-1);
  do_cleanups (old_chain);
  return PS_OK;
}

#ifdef PT_GETXMMREGS
ps_err_e
ps_lgetxmmregs (struct ps_prochandle *ph, lwpid_t lwpid, char *xmmregs)
{
  struct cleanup *old_chain;

  old_chain = save_inferior_ptid ();
  inferior_ptid = BUILD_LWP (lwpid, PIDGET (inferior_ptid));
  target_fetch_registers (-1);
  i387_fill_fxsave (xmmregs, -1);
  do_cleanups (old_chain);
  return PS_OK;
}

ps_err_e
ps_lsetxmmregs (struct ps_prochandle *ph, lwpid_t lwpid,
		const char *xmmregs)
{
  struct cleanup *old_chain;

  old_chain = save_inferior_ptid ();
  inferior_ptid = BUILD_LWP (lwpid, PIDGET (inferior_ptid));
  i387_supply_fxsave (current_regcache, -1, xmmregs);
  target_store_registers (-1);
  do_cleanups (old_chain);
  return PS_OK;
}
#endif

ps_err_e
ps_lstop(struct ps_prochandle *ph, lwpid_t lwpid)
{
  if (ptrace (PT_SUSPEND, lwpid, 0, 0) == -1)
    return PS_ERR;
  return PS_OK;  
}

ps_err_e
ps_lcontinue(struct ps_prochandle *ph, lwpid_t lwpid)
{
  if (ptrace (PT_RESUME, lwpid, 0, 0) == -1)
    return PS_ERR;
  return PS_OK;   
}

ps_err_e
ps_linfo(struct ps_prochandle *ph, lwpid_t lwpid, void *info)
{
  if (fbsd_thread_core) {
    /* XXX should verify lwpid and make a pseudo lwp info */
    memset(info, 0, sizeof(struct ptrace_lwpinfo));
    return PS_OK;
  }

  if (ptrace (PT_LWPINFO, lwpid, info, sizeof(struct ptrace_lwpinfo)) == -1)
    return PS_ERR;
  return PS_OK;
}
