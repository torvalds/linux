/*	$OpenBSD: cdsdhc.c,v 1.2 2022/01/18 11:36:21 patrick Exp $	*/

/*
 * Copyright (c) 2022 Visa Hankala
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
 * Driver glue for Cadence SD/SDIO/eMMC host controller.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/device.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/fdt.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>

#include <dev/sdmmc/sdhcvar.h>
#include <dev/sdmmc/sdmmcvar.h>

/* Host Register Set */
#define HRS06				0x0018
#define  HRS06_ETR				(0x1 << 15)
#define  HRS06_ETV_MASK				(0x3f << 8)
#define  HRS06_ETV_SHIFT			8
#define  HRS06_EMM_MASK				(0x7 << 0)
#define  HRS06_EMM_SD				(0x0 << 0)
#define  HRS06_EMM_MMC_SDR			(0x2 << 0)
#define  HRS06_EMM_MMC_DDR			(0x3 << 0)
#define  HRS06_EMM_MMC_HS200			(0x4 << 0)
#define  HRS06_EMM_MMC_HS400			(0x5 << 0)
#define  HRS06_EMM_MMC_HS400_ENH		(0x6 << 0)
#define HRS31				0x007c
#define  HRS31_HOSTCTLVER(x)			(((x) >> 16) & 0xfff)
#define  HRS31_HOSTFIXVER(x)			((x) & 0xff)

/* Slot Register Set */
#define SRS_OFFSET			0x200
#define SRS_SIZE			0x100

struct cdsdhc_softc {
	struct sdhc_softc	sc_sdhc;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_space_handle_t	sc_srs_ioh;
	void			*sc_ih;

	struct sdhc_host	*sc_host;
};

#define HREAD4(sc, reg) \
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val) \
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

int	cdsdhc_match(struct device *, void *, void*);
void	cdsdhc_attach(struct device *, struct device *, void *);
void	cdsdhc_bus_clock_pre(struct sdhc_softc *, int, int);

const struct cfattach cdsdhc_ca = {
	sizeof(struct cdsdhc_softc), cdsdhc_match, cdsdhc_attach
};

struct cfdriver cdsdhc_cd = {
	NULL, "cdsdhc", DV_DULL
};

int
cdsdhc_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	if (faa->fa_nreg < 1)
		return 0;
	return OF_is_compatible(faa->fa_node, "cdns,sd4hc");
}

void
cdsdhc_attach(struct device *parent, struct device *self, void *aux)
{
	struct fdt_attach_args *faa = aux;
	struct cdsdhc_softc *sc = (struct cdsdhc_softc *)self;
	uint64_t capmask = 0, capset = 0;
	uint32_t ver;

	sc->sc_iot = faa->fa_iot;

	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr, faa->fa_reg[0].size,
	    0, &sc->sc_ioh) != 0) {
		printf(": can't map registers\n");
		return;
	}

	if (bus_space_subregion(sc->sc_iot, sc->sc_ioh, SRS_OFFSET, SRS_SIZE,
	    &sc->sc_srs_ioh) != 0) {
		printf(": can't map SRS subregion\n");
		goto unmap;
	}

	clock_enable_all(faa->fa_node);

	sc->sc_ih = fdt_intr_establish(faa->fa_node, IPL_BIO,
	    sdhc_intr, sc, sc->sc_sdhc.sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		goto disable;
	}

	ver = HREAD4(sc, HRS31);
	printf(": rev 0x%x/0x%x\n", HRS31_HOSTCTLVER(ver),
	    HRS31_HOSTFIXVER(ver));

	sc->sc_sdhc.sc_host = &sc->sc_host;
	sc->sc_sdhc.sc_dmat = faa->fa_dmat;
	sc->sc_sdhc.sc_bus_clock_pre = cdsdhc_bus_clock_pre;
	sdhc_host_found(&sc->sc_sdhc, sc->sc_iot, sc->sc_srs_ioh, SRS_SIZE,
	    1, capmask, capset);
	return;

disable:
	clock_disable_all(faa->fa_node);
unmap:
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, faa->fa_reg[0].size);
}

void
cdsdhc_bus_clock_pre(struct sdhc_softc *sc_sdhc, int freq, int timing)
{
	struct cdsdhc_softc *sc = (struct cdsdhc_softc *)sc_sdhc;
	uint32_t mode, val;

	switch (timing) {
	case SDMMC_TIMING_HIGHSPEED:
		mode = HRS06_EMM_MMC_SDR;
		break;
	case SDMMC_TIMING_MMC_DDR52:
		mode = HRS06_EMM_MMC_DDR;
		break;
	case SDMMC_TIMING_MMC_HS200:
		mode = HRS06_EMM_MMC_HS200;
		break;
	default:
		mode = HRS06_EMM_SD;
		break;
	}

	val = HREAD4(sc, HRS06);
	val &= ~HRS06_EMM_MASK;
	val |= mode;
	HWRITE4(sc, HRS06, val);
}
