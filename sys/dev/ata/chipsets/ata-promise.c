/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 - 2008 Søren Schmidt <sos@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/ata.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sema.h>
#include <sys/taskqueue.h>
#include <vm/uma.h>
#include <machine/stdarg.h>
#include <machine/resource.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/ata/ata-all.h>
#include <dev/ata/ata-pci.h>
#include <ata_if.h>

/* local prototypes */
static int ata_promise_chipinit(device_t dev);
static int ata_promise_ch_attach(device_t dev);
static int ata_promise_status(device_t dev);
static int ata_promise_dmastart(struct ata_request *request);
static int ata_promise_dmastop(struct ata_request *request);
static void ata_promise_dmareset(device_t dev);
static int ata_promise_setmode(device_t dev, int target, int mode);
static int ata_promise_tx2_ch_attach(device_t dev);
static int ata_promise_tx2_status(device_t dev);
static int ata_promise_mio_ch_attach(device_t dev);
static int ata_promise_mio_ch_detach(device_t dev);
static void ata_promise_mio_intr(void *data);
static int ata_promise_mio_status(device_t dev);
static int ata_promise_mio_command(struct ata_request *request);
static void ata_promise_mio_reset(device_t dev);
static int ata_promise_mio_pm_read(device_t dev, int port, int reg, u_int32_t *result);
static int ata_promise_mio_pm_write(device_t dev, int port, int reg, u_int32_t result);
static u_int32_t ata_promise_mio_softreset(device_t dev, int port);
static void ata_promise_mio_dmainit(device_t dev);
static void ata_promise_mio_setprd(void *xsc, bus_dma_segment_t *segs, int nsegs, int error);
static int ata_promise_mio_setmode(device_t dev, int target, int mode);
static int ata_promise_mio_getrev(device_t dev, int target);
static void ata_promise_sx4_intr(void *data);
static int ata_promise_sx4_command(struct ata_request *request);
static int ata_promise_apkt(u_int8_t *bytep, struct ata_request *request);
static void ata_promise_queue_hpkt(struct ata_pci_controller *ctlr, u_int32_t hpkt);
static void ata_promise_next_hpkt(struct ata_pci_controller *ctlr);

/* misc defines */
#define PR_OLD		0
#define PR_NEW		1
#define PR_TX		2
#define PR_MIO		3
#define PR_TX4		0x01
#define PR_SX4X		0x02
#define PR_SX6K		0x04
#define PR_PATA		0x08
#define PR_CMBO		0x10
#define PR_CMBO2	0x20
#define PR_SATA		0x40
#define PR_SATA2	0x80

/*
 * Promise chipset support functions
 */
#define ATA_PDC_APKT_OFFSET     0x00000010 
#define ATA_PDC_HPKT_OFFSET     0x00000040
#define ATA_PDC_ASG_OFFSET      0x00000080
#define ATA_PDC_LSG_OFFSET      0x000000c0
#define ATA_PDC_HSG_OFFSET      0x00000100
#define ATA_PDC_CHN_OFFSET      0x00000400
#define ATA_PDC_BUF_BASE        0x00400000
#define ATA_PDC_BUF_OFFSET      0x00100000
#define ATA_PDC_MAX_HPKT        8
#define ATA_PDC_WRITE_REG       0x00
#define ATA_PDC_WRITE_CTL       0x0e
#define ATA_PDC_WRITE_END       0x08
#define ATA_PDC_WAIT_NBUSY      0x10
#define ATA_PDC_WAIT_READY      0x18
#define ATA_PDC_1B              0x20
#define ATA_PDC_2B              0x40

struct host_packet {
    u_int32_t                   addr;
    TAILQ_ENTRY(host_packet)    chain;
};

struct ata_promise_sx4 {
    struct mtx                  mtx;
    TAILQ_HEAD(, host_packet)   queue;
    int                         busy;
};

