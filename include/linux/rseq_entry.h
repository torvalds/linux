/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_RSEQ_ENTRY_H
#define _LINUX_RSEQ_ENTRY_H

#ifdef CONFIG_RSEQ
#include <linux/rseq.h>

#include <linux/tracepoint-defs.h>

#ifdef CONFIG_TRACEPOINTS
DECLARE_TRACEPOINT(rseq_update);
DECLARE_TRACEPOINT(rseq_ip_fixup);
void __rseq_trace_update(struct task_struct *t);
void __rseq_trace_ip_fixup(unsigned long ip, unsigned long start_ip,
			   unsigned long offset, unsigned long abort_ip);

static inline void rseq_trace_update(struct task_struct *t, struct rseq_ids *ids)
{
	if (tracepoint_enabled(rseq_update) && ids)
		__rseq_trace_update(t);
}

static inline void rseq_trace_ip_fixup(unsigned long ip, unsigned long start_ip,
				       unsigned long offset, unsigned long abort_ip)
{
	if (tracepoint_enabled(rseq_ip_fixup))
		__rseq_trace_ip_fixup(ip, start_ip, offset, abort_ip);
}

#else /* CONFIG_TRACEPOINT */
static inline void rseq_trace_update(struct task_struct *t, struct rseq_ids *ids) { }
static inline void rseq_trace_ip_fixup(unsigned long ip, unsigned long start_ip,
				       unsigned long offset, unsigned long abort_ip) { }
#endif /* !CONFIG_TRACEPOINT */

static __always_inline void rseq_note_user_irq_entry(void)
{
	if (IS_ENABLED(CONFIG_GENERIC_IRQ_ENTRY))
		current->rseq.event.user_irq = true;
}

#else /* CONFIG_RSEQ */
static inline void rseq_note_user_irq_entry(void) { }
#endif /* !CONFIG_RSEQ */

#endif /* _LINUX_RSEQ_ENTRY_H */
