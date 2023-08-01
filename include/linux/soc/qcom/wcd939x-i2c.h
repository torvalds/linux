/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef WCD939X_I2C_H
#define WCD939X_I2C_H

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/notifier.h>
#include <linux/usb/typec.h>

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
	WCD_USBSS_CHARGER,
	WCD_USBSS_GND_MIC_SWAP_HSJ,
	WCD_USBSS_CABLE_TYPE_MAX,
};

enum wcd_usbss_cable_status {
	WCD_USBSS_CABLE_DISCONNECT,
	WCD_USBSS_CABLE_CONNECT,
};

enum wcd_usbss_sbu_switch_orientation {
	/* SBU switch orientation other than the below orientations */
	INVALID_ORIENTATION,
	/* GSBU1 for the sense switches and MG2 for the mic switches */
	GND_SBU1_ORIENTATION_B,
	/* GSBU2 for the sense switches and MG1 for the mic switches */
	GND_SBU2_ORIENTATION_A,
};

enum wcd_usbss_switch_type {
	MIN_SWITCH_TYPE_NUM = 0,
	AGND_SWITCHES = 0,
	MIC_SWITCHES = 1,
	SENSE_SWITCHES = 2,
	DPR_SWITCHES = 3,
	DNL_SWITCHES = 4,
	DP_AUXM_TO_MGX_SWITCHES = 5,
	AP_AUXP_TO_MGX_SWITCHES = 6,
	DEVICE_ENABLE = 7,
	MAX_SWITCH_TYPE_NUM = 7,
};

enum wcd_usbss_switch_state {
	USBSS_SWITCH_DISABLE,
	USBSS_SWITCH_ENABLE,
};

enum linearizer_rdac_cal_code_select {
	LINEARIZER_SOURCE_HW,
	LINEARIZER_SOURCE_SW,
};

enum wcd_usbss_notifier_events {
	WCD_USBSS_SURGE_RESET_EVENT = TYPEC_MAX_ACCESSORY,
};

#if IS_ENABLED(CONFIG_QCOM_WCD_USBSS_I2C)
int wcd_usbss_update_default_trim(void);
int wcd_usbss_switch_update(enum wcd_usbss_cable_types ctype,
			    enum wcd_usbss_cable_status status);
int wcd_usbss_reg_notifier(struct notifier_block *nb,
			 struct device_node *node);
int wcd_usbss_unreg_notifier(struct notifier_block *nb,
			   struct device_node *node);
int wcd_usbss_dpdm_switch_update(bool enable, bool eq_en);
int wcd_usbss_audio_config(bool enable, enum wcd_usbss_config_type config_type,
			   unsigned int power_mode);
enum wcd_usbss_sbu_switch_orientation wcd_usbss_get_sbu_switch_orientation(void);
int wcd_usbss_set_switch_settings_enable(enum wcd_usbss_switch_type switch_type,
					 enum wcd_usbss_switch_state switch_state);
int wcd_usbss_linearizer_rdac_cal_code_select(enum linearizer_rdac_cal_code_select source);
int wcd_usbss_set_linearizer_sw_tap(uint32_t aud_tap, uint32_t gnd_tap);
int wcd_usbss_register_update(uint32_t reg_arr[][2], bool write, size_t arr_size);
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

int wcd_usbss_update_default_trim(void)
{
	return 0;
}

enum wcd_usbss_sbu_switch_orientation wcd_usbss_get_sbu_switch_orientation(void)
{
	return INVALID_ORIENTATION;
}

int wcd_usbss_set_switch_settings_enable(enum wcd_usbss_switch_type switch_type,
					 enum wcd_usbss_switch_state switch_state)
{
	return 0;
}

int wcd_usbss_linearizer_rdac_cal_code_select(enum linearizer_rdac_cal_code_select source)
{
	return 0;
}

int wcd_usbss_set_linearizer_sw_tap(uint32_t aud_tap, uint32_t gnd_tap)
{
	return 0;
}

int wcd_usbss_register_update(uint32_t reg_arr[][2], bool write, size_t arr_size)
{
	return 0;
}
#endif /* CONFIG_QCOM_WCD_USBSS_I2C */

#endif /* WCD939X_I2C_H */
