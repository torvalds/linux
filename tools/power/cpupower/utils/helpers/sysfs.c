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

/*
 * Detect whether a CPU is online
 *
 * Returns:
 *     1 -> if CPU is online
 *     0 -> if CPU is offline
 *     negative errno values in error case
 */
int sysfs_is_cpu_online(unsigned int cpu)
{
	char path[SYSFS_PATH_MAX];
	int fd;
	ssize_t numread;
	unsigned long long value;
	char linebuf[MAX_LINE_LEN];
	char *endp;
	struct stat statbuf;

	snprintf(path, sizeof(path), PATH_TO_CPU "cpu%u", cpu);

	if (stat(path, &statbuf) != 0)
		return 0;

	/*
	 * kernel without CONFIG_HOTPLUG_CPU
	 * -> cpuX directory exists, but not cpuX/online file
	 */
	snprintf(path, sizeof(path), PATH_TO_CPU "cpu%u/online", cpu);
	if (stat(path, &statbuf) != 0)
		return 1;

	fd = open(path, O_RDONLY);
	if (fd == -1)
		return -errno;

	numread = read(fd, linebuf, MAX_LINE_LEN - 1);
	if (numread < 1) {
		close(fd);
		return -EIO;
	}
	linebuf[numread] = '\0';
	close(fd);

	value = strtoull(linebuf, &endp, 0);
	if (value > 1 || value < 0)
		return -EINVAL;

	return value;
}

/* CPUidle idlestate specific /sys/devices/system/cpu/cpuX/cpuidle/ access */


/* CPUidle idlestate specific /sys/devices/system/cpu/cpuX/cpuidle/ access */

/*
 * helper function to check whether a file under "../cpuX/cpuidle/stateX/" dir
 * exists.
 * For example the functionality to disable c-states was introduced in later
 * kernel versions, this function can be used to explicitly check for this
 * feature.
 *
 * returns 1 if the file exists, 0 otherwise.
 */
unsigned int sysfs_idlestate_file_exists(unsigned int cpu,
					 unsigned int idlestate,
					 const char *fname)
{
	char path[SYSFS_PATH_MAX];
	struct stat statbuf;


	snprintf(path, sizeof(path), PATH_TO_CPU "cpu%u/cpuidle/state%u/%s",
		 cpu, idlestate, fname);
	if (stat(path, &statbuf) != 0)
		return 0;
	return 1;
}

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

/* 
 * helper function to write a new value to a /sys file
 * fname is a relative path under "../cpuX/cpuidle/cstateY/" dir
 *
 * Returns the number of bytes written or 0 on error
 */
static
unsigned int sysfs_idlestate_write_file(unsigned int cpu,
					unsigned int idlestate,
					const char *fname,
					const char *value, size_t len)
{
	char path[SYSFS_PATH_MAX];
	int fd;
	ssize_t numwrite;

	snprintf(path, sizeof(path), PATH_TO_CPU "cpu%u/cpuidle/state%u/%s",
		 cpu, idlestate, fname);

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

enum idlestate_value {
	IDLESTATE_USAGE,
	IDLESTATE_POWER,
	IDLESTATE_LATENCY,
	IDLESTATE_TIME,
	IDLESTATE_DISABLE,
	MAX_IDLESTATE_VALUE_FILES
};

static const char *idlestate_value_files[MAX_IDLESTATE_VALUE_FILES] = {
	[IDLESTATE_USAGE] = "usage",
	[IDLESTATE_POWER] = "power",
	[IDLESTATE_LATENCY] = "latency",
	[IDLESTATE_TIME]  = "time",
	[IDLESTATE_DISABLE]  = "disable",
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

/*
 * Returns:
 *    1  if disabled
 *    0  if enabled
 *    -1 if idlestate is not available
 *    -2 if disabling is not supported by the kernel
 */
int sysfs_is_idlestate_disabled(unsigned int cpu,
				unsigned int idlestate)
{
	if (sysfs_get_idlestate_count(cpu) < idlestate)
		return -1;

	if (!sysfs_idlestate_file_exists(cpu, idlestate,
				 idlestate_value_files[IDLESTATE_DISABLE]))
		return -2;
	return sysfs_idlestate_get_one_value(cpu, idlestate, IDLESTATE_DISABLE);
}

/*
 * Pass 1 as last argument to disable or 0 to enable the state
 * Returns:
 *    0  on success
 *    negative values on error, for example:
 *      -1 if idlestate is not available
 *      -2 if disabling is not supported by the kernel
 *      -3 No write access to disable/enable C-states
 */
int sysfs_idlestate_disable(unsigned int cpu,
			    unsigned int idlestate,
			    unsigned int disable)
{
	char value[SYSFS_PATH_MAX];
	int bytes_written;

	if (sysfs_get_idlestate_count(cpu) < idlestate)
		return -1;

	if (!sysfs_idlestate_file_exists(cpu, idlestate,
				 idlestate_value_files[IDLESTATE_DISABLE]))
		return -2;

	snprintf(value, SYSFS_PATH_MAX, "%u", disable);

	bytes_written = sysfs_idlestate_write_file(cpu, idlestate, "disable",
						   value, sizeof(disable));
	if (bytes_written)
		return 0;
	return -3;
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
unsigned int sysfs_get_idlestate_count(unsigned int cpu)
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
	return -ENODEV;
}

/*
 * Get sched_mc or sched_smt settings
 * Pass "mc" or "smt" as argument
 *
 * Returns negative value on failure
 */
int sysfs_set_sched(const char *smt_mc, int val)
{
	return -ENODEV;
}
