/*	$OpenBSD: lm78_isa.c,v 1.12 2022/04/06 18:59:28 naddy Exp $	*/

/*
 * Copyright (c) 2005, 2006 Mark Kettenis
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
#include <sys/sensors.h>
#include <machine/bus.h>

#include <dev/isa/isavar.h>

#include <dev/ic/lm78var.h>

/* ISA registers */
#define LMC_ADDR	0x05
#define LMC_DATA	0x06

extern struct cfdriver lm_cd;

#if defined(LMDEBUG)
#define DPRINTF(x)		do { printf x; } while (0)
#else
#define DPRINTF(x)
#endif

struct lm_isa_softc {
	struct lm_softc sc_lmsc;

	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_ioh;
};

int  lm_isa_match(struct device *, void *, void *);
int  lm_wbsio_match(struct device *, void *, void *);
void lm_isa_attach(struct device *, struct device *, void *);
u_int8_t lm_isa_readreg(struct lm_softc *, int);
void lm_isa_writereg(struct lm_softc *, int, int);
void lm_isa_remove_alias(struct lm_softc *, const char *);

const struct cfattach lm_isa_ca = {
	sizeof(struct lm_isa_softc),
	lm_isa_match,
	lm_isa_attach
};

const struct cfattach lm_wbsio_ca = {
	sizeof(struct lm_isa_softc),
	lm_wbsio_match,
	lm_isa_attach
};

int
lm_wbsio_match(struct device *parent, void *match, void *aux)
{
	bus_space_tag_t iot;
	bus_addr_t iobase;
	bus_space_handle_t ioh;
	struct isa_attach_args *ia = aux;
	int banksel, vendid;

	iot = ia->ia_iot;
	iobase = ia->ipa_io[0].base;

	if (bus_space_map(iot, iobase, 8, 0, &ioh)) {
		DPRINTF(("%s: can't map i/o space\n", __func__));
		return (0);
	}

	/* Probe for Winbond chips. */
	bus_space_write_1(iot, ioh, LMC_ADDR, WB_BANKSEL);
	banksel = bus_space_read_1(iot, ioh, LMC_DATA);
	bus_space_write_1(iot, ioh, LMC_ADDR, WB_BANKSEL);
	bus_space_write_1(iot, ioh, LMC_DATA, WB_BANKSEL_HBAC);
	bus_space_write_1(iot, ioh, LMC_ADDR, WB_VENDID);
	vendid = bus_space_read_1(iot, ioh, LMC_DATA) << 8;
	bus_space_write_1(iot, ioh, LMC_ADDR, WB_BANKSEL);
	bus_space_write_1(iot, ioh, LMC_DATA, 0);
	bus_space_write_1(iot, ioh, LMC_ADDR, WB_VENDID);
	vendid |= bus_space_read_1(iot, ioh, LMC_DATA);
	bus_space_write_1(iot, ioh, LMC_ADDR, WB_BANKSEL);
	bus_space_write_1(iot, ioh, LMC_DATA, banksel);

	bus_space_unmap(iot, ioh, 8);

	if (vendid != WB_VENDID_WINBOND)
		return (0);

	ia->ipa_nio = 1;
	ia->ipa_io[0].length = 8;

	ia->ipa_nmem = 0;
	ia->ipa_nirq = 0;
	ia->ipa_ndrq = 0;

	return (1);

}

