/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __QBG_PROFILE_H__
#define __QBG_PROFILE_H__

#include <linux/types.h>

#define MAX_BP_LUT_ROWS	35
#define MAX_BP_LUT_COLS	8
#define MAX_PROFILE_NAME_LENGTH	256

#define QBG_BPIOCXBP		0x1
#define QBG_BPIOCXBPTABLE	0x2

enum profile_table_type {
	CHARGE_TABLE = 0,
	DISCHARGE_TABLE,
};

struct battery_data_table {
	unsigned short int table[MAX_BP_LUT_ROWS][MAX_BP_LUT_COLS];
	int unit_conv_factor[MAX_BP_LUT_COLS];
	unsigned short int nrows;
	unsigned short int ncols;
} __attribute__ ((__packed__));

struct battery_config {
	char bp_profile_name[MAX_PROFILE_NAME_LENGTH];
	int bp_batt_id;
	int capacity;
	int bp_checksum;
	int soh_range_high;
	int soh_range_low;
	int normal_impedance;
	int aged_impedance;
	int normal_capacity;
	int aged_capacity;
	int recharge_soc_delta;
	int recharge_vflt_delta;
	int recharge_iterm;
} __attribute__ ((__packed__));

struct battery_profile_table {
	enum profile_table_type table_type;
	int table_index;
	struct battery_data_table *table;
} __attribute__ ((__packed__));

/* IOCTLs to query battery profile data */
#define BPIOCXBP	_IOWR('B', 0x01, struct battery_config) /* Battery configuration */
#define BPIOCXBPTABLE	_IOWR('B', 0x02, struct battery_profile_table) /* Battery profile table */

#endif
