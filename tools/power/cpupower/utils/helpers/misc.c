// SPDX-License-Identifier: GPL-2.0

#include <stdio.h>
#include <stdlib.h>

#include "helpers/helpers.h"

#if defined(__i386__) || defined(__x86_64__)

#define MSR_AMD_HWCR	0xc0010015

int cpufreq_has_boost_support(unsigned int cpu, int *support, int *active,
			int *states)
{
	struct cpupower_cpu_info cpu_info;
	int ret;
	unsigned long long val;

	*support = *active = *states = 0;

	ret = get_cpu_info(&cpu_info);
	if (ret)
		return ret;

	if (cpupower_cpu_info.caps & CPUPOWER_CAP_AMD_CBP) {
		*support = 1;

		/* AMD Family 0x17 does not utilize PCI D18F4 like prior
		 * families and has no fixed discrete boost states but
		 * has Hardware determined variable increments instead.
		 */

		if (cpu_info.family == 0x17 || cpu_info.family == 0x18) {
			if (!read_msr(cpu, MSR_AMD_HWCR, &val)) {
				if (!(val & CPUPOWER_AMD_CPBDIS))
					*active = 1;
			}
		} else {
			ret = amd_pci_get_num_boost_states(active, states);
			if (ret)
				return ret;
		}
	} else if (cpupower_cpu_info.caps & CPUPOWER_CAP_INTEL_IDA)
		*support = *active = 1;
	return 0;
}
#endif /* #if defined(__i386__) || defined(__x86_64__) */

/* get_cpustate
 *
 * Gather the information of all online CPUs into bitmask struct
 */
void get_cpustate(void)
{
	unsigned int cpu = 0;

	bitmask_clearall(online_cpus);
	bitmask_clearall(offline_cpus);

	for (cpu = bitmask_first(cpus_chosen);
		cpu <= bitmask_last(cpus_chosen); cpu++) {

		if (cpupower_is_cpu_online(cpu) == 1)
			bitmask_setbit(online_cpus, cpu);
		else
			bitmask_setbit(offline_cpus, cpu);

		continue;
	}
}

/* print_online_cpus
 *
 * Print the CPU numbers of all CPUs that are online currently
 */
void print_online_cpus(void)
{
	int str_len = 0;
	char *online_cpus_str = NULL;

	str_len = online_cpus->size * 5;
	online_cpus_str = (void *)malloc(sizeof(char) * str_len);

	if (!bitmask_isallclear(online_cpus)) {
		bitmask_displaylist(online_cpus_str, str_len, online_cpus);
		printf(_("Following CPUs are online:\n%s\n"), online_cpus_str);
	}
}

/* print_offline_cpus
 *
 * Print the CPU numbers of all CPUs that are offline currently
 */
void print_offline_cpus(void)
{
	int str_len = 0;
	char *offline_cpus_str = NULL;

	str_len = offline_cpus->size * 5;
	offline_cpus_str = (void *)malloc(sizeof(char) * str_len);

	if (!bitmask_isallclear(offline_cpus)) {
		bitmask_displaylist(offline_cpus_str, str_len, offline_cpus);
		printf(_("Following CPUs are offline:\n%s\n"), offline_cpus_str);
		printf(_("cpupower set operation was not performed on them\n"));
	}
}
