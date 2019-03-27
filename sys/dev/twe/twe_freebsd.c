/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2003 Paul Saab
 * Copyright (c) 2003 Vinod Kashyap
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * FreeBSD-specific code.
 */

#include <dev/twe/twe_compat.h>
#include <dev/twe/twereg.h>
#include <dev/twe/tweio.h>
#include <dev/twe/twevar.h>
#include <dev/twe/twe_tables.h>

#include <vm/vm.h>

static devclass_t	twe_devclass;

#ifdef TWE_DEBUG
static u_int32_t	twed_bio_in;
#define TWED_BIO_IN	twed_bio_in++
static u_int32_t	twed_bio_out;
#define TWED_BIO_OUT	twed_bio_out++
#else
#define TWED_BIO_IN
#define TWED_BIO_OUT
#endif

static void	twe_setup_data_dmamap(void *arg, bus_dma_segment_t *segs, int nsegments, int error);
static void	twe_setup_request_dmamap(void *arg, bus_dma_segment_t *segs, int nsegments, int error);

/********************************************************************************
 ********************************************************************************
                                                         Control device interface
 ********************************************************************************
 ********************************************************************************/

static	d_open_t		twe_open;
static	d_close_t		twe_close;
static	d_ioctl_t		twe_ioctl_wrapper;

static struct cdevsw twe_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	twe_open,
	.d_close =	twe_close,
	.d_ioctl =	twe_ioctl_wrapper,
	.d_name =	"twe",
};

/********************************************************************************
 * Accept an open operation on the control device.
 */
static int
twe_open(struct cdev *dev, int flags, int fmt, struct thread *td)
{
    struct twe_softc		*sc = (struct twe_softc *)dev->si_drv1;

    TWE_IO_LOCK(sc);
    if (sc->twe_state & TWE_STATE_DETACHING) {
	TWE_IO_UNLOCK(sc);
	return (ENXIO);
    }
    sc->twe_state |= TWE_STATE_OPEN;
    TWE_IO_UNLOCK(sc);
    return(0);
}

/********************************************************************************
 * Accept the last close on the control device.
 */
static int
twe_close(struct cdev *dev, int flags, int fmt, struct thread *td)
{
    struct twe_softc		*sc = (struct twe_softc *)dev->si_drv1;

    TWE_IO_LOCK(sc);
    sc->twe_state &= ~TWE_STATE_OPEN;
    TWE_IO_UNLOCK(sc);
    return (0);
}

/********************************************************************************
 * Handle controller-specific control operations.
 */
static int
twe_ioctl_wrapper(struct cdev *dev, u_long cmd, caddr_t addr, int32_t flag, struct thread *td)
{
    struct twe_softc		*sc = (struct twe_softc *)dev->si_drv1;
    
    return(twe_ioctl(sc, cmd, addr));
}

/********************************************************************************
 ********************************************************************************
                                                             PCI device interface
 ********************************************************************************
 ********************************************************************************/

static int	twe_probe(device_t dev);
static int	twe_attach(device_t dev);
static void	twe_free(struct twe_softc *sc);
static int	twe_detach(device_t dev);
static int	twe_shutdown(device_t dev);
static int	twe_suspend(device_t dev);
static int	twe_resume(device_t dev);
static void	twe_pci_intr(void *arg);
static void	twe_intrhook(void *arg);

static device_method_t twe_methods[] = {
    /* Device interface */
    DEVMETHOD(device_probe,	twe_probe),
    DEVMETHOD(device_attach,	twe_attach),
    DEVMETHOD(device_detach,	twe_detach),
    DEVMETHOD(device_shutdown,	twe_shutdown),
    DEVMETHOD(device_suspend,	twe_suspend),
    DEVMETHOD(device_resume,	twe_resume),

    DEVMETHOD_END
};

static driver_t twe_pci_driver = {
	"twe",
	twe_methods,
	sizeof(struct twe_softc)
};

DRIVER_MODULE(twe, pci, twe_pci_driver, twe_devclass, 0, 0);

/********************************************************************************
 * Match a 3ware Escalade ATA RAID controller.
 */
static int
twe_probe(device_t dev)
{

    debug_called(4);

    if ((pci_get_vendor(dev) == TWE_VENDOR_ID) &&
	((pci_get_device(dev) == TWE_DEVICE_ID) || 
	 (pci_get_device(dev) == TWE_DEVICE_ID_ASIC))) {
	device_set_desc_copy(dev, TWE_DEVICE_NAME ". Driver version " TWE_DRIVER_VERSION_STRING);
	return(BUS_PROBE_DEFAULT);
    }
    return(ENXIO);
}

/********************************************************************************
 * Allocate resources, initialise the controller.
 */
