// SPDX-License-Identifier: GPL-2.0
/*
 * Basic resctrl file system operations
 *
 * Copyright (C) 2018 Intel Corporation
 *
 * Authors:
 *    Sai Praneeth Prakhya <sai.praneeth.prakhya@intel.com>,
 *    Fenghua Yu <fenghua.yu@intel.com>
 */
#include <fcntl.h>
#include <limits.h>

#include "resctrl.h"

static int find_resctrl_mount(char *buffer)
{
	FILE *mounts;
	char line[256], *fs, *mntpoint;

	mounts = fopen("/proc/mounts", "r");
	if (!mounts) {
		ksft_perror("/proc/mounts");
		return -ENXIO;
	}
	while (!feof(mounts)) {
		if (!fgets(line, 256, mounts))
			break;
		fs = strtok(line, " \t");
		if (!fs)
			continue;
		mntpoint = strtok(NULL, " \t");
		if (!mntpoint)
			continue;
		fs = strtok(NULL, " \t");
		if (!fs)
			continue;
		if (strcmp(fs, "resctrl"))
			continue;

		fclose(mounts);
		if (buffer)
			strncpy(buffer, mntpoint, 256);

		return 0;
	}

	fclose(mounts);

	return -ENOENT;
}

/*
 * mount_resctrlfs - Mount resctrl FS at /sys/fs/resctrl
 *
 * Mounts resctrl FS. Fails if resctrl FS is already mounted to avoid
 * pre-existing settings interfering with the test results.
 *
 * Return: 0 on success, < 0 on error.
 */
int mount_resctrlfs(void)
{
	int ret;

	ret = find_resctrl_mount(NULL);
	if (ret != -ENOENT)
		return -1;

	ksft_print_msg("Mounting resctrl to \"%s\"\n", RESCTRL_PATH);
	ret = mount("resctrl", RESCTRL_PATH, "resctrl", 0, NULL);
	if (ret)
		ksft_perror("mount");

	return ret;
}

int umount_resctrlfs(void)
{
	char mountpoint[256];
	int ret;

	ret = find_resctrl_mount(mountpoint);
	if (ret == -ENOENT)
		return 0;
	if (ret)
		return ret;

	if (umount(mountpoint)) {
		ksft_perror("Unable to umount resctrl");

		return -1;
	}

	return 0;
}

/*
 * get_cache_level - Convert cache level from string to integer
 * @cache_type:		Cache level as string
 *
 * Return: cache level as integer or -1 if @cache_type is invalid.
 */
static int get_cache_level(const char *cache_type)
{
	if (!strcmp(cache_type, "L3"))
		return 3;
	if (!strcmp(cache_type, "L2"))
		return 2;

	ksft_print_msg("Invalid cache level\n");
	return -1;
}

static int get_resource_cache_level(const char *resource)
{
	/* "MB" use L3 (LLC) as resource */
	if (!strcmp(resource, "MB"))
		return 3;
	return get_cache_level(resource);
}

/*
 * get_domain_id - Get resctrl domain ID for a specified CPU
 * @resource:	resource name
 * @cpu_no:	CPU number
 * @domain_id:	domain ID (cache ID; for MB, L3 cache ID)
 *
 * Return: >= 0 on success, < 0 on failure.
 */
int get_domain_id(const char *resource, int cpu_no, int *domain_id)
{
	char phys_pkg_path[1024];
	int cache_num;
	FILE *fp;

	cache_num = get_resource_cache_level(resource);
	if (cache_num < 0)
		return cache_num;

	sprintf(phys_pkg_path, "%s%d/cache/index%d/id", PHYS_ID_PATH, cpu_no, cache_num);

	fp = fopen(phys_pkg_path, "r");
	if (!fp) {
		ksft_perror("Failed to open cache id file");

		return -1;
	}
	if (fscanf(fp, "%d", domain_id) <= 0) {
		ksft_perror("Could not get domain ID");
		fclose(fp);

		return -1;
	}
	fclose(fp);

	return 0;
}

