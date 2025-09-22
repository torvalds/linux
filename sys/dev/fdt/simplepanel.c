/*	$OpenBSD: simplepanel.c,v 1.6 2021/11/07 15:59:09 patrick Exp $	*/
/*
 * Copyright (c) 2020 Patrick Wildt <patrick@blueri.se>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/ofw_regulator.h>

#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

const struct drm_display_mode boe_nv140fhmn49_mode = {
	.clock = 148500,
	.hdisplay = 1920,
	.hsync_start = 1920 + 48,
	.hsync_end = 1920 + 48 + 32,
	.htotal = 2200,
	.vdisplay = 1080,
	.vsync_start = 1080 + 3,
	.vsync_end = 1080 + 3 + 5,
	.vtotal = 1125,
};

int simplepanel_match(struct device *, void *, void *);
void simplepanel_attach(struct device *, struct device *, void *);

struct simplepanel_softc {
	struct device		sc_dev;
	struct device_ports	sc_ports;
	struct drm_panel	sc_panel;
	const struct drm_display_mode *sc_mode;
};

const struct cfattach	simplepanel_ca = {
	sizeof (struct simplepanel_softc),
	simplepanel_match, simplepanel_attach
};

struct cfdriver simplepanel_cd = {
	NULL, "simplepanel", DV_DULL
};

void	*simplepanel_ep_get_cookie(void *, struct endpoint *);
int	simplepanel_get_modes(struct drm_panel *, struct drm_connector *);

struct drm_panel_funcs simplepanel_funcs = {
	.get_modes = simplepanel_get_modes
};

int
simplepanel_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "simple-panel") ||
	    OF_is_compatible(faa->fa_node, "boe,nv140fhmn49"));
}

void
simplepanel_attach(struct device *parent, struct device *self, void *aux)
{
	struct simplepanel_softc *sc = (struct simplepanel_softc *)self;
	struct fdt_attach_args *faa = aux;
	uint32_t power_supply;
	uint32_t *gpios;
	int connector_type = DRM_MODE_CONNECTOR_Unknown;
	int len;

	pinctrl_byname(faa->fa_node, "default");

	power_supply = OF_getpropint(faa->fa_node, "power-supply", 0);
	if (power_supply)
		regulator_enable(power_supply);

	len = OF_getproplen(faa->fa_node, "enable-gpios");
	if (len > 0) {
		gpios = malloc(len, M_TEMP, M_WAITOK);
		OF_getpropintarray(faa->fa_node, "enable-gpios", gpios, len);
		gpio_controller_config_pin(&gpios[0], GPIO_CONFIG_OUTPUT);
		gpio_controller_set_pin(&gpios[0], 1);
		free(gpios, M_TEMP, len);
	}

	if (OF_is_compatible(faa->fa_node, "boe,nv140fhmn49")) {
		sc->sc_mode = &boe_nv140fhmn49_mode;
		connector_type = DRM_MODE_CONNECTOR_eDP;
	}

	drm_panel_init(&sc->sc_panel, self, &simplepanel_funcs,
	    connector_type);
	drm_panel_add(&sc->sc_panel);

	printf("\n");

	sc->sc_ports.dp_node = faa->fa_node;
	sc->sc_ports.dp_cookie = sc;
	sc->sc_ports.dp_ep_get_cookie = simplepanel_ep_get_cookie;
	device_ports_register(&sc->sc_ports, EP_DRM_PANEL);
}

void *
simplepanel_ep_get_cookie(void *cookie, struct endpoint *ep)
{
	struct simplepanel_softc *sc = cookie;
	return &sc->sc_panel;
}

static inline struct simplepanel_softc *
to_simplepanel(struct drm_panel *panel)
{
	return container_of(panel, struct simplepanel_softc, sc_panel);
}

int
simplepanel_get_modes(struct drm_panel *panel, struct drm_connector *connector)
{
	struct simplepanel_softc *sc = to_simplepanel(panel);
	struct drm_display_mode *mode;

	if (sc->sc_mode == NULL)
		return 0;

	mode = drm_mode_duplicate(connector->dev, sc->sc_mode);
	if (mode == NULL)
		return 0;
	mode->type |= DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;

	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);
	return 1;
}
