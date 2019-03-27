/*-
 * Copyright (c) 2005, 2006
 *	Damien Bergamini <damien.bergamini@free.fr>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * PCI/Cardbus front-end for the Ralink RT2560/RT2561/RT2561S/RT2661 driver.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/socket.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_media.h>
#include <net/route.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>
#include <net80211/ieee80211_ratectl.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/ral/rt2560var.h>
#include <dev/ral/rt2661var.h>
#include <dev/ral/rt2860var.h>

MODULE_DEPEND(ral, pci, 1, 1, 1);
MODULE_DEPEND(ral, firmware, 1, 1, 1);
MODULE_DEPEND(ral, wlan, 1, 1, 1);
MODULE_DEPEND(ral, wlan_amrr, 1, 1, 1);

static int ral_msi_disable;
TUNABLE_INT("hw.ral.msi_disable", &ral_msi_disable);

struct ral_pci_ident {
	uint16_t	vendor;
	uint16_t	device;
	const char	*name;
};

static const struct ral_pci_ident ral_pci_ids[] = {
	{ 0x1432, 0x7708, "Edimax RT2860" },
	{ 0x1432, 0x7711, "Edimax RT3591" },
	{ 0x1432, 0x7722, "Edimax RT3591" },
	{ 0x1432, 0x7727, "Edimax RT2860" },
	{ 0x1432, 0x7728, "Edimax RT2860" },
	{ 0x1432, 0x7738, "Edimax RT2860" },
	{ 0x1432, 0x7748, "Edimax RT2860" },
	{ 0x1432, 0x7758, "Edimax RT2860" },
	{ 0x1432, 0x7768, "Edimax RT2860" },
	{ 0x1462, 0x891a, "MSI RT3090" },
	{ 0x1814, 0x0201, "Ralink Technology RT2560" },
	{ 0x1814, 0x0301, "Ralink Technology RT2561S" },
	{ 0x1814, 0x0302, "Ralink Technology RT2561" },
	{ 0x1814, 0x0401, "Ralink Technology RT2661" },
	{ 0x1814, 0x0601, "Ralink Technology RT2860" },
	{ 0x1814, 0x0681, "Ralink Technology RT2890" },
	{ 0x1814, 0x0701, "Ralink Technology RT2760" },
	{ 0x1814, 0x0781, "Ralink Technology RT2790" },
	{ 0x1814, 0x3060, "Ralink Technology RT3060" },
	{ 0x1814, 0x3062, "Ralink Technology RT3062" },
	{ 0x1814, 0x3090, "Ralink Technology RT3090" },
	{ 0x1814, 0x3091, "Ralink Technology RT3091" },
	{ 0x1814, 0x3092, "Ralink Technology RT3092" },
	{ 0x1814, 0x3390, "Ralink Technology RT3390" },
	{ 0x1814, 0x3562, "Ralink Technology RT3562" },
	{ 0x1814, 0x3592, "Ralink Technology RT3592" },
	{ 0x1814, 0x3593, "Ralink Technology RT3593" },
	{ 0x1814, 0x5360, "Ralink Technology RT5390" },
	{ 0x1814, 0x5362, "Ralink Technology RT5392" },
	{ 0x1814, 0x5390, "Ralink Technology RT5390" },
	{ 0x1814, 0x5392, "Ralink Technology RT5392" },
	{ 0x1814, 0x539a, "Ralink Technology RT5390" },
	{ 0x1814, 0x539f, "Ralink Technology RT5390" },
	{ 0x1a3b, 0x1059, "AWT RT2890" },
	{ 0, 0, NULL }
};

static const struct ral_opns {
	int	(*attach)(device_t, int);
	int	(*detach)(void *);
	void	(*shutdown)(void *);
	void	(*suspend)(void *);
	void	(*resume)(void *);
	void	(*intr)(void *);

}  ral_rt2560_opns = {
	rt2560_attach,
	rt2560_detach,
	rt2560_stop,
	rt2560_stop,
	rt2560_resume,
	rt2560_intr

}, ral_rt2661_opns = {
	rt2661_attach,
	rt2661_detach,
	rt2661_shutdown,
	rt2661_suspend,
	rt2661_resume,
	rt2661_intr
}, ral_rt2860_opns = {
	rt2860_attach,
	rt2860_detach,
	rt2860_shutdown,
	rt2860_suspend,
	rt2860_resume,
	rt2860_intr
};

struct ral_pci_softc {
	union {
		struct rt2560_softc sc_rt2560;
		struct rt2661_softc sc_rt2661;
		struct rt2860_softc sc_rt2860;
	} u;

	const struct ral_opns	*sc_opns;
	struct resource		*irq;
	struct resource		*mem;
	void			*sc_ih;
};

static int ral_pci_probe(device_t);
static int ral_pci_attach(device_t);
static int ral_pci_detach(device_t);
static int ral_pci_shutdown(device_t);
static int ral_pci_suspend(device_t);
static int ral_pci_resume(device_t);

static device_method_t ral_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ral_pci_probe),
	DEVMETHOD(device_attach,	ral_pci_attach),
	DEVMETHOD(device_detach,	ral_pci_detach),
	DEVMETHOD(device_shutdown,	ral_pci_shutdown),
	DEVMETHOD(device_suspend,	ral_pci_suspend),
	DEVMETHOD(device_resume,	ral_pci_resume),

	DEVMETHOD_END
};

static driver_t ral_pci_driver = {
	"ral",
	ral_pci_methods,
	sizeof (struct ral_pci_softc)
};

static devclass_t ral_devclass;

DRIVER_MODULE(ral, pci, ral_pci_driver, ral_devclass, NULL, NULL);
MODULE_PNP_INFO("U16:vendor;U16:device;D:#", pci, ral, ral_pci_ids,
    nitems(ral_pci_ids) - 1);

static int
ral_pci_probe(device_t dev)
{
	const struct ral_pci_ident *ident;

	for (ident = ral_pci_ids; ident->name != NULL; ident++) {
		if (pci_get_vendor(dev) == ident->vendor &&
		    pci_get_device(dev) == ident->device) {
			device_set_desc(dev, ident->name);
			return (BUS_PROBE_DEFAULT);
		}
	}
	return ENXIO;
}

static int
ral_pci_attach(device_t dev)
{
	struct ral_pci_softc *psc = device_get_softc(dev);
	struct rt2560_softc *sc = &psc->u.sc_rt2560;
	int count, error, rid;

	pci_enable_busmaster(dev);

	switch (pci_get_device(dev)) {
	case 0x0201:
		psc->sc_opns = &ral_rt2560_opns;
		break;
	case 0x0301:
	case 0x0302:
	case 0x0401:
		psc->sc_opns = &ral_rt2661_opns;
		break;
	default:
		psc->sc_opns = &ral_rt2860_opns;
		break;
	}

	rid = PCIR_BAR(0);
	psc->mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (psc->mem == NULL) {
		device_printf(dev, "could not allocate memory resource\n");
		return ENXIO;
	}

	sc->sc_st = rman_get_bustag(psc->mem);
	sc->sc_sh = rman_get_bushandle(psc->mem);
	sc->sc_invalid = 1;
	
	rid = 0;
	if (ral_msi_disable == 0) {
		count = 1;
		if (pci_alloc_msi(dev, &count) == 0)
			rid = 1;
	}
	psc->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_ACTIVE |
	    (rid != 0 ? 0 : RF_SHAREABLE));
	if (psc->irq == NULL) {
		device_printf(dev, "could not allocate interrupt resource\n");
		pci_release_msi(dev);
		bus_release_resource(dev, SYS_RES_MEMORY,
		    rman_get_rid(psc->mem), psc->mem);
		return ENXIO;
	}

	error = (*psc->sc_opns->attach)(dev, pci_get_device(dev));
	if (error != 0) {
		(void)ral_pci_detach(dev);
		return error;
	}

	/*
	 * Hook our interrupt after all initialization is complete.
	 */
	error = bus_setup_intr(dev, psc->irq, INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, psc->sc_opns->intr, psc, &psc->sc_ih);
	if (error != 0) {
		device_printf(dev, "could not set up interrupt\n");
		(void)ral_pci_detach(dev);
		return error;
	}
	sc->sc_invalid = 0;
	
	return 0;
}

