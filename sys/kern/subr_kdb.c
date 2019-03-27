/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 The FreeBSD Project
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_kdb.h"
#include "opt_stack.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cons.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/sbuf.h>
#include <sys/smp.h>
#include <sys/stack.h>
#include <sys/sysctl.h>

#include <machine/kdb.h>
#include <machine/pcb.h>

#ifdef SMP
#include <machine/smp.h>
#endif

u_char __read_frequently kdb_active = 0;
static void *kdb_jmpbufp = NULL;
struct kdb_dbbe *kdb_dbbe = NULL;
static struct pcb kdb_pcb;
struct pcb *kdb_thrctx = NULL;
struct thread *kdb_thread = NULL;
struct trapframe *kdb_frame = NULL;

#ifdef BREAK_TO_DEBUGGER
#define	KDB_BREAK_TO_DEBUGGER	1
#else
#define	KDB_BREAK_TO_DEBUGGER	0
#endif

#ifdef ALT_BREAK_TO_DEBUGGER
#define	KDB_ALT_BREAK_TO_DEBUGGER	1
#else
#define	KDB_ALT_BREAK_TO_DEBUGGER	0
#endif

static int	kdb_break_to_debugger = KDB_BREAK_TO_DEBUGGER;
static int	kdb_alt_break_to_debugger = KDB_ALT_BREAK_TO_DEBUGGER;

KDB_BACKEND(null, NULL, NULL, NULL, NULL);
SET_DECLARE(kdb_dbbe_set, struct kdb_dbbe);

static int kdb_sysctl_available(SYSCTL_HANDLER_ARGS);
static int kdb_sysctl_current(SYSCTL_HANDLER_ARGS);
static int kdb_sysctl_enter(SYSCTL_HANDLER_ARGS);
static int kdb_sysctl_panic(SYSCTL_HANDLER_ARGS);
static int kdb_sysctl_trap(SYSCTL_HANDLER_ARGS);
static int kdb_sysctl_trap_code(SYSCTL_HANDLER_ARGS);
static int kdb_sysctl_stack_overflow(SYSCTL_HANDLER_ARGS);

static SYSCTL_NODE(_debug, OID_AUTO, kdb, CTLFLAG_RW, NULL, "KDB nodes");

SYSCTL_PROC(_debug_kdb, OID_AUTO, available, CTLTYPE_STRING | CTLFLAG_RD, NULL,
    0, kdb_sysctl_available, "A", "list of available KDB backends");

SYSCTL_PROC(_debug_kdb, OID_AUTO, current, CTLTYPE_STRING | CTLFLAG_RW, NULL,
    0, kdb_sysctl_current, "A", "currently selected KDB backend");

SYSCTL_PROC(_debug_kdb, OID_AUTO, enter,
    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_SECURE, NULL, 0,
    kdb_sysctl_enter, "I", "set to enter the debugger");

SYSCTL_PROC(_debug_kdb, OID_AUTO, panic,
    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_SECURE, NULL, 0,
    kdb_sysctl_panic, "I", "set to panic the kernel");

SYSCTL_PROC(_debug_kdb, OID_AUTO, trap,
    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_SECURE, NULL, 0,
    kdb_sysctl_trap, "I", "set to cause a page fault via data access");

SYSCTL_PROC(_debug_kdb, OID_AUTO, trap_code,
    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_SECURE, NULL, 0,
    kdb_sysctl_trap_code, "I", "set to cause a page fault via code access");

SYSCTL_PROC(_debug_kdb, OID_AUTO, stack_overflow,
    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_SECURE, NULL, 0,
    kdb_sysctl_stack_overflow, "I", "set to cause a stack overflow");

SYSCTL_INT(_debug_kdb, OID_AUTO, break_to_debugger,
    CTLFLAG_RWTUN | CTLFLAG_SECURE,
    &kdb_break_to_debugger, 0, "Enable break to debugger");

