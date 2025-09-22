/*	$OpenBSD: rkusbphy.c,v 1.8 2025/05/11 02:17:19 jcs Exp $ */

/*
 * Copyright (c) 2023 David Gwynne <dlg@openbsd.org>
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

/*
 * Rockchip USB2PHY with Innosilicon IP
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_regulator.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

/*
 * chip stuff
 */

struct rkusbphy_reg {
	bus_size_t			r_offs;
	unsigned int			r_shift;
	uint32_t			r_mask;
	uint32_t			r_set;
};

struct rkusbphy_port_regs {
	struct rkusbphy_reg		phy_enable;
};

struct rkusbphy_regs {
	struct rkusbphy_reg		clk_enable;

	struct rkusbphy_port_regs	otg;
	struct rkusbphy_port_regs	host;
};

struct rkusbphy_chip {
	bus_addr_t			 c_base_addr;
	const struct rkusbphy_regs	*c_regs;
};

/*
 * RK3128 has one USB2PHY.
 */

static const struct rkusbphy_regs rkusbphy_rk3128_usb_regs = {
	/*				shift,	mask,	set */
	.clk_enable =	{ 0x0190,	15,	0x1,	0x0 },

	.otg = {
		.phy_enable =	{ 0x017c,	0,	0x1ff,	0 },
	},

	.host = {
		.phy_enable =	{ 0x0194,	0,	0x1ff,	0 },
	},
};

static const struct rkusbphy_chip rkusbphy_rk3128[] = {
	{
		.c_base_addr = 0x17c,
		.c_regs = &rkusbphy_rk3128_usb_regs,
	},
};

/*
 * RK3399 has two USB2PHY nodes that share a GRF.
 */

static const struct rkusbphy_regs rkusbphy_rk3399_usb0_regs = {
	/*				shift,	mask,	set */
	.clk_enable =	{ 0xe450,	4,	0x1,	0x0 },

	.otg = {
		.phy_enable =	{ 0xe454,	0,	0x3,	0x2 },
	},

	.host = {
		.phy_enable =	{ 0xe458,	0,	0x3,	0x2 },
	},
};

static const struct rkusbphy_regs rkusbphy_rk3399_usb1_regs = {
	/*				shift,	mask,	set */
	.clk_enable =	{ 0xe460,	4,	0x1,	0x0 },

	.otg = {
		.phy_enable =	{ 0xe464,	0,	0x3,	0x2 },
	},

	.host = {
		.phy_enable =	{ 0xe468,	0,	0x3,	0x2 },
	},
 };

static const struct rkusbphy_chip rkusbphy_rk3399[] = {
	{
		.c_base_addr = 0xe450,
		.c_regs = &rkusbphy_rk3399_usb0_regs,
	},
	{
		.c_base_addr = 0xe460,
		.c_regs = &rkusbphy_rk3399_usb1_regs,
	},
};

/*
 * RK3528 has a single USB2PHY node.
 */

static const struct rkusbphy_regs rkusbphy_rk3528_regs = {
	/*				shift,	mask,	set */
	.clk_enable =	{ 0x041c,	2,	0x3f,	0x27 },

	.otg = {
		.phy_enable =	{ 0x004c,	0,	0x3,	0x2 },
	},

	.host = {
		.phy_enable =	{ 0x005c,	0,	0x3,	0x2 },
	},
};

static const struct rkusbphy_chip rkusbphy_rk3528[] = {
	{
		.c_base_addr = 0xffdf0000,
		.c_regs = &rkusbphy_rk3528_regs,
	},
};

/*
 * RK3568 has two USB2PHY nodes that have a GRF each. Each GRF has
 * the same register layout.
 */

static const struct rkusbphy_regs rkusbphy_rk3568_regs = {
	/*				shift,	mask,	set */
	.clk_enable =	{ 0x0008,	4,	0x1,	0x0 },

	.otg = {
		.phy_enable =	{ 0x0000,	0,	0x1ff,	0x1d2 },
	},

	.host = {
		.phy_enable =	{ 0x0004,	0,	0x1ff,	0x1d2 },
	},
};

static const struct rkusbphy_chip rkusbphy_rk3568[] = {
	{
		.c_base_addr = 0xfe8a0000,
		.c_regs = &rkusbphy_rk3568_regs,
	},
	{
		.c_base_addr = 0xfe8b0000,
		.c_regs = &rkusbphy_rk3568_regs,
	},
};

