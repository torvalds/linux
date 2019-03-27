/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kdb.h>
#include <sys/proc.h>

#include <machine/pcb.h>

#include <ddb/ddb.h>
#include <ddb/db_command.h>
#include <ddb/db_sym.h>

void
db_print_thread(void)
{
	pid_t pid;

	pid = -1;
	if (kdb_thread->td_proc != NULL)
		pid = kdb_thread->td_proc->p_pid;
	db_printf("[ thread pid %d tid %ld ]\n", pid, (long)kdb_thread->td_tid);
}

void
db_set_thread(db_expr_t tid, bool hastid, db_expr_t cnt, char *mod)
{
	struct thread *thr;
	int err;

	if (hastid) {
		thr = db_lookup_thread(tid, false);
		if (thr != NULL) {
			err = kdb_thr_select(thr);
			if (err != 0) {
				db_printf("unable to switch to thread %ld\n",
				    (long)thr->td_tid);
				return;
			}
			db_dot = PC_REGS();
		} else {
			db_printf("%d: invalid thread\n", (int)tid);
			return;
		}
	}

	db_print_thread();
	db_print_loc_and_inst(PC_REGS());
}

void
db_show_threads(db_expr_t addr, bool hasaddr, db_expr_t cnt, char *mod)
{
	jmp_buf jb;
	void *prev_jb;
	struct thread *thr;

	thr = kdb_thr_first();
	while (!db_pager_quit && thr != NULL) {
		db_printf("  %6ld (%p) (stack %p)  ", (long)thr->td_tid, thr,
		    (void *)thr->td_kstack);
		prev_jb = kdb_jmpbuf(jb);
		if (setjmp(jb) == 0) {
			if (db_trace_thread(thr, 1) != 0)
				db_printf("***\n");
		}
		kdb_jmpbuf(prev_jb);
		thr = kdb_thr_next(thr);
	}
}

/*
 * Lookup a thread based on a db expression address.  We assume that the
 * address was parsed in hexadecimal.  We reparse the address in decimal
 * first and try to treat it as a thread ID to find an associated thread.
 * If that fails and check_pid is true, we treat the decimal value as a
 * PID.  If that matches a process, we return the first thread in that
 * process.  Otherwise, we treat the addr as a pointer to a thread.
 */
struct thread *
db_lookup_thread(db_expr_t addr, bool check_pid)
{
	struct thread *td;
	db_expr_t decaddr;
	struct proc *p;

	/*
	 * If the parsed address was not a valid decimal expression,
	 * assume it is a thread pointer.
	 */
	decaddr = db_hex2dec(addr);
	if (decaddr == -1)
		return ((struct thread *)addr);

	td = kdb_thr_lookup(decaddr);
	if (td != NULL)
		return (td);
	if (check_pid) {
		FOREACH_PROC_IN_SYSTEM(p) {
			if (p->p_pid == decaddr)
				return (FIRST_THREAD_IN_PROC(p));
		}
		LIST_FOREACH(p, &zombproc, p_list) {
			if (p->p_pid == decaddr)
				return (FIRST_THREAD_IN_PROC(p));
		}
	}
	return ((struct thread *)addr);
}

/*
 * Lookup a process based on a db expression address.  We assume that the
 * address was parsed in hexadecimal.  We reparse the address in decimal
 * first and try to treat it as a PID to find an associated process.
 * If that fails we treat the addr as a pointer to a process.
 */
struct proc *
db_lookup_proc(db_expr_t addr)
{
	db_expr_t decaddr;
	struct proc *p;

	decaddr = db_hex2dec(addr);
	if (decaddr != -1) {
		FOREACH_PROC_IN_SYSTEM(p) {
			if (p->p_pid == decaddr)
				return (p);
		}
		LIST_FOREACH(p, &zombproc, p_list) {
			if (p->p_pid == decaddr)
				return (p);
		}
	}
	return ((struct proc *)addr);
}
