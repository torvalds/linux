// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * sysfs.c sysfs ABI access functions for TMON program
 *
 * Copyright (C) 2013 Intel Corporation. All rights reserved.
 *
 * Author: Jacob Pan <jacob.jun.pan@linux.intel.com>
 */
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <libintl.h>
#include <ctype.h>
#include <time.h>
#include <syslog.h>
#include <sys/time.h>
#include <errno.h>

#include "tmon.h"

struct tmon_platform_data ptdata;
const char *trip_type_name[] = {
	"critical",
	"hot",
	"passive",
	"active",
};

int sysfs_set_ulong(char *path, char *filename, unsigned long val)
{
	FILE *fd;
	int ret = -1;
	char filepath[256];

	snprintf(filepath, 256, "%s/%s", path, filename);

	fd = fopen(filepath, "w");
	if (!fd) {
		syslog(LOG_ERR, "Err: open %s: %s\n", __func__, filepath);
		return ret;
	}
	ret = fprintf(fd, "%lu", val);
	fclose(fd);

	return 0;
}

/* history of thermal data, used for control algo */
#define NR_THERMAL_RECORDS 3
struct thermal_data_record trec[NR_THERMAL_RECORDS];
int cur_thermal_record; /* index to the trec array */

static int sysfs_get_ulong(char *path, char *filename, unsigned long *p_ulong)
{
	FILE *fd;
	int ret = -1;
	char filepath[256];

	snprintf(filepath, 256, "%s/%s", path, filename);

	fd = fopen(filepath, "r");
	if (!fd) {
		syslog(LOG_ERR, "Err: open %s: %s\n", __func__, filepath);
		return ret;
	}
	ret = fscanf(fd, "%lu", p_ulong);
	fclose(fd);

	return 0;
}

static int sysfs_get_string(char *path, char *filename, char *str)
{
	FILE *fd;
	int ret = -1;
	char filepath[256];

	snprintf(filepath, 256, "%s/%s", path, filename);

	fd = fopen(filepath, "r");
	if (!fd) {
		syslog(LOG_ERR, "Err: open %s: %s\n", __func__, filepath);
		return ret;
	}
	ret = fscanf(fd, "%256s", str);
	fclose(fd);

	return ret;
}

/* get states of the cooling device instance */
static int probe_cdev(struct cdev_info *cdi, char *path)
{
	sysfs_get_string(path, "type", cdi->type);
	sysfs_get_ulong(path, "max_state",  &cdi->max_state);
	sysfs_get_ulong(path, "cur_state", &cdi->cur_state);

	syslog(LOG_INFO, "%s: %s: type %s, max %lu, curr %lu inst %d\n",
		__func__, path,
		cdi->type, cdi->max_state, cdi->cur_state, cdi->instance);

	return 0;
}

static int str_to_trip_type(char *name)
{
	int i;

	for (i = 0; i < NR_THERMAL_TRIP_TYPE; i++) {
		if (!strcmp(name, trip_type_name[i]))
			return i;
	}

	return -ENOENT;
}

/* scan and fill in trip point info for a thermal zone and trip point id */
static int get_trip_point_data(char *tz_path, int tzid, int tpid)
{
	char filename[256];
	char temp_str[256];
	int trip_type;

	if (tpid >= MAX_NR_TRIP)
		return -EINVAL;
	/* check trip point type */
	snprintf(filename, sizeof(filename), "trip_point_%d_type", tpid);
	sysfs_get_string(tz_path, filename, temp_str);
	trip_type = str_to_trip_type(temp_str);
	if (trip_type < 0) {
		syslog(LOG_ERR, "%s:%s no matching type\n", __func__, temp_str);
		return -ENOENT;
	}
	ptdata.tzi[tzid].tp[tpid].type = trip_type;
	syslog(LOG_INFO, "%s:tz:%d tp:%d:type:%s type id %d\n", __func__, tzid,
		tpid, temp_str, trip_type);

	/* TODO: check attribute */

	return 0;
}