SYSCTL_INT(_debug_kdb, OID_AUTO, alt_break_to_debugger,
    CTLFLAG_RWTUN | CTLFLAG_SECURE,
    &kdb_alt_break_to_debugger, 0, "Enable alternative break to debugger");

/*
 * Flag to indicate to debuggers why the debugger was entered.
 */
const char * volatile kdb_why = KDB_WHY_UNSET;

static int
kdb_sysctl_available(SYSCTL_HANDLER_ARGS)
{
	struct kdb_dbbe **iter;
	struct sbuf sbuf;
	int error;

	sbuf_new_for_sysctl(&sbuf, NULL, 64, req);
	SET_FOREACH(iter, kdb_dbbe_set) {
		if ((*iter)->dbbe_active == 0)
			sbuf_printf(&sbuf, "%s ", (*iter)->dbbe_name);
	}
	error = sbuf_finish(&sbuf);
	sbuf_delete(&sbuf);
	return (error);
}

static int
kdb_sysctl_current(SYSCTL_HANDLER_ARGS)
{
	char buf[16];
	int error;

	if (kdb_dbbe != NULL)
		strlcpy(buf, kdb_dbbe->dbbe_name, sizeof(buf));
	else
		*buf = '\0';
	error = sysctl_handle_string(oidp, buf, sizeof(buf), req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (kdb_active)
		return (EBUSY);
	return (kdb_dbbe_select(buf));
}

static int
kdb_sysctl_enter(SYSCTL_HANDLER_ARGS)
{
	int error, i;

	error = sysctl_wire_old_buffer(req, sizeof(int));
	if (error == 0) {
		i = 0;
		error = sysctl_handle_int(oidp, &i, 0, req);
	}
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (kdb_active)
		return (EBUSY);
	kdb_enter(KDB_WHY_SYSCTL, "sysctl debug.kdb.enter");
	return (0);
}

static int
kdb_sysctl_panic(SYSCTL_HANDLER_ARGS)
{
	int error, i;

	error = sysctl_wire_old_buffer(req, sizeof(int));
	if (error == 0) {
		i = 0;
		error = sysctl_handle_int(oidp, &i, 0, req);
	}
	if (error != 0 || req->newptr == NULL)
		return (error);
	panic("kdb_sysctl_panic");
	return (0);
}

static int
kdb_sysctl_trap(SYSCTL_HANDLER_ARGS)
{
	int error, i;
	int *addr = (int *)0x10;

	error = sysctl_wire_old_buffer(req, sizeof(int));
	if (error == 0) {
		i = 0;
		error = sysctl_handle_int(oidp, &i, 0, req);
	}
	if (error != 0 || req->newptr == NULL)
		return (error);
	return (*addr);
}

static int
kdb_sysctl_trap_code(SYSCTL_HANDLER_ARGS)
{
	int error, i;
	void (*fp)(u_int, u_int, u_int) = (void *)0xdeadc0de;

	error = sysctl_wire_old_buffer(req, sizeof(int));
	if (error == 0) {
		i = 0;
		error = sysctl_handle_int(oidp, &i, 0, req);
	}
	if (error != 0 || req->newptr == NULL)
		return (error);
	(*fp)(0x11111111, 0x22222222, 0x33333333);
	return (0);
}

static void kdb_stack_overflow(volatile int *x)  __noinline;
static void
kdb_stack_overflow(volatile int *x)
{

	if (*x > 10000000)
		return;
	kdb_stack_overflow(x);
	*x += PCPU_GET(cpuid) / 1000000;
}

static int
kdb_sysctl_stack_overflow(SYSCTL_HANDLER_ARGS)
{
	int error, i;
	volatile int x;

	error = sysctl_wire_old_buffer(req, sizeof(int));
	if (error == 0) {
		i = 0;
		error = sysctl_handle_int(oidp, &i, 0, req);
	}
	if (error != 0 || req->newptr == NULL)
		return (error);
	x = 0;
	kdb_stack_overflow(&x);
	return (0);
}


void
kdb_panic(const char *msg)
{

	printf("KDB: panic\n");
	panic("%s", msg);
}

void
kdb_reboot(void)
{

	printf("KDB: reboot requested\n");
	shutdown_nice(0);
}

/*
 * Solaris implements a new BREAK which is initiated by a character sequence
 * CR ~ ^b which is similar to a familiar pattern used on Sun servers by the
 * Remote Console.
 *
 * Note that this function may be called from almost anywhere, with interrupts
 * disabled and with unknown locks held, so it must not access data other than
 * its arguments.  Its up to the caller to ensure that the state variable is
 * consistent.
 */
#define	KEY_CR		13	/* CR '\r' */
#define	KEY_TILDE	126	/* ~ */
#define	KEY_CRTLB	2	/* ^B */
#define	KEY_CRTLP	16	/* ^P */
#define	KEY_CRTLR	18	/* ^R */

/* States of th KDB "alternate break sequence" detecting state machine. */
enum {
	KDB_ALT_BREAK_SEEN_NONE,
	KDB_ALT_BREAK_SEEN_CR,
	KDB_ALT_BREAK_SEEN_CR_TILDE,
};

int
kdb_break(void)
{

	if (!kdb_break_to_debugger)
		return (0);
	kdb_enter(KDB_WHY_BREAK, "Break to debugger");
	return (KDB_REQ_DEBUGGER);
}

static int
kdb_alt_break_state(int key, int *state)
{
	int brk;

	/* All states transition to KDB_ALT_BREAK_SEEN_CR on a CR. */
	if (key == KEY_CR) {
		*state = KDB_ALT_BREAK_SEEN_CR;
		return (0);
	}

	brk = 0;
	switch (*state) {
	case KDB_ALT_BREAK_SEEN_CR:
		*state = KDB_ALT_BREAK_SEEN_NONE;
		if (key == KEY_TILDE)
			*state = KDB_ALT_BREAK_SEEN_CR_TILDE;
		break;
	case KDB_ALT_BREAK_SEEN_CR_TILDE:
		*state = KDB_ALT_BREAK_SEEN_NONE;
		if (key == KEY_CRTLB)
			brk = KDB_REQ_DEBUGGER;
		else if (key == KEY_CRTLP)
			brk = KDB_REQ_PANIC;
		else if (key == KEY_CRTLR)
			brk = KDB_REQ_REBOOT;
		break;
	case KDB_ALT_BREAK_SEEN_NONE:
	default:
		*state = KDB_ALT_BREAK_SEEN_NONE;
		break;
	}
	return (brk);
}

static int
kdb_alt_break_internal(int key, int *state, int force_gdb)
{
	int brk;

	if (!kdb_alt_break_to_debugger)
		return (0);
	brk = kdb_alt_break_state(key, state);
	switch (brk) {
	case KDB_REQ_DEBUGGER:
		if (force_gdb)
			kdb_dbbe_select("gdb");
		kdb_enter(KDB_WHY_BREAK, "Break to debugger");
		break;

	case KDB_REQ_PANIC:
		if (force_gdb)
			kdb_dbbe_select("gdb");
		kdb_panic("Panic sequence on console");
		break;

	case KDB_REQ_REBOOT:
		kdb_reboot();
		break;
	}
	return (0);
}

int
kdb_alt_break(int key, int *state)
{

	return (kdb_alt_break_internal(key, state, 0));
}

/*
 * This variation on kdb_alt_break() is used only by dcons, which has its own
 * configuration flag to force GDB use regardless of the global KDB
 * configuration.
 */
int
kdb_alt_break_gdb(int key, int *state)
{

	return (kdb_alt_break_internal(key, state, 1));
}

/*
 * Print a backtrace of the calling thread. The backtrace is generated by
 * the selected debugger, provided it supports backtraces. If no debugger
 * is selected or the current debugger does not support backtraces, this
 * function silently returns.
 */
void
kdb_backtrace(void)
{

	if (kdb_dbbe != NULL && kdb_dbbe->dbbe_trace != NULL) {
		printf("KDB: stack backtrace:\n");
		kdb_dbbe->dbbe_trace();
	}
#ifdef STACK
	else {
		struct stack st;

		printf("KDB: stack backtrace:\n");
		stack_zero(&st);
		stack_save(&st);
		stack_print_ddb(&st);
	}
#endif
}

/*
 * Similar to kdb_backtrace() except that it prints a backtrace of an
 * arbitrary thread rather than the calling thread.
 */
void
kdb_backtrace_thread(struct thread *td)
{

	if (kdb_dbbe != NULL && kdb_dbbe->dbbe_trace_thread != NULL) {
		printf("KDB: stack backtrace of thread %d:\n", td->td_tid);
		kdb_dbbe->dbbe_trace_thread(td);
	}
#ifdef STACK
	else {
		struct stack st;

		printf("KDB: stack backtrace of thread %d:\n", td->td_tid);
		stack_zero(&st);
		stack_save_td(&st, td);
		stack_print_ddb(&st);
	}
#endif
}

/*
 * Set/change the current backend.
 */
int
kdb_dbbe_select(const char *name)
{
	struct kdb_dbbe *be, **iter;

	SET_FOREACH(iter, kdb_dbbe_set) {
		be = *iter;
		if (be->dbbe_active == 0 && strcmp(be->dbbe_name, name) == 0) {
			kdb_dbbe = be;
			return (0);
		}
	}
	return (EINVAL);
}

/*
 * Enter the currently selected debugger. If a message has been provided,
 * it is printed first. If the debugger does not support the enter method,
 * it is entered by using breakpoint(), which enters the debugger through
 * kdb_trap().  The 'why' argument will contain a more mechanically usable
 * string than 'msg', and is relied upon by DDB scripting to identify the
 * reason for entering the debugger so that the right script can be run.
 */
void
kdb_enter(const char *why, const char *msg)
{

	if (kdb_dbbe != NULL && kdb_active == 0) {
		if (msg != NULL)
			printf("KDB: enter: %s\n", msg);
		kdb_why = why;
		breakpoint();
		kdb_why = KDB_WHY_UNSET;
	}
}

/*
 * Initialize the kernel debugger interface.
 */
void
kdb_init(void)
{
	struct kdb_dbbe *be, **iter;
	int cur_pri, pri;

	kdb_active = 0;
	kdb_dbbe = NULL;
	cur_pri = -1;
	SET_FOREACH(iter, kdb_dbbe_set) {
		be = *iter;
		pri = (be->dbbe_init != NULL) ? be->dbbe_init() : -1;
		be->dbbe_active = (pri >= 0) ? 0 : -1;
		if (pri > cur_pri) {
			cur_pri = pri;
			kdb_dbbe = be;
		}
	}
	if (kdb_dbbe != NULL) {
		printf("KDB: debugger backends:");
		SET_FOREACH(iter, kdb_dbbe_set) {
			be = *iter;
			if (be->dbbe_active == 0)
				printf(" %s", be->dbbe_name);
		}
		printf("\n");
		printf("KDB: current backend: %s\n",
		    kdb_dbbe->dbbe_name);
	}
}

/*
 * Handle contexts.
 */
void *
kdb_jmpbuf(jmp_buf new)
{
	void *old;

	old = kdb_jmpbufp;
	kdb_jmpbufp = new;
	return (old);
}

void
kdb_reenter(void)
{

	if (!kdb_active || kdb_jmpbufp == NULL)
		return;

	printf("KDB: reentering\n");
	kdb_backtrace();
	longjmp(kdb_jmpbufp, 1);
	/* NOTREACHED */
}

void
kdb_reenter_silent(void)
{

	if (!kdb_active || kdb_jmpbufp == NULL)
		return;

	longjmp(kdb_jmpbufp, 1);
	/* NOTREACHED */
}

/*
 * Thread-related support functions.
 */
struct pcb *
kdb_thr_ctx(struct thread *thr)
{
#if defined(SMP) && defined(KDB_STOPPEDPCB)
	struct pcpu *pc;
#endif

	if (thr == curthread)
		return (&kdb_pcb);

#if defined(SMP) && defined(KDB_STOPPEDPCB)
	STAILQ_FOREACH(pc, &cpuhead, pc_allcpu)  {
		if (pc->pc_curthread == thr &&
		    CPU_ISSET(pc->pc_cpuid, &stopped_cpus))
			return (KDB_STOPPEDPCB(pc));
	}
#endif
	return (thr->td_pcb);
}

struct thread *
kdb_thr_first(void)
{
	struct proc *p;
	struct thread *thr;

	FOREACH_PROC_IN_SYSTEM(p) {
		if (p->p_flag & P_INMEM) {
			thr = FIRST_THREAD_IN_PROC(p);
			if (thr != NULL)
				return (thr);
		}
	}
	return (NULL);
}

struct thread *
kdb_thr_from_pid(pid_t pid)
{
	struct proc *p;

	FOREACH_PROC_IN_SYSTEM(p) {
		if (p->p_flag & P_INMEM && p->p_pid == pid)
			return (FIRST_THREAD_IN_PROC(p));
	}
	return (NULL);
}

struct thread *
kdb_thr_lookup(lwpid_t tid)
{
	struct thread *thr;

	thr = kdb_thr_first();
	while (thr != NULL && thr->td_tid != tid)
		thr = kdb_thr_next(thr);
	return (thr);
}

struct thread *
kdb_thr_next(struct thread *thr)
{
	struct proc *p;

	p = thr->td_proc;
	thr = TAILQ_NEXT(thr, td_plist);
	do {
		if (thr != NULL)
			return (thr);
		p = LIST_NEXT(p, p_list);
		if (p != NULL && (p->p_flag & P_INMEM))
			thr = FIRST_THREAD_IN_PROC(p);
	} while (p != NULL);
	return (NULL);
}

int
kdb_thr_select(struct thread *thr)
{
	if (thr == NULL)
		return (EINVAL);
	kdb_thread = thr;
	kdb_thrctx = kdb_thr_ctx(thr);
	return (0);
}

/*
 * Enter the debugger due to a trap.
 */
int
kdb_trap(int type, int code, struct trapframe *tf)
{
#ifdef SMP
	cpuset_t other_cpus;
#endif
	struct kdb_dbbe *be;
	register_t intr;
	int handled;
	int did_stop_cpus;

	be = kdb_dbbe;
	if (be == NULL || be->dbbe_trap == NULL)
		return (0);

	/* We reenter the debugger through kdb_reenter(). */
	if (kdb_active)
		return (0);

	intr = intr_disable();

	if (!SCHEDULER_STOPPED()) {
#ifdef SMP
		other_cpus = all_cpus;
		CPU_NAND(&other_cpus, &stopped_cpus);
		CPU_CLR(PCPU_GET(cpuid), &other_cpus);
		stop_cpus_hard(other_cpus);
#endif
		curthread->td_stopsched = 1;
		did_stop_cpus = 1;
	} else
		did_stop_cpus = 0;

	kdb_active++;

	kdb_frame = tf;

	/* Let MD code do its thing first... */
	kdb_cpu_trap(type, code);

	makectx(tf, &kdb_pcb);
	kdb_thr_select(curthread);

	cngrab();

	for (;;) {
		handled = be->dbbe_trap(type, code);
		if (be == kdb_dbbe)
			break;
		be = kdb_dbbe;
		if (be == NULL || be->dbbe_trap == NULL)
			break;
		printf("Switching to %s back-end\n", be->dbbe_name);
	}

	cnungrab();

	kdb_active--;

	if (did_stop_cpus) {
		curthread->td_stopsched = 0;
#ifdef SMP
		CPU_AND(&other_cpus, &stopped_cpus);
		restart_cpus(other_cpus);
#endif
	}

	intr_restore(intr);

	return (handled);
}
