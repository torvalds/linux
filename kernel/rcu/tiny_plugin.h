/*
 * Read-Copy Update mechanism for mutual exclusion, the Bloatwatch edition
 * Internal non-public definitions that provide either classic
 * or preemptible semantics.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * Copyright (c) 2010 Linaro
 *
 * Author: Paul E. McKenney <paulmck@linux.vnet.ibm.com>
 */

#if defined(CONFIG_DEBUG_LOCK_ALLOC) || defined(CONFIG_SRCU)
#include <linux/kernel_stat.h>

int rcu_scheduler_active __read_mostly;
EXPORT_SYMBOL_GPL(rcu_scheduler_active);

/*
 * During boot, we forgive RCU lockdep issues.  After this function is
 * invoked, we start taking RCU lockdep issues seriously.  Note that unlike
 * Tree RCU, Tiny RCU transitions directly from RCU_SCHEDULER_INACTIVE
 * to RCU_SCHEDULER_RUNNING, skipping the RCU_SCHEDULER_INIT stage.
 * The reason for this is that Tiny RCU does not need kthreads, so does
 * not have to care about the fact that the scheduler is half-initialized
 * at a certain phase of the boot process.  Unless SRCU is in the mix.
 */
void __init rcu_scheduler_starting(void)
{
	WARN_ON(nr_context_switches() > 0);
	rcu_scheduler_active = IS_ENABLED(CONFIG_SRCU)
		? RCU_SCHEDULER_INIT : RCU_SCHEDULER_RUNNING;
}

#endif /* #if defined(CONFIG_DEBUG_LOCK_ALLOC) || defined(CONFIG_SRCU) */
