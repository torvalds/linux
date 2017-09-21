#if defined(__i386__) || defined(__x86_64__)

#include "helpers/helpers.h"

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

		if (cpu_info.family == 0x17) {
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
