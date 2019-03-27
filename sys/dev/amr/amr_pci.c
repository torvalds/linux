/*-
 * Copyright (c) 1999,2000 Michael Smith
 * Copyright (c) 2000 BSDi
 * All rights reserved.
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
/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2002 Eric Moore
 * Copyright (c) 2002, 2004 LSI Logic Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The party using or redistributing the source code and binary forms
 *    agrees to the disclaimer below and the terms and conditions set forth
 *    herein.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/sysctl.h>

#include <sys/bio.h>
#include <sys/bus.h>
#include <sys/conf.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/amr/amrio.h>
#include <dev/amr/amrreg.h>
#include <dev/amr/amrvar.h>

static int		amr_pci_probe(device_t dev);
static int		amr_pci_attach(device_t dev);
static int              amr_pci_detach(device_t dev);
static int              amr_pci_shutdown(device_t dev);
static int              amr_pci_suspend(device_t dev);
static int              amr_pci_resume(device_t dev);
static void		amr_pci_intr(void *arg);
static void		amr_pci_free(struct amr_softc *sc);
static void		amr_sglist_helper(void *arg, bus_dma_segment_t *segs, int nseg, int error);
static int		amr_sglist_map(struct amr_softc *sc);
static int		amr_setup_mbox(struct amr_softc *sc);
static int		amr_ccb_map(struct amr_softc *sc);

static u_int amr_force_sg32 = 0;
SYSCTL_DECL(_hw_amr);
SYSCTL_UINT(_hw_amr, OID_AUTO, force_sg32, CTLFLAG_RDTUN, &amr_force_sg32, 0,
    "Force the AMR driver to use 32bit scatter gather");

static device_method_t amr_methods[] = {
    /* Device interface */
    DEVMETHOD(device_probe,	amr_pci_probe),
    DEVMETHOD(device_attach,	amr_pci_attach),
    DEVMETHOD(device_detach,	amr_pci_detach),
    DEVMETHOD(device_shutdown,	amr_pci_shutdown),
    DEVMETHOD(device_suspend,	amr_pci_suspend),
    DEVMETHOD(device_resume,	amr_pci_resume),

    DEVMETHOD_END
};

static driver_t amr_pci_driver = {
	"amr",
	amr_methods,
	sizeof(struct amr_softc)
};

static struct amr_ident
{
    uint16_t		vendor;
    uint16_t		device;
    int		flags;
#define AMR_ID_PROBE_SIG	(1<<0)	/* generic i960RD, check signature */
#define AMR_ID_DO_SG64		(1<<1)
#define AMR_ID_QUARTZ		(1<<2)
} amr_device_ids[] = {
    {0x101e, 0x9010, 0},
    {0x101e, 0x9060, 0},
    {0x8086, 0x1960, AMR_ID_QUARTZ | AMR_ID_PROBE_SIG},
    {0x101e, 0x1960, AMR_ID_QUARTZ},
    {0x1000, 0x1960, AMR_ID_QUARTZ | AMR_ID_DO_SG64 | AMR_ID_PROBE_SIG},
    {0x1000, 0x0407, AMR_ID_QUARTZ | AMR_ID_DO_SG64},
    {0x1000, 0x0408, AMR_ID_QUARTZ | AMR_ID_DO_SG64},
    {0x1000, 0x0409, AMR_ID_QUARTZ | AMR_ID_DO_SG64},
    {0x1028, 0x000e, AMR_ID_QUARTZ | AMR_ID_DO_SG64 | AMR_ID_PROBE_SIG}, /* perc4/di i960 */
    {0x1028, 0x000f, AMR_ID_QUARTZ | AMR_ID_DO_SG64}, /* perc4/di Verde*/
    {0x1028, 0x0013, AMR_ID_QUARTZ | AMR_ID_DO_SG64}, /* perc4/di */
    {0, 0, 0}
};

