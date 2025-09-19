// SPDX-License-Identifier: GPL-2.0

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "helpers/helpers.h"
#include "helpers/sysfs.h"
#include "cpufreq.h"
#include "cpupower_intern.h"

#if defined(__i386__) || defined(__x86_64__)

#define MSR_AMD_HWCR	0xc0010015

int cpufreq_has_x86_boost_support(unsigned int cpu, int *support, int *active,
				  int *states)
{
	int ret;
	unsigned long long val;

	*support = *active = *states = 0;

	if (cpupower_cpu_info.caps & CPUPOWER_CAP_AMD_CPB) {
		*support = 1;

		/* AMD Family 0x17 does not utilize PCI D18F4 like prior
		 * families and has no fixed discrete boost states but
		 * has Hardware determined variable increments instead.
		 */

		if (cpupower_cpu_info.caps & CPUPOWER_CAP_AMD_CPB_MSR) {
			if (!read_msr(cpu, MSR_AMD_HWCR, &val)) {
				if (!(val & CPUPOWER_AMD_CPBDIS))
					*active = 1;
			}
		} else {
			ret = amd_pci_get_num_boost_states(active, states);
			if (ret)
				return ret;
		}
	} else if (cpupower_cpu_info.caps & CPUPOWER_CAP_AMD_PSTATE) {
		amd_pstate_boost_init(cpu, support, active);
	} else if (cpupower_cpu_info.caps & CPUPOWER_CAP_INTEL_IDA)
		*support = *active = 1;
	return 0;
}

int cpupower_intel_get_perf_bias(unsigned int cpu)
{
	char linebuf[MAX_LINE_LEN];
	char path[SYSFS_PATH_MAX];
	unsigned long val;
	char *endp;

	if (!(cpupower_cpu_info.caps & CPUPOWER_CAP_PERF_BIAS))
		return -1;

	snprintf(path, sizeof(path), PATH_TO_CPU "cpu%u/power/energy_perf_bias", cpu);

	if (cpupower_read_sysfs(path, linebuf, MAX_LINE_LEN) == 0)
		return -1;

	val = strtol(linebuf, &endp, 0);
	if (endp == linebuf || errno == ERANGE)
		return -1;

	return val;
}

int cpupower_intel_set_perf_bias(unsigned int cpu, unsigned int val)
{
	char path[SYSFS_PATH_MAX];
	char linebuf[3] = {};

	if (!(cpupower_cpu_info.caps & CPUPOWER_CAP_PERF_BIAS))
		return -1;

	snprintf(path, sizeof(path), PATH_TO_CPU "cpu%u/power/energy_perf_bias", cpu);
	snprintf(linebuf, sizeof(linebuf), "%d", val);

	if (cpupower_write_sysfs(path, linebuf, 3) <= 0)
		return -1;

	return 0;
}

int cpupower_set_epp(unsigned int cpu, char *epp)
{
	char path[SYSFS_PATH_MAX];
	char linebuf[30] = {};

	snprintf(path, sizeof(path),
		PATH_TO_CPU "cpu%u/cpufreq/energy_performance_preference", cpu);

	if (!is_valid_path(path))
		return -1;

	snprintf(linebuf, sizeof(linebuf), "%s", epp);

	if (cpupower_write_sysfs(path, linebuf, 30) <= 0)
		return -1;

	return 0;
}

int cpupower_set_amd_pstate_mode(char *mode)
{
	char path[SYSFS_PATH_MAX];
	char linebuf[20] = {};

	snprintf(path, sizeof(path), PATH_TO_CPU "amd_pstate/status");

	if (!is_valid_path(path))
		return -1;

	snprintf(linebuf, sizeof(linebuf), "%s\n", mode);

	if (cpupower_write_sysfs(path, linebuf, 20) <= 0)
		return -1;

	return 0;
}

bool cpupower_amd_pstate_enabled(void)
{
	char *driver = cpufreq_get_driver(0);
	bool ret = false;

	if (!driver)
		return ret;

	if (!strncmp(driver, "amd", 3))
		ret = true;

	cpufreq_put_driver(driver);

	return ret;
}

#endif /* #if defined(__i386__) || defined(__x86_64__) */

int cpufreq_has_generic_boost_support(bool *active)
{
	char path[SYSFS_PATH_MAX];
	char linebuf[2] = {};
	unsigned long val;
	char *endp;

	snprintf(path, sizeof(path), PATH_TO_CPU "cpufreq/boost");

	if (!is_valid_path(path))
		return -EACCES;

	if (cpupower_read_sysfs(path, linebuf, 2) <= 0)
		return -EINVAL;

	val = strtoul(linebuf, &endp, 0);
	if (endp == linebuf || errno == ERANGE)
		return -EINVAL;

	switch (val) {
	case 0:
		*active = false;
		break;
	case 1:
		*active = true;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

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

/*
 * print_speed
 *
 * Print the exact CPU frequency with appropriate unit
 */
void print_speed(unsigned long speed, int no_rounding)
{
	unsigned long tmp;

	if (no_rounding) {
		if (speed > 1000000)
			printf("%u.%06u GHz", ((unsigned int)speed / 1000000),
			       ((unsigned int)speed % 1000000));
		else if (speed > 1000)
			printf("%u.%03u MHz", ((unsigned int)speed / 1000),
			       (unsigned int)(speed % 1000));
		else
			printf("%lu kHz", speed);
	} else {
		if (speed > 1000000) {
			tmp = speed % 10000;
			if (tmp >= 5000)
				speed += 10000;
			printf("%u.%02u GHz", ((unsigned int)speed / 1000000),
			       ((unsigned int)(speed % 1000000) / 10000));
		} else if (speed > 100000) {
			tmp = speed % 1000;
			if (tmp >= 500)
				speed += 1000;
			printf("%u MHz", ((unsigned int)speed / 1000));
		} else if (speed > 1000) {
			tmp = speed % 100;
			if (tmp >= 50)
				speed += 100;
			printf("%u.%01u MHz", ((unsigned int)speed / 1000),
			       ((unsigned int)(speed % 1000) / 100));
		}
	}
}

int cpupower_set_turbo_boost(int turbo_boost)
{
	char path[SYSFS_PATH_MAX];
	char linebuf[2] = {};

	snprintf(path, sizeof(path), PATH_TO_CPU "cpufreq/boost");

	if (!is_valid_path(path))
		return -1;

	snprintf(linebuf, sizeof(linebuf), "%d", turbo_boost);

	if (cpupower_write_sysfs(path, linebuf, 2) <= 0)
		return -1;

	return 0;
}
