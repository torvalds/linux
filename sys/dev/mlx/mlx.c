/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Michael Smith
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

/*
 * Driver for the Mylex DAC960 family of RAID controllers.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/kernel.h>
#include <sys/sx.h>

#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/stat.h>

#include <machine/resource.h>
#include <machine/bus.h>
#include <machine/clock.h>
#include <sys/rman.h>

#include <geom/geom_disk.h>

#include <dev/mlx/mlxio.h>
#include <dev/mlx/mlxvar.h>
#include <dev/mlx/mlxreg.h>

static struct cdevsw mlx_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	mlx_open,
	.d_close =	mlx_close,
	.d_ioctl =	mlx_ioctl,
	.d_name =	"mlx",
};

devclass_t	mlx_devclass;

/*
 * Per-interface accessor methods
 */
static int			mlx_v3_tryqueue(struct mlx_softc *sc, struct mlx_command *mc);
static int			mlx_v3_findcomplete(struct mlx_softc *sc, u_int8_t *slot, u_int16_t *status);
static void			mlx_v3_intaction(struct mlx_softc *sc, int action);
static int			mlx_v3_fw_handshake(struct mlx_softc *sc, int *error, int *param1, int *param2, int first);

static int			mlx_v4_tryqueue(struct mlx_softc *sc, struct mlx_command *mc);
static int			mlx_v4_findcomplete(struct mlx_softc *sc, u_int8_t *slot, u_int16_t *status);
static void			mlx_v4_intaction(struct mlx_softc *sc, int action);
static int			mlx_v4_fw_handshake(struct mlx_softc *sc, int *error, int *param1, int *param2, int first);

static int			mlx_v5_tryqueue(struct mlx_softc *sc, struct mlx_command *mc);
static int			mlx_v5_findcomplete(struct mlx_softc *sc, u_int8_t *slot, u_int16_t *status);
static void			mlx_v5_intaction(struct mlx_softc *sc, int action);
static int			mlx_v5_fw_handshake(struct mlx_softc *sc, int *error, int *param1, int *param2, int first);

/*
 * Status monitoring
 */
static void			mlx_periodic(void *data);
static void			mlx_periodic_enquiry(struct mlx_command *mc);
static void			mlx_periodic_eventlog_poll(struct mlx_softc *sc);
static void			mlx_periodic_eventlog_respond(struct mlx_command *mc);
static void			mlx_periodic_rebuild(struct mlx_command *mc);

/*
 * Channel Pause
 */
static void			mlx_pause_action(struct mlx_softc *sc);
static void			mlx_pause_done(struct mlx_command *mc);

/*
 * Command submission.
 */
static void			*mlx_enquire(struct mlx_softc *sc, int command, size_t bufsize, 
					     void (*complete)(struct mlx_command *mc));
static int			mlx_flush(struct mlx_softc *sc);
static int			mlx_check(struct mlx_softc *sc, int drive);
static int			mlx_rebuild(struct mlx_softc *sc, int channel, int target);
static int			mlx_wait_command(struct mlx_command *mc);
static int			mlx_poll_command(struct mlx_command *mc);
void				mlx_startio_cb(void *arg,
					       bus_dma_segment_t *segs,
					       int nsegments, int error);
static void			mlx_startio(struct mlx_softc *sc);
static void			mlx_completeio(struct mlx_command *mc);
static int			mlx_user_command(struct mlx_softc *sc,
						 struct mlx_usercommand *mu);
void				mlx_user_cb(void *arg, bus_dma_segment_t *segs,
					    int nsegments, int error);

/*
 * Command buffer allocation.
 */
static struct mlx_command	*mlx_alloccmd(struct mlx_softc *sc);
static void			mlx_releasecmd(struct mlx_command *mc);
static void			mlx_freecmd(struct mlx_command *mc);

/*
 * Command management.
 */
static int			mlx_getslot(struct mlx_command *mc);
static void			mlx_setup_dmamap(struct mlx_command *mc,
						 bus_dma_segment_t *segs,
						 int nsegments, int error);
static void			mlx_unmapcmd(struct mlx_command *mc);
static int			mlx_shutdown_locked(struct mlx_softc *sc);
static int			mlx_start(struct mlx_command *mc);
static int			mlx_done(struct mlx_softc *sc, int startio);
static void			mlx_complete(struct mlx_softc *sc);

/*
 * Debugging.
 */
static char			*mlx_diagnose_command(struct mlx_command *mc);
static void			mlx_describe_controller(struct mlx_softc *sc);
static int			mlx_fw_message(struct mlx_softc *sc, int status, int param1, int param2);

/*
 * Utility functions.
 */
static struct mlx_sysdrive	*mlx_findunit(struct mlx_softc *sc, int unit);

/********************************************************************************
 ********************************************************************************
                                                                Public Interfaces
 ********************************************************************************
 ********************************************************************************/

/********************************************************************************
 * Free all of the resources associated with (sc)
 *
 * Should not be called if the controller is active.
 */
void
mlx_free(struct mlx_softc *sc)
{
    struct mlx_command	*mc;

    debug_called(1);

    /* destroy control device */
    if (sc->mlx_dev_t != NULL)
	destroy_dev(sc->mlx_dev_t);

    if (sc->mlx_intr)
	bus_teardown_intr(sc->mlx_dev, sc->mlx_irq, sc->mlx_intr);

    /* cancel status timeout */
    MLX_IO_LOCK(sc);
    callout_stop(&sc->mlx_timeout);

    /* throw away any command buffers */
    while ((mc = TAILQ_FIRST(&sc->mlx_freecmds)) != NULL) {
	TAILQ_REMOVE(&sc->mlx_freecmds, mc, mc_link);
	mlx_freecmd(mc);
    }
    MLX_IO_UNLOCK(sc);
    callout_drain(&sc->mlx_timeout);

    /* destroy data-transfer DMA tag */
    if (sc->mlx_buffer_dmat)
	bus_dma_tag_destroy(sc->mlx_buffer_dmat);

    /* free and destroy DMA memory and tag for s/g lists */
    if (sc->mlx_sgbusaddr)
	bus_dmamap_unload(sc->mlx_sg_dmat, sc->mlx_sg_dmamap);
    if (sc->mlx_sgtable)
	bus_dmamem_free(sc->mlx_sg_dmat, sc->mlx_sgtable, sc->mlx_sg_dmamap);
    if (sc->mlx_sg_dmat)
	bus_dma_tag_destroy(sc->mlx_sg_dmat);

    /* disconnect the interrupt handler */
    if (sc->mlx_irq != NULL)
	bus_release_resource(sc->mlx_dev, SYS_RES_IRQ, 0, sc->mlx_irq);

    /* destroy the parent DMA tag */
    if (sc->mlx_parent_dmat)
	bus_dma_tag_destroy(sc->mlx_parent_dmat);

    /* release the register window mapping */
    if (sc->mlx_mem != NULL)
	bus_release_resource(sc->mlx_dev, sc->mlx_mem_type, sc->mlx_mem_rid, sc->mlx_mem);

    /* free controller enquiry data */
    if (sc->mlx_enq2 != NULL)
	free(sc->mlx_enq2, M_DEVBUF);

    sx_destroy(&sc->mlx_config_lock);
    mtx_destroy(&sc->mlx_io_lock);
}

/********************************************************************************
 * Map the scatter/gather table into bus space
 */
static void
mlx_dma_map_sg(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
    struct mlx_softc	*sc = (struct mlx_softc *)arg;

    debug_called(1);

    /* save base of s/g table's address in bus space */
    sc->mlx_sgbusaddr = segs->ds_addr;
}