static devclass_t	amr_devclass;
DRIVER_MODULE(amr, pci, amr_pci_driver, amr_devclass, 0, 0);
MODULE_PNP_INFO("U16:vendor;U16:device", pci, amr, amr_device_ids,
    nitems(amr_device_ids) - 1);
MODULE_DEPEND(amr, pci, 1, 1, 1);
MODULE_DEPEND(amr, cam, 1, 1, 1);

static struct amr_ident *
amr_find_ident(device_t dev)
{
    struct amr_ident *id;
    int sig;

    for (id = amr_device_ids; id->vendor != 0; id++) {
	if ((pci_get_vendor(dev) == id->vendor) &&
	    (pci_get_device(dev) == id->device)) {

	    /* do we need to test for a signature? */
	    if (id->flags & AMR_ID_PROBE_SIG) {
		sig = pci_read_config(dev, AMR_CFG_SIG, 2);
		if ((sig != AMR_SIGNATURE_1) && (sig != AMR_SIGNATURE_2))
		    continue;
	    }
	    return (id);
	}
    }
    return (NULL);
}
	
static int
amr_pci_probe(device_t dev)
{

    debug_called(1);

    if (amr_find_ident(dev) != NULL) {
	device_set_desc(dev, LSI_DESC_PCI);
	return(BUS_PROBE_DEFAULT);
    }
    return(ENXIO);
}

