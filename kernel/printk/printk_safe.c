// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * printk_safe.c - Safe printk for printk-deadlock-prone contexts
 */

#include <linux/preempt.h>
#include <linux/kdb.h>
#include <linux/smp.h>
#include <linux/cpumask.h>
#include <linux/printk.h>
#include <linux/kprobes.h>

#include "internal.h"

/* Context where printk messages are never suppressed */
static atomic_t force_con;

void printk_force_console_enter(void)
{
	atomic_inc(&force_con);
}

void printk_force_console_exit(void)
{
	atomic_dec(&force_con);
}

bool is_printk_force_console(void)
{
	return atomic_read(&force_con);
}

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

void __printk_deferred_enter(void)
{
	cant_migrate();
	__printk_safe_enter();
}

void __printk_deferred_exit(void)
{
	cant_migrate();
	__printk_safe_exit();
}

bool is_printk_legacy_deferred(void)
{
	/*
	 * The per-CPU variable @printk_context can be read safely in any
	 * context. CPU migration is always disabled when set.
	 *
	 * A context holding the printk_cpu_sync must not spin waiting for
	 * another CPU. For legacy printing, it could be the console_lock
	 * or the port lock.
	 */
	return (force_legacy_kthread() ||
		this_cpu_read(printk_context) ||
		in_nmi() ||
		is_printk_cpu_sync_owner());
}

asmlinkage int vprintk(const char *fmt, va_list args)
{
#ifdef CONFIG_KGDB_KDB
	/* Allow to pass printk() to kdb but avoid a recursion. */
	if (unlikely(kdb_trap_printk && kdb_printf_cpu < 0))
		return vkdb_printf(KDB_MSGSRC_PRINTK, fmt, args);
#endif
	return vprintk_default(fmt, args);
}
EXPORT_SYMBOL(vprintk);