static int
twe_attach(device_t dev)
{
    struct twe_softc	*sc;
    struct sysctl_oid	*sysctl_tree;
    int			rid, error;

    debug_called(4);

    /*
     * Initialise the softc structure.
     */
    sc = device_get_softc(dev);
    sc->twe_dev = dev;
    mtx_init(&sc->twe_io_lock, "twe I/O", NULL, MTX_DEF);
    sx_init(&sc->twe_config_lock, "twe config");

    /*
     * XXX: This sysctl tree must stay at hw.tweX rather than using
     * the device_get_sysctl_tree() created by new-bus because
     * existing 3rd party binary tools such as tw_cli and 3dm2 use the
     * existence of this sysctl node to discover controllers.
     */
    sysctl_tree = SYSCTL_ADD_NODE(device_get_sysctl_ctx(dev),
	SYSCTL_STATIC_CHILDREN(_hw), OID_AUTO,
	device_get_nameunit(dev), CTLFLAG_RD, 0, "");
    if (sysctl_tree == NULL) {
	twe_printf(sc, "cannot add sysctl tree node\n");
	return (ENXIO);
    }
    SYSCTL_ADD_STRING(device_get_sysctl_ctx(dev), SYSCTL_CHILDREN(sysctl_tree),
	OID_AUTO, "driver_version", CTLFLAG_RD, TWE_DRIVER_VERSION_STRING, 0,
	"TWE driver version");

    /*
     * Force the busmaster enable bit on, in case the BIOS forgot.
     */
    pci_enable_busmaster(dev);

    /*
     * Allocate the PCI register window.
     */
    rid = TWE_IO_CONFIG_REG;
    if ((sc->twe_io = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &rid, 
        RF_ACTIVE)) == NULL) {
	twe_printf(sc, "can't allocate register window\n");
	twe_free(sc);
	return(ENXIO);
    }

    /*
     * Allocate the parent bus DMA tag appropriate for PCI.
     */
    if (bus_dma_tag_create(bus_get_dma_tag(dev),		/* PCI parent */
			   1, 0, 				/* alignment, boundary */
			   BUS_SPACE_MAXADDR_32BIT, 		/* lowaddr */
			   BUS_SPACE_MAXADDR, 			/* highaddr */
			   NULL, NULL, 				/* filter, filterarg */
			   BUS_SPACE_MAXSIZE_32BIT,		/* maxsize */
			   BUS_SPACE_UNRESTRICTED,		/* nsegments */
			   BUS_SPACE_MAXSIZE_32BIT,		/* maxsegsize */
			   0,					/* flags */
			   NULL,				/* lockfunc */
			   NULL,				/* lockarg */
			   &sc->twe_parent_dmat)) {
	twe_printf(sc, "can't allocate parent DMA tag\n");
	twe_free(sc);
	return(ENOMEM);
    }

    /* 
     * Allocate and connect our interrupt.
     */
    rid = 0;
    if ((sc->twe_irq = bus_alloc_resource_any(sc->twe_dev, SYS_RES_IRQ,
        &rid, RF_SHAREABLE | RF_ACTIVE)) == NULL) {
	twe_printf(sc, "can't allocate interrupt\n");
	twe_free(sc);
	return(ENXIO);
    }
    if (bus_setup_intr(sc->twe_dev, sc->twe_irq, INTR_TYPE_BIO | INTR_ENTROPY | INTR_MPSAFE,  
		       NULL, twe_pci_intr, sc, &sc->twe_intr)) {
	twe_printf(sc, "can't set up interrupt\n");
	twe_free(sc);
	return(ENXIO);
    }

    /*
     * Create DMA tag for mapping command's into controller-addressable space.
     */
    if (bus_dma_tag_create(sc->twe_parent_dmat, 	/* parent */
			   1, 0, 			/* alignment, boundary */
			   BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
			   BUS_SPACE_MAXADDR, 		/* highaddr */
			   NULL, NULL, 			/* filter, filterarg */
			   sizeof(TWE_Command) *
			   TWE_Q_LENGTH, 1,		/* maxsize, nsegments */
			   BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
			   0,				/* flags */
			   NULL,			/* lockfunc */
			   NULL,			/* lockarg */
			   &sc->twe_cmd_dmat)) {
	twe_printf(sc, "can't allocate data buffer DMA tag\n");
	twe_free(sc);
	return(ENOMEM);
    }
    /*
     * Allocate memory and make it available for DMA.
     */
    if (bus_dmamem_alloc(sc->twe_cmd_dmat, (void **)&sc->twe_cmd,
			 BUS_DMA_NOWAIT, &sc->twe_cmdmap)) {
	twe_printf(sc, "can't allocate command memory\n");
	return(ENOMEM);
    }
    bus_dmamap_load(sc->twe_cmd_dmat, sc->twe_cmdmap, sc->twe_cmd,
		    sizeof(TWE_Command) * TWE_Q_LENGTH,
		    twe_setup_request_dmamap, sc, 0);
    bzero(sc->twe_cmd, sizeof(TWE_Command) * TWE_Q_LENGTH);

    /*
     * Create DMA tag for mapping objects into controller-addressable space.
     */
    if (bus_dma_tag_create(sc->twe_parent_dmat, 	/* parent */
			   1, 0, 			/* alignment, boundary */
			   BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
			   BUS_SPACE_MAXADDR, 		/* highaddr */
			   NULL, NULL, 			/* filter, filterarg */
			   (TWE_MAX_SGL_LENGTH - 1) * PAGE_SIZE,/* maxsize */
			   TWE_MAX_SGL_LENGTH,		/* nsegments */
			   BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
			   BUS_DMA_ALLOCNOW,		/* flags */
			   busdma_lock_mutex,		/* lockfunc */
			   &sc->twe_io_lock,		/* lockarg */
			   &sc->twe_buffer_dmat)) {
	twe_printf(sc, "can't allocate data buffer DMA tag\n");
	twe_free(sc);
	return(ENOMEM);
    }

    /*
     * Create DMA tag for mapping objects into controller-addressable space.
     */
    if (bus_dma_tag_create(sc->twe_parent_dmat, 	/* parent */
			   1, 0, 			/* alignment, boundary */
			   BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
			   BUS_SPACE_MAXADDR, 		/* highaddr */
			   NULL, NULL, 			/* filter, filterarg */
			   DFLTPHYS, 1,			/* maxsize, nsegments */
			   BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
			   0,				/* flags */
			   NULL,			/* lockfunc */
			   NULL,			/* lockarg */
			   &sc->twe_immediate_dmat)) {
	twe_printf(sc, "can't allocate data buffer DMA tag\n");
	twe_free(sc);
	return(ENOMEM);
    }
    /*
     * Allocate memory for requests which cannot sleep or support continuation.
     */
     if (bus_dmamem_alloc(sc->twe_immediate_dmat, (void **)&sc->twe_immediate,
			  BUS_DMA_NOWAIT, &sc->twe_immediate_map)) {
	twe_printf(sc, "can't allocate memory for immediate requests\n");
	return(ENOMEM);
     }

    /*
     * Initialise the controller and driver core.
     */
    if ((error = twe_setup(sc))) {
	twe_free(sc);
	return(error);
    }

    /*
     * Print some information about the controller and configuration.
     */
    twe_describe_controller(sc);

    /*
     * Create the control device.
     */
    sc->twe_dev_t = make_dev(&twe_cdevsw, device_get_unit(sc->twe_dev), UID_ROOT, GID_OPERATOR,
			     S_IRUSR | S_IWUSR, "twe%d", device_get_unit(sc->twe_dev));
    sc->twe_dev_t->si_drv1 = sc;
    /*
     * Schedule ourselves to bring the controller up once interrupts are available.
     * This isn't strictly necessary, since we disable interrupts while probing the
     * controller, but it is more in keeping with common practice for other disk 
     * devices.
     */
    sc->twe_ich.ich_func = twe_intrhook;
    sc->twe_ich.ich_arg = sc;
    if (config_intrhook_establish(&sc->twe_ich) != 0) {
	twe_printf(sc, "can't establish configuration hook\n");
	twe_free(sc);
	return(ENXIO);
    }

    return(0);
}

