#ifndef __ASM_AVR32_KDEBUG_H
#define __ASM_AVR32_KDEBUG_H

#include <linux/notifier.h>

/* Grossly misnamed. */
enum die_val {
	DIE_FAULT,
	DIE_BREAKPOINT,
	DIE_SSTEP,
	DIE_PAGE_FAULT,
};

int register_page_fault_notifier(struct notifier_block *nb);
int unregister_page_fault_notifier(struct notifier_block *nb);

#endif /* __ASM_AVR32_KDEBUG_H */
