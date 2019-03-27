/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000, 2001 Michael Smith
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
 *
 *	$FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/ctype.h>
#include <sys/ioccom.h>
#include <sys/stat.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_periph.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/mly/mlyreg.h>
#include <dev/mly/mlyio.h>
#include <dev/mly/mlyvar.h>
#include <dev/mly/mly_tables.h>

static int	mly_probe(device_t dev);
static int	mly_attach(device_t dev);
static int	mly_pci_attach(struct mly_softc *sc);
static int	mly_detach(device_t dev);
static int	mly_shutdown(device_t dev);
static void	mly_intr(void *arg);

static int	mly_sg_map(struct mly_softc *sc);
static void	mly_sg_map_helper(void *arg, bus_dma_segment_t *segs, int nseg, int error);
static int	mly_mmbox_map(struct mly_softc *sc);
static void	mly_mmbox_map_helper(void *arg, bus_dma_segment_t *segs, int nseg, int error);
static void	mly_free(struct mly_softc *sc);

static int	mly_get_controllerinfo(struct mly_softc *sc);
static void	mly_scan_devices(struct mly_softc *sc);
static void	mly_rescan_btl(struct mly_softc *sc, int bus, int target);
static void	mly_complete_rescan(struct mly_command *mc);
static int	mly_get_eventstatus(struct mly_softc *sc);
static int	mly_enable_mmbox(struct mly_softc *sc);
static int	mly_flush(struct mly_softc *sc);
static int	mly_ioctl(struct mly_softc *sc, struct mly_command_ioctl *ioctl, void **data, 
			  size_t datasize, u_int8_t *status, void *sense_buffer, size_t *sense_length);
static void	mly_check_event(struct mly_softc *sc);
static void	mly_fetch_event(struct mly_softc *sc);
static void	mly_complete_event(struct mly_command *mc);
static void	mly_process_event(struct mly_softc *sc, struct mly_event *me);
static void	mly_periodic(void *data);

static int	mly_immediate_command(struct mly_command *mc);
static int	mly_start(struct mly_command *mc);
static void	mly_done(struct mly_softc *sc);
static void	mly_complete(struct mly_softc *sc);
static void	mly_complete_handler(void *context, int pending);

static int	mly_alloc_command(struct mly_softc *sc, struct mly_command **mcp);
static void	mly_release_command(struct mly_command *mc);
static void	mly_alloc_commands_map(void *arg, bus_dma_segment_t *segs, int nseg, int error);
static int	mly_alloc_commands(struct mly_softc *sc);
static void	mly_release_commands(struct mly_softc *sc);
static void	mly_map_command(struct mly_command *mc);
static void	mly_unmap_command(struct mly_command *mc);

static int	mly_cam_attach(struct mly_softc *sc);
static void	mly_cam_detach(struct mly_softc *sc);
static void	mly_cam_rescan_btl(struct mly_softc *sc, int bus, int target);
static void	mly_cam_action(struct cam_sim *sim, union ccb *ccb);
static int	mly_cam_action_io(struct cam_sim *sim, struct ccb_scsiio *csio);
static void	mly_cam_poll(struct cam_sim *sim);
static void	mly_cam_complete(struct mly_command *mc);
static struct cam_periph *mly_find_periph(struct mly_softc *sc, int bus, int target);
static int	mly_name_device(struct mly_softc *sc, int bus, int target);

static int	mly_fwhandshake(struct mly_softc *sc);

static void	mly_describe_controller(struct mly_softc *sc);
#ifdef MLY_DEBUG
static void	mly_printstate(struct mly_softc *sc);
static void	mly_print_command(struct mly_command *mc);
static void	mly_print_packet(struct mly_command *mc);
static void	mly_panic(struct mly_softc *sc, char *reason);
static void	mly_timeout(void *arg);
#endif
void		mly_print_controller(int controller);


static d_open_t		mly_user_open;
static d_close_t	mly_user_close;
static d_ioctl_t	mly_user_ioctl;
static int	mly_user_command(struct mly_softc *sc, struct mly_user_command *uc);
static int	mly_user_health(struct mly_softc *sc, struct mly_user_health *uh);

#define MLY_CMD_TIMEOUT		20

static device_method_t mly_methods[] = {
    /* Device interface */
    DEVMETHOD(device_probe,	mly_probe),
    DEVMETHOD(device_attach,	mly_attach),
    DEVMETHOD(device_detach,	mly_detach),
    DEVMETHOD(device_shutdown,	mly_shutdown),
    { 0, 0 }
};

static driver_t mly_pci_driver = {
	"mly",
	mly_methods,
	sizeof(struct mly_softc)
};

static devclass_t	mly_devclass;
DRIVER_MODULE(mly, pci, mly_pci_driver, mly_devclass, 0, 0);
MODULE_DEPEND(mly, pci, 1, 1, 1);
MODULE_DEPEND(mly, cam, 1, 1, 1);

static struct cdevsw mly_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	mly_user_open,
	.d_close =	mly_user_close,
	.d_ioctl =	mly_user_ioctl,
	.d_name =	"mly",
};

/********************************************************************************
 ********************************************************************************
                                                                 Device Interface
 ********************************************************************************
 ********************************************************************************/

static struct mly_ident
{
    u_int16_t		vendor;
    u_int16_t		device;
    u_int16_t		subvendor;
    u_int16_t		subdevice;
    int			hwif;
    char		*desc;
} mly_identifiers[] = {
    {0x1069, 0xba56, 0x1069, 0x0040, MLY_HWIF_STRONGARM, "Mylex eXtremeRAID 2000"},
    {0x1069, 0xba56, 0x1069, 0x0030, MLY_HWIF_STRONGARM, "Mylex eXtremeRAID 3000"},
    {0x1069, 0x0050, 0x1069, 0x0050, MLY_HWIF_I960RX,    "Mylex AcceleRAID 352"},
    {0x1069, 0x0050, 0x1069, 0x0052, MLY_HWIF_I960RX,    "Mylex AcceleRAID 170"},
    {0x1069, 0x0050, 0x1069, 0x0054, MLY_HWIF_I960RX,    "Mylex AcceleRAID 160"},
    {0, 0, 0, 0, 0, 0}
};

/********************************************************************************
 * Compare the provided PCI device with the list we support.
 */
static int
mly_probe(device_t dev)
{
    struct mly_ident	*m;

    debug_called(1);

    for (m = mly_identifiers; m->vendor != 0; m++) {
	if ((m->vendor == pci_get_vendor(dev)) &&
	    (m->device == pci_get_device(dev)) &&
	    ((m->subvendor == 0) || ((m->subvendor == pci_get_subvendor(dev)) &&
				     (m->subdevice == pci_get_subdevice(dev))))) {
	    
	    device_set_desc(dev, m->desc);
	    return(BUS_PROBE_DEFAULT);	/* allow room to be overridden */
	}
    }
    return(ENXIO);
}

/********************************************************************************
 * Initialise the controller and softc
 */
static int
mly_attach(device_t dev)
{
    struct mly_softc	*sc = device_get_softc(dev);
    int			error;

    debug_called(1);

    sc->mly_dev = dev;
    mtx_init(&sc->mly_lock, "mly", NULL, MTX_DEF);
    callout_init_mtx(&sc->mly_periodic, &sc->mly_lock, 0);

#ifdef MLY_DEBUG
    callout_init_mtx(&sc->mly_timeout, &sc->mly_lock, 0);
    if (device_get_unit(sc->mly_dev) == 0)
	mly_softc0 = sc;
#endif    

    /*
     * Do PCI-specific initialisation.
     */
    if ((error = mly_pci_attach(sc)) != 0)
	goto out;

    /*
     * Initialise per-controller queues.
     */
    mly_initq_free(sc);
    mly_initq_busy(sc);
    mly_initq_complete(sc);

    /*
     * Initialise command-completion task.
     */
    TASK_INIT(&sc->mly_task_complete, 0, mly_complete_handler, sc);

    /* disable interrupts before we start talking to the controller */
    MLY_MASK_INTERRUPTS(sc);

    /* 
     * Wait for the controller to come ready, handshake with the firmware if required.
     * This is typically only necessary on platforms where the controller BIOS does not
     * run.
     */
    if ((error = mly_fwhandshake(sc)))
	goto out;

    /*
     * Allocate initial command buffers.
     */
    if ((error = mly_alloc_commands(sc)))
	goto out;

    /* 
     * Obtain controller feature information
     */
    MLY_LOCK(sc);
    error = mly_get_controllerinfo(sc);
    MLY_UNLOCK(sc);
    if (error)
	goto out;

    /*
     * Reallocate command buffers now we know how many we want.
     */
    mly_release_commands(sc);
    if ((error = mly_alloc_commands(sc)))
	goto out;

    /*
     * Get the current event counter for health purposes, populate the initial
     * health status buffer.
     */
    MLY_LOCK(sc);
    error = mly_get_eventstatus(sc);

    /*
     * Enable memory-mailbox mode.
     */
    if (error == 0)
	error = mly_enable_mmbox(sc);
    MLY_UNLOCK(sc);
    if (error)
	goto out;

    /*
     * Attach to CAM.
     */
    if ((error = mly_cam_attach(sc)))
	goto out;

    /* 
     * Print a little information about the controller 
     */
    mly_describe_controller(sc);

    /*
     * Mark all attached devices for rescan.
     */
    MLY_LOCK(sc);
    mly_scan_devices(sc);

    /*
     * Instigate the first status poll immediately.  Rescan completions won't
     * happen until interrupts are enabled, which should still be before
     * the SCSI subsystem gets to us, courtesy of the "SCSI settling delay".
     */
    mly_periodic((void *)sc);
    MLY_UNLOCK(sc);

    /*
     * Create the control device.
     */
    sc->mly_dev_t = make_dev(&mly_cdevsw, 0, UID_ROOT, GID_OPERATOR,
			     S_IRUSR | S_IWUSR, "mly%d", device_get_unit(sc->mly_dev));
    sc->mly_dev_t->si_drv1 = sc;

    /* enable interrupts now */
    MLY_UNMASK_INTERRUPTS(sc);

#ifdef MLY_DEBUG
    callout_reset(&sc->mly_timeout, MLY_CMD_TIMEOUT * hz, mly_timeout, sc);
#endif

 out:
    if (error != 0)
	mly_free(sc);
    return(error);
}

/********************************************************************************
 * Perform PCI-specific initialisation.
 */
static int
mly_pci_attach(struct mly_softc *sc)
{
    int			i, error;

    debug_called(1);

    /* assume failure is 'not configured' */
    error = ENXIO;

    /* 
     * Verify that the adapter is correctly set up in PCI space.
     */
    pci_enable_busmaster(sc->mly_dev);

    /*
     * Allocate the PCI register window.
     */
    sc->mly_regs_rid = PCIR_BAR(0);	/* first base address register */
    if ((sc->mly_regs_resource = bus_alloc_resource_any(sc->mly_dev, 
	    SYS_RES_MEMORY, &sc->mly_regs_rid, RF_ACTIVE)) == NULL) {
	mly_printf(sc, "can't allocate register window\n");
	goto fail;
    }

    /* 
     * Allocate and connect our interrupt.
     */
    sc->mly_irq_rid = 0;
    if ((sc->mly_irq = bus_alloc_resource_any(sc->mly_dev, SYS_RES_IRQ, 
		    &sc->mly_irq_rid, RF_SHAREABLE | RF_ACTIVE)) == NULL) {
	mly_printf(sc, "can't allocate interrupt\n");
	goto fail;
    }
    if (bus_setup_intr(sc->mly_dev, sc->mly_irq, INTR_TYPE_CAM | INTR_ENTROPY | INTR_MPSAFE, NULL, mly_intr, sc, &sc->mly_intr)) {
	mly_printf(sc, "can't set up interrupt\n");
	goto fail;
    }

    /* assume failure is 'out of memory' */
    error = ENOMEM;

    /*
     * Allocate the parent bus DMA tag appropriate for our PCI interface.
     * 
     * Note that all of these controllers are 64-bit capable.
     */
    if (bus_dma_tag_create(bus_get_dma_tag(sc->mly_dev),/* PCI parent */
			   1, 0, 			/* alignment, boundary */
			   BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
			   BUS_SPACE_MAXADDR, 		/* highaddr */
			   NULL, NULL, 			/* filter, filterarg */
			   BUS_SPACE_MAXSIZE_32BIT,	/* maxsize */
			   BUS_SPACE_UNRESTRICTED,	/* nsegments */
			   BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
			   BUS_DMA_ALLOCNOW,		/* flags */
			   NULL,			/* lockfunc */
			   NULL,			/* lockarg */
			   &sc->mly_parent_dmat)) {
	mly_printf(sc, "can't allocate parent DMA tag\n");
	goto fail;
    }

    /*
     * Create DMA tag for mapping buffers into controller-addressable space.
     */
    if (bus_dma_tag_create(sc->mly_parent_dmat, 	/* parent */
			   1, 0, 			/* alignment, boundary */
			   BUS_SPACE_MAXADDR,		/* lowaddr */
			   BUS_SPACE_MAXADDR, 		/* highaddr */
			   NULL, NULL, 			/* filter, filterarg */
			   DFLTPHYS,			/* maxsize */
			   MLY_MAX_SGENTRIES,		/* nsegments */
			   BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
			   0,				/* flags */
			   busdma_lock_mutex,		/* lockfunc */
			   &sc->mly_lock,		/* lockarg */
			   &sc->mly_buffer_dmat)) {
	mly_printf(sc, "can't allocate buffer DMA tag\n");
	goto fail;
    }

    /*
     * Initialise the DMA tag for command packets.
     */
    if (bus_dma_tag_create(sc->mly_parent_dmat,		/* parent */
			   1, 0, 			/* alignment, boundary */
			   BUS_SPACE_MAXADDR,		/* lowaddr */
			   BUS_SPACE_MAXADDR, 		/* highaddr */
			   NULL, NULL, 			/* filter, filterarg */
			   sizeof(union mly_command_packet) * MLY_MAX_COMMANDS, 1,	/* maxsize, nsegments */
			   BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
			   BUS_DMA_ALLOCNOW,		/* flags */
			   NULL, NULL,			/* lockfunc, lockarg */
			   &sc->mly_packet_dmat)) {
	mly_printf(sc, "can't allocate command packet DMA tag\n");
	goto fail;
    }

    /* 
     * Detect the hardware interface version 
     */
    for (i = 0; mly_identifiers[i].vendor != 0; i++) {
	if ((mly_identifiers[i].vendor == pci_get_vendor(sc->mly_dev)) &&
	    (mly_identifiers[i].device == pci_get_device(sc->mly_dev))) {
	    sc->mly_hwif = mly_identifiers[i].hwif;
	    switch(sc->mly_hwif) {
	    case MLY_HWIF_I960RX:
		debug(1, "set hardware up for i960RX");
		sc->mly_doorbell_true = 0x00;
		sc->mly_command_mailbox =  MLY_I960RX_COMMAND_MAILBOX;
		sc->mly_status_mailbox =   MLY_I960RX_STATUS_MAILBOX;
		sc->mly_idbr =             MLY_I960RX_IDBR;
		sc->mly_odbr =             MLY_I960RX_ODBR;
		sc->mly_error_status =     MLY_I960RX_ERROR_STATUS;
		sc->mly_interrupt_status = MLY_I960RX_INTERRUPT_STATUS;
		sc->mly_interrupt_mask =   MLY_I960RX_INTERRUPT_MASK;
		break;
	    case MLY_HWIF_STRONGARM:
		debug(1, "set hardware up for StrongARM");
		sc->mly_doorbell_true = 0xff;		/* doorbell 'true' is 0 */
		sc->mly_command_mailbox =  MLY_STRONGARM_COMMAND_MAILBOX;
		sc->mly_status_mailbox =   MLY_STRONGARM_STATUS_MAILBOX;
		sc->mly_idbr =             MLY_STRONGARM_IDBR;
		sc->mly_odbr =             MLY_STRONGARM_ODBR;
		sc->mly_error_status =     MLY_STRONGARM_ERROR_STATUS;
		sc->mly_interrupt_status = MLY_STRONGARM_INTERRUPT_STATUS;
		sc->mly_interrupt_mask =   MLY_STRONGARM_INTERRUPT_MASK;
		break;
	    }
	    break;
	}
    }

    /*
     * Create the scatter/gather mappings.
     */
    if ((error = mly_sg_map(sc)))
	goto fail;

    /*
     * Allocate and map the memory mailbox
     */
    if ((error = mly_mmbox_map(sc)))
	goto fail;

    error = 0;
	    
fail:
    return(error);
}