static int
ata_promise_probe(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    const struct ata_chip_id *idx;
    static const struct ata_chip_id ids[] =
    {{ ATA_PDC20246,  0, PR_OLD, 0x00,     ATA_UDMA2, "PDC20246" },
     { ATA_PDC20262,  0, PR_NEW, 0x00,     ATA_UDMA4, "PDC20262" },
     { ATA_PDC20263,  0, PR_NEW, 0x00,     ATA_UDMA4, "PDC20263" },
     { ATA_PDC20265,  0, PR_NEW, 0x00,     ATA_UDMA5, "PDC20265" },
     { ATA_PDC20267,  0, PR_NEW, 0x00,     ATA_UDMA5, "PDC20267" },
     { ATA_PDC20268,  0, PR_TX,  PR_TX4,   ATA_UDMA5, "PDC20268" },
     { ATA_PDC20269,  0, PR_TX,  0x00,     ATA_UDMA6, "PDC20269" },
     { ATA_PDC20270,  0, PR_TX,  PR_TX4,   ATA_UDMA5, "PDC20270" },
     { ATA_PDC20271,  0, PR_TX,  0x00,     ATA_UDMA6, "PDC20271" },
     { ATA_PDC20275,  0, PR_TX,  0x00,     ATA_UDMA6, "PDC20275" },
     { ATA_PDC20276,  0, PR_TX,  PR_SX6K,  ATA_UDMA6, "PDC20276" },
     { ATA_PDC20277,  0, PR_TX,  0x00,     ATA_UDMA6, "PDC20277" },
     { ATA_PDC20318,  0, PR_MIO, PR_SATA,  ATA_SA150, "PDC20318" },
     { ATA_PDC20319,  0, PR_MIO, PR_SATA,  ATA_SA150, "PDC20319" },
     { ATA_PDC20371,  0, PR_MIO, PR_CMBO,  ATA_SA150, "PDC20371" },
     { ATA_PDC20375,  0, PR_MIO, PR_CMBO,  ATA_SA150, "PDC20375" },
     { ATA_PDC20376,  0, PR_MIO, PR_CMBO,  ATA_SA150, "PDC20376" },
     { ATA_PDC20377,  0, PR_MIO, PR_CMBO,  ATA_SA150, "PDC20377" },
     { ATA_PDC20378,  0, PR_MIO, PR_CMBO,  ATA_SA150, "PDC20378" },
     { ATA_PDC20379,  0, PR_MIO, PR_CMBO,  ATA_SA150, "PDC20379" },
     { ATA_PDC20571,  0, PR_MIO, PR_CMBO2, ATA_SA150, "PDC20571" },
     { ATA_PDC20575,  0, PR_MIO, PR_CMBO2, ATA_SA150, "PDC20575" },
     { ATA_PDC20579,  0, PR_MIO, PR_CMBO2, ATA_SA150, "PDC20579" },
     { ATA_PDC20771,  0, PR_MIO, PR_CMBO2, ATA_SA300, "PDC20771" },
     { ATA_PDC40775,  0, PR_MIO, PR_CMBO2, ATA_SA300, "PDC40775" },
     { ATA_PDC20617,  0, PR_MIO, PR_PATA,  ATA_UDMA6, "PDC20617" },
     { ATA_PDC20618,  0, PR_MIO, PR_PATA,  ATA_UDMA6, "PDC20618" },
     { ATA_PDC20619,  0, PR_MIO, PR_PATA,  ATA_UDMA6, "PDC20619" },
     { ATA_PDC20620,  0, PR_MIO, PR_PATA,  ATA_UDMA6, "PDC20620" },
     { ATA_PDC20621,  0, PR_MIO, PR_SX4X,  ATA_UDMA5, "PDC20621" },
     { ATA_PDC20622,  0, PR_MIO, PR_SX4X,  ATA_SA150, "PDC20622" },
     { ATA_PDC40518,  0, PR_MIO, PR_SATA2, ATA_SA150, "PDC40518" },
     { ATA_PDC40519,  0, PR_MIO, PR_SATA2, ATA_SA150, "PDC40519" },
     { ATA_PDC40718,  0, PR_MIO, PR_SATA2, ATA_SA300, "PDC40718" },
     { ATA_PDC40719,  0, PR_MIO, PR_SATA2, ATA_SA300, "PDC40719" },
     { ATA_PDC40779,  0, PR_MIO, PR_SATA2, ATA_SA300, "PDC40779" },
     { 0, 0, 0, 0, 0, 0}};
    char buffer[64];
    uintptr_t devid = 0;

    if (pci_get_vendor(dev) != ATA_PROMISE_ID)
	return ENXIO;

    if (!(idx = ata_match_chip(dev, ids)))
	return ENXIO;

    /* if we are on a SuperTrak SX6000 dont attach */
    if ((idx->cfg2 & PR_SX6K) && pci_get_class(GRANDPARENT(dev))==PCIC_BRIDGE &&
	!BUS_READ_IVAR(device_get_parent(GRANDPARENT(dev)),
		       GRANDPARENT(dev), PCI_IVAR_DEVID, &devid) &&
	devid == ATA_I960RM) 
	return ENXIO;

    strcpy(buffer, "Promise ");
    strcat(buffer, idx->text);

    /* if we are on a FastTrak TX4, adjust the interrupt resource */
    if ((idx->cfg2 & PR_TX4) && pci_get_class(GRANDPARENT(dev))==PCIC_BRIDGE &&
	!BUS_READ_IVAR(device_get_parent(GRANDPARENT(dev)),
		       GRANDPARENT(dev), PCI_IVAR_DEVID, &devid) &&
	((devid == ATA_DEC_21150) || (devid == ATA_DEC_21150_1))) {
	static rman_res_t start = 0, end = 0;

	if (pci_get_slot(dev) == 1) {
	    bus_get_resource(dev, SYS_RES_IRQ, 0, &start, &end);
	    strcat(buffer, " (channel 0+1)");
	}
	else if (pci_get_slot(dev) == 2 && start && end) {
	    bus_set_resource(dev, SYS_RES_IRQ, 0, start, end);
	    strcat(buffer, " (channel 2+3)");
	}
	else {
	    start = end = 0;
	}
    }
    sprintf(buffer, "%s %s controller", buffer, ata_mode2str(idx->max_dma));
    device_set_desc_copy(dev, buffer);
    ctlr->chip = idx;
    ctlr->chipinit = ata_promise_chipinit;
    return (BUS_PROBE_LOW_PRIORITY);
}

static int
ata_promise_chipinit(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(dev);
    int stat_reg;

    if (ata_setup_interrupt(dev, ata_generic_intr))
	return ENXIO;

    switch  (ctlr->chip->cfg1) {
    case PR_NEW:
	/* setup clocks */
	ATA_OUTB(ctlr->r_res1, 0x11, ATA_INB(ctlr->r_res1, 0x11) | 0x0a);
	/* FALLTHROUGH */

    case PR_OLD:
	/* enable burst mode */
	ATA_OUTB(ctlr->r_res1, 0x1f, ATA_INB(ctlr->r_res1, 0x1f) | 0x01);
	ctlr->ch_attach = ata_promise_ch_attach;
	ctlr->ch_detach = ata_pci_ch_detach;
	ctlr->setmode = ata_promise_setmode;
	return 0;

    case PR_TX:
	ctlr->ch_attach = ata_promise_tx2_ch_attach;
	ctlr->ch_detach = ata_pci_ch_detach;
	ctlr->setmode = ata_promise_setmode;
	return 0;

    case PR_MIO:
	ctlr->r_type1 = SYS_RES_MEMORY;
	ctlr->r_rid1 = PCIR_BAR(4);
	if (!(ctlr->r_res1 = bus_alloc_resource_any(dev, ctlr->r_type1,
						    &ctlr->r_rid1, RF_ACTIVE)))
	    goto failnfree;

#ifdef __sparc64__
	if (ctlr->chip->cfg2 == PR_SX4X &&
	    !bus_space_map(rman_get_bustag(ctlr->r_res1),
	    rman_get_bushandle(ctlr->r_res1), rman_get_size(ctlr->r_res1),
	    BUS_SPACE_MAP_LINEAR, NULL))
		goto failnfree;
#endif

	ctlr->r_type2 = SYS_RES_MEMORY;
	ctlr->r_rid2 = PCIR_BAR(3);
	if (!(ctlr->r_res2 = bus_alloc_resource_any(dev, ctlr->r_type2,
						    &ctlr->r_rid2, RF_ACTIVE)))
	    goto failnfree;

	if (ctlr->chip->cfg2 == PR_SX4X) {
	    struct ata_promise_sx4 *hpkt;
	    u_int32_t dimm = ATA_INL(ctlr->r_res2, 0x000c0080);

	    if (bus_teardown_intr(dev, ctlr->r_irq, ctlr->handle) ||
		bus_setup_intr(dev, ctlr->r_irq, ATA_INTR_FLAGS, NULL,
			       ata_promise_sx4_intr, ctlr, &ctlr->handle)) {
		device_printf(dev, "unable to setup interrupt\n");
		goto failnfree;
	    }

	    /* print info about cache memory */
	    device_printf(dev, "DIMM size %dMB @ 0x%08x%s\n",
			  (((dimm >> 16) & 0xff)-((dimm >> 24) & 0xff)+1) << 4,
			  ((dimm >> 24) & 0xff),
			  ATA_INL(ctlr->r_res2, 0x000c0088) & (1<<16) ?
			  " ECC enabled" : "" );

	    /* adjust cache memory parameters */
	    ATA_OUTL(ctlr->r_res2, 0x000c000c, 
		     (ATA_INL(ctlr->r_res2, 0x000c000c) & 0xffff0000));

	    /* setup host packet controls */
	    hpkt = malloc(sizeof(struct ata_promise_sx4),
			  M_ATAPCI, M_NOWAIT | M_ZERO);
	    if (hpkt == NULL) {
		device_printf(dev, "Cannot allocate HPKT\n");
		goto failnfree;
	    }
	    mtx_init(&hpkt->mtx, "ATA promise HPKT lock", NULL, MTX_DEF);
	    TAILQ_INIT(&hpkt->queue);
	    hpkt->busy = 0;
	    ctlr->chipset_data = hpkt;
	    ctlr->ch_attach = ata_promise_mio_ch_attach;
	    ctlr->ch_detach = ata_promise_mio_ch_detach;
	    ctlr->reset = ata_promise_mio_reset;
	    ctlr->setmode = ata_promise_setmode;
	    ctlr->channels = 4;
	    return 0;
	}

	/* mio type controllers need an interrupt intercept */
	if (bus_teardown_intr(dev, ctlr->r_irq, ctlr->handle) ||
	    bus_setup_intr(dev, ctlr->r_irq, ATA_INTR_FLAGS, NULL,
			       ata_promise_mio_intr, ctlr, &ctlr->handle)) {
		device_printf(dev, "unable to setup interrupt\n");
		goto failnfree;
	}

	switch (ctlr->chip->cfg2) {
	case PR_PATA:
	    ctlr->channels = ((ATA_INL(ctlr->r_res2, 0x48) & 0x01) > 0) +
			     ((ATA_INL(ctlr->r_res2, 0x48) & 0x02) > 0) + 2;
	    goto sata150;
	case PR_CMBO:
	    ctlr->channels = 3;
	    goto sata150;
	case PR_SATA:
	    ctlr->channels = 4;
sata150:
	    stat_reg = 0x6c;
	    break;

	case PR_CMBO2: 
	    ctlr->channels = 3;
	    goto sataii;
	case PR_SATA2:
	default:
	    ctlr->channels = 4;
sataii:
	    stat_reg = 0x60;
	    break;
	}

	/* prime fake interrupt register */
	ctlr->chipset_data = (void *)(uintptr_t)0xffffffff;

	/* clear SATA status and unmask interrupts */
	ATA_OUTL(ctlr->r_res2, stat_reg, 0x000000ff);

	/* enable "long burst length" on gen2 chips */
	if ((ctlr->chip->cfg2 == PR_SATA2) || (ctlr->chip->cfg2 == PR_CMBO2))
	    ATA_OUTL(ctlr->r_res2, 0x44, ATA_INL(ctlr->r_res2, 0x44) | 0x2000);

	ctlr->ch_attach = ata_promise_mio_ch_attach;
	ctlr->ch_detach = ata_promise_mio_ch_detach;
	ctlr->reset = ata_promise_mio_reset;
	ctlr->setmode = ata_promise_mio_setmode;
	ctlr->getrev = ata_promise_mio_getrev;

	return 0;
    }

failnfree:
    if (ctlr->r_res2)
	bus_release_resource(dev, ctlr->r_type2, ctlr->r_rid2, ctlr->r_res2);
    if (ctlr->r_res1)
	bus_release_resource(dev, ctlr->r_type1, ctlr->r_rid1, ctlr->r_res1);
    return ENXIO;
}

