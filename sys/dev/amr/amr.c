/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999,2000 Michael Smith
 * Copyright (c) 2000 BSDi
 * Copyright (c) 2005 Scott Long
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

/*
 * Driver for the AMI MegaRaid family of controllers.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/sysctl.h>

#include <sys/bio.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/stat.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/amr/amrio.h>
#include <dev/amr/amrreg.h>
#include <dev/amr/amrvar.h>
#define AMR_DEFINE_TABLES
#include <dev/amr/amr_tables.h>

SYSCTL_NODE(_hw, OID_AUTO, amr, CTLFLAG_RD, 0, "AMR driver parameters");

static d_open_t         amr_open;
static d_close_t        amr_close;
static d_ioctl_t        amr_ioctl;

static struct cdevsw amr_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	D_NEEDGIANT,
	.d_open =	amr_open,
	.d_close =	amr_close,
	.d_ioctl =	amr_ioctl,
	.d_name =	"amr",
};

int linux_no_adapter = 0;
/*
 * Initialisation, bus interface.
 */
static void	amr_startup(void *arg);

/*
 * Command wrappers
 */
static int	amr_query_controller(struct amr_softc *sc);
static void	*amr_enquiry(struct amr_softc *sc, size_t bufsize, 
			     u_int8_t cmd, u_int8_t cmdsub, u_int8_t cmdqual, int *status);
static void	amr_completeio(struct amr_command *ac);
static int	amr_support_ext_cdb(struct amr_softc *sc);

/*
 * Command buffer allocation.
 */
static void	amr_alloccmd_cluster(struct amr_softc *sc);
static void	amr_freecmd_cluster(struct amr_command_cluster *acc);

/*
 * Command processing.
 */
static int	amr_bio_command(struct amr_softc *sc, struct amr_command **acp);
static int	amr_wait_command(struct amr_command *ac) __unused;
static int	amr_mapcmd(struct amr_command *ac);
static void	amr_unmapcmd(struct amr_command *ac);
static int	amr_start(struct amr_command *ac);
static void	amr_complete(void *context, ac_qhead_t *head);
static void	amr_setup_sg(void *arg, bus_dma_segment_t *segs, int nsegments, int error);
static void	amr_setup_data(void *arg, bus_dma_segment_t *segs, int nsegments, int error);
static void	amr_setup_ccb(void *arg, bus_dma_segment_t *segs, int nsegments, int error);
static void	amr_abort_load(struct amr_command *ac);

/*
 * Interface-specific shims
 */
static int	amr_quartz_submit_command(struct amr_command *ac);
static int	amr_quartz_get_work(struct amr_softc *sc, struct amr_mailbox *mbsave);
static int	amr_quartz_poll_command(struct amr_command *ac);
static int	amr_quartz_poll_command1(struct amr_softc *sc, struct amr_command *ac);

static int	amr_std_submit_command(struct amr_command *ac);
static int	amr_std_get_work(struct amr_softc *sc, struct amr_mailbox *mbsave);
static int	amr_std_poll_command(struct amr_command *ac);
static void	amr_std_attach_mailbox(struct amr_softc *sc);

#ifdef AMR_BOARD_INIT
static int	amr_quartz_init(struct amr_softc *sc);
static int	amr_std_init(struct amr_softc *sc);
#endif

/*
 * Debugging
 */
static void	amr_describe_controller(struct amr_softc *sc);
#ifdef AMR_DEBUG
#if 0
static void	amr_printcommand(struct amr_command *ac);
#endif
#endif

static void	amr_init_sysctl(struct amr_softc *sc);
static int	amr_linux_ioctl_int(struct cdev *dev, u_long cmd, caddr_t addr,
		    int32_t flag, struct thread *td);

static MALLOC_DEFINE(M_AMR, "amr", "AMR memory");

/********************************************************************************
 ********************************************************************************
                                                                      Inline Glue
 ********************************************************************************
 ********************************************************************************/

/********************************************************************************
 ********************************************************************************
                                                                Public Interfaces
 ********************************************************************************
 ********************************************************************************/

/********************************************************************************
 * Initialise the controller and softc.
 */
int
amr_attach(struct amr_softc *sc)
{
    device_t child;

    debug_called(1);

    /*
     * Initialise per-controller queues.
     */
    amr_init_qhead(&sc->amr_freecmds);
    amr_init_qhead(&sc->amr_ready);
    TAILQ_INIT(&sc->amr_cmd_clusters);
    bioq_init(&sc->amr_bioq);

    debug(2, "queue init done");

    /*
     * Configure for this controller type.
     */
    if (AMR_IS_QUARTZ(sc)) {
	sc->amr_submit_command = amr_quartz_submit_command;
	sc->amr_get_work       = amr_quartz_get_work;
	sc->amr_poll_command   = amr_quartz_poll_command;
	sc->amr_poll_command1  = amr_quartz_poll_command1;
    } else {
	sc->amr_submit_command = amr_std_submit_command;
	sc->amr_get_work       = amr_std_get_work;
	sc->amr_poll_command   = amr_std_poll_command;
	amr_std_attach_mailbox(sc);
    }

#ifdef AMR_BOARD_INIT
    if ((AMR_IS_QUARTZ(sc) ? amr_quartz_init(sc) : amr_std_init(sc)))
	return(ENXIO);
#endif

    /*
     * Allocate initial commands.
     */
    amr_alloccmd_cluster(sc);

    /*
     * Quiz controller for features and limits.
     */
    if (amr_query_controller(sc))
	return(ENXIO);

    debug(2, "controller query complete");

    /*
     * preallocate the remaining commands.
     */
    while (sc->amr_nextslot < sc->amr_maxio)
	amr_alloccmd_cluster(sc);

    /*
     * Setup sysctls.
     */
    amr_init_sysctl(sc);

    /*
     * Attach our 'real' SCSI channels to CAM.
     */
    child = device_add_child(sc->amr_dev, "amrp", -1);
    sc->amr_pass = child;
    if (child != NULL) {
	device_set_softc(child, sc);
	device_set_desc(child, "SCSI Passthrough Bus");
	bus_generic_attach(sc->amr_dev);
    }

    /*
     * Create the control device.
     */
    sc->amr_dev_t = make_dev(&amr_cdevsw, device_get_unit(sc->amr_dev), UID_ROOT, GID_OPERATOR,
			     S_IRUSR | S_IWUSR, "amr%d", device_get_unit(sc->amr_dev));
    sc->amr_dev_t->si_drv1 = sc;
    linux_no_adapter++;
    if (device_get_unit(sc->amr_dev) == 0)
	make_dev_alias(sc->amr_dev_t, "megadev0");

    /*
     * Schedule ourselves to bring the controller up once interrupts are
     * available.
     */
    bzero(&sc->amr_ich, sizeof(struct intr_config_hook));
    sc->amr_ich.ich_func = amr_startup;
    sc->amr_ich.ich_arg = sc;
    if (config_intrhook_establish(&sc->amr_ich) != 0) {
	device_printf(sc->amr_dev, "can't establish configuration hook\n");
	return(ENOMEM);
    }

    /*
     * Print a little information about the controller.
     */
    amr_describe_controller(sc);

    debug(2, "attach complete");
    return(0);
}

/********************************************************************************
 * Locate disk resources and attach children to them.
 */
static void
amr_startup(void *arg)
{
    struct amr_softc	*sc = (struct amr_softc *)arg;
    struct amr_logdrive	*dr;
    int			i, error;
    
    debug_called(1);

    /* get up-to-date drive information */
    if (amr_query_controller(sc)) {
	device_printf(sc->amr_dev, "can't scan controller for drives\n");
	return;
    }

    /* iterate over available drives */
    for (i = 0, dr = &sc->amr_drive[0]; (i < AMR_MAXLD) && (dr->al_size != 0xffffffff); i++, dr++) {
	/* are we already attached to this drive? */
	if (dr->al_disk == 0) {
	    /* generate geometry information */
	    if (dr->al_size > 0x200000) {	/* extended translation? */
		dr->al_heads = 255;
		dr->al_sectors = 63;
	    } else {
		dr->al_heads = 64;
		dr->al_sectors = 32;
	    }
	    dr->al_cylinders = dr->al_size / (dr->al_heads * dr->al_sectors);
	    
	    dr->al_disk = device_add_child(sc->amr_dev, NULL, -1);
	    if (dr->al_disk == 0)
		device_printf(sc->amr_dev, "device_add_child failed\n");
	    device_set_ivars(dr->al_disk, dr);
	}
    }
    
    if ((error = bus_generic_attach(sc->amr_dev)) != 0)
	device_printf(sc->amr_dev, "bus_generic_attach returned %d\n", error);
    
    /* mark controller back up */
    sc->amr_state &= ~AMR_STATE_SHUTDOWN;

    /* interrupts will be enabled before we do anything more */
    sc->amr_state |= AMR_STATE_INTEN;

    /* pull ourselves off the intrhook chain */
    if (sc->amr_ich.ich_func)
	config_intrhook_disestablish(&sc->amr_ich);
    sc->amr_ich.ich_func = NULL;

    return;
}

static void
amr_init_sysctl(struct amr_softc *sc)
{

    SYSCTL_ADD_INT(device_get_sysctl_ctx(sc->amr_dev),
	SYSCTL_CHILDREN(device_get_sysctl_tree(sc->amr_dev)),
	OID_AUTO, "allow_volume_configure", CTLFLAG_RW, &sc->amr_allow_vol_config, 0,
	"");
    SYSCTL_ADD_INT(device_get_sysctl_ctx(sc->amr_dev),
	SYSCTL_CHILDREN(device_get_sysctl_tree(sc->amr_dev)),
	OID_AUTO, "nextslot", CTLFLAG_RD, &sc->amr_nextslot, 0,
	"");
    SYSCTL_ADD_INT(device_get_sysctl_ctx(sc->amr_dev),
	SYSCTL_CHILDREN(device_get_sysctl_tree(sc->amr_dev)),
	OID_AUTO, "busyslots", CTLFLAG_RD, &sc->amr_busyslots, 0,
	"");
    SYSCTL_ADD_INT(device_get_sysctl_ctx(sc->amr_dev),
	SYSCTL_CHILDREN(device_get_sysctl_tree(sc->amr_dev)),
	OID_AUTO, "maxio", CTLFLAG_RD, &sc->amr_maxio, 0,
	"");
}