/********************************************************************************
 * Shut the controller down and detach all our resources.
 */
static int
mly_detach(device_t dev)
{
    int			error;

    if ((error = mly_shutdown(dev)) != 0)
	return(error);
    
    mly_free(device_get_softc(dev));
    return(0);
}

/********************************************************************************
 * Bring the controller to a state where it can be safely left alone.
 *
 * Note that it should not be necessary to wait for any outstanding commands,
 * as they should be completed prior to calling here.
 *
 * XXX this applies for I/O, but not status polls; we should beware of
 *     the case where a status command is running while we detach.
 */
static int
mly_shutdown(device_t dev)
{
    struct mly_softc	*sc = device_get_softc(dev);

    debug_called(1);

    MLY_LOCK(sc);
    if (sc->mly_state & MLY_STATE_OPEN) {
	MLY_UNLOCK(sc);
	return(EBUSY);
    }

    /* kill the periodic event */
    callout_stop(&sc->mly_periodic);
#ifdef MLY_DEBUG
    callout_stop(&sc->mly_timeout);
#endif

    /* flush controller */
    mly_printf(sc, "flushing cache...");
    printf("%s\n", mly_flush(sc) ? "failed" : "done");

    MLY_MASK_INTERRUPTS(sc);
    MLY_UNLOCK(sc);

    return(0);
}

/*******************************************************************************
 * Take an interrupt, or be poked by other code to look for interrupt-worthy
 * status.
 */
static void
mly_intr(void *arg)
{
    struct mly_softc	*sc = (struct mly_softc *)arg;

    debug_called(2);

    MLY_LOCK(sc);
    mly_done(sc);
    MLY_UNLOCK(sc);
};

/********************************************************************************
 ********************************************************************************
                                                Bus-dependant Resource Management
 ********************************************************************************
 ********************************************************************************/

/********************************************************************************
 * Allocate memory for the scatter/gather tables
 */
static int
mly_sg_map(struct mly_softc *sc)
{
    size_t	segsize;

    debug_called(1);

    /*
     * Create a single tag describing a region large enough to hold all of
     * the s/g lists we will need.
     */
    segsize = sizeof(struct mly_sg_entry) * MLY_MAX_COMMANDS *MLY_MAX_SGENTRIES;
    if (bus_dma_tag_create(sc->mly_parent_dmat,		/* parent */
			   1, 0, 			/* alignment,boundary */
			   BUS_SPACE_MAXADDR,		/* lowaddr */
			   BUS_SPACE_MAXADDR, 		/* highaddr */
			   NULL, NULL, 			/* filter, filterarg */
			   segsize, 1,			/* maxsize, nsegments */
			   BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
			   BUS_DMA_ALLOCNOW,		/* flags */
			   NULL, NULL,			/* lockfunc, lockarg */
			   &sc->mly_sg_dmat)) {
	mly_printf(sc, "can't allocate scatter/gather DMA tag\n");
	return(ENOMEM);
    }

    /*
     * Allocate enough s/g maps for all commands and permanently map them into
     * controller-visible space.
     *	
     * XXX this assumes we can get enough space for all the s/g maps in one 
     * contiguous slab.
     */
    if (bus_dmamem_alloc(sc->mly_sg_dmat, (void **)&sc->mly_sg_table,
			 BUS_DMA_NOWAIT, &sc->mly_sg_dmamap)) {
	mly_printf(sc, "can't allocate s/g table\n");
	return(ENOMEM);
    }
    if (bus_dmamap_load(sc->mly_sg_dmat, sc->mly_sg_dmamap, sc->mly_sg_table,
			segsize, mly_sg_map_helper, sc, BUS_DMA_NOWAIT) != 0)
	return (ENOMEM);
    return(0);
}

/********************************************************************************
 * Save the physical address of the base of the s/g table.
 */
static void
mly_sg_map_helper(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
    struct mly_softc	*sc = (struct mly_softc *)arg;

    debug_called(1);

    /* save base of s/g table's address in bus space */
    sc->mly_sg_busaddr = segs->ds_addr;
}

/********************************************************************************
 * Allocate memory for the memory-mailbox interface
 */
static int
mly_mmbox_map(struct mly_softc *sc)
{

    /*
     * Create a DMA tag for a single contiguous region large enough for the
     * memory mailbox structure.
     */
    if (bus_dma_tag_create(sc->mly_parent_dmat,		/* parent */
			   1, 0, 			/* alignment,boundary */
			   BUS_SPACE_MAXADDR,		/* lowaddr */
			   BUS_SPACE_MAXADDR, 		/* highaddr */
			   NULL, NULL, 			/* filter, filterarg */
			   sizeof(struct mly_mmbox), 1,	/* maxsize, nsegments */
			   BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
			   BUS_DMA_ALLOCNOW,		/* flags */
			   NULL, NULL,			/* lockfunc, lockarg */
			   &sc->mly_mmbox_dmat)) {
	mly_printf(sc, "can't allocate memory mailbox DMA tag\n");
	return(ENOMEM);
    }

    /*
     * Allocate the buffer
     */
    if (bus_dmamem_alloc(sc->mly_mmbox_dmat, (void **)&sc->mly_mmbox, BUS_DMA_NOWAIT, &sc->mly_mmbox_dmamap)) {
	mly_printf(sc, "can't allocate memory mailbox\n");
	return(ENOMEM);
    }
    if (bus_dmamap_load(sc->mly_mmbox_dmat, sc->mly_mmbox_dmamap, sc->mly_mmbox,
			sizeof(struct mly_mmbox), mly_mmbox_map_helper, sc, 
			BUS_DMA_NOWAIT) != 0)
	return (ENOMEM);
    bzero(sc->mly_mmbox, sizeof(*sc->mly_mmbox));
    return(0);

}

/********************************************************************************
 * Save the physical address of the memory mailbox 
 */
static void
mly_mmbox_map_helper(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
    struct mly_softc	*sc = (struct mly_softc *)arg;

    debug_called(1);

    sc->mly_mmbox_busaddr = segs->ds_addr;
}

/********************************************************************************
 * Free all of the resources associated with (sc)
 *
 * Should not be called if the controller is active.
 */
static void
mly_free(struct mly_softc *sc)
{
    
    debug_called(1);

    /* Remove the management device */
    destroy_dev(sc->mly_dev_t);

    if (sc->mly_intr)
	bus_teardown_intr(sc->mly_dev, sc->mly_irq, sc->mly_intr);
    callout_drain(&sc->mly_periodic);
#ifdef MLY_DEBUG
    callout_drain(&sc->mly_timeout);
#endif

    /* detach from CAM */
    mly_cam_detach(sc);

    /* release command memory */
    mly_release_commands(sc);
    
    /* throw away the controllerinfo structure */
    if (sc->mly_controllerinfo != NULL)
	free(sc->mly_controllerinfo, M_DEVBUF);

    /* throw away the controllerparam structure */
    if (sc->mly_controllerparam != NULL)
	free(sc->mly_controllerparam, M_DEVBUF);

    /* destroy data-transfer DMA tag */
    if (sc->mly_buffer_dmat)
	bus_dma_tag_destroy(sc->mly_buffer_dmat);

    /* free and destroy DMA memory and tag for s/g lists */
    if (sc->mly_sg_table) {
	bus_dmamap_unload(sc->mly_sg_dmat, sc->mly_sg_dmamap);
	bus_dmamem_free(sc->mly_sg_dmat, sc->mly_sg_table, sc->mly_sg_dmamap);
    }
    if (sc->mly_sg_dmat)
	bus_dma_tag_destroy(sc->mly_sg_dmat);

    /* free and destroy DMA memory and tag for memory mailbox */
    if (sc->mly_mmbox) {
	bus_dmamap_unload(sc->mly_mmbox_dmat, sc->mly_mmbox_dmamap);
	bus_dmamem_free(sc->mly_mmbox_dmat, sc->mly_mmbox, sc->mly_mmbox_dmamap);
    }
    if (sc->mly_mmbox_dmat)
	bus_dma_tag_destroy(sc->mly_mmbox_dmat);

    /* disconnect the interrupt handler */
    if (sc->mly_irq != NULL)
	bus_release_resource(sc->mly_dev, SYS_RES_IRQ, sc->mly_irq_rid, sc->mly_irq);

    /* destroy the parent DMA tag */
    if (sc->mly_parent_dmat)
	bus_dma_tag_destroy(sc->mly_parent_dmat);

    /* release the register window mapping */
    if (sc->mly_regs_resource != NULL)
	bus_release_resource(sc->mly_dev, SYS_RES_MEMORY, sc->mly_regs_rid, sc->mly_regs_resource);

    mtx_destroy(&sc->mly_lock);
}

/********************************************************************************
 ********************************************************************************
                                                                 Command Wrappers
 ********************************************************************************
 ********************************************************************************/

/********************************************************************************
 * Fill in the mly_controllerinfo and mly_controllerparam fields in the softc.
 */
static int
mly_get_controllerinfo(struct mly_softc *sc)
{
    struct mly_command_ioctl	mci;
    u_int8_t			status;
    int				error;

    debug_called(1);

    if (sc->mly_controllerinfo != NULL)
	free(sc->mly_controllerinfo, M_DEVBUF);

    /* build the getcontrollerinfo ioctl and send it */
    bzero(&mci, sizeof(mci));
    sc->mly_controllerinfo = NULL;
    mci.sub_ioctl = MDACIOCTL_GETCONTROLLERINFO;
    if ((error = mly_ioctl(sc, &mci, (void **)&sc->mly_controllerinfo, sizeof(*sc->mly_controllerinfo),
			   &status, NULL, NULL)))
	return(error);
    if (status != 0)
	return(EIO);

    if (sc->mly_controllerparam != NULL)
	free(sc->mly_controllerparam, M_DEVBUF);

    /* build the getcontrollerparameter ioctl and send it */
    bzero(&mci, sizeof(mci));
    sc->mly_controllerparam = NULL;
    mci.sub_ioctl = MDACIOCTL_GETCONTROLLERPARAMETER;
    if ((error = mly_ioctl(sc, &mci, (void **)&sc->mly_controllerparam, sizeof(*sc->mly_controllerparam),
			   &status, NULL, NULL)))
	return(error);
    if (status != 0)
	return(EIO);

    return(0);
}

/********************************************************************************
 * Schedule all possible devices for a rescan.
 *
 */
static void
mly_scan_devices(struct mly_softc *sc)
{
    int		bus, target;

    debug_called(1);

    /*
     * Clear any previous BTL information.
     */
    bzero(&sc->mly_btl, sizeof(sc->mly_btl));

    /*
     * Mark all devices as requiring a rescan, and let the next
     * periodic scan collect them. 
     */
    for (bus = 0; bus < sc->mly_cam_channels; bus++)
	if (MLY_BUS_IS_VALID(sc, bus)) 
	    for (target = 0; target < MLY_MAX_TARGETS; target++)
		sc->mly_btl[bus][target].mb_flags = MLY_BTL_RESCAN;

}

/********************************************************************************
 * Rescan a device, possibly as a consequence of getting an event which suggests
 * that it may have changed.
 *
 * If we suffer resource starvation, we can abandon the rescan as we'll be
 * retried.
 */