static int
ral_pci_detach(device_t dev)
{
	struct ral_pci_softc *psc = device_get_softc(dev);
	struct rt2560_softc *sc = &psc->u.sc_rt2560;
	
	/* check if device was removed */
	sc->sc_invalid = !bus_child_present(dev);

	if (psc->sc_ih != NULL)
		bus_teardown_intr(dev, psc->irq, psc->sc_ih);
	(*psc->sc_opns->detach)(psc);

	bus_generic_detach(dev);
	bus_release_resource(dev, SYS_RES_IRQ, rman_get_rid(psc->irq),
	    psc->irq);
	pci_release_msi(dev);

	bus_release_resource(dev, SYS_RES_MEMORY, rman_get_rid(psc->mem),
	    psc->mem);

	return 0;
}

static int
ral_pci_shutdown(device_t dev)
{
	struct ral_pci_softc *psc = device_get_softc(dev);

	(*psc->sc_opns->shutdown)(psc);

	return 0;
}

static int
ral_pci_suspend(device_t dev)
{
	struct ral_pci_softc *psc = device_get_softc(dev);

	(*psc->sc_opns->suspend)(psc);

	return 0;
}

static int
ral_pci_resume(device_t dev)
{
	struct ral_pci_softc *psc = device_get_softc(dev);

	(*psc->sc_opns->resume)(psc);

	return 0;
}
