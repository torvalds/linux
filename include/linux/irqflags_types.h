/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_IRQFLAGS_TYPES_H
#define _LINUX_IRQFLAGS_TYPES_H

#ifdef CONFIG_TRACE_IRQFLAGS

/* Per-task IRQ trace events information. */
struct irqtrace_events {
	unsigned int	irq_events;
	unsigned long	hardirq_enable_ip;
	unsigned long	hardirq_disable_ip;
	unsigned int	hardirq_enable_event;
	unsigned int	hardirq_disable_event;
	unsigned long	softirq_disable_ip;
	unsigned long	softirq_enable_ip;
	unsigned int	softirq_disable_event;
	unsigned int	softirq_enable_event;
};

#endif

#endif /* _LINUX_IRQFLAGS_TYPES_H */