static void
mly_rescan_btl(struct mly_softc *sc, int bus, int target)
{
    struct mly_command		*mc;
    struct mly_command_ioctl	*mci;

    debug_called(1);

    /* check that this bus is valid */
    if (!MLY_BUS_IS_VALID(sc, bus))
	return;

    /* get a command */
    if (mly_alloc_command(sc, &mc))
	return;

    /* set up the data buffer */
    if ((mc->mc_data = malloc(sizeof(union mly_devinfo), M_DEVBUF, M_NOWAIT | M_ZERO)) == NULL) {
	mly_release_command(mc);
	return;
    }
    mc->mc_flags |= MLY_CMD_DATAIN;
    mc->mc_complete = mly_complete_rescan;

    /* 
     * Build the ioctl.
     */
    mci = (struct mly_command_ioctl *)&mc->mc_packet->ioctl;
    mci->opcode = MDACMD_IOCTL;
    mci->addr.phys.controller = 0;
    mci->timeout.value = 30;
    mci->timeout.scale = MLY_TIMEOUT_SECONDS;
    if (MLY_BUS_IS_VIRTUAL(sc, bus)) {
	mc->mc_length = mci->data_size = sizeof(struct mly_ioctl_getlogdevinfovalid);
	mci->sub_ioctl = MDACIOCTL_GETLOGDEVINFOVALID;
	mci->addr.log.logdev = MLY_LOGDEV_ID(sc, bus, target);
	debug(1, "logical device %d", mci->addr.log.logdev);
    } else {
	mc->mc_length = mci->data_size = sizeof(struct mly_ioctl_getphysdevinfovalid);
	mci->sub_ioctl = MDACIOCTL_GETPHYSDEVINFOVALID;
	mci->addr.phys.lun = 0;
	mci->addr.phys.target = target;
	mci->addr.phys.channel = bus;
	debug(1, "physical device %d:%d", mci->addr.phys.channel, mci->addr.phys.target);
    }
    
    /*
     * Dispatch the command.  If we successfully send the command, clear the rescan
     * bit.
     */
    if (mly_start(mc) != 0) {
	mly_release_command(mc);
    } else {
	sc->mly_btl[bus][target].mb_flags &= ~MLY_BTL_RESCAN;	/* success */	
    }
}

/********************************************************************************
 * Handle the completion of a rescan operation
 */
static void
mly_complete_rescan(struct mly_command *mc)
{
    struct mly_softc				*sc = mc->mc_sc;
    struct mly_ioctl_getlogdevinfovalid		*ldi;
    struct mly_ioctl_getphysdevinfovalid	*pdi;
    struct mly_command_ioctl			*mci;
    struct mly_btl				btl, *btlp;
    int						bus, target, rescan;

    debug_called(1);

    /*
     * Recover the bus and target from the command.  We need these even in
     * the case where we don't have a useful response.
     */
    mci = (struct mly_command_ioctl *)&mc->mc_packet->ioctl;
    if (mci->sub_ioctl == MDACIOCTL_GETLOGDEVINFOVALID) {
	bus = MLY_LOGDEV_BUS(sc, mci->addr.log.logdev);
	target = MLY_LOGDEV_TARGET(sc, mci->addr.log.logdev);
    } else {
	bus = mci->addr.phys.channel;
	target = mci->addr.phys.target;
    }
    /* XXX validate bus/target? */
    
    /* the default result is 'no device' */
    bzero(&btl, sizeof(btl));

    /* if the rescan completed OK, we have possibly-new BTL data */
    if (mc->mc_status == 0) {
	if (mc->mc_length == sizeof(*ldi)) {
	    ldi = (struct mly_ioctl_getlogdevinfovalid *)mc->mc_data;
	    if ((MLY_LOGDEV_BUS(sc, ldi->logical_device_number) != bus) ||
		(MLY_LOGDEV_TARGET(sc, ldi->logical_device_number) != target)) {
		mly_printf(sc, "WARNING: BTL rescan for %d:%d returned data for %d:%d instead\n",
			   bus, target, MLY_LOGDEV_BUS(sc, ldi->logical_device_number),
			   MLY_LOGDEV_TARGET(sc, ldi->logical_device_number));
		/* XXX what can we do about this? */
	    }
	    btl.mb_flags = MLY_BTL_LOGICAL;
	    btl.mb_type = ldi->raid_level;
	    btl.mb_state = ldi->state;
	    debug(1, "BTL rescan for %d returns %s, %s", ldi->logical_device_number, 
		  mly_describe_code(mly_table_device_type, ldi->raid_level),
		  mly_describe_code(mly_table_device_state, ldi->state));
	} else if (mc->mc_length == sizeof(*pdi)) {
	    pdi = (struct mly_ioctl_getphysdevinfovalid *)mc->mc_data;
	    if ((pdi->channel != bus) || (pdi->target != target)) {
		mly_printf(sc, "WARNING: BTL rescan for %d:%d returned data for %d:%d instead\n",
			   bus, target, pdi->channel, pdi->target);
		/* XXX what can we do about this? */
	    }
	    btl.mb_flags = MLY_BTL_PHYSICAL;
	    btl.mb_type = MLY_DEVICE_TYPE_PHYSICAL;
	    btl.mb_state = pdi->state;
	    btl.mb_speed = pdi->speed;
	    btl.mb_width = pdi->width;
	    if (pdi->state != MLY_DEVICE_STATE_UNCONFIGURED)
		sc->mly_btl[bus][target].mb_flags |= MLY_BTL_PROTECTED;
	    debug(1, "BTL rescan for %d:%d returns %s", bus, target, 
		  mly_describe_code(mly_table_device_state, pdi->state));
	} else {
	    mly_printf(sc, "BTL rescan result invalid\n");
	}
    }

    free(mc->mc_data, M_DEVBUF);
    mly_release_command(mc);

    /*
     * Decide whether we need to rescan the device.
     */
    rescan = 0;

    /* device type changes (usually between 'nothing' and 'something') */
    btlp = &sc->mly_btl[bus][target];
    if (btl.mb_flags != btlp->mb_flags) {
	debug(1, "flags changed, rescanning");
	rescan = 1;
    }
    
    /* XXX other reasons? */

    /*
     * Update BTL information.
     */
    *btlp = btl;

    /*
     * Perform CAM rescan if required.
     */
    if (rescan)
	mly_cam_rescan_btl(sc, bus, target);
}

/********************************************************************************
 * Get the current health status and set the 'next event' counter to suit.
 */
static int
mly_get_eventstatus(struct mly_softc *sc)
{
    struct mly_command_ioctl	mci;
    struct mly_health_status	*mh;
    u_int8_t			status;
    int				error;

    /* build the gethealthstatus ioctl and send it */
    bzero(&mci, sizeof(mci));
    mh = NULL;
    mci.sub_ioctl = MDACIOCTL_GETHEALTHSTATUS;

    if ((error = mly_ioctl(sc, &mci, (void **)&mh, sizeof(*mh), &status, NULL, NULL)))
	return(error);
    if (status != 0)
	return(EIO);

    /* get the event counter */
    sc->mly_event_change = mh->change_counter;
    sc->mly_event_waiting = mh->next_event;
    sc->mly_event_counter = mh->next_event;

    /* save the health status into the memory mailbox */
    bcopy(mh, &sc->mly_mmbox->mmm_health.status, sizeof(*mh));

    debug(1, "initial change counter %d, event counter %d", mh->change_counter, mh->next_event);
    
    free(mh, M_DEVBUF);
    return(0);
}

/********************************************************************************
 * Enable the memory mailbox mode.
 */
static int
mly_enable_mmbox(struct mly_softc *sc)
{
    struct mly_command_ioctl	mci;
    u_int8_t			*sp, status;
    int				error;

    debug_called(1);

    /* build the ioctl and send it */
    bzero(&mci, sizeof(mci));
    mci.sub_ioctl = MDACIOCTL_SETMEMORYMAILBOX;
    /* set buffer addresses */
    mci.param.setmemorymailbox.command_mailbox_physaddr = 
	sc->mly_mmbox_busaddr + offsetof(struct mly_mmbox, mmm_command);
    mci.param.setmemorymailbox.status_mailbox_physaddr = 
	sc->mly_mmbox_busaddr + offsetof(struct mly_mmbox, mmm_status);
    mci.param.setmemorymailbox.health_buffer_physaddr = 
	sc->mly_mmbox_busaddr + offsetof(struct mly_mmbox, mmm_health);

    /* set buffer sizes - abuse of data_size field is revolting */
    sp = (u_int8_t *)&mci.data_size;
    sp[0] = ((sizeof(union mly_command_packet) * MLY_MMBOX_COMMANDS) / 1024);
    sp[1] = (sizeof(union mly_status_packet) * MLY_MMBOX_STATUS) / 1024;
    mci.param.setmemorymailbox.health_buffer_size = sizeof(union mly_health_region) / 1024;

    debug(1, "memory mailbox at %p (0x%llx/%d 0x%llx/%d 0x%llx/%d", sc->mly_mmbox,
	  mci.param.setmemorymailbox.command_mailbox_physaddr, sp[0],
	  mci.param.setmemorymailbox.status_mailbox_physaddr, sp[1],
	  mci.param.setmemorymailbox.health_buffer_physaddr, 
	  mci.param.setmemorymailbox.health_buffer_size);

    if ((error = mly_ioctl(sc, &mci, NULL, 0, &status, NULL, NULL)))
	return(error);
    if (status != 0)
	return(EIO);
    sc->mly_state |= MLY_STATE_MMBOX_ACTIVE;
    debug(1, "memory mailbox active");
    return(0);
}

/********************************************************************************
 * Flush all pending I/O from the controller.
 */
static int
mly_flush(struct mly_softc *sc)
{
    struct mly_command_ioctl	mci;
    u_int8_t			status;
    int				error;

    debug_called(1);

    /* build the ioctl */
    bzero(&mci, sizeof(mci));
    mci.sub_ioctl = MDACIOCTL_FLUSHDEVICEDATA;
    mci.param.deviceoperation.operation_device = MLY_OPDEVICE_PHYSICAL_CONTROLLER;

    /* pass it off to the controller */
    if ((error = mly_ioctl(sc, &mci, NULL, 0, &status, NULL, NULL)))
	return(error);

    return((status == 0) ? 0 : EIO);
}

/********************************************************************************
 * Perform an ioctl command.
 *
 * If (data) is not NULL, the command requires data transfer.  If (*data) is NULL
 * the command requires data transfer from the controller, and we will allocate
 * a buffer for it.  If (*data) is not NULL, the command requires data transfer
 * to the controller.
 *
 * XXX passing in the whole ioctl structure is ugly.  Better ideas?
 *
 * XXX we don't even try to handle the case where datasize > 4k.  We should.
 */
static int
mly_ioctl(struct mly_softc *sc, struct mly_command_ioctl *ioctl, void **data, size_t datasize, 
	  u_int8_t *status, void *sense_buffer, size_t *sense_length)
{
    struct mly_command		*mc;
    struct mly_command_ioctl	*mci;
    int				error;

    debug_called(1);
    MLY_ASSERT_LOCKED(sc);

    mc = NULL;
    if (mly_alloc_command(sc, &mc)) {
	error = ENOMEM;
	goto out;
    }

    /* copy the ioctl structure, but save some important fields and then fixup */
    mci = &mc->mc_packet->ioctl;
    ioctl->sense_buffer_address = mci->sense_buffer_address;
    ioctl->maximum_sense_size = mci->maximum_sense_size;
    *mci = *ioctl;
    mci->opcode = MDACMD_IOCTL;
    mci->timeout.value = 30;
    mci->timeout.scale = MLY_TIMEOUT_SECONDS;
    
    /* handle the data buffer */
    if (data != NULL) {
	if (*data == NULL) {
	    /* allocate data buffer */
	    if ((mc->mc_data = malloc(datasize, M_DEVBUF, M_NOWAIT)) == NULL) {
		error = ENOMEM;
		goto out;
	    }
	    mc->mc_flags |= MLY_CMD_DATAIN;
	} else {
	    mc->mc_data = *data;
	    mc->mc_flags |= MLY_CMD_DATAOUT;
	}
	mc->mc_length = datasize;
	mc->mc_packet->generic.data_size = datasize;
    }
    
    /* run the command */
    if ((error = mly_immediate_command(mc)))
	goto out;
    
    /* clean up and return any data */
    *status = mc->mc_status;
    if ((mc->mc_sense > 0) && (sense_buffer != NULL)) {
	bcopy(mc->mc_packet, sense_buffer, mc->mc_sense);
	*sense_length = mc->mc_sense;
	goto out;
    }

    /* should we return a data pointer? */
    if ((data != NULL) && (*data == NULL))
	*data = mc->mc_data;

    /* command completed OK */
    error = 0;

out:
    if (mc != NULL) {
	/* do we need to free a data buffer we allocated? */
	if (error && (mc->mc_data != NULL) && (*data == NULL))
	    free(mc->mc_data, M_DEVBUF);
	mly_release_command(mc);
    }
    return(error);
}

/********************************************************************************
 * Check for event(s) outstanding in the controller.
 */
static void
mly_check_event(struct mly_softc *sc)
{
    
    /*
     * The controller may have updated the health status information,
     * so check for it here.  Note that the counters are all in host memory,
     * so this check is very cheap.  Also note that we depend on checking on
     * completion 
     */
    if (sc->mly_mmbox->mmm_health.status.change_counter != sc->mly_event_change) {
	sc->mly_event_change = sc->mly_mmbox->mmm_health.status.change_counter;
	debug(1, "event change %d, event status update, %d -> %d", sc->mly_event_change,
	      sc->mly_event_waiting, sc->mly_mmbox->mmm_health.status.next_event);
	sc->mly_event_waiting = sc->mly_mmbox->mmm_health.status.next_event;

	/* wake up anyone that might be interested in this */
	wakeup(&sc->mly_event_change);
    }
    if (sc->mly_event_counter != sc->mly_event_waiting)
    mly_fetch_event(sc);
}