/*******************************************************************************
 * Free resources associated with a controller instance
 */
void
amr_free(struct amr_softc *sc)
{
    struct amr_command_cluster	*acc;

    /* detach from CAM */
    if (sc->amr_pass != NULL)
	device_delete_child(sc->amr_dev, sc->amr_pass);

    /* throw away any command buffers */
    while ((acc = TAILQ_FIRST(&sc->amr_cmd_clusters)) != NULL) {
	TAILQ_REMOVE(&sc->amr_cmd_clusters, acc, acc_link);
	amr_freecmd_cluster(acc);
    }

    /* destroy control device */
    if( sc->amr_dev_t != (struct cdev *)NULL)
	    destroy_dev(sc->amr_dev_t);

    if (mtx_initialized(&sc->amr_hw_lock))
	mtx_destroy(&sc->amr_hw_lock);

    if (mtx_initialized(&sc->amr_list_lock))
	mtx_destroy(&sc->amr_list_lock);
}

/*******************************************************************************
 * Receive a bio structure from a child device and queue it on a particular
 * disk resource, then poke the disk resource to start as much work as it can.
 */
int
amr_submit_bio(struct amr_softc *sc, struct bio *bio)
{
    debug_called(2);

    mtx_lock(&sc->amr_list_lock);
    amr_enqueue_bio(sc, bio);
    amr_startio(sc);
    mtx_unlock(&sc->amr_list_lock);
    return(0);
}

/********************************************************************************
 * Accept an open operation on the control device.
 */
static int
amr_open(struct cdev *dev, int flags, int fmt, struct thread *td)
{
    int			unit = dev2unit(dev);
    struct amr_softc	*sc = devclass_get_softc(devclass_find("amr"), unit);

    debug_called(1);

    sc->amr_state |= AMR_STATE_OPEN;
    return(0);
}

#ifdef LSI
static int
amr_del_ld(struct amr_softc *sc, int drv_no, int status)
{

    debug_called(1);

    sc->amr_state &= ~AMR_STATE_QUEUE_FRZN;
    sc->amr_state &= ~AMR_STATE_LD_DELETE;
    sc->amr_state |= AMR_STATE_REMAP_LD;
    debug(1, "State Set");

    if (!status) {
	debug(1, "disk begin destroyed %d",drv_no);
	if (--amr_disks_registered == 0)
	    cdevsw_remove(&amrddisk_cdevsw);
	debug(1, "disk begin destroyed success");
    }
    return 0;
}

static int
amr_prepare_ld_delete(struct amr_softc *sc)
{
    
    debug_called(1);
    if (sc->ld_del_supported == 0) 
	return(ENOIOCTL);

    sc->amr_state |= AMR_STATE_QUEUE_FRZN;
    sc->amr_state |= AMR_STATE_LD_DELETE;

    /* 5 minutes for the all the commands to be flushed.*/
    tsleep((void *)&sc->ld_del_supported, PCATCH | PRIBIO,"delete_logical_drv",hz * 60 * 1);
    if ( sc->amr_busyslots )	
	return(ENOIOCTL);

    return 0;
}
#endif

/********************************************************************************
 * Accept the last close on the control device.
 */
static int
amr_close(struct cdev *dev, int flags, int fmt, struct thread *td)
{
    int			unit = dev2unit(dev);
    struct amr_softc	*sc = devclass_get_softc(devclass_find("amr"), unit);

    debug_called(1);

    sc->amr_state &= ~AMR_STATE_OPEN;
    return (0);
}

/********************************************************************************
 * Handle controller-specific control operations.
 */
static void
amr_rescan_drives(struct cdev *dev)
{
    struct amr_softc	*sc = (struct amr_softc *)dev->si_drv1;
    int			i, error = 0;

    sc->amr_state |= AMR_STATE_REMAP_LD;
    while (sc->amr_busyslots) {
	device_printf(sc->amr_dev, "idle controller\n");
	amr_done(sc);
    }

    /* mark ourselves as in-shutdown */
    sc->amr_state |= AMR_STATE_SHUTDOWN;

    /* flush controller */
    device_printf(sc->amr_dev, "flushing cache...");
    printf("%s\n", amr_flush(sc) ? "failed" : "done");

    /* delete all our child devices */
    for(i = 0 ; i < AMR_MAXLD; i++) {
	if(sc->amr_drive[i].al_disk != 0) {
	    if((error = device_delete_child(sc->amr_dev,
		sc->amr_drive[i].al_disk)) != 0)
		goto shutdown_out;

	     sc->amr_drive[i].al_disk = 0;
	}
    }

shutdown_out:
    amr_startup(sc);
}

/*
 * Bug-for-bug compatibility with Linux!
 * Some apps will send commands with inlen and outlen set to 0,
 * even though they expect data to be transferred to them from the
 * card.  Linux accidentally allows this by allocating a 4KB
 * buffer for the transfer anyways, but it then throws it away
 * without copying it back to the app.
 * 
 * The amr(4) firmware relies on this feature.  In fact, it assumes
 * the buffer is always a power of 2 up to a max of 64k.  There is
 * also at least one case where it assumes a buffer less than 16k is
 * greater than 16k.  However, forcing all buffers to a size of 32k
 * causes stalls in the firmware.  Force each command smaller than
 * 64k up to the next power of two except that commands between 8k
 * and 16k are rounded up to 32k instead of 16k.
 */
static unsigned long
amr_ioctl_buffer_length(unsigned long len)
{

    if (len <= 4 * 1024)
	return (4 * 1024);
    if (len <= 8 * 1024)
	return (8 * 1024);
    if (len <= 32 * 1024)
	return (32 * 1024);
    if (len <= 64 * 1024)
	return (64 * 1024);
    return (len);
}

int
amr_linux_ioctl_int(struct cdev *dev, u_long cmd, caddr_t addr, int32_t flag,
    struct thread *td)
{
    struct amr_softc		*sc = (struct amr_softc *)dev->si_drv1;
    struct amr_command		*ac;
    struct amr_mailbox		*mb;
    struct amr_linux_ioctl	ali;
    void			*dp, *temp;
    int				error;
    int				len, ac_flags = 0;
    int				logical_drives_changed = 0;
    u_int32_t			linux_version = 0x02100000;
    u_int8_t			status;
    struct amr_passthrough	*ap;	/* 60 bytes */

    error = 0;
    dp = NULL;
    ac = NULL;
    ap = NULL;

    if ((error = copyin(addr, &ali, sizeof(ali))) != 0)
	return (error);
    switch (ali.ui.fcs.opcode) {
    case 0x82:
	switch(ali.ui.fcs.subopcode) {
	case 'e':
	    copyout(&linux_version, (void *)(uintptr_t)ali.data,
		sizeof(linux_version));
	    error = 0;
	    break;

	case 'm':
	    copyout(&linux_no_adapter, (void *)(uintptr_t)ali.data,
		sizeof(linux_no_adapter));
	    td->td_retval[0] = linux_no_adapter;
	    error = 0;
	    break;

	default:
	    printf("Unknown subopcode\n");
	    error = ENOIOCTL;
	    break;
	}
    break;

    case 0x80:
    case 0x81:
	if (ali.ui.fcs.opcode == 0x80)
	    len = max(ali.outlen, ali.inlen);
	else
	    len = ali.ui.fcs.length;

	mb = (void *)&ali.mbox[0];

	if ((ali.mbox[0] == FC_DEL_LOGDRV  && ali.mbox[2] == OP_DEL_LOGDRV) ||	/* delete */
	    (ali.mbox[0] == AMR_CMD_CONFIG && ali.mbox[2] == 0x0d)) {		/* create */
	    if (sc->amr_allow_vol_config == 0) {
		error = EPERM;
		break;
	    }
	    logical_drives_changed = 1;
	}

	if (ali.mbox[0] == AMR_CMD_PASS) {
	    mtx_lock(&sc->amr_list_lock); 
	    while ((ac = amr_alloccmd(sc)) == NULL)
		msleep(sc, &sc->amr_list_lock, PPAUSE, "amrioc", hz);
	    mtx_unlock(&sc->amr_list_lock);
	    ap = &ac->ac_ccb->ccb_pthru;

	    error = copyin((void *)(uintptr_t)mb->mb_physaddr, ap,
		sizeof(struct amr_passthrough));
	    if (error)
		break;

	    if (ap->ap_data_transfer_length)
		dp = malloc(ap->ap_data_transfer_length, M_AMR,
		    M_WAITOK | M_ZERO);

	    if (ali.inlen) {
		error = copyin((void *)(uintptr_t)ap->ap_data_transfer_address,
		    dp, ap->ap_data_transfer_length);
		if (error)
		    break;
	    }

	    ac_flags = AMR_CMD_DATAIN|AMR_CMD_DATAOUT|AMR_CMD_CCB;
	    bzero(&ac->ac_mailbox, sizeof(ac->ac_mailbox));
	    ac->ac_mailbox.mb_command = AMR_CMD_PASS;
	    ac->ac_flags = ac_flags;

	    ac->ac_data = dp;
	    ac->ac_length = ap->ap_data_transfer_length;
	    temp = (void *)(uintptr_t)ap->ap_data_transfer_address;

	    mtx_lock(&sc->amr_list_lock);
	    error = amr_wait_command(ac);
	    mtx_unlock(&sc->amr_list_lock);
	    if (error)
		break;

	    status = ac->ac_status;
	    error = copyout(&status, &((struct amr_passthrough *)(uintptr_t)mb->mb_physaddr)->ap_scsi_status, sizeof(status));
	    if (error)
		break;

	    if (ali.outlen) {
		error = copyout(dp, temp, ap->ap_data_transfer_length);
	        if (error)
		    break;
	    }
	    error = copyout(ap->ap_request_sense_area, ((struct amr_passthrough *)(uintptr_t)mb->mb_physaddr)->ap_request_sense_area, ap->ap_request_sense_length);
	    if (error)
		break;

	    error = 0;
	    break;
	} else if (ali.mbox[0] == AMR_CMD_PASS_64) {
	    printf("No AMR_CMD_PASS_64\n");
	    error = ENOIOCTL;
	    break;
	} else if (ali.mbox[0] == AMR_CMD_EXTPASS) {
	    printf("No AMR_CMD_EXTPASS\n");
	    error = ENOIOCTL;
	    break;
	} else {
	    len = amr_ioctl_buffer_length(imax(ali.inlen, ali.outlen));

	    dp = malloc(len, M_AMR, M_WAITOK | M_ZERO);

	    if (ali.inlen) {
		error = copyin((void *)(uintptr_t)mb->mb_physaddr, dp, len);
		if (error)
		    break;
	    }

	    mtx_lock(&sc->amr_list_lock); 
	    while ((ac = amr_alloccmd(sc)) == NULL)
		msleep(sc, &sc->amr_list_lock, PPAUSE, "amrioc", hz);

	    ac_flags = AMR_CMD_DATAIN|AMR_CMD_DATAOUT;
	    bzero(&ac->ac_mailbox, sizeof(ac->ac_mailbox));
	    bcopy(&ali.mbox[0], &ac->ac_mailbox, sizeof(ali.mbox));

	    ac->ac_length = len;
	    ac->ac_data = dp;
	    ac->ac_flags = ac_flags;

	    error = amr_wait_command(ac);
	    mtx_unlock(&sc->amr_list_lock); 
	    if (error)
		break;

	    status = ac->ac_status;
	    error = copyout(&status, &((struct amr_mailbox *)&((struct amr_linux_ioctl *)addr)->mbox[0])->mb_status, sizeof(status));
	    if (ali.outlen) {
		error = copyout(dp, (void *)(uintptr_t)mb->mb_physaddr, ali.outlen);
		if (error)
		    break;
	    }

	    error = 0;
	    if (logical_drives_changed)
		amr_rescan_drives(dev);
	    break;
	}
	break;

    default:
	debug(1, "unknown linux ioctl 0x%lx", cmd);
	printf("unknown linux ioctl 0x%lx\n", cmd);
	error = ENOIOCTL;
	break;
    }

