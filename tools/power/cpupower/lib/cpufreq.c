// SPDX-License-Identifier: GPL-2.0-only
/*
 *  (C) 2004-2009  Dominik Brodowski <linux@dominikbrodowski.de>
 */


#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "cpufreq.h"
#include "cpupower_intern.h"

/* CPUFREQ sysfs access **************************************************/

/* helper function to read file from /sys into given buffer */
/* fname is a relative path under "cpuX/cpufreq" dir */
static unsigned int sysfs_cpufreq_read_file(unsigned int cpu, const char *fname,
					    char *buf, size_t buflen)
{
	char path[SYSFS_PATH_MAX];

	snprintf(path, sizeof(path), PATH_TO_CPU "cpu%u/cpufreq/%s",
			 cpu, fname);
	return cpupower_read_sysfs(path, buf, buflen);
}

/* helper function to write a new value to a /sys file */
/* fname is a relative path under "cpuX/cpufreq" dir */
static unsigned int sysfs_cpufreq_write_file(unsigned int cpu,
					     const char *fname,
					     const char *value, size_t len)
{
	char path[SYSFS_PATH_MAX];
	int fd;
	ssize_t numwrite;

	snprintf(path, sizeof(path), PATH_TO_CPU "cpu%u/cpufreq/%s",
			 cpu, fname);

	fd = open(path, O_WRONLY);
	if (fd == -1)
		return 0;

	numwrite = write(fd, value, len);
	if (numwrite < 1) {
		close(fd);
		return 0;
	}

	close(fd);

	return (unsigned int) numwrite;
}

/* read access to files which contain one numeric value */

enum cpufreq_value {
	CPUINFO_CUR_FREQ,
	CPUINFO_MIN_FREQ,
	CPUINFO_MAX_FREQ,
	CPUINFO_LATENCY,
	SCALING_CUR_FREQ,
	SCALING_MIN_FREQ,
	SCALING_MAX_FREQ,
	STATS_NUM_TRANSITIONS,
	MAX_CPUFREQ_VALUE_READ_FILES
};

static const char *cpufreq_value_files[MAX_CPUFREQ_VALUE_READ_FILES] = {
	[CPUINFO_CUR_FREQ] = "cpuinfo_cur_freq",
	[CPUINFO_MIN_FREQ] = "cpuinfo_min_freq",
	[CPUINFO_MAX_FREQ] = "cpuinfo_max_freq",
	[CPUINFO_LATENCY]  = "cpuinfo_transition_latency",
	[SCALING_CUR_FREQ] = "scaling_cur_freq",
	[SCALING_MIN_FREQ] = "scaling_min_freq",
	[SCALING_MAX_FREQ] = "scaling_max_freq",
	[STATS_NUM_TRANSITIONS] = "stats/total_trans"
};


static unsigned long sysfs_cpufreq_get_one_value(unsigned int cpu,
						 enum cpufreq_value which)
{
	unsigned long value;
	unsigned int len;
	char linebuf[MAX_LINE_LEN];
	char *endp;

	if (which >= MAX_CPUFREQ_VALUE_READ_FILES)
		return 0;

	len = sysfs_cpufreq_read_file(cpu, cpufreq_value_files[which],
				linebuf, sizeof(linebuf));

	if (len == 0)
		return 0;

	value = strtoul(linebuf, &endp, 0);

	if (endp == linebuf || errno == ERANGE)
		return 0;

	return value;
}

/* read access to files which contain one string */

enum cpufreq_string {
	SCALING_DRIVER,
	SCALING_GOVERNOR,
	MAX_CPUFREQ_STRING_FILES
};

static const char *cpufreq_string_files[MAX_CPUFREQ_STRING_FILES] = {
	[SCALING_DRIVER] = "scaling_driver",
	[SCALING_GOVERNOR] = "scaling_governor",
};


static char *sysfs_cpufreq_get_one_string(unsigned int cpu,
					   enum cpufreq_string which)
{
	char linebuf[MAX_LINE_LEN];
	char *result;
	unsigned int len;

	if (which >= MAX_CPUFREQ_STRING_FILES)
		return NULL;

	len = sysfs_cpufreq_read_file(cpu, cpufreq_string_files[which],
				linebuf, sizeof(linebuf));
	if (len == 0)
		return NULL;

	result = strdup(linebuf);
	if (result == NULL)
		return NULL;

