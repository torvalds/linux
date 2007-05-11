#ifndef _S390_KDEBUG_H
#define _S390_KDEBUG_H

/*
 * Feb 2006 Ported to s390 <grundym@us.ibm.com>
 */
#include <linux/notifier.h>

struct pt_regs;

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

extern void die(const char *, struct pt_regs *, long);

#endif
