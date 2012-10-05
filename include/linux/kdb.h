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
#include <linux/atomic.h>

#define KDB_POLL_FUNC_MAX	5
extern int kdb_poll_idx;

/*
 * kdb_initial_cpu is initialized to -1, and is set to the cpu
 * number whenever the kernel debugger is entered.
 */
extern int kdb_initial_cpu;
extern atomic_t kdb_event;

/* Types and messages used for dynamically added kdb shell commands */

#define KDB_MAXARGS    16 /* Maximum number of arguments to a function  */

typedef enum {
	KDB_REPEAT_NONE = 0,	/* Do not repeat this command */
	KDB_REPEAT_NO_ARGS,	/* Repeat the command without arguments */
	KDB_REPEAT_WITH_ARGS,	/* Repeat the command including its arguments */
} kdb_repeat_t;

typedef int (*kdb_func_t)(int, const char **);

/* KDB return codes from a command or internal kdb function */
#define KDB_NOTFOUND	(-1)
#define KDB_ARGCOUNT	(-2)
#define KDB_BADWIDTH	(-3)
#define KDB_BADRADIX	(-4)
#define KDB_NOTENV	(-5)
#define KDB_NOENVVALUE	(-6)
#define KDB_NOTIMP	(-7)
#define KDB_ENVFULL	(-8)
#define KDB_ENVBUFFULL	(-9)
#define KDB_TOOMANYBPT	(-10)
#define KDB_TOOMANYDBREGS (-11)
#define KDB_DUPBPT	(-12)
#define KDB_BPTNOTFOUND	(-13)
#define KDB_BADMODE	(-14)
#define KDB_BADINT	(-15)
#define KDB_INVADDRFMT  (-16)
#define KDB_BADREG      (-17)
#define KDB_BADCPUNUM   (-18)
#define KDB_BADLENGTH	(-19)
#define KDB_NOBP	(-20)
#define KDB_BADADDR	(-21)

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
extern __printf(1, 0) int vkdb_printf(const char *fmt, va_list args);
extern __printf(1, 2) int kdb_printf(const char *, ...);
typedef __printf(1, 2) int (*kdb_printf_t)(const char *, ...);

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
#ifdef CONFIG_KALLSYMS
extern const char *kdb_walk_kallsyms(loff_t *pos);
#else /* ! CONFIG_KALLSYMS */
static inline const char *kdb_walk_kallsyms(loff_t *pos)
{
	return NULL;
}
#endif /* ! CONFIG_KALLSYMS */

/* Dynamic kdb shell command registration */
extern int kdb_register(char *, kdb_func_t, char *, char *, short);
extern int kdb_register_repeat(char *, kdb_func_t, char *, char *,
			       short, kdb_repeat_t);
extern int kdb_unregister(char *);
#else /* ! CONFIG_KGDB_KDB */
#define kdb_printf(...)
#define kdb_init(x)
#define kdb_register(...)
#define kdb_register_repeat(...)
#define kdb_uregister(x)
#endif	/* CONFIG_KGDB_KDB */
enum {
	KDB_NOT_INITIALIZED,
	KDB_INIT_EARLY,
	KDB_INIT_FULL,
};

extern int kdbgetintenv(const char *, int *);
extern int kdb_set(int, const char **);

#endif	/* !_KDB_H */