	if (result[strlen(result) - 1] == '\n')
		result[strlen(result) - 1] = '\0';

	return result;
}

/* write access */

enum cpufreq_write {
	WRITE_SCALING_MIN_FREQ,
	WRITE_SCALING_MAX_FREQ,
	WRITE_SCALING_GOVERNOR,
	WRITE_SCALING_SET_SPEED,
	MAX_CPUFREQ_WRITE_FILES
};

static const char *cpufreq_write_files[MAX_CPUFREQ_WRITE_FILES] = {
	[WRITE_SCALING_MIN_FREQ] = "scaling_min_freq",
	[WRITE_SCALING_MAX_FREQ] = "scaling_max_freq",
	[WRITE_SCALING_GOVERNOR] = "scaling_governor",
	[WRITE_SCALING_SET_SPEED] = "scaling_setspeed",
};

static int sysfs_cpufreq_write_one_value(unsigned int cpu,
					 enum cpufreq_write which,
					 const char *new_value, size_t len)
{
	if (which >= MAX_CPUFREQ_WRITE_FILES)
		return 0;

	if (sysfs_cpufreq_write_file(cpu, cpufreq_write_files[which],
					new_value, len) != len)
		return -ENODEV;

	return 0;
};

unsigned long cpufreq_get_freq_kernel(unsigned int cpu)
{
	return sysfs_cpufreq_get_one_value(cpu, SCALING_CUR_FREQ);
}

unsigned long cpufreq_get_freq_hardware(unsigned int cpu)
{
	return sysfs_cpufreq_get_one_value(cpu, CPUINFO_CUR_FREQ);
}

unsigned long cpufreq_get_transition_latency(unsigned int cpu)
{
	return sysfs_cpufreq_get_one_value(cpu, CPUINFO_LATENCY);
}

int cpufreq_get_hardware_limits(unsigned int cpu,
				unsigned long *min,
				unsigned long *max)
{
	if ((!min) || (!max))
		return -EINVAL;

	*min = sysfs_cpufreq_get_one_value(cpu, CPUINFO_MIN_FREQ);
	if (!*min)
		return -ENODEV;

	*max = sysfs_cpufreq_get_one_value(cpu, CPUINFO_MAX_FREQ);
	if (!*max)
		return -ENODEV;

	return 0;
}

char *cpufreq_get_driver(unsigned int cpu)
{
	return sysfs_cpufreq_get_one_string(cpu, SCALING_DRIVER);
}

void cpufreq_put_driver(char *ptr)
{
	if (!ptr)
		return;
	free(ptr);
}

struct cpufreq_policy *cpufreq_get_policy(unsigned int cpu)
{
	struct cpufreq_policy *policy;

	policy = malloc(sizeof(struct cpufreq_policy));
	if (!policy)
		return NULL;

	policy->governor = sysfs_cpufreq_get_one_string(cpu, SCALING_GOVERNOR);
	if (!policy->governor) {
		free(policy);
		return NULL;
	}
	policy->min = sysfs_cpufreq_get_one_value(cpu, SCALING_MIN_FREQ);
	policy->max = sysfs_cpufreq_get_one_value(cpu, SCALING_MAX_FREQ);
	if ((!policy->min) || (!policy->max)) {
		free(policy->governor);
		free(policy);
		return NULL;
	}

	return policy;
}

void cpufreq_put_policy(struct cpufreq_policy *policy)
{
	if ((!policy) || (!policy->governor))
		return;

	free(policy->governor);
	policy->governor = NULL;
	free(policy);
}

struct cpufreq_available_governors *cpufreq_get_available_governors(unsigned
								int cpu)
{
	struct cpufreq_available_governors *first = NULL;
	struct cpufreq_available_governors *current = NULL;
	char linebuf[MAX_LINE_LEN];
	unsigned int pos, i;
	unsigned int len;

	len = sysfs_cpufreq_read_file(cpu, "scaling_available_governors",
				linebuf, sizeof(linebuf));
	if (len == 0)
		return NULL;

	pos = 0;
	for (i = 0; i < len; i++) {
		if (linebuf[i] == ' ' || linebuf[i] == '\n') {
			if (i - pos < 2)
				continue;
			if (current) {
				current->next = malloc(sizeof(*current));
				if (!current->next)
					goto error_out;
				current = current->next;
			} else {
				first = malloc(sizeof(*first));
				if (!first)
					return NULL;
				current = first;
			}
			current->first = first;
			current->next = NULL;

			current->governor = malloc(i - pos + 1);
			if (!current->governor)
				goto error_out;

			memcpy(current->governor, linebuf + pos, i - pos);
			current->governor[i - pos] = '\0';
			pos = i + 1;
		}
	}

	return first;

 error_out:
	while (first) {
		current = first->next;
		if (first->governor)
			free(first->governor);
		free(first);
		first = current;
	}
	return NULL;
}