static int
amr_pci_attach(device_t dev)
{
    struct amr_softc	*sc;
    struct amr_ident	*id;
    int			rid, rtype, error;

    debug_called(1);

    /*
     * Initialise softc.
     */
    sc = device_get_softc(dev);
    bzero(sc, sizeof(*sc));
    sc->amr_dev = dev;

    /* assume failure is 'not configured' */
    error = ENXIO;

    /*
     * Determine board type.
     */
    if ((id = amr_find_ident(dev)) == NULL)
	return (ENXIO);

    if (id->flags & AMR_ID_QUARTZ) {
	sc->amr_type |= AMR_TYPE_QUARTZ;
    }

    if ((amr_force_sg32 == 0) && (id->flags & AMR_ID_DO_SG64) &&
	(sizeof(vm_paddr_t) > 4)) {
	device_printf(dev, "Using 64-bit DMA\n");
	sc->amr_type |= AMR_TYPE_SG64;
    }

    /* force the busmaster enable bit on */
    pci_enable_busmaster(dev);

    /*
     * Allocate the PCI register window.
     */
    rid = PCIR_BAR(0);
    rtype = AMR_IS_QUARTZ(sc) ? SYS_RES_MEMORY : SYS_RES_IOPORT;
    sc->amr_reg = bus_alloc_resource_any(dev, rtype, &rid, RF_ACTIVE);
    if (sc->amr_reg == NULL) {
	device_printf(sc->amr_dev, "can't allocate register window\n");
	goto out;
    }
    sc->amr_btag = rman_get_bustag(sc->amr_reg);
    sc->amr_bhandle = rman_get_bushandle(sc->amr_reg);

    /*
     * Allocate and connect our interrupt.
     */
    rid = 0;
    sc->amr_irq = bus_alloc_resource_any(sc->amr_dev, SYS_RES_IRQ, &rid,
        RF_SHAREABLE | RF_ACTIVE);
    if (sc->amr_irq == NULL) {
        device_printf(sc->amr_dev, "can't allocate interrupt\n");
	goto out;
    }
    if (bus_setup_intr(sc->amr_dev, sc->amr_irq,
	INTR_TYPE_BIO | INTR_ENTROPY | INTR_MPSAFE, NULL, amr_pci_intr,
	sc, &sc->amr_intr)) {
        device_printf(sc->amr_dev, "can't set up interrupt\n");
	goto out;
    }

    debug(2, "interrupt attached");

    /* assume failure is 'out of memory' */
    error = ENOMEM;

    /*
     * Allocate the parent bus DMA tag appropriate for PCI.
     */
    if (bus_dma_tag_create(bus_get_dma_tag(dev),	/* PCI parent */
			   1, 0, 			/* alignment,boundary */
			   AMR_IS_SG64(sc) ?
			   BUS_SPACE_MAXADDR :
			   BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
			   BUS_SPACE_MAXADDR, 		/* highaddr */
			   NULL, NULL, 			/* filter, filterarg */
			   BUS_SPACE_MAXSIZE,		/* maxsize */
			   BUS_SPACE_UNRESTRICTED,	/* nsegments */
			   BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
			   0,				/* flags */
			   NULL, NULL,			/* lockfunc, lockarg */
			   &sc->amr_parent_dmat)) {
	device_printf(dev, "can't allocate parent DMA tag\n");
	goto out;
    }

    /*
     * Create DMA tag for mapping buffers into controller-addressable space.
     */
    if (bus_dma_tag_create(sc->amr_parent_dmat,		/* parent */
			   1, 0,			/* alignment,boundary */
			   BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
			   BUS_SPACE_MAXADDR,		/* highaddr */
			   NULL, NULL,			/* filter, filterarg */
			   DFLTPHYS,			/* maxsize */
			   AMR_NSEG,			/* nsegments */
			   BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
			   0,		/* flags */
			   busdma_lock_mutex,		/* lockfunc */
			   &sc->amr_list_lock,		/* lockarg */
			   &sc->amr_buffer_dmat)) {
        device_printf(sc->amr_dev, "can't allocate buffer DMA tag\n");
	goto out;
    }

    if (bus_dma_tag_create(sc->amr_parent_dmat,		/* parent */
			   1, 0,			/* alignment,boundary */
			   BUS_SPACE_MAXADDR,		/* lowaddr */
			   BUS_SPACE_MAXADDR,		/* highaddr */
			   NULL, NULL,			/* filter, filterarg */
			   DFLTPHYS,			/* maxsize */
			   AMR_NSEG,			/* nsegments */
			   BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
			   0,		/* flags */
			   busdma_lock_mutex,		/* lockfunc */
			   &sc->amr_list_lock,		/* lockarg */
			   &sc->amr_buffer64_dmat)) {
        device_printf(sc->amr_dev, "can't allocate buffer DMA tag\n");
	goto out;
    }

    debug(2, "dma tag done");

    /*
     * Allocate and set up mailbox in a bus-visible fashion.
     */
    mtx_init(&sc->amr_list_lock, "AMR List Lock", NULL, MTX_DEF);
    mtx_init(&sc->amr_hw_lock, "AMR HW Lock", NULL, MTX_DEF);
    if ((error = amr_setup_mbox(sc)) != 0)
	goto out;

    debug(2, "mailbox setup");

    /*
     * Build the scatter/gather buffers.
     */
    if ((error = amr_sglist_map(sc)) != 0)
	goto out;
    debug(2, "s/g list mapped");

    if ((error = amr_ccb_map(sc)) != 0)
	goto out;
    debug(2, "ccb mapped");


    /*
     * Do bus-independant initialisation, bring controller online.
     */
    error = amr_attach(sc);

out:
    if (error)
	amr_pci_free(sc);
    return(error);
}

/********************************************************************************
 * Disconnect from the controller completely, in preparation for unload.
 */
static int
amr_pci_detach(device_t dev)
{
    struct amr_softc	*sc = device_get_softc(dev);
    int			error;

    debug_called(1);

    if (sc->amr_state & AMR_STATE_OPEN)
	return(EBUSY);

    if ((error = amr_pci_shutdown(dev)))
	return(error);

    amr_pci_free(sc);

    return(0);
}

/********************************************************************************
 * Bring the controller down to a dormant state and detach all child devices.
 *
 * This function is called before detach, system shutdown, or before performing
 * an operation which may add or delete system disks.  (Call amr_startup to
 * resume normal operation.)
 *
 * Note that we can assume that the bioq on the controller is empty, as we won't
 * allow shutdown if any device is open.
 */