    /*
     * At this point, we know that there is a lock held and that these
     * objects have been allocated.
     */
    mtx_lock(&sc->amr_list_lock);
    if (ac != NULL)
	amr_releasecmd(ac);
    mtx_unlock(&sc->amr_list_lock);
    if (dp != NULL)
	free(dp, M_AMR);
    return(error);
}

static int
amr_ioctl(struct cdev *dev, u_long cmd, caddr_t addr, int32_t flag, struct thread *td)
{
    struct amr_softc		*sc = (struct amr_softc *)dev->si_drv1;
    union {
	void			*_p;
	struct amr_user_ioctl	*au;
#ifdef AMR_IO_COMMAND32
	struct amr_user_ioctl32	*au32;
#endif
	int			*result;
    } arg;
    struct amr_command		*ac;
    struct amr_mailbox_ioctl	*mbi;
    void			*dp, *au_buffer;
    unsigned long		au_length, real_length;
    unsigned char		*au_cmd;
    int				*au_statusp;
    int				error;
    struct amr_passthrough	*ap;	/* 60 bytes */
    int				logical_drives_changed = 0;

    debug_called(1);

    arg._p = (void *)addr;

    error = 0;
    dp = NULL;
    ac = NULL;
    ap = NULL;

    switch(cmd) {

    case AMR_IO_VERSION:
	debug(1, "AMR_IO_VERSION");
	*arg.result = AMR_IO_VERSION_NUMBER;
	return(0);

#ifdef AMR_IO_COMMAND32
    /*
     * Accept ioctl-s from 32-bit binaries on non-32-bit
     * platforms, such as AMD. LSI's MEGAMGR utility is
     * the only example known today...	-mi
     */
    case AMR_IO_COMMAND32:
	debug(1, "AMR_IO_COMMAND32 0x%x", arg.au32->au_cmd[0]);
	au_cmd = arg.au32->au_cmd;
	au_buffer = (void *)(u_int64_t)arg.au32->au_buffer;
	au_length = arg.au32->au_length;
	au_statusp = &arg.au32->au_status;
	break;
#endif

    case AMR_IO_COMMAND:
	debug(1, "AMR_IO_COMMAND  0x%x", arg.au->au_cmd[0]);
	au_cmd = arg.au->au_cmd;
	au_buffer = (void *)arg.au->au_buffer;
	au_length = arg.au->au_length;
	au_statusp = &arg.au->au_status;
	break;

    case 0xc0046d00:
    case 0xc06e6d00:	/* Linux emulation */
	{
	    devclass_t			devclass;
	    struct amr_linux_ioctl	ali;
	    int				adapter, error;

	    devclass = devclass_find("amr");
	    if (devclass == NULL)
		return (ENOENT);

	    error = copyin(addr, &ali, sizeof(ali));
	    if (error)
		return (error);
	    if (ali.ui.fcs.opcode == 0x82)
		adapter = 0;
	    else
		adapter = (ali.ui.fcs.adapno) ^ 'm' << 8;

	    sc = devclass_get_softc(devclass, adapter);
	    if (sc == NULL)
		return (ENOENT);

	    return (amr_linux_ioctl_int(sc->amr_dev_t, cmd, addr, 0, td));
	}
    default:
	debug(1, "unknown ioctl 0x%lx", cmd);
	return(ENOIOCTL);
    }

    if ((au_cmd[0] == FC_DEL_LOGDRV && au_cmd[1] == OP_DEL_LOGDRV) ||	/* delete */
	(au_cmd[0] == AMR_CMD_CONFIG && au_cmd[1] == 0x0d)) {		/* create */
	if (sc->amr_allow_vol_config == 0) {
	    error = EPERM;
	    goto out;
	}
	logical_drives_changed = 1;
#ifdef LSI
	if ((error = amr_prepare_ld_delete(sc)) != 0)
	    return (error);
#endif
    }

    /* handle inbound data buffer */
    real_length = amr_ioctl_buffer_length(au_length);
    dp = malloc(real_length, M_AMR, M_WAITOK|M_ZERO);
    if (au_length != 0 && au_cmd[0] != 0x06) {
	if ((error = copyin(au_buffer, dp, au_length)) != 0) {
	    free(dp, M_AMR);
	    return (error);
	}
	debug(2, "copyin %ld bytes from %p -> %p", au_length, au_buffer, dp);
    }

    /* Allocate this now before the mutex gets held */

    mtx_lock(&sc->amr_list_lock); 
    while ((ac = amr_alloccmd(sc)) == NULL)
	msleep(sc, &sc->amr_list_lock, PPAUSE, "amrioc", hz);

    /* handle SCSI passthrough command */
    if (au_cmd[0] == AMR_CMD_PASS) {
        int len;

	ap = &ac->ac_ccb->ccb_pthru;
	bzero(ap, sizeof(struct amr_passthrough));

	/* copy cdb */
        len = au_cmd[2];
	ap->ap_cdb_length = len;
	bcopy(au_cmd + 3, ap->ap_cdb, len);

	/* build passthrough */
	ap->ap_timeout		= au_cmd[len + 3] & 0x07;
	ap->ap_ars		= (au_cmd[len + 3] & 0x08) ? 1 : 0;
	ap->ap_islogical		= (au_cmd[len + 3] & 0x80) ? 1 : 0;
	ap->ap_logical_drive_no	= au_cmd[len + 4];
	ap->ap_channel		= au_cmd[len + 5];
	ap->ap_scsi_id 		= au_cmd[len + 6];
	ap->ap_request_sense_length	= 14;
	ap->ap_data_transfer_length	= au_length;
	/* XXX what about the request-sense area? does the caller want it? */

	/* build command */
	ac->ac_mailbox.mb_command = AMR_CMD_PASS;
	ac->ac_flags = AMR_CMD_CCB;

    } else {
	/* direct command to controller */
	mbi = (struct amr_mailbox_ioctl *)&ac->ac_mailbox;

	/* copy pertinent mailbox items */
	mbi->mb_command = au_cmd[0];
	mbi->mb_channel = au_cmd[1];
	mbi->mb_param = au_cmd[2];
	mbi->mb_pad[0] = au_cmd[3];
	mbi->mb_drive = au_cmd[4];
	ac->ac_flags = 0;
    }

    /* build the command */
    ac->ac_data = dp;
    ac->ac_length = real_length;
    ac->ac_flags |= AMR_CMD_DATAIN|AMR_CMD_DATAOUT;

    /* run the command */
    error = amr_wait_command(ac);
    mtx_unlock(&sc->amr_list_lock); 
    if (error)
	goto out;

    /* copy out data and set status */
    if (au_length != 0) {
	error = copyout(dp, au_buffer, au_length);
    }
    debug(2, "copyout %ld bytes from %p -> %p", au_length, dp, au_buffer);
    debug(2, "%p status 0x%x", dp, ac->ac_status);
    *au_statusp = ac->ac_status;

out:
    /*
     * At this point, we know that there is a lock held and that these
     * objects have been allocated.
     */
    mtx_lock(&sc->amr_list_lock);
    if (ac != NULL)
	amr_releasecmd(ac);
    mtx_unlock(&sc->amr_list_lock);
    if (dp != NULL)
	free(dp, M_AMR);

#ifndef LSI
    if (logical_drives_changed)
	amr_rescan_drives(dev);
#endif

    return(error);
}

/********************************************************************************
 ********************************************************************************
                                                                 Command Wrappers
 ********************************************************************************
 ********************************************************************************/

/********************************************************************************
 * Interrogate the controller for the operational parameters we require.
 */
