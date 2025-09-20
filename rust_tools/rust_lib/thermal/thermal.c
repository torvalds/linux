// SPDX-License-Identifier: LGPL-2.1+
// Copyright (C) 2022, Linaro Ltd - Daniel Lezcano <daniel.lezcano@linaro.org>
#include <stdio.h>
#include <limits.h>
#include <thermal.h>

#include "thermal_nl.h"

int for_each_thermal_threshold(struct thermal_threshold *th, cb_th_t cb, void *arg)
{
	int i, ret = 0;

	if (!th)
		return 0;

	for (i = 0; th[i].temperature != INT_MAX; i++)
		ret |= cb(&th[i], arg);

	return ret;
}

int for_each_thermal_cdev(struct thermal_cdev *cdev, cb_tc_t cb, void *arg)
{
	int i, ret = 0;

	if (!cdev)
		return 0;

	for (i = 0; cdev[i].id != -1; i++)
		ret |= cb(&cdev[i], arg);

	return ret;
}

int for_each_thermal_trip(struct thermal_trip *tt, cb_tt_t cb, void *arg)
{
	int i, ret = 0;

	if (!tt)
		return 0;

	for (i = 0; tt[i].id != -1; i++)
		ret |= cb(&tt[i], arg);

	return ret;
}

int for_each_thermal_zone(struct thermal_zone *tz, cb_tz_t cb, void *arg)
{
	int i, ret = 0;

	if (!tz)
		return 0;

	for (i = 0; tz[i].id != -1; i++)
		ret |= cb(&tz[i], arg);

	return ret;
}

struct thermal_zone *thermal_zone_find_by_name(struct thermal_zone *tz,
					       const char *name)
{
	int i;

	if (!tz || !name)
		return NULL;

	for (i = 0; tz[i].id != -1; i++) {
		if (!strcmp(tz[i].name, name))
			return &tz[i];
	}

	return NULL;
}

struct thermal_zone *thermal_zone_find_by_id(struct thermal_zone *tz, int id)
{
	int i;

	if (!tz || id < 0)
		return NULL;

	for (i = 0; tz[i].id != -1; i++) {
		if (tz[i].id == id)
			return &tz[i];
	}

	return NULL;
}

static int __thermal_zone_discover(struct thermal_zone *tz, void *th)
{
	if (thermal_cmd_get_trip(th, tz) < 0)
		return -1;

	if (thermal_cmd_threshold_get(th, tz))
		return -1;

	if (thermal_cmd_get_governor(th, tz))
		return -1;

	return 0;
}

struct thermal_zone *thermal_zone_discover(struct thermal_handler *th)
{
	struct thermal_zone *tz;

	if (thermal_cmd_get_tz(th, &tz) < 0)
		return NULL;

	if (for_each_thermal_zone(tz, __thermal_zone_discover, th))
		return NULL;

	return tz;
}

void thermal_exit(struct thermal_handler *th)
{
	thermal_cmd_exit(th);
	thermal_events_exit(th);
	thermal_sampling_exit(th);

	free(th);
}

struct thermal_handler *thermal_init(struct thermal_ops *ops)
{
	struct thermal_handler *th;

	th = malloc(sizeof(*th));
	if (!th)
		return NULL;
	th->ops = ops;

	if (thermal_events_init(th))
		goto out_free;

	if (thermal_sampling_init(th))
		goto out_free;

	if (thermal_cmd_init(th))
		goto out_free;

	return th;

out_free:
	free(th);

	return NULL;
}
