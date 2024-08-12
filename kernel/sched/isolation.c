// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Housekeeping management. Manage the targets for routine code that can run on
 *  any CPU: unbound workqueues, timers, kthreads and any offloadable work.
 *
 * Copyright (C) 2017 Red Hat, Inc., Frederic Weisbecker
 * Copyright (C) 2017-2018 SUSE, Frederic Weisbecker
 *
 */

enum hk_flags {
	HK_FLAG_TIMER		= BIT(HK_TYPE_TIMER),
	HK_FLAG_RCU		= BIT(HK_TYPE_RCU),
	HK_FLAG_MISC		= BIT(HK_TYPE_MISC),
	HK_FLAG_SCHED		= BIT(HK_TYPE_SCHED),
	HK_FLAG_TICK		= BIT(HK_TYPE_TICK),
	HK_FLAG_DOMAIN		= BIT(HK_TYPE_DOMAIN),
	HK_FLAG_WQ		= BIT(HK_TYPE_WQ),
	HK_FLAG_MANAGED_IRQ	= BIT(HK_TYPE_MANAGED_IRQ),
	HK_FLAG_KTHREAD		= BIT(HK_TYPE_KTHREAD),
};

DEFINE_STATIC_KEY_FALSE(housekeeping_overridden);
EXPORT_SYMBOL_GPL(housekeeping_overridden);

struct housekeeping {
	cpumask_var_t cpumasks[HK_TYPE_MAX];
	unsigned long flags;
};

static struct housekeeping housekeeping;

bool housekeeping_enabled(enum hk_type type)
{
	return !!(housekeeping.flags & BIT(type));
}
EXPORT_SYMBOL_GPL(housekeeping_enabled);

int housekeeping_any_cpu(enum hk_type type)
{
	int cpu;

	if (static_branch_unlikely(&housekeeping_overridden)) {
		if (housekeeping.flags & BIT(type)) {
			cpu = sched_numa_find_closest(housekeeping.cpumasks[type], smp_processor_id());
			if (cpu < nr_cpu_ids)
				return cpu;

			cpu = cpumask_any_and(housekeeping.cpumasks[type], cpu_online_mask);
			if (likely(cpu < nr_cpu_ids))
				return cpu;
			/*
			 * Unless we have another problem this can only happen
			 * at boot time before start_secondary() brings the 1st
			 * housekeeping CPU up.
			 */
			WARN_ON_ONCE(system_state == SYSTEM_RUNNING ||
				     type != HK_TYPE_TIMER);
		}
	}
	return smp_processor_id();
}
EXPORT_SYMBOL_GPL(housekeeping_any_cpu);

const struct cpumask *housekeeping_cpumask(enum hk_type type)
{
	if (static_branch_unlikely(&housekeeping_overridden))
		if (housekeeping.flags & BIT(type))
			return housekeeping.cpumasks[type];
	return cpu_possible_mask;
}
EXPORT_SYMBOL_GPL(housekeeping_cpumask);

void housekeeping_affine(struct task_struct *t, enum hk_type type)
{
	if (static_branch_unlikely(&housekeeping_overridden))
		if (housekeeping.flags & BIT(type))
			set_cpus_allowed_ptr(t, housekeeping.cpumasks[type]);
}
EXPORT_SYMBOL_GPL(housekeeping_affine);

bool housekeeping_test_cpu(int cpu, enum hk_type type)
{
	if (static_branch_unlikely(&housekeeping_overridden))
		if (housekeeping.flags & BIT(type))
			return cpumask_test_cpu(cpu, housekeeping.cpumasks[type]);
	return true;
}
EXPORT_SYMBOL_GPL(housekeeping_test_cpu);

void __init housekeeping_init(void)
{
	enum hk_type type;

	if (!housekeeping.flags)
		return;

	static_branch_enable(&housekeeping_overridden);

	if (housekeeping.flags & HK_FLAG_TICK)
		sched_tick_offload_init();

	for_each_set_bit(type, &housekeeping.flags, HK_TYPE_MAX) {
		/* We need at least one CPU to handle housekeeping work */
		WARN_ON_ONCE(cpumask_empty(housekeeping.cpumasks[type]));
	}
}

static void __init housekeeping_setup_type(enum hk_type type,
					   cpumask_var_t housekeeping_staging)
{

	alloc_bootmem_cpumask_var(&housekeeping.cpumasks[type]);
	cpumask_copy(housekeeping.cpumasks[type],
		     housekeeping_staging);
}

