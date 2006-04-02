#ifndef _I386_KDEBUG_H
#define _I386_KDEBUG_H 1

/*
 * Aug-05 2004 Ported by Prasanna S Panchamukhi <prasanna@in.ibm.com>
 * from x86_64 architecture.
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

extern int register_die_notifier(struct notifier_block *);
extern int unregister_die_notifier(struct notifier_block *);
extern struct atomic_notifier_head i386die_chain;


/* Grossly misnamed. */
enum die_val {
	DIE_OOPS = 1,
	DIE_INT3,
	DIE_DEBUG,
	DIE_PANIC,
	DIE_NMI,
	DIE_DIE,
	DIE_NMIWATCHDOG,
	DIE_KERNELDEBUG,
	DIE_TRAP,
	DIE_GPF,
	DIE_CALL,
	DIE_NMI_IPI,
	DIE_PAGE_FAULT,
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
	return atomic_notifier_call_chain(&i386die_chain, val, &args);
}

#endif
