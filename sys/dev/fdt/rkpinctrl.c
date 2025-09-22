/*	$OpenBSD: rkpinctrl.c,v 1.16 2025/04/20 09:04:10 kettenis Exp $	*/
/*
 * Copyright (c) 2017, 2018 Mark Kettenis <kettenis@openbsd.org>
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

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/fdt.h>

#include <machine/simplebusvar.h>

/* Pin numbers (from devicetree bindings) */
#define RK_PA0		0
#define RK_PA1		1
#define RK_PA2		2
#define RK_PA3		3
#define RK_PA4		4
#define RK_PA5		5
#define RK_PA6		6
#define RK_PA7		7
#define RK_PB0		8
#define RK_PB1		9
#define RK_PB2		10
#define RK_PB3		11
#define RK_PB4		12
#define RK_PB5		13
#define RK_PB6		14
#define RK_PB7		15
#define RK_PC0		16
#define RK_PC1		17
#define RK_PC2		18
#define RK_PC3		19
#define RK_PC4		20
#define RK_PC5		21
#define RK_PC6		22
#define RK_PC7		23
#define RK_PD0		24
#define RK_PD1		25
#define RK_PD2		26
#define RK_PD3		27
#define RK_PD4		28
#define RK_PD5		29
#define RK_PD6		30
#define RK_PD7		31

/* RK3288 registers */
#define RK3288_GRF_GPIO1A_IOMUX		0x0000
#define RK3288_PMUGRF_GPIO0A_IOMUX	0x0084

/* RK3308 registers */
#define RK3308_GRF_GPIO0A_IOMUX		0x0000

/* RK3328 registers */
#define RK3328_GRF_GPIO0A_IOMUX		0x0000

/* RK3399 registers */
#define RK3399_GRF_GPIO2A_IOMUX		0xe000
#define RK3399_PMUGRF_GPIO0A_IOMUX	0x0000

/* RK3528 registers */
#define RK3528_GRF_GPIO0_IOMUX		0x00000
#define RK3528_GRF_GPIO0_DS		0x00100
#define RK3528_GRF_GPIO0_P		0x00200
#define RK3528_GRF_GPIO0_ST		0x00400
#define RK3528_GRF_GPIO1_IOMUX		0x20020
#define RK3528_GRF_GPIO1_DS		0x20120
#define RK3528_GRF_GPIO1_P		0x20210
#define RK3528_GRF_GPIO1_ST		0x20410
#define RK3528_GRF_GPIO2_IOMUX		0x30040
#define RK3528_GRF_GPIO2_DS		0x30160
#define RK3528_GRF_GPIO2_P		0x30220
#define RK3528_GRF_GPIO2_ST		0x30420
#define RK3528_GRF_GPIO3_IOMUX		0x20060
#define RK3528_GRF_GPIO3_DS		0x20190
#define RK3528_GRF_GPIO3_P		0x20230
#define RK3528_GRF_GPIO3_ST		0x20430
#define RK3528_GRF_GPIO4_IOMUX		0x10080
#define RK3528_GRF_GPIO4_DS		0x101c0
#define RK3528_GRF_GPIO4_P		0x10240
#define RK3528_GRF_GPIO4_ST		0x10440

/* RK3568 registers */
#define RK3568_GRF_GPIO1A_IOMUX_L	0x0000
#define RK3568_GRF_GPIO1A_P		0x0080
#define RK3568_GRF_GPIO1A_IE		0x00c0
#define RK3568_GRF_GPIO1A_DS_0		0x0200
#define RK3568_PMUGRF_GPIO0A_IOMUX_L	0x0000
#define RK3568_PMUGRF_GPIO0A_P		0x0020
#define RK3568_PMUGRF_GPIO0A_IE		0x0030
#define RK3568_PMUGRF_GPIO0A_DS_0	0x0070

struct rockchip_route_table {
	u_int bank : 3;
	u_int idx : 5;
	u_int mux : 3;
	u_int grf : 1;
#define ROUTE_GRF	0
#define ROUTE_PMU	1
	uint16_t reg;
	uint32_t val;
#define ROUTE_VAL(bit, val)	((0x3 << (bit)) << 16 | ((val) << (bit)))
};

struct rkpinctrl_softc {
	struct simplebus_softc	sc_sbus;

	struct regmap		*sc_grf;
	struct regmap		*sc_pmu;
};

int	rkpinctrl_match(struct device *, void *, void *);
void	rkpinctrl_attach(struct device *, struct device *, void *);

const struct cfattach	rkpinctrl_ca = {
	sizeof (struct rkpinctrl_softc), rkpinctrl_match, rkpinctrl_attach
};

struct cfdriver rkpinctrl_cd = {
	NULL, "rkpinctrl", DV_DULL
};

int	rk3288_pinctrl(uint32_t, void *);
int	rk3308_pinctrl(uint32_t, void *);
int	rk3328_pinctrl(uint32_t, void *);
int	rk3399_pinctrl(uint32_t, void *);
int	rk3528_pinctrl(uint32_t, void *);
int	rk3568_pinctrl(uint32_t, void *);
int	rk3588_pinctrl(uint32_t, void *);

int
rkpinctrl_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "rockchip,rk3288-pinctrl") ||
	    OF_is_compatible(faa->fa_node, "rockchip,rk3308-pinctrl") ||
	    OF_is_compatible(faa->fa_node, "rockchip,rk3328-pinctrl") ||
	    OF_is_compatible(faa->fa_node, "rockchip,rk3399-pinctrl") ||
	    OF_is_compatible(faa->fa_node, "rockchip,rk3528-pinctrl") ||
	    OF_is_compatible(faa->fa_node, "rockchip,rk3568-pinctrl") ||
	    OF_is_compatible(faa->fa_node, "rockchip,rk3588-pinctrl"));
}