/********************************************************************************
 * Free all of the resources associated with (sc).
 *
 * Should not be called if the controller is active.
 */
static void
twe_free(struct twe_softc *sc)
{
    struct twe_request	*tr;

    debug_called(4);

    /* throw away any command buffers */
    while ((tr = twe_dequeue_free(sc)) != NULL)
	twe_free_request(tr);

    if (sc->twe_cmd != NULL) {
	bus_dmamap_unload(sc->twe_cmd_dmat, sc->twe_cmdmap);
	bus_dmamem_free(sc->twe_cmd_dmat, sc->twe_cmd, sc->twe_cmdmap);
    }

    if (sc->twe_immediate != NULL) {
	bus_dmamap_unload(sc->twe_immediate_dmat, sc->twe_immediate_map);
	bus_dmamem_free(sc->twe_immediate_dmat, sc->twe_immediate,
			sc->twe_immediate_map);
    }

    if (sc->twe_immediate_dmat)
	bus_dma_tag_destroy(sc->twe_immediate_dmat);

    /* destroy the data-transfer DMA tag */
    if (sc->twe_buffer_dmat)
	bus_dma_tag_destroy(sc->twe_buffer_dmat);

    /* disconnect the interrupt handler */
    if (sc->twe_intr)
	bus_teardown_intr(sc->twe_dev, sc->twe_irq, sc->twe_intr);
    if (sc->twe_irq != NULL)
	bus_release_resource(sc->twe_dev, SYS_RES_IRQ, 0, sc->twe_irq);

    /* destroy the parent DMA tag */
    if (sc->twe_parent_dmat)
	bus_dma_tag_destroy(sc->twe_parent_dmat);

    /* release the register window mapping */
    if (sc->twe_io != NULL)
	bus_release_resource(sc->twe_dev, SYS_RES_IOPORT, TWE_IO_CONFIG_REG, sc->twe_io);

    /* destroy control device */
    if (sc->twe_dev_t != (struct cdev *)NULL)
	destroy_dev(sc->twe_dev_t);

    sx_destroy(&sc->twe_config_lock);
    mtx_destroy(&sc->twe_io_lock);
}

