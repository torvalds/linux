#ifndef _KDB_H
#define _KDB_H

/*
 * Kernel Debugger Architecture Independent Global Headers
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2000-2007 Silicon Graphics, Inc.  All Rights Reserved.
 * Copyright (C) 2000 Stephane Eranian <eranian@hpl.hp.com>
 * Copyright (C) 2009 Jason Wessel <jason.wessel@windriver.com>
 */

#ifdef	CONFIG_KGDB_KDB
#include <linux/init.h>
#include <linux/sched.h>
#include <asm/atomic.h>

#define KDB_POLL_FUNC_MAX	5
extern int kdb_poll_idx;

/*
 * kdb_initial_cpu is initialized to -1, and is set to the cpu
 * number whenever the kernel debugger is entered.
 */
extern int kdb_initial_cpu;
extern atomic_t kdb_event;

/*
 * kdb_diemsg
 *
 *	Contains a pointer to the last string supplied to the
 *	kernel 'die' panic function.
 */
extern const char *kdb_diemsg;

#define KDB_FLAG_EARLYKDB	(1 << 0) /* set from boot parameter kdb=early */
#define KDB_FLAG_CATASTROPHIC	(1 << 1) /* A catastrophic event has occurred */
#define KDB_FLAG_CMD_INTERRUPT	(1 << 2) /* Previous command was interrupted */
#define KDB_FLAG_NOIPI		(1 << 3) /* Do not send IPIs */
#define KDB_FLAG_ONLY_DO_DUMP	(1 << 4) /* Only do a dump, used when
					  * kdb is off */
#define KDB_FLAG_NO_CONSOLE	(1 << 5) /* No console is available,
					  * kdb is disabled */
#define KDB_FLAG_NO_VT_CONSOLE	(1 << 6) /* No VT console is available, do
					  * not use keyboard */
#define KDB_FLAG_NO_I8042	(1 << 7) /* No i8042 chip is available, do
					  * not use keyboard */

extern int kdb_flags;	/* Global flags, see kdb_state for per cpu state */

extern void kdb_save_flags(void);
extern void kdb_restore_flags(void);

#define KDB_FLAG(flag)		(kdb_flags & KDB_FLAG_##flag)
#define KDB_FLAG_SET(flag)	((void)(kdb_flags |= KDB_FLAG_##flag))
#define KDB_FLAG_CLEAR(flag)	((void)(kdb_flags &= ~KDB_FLAG_##flag))

/*
 * External entry point for the kernel debugger.  The pt_regs
 * at the time of entry are supplied along with the reason for
 * entry to the kernel debugger.
 */

typedef enum {
	KDB_REASON_ENTER = 1,	/* KDB_ENTER() trap/fault - regs valid */
	KDB_REASON_ENTER_SLAVE,	/* KDB_ENTER_SLAVE() trap/fault - regs valid */
	KDB_REASON_BREAK,	/* Breakpoint inst. - regs valid */
	KDB_REASON_DEBUG,	/* Debug Fault - regs valid */
	KDB_REASON_OOPS,	/* Kernel Oops - regs valid */
	KDB_REASON_SWITCH,	/* CPU switch - regs valid*/
	KDB_REASON_KEYBOARD,	/* Keyboard entry - regs valid */
	KDB_REASON_NMI,		/* Non-maskable interrupt; regs valid */
	KDB_REASON_RECURSE,	/* Recursive entry to kdb;
				 * regs probably valid */
	KDB_REASON_SSTEP,	/* Single Step trap. - regs valid */
} kdb_reason_t;

extern int kdb_trap_printk;
extern int vkdb_printf(const char *fmt, va_list args)
	    __attribute__ ((format (printf, 1, 0)));
extern int kdb_printf(const char *, ...)
	    __attribute__ ((format (printf, 1, 2)));
typedef int (*kdb_printf_t)(const char *, ...)
	     __attribute__ ((format (printf, 1, 2)));

extern void kdb_init(int level);

/* Access to kdb specific polling devices */
typedef int (*get_char_func)(void);
extern get_char_func kdb_poll_funcs[];
extern int kdb_get_kbd_char(void);

static inline
int kdb_process_cpu(const struct task_struct *p)
{
	unsigned int cpu = task_thread_info(p)->cpu;
	if (cpu > num_possible_cpus())
		cpu = 0;
	return cpu;
}

/* kdb access to register set for stack dumping */
extern struct pt_regs *kdb_current_regs;

#else /* ! CONFIG_KGDB_KDB */
#define kdb_printf(...)
#define kdb_init(x)
#endif	/* CONFIG_KGDB_KDB */
enum {
	KDB_NOT_INITIALIZED,
	KDB_INIT_EARLY,
	KDB_INIT_FULL,
};
#endif	/* !_KDB_H */