/********************************************************************************
 * Fetch one event from the controller.
 *
 * If we fail due to resource starvation, we'll be retried the next time a 
 * command completes.
 */
static void
mly_fetch_event(struct mly_softc *sc)
{
    struct mly_command		*mc;
    struct mly_command_ioctl	*mci;
    u_int32_t			event;

    debug_called(1);

    /* get a command */
    if (mly_alloc_command(sc, &mc))
	return;

    /* set up the data buffer */
    if ((mc->mc_data = malloc(sizeof(struct mly_event), M_DEVBUF, M_NOWAIT | M_ZERO)) == NULL) {
	mly_release_command(mc);
	return;
    }
    mc->mc_length = sizeof(struct mly_event);
    mc->mc_flags |= MLY_CMD_DATAIN;
    mc->mc_complete = mly_complete_event;

    /*
     * Get an event number to fetch.  It's possible that we've raced with another
     * context for the last event, in which case there will be no more events.
     */
    if (sc->mly_event_counter == sc->mly_event_waiting) {
	mly_release_command(mc);
	return;
    }
    event = sc->mly_event_counter++;

    /* 
     * Build the ioctl.
     *
     * At this point we are committed to sending this request, as it
     * will be the only one constructed for this particular event number.
     */
    mci = (struct mly_command_ioctl *)&mc->mc_packet->ioctl;
    mci->opcode = MDACMD_IOCTL;
    mci->data_size = sizeof(struct mly_event);
    mci->addr.phys.lun = (event >> 16) & 0xff;
    mci->addr.phys.target = (event >> 24) & 0xff;
    mci->addr.phys.channel = 0;
    mci->addr.phys.controller = 0;
    mci->timeout.value = 30;
    mci->timeout.scale = MLY_TIMEOUT_SECONDS;
    mci->sub_ioctl = MDACIOCTL_GETEVENT;
    mci->param.getevent.sequence_number_low = event & 0xffff;

    debug(1, "fetch event %u", event);

    /*
     * Submit the command.
     *
     * Note that failure of mly_start() will result in this event never being
     * fetched.
     */
    if (mly_start(mc) != 0) {
	mly_printf(sc, "couldn't fetch event %u\n", event);
	mly_release_command(mc);
    }
}

/********************************************************************************
 * Handle the completion of an event poll.
 */
static void
mly_complete_event(struct mly_command *mc)
{
    struct mly_softc	*sc = mc->mc_sc;
    struct mly_event	*me = (struct mly_event *)mc->mc_data;

    debug_called(1);

    /* 
     * If the event was successfully fetched, process it.
     */
    if (mc->mc_status == SCSI_STATUS_OK) {
	mly_process_event(sc, me);
	free(me, M_DEVBUF);
    }
    mly_release_command(mc);

    /*
     * Check for another event.
     */
    mly_check_event(sc);
}

/********************************************************************************
 * Process a controller event.
 */
static void
mly_process_event(struct mly_softc *sc, struct mly_event *me)
{
    struct scsi_sense_data_fixed *ssd;
    char			 *fp, *tp;
    int				 bus, target, event, class, action;

    ssd = (struct scsi_sense_data_fixed *)&me->sense[0];

    /* 
     * Errors can be reported using vendor-unique sense data.  In this case, the
     * event code will be 0x1c (Request sense data present), the sense key will
     * be 0x09 (vendor specific), the MSB of the ASC will be set, and the 
     * actual event code will be a 16-bit value comprised of the ASCQ (low byte)
     * and low seven bits of the ASC (low seven bits of the high byte).
     */
    if ((me->code == 0x1c) && 
	((ssd->flags & SSD_KEY) == SSD_KEY_Vendor_Specific) &&
	(ssd->add_sense_code & 0x80)) {
	event = ((int)(ssd->add_sense_code & ~0x80) << 8) + ssd->add_sense_code_qual;
    } else {
	event = me->code;
    }

    /* look up event, get codes */
    fp = mly_describe_code(mly_table_event, event);

    debug(1, "Event %d  code 0x%x", me->sequence_number, me->code);

    /* quiet event? */
    class = fp[0];
    if (isupper(class) && bootverbose)
	class = tolower(class);

    /* get action code, text string */
    action = fp[1];
    tp = &fp[2];

    /*
     * Print some information about the event.
     *
     * This code uses a table derived from the corresponding portion of the Linux
     * driver, and thus the parser is very similar.
     */
    switch(class) {
    case 'p':		/* error on physical device */
	mly_printf(sc, "physical device %d:%d %s\n", me->channel, me->target, tp);
	if (action == 'r')
	    sc->mly_btl[me->channel][me->target].mb_flags |= MLY_BTL_RESCAN;
	break;
    case 'l':		/* error on logical unit */
    case 'm':		/* message about logical unit */
	bus = MLY_LOGDEV_BUS(sc, me->lun);
	target = MLY_LOGDEV_TARGET(sc, me->lun);
	mly_name_device(sc, bus, target);
	mly_printf(sc, "logical device %d (%s) %s\n", me->lun, sc->mly_btl[bus][target].mb_name, tp);
	if (action == 'r')
	    sc->mly_btl[bus][target].mb_flags |= MLY_BTL_RESCAN;
	break;
    case 's':		/* report of sense data */
	if (((ssd->flags & SSD_KEY) == SSD_KEY_NO_SENSE) ||
	    (((ssd->flags & SSD_KEY) == SSD_KEY_NOT_READY) && 
	     (ssd->add_sense_code == 0x04) && 
	     ((ssd->add_sense_code_qual == 0x01) || (ssd->add_sense_code_qual == 0x02))))
	    break;	/* ignore NO_SENSE or NOT_READY in one case */

	mly_printf(sc, "physical device %d:%d %s\n", me->channel, me->target, tp);
	mly_printf(sc, "  sense key %d  asc %02x  ascq %02x\n", 
		      ssd->flags & SSD_KEY, ssd->add_sense_code, ssd->add_sense_code_qual);
	mly_printf(sc, "  info %4D  csi %4D\n", ssd->info, "", ssd->cmd_spec_info, "");
	if (action == 'r')
	    sc->mly_btl[me->channel][me->target].mb_flags |= MLY_BTL_RESCAN;
	break;
    case 'e':
	mly_printf(sc, tp, me->target, me->lun);
	printf("\n");
	break;
    case 'c':
	mly_printf(sc, "controller %s\n", tp);
	break;
    case '?':
	mly_printf(sc, "%s - %d\n", tp, me->code);
	break;
    default:	/* probably a 'noisy' event being ignored */
	break;
    }
}

/********************************************************************************
 * Perform periodic activities.
 */
static void
mly_periodic(void *data)
{
    struct mly_softc	*sc = (struct mly_softc *)data;
    int			bus, target;

    debug_called(2);
    MLY_ASSERT_LOCKED(sc);

    /*
     * Scan devices.
     */
    for (bus = 0; bus < sc->mly_cam_channels; bus++) {
	if (MLY_BUS_IS_VALID(sc, bus)) {
	    for (target = 0; target < MLY_MAX_TARGETS; target++) {

		/* ignore the controller in this scan */
		if (target == sc->mly_controllerparam->initiator_id)
		    continue;

		/* perform device rescan? */
		if (sc->mly_btl[bus][target].mb_flags & MLY_BTL_RESCAN)
		    mly_rescan_btl(sc, bus, target);
	    }
	}
    }
    
    /* check for controller events */
    mly_check_event(sc);

    /* reschedule ourselves */
    callout_schedule(&sc->mly_periodic, MLY_PERIODIC_INTERVAL * hz);
}

/********************************************************************************
 ********************************************************************************
                                                               Command Processing
 ********************************************************************************
 ********************************************************************************/

/********************************************************************************
 * Run a command and wait for it to complete.
 *
 */
static int
mly_immediate_command(struct mly_command *mc)
{
    struct mly_softc	*sc = mc->mc_sc;
    int			error;

    debug_called(1);

    MLY_ASSERT_LOCKED(sc);
    if ((error = mly_start(mc))) {
	return(error);
    }

    if (sc->mly_state & MLY_STATE_INTERRUPTS_ON) {
	/* sleep on the command */
	while(!(mc->mc_flags & MLY_CMD_COMPLETE)) {
	    mtx_sleep(mc, &sc->mly_lock, PRIBIO, "mlywait", 0);
	}
    } else {
	/* spin and collect status while we do */
	while(!(mc->mc_flags & MLY_CMD_COMPLETE)) {
	    mly_done(mc->mc_sc);
	}
    }
    return(0);
}

/********************************************************************************
 * Deliver a command to the controller.
 *
 * XXX it would be good to just queue commands that we can't submit immediately
 *     and send them later, but we probably want a wrapper for that so that
 *     we don't hang on a failed submission for an immediate command.
 */
static int
mly_start(struct mly_command *mc)
{
    struct mly_softc		*sc = mc->mc_sc;
    union mly_command_packet	*pkt;

    debug_called(2);
    MLY_ASSERT_LOCKED(sc);

    /* 
     * Set the command up for delivery to the controller. 
     */
    mly_map_command(mc);
    mc->mc_packet->generic.command_id = mc->mc_slot;

#ifdef MLY_DEBUG
    mc->mc_timestamp = time_second;
#endif

    /*
     * Do we have to use the hardware mailbox?
     */
    if (!(sc->mly_state & MLY_STATE_MMBOX_ACTIVE)) {
	/*
	 * Check to see if the controller is ready for us.
	 */
	if (MLY_IDBR_TRUE(sc, MLY_HM_CMDSENT)) {
	    return(EBUSY);
	}
	mc->mc_flags |= MLY_CMD_BUSY;
	
	/*
	 * It's ready, send the command.
	 */
	MLY_SET_MBOX(sc, sc->mly_command_mailbox, &mc->mc_packetphys);
	MLY_SET_REG(sc, sc->mly_idbr, MLY_HM_CMDSENT);

    } else {	/* use memory-mailbox mode */

	pkt = &sc->mly_mmbox->mmm_command[sc->mly_mmbox_command_index];

	/* check to see if the next index is free yet */
	if (pkt->mmbox.flag != 0) {
	    return(EBUSY);
	}
	mc->mc_flags |= MLY_CMD_BUSY;
	
	/* copy in new command */
	bcopy(mc->mc_packet->mmbox.data, pkt->mmbox.data, sizeof(pkt->mmbox.data));
	/* barrier to ensure completion of previous write before we write the flag */
	bus_barrier(sc->mly_regs_resource, 0, 0, BUS_SPACE_BARRIER_WRITE);
	/* copy flag last */
	pkt->mmbox.flag = mc->mc_packet->mmbox.flag;
	/* barrier to ensure completion of previous write before we notify the controller */
	bus_barrier(sc->mly_regs_resource, 0, 0, BUS_SPACE_BARRIER_WRITE);

	/* signal controller, update index */
	MLY_SET_REG(sc, sc->mly_idbr, MLY_AM_CMDSENT);
	sc->mly_mmbox_command_index = (sc->mly_mmbox_command_index + 1) % MLY_MMBOX_COMMANDS;
    }

    mly_enqueue_busy(mc);
    return(0);
}

/********************************************************************************
 * Pick up command status from the controller, schedule a completion event
 */
static void
mly_done(struct mly_softc *sc) 
{
    struct mly_command		*mc;
    union mly_status_packet	*sp;
    u_int16_t			slot;
    int				worked;

    MLY_ASSERT_LOCKED(sc);
    worked = 0;

    /* pick up hardware-mailbox commands */
    if (MLY_ODBR_TRUE(sc, MLY_HM_STSREADY)) {
	slot = MLY_GET_REG2(sc, sc->mly_status_mailbox);
	if (slot < MLY_SLOT_MAX) {
	    mc = &sc->mly_command[slot - MLY_SLOT_START];
	    mc->mc_status = MLY_GET_REG(sc, sc->mly_status_mailbox + 2);
	    mc->mc_sense = MLY_GET_REG(sc, sc->mly_status_mailbox + 3);
	    mc->mc_resid = MLY_GET_REG4(sc, sc->mly_status_mailbox + 4);
	    mly_remove_busy(mc);
	    mc->mc_flags &= ~MLY_CMD_BUSY;
	    mly_enqueue_complete(mc);
	    worked = 1;
	} else {
	    /* slot 0xffff may mean "extremely bogus command" */
	    mly_printf(sc, "got HM completion for illegal slot %u\n", slot);
	}
	/* unconditionally acknowledge status */
	MLY_SET_REG(sc, sc->mly_odbr, MLY_HM_STSREADY);
	MLY_SET_REG(sc, sc->mly_idbr, MLY_HM_STSACK);
    }

    /* pick up memory-mailbox commands */
    if (MLY_ODBR_TRUE(sc, MLY_AM_STSREADY)) {
	for (;;) {
	    sp = &sc->mly_mmbox->mmm_status[sc->mly_mmbox_status_index];

	    /* check for more status */
	    if (sp->mmbox.flag == 0)
		break;

	    /* get slot number */
	    slot = sp->status.command_id;
	    if (slot < MLY_SLOT_MAX) {
		mc = &sc->mly_command[slot - MLY_SLOT_START];
		mc->mc_status = sp->status.status;
		mc->mc_sense = sp->status.sense_length;
		mc->mc_resid = sp->status.residue;
		mly_remove_busy(mc);
		mc->mc_flags &= ~MLY_CMD_BUSY;
		mly_enqueue_complete(mc);
		worked = 1;
	    } else {
		/* slot 0xffff may mean "extremely bogus command" */
		mly_printf(sc, "got AM completion for illegal slot %u at %d\n", 
			   slot, sc->mly_mmbox_status_index);
	    }

	    /* clear and move to next index */
	    sp->mmbox.flag = 0;
	    sc->mly_mmbox_status_index = (sc->mly_mmbox_status_index + 1) % MLY_MMBOX_STATUS;
	}
	/* acknowledge that we have collected status value(s) */
	MLY_SET_REG(sc, sc->mly_odbr, MLY_AM_STSREADY);
    }

    if (worked) {
	if (sc->mly_state & MLY_STATE_INTERRUPTS_ON)
	    taskqueue_enqueue(taskqueue_thread, &sc->mly_task_complete);
	else
	    mly_complete(sc);
    }
}