/********************************************************************************
 * Disconnect from the controller completely, in preparation for unload.
 */
static int
twe_detach(device_t dev)
{
    struct twe_softc	*sc = device_get_softc(dev);

    debug_called(4);

    TWE_IO_LOCK(sc);
    if (sc->twe_state & TWE_STATE_OPEN) {
	TWE_IO_UNLOCK(sc);
	return (EBUSY);
    }
    sc->twe_state |= TWE_STATE_DETACHING;
    TWE_IO_UNLOCK(sc);

    /*	
     * Shut the controller down.
     */
    if (twe_shutdown(dev)) {
	TWE_IO_LOCK(sc);
	sc->twe_state &= ~TWE_STATE_DETACHING;
	TWE_IO_UNLOCK(sc);
	return (EBUSY);
    }

    twe_free(sc);

    return(0);
}

/********************************************************************************
 * Bring the controller down to a dormant state and detach all child devices.
 *
 * Note that we can assume that the bioq on the controller is empty, as we won't
 * allow shutdown if any device is open.
 */
static int
twe_shutdown(device_t dev)
{
    struct twe_softc	*sc = device_get_softc(dev);
    int			i, error = 0;

    debug_called(4);

    /* 
     * Delete all our child devices.
     */
    TWE_CONFIG_LOCK(sc);
    for (i = 0; i < TWE_MAX_UNITS; i++) {
	if (sc->twe_drive[i].td_disk != 0) {
	    if ((error = twe_detach_drive(sc, i)) != 0) {
		TWE_CONFIG_UNLOCK(sc);
		return (error);
	    }
	}
    }
    TWE_CONFIG_UNLOCK(sc);

    /*
     * Bring the controller down.
     */
    TWE_IO_LOCK(sc);
    twe_deinit(sc);
    TWE_IO_UNLOCK(sc);

    return(0);
}

/********************************************************************************
 * Bring the controller to a quiescent state, ready for system suspend.
 */
static int
twe_suspend(device_t dev)
{
    struct twe_softc	*sc = device_get_softc(dev);

    debug_called(4);

    TWE_IO_LOCK(sc);
    sc->twe_state |= TWE_STATE_SUSPEND;
    
    twe_disable_interrupts(sc);
    TWE_IO_UNLOCK(sc);

    return(0);
}

/********************************************************************************
 * Bring the controller back to a state ready for operation.
 */
static int
twe_resume(device_t dev)
{
    struct twe_softc	*sc = device_get_softc(dev);

    debug_called(4);

    TWE_IO_LOCK(sc);
    sc->twe_state &= ~TWE_STATE_SUSPEND;
    twe_enable_interrupts(sc);
    TWE_IO_UNLOCK(sc);

    return(0);
}

/*******************************************************************************
 * Take an interrupt, or be poked by other code to look for interrupt-worthy
 * status.
 */
static void
twe_pci_intr(void *arg)
{
    struct twe_softc *sc = arg;

    TWE_IO_LOCK(sc);
    twe_intr(sc);
    TWE_IO_UNLOCK(sc);
}

/********************************************************************************
 * Delayed-startup hook
 */
static void
twe_intrhook(void *arg)
{
    struct twe_softc		*sc = (struct twe_softc *)arg;

    /* pull ourselves off the intrhook chain */
    config_intrhook_disestablish(&sc->twe_ich);

    /* call core startup routine */
    twe_init(sc);
}

/********************************************************************************
 * Given a detected drive, attach it to the bio interface.
 *
 * This is called from twe_add_unit.
 */
int
twe_attach_drive(struct twe_softc *sc, struct twe_drive *dr)
{
    char	buf[80];
    int		error;

    mtx_lock(&Giant);
    dr->td_disk =  device_add_child(sc->twe_dev, NULL, -1);
    if (dr->td_disk == NULL) {
	mtx_unlock(&Giant);
	twe_printf(sc, "Cannot add unit\n");
	return (EIO);
    }
    device_set_ivars(dr->td_disk, dr);

    /* 
     * XXX It would make sense to test the online/initialising bits, but they seem to be
     * always set...
     */
    sprintf(buf, "Unit %d, %s, %s",
	    dr->td_twe_unit,
	    twe_describe_code(twe_table_unittype, dr->td_type),
	    twe_describe_code(twe_table_unitstate, dr->td_state & TWE_PARAM_UNITSTATUS_MASK));
    device_set_desc_copy(dr->td_disk, buf);

    error = device_probe_and_attach(dr->td_disk);
    mtx_unlock(&Giant);
    if (error != 0) {
	twe_printf(sc, "Cannot attach unit to controller. error = %d\n", error);
	return (EIO);
    }
    return (0);
}

