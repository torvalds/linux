/* Internal interfaces for the GNU/Linux specific target code for gdbserver.
   Copyright 2002, 2004 Free Software Foundation, Inc.

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

/*
 * $FreeBSD$
 */

typedef void (*regset_fill_func) (void *);
typedef void (*regset_store_func) (const void *);
enum regset_type {
  GENERAL_REGS,
  FP_REGS,
  EXTENDED_REGS,
};

struct regset_info
{
  int get_request, set_request;
  int size;
  enum regset_type type;
  regset_fill_func fill_function;
  regset_store_func store_function;
};
extern struct regset_info target_regsets[];

struct fbsd_target_ops
{
  int num_regs;
  int *regmap;
  int (*cannot_fetch_register) (int);

  /* Returns 0 if we can store the register, 1 if we can not
     store the register, and 2 if failure to store the register
     is acceptable.  */
  int (*cannot_store_register) (int);
  CORE_ADDR (*get_pc) (void);
  void (*set_pc) (CORE_ADDR newpc);
  const char *breakpoint;
  int breakpoint_len;
  CORE_ADDR (*breakpoint_reinsert_addr) (void);


  int decr_pc_after_break;
  int (*breakpoint_at) (CORE_ADDR pc);
};

extern struct fbsd_target_ops the_low_target;

#define get_process(inf) ((struct process_info *)(inf))
#define get_thread_process(thr) (get_process (inferior_target_data (thr)))
#define get_process_thread(proc) ((struct thread_info *) \
				  find_inferior_id (&all_threads, \
				  get_process (proc)->tid))

struct process_info
{
  struct inferior_list_entry head;
  int thread_known;
  int lwpid;
  int tid;

  /* If this flag is set, the next SIGSTOP will be ignored (the process will
     be immediately resumed).  */
  int stop_expected;

  /* If this flag is set, the process is known to be stopped right now (stop
     event already received in a wait()).  */
  int stopped;

  /* If this flag is set, we have sent a SIGSTOP to this process and are
     waiting for it to stop.  */
  int sigstop_sent;

  /* If this flag is set, STATUS_PENDING is a waitstatus that has not yet
     been reported.  */
  int status_pending_p;
  int status_pending;

  /* If this flag is set, the pending status is a (GDB-placed) breakpoint.  */
  int pending_is_breakpoint;
  CORE_ADDR pending_stop_pc;

  /* If this is non-zero, it is a breakpoint to be reinserted at our next
     stop (SIGTRAP stops only).  */
  CORE_ADDR bp_reinsert;

  /* If this flag is set, the last continue operation on this process
     was a single-step.  */
  int stepping;

  /* If this is non-zero, it points to a chain of signals which need to
     be delivered to this process.  */
  struct pending_signals *pending_signals;

  /* A link used when resuming.  It is initialized from the resume request,
     and then processed and cleared in fbsd_resume_one_process.  */

  struct thread_resume *resume;
};

extern struct inferior_list all_processes;

void fbsd_attach_lwp (int pid, int tid);

int thread_db_init (void);
