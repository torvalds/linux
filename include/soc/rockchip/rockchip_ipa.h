/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018 Fuzhou Rockchip Electronics Co., Ltd
 */
#ifndef __SOC_ROCKCHIP_IPA_H
#define __SOC_ROCKCHIP_IPA_H

struct ipa_power_model_data {
	u32 static_coefficient;
	u32 dynamic_coefficient;
	s32 ts[4];			/* temperature scaling factor */
	struct thermal_zone_device *tz;
	u32 leakage;
	u32 ref_leakage;
	u32 lkg_range[2];		/* min leakage and max leakage */
	s32 ls[3];			/* leakage scaling factor */
};

#if IS_ENABLED(CONFIG_ROCKCHIP_IPA)
struct ipa_power_model_data *rockchip_ipa_power_model_init(struct device *dev,
							   char *lkg_name);
unsigned long
rockchip_ipa_get_static_power(struct ipa_power_model_data *model_data,
			      unsigned long voltage_mv);
#else
static inline struct ipa_power_model_data *
rockchip_ipa_power_model_init(struct device *dev, char *lkg_name)
{
	return ERR_PTR(-ENOTSUPP);
};

static inline unsigned long
rockchip_ipa_get_static_power(struct ipa_power_model_data *data,
			      unsigned long voltage_mv)
{
	return 0;
}
#endif /* CONFIG_ROCKCHIP_IPA */

#endif
