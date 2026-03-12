// SPDX-License-Identifier: GPL-2.0

#include <linux/cpumask.h>

__rust_helper
void rust_helper_cpumask_set_cpu(unsigned int cpu, struct cpumask *dstp)
{
	cpumask_set_cpu(cpu, dstp);
}

__rust_helper
void rust_helper___cpumask_set_cpu(unsigned int cpu, struct cpumask *dstp)
{
	__cpumask_set_cpu(cpu, dstp);
}

__rust_helper
void rust_helper_cpumask_clear_cpu(int cpu, struct cpumask *dstp)
{
	cpumask_clear_cpu(cpu, dstp);
}

__rust_helper
void rust_helper___cpumask_clear_cpu(int cpu, struct cpumask *dstp)
{
	__cpumask_clear_cpu(cpu, dstp);
}

__rust_helper
bool rust_helper_cpumask_test_cpu(int cpu, struct cpumask *srcp)
{
	return cpumask_test_cpu(cpu, srcp);
}

__rust_helper
void rust_helper_cpumask_setall(struct cpumask *dstp)
{
	cpumask_setall(dstp);
}

__rust_helper
bool rust_helper_cpumask_empty(struct cpumask *srcp)
{
	return cpumask_empty(srcp);
}

__rust_helper
bool rust_helper_cpumask_full(struct cpumask *srcp)
{
	return cpumask_full(srcp);
}

__rust_helper
unsigned int rust_helper_cpumask_weight(struct cpumask *srcp)
{
	return cpumask_weight(srcp);
}

__rust_helper
void rust_helper_cpumask_copy(struct cpumask *dstp, const struct cpumask *srcp)
{
	cpumask_copy(dstp, srcp);
}

__rust_helper
bool rust_helper_alloc_cpumask_var(cpumask_var_t *mask, gfp_t flags)
{
	return alloc_cpumask_var(mask, flags);
}

__rust_helper
bool rust_helper_zalloc_cpumask_var(cpumask_var_t *mask, gfp_t flags)
{
	return zalloc_cpumask_var(mask, flags);
}

#ifndef CONFIG_CPUMASK_OFFSTACK
__rust_helper
void rust_helper_free_cpumask_var(cpumask_var_t mask)
{
	free_cpumask_var(mask);
}
#endif
