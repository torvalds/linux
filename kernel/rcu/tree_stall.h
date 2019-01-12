// SPDX-License-Identifier: GPL-2.0+
/*
 * RCU CPU stall warnings for normal RCU grace periods
 *
 * Copyright IBM Corporation, 2019
 *
 * Author: Paul E. McKenney <paulmck@linux.ibm.com>
 */


#ifdef CONFIG_PROVE_RCU
#define RCU_STALL_DELAY_DELTA	       (5 * HZ)
#else
#define RCU_STALL_DELAY_DELTA	       0
#endif

int rcu_jiffies_till_stall_check(void)
{
	int till_stall_check = READ_ONCE(rcu_cpu_stall_timeout);

	/*
	 * Limit check must be consistent with the Kconfig limits
	 * for CONFIG_RCU_CPU_STALL_TIMEOUT.
	 */
	if (till_stall_check < 3) {
		WRITE_ONCE(rcu_cpu_stall_timeout, 3);
		till_stall_check = 3;
	} else if (till_stall_check > 300) {
		WRITE_ONCE(rcu_cpu_stall_timeout, 300);
		till_stall_check = 300;
	}
	return till_stall_check * HZ + RCU_STALL_DELAY_DELTA;
}
EXPORT_SYMBOL_GPL(rcu_jiffies_till_stall_check);

void rcu_sysrq_start(void)
{
	if (!rcu_cpu_stall_suppress)
		rcu_cpu_stall_suppress = 2;
}

void rcu_sysrq_end(void)
{
	if (rcu_cpu_stall_suppress == 2)
		rcu_cpu_stall_suppress = 0;
}

static int rcu_panic(struct notifier_block *this, unsigned long ev, void *ptr)
{
	rcu_cpu_stall_suppress = 1;
	return NOTIFY_DONE;
}

static struct notifier_block rcu_panic_block = {
	.notifier_call = rcu_panic,
};

static int __init check_cpu_stall_init(void)
{
	atomic_notifier_chain_register(&panic_notifier_list, &rcu_panic_block);
	return 0;
}
early_initcall(check_cpu_stall_init);
