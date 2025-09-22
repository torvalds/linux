/*	$OpenBSD: rkemmcphy.c,v 1.4 2021/10/24 17:52:26 mpi Exp $	*/
/*
 * Copyright (c) 2019 Patrick Wildt <patrick@blueri.se>
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

#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

/* Registers */
#define GRF_EMMCPHY_CON0		0x00
#define  GRF_EMMCPHY_CON0_OTAPDLYSEL_4		(0x4 << 7)
#define  GRF_EMMCPHY_CON0_OTAPDLYENA_EN		(1 << 11)
#define  GRF_EMMCPHY_CON0_FREQSEL_200M		(0x0 << 12)
#define  GRF_EMMCPHY_CON0_FREQSEL_50M		(0x1 << 12)
#define  GRF_EMMCPHY_CON0_FREQSEL_100M		(0x2 << 12)
#define  GRF_EMMCPHY_CON0_FREQSEL_150M		(0x3 << 12)
#define  GRF_EMMCPHY_CON0_OTAPDLYSEL_CLR	(0xf << 23)
#define  GRF_EMMCPHY_CON0_OTAPDLYENA_CLR	(1 << 27)
#define  GRF_EMMCPHY_CON0_FREQSEL_CLR		(0x3 << 28)
#define GRF_EMMCPHY_CON1		0x04
#define GRF_EMMCPHY_CON2		0x08
#define GRF_EMMCPHY_CON3		0x0c
#define GRF_EMMCPHY_CON4		0x10
#define GRF_EMMCPHY_CON5		0x14
#define GRF_EMMCPHY_CON6		0x18
#define  GRF_EMMCPHY_CON6_PDB_OFF		(0 << 0)
#define  GRF_EMMCPHY_CON6_PDB_ON		(1 << 0)
#define  GRF_EMMCPHY_CON6_ENDLL_OFF		(0 << 1)
#define  GRF_EMMCPHY_CON6_ENDLL_ON		(1 << 1)
#define  GRF_EMMCPHY_CON6_DR_50OHM		(0x0 << 4)
#define  GRF_EMMCPHY_CON6_DR_33OHM		(0x1 << 4)
#define  GRF_EMMCPHY_CON6_DR_66OHM		(0x2 << 4)
#define  GRF_EMMCPHY_CON6_DR_100OHM		(0x3 << 4)
#define  GRF_EMMCPHY_CON6_DR_40OHM		(0x4 << 4)
#define  GRF_EMMCPHY_CON6_PDB_CLR		(1 << 16)
#define  GRF_EMMCPHY_CON6_ENDLL_CLR		(1 << 17)
#define  GRF_EMMCPHY_CON6_DR_CLR		(0x7 << 20)
#define GRF_EMMCPHY_STATUS		0x20
#define  GRF_EMMCPHY_STATUS_DLLRDY		(1 << 5)
#define  GRF_EMMCPHY_STATUS_CALDONE		(1 << 6)

#define HREAD4(sc, reg)							\
	(regmap_read_4((sc)->sc_rm, (sc)->sc_off + (reg)))
#define HWRITE4(sc, reg, val)						\
	regmap_write_4((sc)->sc_rm, (sc)->sc_off + (reg), (val))

struct rkemmcphy_softc {
	struct device		sc_dev;
	struct regmap		*sc_rm;
	bus_size_t		sc_off;

	struct phy_device	sc_pd;
};

int rkemmcphy_match(struct device *, void *, void *);
void rkemmcphy_attach(struct device *, struct device *, void *);

const struct cfattach	rkemmcphy_ca = {
	sizeof (struct rkemmcphy_softc), rkemmcphy_match, rkemmcphy_attach
};

struct cfdriver rkemmcphy_cd = {
	NULL, "rkemmcphy", DV_DULL
};

int	rkemmcphy_enable(void *, uint32_t *);

int
rkemmcphy_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "rockchip,rk3399-emmc-phy");
}

