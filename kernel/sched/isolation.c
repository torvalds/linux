// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Housekeeping management. Manage the targets for routine code that can run on
 *  any CPU: unbound workqueues, timers, kthreads and any offloadable work.
 *
 * Copyright (C) 2017 Red Hat, Inc., Frederic Weisbecker
 * Copyright (C) 2017-2018 SUSE, Frederic Weisbecker
 *
 */
#include <linux/sched/isolation.h>
#include <linux/pci.h>
#include "sched.h"

enum hk_flags {
	HK_FLAG_DOMAIN_BOOT	= BIT(HK_TYPE_DOMAIN_BOOT),
	HK_FLAG_DOMAIN		= BIT(HK_TYPE_DOMAIN),
	HK_FLAG_MANAGED_IRQ	= BIT(HK_TYPE_MANAGED_IRQ),
	HK_FLAG_KERNEL_NOISE	= BIT(HK_TYPE_KERNEL_NOISE),
};

DEFINE_STATIC_KEY_FALSE(housekeeping_overridden);
EXPORT_SYMBOL_GPL(housekeeping_overridden);

struct housekeeping {
	struct cpumask __rcu *cpumasks[HK_TYPE_MAX];
	unsigned long flags;
};

static struct housekeeping housekeeping;

bool housekeeping_enabled(enum hk_type type)
{
	return !!(READ_ONCE(housekeeping.flags) & BIT(type));
}
EXPORT_SYMBOL_GPL(housekeeping_enabled);

static bool housekeeping_dereference_check(enum hk_type type)
{
	if (IS_ENABLED(CONFIG_LOCKDEP) && type == HK_TYPE_DOMAIN) {
		/* Cpuset isn't even writable yet? */
		if (system_state <= SYSTEM_SCHEDULING)
			return true;

		/* CPU hotplug write locked, so cpuset partition can't be overwritten */
		if (IS_ENABLED(CONFIG_HOTPLUG_CPU) && lockdep_is_cpus_write_held())
			return true;

		/* Cpuset lock held, partitions not writable */
		if (IS_ENABLED(CONFIG_CPUSETS) && lockdep_is_cpuset_held())
			return true;

		return false;
	}

	return true;
}

static inline struct cpumask *housekeeping_cpumask_dereference(enum hk_type type)
{
	return rcu_dereference_all_check(housekeeping.cpumasks[type],
					 housekeeping_dereference_check(type));
}

const struct cpumask *housekeeping_cpumask(enum hk_type type)
{
	const struct cpumask *mask = NULL;

	if (static_branch_unlikely(&housekeeping_overridden)) {
		if (READ_ONCE(housekeeping.flags) & BIT(type))
			mask = housekeeping_cpumask_dereference(type);
	}
	if (!mask)
		mask = cpu_possible_mask;
	return mask;
}
EXPORT_SYMBOL_GPL(housekeeping_cpumask);