void
rkpinctrl_attach(struct device *parent, struct device *self, void *aux)
{
	struct rkpinctrl_softc *sc = (struct rkpinctrl_softc *)self;
	struct fdt_attach_args *faa = aux;
	uint32_t grf, pmu;

	grf = OF_getpropint(faa->fa_node, "rockchip,grf", 0);
	pmu = OF_getpropint(faa->fa_node, "rockchip,pmu", 0);
	sc->sc_grf = regmap_byphandle(grf);
	sc->sc_pmu = regmap_byphandle(pmu);

	if (sc->sc_grf == NULL && sc->sc_pmu == NULL) {
		printf(": no registers\n");
		return;
	}

	if (OF_is_compatible(faa->fa_node, "rockchip,rk3288-pinctrl"))
		pinctrl_register(faa->fa_node, rk3288_pinctrl, sc);
	else if (OF_is_compatible(faa->fa_node, "rockchip,rk3308-pinctrl"))
		pinctrl_register(faa->fa_node, rk3308_pinctrl, sc);
	else if (OF_is_compatible(faa->fa_node, "rockchip,rk3328-pinctrl"))
		pinctrl_register(faa->fa_node, rk3328_pinctrl, sc);
	else if (OF_is_compatible(faa->fa_node, "rockchip,rk3399-pinctrl"))
		pinctrl_register(faa->fa_node, rk3399_pinctrl, sc);
	else if (OF_is_compatible(faa->fa_node, "rockchip,rk3528-pinctrl"))
		pinctrl_register(faa->fa_node, rk3528_pinctrl, sc);
	else if (OF_is_compatible(faa->fa_node, "rockchip,rk3568-pinctrl"))
		pinctrl_register(faa->fa_node, rk3568_pinctrl, sc);
	else
		pinctrl_register(faa->fa_node, rk3588_pinctrl, sc);

	/* Attach GPIO banks. */
	simplebus_attach(parent, &sc->sc_sbus.sc_dev, faa);
}

/*
 * Rockchip RK3288
 */

int
rk3288_pull(uint32_t bank, uint32_t idx, uint32_t phandle)
{
	int node;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return -1;

	/* XXX */
	if (bank == 0)
		return -1;

	if (OF_getproplen(node, "bias-disable") == 0)
		return 0;
	if (OF_getproplen(node, "bias-pull-up") == 0)
		return 1;
	if (OF_getproplen(node, "bias-pull-down") == 0)
		return 2;

	return -1;
}

int
rk3288_strength(uint32_t bank, uint32_t idx, uint32_t phandle)
{
	int strength, level;
	int levels[4] = { 2, 4, 8, 12 };
	int node;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return -1;

	/* XXX */
	if (bank == 0)
		return -1;

	strength = OF_getpropint(node, "drive-strength", -1);
	if (strength == -1)
		return -1;

	/* Convert drive strength to level. */
	for (level = 3; level >= 0; level--) {
		if (strength >= levels[level])
			break;
	}
	return level;
}

int
rk3288_pinctrl(uint32_t phandle, void *cookie)
{
	struct rkpinctrl_softc *sc = cookie;
	uint32_t *pins;
	int node, len, i;

	KASSERT(sc->sc_grf);
	KASSERT(sc->sc_pmu);

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return -1;

	len = OF_getproplen(node, "rockchip,pins");
	if (len <= 0)
		return -1;

	pins = malloc(len, M_TEMP, M_WAITOK);
	if (OF_getpropintarray(node, "rockchip,pins", pins, len) != len)
		goto fail;

	for (i = 0; i < len / sizeof(uint32_t); i += 4) {
		struct regmap *rm;
		bus_size_t base, off;
		uint32_t bank, idx, mux;
		int pull, strength;
		uint32_t mask, bits;
		int s;

		bank = pins[i];
		idx = pins[i + 1];
		mux = pins[i + 2];

		if (bank > 8 || idx >= 32 || mux > 7)
			continue;

		pull = rk3288_pull(bank, idx, pins[i + 3]);
		strength = rk3288_strength(bank, idx, pins[i + 3]);

		/* Bank 0 lives in the PMU. */
		if (bank < 1) {
			rm = sc->sc_pmu;
			base = RK3288_PMUGRF_GPIO0A_IOMUX;
		} else {
			rm = sc->sc_grf;
			base = RK3288_GRF_GPIO1A_IOMUX - 0x10;
		}

		s = splhigh();

		/* IOMUX control */
		off = bank * 0x10 + (idx / 8) * 0x04;

		/* GPIO3D, GPIO4A and GPIO4B are special. */
		if ((bank == 3 && idx >= 24) || (bank == 4 && idx < 16)) {
			mask = (0x7 << ((idx % 4) * 4));
			bits = (mux << ((idx % 4) * 4));
		} else {
			mask = (0x3 << ((idx % 8) * 2));
			bits = (mux << ((idx % 8) * 2));
		}
		if (bank > 3 || (bank == 3 && idx >= 28))
			off += 0x04;
		if (bank > 4 || (bank == 4 && idx >= 4))
			off += 0x04;
		if (bank > 4 || (bank == 4 && idx >= 12))
			off += 0x04;
		regmap_write_4(rm, base + off, mask << 16 | bits);

		/* GPIO pad pull down and pull up control */
		if (pull >= 0) {
			off = 0x140 + bank * 0x10 + (idx / 8) * 0x04;
			mask = (0x3 << ((idx % 8) * 2));
			bits = (pull << ((idx % 8) * 2));
			regmap_write_4(rm, base + off, mask << 16 | bits);
		}

		/* GPIO drive strength control */
		if (strength >= 0) {
			off = 0x1c0 + bank * 0x10 + (idx / 8) * 0x04;
			mask = (0x3 << ((idx % 8) * 2));
			bits = (strength << ((idx % 8) * 2));
			regmap_write_4(rm, base + off, mask << 16 | bits);
		}

		splx(s);
	}

	free(pins, M_TEMP, len);
	return 0;

fail:
	free(pins, M_TEMP, len);
	return -1;
}

/*
 * Rockchip RK3308
 */

int
rk3308_pull(uint32_t bank, uint32_t idx, uint32_t phandle)
{
	int node;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return -1;

	if (OF_getproplen(node, "bias-disable") == 0)
		return 0;
	if (OF_getproplen(node, "bias-pull-up") == 0)
		return 1;
	if (OF_getproplen(node, "bias-pull-down") == 0)
		return 2;

	return -1;
}

int
rk3308_strength(uint32_t bank, uint32_t idx, uint32_t phandle)
{
	int strength, level;
	int levels[4] = { 2, 4, 8, 12 };
	int node;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return -1;

	strength = OF_getpropint(node, "drive-strength", -1);
	if (strength == -1)
		return -1;

	/* Convert drive strength to level. */
	for (level = 3; level >= 0; level--) {
		if (strength >= levels[level])
			break;
	}
	return level;
}