/*
 * get_cache_size - Get cache size for a specified CPU
 * @cpu_no:	CPU number
 * @cache_type:	Cache level L2/L3
 * @cache_size:	pointer to cache_size
 *
 * Return: = 0 on success, < 0 on failure.
 */
int get_cache_size(int cpu_no, const char *cache_type, unsigned long *cache_size)
{
	char cache_path[1024], cache_str[64];
	int length, i, cache_num;
	FILE *fp;

	cache_num = get_cache_level(cache_type);
	if (cache_num < 0)
		return cache_num;

	sprintf(cache_path, "/sys/bus/cpu/devices/cpu%d/cache/index%d/size",
		cpu_no, cache_num);
	fp = fopen(cache_path, "r");
	if (!fp) {
		ksft_perror("Failed to open cache size");

		return -1;
	}
	if (fscanf(fp, "%63s", cache_str) <= 0) {
		ksft_perror("Could not get cache_size");
		fclose(fp);

		return -1;
	}
	fclose(fp);

	length = (int)strlen(cache_str);

	*cache_size = 0;

	for (i = 0; i < length; i++) {
		if ((cache_str[i] >= '0') && (cache_str[i] <= '9'))

			*cache_size = *cache_size * 10 + (cache_str[i] - '0');

		else if (cache_str[i] == 'K')

			*cache_size = *cache_size * 1024;

		else if (cache_str[i] == 'M')

			*cache_size = *cache_size * 1024 * 1024;

		else
			break;
	}

	return 0;
}

#define CORE_SIBLINGS_PATH	"/sys/bus/cpu/devices/cpu"

/*
 * get_bit_mask - Get bit mask from given file
 * @filename:	File containing the mask
 * @mask:	The bit mask returned as unsigned long
 *
 * Return: = 0 on success, < 0 on failure.
 */
static int get_bit_mask(const char *filename, unsigned long *mask)
{
	FILE *fp;

	if (!filename || !mask)
		return -1;

	fp = fopen(filename, "r");
	if (!fp) {
		ksft_print_msg("Failed to open bit mask file '%s': %s\n",
			       filename, strerror(errno));
		return -1;
	}

	if (fscanf(fp, "%lx", mask) <= 0) {
		ksft_print_msg("Could not read bit mask file '%s': %s\n",
			       filename, strerror(errno));
		fclose(fp);

		return -1;
	}
	fclose(fp);

	return 0;
}

/*
 * resource_info_unsigned_get - Read an unsigned value from
 * /sys/fs/resctrl/info/@resource/@filename
 * @resource:	Resource name that matches directory name in
 *		/sys/fs/resctrl/info
 * @filename:	File in /sys/fs/resctrl/info/@resource
 * @val:	Contains read value on success.
 *
 * Return: = 0 on success, < 0 on failure. On success the read
 * value is saved into @val.
 */
int resource_info_unsigned_get(const char *resource, const char *filename,
			       unsigned int *val)
{
	char file_path[PATH_MAX];
	FILE *fp;

	snprintf(file_path, sizeof(file_path), "%s/%s/%s", INFO_PATH, resource,
		 filename);

	fp = fopen(file_path, "r");
	if (!fp) {
		ksft_print_msg("Error opening %s: %m\n", file_path);
		return -1;
	}

	if (fscanf(fp, "%u", val) <= 0) {
		ksft_print_msg("Could not get contents of %s: %m\n", file_path);
		fclose(fp);
		return -1;
	}

	fclose(fp);
	return 0;
}

/*
 * create_bit_mask- Create bit mask from start, len pair
 * @start:	LSB of the mask
 * @len		Number of bits in the mask
 */
unsigned long create_bit_mask(unsigned int start, unsigned int len)
{
	return ((1UL << len) - 1UL) << start;
}

/*
 * count_contiguous_bits - Returns the longest train of bits in a bit mask
 * @val		A bit mask
 * @start	The location of the least-significant bit of the longest train
 *
 * Return:	The length of the contiguous bits in the longest train of bits
 */
