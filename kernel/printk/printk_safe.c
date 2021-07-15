// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * printk_safe.c - Safe printk for printk-deadlock-prone contexts
 */

#include <linux/preempt.h>
#include <linux/spinlock.h>
#include <linux/debug_locks.h>
#include <linux/kdb.h>
#include <linux/smp.h>
#include <linux/cpumask.h>
#include <linux/irq_work.h>
#include <linux/printk.h>
#include <linux/kprobes.h>

#include "internal.h"

static DEFINE_PER_CPU(int, printk_context);

#ifdef CONFIG_PRINTK_NMI
void noinstr printk_nmi_enter(void)
{
	this_cpu_add(printk_context, PRINTK_NMI_CONTEXT_OFFSET);
}

void noinstr printk_nmi_exit(void)
{
	this_cpu_sub(printk_context, PRINTK_NMI_CONTEXT_OFFSET);
}

/*
 * Marks a code that might produce many messages in NMI context
 * and the risk of losing them is more critical than eventual
 * reordering.
 */
void printk_nmi_direct_enter(void)
{
	if (this_cpu_read(printk_context) & PRINTK_NMI_CONTEXT_MASK)
		this_cpu_or(printk_context, PRINTK_NMI_DIRECT_CONTEXT_MASK);
}

void printk_nmi_direct_exit(void)
{
	this_cpu_and(printk_context, ~PRINTK_NMI_DIRECT_CONTEXT_MASK);
}

#endif /* CONFIG_PRINTK_NMI */

/* Can be preempted by NMI. */
void __printk_safe_enter(void)
{
	this_cpu_inc(printk_context);
}

/* Can be preempted by NMI. */
void __printk_safe_exit(void)
{
	this_cpu_dec(printk_context);
}

asmlinkage int vprintk(const char *fmt, va_list args)
{
#ifdef CONFIG_KGDB_KDB
	/* Allow to pass printk() to kdb but avoid a recursion. */
	if (unlikely(kdb_trap_printk && kdb_printf_cpu < 0))
		return vkdb_printf(KDB_MSGSRC_PRINTK, fmt, args);
#endif

	/*
	 * Use the main logbuf even in NMI. But avoid calling console
	 * drivers that might have their own locks.
	 */
	if (this_cpu_read(printk_context) &
	    (PRINTK_NMI_DIRECT_CONTEXT_MASK |
	     PRINTK_NMI_CONTEXT_MASK |
	     PRINTK_SAFE_CONTEXT_MASK)) {
		int len;

		len = vprintk_store(0, LOGLEVEL_DEFAULT, NULL, fmt, args);
		defer_console_output();
		return len;
	}

	/* No obstacles. */
	return vprintk_default(fmt, args);
}
EXPORT_SYMBOL(vprintk);