int
rk3308_pinctrl(uint32_t phandle, void *cookie)
{
	struct rkpinctrl_softc *sc = cookie;
	uint32_t *pins;
	int node, len, i;

	KASSERT(sc->sc_grf);

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return -1;

	len = OF_getproplen(node, "rockchip,pins");
	if (len <= 0)
		return -1;

	pins = malloc(len, M_TEMP, M_WAITOK);
	if (OF_getpropintarray(node, "rockchip,pins", pins, len) != len)
		goto fail;

	for (i = 0; i < len / sizeof(uint32_t); i += 4) {
		struct regmap *rm = sc->sc_grf;
		bus_size_t base, off;
		uint32_t bank, idx, mux;
		int pull, strength;
		uint32_t mask, bits;
		int s;

		bank = pins[i];
		idx = pins[i + 1];
		mux = pins[i + 2];

		if (bank > 4 || idx >= 32 || mux > 7)
			continue;

		pull = rk3308_pull(bank, idx, pins[i + 3]);
		strength = rk3308_strength(bank, idx, pins[i + 3]);
 
		base = RK3308_GRF_GPIO0A_IOMUX;

		s = splhigh();

		/* IOMUX control */
		off = bank * 0x20 + (idx / 8) * 0x08;

		/* GPIO1B, GPIO1C and GPIO3B are special. */
		if ((bank == 1) && (idx == 14)) {
			mask = (0xf << 12);
			bits = (mux << 12);
		} else if ((bank == 1) && (idx == 15)) {
			off += 4;
			mask = 0x3;
			bits = mux;
		} else if ((bank == 1) && (idx >= 16 && idx <= 17)) {
			mask = (0x3 << ((idx - 16) * 2));
			bits = (mux << ((idx - 16) * 2));
		} else if ((bank == 1) && (idx >= 18 && idx <= 20)) {
			mask = (0xf << (((idx - 18) * 4) + 4));
			bits = (mux << (((idx - 18) * 4) + 4));
		} else if ((bank == 1) && (idx >= 21 && idx <= 23)) {
			off += 4;
			mask = (0xf << ((idx - 21) * 4));
			bits = (mux << ((idx - 21) * 4));
		} else if ((bank == 3) && (idx >= 12 && idx <= 13)) {
			mask = (0xf << (((idx - 12) * 4) + 8));
			bits = (mux << (((idx - 12) * 4) + 8));
		} else {
			mask = (0x3 << ((idx % 8) * 2));
			bits = (mux << ((idx % 8) * 2));
		}
		regmap_write_4(rm, base + off, mask << 16 | bits);

		/* GPIO pad pull down and pull up control */
		if (pull >= 0) {
			off = 0xa0 + bank * 0x10 + (idx / 8) * 0x04;
			mask = (0x3 << ((idx % 8) * 2));
			bits = (pull << ((idx % 8) * 2));
			regmap_write_4(rm, base + off, mask << 16 | bits);
		}

		/* GPIO drive strength control */
		if (strength >= 0) {
			off = 0x100 + bank * 0x10 + (idx / 8) * 0x04;
			mask = (0x3 << ((idx % 8) * 2));
			bits = (strength << ((idx % 8) * 2));
			regmap_write_4(rm, base + off, mask << 16 | bits);
		}

		splx(s);
	}

	free(pins, M_TEMP, len);
	return 0;

fail:
	free(pins, M_TEMP, len);
	return -1;
}

/*
 * Rockchip RK3328
 */

int
rk3328_pull(uint32_t bank, uint32_t idx, uint32_t phandle)
{
	int node;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return -1;

	if (OF_getproplen(node, "bias-disable") == 0)
		return 0;
	if (OF_getproplen(node, "bias-pull-up") == 0)
		return 1;
	if (OF_getproplen(node, "bias-pull-down") == 0)
		return 2;

	return -1;
}

int
rk3328_strength(uint32_t bank, uint32_t idx, uint32_t phandle)
{
	int strength, level;
	int levels[4] = { 2, 4, 8, 12 };
	int node;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return -1;

	strength = OF_getpropint(node, "drive-strength", -1);
	if (strength == -1)
		return -1;

	/* Convert drive strength to level. */
	for (level = 3; level >= 0; level--) {
		if (strength >= levels[level])
			break;
	}
	return level;
}

int
rk3328_pinctrl(uint32_t phandle, void *cookie)
{
	struct rkpinctrl_softc *sc = cookie;
	uint32_t *pins;
	int node, len, i;

	KASSERT(sc->sc_grf);

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return -1;

	len = OF_getproplen(node, "rockchip,pins");
	if (len <= 0)
		return -1;

	pins = malloc(len, M_TEMP, M_WAITOK);
	if (OF_getpropintarray(node, "rockchip,pins", pins, len) != len)
		goto fail;

	for (i = 0; i < len / sizeof(uint32_t); i += 4) {
		struct regmap *rm = sc->sc_grf;
		bus_size_t base, off;
		uint32_t bank, idx, mux;
		int pull, strength;
		uint32_t mask, bits;
		int s;

		bank = pins[i];
		idx = pins[i + 1];
		mux = pins[i + 2];

		if (bank > 3 || idx >= 32 || mux > 3)
			continue;

		pull = rk3328_pull(bank, idx, pins[i + 3]);
		strength = rk3328_strength(bank, idx, pins[i + 3]);

		base = RK3328_GRF_GPIO0A_IOMUX;

		s = splhigh();

		/* IOMUX control */
		off = bank * 0x10 + (idx / 8) * 0x04;

		/* GPIO2B, GPIO2C, GPIO3A and GPIO3B are special. */
		if (bank == 2 && idx == 15) {
			mask = 0x7;
			bits = mux;
		} else if (bank == 2 && idx >= 16 && idx <= 20) {
			mask = (0x7 << ((idx - 16) * 3));
			bits = (mux << ((idx - 16) * 3));
		} else if (bank == 2 && idx >= 21 && idx <= 23) {
			mask = (0x7 << ((idx - 21) * 3));
			bits = (mux << ((idx - 21) * 3));
		} else if (bank == 3 && idx <= 4) {
			mask = (0x7 << (idx * 3));
			bits = (mux << (idx * 3));
		} else if (bank == 3 && idx >= 5 && idx <= 7) {
			mask = (0x7 << ((idx - 5) * 3));
			bits = (mux << ((idx - 5) * 3));
		} else if (bank == 3 && idx >= 8 && idx <= 12) {
			mask = (0x7 << ((idx - 8) * 3));
			bits = (mux << ((idx - 8) * 3));
		} else if (bank == 3 && idx >= 13 && idx <= 15) {
			mask = (0x7 << ((idx - 13) * 3));
			bits = (mux << ((idx - 13) * 3));
		} else {
			mask = (0x3 << ((idx % 8) * 2));
			bits = (mux << ((idx % 8) * 2));
		}
		if (bank > 2 || (bank == 2 && idx >= 15))
			off += 0x04;
		if (bank > 2 || (bank == 2 && idx >= 21))
			off += 0x04;
		if (bank > 3 || (bank == 3 && idx >= 5))
			off += 0x04;
		if (bank > 3 || (bank == 3 && idx >= 13))
			off += 0x04;
		regmap_write_4(rm, base + off, mask << 16 | bits);

		/* GPIO pad pull down and pull up control */
		if (pull >= 0) {
			off = 0x100 + bank * 0x10 + (idx / 8) * 0x04;
			mask = (0x3 << ((idx % 8) * 2));
			bits = (pull << ((idx % 8) * 2));
			regmap_write_4(rm, base + off, mask << 16 | bits);
		}

		/* GPIO drive strength control */
		if (strength >= 0) {
			off = 0x200 + bank * 0x10 + (idx / 8) * 0x04;
			mask = (0x3 << ((idx % 8) * 2));
			bits = (strength << ((idx % 8) * 2));
			regmap_write_4(rm, base + off, mask << 16 | bits);
		}

		splx(s);
	}

	free(pins, M_TEMP, len);
	return 0;

fail:
	free(pins, M_TEMP, len);
	return -1;
}

