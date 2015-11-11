/* exynos_drm.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#ifndef _EXYNOS_DRM_H_
#define _EXYNOS_DRM_H_

#include <uapi/drm/exynos_drm.h>
#include <video/videomode.h>

/**
 * A structure for lcd panel information.
 *
 * @timing: default video mode for initializing
 * @width_mm: physical size of lcd width.
 * @height_mm: physical size of lcd height.
 */
struct exynos_drm_panel_info {
	struct videomode vm;
	u32 width_mm;
	u32 height_mm;
};

/**
 * Platform Specific Structure for DRM based FIMD.
 *
 * @panel: default panel info for initializing
 * @default_win: default window layer number to be used for UI.
 * @bpp: default bit per pixel.
 */
struct exynos_drm_fimd_pdata {
	struct exynos_drm_panel_info panel;
	u32				vidcon0;
	u32				vidcon1;
	unsigned int			default_win;
	unsigned int			bpp;
};

/**
 * Platform Specific Structure for DRM based HDMI.
 *
 * @hdmi_dev: device point to specific hdmi driver.
 * @mixer_dev: device point to specific mixer driver.
 *
 * this structure is used for common hdmi driver and each device object
 * would be used to access specific device driver(hdmi or mixer driver)
 */
struct exynos_drm_common_hdmi_pd {
	struct device *hdmi_dev;
	struct device *mixer_dev;
};

/**
 * Platform Specific Structure for DRM based HDMI core.
 *
 * @is_v13: set if hdmi version 13 is.
 * @cfg_hpd: function pointer to configure hdmi hotplug detection pin
 * @get_hpd: function pointer to get value of hdmi hotplug detection pin
 */
struct exynos_drm_hdmi_pdata {
	bool is_v13;
	void (*cfg_hpd)(bool external);
	int (*get_hpd)(void);
};

/**
 * Platform Specific Structure for DRM based IPP.
 *
 * @inv_pclk: if set 1. invert pixel clock
 * @inv_vsync: if set 1. invert vsync signal for wb
 * @inv_href: if set 1. invert href signal
 * @inv_hsync: if set 1. invert hsync signal for wb
 */
struct exynos_drm_ipp_pol {
	unsigned int inv_pclk;
	unsigned int inv_vsync;
	unsigned int inv_href;
	unsigned int inv_hsync;
};

/**
 * Platform Specific Structure for DRM based FIMC.
 *
 * @pol: current hardware block polarity settings.
 * @clk_rate: current hardware clock rate.
 */
struct exynos_drm_fimc_pdata {
	struct exynos_drm_ipp_pol pol;
	int clk_rate;
};

#endif	/* _EXYNOS_DRM_H_ */
