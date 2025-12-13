/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025, Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __WCD_COMMON_H__
#define __WCD_COMMON_H__

struct device;
struct sdw_slave;
struct sdw_bus_params;
struct irq_domain;
enum sdw_slave_status;

#define WCD_MAX_MICBIAS		4

struct wcd_sdw_ch_info {
	int port_num;
	unsigned int ch_mask;
	unsigned int master_ch_mask;
};

#define WCD_SDW_CH(id, pn, cmask)	\
	[id] = {			\
		.port_num = pn,		\
		.ch_mask = cmask,	\
		.master_ch_mask = cmask,	\
	}

struct wcd_common {
	struct device *dev;
	int max_bias;
	u32 micb_mv[WCD_MAX_MICBIAS];
	u32 micb_vout[WCD_MAX_MICBIAS];
};

extern const struct component_ops wcd_sdw_component_ops;
int wcd_get_micb_vout_ctl_val(struct device *dev, u32 micb_mv);
int wcd_dt_parse_micbias_info(struct wcd_common *common);
int wcd_update_status(struct sdw_slave *slave, enum sdw_slave_status status);
int wcd_bus_config(struct sdw_slave *slave, struct sdw_bus_params *params);
int wcd_interrupt_callback(struct sdw_slave *slave, struct irq_domain *slave_irq,
		unsigned int wcd_intr_status0, unsigned int wcd_intr_status1,
		unsigned int wcd_intr_status2);

#endif /* __WCD_COMMON_H__  */