static int __init housekeeping_setup(char *str, unsigned long flags)
{
	cpumask_var_t non_housekeeping_mask, housekeeping_staging;
	unsigned int first_cpu;
	int err = 0;

	if ((flags & HK_FLAG_TICK) && !(housekeeping.flags & HK_FLAG_TICK)) {
		if (!IS_ENABLED(CONFIG_NO_HZ_FULL)) {
			pr_warn("Housekeeping: nohz unsupported."
				" Build with CONFIG_NO_HZ_FULL\n");
			return 0;
		}
	}

	alloc_bootmem_cpumask_var(&non_housekeeping_mask);
	if (cpulist_parse(str, non_housekeeping_mask) < 0) {
		pr_warn("Housekeeping: nohz_full= or isolcpus= incorrect CPU range\n");
		goto free_non_housekeeping_mask;
	}

	alloc_bootmem_cpumask_var(&housekeeping_staging);
	cpumask_andnot(housekeeping_staging,
		       cpu_possible_mask, non_housekeeping_mask);

	first_cpu = cpumask_first_and(cpu_present_mask, housekeeping_staging);
	if (first_cpu >= nr_cpu_ids || first_cpu >= setup_max_cpus) {
		__cpumask_set_cpu(smp_processor_id(), housekeeping_staging);
		__cpumask_clear_cpu(smp_processor_id(), non_housekeeping_mask);
		if (!housekeeping.flags) {
			pr_warn("Housekeeping: must include one present CPU, "
				"using boot CPU:%d\n", smp_processor_id());
		}
	}

	if (cpumask_empty(non_housekeeping_mask))
		goto free_housekeeping_staging;

	if (!housekeeping.flags) {
		/* First setup call ("nohz_full=" or "isolcpus=") */
		enum hk_type type;

		for_each_set_bit(type, &flags, HK_TYPE_MAX)
			housekeeping_setup_type(type, housekeeping_staging);
	} else {
		/* Second setup call ("nohz_full=" after "isolcpus=" or the reverse) */
		enum hk_type type;
		unsigned long iter_flags = flags & housekeeping.flags;

		for_each_set_bit(type, &iter_flags, HK_TYPE_MAX) {
			if (!cpumask_equal(housekeeping_staging,
					   housekeeping.cpumasks[type])) {
				pr_warn("Housekeeping: nohz_full= must match isolcpus=\n");
				goto free_housekeeping_staging;
			}
		}

		iter_flags = flags & ~housekeeping.flags;

		for_each_set_bit(type, &iter_flags, HK_TYPE_MAX)
			housekeeping_setup_type(type, housekeeping_staging);
	}

	if ((flags & HK_FLAG_TICK) && !(housekeeping.flags & HK_FLAG_TICK))
		tick_nohz_full_setup(non_housekeeping_mask);

	housekeeping.flags |= flags;
	err = 1;

free_housekeeping_staging:
	free_bootmem_cpumask_var(housekeeping_staging);
free_non_housekeeping_mask:
	free_bootmem_cpumask_var(non_housekeeping_mask);

	return err;
}

static int __init housekeeping_nohz_full_setup(char *str)
{
	unsigned long flags;

	flags = HK_FLAG_TICK | HK_FLAG_WQ | HK_FLAG_TIMER | HK_FLAG_RCU |
		HK_FLAG_MISC | HK_FLAG_KTHREAD;

	return housekeeping_setup(str, flags);
}
__setup("nohz_full=", housekeeping_nohz_full_setup);

static int __init housekeeping_isolcpus_setup(char *str)
{
	unsigned long flags = 0;
	bool illegal = false;
	char *par;
	int len;

	while (isalpha(*str)) {
		if (!strncmp(str, "nohz,", 5)) {
			str += 5;
			flags |= HK_FLAG_TICK;
			continue;
		}

		if (!strncmp(str, "domain,", 7)) {
			str += 7;
			flags |= HK_FLAG_DOMAIN;
			continue;
		}

		if (!strncmp(str, "managed_irq,", 12)) {
			str += 12;
			flags |= HK_FLAG_MANAGED_IRQ;
			continue;
		}

		/*
		 * Skip unknown sub-parameter and validate that it is not
		 * containing an invalid character.
		 */
		for (par = str, len = 0; *str && *str != ','; str++, len++) {
			if (!isalpha(*str) && *str != '_')
				illegal = true;
		}

		if (illegal) {
			pr_warn("isolcpus: Invalid flag %.*s\n", len, par);
			return 0;
		}

		pr_info("isolcpus: Skipped unknown flag %.*s\n", len, par);
		str++;
	}

	/* Default behaviour for isolcpus without flags */
	if (!flags)
		flags |= HK_FLAG_DOMAIN;

	return housekeeping_setup(str, flags);
}
__setup("isolcpus=", housekeeping_isolcpus_setup);
