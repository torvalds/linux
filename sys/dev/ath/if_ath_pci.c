/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * PCI/Cardbus front-end for the Atheros Wireless LAN controller driver.
 */
#include "opt_ath.h"

#include <sys/param.h>
#include <sys/systm.h> 
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/errno.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <sys/socket.h>
 
#include <net/if.h>
#include <net/if_media.h>
#include <net/if_arp.h>
#include <net/ethernet.h>

#include <net80211/ieee80211_var.h>

#include <dev/ath/if_athvar.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

/* For EEPROM firmware */
#ifdef	ATH_EEPROM_FIRMWARE
#include <sys/linker.h>
#include <sys/firmware.h>
#endif	/* ATH_EEPROM_FIRMWARE */

/*
 * PCI glue.
 */

struct ath_pci_softc {
	struct ath_softc	sc_sc;
	struct resource		*sc_sr;		/* memory resource */
	struct resource		*sc_irq;	/* irq resource */
	void			*sc_ih;		/* interrupt handler */
};

#define	PCI_VDEVICE(v, d)			\
	PCI_DEV(v,d)

#define	PCI_DEVICE_SUB(v, d, sv, sd)		\
	PCI_DEV(v, d), PCI_SUBDEV(sv, sd)

#define	PCI_VENDOR_ID_ATHEROS		0x168c
#define	PCI_VENDOR_ID_SAMSUNG		0x144d
#define	PCI_VENDOR_ID_AZWAVE		0x1a3b
#define	PCI_VENDOR_ID_FOXCONN		0x105b
#define	PCI_VENDOR_ID_ATTANSIC		0x1969
#define	PCI_VENDOR_ID_ASUSTEK		0x1043
#define	PCI_VENDOR_ID_DELL		0x1028
#define	PCI_VENDOR_ID_QMI		0x1a32
#define	PCI_VENDOR_ID_LENOVO		0x17aa
#define	PCI_VENDOR_ID_HP		0x103c

#include "if_ath_pci_devlist.h"

#define	BS_BAR	0x10
#define	PCIR_RETRY_TIMEOUT	0x41
#define	PCIR_CFG_PMCSR		0x48

#define	DEFAULT_CACHESIZE	32

static void
ath_pci_setup(device_t dev)
{
	uint8_t cz;

	/* XXX TODO: need to override the _system_ saved copies of this */

	/*
	 * If the cache line size is 0, force it to a reasonable
	 * value.
	 */
	cz = pci_read_config(dev, PCIR_CACHELNSZ, 1);
	if (cz == 0) {
		pci_write_config(dev, PCIR_CACHELNSZ,
		    DEFAULT_CACHESIZE / 4, 1);
	}

	/* Override the system latency timer */
	pci_write_config(dev, PCIR_LATTIMER, 0xa8, 1);

	/* If a PCI NIC, force wakeup */
#ifdef	ATH_PCI_WAKEUP_WAR
	/* XXX TODO: don't do this for non-PCI (ie, PCIe, Cardbus!) */
	if (1) {
		uint16_t pmcsr;
		pmcsr = pci_read_config(dev, PCIR_CFG_PMCSR, 2);
		pmcsr |= 3;
		pci_write_config(dev, PCIR_CFG_PMCSR, pmcsr, 2);
		pmcsr &= ~3;
		pci_write_config(dev, PCIR_CFG_PMCSR, pmcsr, 2);
	}
#endif

	/*
	 * Disable retry timeout to keep PCI Tx retries from
	 * interfering with C3 CPU state.
	 */
	pci_write_config(dev, PCIR_RETRY_TIMEOUT, 0, 1);
}

static int
ath_pci_probe(device_t dev)
{
	const char* devname;

	devname = ath_hal_probe(pci_get_vendor(dev), pci_get_device(dev));
	if (devname != NULL) {
		device_set_desc(dev, devname);
		return BUS_PROBE_DEFAULT;
	}
	return ENXIO;
}

