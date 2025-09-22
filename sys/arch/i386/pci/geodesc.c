/*	$OpenBSD: geodesc.c,v 1.18 2023/02/04 19:19:36 cheloha Exp $	*/

/*
 * Copyright (c) 2003 Markus Friedl <markus@openbsd.org>
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
 * Geode SC1100 Information Appliance On a Chip
 * http://www.national.com/ds.cgi/SC/SC1100.pdf
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/timetc.h>

#include <machine/bus.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <arch/i386/pci/geodescreg.h>

struct geodesc_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
};

int	geodesc_match(struct device *, void *, void *);
void	geodesc_attach(struct device *, struct device *, void *);
int	geodesc_activate(struct device *, int);
void	sc1100_sysreset(void);

#ifndef SMALL_KERNEL
int	geodesc_wdogctl_cb(void *, int);
#endif /* SMALL_KERNEL */

const struct cfattach geodesc_ca = {
	sizeof(struct geodesc_softc), geodesc_match, geodesc_attach,
	NULL, geodesc_activate
};

struct cfdriver geodesc_cd = {
	NULL, "geodesc", DV_DULL
};

u_int   geodesc_get_timecount(struct timecounter *tc);

struct timecounter geodesc_timecounter = {
	.tc_get_timecount = geodesc_get_timecount,
	.tc_counter_mask = 0xffffffff,
	.tc_frequency = 27000000,
	.tc_name = "GEOTSC",
	.tc_quality = 2000,
	.tc_priv = NULL,
	.tc_user = 0,
};

int
geodesc_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_NS &&
	    (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_NS_SC1100_XBUS ||
	     PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_NS_SCX200_XBUS))
		return (1);
	return (0);
}

#define WDSTSBITS "\20\x04WDRST\x03WDSMI\x02WDINT\x01WDOVF"

void
geodesc_attach(struct device *parent, struct device *self, void *aux)
{
	struct geodesc_softc *sc = (void *) self;
	struct pci_attach_args *pa = aux;
	uint16_t cnfg, cba;
	uint8_t sts, rev, iid;
	pcireg_t reg;
	extern void (*cpuresetfn)(void);

	reg = pci_conf_read(pa->pa_pc, pa->pa_tag, SC1100_F5_SCRATCHPAD);
	sc->sc_iot = pa->pa_iot;
	if (reg == 0 ||
	    bus_space_map(sc->sc_iot, reg, 64, 0, &sc->sc_ioh)) {
		printf(": unable to map registers at 0x%x\n", reg);
		return;
	}
	cba = bus_space_read_2(sc->sc_iot, sc->sc_ioh, GCB_CBA);
	if (cba != reg) {
		printf(": cba mismatch: cba 0x%x != reg 0x%x\n", cba, reg);
		bus_space_unmap(sc->sc_iot, sc->sc_ioh, 64);
		return;
	}
	sts = bus_space_read_1(sc->sc_iot, sc->sc_ioh, GCB_WDSTS);
	cnfg = bus_space_read_2(sc->sc_iot, sc->sc_ioh, GCB_WDCNFG);
	iid = bus_space_read_1(sc->sc_iot, sc->sc_ioh, GCB_IID);
	rev = bus_space_read_1(sc->sc_iot, sc->sc_ioh, GCB_REV);

	printf(": iid %d revision %d wdstatus %b\n", iid, rev, sts, WDSTSBITS);

#ifndef SMALL_KERNEL
	/* setup and register watchdog */
	bus_space_write_2(sc->sc_iot, sc->sc_ioh, GCB_WDTO, 0);
	sts |= WDOVF_CLEAR;
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, GCB_WDSTS, sts);
	cnfg &= ~WDCNFG_MASK;
	cnfg |= WDTYPE1_RESET|WDPRES_DIV_512;
	bus_space_write_2(sc->sc_iot, sc->sc_ioh, GCB_WDCNFG, cnfg);

	wdog_register(geodesc_wdogctl_cb, sc);
#endif /* SMALL_KERNEL */

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, GCB_TSCNFG, TSC_ENABLE);
	/* Hook into the kern_tc */
	geodesc_timecounter.tc_priv = sc;
	tc_init(&geodesc_timecounter);

	/* We have a special way to reset the CPU on the SC1100 */
	cpuresetfn = sc1100_sysreset;
}

int
geodesc_activate(struct device *self, int act)
{
	switch (act) {
	case DVACT_POWERDOWN:
#ifndef SMALL_KERNEL
		wdog_shutdown(self);
#endif
		break;
	}

	return (0);
}

#ifndef SMALL_KERNEL
int
geodesc_wdogctl_cb(void *self, int period)
{
	struct geodesc_softc *sc = self;

	if (period > 0x03ff)
		period = 0x03ff;
	bus_space_write_2(sc->sc_iot, sc->sc_ioh, GCB_WDTO, period * 64);
	return (period);
}
#endif /* SMALL_KERNEL */

u_int
geodesc_get_timecount(struct timecounter *tc)
{
	struct geodesc_softc *sc = tc->tc_priv;

	return (bus_space_read_4(sc->sc_iot, sc->sc_ioh, GCB_TSC));
}

void
sc1100_sysreset(void)
{
	/*
	 * Reset AMD Geode SC1100.
	 *
	 * 1) Write PCI Configuration Address Register (0xcf8) to
	 *    select Function 0, Register 0x44: Bridge Configuration,
	 *    GPIO and LPC Configuration Register Space, Reset
	 *    Control Register.
	 *
	 * 2) Write 0xf to PCI Configuration Data Register (0xcfc)
	 *    to reset IDE controller, IDE bus, and PCI bus, and
	 *    to trigger a system-wide reset.
	 *
	 * See AMD Geode SC1100 Processor Data Book, Revision 2.0,
	 * sections 6.3.1, 6.3.2, and 6.4.1.
	 */
	outl(0xCF8, 0x80009044UL);
	outb(0xCFC, 0x0F);
}
