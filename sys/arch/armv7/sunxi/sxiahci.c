/*	$OpenBSD: sxiahci.c,v 1.17 2021/10/24 17:52:28 mpi Exp $	*/
/*
 * Copyright (c) 2013 Patrick Wildt <patrick@blueri.se>
 * Copyright (c) 2013,2014 Artturi Alm
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

#include <dev/fdt/sunxireg.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_regulator.h>
#include <dev/ofw/fdt.h>

#define	SXIAHCI_CAP	0x0000
#define	SXIAHCI_GHC	0x0004
#define	SXIAHCI_PI	0x000c
#define	SXIAHCI_PHYCS0	0x00c0
#define	SXIAHCI_PHYCS1	0x00c4
#define	SXIAHCI_PHYCS2	0x00c8
#define	SXIAHCI_TIMER1MS	0x00e0
#define	SXIAHCI_RWC	0x00fc
#define	SXIAHCI_TIMEOUT	0x100000
#define SXIAHCI_PWRPIN	40

#define SXIAHCI_PREG_DMA	0x70
#define  SXIAHCI_PREG_DMA_MASK	(0xff<<8)
#define  SXIAHCI_PREG_DMA_INIT	(0x44<<8)

int	sxiahci_match(struct device *, void *, void *);
void	sxiahci_attach(struct device *, struct device *, void *);
int	sxiahci_detach(struct device *, int);
int	sxiahci_activate(struct device *, int);
int	sxiahci_port_start(struct ahci_port *, int);

extern int ahci_intr(void *);
extern u_int32_t ahci_read(struct ahci_softc *, bus_size_t);
extern void ahci_write(struct ahci_softc *, bus_size_t, u_int32_t);
extern u_int32_t ahci_pread(struct ahci_port *, bus_size_t);
extern void ahci_pwrite(struct ahci_port *, bus_size_t, u_int32_t);
extern int ahci_default_port_start(struct ahci_port *, int);

struct sxiahci_softc {
	struct ahci_softc	sc;

};

const struct cfattach sxiahci_ca = {
	sizeof(struct sxiahci_softc),
	sxiahci_match,
	sxiahci_attach,
	sxiahci_detach,
	sxiahci_activate
};

struct cfdriver sxiahci_cd = {
	NULL, "sxiahci", DV_DULL
};

int
sxiahci_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "allwinner,sun4i-a10-ahci") ||
	    OF_is_compatible(faa->fa_node, "allwinner,sun8i-r40-ahci");
}

void
sxiahci_attach(struct device *parent, struct device *self, void *aux)
{
	struct sxiahci_softc *sxisc = (struct sxiahci_softc *)self;
	struct ahci_softc *sc = &sxisc->sc;
	struct fdt_attach_args *faa = aux;
	uint32_t target_supply;
	uint32_t timo;

	if (faa->fa_nreg < 1)
		return;

	sc->sc_iot = faa->fa_iot;
	sc->sc_ios = faa->fa_reg[0].size;
	sc->sc_dmat = faa->fa_dmat;

	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh))
		panic("sxiahci_attach: bus_space_map failed!");

	/* enable clocks */
	clock_enable_all(faa->fa_node);
	delay(5000);

	reset_deassert_all(faa->fa_node);

	/* XXX setup magix */
	SXIWRITE4(sc, SXIAHCI_RWC, 0);
	delay(10);

	SXISET4(sc, SXIAHCI_PHYCS1, 1 << 19);
	delay(10);

	SXICMS4(sc, SXIAHCI_PHYCS0, 7 << 24,
	    1 << 23 | 5 << 24 | 1 << 18);
	delay(10);

	SXICMS4(sc, SXIAHCI_PHYCS1,
	    3 << 16 | 0x1f << 8 | 3 << 6,
	    2 << 16 | 0x06 << 8 | 2 << 6);
	delay(10);

	SXISET4(sc, SXIAHCI_PHYCS1, 1 << 28 | 1 << 15);
	delay(10);

	SXICLR4(sc, SXIAHCI_PHYCS1, 1 << 19);
	delay(10);

	SXICMS4(sc, SXIAHCI_PHYCS0, 0x07 << 20, 0x03 << 20);
	SXICMS4(sc, SXIAHCI_PHYCS2, 0x1f <<  5, 0x19 << 5);
	delay(5000);

	SXISET4(sc, SXIAHCI_PHYCS0, 1 << 19);
	delay(20);

	timo = SXIAHCI_TIMEOUT;
	while ((SXIREAD4(sc, SXIAHCI_PHYCS0) >> 28 & 7) != 2 && --timo)
		delay(10);
	if (!timo) {
		printf(": AHCI phy power up failed.\n");
		goto dismod;
	}

	SXISET4(sc, SXIAHCI_PHYCS2, 1 << 24);

	timo = SXIAHCI_TIMEOUT;
	while ((SXIREAD4(sc, SXIAHCI_PHYCS2) & (1 << 24)) && --timo)
		delay(10);
	if (!timo) {
		printf(": AHCI phy calibration failed.\n");
		goto dismod;
	}

	delay(15000);
	SXIWRITE4(sc, SXIAHCI_RWC, 7);

	/* power up phy */
	target_supply = OF_getpropint(faa->fa_node, "target-supply", 0);
	if (target_supply)
		regulator_enable(target_supply);

	sc->sc_ih = arm_intr_establish_fdt(faa->fa_node, IPL_BIO,
	    ahci_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": unable to establish interrupt\n");
		goto clrpwr;
	}

	printf(":");

	SXIWRITE4(sc, SXIAHCI_PI, 1);
	SXICLR4(sc, SXIAHCI_CAP, AHCI_REG_CAP_SPM);
	sc->sc_flags |= AHCI_F_NO_PMP;
	sc->sc_port_start = sxiahci_port_start;
	if (ahci_attach(sc) != 0) {
		/* error printed by ahci_attach */
		goto irq;
	}

	return;
irq:
	arm_intr_disestablish(sc->sc_ih);
clrpwr:
	if (target_supply)
		regulator_disable(target_supply);
dismod:
	clock_disable_all(faa->fa_node);
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_ios);
}

int
sxiahci_detach(struct device *self, int flags)
{
	struct sxiahci_softc *sxisc = (struct sxiahci_softc *) self;
	struct ahci_softc *sc = &sxisc->sc;

	ahci_detach(sc, flags);
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_ios);
	return 0;
}

int
sxiahci_activate(struct device *self, int act)
{
	struct sxiahci_softc *sxisc = (struct sxiahci_softc *) self;
	struct ahci_softc *sc = &sxisc->sc;

	return ahci_activate((struct device *)sc, act);
}

int
sxiahci_port_start(struct ahci_port *ap, int fre_only)
{
	uint32_t r;

	/* Setup DMA */
	r = ahci_pread(ap, SXIAHCI_PREG_DMA);
	r &= ~SXIAHCI_PREG_DMA_MASK;
	r |= SXIAHCI_PREG_DMA_INIT; /* XXX if fre_only? */
	ahci_pwrite(ap, SXIAHCI_PREG_DMA, r);

	return (ahci_default_port_start(ap, fre_only));
}
