// SPDX-License-Identifier: GPL-2.0-only
/*
 *  (C) 2016 SUSE Software Solutions GmbH
 *           Thomas Renninger <trenn@suse.de>
 */

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <getopt.h>

#include "powercap.h"
#include "helpers/helpers.h"

int powercap_show_all;

static struct option info_opts[] = {
	{ "all",		no_argument,		 NULL,	 'a'},
	{ },
};

static int powercap_print_one_zone(struct powercap_zone *zone)
{
	int mode, i, ret = 0;
	char pr_prefix[1024] = "";

	for (i = 0; i < zone->tree_depth && i < POWERCAP_MAX_TREE_DEPTH; i++)
		strcat(pr_prefix, "\t");

	printf("%sZone: %s", pr_prefix, zone->name);
	ret = powercap_zone_get_enabled(zone, &mode);
	if (ret < 0)
		return ret;
	printf(" (%s)\n", mode ? "enabled" : "disabled");

	if (zone->has_power_uw)
		printf(_("%sPower can be monitored in micro Jules\n"),
		       pr_prefix);

	if (zone->has_energy_uj)
		printf(_("%sPower can be monitored in micro Watts\n"),
		       pr_prefix);

	printf("\n");

	if (ret != 0)
		return ret;
	return ret;
}

static int powercap_show(void)
{
	struct powercap_zone *root_zone;
	char line[MAX_LINE_LEN] = "";
	int ret, val;

	ret = powercap_get_driver(line, MAX_LINE_LEN);
	if (ret < 0) {
		printf(_("No powercapping driver loaded\n"));
		return ret;
	}

	printf("Driver: %s\n", line);
	ret = powercap_get_enabled(&val);
	if (ret < 0)
		return ret;
	if (!val) {
		printf(_("Powercapping is disabled\n"));
		return -1;
	}

	printf(_("Powercap domain hierarchy:\n\n"));
	root_zone = powercap_init_zones();

	if (root_zone == NULL) {
		printf(_("No powercap info found\n"));
		return 1;
	}

	powercap_walk_zones(root_zone, powercap_print_one_zone);

	return 0;
}

int cmd_cap_set(int argc, char **argv)
{
	return 0;
};
int cmd_cap_info(int argc, char **argv)
{
	int ret = 0, cont = 1;

	do {
		ret = getopt_long(argc, argv, "a", info_opts, NULL);
		switch (ret) {
		case '?':
			cont = 0;
			break;
		case -1:
			cont = 0;
			break;
		case 'a':
			powercap_show_all = 1;
			break;
		default:
			fprintf(stderr, _("invalid or unknown argument\n"));
			return EXIT_FAILURE;
		}
	} while (cont);

	powercap_show();
	return 0;
}
