// SPDX-License-Identifier: GPL-2.0

#include <linux/cpufreq.h>

#ifdef CONFIG_CPU_FREQ
void rust_helper_cpufreq_register_em_with_opp(struct cpufreq_policy *policy)
{
	cpufreq_register_em_with_opp(policy);
}
#endif