int housekeeping_any_cpu(enum hk_type type)
{
	int cpu;

	if (static_branch_unlikely(&housekeeping_overridden)) {
		if (housekeeping.flags & BIT(type)) {
			cpu = sched_numa_find_closest(housekeeping_cpumask(type), smp_processor_id());
			if (cpu < nr_cpu_ids)
				return cpu;

			cpu = cpumask_any_and_distribute(housekeeping_cpumask(type), cpu_online_mask);
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

void housekeeping_affine(struct task_struct *t, enum hk_type type)
{
	if (static_branch_unlikely(&housekeeping_overridden))
		if (housekeeping.flags & BIT(type))
			set_cpus_allowed_ptr(t, housekeeping_cpumask(type));
}
EXPORT_SYMBOL_GPL(housekeeping_affine);

bool housekeeping_test_cpu(int cpu, enum hk_type type)
{
	if (static_branch_unlikely(&housekeeping_overridden) &&
	    READ_ONCE(housekeeping.flags) & BIT(type))
		return cpumask_test_cpu(cpu, housekeeping_cpumask(type));
	return true;
}
EXPORT_SYMBOL_GPL(housekeeping_test_cpu);

int housekeeping_update(struct cpumask *isol_mask)
{
	struct cpumask *trial, *old = NULL;
	int err;

	lockdep_assert_cpus_held();

	trial = kmalloc(cpumask_size(), GFP_KERNEL);
	if (!trial)
		return -ENOMEM;

	cpumask_andnot(trial, housekeeping_cpumask(HK_TYPE_DOMAIN_BOOT), isol_mask);
	if (!cpumask_intersects(trial, cpu_online_mask)) {
		kfree(trial);
		return -EINVAL;
	}

	if (!housekeeping.flags)
		static_branch_enable_cpuslocked(&housekeeping_overridden);

	if (housekeeping.flags & HK_FLAG_DOMAIN)
		old = housekeeping_cpumask_dereference(HK_TYPE_DOMAIN);
	else
		WRITE_ONCE(housekeeping.flags, housekeeping.flags | HK_FLAG_DOMAIN);
	rcu_assign_pointer(housekeeping.cpumasks[HK_TYPE_DOMAIN], trial);

	synchronize_rcu();

	pci_probe_flush_workqueue();
	mem_cgroup_flush_workqueue();
	vmstat_flush_workqueue();

	err = workqueue_unbound_housekeeping_update(housekeeping_cpumask(HK_TYPE_DOMAIN));
	WARN_ON_ONCE(err < 0);

	err = tmigr_isolated_exclude_cpumask(isol_mask);
	WARN_ON_ONCE(err < 0);

	err = kthreads_update_housekeeping();
	WARN_ON_ONCE(err < 0);

	kfree(old);

	return 0;
}

void __init housekeeping_init(void)
{
	enum hk_type type;

	if (!housekeeping.flags)
		return;

	static_branch_enable(&housekeeping_overridden);

	if (housekeeping.flags & HK_FLAG_KERNEL_NOISE)
		sched_tick_offload_init();
	/*
	 * Realloc with a proper allocator so that any cpumask update
	 * can indifferently free the old version with kfree().
	 */
	for_each_set_bit(type, &housekeeping.flags, HK_TYPE_MAX) {
		struct cpumask *omask, *nmask = kmalloc(cpumask_size(), GFP_KERNEL);

		if (WARN_ON_ONCE(!nmask))
			return;

		omask = rcu_dereference(housekeeping.cpumasks[type]);

		/* We need at least one CPU to handle housekeeping work */
		WARN_ON_ONCE(cpumask_empty(omask));
		cpumask_copy(nmask, omask);
		RCU_INIT_POINTER(housekeeping.cpumasks[type], nmask);
		memblock_free(omask, cpumask_size());
	}
}

static void __init housekeeping_setup_type(enum hk_type type,
					   cpumask_var_t housekeeping_staging)
{
	struct cpumask *mask = memblock_alloc_or_panic(cpumask_size(), SMP_CACHE_BYTES);

	cpumask_copy(mask, housekeeping_staging);
	RCU_INIT_POINTER(housekeeping.cpumasks[type], mask);
}

static int __init housekeeping_setup(char *str, unsigned long flags)
{
	cpumask_var_t non_housekeeping_mask, housekeeping_staging;
	unsigned int first_cpu;
	int err = 0;

	if ((flags & HK_FLAG_KERNEL_NOISE) && !(housekeeping.flags & HK_FLAG_KERNEL_NOISE)) {
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
					   housekeeping_cpumask(type))) {
				pr_warn("Housekeeping: nohz_full= must match isolcpus=\n");
				goto free_housekeeping_staging;
			}
		}

		/*
		 * Check the combination of nohz_full and isolcpus=domain,
		 * necessary to avoid problems with the timer migration
		 * hierarchy. managed_irq is ignored by this check since it
		 * isn't considered in the timer migration logic.
		 */
		iter_flags = housekeeping.flags & (HK_FLAG_KERNEL_NOISE | HK_FLAG_DOMAIN);
		type = find_first_bit(&iter_flags, HK_TYPE_MAX);
		/*
		 * Pass the check if none of these flags were previously set or
		 * are not in the current selection.
		 */
		iter_flags = flags & (HK_FLAG_KERNEL_NOISE | HK_FLAG_DOMAIN);
		first_cpu = (type == HK_TYPE_MAX || !iter_flags) ? 0 :
			    cpumask_first_and_and(cpu_present_mask,
						  housekeeping_staging, housekeeping_cpumask(type));
		if (first_cpu >= min(nr_cpu_ids, setup_max_cpus)) {
			pr_warn("Housekeeping: must include one present CPU "
				"neither in nohz_full= nor in isolcpus=domain, "
				"ignoring setting %s\n", str);
			goto free_housekeeping_staging;
		}

		iter_flags = flags & ~housekeeping.flags;

		for_each_set_bit(type, &iter_flags, HK_TYPE_MAX)
			housekeeping_setup_type(type, housekeeping_staging);
	}

	if ((flags & HK_FLAG_KERNEL_NOISE) && !(housekeeping.flags & HK_FLAG_KERNEL_NOISE))
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

	flags = HK_FLAG_KERNEL_NOISE;

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
		/*
		 * isolcpus=nohz is equivalent to nohz_full.
		 */
		if (!strncmp(str, "nohz,", 5)) {
			str += 5;
			flags |= HK_FLAG_KERNEL_NOISE;
			continue;
		}

		if (!strncmp(str, "domain,", 7)) {
			str += 7;
			flags |= HK_FLAG_DOMAIN | HK_FLAG_DOMAIN_BOOT;
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
		flags |= HK_FLAG_DOMAIN | HK_FLAG_DOMAIN_BOOT;

	return housekeeping_setup(str, flags);
}
__setup("isolcpus=", housekeeping_isolcpus_setup);
