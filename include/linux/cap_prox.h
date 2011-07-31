/*
 * include/linux/cap_prox.h
 *
 * Copyright (C) 2010 Motorola, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _LINUX_CAP_PROX_H
#define _LINUX_CAP_PROX_H

struct cap_prox_cfg {
	uint8_t	lp_mode;
	uint8_t	address_ptr;
	uint8_t	reset;
	uint8_t	key_enable_mask;
	uint8_t	data_integration;
	uint8_t	neg_drift_rate;
	uint8_t	pos_drift_rate;
	uint8_t	force_detect;
	uint8_t	calibrate;
	uint8_t	thres_key1;
	uint8_t	ref_backup;
	uint8_t	thres_key2;
	uint8_t	reserved12;
	uint8_t	drift_hold_time;
	uint8_t	reserved14;
	uint8_t	reserved15;
} __attribute__ ((packed));

struct cap_prox_platform_data {
	int poll_interval;
	int min_poll_interval;
	uint8_t key1_ref_drift_thres_l;
	uint8_t key3_ref_drift_thres_l;
	uint8_t key1_ref_drift_thres_h;
	uint8_t key3_ref_drift_thres_h;
	uint8_t ref_drift_diff_thres;
	uint8_t key1_save_drift_thres;
	uint8_t key3_save_drift_thres;
	uint8_t save_drift_diff_thres;
	uint8_t key1_failsafe_thres;
	uint8_t key3_failsafe_thres;
	uint16_t key2_signal_thres;
	uint16_t key4_signal_thres;

	struct cap_prox_cfg	plat_cap_prox_cfg;
};

#endif /* _LINUX_CAP_PROX_H */