static int
amr_query_controller(struct amr_softc *sc)
{
    struct amr_enquiry3	*aex;
    struct amr_prodinfo	*ap;
    struct amr_enquiry	*ae;
    int			ldrv;
    int			status;

    /*
     * Greater than 10 byte cdb support
     */
    sc->support_ext_cdb = amr_support_ext_cdb(sc);

    if(sc->support_ext_cdb) {
	debug(2,"supports extended CDBs.");
    }

    /* 
     * Try to issue an ENQUIRY3 command 
     */
    if ((aex = amr_enquiry(sc, 2048, AMR_CMD_CONFIG, AMR_CONFIG_ENQ3, 
			   AMR_CONFIG_ENQ3_SOLICITED_FULL, &status)) != NULL) {

	/*
	 * Fetch current state of logical drives.
	 */
	for (ldrv = 0; ldrv < aex->ae_numldrives; ldrv++) {
	    sc->amr_drive[ldrv].al_size       = aex->ae_drivesize[ldrv];
	    sc->amr_drive[ldrv].al_state      = aex->ae_drivestate[ldrv];
	    sc->amr_drive[ldrv].al_properties = aex->ae_driveprop[ldrv];
	    debug(2, "  drive %d: %d state %x properties %x\n", ldrv, sc->amr_drive[ldrv].al_size,
		  sc->amr_drive[ldrv].al_state, sc->amr_drive[ldrv].al_properties);
	}
	free(aex, M_AMR);

	/*
	 * Get product info for channel count.
	 */
	if ((ap = amr_enquiry(sc, 2048, AMR_CMD_CONFIG, AMR_CONFIG_PRODUCT_INFO, 0, &status)) == NULL) {
	    device_printf(sc->amr_dev, "can't obtain product data from controller\n");
	    return(1);
	}
	sc->amr_maxdrives = 40;
	sc->amr_maxchan = ap->ap_nschan;
	sc->amr_maxio = ap->ap_maxio;
	sc->amr_type |= AMR_TYPE_40LD;
	free(ap, M_AMR);

	ap = amr_enquiry(sc, 0, FC_DEL_LOGDRV, OP_SUP_DEL_LOGDRV, 0, &status);
	if (ap != NULL)
	    free(ap, M_AMR);
	if (!status) {
	    sc->amr_ld_del_supported = 1;
	    device_printf(sc->amr_dev, "delete logical drives supported by controller\n");
	}
    } else {

	/* failed, try the 8LD ENQUIRY commands */
	if ((ae = (struct amr_enquiry *)amr_enquiry(sc, 2048, AMR_CMD_EXT_ENQUIRY2, 0, 0, &status)) == NULL) {
	    if ((ae = (struct amr_enquiry *)amr_enquiry(sc, 2048, AMR_CMD_ENQUIRY, 0, 0, &status)) == NULL) {
		device_printf(sc->amr_dev, "can't obtain configuration data from controller\n");
		return(1);
	    }
	    ae->ae_signature = 0;
	}

	/*
	 * Fetch current state of logical drives.
	 */
	for (ldrv = 0; ldrv < ae->ae_ldrv.al_numdrives; ldrv++) {
	    sc->amr_drive[ldrv].al_size       = ae->ae_ldrv.al_size[ldrv];
	    sc->amr_drive[ldrv].al_state      = ae->ae_ldrv.al_state[ldrv];
	    sc->amr_drive[ldrv].al_properties = ae->ae_ldrv.al_properties[ldrv];
	    debug(2, "  drive %d: %d state %x properties %x\n", ldrv, sc->amr_drive[ldrv].al_size,
		  sc->amr_drive[ldrv].al_state, sc->amr_drive[ldrv].al_properties);
	}

	sc->amr_maxdrives = 8;
	sc->amr_maxchan = ae->ae_adapter.aa_channels;
	sc->amr_maxio = ae->ae_adapter.aa_maxio;
	free(ae, M_AMR);
    }

    /*
     * Mark remaining drives as unused.
     */
    for (; ldrv < AMR_MAXLD; ldrv++)
	sc->amr_drive[ldrv].al_size = 0xffffffff;

    /* 
     * Cap the maximum number of outstanding I/Os.  AMI's Linux driver doesn't trust
     * the controller's reported value, and lockups have been seen when we do.
     */
    sc->amr_maxio = imin(sc->amr_maxio, AMR_LIMITCMD);

    return(0);
}

/********************************************************************************
 * Run a generic enquiry-style command.
 */
static void *
amr_enquiry(struct amr_softc *sc, size_t bufsize, u_int8_t cmd, u_int8_t cmdsub, u_int8_t cmdqual, int *status)
{
    struct amr_command	*ac;
    void		*result;
    u_int8_t		*mbox;
    int			error;

    debug_called(1);

    error = 1;
    result = NULL;
    
    /* get ourselves a command buffer */
    mtx_lock(&sc->amr_list_lock);
    ac = amr_alloccmd(sc);
    mtx_unlock(&sc->amr_list_lock);
    if (ac == NULL)
	goto out;
    /* allocate the response structure */
    if ((result = malloc(bufsize, M_AMR, M_ZERO|M_NOWAIT)) == NULL)
	goto out;
    /* set command flags */

    ac->ac_flags |= AMR_CMD_PRIORITY | AMR_CMD_DATAIN;
    
    /* point the command at our data */
    ac->ac_data = result;
    ac->ac_length = bufsize;
    
    /* build the command proper */
    mbox = (u_int8_t *)&ac->ac_mailbox;		/* XXX want a real structure for this? */
    mbox[0] = cmd;
    mbox[2] = cmdsub;
    mbox[3] = cmdqual;
    *status = 0;

    /* can't assume that interrupts are going to work here, so play it safe */
    if (sc->amr_poll_command(ac))
	goto out;
    error = ac->ac_status;
    *status = ac->ac_status;
    
 out:
    mtx_lock(&sc->amr_list_lock);
    if (ac != NULL)
	amr_releasecmd(ac);
    mtx_unlock(&sc->amr_list_lock);
    if ((error != 0) && (result != NULL)) {
	free(result, M_AMR);
	result = NULL;
    }
    return(result);
}

/********************************************************************************
 * Flush the controller's internal cache, return status.
 */
int
amr_flush(struct amr_softc *sc)
{
    struct amr_command	*ac;
    int			error;

    /* get ourselves a command buffer */
    error = 1;
    mtx_lock(&sc->amr_list_lock);
    ac = amr_alloccmd(sc);
    mtx_unlock(&sc->amr_list_lock);
    if (ac == NULL)
	goto out;
    /* set command flags */
    ac->ac_flags |= AMR_CMD_PRIORITY | AMR_CMD_DATAOUT;
    
    /* build the command proper */
    ac->ac_mailbox.mb_command = AMR_CMD_FLUSH;

    /* we have to poll, as the system may be going down or otherwise damaged */
    if (sc->amr_poll_command(ac))
	goto out;
    error = ac->ac_status;
    
 out:
    mtx_lock(&sc->amr_list_lock);
    if (ac != NULL)
	amr_releasecmd(ac);
    mtx_unlock(&sc->amr_list_lock);
    return(error);
}

/********************************************************************************
 * Detect extented cdb >> greater than 10 byte cdb support
 * returns '1' means this support exist
 * returns '0' means this support doesn't exist
 */
static int
amr_support_ext_cdb(struct amr_softc *sc)
{
    struct amr_command	*ac;
    u_int8_t		*mbox;
    int			error;

    /* get ourselves a command buffer */
    error = 0;
    mtx_lock(&sc->amr_list_lock);
    ac = amr_alloccmd(sc);
    mtx_unlock(&sc->amr_list_lock);
    if (ac == NULL)
	goto out;
    /* set command flags */
    ac->ac_flags |= AMR_CMD_PRIORITY | AMR_CMD_DATAOUT;

    /* build the command proper */
    mbox = (u_int8_t *)&ac->ac_mailbox;		/* XXX want a real structure for this? */
    mbox[0] = 0xA4;
    mbox[2] = 0x16;


    /* we have to poll, as the system may be going down or otherwise damaged */
    if (sc->amr_poll_command(ac))
	goto out;
    if( ac->ac_status == AMR_STATUS_SUCCESS ) {
	    error = 1;
    }

out:
    mtx_lock(&sc->amr_list_lock);
    if (ac != NULL)
	amr_releasecmd(ac);
    mtx_unlock(&sc->amr_list_lock);
    return(error);
}

/********************************************************************************
 * Try to find I/O work for the controller from one or more of the work queues.
 *
 * We make the assumption that if the controller is not ready to take a command
 * at some given time, it will generate an interrupt at some later time when
 * it is.
 */
void
amr_startio(struct amr_softc *sc)
{
    struct amr_command	*ac;

    /* spin until something prevents us from doing any work */
    for (;;) {

	/* Don't bother to queue commands no bounce buffers are available. */
	if (sc->amr_state & AMR_STATE_QUEUE_FRZN)
	    break;

	/* try to get a ready command */
	ac = amr_dequeue_ready(sc);

	/* if that failed, build a command from a bio */
	if (ac == NULL)
	    (void)amr_bio_command(sc, &ac);

	/* if that failed, build a command from a ccb */
	if ((ac == NULL) && (sc->amr_cam_command != NULL))
	    sc->amr_cam_command(sc, &ac);

	/* if we don't have anything to do, give up */
	if (ac == NULL)
	    break;

	/* try to give the command to the controller; if this fails save it for later and give up */
	if (amr_start(ac)) {
	    debug(2, "controller busy, command deferred");
	    amr_requeue_ready(ac);	/* XXX schedule retry very soon? */
	    break;
	}
    }
}

/********************************************************************************
 * Handle completion of an I/O command.
 */
static void
amr_completeio(struct amr_command *ac)
{
    struct amrd_softc		*sc = ac->ac_bio->bio_disk->d_drv1;
    static struct timeval	lastfail;
    static int			curfail;

    if (ac->ac_status != AMR_STATUS_SUCCESS) {	/* could be more verbose here? */
	ac->ac_bio->bio_error = EIO;
	ac->ac_bio->bio_flags |= BIO_ERROR;

	if (ppsratecheck(&lastfail, &curfail, 1))
	    device_printf(sc->amrd_dev, "I/O error - 0x%x\n", ac->ac_status);
/*	amr_printcommand(ac);*/
    }
    amrd_intr(ac->ac_bio);
    mtx_lock(&ac->ac_sc->amr_list_lock);
    amr_releasecmd(ac);
    mtx_unlock(&ac->ac_sc->amr_list_lock);
}

/********************************************************************************
 ********************************************************************************
                                                               Command Processing
 ********************************************************************************
 ********************************************************************************/

/********************************************************************************
 * Convert a bio off the top of the bio queue into a command.
 */
