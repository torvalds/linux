/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Adaptec Inc.
 * All rights reserved.
 *
 * Written by: David Jeffery
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dev/ips/ipsreg.h>
#include <dev/ips/ips.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

static int ips_pci_free(ips_softc_t *sc);
static void ips_intrhook(void *arg);

static int ips_pci_probe(device_t dev)
{

        if ((pci_get_vendor(dev) == IPS_VENDOR_ID) &&
	    (pci_get_device(dev) == IPS_MORPHEUS_DEVICE_ID)) {
		device_set_desc(dev, "IBM ServeRAID Adapter");
                return (BUS_PROBE_DEFAULT);
        } else if ((pci_get_vendor(dev) == IPS_VENDOR_ID) &&
	    (pci_get_device(dev) == IPS_COPPERHEAD_DEVICE_ID)) {
		device_set_desc(dev, "IBM ServeRAID Adapter");
		return (BUS_PROBE_DEFAULT);
        } else if ((pci_get_vendor(dev) == IPS_VENDOR_ID_ADAPTEC) &&
	    (pci_get_device(dev) == IPS_MARCO_DEVICE_ID)) {
		device_set_desc(dev, "Adaptec ServeRAID Adapter");
		return (BUS_PROBE_DEFAULT);
	}
        return(ENXIO);
}

static int ips_pci_attach(device_t dev)
{
        ips_softc_t *sc;

        DEVICE_PRINTF(1, dev, "in attach.\n");
        sc = (ips_softc_t *)device_get_softc(dev);
        sc->dev = dev;
	mtx_init(&sc->queue_mtx, "IPS bioqueue lock", NULL, MTX_DEF);
	sema_init(&sc->cmd_sema, 0, "IPS Command Semaphore");
	callout_init_mtx(&sc->timer, &sc->queue_mtx, 0);

        if(pci_get_device(dev) == IPS_MORPHEUS_DEVICE_ID){
		sc->ips_adapter_reinit = ips_morpheus_reinit;
                sc->ips_adapter_intr = ips_morpheus_intr;
		sc->ips_issue_cmd    = ips_issue_morpheus_cmd;
		sc->ips_poll_cmd     = ips_morpheus_poll;
        } else if(pci_get_device(dev) == IPS_COPPERHEAD_DEVICE_ID){
		sc->ips_adapter_reinit = ips_copperhead_reinit;
                sc->ips_adapter_intr = ips_copperhead_intr;
		sc->ips_issue_cmd    = ips_issue_copperhead_cmd;
		sc->ips_poll_cmd     = ips_copperhead_poll;
	} else if (pci_get_device(dev) == IPS_MARCO_DEVICE_ID){
		sc->ips_adapter_reinit = ips_morpheus_reinit;
		sc->ips_adapter_intr = ips_morpheus_intr;
		sc->ips_issue_cmd = ips_issue_morpheus_cmd;
		sc->ips_poll_cmd     = ips_morpheus_poll;
	} else
                goto error;
        /* make sure busmastering is on */
	pci_enable_busmaster(dev);
        /* setting up io space */
        sc->iores = NULL;
	PRINTF(10, "trying MEMIO\n");
	if(pci_get_device(dev) == IPS_COPPERHEAD_DEVICE_ID)
		sc->rid = PCIR_BAR(1);
	else
		sc->rid = PCIR_BAR(0);
	sc->iotype = SYS_RES_MEMORY;
	sc->iores = bus_alloc_resource_any(dev, sc->iotype, &sc->rid,
	    RF_ACTIVE);
        if(!sc->iores){
                PRINTF(10, "trying PORTIO\n");
                sc->rid = PCIR_BAR(0);
                sc->iotype = SYS_RES_IOPORT;
                sc->iores = bus_alloc_resource_any(dev, sc->iotype, 
			&sc->rid, RF_ACTIVE);
        }
        if(sc->iores == NULL){
                device_printf(dev, "resource allocation failed\n");
                return (ENXIO);
        }
        /*allocate an interrupt. when does the irq become active? after leaving attach? */
        sc->irqrid = 0;
        if(!(sc->irqres = bus_alloc_resource_any(dev, SYS_RES_IRQ,
		&sc->irqrid, RF_SHAREABLE | RF_ACTIVE))){
                device_printf(dev, "irq allocation failed\n");
                goto error;
        }
	if(bus_setup_intr(dev, sc->irqres, INTR_TYPE_BIO|INTR_MPSAFE, NULL, 
	    sc->ips_adapter_intr, sc, &sc->irqcookie)){
                device_printf(dev, "irq setup failed\n");
                goto error;
        }
	if (bus_dma_tag_create(	/* PCI parent */bus_get_dma_tag(dev),
				/* alignemnt */	1,
				/* boundary  */	0,
				/* lowaddr   */	BUS_SPACE_MAXADDR_32BIT,
				/* highaddr  */	BUS_SPACE_MAXADDR,
				/* filter    */	NULL,
				/* filterarg */	NULL,
				/* maxsize   */	BUS_SPACE_MAXSIZE_32BIT,
				/* numsegs   */	IPS_MAX_SG_ELEMENTS,
				/* maxsegsize*/	BUS_SPACE_MAXSIZE_32BIT,
				/* flags     */	0,
				/* lockfunc  */ NULL,
				/* lockarg   */ NULL,
				&sc->adapter_dmatag) != 0) {
                device_printf(dev, "can't alloc dma tag\n");
                goto error;
        }
	sc->ips_ich.ich_func = ips_intrhook;
	sc->ips_ich.ich_arg = sc;
	bioq_init(&sc->queue);
	if (config_intrhook_establish(&sc->ips_ich) != 0) {
		printf("IPS can't establish configuration hook\n");
		goto error;
	}
        return 0;
error:
	ips_pci_free(sc);
        return (ENXIO);
}

