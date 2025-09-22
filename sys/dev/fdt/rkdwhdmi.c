/* $OpenBSD: rkdwhdmi.c,v 1.9 2025/03/24 11:26:59 jmatthew Exp $ */
/* $NetBSD: rk_dwhdmi.c,v 1.4 2019/12/17 18:26:36 jakllsch Exp $ */

/*-
 * Copyright (c) 2019 Jared D. McNeill <jmcneill@invisible.ca>
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
#include <sys/kernel.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/fdt.h>

#include <drm/drm_crtc_helper.h>
#include <drm/drm_atomic_helper.h>

#include <dev/ic/dwhdmi.h>

#define	RK3399_GRF_SOC_CON20		0x6250
#define	 HDMI_LCDC_SEL			(1 << 6)

const struct dwhdmi_mpll_config rkdwhdmi_mpll_config[] = {
	{ 40000,	0x00b3, 0x0000, 0x0018 },
	{ 65000,	0x0072, 0x0001, 0x0028 },
	{ 66000,	0x013e, 0x0003, 0x0038 },
	{ 83500,	0x0072, 0x0001, 0x0028 },
	{ 146250,	0x0051, 0x0002, 0x0038 },
	{ 148500,	0x0051, 0x0003, 0x0000 },
	{ 272000,	0x0040, 0x0003, 0x0000 },
	{ 340000,	0x0040, 0x0003, 0x0000 },
	{ 0,		0x0051, 0x0003, 0x0000 },
};

const struct dwhdmi_phy_config rkdwhdmi_phy_config[] = {
	{ 74250,	0x8009, 0x0004, 0x0272 },
	{ 148500,	0x802b, 0x0004, 0x028d },
	{ 297000,	0x8039, 0x0005, 0x028d },
	{ 594000,	0x8039, 0x0000, 0x019d },
	{ 0,		0x0000, 0x0000, 0x0000 }
};

struct rkdwhdmi_softc {
	struct dwhdmi_softc	sc_base;
	int			sc_node;
	int			sc_clk_vpll;

	struct drm_display_mode	sc_curmode;
	struct drm_encoder	sc_encoder;
	struct regmap		*sc_grf;

	int			sc_activated;

	struct device_ports	sc_ports;
};

#define	to_rkdwhdmi_softc(x)	container_of(x, struct rkdwhdmi_softc, sc_base)
#define	to_rkdwhdmi_encoder(x)	container_of(x, struct rkdwhdmi_softc, sc_encoder)

int rkdwhdmi_match(struct device *, void *, void *);
void rkdwhdmi_attach(struct device *, struct device *, void *);

void rkdwhdmi_select_input(struct rkdwhdmi_softc *, u_int);
void rkdwhdmi_encoder_enable(struct drm_encoder *);

int rkdwhdmi_ep_activate(void *, struct endpoint *, void *);
void *rkdwhdmi_ep_get_cookie(void *, struct endpoint *);

void rkdwhdmi_enable(struct dwhdmi_softc *);
void rkdwhdmi_mode_set(struct dwhdmi_softc *, const struct drm_display_mode *,
    const struct drm_display_mode *);
enum drm_mode_status rkdwhdmi_mode_valid(struct dwhdmi_softc *,
    const struct drm_display_mode *);

const struct cfattach rkdwhdmi_ca = {
	sizeof (struct rkdwhdmi_softc), rkdwhdmi_match, rkdwhdmi_attach
};

struct cfdriver rkdwhdmi_cd = {
	NULL, "rkdwhdmi", DV_DULL
};

int
rkdwhdmi_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "rockchip,rk3399-dw-hdmi");
}

void
rkdwhdmi_attach(struct device *parent, struct device *self, void *aux)
{
	struct rkdwhdmi_softc *sc = (struct rkdwhdmi_softc *)self;
	struct fdt_attach_args *faa = aux;
	uint32_t grf;
	uint32_t phandle;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	pinctrl_byname(faa->fa_node, "default");

	clock_enable(faa->fa_node, "iahb");
	clock_enable(faa->fa_node, "isfr");
	clock_enable(faa->fa_node, "vpll");
	clock_enable(faa->fa_node, "grf");
	clock_enable(faa->fa_node, "cec");

	sc->sc_base.sc_reg_width =
	    OF_getpropint(faa->fa_node, "reg-io-width", 4);

	sc->sc_base.sc_bst = faa->fa_iot;
	if (bus_space_map(sc->sc_base.sc_bst, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_base.sc_bsh)) {
		printf(": can't map registers\n");
		return;
	}

	sc->sc_node = faa->fa_node;
	sc->sc_clk_vpll = OF_getindex(faa->fa_node, "vpll", "clock-names");

	grf = OF_getpropint(faa->fa_node, "rockchip,grf", 0);
	sc->sc_grf = regmap_byphandle(grf);
	if (sc->sc_grf == NULL) {
		printf(": can't get grf\n");
		return;
	}

	printf(": HDMI TX\n");

	phandle = OF_getpropint(faa->fa_node, "ddc-i2c-bus", 0);
	sc->sc_base.sc_ic = i2c_byphandle(phandle);
	if (phandle && sc->sc_base.sc_ic == NULL) {
		printf("%s: couldn't find external I2C master\n",
		    self->dv_xname);
		return;
	}

	sc->sc_base.sc_flags |= DWHDMI_USE_INTERNAL_PHY;
	sc->sc_base.sc_detect = dwhdmi_phy_detect;
	sc->sc_base.sc_enable = rkdwhdmi_enable;
	sc->sc_base.sc_disable = dwhdmi_phy_disable;
	sc->sc_base.sc_mode_set = rkdwhdmi_mode_set;
	sc->sc_base.sc_mode_valid = rkdwhdmi_mode_valid;
	sc->sc_base.sc_mpll_config = rkdwhdmi_mpll_config;
	sc->sc_base.sc_phy_config = rkdwhdmi_phy_config;

	if (dwhdmi_attach(&sc->sc_base) != 0) {
		printf("%s: failed to attach driver\n", self->dv_xname);
		return;
	}

	sc->sc_ports.dp_node = faa->fa_node;
	sc->sc_ports.dp_cookie = sc;
	sc->sc_ports.dp_ep_activate = rkdwhdmi_ep_activate;
	sc->sc_ports.dp_ep_get_cookie = rkdwhdmi_ep_get_cookie;
	device_ports_register(&sc->sc_ports, EP_DRM_ENCODER);

#ifdef notyet
	fdtbus_register_dai_controller(self, phandle, &rkdwhdmi_dai_funcs);
#endif
}

void
rkdwhdmi_select_input(struct rkdwhdmi_softc *sc, u_int crtc_index)
{
	const uint32_t write_mask = HDMI_LCDC_SEL << 16;
	const uint32_t write_val = crtc_index == 0 ? HDMI_LCDC_SEL : 0;

	regmap_write_4(sc->sc_grf, RK3399_GRF_SOC_CON20, write_mask | write_val);
}

void
rkdwhdmi_encoder_enable(struct drm_encoder *encoder)
{
	struct rkdwhdmi_softc * const sc = to_rkdwhdmi_encoder(encoder);
	const u_int crtc_index = drm_crtc_index(encoder->crtc);

	rkdwhdmi_select_input(sc, crtc_index);
}

struct drm_encoder_funcs rkdwhdmi_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

struct drm_encoder_helper_funcs rkdwhdmi_encoder_helper_funcs = {
	.enable = rkdwhdmi_encoder_enable,
};

int
rkdwhdmi_ep_activate(void *cookie, struct endpoint *ep, void *arg)
{
	struct rkdwhdmi_softc *sc = cookie;
	struct drm_crtc *crtc = NULL;
	struct endpoint *rep;
	int error;

	if (sc->sc_activated)
		return 0;

	rep = endpoint_remote(ep);
	if (rep && rep->ep_type == EP_DRM_CRTC)
		crtc = endpoint_get_cookie(rep);
	if (crtc == NULL)
		return EINVAL;

	sc->sc_encoder.possible_crtcs = 0x3; /* XXX */
	drm_encoder_init(crtc->dev, &sc->sc_encoder, &rkdwhdmi_encoder_funcs,
	    DRM_MODE_ENCODER_TMDS, NULL);
	drm_encoder_helper_add(&sc->sc_encoder, &rkdwhdmi_encoder_helper_funcs);

	sc->sc_base.sc_connector.base.connector_type = DRM_MODE_CONNECTOR_HDMIA;
	error = dwhdmi_bind(&sc->sc_base, &sc->sc_encoder);
	if (error != 0)
		return error;

	sc->sc_activated = 1;
	return 0;
}

