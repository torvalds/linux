// SPDX-License-Identifier: MIT
/*
 * Copyright © 2018 Intel Corporation
 */

#include <drm/drm_mipi_dsi.h>

#include "i915_drv.h"
#include "intel_dsi.h"
#include "intel_panel.h"

void intel_dsi_wait_panel_power_cycle(struct intel_dsi *intel_dsi)
{
	ktime_t panel_power_on_time;
	s64 panel_power_off_duration;

	panel_power_on_time = ktime_get_boottime();
	panel_power_off_duration = ktime_ms_delta(panel_power_on_time,
						  intel_dsi->panel_power_off_time);

	if (panel_power_off_duration < (s64)intel_dsi->panel_pwr_cycle_delay)
		drm_msleep(intel_dsi->panel_pwr_cycle_delay - panel_power_off_duration);
}

void intel_dsi_shutdown(struct intel_encoder *encoder)
{
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);

	intel_dsi_wait_panel_power_cycle(intel_dsi);
}

int intel_dsi_bitrate(const struct intel_dsi *intel_dsi)
{
	int bpp = mipi_dsi_pixel_format_to_bpp(intel_dsi->pixel_format);

	if (WARN_ON(bpp < 0))
		bpp = 16;

	return intel_dsi->pclk * bpp / intel_dsi->lane_count;
}

int intel_dsi_tlpx_ns(const struct intel_dsi *intel_dsi)
{
	switch (intel_dsi->escape_clk_div) {
	default:
	case 0:
		return 50;
	case 1:
		return 100;
	case 2:
		return 200;
	}
}

int intel_dsi_get_modes(struct drm_connector *connector)
{
	return intel_panel_get_modes(to_intel_connector(connector));
}

enum drm_mode_status intel_dsi_mode_valid(struct drm_connector *connector,
					  struct drm_display_mode *mode)
{
	struct drm_i915_private *dev_priv = to_i915(connector->dev);
	struct intel_connector *intel_connector = to_intel_connector(connector);
	const struct drm_display_mode *fixed_mode =
		intel_panel_fixed_mode(intel_connector, mode);
	int max_dotclk = to_i915(connector->dev)->display.cdclk.max_dotclk_freq;
	enum drm_mode_status status;

	drm_dbg_kms(&dev_priv->drm, "\n");

	status = intel_panel_mode_valid(intel_connector, mode);
	if (status != MODE_OK)
		return status;

	if (fixed_mode->clock > max_dotclk)
		return MODE_CLOCK_HIGH;

	return intel_mode_valid_max_plane_size(dev_priv, mode, false);
}

struct intel_dsi_host *intel_dsi_host_init(struct intel_dsi *intel_dsi,
					   const struct mipi_dsi_host_ops *funcs,
					   enum port port)
{
	struct intel_dsi_host *host;
	struct mipi_dsi_device *device;

	host = kzalloc(sizeof(*host), GFP_KERNEL);
	if (!host)
		return NULL;

	host->base.ops = funcs;
	host->intel_dsi = intel_dsi;
	host->port = port;

	/*
	 * We should call mipi_dsi_host_register(&host->base) here, but we don't
	 * have a host->dev, and we don't have OF stuff either. So just use the
	 * dsi framework as a library and hope for the best. Create the dsi
	 * devices by ourselves here too. Need to be careful though, because we
	 * don't initialize any of the driver model devices here.
	 */
	device = kzalloc(sizeof(*device), GFP_KERNEL);
	if (!device) {
		kfree(host);
		return NULL;
	}

	device->host = &host->base;
	host->device = device;

	return host;
}

enum drm_panel_orientation
intel_dsi_get_panel_orientation(struct intel_connector *connector)
{
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	enum drm_panel_orientation orientation;

	orientation = connector->panel.vbt.dsi.orientation;
	if (orientation != DRM_MODE_PANEL_ORIENTATION_UNKNOWN)
		return orientation;

	orientation = dev_priv->display.vbt.orientation;
	if (orientation != DRM_MODE_PANEL_ORIENTATION_UNKNOWN)
		return orientation;

	return DRM_MODE_PANEL_ORIENTATION_NORMAL;
}
