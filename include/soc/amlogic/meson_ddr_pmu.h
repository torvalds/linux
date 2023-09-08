/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Amlogic, Inc. All rights reserved.
 */

#ifndef __MESON_DDR_PMU_H__
#define __MESON_DDR_PMU_H__

#define MAX_CHANNEL_NUM		8

enum {
	ALL_CHAN_COUNTER_ID,
	CHAN1_COUNTER_ID,
	CHAN2_COUNTER_ID,
	CHAN3_COUNTER_ID,
	CHAN4_COUNTER_ID,
	CHAN5_COUNTER_ID,
	CHAN6_COUNTER_ID,
	CHAN7_COUNTER_ID,
	CHAN8_COUNTER_ID,
	COUNTER_MAX_ID,
};

struct dmc_info;

struct dmc_counter {
	u64 all_cnt;	/* The count of all requests come in/out ddr controller */
	union {
		u64 all_req;
		struct {
			u64 all_idle_cnt;
			u64 all_16bit_cnt;
		};
	};
	u64 channel_cnt[MAX_CHANNEL_NUM]; /* To save a DMC bandwidth-monitor channel counter */
};

struct dmc_hw_info {
	void (*enable)(struct dmc_info *info);
	void (*disable)(struct dmc_info *info);
	/* Bind an axi line to a bandwidth-monitor channel */
	void (*set_axi_filter)(struct dmc_info *info, int axi_id, int chann);
	int (*irq_handler)(struct dmc_info *info,
			   struct dmc_counter *counter);
	void (*get_counters)(struct dmc_info *info,
			     struct dmc_counter *counter);

	int dmc_nr;			/* The number of dmc controller */
	int chann_nr;			/* The number of dmc bandwidth monitor channels */
	struct attribute **fmt_attr;
	const u64 capability[2];
};

struct dmc_info {
	const struct dmc_hw_info *hw_info;

	void __iomem *ddr_reg[4];
	unsigned long timer_value;	/* Timer value in TIMER register */
	void __iomem *pll_reg;
	int irq_num;			/* irq vector number */
};

int meson_ddr_pmu_create(struct platform_device *pdev);
int meson_ddr_pmu_remove(struct platform_device *pdev);

#endif /* __MESON_DDR_PMU_H__ */