/* return instance id for file format such as trip_point_4_temp */
static int get_instance_id(char *name, int pos, int skip)
{
	char *ch;
	int i = 0;

	ch = strtok(name, "_");
	while (ch != NULL) {
		++i;
		syslog(LOG_INFO, "%s:%s:%s:%d", __func__, name, ch, i);
		ch = strtok(NULL, "_");
		if (pos == i)
			return atol(ch + skip);
	}

	return -1;
}

/* Find trip point info of a thermal zone */
static int find_tzone_tp(char *tz_name, char *d_name, struct tz_info *tzi,
			int tz_id)
{
	int tp_id;
	unsigned long temp_ulong;

	if (strstr(d_name, "trip_point") &&
		strstr(d_name, "temp")) {
		/* check if trip point temp is non-zero
		 * ignore 0/invalid trip points
		 */
		sysfs_get_ulong(tz_name, d_name, &temp_ulong);
		if (temp_ulong < MAX_TEMP_KC) {
			tzi->nr_trip_pts++;
			/* found a valid trip point */
			tp_id = get_instance_id(d_name, 2, 0);
			syslog(LOG_DEBUG, "tzone %s trip %d temp %lu tpnode %s",
				tz_name, tp_id, temp_ulong, d_name);
			if (tp_id < 0 || tp_id >= MAX_NR_TRIP) {
				syslog(LOG_ERR, "Failed to find TP inst %s\n",
					d_name);
				return -1;
			}
			get_trip_point_data(tz_name, tz_id, tp_id);
			tzi->tp[tp_id].temp = temp_ulong;
		}
	}

	return 0;
}

/* check cooling devices for binding info. */
static int find_tzone_cdev(struct dirent *nl, char *tz_name,
			struct tz_info *tzi, int tz_id, int cid)
{
	unsigned long trip_instance = 0;
	char cdev_name_linked[256];
	char cdev_name[256];
	char cdev_trip_name[256];
	int cdev_id;

	if (nl->d_type == DT_LNK) {
		syslog(LOG_DEBUG, "TZ%d: cdev: %s cid %d\n", tz_id, nl->d_name,
			cid);
		tzi->nr_cdev++;
		if (tzi->nr_cdev > ptdata.nr_cooling_dev) {
			syslog(LOG_ERR, "Err: Too many cdev? %d\n",
				tzi->nr_cdev);
			return -EINVAL;
		}
		/* find the link to real cooling device record binding */
		snprintf(cdev_name, 256, "%s/%s", tz_name, nl->d_name);
		memset(cdev_name_linked, 0, sizeof(cdev_name_linked));
		if (readlink(cdev_name, cdev_name_linked,
				sizeof(cdev_name_linked) - 1) != -1) {
			cdev_id = get_instance_id(cdev_name_linked, 1,
						sizeof("device") - 1);
			syslog(LOG_DEBUG, "cdev %s linked to %s : %d\n",
				cdev_name, cdev_name_linked, cdev_id);
			tzi->cdev_binding |= (1 << cdev_id);

			/* find the trip point in which the cdev is binded to
			 * in this tzone
			 */
			snprintf(cdev_trip_name, 256, "%s%s", nl->d_name,
				"_trip_point");
			sysfs_get_ulong(tz_name, cdev_trip_name,
					&trip_instance);
			/* validate trip point range, e.g. trip could return -1
			 * when passive is enabled
			 */
			if (trip_instance > MAX_NR_TRIP)
				trip_instance = 0;
			tzi->trip_binding[cdev_id] |= 1 << trip_instance;
			syslog(LOG_DEBUG, "cdev %s -> trip:%lu: 0x%lx %d\n",
				cdev_name, trip_instance,
				tzi->trip_binding[cdev_id],
				cdev_id);


		}
		return 0;
	}

	return -ENODEV;
}



