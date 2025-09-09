// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2025, Qualcomm Technologies, Inc. and/or its subsidiaries.

#include <linux/export.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/printk.h>

#include "wcd-common.h"

#define WCD_MIN_MICBIAS_MV	1000
#define WCD_DEF_MICBIAS_MV	1800
#define WCD_MAX_MICBIAS_MV	2850

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
	int i;

	for (i = 0; i < common->max_bias; i++) {
		common->micb_vout[i] = wcd_get_micbias_val(common->dev, i + 1, &common->micb_mv[i]);
		if (common->micb_vout[i] < 0)
			return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(wcd_dt_parse_micbias_info);
MODULE_DESCRIPTION("Common Qualcomm WCD Codec helpers driver");
MODULE_LICENSE("GPL");