unsigned int count_contiguous_bits(unsigned long val, unsigned int *start)
{
	unsigned long last_val;
	unsigned int count = 0;

	while (val) {
		last_val = val;
		val &= (val >> 1);
		count++;
	}

	if (start) {
		if (count)
			*start = ffsl(last_val) - 1;
		else
			*start = 0;
	}

	return count;
}

/*
 * get_full_cbm - Get full Cache Bit Mask (CBM)
 * @cache_type:	Cache type as "L2" or "L3"
 * @mask:	Full cache bit mask representing the maximal portion of cache
 *		available for allocation, returned as unsigned long.
 *
 * Return: = 0 on success, < 0 on failure.
 */
int get_full_cbm(const char *cache_type, unsigned long *mask)
{
	char cbm_path[PATH_MAX];
	int ret;

	if (!cache_type)
		return -1;

	snprintf(cbm_path, sizeof(cbm_path), "%s/%s/cbm_mask",
		 INFO_PATH, cache_type);

	ret = get_bit_mask(cbm_path, mask);
	if (ret || !*mask)
		return -1;

	return 0;
}

/*
 * get_shareable_mask - Get shareable mask from shareable_bits
 * @cache_type:		Cache type as "L2" or "L3"
 * @shareable_mask:	Shareable mask returned as unsigned long
 *
 * Return: = 0 on success, < 0 on failure.
 */
static int get_shareable_mask(const char *cache_type, unsigned long *shareable_mask)
{
	char mask_path[PATH_MAX];

	if (!cache_type)
		return -1;

	snprintf(mask_path, sizeof(mask_path), "%s/%s/shareable_bits",
		 INFO_PATH, cache_type);

	return get_bit_mask(mask_path, shareable_mask);
}

/*
 * get_mask_no_shareable - Get Cache Bit Mask (CBM) without shareable bits
 * @cache_type:		Cache type as "L2" or "L3"
 * @mask:		The largest exclusive portion of the cache out of the
 *			full CBM, returned as unsigned long
 *
 * Parts of a cache may be shared with other devices such as GPU. This function
 * calculates the largest exclusive portion of the cache where no other devices
 * besides CPU have access to the cache portion.
 *
 * Return: = 0 on success, < 0 on failure.
 */
int get_mask_no_shareable(const char *cache_type, unsigned long *mask)
{
	unsigned long full_mask, shareable_mask;
	unsigned int start, len;

	if (get_full_cbm(cache_type, &full_mask) < 0)
		return -1;
	if (get_shareable_mask(cache_type, &shareable_mask) < 0)
		return -1;

	len = count_contiguous_bits(full_mask & ~shareable_mask, &start);
	if (!len)
		return -1;

	*mask = create_bit_mask(start, len);

	return 0;
}

/*
 * taskset_benchmark - Taskset PID (i.e. benchmark) to a specified cpu
 * @bm_pid:		PID that should be binded
 * @cpu_no:		CPU number at which the PID would be binded
 * @old_affinity:	When not NULL, set to old CPU affinity
 *
 * Return: 0 on success, < 0 on error.
 */
int taskset_benchmark(pid_t bm_pid, int cpu_no, cpu_set_t *old_affinity)
{
	cpu_set_t my_set;

	if (old_affinity) {
		CPU_ZERO(old_affinity);
		if (sched_getaffinity(bm_pid, sizeof(*old_affinity),
				      old_affinity)) {
			ksft_perror("Unable to read CPU affinity");
			return -1;
		}
	}

	CPU_ZERO(&my_set);
	CPU_SET(cpu_no, &my_set);

	if (sched_setaffinity(bm_pid, sizeof(cpu_set_t), &my_set)) {
		ksft_perror("Unable to taskset benchmark");

		return -1;
	}

	return 0;
}

/*
 * taskset_restore - Taskset PID to the earlier CPU affinity
 * @bm_pid:		PID that should be reset
 * @old_affinity:	The old CPU affinity to restore
 *
 * Return: 0 on success, < 0 on error.
 */
