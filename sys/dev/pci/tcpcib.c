/*	$OpenBSD: tcpcib.c,v 1.9 2022/03/11 18:00:52 mpi Exp $	*/

/*
 * Copyright (c) 2012 Matt Dainty <matt@bodgit-n-scarper.com>
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
 * Intel Atom E600 series LPC bridge also containing HPET and watchdog
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/timetc.h>

#include <machine/bus.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#define	E600_LPC_SMBA		0x40		/* SMBus Base Address */
#define	E600_LPC_GBA		0x44		/* GPIO Base Address */
#define	E600_LPC_WDTBA		0x84		/* WDT Base Address */

#define	E600_WDT_SIZE		64		/* I/O region size */
#define	E600_WDT_PV1		0x00		/* Preload Value 1 Register */
#define	E600_WDT_PV2		0x04		/* Preload Value 2 Register */
#define	E600_WDT_RR0		0x0c		/* Reload Register 0 */
#define	E600_WDT_RR1		0x0d		/* Reload Register 1 */
#define	E600_WDT_RR1_RELOAD	(1 << 0)	/* WDT Reload Flag */
#define	E600_WDT_RR1_TIMEOUT	(1 << 1)	/* WDT Timeout Flag */
#define	E600_WDT_WDTCR		0x10		/* WDT Configuration Register */
#define	E600_WDT_WDTCR_PRE	(1 << 2)	/* WDT Prescalar Select */
#define	E600_WDT_WDTCR_RESET	(1 << 3)	/* WDT Reset Select */
#define	E600_WDT_WDTCR_ENABLE	(1 << 4)	/* WDT Reset Enable */
#define	E600_WDT_WDTCR_TIMEOUT	(1 << 5)	/* WDT Timeout Output Enable */
#define	E600_WDT_DCR		0x14		/* Down Counter Register */
#define	E600_WDT_WDTLR		0x18		/* WDT Lock Register */
#define	E600_WDT_WDTLR_LOCK	(1 << 0)	/* Watchdog Timer Lock */
#define	E600_WDT_WDTLR_ENABLE	(1 << 1)	/* Watchdog Timer Enable */
#define	E600_WDT_WDTLR_TIMEOUT	(1 << 2)	/* WDT Timeout Configuration */

#define	E600_HPET_BASE		0xfed00000	/* HPET register base */
#define	E600_HPET_SIZE		0x00000400	/* HPET register size */

#define	E600_HPET_GCID		0x000		/* Capabilities and ID */
#define	E600_HPET_GCID_WIDTH	(1 << 13)	/* Counter Size */
#define	E600_HPET_PERIOD	0x004		/* Counter Tick Period */
#define	E600_HPET_GC		0x010		/* General Configuration */
#define	E600_HPET_GC_ENABLE	(1 << 0)	/* Overall Enable */
#define	E600_HPET_GIS		0x020		/* General Interrupt Status */
#define	E600_HPET_MCV		0x0f0		/* Main Counter Value */
#define	E600_HPET_T0C		0x100		/* Timer 0 Config and Capabilities */
#define	E600_HPET_T0CV		0x108		/* Timer 0 Comparator Value */
#define	E600_HPET_T1C		0x120		/* Timer 1 Config and Capabilities */
#define	E600_HPET_T1CV		0x128		/* Timer 1 Comparator Value */
#define	E600_HPET_T2C		0x140		/* Timer 2 Config and Capabilities */
#define	E600_HPET_T2CV		0x148		/* Timer 2 Comparator Value */

struct tcpcib_softc {
	struct device sc_dev;

	/* Keep track of which parts of the hardware are active */
	int sc_active;
#define	E600_WDT_ACTIVE		(1 << 0)
#define	E600_HPET_ACTIVE	(1 << 1)

	/* Watchdog interface */
	bus_space_tag_t sc_wdt_iot;
	bus_space_handle_t sc_wdt_ioh;

	int sc_wdt_period;

	/* High Precision Event Timer */
	bus_space_tag_t sc_hpet_iot;
	bus_space_handle_t sc_hpet_ioh;

	struct timecounter sc_hpet_timecounter;
};

struct cfdriver tcpcib_cd = {
	NULL, "tcpcib", DV_DULL
};

int	 tcpcib_match(struct device *, void *, void *);
void	 tcpcib_attach(struct device *, struct device *, void *);
int	 tcpcib_activate(struct device *, int);

int	 tcpcib_wdt_cb(void *, int);
void	 tcpcib_wdt_init(struct tcpcib_softc *, int);
void	 tcpcib_wdt_start(struct tcpcib_softc *);
void	 tcpcib_wdt_stop(struct tcpcib_softc *);