void cpufreq_put_available_governors(struct cpufreq_available_governors *any)
{
	struct cpufreq_available_governors *tmp, *next;

	if (!any)
		return;

	tmp = any->first;
	while (tmp) {
		next = tmp->next;
		if (tmp->governor)
			free(tmp->governor);
		free(tmp);
		tmp = next;
	}
}


struct cpufreq_available_frequencies
*cpufreq_get_available_frequencies(unsigned int cpu)
{
	struct cpufreq_available_frequencies *first = NULL;
	struct cpufreq_available_frequencies *current = NULL;
	char one_value[SYSFS_PATH_MAX];
	char linebuf[MAX_LINE_LEN];
	unsigned int pos, i;
	unsigned int len;

	len = sysfs_cpufreq_read_file(cpu, "scaling_available_frequencies",
				      linebuf, sizeof(linebuf));
	if (len == 0)
		return NULL;

	pos = 0;
	for (i = 0; i < len; i++) {
		if (linebuf[i] == ' ' || linebuf[i] == '\n') {
			if (i - pos < 2)
				continue;
			if (i - pos >= SYSFS_PATH_MAX)
				goto error_out;
			if (current) {
				current->next = malloc(sizeof(*current));
				if (!current->next)
					goto error_out;
				current = current->next;
			} else {
				first = malloc(sizeof(*first));
				if (!first)
					return NULL;
				current = first;
			}
			current->first = first;
			current->next = NULL;

			memcpy(one_value, linebuf + pos, i - pos);
			one_value[i - pos] = '\0';
			if (sscanf(one_value, "%lu", &current->frequency) != 1)
				goto error_out;

			pos = i + 1;
		}
	}

	return first;

 error_out:
	while (first) {
		current = first->next;
		free(first);
		first = current;
	}
	return NULL;
}

struct cpufreq_available_frequencies
*cpufreq_get_boost_frequencies(unsigned int cpu)
{
	struct cpufreq_available_frequencies *first = NULL;
	struct cpufreq_available_frequencies *current = NULL;
	char one_value[SYSFS_PATH_MAX];
	char linebuf[MAX_LINE_LEN];
	unsigned int pos, i;
	unsigned int len;

	len = sysfs_cpufreq_read_file(cpu, "scaling_boost_frequencies",
				      linebuf, sizeof(linebuf));
	if (len == 0)
		return NULL;

	pos = 0;
	for (i = 0; i < len; i++) {
		if (linebuf[i] == ' ' || linebuf[i] == '\n') {
			if (i - pos < 2)
				continue;
			if (i - pos >= SYSFS_PATH_MAX)
				goto error_out;
			if (current) {
				current->next = malloc(sizeof(*current));
				if (!current->next)
					goto error_out;
				current = current->next;
			} else {
				first = malloc(sizeof(*first));
				if (!first)
					return NULL;
				current = first;
			}
			current->first = first;
			current->next = NULL;

			memcpy(one_value, linebuf + pos, i - pos);
			one_value[i - pos] = '\0';
			if (sscanf(one_value, "%lu", &current->frequency) != 1)
				goto error_out;

			pos = i + 1;
		}
	}

	return first;

 error_out:
	while (first) {
		current = first->next;
		free(first);
		first = current;
	}
	return NULL;
}

void cpufreq_put_available_frequencies(struct cpufreq_available_frequencies *any)
{
	struct cpufreq_available_frequencies *tmp, *next;

	if (!any)
		return;

	tmp = any->first;
	while (tmp) {
		next = tmp->next;
		free(tmp);
		tmp = next;
	}
}

void cpufreq_put_boost_frequencies(struct cpufreq_available_frequencies *any)
{
	cpufreq_put_available_frequencies(any);
}

