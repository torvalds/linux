/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
 * Copyright (c) 2010-2011 Adrian Chadd, Xenion Pty Ltd
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
 * AHB bus front-end for the Atheros Wireless LAN controller driver.
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

#include <mips/atheros/ar71xxreg.h>
#include <mips/atheros/ar91xxreg.h>
#include <mips/atheros/ar71xx_cpudef.h>

/* For EEPROM firmware */
#ifdef	ATH_EEPROM_FIRMWARE
#include <sys/linker.h>
#include <sys/firmware.h>
#endif	/* ATH_EEPROM_FIRMWARE */

/*
 * bus glue.
 */

/* number of 16 bit words */
#define	ATH_EEPROM_DATA_SIZE	2048

struct ath_ahb_softc {
	struct ath_softc	sc_sc;
	struct resource		*sc_sr;		/* memory resource */
	struct resource		*sc_irq;	/* irq resource */
	void			*sc_ih;		/* interrupt handler */
};

#define	VENDOR_ATHEROS	0x168c
#define	AR9130_DEVID	0x000b

static int
ath_ahb_probe(device_t dev)
{
	int vendor_id, device_id;
	const char* devname;

	/*
	 * Check if a device/vendor ID is provided in hints.
	 */
	if (resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "vendor_id", &vendor_id) != 0) {
		vendor_id = VENDOR_ATHEROS;
	}

	if (resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "device_id", &device_id) != 0) {
		device_id = AR9130_DEVID;
	}

	device_printf(dev, "Vendor=0x%04x, Device=0x%04x\n",
	    vendor_id & 0xffff,
	    device_id & 0xffff);

	/* Attempt to probe */
	devname = ath_hal_probe(vendor_id, device_id);

	if (devname != NULL) {
		device_set_desc(dev, devname);
		return BUS_PROBE_DEFAULT;
	}
	return ENXIO;
}

static void
ath_ahb_intr(void *arg)
{
	/* XXX TODO: check if its ours! */
	ar71xx_device_flush_ddr(AR71XX_CPU_DDR_FLUSH_WMAC);
	ath_intr(arg);
}

static int
ath_ahb_attach(device_t dev)
{
	struct ath_ahb_softc *psc = device_get_softc(dev);
	struct ath_softc *sc = &psc->sc_sc;
	int error = ENXIO;
	int rid;
	int device_id, vendor_id;
#ifdef	ATH_EEPROM_FIRMWARE
	const struct firmware *fw = NULL;
	const char *buf;
#endif

	sc->sc_dev = dev;

	rid = 0;
	psc->sc_sr = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
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

#ifdef	ATH_EEPROM_FIRMWARE
	/*
	 * If there's an EEPROM firmware image, load that in.
	 */
	if (resource_string_value(device_get_name(dev), device_get_unit(dev),
	    "eeprom_firmware", &buf) == 0) {
		device_printf(dev, "%s: looking up firmware @ '%s'\n",
		    __func__, buf);

		fw = firmware_get(buf);
		if (fw == NULL) {
			device_printf(dev, "%s: couldn't find firmware\n",
			    __func__);
			goto bad1;
		}

		device_printf(dev, "%s: EEPROM firmware @ %p\n",
		    __func__, fw->data);
		sc->sc_eepromdata =
		    malloc(fw->datasize, M_TEMP, M_WAITOK | M_ZERO);
		if (! sc->sc_eepromdata) {
			device_printf(dev, "%s: can't malloc eepromdata\n",
			    __func__);
			goto bad1;
		}
		memcpy(sc->sc_eepromdata, fw->data, fw->datasize);
		firmware_put(fw, 0);
	}
#endif	/* ATH_EEPROM_FIRMWARE */

	/*
	 * Arrange interrupt line.
	 */
	rid = 0;
	psc->sc_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_SHAREABLE|RF_ACTIVE);
	if (psc->sc_irq == NULL) {
		device_printf(dev, "could not map interrupt\n");
		goto bad1;
	}
	if (bus_setup_intr(dev, psc->sc_irq,
			   INTR_TYPE_NET | INTR_MPSAFE,
			   NULL, ath_ahb_intr, sc, &psc->sc_ih)) {
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

	/*
	 * Check if a device/vendor ID is provided in hints.
	 */
	if (resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "vendor_id", &vendor_id) != 0) {
		vendor_id = VENDOR_ATHEROS;
	}

	if (resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "device_id", &device_id) != 0) {
		device_id = AR9130_DEVID;
	}

	ATH_LOCK_INIT(sc);
	ATH_PCU_LOCK_INIT(sc);
	ATH_RX_LOCK_INIT(sc);
	ATH_TX_LOCK_INIT(sc);
	ATH_TXSTATUS_LOCK_INIT(sc);

	error = ath_attach(device_id, sc);
	if (error == 0)					/* success */
		return 0;

	ATH_TXSTATUS_LOCK_DESTROY(sc);
	ATH_RX_LOCK_DESTROY(sc);
	ATH_TX_LOCK_DESTROY(sc);
	ATH_PCU_LOCK_DESTROY(sc);
	ATH_LOCK_DESTROY(sc);
	bus_dma_tag_destroy(sc->sc_dmat);
