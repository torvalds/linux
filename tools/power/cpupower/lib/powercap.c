// SPDX-License-Identifier: GPL-2.0-only
/*
 *  (C) 2016 SUSE Software Solutions GmbH
 *           Thomas Renninger <trenn@suse.de>
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <dirent.h>

#include "powercap.h"

static unsigned int sysfs_read_file(const char *path, char *buf, size_t buflen)
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

static int sysfs_get_enabled(char *path, int *mode)
{
	int fd;
	char yes_no;
	int ret = 0;

	*mode = 0;

	fd = open(path, O_RDONLY);
	if (fd == -1) {
		ret = -1;
		goto out;
	}

	if (read(fd, &yes_no, 1) != 1) {
		ret = -1;
		goto out_close;
	}

	if (yes_no == '1') {
		*mode = 1;
		goto out_close;
	} else if (yes_no == '0') {
		goto out_close;
	} else {
		ret = -1;
		goto out_close;
	}
out_close:
	close(fd);
out:
	return ret;
}

int powercap_get_enabled(int *mode)
{
	char path[SYSFS_PATH_MAX] = PATH_TO_POWERCAP "/intel-rapl/enabled";

	return sysfs_get_enabled(path, mode);
}

/*
 * Hardcoded, because rapl is the only powercap implementation
- * this needs to get more generic if more powercap implementations
 * should show up
 */
int powercap_get_driver(char *driver, int buflen)
{
	char file[SYSFS_PATH_MAX] = PATH_TO_RAPL;

	struct stat statbuf;

	if (stat(file, &statbuf) != 0 || !S_ISDIR(statbuf.st_mode)) {
		driver = "";
		return -1;
	} else if (buflen > 10) {
		strcpy(driver, "intel-rapl");
		return 0;
	} else
		return -1;
}

enum powercap_get64 {
	GET_ENERGY_UJ,
	GET_MAX_ENERGY_RANGE_UJ,
	GET_POWER_UW,
	GET_MAX_POWER_RANGE_UW,
	MAX_GET_64_FILES
};

static const char *powercap_get64_files[MAX_GET_64_FILES] = {
	[GET_POWER_UW] = "power_uw",
	[GET_MAX_POWER_RANGE_UW] = "max_power_range_uw",
	[GET_ENERGY_UJ] = "energy_uj",
	[GET_MAX_ENERGY_RANGE_UJ] = "max_energy_range_uj",
};

static int sysfs_powercap_get64_val(struct powercap_zone *zone,
				      enum powercap_get64 which,
				      uint64_t *val)
{
	char file[SYSFS_PATH_MAX] = PATH_TO_POWERCAP "/";
	int ret;
	char buf[MAX_LINE_LEN];

	strcat(file, zone->sys_name);
	strcat(file, "/");
	strcat(file, powercap_get64_files[which]);

	ret = sysfs_read_file(file, buf, MAX_LINE_LEN);
	if (ret < 0)
		return ret;
	if (ret == 0)
		return -1;

	*val = strtoll(buf, NULL, 10);
	return 0;
}

int powercap_get_max_energy_range_uj(struct powercap_zone *zone, uint64_t *val)
{
	return sysfs_powercap_get64_val(zone, GET_MAX_ENERGY_RANGE_UJ, val);
}

int powercap_get_energy_uj(struct powercap_zone *zone, uint64_t *val)
{
	return sysfs_powercap_get64_val(zone, GET_ENERGY_UJ, val);
}

int powercap_get_max_power_range_uw(struct powercap_zone *zone, uint64_t *val)
{
	return sysfs_powercap_get64_val(zone, GET_MAX_POWER_RANGE_UW, val);
}

int powercap_get_power_uw(struct powercap_zone *zone, uint64_t *val)
{
	return sysfs_powercap_get64_val(zone, GET_POWER_UW, val);
}

int powercap_zone_get_enabled(struct powercap_zone *zone, int *mode)
{
	char path[SYSFS_PATH_MAX] = PATH_TO_POWERCAP;

	if ((strlen(PATH_TO_POWERCAP) + strlen(zone->sys_name)) +
	    strlen("/enabled") + 1 >= SYSFS_PATH_MAX)
		return -1;

	strcat(path, "/");
	strcat(path, zone->sys_name);
	strcat(path, "/enabled");

	return sysfs_get_enabled(path, mode);
}

