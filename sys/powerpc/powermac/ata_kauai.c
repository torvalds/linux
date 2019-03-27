/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2004 by Peter Grehan. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Mac 'Kauai' PCI ATA controller
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/sema.h>
#include <sys/taskqueue.h>
#include <vm/uma.h>
#include <machine/stdarg.h>
#include <machine/resource.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <sys/ata.h>
#include <dev/ata/ata-all.h>
#include <ata_if.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <machine/intr_machdep.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include "ata_dbdma.h"

#define  ATA_KAUAI_REGOFFSET	0x2000
#define  ATA_KAUAI_DBDMAOFFSET	0x1000

/*
 * Offset to alt-control register from base
 */
#define  ATA_KAUAI_ALTOFFSET    (ATA_KAUAI_REGOFFSET + 0x160)

/*
 * Define the gap between registers
 */
#define ATA_KAUAI_REGGAP        16

/*
 * PIO and DMA access registers
 */
#define PIO_CONFIG_REG	(ATA_KAUAI_REGOFFSET + 0x200)
#define UDMA_CONFIG_REG	(ATA_KAUAI_REGOFFSET + 0x210)
#define DMA_IRQ_REG	(ATA_KAUAI_REGOFFSET + 0x300)

#define USE_DBDMA_IRQ	0

/*
 * Define the kauai pci bus attachment.
 */
static  int  ata_kauai_probe(device_t dev);
static  int  ata_kauai_attach(device_t dev);
static  int  ata_kauai_setmode(device_t dev, int target, int mode);
static  int  ata_kauai_begin_transaction(struct ata_request *request);

static device_method_t ata_kauai_methods[] = {
        /* Device interface */
	DEVMETHOD(device_probe,		ata_kauai_probe),
	DEVMETHOD(device_attach,	ata_kauai_attach),
	DEVMETHOD(device_detach,	bus_generic_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* ATA interface */
	DEVMETHOD(ata_setmode,		ata_kauai_setmode),
	DEVMETHOD_END
};

struct ata_kauai_softc {
	struct ata_dbdma_channel sc_ch;

	struct resource *sc_memr;

	int shasta;

	uint32_t udmaconf[2];
	uint32_t wdmaconf[2];
	uint32_t pioconf[2];
};

static driver_t ata_kauai_driver = {
	"ata",
	ata_kauai_methods,
	sizeof(struct ata_kauai_softc),
};

DRIVER_MODULE(ata, pci, ata_kauai_driver, ata_devclass, NULL, NULL);
MODULE_DEPEND(ata, ata, 1, 1, 1);

/*
 * PCI ID search table
 */
static const struct kauai_pci_dev {
        u_int32_t	kpd_devid;
        const char	*kpd_desc;
} kauai_pci_devlist[] = {
        { 0x0033106b, "Uninorth2 Kauai ATA Controller" },
        { 0x003b106b, "Intrepid Kauai ATA Controller" },
        { 0x0043106b, "K2 Kauai ATA Controller" },
        { 0x0050106b, "Shasta Kauai ATA Controller" },
        { 0x0069106b, "Intrepid-2 Kauai ATA Controller" },
        { 0, NULL }
};

/*
 * IDE transfer timings
 */
#define KAUAI_PIO_MASK	0xff000fff
#define KAUAI_DMA_MASK	0x00fff000
#define KAUAI_UDMA_MASK	0x0000ffff

static const u_int pio_timing_kauai[] = {
	0x08000a92,	/* PIO0 */
	0x0800060f,	/* PIO1 */
	0x0800038b,	/* PIO2 */
	0x05000249,	/* PIO3 */
	0x04000148	/* PIO4 */
};

static const u_int pio_timing_shasta[] = {
	0x0a000c97,	/* PIO0 */
	0x07000712,	/* PIO1 */
	0x040003cd,	/* PIO2 */
	0x0400028b,	/* PIO3 */
	0x0400010a	/* PIO4 */
};

static const u_int dma_timing_kauai[] = {
        0x00618000,	/* WDMA0 */
        0x00209000,	/* WDMA1 */
        0x00148000	/* WDMA2 */
};

static const u_int dma_timing_shasta[] = {
        0x00820800,	/* WDMA0 */
        0x0028b000,	/* WDMA1 */
        0x001ca000	/* WDMA2 */
};

static const u_int udma_timing_kauai[] = {
        0x000070c1,	/* UDMA0 */
        0x00005d81,	/* UDMA1 */
        0x00004a61,	/* UDMA2 */
        0x00003a51,	/* UDMA3 */
        0x00002a31,	/* UDMA4 */
        0x00002921	/* UDMA5 */
};

static const u_int udma_timing_shasta[] = {
        0x00035901,	/* UDMA0 */
        0x000348b1,	/* UDMA1 */
        0x00033881,	/* UDMA2 */
        0x00033861,	/* UDMA3 */
        0x00033841,	/* UDMA4 */
        0x00033031,	/* UDMA5 */
        0x00033021	/* UDMA6 */
};

static int
ata_kauai_probe(device_t dev)
{
	struct ata_kauai_softc *sc;
	u_int32_t devid;
	phandle_t node;
	const char *compatstring = NULL;
	int i, found;

	found = 0;
	devid = pci_get_devid(dev);
        for (i = 0; kauai_pci_devlist[i].kpd_desc != NULL; i++) {
                if (devid == kauai_pci_devlist[i].kpd_devid) {
			found = 1;
                        device_set_desc(dev, kauai_pci_devlist[i].kpd_desc);
		}
	}

	if (!found)
		return (ENXIO);

	node = ofw_bus_get_node(dev);
	sc = device_get_softc(dev);
	bzero(sc, sizeof(struct ata_kauai_softc));

	compatstring = ofw_bus_get_compat(dev);
	if (compatstring != NULL && strcmp(compatstring,"shasta-ata") == 0)
		sc->shasta = 1;

	/* Pre-K2 controllers apparently need this hack */
	if (!sc->shasta &&
	    (compatstring == NULL || strcmp(compatstring, "K2-UATA") != 0))
		bus_set_resource(dev, SYS_RES_IRQ, 0, 39, 1);

        return (ata_probe(dev));
}

#if USE_DBDMA_IRQ
static int
ata_kauai_dma_interrupt(struct ata_kauai_softc *sc)
{
	/* Clear the DMA interrupt bits */

	bus_write_4(sc->sc_memr, DMA_IRQ_REG, 0x80000000);

	return ata_interrupt(sc);
}
#endif

static int
ata_kauai_attach(device_t dev)
{
	struct ata_kauai_softc *sc = device_get_softc(dev);
	struct ata_channel *ch;
	int i, rid;
#if USE_DBDMA_IRQ
	int dbdma_irq_rid = 1;
	struct resource *dbdma_irq;
	void *cookie;
#endif

	ch = &sc->sc_ch.sc_ch;

        rid = PCIR_BARS;
	sc->sc_memr = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, 
	    RF_ACTIVE);
        if (sc->sc_memr == NULL) {
                device_printf(dev, "could not allocate memory\n");
                return (ENXIO);
        }

	/*
	 * Set up the resource vectors
	 */
        for (i = ATA_DATA; i <= ATA_COMMAND; i++) {
                ch->r_io[i].res = sc->sc_memr;
                ch->r_io[i].offset = i*ATA_KAUAI_REGGAP + ATA_KAUAI_REGOFFSET;
        }
        ch->r_io[ATA_CONTROL].res = sc->sc_memr;
        ch->r_io[ATA_CONTROL].offset = ATA_KAUAI_ALTOFFSET;
	ata_default_registers(dev);

	ch->unit = 0;
	ch->flags |= ATA_USE_16BIT;

	/* XXX: ATAPI DMA is unreliable. We should find out why. */
	ch->flags |= ATA_NO_ATAPI_DMA;
	ata_generic_hw(dev);

	pci_enable_busmaster(dev);

	/* Init DMA engine */

	sc->sc_ch.dbdma_rid = 1;
	sc->sc_ch.dbdma_regs = sc->sc_memr;
	sc->sc_ch.dbdma_offset = ATA_KAUAI_DBDMAOFFSET;

	ata_dbdma_dmainit(dev);

#if USE_DBDMA_IRQ
	/* Bind to DBDMA interrupt as well */
	if ((dbdma_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &dbdma_irq_rid, RF_SHAREABLE | RF_ACTIVE)) != NULL) {
		bus_setup_intr(dev, dbdma_irq, ATA_INTR_FLAGS, NULL,
		    (driver_intr_t *)ata_kauai_dma_interrupt, sc,&cookie);
	}