static int
amr_bio_command(struct amr_softc *sc, struct amr_command **acp)
{
    struct amr_command	*ac;
    struct amrd_softc	*amrd;
    struct bio		*bio;
    int			error;
    int			blkcount;
    int			driveno;
    int			cmd;

    ac = NULL;
    error = 0;

    /* get a command */
    if ((ac = amr_alloccmd(sc)) == NULL)
	return (ENOMEM);

    /* get a bio to work on */
    if ((bio = amr_dequeue_bio(sc)) == NULL) {
	amr_releasecmd(ac);
	return (0);
    }

    /* connect the bio to the command */
    ac->ac_complete = amr_completeio;
    ac->ac_bio = bio;
    ac->ac_data = bio->bio_data;
    ac->ac_length = bio->bio_bcount;
    cmd = 0;
    switch (bio->bio_cmd) {
    case BIO_READ:
	ac->ac_flags |= AMR_CMD_DATAIN;
	if (AMR_IS_SG64(sc)) {
	    cmd = AMR_CMD_LREAD64;
	    ac->ac_flags |= AMR_CMD_SG64;
	} else
	    cmd = AMR_CMD_LREAD;
	break;
    case BIO_WRITE:
	ac->ac_flags |= AMR_CMD_DATAOUT;
	if (AMR_IS_SG64(sc)) {
	    cmd = AMR_CMD_LWRITE64;
	    ac->ac_flags |= AMR_CMD_SG64;
	} else
	    cmd = AMR_CMD_LWRITE;
	break;
    case BIO_FLUSH:
	ac->ac_flags |= AMR_CMD_PRIORITY | AMR_CMD_DATAOUT;
	cmd = AMR_CMD_FLUSH;
	break;
    }
    amrd = (struct amrd_softc *)bio->bio_disk->d_drv1;
    driveno = amrd->amrd_drive - sc->amr_drive;
    blkcount = howmany(bio->bio_bcount, AMR_BLKSIZE);

    ac->ac_mailbox.mb_command = cmd;
    if (bio->bio_cmd == BIO_READ || bio->bio_cmd == BIO_WRITE) {
	ac->ac_mailbox.mb_blkcount = blkcount;
	ac->ac_mailbox.mb_lba = bio->bio_pblkno;
	if ((bio->bio_pblkno + blkcount) > sc->amr_drive[driveno].al_size) {
	    device_printf(sc->amr_dev,
			  "I/O beyond end of unit (%lld,%d > %lu)\n", 
			  (long long)bio->bio_pblkno, blkcount,
			  (u_long)sc->amr_drive[driveno].al_size);
	}
    }
    ac->ac_mailbox.mb_drive = driveno;
    if (sc->amr_state & AMR_STATE_REMAP_LD)
	ac->ac_mailbox.mb_drive |= 0x80;

    /* we fill in the s/g related data when the command is mapped */


    *acp = ac;
    return(error);
}

/********************************************************************************
 * Take a command, submit it to the controller and sleep until it completes
 * or fails.  Interrupts must be enabled, returns nonzero on error.
 */
static int
amr_wait_command(struct amr_command *ac)
{
    int			error = 0;
    struct amr_softc	*sc = ac->ac_sc;

    debug_called(1);

    ac->ac_complete = NULL;
    ac->ac_flags |= AMR_CMD_SLEEP;
    if ((error = amr_start(ac)) != 0) {
	return(error);
    }
    
    while ((ac->ac_flags & AMR_CMD_BUSY) && (error != EWOULDBLOCK)) {
	error = msleep(ac,&sc->amr_list_lock, PRIBIO, "amrwcmd", 0);
    }

    return(error);
}

/********************************************************************************
 * Take a command, submit it to the controller and busy-wait for it to return.
 * Returns nonzero on error.  Can be safely called with interrupts enabled.
 */
static int
amr_std_poll_command(struct amr_command *ac)
{
    struct amr_softc	*sc = ac->ac_sc;
    int			error, count;

    debug_called(2);

    ac->ac_complete = NULL;
    if ((error = amr_start(ac)) != 0)
	return(error);

    count = 0;
    do {
	/* 
	 * Poll for completion, although the interrupt handler may beat us to it. 
	 * Note that the timeout here is somewhat arbitrary.
	 */
	amr_done(sc);
	DELAY(1000);
    } while ((ac->ac_flags & AMR_CMD_BUSY) && (count++ < 1000));
    if (!(ac->ac_flags & AMR_CMD_BUSY)) {
	error = 0;
    } else {
	/* XXX the slot is now marked permanently busy */
	error = EIO;
	device_printf(sc->amr_dev, "polled command timeout\n");
    }
    return(error);
}

static void
amr_setup_polled_dmamap(void *arg, bus_dma_segment_t *segs, int nsegs, int err)
{
    struct amr_command *ac = arg;
    struct amr_softc *sc = ac->ac_sc;
    int mb_channel;

    if (err) {
	device_printf(sc->amr_dev, "error %d in %s", err, __FUNCTION__);
	ac->ac_status = AMR_STATUS_ABORTED;
	return;
    }

    amr_setup_sg(arg, segs, nsegs, err);

    /* for AMR_CMD_CONFIG Read/Write the s/g count goes elsewhere */
    mb_channel = ((struct amr_mailbox_ioctl *)&ac->ac_mailbox)->mb_channel;
    if (ac->ac_mailbox.mb_command == AMR_CMD_CONFIG &&
        ((mb_channel == AMR_CONFIG_READ_NVRAM_CONFIG) ||
        (mb_channel == AMR_CONFIG_WRITE_NVRAM_CONFIG)))
	((struct amr_mailbox_ioctl *)&ac->ac_mailbox)->mb_param = ac->ac_nsegments;

    ac->ac_mailbox.mb_nsgelem = ac->ac_nsegments;
    ac->ac_mailbox.mb_physaddr = ac->ac_mb_physaddr;
    if (AC_IS_SG64(ac)) {
	ac->ac_sg64_hi = 0;
	ac->ac_sg64_lo = ac->ac_sgbusaddr;
    }

    sc->amr_poll_command1(sc, ac);
}

/********************************************************************************
 * Take a command, submit it to the controller and busy-wait for it to return.
 * Returns nonzero on error.  Can be safely called with interrupts enabled.
 */
static int
amr_quartz_poll_command(struct amr_command *ac)
{
    struct amr_softc	*sc = ac->ac_sc;
    int			error;

    debug_called(2);

    error = 0;

    if (AC_IS_SG64(ac)) {
	ac->ac_tag = sc->amr_buffer64_dmat;
	ac->ac_datamap = ac->ac_dma64map;
    } else {
	ac->ac_tag = sc->amr_buffer_dmat;
	ac->ac_datamap = ac->ac_dmamap;
    }

    /* now we have a slot, we can map the command (unmapped in amr_complete) */
    if (ac->ac_data != 0) {
	if (bus_dmamap_load(ac->ac_tag, ac->ac_datamap, ac->ac_data,
	    ac->ac_length, amr_setup_polled_dmamap, ac, BUS_DMA_NOWAIT) != 0) {
	    error = 1;
	}
    } else {
	error = amr_quartz_poll_command1(sc, ac);
    }

    return (error);
}

static int
amr_quartz_poll_command1(struct amr_softc *sc, struct amr_command *ac)
{
    int count, error;

    mtx_lock(&sc->amr_hw_lock);
    if ((sc->amr_state & AMR_STATE_INTEN) == 0) {
	count=0;
	while (sc->amr_busyslots) {
	    msleep(sc, &sc->amr_hw_lock, PRIBIO | PCATCH, "amrpoll", hz);
	    if(count++>10) {
		break;
	    }
	}

	if(sc->amr_busyslots) {
	    device_printf(sc->amr_dev, "adapter is busy\n");
	    mtx_unlock(&sc->amr_hw_lock);
	    if (ac->ac_data != NULL) {
		bus_dmamap_unload(ac->ac_tag, ac->ac_datamap);
	    }
    	    ac->ac_status=0;
	    return(1);
	}
    }

    bcopy(&ac->ac_mailbox, (void *)(uintptr_t)(volatile void *)sc->amr_mailbox, AMR_MBOX_CMDSIZE);

    /* clear the poll/ack fields in the mailbox */
    sc->amr_mailbox->mb_ident = 0xFE;
    sc->amr_mailbox->mb_nstatus = 0xFF;
    sc->amr_mailbox->mb_status = 0xFF;
    sc->amr_mailbox->mb_poll = 0;
    sc->amr_mailbox->mb_ack = 0;
    sc->amr_mailbox->mb_busy = 1;

    AMR_QPUT_IDB(sc, sc->amr_mailboxphys | AMR_QIDB_SUBMIT);

    while(sc->amr_mailbox->mb_nstatus == 0xFF)
	DELAY(1);
    while(sc->amr_mailbox->mb_status == 0xFF)
	DELAY(1);
    ac->ac_status=sc->amr_mailbox->mb_status;
    error = (ac->ac_status !=AMR_STATUS_SUCCESS) ? 1:0;
    while(sc->amr_mailbox->mb_poll != 0x77)
	DELAY(1);
    sc->amr_mailbox->mb_poll = 0;
    sc->amr_mailbox->mb_ack = 0x77;

    /* acknowledge that we have the commands */
    AMR_QPUT_IDB(sc, sc->amr_mailboxphys | AMR_QIDB_ACK);
    while(AMR_QGET_IDB(sc) & AMR_QIDB_ACK)
	DELAY(1);
    mtx_unlock(&sc->amr_hw_lock);

    /* unmap the command's data buffer */
    if (ac->ac_flags & AMR_CMD_DATAIN) {
	bus_dmamap_sync(ac->ac_tag, ac->ac_datamap, BUS_DMASYNC_POSTREAD);
    }
    if (ac->ac_flags & AMR_CMD_DATAOUT) {
	bus_dmamap_sync(ac->ac_tag, ac->ac_datamap, BUS_DMASYNC_POSTWRITE);
    }
    bus_dmamap_unload(ac->ac_tag, ac->ac_datamap);

    return(error);
}

static __inline int
amr_freeslot(struct amr_command *ac)
{
    struct amr_softc *sc = ac->ac_sc;
    int			slot;

    debug_called(3);

    slot = ac->ac_slot;
    if (sc->amr_busycmd[slot] == NULL)
	panic("amr: slot %d not busy?\n", slot);

    sc->amr_busycmd[slot] = NULL;
    atomic_subtract_int(&sc->amr_busyslots, 1);

    return (0);
}

/********************************************************************************
 * Map/unmap (ac)'s data in the controller's addressable space as required.
 *
 * These functions may be safely called multiple times on a given command.
 */
