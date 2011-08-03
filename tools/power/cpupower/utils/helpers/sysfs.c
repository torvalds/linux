/*
 *  (C) 2004-2009  Dominik Brodowski <linux@dominikbrodowski.de>
 *  (C) 2011       Thomas Renninger <trenn@novell.com> Novell Inc.
 *
 *  Licensed under the terms of the GNU GPL License version 2.
 */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "helpers/sysfs.h"

unsigned int sysfs_read_file(const char *path, char *buf, size_t buflen)
{
	int fd;
	ssize_t numread;

	fd = open(path, O_RDONLY);
	if (fd == -1)
		return 0;

	numread = read(fd, buf, buflen - 1);
	if (numread < 1) {
		close(fd);
		return 0;
	}

	buf[numread] = '\0';
	close(fd);

	return (unsigned int) numread;
}

static unsigned int sysfs_write_file(const char *path,
				     const char *value, size_t len)
{
	int fd;
	ssize_t numwrite;

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

/* CPUidle idlestate specific /sys/devices/system/cpu/cpuX/cpuidle/ access */

/*
 * helper function to read file from /sys into given buffer
 * fname is a relative path under "cpuX/cpuidle/stateX/" dir
 * cstates starting with 0, C0 is not counted as cstate.
 * This means if you want C1 info, pass 0 as idlestate param
 */
unsigned int sysfs_idlestate_read_file(unsigned int cpu, unsigned int idlestate,
			     const char *fname, char *buf, size_t buflen)
{
	char path[SYSFS_PATH_MAX];
	int fd;
	ssize_t numread;

	snprintf(path, sizeof(path), PATH_TO_CPU "cpu%u/cpuidle/state%u/%s",
		 cpu, idlestate, fname);

	fd = open(path, O_RDONLY);
	if (fd == -1)
		return 0;

	numread = read(fd, buf, buflen - 1);
	if (numread < 1) {
		close(fd);
		return 0;
	}

	buf[numread] = '\0';
	close(fd);

	return (unsigned int) numread;
}

/* read access to files which contain one numeric value */

enum idlestate_value {
	IDLESTATE_USAGE,
	IDLESTATE_POWER,
	IDLESTATE_LATENCY,
	IDLESTATE_TIME,
	MAX_IDLESTATE_VALUE_FILES
};

static const char *idlestate_value_files[MAX_IDLESTATE_VALUE_FILES] = {
	[IDLESTATE_USAGE] = "usage",
	[IDLESTATE_POWER] = "power",
	[IDLESTATE_LATENCY] = "latency",
	[IDLESTATE_TIME]  = "time",
};

static unsigned long long sysfs_idlestate_get_one_value(unsigned int cpu,
						     unsigned int idlestate,
						     enum idlestate_value which)
{
	unsigned long long value;
	unsigned int len;
	char linebuf[MAX_LINE_LEN];
	char *endp;

	if (which >= MAX_IDLESTATE_VALUE_FILES)
		return 0;

	len = sysfs_idlestate_read_file(cpu, idlestate,
					idlestate_value_files[which],
					linebuf, sizeof(linebuf));
	if (len == 0)
		return 0;

	value = strtoull(linebuf, &endp, 0);

	if (endp == linebuf || errno == ERANGE)
		return 0;

	return value;
}

/* read access to files which contain one string */

enum idlestate_string {
	IDLESTATE_DESC,
	IDLESTATE_NAME,
	MAX_IDLESTATE_STRING_FILES
};

static const char *idlestate_string_files[MAX_IDLESTATE_STRING_FILES] = {
	[IDLESTATE_DESC] = "desc",
	[IDLESTATE_NAME] = "name",
};


static char *sysfs_idlestate_get_one_string(unsigned int cpu,
					unsigned int idlestate,
					enum idlestate_string which)
{
	char linebuf[MAX_LINE_LEN];
	char *result;
	unsigned int len;

	if (which >= MAX_IDLESTATE_STRING_FILES)
		return NULL;

	len = sysfs_idlestate_read_file(cpu, idlestate,
					idlestate_string_files[which],
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

unsigned long sysfs_get_idlestate_latency(unsigned int cpu,
					unsigned int idlestate)
{
	return sysfs_idlestate_get_one_value(cpu, idlestate, IDLESTATE_LATENCY);
}

unsigned long sysfs_get_idlestate_usage(unsigned int cpu,
					unsigned int idlestate)
{
	return sysfs_idlestate_get_one_value(cpu, idlestate, IDLESTATE_USAGE);
}

unsigned long long sysfs_get_idlestate_time(unsigned int cpu,
					unsigned int idlestate)
{
	return sysfs_idlestate_get_one_value(cpu, idlestate, IDLESTATE_TIME);
}

char *sysfs_get_idlestate_name(unsigned int cpu, unsigned int idlestate)
{
	return sysfs_idlestate_get_one_string(cpu, idlestate, IDLESTATE_NAME);
}

char *sysfs_get_idlestate_desc(unsigned int cpu, unsigned int idlestate)
{
	return sysfs_idlestate_get_one_string(cpu, idlestate, IDLESTATE_DESC);
}

/*
 * Returns number of supported C-states of CPU core cpu
 * Negativ in error case
 * Zero if cpuidle does not export any C-states
 */
int sysfs_get_idlestate_count(unsigned int cpu)
{
	char file[SYSFS_PATH_MAX];
	struct stat statbuf;
	int idlestates = 1;


	snprintf(file, SYSFS_PATH_MAX, PATH_TO_CPU "cpuidle");
	if (stat(file, &statbuf) != 0 || !S_ISDIR(statbuf.st_mode))
		return -ENODEV;

	snprintf(file, SYSFS_PATH_MAX, PATH_TO_CPU "cpu%u/cpuidle/state0", cpu);
	if (stat(file, &statbuf) != 0 || !S_ISDIR(statbuf.st_mode))
		return 0;

	while (stat(file, &statbuf) == 0 && S_ISDIR(statbuf.st_mode)) {
		snprintf(file, SYSFS_PATH_MAX, PATH_TO_CPU
			 "cpu%u/cpuidle/state%d", cpu, idlestates);
		idlestates++;
	}
	idlestates--;
	return idlestates;
}

/* CPUidle general /sys/devices/system/cpu/cpuidle/ sysfs access ********/

/*
 * helper function to read file from /sys into given buffer
 * fname is a relative path under "cpu/cpuidle/" dir
 */
static unsigned int sysfs_cpuidle_read_file(const char *fname, char *buf,
					    size_t buflen)
{
	char path[SYSFS_PATH_MAX];

	snprintf(path, sizeof(path), PATH_TO_CPU "cpuidle/%s", fname);

	return sysfs_read_file(path, buf, buflen);
}



/* read access to files which contain one string */

enum cpuidle_string {
	CPUIDLE_GOVERNOR,
	CPUIDLE_GOVERNOR_RO,
	CPUIDLE_DRIVER,
	MAX_CPUIDLE_STRING_FILES
};

static const char *cpuidle_string_files[MAX_CPUIDLE_STRING_FILES] = {
	[CPUIDLE_GOVERNOR]	= "current_governor",
	[CPUIDLE_GOVERNOR_RO]	= "current_governor_ro",
	[CPUIDLE_DRIVER]	= "current_driver",
};


static char *sysfs_cpuidle_get_one_string(enum cpuidle_string which)
{
	char linebuf[MAX_LINE_LEN];
	char *result;
	unsigned int len;

	if (which >= MAX_CPUIDLE_STRING_FILES)
		return NULL;

	len = sysfs_cpuidle_read_file(cpuidle_string_files[which],
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

char *sysfs_get_cpuidle_governor(void)
{
	char *tmp = sysfs_cpuidle_get_one_string(CPUIDLE_GOVERNOR_RO);
	if (!tmp)
		return sysfs_cpuidle_get_one_string(CPUIDLE_GOVERNOR);
	else
		return tmp;
}

char *sysfs_get_cpuidle_driver(void)
{
	return sysfs_cpuidle_get_one_string(CPUIDLE_DRIVER);
}
/* CPUidle idlestate specific /sys/devices/system/cpu/cpuX/cpuidle/ access */

/*
 * Get sched_mc or sched_smt settings
 * Pass "mc" or "smt" as argument
 *
 * Returns negative value on failure
 */
int sysfs_get_sched(const char *smt_mc)
{
	unsigned long value;
	char linebuf[MAX_LINE_LEN];
	char *endp;
	char path[SYSFS_PATH_MAX];

	if (strcmp("mc", smt_mc) && strcmp("smt", smt_mc))
		return -EINVAL;

	snprintf(path, sizeof(path),
		PATH_TO_CPU "sched_%s_power_savings", smt_mc);
	if (sysfs_read_file(path, linebuf, MAX_LINE_LEN) == 0)
		return -1;
	value = strtoul(linebuf, &endp, 0);
	if (endp == linebuf || errno == ERANGE)
		return -1;
	return value;
}

/*
 * Get sched_mc or sched_smt settings
 * Pass "mc" or "smt" as argument
 *
 * Returns negative value on failure
 */
int sysfs_set_sched(const char *smt_mc, int val)
{
	char linebuf[MAX_LINE_LEN];
	char path[SYSFS_PATH_MAX];
	struct stat statbuf;

	if (strcmp("mc", smt_mc) && strcmp("smt", smt_mc))
		return -EINVAL;

	snprintf(path, sizeof(path),
		PATH_TO_CPU "sched_%s_power_savings", smt_mc);
	sprintf(linebuf, "%d", val);

	if (stat(path, &statbuf) != 0)
		return -ENODEV;

	if (sysfs_write_file(path, linebuf, MAX_LINE_LEN) == 0)
		return -1;
	return 0;
}
