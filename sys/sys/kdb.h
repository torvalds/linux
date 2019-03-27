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
 *
 * $FreeBSD$
 */

#ifndef _SYS_KDB_H_
#define	_SYS_KDB_H_

#include <machine/setjmp.h>

struct pcb;
struct thread;
struct trapframe;

typedef int dbbe_init_f(void);
typedef void dbbe_trace_f(void);
typedef void dbbe_trace_thread_f(struct thread *);
typedef int dbbe_trap_f(int, int);

struct kdb_dbbe {
	const char	*dbbe_name;
	dbbe_init_f	*dbbe_init;
	dbbe_trace_f	*dbbe_trace;
	dbbe_trace_thread_f *dbbe_trace_thread;
	dbbe_trap_f	*dbbe_trap;
	int		dbbe_active;
};

#define	KDB_BACKEND(name, init, trace, trace_thread, trap) \
	static struct kdb_dbbe name##_dbbe = {		\
		.dbbe_name = #name,			\
		.dbbe_init = init,			\
		.dbbe_trace = trace,			\
		.dbbe_trace_thread = trace_thread,	\
		.dbbe_trap = trap			\
	};						\
	DATA_SET(kdb_dbbe_set, name##_dbbe)

extern u_char kdb_active;		/* Non-zero while in debugger. */
extern int debugger_on_trap;		/* enter the debugger on trap. */
extern struct kdb_dbbe *kdb_dbbe;	/* Default debugger backend or NULL. */
extern struct trapframe *kdb_frame;	/* Frame to kdb_trap(). */
extern struct pcb *kdb_thrctx;		/* Current context. */
extern struct thread *kdb_thread;	/* Current thread. */

int	kdb_alt_break(int, int *);
int	kdb_alt_break_gdb(int, int *);
int	kdb_break(void);
void	kdb_backtrace(void);
void	kdb_backtrace_thread(struct thread *);
int	kdb_dbbe_select(const char *);
void	kdb_enter(const char *, const char *);
void	kdb_init(void);
void *	kdb_jmpbuf(jmp_buf);
void	kdb_panic(const char *);
void	kdb_reboot(void);
void	kdb_reenter(void);
void	kdb_reenter_silent(void);
struct pcb *kdb_thr_ctx(struct thread *);
struct thread *kdb_thr_first(void);
struct thread *kdb_thr_from_pid(pid_t);
struct thread *kdb_thr_lookup(lwpid_t);
struct thread *kdb_thr_next(struct thread *);
int	kdb_thr_select(struct thread *);
int	kdb_trap(int, int, struct trapframe *);

/*
 * KDB enters the debugger via breakpoint(), which leaves the debugger without
 * a lot of information about why it was entered.  This simple enumerated set
 * captures some basic information.
 *
 * It is recommended that values here be short (<16 character) alpha-numeric
 * strings, as they will be used to construct DDB(4) script names.
 */
extern const char * volatile kdb_why;
#define	KDB_WHY_UNSET		NULL		/* No reason set. */
#define	KDB_WHY_PANIC		"panic"		/* panic() was called. */
#define	KDB_WHY_KASSERT		"kassert"	/* kassert failed. */
#define	KDB_WHY_TRAP		"trap"		/* Fatal trap. */
#define	KDB_WHY_SYSCTL		"sysctl"	/* Sysctl entered debugger. */
#define	KDB_WHY_BOOTFLAGS	"bootflags"	/* Boot flags were set. */
#define	KDB_WHY_WITNESS		"witness"	/* Witness entered debugger. */
#define	KDB_WHY_VFSLOCK		"vfslock"	/* VFS detected lock problem. */
#define	KDB_WHY_NETGRAPH	"netgraph"	/* Netgraph entered debugger. */
#define	KDB_WHY_BREAK		"break"		/* Console or serial break. */
#define	KDB_WHY_WATCHDOG	"watchdog"	/* Watchdog entered debugger. */
#define	KDB_WHY_CAM		"cam"		/* CAM has entered debugger. */
#define	KDB_WHY_NDIS		"ndis"		/* NDIS entered debugger. */
#define	KDB_WHY_ACPI		"acpi"		/* ACPI entered debugger. */
#define	KDB_WHY_TRAPSIG		"trapsig"	/* Sparc fault. */
#define	KDB_WHY_POWERFAIL	"powerfail"	/* Powerfail NMI. */
#define	KDB_WHY_MAC		"mac"		/* MAC Framework. */
#define	KDB_WHY_POWERPC		"powerpc"	/* Unhandled powerpc intr. */
#define	KDB_WHY_UNIONFS		"unionfs"	/* Unionfs bug. */
#define	KDB_WHY_DTRACE		"dtrace"	/* DTrace action entered debugger. */

/* Return values for kdb_alt_break */
#define	KDB_REQ_DEBUGGER	1	/* User requested Debugger */
#define	KDB_REQ_PANIC		2	/* User requested a panic */
#define	KDB_REQ_REBOOT		3	/* User requested a clean reboot */

#endif /* !_SYS_KDB_H_ */
