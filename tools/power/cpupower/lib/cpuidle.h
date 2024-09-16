/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __CPUPOWER_CPUIDLE_H__
#define __CPUPOWER_CPUIDLE_H__

int cpuidle_is_state_disabled(unsigned int cpu,
				       unsigned int idlestate);
int cpuidle_state_disable(unsigned int cpu, unsigned int idlestate,
				   unsigned int disable);
unsigned long cpuidle_state_latency(unsigned int cpu,
						unsigned int idlestate);
unsigned long cpuidle_state_residency(unsigned int cpu,
						unsigned int idlestate);
unsigned long cpuidle_state_usage(unsigned int cpu,
					unsigned int idlestate);
unsigned long long cpuidle_state_time(unsigned int cpu,
						unsigned int idlestate);
char *cpuidle_state_name(unsigned int cpu,
				unsigned int idlestate);
char *cpuidle_state_desc(unsigned int cpu,
				unsigned int idlestate);
unsigned int cpuidle_state_count(unsigned int cpu);

char *cpuidle_get_governor(void);
char *cpuidle_get_driver(void);

#endif /* __CPUPOWER_HELPERS_SYSFS_H__ */
