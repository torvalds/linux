// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2025, Qualcomm Technologies, Inc. and/or its subsidiaries.

#include <linux/export.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/printk.h>
#include <linux/component.h>
#include <linux/pm_runtime.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <linux/regmap.h>

#include "wcd-common.h"

#define WCD_MIN_MICBIAS_MV	1000
#define WCD_DEF_MICBIAS_MV	1800
#define WCD_MAX_MICBIAS_MV	2850

#define SWRS_SCP_HOST_CLK_DIV2_CTL_BANK(m) (0xE0 + 0x10 * (m))

int wcd_get_micb_vout_ctl_val(struct device *dev, u32 micb_mv)
{
	/* min micbias voltage is 1V and maximum is 2.85V */
	if (micb_mv < WCD_MIN_MICBIAS_MV || micb_mv > WCD_MAX_MICBIAS_MV) {
		dev_err(dev, "Unsupported micbias voltage (%u mV)\n", micb_mv);
		return -EINVAL;
	}

	return (micb_mv - WCD_MIN_MICBIAS_MV) / 50;
}
EXPORT_SYMBOL_GPL(wcd_get_micb_vout_ctl_val);

static int wcd_get_micbias_val(struct device *dev, int micb_num, u32 *micb_mv)
{
	char micbias[64];
	int mv;

	sprintf(micbias, "qcom,micbias%d-microvolt", micb_num);

	if (of_property_read_u32(dev->of_node, micbias, &mv)) {
		dev_err(dev, "%s value not found, using default\n", micbias);
		mv = WCD_DEF_MICBIAS_MV;
	} else {
		/* convert it to milli volts */
		mv = mv/1000;
	}
	if (micb_mv)
		*micb_mv = mv;

	mv = wcd_get_micb_vout_ctl_val(dev, mv);
	if (mv < 0) {
		dev_err(dev, "Unsupported %s voltage (%d mV), falling back to default (%d mV)\n",
				micbias, mv, WCD_DEF_MICBIAS_MV);
		return wcd_get_micb_vout_ctl_val(dev, WCD_DEF_MICBIAS_MV);
	}

	return mv;
}

int wcd_dt_parse_micbias_info(struct wcd_common *common)
{
	int ret, i;

	for (i = 0; i < common->max_bias; i++) {
		ret = wcd_get_micbias_val(common->dev, i + 1, &common->micb_mv[i]);
		if (ret < 0)
			return ret;
		common->micb_vout[i] = ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(wcd_dt_parse_micbias_info);

static int wcd_sdw_component_bind(struct device *dev, struct device *master, void *data)
{
	pm_runtime_set_autosuspend_delay(dev, 3000);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	return 0;
}

static void wcd_sdw_component_unbind(struct device *dev, struct device *master, void *data)
{
	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	pm_runtime_dont_use_autosuspend(dev);
}

const struct component_ops wcd_sdw_component_ops = {
	.bind = wcd_sdw_component_bind,
	.unbind = wcd_sdw_component_unbind,
};
EXPORT_SYMBOL_GPL(wcd_sdw_component_ops);

int wcd_update_status(struct sdw_slave *slave, enum sdw_slave_status status)
{
	struct regmap *regmap = dev_get_regmap(&slave->dev, NULL);

	if (regmap && status == SDW_SLAVE_ATTACHED) {
		/* Write out any cached changes that happened between probe and attach */
		regcache_cache_only(regmap, false);
		return regcache_sync(regmap);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(wcd_update_status);

int wcd_bus_config(struct sdw_slave *slave, struct sdw_bus_params *params)
{
	sdw_write(slave, SWRS_SCP_HOST_CLK_DIV2_CTL_BANK(params->next_bank), 0x01);

	return 0;
}
EXPORT_SYMBOL_GPL(wcd_bus_config);

int wcd_interrupt_callback(struct sdw_slave *slave, struct irq_domain *slave_irq,
		unsigned int wcd_intr_status0, unsigned int wcd_intr_status1,
		unsigned int wcd_intr_status2)
{
	struct regmap *regmap = dev_get_regmap(&slave->dev, NULL);
	u32 sts1, sts2, sts3;

	do {
		handle_nested_irq(irq_find_mapping(slave_irq, 0));
		regmap_read(regmap, wcd_intr_status0, &sts1);
		regmap_read(regmap, wcd_intr_status1, &sts2);
		regmap_read(regmap, wcd_intr_status2, &sts3);

	} while (sts1 || sts2 || sts3);

	return IRQ_HANDLED;
}
EXPORT_SYMBOL_GPL(wcd_interrupt_callback);

MODULE_DESCRIPTION("Common Qualcomm WCD Codec helpers driver");
MODULE_LICENSE("GPL");