void *
rkdwhdmi_ep_get_cookie(void *cookie, struct endpoint *ep)
{
	struct rkdwhdmi_softc *sc = cookie;
	return &sc->sc_encoder;
}

void
rkdwhdmi_enable(struct dwhdmi_softc *dsc)
{
	dwhdmi_phy_enable(dsc);
}

void
rkdwhdmi_mode_set(struct dwhdmi_softc *dsc,
    const struct drm_display_mode *mode, const struct drm_display_mode *adjusted_mode)
{
	struct rkdwhdmi_softc *sc = to_rkdwhdmi_softc(dsc);
	int error;

	if (sc->sc_clk_vpll != -1) {
		error = clock_set_frequency(sc->sc_node, "vpll",
		    adjusted_mode->clock * 1000);
		if (error != 0)
			printf("%s: couldn't set pixel clock to %u Hz: %d\n",
			    dsc->sc_dev.dv_xname, adjusted_mode->clock * 1000,
			    error);
	}

	dwhdmi_phy_mode_set(dsc, mode, adjusted_mode);
}

enum drm_mode_status
rkdwhdmi_mode_valid(struct dwhdmi_softc *dsc, const struct drm_display_mode *mode)
{
	struct rkdwhdmi_softc *sc = to_rkdwhdmi_softc(dsc);
	int i;

	for (i = 0; sc->sc_base.sc_mpll_config[i].pixel_clock != 0; i++)
		if (mode->clock == sc->sc_base.sc_mpll_config[i].pixel_clock)
			return MODE_OK;

	return MODE_BAD;
}

#ifdef notyet

static audio_dai_tag_t
rkdwhdmi_dai_get_tag(device_t dev, const void *data, size_t len)
{
	struct rkdwhdmi_softc * const sc = device_private(dev);

	if (len != 4)
		return NULL;

	return &sc->sc_base.sc_dai;
}

static struct fdtbus_dai_controller_func rkdwhdmi_dai_funcs = {
	.get_tag = rkdwhdmi_dai_get_tag
};

#endif