static struct cpufreq_affected_cpus *sysfs_get_cpu_list(unsigned int cpu,
							const char *file)
{
	struct cpufreq_affected_cpus *first = NULL;
	struct cpufreq_affected_cpus *current = NULL;
	char one_value[SYSFS_PATH_MAX];
	char linebuf[MAX_LINE_LEN];
	unsigned int pos, i;
	unsigned int len;

	len = sysfs_cpufreq_read_file(cpu, file, linebuf, sizeof(linebuf));
	if (len == 0)
		return NULL;

	pos = 0;
	for (i = 0; i < len; i++) {
		if (i == len || linebuf[i] == ' ' || linebuf[i] == '\n') {
			if (i - pos  < 1)
				continue;
			if (i - pos >= SYSFS_PATH_MAX)
				goto error_out;
			if (current) {
				current->next = malloc(sizeof(*current));
				if (!current->next)
					goto error_out;
				current = current->next;
			} else {
				first = malloc(sizeof(*first));
				if (!first)
					return NULL;
				current = first;
			}
			current->first = first;
			current->next = NULL;

			memcpy(one_value, linebuf + pos, i - pos);
			one_value[i - pos] = '\0';

			if (sscanf(one_value, "%u", &current->cpu) != 1)
				goto error_out;

			pos = i + 1;
		}
	}

	return first;

 error_out:
	while (first) {
		current = first->next;
		free(first);
		first = current;
	}
	return NULL;
}

struct cpufreq_affected_cpus *cpufreq_get_affected_cpus(unsigned int cpu)
{
	return sysfs_get_cpu_list(cpu, "affected_cpus");
}

void cpufreq_put_affected_cpus(struct cpufreq_affected_cpus *any)
{
	struct cpufreq_affected_cpus *tmp, *next;

	if (!any)
		return;

	tmp = any->first;
	while (tmp) {
		next = tmp->next;
		free(tmp);
		tmp = next;
	}
}


struct cpufreq_affected_cpus *cpufreq_get_related_cpus(unsigned int cpu)
{
	return sysfs_get_cpu_list(cpu, "related_cpus");
}

void cpufreq_put_related_cpus(struct cpufreq_affected_cpus *any)
{
	cpufreq_put_affected_cpus(any);
}

static int verify_gov(char *new_gov, char *passed_gov)
{
	unsigned int i, j = 0;

	if (!passed_gov || (strlen(passed_gov) > 19))
		return -EINVAL;

	strncpy(new_gov, passed_gov, 20);
	for (i = 0; i < 20; i++) {
		if (j) {
			new_gov[i] = '\0';
			continue;
		}
		if ((new_gov[i] >= 'a') && (new_gov[i] <= 'z'))
			continue;

		if ((new_gov[i] >= 'A') && (new_gov[i] <= 'Z'))
			continue;

		if (new_gov[i] == '-')
			continue;

		if (new_gov[i] == '_')
			continue;

		if (new_gov[i] == '\0') {
			j = 1;
			continue;
		}
		return -EINVAL;
	}
	new_gov[19] = '\0';
	return 0;
}

int cpufreq_set_policy(unsigned int cpu, struct cpufreq_policy *policy)
{
	char min[SYSFS_PATH_MAX];
	char max[SYSFS_PATH_MAX];
	char gov[SYSFS_PATH_MAX];
	int ret;
	unsigned long old_min;
	int write_max_first;

	if (!policy || !(policy->governor))
		return -EINVAL;

	if (policy->max < policy->min)
		return -EINVAL;

	if (verify_gov(gov, policy->governor))
		return -EINVAL;

	snprintf(min, SYSFS_PATH_MAX, "%lu", policy->min);
	snprintf(max, SYSFS_PATH_MAX, "%lu", policy->max);

	old_min = sysfs_cpufreq_get_one_value(cpu, SCALING_MIN_FREQ);
	write_max_first = (old_min && (policy->max < old_min) ? 0 : 1);

	if (write_max_first) {
		ret = sysfs_cpufreq_write_one_value(cpu, WRITE_SCALING_MAX_FREQ,
						    max, strlen(max));
		if (ret)
			return ret;
	}

	ret = sysfs_cpufreq_write_one_value(cpu, WRITE_SCALING_MIN_FREQ, min,
					    strlen(min));
	if (ret)
		return ret;

	if (!write_max_first) {
		ret = sysfs_cpufreq_write_one_value(cpu, WRITE_SCALING_MAX_FREQ,
						    max, strlen(max));
		if (ret)
			return ret;
	}

	return sysfs_cpufreq_write_one_value(cpu, WRITE_SCALING_GOVERNOR,
					     gov, strlen(gov));
}


