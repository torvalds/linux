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
 * Return: 0 on success, non-zero on failure
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

		return errno;
	}

	return 0;
}

/*
 * get_resource_id - Get socket number/l3 id for a specified CPU
 * @cpu_no:	CPU number
 * @resource_id: Socket number or l3_id
 *
 * Return: >= 0 on success, < 0 on failure.
 */
int get_resource_id(int cpu_no, int *resource_id)
{
	char phys_pkg_path[1024];
	FILE *fp;

	if (get_vendor() == ARCH_AMD)
		sprintf(phys_pkg_path, "%s%d/cache/index3/id",
			PHYS_ID_PATH, cpu_no);
	else
		sprintf(phys_pkg_path, "%s%d/topology/physical_package_id",
			PHYS_ID_PATH, cpu_no);

	fp = fopen(phys_pkg_path, "r");
	if (!fp) {
		ksft_perror("Failed to open physical_package_id");

		return -1;
	}
	if (fscanf(fp, "%d", resource_id) <= 0) {
		ksft_perror("Could not get socket number or l3 id");
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
int get_cache_size(int cpu_no, char *cache_type, unsigned long *cache_size)
{
	char cache_path[1024], cache_str[64];
	int length, i, cache_num;
	FILE *fp;

	if (!strcmp(cache_type, "L3")) {
		cache_num = 3;
	} else if (!strcmp(cache_type, "L2")) {
		cache_num = 2;
	} else {
		ksft_print_msg("Invalid cache level\n");
		return -1;
	}

	sprintf(cache_path, "/sys/bus/cpu/devices/cpu%d/cache/index%d/size",
		cpu_no, cache_num);
	fp = fopen(cache_path, "r");
	if (!fp) {
		ksft_perror("Failed to open cache size");

		return -1;
	}
	if (fscanf(fp, "%s", cache_str) <= 0) {
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
 * get_cbm_mask - Get cbm mask for given cache
 * @cache_type:	Cache level L2/L3
 * @cbm_mask:	cbm_mask returned as a string
 *
 * Return: = 0 on success, < 0 on failure.
 */
int get_cbm_mask(char *cache_type, char *cbm_mask)
{
	char cbm_mask_path[1024];
	FILE *fp;

	if (!cbm_mask)
		return -1;

	sprintf(cbm_mask_path, "%s/%s/cbm_mask", INFO_PATH, cache_type);

	fp = fopen(cbm_mask_path, "r");
	if (!fp) {
		ksft_perror("Failed to open cache level");

		return -1;
	}
	if (fscanf(fp, "%s", cbm_mask) <= 0) {
		ksft_perror("Could not get max cbm_mask");
		fclose(fp);

		return -1;
	}
	fclose(fp);

	return 0;
}

/*
 * get_core_sibling - Get sibling core id from the same socket for given CPU
 * @cpu_no:	CPU number
 *
 * Return:	> 0 on success, < 0 on failure.
 */
int get_core_sibling(int cpu_no)
{
	char core_siblings_path[1024], cpu_list_str[64];
	int sibling_cpu_no = -1;
	FILE *fp;

	sprintf(core_siblings_path, "%s%d/topology/core_siblings_list",
		CORE_SIBLINGS_PATH, cpu_no);

	fp = fopen(core_siblings_path, "r");
	if (!fp) {
		ksft_perror("Failed to open core siblings path");

		return -1;
	}
	if (fscanf(fp, "%s", cpu_list_str) <= 0) {
		ksft_perror("Could not get core_siblings list");
		fclose(fp);

		return -1;
	}
	fclose(fp);

	char *token = strtok(cpu_list_str, "-,");

	while (token) {
		sibling_cpu_no = atoi(token);
		/* Skipping core 0 as we don't want to run test on core 0 */
		if (sibling_cpu_no != 0 && sibling_cpu_no != cpu_no)
			break;
		token = strtok(NULL, "-,");
	}

	return sibling_cpu_no;
}

/*
 * taskset_benchmark - Taskset PID (i.e. benchmark) to a specified cpu
 * @bm_pid:	PID that should be binded
 * @cpu_no:	CPU number at which the PID would be binded
 *
 * Return: 0 on success, non-zero on failure
 */
int taskset_benchmark(pid_t bm_pid, int cpu_no)
{
	cpu_set_t my_set;

	CPU_ZERO(&my_set);
	CPU_SET(cpu_no, &my_set);

	if (sched_setaffinity(bm_pid, sizeof(cpu_set_t), &my_set)) {
		ksft_perror("Unable to taskset benchmark");

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
 * Return: 0 on success, non-zero on failure
 */
static int create_grp(const char *grp_name, char *grp, const char *parent_grp)
{
	int found_grp = 0;
	struct dirent *ep;
	DIR *dp;

	/*
	 * At this point, we are guaranteed to have resctrl FS mounted and if
	 * length of grp_name == 0, it means, user wants to use root con_mon
	 * grp, so do nothing
	 */
	if (strlen(grp_name) == 0)
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
	if (fprintf(fp, "%d\n", pid) < 0) {
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
 * @resctrl_val:	Resctrl feature (Eg: mbm, mba.. etc)
 *
 * If a con_mon grp is requested, create it and write pid to it, otherwise
 * write pid to root con_mon grp.
 * If a mon grp is requested, create it and write pid to it, otherwise
 * pid is not written, this means that pid is in con_mon grp and hence
 * should consult con_mon grp's mon_data directory for results.
 *
 * Return: 0 on success, non-zero on failure
 */
int write_bm_pid_to_resctrl(pid_t bm_pid, char *ctrlgrp, char *mongrp,
			    char *resctrl_val)
{
	char controlgroup[128], monitorgroup[512], monitorgroup_p[256];
	char tasks[1024];
	int ret = 0;

	if (strlen(ctrlgrp))
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

	/* Create mon grp and write pid into it for "mbm" and "cmt" test */
	if (!strncmp(resctrl_val, CMT_STR, sizeof(CMT_STR)) ||
	    !strncmp(resctrl_val, MBM_STR, sizeof(MBM_STR))) {
		if (strlen(mongrp)) {
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
 * @resctrl_val:	Resctrl feature (Eg: mbm, mba.. etc)
 *
 * Update schemata of a con_mon grp *only* if requested resctrl feature is
 * allocation type
 *
 * Return: 0 on success, non-zero on failure
 */
int write_schemata(char *ctrlgrp, char *schemata, int cpu_no, char *resctrl_val)
{
	char controlgroup[1024], reason[128], schema[1024] = {};
	int resource_id, fd, schema_len = -1, ret = 0;

	if (strncmp(resctrl_val, MBA_STR, sizeof(MBA_STR)) &&
	    strncmp(resctrl_val, MBM_STR, sizeof(MBM_STR)) &&
	    strncmp(resctrl_val, CAT_STR, sizeof(CAT_STR)) &&
	    strncmp(resctrl_val, CMT_STR, sizeof(CMT_STR)))
		return -ENOENT;

	if (!schemata) {
		ksft_print_msg("Skipping empty schemata update\n");

		return -1;
	}

	if (get_resource_id(cpu_no, &resource_id) < 0) {
		sprintf(reason, "Failed to get resource id");
		ret = -1;

		goto out;
	}

	if (strlen(ctrlgrp) != 0)
		sprintf(controlgroup, "%s/%s/schemata", RESCTRL_PATH, ctrlgrp);
	else
		sprintf(controlgroup, "%s/schemata", RESCTRL_PATH);

	if (!strncmp(resctrl_val, CAT_STR, sizeof(CAT_STR)) ||
	    !strncmp(resctrl_val, CMT_STR, sizeof(CMT_STR)))
		schema_len = snprintf(schema, sizeof(schema), "%s%d%c%s\n",
				      "L3:", resource_id, '=', schemata);
	if (!strncmp(resctrl_val, MBA_STR, sizeof(MBA_STR)) ||
	    !strncmp(resctrl_val, MBM_STR, sizeof(MBM_STR)))
		schema_len = snprintf(schema, sizeof(schema), "%s%d%c%s\n",
				      "MB:", resource_id, '=', schemata);
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
 * validate_resctrl_feature_request - Check if requested feature is valid.
 * @resource:	Required resource (e.g., MB, L3, L2, L3_MON, etc.)
 * @feature:	Required monitor feature (in mon_features file). Can only be
 *		set for L3_MON. Must be NULL for all other resources.
 *
 * Return: True if the resource/feature is supported, else false. False is
 *         also returned if resctrl FS is not mounted.
 */
bool validate_resctrl_feature_request(const char *resource, const char *feature)
{
	char res_path[PATH_MAX];
	struct stat statbuf;
	char *res;
	FILE *inf;
	int ret;

	if (!resource)
		return false;

	ret = find_resctrl_mount(NULL);
	if (ret)
		return false;

	snprintf(res_path, sizeof(res_path), "%s/%s", INFO_PATH, resource);

	if (stat(res_path, &statbuf))
		return false;

	if (!feature)
		return true;

	snprintf(res_path, sizeof(res_path), "%s/%s/mon_features", INFO_PATH, resource);
	inf = fopen(res_path, "r");
	if (!inf)
		return false;

	res = fgrep(inf, feature);
	free(res);
	fclose(inf);

	return !!res;
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

int validate_bw_report_request(char *bw_report)
{
	if (strcmp(bw_report, "reads") == 0)
		return 0;
	if (strcmp(bw_report, "writes") == 0)
		return 0;
	if (strcmp(bw_report, "nt-writes") == 0) {
		strcpy(bw_report, "writes");
		return 0;
	}
	if (strcmp(bw_report, "total") == 0)
		return 0;

	fprintf(stderr, "Requested iMC B/W report type unavailable\n");

	return -1;
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
