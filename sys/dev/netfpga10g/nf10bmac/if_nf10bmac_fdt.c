/*-
 * Copyright (c) 2013-2014 Bjoern A. Zeeb
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-11-C-0249
 * ("MRC2"), as part of the DARPA MRC research programme.
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
 *
 * This driver is modelled after atse(4).  We need to seriously reduce the
 * per-driver code we have to write^wcopy & paste.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_media.h>
#include <net/if_var.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "if_nf10bmacreg.h"

static int
nf10bmac_probe_fdt(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_is_compatible(dev, "netfpag10g,nf10bmac")) {
		device_set_desc(dev, "NetFPGA-10G Embedded CPU Ethernet Core"); 
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int
nf10bmac_attach_fdt(device_t dev)
{
	struct nf10bmac_softc *sc;
	int error;

	sc = device_get_softc(dev);
	sc->nf10bmac_dev = dev;
	sc->nf10bmac_unit = device_get_unit(dev);

	/*
	 * FDT lists our resources.  For convenience we use three different
	 * mappings.  We need to attach them in the oder specified in .dts:
	 * LOOP (size 0x1f), TX (0x2f), RX (0x2f), INTR (0xf).
	 */

	/*
	 * LOOP memory region (this could be a general control region).
	 * 0x00: 32/64bit register to enable a Y-"lopback".
	 */
	sc->nf10bmac_ctrl_rid = 0;
	sc->nf10bmac_ctrl_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->nf10bmac_ctrl_rid, RF_ACTIVE);
	if (sc->nf10bmac_ctrl_res == NULL) {
		device_printf(dev, "failed to map memory for CTRL region\n");
		error = ENXIO;
		goto err;
	} 
	if (bootverbose)
		device_printf(sc->nf10bmac_dev, "CTRL region at mem %p-%p\n",
		    (void *)rman_get_start(sc->nf10bmac_ctrl_res),
		    (void *)(rman_get_start(sc->nf10bmac_ctrl_res) + 
		    rman_get_size(sc->nf10bmac_ctrl_res)));

	/*
	 * TX and TX metadata FIFO memory region.
	 * 0x00: 32/64bit FIFO data,
	 * 0x08: 32/64bit FIFO metadata,
	 * 0x10: 32/64bit packet length.
	 */
	sc->nf10bmac_tx_mem_rid = 1;
	sc->nf10bmac_tx_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->nf10bmac_tx_mem_rid, RF_ACTIVE);
	if (sc->nf10bmac_tx_mem_res == NULL) {
		device_printf(dev, "failed to map memory for TX FIFO\n");
		error = ENXIO;
		goto err;
	}
	if (bootverbose)
		device_printf(sc->nf10bmac_dev, "TX FIFO at mem %p-%p\n",
		    (void *)rman_get_start(sc->nf10bmac_tx_mem_res),
		    (void *)(rman_get_start(sc->nf10bmac_tx_mem_res) +
		    rman_get_size(sc->nf10bmac_tx_mem_res)));

	/*
	 * RX and RXC metadata FIFO memory region.
	 * 0x00: 32/64bit FIFO data,
	 * 0x08: 32/64bit FIFO metadata,
	 * 0x10: 32/64bit packet length.
	 */
	sc->nf10bmac_rx_mem_rid = 2;
	sc->nf10bmac_rx_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->nf10bmac_rx_mem_rid, RF_ACTIVE);
	if (sc->nf10bmac_rx_mem_res == NULL) {
		device_printf(dev, "failed to map memory for RX FIFO\n");
		error = ENXIO;
		goto err;
	} 
	if (bootverbose)
		device_printf(sc->nf10bmac_dev, "RX FIFO at mem %p-%p\n",
		    (void *)rman_get_start(sc->nf10bmac_rx_mem_res),
		    (void *)(rman_get_start(sc->nf10bmac_rx_mem_res) + 
		    rman_get_size(sc->nf10bmac_rx_mem_res)));

	/*
	 * Interrupt handling registers.
	 * 0x00: 32/64bit register to clear (and disable) the RX interrupt.
	 * 0x08: 32/64bit register to enable or disable the RX interrupt.
	 */
	sc->nf10bmac_intr_rid = 3;
	sc->nf10bmac_intr_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->nf10bmac_intr_rid, RF_ACTIVE);
	if (sc->nf10bmac_intr_res == NULL) {
		device_printf(dev, "failed to map memory for INTR region\n");
		error = ENXIO;
		goto err;
	} 
	if (bootverbose)
		device_printf(sc->nf10bmac_dev, "INTR region at mem %p-%p\n",
		    (void *)rman_get_start(sc->nf10bmac_intr_res),
		    (void *)(rman_get_start(sc->nf10bmac_intr_res) + 
		    rman_get_size(sc->nf10bmac_intr_res)));

	/* (Optional) RX and TX IRQ. */
	sc->nf10bmac_rx_irq_rid = 0;
	sc->nf10bmac_rx_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &sc->nf10bmac_rx_irq_rid, RF_ACTIVE | RF_SHAREABLE);

	error = nf10bmac_attach(dev);
	if (error)
		goto err;

	return (0);

err:
	/* Cleanup. */
	nf10bmac_detach_resources(dev);

	return (error);
}

static device_method_t nf10bmac_methods_fdt[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		nf10bmac_probe_fdt),
	DEVMETHOD(device_attach,	nf10bmac_attach_fdt),
	DEVMETHOD(device_detach,	nf10bmac_detach_dev),

	DEVMETHOD_END   
};

static driver_t nf10bmac_driver_fdt = {
	"nf10bmac",
	nf10bmac_methods_fdt,
	sizeof(struct nf10bmac_softc)
};

DRIVER_MODULE(nf10bmac, simplebus, nf10bmac_driver_fdt, nf10bmac_devclass, 0,0);

/* end */