static const struct rkusbphy_regs rkusbphy_rk3588_regs = {
	/*				shift,	mask,	set */
	.clk_enable =	{ 0x0000,	0,	0x1,	0x0 },

	.otg = {
		.phy_enable =	{ 0x000c,	11,	0x1,	0x0 },
	},

	.host = {
		.phy_enable =	{ 0x0008,	2,	0x1,	0x0 },
	},
};

static const struct rkusbphy_chip rkusbphy_rk3588[] = {
	{
		.c_base_addr = 0x0000,
		.c_regs = &rkusbphy_rk3588_regs,
	},
	{
		.c_base_addr = 0x4000,
		.c_regs = &rkusbphy_rk3588_regs,
	},
	{
		.c_base_addr = 0x8000,
		.c_regs = &rkusbphy_rk3588_regs,
	},
	{
		.c_base_addr = 0xc000,
		.c_regs = &rkusbphy_rk3588_regs,
	},
};

/*
 * driver stuff
 */

struct rkusbphy_softc {
	struct device			 sc_dev;
	const struct rkusbphy_regs	*sc_regs;
	struct regmap			*sc_grf;
	bus_space_tag_t			 sc_iot;
	bus_space_handle_t		 sc_ioh;
	struct regmap			*sc_clk;
	int				 sc_node;

	int				 sc_running;

	struct phy_device		 sc_otg_phy;
	struct phy_device		 sc_host_phy;
};
#define DEVNAME(_sc) ((_sc)->sc_dev.dv_xname)

static int		rkusbphy_match(struct device *, void *, void *);
static void		rkusbphy_attach(struct device *, struct device *,
			    void *);

static uint32_t		rkusbphy_rd(struct regmap *,
			    const struct rkusbphy_reg *);
static int		rkusbphy_isset(struct regmap *,
			    const struct rkusbphy_reg *);
static void		rkusbphy_wr(struct regmap *,
			    const struct rkusbphy_reg *, uint32_t);
static void		rkusbphy_set(struct regmap *,
			    const struct rkusbphy_reg *);

static int		rkusbphy_otg_phy_enable(void *, uint32_t *);
static int		rkusbphy_host_phy_enable(void *, uint32_t *);

struct rkusbphy_port_config {
	const char			*pc_name;
	int (*pc_enable)(void *, uint32_t *);
};

static void	rkusbphy_register(struct rkusbphy_softc *,
		    struct phy_device *, const struct rkusbphy_port_config *);

static const struct rkusbphy_port_config rkusbphy_otg_config = {
	.pc_name = "otg-port",
	.pc_enable = rkusbphy_otg_phy_enable,
};

static const struct rkusbphy_port_config rkusbphy_host_config = {
	.pc_name = "host-port",
	.pc_enable = rkusbphy_host_phy_enable,
};

const struct cfattach rkusbphy_ca = {
	sizeof (struct rkusbphy_softc), rkusbphy_match, rkusbphy_attach
};

struct cfdriver rkusbphy_cd = {
	NULL, "rkusbphy", DV_DULL
};

struct rkusbphy_id {
	const char			*id_name;
	const struct rkusbphy_chip	*id_chips;
	size_t				 id_nchips;
};

#define RKUSBPHY_ID(_n, _c) { _n, _c, nitems(_c) }

static const struct rkusbphy_id rkusbphy_ids[] = {
	RKUSBPHY_ID("rockchip,rk3128-usb2phy", rkusbphy_rk3128),
	RKUSBPHY_ID("rockchip,rk3399-usb2phy", rkusbphy_rk3399),
	RKUSBPHY_ID("rockchip,rk3528-usb2phy", rkusbphy_rk3528),
	RKUSBPHY_ID("rockchip,rk3568-usb2phy", rkusbphy_rk3568),
	RKUSBPHY_ID("rockchip,rk3588-usb2phy", rkusbphy_rk3588),
};

static const struct rkusbphy_id *
rkusbphy_lookup(struct fdt_attach_args *faa)
{
	size_t i;

	for (i = 0; i < nitems(rkusbphy_ids); i++) {
		const struct rkusbphy_id *id = &rkusbphy_ids[i];
		if (OF_is_compatible(faa->fa_node, id->id_name))
			return (id);
	}

	return (NULL);
}

static int
rkusbphy_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (rkusbphy_lookup(faa) != NULL ? 1 : 0);
}