static int
ata_promise_ch_attach(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);

    if (ata_pci_ch_attach(dev))
	return ENXIO;

    if (ctlr->chip->cfg1 == PR_NEW) {
        ch->dma.start = ata_promise_dmastart;
        ch->dma.stop = ata_promise_dmastop;
        ch->dma.reset = ata_promise_dmareset;
    }

    ch->hw.status = ata_promise_status;
    ch->flags |= ATA_NO_ATAPI_DMA;
    ch->flags |= ATA_CHECKS_CABLE;
    return 0;
}

static int
ata_promise_status(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);

    if (ATA_INL(ctlr->r_res1, 0x1c) & (ch->unit ? 0x00004000 : 0x00000400)) {
	return ata_pci_status(dev);
    }
    return 0;
}

static int
ata_promise_dmastart(struct ata_request *request)
{
    struct ata_pci_controller *ctlr=device_get_softc(device_get_parent(request->parent));
    struct ata_channel *ch = device_get_softc(request->parent);

    if (request->flags & ATA_R_48BIT) {
	ATA_OUTB(ctlr->r_res1, 0x11,
		 ATA_INB(ctlr->r_res1, 0x11) | (ch->unit ? 0x08 : 0x02));
	ATA_OUTL(ctlr->r_res1, ch->unit ? 0x24 : 0x20,
		 ((request->flags & ATA_R_READ) ? 0x05000000 : 0x06000000) |
		 (request->bytecount >> 1));
    }
    ATA_IDX_OUTB(ch, ATA_BMSTAT_PORT, (ATA_IDX_INB(ch, ATA_BMSTAT_PORT) |
		 (ATA_BMSTAT_INTERRUPT | ATA_BMSTAT_ERROR)));
    ATA_IDX_OUTL(ch, ATA_BMDTP_PORT, request->dma->sg_bus);
    ATA_IDX_OUTB(ch, ATA_BMCMD_PORT,
		 ((request->flags & ATA_R_READ) ? ATA_BMCMD_WRITE_READ : 0) |
		 ATA_BMCMD_START_STOP);
    ch->dma.flags |= ATA_DMA_ACTIVE;
    return 0;
}

static int
ata_promise_dmastop(struct ata_request *request)
{
    struct ata_pci_controller *ctlr=device_get_softc(device_get_parent(request->parent));
    struct ata_channel *ch = device_get_softc(request->parent);
    int error;

    if (request->flags & ATA_R_48BIT) {
	ATA_OUTB(ctlr->r_res1, 0x11,
		 ATA_INB(ctlr->r_res1, 0x11) & ~(ch->unit ? 0x08 : 0x02));
	ATA_OUTL(ctlr->r_res1, ch->unit ? 0x24 : 0x20, 0);
    }
    error = ATA_IDX_INB(ch, ATA_BMSTAT_PORT);
    ATA_IDX_OUTB(ch, ATA_BMCMD_PORT,
		 ATA_IDX_INB(ch, ATA_BMCMD_PORT) & ~ATA_BMCMD_START_STOP);
    ATA_IDX_OUTB(ch, ATA_BMSTAT_PORT, ATA_BMSTAT_INTERRUPT | ATA_BMSTAT_ERROR); 
    ch->dma.flags &= ~ATA_DMA_ACTIVE;
    return error;
}

static void
ata_promise_dmareset(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);

    ATA_IDX_OUTB(ch, ATA_BMCMD_PORT,
		 ATA_IDX_INB(ch, ATA_BMCMD_PORT) & ~ATA_BMCMD_START_STOP);
    ATA_IDX_OUTB(ch, ATA_BMSTAT_PORT, ATA_BMSTAT_INTERRUPT | ATA_BMSTAT_ERROR); 
    ch->flags &= ~ATA_DMA_ACTIVE;
}