/* 
 * Rockchip RK3399 
 */

int
rk3399_pull(uint32_t bank, uint32_t idx, uint32_t phandle)
{
	int pull_up, pull_down;
	int node;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return -1;

	if (bank == 2 && idx >= 16) {
		pull_up = 3;
		pull_down = 1;
	} else {
		pull_up = 1;
		pull_down = 2;
	}

	if (OF_getproplen(node, "bias-disable") == 0)
		return 0;
	if (OF_getproplen(node, "bias-pull-up") == 0)
		return pull_up;
	if (OF_getproplen(node, "bias-pull-down") == 0)
		return pull_down;

	return -1;
}

/* Magic because the drive strength configurations vary wildly. */

const int rk3399_strength_levels[][8] = {
	{ 2, 4, 8, 12 },			/* default */
	{ 3, 6, 9, 12 },			/* 1.8V or 3.0V */
	{ 5, 10, 15, 20 },			/* 1.8V only */
	{ 4, 6, 8, 10, 12, 14, 16, 18 },	/* 1.8V or 3.0V auto */
	{ 4, 7, 10, 13, 16, 19, 22, 26 },	/* 3.3V */
};

const int rk3399_strength_types[][4] = {
	{ 2, 2, 0, 0 },
	{ 1, 1, 1, 1 },
	{ 1, 1, 2, 2 },
	{ 4, 4, 4, 1 },
	{ 1, 3, 1, 1 },
};

const int rk3399_strength_regs[][4] = {
	{ 0x0080, 0x0088, 0x0090, 0x0098 },
	{ 0x00a0, 0x00a8, 0x00b0, 0x00b8 },
	{ 0x0100, 0x0104, 0x0108, 0x010c },
	{ 0x0110, 0x0118, 0x0120, 0x0128 },
	{ 0x012c, 0x0130, 0x0138, 0x013c },
};

int
rk3399_strength(uint32_t bank, uint32_t idx, uint32_t phandle)
{
	int strength, type, level;
	const int *levels;
	int node;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return -1;

	strength = OF_getpropint(node, "drive-strength", -1);
	if (strength == -1)
		return -1;

	/* Convert drive strength to level. */
	type = rk3399_strength_types[bank][idx / 8];
	levels = rk3399_strength_levels[type];
	for (level = 7; level >= 0; level--) {
		if (strength >= levels[level] && levels[level] > 0)
			break;
	}
	return level;
}

int
rk3399_pinctrl(uint32_t phandle, void *cookie)
{
	struct rkpinctrl_softc *sc = cookie;
	uint32_t *pins;
	int node, len, i;

	KASSERT(sc->sc_grf);
	KASSERT(sc->sc_pmu);

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return -1;

	len = OF_getproplen(node, "rockchip,pins");
	if (len <= 0)
		return -1;

	pins = malloc(len, M_TEMP, M_WAITOK);
	if (OF_getpropintarray(node, "rockchip,pins", pins, len) != len)
		goto fail;

	for (i = 0; i < len / sizeof(uint32_t); i += 4) {
		struct regmap *rm;
		bus_size_t base, off;
		uint32_t bank, idx, mux;
		int pull, strength, type, shift;
		uint32_t mask, bits;
		int s;

		bank = pins[i];
		idx = pins[i + 1];
		mux = pins[i + 2];

		if (bank > 4 || idx >= 32 || mux > 3)
			continue;

		pull = rk3399_pull(bank, idx, pins[i + 3]);
		strength = rk3399_strength(bank, idx, pins[i + 3]);

		/* Bank 0 and 1 live in the PMU. */
		if (bank < 2) {
			rm = sc->sc_pmu;
			base = RK3399_PMUGRF_GPIO0A_IOMUX;
		} else {
			rm = sc->sc_grf;
			base = RK3399_GRF_GPIO2A_IOMUX - 0x20;
		}

		s = splhigh();

		/* IOMUX control */
		off = bank * 0x10 + (idx / 8) * 0x04;
		mask = (0x3 << ((idx % 8) * 2));
		bits = (mux << ((idx % 8) * 2));
		regmap_write_4(rm, base + off, mask << 16 | bits);

		/* GPIO pad pull down and pull up control */
		if (pull >= 0) {
			off = 0x40 + bank * 0x10 + (idx / 8) * 0x04;
			mask = (0x3 << ((idx % 8) * 2));
			bits = (pull << ((idx % 8) * 2));
			regmap_write_4(rm, base + off, mask << 16 | bits);
		}

		/* GPIO drive strength control */
		if (strength >= 0) {
			off = rk3399_strength_regs[bank][idx / 8];
			type = rk3399_strength_types[bank][idx / 8];
			shift = (type > 2) ? 3 : 2;
			mask = (((1 << shift) - 1) << ((idx % 8) * shift));
			bits = (strength << ((idx % 8) * shift));
			if (mask & 0x0000ffff) {
				regmap_write_4(rm, base + off,
				    mask << 16 | (bits & 0x0000ffff));
			}
			if (mask & 0xffff0000) {
				regmap_write_4(rm, base + off + 0x04,
				    (mask & 0xffff0000) | bits >> 16);
			}
		}

		splx(s);
	}

	free(pins, M_TEMP, len);
	return 0;

fail:
	free(pins, M_TEMP, len);
	return -1;
}

/* 
 * Rockchip RK3528
 */

int
rk3528_pull(uint32_t bank, uint32_t idx, uint32_t phandle)
{
	int node;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return -1;

	if (OF_getproplen(node, "bias-disable") == 0)
		return 0;
	if (OF_getproplen(node, "bias-pull-up") == 0)
		return 1;
	if (OF_getproplen(node, "bias-pull-down") == 0)
		return 2;

	return -1;
}

