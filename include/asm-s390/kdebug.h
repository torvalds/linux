#ifndef _S390_KDEBUG_H
#define _S390_KDEBUG_H

/*
 * Feb 2006 Ported to s390 <grundym@us.ibm.com>
 */
#include <linux/notifier.h>

struct pt_regs;

struct die_args {
	struct pt_regs *regs;
	const char *str;
	long err;
	int trapnr;
	int signr;
};

/* Note - you should never unregister because that can race with NMIs.
 * If you really want to do it first unregister - then synchronize_sched
 *  - then free.
 */
extern int register_die_notifier(struct notifier_block *);
extern int unregister_die_notifier(struct notifier_block *);

/*
 * These are only here because kprobes.c wants them to implement a
 * blatant layering violation. Will hopefully go away soon once all
 * architectures are updated.
 */
static inline int register_page_fault_notifier(struct notifier_block *nb)
{
	return 0;
}
static inline int unregister_page_fault_notifier(struct notifier_block *nb)
{
	return 0;
}

extern struct atomic_notifier_head s390die_chain;

enum die_val {
	DIE_OOPS = 1,
	DIE_BPT,
	DIE_SSTEP,
	DIE_PANIC,
	DIE_NMI,
	DIE_DIE,
	DIE_NMIWATCHDOG,
	DIE_KERNELDEBUG,
	DIE_TRAP,
	DIE_GPF,
	DIE_CALL,
	DIE_NMI_IPI,
};

static inline int notify_die(enum die_val val, const char *str,
			struct pt_regs *regs, long err, int trap, int sig)
{
	struct die_args args = {
		.regs = regs,
		.str = str,
		.err = err,
		.trapnr = trap,
		.signr = sig
	};
	return atomic_notifier_call_chain(&s390die_chain, val, &args);
}

extern void die(const char *, struct pt_regs *, long);

#endif