static void
ips_intrhook(void *arg)
{
	struct ips_softc *sc = (struct ips_softc *)arg;

	config_intrhook_disestablish(&sc->ips_ich);
	if (ips_adapter_init(sc))
		ips_pci_free(sc);
	else
		sc->configured = 1;
}

static int ips_pci_free(ips_softc_t *sc)
{
	if(sc->adapter_dmatag)
		bus_dma_tag_destroy(sc->adapter_dmatag);
	if(sc->irqcookie)
                bus_teardown_intr(sc->dev, sc->irqres, sc->irqcookie);
        if(sc->irqres)
               bus_release_resource(sc->dev, SYS_RES_IRQ, sc->irqrid, sc->irqres);
        if(sc->iores)
                bus_release_resource(sc->dev, sc->iotype, sc->rid, sc->iores);
	sc->configured = 0;
	mtx_destroy(&sc->queue_mtx);
	sema_destroy(&sc->cmd_sema);
	return 0;
}

static int ips_pci_detach(device_t dev)
{
        ips_softc_t *sc;
        DEVICE_PRINTF(1, dev, "detaching ServeRaid\n");
        sc = (ips_softc_t *) device_get_softc(dev);
	if (sc->configured) {
		sc->configured = 0;
		ips_flush_cache(sc);
		if(ips_adapter_free(sc))
			return EBUSY;
		ips_pci_free(sc);
		bioq_flush(&sc->queue, NULL, ENXIO);
	}
	return 0;
}

static int ips_pci_shutdown(device_t dev)
{
	ips_softc_t *sc = (ips_softc_t *) device_get_softc(dev);
	if (sc->configured) {
		ips_flush_cache(sc);
	}
	return 0;
}

static device_method_t ips_driver_methods[] = {
        DEVMETHOD(device_probe, ips_pci_probe),
        DEVMETHOD(device_attach, ips_pci_attach),
        DEVMETHOD(device_detach, ips_pci_detach),
	DEVMETHOD(device_shutdown, ips_pci_shutdown),
        {0,0}
};

static driver_t ips_pci_driver = {
        "ips",
        ips_driver_methods,
        sizeof(ips_softc_t),
};

static devclass_t ips_devclass;
DRIVER_MODULE(ips, pci, ips_pci_driver, ips_devclass, 0, 0);
