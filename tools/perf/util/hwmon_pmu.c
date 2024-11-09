// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
#include "debug.h"
#include "hwmon_pmu.h"
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <linux/kernel.h>
#include <linux/string.h>

/** Strings that correspond to enum hwmon_type. */
static const char * const hwmon_type_strs[HWMON_TYPE_MAX] = {
	NULL,
	"cpu",
	"curr",
	"energy",
	"fan",
	"humidity",
	"in",
	"intrusion",
	"power",
	"pwm",
	"temp",
};
#define LONGEST_HWMON_TYPE_STR "intrusion"

/** Strings that correspond to enum hwmon_item. */
static const char * const hwmon_item_strs[HWMON_ITEM__MAX] = {
	NULL,
	"accuracy",
	"alarm",
	"auto_channels_temp",
	"average",
	"average_highest",
	"average_interval",
	"average_interval_max",
	"average_interval_min",
	"average_lowest",
	"average_max",
	"average_min",
	"beep",
	"cap",
	"cap_hyst",
	"cap_max",
	"cap_min",
	"crit",
	"crit_hyst",
	"div",
	"emergency",
	"emergency_hist",
	"enable",
	"fault",
	"freq",
	"highest",
	"input",
	"label",
	"lcrit",
	"lcrit_hyst",
	"lowest",
	"max",
	"max_hyst",
	"min",
	"min_hyst",
	"mod",
	"offset",
	"pulses",
	"rated_max",
	"rated_min",
	"reset_history",
	"target",
	"type",
	"vid",
};
#define LONGEST_HWMON_ITEM_STR "average_interval_max"

static int hwmon_strcmp(const void *a, const void *b)
{
	const char *sa = a;
	const char * const *sb = b;

	return strcmp(sa, *sb);
}

bool parse_hwmon_filename(const char *filename,
			  enum hwmon_type *type,
			  int *number,
			  enum hwmon_item *item,
			  bool *alarm)
{
	char fn_type[24];
	const char **elem;
	const char *fn_item = NULL;
	size_t fn_item_len;

	assert(strlen(LONGEST_HWMON_TYPE_STR) < sizeof(fn_type));
	strlcpy(fn_type, filename, sizeof(fn_type));
	for (size_t i = 0; fn_type[i] != '\0'; i++) {
		if (fn_type[i] >= '0' && fn_type[i] <= '9') {
			fn_type[i] = '\0';
			*number = strtoul(&filename[i], (char **)&fn_item, 10);
			if (*fn_item == '_')
				fn_item++;
			break;
		}
		if (fn_type[i] == '_') {
			fn_type[i] = '\0';
			*number = -1;
			fn_item = &filename[i + 1];
			break;
		}
	}
	if (fn_item == NULL || fn_type[0] == '\0' || (item != NULL && fn_item[0] == '\0')) {
		pr_debug("hwmon_pmu: not a hwmon file '%s'\n", filename);
		return false;
	}
	elem = bsearch(&fn_type, hwmon_type_strs + 1, ARRAY_SIZE(hwmon_type_strs) - 1,
		       sizeof(hwmon_type_strs[0]), hwmon_strcmp);
	if (!elem) {
		pr_debug("hwmon_pmu: not a hwmon type '%s' in file name '%s'\n",
			 fn_type, filename);
		return false;
	}

	*type = elem - &hwmon_type_strs[0];
	if (!item)
		return true;

	*alarm = false;
	fn_item_len = strlen(fn_item);
	if (fn_item_len > 6 && !strcmp(&fn_item[fn_item_len - 6], "_alarm")) {
		assert(strlen(LONGEST_HWMON_ITEM_STR) < sizeof(fn_type));
		strlcpy(fn_type, fn_item, fn_item_len - 5);
		fn_item = fn_type;
		*alarm = true;
	}
	elem = bsearch(fn_item, hwmon_item_strs + 1, ARRAY_SIZE(hwmon_item_strs) - 1,
		       sizeof(hwmon_item_strs[0]), hwmon_strcmp);
	if (!elem) {
		pr_debug("hwmon_pmu: not a hwmon item '%s' in file name '%s'\n",
			 fn_item, filename);
		return false;
	}
	*item = elem - &hwmon_item_strs[0];
	return true;
}