static int
ata_promise_setmode(device_t dev, int target, int mode)
{
    device_t parent = device_get_parent(dev);
    struct ata_pci_controller *ctlr = device_get_softc(parent);
    struct ata_channel *ch = device_get_softc(dev);
    int devno = (ch->unit << 1) + target;
    static const uint32_t timings[][2] = {
    /*    PR_OLD      PR_NEW               mode */
	{ 0x004ff329, 0x004fff2f },     /* PIO 0 */
	{ 0x004fec25, 0x004ff82a },     /* PIO 1 */
	{ 0x004fe823, 0x004ff026 },     /* PIO 2 */
	{ 0x004fe622, 0x004fec24 },     /* PIO 3 */
	{ 0x004fe421, 0x004fe822 },     /* PIO 4 */
	{ 0x004567f3, 0x004acef6 },     /* MWDMA 0 */
	{ 0x004467f3, 0x0048cef6 },     /* MWDMA 1 */
	{ 0x004367f3, 0x0046cef6 },     /* MWDMA 2 */
	{ 0x004367f3, 0x0046cef6 },     /* UDMA 0 */
	{ 0x004247f3, 0x00448ef6 },     /* UDMA 1 */
	{ 0x004127f3, 0x00436ef6 },     /* UDMA 2 */
	{ 0,          0x00424ef6 },     /* UDMA 3 */
	{ 0,          0x004127f3 },     /* UDMA 4 */
	{ 0,          0x004127f3 }      /* UDMA 5 */
    };

    mode = min(mode, ctlr->chip->max_dma);

    switch (ctlr->chip->cfg1) {
    case PR_OLD:
    case PR_NEW:
	if (ata_dma_check_80pin && mode > ATA_UDMA2 &&
	    (pci_read_config(parent, 0x50, 2) &
				 (ch->unit ? 1 << 11 : 1 << 10))) {
	    ata_print_cable(dev, "controller");
	    mode = ATA_UDMA2;
	}
	break;

    case PR_TX:
	ATA_IDX_OUTB(ch, ATA_BMDEVSPEC_0, 0x0b);
	if (ata_dma_check_80pin && mode > ATA_UDMA2 &&
	    ATA_IDX_INB(ch, ATA_BMDEVSPEC_1) & 0x04) {
	    ata_print_cable(dev, "controller");
	    mode = ATA_UDMA2;
	}
	break;
   
    case PR_MIO:
	if (ata_dma_check_80pin && mode > ATA_UDMA2 &&
	    (ATA_INL(ctlr->r_res2,
		     (ctlr->chip->cfg2 & PR_SX4X ? 0x000c0260 : 0x0260) +
		     (ch->unit << 7)) & 0x01000000)) {
	    ata_print_cable(dev, "controller");
	    mode = ATA_UDMA2;
	}
	break;
    }

	if (ctlr->chip->cfg1 < PR_TX)
	    pci_write_config(parent, 0x60 + (devno << 2),
			     timings[ata_mode2idx(mode)][ctlr->chip->cfg1], 4);
	return (mode);
}

static int
ata_promise_tx2_ch_attach(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);

    if (ata_pci_ch_attach(dev))
	return ENXIO;

    ch->hw.status = ata_promise_tx2_status;
    ch->flags |= ATA_CHECKS_CABLE;
    return 0;
}

static int
ata_promise_tx2_status(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);

    ATA_IDX_OUTB(ch, ATA_BMDEVSPEC_0, 0x0b);
    if (ATA_IDX_INB(ch, ATA_BMDEVSPEC_1) & 0x20) {
	return ata_pci_status(dev);
    }
    return 0;
}

static int
ata_promise_mio_ch_attach(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);
    int offset = (ctlr->chip->cfg2 & PR_SX4X) ? 0x000c0000 : 0;
    int i;

    ata_promise_mio_dmainit(dev);

    for (i = ATA_DATA; i <= ATA_COMMAND; i++) {
	ch->r_io[i].res = ctlr->r_res2;
	ch->r_io[i].offset = offset + 0x0200 + (i << 2) + (ch->unit << 7); 
    }
    ch->r_io[ATA_CONTROL].res = ctlr->r_res2;
    ch->r_io[ATA_CONTROL].offset = offset + 0x0238 + (ch->unit << 7);
    ch->r_io[ATA_IDX_ADDR].res = ctlr->r_res2;
    ata_default_registers(dev);
    if ((ctlr->chip->cfg2 & (PR_SATA | PR_SATA2)) ||
	((ctlr->chip->cfg2 & (PR_CMBO | PR_CMBO2)) && ch->unit < 2)) {
	ch->r_io[ATA_SSTATUS].res = ctlr->r_res2;
	ch->r_io[ATA_SSTATUS].offset = 0x400 + (ch->unit << 8);
	ch->r_io[ATA_SERROR].res = ctlr->r_res2;
	ch->r_io[ATA_SERROR].offset = 0x404 + (ch->unit << 8);
	ch->r_io[ATA_SCONTROL].res = ctlr->r_res2;
	ch->r_io[ATA_SCONTROL].offset = 0x408 + (ch->unit << 8);
	ch->flags |= ATA_NO_SLAVE;
	ch->flags |= ATA_SATA;
    }
    ch->flags |= ATA_USE_16BIT;
    ch->flags |= ATA_CHECKS_CABLE;

    ata_generic_hw(dev);
    if (ctlr->chip->cfg2 & PR_SX4X) {
	ch->hw.command = ata_promise_sx4_command;
    }
    else {
	ch->hw.command = ata_promise_mio_command;
	ch->hw.status = ata_promise_mio_status;
	ch->hw.softreset = ata_promise_mio_softreset;
	ch->hw.pm_read = ata_promise_mio_pm_read;
	ch->hw.pm_write = ata_promise_mio_pm_write;
     }
    return 0;
}

static int
ata_promise_mio_ch_detach(device_t dev)
{

    ata_dmafini(dev);
    return (0);
}

static void
ata_promise_mio_intr(void *data)
{
    struct ata_pci_controller *ctlr = data;
    struct ata_channel *ch;
    u_int32_t vector;
    int unit;

    /*
     * since reading interrupt status register on early "mio" chips
     * clears the status bits we cannot read it for each channel later on
     * in the generic interrupt routine.
     */
    vector = ATA_INL(ctlr->r_res2, 0x040);
    ATA_OUTL(ctlr->r_res2, 0x040, vector);
    ctlr->chipset_data = (void *)(uintptr_t)vector;

    for (unit = 0; unit < ctlr->channels; unit++) {
	if ((ch = ctlr->interrupt[unit].argument))
	    ctlr->interrupt[unit].function(ch);
    }

    ctlr->chipset_data = (void *)(uintptr_t)0xffffffff;
}