/********************************************************************************
 * Detach the specified unit if it exsists
 *
 * This is called from twe_del_unit.
 */
int
twe_detach_drive(struct twe_softc *sc, int unit)
{
    int error = 0;

    TWE_CONFIG_ASSERT_LOCKED(sc);
    mtx_lock(&Giant);
    error = device_delete_child(sc->twe_dev, sc->twe_drive[unit].td_disk);
    mtx_unlock(&Giant);
    if (error != 0) {
	twe_printf(sc, "failed to delete unit %d\n", unit);
	return(error);
    }
    bzero(&sc->twe_drive[unit], sizeof(sc->twe_drive[unit]));
    return(error);
}

/********************************************************************************
 * Clear a PCI parity error.
 */
void
twe_clear_pci_parity_error(struct twe_softc *sc)
{
    TWE_CONTROL(sc, TWE_CONTROL_CLEAR_PARITY_ERROR);
    pci_write_config(sc->twe_dev, PCIR_STATUS, TWE_PCI_CLEAR_PARITY_ERROR, 2);
}

/********************************************************************************
 * Clear a PCI abort.
 */
void
twe_clear_pci_abort(struct twe_softc *sc)
{
    TWE_CONTROL(sc, TWE_CONTROL_CLEAR_PCI_ABORT);
    pci_write_config(sc->twe_dev, PCIR_STATUS, TWE_PCI_CLEAR_PCI_ABORT, 2);
}

/********************************************************************************
 ********************************************************************************
                                                                      Disk device
 ********************************************************************************
 ********************************************************************************/

/*
 * Disk device softc
 */
struct twed_softc
{
    device_t		twed_dev;
    struct twe_softc	*twed_controller;	/* parent device softc */
    struct twe_drive	*twed_drive;		/* drive data in parent softc */
    struct disk		*twed_disk;		/* generic disk handle */
};

/*
 * Disk device bus interface
 */
static int twed_probe(device_t dev);
static int twed_attach(device_t dev);
static int twed_detach(device_t dev);

static device_method_t twed_methods[] = {
    DEVMETHOD(device_probe,	twed_probe),
    DEVMETHOD(device_attach,	twed_attach),
    DEVMETHOD(device_detach,	twed_detach),
    { 0, 0 }
};

static driver_t twed_driver = {
    "twed",
    twed_methods,
    sizeof(struct twed_softc)
};

static devclass_t	twed_devclass;
DRIVER_MODULE(twed, twe, twed_driver, twed_devclass, 0, 0);

/*
 * Disk device control interface.
 */

/********************************************************************************
 * Handle open from generic layer.
 *
 * Note that this is typically only called by the diskslice code, and not
 * for opens on subdevices (eg. slices, partitions).
 */
static int
twed_open(struct disk *dp)
{
    struct twed_softc	*sc = (struct twed_softc *)dp->d_drv1;

    debug_called(4);
	
    if (sc == NULL)
	return (ENXIO);

    /* check that the controller is up and running */
    if (sc->twed_controller->twe_state & TWE_STATE_SHUTDOWN)
	return(ENXIO);

    return (0);
}

/********************************************************************************
 * Handle an I/O request.
 */
static void
twed_strategy(struct bio *bp)
{
    struct twed_softc	*sc = bp->bio_disk->d_drv1;

    debug_called(4);

    bp->bio_driver1 = &sc->twed_drive->td_twe_unit;
    TWED_BIO_IN;

    /* bogus disk? */
    if (sc == NULL || sc->twed_drive->td_disk == NULL) {
	bp->bio_error = EINVAL;
	bp->bio_flags |= BIO_ERROR;
	printf("twe: bio for invalid disk!\n");
	biodone(bp);
	TWED_BIO_OUT;
	return;
    }

    /* queue the bio on the controller */
    TWE_IO_LOCK(sc->twed_controller);
    twe_enqueue_bio(sc->twed_controller, bp);

    /* poke the controller to start I/O */
    twe_startio(sc->twed_controller);
    TWE_IO_UNLOCK(sc->twed_controller);
    return;
}

/********************************************************************************
 * System crashdump support
 */
static int
twed_dump(void *arg, void *virtual, vm_offset_t physical, off_t offset, size_t length)
{
    struct twed_softc	*twed_sc;
    struct twe_softc	*twe_sc;
    int			error;
    struct disk		*dp;

    dp = arg;
    twed_sc = (struct twed_softc *)dp->d_drv1;
    if (twed_sc == NULL)
	return(ENXIO);
    twe_sc  = (struct twe_softc *)twed_sc->twed_controller;

    if (length > 0) {
	if ((error = twe_dump_blocks(twe_sc, twed_sc->twed_drive->td_twe_unit, offset / TWE_BLOCK_SIZE, virtual, length / TWE_BLOCK_SIZE)) != 0)
	    return(error);
    }
    return(0);
}

/********************************************************************************
 * Handle completion of an I/O request.
 */
