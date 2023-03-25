/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __QCOM_THERMAL_MINIDUMP_H__
#define __QCOM_THERMAL_MINIDUMP_H__

#include <linux/kernel.h>
#include <linux/thermal.h>
#include <linux/types.h>
#include <soc/qcom/minidump.h>

/* The amount of thermal data stored in the minidump  */
#define MD_NUM		50

struct sensor_type {
	char sensor_type[THERMAL_NAME_LENGTH];
};

/**
 * struct minidump_data - Thermal data stored in minidump
 * @type: The type of sensor, this data must be the second item
 *        of the structure, otherwise the parsing will fail
 * @temp: The temp of sensor, this data must be the second item
 *        of the structure, otherwise the parsing will fail
 * @md_count: flag for minidump data store count, this data must
 *            be the third item of the structure, otherwise the
 *            parsing will fail
 * @md_entry: Minidump table entry
 * @region: region number, entry position in minidump tables
 */
struct minidump_data {
	struct sensor_type type[MD_NUM];
	int temp[MD_NUM];
	u32 md_count;

	struct md_region md_entry;
	int region;

	spinlock_t update_md_lock;
};
#if IS_ENABLED(CONFIG_QTI_THERMAL_MINIDUMP)
int thermal_minidump_update_data(struct minidump_data *md,
		char *type, int *temp);
struct minidump_data *thermal_minidump_register(const char *name);
void thermal_minidump_unregister(struct minidump_data *md);
#else
static inline int thermal_minidump_update_data(
		struct minidump_data *md, char *type, int *temp)
{ return -ENXIO; }
static inline struct minidump_data *thermal_minidump_register(const char *name)
{ return NULL; }
static inline void thermal_minidump_unregister(struct minidump_data *md) { }
#endif
#endif
