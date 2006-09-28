#ifndef __ASM_AVR32_KDEBUG_H
#define __ASM_AVR32_KDEBUG_H

#include <linux/notifier.h>

struct pt_regs;

struct die_args {
	struct pt_regs *regs;
	int trapnr;
};

int register_die_notifier(struct notifier_block *nb);
int unregister_die_notifier(struct notifier_block *nb);
int register_page_fault_notifier(struct notifier_block *nb);
int unregister_page_fault_notifier(struct notifier_block *nb);
extern struct atomic_notifier_head avr32_die_chain;

/* Grossly misnamed. */
enum die_val {
	DIE_FAULT,
	DIE_BREAKPOINT,
	DIE_SSTEP,
	DIE_PAGE_FAULT,
};

static inline int notify_die(enum die_val val, struct pt_regs *regs,
			     int trap, int sig)
{
	struct die_args args = {
		.regs = regs,
		.trapnr = trap,
	};

	return atomic_notifier_call_chain(&avr32_die_chain, val, &args);
}

#endif /* __ASM_AVR32_KDEBUG_H */