#endif

	/* Set up initial mode */
	sc->pioconf[0] = sc->pioconf[1] = 
	    bus_read_4(sc->sc_memr, PIO_CONFIG_REG) & 0x0f000fff;

	sc->udmaconf[0] = sc->udmaconf[1] = 0;
	sc->wdmaconf[0] = sc->wdmaconf[1] = 0;

	/* Magic FCR value from Apple */
	bus_write_4(sc->sc_memr, 0, 0x00000007);

	/* Set begin_transaction */
	sc->sc_ch.sc_ch.hw.begin_transaction = ata_kauai_begin_transaction;

	return ata_attach(dev);
}

static int
ata_kauai_setmode(device_t dev, int target, int mode)
{
	struct ata_kauai_softc *sc = device_get_softc(dev);

	mode = min(mode,sc->shasta ? ATA_UDMA6 : ATA_UDMA5);

	if (sc->shasta) {
		switch (mode & ATA_DMA_MASK) {
		    case ATA_UDMA0:
			sc->udmaconf[target] 
			    = udma_timing_shasta[mode & ATA_MODE_MASK];
			break;
		    case ATA_WDMA0:
			sc->udmaconf[target] = 0;
			sc->wdmaconf[target] 
			    = dma_timing_shasta[mode & ATA_MODE_MASK];
			break;
		    default:
			sc->pioconf[target] 
			    = pio_timing_shasta[(mode & ATA_MODE_MASK) - 
			    ATA_PIO0];
			break;
		}
	} else {
		switch (mode & ATA_DMA_MASK) {
		    case ATA_UDMA0:
			sc->udmaconf[target] 
			    = udma_timing_kauai[mode & ATA_MODE_MASK];
			break;
		    case ATA_WDMA0:
			sc->udmaconf[target] = 0;
			sc->wdmaconf[target]
			    = dma_timing_kauai[mode & ATA_MODE_MASK];
			break;
		    default:
			sc->pioconf[target] 
			    = pio_timing_kauai[(mode & ATA_MODE_MASK)
			    - ATA_PIO0];
			break;
		}
	}

	return (mode);
}

static int
ata_kauai_begin_transaction(struct ata_request *request)
{
	struct ata_kauai_softc *sc = device_get_softc(request->parent);

	bus_write_4(sc->sc_memr, UDMA_CONFIG_REG, sc->udmaconf[request->unit]);
	bus_write_4(sc->sc_memr, PIO_CONFIG_REG, 
	    sc->wdmaconf[request->unit] | sc->pioconf[request->unit]);

	return ata_begin_transaction(request);
}