/*****************************************************************************
 * Before calling scan_tzones, thermal sysfs must be probed to determine
 * the number of thermal zones and cooling devices.
 * We loop through each thermal zone and fill in tz_info struct, i.e.
 * ptdata.tzi[]
root@jacob-chiefriver:~# tree -d /sys/class/thermal/thermal_zone0
/sys/class/thermal/thermal_zone0
|-- cdev0 -> ../cooling_device4
|-- cdev1 -> ../cooling_device3
|-- cdev10 -> ../cooling_device7
|-- cdev11 -> ../cooling_device6
|-- cdev12 -> ../cooling_device5
|-- cdev2 -> ../cooling_device2
|-- cdev3 -> ../cooling_device1
|-- cdev4 -> ../cooling_device0
|-- cdev5 -> ../cooling_device12
|-- cdev6 -> ../cooling_device11
|-- cdev7 -> ../cooling_device10
|-- cdev8 -> ../cooling_device9
|-- cdev9 -> ../cooling_device8
|-- device -> ../../../LNXSYSTM:00/device:62/LNXTHERM:00
|-- power
`-- subsystem -> ../../../../class/thermal
*****************************************************************************/
static int scan_tzones(void)
{
	DIR *dir;
	struct dirent **namelist;
	char tz_name[256];
	int i, j, n, k = 0;

	if (!ptdata.nr_tz_sensor)
		return -1;

	for (i = 0; i <= ptdata.max_tz_instance; i++) {
		memset(tz_name, 0, sizeof(tz_name));
		snprintf(tz_name, 256, "%s/%s%d", THERMAL_SYSFS, TZONE, i);

		dir = opendir(tz_name);
		if (!dir) {
			syslog(LOG_INFO, "Thermal zone %s skipped\n", tz_name);
			continue;
		}
		/* keep track of valid tzones */
		n = scandir(tz_name, &namelist, 0, alphasort);
		if (n < 0)
			syslog(LOG_ERR, "scandir failed in %s",  tz_name);
		else {
			sysfs_get_string(tz_name, "type", ptdata.tzi[k].type);
			ptdata.tzi[k].instance = i;
			/* detect trip points and cdev attached to this tzone */
			j = 0; /* index for cdev */
			ptdata.tzi[k].nr_cdev = 0;
			ptdata.tzi[k].nr_trip_pts = 0;
			while (n--) {
				char *temp_str;

				if (find_tzone_tp(tz_name, namelist[n]->d_name,
							&ptdata.tzi[k], k))
					break;
				temp_str = strstr(namelist[n]->d_name, "cdev");
				if (!temp_str) {
					free(namelist[n]);
					continue;
				}
				if (!find_tzone_cdev(namelist[n], tz_name,
							&ptdata.tzi[k], i, j))
					j++; /* increment cdev index */
				free(namelist[n]);
			}
			free(namelist);
		}
		/*TODO: reverse trip points */
		closedir(dir);
		syslog(LOG_INFO, "TZ %d has %d cdev\n",	i,
			ptdata.tzi[k].nr_cdev);
		k++;
	}

	return 0;
}

static int scan_cdevs(void)
{
	DIR *dir;
	struct dirent **namelist;
	char cdev_name[256];
	int i, n, k = 0;

	if (!ptdata.nr_cooling_dev) {
		fprintf(stderr, "No cooling devices found\n");
		return 0;
	}
	for (i = 0; i <= ptdata.max_cdev_instance; i++) {
		memset(cdev_name, 0, sizeof(cdev_name));
		snprintf(cdev_name, 256, "%s/%s%d", THERMAL_SYSFS, CDEV, i);

		dir = opendir(cdev_name);
		if (!dir) {
			syslog(LOG_INFO, "Cooling dev %s skipped\n", cdev_name);
			/* there is a gap in cooling device id, check again
			 * for the same index.
			 */
			continue;
		}

		n = scandir(cdev_name, &namelist, 0, alphasort);
		if (n < 0)
			syslog(LOG_ERR, "scandir failed in %s",  cdev_name);
		else {
			sysfs_get_string(cdev_name, "type", ptdata.cdi[k].type);
			ptdata.cdi[k].instance = i;
			if (strstr(ptdata.cdi[k].type, ctrl_cdev)) {
				ptdata.cdi[k].flag |= CDEV_FLAG_IN_CONTROL;
				syslog(LOG_DEBUG, "control cdev id %d\n", i);
			}
			while (n--)
				free(namelist[n]);
			free(namelist);
		}
		closedir(dir);
		k++;
	}
	return 0;
}


