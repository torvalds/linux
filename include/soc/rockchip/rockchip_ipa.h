/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018 Fuzhou Rockchip Electronics Co., Ltd
 */
#ifndef __SOC_ROCKCHIP_IPA_H
#define __SOC_ROCKCHIP_IPA_H

struct ipa_power_model_data {
	u32 static_coefficient;
	u32 dynamic_coefficient;
	s32 ts[4];
	struct thermal_zone_device *tz;
};

#ifdef CONFIG_ROCKCHIP_IPA
int rockchip_ipa_power_model_init(struct device *dev,
				  struct ipa_power_model_data **data);
unsigned long
rockchip_ipa_get_static_power(struct ipa_power_model_data *model_data,
			      unsigned long voltage);
#else
static inline int
rockchip_ipa_power_model_init(struct device *dev,
			      struct ipa_power_model_data **data)
{
	return -ENOTSUPP;
};

static inline unsigned long
rockchip_ipa_get_static_power(struct ipa_power_model_data *data,
			      unsigned long voltage)
{
	return 0;
}
#endif /* CONFIG_ROCKCHIP_IPA */

#endif