static int
amr_pci_shutdown(device_t dev)
{
    struct amr_softc	*sc = device_get_softc(dev);
    int			i,error;

    debug_called(1);

    /* mark ourselves as in-shutdown */
    sc->amr_state |= AMR_STATE_SHUTDOWN;


    /* flush controller */
    device_printf(sc->amr_dev, "flushing cache...");
    printf("%s\n", amr_flush(sc) ? "failed" : "done");

    error = 0;

    /* delete all our child devices */
    for(i = 0 ; i < AMR_MAXLD; i++) {
	if( sc->amr_drive[i].al_disk != 0) {
	    if((error = device_delete_child(sc->amr_dev,sc->amr_drive[i].al_disk)) != 0)
		goto shutdown_out;
	    sc->amr_drive[i].al_disk = 0;
	}
    }

    /* XXX disable interrupts? */

shutdown_out:
    return(error);
}

/********************************************************************************
 * Bring the controller to a quiescent state, ready for system suspend.
 */
static int
amr_pci_suspend(device_t dev)
{
    struct amr_softc	*sc = device_get_softc(dev);

    debug_called(1);

    sc->amr_state |= AMR_STATE_SUSPEND;

    /* flush controller */
    device_printf(sc->amr_dev, "flushing cache...");
    printf("%s\n", amr_flush(sc) ? "failed" : "done");
    
    /* XXX disable interrupts? */

    return(0);
}

/********************************************************************************
 * Bring the controller back to a state ready for operation.
 */
static int
amr_pci_resume(device_t dev)
{
    struct amr_softc	*sc = device_get_softc(dev);

    debug_called(1);

    sc->amr_state &= ~AMR_STATE_SUSPEND;

    /* XXX enable interrupts? */

    return(0);
}

/*******************************************************************************
 * Take an interrupt, or be poked by other code to look for interrupt-worthy
 * status.
 */
static void
amr_pci_intr(void *arg)
{
    struct amr_softc	*sc = (struct amr_softc *)arg;

    debug_called(3);

    /* collect finished commands, queue anything waiting */
    amr_done(sc);
}

/********************************************************************************
 * Free all of the resources associated with (sc)
 *
 * Should not be called if the controller is active.
 */
static void
amr_pci_free(struct amr_softc *sc)
{
    void *p;

    debug_called(1);

    amr_free(sc);

    /* destroy data-transfer DMA tag */
    if (sc->amr_buffer_dmat)
	bus_dma_tag_destroy(sc->amr_buffer_dmat);
    if (sc->amr_buffer64_dmat)
	bus_dma_tag_destroy(sc->amr_buffer64_dmat);

    /* free and destroy DMA memory and tag for passthrough pool */
    if (sc->amr_ccb) {
	bus_dmamap_unload(sc->amr_ccb_dmat, sc->amr_ccb_dmamap);
	bus_dmamem_free(sc->amr_ccb_dmat, sc->amr_ccb, sc->amr_ccb_dmamap);
    }
    if (sc->amr_ccb_dmat)
	bus_dma_tag_destroy(sc->amr_ccb_dmat);

    /* free and destroy DMA memory and tag for s/g lists */
    if (sc->amr_sgtable) {
	bus_dmamap_unload(sc->amr_sg_dmat, sc->amr_sg_dmamap);
	bus_dmamem_free(sc->amr_sg_dmat, sc->amr_sgtable, sc->amr_sg_dmamap);
    }
    if (sc->amr_sg_dmat)
	bus_dma_tag_destroy(sc->amr_sg_dmat);

    /* free and destroy DMA memory and tag for mailbox */
    p = (void *)(uintptr_t)(volatile void *)sc->amr_mailbox64;
    if (sc->amr_mailbox) {
	bus_dmamap_unload(sc->amr_mailbox_dmat, sc->amr_mailbox_dmamap);
	bus_dmamem_free(sc->amr_mailbox_dmat, p, sc->amr_mailbox_dmamap);
    }
    if (sc->amr_mailbox_dmat)
	bus_dma_tag_destroy(sc->amr_mailbox_dmat);

    /* disconnect the interrupt handler */
    if (sc->amr_intr)
	bus_teardown_intr(sc->amr_dev, sc->amr_irq, sc->amr_intr);
    if (sc->amr_irq != NULL)
	bus_release_resource(sc->amr_dev, SYS_RES_IRQ, 0, sc->amr_irq);

    /* destroy the parent DMA tag */
    if (sc->amr_parent_dmat)
	bus_dma_tag_destroy(sc->amr_parent_dmat);

    /* release the register window mapping */
    if (sc->amr_reg != NULL)
	bus_release_resource(sc->amr_dev,
			     AMR_IS_QUARTZ(sc) ? SYS_RES_MEMORY : SYS_RES_IOPORT,
			     PCIR_BAR(0), sc->amr_reg);
}