int
rk3528_strength(uint32_t bank, uint32_t idx, uint32_t phandle)
{
	int node;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return -1;

	return OF_getpropint(node, "drive-strength", -1);
}

int
rk3528_schmitt(uint32_t bank, uint32_t idx, uint32_t phandle)
{
	int node;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return -1;

	if (OF_getproplen(node, "input-schmitt-disable") == 0)
		return 0;
	if (OF_getproplen(node, "input-schmitt-enable") == 0)
		return 1;

	return -1;
}

int
rk3528_pinctrl(uint32_t phandle, void *cookie)
{
	struct rkpinctrl_softc *sc = cookie;
	uint32_t *pins;
	int node, len, i;

	KASSERT(sc->sc_grf);

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return -1;

	len = OF_getproplen(node, "rockchip,pins");
	if (len <= 0)
		return -1;

	pins = malloc(len, M_TEMP, M_WAITOK);
	if (OF_getpropintarray(node, "rockchip,pins", pins, len) != len)
		goto fail;

	for (i = 0; i < len / sizeof(uint32_t); i += 4) {
		struct regmap *rm;
		bus_size_t iomux_base, p_base, ds_base, st_base, off;
		uint32_t bank, idx, mux;
		int pull, strength, schmitt;
		uint32_t mask, bits;
		int s;

		bank = pins[i];
		idx = pins[i + 1];
		mux = pins[i + 2];

		if (bank > 4 || idx >= 32 || mux > 7)
			continue;

		pull = rk3528_pull(bank, idx, pins[i + 3]);
		strength = rk3528_strength(bank, idx, pins[i + 3]);
		schmitt = rk3528_schmitt(bank, idx, pins[i + 3]);

		rm = sc->sc_grf;
		switch (bank) {
		case 0:
			iomux_base = RK3528_GRF_GPIO0_IOMUX;
			p_base = RK3528_GRF_GPIO0_P;
			ds_base = RK3528_GRF_GPIO0_DS;
			st_base = RK3528_GRF_GPIO0_ST;
			break;
		case 1:
			iomux_base = RK3528_GRF_GPIO1_IOMUX;
			p_base = RK3528_GRF_GPIO1_P;
			ds_base = RK3528_GRF_GPIO1_DS;
			st_base = RK3528_GRF_GPIO1_ST;
			break;
		case 2:
			iomux_base = RK3528_GRF_GPIO2_IOMUX;
			p_base = RK3528_GRF_GPIO2_P;
			ds_base = RK3528_GRF_GPIO2_DS;
			st_base = RK3528_GRF_GPIO2_ST;
			break;
		case 3:
			iomux_base = RK3528_GRF_GPIO3_IOMUX;
			p_base = RK3528_GRF_GPIO3_P;
			ds_base = RK3528_GRF_GPIO3_DS;
			st_base = RK3528_GRF_GPIO3_ST;
			break;
		case 4:
			iomux_base = RK3528_GRF_GPIO4_IOMUX;
			p_base = RK3528_GRF_GPIO4_P;
			ds_base = RK3528_GRF_GPIO4_DS;
			st_base = RK3528_GRF_GPIO4_ST;
			break;
		}

		s = splhigh();

		/* IOMUX control */
		off = (idx / 4) * 0x04;
		mask = (0x7 << ((idx % 4) * 4));
		bits = (mux << ((idx % 4) * 4));
		regmap_write_4(rm, iomux_base + off, mask << 16 | bits);

		/* GPIO pad pull down and pull up control */
		if (pull >= 0) {
			off = (idx / 8) * 0x04;
			mask = (0x3 << ((idx % 8) * 2));
			bits = (pull << ((idx % 8) * 2));
			regmap_write_4(rm, p_base + off, mask << 16 | bits);
		}

		/* GPIO drive strength control */
		if (strength >= 0) {
			off = (idx / 2) * 0x04;
			mask = (0x3f << ((idx % 2) * 8));
			bits = ((1 << (strength + 1)) - 1) << ((idx % 2) * 8);
			regmap_write_4(rm, ds_base + off, mask << 16 | bits);
		}

		/* GPIO Schmitt trigger. */
		if (schmitt >= 0) {
			off = (idx / 8) * 0x04;
			mask = (0x1 << (idx % 8));
			bits = schmitt << (idx % 8);
			regmap_write_4(rm, st_base + off, mask << 16 | bits);
		}

		splx(s);
	}

	free(pins, M_TEMP, len);
	return 0;

fail:
	free(pins, M_TEMP, len);
	return -1;
}

/* 
 * Rockchip RK3568
 */

int
rk3568_pull(uint32_t bank, uint32_t idx, uint32_t phandle)
{
	int node;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return -1;

	if (OF_getproplen(node, "bias-disable") == 0)
		return 0;
	if (OF_getproplen(node, "bias-pull-up") == 0)
		return 1;
	if (OF_getproplen(node, "bias-pull-down") == 0)
		return 2;

	return -1;
}

int
rk3568_strength(uint32_t bank, uint32_t idx, uint32_t phandle)
{
	int node;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return -1;

	return OF_getpropint(node, "drive-strength", -1);
}

int
rk3568_schmitt(uint32_t bank, uint32_t idx, uint32_t phandle)
{
	int node;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return -1;

	if (OF_getproplen(node, "input-schmitt-disable") == 0)
		return 1;
	if (OF_getproplen(node, "input-schmitt-enable") == 0)
		return 2;

	return -1;
}

struct rockchip_route_table rk3568_route_table[] = {
	{ 0, RK_PB7, 1, ROUTE_PMU, 0x0110, ROUTE_VAL(0, 0) }, /* PWM0 M0 */
	{ 0, RK_PC7, 2, ROUTE_PMU, 0x0110, ROUTE_VAL(0, 1) }, /* PWM0 M1 */
	{ 0, RK_PC0, 1, ROUTE_PMU, 0x0110, ROUTE_VAL(2, 0) }, /* PWM1 M0 */
	{ 0, RK_PB5, 4, ROUTE_PMU, 0x0110, ROUTE_VAL(2, 1) }, /* PWM1 M1 */
	{ 0, RK_PC1, 1, ROUTE_PMU, 0x0110, ROUTE_VAL(4, 0) }, /* PWM2 M0 */
	{ 0, RK_PB6, 4, ROUTE_PMU, 0x0110, ROUTE_VAL(4, 1) }, /* PWM2 M1 */