static int
mlx_sglist_map(struct mlx_softc *sc)
{
    size_t	segsize;
    int		error, ncmd;

    debug_called(1);

    /* destroy any existing mappings */
    if (sc->mlx_sgbusaddr)
	bus_dmamap_unload(sc->mlx_sg_dmat, sc->mlx_sg_dmamap);
    if (sc->mlx_sgtable)
	bus_dmamem_free(sc->mlx_sg_dmat, sc->mlx_sgtable, sc->mlx_sg_dmamap);
    if (sc->mlx_sg_dmat)
	bus_dma_tag_destroy(sc->mlx_sg_dmat);
    sc->mlx_sgbusaddr = 0;
    sc->mlx_sgtable = NULL;
    sc->mlx_sg_dmat = NULL;

    /*
     * Create a single tag describing a region large enough to hold all of
     * the s/g lists we will need.  If we're called early on, we don't know how
     * many commands we're going to be asked to support, so only allocate enough
     * for a couple.
     */
    if (sc->mlx_enq2 == NULL) {
	ncmd = 2;
    } else {
	ncmd = sc->mlx_enq2->me_max_commands;
    }
    segsize = sizeof(struct mlx_sgentry) * MLX_NSEG * ncmd;
    error = bus_dma_tag_create(sc->mlx_parent_dmat, 	/* parent */
			       1, 0, 			/* alignment,boundary */
			       BUS_SPACE_MAXADDR,	/* lowaddr */
			       BUS_SPACE_MAXADDR, 	/* highaddr */
			       NULL, NULL, 		/* filter, filterarg */
			       segsize, 1,		/* maxsize, nsegments */
			       BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
			       0,			/* flags */
			       NULL, NULL,		/* lockfunc, lockarg */
			       &sc->mlx_sg_dmat);
    if (error != 0) {
	device_printf(sc->mlx_dev, "can't allocate scatter/gather DMA tag\n");
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
     */
    error = bus_dmamem_alloc(sc->mlx_sg_dmat, (void **)&sc->mlx_sgtable,
			     BUS_DMA_NOWAIT, &sc->mlx_sg_dmamap);
    if (error) {
	device_printf(sc->mlx_dev, "can't allocate s/g table\n");
	return(ENOMEM);
    }
    (void)bus_dmamap_load(sc->mlx_sg_dmat, sc->mlx_sg_dmamap, sc->mlx_sgtable,
			  segsize, mlx_dma_map_sg, sc, 0);
    return(0);
}

/********************************************************************************
 * Initialise the controller and softc
 */
int
mlx_attach(struct mlx_softc *sc)
{
    struct mlx_enquiry_old	*meo;
    int				rid, error, fwminor, hscode, hserror, hsparam1, hsparam2, hsmsg;

    debug_called(1);

    /*
     * Initialise per-controller queues.
     */
    TAILQ_INIT(&sc->mlx_work);
    TAILQ_INIT(&sc->mlx_freecmds);
    bioq_init(&sc->mlx_bioq);

    /* 
     * Select accessor methods based on controller interface type.
     */
    switch(sc->mlx_iftype) {
    case MLX_IFTYPE_2:
    case MLX_IFTYPE_3:
	sc->mlx_tryqueue	= mlx_v3_tryqueue;
	sc->mlx_findcomplete	= mlx_v3_findcomplete;
	sc->mlx_intaction	= mlx_v3_intaction;
	sc->mlx_fw_handshake	= mlx_v3_fw_handshake;
	break;
    case MLX_IFTYPE_4:
	sc->mlx_tryqueue	= mlx_v4_tryqueue;
	sc->mlx_findcomplete	= mlx_v4_findcomplete;
	sc->mlx_intaction	= mlx_v4_intaction;
	sc->mlx_fw_handshake	= mlx_v4_fw_handshake;
	break;
    case MLX_IFTYPE_5:
	sc->mlx_tryqueue	= mlx_v5_tryqueue;
	sc->mlx_findcomplete	= mlx_v5_findcomplete;
	sc->mlx_intaction	= mlx_v5_intaction;
	sc->mlx_fw_handshake	= mlx_v5_fw_handshake;
	break;
    default:
	return(ENXIO);		/* should never happen */
    }

    /* disable interrupts before we start talking to the controller */
    MLX_IO_LOCK(sc);
    sc->mlx_intaction(sc, MLX_INTACTION_DISABLE);
    MLX_IO_UNLOCK(sc);

    /* 
     * Wait for the controller to come ready, handshake with the firmware if required.
     * This is typically only necessary on platforms where the controller BIOS does not
     * run.
     */
    hsmsg = 0;
    DELAY(1000);
    while ((hscode = sc->mlx_fw_handshake(sc, &hserror, &hsparam1, &hsparam2,
	hsmsg == 0)) != 0) {
	/* report first time around... */
	if (hsmsg == 0) {
	    device_printf(sc->mlx_dev, "controller initialisation in progress...\n");
	    hsmsg = 1;
	}
	/* did we get a real message? */
	if (hscode == 2) {
	    hscode = mlx_fw_message(sc, hserror, hsparam1, hsparam2);
	    /* fatal initialisation error? */
	    if (hscode != 0) {
		return(ENXIO);
	    }
	}
    }
    if (hsmsg == 1)
	device_printf(sc->mlx_dev, "initialisation complete.\n");

    /* 
     * Allocate and connect our interrupt.
     */
    rid = 0;
    sc->mlx_irq = bus_alloc_resource_any(sc->mlx_dev, SYS_RES_IRQ, &rid,
        RF_SHAREABLE | RF_ACTIVE);
    if (sc->mlx_irq == NULL) {
	device_printf(sc->mlx_dev, "can't allocate interrupt\n");
	return(ENXIO);
    }
    error = bus_setup_intr(sc->mlx_dev, sc->mlx_irq, INTR_TYPE_BIO |
	INTR_ENTROPY | INTR_MPSAFE, NULL, mlx_intr, sc, &sc->mlx_intr);
    if (error) {
	device_printf(sc->mlx_dev, "can't set up interrupt\n");
	return(ENXIO);
    }

    /*
     * Create DMA tag for mapping buffers into controller-addressable space.
     */
    error = bus_dma_tag_create(sc->mlx_parent_dmat, 	/* parent */
			       1, 0, 			/* align, boundary */
			       BUS_SPACE_MAXADDR,	/* lowaddr */
			       BUS_SPACE_MAXADDR, 	/* highaddr */
			       NULL, NULL, 		/* filter, filterarg */
			       MLX_MAXPHYS,		/* maxsize */
			       MLX_NSEG,		/* nsegments */
			       BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
			       0,			/* flags */
			       busdma_lock_mutex,	/* lockfunc */
			       &sc->mlx_io_lock,	/* lockarg */
			       &sc->mlx_buffer_dmat);
    if (error != 0) {
	device_printf(sc->mlx_dev, "can't allocate buffer DMA tag\n");
	return(ENOMEM);
    }

    /*
     * Create some initial scatter/gather mappings so we can run the probe
     * commands.
     */
    error = mlx_sglist_map(sc);
    if (error != 0) {
	device_printf(sc->mlx_dev, "can't make initial s/g list mapping\n");
	return(error);
    }

    /*
     * We don't (yet) know where the event log is up to.
     */
    sc->mlx_currevent = -1;

    /* 
     * Obtain controller feature information
     */
    MLX_IO_LOCK(sc);
    if ((sc->mlx_enq2 = mlx_enquire(sc, MLX_CMD_ENQUIRY2, sizeof(struct mlx_enquiry2), NULL)) == NULL) {
	MLX_IO_UNLOCK(sc);
	device_printf(sc->mlx_dev, "ENQUIRY2 failed\n");
	return(ENXIO);
    }

    /*
     * Do quirk/feature related things.
     */
    fwminor = (sc->mlx_enq2->me_firmware_id >> 8) & 0xff;
    switch(sc->mlx_iftype) {
    case MLX_IFTYPE_2:
	/* These controllers don't report the firmware version in the ENQUIRY2 response */
	if ((meo = mlx_enquire(sc, MLX_CMD_ENQUIRY_OLD, sizeof(struct mlx_enquiry_old), NULL)) == NULL) {
	    MLX_IO_UNLOCK(sc);
	    device_printf(sc->mlx_dev, "ENQUIRY_OLD failed\n");
	    return(ENXIO);
	}
	sc->mlx_enq2->me_firmware_id = ('0' << 24) | (0 << 16) | (meo->me_fwminor << 8) | meo->me_fwmajor;
	
	/* XXX require 2.42 or better (PCI) */
	if (meo->me_fwminor < 42) {
	    device_printf(sc->mlx_dev, " *** WARNING *** This firmware revision is not recommended\n");
	    device_printf(sc->mlx_dev, " *** WARNING *** Use revision 2.42 or later\n");
	}
	free(meo, M_DEVBUF);
	break;
    case MLX_IFTYPE_3:
	/* XXX certify 3.52? */
	if (fwminor < 51) {
	    device_printf(sc->mlx_dev, " *** WARNING *** This firmware revision is not recommended\n");
	    device_printf(sc->mlx_dev, " *** WARNING *** Use revision 3.51 or later\n");
	}
	break;
    case MLX_IFTYPE_4:
	/* XXX certify firmware versions? */
	if (fwminor < 6) {
	    device_printf(sc->mlx_dev, " *** WARNING *** This firmware revision is not recommended\n");
	    device_printf(sc->mlx_dev, " *** WARNING *** Use revision 4.06 or later\n");
	}
	break;
    case MLX_IFTYPE_5:
	if (fwminor < 7) {
	    device_printf(sc->mlx_dev, " *** WARNING *** This firmware revision is not recommended\n");
	    device_printf(sc->mlx_dev, " *** WARNING *** Use revision 5.07 or later\n");
	}
	break;
    default:
	MLX_IO_UNLOCK(sc);
	return(ENXIO);		/* should never happen */
    }
    MLX_IO_UNLOCK(sc);

    /*
     * Create the final scatter/gather mappings now that we have characterised the controller.
     */
    error = mlx_sglist_map(sc);
    if (error != 0) {
	device_printf(sc->mlx_dev, "can't make final s/g list mapping\n");
	return(error);
    }

    /*
     * No user-requested background operation is in progress.
     */
    sc->mlx_background = 0;
    sc->mlx_rebuildstat.rs_code = MLX_REBUILDSTAT_IDLE;

    /*
     * Create the control device.
     */
    sc->mlx_dev_t = make_dev(&mlx_cdevsw, 0, UID_ROOT, GID_OPERATOR, 
			     S_IRUSR | S_IWUSR, "mlx%d", device_get_unit(sc->mlx_dev));
    sc->mlx_dev_t->si_drv1 = sc;

    /*
     * Start the timeout routine.
     */
    callout_reset(&sc->mlx_timeout, hz, mlx_periodic, sc);

    /* print a little information about the controller */
    mlx_describe_controller(sc);

    return(0);
}

/********************************************************************************
 * Locate disk resources and attach children to them.
 */
void
mlx_startup(struct mlx_softc *sc)
{
    struct mlx_enq_sys_drive	*mes;
    struct mlx_sysdrive		*dr;
    int				i, error;

    debug_called(1);
    
    /*
     * Scan all the system drives and attach children for those that
     * don't currently have them.
     */
    MLX_IO_LOCK(sc);
    mes = mlx_enquire(sc, MLX_CMD_ENQSYSDRIVE, sizeof(*mes) * MLX_MAXDRIVES, NULL);
    MLX_IO_UNLOCK(sc);
    if (mes == NULL) {
	device_printf(sc->mlx_dev, "error fetching drive status\n");
	return;
    }
    
    /* iterate over drives returned */
    MLX_CONFIG_LOCK(sc);
    for (i = 0, dr = &sc->mlx_sysdrive[0];
	 (i < MLX_MAXDRIVES) && (mes[i].sd_size != 0xffffffff);
	 i++, dr++) {
	/* are we already attached to this drive? */
    	if (dr->ms_disk == 0) {
	    /* pick up drive information */
	    dr->ms_size = mes[i].sd_size;
	    dr->ms_raidlevel = mes[i].sd_raidlevel & 0xf;
	    dr->ms_state = mes[i].sd_state;

	    /* generate geometry information */
	    if (sc->mlx_geom == MLX_GEOM_128_32) {
		dr->ms_heads = 128;
		dr->ms_sectors = 32;
		dr->ms_cylinders = dr->ms_size / (128 * 32);
	    } else {        /* MLX_GEOM_255/63 */
		dr->ms_heads = 255;
		dr->ms_sectors = 63;
		dr->ms_cylinders = dr->ms_size / (255 * 63);
	    }
	    dr->ms_disk =  device_add_child(sc->mlx_dev, /*"mlxd"*/NULL, -1);
	    if (dr->ms_disk == 0)
		device_printf(sc->mlx_dev, "device_add_child failed\n");
	    device_set_ivars(dr->ms_disk, dr);
	}
    }
    free(mes, M_DEVBUF);
    if ((error = bus_generic_attach(sc->mlx_dev)) != 0)
	device_printf(sc->mlx_dev, "bus_generic_attach returned %d", error);

    /* mark controller back up */
    MLX_IO_LOCK(sc);
    sc->mlx_state &= ~MLX_STATE_SHUTDOWN;

    /* enable interrupts */
    sc->mlx_intaction(sc, MLX_INTACTION_ENABLE);
    MLX_IO_UNLOCK(sc);
    MLX_CONFIG_UNLOCK(sc);
}

/********************************************************************************
 * Disconnect from the controller completely, in preparation for unload.
 */
int
mlx_detach(device_t dev)
{
    struct mlx_softc	*sc = device_get_softc(dev);
    struct mlxd_softc	*mlxd;
    int			i, error;

    debug_called(1);

    error = EBUSY;
    MLX_CONFIG_LOCK(sc);
    if (sc->mlx_state & MLX_STATE_OPEN)
	goto out;

    for (i = 0; i < MLX_MAXDRIVES; i++) {
	if (sc->mlx_sysdrive[i].ms_disk != 0) {
	    mlxd = device_get_softc(sc->mlx_sysdrive[i].ms_disk);
	    if (mlxd->mlxd_flags & MLXD_OPEN) {		/* drive is mounted, abort detach */
		device_printf(sc->mlx_sysdrive[i].ms_disk, "still open, can't detach\n");
		goto out;
	    }
	}
    }
    if ((error = mlx_shutdown(dev)))
	goto out;
    MLX_CONFIG_UNLOCK(sc);

    mlx_free(sc);

    return (0);
 out:
    MLX_CONFIG_UNLOCK(sc);
    return(error);
}

/********************************************************************************
 * Bring the controller down to a dormant state and detach all child devices.
 *
 * This function is called before detach, system shutdown, or before performing
 * an operation which may add or delete system disks.  (Call mlx_startup to
 * resume normal operation.)
 *
 * Note that we can assume that the bioq on the controller is empty, as we won't
 * allow shutdown if any device is open.
 */
int
mlx_shutdown(device_t dev)
{
    struct mlx_softc	*sc = device_get_softc(dev);
    int			error;

    MLX_CONFIG_LOCK(sc);
    error = mlx_shutdown_locked(sc);
    MLX_CONFIG_UNLOCK(sc);
    return (error);
}

static int
mlx_shutdown_locked(struct mlx_softc *sc)
{
    int			i, error;

    debug_called(1);

    MLX_CONFIG_ASSERT_LOCKED(sc);

    MLX_IO_LOCK(sc);
    sc->mlx_state |= MLX_STATE_SHUTDOWN;
    sc->mlx_intaction(sc, MLX_INTACTION_DISABLE);

    /* flush controller */
    device_printf(sc->mlx_dev, "flushing cache...");
    if (mlx_flush(sc)) {
	printf("failed\n");
    } else {
	printf("done\n");
    }
    MLX_IO_UNLOCK(sc);
    
    /* delete all our child devices */
    for (i = 0; i < MLX_MAXDRIVES; i++) {
	if (sc->mlx_sysdrive[i].ms_disk != 0) {
	    if ((error = device_delete_child(sc->mlx_dev, sc->mlx_sysdrive[i].ms_disk)) != 0)
		return (error);
	    sc->mlx_sysdrive[i].ms_disk = 0;
	}
    }

    return (0);
}

/********************************************************************************
 * Bring the controller to a quiescent state, ready for system suspend.
 */
int
mlx_suspend(device_t dev)
{
    struct mlx_softc	*sc = device_get_softc(dev);

    debug_called(1);

    MLX_IO_LOCK(sc);
    sc->mlx_state |= MLX_STATE_SUSPEND;
    
    /* flush controller */
    device_printf(sc->mlx_dev, "flushing cache...");
    printf("%s\n", mlx_flush(sc) ? "failed" : "done");

    sc->mlx_intaction(sc, MLX_INTACTION_DISABLE);
    MLX_IO_UNLOCK(sc);

    return(0);
}

/********************************************************************************
 * Bring the controller back to a state ready for operation.
 */
int
mlx_resume(device_t dev)
{
    struct mlx_softc	*sc = device_get_softc(dev);

    debug_called(1);

    MLX_IO_LOCK(sc);
    sc->mlx_state &= ~MLX_STATE_SUSPEND;
    sc->mlx_intaction(sc, MLX_INTACTION_ENABLE);
    MLX_IO_UNLOCK(sc);

    return(0);
}

/*******************************************************************************
 * Take an interrupt, or be poked by other code to look for interrupt-worthy
 * status.
 */
void
mlx_intr(void *arg)
{
    struct mlx_softc	*sc = (struct mlx_softc *)arg;

    debug_called(1);

    /* collect finished commands, queue anything waiting */
    MLX_IO_LOCK(sc);
    mlx_done(sc, 1);
    MLX_IO_UNLOCK(sc);
};

/*******************************************************************************
 * Receive a buf structure from a child device and queue it on a particular
 * disk resource, then poke the disk resource to start as much work as it can.
 */
int
mlx_submit_buf(struct mlx_softc *sc, struct bio *bp)
{
    
    debug_called(1);

    MLX_IO_ASSERT_LOCKED(sc);
    bioq_insert_tail(&sc->mlx_bioq, bp);
    sc->mlx_waitbufs++;
    mlx_startio(sc);
    return(0);
}

/********************************************************************************
 * Accept an open operation on the control device.
 */
int
mlx_open(struct cdev *dev, int flags, int fmt, struct thread *td)
{
    struct mlx_softc	*sc = dev->si_drv1;

    MLX_CONFIG_LOCK(sc);
    MLX_IO_LOCK(sc);
    sc->mlx_state |= MLX_STATE_OPEN;
    MLX_IO_UNLOCK(sc);
    MLX_CONFIG_UNLOCK(sc);
    return(0);
}

/********************************************************************************
 * Accept the last close on the control device.
 */
int
mlx_close(struct cdev *dev, int flags, int fmt, struct thread *td)
{
    struct mlx_softc	*sc = dev->si_drv1;

    MLX_CONFIG_LOCK(sc);
    MLX_IO_LOCK(sc);
    sc->mlx_state &= ~MLX_STATE_OPEN;
    MLX_IO_UNLOCK(sc);
    MLX_CONFIG_UNLOCK(sc);
    return (0);
}

/********************************************************************************
 * Handle controller-specific control operations.
 */
int
mlx_ioctl(struct cdev *dev, u_long cmd, caddr_t addr, int32_t flag, struct thread *td)
{
    struct mlx_softc		*sc = dev->si_drv1;
    struct mlx_rebuild_request	*rb = (struct mlx_rebuild_request *)addr;
    struct mlx_rebuild_status	*rs = (struct mlx_rebuild_status *)addr;
    int				*arg = (int *)addr;
    struct mlx_pause		*mp;
    struct mlx_sysdrive		*dr;
    struct mlxd_softc		*mlxd;
    int				i, error;
    
    switch(cmd) {
	/*
	 * Enumerate connected system drives; returns the first system drive's
	 * unit number if *arg is -1, or the next unit after *arg if it's
	 * a valid unit on this controller.
	 */
    case MLX_NEXT_CHILD:
	/* search system drives */
	MLX_CONFIG_LOCK(sc);
	for (i = 0; i < MLX_MAXDRIVES; i++) {
	    /* is this one attached? */
	    if (sc->mlx_sysdrive[i].ms_disk != 0) {
		/* looking for the next one we come across? */
		if (*arg == -1) {
		    *arg = device_get_unit(sc->mlx_sysdrive[i].ms_disk);
		    MLX_CONFIG_UNLOCK(sc);
		    return(0);
		}
		/* we want the one after this one */
		if (*arg == device_get_unit(sc->mlx_sysdrive[i].ms_disk))
		    *arg = -1;
	    }
	}
	MLX_CONFIG_UNLOCK(sc);
	return(ENOENT);

	/*
	 * Scan the controller to see whether new drives have appeared.
	 */
    case MLX_RESCAN_DRIVES:
	mtx_lock(&Giant);
	mlx_startup(sc);
	mtx_unlock(&Giant);	
	return(0);

	/*
	 * Disconnect from the specified drive; it may be about to go 
	 * away.
	 */
    case MLX_DETACH_DRIVE:			/* detach one drive */
	MLX_CONFIG_LOCK(sc);
	if (((dr = mlx_findunit(sc, *arg)) == NULL) || 
	    ((mlxd = device_get_softc(dr->ms_disk)) == NULL)) {
	    MLX_CONFIG_UNLOCK(sc);
	    return(ENOENT);
	}

	device_printf(dr->ms_disk, "detaching...");
	error = 0;
	if (mlxd->mlxd_flags & MLXD_OPEN) {
	    error = EBUSY;
	    goto detach_out;
	}
	
	/* flush controller */
	MLX_IO_LOCK(sc);
	if (mlx_flush(sc)) {
	    MLX_IO_UNLOCK(sc);
	    error = EBUSY;
	    goto detach_out;
	}
	MLX_IO_UNLOCK(sc);

	/* nuke drive */
	if ((error = device_delete_child(sc->mlx_dev, dr->ms_disk)) != 0)
	    goto detach_out;
	dr->ms_disk = 0;

    detach_out:
	MLX_CONFIG_UNLOCK(sc);
	if (error) {
	    printf("failed\n");
	} else {
	    printf("done\n");
	}
	return(error);

	/*
	 * Pause one or more SCSI channels for a period of time, to assist
	 * in the process of hot-swapping devices.
	 *
	 * Note that at least the 3.51 firmware on the DAC960PL doesn't seem
	 * to do this right.
	 */
    case MLX_PAUSE_CHANNEL:			/* schedule a channel pause */
	/* Does this command work on this firmware? */
	if (!(sc->mlx_feature & MLX_FEAT_PAUSEWORKS))
	    return(EOPNOTSUPP);

	/* check time values */
	mp = (struct mlx_pause *)addr;
	if ((mp->mp_when < 0) || (mp->mp_when > 3600))
	    return(EINVAL);
	if ((mp->mp_howlong < 1) || (mp->mp_howlong > (0xf * 30)))
	    return(EINVAL);

	MLX_IO_LOCK(sc);
	if ((mp->mp_which == MLX_PAUSE_CANCEL) && (sc->mlx_pause.mp_when != 0)) {
	    /* cancel a pending pause operation */
	    sc->mlx_pause.mp_which = 0;
	} else {
	    /* fix for legal channels */
	    mp->mp_which &= ((1 << sc->mlx_enq2->me_actual_channels) -1);
	    
	    /* check for a pause currently running */
	    if ((sc->mlx_pause.mp_which != 0) && (sc->mlx_pause.mp_when == 0)) {
		MLX_IO_UNLOCK(sc);
		return(EBUSY);
	    }

	    /* looks ok, go with it */
	    sc->mlx_pause.mp_which = mp->mp_which;
	    sc->mlx_pause.mp_when = time_second + mp->mp_when;
	    sc->mlx_pause.mp_howlong = sc->mlx_pause.mp_when + mp->mp_howlong;
	}
	MLX_IO_UNLOCK(sc);
	return(0);

	/*
	 * Accept a command passthrough-style.
	 */
    case MLX_COMMAND:
	return(mlx_user_command(sc, (struct mlx_usercommand *)addr));

	/*
	 * Start a rebuild on a given SCSI disk
	 */
    case MLX_REBUILDASYNC:
	MLX_IO_LOCK(sc);
	if (sc->mlx_background != 0) {
	    MLX_IO_UNLOCK(sc);
	    rb->rr_status = 0x0106;
	    return(EBUSY);
	}
	rb->rr_status = mlx_rebuild(sc, rb->rr_channel, rb->rr_target);
	switch (rb->rr_status) {
	case 0:
	    error = 0;
	    break;
	case 0x10000:
	    error = ENOMEM;		/* couldn't set up the command */
	    break;
	case 0x0002:	
	    error = EBUSY;
	    break;
	case 0x0104:
	    error = EIO;
	    break;
	case 0x0105:
	    error = ERANGE;
	    break;
	case 0x0106:
	    error = EBUSY;
	    break;
	default:
	    error = EINVAL;
	    break;
	}
	if (error == 0)
	    sc->mlx_background = MLX_BACKGROUND_REBUILD;
	MLX_IO_UNLOCK(sc);
	return(error);
	
	/*
	 * Get the status of the current rebuild or consistency check.
	 */
    case MLX_REBUILDSTAT:
	MLX_IO_LOCK(sc);
	*rs = sc->mlx_rebuildstat;
	MLX_IO_UNLOCK(sc);
	return(0);

	/*
	 * Return the per-controller system drive number matching the
	 * disk device number in (arg), if it happens to belong to us.
	 */
    case MLX_GET_SYSDRIVE:
	error = ENOENT;
	MLX_CONFIG_LOCK(sc);
	mtx_lock(&Giant);
	mlxd = (struct mlxd_softc *)devclass_get_softc(mlxd_devclass, *arg);
	mtx_unlock(&Giant);
	if ((mlxd != NULL) && (mlxd->mlxd_drive >= sc->mlx_sysdrive) && 
	    (mlxd->mlxd_drive < (sc->mlx_sysdrive + MLX_MAXDRIVES))) {
	    error = 0;
	    *arg = mlxd->mlxd_drive - sc->mlx_sysdrive;
	}
	MLX_CONFIG_UNLOCK(sc);
	return(error);
	
    default:	
	return(ENOTTY);
    }
}

/********************************************************************************
 * Handle operations requested by a System Drive connected to this controller.
 */
int
mlx_submit_ioctl(struct mlx_softc *sc, struct mlx_sysdrive *drive, u_long cmd, 
		caddr_t addr, int32_t flag, struct thread *td)
{
    int				*arg = (int *)addr;
    int				error, result;

    switch(cmd) {
	/*
	 * Return the current status of this drive.
	 */
    case MLXD_STATUS:
	MLX_IO_LOCK(sc);
	*arg = drive->ms_state;
	MLX_IO_UNLOCK(sc);
	return(0);
	
	/*
	 * Start a background consistency check on this drive.
	 */
    case MLXD_CHECKASYNC:		/* start a background consistency check */
	MLX_IO_LOCK(sc);
	if (sc->mlx_background != 0) {
	    MLX_IO_UNLOCK(sc);
	    *arg = 0x0106;
	    return(EBUSY);
	}
	result = mlx_check(sc, drive - &sc->mlx_sysdrive[0]);
	switch (result) {
	case 0:
	    error = 0;
	    break;
	case 0x10000:
	    error = ENOMEM;		/* couldn't set up the command */
	    break;
	case 0x0002:	
	    error = EIO;
	    break;
	case 0x0105:
	    error = ERANGE;
	    break;
	case 0x0106:
	    error = EBUSY;
	    break;
	default:
	    error = EINVAL;
	    break;
	}
	if (error == 0)
	    sc->mlx_background = MLX_BACKGROUND_CHECK;
	MLX_IO_UNLOCK(sc);
	*arg = result;
	return(error);

    }
    return(ENOIOCTL);
}


/********************************************************************************
 ********************************************************************************
                                                                Status Monitoring
 ********************************************************************************
 ********************************************************************************/

/********************************************************************************
 * Fire off commands to periodically check the status of connected drives.
 */
static void
mlx_periodic(void *data)
{
    struct mlx_softc *sc = (struct mlx_softc *)data;

    debug_called(1);
    MLX_IO_ASSERT_LOCKED(sc);

    /*
     * Run a bus pause? 
     */
    if ((sc->mlx_pause.mp_which != 0) &&
	(sc->mlx_pause.mp_when > 0) &&
	(time_second >= sc->mlx_pause.mp_when)){

	mlx_pause_action(sc);		/* pause is running */
	sc->mlx_pause.mp_when = 0;
	sysbeep(500, hz);

	/* 
	 * Bus pause still running?
	 */
    } else if ((sc->mlx_pause.mp_which != 0) &&
	       (sc->mlx_pause.mp_when == 0)) {

	/* time to stop bus pause? */
	if (time_second >= sc->mlx_pause.mp_howlong) {
	    mlx_pause_action(sc);
	    sc->mlx_pause.mp_which = 0;	/* pause is complete */
	    sysbeep(500, hz);
	} else {
	    sysbeep((time_second % 5) * 100 + 500, hz/8);
	}

	/* 
	 * Run normal periodic activities? 
	 */
    } else if (time_second > (sc->mlx_lastpoll + 10)) {
	sc->mlx_lastpoll = time_second;

	/* 
	 * Check controller status.
	 *
	 * XXX Note that this may not actually launch a command in situations of high load.
	 */
	mlx_enquire(sc, (sc->mlx_iftype == MLX_IFTYPE_2) ? MLX_CMD_ENQUIRY_OLD : MLX_CMD_ENQUIRY, 
		    imax(sizeof(struct mlx_enquiry), sizeof(struct mlx_enquiry_old)), mlx_periodic_enquiry);

	/*
	 * Check system drive status.
	 *
	 * XXX This might be better left to event-driven detection, eg. I/O to an offline
	 *     drive will detect it's offline, rebuilds etc. should detect the drive is back
	 *     online.
	 */
	mlx_enquire(sc, MLX_CMD_ENQSYSDRIVE, sizeof(struct mlx_enq_sys_drive) * MLX_MAXDRIVES, 
			mlx_periodic_enquiry);
		
    }

    /* get drive rebuild/check status */
    /* XXX should check sc->mlx_background if this is only valid while in progress */
    mlx_enquire(sc, MLX_CMD_REBUILDSTAT, sizeof(struct mlx_rebuild_stat), mlx_periodic_rebuild);

    /* deal with possibly-missed interrupts and timed-out commands */
    mlx_done(sc, 1);

    /* reschedule another poll next second or so */
    callout_reset(&sc->mlx_timeout, hz, mlx_periodic, sc);
}

/********************************************************************************
 * Handle the result of an ENQUIRY command instigated by periodic status polling.
 */
static void
mlx_periodic_enquiry(struct mlx_command *mc)
{
    struct mlx_softc		*sc = mc->mc_sc;

    debug_called(1);
    MLX_IO_ASSERT_LOCKED(sc);

    /* Command completed OK? */
    if (mc->mc_status != 0) {
	device_printf(sc->mlx_dev, "periodic enquiry failed - %s\n", mlx_diagnose_command(mc));
	goto out;
    }

    /* respond to command */
    switch(mc->mc_mailbox[0]) {
	/*
	 * This is currently a bit fruitless, as we don't know how to extract the eventlog
	 * pointer yet.
	 */
    case MLX_CMD_ENQUIRY_OLD:
    {
	struct mlx_enquiry		*me = (struct mlx_enquiry *)mc->mc_data;
	struct mlx_enquiry_old		*meo = (struct mlx_enquiry_old *)mc->mc_data;
	int				i;

	/* convert data in-place to new format */
	for (i = (sizeof(me->me_dead) / sizeof(me->me_dead[0])) - 1; i >= 0; i--) {
	    me->me_dead[i].dd_chan = meo->me_dead[i].dd_chan;
	    me->me_dead[i].dd_targ = meo->me_dead[i].dd_targ;
	}
	me->me_misc_flags        = 0;
	me->me_rebuild_count     = meo->me_rebuild_count;
	me->me_dead_count        = meo->me_dead_count;
	me->me_critical_sd_count = meo->me_critical_sd_count;
	me->me_event_log_seq_num = 0;
	me->me_offline_sd_count  = meo->me_offline_sd_count;
	me->me_max_commands      = meo->me_max_commands;
	me->me_rebuild_flag      = meo->me_rebuild_flag;
	me->me_fwmajor           = meo->me_fwmajor;
	me->me_fwminor           = meo->me_fwminor;
	me->me_status_flags      = meo->me_status_flags;
	me->me_flash_age         = meo->me_flash_age;
	for (i = (sizeof(me->me_drvsize) / sizeof(me->me_drvsize[0])) - 1; i >= 0; i--) {
	    if (i > ((sizeof(meo->me_drvsize) / sizeof(meo->me_drvsize[0])) - 1)) {
		me->me_drvsize[i] = 0;		/* drive beyond supported range */
	    } else {
		me->me_drvsize[i] = meo->me_drvsize[i];
	    }
	}
	me->me_num_sys_drvs = meo->me_num_sys_drvs;
    }
    /* FALLTHROUGH */

	/*
	 * Generic controller status update.  We could do more with this than just
	 * checking the event log.
	 */
    case MLX_CMD_ENQUIRY:
    {
	struct mlx_enquiry		*me = (struct mlx_enquiry *)mc->mc_data;
	
	if (sc->mlx_currevent == -1) {
	    /* initialise our view of the event log */
	    sc->mlx_currevent = sc->mlx_lastevent = me->me_event_log_seq_num;
	} else if ((me->me_event_log_seq_num != sc->mlx_lastevent) && !(sc->mlx_flags & MLX_EVENTLOG_BUSY)) {
	    /* record where current events are up to */
	    sc->mlx_currevent = me->me_event_log_seq_num;
	    debug(1, "event log pointer was %d, now %d\n", sc->mlx_lastevent, sc->mlx_currevent);

	    /* mark the event log as busy */
	    sc->mlx_flags |= MLX_EVENTLOG_BUSY;
	    
	    /* drain new eventlog entries */
	    mlx_periodic_eventlog_poll(sc);
	}
	break;
    }
    case MLX_CMD_ENQSYSDRIVE:
    {
	struct mlx_enq_sys_drive	*mes = (struct mlx_enq_sys_drive *)mc->mc_data;
	struct mlx_sysdrive		*dr;
	int				i;
	
	for (i = 0, dr = &sc->mlx_sysdrive[0]; 
	     (i < MLX_MAXDRIVES) && (mes[i].sd_size != 0xffffffff); 
	     i++) {

	    /* has state been changed by controller? */
	    if (dr->ms_state != mes[i].sd_state) {
		switch(mes[i].sd_state) {
		case MLX_SYSD_OFFLINE:
		    device_printf(dr->ms_disk, "drive offline\n");
		    break;
		case MLX_SYSD_ONLINE:
		    device_printf(dr->ms_disk, "drive online\n");
		    break;
		case MLX_SYSD_CRITICAL:
		    device_printf(dr->ms_disk, "drive critical\n");
		    break;
		}
		/* save new state */
		dr->ms_state = mes[i].sd_state;
	    }
	}
	break;
    }
    default:
	device_printf(sc->mlx_dev, "%s: unknown command 0x%x", __func__, mc->mc_mailbox[0]);
	break;
    }

 out:
    free(mc->mc_data, M_DEVBUF);
    mlx_releasecmd(mc);
}

static void
mlx_eventlog_cb(void *arg, bus_dma_segment_t *segs, int nsegments, int error)
{
    struct mlx_command *mc;

    mc = (struct mlx_command *)arg;
    mlx_setup_dmamap(mc, segs, nsegments, error);

    /* build the command to get one entry */
    mlx_make_type3(mc, MLX_CMD_LOGOP, MLX_LOGOP_GET, 1,
		   mc->mc_sc->mlx_lastevent, 0, 0, mc->mc_dataphys, 0);
    mc->mc_complete = mlx_periodic_eventlog_respond;
    mc->mc_private = mc;

    /* start the command */
    if (mlx_start(mc) != 0) {
	mlx_releasecmd(mc);
	free(mc->mc_data, M_DEVBUF);
	mc->mc_data = NULL;
    }
    
}

/********************************************************************************
 * Instigate a poll for one event log message on (sc).
 * We only poll for one message at a time, to keep our command usage down.
 */
static void
mlx_periodic_eventlog_poll(struct mlx_softc *sc)
{
    struct mlx_command	*mc;
    void		*result = NULL;
    int			error = 0;

    debug_called(1);
    MLX_IO_ASSERT_LOCKED(sc);

    /* get ourselves a command buffer */
    error = 1;
    if ((mc = mlx_alloccmd(sc)) == NULL)
	goto out;

    /* allocate the response structure */
    if ((result = malloc(/*sizeof(struct mlx_eventlog_entry)*/1024, M_DEVBUF,
			 M_NOWAIT)) == NULL)
	goto out;

    /* get a command slot */
    if (mlx_getslot(mc))
	goto out;

    /* map the command so the controller can see it */
    mc->mc_data = result;
    mc->mc_length = /*sizeof(struct mlx_eventlog_entry)*/1024;
    error = bus_dmamap_load(sc->mlx_buffer_dmat, mc->mc_dmamap, mc->mc_data,
			    mc->mc_length, mlx_eventlog_cb, mc, BUS_DMA_NOWAIT);

 out:
    if (error != 0) {
	if (mc != NULL)
	    mlx_releasecmd(mc);
	if ((result != NULL) && (mc->mc_data != NULL))
	    free(result, M_DEVBUF);
    }
}

/********************************************************************************
 * Handle the result of polling for a log message, generate diagnostic output.
 * If this wasn't the last message waiting for us, we'll go collect another.
 */
static char *mlx_sense_messages[] = {
    "because write recovery failed",
    "because of SCSI bus reset failure",
    "because of double check condition",
    "because it was removed",
    "because of gross error on SCSI chip",
    "because of bad tag returned from drive",
    "because of timeout on SCSI command",
    "because of reset SCSI command issued from system",
    "because busy or parity error count exceeded limit",
    "because of 'kill drive' command from system",
    "because of selection timeout",
    "due to SCSI phase sequence error",
    "due to unknown status"
};

static void
mlx_periodic_eventlog_respond(struct mlx_command *mc)
{
    struct mlx_softc		*sc = mc->mc_sc;
    struct mlx_eventlog_entry	*el = (struct mlx_eventlog_entry *)mc->mc_data;
    char			*reason;

    debug_called(1);
    MLX_IO_ASSERT_LOCKED(sc);

    sc->mlx_lastevent++;		/* next message... */
    if (mc->mc_status == 0) {

	/* handle event log message */
	switch(el->el_type) {
	    /*
	     * This is the only sort of message we understand at the moment.
	     * The tests here are probably incomplete.
	     */
	case MLX_LOGMSG_SENSE:	/* sense data */
	    /* Mylex vendor-specific message indicating a drive was killed? */
	    if ((el->el_sensekey == 9) &&
		(el->el_asc == 0x80)) {
		if (el->el_asq < nitems(mlx_sense_messages)) {
		    reason = mlx_sense_messages[el->el_asq];
		} else {
		    reason = "for unknown reason";
		}
		device_printf(sc->mlx_dev, "physical drive %d:%d killed %s\n",
			      el->el_channel, el->el_target, reason);
	    }
	    /* SCSI drive was reset? */
	    if ((el->el_sensekey == 6) && (el->el_asc == 0x29)) {
		device_printf(sc->mlx_dev, "physical drive %d:%d reset\n", 
			      el->el_channel, el->el_target);
	    }
	    /* SCSI drive error? */
	    if (!((el->el_sensekey == 0) ||
		  ((el->el_sensekey == 2) &&
		   (el->el_asc == 0x04) &&
		   ((el->el_asq == 0x01) ||
		    (el->el_asq == 0x02))))) {
		device_printf(sc->mlx_dev, "physical drive %d:%d error log: sense = %d asc = %x asq = %x\n",
			      el->el_channel, el->el_target, el->el_sensekey, el->el_asc, el->el_asq);
		device_printf(sc->mlx_dev, "  info %4D csi %4D\n", el->el_information, ":", el->el_csi, ":");
	    }
	    break;
	    
	default:
	    device_printf(sc->mlx_dev, "unknown log message type 0x%x\n", el->el_type);
	    break;
	}
    } else {
	device_printf(sc->mlx_dev, "error reading message log - %s\n", mlx_diagnose_command(mc));
	/* give up on all the outstanding messages, as we may have come unsynched */
	sc->mlx_lastevent = sc->mlx_currevent;
    }
	
    /* dispose of command and data */
    free(mc->mc_data, M_DEVBUF);
    mlx_releasecmd(mc);

    /* is there another message to obtain? */
    if (sc->mlx_lastevent != sc->mlx_currevent) {
	mlx_periodic_eventlog_poll(sc);
    } else {
	/* clear log-busy status */
	sc->mlx_flags &= ~MLX_EVENTLOG_BUSY;
    }
}

/********************************************************************************
 * Handle check/rebuild operations in progress.
 */
static void
mlx_periodic_rebuild(struct mlx_command *mc)
{
    struct mlx_softc		*sc = mc->mc_sc;
    struct mlx_rebuild_status	*mr = (struct mlx_rebuild_status *)mc->mc_data;

    MLX_IO_ASSERT_LOCKED(sc);
    switch(mc->mc_status) {
    case 0:				/* operation running, update stats */
	sc->mlx_rebuildstat = *mr;

	/* spontaneous rebuild/check? */
	if (sc->mlx_background == 0) {
	    sc->mlx_background = MLX_BACKGROUND_SPONTANEOUS;
	    device_printf(sc->mlx_dev, "background check/rebuild operation started\n");
	}
	break;

    case 0x0105:			/* nothing running, finalise stats and report */
	switch(sc->mlx_background) {
	case MLX_BACKGROUND_CHECK:
	    device_printf(sc->mlx_dev, "consistency check completed\n");	/* XXX print drive? */
	    break;
	case MLX_BACKGROUND_REBUILD:
	    device_printf(sc->mlx_dev, "drive rebuild completed\n");	/* XXX print channel/target? */
	    break;
	case MLX_BACKGROUND_SPONTANEOUS:
	default:
	    /* if we have previously been non-idle, report the transition */
	    if (sc->mlx_rebuildstat.rs_code != MLX_REBUILDSTAT_IDLE) {
		device_printf(sc->mlx_dev, "background check/rebuild operation completed\n");
	    }
	}
	sc->mlx_background = 0;
	sc->mlx_rebuildstat.rs_code = MLX_REBUILDSTAT_IDLE;
	break;
    }
    free(mc->mc_data, M_DEVBUF);
    mlx_releasecmd(mc);
}

/********************************************************************************
 ********************************************************************************
                                                                    Channel Pause
 ********************************************************************************
 ********************************************************************************/

/********************************************************************************
 * It's time to perform a channel pause action for (sc), either start or stop
 * the pause.
 */
static void
mlx_pause_action(struct mlx_softc *sc)
{
    struct mlx_command	*mc;
    int			failsafe, i, command;

    MLX_IO_ASSERT_LOCKED(sc);

    /* What are we doing here? */
    if (sc->mlx_pause.mp_when == 0) {
	command = MLX_CMD_STARTCHANNEL;
	failsafe = 0;

    } else {
	command = MLX_CMD_STOPCHANNEL;

	/* 
	 * Channels will always start again after the failsafe period, 
	 * which is specified in multiples of 30 seconds.
	 * This constrains us to a maximum pause of 450 seconds.
	 */
	failsafe = ((sc->mlx_pause.mp_howlong - time_second) + 5) / 30;
	if (failsafe > 0xf) {
	    failsafe = 0xf;
	    sc->mlx_pause.mp_howlong = time_second + (0xf * 30) - 5;
	}
    }

    /* build commands for every channel requested */
    for (i = 0; i < sc->mlx_enq2->me_actual_channels; i++) {
	if ((1 << i) & sc->mlx_pause.mp_which) {

	    /* get ourselves a command buffer */
	    if ((mc = mlx_alloccmd(sc)) == NULL)
		goto fail;
	    /* get a command slot */
	    mc->mc_flags |= MLX_CMD_PRIORITY;
	    if (mlx_getslot(mc))
		goto fail;

	    /* build the command */
	    mlx_make_type2(mc, command, (failsafe << 4) | i, 0, 0, 0, 0, 0, 0, 0);
	    mc->mc_complete = mlx_pause_done;
	    mc->mc_private = sc;		/* XXX not needed */
	    if (mlx_start(mc))
		goto fail;
	    /* command submitted OK */
	    return;
    
	fail:
	    device_printf(sc->mlx_dev, "%s failed for channel %d\n", 
			  command == MLX_CMD_STOPCHANNEL ? "pause" : "resume", i);
	    if (mc != NULL)
		mlx_releasecmd(mc);
	}
    }
}

static void
mlx_pause_done(struct mlx_command *mc)
{
    struct mlx_softc	*sc = mc->mc_sc;
    int			command = mc->mc_mailbox[0];
    int			channel = mc->mc_mailbox[2] & 0xf;

    MLX_IO_ASSERT_LOCKED(sc);
    if (mc->mc_status != 0) {
	device_printf(sc->mlx_dev, "%s command failed - %s\n", 
		      command == MLX_CMD_STOPCHANNEL ? "pause" : "resume", mlx_diagnose_command(mc));
    } else if (command == MLX_CMD_STOPCHANNEL) {
	device_printf(sc->mlx_dev, "channel %d pausing for %ld seconds\n", 
		      channel, (long)(sc->mlx_pause.mp_howlong - time_second));
    } else {
	device_printf(sc->mlx_dev, "channel %d resuming\n", channel);
    }
    mlx_releasecmd(mc);
}

/********************************************************************************
 ********************************************************************************
                                                               Command Submission
 ********************************************************************************
 ********************************************************************************/

static void
mlx_enquire_cb(void *arg, bus_dma_segment_t *segs, int nsegments, int error)
{
    struct mlx_softc *sc;
    struct mlx_command *mc;

    mc = (struct mlx_command *)arg;
    if (error)
	return;

    mlx_setup_dmamap(mc, segs, nsegments, error);

    /* build an enquiry command */
    sc = mc->mc_sc;
    mlx_make_type2(mc, mc->mc_command, 0, 0, 0, 0, 0, 0, mc->mc_dataphys, 0);

    /* do we want a completion callback? */
    if (mc->mc_complete != NULL) {
	if ((error = mlx_start(mc)) != 0)
	    return;
    } else {
	/* run the command in either polled or wait mode */
	if ((sc->mlx_state & MLX_STATE_INTEN) ? mlx_wait_command(mc) :
						mlx_poll_command(mc))
	    return;
    
	/* command completed OK? */
	if (mc->mc_status != 0) {
	    device_printf(sc->mlx_dev, "ENQUIRY failed - %s\n",
			  mlx_diagnose_command(mc));
	    return;
	}
    }
}

/********************************************************************************
 * Perform an Enquiry command using a type-3 command buffer and a return a single
 * linear result buffer.  If the completion function is specified, it will
 * be called with the completed command (and the result response will not be
 * valid until that point).  Otherwise, the command will either be busy-waited
 * for (interrupts not enabled), or slept for.
 */
static void *
mlx_enquire(struct mlx_softc *sc, int command, size_t bufsize, void (* complete)(struct mlx_command *mc))
{
    struct mlx_command	*mc;
    void		*result;
    int			error;

    debug_called(1);
    MLX_IO_ASSERT_LOCKED(sc);

    /* get ourselves a command buffer */
    error = 1;
    result = NULL;
    if ((mc = mlx_alloccmd(sc)) == NULL)
	goto out;
    /* allocate the response structure */
    if ((result = malloc(bufsize, M_DEVBUF, M_NOWAIT)) == NULL)
	goto out;
    /* get a command slot */
    mc->mc_flags |= MLX_CMD_PRIORITY | MLX_CMD_DATAOUT;
    if (mlx_getslot(mc))
	goto out;

    /* map the command so the controller can see it */
    mc->mc_data = result;
    mc->mc_length = bufsize;
    mc->mc_command = command;

    if (complete != NULL) {
	mc->mc_complete = complete;
	mc->mc_private = mc;
    }

    error = bus_dmamap_load(sc->mlx_buffer_dmat, mc->mc_dmamap, mc->mc_data,
			    mc->mc_length, mlx_enquire_cb, mc, BUS_DMA_NOWAIT);

 out:
    /* we got a command, but nobody else will free it */
    if ((mc != NULL) && (mc->mc_complete == NULL))
	mlx_releasecmd(mc);
    /* we got an error, and we allocated a result */
    if ((error != 0) && (result != NULL)) {
	free(result, M_DEVBUF);
	result = NULL;
    }
    return(result);
}


/********************************************************************************
 * Perform a Flush command on the nominated controller.
 *
 * May be called with interrupts enabled or disabled; will not return until
 * the flush operation completes or fails.
 */
static int
mlx_flush(struct mlx_softc *sc)
{
    struct mlx_command	*mc;
    int			error;

    debug_called(1);
    MLX_IO_ASSERT_LOCKED(sc);

    /* get ourselves a command buffer */
    error = 1;
    if ((mc = mlx_alloccmd(sc)) == NULL)
	goto out;
    /* get a command slot */
    if (mlx_getslot(mc))
	goto out;

    /* build a flush command */
    mlx_make_type2(mc, MLX_CMD_FLUSH, 0, 0, 0, 0, 0, 0, 0, 0);

    /* can't assume that interrupts are going to work here, so play it safe */
    if (mlx_poll_command(mc))
	goto out;
    
    /* command completed OK? */
    if (mc->mc_status != 0) {
	device_printf(sc->mlx_dev, "FLUSH failed - %s\n", mlx_diagnose_command(mc));
	goto out;
    }
    
    error = 0;			/* success */
 out:
    if (mc != NULL)
	mlx_releasecmd(mc);
    return(error);
}

/********************************************************************************
 * Start a background consistency check on (drive).
 *
 * May be called with interrupts enabled or disabled; will return as soon as the
 * operation has started or been refused.
 */
static int
mlx_check(struct mlx_softc *sc, int drive)
{
    struct mlx_command	*mc;
    int			error;

    debug_called(1);
    MLX_IO_ASSERT_LOCKED(sc);

    /* get ourselves a command buffer */
    error = 0x10000;
    if ((mc = mlx_alloccmd(sc)) == NULL)
	goto out;
    /* get a command slot */
    if (mlx_getslot(mc))
	goto out;

    /* build a checkasync command, set the "fix it" flag */
    mlx_make_type2(mc, MLX_CMD_CHECKASYNC, 0, 0, 0, 0, 0, drive | 0x80, 0, 0);

    /* start the command and wait for it to be returned */
    if (mlx_wait_command(mc))
	goto out;
    
    /* command completed OK? */
    if (mc->mc_status != 0) {	
	device_printf(sc->mlx_dev, "CHECK ASYNC failed - %s\n", mlx_diagnose_command(mc));
    } else {
	device_printf(sc->mlx_sysdrive[drive].ms_disk, "consistency check started");
    }
    error = mc->mc_status;

 out:
    if (mc != NULL)
	mlx_releasecmd(mc);
    return(error);
}

/********************************************************************************
 * Start a background rebuild of the physical drive at (channel),(target).
 *
 * May be called with interrupts enabled or disabled; will return as soon as the
 * operation has started or been refused.
 */
static int
mlx_rebuild(struct mlx_softc *sc, int channel, int target)
{
    struct mlx_command	*mc;
    int			error;

    debug_called(1);
    MLX_IO_ASSERT_LOCKED(sc);

    /* get ourselves a command buffer */
    error = 0x10000;
    if ((mc = mlx_alloccmd(sc)) == NULL)
	goto out;
    /* get a command slot */
    if (mlx_getslot(mc))
	goto out;

    /* build a checkasync command, set the "fix it" flag */
    mlx_make_type2(mc, MLX_CMD_REBUILDASYNC, channel, target, 0, 0, 0, 0, 0, 0);

    /* start the command and wait for it to be returned */
    if (mlx_wait_command(mc))
	goto out;
    
    /* command completed OK? */
    if (mc->mc_status != 0) {	
	device_printf(sc->mlx_dev, "REBUILD ASYNC failed - %s\n", mlx_diagnose_command(mc));
    } else {
	device_printf(sc->mlx_dev, "drive rebuild started for %d:%d\n", channel, target);
    }
    error = mc->mc_status;

 out:
    if (mc != NULL)
	mlx_releasecmd(mc);
    return(error);
}

/********************************************************************************
 * Run the command (mc) and return when it completes.
 *
 * Interrupts need to be enabled; returns nonzero on error.
 */
static int
mlx_wait_command(struct mlx_command *mc)
{
    struct mlx_softc	*sc = mc->mc_sc;
    int			error, count;

    debug_called(1);
    MLX_IO_ASSERT_LOCKED(sc);

    mc->mc_complete = NULL;
    mc->mc_private = mc;		/* wake us when you're done */
    if ((error = mlx_start(mc)) != 0)
	return(error);

    count = 0;
    /* XXX better timeout? */
    while ((mc->mc_status == MLX_STATUS_BUSY) && (count < 30)) {
	mtx_sleep(mc->mc_private, &sc->mlx_io_lock, PRIBIO | PCATCH, "mlxwcmd", hz);
    }

    if (mc->mc_status != 0) {
	device_printf(sc->mlx_dev, "command failed - %s\n", mlx_diagnose_command(mc));
	return(EIO);
    }
    return(0);
}


/********************************************************************************
 * Start the command (mc) and busy-wait for it to complete.
 *
 * Should only be used when interrupts can't be relied upon. Returns 0 on 
 * success, nonzero on error.
 * Successfully completed commands are dequeued.
 */
static int
mlx_poll_command(struct mlx_command *mc)
{
    struct mlx_softc	*sc = mc->mc_sc;
    int			error, count;

    debug_called(1);
    MLX_IO_ASSERT_LOCKED(sc);

    mc->mc_complete = NULL;
    mc->mc_private = NULL;	/* we will poll for it */
    if ((error = mlx_start(mc)) != 0)
	return(error);
    
    count = 0;
    do {
	/* poll for completion */
	mlx_done(mc->mc_sc, 1);
	
    } while ((mc->mc_status == MLX_STATUS_BUSY) && (count++ < 15000000));
    if (mc->mc_status != MLX_STATUS_BUSY) {
	TAILQ_REMOVE(&sc->mlx_work, mc, mc_link);
	return(0);
    }
    device_printf(sc->mlx_dev, "command failed - %s\n", mlx_diagnose_command(mc));
    return(EIO);
}

void
mlx_startio_cb(void *arg, bus_dma_segment_t *segs, int nsegments, int error)
{
    struct mlx_command	*mc;
    struct mlxd_softc	*mlxd;
    struct mlx_softc	*sc;
    struct bio		*bp;
    int			blkcount;
    int			driveno;
    int			cmd;

    mc = (struct mlx_command *)arg;
    mlx_setup_dmamap(mc, segs, nsegments, error);

    sc = mc->mc_sc;
    bp = mc->mc_private;

    if (bp->bio_cmd == BIO_READ) {
	mc->mc_flags |= MLX_CMD_DATAIN;
	cmd = MLX_CMD_READSG;
    } else {
	mc->mc_flags |= MLX_CMD_DATAOUT;
	cmd = MLX_CMD_WRITESG;
    }

    /* build a suitable I/O command (assumes 512-byte rounded transfers) */
    mlxd = bp->bio_disk->d_drv1;
    driveno = mlxd->mlxd_drive - sc->mlx_sysdrive;
    blkcount = howmany(bp->bio_bcount, MLX_BLKSIZE);

    if ((bp->bio_pblkno + blkcount) > sc->mlx_sysdrive[driveno].ms_size)
	device_printf(sc->mlx_dev,
		      "I/O beyond end of unit (%lld,%d > %lu)\n", 
		      (long long)bp->bio_pblkno, blkcount,
		      (u_long)sc->mlx_sysdrive[driveno].ms_size);

    /*
     * Build the I/O command.  Note that the SG list type bits are set to zero,
     * denoting the format of SG list that we are using.
     */
    if (sc->mlx_iftype == MLX_IFTYPE_2) {
	mlx_make_type1(mc, (cmd == MLX_CMD_WRITESG) ? MLX_CMD_WRITESG_OLD :
						      MLX_CMD_READSG_OLD,
		       blkcount & 0xff, 	/* xfer length low byte */
		       bp->bio_pblkno,		/* physical block number */
		       driveno,			/* target drive number */
		       mc->mc_sgphys,		/* location of SG list */
		       mc->mc_nsgent & 0x3f);	/* size of SG list */
	} else {
	mlx_make_type5(mc, cmd, 
		       blkcount & 0xff, 	/* xfer length low byte */
		       (driveno << 3) | ((blkcount >> 8) & 0x07),
						/* target+length high 3 bits */
		       bp->bio_pblkno,		/* physical block number */
		       mc->mc_sgphys,		/* location of SG list */
		       mc->mc_nsgent & 0x3f);	/* size of SG list */
    }

    /* try to give command to controller */
    if (mlx_start(mc) != 0) {
	/* fail the command */
	mc->mc_status = MLX_STATUS_WEDGED;
	mlx_completeio(mc);
    }

    sc->mlx_state &= ~MLX_STATE_QFROZEN;
}

/********************************************************************************
 * Pull as much work off the softc's work queue as possible and give it to the
 * controller.  Leave a couple of slots free for emergencies.
 */
static void
mlx_startio(struct mlx_softc *sc)
{
    struct mlx_command	*mc;
    struct bio		*bp;
    int			error;

    MLX_IO_ASSERT_LOCKED(sc);

    /* spin until something prevents us from doing any work */
    for (;;) {
	if (sc->mlx_state & MLX_STATE_QFROZEN)
	    break;

	/* see if there's work to be done */
	if ((bp = bioq_first(&sc->mlx_bioq)) == NULL)
	    break;
	/* get a command */
	if ((mc = mlx_alloccmd(sc)) == NULL)
	    break;
	/* get a slot for the command */
	if (mlx_getslot(mc) != 0) {
	    mlx_releasecmd(mc);
	    break;
	}
	/* get the buf containing our work */
	bioq_remove(&sc->mlx_bioq, bp);
	sc->mlx_waitbufs--;
	
	/* connect the buf to the command */
	mc->mc_complete = mlx_completeio;
	mc->mc_private = bp;
	mc->mc_data = bp->bio_data;
	mc->mc_length = bp->bio_bcount;
	
	/* map the command so the controller can work with it */
	error = bus_dmamap_load(sc->mlx_buffer_dmat, mc->mc_dmamap, mc->mc_data,
				mc->mc_length, mlx_startio_cb, mc, 0);
	if (error == EINPROGRESS) {
	    sc->mlx_state |= MLX_STATE_QFROZEN;
	    break;
	}
    }
}

/********************************************************************************
 * Handle completion of an I/O command.
 */
static void
mlx_completeio(struct mlx_command *mc)
{
    struct mlx_softc	*sc = mc->mc_sc;
    struct bio		*bp = mc->mc_private;
    struct mlxd_softc	*mlxd = bp->bio_disk->d_drv1;

    MLX_IO_ASSERT_LOCKED(sc);
    if (mc->mc_status != MLX_STATUS_OK) {	/* could be more verbose here? */
	bp->bio_error = EIO;
	bp->bio_flags |= BIO_ERROR;

	switch(mc->mc_status) {
	case MLX_STATUS_RDWROFFLINE:		/* system drive has gone offline */
	    device_printf(mlxd->mlxd_dev, "drive offline\n");
	    /* should signal this with a return code */
	    mlxd->mlxd_drive->ms_state = MLX_SYSD_OFFLINE;
	    break;

	default:				/* other I/O error */
	    device_printf(sc->mlx_dev, "I/O error - %s\n", mlx_diagnose_command(mc));
#if 0
	    device_printf(sc->mlx_dev, "  b_bcount %ld  blkcount %ld  b_pblkno %d\n", 
			  bp->bio_bcount, bp->bio_bcount / MLX_BLKSIZE, bp->bio_pblkno);
	    device_printf(sc->mlx_dev, "  %13D\n", mc->mc_mailbox, " ");
#endif
	    break;
	}
    }
    mlx_releasecmd(mc);
    mlxd_intr(bp);
}

void
mlx_user_cb(void *arg, bus_dma_segment_t *segs, int nsegments, int error)
{
    struct mlx_usercommand *mu;
    struct mlx_command *mc;
    struct mlx_dcdb	*dcdb;

    mc = (struct mlx_command *)arg;
    if (error)
	return;

    mlx_setup_dmamap(mc, segs, nsegments, error);

    mu = (struct mlx_usercommand *)mc->mc_private;
    dcdb = NULL;

    /* 
     * If this is a passthrough SCSI command, the DCDB is packed at the 
     * beginning of the data area.  Fix up the DCDB to point to the correct
     * physical address and override any bufptr supplied by the caller since
     * we know what it's meant to be.
     */
    if (mc->mc_mailbox[0] == MLX_CMD_DIRECT_CDB) {
	dcdb = (struct mlx_dcdb *)mc->mc_data;
	dcdb->dcdb_physaddr = mc->mc_dataphys + sizeof(*dcdb);
	mu->mu_bufptr = 8;
    }
    
    /* 
     * If there's a data buffer, fix up the command's buffer pointer.
     */
    if (mu->mu_datasize > 0) {
	mc->mc_mailbox[mu->mu_bufptr    ] =  mc->mc_dataphys        & 0xff;
	mc->mc_mailbox[mu->mu_bufptr + 1] = (mc->mc_dataphys >> 8)  & 0xff;
	mc->mc_mailbox[mu->mu_bufptr + 2] = (mc->mc_dataphys >> 16) & 0xff;
	mc->mc_mailbox[mu->mu_bufptr + 3] = (mc->mc_dataphys >> 24) & 0xff;
    }
    debug(0, "command fixup");

    /* submit the command and wait */
    if (mlx_wait_command(mc) != 0)
	return;

}

/********************************************************************************
 * Take a command from user-space and try to run it.
 *
 * XXX Note that this can't perform very much in the way of error checking, and
 *     as such, applications _must_ be considered trustworthy.
 * XXX Commands using S/G for data are not supported.
 */
static int
mlx_user_command(struct mlx_softc *sc, struct mlx_usercommand *mu)
{
    struct mlx_command	*mc;
    void		*kbuf;
    int			error;
    
    debug_called(0);
    
    kbuf = NULL;
    mc = NULL;
    error = ENOMEM;

    /* get ourselves a command and copy in from user space */
    MLX_IO_LOCK(sc);
    if ((mc = mlx_alloccmd(sc)) == NULL) {
	MLX_IO_UNLOCK(sc);
	return(error);
    }
    bcopy(mu->mu_command, mc->mc_mailbox, sizeof(mc->mc_mailbox));
    debug(0, "got command buffer");

    /*
     * if we need a buffer for data transfer, allocate one and copy in its
     * initial contents
     */
    if (mu->mu_datasize > 0) {
	if (mu->mu_datasize > MLX_MAXPHYS) {
	    error = EINVAL;
	    goto out;
	}
	MLX_IO_UNLOCK(sc);
	if (((kbuf = malloc(mu->mu_datasize, M_DEVBUF, M_WAITOK)) == NULL) ||
	    (error = copyin(mu->mu_buf, kbuf, mu->mu_datasize))) {
	    MLX_IO_LOCK(sc);
	    goto out;
	}
	MLX_IO_LOCK(sc);
	debug(0, "got kernel buffer");
    }

    /* get a command slot */
    if (mlx_getslot(mc))
	goto out;
    debug(0, "got a slot");

    if (mu->mu_datasize > 0) {

	/* range check the pointer to physical buffer address */
	if ((mu->mu_bufptr < 0) || (mu->mu_bufptr > (sizeof(mu->mu_command) -
						     sizeof(u_int32_t)))) {
	    error = EINVAL;
	    goto out;
	}
    }

    /* map the command so the controller can see it */
    mc->mc_data = kbuf;
    mc->mc_length = mu->mu_datasize;
    mc->mc_private = mu;
    error = bus_dmamap_load(sc->mlx_buffer_dmat, mc->mc_dmamap, mc->mc_data,
			    mc->mc_length, mlx_user_cb, mc, BUS_DMA_NOWAIT);
    if (error)
	goto out;

    /* copy out status and data */
    mu->mu_status = mc->mc_status;
    if (mu->mu_datasize > 0) {
	MLX_IO_UNLOCK(sc);
	error = copyout(kbuf, mu->mu_buf, mu->mu_datasize);
	MLX_IO_LOCK(sc);
    }

 out:
    mlx_releasecmd(mc);
    MLX_IO_UNLOCK(sc);
    if (kbuf != NULL)
	free(kbuf, M_DEVBUF);
    return(error);
}

/********************************************************************************
 ********************************************************************************
                                                        Command I/O to Controller
 ********************************************************************************
 ********************************************************************************/

/********************************************************************************
 * Find a free command slot for (mc).
 *
 * Don't hand out a slot to a normal-priority command unless there are at least
 * 4 slots free for priority commands.
 */
static int
mlx_getslot(struct mlx_command *mc)
{
    struct mlx_softc	*sc = mc->mc_sc;
    int			slot, limit;

    debug_called(1);

    MLX_IO_ASSERT_LOCKED(sc);

    /* 
     * Enforce slot-usage limit, if we have the required information.
     */
    if (sc->mlx_enq2 != NULL) {
	limit = sc->mlx_enq2->me_max_commands;
    } else {
	limit = 2;
    }
    if (sc->mlx_busycmds >= ((mc->mc_flags & MLX_CMD_PRIORITY) ? limit : limit - 4))
	return(EBUSY);

    /* 
     * Allocate an outstanding command slot 
     *
     * XXX linear search is slow
     */
    for (slot = 0; slot < limit; slot++) {
	debug(2, "try slot %d", slot);
	if (sc->mlx_busycmd[slot] == NULL)
	    break;
    }
    if (slot < limit) {
	sc->mlx_busycmd[slot] = mc;
	sc->mlx_busycmds++;
    }

    /* out of slots? */
    if (slot >= limit)
	return(EBUSY);

    debug(2, "got slot %d", slot);
    mc->mc_slot = slot;
    return(0);
}

/********************************************************************************
 * Map/unmap (mc)'s data in the controller's addressable space.
 */
static void
mlx_setup_dmamap(struct mlx_command *mc, bus_dma_segment_t *segs, int nsegments,
		 int error)
{
    struct mlx_softc	*sc = mc->mc_sc;
    struct mlx_sgentry	*sg;
    int			i;

    debug_called(1);

    /* XXX should be unnecessary */
    if (sc->mlx_enq2 && (nsegments > sc->mlx_enq2->me_max_sg))
	panic("MLX: too many s/g segments (%d, max %d)", nsegments,
	      sc->mlx_enq2->me_max_sg);

    /* get base address of s/g table */
    sg = sc->mlx_sgtable + (mc->mc_slot * MLX_NSEG);

    /* save s/g table information in command */
    mc->mc_nsgent = nsegments;
    mc->mc_sgphys = sc->mlx_sgbusaddr +
		   (mc->mc_slot * MLX_NSEG * sizeof(struct mlx_sgentry));
    mc->mc_dataphys = segs[0].ds_addr;

    /* populate s/g table */
    for (i = 0; i < nsegments; i++, sg++) {
	sg->sg_addr = segs[i].ds_addr;
	sg->sg_count = segs[i].ds_len;
    }

    /* Make sure the buffers are visible on the bus. */
    if (mc->mc_flags & MLX_CMD_DATAIN)
	bus_dmamap_sync(sc->mlx_buffer_dmat, mc->mc_dmamap,
			BUS_DMASYNC_PREREAD);
    if (mc->mc_flags & MLX_CMD_DATAOUT)
	bus_dmamap_sync(sc->mlx_buffer_dmat, mc->mc_dmamap,
			BUS_DMASYNC_PREWRITE);
}

static void
mlx_unmapcmd(struct mlx_command *mc)
{
    struct mlx_softc	*sc = mc->mc_sc;

    debug_called(1);

    /* if the command involved data at all */
    if (mc->mc_data != NULL) {
	
	if (mc->mc_flags & MLX_CMD_DATAIN)
	    bus_dmamap_sync(sc->mlx_buffer_dmat, mc->mc_dmamap, BUS_DMASYNC_POSTREAD);
	if (mc->mc_flags & MLX_CMD_DATAOUT)
	    bus_dmamap_sync(sc->mlx_buffer_dmat, mc->mc_dmamap, BUS_DMASYNC_POSTWRITE);

	bus_dmamap_unload(sc->mlx_buffer_dmat, mc->mc_dmamap); 
    }
}

/********************************************************************************
 * Try to deliver (mc) to the controller.
 *
 * Can be called at any interrupt level, with or without interrupts enabled.
 */
static int
mlx_start(struct mlx_command *mc)
{
    struct mlx_softc	*sc = mc->mc_sc;
    int			i;

    debug_called(1);

    /* save the slot number as ident so we can handle this command when complete */
    mc->mc_mailbox[0x1] = mc->mc_slot;

    /* mark the command as currently being processed */
    mc->mc_status = MLX_STATUS_BUSY;

    /* set a default 60-second timeout  XXX tunable?  XXX not currently used */
    mc->mc_timeout = time_second + 60;

    /* spin waiting for the mailbox */
    for (i = 100000; i > 0; i--) {
	if (sc->mlx_tryqueue(sc, mc)) {
	    /* move command to work queue */
	    TAILQ_INSERT_TAIL(&sc->mlx_work, mc, mc_link);
	    return (0);
	} else if (i > 1)
	    mlx_done(sc, 0);
    }

    /* 
     * We couldn't get the controller to take the command.  Revoke the slot
     * that the command was given and return it with a bad status.
     */
    sc->mlx_busycmd[mc->mc_slot] = NULL;
    device_printf(sc->mlx_dev, "controller wedged (not taking commands)\n");
    mc->mc_status = MLX_STATUS_WEDGED;
    mlx_complete(sc);
    return(EIO);
}

/********************************************************************************
 * Poll the controller (sc) for completed commands.
 * Update command status and free slots for reuse.  If any slots were freed,
 * new commands may be posted.
 *
 * Returns nonzero if one or more commands were completed.
 */
static int
mlx_done(struct mlx_softc *sc, int startio)
{
    struct mlx_command	*mc;
    int			result;
    u_int8_t		slot;
    u_int16_t		status;
    
    debug_called(2);
    MLX_IO_ASSERT_LOCKED(sc);

    result = 0;

    /* loop collecting completed commands */
    for (;;) {
	/* poll for a completed command's identifier and status */
	if (sc->mlx_findcomplete(sc, &slot, &status)) {
	    result = 1;
	    mc = sc->mlx_busycmd[slot];			/* find command */
	    if (mc != NULL) {				/* paranoia */
		if (mc->mc_status == MLX_STATUS_BUSY) {
		    mc->mc_status = status;		/* save status */

		    /* free slot for reuse */
		    sc->mlx_busycmd[slot] = NULL;
		    sc->mlx_busycmds--;
		} else {
		    device_printf(sc->mlx_dev, "duplicate done event for slot %d\n", slot);
		}
	    } else {
		device_printf(sc->mlx_dev, "done event for nonbusy slot %d\n", slot);
	    }
	} else {
	    break;
	}
    }

    /* if we've completed any commands, try posting some more */
    if (result && startio)
	mlx_startio(sc);

    /* handle completion and timeouts */
    mlx_complete(sc);

    return(result);
}

/********************************************************************************
 * Perform post-completion processing for commands on (sc).
 */
static void
mlx_complete(struct mlx_softc *sc) 
{
    struct mlx_command	*mc, *nc;
    
    debug_called(2);
    MLX_IO_ASSERT_LOCKED(sc);

    /* scan the list of busy/done commands */
    mc = TAILQ_FIRST(&sc->mlx_work);
    while (mc != NULL) {
	nc = TAILQ_NEXT(mc, mc_link);

	/* Command has been completed in some fashion */
	if (mc->mc_status != MLX_STATUS_BUSY) {
	
	    /* unmap the command's data buffer */
	    mlx_unmapcmd(mc);
	    /*
	     * Does the command have a completion handler?
	     */
	    if (mc->mc_complete != NULL) {
		/* remove from list and give to handler */
		TAILQ_REMOVE(&sc->mlx_work, mc, mc_link);
		mc->mc_complete(mc);

		/* 
		 * Is there a sleeper waiting on this command?
		 */
	    } else if (mc->mc_private != NULL) {	/* sleeping caller wants to know about it */

		/* remove from list and wake up sleeper */
		TAILQ_REMOVE(&sc->mlx_work, mc, mc_link);
		wakeup_one(mc->mc_private);

		/*
		 * Leave the command for a caller that's polling for it.
		 */
	    } else {
	    }
	}
	mc = nc;
    }
}

/********************************************************************************
 ********************************************************************************
                                                        Command Buffer Management
 ********************************************************************************
 ********************************************************************************/

/********************************************************************************
 * Get a new command buffer.
 *
 * This may return NULL in low-memory cases.
 *
 * Note that using malloc() is expensive (the command buffer is << 1 page) but
 * necessary if we are to be a loadable module before the zone allocator is fixed.
 *
 * If possible, we recycle a command buffer that's been used before.
 *
 * XXX Note that command buffers are not cleaned out - it is the caller's 
 *     responsibility to ensure that all required fields are filled in before
 *     using a buffer.
 */
static struct mlx_command *
mlx_alloccmd(struct mlx_softc *sc)
{
    struct mlx_command	*mc;
    int			error;

    debug_called(1);

    MLX_IO_ASSERT_LOCKED(sc);
    if ((mc = TAILQ_FIRST(&sc->mlx_freecmds)) != NULL)
	TAILQ_REMOVE(&sc->mlx_freecmds, mc, mc_link);

    /* allocate a new command buffer? */
    if (mc == NULL) {
	mc = (struct mlx_command *)malloc(sizeof(*mc), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (mc != NULL) {
	    mc->mc_sc = sc;
	    error = bus_dmamap_create(sc->mlx_buffer_dmat, 0, &mc->mc_dmamap);
	    if (error) {
		free(mc, M_DEVBUF);
		return(NULL);
	    }
	}
    }
    return(mc);
}

/********************************************************************************
 * Release a command buffer for recycling.
 *
 * XXX It might be a good idea to limit the number of commands we save for reuse
 *     if it's shown that this list bloats out massively.
 */
static void
mlx_releasecmd(struct mlx_command *mc)
{
    
    debug_called(1);

    MLX_IO_ASSERT_LOCKED(mc->mc_sc);
    TAILQ_INSERT_HEAD(&mc->mc_sc->mlx_freecmds, mc, mc_link);
}

/********************************************************************************
 * Permanently discard a command buffer.
 */
static void
mlx_freecmd(struct mlx_command *mc) 
{
    struct mlx_softc	*sc = mc->mc_sc;
    
    debug_called(1);
    bus_dmamap_destroy(sc->mlx_buffer_dmat, mc->mc_dmamap);
    free(mc, M_DEVBUF);
}


/********************************************************************************
 ********************************************************************************
                                                Type 3 interface accessor methods
 ********************************************************************************
 ********************************************************************************/

/********************************************************************************
 * Try to give (mc) to the controller.  Returns 1 if successful, 0 on failure
 * (the controller is not ready to take a command).
 */
static int
mlx_v3_tryqueue(struct mlx_softc *sc, struct mlx_command *mc)
{
    int		i;
    
    debug_called(2);
    MLX_IO_ASSERT_LOCKED(sc);

    /* ready for our command? */
    if (!(MLX_V3_GET_IDBR(sc) & MLX_V3_IDB_FULL)) {
	/* copy mailbox data to window */
	for (i = 0; i < 13; i++)
	    MLX_V3_PUT_MAILBOX(sc, i, mc->mc_mailbox[i]);
	
	/* post command */
	MLX_V3_PUT_IDBR(sc, MLX_V3_IDB_FULL);
	return(1);
    }
    return(0);
}

/********************************************************************************
 * See if a command has been completed, if so acknowledge its completion
 * and recover the slot number and status code.
 */
static int
mlx_v3_findcomplete(struct mlx_softc *sc, u_int8_t *slot, u_int16_t *status)
{

    debug_called(2);
    MLX_IO_ASSERT_LOCKED(sc);

    /* status available? */
    if (MLX_V3_GET_ODBR(sc) & MLX_V3_ODB_SAVAIL) {
	*slot = MLX_V3_GET_STATUS_IDENT(sc);		/* get command identifier */
	*status = MLX_V3_GET_STATUS(sc);		/* get status */

	/* acknowledge completion */
	MLX_V3_PUT_ODBR(sc, MLX_V3_ODB_SAVAIL);
	MLX_V3_PUT_IDBR(sc, MLX_V3_IDB_SACK);
	return(1);
    }
    return(0);
}

/********************************************************************************
 * Enable/disable interrupts as requested. (No acknowledge required)
 */
static void
mlx_v3_intaction(struct mlx_softc *sc, int action)
{
    debug_called(1);
    MLX_IO_ASSERT_LOCKED(sc);

    switch(action) {
    case MLX_INTACTION_DISABLE:
	MLX_V3_PUT_IER(sc, 0);
	sc->mlx_state &= ~MLX_STATE_INTEN;
	break;
    case MLX_INTACTION_ENABLE:
	MLX_V3_PUT_IER(sc, 1);
	sc->mlx_state |= MLX_STATE_INTEN;
	break;
    }
}

/********************************************************************************
 * Poll for firmware error codes during controller initialisation.
 * Returns 0 if initialisation is complete, 1 if still in progress but no 
 * error has been fetched, 2 if an error has been retrieved.
 */
static int 
mlx_v3_fw_handshake(struct mlx_softc *sc, int *error, int *param1, int *param2,
    int first)
{
    u_int8_t	fwerror;

    debug_called(2);

    /* first time around, clear any hardware completion status */
    if (first) {
	MLX_V3_PUT_IDBR(sc, MLX_V3_IDB_SACK);
	DELAY(1000);
    }

    /* init in progress? */
    if (!(MLX_V3_GET_IDBR(sc) & MLX_V3_IDB_INIT_BUSY))
	return(0);

    /* test error value */
    fwerror = MLX_V3_GET_FWERROR(sc);
    if (!(fwerror & MLX_V3_FWERROR_PEND))
	return(1);

    /* mask status pending bit, fetch status */
    *error = fwerror & ~MLX_V3_FWERROR_PEND;
    *param1 = MLX_V3_GET_FWERROR_PARAM1(sc);
    *param2 = MLX_V3_GET_FWERROR_PARAM2(sc);

    /* acknowledge */
    MLX_V3_PUT_FWERROR(sc, 0);

    return(2);
}

/********************************************************************************
 ********************************************************************************
                                                Type 4 interface accessor methods
 ********************************************************************************
 ********************************************************************************/

/********************************************************************************
 * Try to give (mc) to the controller.  Returns 1 if successful, 0 on failure
 * (the controller is not ready to take a command).
 */
static int
mlx_v4_tryqueue(struct mlx_softc *sc, struct mlx_command *mc)
{
    int		i;
    
    debug_called(2);
    MLX_IO_ASSERT_LOCKED(sc);

    /* ready for our command? */
    if (!(MLX_V4_GET_IDBR(sc) & MLX_V4_IDB_FULL)) {
	/* copy mailbox data to window */
	for (i = 0; i < 13; i++)
	    MLX_V4_PUT_MAILBOX(sc, i, mc->mc_mailbox[i]);
	
	/* memory-mapped controller, so issue a write barrier to ensure the mailbox is filled */
	bus_barrier(sc->mlx_mem, MLX_V4_MAILBOX, MLX_V4_MAILBOX_LENGTH,
			  BUS_SPACE_BARRIER_WRITE);

	/* post command */
	MLX_V4_PUT_IDBR(sc, MLX_V4_IDB_HWMBOX_CMD);
	return(1);
    }
    return(0);
}

/********************************************************************************
 * See if a command has been completed, if so acknowledge its completion
 * and recover the slot number and status code.
 */
static int
mlx_v4_findcomplete(struct mlx_softc *sc, u_int8_t *slot, u_int16_t *status)
{

    debug_called(2);
    MLX_IO_ASSERT_LOCKED(sc);

    /* status available? */
    if (MLX_V4_GET_ODBR(sc) & MLX_V4_ODB_HWSAVAIL) {
	*slot = MLX_V4_GET_STATUS_IDENT(sc);		/* get command identifier */
	*status = MLX_V4_GET_STATUS(sc);		/* get status */

	/* acknowledge completion */
	MLX_V4_PUT_ODBR(sc, MLX_V4_ODB_HWMBOX_ACK);
	MLX_V4_PUT_IDBR(sc, MLX_V4_IDB_SACK);
	return(1);
    }
    return(0);
}

/********************************************************************************
 * Enable/disable interrupts as requested.
 */
static void
mlx_v4_intaction(struct mlx_softc *sc, int action)
{
    debug_called(1);
    MLX_IO_ASSERT_LOCKED(sc);

    switch(action) {
    case MLX_INTACTION_DISABLE:
	MLX_V4_PUT_IER(sc, MLX_V4_IER_MASK | MLX_V4_IER_DISINT);
	sc->mlx_state &= ~MLX_STATE_INTEN;
	break;
    case MLX_INTACTION_ENABLE:
	MLX_V4_PUT_IER(sc, MLX_V4_IER_MASK & ~MLX_V4_IER_DISINT);
	sc->mlx_state |= MLX_STATE_INTEN;
	break;
    }
}

/********************************************************************************
 * Poll for firmware error codes during controller initialisation.
 * Returns 0 if initialisation is complete, 1 if still in progress but no 
 * error has been fetched, 2 if an error has been retrieved.
 */
static int 
mlx_v4_fw_handshake(struct mlx_softc *sc, int *error, int *param1, int *param2,
    int first)
{
    u_int8_t	fwerror;

    debug_called(2);

    /* first time around, clear any hardware completion status */
    if (first) {
	MLX_V4_PUT_IDBR(sc, MLX_V4_IDB_SACK);
	DELAY(1000);
    }

    /* init in progress? */
    if (!(MLX_V4_GET_IDBR(sc) & MLX_V4_IDB_INIT_BUSY))
	return(0);

    /* test error value */
    fwerror = MLX_V4_GET_FWERROR(sc);
    if (!(fwerror & MLX_V4_FWERROR_PEND))
	return(1);

    /* mask status pending bit, fetch status */
    *error = fwerror & ~MLX_V4_FWERROR_PEND;
    *param1 = MLX_V4_GET_FWERROR_PARAM1(sc);
    *param2 = MLX_V4_GET_FWERROR_PARAM2(sc);

    /* acknowledge */
    MLX_V4_PUT_FWERROR(sc, 0);

    return(2);
}

/********************************************************************************
 ********************************************************************************
                                                Type 5 interface accessor methods
 ********************************************************************************
 ********************************************************************************/

/********************************************************************************
 * Try to give (mc) to the controller.  Returns 1 if successful, 0 on failure
 * (the controller is not ready to take a command).
 */
static int
mlx_v5_tryqueue(struct mlx_softc *sc, struct mlx_command *mc)
{
    int		i;

    debug_called(2);
    MLX_IO_ASSERT_LOCKED(sc);

    /* ready for our command? */
    if (MLX_V5_GET_IDBR(sc) & MLX_V5_IDB_EMPTY) {
	/* copy mailbox data to window */
	for (i = 0; i < 13; i++)
	    MLX_V5_PUT_MAILBOX(sc, i, mc->mc_mailbox[i]);

	/* post command */
	MLX_V5_PUT_IDBR(sc, MLX_V5_IDB_HWMBOX_CMD);
	return(1);
    }
    return(0);
}

/********************************************************************************
 * See if a command has been completed, if so acknowledge its completion
 * and recover the slot number and status code.
 */
static int
mlx_v5_findcomplete(struct mlx_softc *sc, u_int8_t *slot, u_int16_t *status)
{

    debug_called(2);
    MLX_IO_ASSERT_LOCKED(sc);

    /* status available? */
    if (MLX_V5_GET_ODBR(sc) & MLX_V5_ODB_HWSAVAIL) {
	*slot = MLX_V5_GET_STATUS_IDENT(sc);		/* get command identifier */
	*status = MLX_V5_GET_STATUS(sc);		/* get status */

	/* acknowledge completion */
	MLX_V5_PUT_ODBR(sc, MLX_V5_ODB_HWMBOX_ACK);
	MLX_V5_PUT_IDBR(sc, MLX_V5_IDB_SACK);
	return(1);
    }
    return(0);
}

/********************************************************************************
 * Enable/disable interrupts as requested.
 */
static void
mlx_v5_intaction(struct mlx_softc *sc, int action)
{
    debug_called(1);
    MLX_IO_ASSERT_LOCKED(sc);

    switch(action) {
    case MLX_INTACTION_DISABLE:
	MLX_V5_PUT_IER(sc, 0xff & MLX_V5_IER_DISINT);
	sc->mlx_state &= ~MLX_STATE_INTEN;
	break;
    case MLX_INTACTION_ENABLE:
	MLX_V5_PUT_IER(sc, 0xff & ~MLX_V5_IER_DISINT);
	sc->mlx_state |= MLX_STATE_INTEN;
	break;
    }
}

/********************************************************************************
 * Poll for firmware error codes during controller initialisation.
 * Returns 0 if initialisation is complete, 1 if still in progress but no 
 * error has been fetched, 2 if an error has been retrieved.
 */
static int 
mlx_v5_fw_handshake(struct mlx_softc *sc, int *error, int *param1, int *param2,
    int first)
{
    u_int8_t	fwerror;

    debug_called(2);

    /* first time around, clear any hardware completion status */
    if (first) {
	MLX_V5_PUT_IDBR(sc, MLX_V5_IDB_SACK);
	DELAY(1000);
    }

    /* init in progress? */
    if (MLX_V5_GET_IDBR(sc) & MLX_V5_IDB_INIT_DONE)
	return(0);

    /* test for error value */
    fwerror = MLX_V5_GET_FWERROR(sc);
    if (!(fwerror & MLX_V5_FWERROR_PEND))
	return(1);

    /* mask status pending bit, fetch status */
    *error = fwerror & ~MLX_V5_FWERROR_PEND;
    *param1 = MLX_V5_GET_FWERROR_PARAM1(sc);
    *param2 = MLX_V5_GET_FWERROR_PARAM2(sc);

    /* acknowledge */
    MLX_V5_PUT_FWERROR(sc, 0xff);

    return(2);
}

/********************************************************************************
 ********************************************************************************
                                                                        Debugging
 ********************************************************************************
 ********************************************************************************/

/********************************************************************************
 * Return a status message describing (mc)
 */
static char *mlx_status_messages[] = {
    "normal completion",			/* 00 */
    "irrecoverable data error",			/* 01 */
    "drive does not exist, or is offline",	/* 02 */
    "attempt to write beyond end of drive",	/* 03 */
    "bad data encountered",			/* 04 */
    "invalid log entry request",		/* 05 */
    "attempt to rebuild online drive",		/* 06 */
    "new disk failed during rebuild",		/* 07 */
    "invalid channel/target",			/* 08 */
    "rebuild/check already in progress",	/* 09 */
    "one or more disks are dead",		/* 10 */
    "invalid or non-redundant drive",		/* 11 */
    "channel is busy",				/* 12 */
    "channel is not stopped",			/* 13 */
    "rebuild successfully terminated",		/* 14 */
    "unsupported command",			/* 15 */
    "check condition received",			/* 16 */
    "device is busy",				/* 17 */
    "selection or command timeout",		/* 18 */
    "command terminated abnormally",		/* 19 */
    ""
};

static struct
{
    int		command;
    u_int16_t	status;
    int		msg;
} mlx_messages[] = {
    {MLX_CMD_READSG,		0x0001,	 1},
    {MLX_CMD_READSG,		0x0002,	 1},
    {MLX_CMD_READSG,		0x0105,	 3},
    {MLX_CMD_READSG,		0x010c,	 4},
    {MLX_CMD_WRITESG,		0x0001,	 1},
    {MLX_CMD_WRITESG,		0x0002,	 1},
    {MLX_CMD_WRITESG,		0x0105,	 3},
    {MLX_CMD_READSG_OLD,	0x0001,	 1},
    {MLX_CMD_READSG_OLD,	0x0002,	 1},
    {MLX_CMD_READSG_OLD,	0x0105,	 3},
    {MLX_CMD_WRITESG_OLD,	0x0001,	 1},
    {MLX_CMD_WRITESG_OLD,	0x0002,	 1},
    {MLX_CMD_WRITESG_OLD,	0x0105,	 3},
    {MLX_CMD_LOGOP,		0x0105,	 5},
    {MLX_CMD_REBUILDASYNC,	0x0002,  6},
    {MLX_CMD_REBUILDASYNC,	0x0004,  7},
    {MLX_CMD_REBUILDASYNC,	0x0105,  8},
    {MLX_CMD_REBUILDASYNC,	0x0106,  9},
    {MLX_CMD_REBUILDASYNC,	0x0107, 14},
    {MLX_CMD_CHECKASYNC,	0x0002, 10},
    {MLX_CMD_CHECKASYNC,	0x0105, 11},
    {MLX_CMD_CHECKASYNC,	0x0106,  9},
    {MLX_CMD_STOPCHANNEL,	0x0106, 12},
    {MLX_CMD_STOPCHANNEL,	0x0105,  8},
    {MLX_CMD_STARTCHANNEL,	0x0005, 13},
    {MLX_CMD_STARTCHANNEL,	0x0105,  8},
    {MLX_CMD_DIRECT_CDB,	0x0002, 16},
    {MLX_CMD_DIRECT_CDB,	0x0008, 17},
    {MLX_CMD_DIRECT_CDB,	0x000e, 18},
    {MLX_CMD_DIRECT_CDB,	0x000f, 19},
    {MLX_CMD_DIRECT_CDB,	0x0105,  8},
    
    {0,				0x0104, 14},
    {-1, 0, 0}
};

static char *
mlx_diagnose_command(struct mlx_command *mc)
{
    static char	unkmsg[80];
    int		i;
    
    /* look up message in table */
    for (i = 0; mlx_messages[i].command != -1; i++)
	if (((mc->mc_mailbox[0] == mlx_messages[i].command) || (mlx_messages[i].command == 0)) &&
	    (mc->mc_status == mlx_messages[i].status))
	    return(mlx_status_messages[mlx_messages[i].msg]);
	
    sprintf(unkmsg, "unknown response 0x%x for command 0x%x", (int)mc->mc_status, (int)mc->mc_mailbox[0]);
    return(unkmsg);
}

/*******************************************************************************
 * Print a string describing the controller (sc)
 */
static struct 
{
    int		hwid;
    char	*name;
} mlx_controller_names[] = {
    {0x01,	"960P/PD"},
    {0x02,	"960PL"},
    {0x10,	"960PG"},
    {0x11,	"960PJ"},
    {0x12,	"960PR"},
    {0x13,	"960PT"},
    {0x14,	"960PTL0"},
    {0x15,	"960PRL"},
    {0x16,	"960PTL1"},
    {0x20,	"1164PVX"},
    {-1, NULL}
};

static void
mlx_describe_controller(struct mlx_softc *sc) 
{
    static char		buf[80];
    char		*model;
    int			i;

    for (i = 0, model = NULL; mlx_controller_names[i].name != NULL; i++) {
	if ((sc->mlx_enq2->me_hardware_id & 0xff) == mlx_controller_names[i].hwid) {
	    model = mlx_controller_names[i].name;
	    break;
	}
    }
    if (model == NULL) {
	sprintf(buf, " model 0x%x", sc->mlx_enq2->me_hardware_id & 0xff);
	model = buf;
    }
    device_printf(sc->mlx_dev, "DAC%s, %d channel%s, firmware %d.%02d-%c-%02d, %dMB RAM\n",
		  model, 
		  sc->mlx_enq2->me_actual_channels, 
		  sc->mlx_enq2->me_actual_channels > 1 ? "s" : "",
		  sc->mlx_enq2->me_firmware_id & 0xff,
		  (sc->mlx_enq2->me_firmware_id >> 8) & 0xff,
		  (sc->mlx_enq2->me_firmware_id >> 24) & 0xff,
		  (sc->mlx_enq2->me_firmware_id >> 16) & 0xff,
		  sc->mlx_enq2->me_mem_size / (1024 * 1024));

    if (bootverbose) {
	device_printf(sc->mlx_dev, "  Hardware ID                 0x%08x\n", sc->mlx_enq2->me_hardware_id);
	device_printf(sc->mlx_dev, "  Firmware ID                 0x%08x\n", sc->mlx_enq2->me_firmware_id);
	device_printf(sc->mlx_dev, "  Configured/Actual channels  %d/%d\n", sc->mlx_enq2->me_configured_channels,
		      sc->mlx_enq2->me_actual_channels);
	device_printf(sc->mlx_dev, "  Max Targets                 %d\n", sc->mlx_enq2->me_max_targets);
	device_printf(sc->mlx_dev, "  Max Tags                    %d\n", sc->mlx_enq2->me_max_tags);
	device_printf(sc->mlx_dev, "  Max System Drives           %d\n", sc->mlx_enq2->me_max_sys_drives);
	device_printf(sc->mlx_dev, "  Max Arms                    %d\n", sc->mlx_enq2->me_max_arms);
	device_printf(sc->mlx_dev, "  Max Spans                   %d\n", sc->mlx_enq2->me_max_spans);
	device_printf(sc->mlx_dev, "  DRAM/cache/flash/NVRAM size %d/%d/%d/%d\n", sc->mlx_enq2->me_mem_size,
		      sc->mlx_enq2->me_cache_size, sc->mlx_enq2->me_flash_size, sc->mlx_enq2->me_nvram_size);
	device_printf(sc->mlx_dev, "  DRAM type                   %d\n", sc->mlx_enq2->me_mem_type);
	device_printf(sc->mlx_dev, "  Clock Speed                 %dns\n", sc->mlx_enq2->me_clock_speed);
	device_printf(sc->mlx_dev, "  Hardware Speed              %dns\n", sc->mlx_enq2->me_hardware_speed);
	device_printf(sc->mlx_dev, "  Max Commands                %d\n", sc->mlx_enq2->me_max_commands);
	device_printf(sc->mlx_dev, "  Max SG Entries              %d\n", sc->mlx_enq2->me_max_sg);
	device_printf(sc->mlx_dev, "  Max DP                      %d\n", sc->mlx_enq2->me_max_dp);
	device_printf(sc->mlx_dev, "  Max IOD                     %d\n", sc->mlx_enq2->me_max_iod);
	device_printf(sc->mlx_dev, "  Max Comb                    %d\n", sc->mlx_enq2->me_max_comb);
	device_printf(sc->mlx_dev, "  Latency                     %ds\n", sc->mlx_enq2->me_latency);
	device_printf(sc->mlx_dev, "  SCSI Timeout                %ds\n", sc->mlx_enq2->me_scsi_timeout);
	device_printf(sc->mlx_dev, "  Min Free Lines              %d\n", sc->mlx_enq2->me_min_freelines);
	device_printf(sc->mlx_dev, "  Rate Constant               %d\n", sc->mlx_enq2->me_rate_const);
	device_printf(sc->mlx_dev, "  MAXBLK                      %d\n", sc->mlx_enq2->me_maxblk);
	device_printf(sc->mlx_dev, "  Blocking Factor             %d sectors\n", sc->mlx_enq2->me_blocking_factor);
	device_printf(sc->mlx_dev, "  Cache Line Size             %d blocks\n", sc->mlx_enq2->me_cacheline);
	device_printf(sc->mlx_dev, "  SCSI Capability             %s%dMHz, %d bit\n", 
		      sc->mlx_enq2->me_scsi_cap & (1<<4) ? "differential " : "",
		      (1 << ((sc->mlx_enq2->me_scsi_cap >> 2) & 3)) * 10,
		      8 << (sc->mlx_enq2->me_scsi_cap & 0x3));
	device_printf(sc->mlx_dev, "  Firmware Build Number       %d\n", sc->mlx_enq2->me_firmware_build);
	device_printf(sc->mlx_dev, "  Fault Management Type       %d\n", sc->mlx_enq2->me_fault_mgmt_type);
	device_printf(sc->mlx_dev, "  Features                    %b\n", sc->mlx_enq2->me_firmware_features,
		      "\20\4Background Init\3Read Ahead\2MORE\1Cluster\n");
	
    }
}

/*******************************************************************************
 * Emit a string describing the firmware handshake status code, and return a flag 
 * indicating whether the code represents a fatal error.
 *
 * Error code interpretations are from the Linux driver, and don't directly match
 * the messages printed by Mylex's BIOS.  This may change if documentation on the
 * codes is forthcoming.
 */
static int
mlx_fw_message(struct mlx_softc *sc, int error, int param1, int param2)
{
    switch(error) {
    case 0x00:
	device_printf(sc->mlx_dev, "physical drive %d:%d not responding\n", param2, param1);
	break;
    case 0x08:
	/* we could be neater about this and give some indication when we receive more of them */
	if (!(sc->mlx_flags & MLX_SPINUP_REPORTED)) {
	    device_printf(sc->mlx_dev, "spinning up drives...\n");
	    sc->mlx_flags |= MLX_SPINUP_REPORTED;
	}
	break;
    case 0x30:
	device_printf(sc->mlx_dev, "configuration checksum error\n");
	break;
    case 0x60:
	device_printf(sc->mlx_dev, "mirror race recovery failed\n");
	break;
    case 0x70:
	device_printf(sc->mlx_dev, "mirror race recovery in progress\n");
	break;
    case 0x90:
	device_printf(sc->mlx_dev, "physical drive %d:%d COD mismatch\n", param2, param1);
	break;
    case 0xa0:
	device_printf(sc->mlx_dev, "logical drive installation aborted\n");
	break;
    case 0xb0:
	device_printf(sc->mlx_dev, "mirror race on a critical system drive\n");
	break;
    case 0xd0:
	device_printf(sc->mlx_dev, "new controller configuration found\n");
	break;
    case 0xf0:
	device_printf(sc->mlx_dev, "FATAL MEMORY PARITY ERROR\n");
	return(1);
    default:
	device_printf(sc->mlx_dev, "unknown firmware initialisation error %02x:%02x:%02x\n", error, param1, param2);
	break;
    }
    return(0);
}

/********************************************************************************
 ********************************************************************************
                                                                Utility Functions
 ********************************************************************************
 ********************************************************************************/

/********************************************************************************
 * Find the disk whose unit number is (unit) on this controller
 */
static struct mlx_sysdrive *
mlx_findunit(struct mlx_softc *sc, int unit)
{
    int		i;
    
    /* search system drives */
    MLX_CONFIG_ASSERT_LOCKED(sc);
    for (i = 0; i < MLX_MAXDRIVES; i++) {
	/* is this one attached? */
	if (sc->mlx_sysdrive[i].ms_disk != 0) {
	    /* is this the one? */
	    if (unit == device_get_unit(sc->mlx_sysdrive[i].ms_disk))
		return(&sc->mlx_sysdrive[i]);
	}
    }
    return(NULL);
}
