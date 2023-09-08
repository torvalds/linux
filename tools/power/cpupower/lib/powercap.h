/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  (C) 2016 SUSE Software Solutions GmbH
 *           Thomas Renninger <trenn@suse.de>
 */

#ifndef __CPUPOWER_RAPL_H__
#define __CPUPOWER_RAPL_H__

#define PATH_TO_POWERCAP "/sys/devices/virtual/powercap"
#define PATH_TO_RAPL "/sys/devices/virtual/powercap/intel-rapl"
#define PATH_TO_RAPL_CLASS "/sys/devices/virtual/powercap/intel-rapl"

#define POWERCAP_MAX_CHILD_ZONES 10
#define POWERCAP_MAX_TREE_DEPTH 10

#define MAX_LINE_LEN 4096
#define SYSFS_PATH_MAX 255

#include <stdint.h>

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


#endif /* __CPUPOWER_RAPL_H__ */