static void
amr_setup_sg(void *arg, bus_dma_segment_t *segs, int nsegments, int error)
{
    struct amr_command	*ac = (struct amr_command *)arg;
    struct amr_sgentry	*sg;
    struct amr_sg64entry *sg64;
    int flags, i;

    debug_called(3);

    /* get base address of s/g table */
    sg = ac->ac_sg.sg32;
    sg64 = ac->ac_sg.sg64;

    if (AC_IS_SG64(ac)) {
	ac->ac_nsegments = nsegments;
	ac->ac_mb_physaddr = 0xffffffff;
	for (i = 0; i < nsegments; i++, sg64++) {
	    sg64->sg_addr = segs[i].ds_addr;
	    sg64->sg_count = segs[i].ds_len;
	}
    } else {
	/* decide whether we need to populate the s/g table */
	if (nsegments < 2) {
	    ac->ac_nsegments = 0;
	    ac->ac_mb_physaddr = segs[0].ds_addr;
	} else {
            ac->ac_nsegments = nsegments;
	    ac->ac_mb_physaddr = ac->ac_sgbusaddr;
	    for (i = 0; i < nsegments; i++, sg++) {
		sg->sg_addr = segs[i].ds_addr;
		sg->sg_count = segs[i].ds_len;
	    }
	}
    }

    flags = 0;
    if (ac->ac_flags & AMR_CMD_DATAIN)
	flags |= BUS_DMASYNC_PREREAD;
    if (ac->ac_flags & AMR_CMD_DATAOUT)
	flags |= BUS_DMASYNC_PREWRITE;
    bus_dmamap_sync(ac->ac_tag, ac->ac_datamap, flags);
    ac->ac_flags |= AMR_CMD_MAPPED;
}

static void
amr_setup_data(void *arg, bus_dma_segment_t *segs, int nsegs, int err)
{
    struct amr_command *ac = arg;
    struct amr_softc *sc = ac->ac_sc;
    int mb_channel;

    if (err) {
	device_printf(sc->amr_dev, "error %d in %s", err, __FUNCTION__);
	amr_abort_load(ac);
	return;
    }

    amr_setup_sg(arg, segs, nsegs, err);

    /* for AMR_CMD_CONFIG Read/Write the s/g count goes elsewhere */
    mb_channel = ((struct amr_mailbox_ioctl *)&ac->ac_mailbox)->mb_channel;
    if (ac->ac_mailbox.mb_command == AMR_CMD_CONFIG &&
        ((mb_channel == AMR_CONFIG_READ_NVRAM_CONFIG) ||
        (mb_channel == AMR_CONFIG_WRITE_NVRAM_CONFIG)))
	((struct amr_mailbox_ioctl *)&ac->ac_mailbox)->mb_param = ac->ac_nsegments;

    ac->ac_mailbox.mb_nsgelem = ac->ac_nsegments;
    ac->ac_mailbox.mb_physaddr = ac->ac_mb_physaddr;
    if (AC_IS_SG64(ac)) {
	ac->ac_sg64_hi = 0;
	ac->ac_sg64_lo = ac->ac_sgbusaddr;
    }

    if (sc->amr_submit_command(ac) == EBUSY) {
	amr_freeslot(ac);
	amr_requeue_ready(ac);
    }
}
 
static void
amr_setup_ccb(void *arg, bus_dma_segment_t *segs, int nsegs, int err)
{
    struct amr_command *ac = arg;
    struct amr_softc *sc = ac->ac_sc;
    struct amr_passthrough *ap = &ac->ac_ccb->ccb_pthru;
    struct amr_ext_passthrough *aep = &ac->ac_ccb->ccb_epthru;

    if (err) {
	device_printf(sc->amr_dev, "error %d in %s", err, __FUNCTION__);
	amr_abort_load(ac);
	return;
    }

    /* Set up the mailbox portion of the command to point at the ccb */
    ac->ac_mailbox.mb_nsgelem = 0;
    ac->ac_mailbox.mb_physaddr = ac->ac_ccb_busaddr;

    amr_setup_sg(arg, segs, nsegs, err);

    switch (ac->ac_mailbox.mb_command) {
    case AMR_CMD_EXTPASS:
	aep->ap_no_sg_elements = ac->ac_nsegments;
	aep->ap_data_transfer_address = ac->ac_mb_physaddr;
        break;
    case AMR_CMD_PASS:
	ap->ap_no_sg_elements = ac->ac_nsegments;
	ap->ap_data_transfer_address = ac->ac_mb_physaddr;
	break;
    default:
	panic("Unknown ccb command");
    }

    if (sc->amr_submit_command(ac) == EBUSY) {
	amr_freeslot(ac);
	amr_requeue_ready(ac);
    }
}

static int
amr_mapcmd(struct amr_command *ac)
{
    bus_dmamap_callback_t *cb;
    struct amr_softc	*sc = ac->ac_sc;

    debug_called(3);

    if (AC_IS_SG64(ac)) {
	ac->ac_tag = sc->amr_buffer64_dmat;
	ac->ac_datamap = ac->ac_dma64map;
    } else {
	ac->ac_tag = sc->amr_buffer_dmat;
	ac->ac_datamap = ac->ac_dmamap;
    }

    if (ac->ac_flags & AMR_CMD_CCB)
	cb = amr_setup_ccb;
    else
	cb = amr_setup_data;

    /* if the command involves data at all, and hasn't been mapped */
    if ((ac->ac_flags & AMR_CMD_MAPPED) == 0 && (ac->ac_data != NULL)) {
	/* map the data buffers into bus space and build the s/g list */
	if (bus_dmamap_load(ac->ac_tag, ac->ac_datamap, ac->ac_data,
	     ac->ac_length, cb, ac, 0) == EINPROGRESS) {
	    sc->amr_state |= AMR_STATE_QUEUE_FRZN;
	}
   } else {
    	if (sc->amr_submit_command(ac) == EBUSY) {
	    amr_freeslot(ac);
	    amr_requeue_ready(ac);
	}
   }

    return (0);
}

static void
amr_unmapcmd(struct amr_command *ac)
{
    int			flag;

    debug_called(3);

    /* if the command involved data at all and was mapped */
    if (ac->ac_flags & AMR_CMD_MAPPED) {

	if (ac->ac_data != NULL) {

	    flag = 0;
	    if (ac->ac_flags & AMR_CMD_DATAIN)
		flag |= BUS_DMASYNC_POSTREAD;
	    if (ac->ac_flags & AMR_CMD_DATAOUT)
		flag |= BUS_DMASYNC_POSTWRITE;

	    bus_dmamap_sync(ac->ac_tag, ac->ac_datamap, flag);
	    bus_dmamap_unload(ac->ac_tag, ac->ac_datamap);
	}

	ac->ac_flags &= ~AMR_CMD_MAPPED;
    }
}

static void
amr_abort_load(struct amr_command *ac)
{
    ac_qhead_t head;
    struct amr_softc *sc = ac->ac_sc;

    mtx_assert(&sc->amr_list_lock, MA_OWNED);

    ac->ac_status = AMR_STATUS_ABORTED;
    amr_init_qhead(&head);
    amr_enqueue_completed(ac, &head);

    mtx_unlock(&sc->amr_list_lock);
    amr_complete(sc, &head);
    mtx_lock(&sc->amr_list_lock);
}

/********************************************************************************
 * Take a command and give it to the controller, returns 0 if successful, or
 * EBUSY if the command should be retried later.
 */
static int
amr_start(struct amr_command *ac)
{
    struct amr_softc *sc;
    int error = 0;
    int slot;

    debug_called(3);

    /* mark command as busy so that polling consumer can tell */
    sc = ac->ac_sc;
    ac->ac_flags |= AMR_CMD_BUSY;

    /* get a command slot (freed in amr_done) */
    slot = ac->ac_slot;
    if (sc->amr_busycmd[slot] != NULL)
	panic("amr: slot %d busy?\n", slot);
    sc->amr_busycmd[slot] = ac;
    atomic_add_int(&sc->amr_busyslots, 1);

    /* Now we have a slot, we can map the command (unmapped in amr_complete). */
    if ((error = amr_mapcmd(ac)) == ENOMEM) {
	/*
	 * Memory resources are short, so free the slot and let this be tried
	 * later.
	 */
	amr_freeslot(ac);
    }

    return (error);
}

/********************************************************************************
 * Extract one or more completed commands from the controller (sc)
 *
 * Returns nonzero if any commands on the work queue were marked as completed.
 */

int
amr_done(struct amr_softc *sc)
{
    ac_qhead_t		head;
    struct amr_command	*ac;
    struct amr_mailbox	mbox;
    int			i, idx, result;
    
    debug_called(3);

    /* See if there's anything for us to do */
    result = 0;
    amr_init_qhead(&head);

    /* loop collecting completed commands */
    for (;;) {
	/* poll for a completed command's identifier and status */
	if (sc->amr_get_work(sc, &mbox)) {
	    result = 1;
	    
	    /* iterate over completed commands in this result */
	    for (i = 0; i < mbox.mb_nstatus; i++) {
		/* get pointer to busy command */
		idx = mbox.mb_completed[i] - 1;
		ac = sc->amr_busycmd[idx];

		/* really a busy command? */
		if (ac != NULL) {

		    /* pull the command from the busy index */
		    amr_freeslot(ac);
		
		    /* save status for later use */
		    ac->ac_status = mbox.mb_status;
		    amr_enqueue_completed(ac, &head);
		    debug(3, "completed command with status %x", mbox.mb_status);
		} else {
		    device_printf(sc->amr_dev, "bad slot %d completed\n", idx);
		}
	    }
	} else
	    break;	/* no work */
    }

    /* handle completion and timeouts */
    amr_complete(sc, &head);

    return(result);
}

/********************************************************************************
 * Do completion processing on done commands on (sc)
 */

