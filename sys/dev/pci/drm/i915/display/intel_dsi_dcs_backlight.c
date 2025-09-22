/*
 * Copyright © 2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Author: Deepak M <m.deepak at intel.com>
 */

#include <drm/drm_mipi_dsi.h>
#include <video/mipi_display.h>

#include "i915_drv.h"
#include "intel_display_types.h"
#include "intel_dsi.h"
#include "intel_dsi_dcs_backlight.h"

#define CONTROL_DISPLAY_BCTRL		(1 << 5)
#define CONTROL_DISPLAY_DD		(1 << 3)
#define CONTROL_DISPLAY_BL		(1 << 2)

#define POWER_SAVE_OFF			(0 << 0)
#define POWER_SAVE_LOW			(1 << 0)
#define POWER_SAVE_MEDIUM		(2 << 0)
#define POWER_SAVE_HIGH			(3 << 0)
#define POWER_SAVE_OUTDOOR_MODE		(4 << 0)

#define PANEL_PWM_MAX_VALUE		0xFF

static u32 dcs_get_backlight(struct intel_connector *connector, enum pipe unused)
{
	struct intel_encoder *encoder = intel_attached_encoder(connector);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	struct intel_panel *panel = &connector->panel;
	struct mipi_dsi_device *dsi_device;
	u8 data[2] = {};
	enum port port;
	size_t len = panel->backlight.max > U8_MAX ? 2 : 1;

	for_each_dsi_port(port, panel->vbt.dsi.bl_ports) {
		dsi_device = intel_dsi->dsi_hosts[port]->device;
		mipi_dsi_dcs_read(dsi_device, MIPI_DCS_GET_DISPLAY_BRIGHTNESS,
				  &data, len);
		break;
	}

	return (data[1] << 8) | data[0];
}

static void dcs_set_backlight(const struct drm_connector_state *conn_state, u32 level)
{
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(to_intel_encoder(conn_state->best_encoder));
	struct intel_panel *panel = &to_intel_connector(conn_state->connector)->panel;
	struct mipi_dsi_device *dsi_device;
	u8 data[2] = {};
	enum port port;
	size_t len = panel->backlight.max > U8_MAX ? 2 : 1;
	unsigned long mode_flags;

	if (len == 1) {
		data[0] = level;
	} else {
		data[0] = level >> 8;
		data[1] = level;
	}

	for_each_dsi_port(port, panel->vbt.dsi.bl_ports) {
		dsi_device = intel_dsi->dsi_hosts[port]->device;
		mode_flags = dsi_device->mode_flags;
		dsi_device->mode_flags &= ~MIPI_DSI_MODE_LPM;
		mipi_dsi_dcs_write(dsi_device, MIPI_DCS_SET_DISPLAY_BRIGHTNESS,
				   &data, len);
		dsi_device->mode_flags = mode_flags;
	}
}

static void dcs_disable_backlight(const struct drm_connector_state *conn_state, u32 level)
{
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(to_intel_encoder(conn_state->best_encoder));
	struct intel_panel *panel = &to_intel_connector(conn_state->connector)->panel;
	struct mipi_dsi_device *dsi_device;
	enum port port;

	dcs_set_backlight(conn_state, 0);

	for_each_dsi_port(port, panel->vbt.dsi.cabc_ports) {
		u8 cabc = POWER_SAVE_OFF;

		dsi_device = intel_dsi->dsi_hosts[port]->device;
		mipi_dsi_dcs_write(dsi_device, MIPI_DCS_WRITE_POWER_SAVE,
				   &cabc, sizeof(cabc));
	}

	for_each_dsi_port(port, panel->vbt.dsi.bl_ports) {
		u8 ctrl = 0;

		dsi_device = intel_dsi->dsi_hosts[port]->device;

		mipi_dsi_dcs_read(dsi_device, MIPI_DCS_GET_CONTROL_DISPLAY,
				  &ctrl, sizeof(ctrl));

		ctrl &= ~CONTROL_DISPLAY_BL;
		ctrl &= ~CONTROL_DISPLAY_DD;
		ctrl &= ~CONTROL_DISPLAY_BCTRL;

		mipi_dsi_dcs_write(dsi_device, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				   &ctrl, sizeof(ctrl));
	}
}

static void dcs_enable_backlight(const struct intel_crtc_state *crtc_state,
				 const struct drm_connector_state *conn_state, u32 level)
{
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(to_intel_encoder(conn_state->best_encoder));
	struct intel_panel *panel = &to_intel_connector(conn_state->connector)->panel;
	struct mipi_dsi_device *dsi_device;
	enum port port;

	for_each_dsi_port(port, panel->vbt.dsi.bl_ports) {
		u8 ctrl = 0;

		dsi_device = intel_dsi->dsi_hosts[port]->device;

		mipi_dsi_dcs_read(dsi_device, MIPI_DCS_GET_CONTROL_DISPLAY,
				  &ctrl, sizeof(ctrl));

		ctrl |= CONTROL_DISPLAY_BL;
		ctrl |= CONTROL_DISPLAY_DD;
		ctrl |= CONTROL_DISPLAY_BCTRL;

		mipi_dsi_dcs_write(dsi_device, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				   &ctrl, sizeof(ctrl));
	}

	for_each_dsi_port(port, panel->vbt.dsi.cabc_ports) {
		u8 cabc = POWER_SAVE_MEDIUM;

		dsi_device = intel_dsi->dsi_hosts[port]->device;
		mipi_dsi_dcs_write(dsi_device, MIPI_DCS_WRITE_POWER_SAVE,
				   &cabc, sizeof(cabc));
	}

	dcs_set_backlight(conn_state, level);
}

static int dcs_setup_backlight(struct intel_connector *connector,
			       enum pipe unused)
{
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct intel_panel *panel = &connector->panel;

	if (panel->vbt.backlight.brightness_precision_bits > 8)
		panel->backlight.max = (1 << panel->vbt.backlight.brightness_precision_bits) - 1;
	else
		panel->backlight.max = PANEL_PWM_MAX_VALUE;

	panel->backlight.level = panel->backlight.max;

	drm_dbg_kms(&i915->drm,
		    "[CONNECTOR:%d:%s] Using DCS for backlight control\n",
		    connector->base.base.id, connector->base.name);

	return 0;
}

static const struct intel_panel_bl_funcs dcs_bl_funcs = {
	.setup = dcs_setup_backlight,
	.enable = dcs_enable_backlight,
	.disable = dcs_disable_backlight,
	.set = dcs_set_backlight,
	.get = dcs_get_backlight,
};

int intel_dsi_dcs_init_backlight_funcs(struct intel_connector *intel_connector)
{
	struct drm_device *dev = intel_connector->base.dev;
	struct intel_encoder *encoder = intel_attached_encoder(intel_connector);
	struct intel_panel *panel = &intel_connector->panel;

	if (panel->vbt.backlight.type != INTEL_BACKLIGHT_DSI_DCS)
		return -ENODEV;

	if (drm_WARN_ON(dev, encoder->type != INTEL_OUTPUT_DSI))
		return -EINVAL;

	panel->backlight.funcs = &dcs_bl_funcs;

	return 0;
}
