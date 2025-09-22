/* $OpenBSD: rkanxdp.c,v 1.6 2024/01/16 23:37:50 jsg Exp $ */
/* $NetBSD: rk_anxdp.c,v 1.2 2020/01/04 12:08:32 jmcneill Exp $ */
/*-
 * Copyright (c) 2019 Jonathan A. Kollasch <jakllsch@kollasch.net>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/fdt.h>

#include <drm/drm_crtc_helper.h>
#include <drm/drm_atomic_helper.h>

#include <dev/ic/anxdp.h>

#define	RK3399_GRF_SOC_CON20		0x6250
#define	 EDP_LCDC_SEL				(1 << 5)

enum {
	ANXDP_PORT_INPUT = 0,
	ANXDP_PORT_OUTPUT = 1,
};

struct rkanxdp_softc {
	struct anxdp_softc	sc_base;

	struct drm_encoder	sc_encoder;
	struct drm_display_mode	sc_curmode;
	struct regmap		*sc_grf;

	int			sc_activated;

	struct device_ports	sc_ports;
};

#define	to_rkanxdp_softc(x)	container_of(x, struct rkanxdp_softc, sc_base)
#define	to_rkanxdp_encoder(x)	container_of(x, struct rkanxdp_softc, sc_encoder)

int rkanxdp_match(struct device *, void *, void *);
void rkanxdp_attach(struct device *, struct device *, void *);

void rkanxdp_select_input(struct rkanxdp_softc *, u_int);
void rkanxdp_encoder_enable(struct drm_encoder *);
void rkanxdp_encoder_dpms(struct drm_encoder *, int);

int rkanxdp_ep_activate(void *, struct endpoint *, void *);
void *rkanxdp_ep_get_cookie(void *, struct endpoint *);

const struct cfattach rkanxdp_ca = {
	sizeof (struct rkanxdp_softc), rkanxdp_match, rkanxdp_attach
};

struct cfdriver rkanxdp_cd = {
	NULL, "rkanxdp", DV_DULL
};

int
rkanxdp_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "rockchip,rk3399-edp");
}

void
rkanxdp_attach(struct device *parent, struct device *self, void *aux)
{
	struct rkanxdp_softc *sc = (struct rkanxdp_softc *)self;
	struct fdt_attach_args *faa = aux;
	uint32_t grf;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	pinctrl_byname(faa->fa_node, "default");

	reset_deassert(faa->fa_node, "dp");

	clock_enable(faa->fa_node, "pclk");
	clock_enable(faa->fa_node, "dp");
	clock_enable(faa->fa_node, "grf");

	sc->sc_base.sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_base.sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_base.sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	grf = OF_getpropint(faa->fa_node, "rockchip,grf", 0);
	sc->sc_grf = regmap_byphandle(grf);
	if (sc->sc_grf == NULL) {
		printf(": can't get grf\n");
		return;
	}

	printf(": eDP TX\n");

	sc->sc_base.sc_flags |= ANXDP_FLAG_ROCKCHIP;

	if (anxdp_attach(&sc->sc_base) != 0) {
		printf("%s: failed to attach driver\n",
		    sc->sc_base.sc_dev.dv_xname);
		return;
	}

	sc->sc_ports.dp_node = faa->fa_node;
	sc->sc_ports.dp_cookie = sc;
	sc->sc_ports.dp_ep_activate = rkanxdp_ep_activate;
	sc->sc_ports.dp_ep_get_cookie = rkanxdp_ep_get_cookie;
	device_ports_register(&sc->sc_ports, EP_DRM_ENCODER);
}

void
rkanxdp_select_input(struct rkanxdp_softc *sc, u_int crtc_index)
{
	uint32_t write_mask = EDP_LCDC_SEL << 16;
	uint32_t write_val = crtc_index == 0 ? EDP_LCDC_SEL : 0;

	regmap_write_4(sc->sc_grf, RK3399_GRF_SOC_CON20, write_mask | write_val);
}

void
rkanxdp_encoder_enable(struct drm_encoder *encoder)
{
	struct rkanxdp_softc *sc = to_rkanxdp_encoder(encoder);
	u_int crtc_index = drm_crtc_index(encoder->crtc);

	rkanxdp_select_input(sc, crtc_index);
}

void
rkanxdp_encoder_dpms(struct drm_encoder *encoder, int mode)
{
	struct rkanxdp_softc *sc = to_rkanxdp_encoder(encoder);

	anxdp_dpms(&sc->sc_base, mode);
}

struct drm_encoder_funcs rkanxdp_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

struct drm_encoder_helper_funcs rkanxdp_encoder_helper_funcs = {
	.enable = rkanxdp_encoder_enable,
	.dpms = rkanxdp_encoder_dpms,
};

int
rkanxdp_ep_activate(void *cookie, struct endpoint *ep, void *arg)
{
	struct rkanxdp_softc *sc = cookie;
	struct drm_crtc *crtc = NULL;
	struct endpoint *rep;
	int error;

	if (sc->sc_activated)
		return 0;

	if (ep->ep_port->dp_reg != ANXDP_PORT_INPUT)
		return EINVAL;

	rep = endpoint_remote(ep);
	if (rep && rep->ep_type == EP_DRM_CRTC)
		crtc = endpoint_get_cookie(rep);
	if (crtc == NULL)
		return EINVAL;

	sc->sc_encoder.possible_crtcs = 0x3; /* XXX */
	drm_encoder_init(crtc->dev, &sc->sc_encoder, &rkanxdp_encoder_funcs,
	    DRM_MODE_ENCODER_TMDS, NULL);
	drm_encoder_helper_add(&sc->sc_encoder, &rkanxdp_encoder_helper_funcs);

	ep = endpoint_byreg(&sc->sc_ports, ANXDP_PORT_OUTPUT, 0);
	if (ep) {
		rep = endpoint_remote(ep);
		if (rep && rep->ep_type == EP_DRM_PANEL)
			sc->sc_base.sc_panel = endpoint_get_cookie(rep);
	}

	sc->sc_base.sc_connector.base.connector_type = DRM_MODE_CONNECTOR_eDP;
	error = anxdp_bind(&sc->sc_base, &sc->sc_encoder);
	if (error != 0)
		return error;

	sc->sc_activated = 1;
	return 0;
}

void *
rkanxdp_ep_get_cookie(void *cookie, struct endpoint *ep)
{
	struct rkanxdp_softc *sc = cookie;
	return &sc->sc_encoder;
}