static void
amr_complete(void *context, ac_qhead_t *head)
{
    struct amr_softc	*sc = (struct amr_softc *)context;
    struct amr_command	*ac;

    debug_called(3);

    /* pull completed commands off the queue */
    for (;;) {
	ac = amr_dequeue_completed(sc, head);
	if (ac == NULL)
	    break;

	/* unmap the command's data buffer */
	amr_unmapcmd(ac);

	/* 
	 * Is there a completion handler? 
	 */
	if (ac->ac_complete != NULL) {
	    /* unbusy the command */
	    ac->ac_flags &= ~AMR_CMD_BUSY;
	    ac->ac_complete(ac);
	    
	    /* 
	     * Is someone sleeping on this one?
	     */
	} else {
	    mtx_lock(&sc->amr_list_lock);
	    ac->ac_flags &= ~AMR_CMD_BUSY;
	    if (ac->ac_flags & AMR_CMD_SLEEP) {
		/* unbusy the command */
		wakeup(ac);
	    }
	    mtx_unlock(&sc->amr_list_lock);
	}

	if(!sc->amr_busyslots) {
	    wakeup(sc);
	}
    }

    mtx_lock(&sc->amr_list_lock);
    sc->amr_state &= ~AMR_STATE_QUEUE_FRZN;
    amr_startio(sc);
    mtx_unlock(&sc->amr_list_lock);
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
 * If possible, we recycle a command buffer that's been used before.
 */
struct amr_command *
amr_alloccmd(struct amr_softc *sc)
{
    struct amr_command	*ac;

    debug_called(3);

    ac = amr_dequeue_free(sc);
    if (ac == NULL) {
	sc->amr_state |= AMR_STATE_QUEUE_FRZN;
	return(NULL);
    }

    /* clear out significant fields */
    ac->ac_status = 0;
    bzero(&ac->ac_mailbox, sizeof(struct amr_mailbox));
    ac->ac_flags = 0;
    ac->ac_bio = NULL;
    ac->ac_data = NULL;
    ac->ac_complete = NULL;
    ac->ac_retries = 0;
    ac->ac_tag = NULL;
    ac->ac_datamap = NULL;
    return(ac);
}

/********************************************************************************
 * Release a command buffer for recycling.
 */
void
amr_releasecmd(struct amr_command *ac)
{
    debug_called(3);

    amr_enqueue_free(ac);
}

/********************************************************************************
 * Allocate a new command cluster and initialise it.
 */
static void
amr_alloccmd_cluster(struct amr_softc *sc)
{
    struct amr_command_cluster	*acc;
    struct amr_command		*ac;
    int				i, nextslot;

    /* 
     * If we haven't found the real limit yet, let us have a couple of
     * commands in order to be able to probe.
     */
    if (sc->amr_maxio == 0)
	sc->amr_maxio = 2;

    if (sc->amr_nextslot > sc->amr_maxio)
	return;
    acc = malloc(AMR_CMD_CLUSTERSIZE, M_AMR, M_NOWAIT | M_ZERO);
    if (acc != NULL) {
	nextslot = sc->amr_nextslot;
	mtx_lock(&sc->amr_list_lock);
	TAILQ_INSERT_TAIL(&sc->amr_cmd_clusters, acc, acc_link);
	mtx_unlock(&sc->amr_list_lock);
	for (i = 0; i < AMR_CMD_CLUSTERCOUNT; i++) {
	    ac = &acc->acc_command[i];
	    ac->ac_sc = sc;
	    ac->ac_slot = nextslot;

	    /*
	     * The SG table for each slot is a fixed size and is assumed to
	     * to hold 64-bit s/g objects when the driver is configured to do
	     * 64-bit DMA.  32-bit DMA commands still use the same table, but
	     * cast down to 32-bit objects.
	     */
	    if (AMR_IS_SG64(sc)) {
		ac->ac_sgbusaddr = sc->amr_sgbusaddr +
		    (ac->ac_slot * AMR_NSEG * sizeof(struct amr_sg64entry));
	        ac->ac_sg.sg64 = sc->amr_sg64table + (ac->ac_slot * AMR_NSEG);
	    } else {
		ac->ac_sgbusaddr = sc->amr_sgbusaddr +
		    (ac->ac_slot * AMR_NSEG * sizeof(struct amr_sgentry));
	        ac->ac_sg.sg32 = sc->amr_sgtable + (ac->ac_slot * AMR_NSEG);
	    }

	    ac->ac_ccb = sc->amr_ccb + ac->ac_slot;
	    ac->ac_ccb_busaddr = sc->amr_ccb_busaddr +
		(ac->ac_slot * sizeof(union amr_ccb));

	    if (bus_dmamap_create(sc->amr_buffer_dmat, 0, &ac->ac_dmamap))
		break;
	    if (AMR_IS_SG64(sc) &&
		(bus_dmamap_create(sc->amr_buffer64_dmat, 0,&ac->ac_dma64map)))
		break;
	    amr_releasecmd(ac);
	    if (++nextslot > sc->amr_maxio)
		break;
	}
	sc->amr_nextslot = nextslot;
    }
}

/********************************************************************************
 * Free a command cluster
 */
static void
amr_freecmd_cluster(struct amr_command_cluster *acc)
{
    struct amr_softc	*sc = acc->acc_command[0].ac_sc;
    int			i;

    for (i = 0; i < AMR_CMD_CLUSTERCOUNT; i++) {
	if (acc->acc_command[i].ac_sc == NULL)
	    break;
	bus_dmamap_destroy(sc->amr_buffer_dmat, acc->acc_command[i].ac_dmamap);
	if (AMR_IS_SG64(sc))
		bus_dmamap_destroy(sc->amr_buffer64_dmat, acc->acc_command[i].ac_dma64map);
    }
    free(acc, M_AMR);
}

/********************************************************************************
 ********************************************************************************
                                                         Interface-specific Shims
 ********************************************************************************
 ********************************************************************************/

/********************************************************************************
 * Tell the controller that the mailbox contains a valid command
 */
static int
amr_quartz_submit_command(struct amr_command *ac)
{
    struct amr_softc	*sc = ac->ac_sc;
    static struct timeval lastfail;
    static int		curfail;
    int			i = 0;
  
    mtx_lock(&sc->amr_hw_lock);
    while (sc->amr_mailbox->mb_busy && (i++ < 10)) {
        DELAY(1);
	/* This is a no-op read that flushes pending mailbox updates */
	AMR_QGET_ODB(sc);
    }
    if (sc->amr_mailbox->mb_busy) {
	mtx_unlock(&sc->amr_hw_lock);
	if (ac->ac_retries++ > 1000) {
	    if (ppsratecheck(&lastfail, &curfail, 1))
		device_printf(sc->amr_dev, "Too many retries on command %p.  "
			      "Controller is likely dead\n", ac);
	    ac->ac_retries = 0;
	}
	return (EBUSY);
    }

    /* 
     * Save the slot number so that we can locate this command when complete.
     * Note that ident = 0 seems to be special, so we don't use it.
     */
    ac->ac_mailbox.mb_ident = ac->ac_slot + 1; /* will be coppied into mbox */
    bcopy(&ac->ac_mailbox, (void *)(uintptr_t)(volatile void *)sc->amr_mailbox, 14);
    sc->amr_mailbox->mb_busy = 1;
    sc->amr_mailbox->mb_poll = 0;
    sc->amr_mailbox->mb_ack  = 0;
    sc->amr_mailbox64->sg64_hi = ac->ac_sg64_hi;
    sc->amr_mailbox64->sg64_lo = ac->ac_sg64_lo;

    AMR_QPUT_IDB(sc, sc->amr_mailboxphys | AMR_QIDB_SUBMIT);
    mtx_unlock(&sc->amr_hw_lock);
    return(0);
}

static int
amr_std_submit_command(struct amr_command *ac)
{
    struct amr_softc	*sc = ac->ac_sc;
    static struct timeval lastfail;
    static int		curfail;
  
    mtx_lock(&sc->amr_hw_lock);
    if (AMR_SGET_MBSTAT(sc) & AMR_SMBOX_BUSYFLAG) {
	mtx_unlock(&sc->amr_hw_lock);
	if (ac->ac_retries++ > 1000) {
	    if (ppsratecheck(&lastfail, &curfail, 1))
		device_printf(sc->amr_dev, "Too many retries on command %p.  "
			      "Controller is likely dead\n", ac);
	    ac->ac_retries = 0;
	}
	return (EBUSY);
    }

    /* 
     * Save the slot number so that we can locate this command when complete.
     * Note that ident = 0 seems to be special, so we don't use it.
     */
    ac->ac_mailbox.mb_ident = ac->ac_slot + 1; /* will be coppied into mbox */
    bcopy(&ac->ac_mailbox, (void *)(uintptr_t)(volatile void *)sc->amr_mailbox, 14);
    sc->amr_mailbox->mb_busy = 1;
    sc->amr_mailbox->mb_poll = 0;
    sc->amr_mailbox->mb_ack  = 0;

    AMR_SPOST_COMMAND(sc);
    mtx_unlock(&sc->amr_hw_lock);
    return(0);
}

/********************************************************************************
 * Claim any work that the controller has completed; acknowledge completion,
 * save details of the completion in (mbsave)
 */
static int
amr_quartz_get_work(struct amr_softc *sc, struct amr_mailbox *mbsave)
{
    int		worked, i;
    u_int32_t	outd;
    u_int8_t	nstatus;
    u_int8_t	completed[46];

    debug_called(3);

    worked = 0;

    /* work waiting for us? */
    if ((outd = AMR_QGET_ODB(sc)) == AMR_QODB_READY) {

	/* acknowledge interrupt */
	AMR_QPUT_ODB(sc, AMR_QODB_READY);

	while ((nstatus = sc->amr_mailbox->mb_nstatus) == 0xff)
	    DELAY(1);
	sc->amr_mailbox->mb_nstatus = 0xff;

	/* wait until fw wrote out all completions */
	for (i = 0; i < nstatus; i++) {
	    while ((completed[i] = sc->amr_mailbox->mb_completed[i]) == 0xff)
		DELAY(1);
	    sc->amr_mailbox->mb_completed[i] = 0xff;
	}

	/* Save information for later processing */
	mbsave->mb_nstatus = nstatus;
	mbsave->mb_status = sc->amr_mailbox->mb_status;
	sc->amr_mailbox->mb_status = 0xff;

	for (i = 0; i < nstatus; i++)
	    mbsave->mb_completed[i] = completed[i];

	/* acknowledge that we have the commands */
	AMR_QPUT_IDB(sc, AMR_QIDB_ACK);

#if 0
#ifndef AMR_QUARTZ_GOFASTER
	/*
	 * This waits for the controller to notice that we've taken the
	 * command from it.  It's very inefficient, and we shouldn't do it,
	 * but if we remove this code, we stop completing commands under
	 * load.
	 *
	 * Peter J says we shouldn't do this.  The documentation says we
	 * should.  Who is right?
	 */
	while(AMR_QGET_IDB(sc) & AMR_QIDB_ACK)
	    ;				/* XXX aiee! what if it dies? */
#endif
#endif

	worked = 1;			/* got some work */
    }

    return(worked);
}

static int
amr_std_get_work(struct amr_softc *sc, struct amr_mailbox *mbsave)
{
    int		worked;
    u_int8_t	istat;

    debug_called(3);

    worked = 0;

    /* check for valid interrupt status */
    istat = AMR_SGET_ISTAT(sc);
    if ((istat & AMR_SINTR_VALID) != 0) {
	AMR_SPUT_ISTAT(sc, istat);	/* ack interrupt status */

	/* save mailbox, which contains a list of completed commands */
	bcopy((void *)(uintptr_t)(volatile void *)sc->amr_mailbox, mbsave, sizeof(*mbsave));

	AMR_SACK_INTERRUPT(sc);		/* acknowledge we have the mailbox */
	worked = 1;
    }

    return(worked);
}

/********************************************************************************
 * Notify the controller of the mailbox location.
 */
static void
amr_std_attach_mailbox(struct amr_softc *sc)
{

    /* program the mailbox physical address */
    AMR_SBYTE_SET(sc, AMR_SMBOX_0, sc->amr_mailboxphys         & 0xff);
    AMR_SBYTE_SET(sc, AMR_SMBOX_1, (sc->amr_mailboxphys >>  8) & 0xff);
    AMR_SBYTE_SET(sc, AMR_SMBOX_2, (sc->amr_mailboxphys >> 16) & 0xff);
    AMR_SBYTE_SET(sc, AMR_SMBOX_3, (sc->amr_mailboxphys >> 24) & 0xff);
    AMR_SBYTE_SET(sc, AMR_SMBOX_ENABLE, AMR_SMBOX_ADDR);

    /* clear any outstanding interrupt and enable interrupts proper */
    AMR_SACK_INTERRUPT(sc);
    AMR_SENABLE_INTR(sc);
}

#ifdef AMR_BOARD_INIT
/********************************************************************************
 * Initialise the controller
 */
static int
amr_quartz_init(struct amr_softc *sc)
{
    int		status, ostatus;

    device_printf(sc->amr_dev, "initial init status %x\n", AMR_QGET_INITSTATUS(sc));

    AMR_QRESET(sc);

    ostatus = 0xff;
    while ((status = AMR_QGET_INITSTATUS(sc)) != AMR_QINIT_DONE) {
	if (status != ostatus) {
	    device_printf(sc->amr_dev, "(%x) %s\n", status, amr_describe_code(amr_table_qinit, status));
	    ostatus = status;
	}
	switch (status) {
	case AMR_QINIT_NOMEM:
	    return(ENOMEM);

	case AMR_QINIT_SCAN:
	    /* XXX we could print channel/target here */
	    break;
	}
    }
    return(0);
}

static int
amr_std_init(struct amr_softc *sc)
{
    int		status, ostatus;

    device_printf(sc->amr_dev, "initial init status %x\n", AMR_SGET_INITSTATUS(sc));

    AMR_SRESET(sc);
 
    ostatus = 0xff;
    while ((status = AMR_SGET_INITSTATUS(sc)) != AMR_SINIT_DONE) {
	if (status != ostatus) {
	    device_printf(sc->amr_dev, "(%x) %s\n", status, amr_describe_code(amr_table_sinit, status));
	    ostatus = status;
	}
	switch (status) {
	case AMR_SINIT_NOMEM:
	    return(ENOMEM);

	case AMR_SINIT_INPROG:
	    /* XXX we could print channel/target here? */
	    break;
	}
    }
    return(0);
}
#endif

/********************************************************************************
 ********************************************************************************
                                                                        Debugging
 ********************************************************************************
 ********************************************************************************/

/********************************************************************************
 * Identify the controller and print some information about it.
 */
static void
amr_describe_controller(struct amr_softc *sc)
{
    struct amr_prodinfo	*ap;
    struct amr_enquiry	*ae;
    char		*prod;
    int			status;

    /*
     * Try to get 40LD product info, which tells us what the card is labelled as.
     */
    if ((ap = amr_enquiry(sc, 2048, AMR_CMD_CONFIG, AMR_CONFIG_PRODUCT_INFO, 0, &status)) != NULL) {
	device_printf(sc->amr_dev, "<LSILogic %.80s> Firmware %.16s, BIOS %.16s, %dMB RAM\n",
		      ap->ap_product, ap->ap_firmware, ap->ap_bios,
		      ap->ap_memsize);

	free(ap, M_AMR);
	return;
    }

    /*
     * Try 8LD extended ENQUIRY to get controller signature, and use lookup table.
     */
    if ((ae = (struct amr_enquiry *)amr_enquiry(sc, 2048, AMR_CMD_EXT_ENQUIRY2, 0, 0, &status)) != NULL) {
	prod = amr_describe_code(amr_table_adaptertype, ae->ae_signature);

    } else if ((ae = (struct amr_enquiry *)amr_enquiry(sc, 2048, AMR_CMD_ENQUIRY, 0, 0, &status)) != NULL) {

	/*
	 * Try to work it out based on the PCI signatures.
	 */
	switch (pci_get_device(sc->amr_dev)) {
	case 0x9010:
	    prod = "Series 428";
	    break;
	case 0x9060:
	    prod = "Series 434";
	    break;
	default:
	    prod = "unknown controller";
	    break;
	}
    } else {
	device_printf(sc->amr_dev, "<unsupported controller>\n");
	return;
    }

    /*
     * HP NetRaid controllers have a special encoding of the firmware and
     * BIOS versions. The AMI version seems to have it as strings whereas
     * the HP version does it with a leading uppercase character and two
     * binary numbers.
     */
     
    if(ae->ae_adapter.aa_firmware[2] >= 'A' &&
       ae->ae_adapter.aa_firmware[2] <= 'Z' &&
       ae->ae_adapter.aa_firmware[1] <  ' ' &&
       ae->ae_adapter.aa_firmware[0] <  ' ' &&
       ae->ae_adapter.aa_bios[2] >= 'A'     &&
       ae->ae_adapter.aa_bios[2] <= 'Z'     &&
       ae->ae_adapter.aa_bios[1] <  ' '     &&
       ae->ae_adapter.aa_bios[0] <  ' ') {

	/* this looks like we have an HP NetRaid version of the MegaRaid */

    	if(ae->ae_signature == AMR_SIG_438) {
    		/* the AMI 438 is a NetRaid 3si in HP-land */
    		prod = "HP NetRaid 3si";
    	}
    	
	device_printf(sc->amr_dev, "<%s> Firmware %c.%02d.%02d, BIOS %c.%02d.%02d, %dMB RAM\n",
		      prod, ae->ae_adapter.aa_firmware[2],
		      ae->ae_adapter.aa_firmware[1],
		      ae->ae_adapter.aa_firmware[0],
		      ae->ae_adapter.aa_bios[2],
		      ae->ae_adapter.aa_bios[1],
		      ae->ae_adapter.aa_bios[0],
		      ae->ae_adapter.aa_memorysize);		
    } else {
	device_printf(sc->amr_dev, "<%s> Firmware %.4s, BIOS %.4s, %dMB RAM\n", 
		      prod, ae->ae_adapter.aa_firmware, ae->ae_adapter.aa_bios,
		      ae->ae_adapter.aa_memorysize);
    }    	
    free(ae, M_AMR);
}

int
amr_dump_blocks(struct amr_softc *sc, int unit, u_int32_t lba, void *data, int blks)
{
    struct amr_command	*ac;
    int			error = EIO;

    debug_called(1);

    sc->amr_state |= AMR_STATE_INTEN;

    /* get ourselves a command buffer */
    if ((ac = amr_alloccmd(sc)) == NULL)
	goto out;
    /* set command flags */
    ac->ac_flags |= AMR_CMD_PRIORITY | AMR_CMD_DATAOUT;
    
    /* point the command at our data */
    ac->ac_data = data;
    ac->ac_length = blks * AMR_BLKSIZE;
    
    /* build the command proper */
    ac->ac_mailbox.mb_command 	= AMR_CMD_LWRITE;
    ac->ac_mailbox.mb_blkcount	= blks;
    ac->ac_mailbox.mb_lba	= lba;
    ac->ac_mailbox.mb_drive	= unit;

    /* can't assume that interrupts are going to work here, so play it safe */
    if (sc->amr_poll_command(ac))
	goto out;
    error = ac->ac_status;
    
 out:
    if (ac != NULL)
	amr_releasecmd(ac);

    sc->amr_state &= ~AMR_STATE_INTEN;
    return (error);
}



#ifdef AMR_DEBUG
/********************************************************************************
 * Print the command (ac) in human-readable format
 */
#if 0
static void
amr_printcommand(struct amr_command *ac)
{
    struct amr_softc	*sc = ac->ac_sc;
    struct amr_sgentry	*sg;
    int			i;
    
    device_printf(sc->amr_dev, "cmd %x  ident %d  drive %d\n",
		  ac->ac_mailbox.mb_command, ac->ac_mailbox.mb_ident, ac->ac_mailbox.mb_drive);
    device_printf(sc->amr_dev, "blkcount %d  lba %d\n", 
		  ac->ac_mailbox.mb_blkcount, ac->ac_mailbox.mb_lba);
    device_printf(sc->amr_dev, "virtaddr %p  length %lu\n", ac->ac_data, (unsigned long)ac->ac_length);
    device_printf(sc->amr_dev, "sg physaddr %08x  nsg %d\n",
		  ac->ac_mailbox.mb_physaddr, ac->ac_mailbox.mb_nsgelem);
    device_printf(sc->amr_dev, "ccb %p  bio %p\n", ac->ac_ccb_data, ac->ac_bio);

    /* get base address of s/g table */
    sg = sc->amr_sgtable + (ac->ac_slot * AMR_NSEG);
    for (i = 0; i < ac->ac_mailbox.mb_nsgelem; i++, sg++)
	device_printf(sc->amr_dev, "  %x/%d\n", sg->sg_addr, sg->sg_count);
}
#endif
#endif