int cpufreq_modify_policy_min(unsigned int cpu, unsigned long min_freq)
{
	char value[SYSFS_PATH_MAX];

	snprintf(value, SYSFS_PATH_MAX, "%lu", min_freq);

	return sysfs_cpufreq_write_one_value(cpu, WRITE_SCALING_MIN_FREQ,
					     value, strlen(value));
}


int cpufreq_modify_policy_max(unsigned int cpu, unsigned long max_freq)
{
	char value[SYSFS_PATH_MAX];

	snprintf(value, SYSFS_PATH_MAX, "%lu", max_freq);

	return sysfs_cpufreq_write_one_value(cpu, WRITE_SCALING_MAX_FREQ,
					     value, strlen(value));
}

int cpufreq_modify_policy_governor(unsigned int cpu, char *governor)
{
	char new_gov[SYSFS_PATH_MAX];

	if ((!governor) || (strlen(governor) > 19))
		return -EINVAL;

	if (verify_gov(new_gov, governor))
		return -EINVAL;

	return sysfs_cpufreq_write_one_value(cpu, WRITE_SCALING_GOVERNOR,
					     new_gov, strlen(new_gov));
}

int cpufreq_set_frequency(unsigned int cpu, unsigned long target_frequency)
{
	struct cpufreq_policy *pol = cpufreq_get_policy(cpu);
	char userspace_gov[] = "userspace";
	char freq[SYSFS_PATH_MAX];
	int ret;

	if (!pol)
		return -ENODEV;

	if (strncmp(pol->governor, userspace_gov, 9) != 0) {
		ret = cpufreq_modify_policy_governor(cpu, userspace_gov);
		if (ret) {
			cpufreq_put_policy(pol);
			return ret;
		}
	}

	cpufreq_put_policy(pol);

	snprintf(freq, SYSFS_PATH_MAX, "%lu", target_frequency);

	return sysfs_cpufreq_write_one_value(cpu, WRITE_SCALING_SET_SPEED,
					     freq, strlen(freq));
}

struct cpufreq_stats *cpufreq_get_stats(unsigned int cpu,
					unsigned long long *total_time)
{
	struct cpufreq_stats *first = NULL;
	struct cpufreq_stats *current = NULL;
	char one_value[SYSFS_PATH_MAX];
	char linebuf[MAX_LINE_LEN];
	unsigned int pos, i;
	unsigned int len;

	len = sysfs_cpufreq_read_file(cpu, "stats/time_in_state",
				linebuf, sizeof(linebuf));
	if (len == 0)
		return NULL;

	*total_time = 0;
	pos = 0;
	for (i = 0; i < len; i++) {
		if (i == strlen(linebuf) || linebuf[i] == '\n')	{
			if (i - pos < 2)
				continue;
			if ((i - pos) >= SYSFS_PATH_MAX)
				goto error_out;
			if (current) {
				current->next = malloc(sizeof(*current));
				if (!current->next)
					goto error_out;
				current = current->next;
			} else {
				first = malloc(sizeof(*first));
				if (!first)
					return NULL;
				current = first;
			}
			current->first = first;
			current->next = NULL;

			memcpy(one_value, linebuf + pos, i - pos);
			one_value[i - pos] = '\0';
			if (sscanf(one_value, "%lu %llu",
					&current->frequency,
					&current->time_in_state) != 2)
				goto error_out;

			*total_time = *total_time + current->time_in_state;
			pos = i + 1;
		}
	}

	return first;

 error_out:
	while (first) {
		current = first->next;
		free(first);
		first = current;
	}
	return NULL;
}

void cpufreq_put_stats(struct cpufreq_stats *any)
{
	struct cpufreq_stats *tmp, *next;

	if (!any)
		return;

	tmp = any->first;
	while (tmp) {
		next = tmp->next;
		free(tmp);
		tmp = next;
	}
}

unsigned long cpufreq_get_transitions(unsigned int cpu)
{
	return sysfs_cpufreq_get_one_value(cpu, STATS_NUM_TRANSITIONS);
}