int taskset_restore(pid_t bm_pid, cpu_set_t *old_affinity)
{
	if (sched_setaffinity(bm_pid, sizeof(*old_affinity), old_affinity)) {
		ksft_perror("Unable to restore CPU affinity");
		return -1;
	}

	return 0;
}

/*
 * create_grp - Create a group only if one doesn't exist
 * @grp_name:	Name of the group
 * @grp:	Full path and name of the group
 * @parent_grp:	Full path and name of the parent group
 *
 * Creates a group @grp_name if it does not exist yet. If @grp_name is NULL,
 * it is interpreted as the root group which always results in success.
 *
 * Return: 0 on success, < 0 on error.
 */
static int create_grp(const char *grp_name, char *grp, const char *parent_grp)
{
	int found_grp = 0;
	struct dirent *ep;
	DIR *dp;

	if (!grp_name)
		return 0;

	/* Check if requested grp exists or not */
	dp = opendir(parent_grp);
	if (dp) {
		while ((ep = readdir(dp)) != NULL) {
			if (strcmp(ep->d_name, grp_name) == 0)
				found_grp = 1;
		}
		closedir(dp);
	} else {
		ksft_perror("Unable to open resctrl for group");

		return -1;
	}

	/* Requested grp doesn't exist, hence create it */
	if (found_grp == 0) {
		if (mkdir(grp, 0) == -1) {
			ksft_perror("Unable to create group");

			return -1;
		}
	}

	return 0;
}

static int write_pid_to_tasks(char *tasks, pid_t pid)
{
	FILE *fp;

	fp = fopen(tasks, "w");
	if (!fp) {
		ksft_perror("Failed to open tasks file");

		return -1;
	}
	if (fprintf(fp, "%d\n", (int)pid) < 0) {
		ksft_print_msg("Failed to write pid to tasks file\n");
		fclose(fp);

		return -1;
	}
	fclose(fp);

	return 0;
}

/*
 * write_bm_pid_to_resctrl - Write a PID (i.e. benchmark) to resctrl FS
 * @bm_pid:		PID that should be written
 * @ctrlgrp:		Name of the control monitor group (con_mon grp)
 * @mongrp:		Name of the monitor group (mon grp)
 *
 * If a con_mon grp is requested, create it and write pid to it, otherwise
 * write pid to root con_mon grp.
 * If a mon grp is requested, create it and write pid to it, otherwise
 * pid is not written, this means that pid is in con_mon grp and hence
 * should consult con_mon grp's mon_data directory for results.
 *
 * Return: 0 on success, < 0 on error.
 */
int write_bm_pid_to_resctrl(pid_t bm_pid, const char *ctrlgrp, const char *mongrp)
{
	char controlgroup[128], monitorgroup[512], monitorgroup_p[256];
	char tasks[1024];
	int ret = 0;

	if (ctrlgrp)
		sprintf(controlgroup, "%s/%s", RESCTRL_PATH, ctrlgrp);
	else
		sprintf(controlgroup, "%s", RESCTRL_PATH);

	/* Create control and monitoring group and write pid into it */
	ret = create_grp(ctrlgrp, controlgroup, RESCTRL_PATH);
	if (ret)
		goto out;
	sprintf(tasks, "%s/tasks", controlgroup);
	ret = write_pid_to_tasks(tasks, bm_pid);
	if (ret)
		goto out;

	/* Create monitor group and write pid into if it is used */
	if (mongrp) {
		sprintf(monitorgroup_p, "%s/mon_groups", controlgroup);
		sprintf(monitorgroup, "%s/%s", monitorgroup_p, mongrp);
		ret = create_grp(mongrp, monitorgroup, monitorgroup_p);
		if (ret)
			goto out;

		sprintf(tasks, "%s/mon_groups/%s/tasks",
			controlgroup, mongrp);
		ret = write_pid_to_tasks(tasks, bm_pid);
		if (ret)
			goto out;
	}

out:
	ksft_print_msg("Writing benchmark parameters to resctrl FS\n");
	if (ret)
		ksft_print_msg("Failed writing to resctrlfs\n");

	return ret;
}