void
twed_intr(struct bio *bp)
{
    debug_called(4);

    /* if no error, transfer completed */
    if (!(bp->bio_flags & BIO_ERROR))
	bp->bio_resid = 0;

    biodone(bp);
    TWED_BIO_OUT;
}

/********************************************************************************
 * Default probe stub.
 */
static int
twed_probe(device_t dev)
{
    return (0);
}

/********************************************************************************
 * Attach a unit to the controller.
 */
static int
twed_attach(device_t dev)
{
    struct twed_softc	*sc;
    device_t		parent;
    
    debug_called(4);

    /* initialise our softc */
    sc = device_get_softc(dev);
    parent = device_get_parent(dev);
    sc->twed_controller = (struct twe_softc *)device_get_softc(parent);
    sc->twed_drive = device_get_ivars(dev);
    sc->twed_dev = dev;

    /* report the drive */
    twed_printf(sc, "%uMB (%u sectors)\n",
		sc->twed_drive->td_size / ((1024 * 1024) / TWE_BLOCK_SIZE),
		sc->twed_drive->td_size);
    
    /* attach a generic disk device to ourselves */

    sc->twed_drive->td_sys_unit = device_get_unit(dev);

    sc->twed_disk = disk_alloc();
    sc->twed_disk->d_open = twed_open;
    sc->twed_disk->d_strategy = twed_strategy;
    sc->twed_disk->d_dump = (dumper_t *)twed_dump;
    sc->twed_disk->d_name = "twed";
    sc->twed_disk->d_drv1 = sc;
    sc->twed_disk->d_maxsize = (TWE_MAX_SGL_LENGTH - 1) * PAGE_SIZE;
    sc->twed_disk->d_sectorsize = TWE_BLOCK_SIZE;
    sc->twed_disk->d_mediasize = TWE_BLOCK_SIZE * (off_t)sc->twed_drive->td_size;
    if (sc->twed_drive->td_type == TWE_UD_CONFIG_RAID0 ||
	sc->twed_drive->td_type == TWE_UD_CONFIG_RAID5 ||
	sc->twed_drive->td_type == TWE_UD_CONFIG_RAID10) {
	    sc->twed_disk->d_stripesize =
		TWE_BLOCK_SIZE << sc->twed_drive->td_stripe;
	    sc->twed_disk->d_stripeoffset = 0;
    }
    sc->twed_disk->d_fwsectors = sc->twed_drive->td_sectors;
    sc->twed_disk->d_fwheads = sc->twed_drive->td_heads;
    sc->twed_disk->d_unit = sc->twed_drive->td_sys_unit;

    disk_create(sc->twed_disk, DISK_VERSION);

    /* set the maximum I/O size to the theoretical maximum allowed by the S/G list size */

    return (0);
}

/********************************************************************************
 * Disconnect ourselves from the system.
 */
static int
twed_detach(device_t dev)
{
    struct twed_softc *sc = (struct twed_softc *)device_get_softc(dev);

    debug_called(4);

    if (sc->twed_disk->d_flags & DISKFLAG_OPEN)
	return(EBUSY);

    disk_destroy(sc->twed_disk);

    return(0);
}

/********************************************************************************
 ********************************************************************************
                                                                             Misc
 ********************************************************************************
 ********************************************************************************/

/********************************************************************************
 * Allocate a command buffer
 */
static MALLOC_DEFINE(TWE_MALLOC_CLASS, "twe_commands", "twe commands");

struct twe_request *
twe_allocate_request(struct twe_softc *sc, int tag)
{
    struct twe_request	*tr;

    tr = malloc(sizeof(struct twe_request), TWE_MALLOC_CLASS, M_WAITOK | M_ZERO);
    tr->tr_sc = sc;
    tr->tr_tag = tag;
    if (bus_dmamap_create(sc->twe_buffer_dmat, 0, &tr->tr_dmamap)) {
	twe_free_request(tr);
	twe_printf(sc, "unable to allocate dmamap for tag %d\n", tag);
	return(NULL);
    }    
    return(tr);
}

/********************************************************************************
 * Permanently discard a command buffer.
 */
void
twe_free_request(struct twe_request *tr) 
{
    struct twe_softc	*sc = tr->tr_sc;
    
    debug_called(4);

    bus_dmamap_destroy(sc->twe_buffer_dmat, tr->tr_dmamap);
    free(tr, TWE_MALLOC_CLASS);
}

/********************************************************************************
 * Map/unmap (tr)'s command and data in the controller's addressable space.
 *
 * These routines ensure that the data which the controller is going to try to
 * access is actually visible to the controller, in a machine-independant 
 * fashion.  Due to a hardware limitation, I/O buffers must be 512-byte aligned
 * and we take care of that here as well.
 */