int probe_thermal_sysfs(void)
{
	DIR *dir;
	struct dirent **namelist;
	int n;

	dir = opendir(THERMAL_SYSFS);
	if (!dir) {
		fprintf(stderr, "\nNo thermal sysfs, exit\n");
		return -1;
	}
	n = scandir(THERMAL_SYSFS, &namelist, 0, alphasort);
	if (n < 0)
		syslog(LOG_ERR, "scandir failed in thermal sysfs");
	else {
		/* detect number of thermal zones and cooling devices */
		while (n--) {
			int inst;

			if (strstr(namelist[n]->d_name, CDEV)) {
				inst = get_instance_id(namelist[n]->d_name, 1,
						sizeof("device") - 1);
				/* keep track of the max cooling device since
				 * there may be gaps.
				 */
				if (inst > ptdata.max_cdev_instance)
					ptdata.max_cdev_instance = inst;

				syslog(LOG_DEBUG, "found cdev: %s %d %d\n",
					namelist[n]->d_name,
					ptdata.nr_cooling_dev,
					ptdata.max_cdev_instance);
				ptdata.nr_cooling_dev++;
			} else if (strstr(namelist[n]->d_name, TZONE)) {
				inst = get_instance_id(namelist[n]->d_name, 1,
						sizeof("zone") - 1);
				if (inst > ptdata.max_tz_instance)
					ptdata.max_tz_instance = inst;

				syslog(LOG_DEBUG, "found tzone: %s %d %d\n",
					namelist[n]->d_name,
					ptdata.nr_tz_sensor,
					ptdata.max_tz_instance);
				ptdata.nr_tz_sensor++;
			}
			free(namelist[n]);
		}
		free(namelist);
	}
	syslog(LOG_INFO, "found %d tzone(s), %d cdev(s), target zone %d\n",
		ptdata.nr_tz_sensor, ptdata.nr_cooling_dev,
		target_thermal_zone);
	closedir(dir);

	if (!ptdata.nr_tz_sensor) {
		fprintf(stderr, "\nNo thermal zones found, exit\n\n");
		return -1;
	}

	ptdata.tzi = calloc(ptdata.max_tz_instance+1, sizeof(struct tz_info));
	if (!ptdata.tzi) {
		fprintf(stderr, "Err: allocate tz_info\n");
		return -1;
	}

	/* we still show thermal zone information if there is no cdev */
	if (ptdata.nr_cooling_dev) {
		ptdata.cdi = calloc(ptdata.max_cdev_instance + 1,
				sizeof(struct cdev_info));
		if (!ptdata.cdi) {
			free(ptdata.tzi);
			fprintf(stderr, "Err: allocate cdev_info\n");
			return -1;
		}
	}

	/* now probe tzones */
	if (scan_tzones())
		return -1;
	if (scan_cdevs())
		return -1;
	return 0;
}

/* convert sysfs zone instance to zone array index */
int zone_instance_to_index(int zone_inst)
{
	int i;

	for (i = 0; i < ptdata.nr_tz_sensor; i++)
		if (ptdata.tzi[i].instance == zone_inst)
			return i;
	return -ENOENT;
}

