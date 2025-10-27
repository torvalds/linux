/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_RSEQ_ENTRY_H
#define _LINUX_RSEQ_ENTRY_H

#ifdef CONFIG_RSEQ
#include <linux/rseq.h>

static __always_inline void rseq_note_user_irq_entry(void)
{
	if (IS_ENABLED(CONFIG_GENERIC_IRQ_ENTRY))
		current->rseq.event.user_irq = true;
}

#else /* CONFIG_RSEQ */
static inline void rseq_note_user_irq_entry(void) { }
#endif /* !CONFIG_RSEQ */

#endif /* _LINUX_RSEQ_ENTRY_H */