/********************************************************************************
 * Process completed commands
 */
static void
mly_complete_handler(void *context, int pending)
{
    struct mly_softc	*sc = (struct mly_softc *)context;

    MLY_LOCK(sc);
    mly_complete(sc);
    MLY_UNLOCK(sc);
}

static void
mly_complete(struct mly_softc *sc)
{
    struct mly_command	*mc;
    void	        (* mc_complete)(struct mly_command *mc);

    debug_called(2);

    /* 
     * Spin pulling commands off the completed queue and processing them.
     */
    while ((mc = mly_dequeue_complete(sc)) != NULL) {

	/*
	 * Free controller resources, mark command complete.
	 *
	 * Note that as soon as we mark the command complete, it may be freed
	 * out from under us, so we need to save the mc_complete field in
	 * order to later avoid dereferencing mc.  (We would not expect to
	 * have a polling/sleeping consumer with mc_complete != NULL).
	 */
	mly_unmap_command(mc);
	mc_complete = mc->mc_complete;
	mc->mc_flags |= MLY_CMD_COMPLETE;

	/* 
	 * Call completion handler or wake up sleeping consumer.
	 */
	if (mc_complete != NULL) {
	    mc_complete(mc);
	} else {
	    wakeup(mc);
	}
    }
    
    /*
     * XXX if we are deferring commands due to controller-busy status, we should
     *     retry submitting them here.
     */
}

/********************************************************************************
 ********************************************************************************
                                                        Command Buffer Management
 ********************************************************************************
 ********************************************************************************/

/********************************************************************************
 * Allocate a command.
 */
static int
mly_alloc_command(struct mly_softc *sc, struct mly_command **mcp)
{
    struct mly_command	*mc;

    debug_called(3);

    if ((mc = mly_dequeue_free(sc)) == NULL)
	return(ENOMEM);

    *mcp = mc;
    return(0);
}

/********************************************************************************
 * Release a command back to the freelist.
 */
static void
mly_release_command(struct mly_command *mc)
{
    debug_called(3);

    /*
     * Fill in parts of the command that may cause confusion if
     * a consumer doesn't when we are later allocated.
     */
    mc->mc_data = NULL;
    mc->mc_flags = 0;
    mc->mc_complete = NULL;
    mc->mc_private = NULL;

    /*
     * By default, we set up to overwrite the command packet with
     * sense information.
     */
    mc->mc_packet->generic.sense_buffer_address = mc->mc_packetphys;
    mc->mc_packet->generic.maximum_sense_size = sizeof(union mly_command_packet);

    mly_enqueue_free(mc);
}

/********************************************************************************
 * Map helper for command allocation.
 */
static void
mly_alloc_commands_map(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
    struct mly_softc	*sc = (struct mly_softc *)arg;

    debug_called(1);

    sc->mly_packetphys = segs[0].ds_addr;
}

/********************************************************************************
 * Allocate and initialise command and packet structures.
 *
 * If the controller supports fewer than MLY_MAX_COMMANDS commands, limit our
 * allocation to that number.  If we don't yet know how many commands the
 * controller supports, allocate a very small set (suitable for initialisation
 * purposes only).
 */
static int
mly_alloc_commands(struct mly_softc *sc)
{
    struct mly_command		*mc;
    int				i, ncmd;
 
    if (sc->mly_controllerinfo == NULL) {
	ncmd = 4;
    } else {
	ncmd = min(MLY_MAX_COMMANDS, sc->mly_controllerinfo->maximum_parallel_commands);
    }

    /*
     * Allocate enough space for all the command packets in one chunk and
     * map them permanently into controller-visible space.
     */
    if (bus_dmamem_alloc(sc->mly_packet_dmat, (void **)&sc->mly_packet, 
			 BUS_DMA_NOWAIT, &sc->mly_packetmap)) {
	return(ENOMEM);
    }
    if (bus_dmamap_load(sc->mly_packet_dmat, sc->mly_packetmap, sc->mly_packet, 
			ncmd * sizeof(union mly_command_packet), 
			mly_alloc_commands_map, sc, BUS_DMA_NOWAIT) != 0)
	return (ENOMEM);

    for (i = 0; i < ncmd; i++) {
	mc = &sc->mly_command[i];
	bzero(mc, sizeof(*mc));
	mc->mc_sc = sc;
	mc->mc_slot = MLY_SLOT_START + i;
	mc->mc_packet = sc->mly_packet + i;
	mc->mc_packetphys = sc->mly_packetphys + (i * sizeof(union mly_command_packet));
	if (!bus_dmamap_create(sc->mly_buffer_dmat, 0, &mc->mc_datamap))
	    mly_release_command(mc);
    }
    return(0);
}

/********************************************************************************
 * Free all the storage held by commands.
 *
 * Must be called with all commands on the free list.
 */
static void
mly_release_commands(struct mly_softc *sc)
{
    struct mly_command	*mc;

    /* throw away command buffer DMA maps */
    while (mly_alloc_command(sc, &mc) == 0)
	bus_dmamap_destroy(sc->mly_buffer_dmat, mc->mc_datamap);

    /* release the packet storage */
    if (sc->mly_packet != NULL) {
	bus_dmamap_unload(sc->mly_packet_dmat, sc->mly_packetmap);
	bus_dmamem_free(sc->mly_packet_dmat, sc->mly_packet, sc->mly_packetmap);
	sc->mly_packet = NULL;
    }
}


/********************************************************************************
 * Command-mapping helper function - populate this command's s/g table
 * with the s/g entries for its data.
 */
static void
mly_map_command_sg(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
    struct mly_command		*mc = (struct mly_command *)arg;
    struct mly_softc		*sc = mc->mc_sc;
    struct mly_command_generic	*gen = &(mc->mc_packet->generic);
    struct mly_sg_entry		*sg;
    int				i, tabofs;

    debug_called(2);

    /* can we use the transfer structure directly? */
    if (nseg <= 2) {
	sg = &gen->transfer.direct.sg[0];
	gen->command_control.extended_sg_table = 0;
    } else {
	tabofs = ((mc->mc_slot - MLY_SLOT_START) * MLY_MAX_SGENTRIES);
	sg = sc->mly_sg_table + tabofs;
	gen->transfer.indirect.entries[0] = nseg;
	gen->transfer.indirect.table_physaddr[0] = sc->mly_sg_busaddr + (tabofs * sizeof(struct mly_sg_entry));
	gen->command_control.extended_sg_table = 1;
    }

    /* copy the s/g table */
    for (i = 0; i < nseg; i++) {
	sg[i].physaddr = segs[i].ds_addr;
	sg[i].length = segs[i].ds_len;
    }

}

#if 0
/********************************************************************************
 * Command-mapping helper function - save the cdb's physical address.
 *
 * We don't support 'large' SCSI commands at this time, so this is unused.
 */
static void
mly_map_command_cdb(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
    struct mly_command			*mc = (struct mly_command *)arg;

    debug_called(2);

    /* XXX can we safely assume that a CDB will never cross a page boundary? */
    if ((segs[0].ds_addr % PAGE_SIZE) > 
	((segs[0].ds_addr + mc->mc_packet->scsi_large.cdb_length) % PAGE_SIZE))
	panic("cdb crosses page boundary");

    /* fix up fields in the command packet */
    mc->mc_packet->scsi_large.cdb_physaddr = segs[0].ds_addr;
}
#endif

/********************************************************************************
 * Map a command into controller-visible space
 */
static void
mly_map_command(struct mly_command *mc)
{
    struct mly_softc	*sc = mc->mc_sc;

    debug_called(2);

    /* don't map more than once */
    if (mc->mc_flags & MLY_CMD_MAPPED)
	return;

    /* does the command have a data buffer? */
    if (mc->mc_data != NULL) {
	if (mc->mc_flags & MLY_CMD_CCB)
		bus_dmamap_load_ccb(sc->mly_buffer_dmat, mc->mc_datamap,
				mc->mc_data, mly_map_command_sg, mc, 0);
	else 
		bus_dmamap_load(sc->mly_buffer_dmat, mc->mc_datamap,
				mc->mc_data, mc->mc_length, 
				mly_map_command_sg, mc, 0);
	if (mc->mc_flags & MLY_CMD_DATAIN)
	    bus_dmamap_sync(sc->mly_buffer_dmat, mc->mc_datamap, BUS_DMASYNC_PREREAD);
	if (mc->mc_flags & MLY_CMD_DATAOUT)
	    bus_dmamap_sync(sc->mly_buffer_dmat, mc->mc_datamap, BUS_DMASYNC_PREWRITE);
    }
    mc->mc_flags |= MLY_CMD_MAPPED;
}

/********************************************************************************
 * Unmap a command from controller-visible space
 */
static void
mly_unmap_command(struct mly_command *mc)
{
    struct mly_softc	*sc = mc->mc_sc;

    debug_called(2);

    if (!(mc->mc_flags & MLY_CMD_MAPPED))
	return;

    /* does the command have a data buffer? */
    if (mc->mc_data != NULL) {
	if (mc->mc_flags & MLY_CMD_DATAIN)
	    bus_dmamap_sync(sc->mly_buffer_dmat, mc->mc_datamap, BUS_DMASYNC_POSTREAD);
	if (mc->mc_flags & MLY_CMD_DATAOUT)
	    bus_dmamap_sync(sc->mly_buffer_dmat, mc->mc_datamap, BUS_DMASYNC_POSTWRITE);

	bus_dmamap_unload(sc->mly_buffer_dmat, mc->mc_datamap);
    }
    mc->mc_flags &= ~MLY_CMD_MAPPED;
}


/********************************************************************************
 ********************************************************************************
                                                                    CAM interface
 ********************************************************************************
 ********************************************************************************/

/********************************************************************************
 * Attach the physical and virtual SCSI busses to CAM.
 *
 * Physical bus numbering starts from 0, virtual bus numbering from one greater
 * than the highest physical bus.  Physical busses are only registered if
 * the kernel environment variable "hw.mly.register_physical_channels" is set.
 *
 * When we refer to a "bus", we are referring to the bus number registered with
 * the SIM, whereas a "channel" is a channel number given to the adapter.  In order
 * to keep things simple, we map these 1:1, so "bus" and "channel" may be used
 * interchangeably.
 */
static int
mly_cam_attach(struct mly_softc *sc)
{
    struct cam_devq	*devq;
    int			chn, i;

    debug_called(1);

    /*
     * Allocate a devq for all our channels combined.
     */
    if ((devq = cam_simq_alloc(sc->mly_controllerinfo->maximum_parallel_commands)) == NULL) {
	mly_printf(sc, "can't allocate CAM SIM queue\n");
	return(ENOMEM);
    }

    /*
     * If physical channel registration has been requested, register these first.
     * Note that we enable tagged command queueing for physical channels.
     */
    if (testenv("hw.mly.register_physical_channels")) {
	chn = 0;
	for (i = 0; i < sc->mly_controllerinfo->physical_channels_present; i++, chn++) {

	    if ((sc->mly_cam_sim[chn] = cam_sim_alloc(mly_cam_action, mly_cam_poll, "mly", sc,
						      device_get_unit(sc->mly_dev),
						      &sc->mly_lock,
						      sc->mly_controllerinfo->maximum_parallel_commands,
						      1, devq)) == NULL) {
		return(ENOMEM);
	    }
	    MLY_LOCK(sc);
	    if (xpt_bus_register(sc->mly_cam_sim[chn], sc->mly_dev, chn)) {
		MLY_UNLOCK(sc);
		mly_printf(sc, "CAM XPT phsyical channel registration failed\n");
		return(ENXIO);
	    }
	    MLY_UNLOCK(sc);
	    debug(1, "registered physical channel %d", chn);
	}
    }

    /*
     * Register our virtual channels, with bus numbers matching channel numbers.
     */
    chn = sc->mly_controllerinfo->physical_channels_present;
    for (i = 0; i < sc->mly_controllerinfo->virtual_channels_present; i++, chn++) {
	if ((sc->mly_cam_sim[chn] = cam_sim_alloc(mly_cam_action, mly_cam_poll, "mly", sc,
						  device_get_unit(sc->mly_dev),
						  &sc->mly_lock,
						  sc->mly_controllerinfo->maximum_parallel_commands,
						  0, devq)) == NULL) {
	    return(ENOMEM);
	}
	MLY_LOCK(sc);
	if (xpt_bus_register(sc->mly_cam_sim[chn], sc->mly_dev, chn)) {
	    MLY_UNLOCK(sc);
	    mly_printf(sc, "CAM XPT virtual channel registration failed\n");
	    return(ENXIO);
	}
	MLY_UNLOCK(sc);
	debug(1, "registered virtual channel %d", chn);
    }

    /*
     * This is the total number of channels that (might have been) registered with
     * CAM.  Some may not have been; check the mly_cam_sim array to be certain.
     */
    sc->mly_cam_channels = sc->mly_controllerinfo->physical_channels_present +
	sc->mly_controllerinfo->virtual_channels_present;

    return(0);
}

/********************************************************************************
 * Detach from CAM
 */
static void
mly_cam_detach(struct mly_softc *sc)
{
    int		i;
    
    debug_called(1);

    MLY_LOCK(sc);
    for (i = 0; i < sc->mly_cam_channels; i++) {
	if (sc->mly_cam_sim[i] != NULL) {
	    xpt_bus_deregister(cam_sim_path(sc->mly_cam_sim[i]));
	    cam_sim_free(sc->mly_cam_sim[i], 0);
	}
    }
    MLY_UNLOCK(sc);
    if (sc->mly_cam_devq != NULL)
	cam_simq_free(sc->mly_cam_devq);
}