static int
ata_promise_mio_status(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);
    u_int32_t stat_reg, vector, status;

    switch (ctlr->chip->cfg2) {
    case PR_PATA:
    case PR_CMBO:
    case PR_SATA:
	stat_reg = 0x6c;
	break;
    case PR_CMBO2: 
    case PR_SATA2:
    default:
	stat_reg = 0x60;
	break;
    }

    /* read and acknowledge interrupt */
    vector = (uint32_t)(uintptr_t)ctlr->chipset_data;

    /* read and clear interface status */
    status = ATA_INL(ctlr->r_res2, stat_reg);
    ATA_OUTL(ctlr->r_res2, stat_reg, status & (0x00000011 << ch->unit));

    /* check for and handle disconnect events */
    if (status & (0x00000001 << ch->unit)) {
	if (bootverbose)
	    device_printf(dev, "DISCONNECT requested\n");
	taskqueue_enqueue(taskqueue_thread, &ch->conntask);
    }

    /* check for and handle connect events */
    if (status & (0x00000010 << ch->unit)) {
	if (bootverbose)
	    device_printf(dev, "CONNECT requested\n");
	taskqueue_enqueue(taskqueue_thread, &ch->conntask);
    }

    /* do we have any device action ? */
    return (vector & (1 << (ch->unit + 1)));
}

static int
ata_promise_mio_command(struct ata_request *request)
{
    struct ata_pci_controller *ctlr=device_get_softc(device_get_parent(request->parent));
    struct ata_channel *ch = device_get_softc(request->parent);

    u_int32_t *wordp = (u_int32_t *)ch->dma.work;

    ATA_OUTL(ctlr->r_res2, (ch->unit + 1) << 2, 0x00000001);

    if ((ctlr->chip->cfg2 == PR_SATA2) ||
        ((ctlr->chip->cfg2 == PR_CMBO2) && (ch->unit < 2))) {
	/* set portmultiplier port */
	ATA_OUTB(ctlr->r_res2, 0x4e8 + (ch->unit << 8), request->unit & 0x0f);
    }

    /* XXX SOS add ATAPI commands support later */
    switch (request->u.ata.command) {
    default:
	return ata_generic_command(request);

    case ATA_READ_DMA:
    case ATA_READ_DMA48:
	wordp[0] = htole32(0x04 | ((ch->unit + 1) << 16) | (0x00 << 24));
	break;

    case ATA_WRITE_DMA:
    case ATA_WRITE_DMA48:
	wordp[0] = htole32(0x00 | ((ch->unit + 1) << 16) | (0x00 << 24));
	break;
    }
    wordp[1] = htole32(request->dma->sg_bus);
    wordp[2] = 0;
    ata_promise_apkt((u_int8_t*)wordp, request);

    ATA_OUTL(ctlr->r_res2, 0x0240 + (ch->unit << 7), ch->dma.work_bus);
    return 0;
}

static void
ata_promise_mio_reset(device_t dev)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);
    struct ata_promise_sx4 *hpktp;

    switch (ctlr->chip->cfg2) {
    case PR_SX4X:

	/* softreset channel ATA module */
	hpktp = ctlr->chipset_data;
	ATA_OUTL(ctlr->r_res2, 0xc0260 + (ch->unit << 7), ch->unit + 1);
	ata_udelay(1000);
	ATA_OUTL(ctlr->r_res2, 0xc0260 + (ch->unit << 7),
		 (ATA_INL(ctlr->r_res2, 0xc0260 + (ch->unit << 7)) &
		  ~0x00003f9f) | (ch->unit + 1));

	/* softreset HOST module */ /* XXX SOS what about other outstandings */
	mtx_lock(&hpktp->mtx);
	ATA_OUTL(ctlr->r_res2, 0xc012c,
		 (ATA_INL(ctlr->r_res2, 0xc012c) & ~0x00000f9f) | (1 << 11));
	DELAY(10);
	ATA_OUTL(ctlr->r_res2, 0xc012c,
		 (ATA_INL(ctlr->r_res2, 0xc012c) & ~0x00000f9f));
	hpktp->busy = 0;
	mtx_unlock(&hpktp->mtx);
	ata_generic_reset(dev);
	break;

    case PR_PATA:
    case PR_CMBO:
    case PR_SATA:
	if ((ctlr->chip->cfg2 == PR_SATA) ||
	    ((ctlr->chip->cfg2 == PR_CMBO) && (ch->unit < 2))) {

	    /* mask plug/unplug intr */
	    ATA_OUTL(ctlr->r_res2, 0x06c, (0x00110000 << ch->unit));
	}

	/* softreset channels ATA module */
	ATA_OUTL(ctlr->r_res2, 0x0260 + (ch->unit << 7), (1 << 11));
	ata_udelay(10000);
	ATA_OUTL(ctlr->r_res2, 0x0260 + (ch->unit << 7),
		 (ATA_INL(ctlr->r_res2, 0x0260 + (ch->unit << 7)) &
		  ~0x00003f9f) | (ch->unit + 1));

	if ((ctlr->chip->cfg2 == PR_SATA) ||
	    ((ctlr->chip->cfg2 == PR_CMBO) && (ch->unit < 2))) {

	    if (ata_sata_phy_reset(dev, -1, 1))
		ata_generic_reset(dev);
	    else
		ch->devices = 0;

	    /* reset and enable plug/unplug intr */
	    ATA_OUTL(ctlr->r_res2, 0x06c, (0x00000011 << ch->unit));
	}
	else
	    ata_generic_reset(dev);
	break;

    case PR_CMBO2:
    case PR_SATA2:
	if ((ctlr->chip->cfg2 == PR_SATA2) ||
	    ((ctlr->chip->cfg2 == PR_CMBO2) && (ch->unit < 2))) {
	    /* set portmultiplier port */
	    //ATA_OUTL(ctlr->r_res2, 0x4e8 + (ch->unit << 8), 0x0f);

	    /* mask plug/unplug intr */
	    ATA_OUTL(ctlr->r_res2, 0x060, (0x00110000 << ch->unit));
	}

	/* softreset channels ATA module */
	ATA_OUTL(ctlr->r_res2, 0x0260 + (ch->unit << 7), (1 << 11));
	ata_udelay(10000);
	ATA_OUTL(ctlr->r_res2, 0x0260 + (ch->unit << 7),
		 (ATA_INL(ctlr->r_res2, 0x0260 + (ch->unit << 7)) &
		  ~0x00003f9f) | (ch->unit + 1));

	if ((ctlr->chip->cfg2 == PR_SATA2) ||
	    ((ctlr->chip->cfg2 == PR_CMBO2) && (ch->unit < 2))) {

	    /* set PHY mode to "improved" */
	    ATA_OUTL(ctlr->r_res2, 0x414 + (ch->unit << 8),
		     (ATA_INL(ctlr->r_res2, 0x414 + (ch->unit << 8)) &
		     ~0x00000003) | 0x00000001);

	    if (ata_sata_phy_reset(dev, -1, 1)) {
		u_int32_t signature = ch->hw.softreset(dev, ATA_PM);

		if (1 | bootverbose)
        	    device_printf(dev, "SIGNATURE: %08x\n", signature);

		switch (signature >> 16) {
		case 0x0000:
		    ch->devices = ATA_ATA_MASTER;
		    break;
		case 0x9669:
		    ch->devices = ATA_PORTMULTIPLIER;
		    ata_pm_identify(dev);
		    break;
		case 0xeb14:
		    ch->devices = ATA_ATAPI_MASTER;
		    break;
		default: /* SOS XXX */
		    if (bootverbose)
			device_printf(dev,
				      "No signature, assuming disk device\n");
		    ch->devices = ATA_ATA_MASTER;
		}
		if (bootverbose)
		    device_printf(dev, "promise_mio_reset devices=%08x\n",
		    		  ch->devices);

	    } else
		ch->devices = 0;

	    /* reset and enable plug/unplug intr */
	    ATA_OUTL(ctlr->r_res2, 0x060, (0x00000011 << ch->unit));

	    ///* set portmultiplier port */
	    ATA_OUTL(ctlr->r_res2, 0x4e8 + (ch->unit << 8), 0x00);
	}
	else
	    ata_generic_reset(dev);
	break;

    }
}