/********************************************************************************
 * Allocate and map the scatter/gather table in bus space.
 */
static void
amr_sglist_helper(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
    uint32_t *addr;

    debug_called(1);

    addr = arg;
    *addr = segs[0].ds_addr;
}

static int
amr_sglist_map(struct amr_softc *sc)
{
    size_t	segsize;
    void	*p;
    int		error;

    debug_called(1);

    /*
     * Create a single tag describing a region large enough to hold all of
     * the s/g lists we will need.
     *
     * Note that we could probably use AMR_LIMITCMD here, but that may become
     * tunable.
     */
    if (AMR_IS_SG64(sc))
	segsize = sizeof(struct amr_sg64entry) * AMR_NSEG * AMR_MAXCMD;
    else
	segsize = sizeof(struct amr_sgentry) * AMR_NSEG * AMR_MAXCMD;

    error = bus_dma_tag_create(sc->amr_parent_dmat, 	/* parent */
			       512, 0, 			/* alignment,boundary */
			       BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
			       BUS_SPACE_MAXADDR, 	/* highaddr */
			       NULL, NULL, 		/* filter, filterarg */
			       segsize, 1,		/* maxsize, nsegments */
			       BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
			       0,			/* flags */
			       NULL, NULL,		/* lockfunc, lockarg */
			       &sc->amr_sg_dmat);
    if (error != 0) {
	device_printf(sc->amr_dev, "can't allocate scatter/gather DMA tag\n");
	return(ENOMEM);
    }

    /*
     * Allocate enough s/g maps for all commands and permanently map them into
     * controller-visible space.
     *	
     * XXX this assumes we can get enough space for all the s/g maps in one 
     * contiguous slab.  We may need to switch to a more complex arrangement
     * where we allocate in smaller chunks and keep a lookup table from slot
     * to bus address.
     *
     * XXX HACK ALERT:	at least some controllers don't like the s/g memory
     *			being allocated below 0x2000.  We leak some memory if
     *			we get some below this mark and allocate again.  We
     *			should be able to avoid this with the tag setup, but
     *			that does't seem to work.
     */
retry:
    error = bus_dmamem_alloc(sc->amr_sg_dmat, (void **)&p, BUS_DMA_NOWAIT, &sc->amr_sg_dmamap);
    if (error) {
	device_printf(sc->amr_dev, "can't allocate s/g table\n");
	return(ENOMEM);
    }
    bus_dmamap_load(sc->amr_sg_dmat, sc->amr_sg_dmamap, p, segsize, amr_sglist_helper, &sc->amr_sgbusaddr, 0);
    if (sc->amr_sgbusaddr < 0x2000) {
	debug(1, "s/g table too low (0x%x), reallocating\n", sc->amr_sgbusaddr);
	goto retry;
    }

    if (AMR_IS_SG64(sc))
	sc->amr_sg64table = (struct amr_sg64entry *)p;
    sc->amr_sgtable = (struct amr_sgentry *)p;

    return(0);
}