void
rkemmcphy_attach(struct device *parent, struct device *self, void *aux)
{
	struct rkemmcphy_softc *sc = (struct rkemmcphy_softc *)self;
	struct fdt_attach_args *faa = aux;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}
	sc->sc_off = faa->fa_reg[0].addr;

	sc->sc_rm = regmap_bynode(OF_parent(faa->fa_node));
	if (sc->sc_rm == NULL) {
		printf(": can't map registers\n");
		return;
	}

	printf("\n");

	sc->sc_pd.pd_node = faa->fa_node;
	sc->sc_pd.pd_cookie = sc;
	sc->sc_pd.pd_enable = rkemmcphy_enable;
	phy_register(&sc->sc_pd);
}

int
rkemmcphy_enable(void *cookie, uint32_t *cells)
{
	struct rkemmcphy_softc *sc = cookie;
	uint32_t impedance, freqsel, freq, reg;
	int node = sc->sc_pd.pd_node;
	int i;

	impedance = OF_getpropint(node, "drive-impedance-ohm", 0);
	freq = clock_get_frequency(node, "emmcclk");

	switch (impedance) {
	case 100:
		impedance = GRF_EMMCPHY_CON6_DR_100OHM;
		break;
	case 66:
		impedance = GRF_EMMCPHY_CON6_DR_66OHM;
		break;
	case 50:
		impedance = GRF_EMMCPHY_CON6_DR_50OHM;
		break;
	case 40:
		impedance = GRF_EMMCPHY_CON6_DR_40OHM;
		break;
	case 33:
		impedance = GRF_EMMCPHY_CON6_DR_33OHM;
		break;
	default:
		impedance = GRF_EMMCPHY_CON6_DR_50OHM;
		break;
	}

	if (freq == 0) {
		freqsel = GRF_EMMCPHY_CON0_FREQSEL_200M;
	} else if (freq < 75000000) {
		freqsel = GRF_EMMCPHY_CON0_FREQSEL_50M;
	} else if (freq < 125000000) {
		freqsel = GRF_EMMCPHY_CON0_FREQSEL_100M;
	} else if (freq < 175000000) {
		freqsel = GRF_EMMCPHY_CON0_FREQSEL_150M;
	} else {
		freqsel = GRF_EMMCPHY_CON0_FREQSEL_200M;
	}

	HWRITE4(sc, GRF_EMMCPHY_CON6, GRF_EMMCPHY_CON6_DR_CLR | impedance);

	HWRITE4(sc, GRF_EMMCPHY_CON0,
	    GRF_EMMCPHY_CON0_OTAPDLYENA_CLR | GRF_EMMCPHY_CON0_OTAPDLYENA_EN);
	HWRITE4(sc, GRF_EMMCPHY_CON0,
	    GRF_EMMCPHY_CON0_OTAPDLYSEL_CLR | GRF_EMMCPHY_CON0_OTAPDLYSEL_4);

	HWRITE4(sc, GRF_EMMCPHY_CON6,
	    GRF_EMMCPHY_CON6_PDB_CLR | GRF_EMMCPHY_CON6_PDB_OFF |
	    GRF_EMMCPHY_CON6_ENDLL_CLR | GRF_EMMCPHY_CON6_ENDLL_OFF);

	delay(3);
	HWRITE4(sc, GRF_EMMCPHY_CON6, GRF_EMMCPHY_CON6_PDB_CLR |
	    GRF_EMMCPHY_CON6_PDB_ON);

	for (i = 5; i > 0; i--) {
		reg = HREAD4(sc, GRF_EMMCPHY_STATUS);
		if (reg & GRF_EMMCPHY_STATUS_CALDONE)
			break;
		delay(10);
	}
	if (i == 0)
		printf("%s: timeout\n", sc->sc_dev.dv_xname);

	HWRITE4(sc, GRF_EMMCPHY_CON0, GRF_EMMCPHY_CON0_FREQSEL_CLR | freqsel);
	HWRITE4(sc, GRF_EMMCPHY_CON6, GRF_EMMCPHY_CON6_ENDLL_CLR |
	    GRF_EMMCPHY_CON6_ENDLL_ON);

	if (freq != 0) {
		for (i = 5; i > 0; i--) {
			reg = HREAD4(sc, GRF_EMMCPHY_STATUS);
			if (reg & GRF_EMMCPHY_STATUS_DLLRDY)
				break;
			delay(10 * 1000);
		}
		if (i == 0)
			printf("%s: timeout\n", sc->sc_dev.dv_xname);
	}

	return 0;
}
