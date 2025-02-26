/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, Fuzhou Rockchip Electronics Co., Ltd
 *
 * Authors: Guochun Huang <hero.huang@rock-chips.com>
 *          Heiko Stuebner <heiko.stuebner@cherry.de>
 */

#ifndef __DW_MIPI_DSI2__
#define __DW_MIPI_DSI2__

#include <linux/regmap.h>
#include <linux/types.h>

#include <drm/drm_atomic.h>
#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_modes.h>

struct drm_display_mode;
struct drm_encoder;
struct dw_mipi_dsi2;
struct mipi_dsi_device;
struct platform_device;

enum dw_mipi_dsi2_phy_type {
	DW_MIPI_DSI2_DPHY,
	DW_MIPI_DSI2_CPHY,
};

struct dw_mipi_dsi2_phy_iface {
	int ppi_width;
	enum dw_mipi_dsi2_phy_type phy_type;
};

struct dw_mipi_dsi2_phy_timing {
	u32 data_hs2lp;
	u32 data_lp2hs;
};

struct dw_mipi_dsi2_phy_ops {
	int (*init)(void *priv_data);
	void (*power_on)(void *priv_data);
	void (*power_off)(void *priv_data);
	void (*get_interface)(void *priv_data, struct dw_mipi_dsi2_phy_iface *iface);
	int (*get_lane_mbps)(void *priv_data,
			     const struct drm_display_mode *mode,
			     unsigned long mode_flags, u32 lanes, u32 format,
			     unsigned int *lane_mbps);
	int (*get_timing)(void *priv_data, unsigned int lane_mbps,
			  struct dw_mipi_dsi2_phy_timing *timing);
	int (*get_esc_clk_rate)(void *priv_data, unsigned int *esc_clk_rate);
};

struct dw_mipi_dsi2_host_ops {
	int (*attach)(void *priv_data,
		      struct mipi_dsi_device *dsi);
	int (*detach)(void *priv_data,
		      struct mipi_dsi_device *dsi);
};

struct dw_mipi_dsi2_plat_data {
	struct regmap *regmap;
	unsigned int max_data_lanes;

	enum drm_mode_status (*mode_valid)(void *priv_data,
					   const struct drm_display_mode *mode,
					   unsigned long mode_flags,
					   u32 lanes, u32 format);

	bool (*mode_fixup)(void *priv_data, const struct drm_display_mode *mode,
			   struct drm_display_mode *adjusted_mode);

	u32 *(*get_input_bus_fmts)(void *priv_data,
				   struct drm_bridge *bridge,
				   struct drm_bridge_state *bridge_state,
				   struct drm_crtc_state *crtc_state,
				   struct drm_connector_state *conn_state,
				   u32 output_fmt,
				   unsigned int *num_input_fmts);

	const struct dw_mipi_dsi2_phy_ops *phy_ops;
	const struct dw_mipi_dsi2_host_ops *host_ops;

	void *priv_data;
};

struct dw_mipi_dsi2 *dw_mipi_dsi2_probe(struct platform_device *pdev,
					const struct dw_mipi_dsi2_plat_data *plat_data);
void dw_mipi_dsi2_remove(struct dw_mipi_dsi2 *dsi2);
int dw_mipi_dsi2_bind(struct dw_mipi_dsi2 *dsi2, struct drm_encoder *encoder);
void dw_mipi_dsi2_unbind(struct dw_mipi_dsi2 *dsi2);

#endif /* __DW_MIPI_DSI2__ */
