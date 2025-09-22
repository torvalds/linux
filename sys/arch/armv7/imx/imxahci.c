/* $OpenBSD: imxahci.c,v 1.13 2021/10/24 17:52:27 mpi Exp $ */
/*
 * Copyright (c) 2013 Patrick Wildt <patrick@blueri.se>
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

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ic/ahcireg.h>
#include <dev/ic/ahcivar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

/* registers */
#define SATA_CAP		0x000
#define SATA_GHC		0x004
#define SATA_IS			0x008
#define SATA_PI			0x00C
#define SATA_VS			0x010
#define SATA_CCC_CTL		0x014
#define SATA_CCC_PORTS		0x018
#define SATA_CAP2		0x024
#define SATA_BISTAFR		0x0A0
#define SATA_BISTCR		0x0A4
#define SATA_BISTFCTR		0x0A8
#define SATA_BSTSR		0x0AC
#define SATA_OOBR		0x0BC
#define SATA_GPCR		0x0D0
#define SATA_GPSR		0x0D4
#define SATA_TIMER1MS		0x0E0
#define SATA_TESTR		0x0F4
#define SATA_VERSIONR		0x0F8
#define SATA_P0CLB		0x100
#define SATA_P0FB		0x108
#define SATA_P0IS		0x110
#define SATA_P0IE		0x114
#define SATA_P0CMD		0x118
#define SATA_P0TFD		0x120
#define SATA_P0SIG		0x124
#define SATA_P0SSTS		0x128
#define SATA_P0SCTL		0x12C
#define SATA_P0SERR		0x130
#define SATA_P0SACT		0x134
#define SATA_P0CI		0x138
#define SATA_P0SNTF		0x13C
#define SATA_P0DMACR		0x170
#define SATA_P0PHYCR		0x178
#define SATA_P0PHYSR		0x17C

#define SATA_CAP_SSS		(1 << 27)
#define SATA_GHC_HR		(1 << 0)
#define SATA_P0PHYCR_TEST_PDDQ	(1 << 20)

/* iomuxc */
#define IOMUXC_GPR13				0x034
#define  IOMUXC_GPR13_SATA_PHY_1_TX_EDGE_RATE		(1 << 0)
#define  IOMUXC_GPR13_SATA_PHY_1_MPLL_CLK_EN		(1 << 1)
#define  IOMUXC_GPR13_SATA_PHY_2_1104V			(0x11 << 2)
#define  IOMUXC_GPR13_SATA_PHY_3_333DB			(0x00 << 7)
#define  IOMUXC_GPR13_SATA_PHY_4_9_16			(0x04 << 11)
#define  IOMUXC_GPR13_SATA_PHY_5_SS			(0x01 << 14)
#define  IOMUXC_GPR13_SATA_SPEED_3G			(0x01 << 15)
#define  IOMUXC_GPR13_SATA_PHY_6				(0x03 << 16)
#define  IOMUXC_GPR13_SATA_PHY_7_SATA2M			(0x12 << 19)
#define  IOMUXC_GPR13_SATA_PHY_8_30DB			(0x05 << 24)
#define  IOMUXC_GPR13_SATA_MASK				0x07FFFFFF

int	imxahci_match(struct device *, void *, void *);
void	imxahci_attach(struct device *, struct device *, void *);
int	imxahci_detach(struct device *, int);
int	imxahci_activate(struct device *, int);

extern int ahci_intr(void *);

struct imxahci_softc {
	struct ahci_softc	sc;
};

const struct cfattach imxahci_ca = {
	sizeof(struct imxahci_softc),
	imxahci_match,
	imxahci_attach,
	imxahci_detach,
	imxahci_activate
};

struct cfdriver imxahci_cd = {
	NULL, "imxahci", DV_DULL
};

int
imxahci_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "fsl,imx6q-ahci");
}

void
imxahci_attach(struct device *parent, struct device *self, void *aux)
{
	struct imxahci_softc *imxsc = (struct imxahci_softc *) self;
	struct ahci_softc *sc = &imxsc->sc;
	struct fdt_attach_args *faa = aux;
	uint32_t timeout = 0x100000;
	struct regmap *rm;
	uint32_t reg;

	if (faa->fa_nreg < 1)
		return;

	sc->sc_iot = faa->fa_iot;
	sc->sc_ios = faa->fa_reg[0].size;
	sc->sc_dmat = faa->fa_dmat;

	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh))
		panic("imxahci_attach: bus_space_map failed!");

	sc->sc_ih = arm_intr_establish_fdt(faa->fa_node, IPL_BIO,
	    ahci_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": unable to establish interrupt\n");
		goto unmap;
	}

	/* power it up */
	clock_enable(faa->fa_node, "sata_ref");
	clock_enable(faa->fa_node, "sata");
	delay(100);

	/* power phy up */
	rm = regmap_bycompatible("fsl,imx6q-iomuxc-gpr");
	if (rm != NULL) {
		reg = regmap_read_4(rm, IOMUXC_GPR13);
		reg &= ~IOMUXC_GPR13_SATA_MASK;
		reg |= IOMUXC_GPR13_SATA_PHY_2_1104V |
		    IOMUXC_GPR13_SATA_PHY_3_333DB |
		    IOMUXC_GPR13_SATA_PHY_4_9_16 |
		    IOMUXC_GPR13_SATA_SPEED_3G |
		    IOMUXC_GPR13_SATA_PHY_6 |
		    IOMUXC_GPR13_SATA_PHY_7_SATA2M |
		    IOMUXC_GPR13_SATA_PHY_8_30DB;
		regmap_write_4(rm, IOMUXC_GPR13, reg);
		reg = regmap_read_4(rm, IOMUXC_GPR13);
		reg |= IOMUXC_GPR13_SATA_PHY_1_MPLL_CLK_EN;
		regmap_write_4(rm, IOMUXC_GPR13, reg);
	}

	/* setup */
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, SATA_P0PHYCR,
	    bus_space_read_4(sc->sc_iot, sc->sc_ioh, SATA_P0PHYCR) & ~SATA_P0PHYCR_TEST_PDDQ);

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, SATA_GHC, SATA_GHC_HR);

	while (!bus_space_read_4(sc->sc_iot, sc->sc_ioh, SATA_VERSIONR));

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, SATA_CAP,
	    bus_space_read_4(sc->sc_iot, sc->sc_ioh, SATA_CAP) | SATA_CAP_SSS);

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, SATA_PI, 1);

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, SATA_TIMER1MS,
	    clock_get_frequency(faa->fa_node, "ahb"));

	while (!(bus_space_read_4(sc->sc_iot, sc->sc_ioh, SATA_P0SSTS) & 0xF) && timeout--);

	printf(":");

	if (ahci_attach(sc) != 0) {
		/* error printed by ahci_attach */
		goto irq;
	}

	return;
irq:
	arm_intr_disestablish(sc->sc_ih);
unmap:
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_ios);
}

int
imxahci_detach(struct device *self, int flags)
{
	struct imxahci_softc *imxsc = (struct imxahci_softc *) self;
	struct ahci_softc *sc = &imxsc->sc;

	ahci_detach(sc, flags);
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_ios);
	return 0;
}

int
imxahci_activate(struct device *self, int act)
{
	struct imxahci_softc *imxsc = (struct imxahci_softc *) self;
	struct ahci_softc *sc = &imxsc->sc;

	return ahci_activate((struct device *)sc, act);
}