static void
twe_fillin_sgl(TWE_SG_Entry *sgl, bus_dma_segment_t *segs, int nsegments, int max_sgl)
{
    int i;

    for (i = 0; i < nsegments; i++) {
	sgl[i].address = segs[i].ds_addr;
	sgl[i].length = segs[i].ds_len;
    }
    for (; i < max_sgl; i++) {				/* XXX necessary? */
	sgl[i].address = 0;
	sgl[i].length = 0;
    }
}
		
static void
twe_setup_data_dmamap(void *arg, bus_dma_segment_t *segs, int nsegments, int error)
{
    struct twe_request	*tr = (struct twe_request *)arg;
    struct twe_softc	*sc = tr->tr_sc;
    TWE_Command		*cmd = TWE_FIND_COMMAND(tr);

    debug_called(4);

    if (tr->tr_flags & TWE_CMD_MAPPED)
	panic("already mapped command");

    tr->tr_flags |= TWE_CMD_MAPPED;

    if (tr->tr_flags & TWE_CMD_IN_PROGRESS)
	sc->twe_state &= ~TWE_STATE_FRZN;
    /* save base of first segment in command (applicable if there only one segment) */
    tr->tr_dataphys = segs[0].ds_addr;

    /* correct command size for s/g list size */
    cmd->generic.size += 2 * nsegments;

    /*
     * Due to the fact that parameter and I/O commands have the scatter/gather list in
     * different places, we need to determine which sort of command this actually is
     * before we can populate it correctly.
     */
    switch(cmd->generic.opcode) {
    case TWE_OP_GET_PARAM:
    case TWE_OP_SET_PARAM:
	cmd->generic.sgl_offset = 2;
	twe_fillin_sgl(&cmd->param.sgl[0], segs, nsegments, TWE_MAX_SGL_LENGTH);
	break;
    case TWE_OP_READ:
    case TWE_OP_WRITE:
	cmd->generic.sgl_offset = 3;
	twe_fillin_sgl(&cmd->io.sgl[0], segs, nsegments, TWE_MAX_SGL_LENGTH);
	break;
    case TWE_OP_ATA_PASSTHROUGH:
	cmd->generic.sgl_offset = 5;
	twe_fillin_sgl(&cmd->ata.sgl[0], segs, nsegments, TWE_MAX_ATA_SGL_LENGTH);
	break;
    default:
	/*
	 * Fall back to what the linux driver does.
	 * Do this because the API may send an opcode
	 * the driver knows nothing about and this will
	 * at least stop PCIABRT's from hosing us.
	 */
	switch (cmd->generic.sgl_offset) {
	case 2:
	    twe_fillin_sgl(&cmd->param.sgl[0], segs, nsegments, TWE_MAX_SGL_LENGTH);
	    break;
	case 3:
	    twe_fillin_sgl(&cmd->io.sgl[0], segs, nsegments, TWE_MAX_SGL_LENGTH);
	    break;
	case 5:
	    twe_fillin_sgl(&cmd->ata.sgl[0], segs, nsegments, TWE_MAX_ATA_SGL_LENGTH);
	    break;
	}
    }

    if (tr->tr_flags & TWE_CMD_DATAIN) {
	if (tr->tr_flags & TWE_CMD_IMMEDIATE) {
	    bus_dmamap_sync(sc->twe_immediate_dmat, sc->twe_immediate_map,
			    BUS_DMASYNC_PREREAD);
	} else {
	    bus_dmamap_sync(sc->twe_buffer_dmat, tr->tr_dmamap,
			    BUS_DMASYNC_PREREAD);
	}
    }

    if (tr->tr_flags & TWE_CMD_DATAOUT) {
	/*
	 * if we're using an alignment buffer, and we're writing data
	 * copy the real data out
	 */
	if (tr->tr_flags & TWE_CMD_ALIGNBUF)
	    bcopy(tr->tr_realdata, tr->tr_data, tr->tr_length);

	if (tr->tr_flags & TWE_CMD_IMMEDIATE) {
	    bus_dmamap_sync(sc->twe_immediate_dmat, sc->twe_immediate_map,
			    BUS_DMASYNC_PREWRITE);
	} else {
	    bus_dmamap_sync(sc->twe_buffer_dmat, tr->tr_dmamap,
			    BUS_DMASYNC_PREWRITE);
	}
    }

    if (twe_start(tr) == EBUSY) {
	tr->tr_sc->twe_state |= TWE_STATE_CTLR_BUSY;
	twe_requeue_ready(tr);
    }
}

static void
twe_setup_request_dmamap(void *arg, bus_dma_segment_t *segs, int nsegments, int error)
{
    struct twe_softc	*sc = (struct twe_softc *)arg;

    debug_called(4);

    /* command can't cross a page boundary */
    sc->twe_cmdphys = segs[0].ds_addr;
}