/*
 * write_schemata - Update schemata of a con_mon grp
 * @ctrlgrp:		Name of the con_mon grp
 * @schemata:		Schemata that should be updated to
 * @cpu_no:		CPU number that the benchmark PID is binded to
 * @resource:		Resctrl resource (Eg: MB, L3, L2, etc.)
 *
 * Update schemata of a con_mon grp *only* if requested resctrl resource is
 * allocation type
 *
 * Return: 0 on success, < 0 on error.
 */
int write_schemata(const char *ctrlgrp, char *schemata, int cpu_no,
		   const char *resource)
{
	char controlgroup[1024], reason[128], schema[1024] = {};
	int domain_id, fd, schema_len, ret = 0;

	if (!schemata) {
		ksft_print_msg("Skipping empty schemata update\n");

		return -1;
	}

	if (get_domain_id(resource, cpu_no, &domain_id) < 0) {
		sprintf(reason, "Failed to get domain ID");
		ret = -1;

		goto out;
	}

	if (ctrlgrp)
		sprintf(controlgroup, "%s/%s/schemata", RESCTRL_PATH, ctrlgrp);
	else
		sprintf(controlgroup, "%s/schemata", RESCTRL_PATH);

	schema_len = snprintf(schema, sizeof(schema), "%s:%d=%s\n",
			      resource, domain_id, schemata);
	if (schema_len < 0 || schema_len >= sizeof(schema)) {
		snprintf(reason, sizeof(reason),
			 "snprintf() failed with return value : %d", schema_len);
		ret = -1;
		goto out;
	}

	fd = open(controlgroup, O_WRONLY);
	if (fd < 0) {
		snprintf(reason, sizeof(reason),
			 "open() failed : %s", strerror(errno));
		ret = -1;

		goto err_schema_not_empty;
	}
	if (write(fd, schema, schema_len) < 0) {
		snprintf(reason, sizeof(reason),
			 "write() failed : %s", strerror(errno));
		close(fd);
		ret = -1;

		goto err_schema_not_empty;
	}
	close(fd);

err_schema_not_empty:
	schema[schema_len - 1] = 0;
out:
	ksft_print_msg("Write schema \"%s\" to resctrl FS%s%s\n",
		       schema, ret ? " # " : "",
		       ret ? reason : "");

	return ret;
}

bool check_resctrlfs_support(void)
{
	FILE *inf = fopen("/proc/filesystems", "r");
	DIR *dp;
	char *res;
	bool ret = false;

	if (!inf)
		return false;

	res = fgrep(inf, "nodev\tresctrl\n");

	if (res) {
		ret = true;
		free(res);
	}

	fclose(inf);

	ksft_print_msg("%s Check kernel supports resctrl filesystem\n",
		       ret ? "Pass:" : "Fail:");

	if (!ret)
		return ret;

	dp = opendir(RESCTRL_PATH);
	ksft_print_msg("%s Check resctrl mountpoint \"%s\" exists\n",
		       dp ? "Pass:" : "Fail:", RESCTRL_PATH);
	if (dp)
		closedir(dp);

	ksft_print_msg("resctrl filesystem %s mounted\n",
		       find_resctrl_mount(NULL) ? "not" : "is");

	return ret;
}

char *fgrep(FILE *inf, const char *str)
{
	char line[256];
	int slen = strlen(str);

	while (!feof(inf)) {
		if (!fgets(line, 256, inf))
			break;
		if (strncmp(line, str, slen))
			continue;

		return strdup(line);
	}

	return NULL;
}

/*
 * resctrl_resource_exists - Check if a resource is supported.
 * @resource:	Resctrl resource (e.g., MB, L3, L2, L3_MON, etc.)
 *
 * Return: True if the resource is supported, else false. False is
 *         also returned if resctrl FS is not mounted.
 */