/************************************************************************
 * Rescan a device.
 */ 
static void
mly_cam_rescan_btl(struct mly_softc *sc, int bus, int target)
{
    union ccb	*ccb;

    debug_called(1);

    if ((ccb = xpt_alloc_ccb()) == NULL) {
	mly_printf(sc, "rescan failed (can't allocate CCB)\n");
	return;
    }
    if (xpt_create_path(&ccb->ccb_h.path, NULL,
	    cam_sim_path(sc->mly_cam_sim[bus]), target, 0) != CAM_REQ_CMP) {
	mly_printf(sc, "rescan failed (can't create path)\n");
	xpt_free_ccb(ccb);
	return;
    }
    debug(1, "rescan target %d:%d", bus, target);
    xpt_rescan(ccb);
}

/********************************************************************************
 * Handle an action requested by CAM
 */
static void
mly_cam_action(struct cam_sim *sim, union ccb *ccb)
{
    struct mly_softc	*sc = cam_sim_softc(sim);

    debug_called(2);
    MLY_ASSERT_LOCKED(sc);

    switch (ccb->ccb_h.func_code) {

	/* perform SCSI I/O */
    case XPT_SCSI_IO:
	if (!mly_cam_action_io(sim, (struct ccb_scsiio *)&ccb->csio))
	    return;
	break;

	/* perform geometry calculations */
    case XPT_CALC_GEOMETRY:
    {
	struct ccb_calc_geometry	*ccg = &ccb->ccg;
        u_int32_t			secs_per_cylinder;

	debug(2, "XPT_CALC_GEOMETRY %d:%d:%d", cam_sim_bus(sim), ccb->ccb_h.target_id, ccb->ccb_h.target_lun);

	if (sc->mly_controllerparam->bios_geometry == MLY_BIOSGEOM_8G) {
	    ccg->heads = 255;
            ccg->secs_per_track = 63;
	} else {				/* MLY_BIOSGEOM_2G */
	    ccg->heads = 128;
            ccg->secs_per_track = 32;
	}
	secs_per_cylinder = ccg->heads * ccg->secs_per_track;
        ccg->cylinders = ccg->volume_size / secs_per_cylinder;
        ccb->ccb_h.status = CAM_REQ_CMP;
        break;
    }

	/* handle path attribute inquiry */
    case XPT_PATH_INQ:
    {
	struct ccb_pathinq	*cpi = &ccb->cpi;

	debug(2, "XPT_PATH_INQ %d:%d:%d", cam_sim_bus(sim), ccb->ccb_h.target_id, ccb->ccb_h.target_lun);

	cpi->version_num = 1;
	cpi->hba_inquiry = PI_TAG_ABLE;		/* XXX extra flags for physical channels? */
	cpi->target_sprt = 0;
	cpi->hba_misc = 0;
	cpi->max_target = MLY_MAX_TARGETS - 1;
	cpi->max_lun = MLY_MAX_LUNS - 1;
	cpi->initiator_id = sc->mly_controllerparam->initiator_id;
	strlcpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
	strlcpy(cpi->hba_vid, "Mylex", HBA_IDLEN);
	strlcpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
	cpi->unit_number = cam_sim_unit(sim);
	cpi->bus_id = cam_sim_bus(sim);
	cpi->base_transfer_speed = 132 * 1024;	/* XXX what to set this to? */
	cpi->transport = XPORT_SPI;
	cpi->transport_version = 2;
	cpi->protocol = PROTO_SCSI;
	cpi->protocol_version = SCSI_REV_2;
	ccb->ccb_h.status = CAM_REQ_CMP;
	break;
    }

    case XPT_GET_TRAN_SETTINGS:
    {
	struct ccb_trans_settings	*cts = &ccb->cts;
	int				bus, target;
	struct ccb_trans_settings_scsi *scsi = &cts->proto_specific.scsi;
	struct ccb_trans_settings_spi *spi = &cts->xport_specific.spi;

	cts->protocol = PROTO_SCSI;
	cts->protocol_version = SCSI_REV_2;
	cts->transport = XPORT_SPI;
	cts->transport_version = 2;

	scsi->flags = 0;
	scsi->valid = 0;
	spi->flags = 0;
	spi->valid = 0;

	bus = cam_sim_bus(sim);
	target = cts->ccb_h.target_id;
	debug(2, "XPT_GET_TRAN_SETTINGS %d:%d", bus, target);
	/* logical device? */
	if (sc->mly_btl[bus][target].mb_flags & MLY_BTL_LOGICAL) {
	    /* nothing special for these */
	/* physical device? */
	} else if (sc->mly_btl[bus][target].mb_flags & MLY_BTL_PHYSICAL) {
	    /* allow CAM to try tagged transactions */
	    scsi->flags |= CTS_SCSI_FLAGS_TAG_ENB;
	    scsi->valid |= CTS_SCSI_VALID_TQ;

	    /* convert speed (MHz) to usec */
	    if (sc->mly_btl[bus][target].mb_speed == 0) {
		spi->sync_period = 1000000 / 5;
	    } else {
		spi->sync_period = 1000000 / sc->mly_btl[bus][target].mb_speed;
	    }

	    /* convert bus width to CAM internal encoding */
	    switch (sc->mly_btl[bus][target].mb_width) {
	    case 32:
		spi->bus_width = MSG_EXT_WDTR_BUS_32_BIT;
		break;
	    case 16:
		spi->bus_width = MSG_EXT_WDTR_BUS_16_BIT;
		break;
	    case 8:
	    default:
		spi->bus_width = MSG_EXT_WDTR_BUS_8_BIT;
		break;
	    }
	    spi->valid |= CTS_SPI_VALID_SYNC_RATE | CTS_SPI_VALID_BUS_WIDTH;

	    /* not a device, bail out */
	} else {
	    cts->ccb_h.status = CAM_REQ_CMP_ERR;
	    break;
	}

	/* disconnect always OK */
	spi->flags |= CTS_SPI_FLAGS_DISC_ENB;
	spi->valid |= CTS_SPI_VALID_DISC;

	cts->ccb_h.status = CAM_REQ_CMP;
	break;
    }

    default:		/* we can't do this */
	debug(2, "unspported func_code = 0x%x", ccb->ccb_h.func_code);
	ccb->ccb_h.status = CAM_REQ_INVALID;
	break;
    }

    xpt_done(ccb);
}

/********************************************************************************
 * Handle an I/O operation requested by CAM
 */
