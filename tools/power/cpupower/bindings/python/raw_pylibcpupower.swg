/* SPDX-License-Identifier: GPL-2.0-only */

%module raw_pylibcpupower
%{
#include "../../lib/cpupower_intern.h"
#include "../../lib/acpi_cppc.h"
#include "../../lib/cpufreq.h"
#include "../../lib/cpuidle.h"
#include "../../lib/cpupower.h"
#include "../../lib/powercap.h"
%}

/*
 * cpupower_intern.h
 */

#define PATH_TO_CPU "/sys/devices/system/cpu/"
#define MAX_LINE_LEN 4096
#define SYSFS_PATH_MAX 255

int is_valid_path(const char *path);

unsigned int cpupower_read_sysfs(const char *path, char *buf, size_t buflen);

unsigned int cpupower_write_sysfs(const char *path, char *buf, size_t buflen);

/*
 * acpi_cppc.h
 */

enum acpi_cppc_value {
	HIGHEST_PERF,
	LOWEST_PERF,
	NOMINAL_PERF,
	LOWEST_NONLINEAR_PERF,
	LOWEST_FREQ,
	NOMINAL_FREQ,
	REFERENCE_PERF,
	WRAPAROUND_TIME,
	MAX_CPPC_VALUE_FILES
};

unsigned long acpi_cppc_get_data(unsigned int cpu,
				 enum acpi_cppc_value which);

/*
 * cpufreq.h
 */

struct cpufreq_policy {
	unsigned long min;
	unsigned long max;
	char *governor;
};

struct cpufreq_available_governors {
	char *governor;
	struct cpufreq_available_governors *next;
	struct cpufreq_available_governors *first;
};

struct cpufreq_available_frequencies {
	unsigned long frequency;
	struct cpufreq_available_frequencies *next;
	struct cpufreq_available_frequencies *first;
};


struct cpufreq_affected_cpus {
	unsigned int cpu;
	struct cpufreq_affected_cpus *next;
	struct cpufreq_affected_cpus *first;
};

struct cpufreq_stats {
	unsigned long frequency;
	unsigned long long time_in_state;
	struct cpufreq_stats *next;
	struct cpufreq_stats *first;
};

unsigned long cpufreq_get_freq_kernel(unsigned int cpu);

unsigned long cpufreq_get_freq_hardware(unsigned int cpu);

#define cpufreq_get(cpu) cpufreq_get_freq_kernel(cpu);

unsigned long cpufreq_get_transition_latency(unsigned int cpu);

int cpufreq_get_hardware_limits(unsigned int cpu,
				unsigned long *min,
				unsigned long *max);

char *cpufreq_get_driver(unsigned int cpu);

void cpufreq_put_driver(char *ptr);

struct cpufreq_policy *cpufreq_get_policy(unsigned int cpu);

void cpufreq_put_policy(struct cpufreq_policy *policy);

struct cpufreq_available_governors
*cpufreq_get_available_governors(unsigned int cpu);

void cpufreq_put_available_governors(
	struct cpufreq_available_governors *first);

struct cpufreq_available_frequencies
*cpufreq_get_available_frequencies(unsigned int cpu);

void cpufreq_put_available_frequencies(
		struct cpufreq_available_frequencies *first);

struct cpufreq_available_frequencies
*cpufreq_get_boost_frequencies(unsigned int cpu);

void cpufreq_put_boost_frequencies(
		struct cpufreq_available_frequencies *first);

struct cpufreq_affected_cpus *cpufreq_get_affected_cpus(unsigned
							int cpu);

void cpufreq_put_affected_cpus(struct cpufreq_affected_cpus *first);

struct cpufreq_affected_cpus *cpufreq_get_related_cpus(unsigned
							int cpu);

void cpufreq_put_related_cpus(struct cpufreq_affected_cpus *first);

struct cpufreq_stats *cpufreq_get_stats(unsigned int cpu,
					unsigned long long *total_time);

void cpufreq_put_stats(struct cpufreq_stats *stats);

unsigned long cpufreq_get_transitions(unsigned int cpu);

int cpufreq_set_policy(unsigned int cpu, struct cpufreq_policy *policy);

int cpufreq_modify_policy_min(unsigned int cpu, unsigned long min_freq);

int cpufreq_modify_policy_max(unsigned int cpu, unsigned long max_freq);

int cpufreq_modify_policy_governor(unsigned int cpu, char *governor);

int cpufreq_set_frequency(unsigned int cpu,
				unsigned long target_frequency);

unsigned long cpufreq_get_sysfs_value_from_table(unsigned int cpu,
						 const char **table,
						 unsigned int index,
						 unsigned int size);

/*
 * cpuidle.h
 */

int cpuidle_is_state_disabled(unsigned int cpu,
				       unsigned int idlestate);
int cpuidle_state_disable(unsigned int cpu, unsigned int idlestate,
				   unsigned int disable);
unsigned long cpuidle_state_latency(unsigned int cpu,
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

/*
 * cpupower.h
 */

struct cpupower_topology {
	/* Amount of CPU cores, packages and threads per core in the system */
	unsigned int cores;
	unsigned int pkgs;
	unsigned int threads; /* per core */

	/* Array gets mallocated with cores entries, holding per core info */
	struct cpuid_core_info *core_info;
};

struct cpuid_core_info {
	int pkg;
	int core;
	int cpu;

	/* flags */
	unsigned int is_online:1;
};

int get_cpu_topology(struct cpupower_topology *cpu_top);

void cpu_topology_release(struct cpupower_topology cpu_top);

int cpupower_is_cpu_online(unsigned int cpu);

/*
 * powercap.h
 */

struct powercap_zone {
	char name[MAX_LINE_LEN];
	/*
	 * sys_name relative to PATH_TO_POWERCAP,
	 * do not forget the / in between
	 */
	char sys_name[SYSFS_PATH_MAX];
	int tree_depth;
	struct powercap_zone *parent;
	struct powercap_zone *children[POWERCAP_MAX_CHILD_ZONES];
	/* More possible caps or attributes to be added? */
	uint32_t has_power_uw:1,
		 has_energy_uj:1;

};

int powercap_walk_zones(struct powercap_zone *zone,
			int (*f)(struct powercap_zone *zone));

struct powercap_zone *powercap_init_zones(void);

int powercap_get_enabled(int *mode);

int powercap_set_enabled(int mode);

int powercap_get_driver(char *driver, int buflen);

int powercap_get_max_energy_range_uj(struct powercap_zone *zone, uint64_t *val);

int powercap_get_energy_uj(struct powercap_zone *zone, uint64_t *val);

int powercap_get_max_power_range_uw(struct powercap_zone *zone, uint64_t *val);

int powercap_get_power_uw(struct powercap_zone *zone, uint64_t *val);

int powercap_zone_get_enabled(struct powercap_zone *zone, int *mode);

int powercap_zone_set_enabled(struct powercap_zone *zone, int mode);