bad3:
	bus_teardown_intr(dev, psc->sc_irq, psc->sc_ih);
bad2:
	bus_release_resource(dev, SYS_RES_IRQ, 0, psc->sc_irq);
bad1:
	bus_release_resource(dev, SYS_RES_MEMORY, 0, psc->sc_sr);
bad:
	/* XXX?! */
	if (sc->sc_eepromdata)
		free(sc->sc_eepromdata, M_TEMP);
	return (error);
}

static int
ath_ahb_detach(device_t dev)
{
	struct ath_ahb_softc *psc = device_get_softc(dev);
	struct ath_softc *sc = &psc->sc_sc;

	/* check if device was removed */
	sc->sc_invalid = !bus_child_present(dev);

	ath_detach(sc);

	bus_generic_detach(dev);
	bus_teardown_intr(dev, psc->sc_irq, psc->sc_ih);
	bus_release_resource(dev, SYS_RES_IRQ, 0, psc->sc_irq);

	bus_dma_tag_destroy(sc->sc_dmat);
	bus_release_resource(dev, SYS_RES_MEMORY, 0, psc->sc_sr);
	/* XXX?! */
	if (sc->sc_eepromdata)
		free(sc->sc_eepromdata, M_TEMP);

	ATH_TXSTATUS_LOCK_DESTROY(sc);
	ATH_RX_LOCK_DESTROY(sc);
	ATH_TX_LOCK_DESTROY(sc);
	ATH_PCU_LOCK_DESTROY(sc);
	ATH_LOCK_DESTROY(sc);

	return (0);
}

static int
ath_ahb_shutdown(device_t dev)
{
	struct ath_ahb_softc *psc = device_get_softc(dev);

	ath_shutdown(&psc->sc_sc);
	return (0);
}

static int
ath_ahb_suspend(device_t dev)
{
	struct ath_ahb_softc *psc = device_get_softc(dev);

	ath_suspend(&psc->sc_sc);

	return (0);
}

static int
ath_ahb_resume(device_t dev)
{
	struct ath_ahb_softc *psc = device_get_softc(dev);

	ath_resume(&psc->sc_sc);

	return (0);
}

static device_method_t ath_ahb_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ath_ahb_probe),
	DEVMETHOD(device_attach,	ath_ahb_attach),
	DEVMETHOD(device_detach,	ath_ahb_detach),
	DEVMETHOD(device_shutdown,	ath_ahb_shutdown),
	DEVMETHOD(device_suspend,	ath_ahb_suspend),
	DEVMETHOD(device_resume,	ath_ahb_resume),

	{ 0,0 }
};
static driver_t ath_ahb_driver = {
	"ath",
	ath_ahb_methods,
	sizeof (struct ath_ahb_softc)
};
static	devclass_t ath_devclass;
DRIVER_MODULE(if_ath_ahb, nexus, ath_ahb_driver, ath_devclass, 0, 0);
DRIVER_MODULE(if_ath_ahb, apb, ath_ahb_driver, ath_devclass, 0, 0);
MODULE_VERSION(if_ath_ahb, 1);
MODULE_DEPEND(if_ath_ahb, wlan, 1, 1, 1);		/* 802.11 media layer */
MODULE_DEPEND(if_ath_ahb, ath_main, 1, 1, 1);		/* if_ath driver */
MODULE_DEPEND(if_ath_ahb, ath_hal, 1, 1, 1);		/* ath HAL */
