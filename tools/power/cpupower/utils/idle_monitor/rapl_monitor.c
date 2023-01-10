// SPDX-License-Identifier: GPL-2.0-only
/*
 *  (C) 2016 SUSE Software Solutions GmbH
 *           Thomas Renninger <trenn@suse.de>
 */

#if defined(__i386__) || defined(__x86_64__)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <string.h>

#include <pci/pci.h>

#include "idle_monitor/cpupower-monitor.h"
#include "helpers/helpers.h"
#include "powercap.h"

#define MAX_RAPL_ZONES 10

int rapl_zone_count;
cstate_t rapl_zones[MAX_RAPL_ZONES];
struct powercap_zone *rapl_zones_pt[MAX_RAPL_ZONES] = { 0 };

unsigned long long rapl_zone_previous_count[MAX_RAPL_ZONES];
unsigned long long rapl_zone_current_count[MAX_RAPL_ZONES];
unsigned long long rapl_max_count;

static int rapl_get_count_uj(unsigned int id, unsigned long long *count,
			     unsigned int cpu)
{
	if (rapl_zones_pt[id] == NULL)
		/* error */
		return -1;

	*count = rapl_zone_current_count[id] - rapl_zone_previous_count[id];

	return 0;
}

static int powercap_count_zones(struct powercap_zone *zone)
{
	uint64_t val;
	int uj;

	if (rapl_zone_count >= MAX_RAPL_ZONES)
		return -1;

	if (!zone->has_energy_uj)
		return 0;

	printf("%s\n", zone->sys_name);
	uj = powercap_get_energy_uj(zone, &val);
	printf("%d\n", uj);

	strncpy(rapl_zones[rapl_zone_count].name, zone->name, CSTATE_NAME_LEN - 1);
	strcpy(rapl_zones[rapl_zone_count].desc, "");
	rapl_zones[rapl_zone_count].id = rapl_zone_count;
	rapl_zones[rapl_zone_count].range = RANGE_MACHINE;
	rapl_zones[rapl_zone_count].get_count = rapl_get_count_uj;
	rapl_zones_pt[rapl_zone_count] = zone;
	rapl_zone_count++;

	return 0;
}

static int rapl_start(void)
{
	int i, ret;
	uint64_t uj_val;

	for (i = 0; i < rapl_zone_count; i++) {
		ret = powercap_get_energy_uj(rapl_zones_pt[i], &uj_val);
		if (ret)
			return ret;
		rapl_zone_previous_count[i] = uj_val;
	}

	return 0;
}

static int rapl_stop(void)
{
	int i;
	uint64_t uj_val;

	for (i = 0; i < rapl_zone_count; i++) {
		int ret;

		ret = powercap_get_energy_uj(rapl_zones_pt[i], &uj_val);
		if (ret)
			return ret;
		rapl_zone_current_count[i] = uj_val;
		if (rapl_max_count < uj_val)
			rapl_max_count = uj_val - rapl_zone_previous_count[i];
	}
	return 0;
}

struct cpuidle_monitor *rapl_register(void)
{
	struct powercap_zone *root_zone;
	char line[MAX_LINE_LEN] = "";
	int ret, val;

	ret = powercap_get_driver(line, MAX_LINE_LEN);
	if (ret < 0) {
		dprint("No powercapping driver loaded\n");
		return NULL;
	}

	dprint("Driver: %s\n", line);
	ret = powercap_get_enabled(&val);
	if (ret < 0)
		return NULL;
	if (!val) {
		dprint("Powercapping is disabled\n");
		return NULL;
	}

	dprint("Powercap domain hierarchy:\n\n");
	root_zone = powercap_init_zones();

	if (root_zone == NULL) {
		dprint("No powercap info found\n");
		return NULL;
	}

	powercap_walk_zones(root_zone, powercap_count_zones);
	rapl_monitor.hw_states_num = rapl_zone_count;

	return &rapl_monitor;
}

struct cpuidle_monitor rapl_monitor = {
	.name			= "RAPL",
	.hw_states		= rapl_zones,
	.hw_states_num		= 0,
	.start			= rapl_start,
	.stop			= rapl_stop,
	.do_register		= rapl_register,
	.flags.needs_root	= 0,
	.overflow_s		= 60 * 60 * 24 * 100, /* To be implemented */
};

#endif