static int
ath_pci_attach(device_t dev)
{
	struct ath_pci_softc *psc = device_get_softc(dev);
	struct ath_softc *sc = &psc->sc_sc;
	int error = ENXIO;
	int rid;
#ifdef	ATH_EEPROM_FIRMWARE
	const struct firmware *fw = NULL;
	const char *buf;
#endif
	const struct pci_device_table *pd;

	sc->sc_dev = dev;

	/* Do this lookup anyway; figure out what to do with it later */
	pd = PCI_MATCH(dev, ath_pci_id_table);
	if (pd)
		sc->sc_pci_devinfo = pd->driver_data;

	/*
	 * Enable bus mastering.
	 */
	pci_enable_busmaster(dev);

	/*
	 * Setup other PCI bus configuration parameters.
	 */
	ath_pci_setup(dev);

	/* 
	 * Setup memory-mapping of PCI registers.
	 */
	rid = BS_BAR;
	psc->sc_sr = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
					    RF_ACTIVE);
	if (psc->sc_sr == NULL) {
		device_printf(dev, "cannot map register space\n");
		goto bad;
	}
	sc->sc_st = (HAL_BUS_TAG) rman_get_bustag(psc->sc_sr);
	sc->sc_sh = (HAL_BUS_HANDLE) rman_get_bushandle(psc->sc_sr);
	/*
	 * Mark device invalid so any interrupts (shared or otherwise)
	 * that arrive before the HAL is setup are discarded.
	 */
	sc->sc_invalid = 1;

	ATH_LOCK_INIT(sc);
	ATH_PCU_LOCK_INIT(sc);
	ATH_RX_LOCK_INIT(sc);
	ATH_TX_LOCK_INIT(sc);
	ATH_TXSTATUS_LOCK_INIT(sc);

	/*
	 * Arrange interrupt line.
	 */
	rid = 0;
	psc->sc_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
					     RF_SHAREABLE|RF_ACTIVE);
	if (psc->sc_irq == NULL) {
		device_printf(dev, "could not map interrupt\n");
		goto bad1;
	}
	if (bus_setup_intr(dev, psc->sc_irq,
			   INTR_TYPE_NET | INTR_MPSAFE,
			   NULL, ath_intr, sc, &psc->sc_ih)) {
		device_printf(dev, "could not establish interrupt\n");
		goto bad2;
	}

	/*
	 * Setup DMA descriptor area.
	 */
	if (bus_dma_tag_create(bus_get_dma_tag(dev),	/* parent */
			       1, 0,			/* alignment, bounds */
			       BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
			       BUS_SPACE_MAXADDR,	/* highaddr */
			       NULL, NULL,		/* filter, filterarg */
			       0x3ffff,			/* maxsize XXX */
			       ATH_MAX_SCATTER,		/* nsegments */
			       0x3ffff,			/* maxsegsize XXX */
			       BUS_DMA_ALLOCNOW,	/* flags */
			       NULL,			/* lockfunc */
			       NULL,			/* lockarg */
			       &sc->sc_dmat)) {
		device_printf(dev, "cannot allocate DMA tag\n");
		goto bad3;
	}

#ifdef	ATH_EEPROM_FIRMWARE
	/*
	 * If there's an EEPROM firmware image, load that in.
	 */
	if (resource_string_value(device_get_name(dev), device_get_unit(dev),
	    "eeprom_firmware", &buf) == 0) {
		if (bootverbose)
			device_printf(dev, "%s: looking up firmware @ '%s'\n",
			    __func__, buf);

		fw = firmware_get(buf);
		if (fw == NULL) {
			device_printf(dev, "%s: couldn't find firmware\n",
			    __func__);
			goto bad4;
		}

		device_printf(dev, "%s: EEPROM firmware @ %p\n",
		    __func__, fw->data);
		sc->sc_eepromdata =
		    malloc(fw->datasize, M_TEMP, M_WAITOK | M_ZERO);
		if (! sc->sc_eepromdata) {
			device_printf(dev, "%s: can't malloc eepromdata\n",
			    __func__);
			goto bad4;
		}
		memcpy(sc->sc_eepromdata, fw->data, fw->datasize);
		firmware_put(fw, 0);
	}
