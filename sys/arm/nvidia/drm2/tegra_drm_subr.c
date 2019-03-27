/*-
 * Copyright (c) 2015 Michal Meloun
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>

#include <machine/bus.h>

#include <dev/extres/clk/clk.h>
#include <dev/drm2/drmP.h>
#include <dev/drm2/drm_crtc.h>
#include <dev/drm2/drm_crtc_helper.h>
#include <dev/drm2/drm_edid.h>
#include <dev/drm2/drm_fb_helper.h>
#include <dev/gpio/gpiobusvar.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/nvidia/drm2/tegra_drm.h>

#include <gnu/dts/include/dt-bindings/gpio/gpio.h>

int
tegra_drm_connector_get_modes(struct drm_connector *connector)
{
	struct tegra_drm_encoder *output;
	struct edid *edid = NULL;
	int rv;

	output = container_of(connector, struct tegra_drm_encoder,
	     connector);

	/* Panel is first */
	if (output->panel != NULL) {
		/* XXX panel parsing */
		return (0);
	}

	/* static EDID is second*/
	edid = output->edid;

	/* EDID from monitor is last */
	if (edid == NULL)
		edid = drm_get_edid(connector, output->ddc);

	if (edid == NULL)
		return (0);

	/* Process EDID */
	drm_mode_connector_update_edid_property(connector, edid);
	rv = drm_add_edid_modes(connector, edid);
	drm_edid_to_eld(connector, edid);
	return (rv);
}

struct drm_encoder *
tegra_drm_connector_best_encoder(struct drm_connector *connector)
{
	struct tegra_drm_encoder *output;

	output = container_of(connector, struct tegra_drm_encoder,
	     connector);

	return &(output->encoder);
}

enum drm_connector_status
tegra_drm_connector_detect(struct drm_connector *connector, bool force)
{
	struct tegra_drm_encoder *output;
	bool active;
	int rv;

	output = container_of(connector, struct tegra_drm_encoder,
	     connector);
	if (output->gpio_hpd == NULL) {
		return ((output->panel != NULL) ?
		    connector_status_connected:
		    connector_status_disconnected);
	}

	rv = gpio_pin_is_active(output->gpio_hpd, &active);
	if (rv  != 0) {
		device_printf(output->dev, " GPIO read failed: %d\n", rv);
		return (connector_status_unknown);
	}

	return (active ?
	    connector_status_connected : connector_status_disconnected);
}

int
tegra_drm_encoder_attach(struct tegra_drm_encoder *output, phandle_t node)
{
	int rv;
	phandle_t ddc;

	/* XXX parse output panel here */

	rv = OF_getencprop_alloc(node, "nvidia,edid",
	    (void **)&output->edid);

	/* EDID exist but have invalid size */
	if ((rv >= 0) && (rv != sizeof(struct edid))) {
		device_printf(output->dev,
		    "Malformed \"nvidia,edid\" property\n");
		if (output->edid != NULL)
			free(output->edid, M_OFWPROP);
		return (ENXIO);
	}

	gpio_pin_get_by_ofw_property(output->dev, node, "nvidia,hpd-gpio",
	    &output->gpio_hpd);
	ddc = 0;
	OF_getencprop(node, "nvidia,ddc-i2c-bus", &ddc, sizeof(ddc));
	if (ddc > 0)
		output->ddc = OF_device_from_xref(ddc);
	if ((output->edid == NULL) && (output->ddc == NULL))
		return (ENXIO);

	if (output->gpio_hpd != NULL) {
		output->connector.polled =
//		    DRM_CONNECTOR_POLL_HPD;
		    DRM_CONNECTOR_POLL_DISCONNECT |
		    DRM_CONNECTOR_POLL_CONNECT;
	}

	return (0);
}

int tegra_drm_encoder_init(struct tegra_drm_encoder *output,
    struct tegra_drm *drm)
{

	if (output->panel) {
		/* attach panel */
	}
	return (0);
}

int tegra_drm_encoder_exit(struct tegra_drm_encoder *output,
    struct tegra_drm *drm)
{

	if (output->panel) {
		/* detach panel */
	}
	return (0);
}