bool resctrl_resource_exists(const char *resource)
{
	char res_path[PATH_MAX];
	struct stat statbuf;
	int ret;

	if (!resource)
		return false;

	ret = find_resctrl_mount(NULL);
	if (ret)
		return false;

	snprintf(res_path, sizeof(res_path), "%s/%s", INFO_PATH, resource);

	if (stat(res_path, &statbuf))
		return false;

	return true;
}

/*
 * resctrl_mon_feature_exists - Check if requested monitoring feature is valid.
 * @resource:	Resource that uses the mon_features file. Currently only L3_MON
 *		is valid.
 * @feature:	Required monitor feature (in mon_features file).
 *
 * Return: True if the feature is supported, else false.
 */
bool resctrl_mon_feature_exists(const char *resource, const char *feature)
{
	char res_path[PATH_MAX];
	char *res;
	FILE *inf;

	if (!feature || !resource)
		return false;

	snprintf(res_path, sizeof(res_path), "%s/%s/mon_features", INFO_PATH, resource);
	inf = fopen(res_path, "r");
	if (!inf)
		return false;

	res = fgrep(inf, feature);
	free(res);
	fclose(inf);

	return !!res;
}

/*
 * resource_info_file_exists - Check if a file is present inside
 * /sys/fs/resctrl/info/@resource.
 * @resource:	Required resource (Eg: MB, L3, L2, etc.)
 * @file:	Required file.
 *
 * Return: True if the /sys/fs/resctrl/info/@resource/@file exists, else false.
 */
bool resource_info_file_exists(const char *resource, const char *file)
{
	char res_path[PATH_MAX];
	struct stat statbuf;

	if (!file || !resource)
		return false;

	snprintf(res_path, sizeof(res_path), "%s/%s/%s", INFO_PATH, resource,
		 file);

	if (stat(res_path, &statbuf))
		return false;

	return true;
}

bool test_resource_feature_check(const struct resctrl_test *test)
{
	return resctrl_resource_exists(test->resource);
}

int filter_dmesg(void)
{
	char line[1024];
	FILE *fp;
	int pipefds[2];
	pid_t pid;
	int ret;

	ret = pipe(pipefds);
	if (ret) {
		ksft_perror("pipe");
		return ret;
	}
	fflush(stdout);
	pid = fork();
	if (pid == 0) {
		close(pipefds[0]);
		dup2(pipefds[1], STDOUT_FILENO);
		execlp("dmesg", "dmesg", NULL);
		ksft_perror("Executing dmesg");
		exit(1);
	}
	close(pipefds[1]);
	fp = fdopen(pipefds[0], "r");
	if (!fp) {
		ksft_perror("fdopen(pipe)");
		kill(pid, SIGTERM);

		return -1;
	}

	while (fgets(line, 1024, fp)) {
		if (strstr(line, "intel_rdt:"))
			ksft_print_msg("dmesg: %s", line);
		if (strstr(line, "resctrl:"))
			ksft_print_msg("dmesg: %s", line);
	}
	fclose(fp);
	waitpid(pid, NULL, 0);

	return 0;
}

const char *get_bw_report_type(const char *bw_report)
{
	if (strcmp(bw_report, "reads") == 0)
		return bw_report;
	if (strcmp(bw_report, "writes") == 0)
		return bw_report;
	if (strcmp(bw_report, "nt-writes") == 0) {
		return "writes";
	}
	if (strcmp(bw_report, "total") == 0)
		return bw_report;

	fprintf(stderr, "Requested iMC bandwidth report type unavailable\n");

	return NULL;
}

int perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu,
		    int group_fd, unsigned long flags)
{
	int ret;

	ret = syscall(__NR_perf_event_open, hw_event, pid, cpu,
		      group_fd, flags);
	return ret;
}

unsigned int count_bits(unsigned long n)
{
	unsigned int count = 0;

	while (n) {
		count += n & 1;
		n >>= 1;
	}

	return count;
}