	{ 3, RK_PB1, 3, ROUTE_GRF, 0x0300, ROUTE_VAL(8, 0) }, /* GMAC1 M0 */
	{ 4, RK_PA7, 3, ROUTE_GRF, 0x0300, ROUTE_VAL(8, 1) }, /* GMAC1 M1 */
	{ 0, RK_PB6, 1, ROUTE_GRF, 0x0300, ROUTE_VAL(14, 0) }, /* I2C2 M0 */
	{ 4, RK_PB4, 1, ROUTE_GRF, 0x0300, ROUTE_VAL(14, 1) }, /* I2C2 M1 */

	{ 1, RK_PA0, 1, ROUTE_GRF, 0x0304, ROUTE_VAL(0, 0) }, /* I2C3 M0 */
	{ 3, RK_PB6, 4, ROUTE_GRF, 0x0304, ROUTE_VAL(0, 1) }, /* I2C3 M1 */
	{ 4, RK_PB2, 1, ROUTE_GRF, 0x0304, ROUTE_VAL(2, 0) }, /* I2C4 M0 */
	{ 2, RK_PB1, 2, ROUTE_GRF, 0x0304, ROUTE_VAL(2, 1) }, /* I2C4 M1 */
	{ 3, RK_PB4, 4, ROUTE_GRF, 0x0304, ROUTE_VAL(4, 0) }, /* I2C5 M0 */
	{ 4, RK_PD0, 2, ROUTE_GRF, 0x0304, ROUTE_VAL(4, 1) }, /* I2C5 M1 */
	{ 3, RK_PB1, 5, ROUTE_GRF, 0x0304, ROUTE_VAL(14, 0) }, /* PWM8 M0 */
	{ 1, RK_PD5, 4, ROUTE_GRF, 0x0304, ROUTE_VAL(14, 1) }, /* PWM8 M1 */
	
	{ 3, RK_PB2, 5, ROUTE_GRF, 0x0308, ROUTE_VAL(0, 0) }, /* PWM9 M0 */
	{ 1, RK_PD6, 4, ROUTE_GRF, 0x0308, ROUTE_VAL(0, 1) }, /* PWM9 M1 */
	{ 3, RK_PB5, 5, ROUTE_GRF, 0x0308, ROUTE_VAL(2, 0) }, /* PWM10 M0 */
	{ 2, RK_PA1, 2, ROUTE_GRF, 0x0308, ROUTE_VAL(2, 1) }, /* PWM10 M1 */
	{ 3, RK_PB6, 5, ROUTE_GRF, 0x0308, ROUTE_VAL(4, 0) }, /* PWM11 M0 */
	{ 4, RK_PC0, 3, ROUTE_GRF, 0x0308, ROUTE_VAL(4, 1) }, /* PWM11 M1 */
	{ 3, RK_PB7, 2, ROUTE_GRF, 0x0308, ROUTE_VAL(6, 0) }, /* PWM12 M0 */
	{ 4, RK_PC5, 1, ROUTE_GRF, 0x0308, ROUTE_VAL(6, 1) }, /* PWM12 M1 */
	{ 3, RK_PC0, 2, ROUTE_GRF, 0x0308, ROUTE_VAL(8, 0) }, /* PWM13 M0 */
	{ 4, RK_PC6, 1, ROUTE_GRF, 0x0308, ROUTE_VAL(8, 1) }, /* PWM13 M1 */
	{ 3, RK_PC4, 1, ROUTE_GRF, 0x0308, ROUTE_VAL(10, 0) }, /* PWM14 M0 */
	{ 4, RK_PC2, 1, ROUTE_GRF, 0x0308, ROUTE_VAL(10, 1) }, /* PWM14 M1 */
	{ 3, RK_PC5, 1, ROUTE_GRF, 0x0308, ROUTE_VAL(12, 0) }, /* PWM15 M0 */
	{ 4, RK_PC3, 1, ROUTE_GRF, 0x0308, ROUTE_VAL(12, 1) }, /* PWM15 M1 */
	{ 3, RK_PD2, 3, ROUTE_GRF, 0x0308, ROUTE_VAL(14, 0) }, /* SDMMC2 M0 */
	{ 3, RK_PA5, 5, ROUTE_GRF, 0x0308, ROUTE_VAL(14, 1) }, /* SDMMC2 M1 */

	{ 2, RK_PB4, 2, ROUTE_GRF, 0x030c, ROUTE_VAL(8, 0) }, /* UART1 M0 */
	{ 3, RK_PD6, 4, ROUTE_GRF, 0x030c, ROUTE_VAL(8, 1) }, /* UART1 M1 */
	{ 0, RK_PD1, 1, ROUTE_GRF, 0x030c, ROUTE_VAL(10, 0) }, /* UART2 M0 */
	{ 1, RK_PD5, 2, ROUTE_GRF, 0x030c, ROUTE_VAL(10, 1) }, /* UART2 M1 */
	{ 1, RK_PA1, 2, ROUTE_GRF, 0x030c, ROUTE_VAL(12, 0) }, /* UART3 M0 */
	{ 3, RK_PB7, 4, ROUTE_GRF, 0x030c, ROUTE_VAL(12, 1) }, /* UART3 M1 */
	{ 1, RK_PA6, 2, ROUTE_GRF, 0x030c, ROUTE_VAL(14, 0) }, /* UART4 M0 */
	{ 3, RK_PB2, 4, ROUTE_GRF, 0x030c, ROUTE_VAL(14, 1) }, /* UART4 M1 */

	{ 2, RK_PA2, 3, ROUTE_GRF, 0x0310, ROUTE_VAL(0, 0) }, /* UART5 M0 */
	{ 3, RK_PC2, 4, ROUTE_GRF, 0x0310, ROUTE_VAL(0, 1) }, /* UART5 M1 */
	{ 2, RK_PA4, 3, ROUTE_GRF, 0x0310, ROUTE_VAL(2, 0) }, /* UART6 M0 */
	{ 1, RK_PD5, 3, ROUTE_GRF, 0x0310, ROUTE_VAL(2, 1) }, /* UART6 M1 */
	{ 2, RK_PA6, 3, ROUTE_GRF, 0x0310, ROUTE_VAL(4, 0) }, /* UART7 M0 */
	{ 3, RK_PC4, 4, ROUTE_GRF, 0x0310, ROUTE_VAL(4, 1) }, /* UART7 M1 */
	{ 4, RK_PA2, 4, ROUTE_GRF, 0x0310, ROUTE_VAL(4, 2) }, /* UART7 M2 */
	{ 2, RK_PC5, 3, ROUTE_GRF, 0x0310, ROUTE_VAL(6, 0) }, /* UART8 M0 */
	{ 2, RK_PD7, 4, ROUTE_GRF, 0x0310, ROUTE_VAL(6, 1) }, /* UART8 M1 */
	{ 2, RK_PB0, 3, ROUTE_GRF, 0x0310, ROUTE_VAL(8, 0) }, /* UART9 M0 */
	{ 4, RK_PC5, 4, ROUTE_GRF, 0x0310, ROUTE_VAL(8, 1) }, /* UART9 M1 */
	{ 4, RK_PA4, 4, ROUTE_GRF, 0x0310, ROUTE_VAL(8, 2) }, /* UART9 M2 */
	{ 1, RK_PA2, 1, ROUTE_GRF, 0x0310, ROUTE_VAL(10, 0) }, /* I2S1 M0 */
	{ 3, RK_PC6, 4, ROUTE_GRF, 0x0310, ROUTE_VAL(10, 1) }, /* I2S1 M1 */
	{ 2, RK_PD0, 5, ROUTE_GRF, 0x0310, ROUTE_VAL(10, 2) }, /* I2S1 M2 */
	{ 2, RK_PC1, 1, ROUTE_GRF, 0x0310, ROUTE_VAL(12, 0) }, /* I2S2 M0 */
	{ 4, RK_PB6, 5, ROUTE_GRF, 0x0310, ROUTE_VAL(12, 1) }, /* I2S2 M1 */
	{ 3, RK_PA2, 4, ROUTE_GRF, 0x0310, ROUTE_VAL(14, 0) }, /* I2S3 M0 */
	{ 4, RK_PC2, 5, ROUTE_GRF, 0x0310, ROUTE_VAL(14, 1) }, /* I2S3 M1 */

