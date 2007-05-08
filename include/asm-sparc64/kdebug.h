#ifndef _SPARC64_KDEBUG_H
#define _SPARC64_KDEBUG_H

/* Nearly identical to x86_64/i386 code. */

#include <linux/notifier.h>

struct pt_regs;

extern int register_page_fault_notifier(struct notifier_block *);
extern int unregister_page_fault_notifier(struct notifier_block *);

extern void bad_trap(struct pt_regs *, long);

/* Grossly misnamed. */
enum die_val {
	DIE_OOPS = 1,
	DIE_DEBUG,	/* ta 0x70 */
	DIE_DEBUG_2,	/* ta 0x71 */
	DIE_DIE,
	DIE_TRAP,
	DIE_TRAP_TL1,
	DIE_GPF,
	DIE_CALL,
	DIE_PAGE_FAULT,
};

#endif