static int
ata_promise_mio_pm_read(device_t dev, int port, int reg, u_int32_t *result)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);
    int timeout = 0;

    if (port < 0) {
	*result = ATA_IDX_INL(ch, reg);
	return (0);
    }
    if (port < ATA_PM) {
	switch (reg) {
	case ATA_SSTATUS:
	    reg = 0;
	    break;
	case ATA_SERROR:
	    reg = 1;
	    break;
	case ATA_SCONTROL:
	    reg = 2;
	    break;
	default:
	    return (EINVAL);
	}
    }
    /* set portmultiplier port */
    ATA_OUTB(ctlr->r_res2, 0x4e8 + (ch->unit << 8), 0x0f);

    ATA_IDX_OUTB(ch, ATA_FEATURE, reg);
    ATA_IDX_OUTB(ch, ATA_DRIVE, port);

    ATA_IDX_OUTB(ch, ATA_COMMAND, ATA_READ_PM);

    while (timeout < 1000000) {
	u_int8_t status = ATA_IDX_INB(ch, ATA_STATUS);
	if (!(status & ATA_S_BUSY))
	    break;
	timeout += 1000;
	DELAY(1000);
    }
    if (timeout >= 1000000)
	return ATA_E_ABORT;

    *result = ATA_IDX_INB(ch, ATA_COUNT) |
	      (ATA_IDX_INB(ch, ATA_SECTOR) << 8) |
	      (ATA_IDX_INB(ch, ATA_CYL_LSB) << 16) |
	      (ATA_IDX_INB(ch, ATA_CYL_MSB) << 24);
    return 0;
}

static int
ata_promise_mio_pm_write(device_t dev, int port, int reg, u_int32_t value)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);
    int timeout = 0;

    if (port < 0) {
	ATA_IDX_OUTL(ch, reg, value);
	return (0);
    }
    if (port < ATA_PM) {
	switch (reg) {
	case ATA_SSTATUS:
	    reg = 0;
	    break;
	case ATA_SERROR:
	    reg = 1;
	    break;
	case ATA_SCONTROL:
	    reg = 2;
	    break;
	default:
	    return (EINVAL);
	}
    }
    /* set portmultiplier port */
    ATA_OUTB(ctlr->r_res2, 0x4e8 + (ch->unit << 8), 0x0f);

    ATA_IDX_OUTB(ch, ATA_FEATURE, reg);
    ATA_IDX_OUTB(ch, ATA_DRIVE, port);
    ATA_IDX_OUTB(ch, ATA_COUNT, value & 0xff);
    ATA_IDX_OUTB(ch, ATA_SECTOR, (value >> 8) & 0xff);
    ATA_IDX_OUTB(ch, ATA_CYL_LSB, (value >> 16) & 0xff);
    ATA_IDX_OUTB(ch, ATA_CYL_MSB, (value >> 24) & 0xff);

    ATA_IDX_OUTB(ch, ATA_COMMAND, ATA_WRITE_PM);

    while (timeout < 1000000) {
	u_int8_t status = ATA_IDX_INB(ch, ATA_STATUS);
	if (!(status & ATA_S_BUSY))
	    break;
	timeout += 1000;
	DELAY(1000);
    }
    if (timeout >= 1000000)
	return ATA_E_ABORT;

    return ATA_IDX_INB(ch, ATA_ERROR);
}

/* must be called with ATA channel locked and state_mtx held */
static u_int32_t
ata_promise_mio_softreset(device_t dev, int port)
{
    struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
    struct ata_channel *ch = device_get_softc(dev);
    int timeout;

    /* set portmultiplier port */
    ATA_OUTB(ctlr->r_res2, 0x4e8 + (ch->unit << 8), port & 0x0f);

    /* softreset device on this channel */
    ATA_IDX_OUTB(ch, ATA_DRIVE, ATA_D_IBM | ATA_D_LBA | ATA_DEV(ATA_MASTER));
    DELAY(10);
    ATA_IDX_OUTB(ch, ATA_CONTROL, ATA_A_IDS | ATA_A_RESET);
    ata_udelay(10000); 
    ATA_IDX_OUTB(ch, ATA_CONTROL, ATA_A_IDS);
    ata_udelay(150000);
    ATA_IDX_INB(ch, ATA_ERROR);

    /* wait for BUSY to go inactive */
    for (timeout = 0; timeout < 100; timeout++) {
	u_int8_t /* err, */ stat;

	/* err = */ ATA_IDX_INB(ch, ATA_ERROR);
	stat = ATA_IDX_INB(ch, ATA_STATUS);

	//if (stat == err && timeout > (stat & ATA_S_BUSY ? 100 : 10))
	    //break;

	if (!(stat & ATA_S_BUSY)) {
	    //if ((err & 0x7f) == ATA_E_ILI) {
		return ATA_IDX_INB(ch, ATA_COUNT) |
		       (ATA_IDX_INB(ch, ATA_SECTOR) << 8) |
		       (ATA_IDX_INB(ch, ATA_CYL_LSB) << 16) |
		       (ATA_IDX_INB(ch, ATA_CYL_MSB) << 24);
	    //}
	    //else if (stat & 0x0f) {
		//stat |= ATA_S_BUSY;
	    //}
	}

	if (!(stat & ATA_S_BUSY) || (stat == 0xff && timeout > 10))
	    break;
	ata_udelay(100000);
    }
    return -1;
}

static void
ata_promise_mio_dmainit(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);

    /* note start and stop are not used here */
    ch->dma.setprd = ata_promise_mio_setprd;
    ch->dma.max_iosize = 65536;
    ata_dmainit(dev);
}

#define MAXLASTSGSIZE (32 * sizeof(u_int32_t))
static void 
ata_promise_mio_setprd(void *xsc, bus_dma_segment_t *segs, int nsegs, int error)
{
    struct ata_dmasetprd_args *args = xsc;
    struct ata_dma_prdentry *prd = args->dmatab;
    int i;

    if ((args->error = error))
	return;

    for (i = 0; i < nsegs; i++) {
	prd[i].addr = htole32(segs[i].ds_addr);
	prd[i].count = htole32(segs[i].ds_len);
    }
    if (segs[i - 1].ds_len > MAXLASTSGSIZE) {
	//printf("split last SG element of %u\n", segs[i - 1].ds_len);
	prd[i - 1].count = htole32(segs[i - 1].ds_len - MAXLASTSGSIZE);
	prd[i].count = htole32(MAXLASTSGSIZE);
	prd[i].addr = htole32(segs[i - 1].ds_addr +
			      (segs[i - 1].ds_len - MAXLASTSGSIZE));
	nsegs++;
	i++;
    }
    prd[i - 1].count |= htole32(ATA_DMA_EOT);
    KASSERT(nsegs <= ATA_DMA_ENTRIES, ("too many DMA segment entries\n"));
    args->nsegs = nsegs;
}