static int
mly_cam_action_io(struct cam_sim *sim, struct ccb_scsiio *csio)
{
    struct mly_softc			*sc = cam_sim_softc(sim);
    struct mly_command			*mc;
    struct mly_command_scsi_small	*ss;
    int					bus, target;
    int					error;

    bus = cam_sim_bus(sim);
    target = csio->ccb_h.target_id;

    debug(2, "XPT_SCSI_IO %d:%d:%d", bus, target, csio->ccb_h.target_lun);

    /* validate bus number */
    if (!MLY_BUS_IS_VALID(sc, bus)) {
	debug(0, " invalid bus %d", bus);
	csio->ccb_h.status = CAM_REQ_CMP_ERR;
    }

    /*  check for I/O attempt to a protected device */
    if (sc->mly_btl[bus][target].mb_flags & MLY_BTL_PROTECTED) {
	debug(2, "  device protected");
	csio->ccb_h.status = CAM_REQ_CMP_ERR;
    }

    /* check for I/O attempt to nonexistent device */
    if (!(sc->mly_btl[bus][target].mb_flags & (MLY_BTL_LOGICAL | MLY_BTL_PHYSICAL))) {
	debug(2, "  device %d:%d does not exist", bus, target);
	csio->ccb_h.status = CAM_REQ_CMP_ERR;
    }

    /* XXX increase if/when we support large SCSI commands */
    if (csio->cdb_len > MLY_CMD_SCSI_SMALL_CDB) {
	debug(0, "  command too large (%d > %d)", csio->cdb_len, MLY_CMD_SCSI_SMALL_CDB);
	csio->ccb_h.status = CAM_REQ_CMP_ERR;
    }

    /* check that the CDB pointer is not to a physical address */
    if ((csio->ccb_h.flags & CAM_CDB_POINTER) && (csio->ccb_h.flags & CAM_CDB_PHYS)) {
	debug(0, "  CDB pointer is to physical address");
	csio->ccb_h.status = CAM_REQ_CMP_ERR;
    }

    /* abandon aborted ccbs or those that have failed validation */
    if ((csio->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_INPROG) {
	debug(2, "abandoning CCB due to abort/validation failure");
	return(EINVAL);
    }

    /*
     * Get a command, or push the ccb back to CAM and freeze the queue.
     */
    if ((error = mly_alloc_command(sc, &mc))) {
	xpt_freeze_simq(sim, 1);
	csio->ccb_h.status |= CAM_REQUEUE_REQ;
	sc->mly_qfrzn_cnt++;
	return(error);
    }
    
    /* build the command */
    mc->mc_data = csio;
    mc->mc_length = csio->dxfer_len;
    mc->mc_complete = mly_cam_complete;
    mc->mc_private = csio;
    mc->mc_flags |= MLY_CMD_CCB;
    /* XXX This code doesn't set the data direction in mc_flags. */

    /* save the bus number in the ccb for later recovery XXX should be a better way */
     csio->ccb_h.sim_priv.entries[0].field = bus;

    /* build the packet for the controller */
    ss = &mc->mc_packet->scsi_small;
    ss->opcode = MDACMD_SCSI;
    if (csio->ccb_h.flags & CAM_DIS_DISCONNECT)
	ss->command_control.disable_disconnect = 1;
    if ((csio->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_OUT)
	ss->command_control.data_direction = MLY_CCB_WRITE;
    ss->data_size = csio->dxfer_len;
    ss->addr.phys.lun = csio->ccb_h.target_lun;
    ss->addr.phys.target = csio->ccb_h.target_id;
    ss->addr.phys.channel = bus;
    if (csio->ccb_h.timeout < (60 * 1000)) {
	ss->timeout.value = csio->ccb_h.timeout / 1000;
	ss->timeout.scale = MLY_TIMEOUT_SECONDS;
    } else if (csio->ccb_h.timeout < (60 * 60 * 1000)) {
	ss->timeout.value = csio->ccb_h.timeout / (60 * 1000);
	ss->timeout.scale = MLY_TIMEOUT_MINUTES;
    } else {
	ss->timeout.value = csio->ccb_h.timeout / (60 * 60 * 1000);	/* overflow? */
	ss->timeout.scale = MLY_TIMEOUT_HOURS;
    }
    ss->maximum_sense_size = csio->sense_len;
    ss->cdb_length = csio->cdb_len;
    if (csio->ccb_h.flags & CAM_CDB_POINTER) {
	bcopy(csio->cdb_io.cdb_ptr, ss->cdb, csio->cdb_len);
    } else {
	bcopy(csio->cdb_io.cdb_bytes, ss->cdb, csio->cdb_len);
    }

    /* give the command to the controller */
    if ((error = mly_start(mc))) {
	xpt_freeze_simq(sim, 1);
	csio->ccb_h.status |= CAM_REQUEUE_REQ;
	sc->mly_qfrzn_cnt++;
	return(error);
    }

    return(0);
}

/********************************************************************************
 * Check for possibly-completed commands.
 */
static void
mly_cam_poll(struct cam_sim *sim)
{
    struct mly_softc	*sc = cam_sim_softc(sim);

    debug_called(2);

    mly_done(sc);
}

/********************************************************************************
 * Handle completion of a command - pass results back through the CCB
 */
static void
mly_cam_complete(struct mly_command *mc)
{
    struct mly_softc		*sc = mc->mc_sc;
    struct ccb_scsiio		*csio = (struct ccb_scsiio *)mc->mc_private;
    struct scsi_inquiry_data	*inq = (struct scsi_inquiry_data *)csio->data_ptr;
    struct mly_btl		*btl;
    u_int8_t			cmd;
    int				bus, target;

    debug_called(2);

    csio->scsi_status = mc->mc_status;
    switch(mc->mc_status) {
    case SCSI_STATUS_OK:
	/*
	 * In order to report logical device type and status, we overwrite
	 * the result of the INQUIRY command to logical devices.
	 */
	bus = csio->ccb_h.sim_priv.entries[0].field;
	target = csio->ccb_h.target_id;
	/* XXX validate bus/target? */
	if (sc->mly_btl[bus][target].mb_flags & MLY_BTL_LOGICAL) {
	    if (csio->ccb_h.flags & CAM_CDB_POINTER) {
		cmd = *csio->cdb_io.cdb_ptr;
	    } else {
		cmd = csio->cdb_io.cdb_bytes[0];
	    }
	    if (cmd == INQUIRY) {
		btl = &sc->mly_btl[bus][target];
		padstr(inq->vendor, mly_describe_code(mly_table_device_type, btl->mb_type), 8);
		padstr(inq->product, mly_describe_code(mly_table_device_state, btl->mb_state), 16);
		padstr(inq->revision, "", 4);
	    }
	}

	debug(2, "SCSI_STATUS_OK");
	csio->ccb_h.status = CAM_REQ_CMP;
	break;

    case SCSI_STATUS_CHECK_COND:
	debug(1, "SCSI_STATUS_CHECK_COND  sense %d  resid %d", mc->mc_sense, mc->mc_resid);
	csio->ccb_h.status = CAM_SCSI_STATUS_ERROR;
	bzero(&csio->sense_data, SSD_FULL_SIZE);
	bcopy(mc->mc_packet, &csio->sense_data, mc->mc_sense);
	csio->sense_len = mc->mc_sense;
	csio->ccb_h.status |= CAM_AUTOSNS_VALID;
	csio->resid = mc->mc_resid;	/* XXX this is a signed value... */
	break;

    case SCSI_STATUS_BUSY:
	debug(1, "SCSI_STATUS_BUSY");
	csio->ccb_h.status = CAM_SCSI_BUSY;
	break;

    default:
	debug(1, "unknown status 0x%x", csio->scsi_status);
	csio->ccb_h.status = CAM_REQ_CMP_ERR;
	break;
    }

    if (sc->mly_qfrzn_cnt) {
	csio->ccb_h.status |= CAM_RELEASE_SIMQ;
	sc->mly_qfrzn_cnt--;
    }

    xpt_done((union ccb *)csio);
    mly_release_command(mc);
}

/********************************************************************************
 * Find a peripheral attahed at (bus),(target)
 */
static struct cam_periph *
mly_find_periph(struct mly_softc *sc, int bus, int target)
{
    struct cam_periph	*periph;
    struct cam_path	*path;
    int			status;

    status = xpt_create_path(&path, NULL, cam_sim_path(sc->mly_cam_sim[bus]), target, 0);
    if (status == CAM_REQ_CMP) {
	periph = cam_periph_find(path, NULL);
	xpt_free_path(path);
    } else {
	periph = NULL;
    }
    return(periph);
}

/********************************************************************************
 * Name the device at (bus)(target)
 */
static int
mly_name_device(struct mly_softc *sc, int bus, int target)
{
    struct cam_periph	*periph;

    if ((periph = mly_find_periph(sc, bus, target)) != NULL) {
	sprintf(sc->mly_btl[bus][target].mb_name, "%s%d", periph->periph_name, periph->unit_number);
	return(0);
    }
    sc->mly_btl[bus][target].mb_name[0] = 0;
    return(ENOENT);
}

/********************************************************************************
 ********************************************************************************
                                                                 Hardware Control
 ********************************************************************************
 ********************************************************************************/

/********************************************************************************
 * Handshake with the firmware while the card is being initialised.
 */
static int
mly_fwhandshake(struct mly_softc *sc) 
{
    u_int8_t	error, param0, param1;
    int		spinup = 0;

    debug_called(1);

    /* set HM_STSACK and let the firmware initialise */
    MLY_SET_REG(sc, sc->mly_idbr, MLY_HM_STSACK);
    DELAY(1000);	/* too short? */

    /* if HM_STSACK is still true, the controller is initialising */
    if (!MLY_IDBR_TRUE(sc, MLY_HM_STSACK))
	return(0);
    mly_printf(sc, "controller initialisation started\n");

    /* spin waiting for initialisation to finish, or for a message to be delivered */
    while (MLY_IDBR_TRUE(sc, MLY_HM_STSACK)) {
	/* check for a message */
	if (MLY_ERROR_VALID(sc)) {
	    error = MLY_GET_REG(sc, sc->mly_error_status) & ~MLY_MSG_EMPTY;
	    param0 = MLY_GET_REG(sc, sc->mly_command_mailbox);
	    param1 = MLY_GET_REG(sc, sc->mly_command_mailbox + 1);

	    switch(error) {
	    case MLY_MSG_SPINUP:
		if (!spinup) {
		    mly_printf(sc, "drive spinup in progress\n");
		    spinup = 1;			/* only print this once (should print drive being spun?) */
		}
		break;
	    case MLY_MSG_RACE_RECOVERY_FAIL:
		mly_printf(sc, "mirror race recovery failed, one or more drives offline\n");
		break;
	    case MLY_MSG_RACE_IN_PROGRESS:
		mly_printf(sc, "mirror race recovery in progress\n");
		break;
	    case MLY_MSG_RACE_ON_CRITICAL:
		mly_printf(sc, "mirror race recovery on a critical drive\n");
		break;
	    case MLY_MSG_PARITY_ERROR:
		mly_printf(sc, "FATAL MEMORY PARITY ERROR\n");
		return(ENXIO);
	    default:
		mly_printf(sc, "unknown initialisation code 0x%x\n", error);
	    }
	}
    }
    return(0);
}

/********************************************************************************
 ********************************************************************************
                                                        Debugging and Diagnostics
 ********************************************************************************
 ********************************************************************************/

/********************************************************************************
 * Print some information about the controller.
 */
static void
mly_describe_controller(struct mly_softc *sc)
{
    struct mly_ioctl_getcontrollerinfo	*mi = sc->mly_controllerinfo;

    mly_printf(sc, "%16s, %d channel%s, firmware %d.%02d-%d-%02d (%02d%02d%02d%02d), %dMB RAM\n", 
	       mi->controller_name, mi->physical_channels_present, (mi->physical_channels_present) > 1 ? "s" : "",
	       mi->fw_major, mi->fw_minor, mi->fw_turn, mi->fw_build,	/* XXX turn encoding? */
	       mi->fw_century, mi->fw_year, mi->fw_month, mi->fw_day,
	       mi->memory_size);

    if (bootverbose) {
	mly_printf(sc, "%s %s (%x), %dMHz %d-bit %.16s\n", 
		   mly_describe_code(mly_table_oemname, mi->oem_information), 
		   mly_describe_code(mly_table_controllertype, mi->controller_type), mi->controller_type,
		   mi->interface_speed, mi->interface_width, mi->interface_name);
	mly_printf(sc, "%dMB %dMHz %d-bit %s%s%s, cache %dMB\n",
		   mi->memory_size, mi->memory_speed, mi->memory_width, 
		   mly_describe_code(mly_table_memorytype, mi->memory_type),
		   mi->memory_parity ? "+parity": "",mi->memory_ecc ? "+ECC": "",
		   mi->cache_size);
	mly_printf(sc, "CPU: %s @ %dMHz\n", 
		   mly_describe_code(mly_table_cputype, mi->cpu[0].type), mi->cpu[0].speed);
	if (mi->l2cache_size != 0)
	    mly_printf(sc, "%dKB L2 cache\n", mi->l2cache_size);
	if (mi->exmemory_size != 0)
	    mly_printf(sc, "%dMB %dMHz %d-bit private %s%s%s\n",
		       mi->exmemory_size, mi->exmemory_speed, mi->exmemory_width,
		       mly_describe_code(mly_table_memorytype, mi->exmemory_type),
		       mi->exmemory_parity ? "+parity": "",mi->exmemory_ecc ? "+ECC": "");
	mly_printf(sc, "battery backup %s\n", mi->bbu_present ? "present" : "not installed");
	mly_printf(sc, "maximum data transfer %d blocks, maximum sg entries/command %d\n",
		   mi->maximum_block_count, mi->maximum_sg_entries);
	mly_printf(sc, "logical devices present/critical/offline %d/%d/%d\n",
		   mi->logical_devices_present, mi->logical_devices_critical, mi->logical_devices_offline);
	mly_printf(sc, "physical devices present %d\n",
		   mi->physical_devices_present);
	mly_printf(sc, "physical disks present/offline %d/%d\n",
		   mi->physical_disks_present, mi->physical_disks_offline);
	mly_printf(sc, "%d physical channel%s, %d virtual channel%s of %d possible\n",
		   mi->physical_channels_present, mi->physical_channels_present == 1 ? "" : "s",
		   mi->virtual_channels_present, mi->virtual_channels_present == 1 ? "" : "s",
		   mi->virtual_channels_possible);
	mly_printf(sc, "%d parallel commands supported\n", mi->maximum_parallel_commands);
	mly_printf(sc, "%dMB flash ROM, %d of %d maximum cycles\n",
		   mi->flash_size, mi->flash_age, mi->flash_maximum_age);
    }
}

#ifdef MLY_DEBUG
/********************************************************************************
 * Print some controller state
 */
static void
mly_printstate(struct mly_softc *sc)
{
    mly_printf(sc, "IDBR %02x  ODBR %02x  ERROR %02x  (%x %x %x)\n",
		  MLY_GET_REG(sc, sc->mly_idbr),
		  MLY_GET_REG(sc, sc->mly_odbr),
		  MLY_GET_REG(sc, sc->mly_error_status),
		  sc->mly_idbr,
		  sc->mly_odbr,
		  sc->mly_error_status);
    mly_printf(sc, "IMASK %02x  ISTATUS %02x\n",
		  MLY_GET_REG(sc, sc->mly_interrupt_mask),
		  MLY_GET_REG(sc, sc->mly_interrupt_status));
    mly_printf(sc, "COMMAND %02x %02x %02x %02x %02x %02x %02x %02x\n",
		  MLY_GET_REG(sc, sc->mly_command_mailbox),
		  MLY_GET_REG(sc, sc->mly_command_mailbox + 1),
		  MLY_GET_REG(sc, sc->mly_command_mailbox + 2),
		  MLY_GET_REG(sc, sc->mly_command_mailbox + 3),
		  MLY_GET_REG(sc, sc->mly_command_mailbox + 4),
		  MLY_GET_REG(sc, sc->mly_command_mailbox + 5),
		  MLY_GET_REG(sc, sc->mly_command_mailbox + 6),
		  MLY_GET_REG(sc, sc->mly_command_mailbox + 7));
    mly_printf(sc, "STATUS  %02x %02x %02x %02x %02x %02x %02x %02x\n",
		  MLY_GET_REG(sc, sc->mly_status_mailbox),
		  MLY_GET_REG(sc, sc->mly_status_mailbox + 1),
		  MLY_GET_REG(sc, sc->mly_status_mailbox + 2),
		  MLY_GET_REG(sc, sc->mly_status_mailbox + 3),
		  MLY_GET_REG(sc, sc->mly_status_mailbox + 4),
		  MLY_GET_REG(sc, sc->mly_status_mailbox + 5),
		  MLY_GET_REG(sc, sc->mly_status_mailbox + 6),
		  MLY_GET_REG(sc, sc->mly_status_mailbox + 7));
    mly_printf(sc, "        %04x        %08x\n",
		  MLY_GET_REG2(sc, sc->mly_status_mailbox),
		  MLY_GET_REG4(sc, sc->mly_status_mailbox + 4));
}

struct mly_softc	*mly_softc0 = NULL;
void
mly_printstate0(void)
{
    if (mly_softc0 != NULL)
	mly_printstate(mly_softc0);
}

/********************************************************************************
 * Print a command
 */
static void
mly_print_command(struct mly_command *mc)
{
    struct mly_softc	*sc = mc->mc_sc;
    
    mly_printf(sc, "COMMAND @ %p\n", mc);
    mly_printf(sc, "  slot      %d\n", mc->mc_slot);
    mly_printf(sc, "  status    0x%x\n", mc->mc_status);
    mly_printf(sc, "  sense len %d\n", mc->mc_sense);
    mly_printf(sc, "  resid     %d\n", mc->mc_resid);
    mly_printf(sc, "  packet    %p/0x%llx\n", mc->mc_packet, mc->mc_packetphys);
    if (mc->mc_packet != NULL)
	mly_print_packet(mc);
    mly_printf(sc, "  data      %p/%d\n", mc->mc_data, mc->mc_length);
    mly_printf(sc, "  flags     %b\n", mc->mc_flags, "\20\1busy\2complete\3slotted\4mapped\5datain\6dataout\n");
    mly_printf(sc, "  complete  %p\n", mc->mc_complete);
    mly_printf(sc, "  private   %p\n", mc->mc_private);
}

/********************************************************************************
 * Print a command packet
 */
static void
mly_print_packet(struct mly_command *mc)
{
    struct mly_softc			*sc = mc->mc_sc;
    struct mly_command_generic		*ge = (struct mly_command_generic *)mc->mc_packet;
    struct mly_command_scsi_small	*ss = (struct mly_command_scsi_small *)mc->mc_packet;
    struct mly_command_scsi_large	*sl = (struct mly_command_scsi_large *)mc->mc_packet;
    struct mly_command_ioctl		*io = (struct mly_command_ioctl *)mc->mc_packet;
    int					transfer;

    mly_printf(sc, "   command_id           %d\n", ge->command_id);
    mly_printf(sc, "   opcode               %d\n", ge->opcode);
    mly_printf(sc, "   command_control      fua %d  dpo %d  est %d  dd %s  nas %d ddis %d\n",
		  ge->command_control.force_unit_access,
		  ge->command_control.disable_page_out,
		  ge->command_control.extended_sg_table,
		  (ge->command_control.data_direction == MLY_CCB_WRITE) ? "WRITE" : "READ",
		  ge->command_control.no_auto_sense,
		  ge->command_control.disable_disconnect);
    mly_printf(sc, "   data_size            %d\n", ge->data_size);
    mly_printf(sc, "   sense_buffer_address 0x%llx\n", ge->sense_buffer_address);
    mly_printf(sc, "   lun                  %d\n", ge->addr.phys.lun);
    mly_printf(sc, "   target               %d\n", ge->addr.phys.target);
    mly_printf(sc, "   channel              %d\n", ge->addr.phys.channel);
    mly_printf(sc, "   logical device       %d\n", ge->addr.log.logdev);
    mly_printf(sc, "   controller           %d\n", ge->addr.phys.controller);
    mly_printf(sc, "   timeout              %d %s\n", 
		  ge->timeout.value,
		  (ge->timeout.scale == MLY_TIMEOUT_SECONDS) ? "seconds" : 
		  ((ge->timeout.scale == MLY_TIMEOUT_MINUTES) ? "minutes" : "hours"));
    mly_printf(sc, "   maximum_sense_size   %d\n", ge->maximum_sense_size);
    switch(ge->opcode) {
    case MDACMD_SCSIPT:
    case MDACMD_SCSI:
	mly_printf(sc, "   cdb length           %d\n", ss->cdb_length);
	mly_printf(sc, "   cdb                  %*D\n", ss->cdb_length, ss->cdb, " ");
	transfer = 1;
	break;
    case MDACMD_SCSILC:
    case MDACMD_SCSILCPT:
	mly_printf(sc, "   cdb length           %d\n", sl->cdb_length);
	mly_printf(sc, "   cdb                  0x%llx\n", sl->cdb_physaddr);
	transfer = 1;
	break;
    case MDACMD_IOCTL:
	mly_printf(sc, "   sub_ioctl            0x%x\n", io->sub_ioctl);
	switch(io->sub_ioctl) {
	case MDACIOCTL_SETMEMORYMAILBOX:
	    mly_printf(sc, "   health_buffer_size   %d\n", 
			  io->param.setmemorymailbox.health_buffer_size);
	    mly_printf(sc, "   health_buffer_phys   0x%llx\n",
			  io->param.setmemorymailbox.health_buffer_physaddr);
	    mly_printf(sc, "   command_mailbox      0x%llx\n",
			  io->param.setmemorymailbox.command_mailbox_physaddr);
	    mly_printf(sc, "   status_mailbox       0x%llx\n",
			  io->param.setmemorymailbox.status_mailbox_physaddr);
	    transfer = 0;
	    break;

	case MDACIOCTL_SETREALTIMECLOCK:
	case MDACIOCTL_GETHEALTHSTATUS:
	case MDACIOCTL_GETCONTROLLERINFO:
	case MDACIOCTL_GETLOGDEVINFOVALID:
	case MDACIOCTL_GETPHYSDEVINFOVALID:
	case MDACIOCTL_GETPHYSDEVSTATISTICS:
	case MDACIOCTL_GETLOGDEVSTATISTICS:
	case MDACIOCTL_GETCONTROLLERSTATISTICS:
	case MDACIOCTL_GETBDT_FOR_SYSDRIVE:	    
	case MDACIOCTL_CREATENEWCONF:
	case MDACIOCTL_ADDNEWCONF:
	case MDACIOCTL_GETDEVCONFINFO:
	case MDACIOCTL_GETFREESPACELIST:
	case MDACIOCTL_MORE:
	case MDACIOCTL_SETPHYSDEVPARAMETER:
	case MDACIOCTL_GETPHYSDEVPARAMETER:
	case MDACIOCTL_GETLOGDEVPARAMETER:
	case MDACIOCTL_SETLOGDEVPARAMETER:
	    mly_printf(sc, "   param                %10D\n", io->param.data.param, " ");
	    transfer = 1;
	    break;

	case MDACIOCTL_GETEVENT:
	    mly_printf(sc, "   event                %d\n", 
		       io->param.getevent.sequence_number_low + ((u_int32_t)io->addr.log.logdev << 16));
	    transfer = 1;
	    break;

	case MDACIOCTL_SETRAIDDEVSTATE:
	    mly_printf(sc, "   state                %d\n", io->param.setraiddevstate.state);
	    transfer = 0;
	    break;

	case MDACIOCTL_XLATEPHYSDEVTORAIDDEV:
	    mly_printf(sc, "   raid_device          %d\n", io->param.xlatephysdevtoraiddev.raid_device);
	    mly_printf(sc, "   controller           %d\n", io->param.xlatephysdevtoraiddev.controller);
	    mly_printf(sc, "   channel              %d\n", io->param.xlatephysdevtoraiddev.channel);
	    mly_printf(sc, "   target               %d\n", io->param.xlatephysdevtoraiddev.target);
	    mly_printf(sc, "   lun                  %d\n", io->param.xlatephysdevtoraiddev.lun);
	    transfer = 0;
	    break;

	case MDACIOCTL_GETGROUPCONFINFO:
	    mly_printf(sc, "   group                %d\n", io->param.getgroupconfinfo.group);
	    transfer = 1;
	    break;

	case MDACIOCTL_GET_SUBSYSTEM_DATA:
	case MDACIOCTL_SET_SUBSYSTEM_DATA:
	case MDACIOCTL_STARTDISOCVERY:
	case MDACIOCTL_INITPHYSDEVSTART:
	case MDACIOCTL_INITPHYSDEVSTOP:
	case MDACIOCTL_INITRAIDDEVSTART:
	case MDACIOCTL_INITRAIDDEVSTOP:
	case MDACIOCTL_REBUILDRAIDDEVSTART:
	case MDACIOCTL_REBUILDRAIDDEVSTOP:
	case MDACIOCTL_MAKECONSISTENTDATASTART:
	case MDACIOCTL_MAKECONSISTENTDATASTOP:
	case MDACIOCTL_CONSISTENCYCHECKSTART:
	case MDACIOCTL_CONSISTENCYCHECKSTOP:
	case MDACIOCTL_RESETDEVICE:
	case MDACIOCTL_FLUSHDEVICEDATA:
	case MDACIOCTL_PAUSEDEVICE:
	case MDACIOCTL_UNPAUSEDEVICE:
	case MDACIOCTL_LOCATEDEVICE:
	case MDACIOCTL_SETMASTERSLAVEMODE:
	case MDACIOCTL_DELETERAIDDEV:
	case MDACIOCTL_REPLACEINTERNALDEV:
	case MDACIOCTL_CLEARCONF:
	case MDACIOCTL_GETCONTROLLERPARAMETER:
	case MDACIOCTL_SETCONTRLLERPARAMETER:
	case MDACIOCTL_CLEARCONFSUSPMODE:
	case MDACIOCTL_STOREIMAGE:
	case MDACIOCTL_READIMAGE:
	case MDACIOCTL_FLASHIMAGES:
	case MDACIOCTL_RENAMERAIDDEV:
	default:			/* no idea what to print */
	    transfer = 0;
	    break;
	}
	break;

    case MDACMD_IOCTLCHECK:
    case MDACMD_MEMCOPY:
    default:
	transfer = 0;
	break;	/* print nothing */
    }
    if (transfer) {
	if (ge->command_control.extended_sg_table) {
	    mly_printf(sc, "   sg table             0x%llx/%d\n",
			  ge->transfer.indirect.table_physaddr[0], ge->transfer.indirect.entries[0]);
	} else {
	    mly_printf(sc, "   0000                 0x%llx/%lld\n",
			  ge->transfer.direct.sg[0].physaddr, ge->transfer.direct.sg[0].length);
	    mly_printf(sc, "   0001                 0x%llx/%lld\n",
			  ge->transfer.direct.sg[1].physaddr, ge->transfer.direct.sg[1].length);
	}
    }
}

/********************************************************************************
 * Panic in a slightly informative fashion
 */
static void
mly_panic(struct mly_softc *sc, char *reason)
{
    mly_printstate(sc);
    panic(reason);
}

/********************************************************************************
 * Print queue statistics, callable from DDB.
 */
void
mly_print_controller(int controller)
{
    struct mly_softc	*sc;
    
    if ((sc = devclass_get_softc(devclass_find("mly"), controller)) == NULL) {
	printf("mly: controller %d invalid\n", controller);
    } else {
	device_printf(sc->mly_dev, "queue    curr max\n");
	device_printf(sc->mly_dev, "free     %04d/%04d\n", 
		      sc->mly_qstat[MLYQ_FREE].q_length, sc->mly_qstat[MLYQ_FREE].q_max);
	device_printf(sc->mly_dev, "busy     %04d/%04d\n", 
		      sc->mly_qstat[MLYQ_BUSY].q_length, sc->mly_qstat[MLYQ_BUSY].q_max);
	device_printf(sc->mly_dev, "complete %04d/%04d\n", 
		      sc->mly_qstat[MLYQ_COMPLETE].q_length, sc->mly_qstat[MLYQ_COMPLETE].q_max);
    }
}
#endif


/********************************************************************************
 ********************************************************************************
                                                         Control device interface
 ********************************************************************************
 ********************************************************************************/

/********************************************************************************
 * Accept an open operation on the control device.
 */
static int
mly_user_open(struct cdev *dev, int flags, int fmt, struct thread *td)
{
    struct mly_softc	*sc = dev->si_drv1;

    MLY_LOCK(sc);
    sc->mly_state |= MLY_STATE_OPEN;
    MLY_UNLOCK(sc);
    return(0);
}

/********************************************************************************
 * Accept the last close on the control device.
 */
static int
mly_user_close(struct cdev *dev, int flags, int fmt, struct thread *td)
{
    struct mly_softc	*sc = dev->si_drv1;

    MLY_LOCK(sc);
    sc->mly_state &= ~MLY_STATE_OPEN;
    MLY_UNLOCK(sc);
    return (0);
}

/********************************************************************************
 * Handle controller-specific control operations.
 */
static int
mly_user_ioctl(struct cdev *dev, u_long cmd, caddr_t addr,
				int32_t flag, struct thread *td)
{
    struct mly_softc		*sc = (struct mly_softc *)dev->si_drv1;
    struct mly_user_command	*uc = (struct mly_user_command *)addr;
    struct mly_user_health	*uh = (struct mly_user_health *)addr;
    
    switch(cmd) {
    case MLYIO_COMMAND:
	return(mly_user_command(sc, uc));
    case MLYIO_HEALTH:
	return(mly_user_health(sc, uh));
    default:
	return(ENOIOCTL);
    }
}

/********************************************************************************
 * Execute a command passed in from userspace.
 *
 * The control structure contains the actual command for the controller, as well
 * as the user-space data pointer and data size, and an optional sense buffer
 * size/pointer.  On completion, the data size is adjusted to the command
 * residual, and the sense buffer size to the size of the returned sense data.
 * 
 */
static int
mly_user_command(struct mly_softc *sc, struct mly_user_command *uc)
{
    struct mly_command	*mc;
    int			error;

    /* allocate a command */
    MLY_LOCK(sc);
    if (mly_alloc_command(sc, &mc)) {
	MLY_UNLOCK(sc);
	return (ENOMEM);	/* XXX Linux version will wait for a command */
    }
    MLY_UNLOCK(sc);

    /* handle data size/direction */
    mc->mc_length = (uc->DataTransferLength >= 0) ? uc->DataTransferLength : -uc->DataTransferLength;
    if (mc->mc_length > 0) {
	if ((mc->mc_data = malloc(mc->mc_length, M_DEVBUF, M_NOWAIT)) == NULL) {
	    error = ENOMEM;
	    goto out;
	}
    }
    if (uc->DataTransferLength > 0) {
	mc->mc_flags |= MLY_CMD_DATAIN;
	bzero(mc->mc_data, mc->mc_length);
    }
    if (uc->DataTransferLength < 0) {
	mc->mc_flags |= MLY_CMD_DATAOUT;
	if ((error = copyin(uc->DataTransferBuffer, mc->mc_data, mc->mc_length)) != 0)
	    goto out;
    }

    /* copy the controller command */
    bcopy(&uc->CommandMailbox, mc->mc_packet, sizeof(uc->CommandMailbox));

    /* clear command completion handler so that we get woken up */
    mc->mc_complete = NULL;

    /* execute the command */
    MLY_LOCK(sc);
    if ((error = mly_start(mc)) != 0) {
	MLY_UNLOCK(sc);
	goto out;
    }
    while (!(mc->mc_flags & MLY_CMD_COMPLETE))
	mtx_sleep(mc, &sc->mly_lock, PRIBIO, "mlyioctl", 0);
    MLY_UNLOCK(sc);

    /* return the data to userspace */
    if (uc->DataTransferLength > 0)
	if ((error = copyout(mc->mc_data, uc->DataTransferBuffer, mc->mc_length)) != 0)
	    goto out;
    
    /* return the sense buffer to userspace */
    if ((uc->RequestSenseLength > 0) && (mc->mc_sense > 0)) {
	if ((error = copyout(mc->mc_packet, uc->RequestSenseBuffer, 
			     min(uc->RequestSenseLength, mc->mc_sense))) != 0)
	    goto out;
    }
    
    /* return command results to userspace (caller will copy out) */
    uc->DataTransferLength = mc->mc_resid;
    uc->RequestSenseLength = min(uc->RequestSenseLength, mc->mc_sense);
    uc->CommandStatus = mc->mc_status;
    error = 0;

 out:
    if (mc->mc_data != NULL)
	free(mc->mc_data, M_DEVBUF);
    MLY_LOCK(sc);
    mly_release_command(mc);
    MLY_UNLOCK(sc);
    return(error);
}

/********************************************************************************
 * Return health status to userspace.  If the health change index in the user
 * structure does not match that currently exported by the controller, we
 * return the current status immediately.  Otherwise, we block until either
 * interrupted or new status is delivered.
 */
static int
mly_user_health(struct mly_softc *sc, struct mly_user_health *uh)
{
    struct mly_health_status		mh;
    int					error;
    
    /* fetch the current health status from userspace */
    if ((error = copyin(uh->HealthStatusBuffer, &mh, sizeof(mh))) != 0)
	return(error);

    /* spin waiting for a status update */
    MLY_LOCK(sc);
    error = EWOULDBLOCK;
    while ((error != 0) && (sc->mly_event_change == mh.change_counter))
	error = mtx_sleep(&sc->mly_event_change, &sc->mly_lock, PRIBIO | PCATCH,
	    "mlyhealth", 0);
    mh = sc->mly_mmbox->mmm_health.status;
    MLY_UNLOCK(sc);
    
    /* copy the controller's health status buffer out */
    error = copyout(&mh, uh->HealthStatusBuffer, sizeof(mh));
    return(error);
}

#ifdef MLY_DEBUG
static void
mly_timeout(void *arg)
{
	struct mly_softc *sc;
	struct mly_command *mc;
	int deadline;

	sc = arg;
	MLY_ASSERT_LOCKED(sc);
	deadline = time_second - MLY_CMD_TIMEOUT;
	TAILQ_FOREACH(mc, &sc->mly_busy, mc_link) {
		if ((mc->mc_timestamp < deadline)) {
			device_printf(sc->mly_dev,
			    "COMMAND %p TIMEOUT AFTER %d SECONDS\n", mc,
			    (int)(time_second - mc->mc_timestamp));
		}
	}

	callout_reset(&sc->mly_timeout, MLY_CMD_TIMEOUT * hz, mly_timeout, sc);
}
#endif