/********************************************************************************
 * Allocate and set up mailbox areas for the controller (sc)
 *
 * The basic mailbox structure should be 16-byte aligned.
 */
static int
amr_setup_mbox(struct amr_softc *sc)
{
    int		error;
    void	*p;
    uint32_t	baddr;
    
    debug_called(1);

    /*
     * Create a single tag describing a region large enough to hold the entire
     * mailbox.
     */
    error = bus_dma_tag_create(sc->amr_parent_dmat,	/* parent */
			       16, 0,			/* alignment,boundary */
			       BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
			       BUS_SPACE_MAXADDR,	/* highaddr */
			       NULL, NULL,		/* filter, filterarg */
			       sizeof(struct amr_mailbox64), /* maxsize */
			       1,			/* nsegments */
			       BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
			       0,			/* flags */
			       NULL, NULL,		/* lockfunc, lockarg */
			       &sc->amr_mailbox_dmat);
    if (error != 0) {
	device_printf(sc->amr_dev, "can't allocate mailbox tag\n");
	return(ENOMEM);
    }

    /*
     * Allocate the mailbox structure and permanently map it into
     * controller-visible space.
     */
    error = bus_dmamem_alloc(sc->amr_mailbox_dmat, (void **)&p, BUS_DMA_NOWAIT,
			     &sc->amr_mailbox_dmamap);
    if (error) {
	device_printf(sc->amr_dev, "can't allocate mailbox memory\n");
	return(ENOMEM);
    }
    bus_dmamap_load(sc->amr_mailbox_dmat, sc->amr_mailbox_dmamap, p,
		    sizeof(struct amr_mailbox64), amr_sglist_helper, &baddr, 0);
    /*
     * Conventional mailbox is inside the mailbox64 region.
     */
    /* save physical base of the basic mailbox structure */
    sc->amr_mailboxphys = baddr + offsetof(struct amr_mailbox64, mb);
    bzero(p, sizeof(struct amr_mailbox64));
    sc->amr_mailbox64 = (struct amr_mailbox64 *)p;
    sc->amr_mailbox = &sc->amr_mailbox64->mb;

    return(0);
}

static int
amr_ccb_map(struct amr_softc *sc)
{
    int		ccbsize, error;

    /*
     * Passthrough and Extended passthrough structures will share the same
     * memory.
     */
    ccbsize = sizeof(union amr_ccb) * AMR_MAXCMD;
    error = bus_dma_tag_create(sc->amr_parent_dmat,	/* parent */
				128, 0,			/* alignment,boundary */
				BUS_SPACE_MAXADDR_32BIT,/* lowaddr */
				BUS_SPACE_MAXADDR,	/* highaddr */
				NULL, NULL,		/* filter, filterarg */
				ccbsize,		/* maxsize */
				1,			/* nsegments */
				ccbsize,		/* maxsegsize */
				0,			/* flags */
				NULL, NULL,		/* lockfunc, lockarg */
				&sc->amr_ccb_dmat);
    if (error != 0) {
	device_printf(sc->amr_dev, "can't allocate ccb tag\n");
	return (ENOMEM);
    }

    error = bus_dmamem_alloc(sc->amr_ccb_dmat, (void **)&sc->amr_ccb,
			     BUS_DMA_NOWAIT, &sc->amr_ccb_dmamap);
    if (error) {
	device_printf(sc->amr_dev, "can't allocate ccb memory\n");
	return (ENOMEM);
    }
    bus_dmamap_load(sc->amr_ccb_dmat, sc->amr_ccb_dmamap, sc->amr_ccb,
		    ccbsize, amr_sglist_helper, &sc->amr_ccb_busaddr, 0);
    bzero(sc->amr_ccb, ccbsize);

    return (0);
}