u_int	 tcpcib_hpet_get_timecount(struct timecounter *tc);

const struct cfattach tcpcib_ca = {
	sizeof(struct tcpcib_softc), tcpcib_match, tcpcib_attach,
	NULL, tcpcib_activate
};

/* from arch/<*>/pci/pcib.c */
void	pcibattach(struct device *parent, struct device *self, void *aux);

const struct pci_matchid tcpcib_devices[] = {
	{ PCI_VENDOR_INTEL,	PCI_PRODUCT_INTEL_E600_LPC }
};

static __inline void
tcpcib_wdt_unlock(struct tcpcib_softc *sc)
{
	/* Register unlocking sequence */
	bus_space_write_1(sc->sc_wdt_iot, sc->sc_wdt_ioh, E600_WDT_RR0, 0x80);
	bus_space_write_1(sc->sc_wdt_iot, sc->sc_wdt_ioh, E600_WDT_RR0, 0x86);
}

void
tcpcib_wdt_init(struct tcpcib_softc *sc, int period)
{
	u_int32_t preload;

	/* Set new timeout */
	preload = (period * 33000000) >> 15;
	preload--;

	/*
	 * Set watchdog to perform a cold reset toggling the GPIO pin and the
	 * prescaler set to 1ms-10m resolution
	 */
	bus_space_write_1(sc->sc_wdt_iot, sc->sc_wdt_ioh, E600_WDT_WDTCR,
	    E600_WDT_WDTCR_ENABLE);
	tcpcib_wdt_unlock(sc);
	bus_space_write_4(sc->sc_wdt_iot, sc->sc_wdt_ioh, E600_WDT_PV1, 0);
	tcpcib_wdt_unlock(sc);
	bus_space_write_4(sc->sc_wdt_iot, sc->sc_wdt_ioh, E600_WDT_PV2,
	    preload);
	tcpcib_wdt_unlock(sc);
	bus_space_write_1(sc->sc_wdt_iot, sc->sc_wdt_ioh, E600_WDT_RR1,
	    E600_WDT_RR1_RELOAD);
}

void
tcpcib_wdt_start(struct tcpcib_softc *sc)
{
	/* Enable watchdog */
	bus_space_write_1(sc->sc_wdt_iot, sc->sc_wdt_ioh, E600_WDT_WDTLR,
	    E600_WDT_WDTLR_ENABLE);
}

void
tcpcib_wdt_stop(struct tcpcib_softc *sc)
{
	/* Disable watchdog, with a reload before for safety */
	tcpcib_wdt_unlock(sc);
	bus_space_write_1(sc->sc_wdt_iot, sc->sc_wdt_ioh, E600_WDT_RR1,
	    E600_WDT_RR1_RELOAD);
	bus_space_write_1(sc->sc_wdt_iot, sc->sc_wdt_ioh, E600_WDT_WDTLR, 0);
}

int
tcpcib_match(struct device *parent, void *match, void *aux)
{
	if (pci_matchbyid((struct pci_attach_args *)aux, tcpcib_devices,
	    sizeof(tcpcib_devices) / sizeof(tcpcib_devices[0])))
		return (2);

	return (0);
}

