// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * printk_safe.c - Safe printk for printk-deadlock-prone contexts
 */

#include <linux/preempt.h>
#include <linux/kdb.h>
#include <linux/smp.h>
#include <linux/cpumask.h>
#include <linux/printk.h>
#include <linux/console.h>
#include <linux/kprobes.h>
#include <linux/delay.h>

#include "internal.h"

static DEFINE_PER_CPU(int, printk_context);

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
	if (this_cpu_read(printk_context) || in_nmi()) {
		int len;

		len = vprintk_store(0, LOGLEVEL_DEFAULT, NULL, fmt, args);
		defer_console_output();
		return len;
	}

	/* No obstacles. */
	return vprintk_default(fmt, args);
}
EXPORT_SYMBOL(vprintk);

/**
 * try_block_console_kthreads() - Try to block console kthreads and
 *	make the global console_lock() avaialble
 *
 * @timeout_ms:        The maximum time (in ms) to wait.
 *
 * Prevent console kthreads from starting processing new messages. Wait
 * until the global console_lock() become available.
 *
 * Context: Can be called in any context.
 */
void try_block_console_kthreads(int timeout_ms)
{
	block_console_kthreads = true;

	/* Do not wait when the console lock could not be safely taken. */
	if (this_cpu_read(printk_context) || in_nmi())
		return;

	while (timeout_ms > 0) {
		if (console_trylock()) {
			console_unlock();
			return;
		}

		udelay(1000);
		timeout_ms -= 1;
	}
}