int
lm_isa_match(struct device *parent, void *match, void *aux)
{
	bus_space_tag_t iot;
	bus_addr_t iobase;
	bus_space_handle_t ioh;
	struct isa_attach_args *ia = aux;
	int banksel, vendid, chipid, addr;

	iot = ia->ia_iot;
	iobase = ia->ipa_io[0].base;

	if (bus_space_map(iot, iobase, 8, 0, &ioh)) {
		DPRINTF(("%s: can't map i/o space\n", __func__));
		return (0);
	}

	/* Probe for Winbond chips. */
	bus_space_write_1(iot, ioh, LMC_ADDR, WB_BANKSEL);
	banksel = bus_space_read_1(iot, ioh, LMC_DATA);
	bus_space_write_1(iot, ioh, LMC_ADDR, WB_VENDID);
	vendid = bus_space_read_1(iot, ioh, LMC_DATA);
	if (((banksel & 0x80) && vendid == (WB_VENDID_WINBOND >> 8)) ||
	    (!(banksel & 0x80) && vendid == (WB_VENDID_WINBOND & 0xff)))
		goto found;

	/* Probe for ITE chips (and don't attach if we find one). */
	bus_space_write_1(iot, ioh, LMC_ADDR, 0x58);
	if ((vendid = bus_space_read_1(iot, ioh, LMC_DATA)) == 0x90)
		goto notfound;

	/*
	 * Probe for National Semiconductor LM78/79/81.
	 *
	 * XXX This assumes the address has not been changed from the
	 * power up default.  This is probably a reasonable
	 * assumption, and if it isn't true, we should be able to
	 * access the chip using the serial bus.
	 */
	bus_space_write_1(iot, ioh, LMC_ADDR, LM_SBUSADDR);
	addr = bus_space_read_1(iot, ioh, LMC_DATA);
	if ((addr & 0xfc) == 0x2c) {
		bus_space_write_1(iot, ioh, LMC_ADDR, LM_CHIPID);
		chipid = bus_space_read_1(iot, ioh, LMC_DATA);

		switch (chipid & LM_CHIPID_MASK) {
		case LM_CHIPID_LM78:
		case LM_CHIPID_LM78J:
		case LM_CHIPID_LM79:
		case LM_CHIPID_LM81:
			goto found;
		}
	}

 notfound:
	bus_space_unmap(iot, ioh, 8);

	return (0);

 found:
	bus_space_unmap(iot, ioh, 8);

	ia->ipa_nio = 1;
	ia->ipa_io[0].length = 8;

	ia->ipa_nmem = 0;
	ia->ipa_nirq = 0;
	ia->ipa_ndrq = 0;

	return (1);
}

void
lm_isa_attach(struct device *parent, struct device *self, void *aux)
{
	struct lm_isa_softc *sc = (struct lm_isa_softc *)self;
	struct isa_attach_args *ia = aux;
	struct lm_softc *lmsc;
	bus_addr_t iobase;
	int i;
	u_int8_t sbusaddr;

	sc->sc_iot = ia->ia_iot;
	iobase = ia->ipa_io[0].base;

	if (bus_space_map(sc->sc_iot, iobase, 8, 0, &sc->sc_ioh)) {
		printf(": can't map i/o space\n");
		return;
	}

	/* Bus-independent attachment */
	sc->sc_lmsc.lm_writereg = lm_isa_writereg;
	sc->sc_lmsc.lm_readreg = lm_isa_readreg;

	/* pass through wbsio(4) devid */
	if (ia->ia_aux)
		sc->sc_lmsc.sioid = (u_int8_t)(u_long)ia->ia_aux;

	lm_attach(&sc->sc_lmsc);

	/*
	 * Most devices supported by this driver can attach to iic(4)
	 * as well.  However, we prefer to attach them to isa(4) since
	 * that causes less overhead and is more reliable.  We look
	 * through all previously attached devices, and if we find an
	 * identical chip at the same serial bus address, we stop
	 * updating its sensors and mark them as invalid.
	 */

	sbusaddr = lm_isa_readreg(&sc->sc_lmsc, LM_SBUSADDR);
	if (sbusaddr == 0)
		return;

	for (i = 0; i < lm_cd.cd_ndevs; i++) {
		lmsc = lm_cd.cd_devs[i];
		if (lmsc == &sc->sc_lmsc)
			continue;
		if (lmsc && lmsc->sbusaddr == sbusaddr &&
		    lmsc->chipid == sc->sc_lmsc.chipid) {
			lm_isa_remove_alias(lmsc, sc->sc_lmsc.sc_dev.dv_xname);
			break;
		}
	}
}

/* Remove sensors of the i2c alias, since we prefer to use the isa access */
void
lm_isa_remove_alias(struct lm_softc *sc, const char *isa)
{
	int i;

	printf("%s: disabling sensors due to alias with %s\n",
	    sc->sc_dev.dv_xname, isa);
	sensordev_deinstall(&sc->sensordev);
	for (i = 0; i < sc->numsensors; i++)
		sensor_detach(&sc->sensordev, &sc->sensors[i]);
	if (sc->sensortask != NULL)
		sensor_task_unregister(sc->sensortask);
	sc->sensortask = NULL;
}

u_int8_t
lm_isa_readreg(struct lm_softc *lmsc, int reg)
{
	struct lm_isa_softc *sc = (struct lm_isa_softc *)lmsc;

	bus_space_write_1(sc->sc_iot, sc->sc_ioh, LMC_ADDR, reg);
	return (bus_space_read_1(sc->sc_iot, sc->sc_ioh, LMC_DATA));
}

void
lm_isa_writereg(struct lm_softc *lmsc, int reg, int val)
{
	struct lm_isa_softc *sc = (struct lm_isa_softc *)lmsc;

	bus_space_write_1(sc->sc_iot, sc->sc_ioh, LMC_ADDR, reg);
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, LMC_DATA, val);
}
