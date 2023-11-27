/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __QBG_H__
#define __QBG_H__

#include <linux/types.h>

#define MAX_FIFO_COUNT			36
#define QBG_MAX_STEP_CHG_ENTRIES	6

#define QBG_QBGIOCXCFG			0x01
#define QBG_QBGIOCXEPR			0x02
#define QBG_QBGIOCXEPW			0x03
#define QBG_QBGIOCXSTEPCHGCFG		0x04

enum QBG_STATE {
	QBG_LPM,
	QBG_MPM,
	QBG_HPM,
	QBG_FAST_CHAR,
	QBG_PON_OCV,
	QBG_STATE_MAX,
};

enum QBG_SDAM_DATA_OFFSET {
	QBG_ACC0_OFFSET = 0,
	QBG_ACC1_OFFSET = 2,
	QBG_ACC2_OFFSET = 4,
	QBG_TBAT_OFFSET = 6,
	QBG_IBAT_OFFSET = 8,
	QBG_VREF_OFFSET = 10,
	QBG_DATA_TAG_OFFSET = 12,
	QBG_QG_STS_OFFSET,
	QBG_STS1_OFFSET,
	QBG_STS2_OFFSET,
	QBG_STS3_OFFSET,
	QBG_ONE_FIFO_LENGTH,
};

enum qbg {
	QBG_PARAM_SOC,
	QBG_PARAM_BATT_SOC,
	QBG_PARAM_SYS_SOC,
	QBG_PARAM_ESR,
	QBG_PARAM_OCV_UV,
	QBG_PARAM_MAX_LOAD_NOW,
	QBG_PARAM_MAX_LOAD_AVG,
	QBG_PARAM_HOLD_SOC_100PCT,
	QBG_PARAM_CHARGE_CYCLE_COUNT,
	QBG_PARAM_LEARNED_CAPACITY,
	QBG_PARAM_TTF_100MS,
	QBG_PARAM_TTE_100MS,
	QBG_PARAM_SOH,
	QBG_PARAM_TBAT,
	QBG_PARAM_SYS_SOC_HOLD_100PCT,
	QBG_PARAM_JEITA_COOL_THRESHOLD,
	QBG_PARAM_TOTAL_IMPEDANCE,
	QBG_PARAM_ESSENTIAL_PARAM_REVID,
	QBG_PARAM_FIFO_TIMESTAMP,
	QBG_PARAM_MAX,
};

struct qbg_essential_params {
	short int		msoc;
	short int		cutoff_soc;
	short int		full_soc;
	short int		x0;
	short int		x1;
	short int		x2;
	short int		soh_r;
	short int		soh_c;
	short int		theta0;
	short int		theta1;
	short int		theta2;
	short int		i1full;
	short int		i2full;
	short int		i1cutoff;
	short int		i2cutoff;
	short int		syssoc;
	int			discharge_cycle_count;
	int			charge_cycle_count;
	unsigned int		rtc_time;
	short int		batt_therm;
	unsigned short int	ocv;
} __attribute__ ((__packed__));

struct fifo_data {
	unsigned short int	v1;
	unsigned short int	v2;
	unsigned short int	i;
	unsigned short int	tbat;
	unsigned short int	ibat;
	unsigned short int	vref;
	char			data_tag;
	char			qg_sts;
	char			sts1;
	char			sts2;
	char			sts3;
} __attribute__ ((__packed__));

struct k_fifo_data {
	unsigned int	v1;
	unsigned int	v2;
	unsigned int	i;
	unsigned int	tbat;
	unsigned int	ibat;
	unsigned int	vref;
	unsigned int	data_tag;
	unsigned int	qg_sts;
	unsigned int	sts1;
	unsigned int	sts2;
	unsigned int	sts3;
} __attribute__ ((__packed__));

struct qbg_config {
	unsigned int	batt_id;
	unsigned int	pon_ocv;
	unsigned int	pon_ibat;
	unsigned int	pon_tbat;
	unsigned int	pon_soc;
	unsigned int	float_volt_uv;
	unsigned int	fastchg_curr_ma;
	unsigned int	vbat_cutoff_mv;
	unsigned int	ibat_cutoff_ma;
	unsigned int	vph_min_mv;
	unsigned int	iterm_ma;
	unsigned int	rconn_mohm;
	__u64		current_time;
	unsigned int	sdam_batt_id;
	unsigned int	essential_param_revid;
	__u64		sample_time_us[QBG_STATE_MAX];
} __attribute__ ((__packed__));

struct qbg_param {
	unsigned int			data;
	_Bool				valid;
};

struct qbg_kernel_data {
	unsigned int			seq_no;
	unsigned int			fifo_time;
	unsigned int			fifo_count;
	struct k_fifo_data		fifo[MAX_FIFO_COUNT];
	struct qbg_param		param[QBG_PARAM_MAX];
} __attribute__ ((__packed__));

struct qbg_user_data {
	struct qbg_param		param[QBG_PARAM_MAX];
} __attribute__ ((__packed__));

struct range_data {
	int		low_threshold;
	int		high_threshold;
	unsigned int	value;
} __attribute__ ((__packed__));

struct ranges {
	struct		range_data data[QBG_MAX_STEP_CHG_ENTRIES];
	unsigned char	range_count;
	_Bool		valid;
} __attribute__((__packed__));

struct qbg_step_chg_jeita_params {
	int		jeita_full_fv_10nv;
	int		jeita_full_iterm_10na;
	int		jeita_warm_adc_value;
	int		jeita_cool_adc_value;
	int		battery_beta;
	int		battery_therm_kohm;
	struct ranges	step_fcc_cfg;
	struct ranges	jeita_fcc_cfg;
	struct ranges	jeita_fv_cfg;
	unsigned char	ttf_calc_mode;
} __attribute__ ((__packed__));

/*  IOCTLs to read & write QBG config and essential params */
#define QBGIOCXCFG	_IOR('B', 0x01, struct qbg_config)
#define QBGIOCXEPR	_IOR('B', 0x02, struct qbg_essential_params)
#define QBGIOCXEPW	_IOWR('B', 0x03, struct qbg_essential_params)
#define QBGIOCXSTEPCHGCFG	_IOWR('B', 0x04, struct qbg_step_chg_jeita_params)

#endif