void
tcpcib_attach(struct device *parent, struct device *self, void *aux)
{
	struct tcpcib_softc *sc = (struct tcpcib_softc *)self;
	struct pci_attach_args *pa = aux;
	struct timecounter *tc = &sc->sc_hpet_timecounter;
	u_int32_t reg, wdtbase;

	sc->sc_active = 0;

	/* High Precision Event Timer */
	sc->sc_hpet_iot = pa->pa_memt;
	if (bus_space_map(sc->sc_hpet_iot, E600_HPET_BASE, E600_HPET_SIZE, 0,
	    &sc->sc_hpet_ioh) == 0) {
		tc->tc_get_timecount = tcpcib_hpet_get_timecount;
		/* XXX 64-bit counter is not supported! */
		tc->tc_counter_mask = 0xffffffff;

		reg = bus_space_read_4(sc->sc_hpet_iot, sc->sc_hpet_ioh,
		    E600_HPET_PERIOD);
		/* femtosecs -> Hz */
		tc->tc_frequency = 1000000000000000ULL / reg;

		tc->tc_name = sc->sc_dev.dv_xname;
		tc->tc_quality = 2000;
		tc->tc_priv = sc;
		tc_init(tc);

		/* Enable counting */
		bus_space_write_4(sc->sc_hpet_iot, sc->sc_hpet_ioh,
		    E600_HPET_GC, E600_HPET_GC_ENABLE);

		sc->sc_active |= E600_HPET_ACTIVE;

		printf(": %llu Hz timer", tc->tc_frequency);
	}

	/* Map Watchdog I/O space */
	reg = pci_conf_read(pa->pa_pc, pa->pa_tag, E600_LPC_WDTBA);
	wdtbase = reg & 0xffff;
	sc->sc_wdt_iot = pa->pa_iot;
	if (reg & (1U << 31) && wdtbase) {
		if (PCI_MAPREG_IO_ADDR(wdtbase) == 0 ||
		    bus_space_map(sc->sc_wdt_iot, PCI_MAPREG_IO_ADDR(wdtbase),
		    E600_WDT_SIZE, 0, &sc->sc_wdt_ioh)) {
			printf("%c can't map watchdog I/O space",
			    sc->sc_active ? ',' : ':');
			goto corepcib;
		}
		printf("%c watchdog", sc->sc_active ? ',' : ':');

		/* Check for reboot on timeout */
		reg = bus_space_read_1(sc->sc_wdt_iot, sc->sc_wdt_ioh,
		    E600_WDT_RR1);
		if (reg & E600_WDT_RR1_TIMEOUT) {
			printf(", reboot on timeout");

			/* Clear timeout bit */
			tcpcib_wdt_unlock(sc);
			bus_space_write_1(sc->sc_wdt_iot, sc->sc_wdt_ioh,
			    E600_WDT_RR1, E600_WDT_RR1_TIMEOUT);
		}

		/* Check it's not locked already */
		reg = bus_space_read_1(sc->sc_wdt_iot, sc->sc_wdt_ioh,
		    E600_WDT_WDTLR);
		if (reg & E600_WDT_WDTLR_LOCK) {
			printf(", locked");
			goto corepcib;
		}

		/* Disable watchdog */
		tcpcib_wdt_stop(sc);
		sc->sc_wdt_period = 0;

		sc->sc_active |= E600_WDT_ACTIVE;

		/* Register new watchdog */
		wdog_register(tcpcib_wdt_cb, sc);
	}

corepcib:
	/* Provide core pcib(4) functionality */
	pcibattach(parent, self, aux);
}

int
tcpcib_activate(struct device *self, int act)
{
	struct tcpcib_softc *sc = (struct tcpcib_softc *)self;
	int rv = 0;
	
	switch (act) {
	case DVACT_SUSPEND:
		rv = config_activate_children(self, act);
		/* Watchdog is running, disable it */
		if (sc->sc_active & E600_WDT_ACTIVE && sc->sc_wdt_period != 0)
			tcpcib_wdt_stop(sc);
		break;
	case DVACT_RESUME:
		if (sc->sc_active & E600_WDT_ACTIVE) {
			/*
			 * Watchdog was running prior to suspend so reenable
			 * it, otherwise make sure it stays disabled
			 */
			if (sc->sc_wdt_period != 0) {
				tcpcib_wdt_init(sc, sc->sc_wdt_period);
				tcpcib_wdt_start(sc);
			} else
				tcpcib_wdt_stop(sc);
		}
		if (sc->sc_active & E600_HPET_ACTIVE)
			bus_space_write_4(sc->sc_hpet_iot, sc->sc_hpet_ioh,
			    E600_HPET_GC, E600_HPET_GC_ENABLE);
		rv = config_activate_children(self, act);
		break;
	case DVACT_POWERDOWN:
		if (sc->sc_active & E600_WDT_ACTIVE)
			wdog_shutdown(self);
		rv = config_activate_children(self, act);
		break;
	default:
		rv = config_activate_children(self, act);
		break;
	}
	return (rv);
}

int
tcpcib_wdt_cb(void *arg, int period)
{
	struct tcpcib_softc *sc = arg;

	if (period == 0) {
		if (sc->sc_wdt_period != 0)
			tcpcib_wdt_stop(sc);
	} else {
		/* 600 seconds is the maximum supported timeout value */
		if (period > 600)
			period = 600;
		if (sc->sc_wdt_period != period)
			tcpcib_wdt_init(sc, period);
		if (sc->sc_wdt_period == 0) {
			tcpcib_wdt_start(sc);
		} else {
			/* Reset timer */
			tcpcib_wdt_unlock(sc);
			bus_space_write_1(sc->sc_wdt_iot, sc->sc_wdt_ioh,
			    E600_WDT_RR1, E600_WDT_RR1_RELOAD);
		}
	}
	sc->sc_wdt_period = period;

	return (period);
}

u_int
tcpcib_hpet_get_timecount(struct timecounter *tc)
{
	struct tcpcib_softc *sc = tc->tc_priv;

	/* XXX 64-bit counter is not supported! */
	return bus_space_read_4(sc->sc_hpet_iot, sc->sc_hpet_ioh,
	    E600_HPET_MCV);
}