static void
rkusbphy_attach(struct device *parent, struct device *self, void *aux)
{
	struct rkusbphy_softc *sc = (struct rkusbphy_softc *)self;
	struct fdt_attach_args *faa = aux;
	const struct rkusbphy_id *id = rkusbphy_lookup(faa);
	size_t i;
	uint32_t grfph;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	for (i = 0; i < id->id_nchips; i++) {
		const struct rkusbphy_chip *c = &id->id_chips[i];
		if (faa->fa_reg[0].addr == c->c_base_addr) {
			printf(": phy %zu\n", i);
			sc->sc_regs = c->c_regs;
			break;
		}
	}
	if (sc->sc_regs == NULL) {
		printf(": unknown base address 0x%llu\n", faa->fa_reg[0].addr);
		return;
	}

	sc->sc_node = faa->fa_node;

	grfph = OF_getpropint(sc->sc_node, "rockchip,usbgrf", 0);
	if (grfph)
		sc->sc_grf = regmap_byphandle(grfph);
	else
		sc->sc_grf = regmap_bynode(OF_parent(faa->fa_node));
	if (sc->sc_grf == NULL) {
		printf("%s: rockchip,usbgrf 0x%x not found\n", DEVNAME(sc),
		    grfph);
		return;
	}

	if (OF_is_compatible(faa->fa_node, "rockchip,rk3528-usb2phy")) {
		sc->sc_iot = faa->fa_iot;
		if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
		    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
			printf(": can't map registers\n");
			return;
		}
		regmap_register(faa->fa_node, sc->sc_iot, sc->sc_ioh,
		    faa->fa_reg[0].size);
		sc->sc_clk = regmap_bynode(faa->fa_node);
	} else {
		sc->sc_clk = sc->sc_grf;
	}

	rkusbphy_register(sc, &sc->sc_otg_phy, &rkusbphy_otg_config);
	rkusbphy_register(sc, &sc->sc_host_phy, &rkusbphy_host_config);
}

static uint32_t
rkusbphy_rd(struct regmap *rm, const struct rkusbphy_reg *r)
{
	uint32_t v;

	if (r->r_mask == 0)
		return (0);

	v = regmap_read_4(rm, r->r_offs);

	return ((v >> r->r_shift) & r->r_mask);
}

static int
rkusbphy_isset(struct regmap *rm, const struct rkusbphy_reg *r)
{
	return (rkusbphy_rd(rm, r) == r->r_set);
}

static void
rkusbphy_wr(struct regmap *rm, const struct rkusbphy_reg *r, uint32_t v)
{
	if (r->r_mask == 0)
		return;

	regmap_write_4(rm, r->r_offs,
	    (r->r_mask << (r->r_shift + 16)) | (v << r->r_shift));
}

static void
rkusbphy_set(struct regmap *rm, const struct rkusbphy_reg *r)
{
	rkusbphy_wr(rm, r, r->r_set);
}

static void
rkusbphy_register(struct rkusbphy_softc *sc, struct phy_device *pd,
    const struct rkusbphy_port_config *pc)
{
	char status[32];
	int node;

	node = OF_getnodebyname(sc->sc_node, pc->pc_name);
	if (node == 0)
		return;

	if (OF_getprop(node, "status", status, sizeof(status)) > 0 &&
	    strcmp(status, "disabled") == 0)
		return;

	pd->pd_node = node;
	pd->pd_cookie = sc;
	pd->pd_enable = pc->pc_enable;
	phy_register(pd);
}

static void
rkusbphy_phy_supply(struct rkusbphy_softc *sc, int node)
{
	int phandle;

	if (!sc->sc_running) {
		clock_enable(sc->sc_node, "phyclk");
		if (!rkusbphy_isset(sc->sc_clk, &sc->sc_regs->clk_enable)) {
			rkusbphy_set(sc->sc_clk, &sc->sc_regs->clk_enable);
			delay(1200);
		}

		sc->sc_running = 1;
	}

	phandle = OF_getpropint(node, "phy-supply", 0);
	if (phandle == 0)
		return;

	regulator_enable(phandle);
}

static int
rkusbphy_otg_phy_enable(void *cookie, uint32_t *cells)
{
	struct rkusbphy_softc *sc = cookie;

	rkusbphy_phy_supply(sc, sc->sc_otg_phy.pd_node);

	rkusbphy_set(sc->sc_grf, &sc->sc_regs->otg.phy_enable);
	delay(1500);

	return (EINVAL);
}

static int
rkusbphy_host_phy_enable(void *cookie, uint32_t *cells)
{
	struct rkusbphy_softc *sc = cookie;

	rkusbphy_phy_supply(sc, sc->sc_host_phy.pd_node);

	rkusbphy_set(sc->sc_grf, &sc->sc_regs->host.phy_enable);
	delay(1500);

	return (EINVAL);
}
