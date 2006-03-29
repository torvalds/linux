#ifndef _ASM_POWERPC_KDEBUG_H
#define _ASM_POWERPC_KDEBUG_H
#ifdef __KERNEL__

/* nearly identical to x86_64/i386 code */

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
extern struct atomic_notifier_head powerpc_die_chain;

/* Grossly misnamed. */
enum die_val {
	DIE_OOPS = 1,
	DIE_IABR_MATCH,
	DIE_DABR_MATCH,
	DIE_BPT,
	DIE_SSTEP,
	DIE_PAGE_FAULT,
};

static inline int notify_die(enum die_val val,char *str,struct pt_regs *regs,long err,int trap, int sig)
{
	struct die_args args = { .regs=regs, .str=str, .err=err, .trapnr=trap,.signr=sig };
	return atomic_notifier_call_chain(&powerpc_die_chain, val, &args);
}

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_KDEBUG_H */