static int
ata_promise_mio_setmode(device_t dev, int target, int mode)
{
        struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
        struct ata_channel *ch = device_get_softc(dev);

        if ( (ctlr->chip->cfg2 == PR_SATA) ||
    	    ((ctlr->chip->cfg2 == PR_CMBO) && (ch->unit < 2)) ||
	     (ctlr->chip->cfg2 == PR_SATA2) ||
	    ((ctlr->chip->cfg2 == PR_CMBO2) && (ch->unit < 2)))
		mode = ata_sata_setmode(dev, target, mode);
	else
		mode = ata_promise_setmode(dev, target, mode);
	return (mode);
}

static int
ata_promise_mio_getrev(device_t dev, int target)
{
        struct ata_pci_controller *ctlr = device_get_softc(device_get_parent(dev));
        struct ata_channel *ch = device_get_softc(dev);

        if ( (ctlr->chip->cfg2 == PR_SATA) ||
    	    ((ctlr->chip->cfg2 == PR_CMBO) && (ch->unit < 2)) ||
	     (ctlr->chip->cfg2 == PR_SATA2) ||
	    ((ctlr->chip->cfg2 == PR_CMBO2) && (ch->unit < 2)))
		return (ata_sata_getrev(dev, target));
	else
		return (0);
}

static void
ata_promise_sx4_intr(void *data)
{
    struct ata_pci_controller *ctlr = data;
    struct ata_channel *ch;
    u_int32_t vector = ATA_INL(ctlr->r_res2, 0x000c0480);
    int unit;

    for (unit = 0; unit < ctlr->channels; unit++) {
	if (vector & (1 << (unit + 1)))
	    if ((ch = ctlr->interrupt[unit].argument))
		ctlr->interrupt[unit].function(ch);
	if (vector & (1 << (unit + 5)))
	    if ((ch = ctlr->interrupt[unit].argument))
		ata_promise_queue_hpkt(ctlr,
				       htole32((ch->unit * ATA_PDC_CHN_OFFSET) +
					       ATA_PDC_HPKT_OFFSET));
	if (vector & (1 << (unit + 9))) {
	    ata_promise_next_hpkt(ctlr);
	    if ((ch = ctlr->interrupt[unit].argument))
		ctlr->interrupt[unit].function(ch);
	}
	if (vector & (1 << (unit + 13))) {
	    ata_promise_next_hpkt(ctlr);
	    if ((ch = ctlr->interrupt[unit].argument))
		ATA_OUTL(ctlr->r_res2, 0x000c0240 + (ch->unit << 7),
			 htole32((ch->unit * ATA_PDC_CHN_OFFSET) +
			 ATA_PDC_APKT_OFFSET));
	}
    }
}

static int
ata_promise_sx4_command(struct ata_request *request)
{
    device_t gparent = device_get_parent(request->parent);
    struct ata_pci_controller *ctlr = device_get_softc(gparent);
    struct ata_channel *ch = device_get_softc(request->parent);
    struct ata_dma_prdentry *prd;
    caddr_t window = rman_get_virtual(ctlr->r_res1);
    u_int32_t *wordp;
    int i, idx, length = 0;

    /* XXX SOS add ATAPI commands support later */
    switch (request->u.ata.command) {    

    default:
	return -1;

    case ATA_ATA_IDENTIFY:
    case ATA_READ:
    case ATA_READ48:
    case ATA_READ_MUL:
    case ATA_READ_MUL48:
    case ATA_WRITE:
    case ATA_WRITE48:
    case ATA_WRITE_MUL:
    case ATA_WRITE_MUL48:
	ATA_OUTL(ctlr->r_res2, 0x000c0400 + ((ch->unit + 1) << 2), 0x00000001);
	return ata_generic_command(request);

    case ATA_SETFEATURES:
    case ATA_FLUSHCACHE:
    case ATA_FLUSHCACHE48:
    case ATA_SLEEP:
    case ATA_SET_MULTI:
	wordp = (u_int32_t *)
	    (window + (ch->unit * ATA_PDC_CHN_OFFSET) + ATA_PDC_APKT_OFFSET);
	wordp[0] = htole32(0x08 | ((ch->unit + 1)<<16) | (0x00 << 24));
	wordp[1] = 0;
	wordp[2] = 0;
	ata_promise_apkt((u_int8_t *)wordp, request);
	ATA_OUTL(ctlr->r_res2, 0x000c0484, 0x00000001);
	ATA_OUTL(ctlr->r_res2, 0x000c0400 + ((ch->unit + 1) << 2), 0x00000001);
	ATA_OUTL(ctlr->r_res2, 0x000c0240 + (ch->unit << 7),
		 htole32((ch->unit * ATA_PDC_CHN_OFFSET)+ATA_PDC_APKT_OFFSET));
	return 0;

    case ATA_READ_DMA:
    case ATA_READ_DMA48:
    case ATA_WRITE_DMA:
    case ATA_WRITE_DMA48:
	prd = request->dma->sg;
	wordp = (u_int32_t *)
	    (window + (ch->unit * ATA_PDC_CHN_OFFSET) + ATA_PDC_HSG_OFFSET);
	i = idx = 0;
	do {
	    wordp[idx++] = prd[i].addr;
	    wordp[idx++] = prd[i].count;
	    length += (prd[i].count & ~ATA_DMA_EOT);
	} while (!(prd[i++].count & ATA_DMA_EOT));

	wordp = (u_int32_t *)
	    (window + (ch->unit * ATA_PDC_CHN_OFFSET) + ATA_PDC_LSG_OFFSET);
	wordp[0] = htole32((ch->unit * ATA_PDC_BUF_OFFSET) + ATA_PDC_BUF_BASE);
	wordp[1] = htole32(request->bytecount | ATA_DMA_EOT);

	wordp = (u_int32_t *)
	    (window + (ch->unit * ATA_PDC_CHN_OFFSET) + ATA_PDC_ASG_OFFSET);
	wordp[0] = htole32((ch->unit * ATA_PDC_BUF_OFFSET) + ATA_PDC_BUF_BASE);
	wordp[1] = htole32(request->bytecount | ATA_DMA_EOT);

	wordp = (u_int32_t *)
	    (window + (ch->unit * ATA_PDC_CHN_OFFSET) + ATA_PDC_HPKT_OFFSET);
	if (request->flags & ATA_R_READ)
	    wordp[0] = htole32(0x14 | ((ch->unit+9)<<16) | ((ch->unit+5)<<24));
	if (request->flags & ATA_R_WRITE)
	    wordp[0] = htole32(0x00 | ((ch->unit+13)<<16) | (0x00<<24));
	wordp[1] = htole32((ch->unit * ATA_PDC_CHN_OFFSET)+ATA_PDC_HSG_OFFSET);
	wordp[2] = htole32((ch->unit * ATA_PDC_CHN_OFFSET)+ATA_PDC_LSG_OFFSET);
	wordp[3] = 0;

	wordp = (u_int32_t *)
	    (window + (ch->unit * ATA_PDC_CHN_OFFSET) + ATA_PDC_APKT_OFFSET);
	if (request->flags & ATA_R_READ)
	    wordp[0] = htole32(0x04 | ((ch->unit+5)<<16) | (0x00<<24));
	if (request->flags & ATA_R_WRITE)
	    wordp[0] = htole32(0x10 | ((ch->unit+1)<<16) | ((ch->unit+13)<<24));
	wordp[1] = htole32((ch->unit * ATA_PDC_CHN_OFFSET)+ATA_PDC_ASG_OFFSET);
	wordp[2] = 0;
	ata_promise_apkt((u_int8_t *)wordp, request);
	ATA_OUTL(ctlr->r_res2, 0x000c0484, 0x00000001);

	if (request->flags & ATA_R_READ) {
	    ATA_OUTL(ctlr->r_res2, 0x000c0400 + ((ch->unit+5)<<2), 0x00000001);
	    ATA_OUTL(ctlr->r_res2, 0x000c0400 + ((ch->unit+9)<<2), 0x00000001);
	    ATA_OUTL(ctlr->r_res2, 0x000c0240 + (ch->unit << 7),
		htole32((ch->unit * ATA_PDC_CHN_OFFSET) + ATA_PDC_APKT_OFFSET));
	}
	if (request->flags & ATA_R_WRITE) {
	    ATA_OUTL(ctlr->r_res2, 0x000c0400 + ((ch->unit+1)<<2), 0x00000001);
	    ATA_OUTL(ctlr->r_res2, 0x000c0400 + ((ch->unit+13)<<2), 0x00000001);
	    ata_promise_queue_hpkt(ctlr,
		htole32((ch->unit * ATA_PDC_CHN_OFFSET) + ATA_PDC_HPKT_OFFSET));
	}
	return 0;
    }
}

