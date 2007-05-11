#ifndef _ASM_POWERPC_KDEBUG_H
#define _ASM_POWERPC_KDEBUG_H
#ifdef __KERNEL__

/* nearly identical to x86_64/i386 code */

#include <linux/notifier.h>

/*
 * These are only here because kprobes.c wants them to implement a
 * blatant layering violation.  Will hopefully go away soon once all
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
extern struct atomic_notifier_head powerpc_die_chain;

/* Grossly misnamed. */
enum die_val {
	DIE_OOPS = 1,
	DIE_IABR_MATCH,
	DIE_DABR_MATCH,
	DIE_BPT,
	DIE_SSTEP,
};

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_KDEBUG_H */