	{ 0, RK_PA5, 3, ROUTE_GRF, 0x0314, ROUTE_VAL(2, 0) }, /* PCIE20 M0 */
	{ 2, RK_PD0, 4, ROUTE_GRF, 0x0314, ROUTE_VAL(2, 1) }, /* PCIE20 M1 */
	{ 1, RK_PB0, 4, ROUTE_GRF, 0x0314, ROUTE_VAL(2, 2) }, /* PCIE20 M2 */
	{ 0, RK_PA4, 3, ROUTE_GRF, 0x0314, ROUTE_VAL(4, 0) }, /* PCIE30X1 M0 */
	{ 2, RK_PD2, 4, ROUTE_GRF, 0x0314, ROUTE_VAL(4, 1) }, /* PCIE30X1 M1 */
	{ 1, RK_PA5, 4, ROUTE_GRF, 0x0314, ROUTE_VAL(4, 2) }, /* PCIE30X1 M2 */
	{ 0, RK_PA6, 2, ROUTE_GRF, 0x0314, ROUTE_VAL(6, 0) }, /* PCIE30X2 M0 */
	{ 2, RK_PD4, 4, ROUTE_GRF, 0x0314, ROUTE_VAL(6, 1) }, /* PCIE30X2 M1 */
	{ 4, RK_PC2, 4, ROUTE_GRF, 0x0314, ROUTE_VAL(6, 2) }, /* PCIE30X2 M2 */
};

void
rk3568_route(struct rkpinctrl_softc *sc, uint32_t *pins)
{
	struct rockchip_route_table *route = NULL;
	struct regmap *rm;
	int bank = pins[0];
	int idx = pins[1];
	int mux = pins[2];
	int i;

	for (i = 0; i < nitems(rk3568_route_table); i++) {
		if (bank == rk3568_route_table[i].bank &&
		    idx == rk3568_route_table[i].idx &&
		    mux == rk3568_route_table[i].mux) {
			route = &rk3568_route_table[i];
			break;
		}
	}
	if (route == NULL)
		return;

	rm = route->grf ? sc->sc_pmu : sc->sc_grf;
	regmap_write_4(rm, route->reg, route->val);
}

int
rk3568_pinctrl(uint32_t phandle, void *cookie)
{
	struct rkpinctrl_softc *sc = cookie;
	uint32_t *pins;
	int node, len, i;

	KASSERT(sc->sc_grf);
	KASSERT(sc->sc_pmu);

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return -1;

	len = OF_getproplen(node, "rockchip,pins");
	if (len <= 0)
		return -1;

	pins = malloc(len, M_TEMP, M_WAITOK);
	if (OF_getpropintarray(node, "rockchip,pins", pins, len) != len)
		goto fail;

	for (i = 0; i < len / sizeof(uint32_t); i += 4) {
		struct regmap *rm;
		bus_size_t iomux_base, p_base, ds_base, ie_base, off;
		uint32_t bank, idx, mux;
		int pull, strength, schmitt;
		uint32_t mask, bits;
		int s;

		bank = pins[i];
		idx = pins[i + 1];
		mux = pins[i + 2];

		if (bank > 5 || idx >= 32 || mux > 7)
			continue;

		pull = rk3568_pull(bank, idx, pins[i + 3]);
		strength = rk3568_strength(bank, idx, pins[i + 3]);
		schmitt = rk3568_schmitt(bank, idx, pins[i + 3]);

		/* Bank 0 lives in the PMU. */
		if (bank < 1) {
			rm = sc->sc_pmu;
			iomux_base = RK3568_PMUGRF_GPIO0A_IOMUX_L;
			p_base = RK3568_PMUGRF_GPIO0A_P;
			ds_base = RK3568_PMUGRF_GPIO0A_DS_0;
			ie_base = RK3568_PMUGRF_GPIO0A_IE;
		} else {
			rm = sc->sc_grf;
			iomux_base = RK3568_GRF_GPIO1A_IOMUX_L;
			p_base = RK3568_GRF_GPIO1A_P;
			ds_base = RK3568_GRF_GPIO1A_DS_0;
			ie_base = RK3568_GRF_GPIO1A_IE;
			bank = bank - 1;
		}

		s = splhigh();

		/* IOMUX control */
		rk3568_route(sc, &pins[i]);
		off = bank * 0x20 + (idx / 4) * 0x04;
		mask = (0x7 << ((idx % 4) * 4));
		bits = (mux << ((idx % 4) * 4));
		regmap_write_4(rm, iomux_base + off, mask << 16 | bits);

		/* GPIO pad pull down and pull up control */
		if (pull >= 0) {
			off = bank * 0x10 + (idx / 8) * 0x04;
			mask = (0x3 << ((idx % 8) * 2));
			bits = (pull << ((idx % 8) * 2));
			regmap_write_4(rm, p_base + off, mask << 16 | bits);
		}

		/* GPIO drive strength control */
		if (strength >= 0) {
			off = bank * 0x40 + (idx / 2) * 0x04;
			mask = (0x3f << ((idx % 2) * 8));
			bits = ((1 << (strength + 1)) - 1) << ((idx % 2) * 8);
			regmap_write_4(rm, ds_base + off, mask << 16 | bits);
		}

		/* GPIO Schmitt trigger. */
		if (schmitt >= 0) {
			off = bank * 0x10 + (idx / 8) * 0x04;
			mask = (0x3 << ((idx % 8) * 2));
			bits = schmitt << ((idx % 8) * 2);
			regmap_write_4(rm, ie_base + off, mask << 16 | bits);
		}

		splx(s);
	}

	free(pins, M_TEMP, len);
	return 0;

fail:
	free(pins, M_TEMP, len);
	return -1;
}

