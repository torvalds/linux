/*      $OpenBSD: amdpcib.c,v 1.4 2022/03/11 18:00:45 mpi Exp $	*/

/*
 * Copyright (c) 2007 Michael Shalayeff
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * AMD8111 series LPC bridge also containing HPET and watchdog
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/timetc.h>

#include <machine/bus.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#define	AMD8111_HPET	0xa0	/* PCI config space */
#define	AMD8111_HPET_ENA	0x00000001
#define	AMD8111_HPET_BASE	0xfffffc00
#define	AMD8111_HPET_SIZE	0x00000400

#define	AMD8111_HPET_ID		0x000
#define	AMD8111_HPET_WIDTH	0x00002000
#define	AMD8111_HPET_REV	0x000000ff
#define	AMD8111_HPET_PERIOD	0x004
#define	AMD8111_HPET_CFG	0x010
#define	AMD8111_HPET_CFG_GIEN	0x00000001
#define	AMD8111_HPET_ISTAT	0x020
#define	AMD8111_HPET_MAIN	0x0f0
#define	AMD8111_HPET_T0CFG	0x100
#define	AMD8111_HPET_T0CMP	0x108
#define	AMD8111_HPET_T1CFG	0x120
#define	AMD8111_HPET_T1CMP	0x128
#define	AMD8111_HPET_T2CFG	0x140
#define	AMD8111_HPET_T2CMP	0x148

#define	AMD8111_WDOG	0xa8	/* PCI config space */
#define	AMD8111_WDOG_ENA	0x00000001
#define	AMD8111_WDOG_HALT	0x00000002
#define	AMD8111_WDOG_SILENT	0x00000004
#define	AMD8111_WDOG_BASE	0xffffffe0

#define	AMD8111_WDOG_CTRL	0x00
#define	AMD8111_WDOG_RSTOP	0x0001
#define	AMD8111_WDOG_WFIR	0x0002
#define	AMD8111_WDOG_WACT	0x0004
#define	AMD8111_WDOG_WDALIAS	0x0008
#define	AMD8111_WDOG_WTRIG	0x0080
#define	AMD8111_WDOG_COUNT	0x08
#define	AMD8111_WDOG_MASK	0xffff

struct amdpcib_softc {
	struct device sc_dev;

	bus_space_tag_t sc_hpet_iot;
	bus_space_handle_t sc_hpet_ioh;
	struct timecounter sc_hpet_timecounter;
};

struct cfdriver amdpcib_cd = {
	NULL, "amdpcib", DV_DULL
};

int	amdpcib_match(struct device *, void *, void *);
void	amdpcib_attach(struct device *, struct device *, void *);

const struct cfattach amdpcib_ca = {
	sizeof(struct amdpcib_softc), amdpcib_match, amdpcib_attach
};

/* from arch/<*>/pci/pcib.c */
void	pcibattach(struct device *parent, struct device *self, void *aux);

u_int	amdpcib_get_timecount(struct timecounter *tc);

const struct pci_matchid amdpcib_devices[] = {
	{ PCI_VENDOR_AMD,	PCI_PRODUCT_AMD_PBC8111_LPC }
	/* also available on 590 and 690 chipsets */
};

int
amdpcib_match(struct device *parent, void *match, void *aux)
{ 
	if (pci_matchbyid((struct pci_attach_args *)aux, amdpcib_devices,
	    sizeof(amdpcib_devices) / sizeof(amdpcib_devices[0])))
		return 2;

	return 0;
}

void
amdpcib_attach(struct device *parent, struct device *self, void *aux)
{
        struct amdpcib_softc *sc = (struct amdpcib_softc *)self;
	struct pci_attach_args *pa = aux;
	struct timecounter *tc = &sc->sc_hpet_timecounter;
	pcireg_t reg;
	u_int32_t v;


	sc->sc_hpet_iot = pa->pa_memt;
	reg = pci_conf_read(pa->pa_pc, pa->pa_tag, AMD8111_HPET);
	if (reg & AMD8111_HPET_ENA &&
	    bus_space_map(sc->sc_hpet_iot, reg & AMD8111_HPET_BASE,
	     AMD8111_HPET_SIZE, 0, &sc->sc_hpet_ioh) == 0) {

		tc->tc_get_timecount = amdpcib_get_timecount;
		/* XXX 64-bit counter is not supported! */
		tc->tc_counter_mask = 0xffffffff;

		v = bus_space_read_4(sc->sc_hpet_iot, sc->sc_hpet_ioh,
		    AMD8111_HPET_PERIOD);
		/* femtosecs -> Hz */
		tc->tc_frequency = 1000000000000000ULL / v;

		tc->tc_name = "AMD8111";
		tc->tc_quality = 2000;
		tc->tc_priv = sc;
		tc_init(tc);

		/* enable counting */
		bus_space_write_4(sc->sc_hpet_iot, sc->sc_hpet_ioh,
		    AMD8111_HPET_CFG, AMD8111_HPET_CFG_GIEN);

		v = bus_space_read_4(sc->sc_hpet_iot, sc->sc_hpet_ioh,
		    AMD8111_HPET_ID);
		printf(": %d-bit %lluHz timer rev %d",
		    (v & AMD8111_HPET_WIDTH? 64 : 32), tc->tc_frequency,
		    v & AMD8111_HPET_REV);
	}

	pcibattach(parent, self, aux);
}

u_int
amdpcib_get_timecount(struct timecounter *tc)
{
        struct amdpcib_softc *sc = tc->tc_priv;

	/* XXX 64-bit counter is not supported! */
	return bus_space_read_4(sc->sc_hpet_iot, sc->sc_hpet_ioh,
	    AMD8111_HPET_MAIN);
}