int
twe_map_request(struct twe_request *tr)
{
    struct twe_softc	*sc = tr->tr_sc;
    int			error = 0;

    debug_called(4);

    if (!dumping)
	TWE_IO_ASSERT_LOCKED(sc);
    if (sc->twe_state & (TWE_STATE_CTLR_BUSY | TWE_STATE_FRZN)) {
	twe_requeue_ready(tr);
	return (EBUSY);
    }

    bus_dmamap_sync(sc->twe_cmd_dmat, sc->twe_cmdmap, BUS_DMASYNC_PREWRITE);

    /*
     * If the command involves data, map that too.
     */
    if (tr->tr_data != NULL && ((tr->tr_flags & TWE_CMD_MAPPED) == 0)) {

	/* 
	 * Data must be 64-byte aligned; allocate a fixup buffer if it's not.
	 */
	if (((vm_offset_t)tr->tr_data % TWE_ALIGNMENT) != 0) {
	    tr->tr_realdata = tr->tr_data;				/* save pointer to 'real' data */
	    tr->tr_flags |= TWE_CMD_ALIGNBUF;
	    tr->tr_data = malloc(tr->tr_length, TWE_MALLOC_CLASS, M_NOWAIT);
	    if (tr->tr_data == NULL) {
		twe_printf(sc, "%s: malloc failed\n", __func__);
		tr->tr_data = tr->tr_realdata; /* restore original data pointer */
		return(ENOMEM);
	    }
	}
	
	/*
	 * Map the data buffer into bus space and build the s/g list.
	 */
	if (tr->tr_flags & TWE_CMD_IMMEDIATE) {
	    error = bus_dmamap_load(sc->twe_immediate_dmat, sc->twe_immediate_map, sc->twe_immediate,
			    tr->tr_length, twe_setup_data_dmamap, tr, BUS_DMA_NOWAIT);
	} else {
	    error = bus_dmamap_load(sc->twe_buffer_dmat, tr->tr_dmamap, tr->tr_data, tr->tr_length, 
				    twe_setup_data_dmamap, tr, 0);
	}
	if (error == EINPROGRESS) {
	    tr->tr_flags |= TWE_CMD_IN_PROGRESS;
	    sc->twe_state |= TWE_STATE_FRZN;
	    error = 0;
	}
    } else
	if ((error = twe_start(tr)) == EBUSY) {
	    sc->twe_state |= TWE_STATE_CTLR_BUSY;
	    twe_requeue_ready(tr);
	}

    return(error);
}

void
twe_unmap_request(struct twe_request *tr)
{
    struct twe_softc	*sc = tr->tr_sc;

    debug_called(4);

    if (!dumping)
	TWE_IO_ASSERT_LOCKED(sc);
    bus_dmamap_sync(sc->twe_cmd_dmat, sc->twe_cmdmap, BUS_DMASYNC_POSTWRITE);

    /*
     * If the command involved data, unmap that too.
     */
    if (tr->tr_data != NULL) {
	if (tr->tr_flags & TWE_CMD_DATAIN) {
	    if (tr->tr_flags & TWE_CMD_IMMEDIATE) {
		bus_dmamap_sync(sc->twe_immediate_dmat, sc->twe_immediate_map,
				BUS_DMASYNC_POSTREAD);
	    } else {
		bus_dmamap_sync(sc->twe_buffer_dmat, tr->tr_dmamap,
				BUS_DMASYNC_POSTREAD);
	    }

	    /* if we're using an alignment buffer, and we're reading data, copy the real data in */
	    if (tr->tr_flags & TWE_CMD_ALIGNBUF)
		bcopy(tr->tr_data, tr->tr_realdata, tr->tr_length);
	}
	if (tr->tr_flags & TWE_CMD_DATAOUT) {
	    if (tr->tr_flags & TWE_CMD_IMMEDIATE) {
		bus_dmamap_sync(sc->twe_immediate_dmat, sc->twe_immediate_map,
				BUS_DMASYNC_POSTWRITE);
	    } else {
		bus_dmamap_sync(sc->twe_buffer_dmat, tr->tr_dmamap,
				BUS_DMASYNC_POSTWRITE);
	    }
	}

	if (tr->tr_flags & TWE_CMD_IMMEDIATE) {
	    bus_dmamap_unload(sc->twe_immediate_dmat, sc->twe_immediate_map);
	} else {
	    bus_dmamap_unload(sc->twe_buffer_dmat, tr->tr_dmamap); 
	}
    }

    /* free alignment buffer if it was used */
    if (tr->tr_flags & TWE_CMD_ALIGNBUF) {
	free(tr->tr_data, TWE_MALLOC_CLASS);
	tr->tr_data = tr->tr_realdata;		/* restore 'real' data pointer */
    }
}

#ifdef TWE_DEBUG
void twe_report(void);
/********************************************************************************
 * Print current controller status, call from DDB.
 */
void
twe_report(void)
{
    struct twe_softc	*sc;
    int			i;

    for (i = 0; (sc = devclass_get_softc(twe_devclass, i)) != NULL; i++)
	twe_print_controller(sc);
    printf("twed: total bio count in %u  out %u\n", twed_bio_in, twed_bio_out);
}
#endif