/* 
 * Rockchip RK3588
 */

int
rk3588_pull(uint32_t bank, uint32_t idx, uint32_t phandle)
{
	int node;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return -1;

	if (OF_getproplen(node, "bias-disable") == 0)
		return 0;
	if (OF_getproplen(node, "bias-pull-up") == 0)
		return 3;
	if (OF_getproplen(node, "bias-pull-down") == 0)
		return 1;

	return -1;
}

int
rk3588_strength(uint32_t bank, uint32_t idx, uint32_t phandle)
{
	int node;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return -1;

	return OF_getpropint(node, "drive-strength", -1);
}

int
rk3588_schmitt(uint32_t bank, uint32_t idx, uint32_t phandle)
{
	int node;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return -1;

	if (OF_getproplen(node, "input-schmitt-disable") == 0)
		return 0;
	if (OF_getproplen(node, "input-schmitt-enable") == 0)
		return 1;

	return -1;
}

#define RK3588_PMU1_IOC		0x0000
#define RK3588_PMU2_IOC		0x4000
#define RK3588_BUS_IOC		0x8000
#define RK3588_VCCIO1_4_IOC	0x9000
#define RK3588_VCCIO3_5_IOC	0xa000	
#define RK3588_VCCIO2_IOC	0xb000
#define RK3588_VCCIO6_IOC	0xc000
#define RK3588_EMMC_IOC		0xd000

bus_size_t
rk3588_base(uint32_t bank, uint32_t idx)
{
	if (bank == 1 && idx < 32)
		return RK3588_VCCIO1_4_IOC;
	if (bank == 2 && idx < 6)
		return RK3588_EMMC_IOC;
	if (bank == 2 && idx < 24)
		return RK3588_VCCIO3_5_IOC;
	if (bank == 2 && idx < 32)
		return RK3588_EMMC_IOC;
	if (bank == 3 && idx < 32)
		return RK3588_VCCIO3_5_IOC;
	if (bank == 4 && idx < 18)
		return RK3588_VCCIO6_IOC;
	if (bank == 4 && idx < 24)
		return RK3588_VCCIO3_5_IOC;
	if (bank == 4 && idx < 32)
		return RK3588_VCCIO2_IOC;

	return (bus_size_t)-1;
}

int
rk3588_pinctrl(uint32_t phandle, void *cookie)
{
	struct rkpinctrl_softc *sc = cookie;
	struct regmap *rm = sc->sc_grf;
	uint32_t *pins;
	int node, len, i;

	KASSERT(sc->sc_grf);

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return -1;

	len = OF_getproplen(node, "rockchip,pins");
	if (len <= 0)
		return -1;

	pins = malloc(len, M_TEMP, M_WAITOK);
	if (OF_getpropintarray(node, "rockchip,pins", pins, len) != len)
		goto fail;

	for (i = 0; i < len / sizeof(uint32_t); i += 4) {
		bus_size_t iomux_base, p_base, ds_base, smt_base, off;
		uint32_t bank, idx, mux;
		int pull, strength, schmitt;
		uint32_t mask, bits;
		int s;

		bank = pins[i];
		idx = pins[i + 1];
		mux = pins[i + 2];

		if (bank > 5 || idx >= 32 || mux > 15)
			continue;

		pull = rk3588_pull(bank, idx, pins[i + 3]);
		strength = rk3588_strength(bank, idx, pins[i + 3]);
		schmitt = rk3588_schmitt(bank, idx, pins[i + 3]);

		if (bank == 0 && idx < 12) {
			/* PMU1 */
			iomux_base = RK3588_PMU1_IOC;
		} else {
			/* BUS */
			iomux_base = RK3588_BUS_IOC;
		}

		if (bank == 0) {
			if (idx < 12) {
				/* PMU1 */
				p_base = RK3588_PMU1_IOC + 0x0020;
				ds_base = RK3588_PMU1_IOC + 0x0010;
				smt_base = RK3588_PMU1_IOC + 0x0030;
			} else {
				/* PMU2 */
				p_base = RK3588_PMU2_IOC + 0x0024;
				ds_base = RK3588_PMU2_IOC + 0x0008;
				smt_base = RK3588_PMU2_IOC + 0x003c;
			}
		} else {
			bus_size_t base = rk3588_base(bank, idx);
			KASSERT(base != (bus_size_t)-1);

			p_base = base + 0x0100;
			ds_base = base + 0x0000;
			smt_base = base + 0x0200;
		}

		s = splhigh();

		/* IOMUX control */
		off = bank * 0x20 + (idx / 4) * 0x04;
		mask = (0xf << ((idx % 4) * 4));
		bits = (mux << ((idx % 4) * 4));
		regmap_write_4(rm, iomux_base + off, mask << 16 | bits);
		if (bank == 0 && idx > 12) {
			iomux_base = RK3588_PMU2_IOC;
			off = (idx - 12) / 4 * 0x04;
			mux = (mux < 8) ? mux : 8;
			bits = (mux << ((idx % 4) * 4));
			regmap_write_4(rm, iomux_base + off, mask << 16 | bits);
		}

		/* GPIO pad pull down and pull up control */
		if (pull >= 0) {
			off = bank * 0x10 + (idx / 8) * 0x04;
			mask = (0x3 << ((idx % 8) * 2));
			bits = (pull << ((idx % 8) * 2));
			regmap_write_4(rm, p_base + off, mask << 16 | bits);
		}

		/* GPIO drive strength control */
		if (strength >= 0) {
			off = bank * 0x20 + (idx / 4) * 0x04;
			mask = (0xf << ((idx % 4) * 4));
			bits = (strength << ((idx % 4) * 4));
			regmap_write_4(rm, ds_base + off, mask << 16 | bits);
		}

		/* GPIO Schmitt trigger. */
		if (schmitt >= 0) {
			off = bank * 0x10 + (idx / 8) * 0x04;
			mask = (0x1 << (idx % 8));
			bits = (schmitt << (idx % 8));
			regmap_write_4(rm, smt_base + off, mask << 16 | bits);
		}

		splx(s);
	}

	free(pins, M_TEMP, len);
	return 0;

fail:
	free(pins, M_TEMP, len);
	return -1;
}
