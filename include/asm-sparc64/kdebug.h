#ifndef _SPARC64_KDEBUG_H
#define _SPARC64_KDEBUG_H

/* Nearly identical to x86_64/i386 code. */

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
 * - then free.
 */
int register_die_notifier(struct notifier_block *nb);
extern struct notifier_block *sparc64die_chain;

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

static inline int notify_die(enum die_val val,char *str, struct pt_regs *regs,
			     long err, int trap, int sig)
{
	struct die_args args = { .regs		= regs,
				 .str		= str,
				 .err		= err,
				 .trapnr	= trap,
				 .signr		= sig };

	return notifier_call_chain(&sparc64die_chain, val, &args);
}

#endif