static int
ata_promise_apkt(u_int8_t *bytep, struct ata_request *request)
{ 
    int i = 12;

    bytep[i++] = ATA_PDC_1B | ATA_PDC_WRITE_REG | ATA_PDC_WAIT_NBUSY|ATA_DRIVE;
    bytep[i++] = ATA_D_IBM | ATA_D_LBA | ATA_DEV(request->unit);
    bytep[i++] = ATA_PDC_1B | ATA_PDC_WRITE_CTL;
    bytep[i++] = ATA_A_4BIT;

    if (request->flags & ATA_R_48BIT) {
	bytep[i++] = ATA_PDC_2B | ATA_PDC_WRITE_REG | ATA_FEATURE;
	bytep[i++] = request->u.ata.feature >> 8;
	bytep[i++] = request->u.ata.feature;
	bytep[i++] = ATA_PDC_2B | ATA_PDC_WRITE_REG | ATA_COUNT;
	bytep[i++] = request->u.ata.count >> 8;
	bytep[i++] = request->u.ata.count;
	bytep[i++] = ATA_PDC_2B | ATA_PDC_WRITE_REG | ATA_SECTOR;
	bytep[i++] = request->u.ata.lba >> 24;
	bytep[i++] = request->u.ata.lba;
	bytep[i++] = ATA_PDC_2B | ATA_PDC_WRITE_REG | ATA_CYL_LSB;
	bytep[i++] = request->u.ata.lba >> 32;
	bytep[i++] = request->u.ata.lba >> 8;
	bytep[i++] = ATA_PDC_2B | ATA_PDC_WRITE_REG | ATA_CYL_MSB;
	bytep[i++] = request->u.ata.lba >> 40;
	bytep[i++] = request->u.ata.lba >> 16;
	bytep[i++] = ATA_PDC_1B | ATA_PDC_WRITE_REG | ATA_DRIVE;
	bytep[i++] = ATA_D_LBA | ATA_DEV(request->unit);
    }
    else {
	bytep[i++] = ATA_PDC_1B | ATA_PDC_WRITE_REG | ATA_FEATURE;
	bytep[i++] = request->u.ata.feature;
	bytep[i++] = ATA_PDC_1B | ATA_PDC_WRITE_REG | ATA_COUNT;
	bytep[i++] = request->u.ata.count;
	bytep[i++] = ATA_PDC_1B | ATA_PDC_WRITE_REG | ATA_SECTOR;
	bytep[i++] = request->u.ata.lba;
	bytep[i++] = ATA_PDC_1B | ATA_PDC_WRITE_REG | ATA_CYL_LSB;
	bytep[i++] = request->u.ata.lba >> 8;
	bytep[i++] = ATA_PDC_1B | ATA_PDC_WRITE_REG | ATA_CYL_MSB;
	bytep[i++] = request->u.ata.lba >> 16;
	bytep[i++] = ATA_PDC_1B | ATA_PDC_WRITE_REG | ATA_DRIVE;
	bytep[i++] = ATA_D_LBA | ATA_D_IBM | ATA_DEV(request->unit) |
		     ((request->u.ata.lba >> 24)&0xf);
    }
    bytep[i++] = ATA_PDC_1B | ATA_PDC_WRITE_END | ATA_COMMAND;
    bytep[i++] = request->u.ata.command;
    return i;
}

static void
ata_promise_queue_hpkt(struct ata_pci_controller *ctlr, u_int32_t hpkt)
{
    struct ata_promise_sx4 *hpktp = ctlr->chipset_data;

    mtx_lock(&hpktp->mtx);
    if (hpktp->busy) {
	struct host_packet *hp = 
	    malloc(sizeof(struct host_packet), M_TEMP, M_NOWAIT | M_ZERO);
	hp->addr = hpkt;
	TAILQ_INSERT_TAIL(&hpktp->queue, hp, chain);
    }
    else {
	hpktp->busy = 1;
	ATA_OUTL(ctlr->r_res2, 0x000c0100, hpkt);
    }
    mtx_unlock(&hpktp->mtx);
}

static void
ata_promise_next_hpkt(struct ata_pci_controller *ctlr)
{
    struct ata_promise_sx4 *hpktp = ctlr->chipset_data;
    struct host_packet *hp;

    mtx_lock(&hpktp->mtx);
    if ((hp = TAILQ_FIRST(&hpktp->queue))) {
	TAILQ_REMOVE(&hpktp->queue, hp, chain);
	ATA_OUTL(ctlr->r_res2, 0x000c0100, hp->addr);
	free(hp, M_TEMP);
    }
    else
	hpktp->busy = 0;
    mtx_unlock(&hpktp->mtx);
}

ATA_DECLARE_DRIVER(ata_promise);