#endif /* ATH_EEPROM_FIRMWARE */

	error = ath_attach(pci_get_device(dev), sc);
	if (error == 0)					/* success */
		return 0;

#ifdef	ATH_EEPROM_FIRMWARE
bad4:
#endif
	bus_dma_tag_destroy(sc->sc_dmat);
bad3:
	bus_teardown_intr(dev, psc->sc_irq, psc->sc_ih);
bad2:
	bus_release_resource(dev, SYS_RES_IRQ, 0, psc->sc_irq);
bad1:
	bus_release_resource(dev, SYS_RES_MEMORY, BS_BAR, psc->sc_sr);

	ATH_TXSTATUS_LOCK_DESTROY(sc);
	ATH_PCU_LOCK_DESTROY(sc);
	ATH_RX_LOCK_DESTROY(sc);
	ATH_TX_LOCK_DESTROY(sc);
	ATH_LOCK_DESTROY(sc);

bad:
	return (error);
}

static int
ath_pci_detach(device_t dev)
{
	struct ath_pci_softc *psc = device_get_softc(dev);
	struct ath_softc *sc = &psc->sc_sc;

	/* check if device was removed */
	sc->sc_invalid = !bus_child_present(dev);

	/*
	 * Do a config read to clear pre-existing pci error status.
	 */
	(void) pci_read_config(dev, PCIR_COMMAND, 4);

	ath_detach(sc);

	bus_generic_detach(dev);
	bus_teardown_intr(dev, psc->sc_irq, psc->sc_ih);
	bus_release_resource(dev, SYS_RES_IRQ, 0, psc->sc_irq);

	bus_dma_tag_destroy(sc->sc_dmat);
	bus_release_resource(dev, SYS_RES_MEMORY, BS_BAR, psc->sc_sr);

	if (sc->sc_eepromdata)
		free(sc->sc_eepromdata, M_TEMP);

	ATH_TXSTATUS_LOCK_DESTROY(sc);
	ATH_PCU_LOCK_DESTROY(sc);
	ATH_RX_LOCK_DESTROY(sc);
	ATH_TX_LOCK_DESTROY(sc);
	ATH_LOCK_DESTROY(sc);

	return (0);
}

static int
ath_pci_shutdown(device_t dev)
{
	struct ath_pci_softc *psc = device_get_softc(dev);

	ath_shutdown(&psc->sc_sc);
	return (0);
}

static int
ath_pci_suspend(device_t dev)
{
	struct ath_pci_softc *psc = device_get_softc(dev);

	ath_suspend(&psc->sc_sc);

	return (0);
}

static int
ath_pci_resume(device_t dev)
{
	struct ath_pci_softc *psc = device_get_softc(dev);

	/*
	 * Suspend/resume resets the PCI configuration space.
	 */
	ath_pci_setup(dev);

	ath_resume(&psc->sc_sc);

	return (0);
}

static device_method_t ath_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ath_pci_probe),
	DEVMETHOD(device_attach,	ath_pci_attach),
	DEVMETHOD(device_detach,	ath_pci_detach),
	DEVMETHOD(device_shutdown,	ath_pci_shutdown),
	DEVMETHOD(device_suspend,	ath_pci_suspend),
	DEVMETHOD(device_resume,	ath_pci_resume),

	{ 0,0 }
};
static driver_t ath_pci_driver = {
	"ath",
	ath_pci_methods,
	sizeof (struct ath_pci_softc)
};
static	devclass_t ath_devclass;
DRIVER_MODULE(if_ath_pci, pci, ath_pci_driver, ath_devclass, 0, 0);
MODULE_VERSION(if_ath_pci, 1);
MODULE_DEPEND(if_ath_pci, wlan, 1, 1, 1);		/* 802.11 media layer */
MODULE_DEPEND(if_ath_pci, ath_main, 1, 1, 1);	/* if_ath driver */
MODULE_DEPEND(if_ath_pci, ath_hal, 1, 1, 1);	/* ath HAL */