int powercap_zone_set_enabled(struct powercap_zone *zone, int mode)
{
	/* To be done if needed */
	return 0;
}


int powercap_read_zone(struct powercap_zone *zone)
{
	struct dirent *dent;
	DIR *zone_dir;
	char sysfs_dir[SYSFS_PATH_MAX] = PATH_TO_POWERCAP;
	struct powercap_zone *child_zone;
	char file[SYSFS_PATH_MAX] = PATH_TO_POWERCAP;
	int i, ret = 0;
	uint64_t val = 0;

	strcat(sysfs_dir, "/");
	strcat(sysfs_dir, zone->sys_name);

	zone_dir = opendir(sysfs_dir);
	if (zone_dir == NULL)
		return -1;

	strcat(file, "/");
	strcat(file, zone->sys_name);
	strcat(file, "/name");
	sysfs_read_file(file, zone->name, MAX_LINE_LEN);
	if (zone->parent)
		zone->tree_depth = zone->parent->tree_depth + 1;
	ret = powercap_get_energy_uj(zone, &val);
	if (ret == 0)
		zone->has_energy_uj = 1;
	ret = powercap_get_power_uw(zone, &val);
	if (ret == 0)
		zone->has_power_uw = 1;

	while ((dent = readdir(zone_dir)) != NULL) {
		struct stat st;

		if (strcmp(dent->d_name, ".") == 0 || strcmp(dent->d_name, "..") == 0)
			continue;

		if (stat(dent->d_name, &st) != 0 || !S_ISDIR(st.st_mode))
			if (fstatat(dirfd(zone_dir), dent->d_name, &st, 0) < 0)
				continue;

		if (strncmp(dent->d_name, "intel-rapl:", 11) != 0)
			continue;

		child_zone = calloc(1, sizeof(struct powercap_zone));
		if (child_zone == NULL)
			return -1;
		for (i = 0; i < POWERCAP_MAX_CHILD_ZONES; i++) {
			if (zone->children[i] == NULL) {
				zone->children[i] = child_zone;
				break;
			}
			if (i == POWERCAP_MAX_CHILD_ZONES - 1) {
				free(child_zone);
				fprintf(stderr, "Reached POWERCAP_MAX_CHILD_ZONES %d\n",
				       POWERCAP_MAX_CHILD_ZONES);
				return -1;
			}
		}
		strcpy(child_zone->sys_name, zone->sys_name);
		strcat(child_zone->sys_name, "/");
		strcat(child_zone->sys_name, dent->d_name);
		child_zone->parent = zone;
		if (zone->tree_depth >= POWERCAP_MAX_TREE_DEPTH) {
			fprintf(stderr, "Maximum zone hierarchy depth[%d] reached\n",
				POWERCAP_MAX_TREE_DEPTH);
			ret = -1;
			break;
		}
		powercap_read_zone(child_zone);
	}
	closedir(zone_dir);
	return ret;
}

struct powercap_zone *powercap_init_zones(void)
{
	int enabled;
	struct powercap_zone *root_zone;
	int ret;
	char file[SYSFS_PATH_MAX] = PATH_TO_RAPL "/enabled";

	ret = sysfs_get_enabled(file, &enabled);

	if (ret)
		return NULL;

	if (!enabled)
		return NULL;

	root_zone = calloc(1, sizeof(struct powercap_zone));
	if (!root_zone)
		return NULL;

	strcpy(root_zone->sys_name, "intel-rapl/intel-rapl:0");

	powercap_read_zone(root_zone);

	return root_zone;
}

/* Call function *f on the passed zone and all its children */

int powercap_walk_zones(struct powercap_zone *zone,
			int (*f)(struct powercap_zone *zone))
{
	int i, ret;

	if (!zone)
		return -1;

	ret = f(zone);
	if (ret)
		return ret;

	for (i = 0; i < POWERCAP_MAX_CHILD_ZONES; i++) {
		if (zone->children[i] != NULL)
			powercap_walk_zones(zone->children[i], f);
	}
	return 0;
}
