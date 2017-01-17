/*
 * Copyright (C) 2011 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __DW_HDMI__
#define __DW_HDMI__

#include <drm/drmP.h>

struct dw_hdmi;

enum {
	DW_HDMI_RES_8,
	DW_HDMI_RES_10,
	DW_HDMI_RES_12,
	DW_HDMI_RES_MAX,
};

enum dw_hdmi_devtype {
	IMX6Q_HDMI,
	IMX6DL_HDMI,
	RK3288_HDMI,
};

struct dw_hdmi_mpll_config {
	unsigned long mpixelclock;
	struct {
		u16 cpce;
		u16 gmp;
	} res[DW_HDMI_RES_MAX];
};

struct dw_hdmi_curr_ctrl {
	unsigned long mpixelclock;
	u16 curr[DW_HDMI_RES_MAX];
};

struct dw_hdmi_phy_config {
	unsigned long mpixelclock;
	u16 sym_ctr;    /*clock symbol and transmitter control*/
	u16 term;       /*transmission termination value*/
	u16 vlev_ctr;   /* voltage level control */
};

struct dw_hdmi_plat_data {
	enum dw_hdmi_devtype dev_type;
	const struct dw_hdmi_mpll_config *mpll_cfg;
	const struct dw_hdmi_curr_ctrl *cur_ctr;
	const struct dw_hdmi_phy_config *phy_config;
	enum drm_mode_status (*mode_valid)(struct drm_connector *connector,
					   struct drm_display_mode *mode);
};

void dw_hdmi_unbind(struct device *dev);
int dw_hdmi_bind(struct platform_device *pdev, struct drm_encoder *encoder,
		 const struct dw_hdmi_plat_data *plat_data);

void dw_hdmi_set_sample_rate(struct dw_hdmi *hdmi, unsigned int rate);
void dw_hdmi_audio_enable(struct dw_hdmi *hdmi);
void dw_hdmi_audio_disable(struct dw_hdmi *hdmi);

#endif /* __IMX_HDMI_H__ */
