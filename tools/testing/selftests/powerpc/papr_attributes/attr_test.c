// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PAPR Energy attributes sniff test
 * This checks if the papr folders and contents are populated relating to
 * the energy and frequency attributes
 *
 * Copyright 2022, Pratik Rajesh Sampat, IBM Corp.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>

#include "utils.h"

enum energy_freq_attrs {
	POWER_PERFORMANCE_MODE = 1,
	IDLE_POWER_SAVER_STATUS = 2,
	MIN_FREQ = 3,
	STAT_FREQ = 4,
	MAX_FREQ = 6,
	PROC_FOLDING_STATUS = 8
};

enum type {
	INVALID,
	STR_VAL,
	NUM_VAL
};

static int value_type(int id)
{
	int val_type;

	switch (id) {
	case POWER_PERFORMANCE_MODE:
	case IDLE_POWER_SAVER_STATUS:
		val_type = STR_VAL;
		break;
	case MIN_FREQ:
	case STAT_FREQ:
	case MAX_FREQ:
	case PROC_FOLDING_STATUS:
		val_type = NUM_VAL;
		break;
	default:
		val_type = INVALID;
	}

	return val_type;
}

static int verify_energy_info(void)
{
	const char *path = "/sys/firmware/papr/energy_scale_info";
	struct dirent *entry;
	struct stat s;
	DIR *dirp;

	errno = 0;
	if (stat(path, &s)) {
		SKIP_IF(errno == ENOENT);
		FAIL_IF(errno);
	}

	FAIL_IF(!S_ISDIR(s.st_mode));

	dirp = opendir(path);

	while ((entry = readdir(dirp)) != NULL) {
		char file_name[64];
		int id, attr_type;
		FILE *f;

		if (strcmp(entry->d_name, ".") == 0 ||
		    strcmp(entry->d_name, "..") == 0)
			continue;

		id = atoi(entry->d_name);
		attr_type = value_type(id);
		FAIL_IF(attr_type == INVALID);

		/* Check if the files exist and have data in them */
		sprintf(file_name, "%s/%d/desc", path, id);
		f = fopen(file_name, "r");
		FAIL_IF(!f);
		FAIL_IF(fgetc(f) == EOF);

		sprintf(file_name, "%s/%d/value", path, id);
		f = fopen(file_name, "r");
		FAIL_IF(!f);
		FAIL_IF(fgetc(f) == EOF);

		if (attr_type == STR_VAL) {
			sprintf(file_name, "%s/%d/value_desc", path, id);
			f = fopen(file_name, "r");
			FAIL_IF(!f);
			FAIL_IF(fgetc(f) == EOF);
		}
	}

	return 0;
}

int main(void)
{
	return test_harness(verify_energy_info, "papr_attributes");
}