/* read temperature of all thermal zones */
int update_thermal_data()
{
	int i;
	int next_thermal_record = cur_thermal_record + 1;
	char tz_name[256];
	static unsigned long samples;

	if (!ptdata.nr_tz_sensor) {
		syslog(LOG_ERR, "No thermal zones found!\n");
		return -1;
	}

	/* circular buffer for keeping historic data */
	if (next_thermal_record >= NR_THERMAL_RECORDS)
		next_thermal_record = 0;
	gettimeofday(&trec[next_thermal_record].tv, NULL);
	if (tmon_log) {
		fprintf(tmon_log, "%lu ", ++samples);
		fprintf(tmon_log, "%3.1f ", p_param.t_target);
	}
	for (i = 0; i < ptdata.nr_tz_sensor; i++) {
		memset(tz_name, 0, sizeof(tz_name));
		snprintf(tz_name, 256, "%s/%s%d", THERMAL_SYSFS, TZONE,
			ptdata.tzi[i].instance);
		sysfs_get_ulong(tz_name, "temp",
				&trec[next_thermal_record].temp[i]);
		if (tmon_log)
			fprintf(tmon_log, "%lu ",
				trec[next_thermal_record].temp[i] / 1000);
	}
	cur_thermal_record = next_thermal_record;
	for (i = 0; i < ptdata.nr_cooling_dev; i++) {
		char cdev_name[256];
		unsigned long val;

		snprintf(cdev_name, 256, "%s/%s%d", THERMAL_SYSFS, CDEV,
			ptdata.cdi[i].instance);
		probe_cdev(&ptdata.cdi[i], cdev_name);
		val = ptdata.cdi[i].cur_state;
		if (val > 1000000)
			val = 0;
		if (tmon_log)
			fprintf(tmon_log, "%lu ", val);
	}

	if (tmon_log) {
		fprintf(tmon_log, "\n");
		fflush(tmon_log);
	}

	return 0;
}

void set_ctrl_state(unsigned long state)
{
	char ctrl_cdev_path[256];
	int i;
	unsigned long cdev_state;

	if (no_control)
		return;
	/* set all ctrl cdev to the same state */
	for (i = 0; i < ptdata.nr_cooling_dev; i++) {
		if (ptdata.cdi[i].flag & CDEV_FLAG_IN_CONTROL) {
			if (ptdata.cdi[i].max_state < 10) {
				strcpy(ctrl_cdev, "None.");
				return;
			}
			/* scale to percentage of max_state */
			cdev_state = state * ptdata.cdi[i].max_state/100;
			syslog(LOG_DEBUG,
				"ctrl cdev %d set state %lu scaled to %lu\n",
				ptdata.cdi[i].instance, state, cdev_state);
			snprintf(ctrl_cdev_path, 256, "%s/%s%d", THERMAL_SYSFS,
				CDEV, ptdata.cdi[i].instance);
			syslog(LOG_DEBUG, "ctrl cdev path %s", ctrl_cdev_path);
			sysfs_set_ulong(ctrl_cdev_path, "cur_state",
					cdev_state);
		}
	}
}

void get_ctrl_state(unsigned long *state)
{
	char ctrl_cdev_path[256];
	int ctrl_cdev_id = -1;
	int i;

	/* TODO: take average of all ctrl types. also consider change based on
	 * uevent. Take the first reading for now.
	 */
	for (i = 0; i < ptdata.nr_cooling_dev; i++) {
		if (ptdata.cdi[i].flag & CDEV_FLAG_IN_CONTROL) {
			ctrl_cdev_id = ptdata.cdi[i].instance;
			syslog(LOG_INFO, "ctrl cdev %d get state\n",
				ptdata.cdi[i].instance);
			break;
		}
	}
	if (ctrl_cdev_id == -1) {
		*state = 0;
		return;
	}
	snprintf(ctrl_cdev_path, 256, "%s/%s%d", THERMAL_SYSFS,
		CDEV, ctrl_cdev_id);
	sysfs_get_ulong(ctrl_cdev_path, "cur_state", state);
}

void free_thermal_data(void)
{
	free(ptdata.tzi);
	free(ptdata.cdi);
}
