/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef WCD939X_I2C_H
#define WCD939X_I2C_H

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/notifier.h>

enum wcd_usbss_config_type {
	WCD_USBSS_CONFIG_TYPE_POWER_MODE,
	WCD_USBSS_CONFIG_TYPE_ZDET,
};

enum wcd_usbss_cable_types {
	WCD_USBSS_USB,
	WCD_USBSS_DP_AUX_CC1,
	WCD_USBSS_DP_AUX_CC2,
	WCD_USBSS_AATC,
	WCD_USBSS_GND_MIC_SWAP_AATC,
	WCD_USBSS_HSJ_CONNECT,
};

enum wcd_usbss_cable_status {
	WCD_USBSS_CABLE_DISCONNECT,
	WCD_USBSS_CABLE_CONNECT,
};

#if IS_ENABLED(CONFIG_QCOM_WCD_USBSS_I2C)
int wcd_usbss_switch_update(enum wcd_usbss_cable_types ctype,
		enum wcd_usbss_cable_status status);
int wcd_usbss_reg_notifier(struct notifier_block *nb,
			 struct device_node *node);
int wcd_usbss_unreg_notifier(struct notifier_block *nb,
			   struct device_node *node);
int wcd_usbss_dpdm_switch_update(bool enable, bool eq_en);
int wcd_usbss_audio_config(bool enable, enum wcd_usbss_config_type config_type,
		unsigned int power_mode);
#else
static inline int wcd_usbss_switch_update(enum wcd_usbss_cable_types ctype,
		enum wcd_usbss_cable_status status)
{
	return 0;
}

static inline int wcd_usbss_reg_notifier(struct notifier_block *nb,
				       struct device_node *node)
{
	return 0;
}

static inline int wcd_usbss_unreg_notifier(struct notifier_block *nb,
					 struct device_node *node)
{
	return 0;
}

static inline int wcd_usbss_dpdm_switch_update(bool enable, bool eq_en)
{
	return 0;
}

int wcd_usbss_audio_config(bool enable, enum wcd_usbss_config_type config_type,
		unsigned int power_mode)
{
	return 0;
}
#endif /* CONFIG_QCOM_WCD_USBSS_I2C */

#endif /* WCD939X_I2C_H */
