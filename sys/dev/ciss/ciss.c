/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Michael Smith
 * Copyright (c) 2004 Paul Saab
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
 * Common Interface for SCSI-3 Support driver.
 *
 * CISS claims to provide a common interface between a generic SCSI
 * transport and an intelligent host adapter.
 *
 * This driver supports CISS as defined in the document "CISS Command
 * Interface for SCSI-3 Support Open Specification", Version 1.04,
 * Valence Number 1, dated 20001127, produced by Compaq Computer
 * Corporation.  This document appears to be a hastily and somewhat
 * arbitrarlily cut-down version of a larger (and probably even more
 * chaotic and inconsistent) Compaq internal document.  Various
 * details were also gleaned from Compaq's "cciss" driver for Linux.
 *
 * We provide a shim layer between the CISS interface and CAM,
 * offloading most of the queueing and being-a-disk chores onto CAM.
 * Entry to the driver is via the PCI bus attachment (ciss_probe,
 * ciss_attach, etc) and via the CAM interface (ciss_cam_action,
 * ciss_cam_poll).  The Compaq CISS adapters are, however, poor SCSI
 * citizens and we have to fake up some responses to get reasonable
 * behaviour out of them.  In addition, the CISS command set is by no
 * means adequate to support the functionality of a RAID controller,
 * and thus the supported Compaq adapters utilise portions of the
 * control protocol from earlier Compaq adapter families.
 *
 * Note that we only support the "simple" transport layer over PCI.
 * This interface (ab)uses the I2O register set (specifically the post
 * queues) to exchange commands with the adapter.  Other interfaces
 * are available, but we aren't supposed to know about them, and it is
 * dubious whether they would provide major performance improvements
 * except under extreme load.
 *
 * Currently the only supported CISS adapters are the Compaq Smart
 * Array 5* series (5300, 5i, 532).  Even with only three adapters,
 * Compaq still manage to have interface variations.
 *
 *
 * Thanks must go to Fred Harris and Darryl DeVinney at Compaq, as
 * well as Paul Saab at Yahoo! for their assistance in making this
 * driver happen.
 *
 * More thanks must go to John Cagle at HP for the countless hours
 * spent making this driver "work" with the MSA* series storage
 * enclosures.  Without his help (and nagging), this driver could not
 * be used with these enclosures.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/stat.h>
#include <sys/kthread.h>
#include <sys/queue.h>
#include <sys/sysctl.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_periph.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>

#include <machine/bus.h>
#include <machine/endian.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/ciss/cissreg.h>
#include <dev/ciss/cissio.h>
#include <dev/ciss/cissvar.h>

static MALLOC_DEFINE(CISS_MALLOC_CLASS, "ciss_data",
    "ciss internal data buffers");

/* pci interface */
static int	ciss_lookup(device_t dev);
static int	ciss_probe(device_t dev);
static int	ciss_attach(device_t dev);
static int	ciss_detach(device_t dev);
static int	ciss_shutdown(device_t dev);

/* (de)initialisation functions, control wrappers */
static int	ciss_init_pci(struct ciss_softc *sc);
static int	ciss_setup_msix(struct ciss_softc *sc);
static int	ciss_init_perf(struct ciss_softc *sc);
static int	ciss_wait_adapter(struct ciss_softc *sc);
static int	ciss_flush_adapter(struct ciss_softc *sc);
static int	ciss_init_requests(struct ciss_softc *sc);
static void	ciss_command_map_helper(void *arg, bus_dma_segment_t *segs,
					int nseg, int error);
static int	ciss_identify_adapter(struct ciss_softc *sc);
static int	ciss_init_logical(struct ciss_softc *sc);
static int	ciss_init_physical(struct ciss_softc *sc);
static int	ciss_filter_physical(struct ciss_softc *sc, struct ciss_lun_report *cll);
static int	ciss_identify_logical(struct ciss_softc *sc, struct ciss_ldrive *ld);
static int	ciss_get_ldrive_status(struct ciss_softc *sc,  struct ciss_ldrive *ld);
static int	ciss_update_config(struct ciss_softc *sc);
static int	ciss_accept_media(struct ciss_softc *sc, struct ciss_ldrive *ld);
static void	ciss_init_sysctl(struct ciss_softc *sc);
static void	ciss_soft_reset(struct ciss_softc *sc);
static void	ciss_free(struct ciss_softc *sc);
static void	ciss_spawn_notify_thread(struct ciss_softc *sc);
static void	ciss_kill_notify_thread(struct ciss_softc *sc);

/* request submission/completion */
static int	ciss_start(struct ciss_request *cr);
static void	ciss_done(struct ciss_softc *sc, cr_qhead_t *qh);
static void	ciss_perf_done(struct ciss_softc *sc, cr_qhead_t *qh);
static void	ciss_intr(void *arg);
static void	ciss_perf_intr(void *arg);
static void	ciss_perf_msi_intr(void *arg);
static void	ciss_complete(struct ciss_softc *sc, cr_qhead_t *qh);
static int	_ciss_report_request(struct ciss_request *cr, int *command_status, int *scsi_status, const char *func);
static int	ciss_synch_request(struct ciss_request *cr, int timeout);
static int	ciss_poll_request(struct ciss_request *cr, int timeout);
static int	ciss_wait_request(struct ciss_request *cr, int timeout);
#if 0
static int	ciss_abort_request(struct ciss_request *cr);
#endif

/* request queueing */
static int	ciss_get_request(struct ciss_softc *sc, struct ciss_request **crp);
static void	ciss_preen_command(struct ciss_request *cr);
static void 	ciss_release_request(struct ciss_request *cr);

/* request helpers */
static int	ciss_get_bmic_request(struct ciss_softc *sc, struct ciss_request **crp,
				      int opcode, void **bufp, size_t bufsize);
static int	ciss_user_command(struct ciss_softc *sc, IOCTL_Command_struct *ioc);

/* DMA map/unmap */
static int	ciss_map_request(struct ciss_request *cr);
static void	ciss_request_map_helper(void *arg, bus_dma_segment_t *segs,
					int nseg, int error);
static void	ciss_unmap_request(struct ciss_request *cr);

/* CAM interface */
static int	ciss_cam_init(struct ciss_softc *sc);
static void	ciss_cam_rescan_target(struct ciss_softc *sc,
				       int bus, int target);
static void	ciss_cam_action(struct cam_sim *sim, union ccb *ccb);
static int	ciss_cam_action_io(struct cam_sim *sim, struct ccb_scsiio *csio);
static int	ciss_cam_emulate(struct ciss_softc *sc, struct ccb_scsiio *csio);
static void	ciss_cam_poll(struct cam_sim *sim);
static void	ciss_cam_complete(struct ciss_request *cr);
static void	ciss_cam_complete_fixup(struct ciss_softc *sc, struct ccb_scsiio *csio);
static int	ciss_name_device(struct ciss_softc *sc, int bus, int target);

/* periodic status monitoring */
static void	ciss_periodic(void *arg);
static void	ciss_nop_complete(struct ciss_request *cr);
static void	ciss_disable_adapter(struct ciss_softc *sc);
static void	ciss_notify_event(struct ciss_softc *sc);
static void	ciss_notify_complete(struct ciss_request *cr);
static int	ciss_notify_abort(struct ciss_softc *sc);
static int	ciss_notify_abort_bmic(struct ciss_softc *sc);
static void	ciss_notify_hotplug(struct ciss_softc *sc, struct ciss_notify *cn);
static void	ciss_notify_logical(struct ciss_softc *sc, struct ciss_notify *cn);
static void	ciss_notify_physical(struct ciss_softc *sc, struct ciss_notify *cn);

/* debugging output */
static void	ciss_print_request(struct ciss_request *cr);
static void	ciss_print_ldrive(struct ciss_softc *sc, struct ciss_ldrive *ld);
static const char *ciss_name_ldrive_status(int status);
static int	ciss_decode_ldrive_status(int status);
static const char *ciss_name_ldrive_org(int org);
static const char *ciss_name_command_status(int status);

/*
 * PCI bus interface.
 */
static device_method_t ciss_methods[] = {
    /* Device interface */
    DEVMETHOD(device_probe,	ciss_probe),
    DEVMETHOD(device_attach,	ciss_attach),
    DEVMETHOD(device_detach,	ciss_detach),
    DEVMETHOD(device_shutdown,	ciss_shutdown),
    { 0, 0 }
};

static driver_t ciss_pci_driver = {
    "ciss",
    ciss_methods,
    sizeof(struct ciss_softc)
};

/*
 * Control device interface.
 */
static d_open_t		ciss_open;
static d_close_t	ciss_close;
static d_ioctl_t	ciss_ioctl;

static struct cdevsw ciss_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	0,
	.d_open =	ciss_open,
	.d_close =	ciss_close,
	.d_ioctl =	ciss_ioctl,
	.d_name =	"ciss",
};

/*
 * This tunable can be set at boot time and controls whether physical devices
 * that are marked hidden by the firmware should be exposed anyways.
 */
static unsigned int ciss_expose_hidden_physical = 0;
TUNABLE_INT("hw.ciss.expose_hidden_physical", &ciss_expose_hidden_physical);

static unsigned int ciss_nop_message_heartbeat = 0;
TUNABLE_INT("hw.ciss.nop_message_heartbeat", &ciss_nop_message_heartbeat);

/*
 * This tunable can force a particular transport to be used:
 * <= 0 : use default
 *    1 : force simple
 *    2 : force performant
 */
static int ciss_force_transport = 0;
TUNABLE_INT("hw.ciss.force_transport", &ciss_force_transport);

/*
 * This tunable can force a particular interrupt delivery method to be used:
 * <= 0 : use default
 *    1 : force INTx
 *    2 : force MSIX
 */
static int ciss_force_interrupt = 0;
TUNABLE_INT("hw.ciss.force_interrupt", &ciss_force_interrupt);


/************************************************************************
 * CISS adapters amazingly don't have a defined programming interface
 * value.  (One could say some very despairing things about PCI and
 * people just not getting the general idea.)  So we are forced to
 * stick with matching against subvendor/subdevice, and thus have to
 * be updated for every new CISS adapter that appears.
 */
#define CISS_BOARD_UNKNWON	0
#define CISS_BOARD_SA5		1
#define CISS_BOARD_SA5B		2
#define CISS_BOARD_NOMSI	(1<<4)
#define CISS_BOARD_SIMPLE       (1<<5)

static struct
{
    u_int16_t	subvendor;
    u_int16_t	subdevice;
    int		flags;
    char	*desc;
} ciss_vendor_data[] = {
    { 0x0e11, 0x4070, CISS_BOARD_SA5|CISS_BOARD_NOMSI|CISS_BOARD_SIMPLE,
                                                        "Compaq Smart Array 5300" },
    { 0x0e11, 0x4080, CISS_BOARD_SA5B|CISS_BOARD_NOMSI,	"Compaq Smart Array 5i" },
    { 0x0e11, 0x4082, CISS_BOARD_SA5B|CISS_BOARD_NOMSI,	"Compaq Smart Array 532" },
    { 0x0e11, 0x4083, CISS_BOARD_SA5B|CISS_BOARD_NOMSI,	"HP Smart Array 5312" },
    { 0x0e11, 0x4091, CISS_BOARD_SA5,	"HP Smart Array 6i" },
    { 0x0e11, 0x409A, CISS_BOARD_SA5,	"HP Smart Array 641" },
    { 0x0e11, 0x409B, CISS_BOARD_SA5,	"HP Smart Array 642" },
    { 0x0e11, 0x409C, CISS_BOARD_SA5,	"HP Smart Array 6400" },
    { 0x0e11, 0x409D, CISS_BOARD_SA5,	"HP Smart Array 6400 EM" },
    { 0x103C, 0x3211, CISS_BOARD_SA5,	"HP Smart Array E200i" },
    { 0x103C, 0x3212, CISS_BOARD_SA5,	"HP Smart Array E200" },
    { 0x103C, 0x3213, CISS_BOARD_SA5,	"HP Smart Array E200i" },
    { 0x103C, 0x3214, CISS_BOARD_SA5,	"HP Smart Array E200i" },
    { 0x103C, 0x3215, CISS_BOARD_SA5,	"HP Smart Array E200i" },
    { 0x103C, 0x3220, CISS_BOARD_SA5,	"HP Smart Array" },
    { 0x103C, 0x3222, CISS_BOARD_SA5,	"HP Smart Array" },
    { 0x103C, 0x3223, CISS_BOARD_SA5,	"HP Smart Array P800" },
    { 0x103C, 0x3225, CISS_BOARD_SA5,	"HP Smart Array P600" },
    { 0x103C, 0x3230, CISS_BOARD_SA5,	"HP Smart Array" },
    { 0x103C, 0x3231, CISS_BOARD_SA5,	"HP Smart Array" },
    { 0x103C, 0x3232, CISS_BOARD_SA5,	"HP Smart Array" },
    { 0x103C, 0x3233, CISS_BOARD_SA5,	"HP Smart Array" },
    { 0x103C, 0x3234, CISS_BOARD_SA5,	"HP Smart Array P400" },
    { 0x103C, 0x3235, CISS_BOARD_SA5,	"HP Smart Array P400i" },
    { 0x103C, 0x3236, CISS_BOARD_SA5,	"HP Smart Array" },
    { 0x103C, 0x3237, CISS_BOARD_SA5,	"HP Smart Array E500" },
    { 0x103C, 0x3238, CISS_BOARD_SA5,	"HP Smart Array" },
    { 0x103C, 0x3239, CISS_BOARD_SA5,	"HP Smart Array" },
    { 0x103C, 0x323A, CISS_BOARD_SA5,	"HP Smart Array" },
    { 0x103C, 0x323B, CISS_BOARD_SA5,	"HP Smart Array" },
    { 0x103C, 0x323C, CISS_BOARD_SA5,	"HP Smart Array" },
    { 0x103C, 0x323D, CISS_BOARD_SA5,	"HP Smart Array P700m" },
    { 0x103C, 0x3241, CISS_BOARD_SA5,	"HP Smart Array P212" },
    { 0x103C, 0x3243, CISS_BOARD_SA5,	"HP Smart Array P410" },
    { 0x103C, 0x3245, CISS_BOARD_SA5,	"HP Smart Array P410i" },
    { 0x103C, 0x3247, CISS_BOARD_SA5,	"HP Smart Array P411" },
    { 0x103C, 0x3249, CISS_BOARD_SA5,	"HP Smart Array P812" },
    { 0x103C, 0x324A, CISS_BOARD_SA5,	"HP Smart Array P712m" },
    { 0x103C, 0x324B, CISS_BOARD_SA5,	"HP Smart Array" },
    { 0x103C, 0x3350, CISS_BOARD_SA5,   "HP Smart Array P222" },
    { 0x103C, 0x3351, CISS_BOARD_SA5,   "HP Smart Array P420" },
    { 0x103C, 0x3352, CISS_BOARD_SA5,   "HP Smart Array P421" },
    { 0x103C, 0x3353, CISS_BOARD_SA5,   "HP Smart Array P822" },
    { 0x103C, 0x3354, CISS_BOARD_SA5,   "HP Smart Array P420i" },
    { 0x103C, 0x3355, CISS_BOARD_SA5,   "HP Smart Array P220i" },
    { 0x103C, 0x3356, CISS_BOARD_SA5,   "HP Smart Array P721m" },
    { 0x103C, 0x1920, CISS_BOARD_SA5,   "HP Smart Array P430i" },
    { 0x103C, 0x1921, CISS_BOARD_SA5,   "HP Smart Array P830i" },
    { 0x103C, 0x1922, CISS_BOARD_SA5,   "HP Smart Array P430" },
    { 0x103C, 0x1923, CISS_BOARD_SA5,   "HP Smart Array P431" },
    { 0x103C, 0x1924, CISS_BOARD_SA5,   "HP Smart Array P830" },
    { 0x103C, 0x1926, CISS_BOARD_SA5,   "HP Smart Array P731m" },
    { 0x103C, 0x1928, CISS_BOARD_SA5,   "HP Smart Array P230i" },
    { 0x103C, 0x1929, CISS_BOARD_SA5,   "HP Smart Array P530" },
    { 0x103C, 0x192A, CISS_BOARD_SA5,   "HP Smart Array P531" },
    { 0x103C, 0x21BD, CISS_BOARD_SA5,   "HP Smart Array P244br" },
    { 0x103C, 0x21BE, CISS_BOARD_SA5,   "HP Smart Array P741m" },
    { 0x103C, 0x21BF, CISS_BOARD_SA5,   "HP Smart Array H240ar" },
    { 0x103C, 0x21C0, CISS_BOARD_SA5,   "HP Smart Array P440ar" },
    { 0x103C, 0x21C1, CISS_BOARD_SA5,   "HP Smart Array P840ar" },
    { 0x103C, 0x21C2, CISS_BOARD_SA5,   "HP Smart Array P440" },
    { 0x103C, 0x21C3, CISS_BOARD_SA5,   "HP Smart Array P441" },
    { 0x103C, 0x21C5, CISS_BOARD_SA5,   "HP Smart Array P841" },
    { 0x103C, 0x21C6, CISS_BOARD_SA5,   "HP Smart Array H244br" },
    { 0x103C, 0x21C7, CISS_BOARD_SA5,   "HP Smart Array H240" },
    { 0x103C, 0x21C8, CISS_BOARD_SA5,   "HP Smart Array H241" },
    { 0x103C, 0x21CA, CISS_BOARD_SA5,   "HP Smart Array P246br" },
    { 0x103C, 0x21CB, CISS_BOARD_SA5,   "HP Smart Array P840" },
    { 0x103C, 0x21CC, CISS_BOARD_SA5,   "HP Smart Array P542d" },
    { 0x103C, 0x21CD, CISS_BOARD_SA5,   "HP Smart Array P240nr" },
    { 0x103C, 0x21CE, CISS_BOARD_SA5,   "HP Smart Array H240nr" },
    { 0, 0, 0, NULL }
};

static devclass_t	ciss_devclass;
DRIVER_MODULE(ciss, pci, ciss_pci_driver, ciss_devclass, 0, 0);
MODULE_PNP_INFO("U16:vendor;U16:device;", pci, ciss, ciss_vendor_data,
    nitems(ciss_vendor_data) - 1);
MODULE_DEPEND(ciss, cam, 1, 1, 1);
MODULE_DEPEND(ciss, pci, 1, 1, 1);

/************************************************************************
 * Find a match for the device in our list of known adapters.
 */
static int
ciss_lookup(device_t dev)
{
    int 	i;

    for (i = 0; ciss_vendor_data[i].desc != NULL; i++)
	if ((pci_get_subvendor(dev) == ciss_vendor_data[i].subvendor) &&
	    (pci_get_subdevice(dev) == ciss_vendor_data[i].subdevice)) {
	    return(i);
	}
    return(-1);
}

/************************************************************************
 * Match a known CISS adapter.
 */
static int
ciss_probe(device_t dev)
{
    int		i;

    i = ciss_lookup(dev);
    if (i != -1) {
	device_set_desc(dev, ciss_vendor_data[i].desc);
	return(BUS_PROBE_DEFAULT);
    }
    return(ENOENT);
}

/************************************************************************
 * Attach the driver to this adapter.
 */
static int
ciss_attach(device_t dev)
{
    struct ciss_softc	*sc;
    int			error;

    debug_called(1);

#ifdef CISS_DEBUG
    /* print structure/union sizes */
    debug_struct(ciss_command);
    debug_struct(ciss_header);
    debug_union(ciss_device_address);
    debug_struct(ciss_cdb);
    debug_struct(ciss_report_cdb);
    debug_struct(ciss_notify_cdb);
    debug_struct(ciss_notify);
    debug_struct(ciss_message_cdb);
    debug_struct(ciss_error_info_pointer);
    debug_struct(ciss_error_info);
    debug_struct(ciss_sg_entry);
    debug_struct(ciss_config_table);
    debug_struct(ciss_bmic_cdb);
    debug_struct(ciss_bmic_id_ldrive);
    debug_struct(ciss_bmic_id_lstatus);
    debug_struct(ciss_bmic_id_table);
    debug_struct(ciss_bmic_id_pdrive);
    debug_struct(ciss_bmic_blink_pdrive);
    debug_struct(ciss_bmic_flush_cache);
    debug_const(CISS_MAX_REQUESTS);
    debug_const(CISS_MAX_LOGICAL);
    debug_const(CISS_INTERRUPT_COALESCE_DELAY);
    debug_const(CISS_INTERRUPT_COALESCE_COUNT);
    debug_const(CISS_COMMAND_ALLOC_SIZE);
    debug_const(CISS_COMMAND_SG_LENGTH);

    debug_type(cciss_pci_info_struct);
    debug_type(cciss_coalint_struct);
    debug_type(cciss_coalint_struct);
    debug_type(NodeName_type);
    debug_type(NodeName_type);
    debug_type(Heartbeat_type);
    debug_type(BusTypes_type);
    debug_type(FirmwareVer_type);
    debug_type(DriverVer_type);
    debug_type(IOCTL_Command_struct);
#endif

    sc = device_get_softc(dev);
    sc->ciss_dev = dev;
    mtx_init(&sc->ciss_mtx, "cissmtx", NULL, MTX_DEF);
    callout_init_mtx(&sc->ciss_periodic, &sc->ciss_mtx, 0);

    /*
     * Do PCI-specific init.
     */
    if ((error = ciss_init_pci(sc)) != 0)
	goto out;

    /*
     * Initialise driver queues.
     */
    ciss_initq_free(sc);
    ciss_initq_notify(sc);

    /*
     * Initialize device sysctls.
     */
    ciss_init_sysctl(sc);

    /*
     * Initialise command/request pool.
     */
    if ((error = ciss_init_requests(sc)) != 0)
	goto out;

    /*
     * Get adapter information.
     */
    if ((error = ciss_identify_adapter(sc)) != 0)
	goto out;

    /*
     * Find all the physical devices.
     */
    if ((error = ciss_init_physical(sc)) != 0)
	goto out;

    /*
     * Build our private table of logical devices.
     */
    if ((error = ciss_init_logical(sc)) != 0)
	goto out;

    /*
     * Enable interrupts so that the CAM scan can complete.
     */
    CISS_TL_SIMPLE_ENABLE_INTERRUPTS(sc);

    /*
     * Initialise the CAM interface.
     */
    if ((error = ciss_cam_init(sc)) != 0)
	goto out;

    /*
     * Start the heartbeat routine and event chain.
     */
    ciss_periodic(sc);

   /*
     * Create the control device.
     */
    sc->ciss_dev_t = make_dev(&ciss_cdevsw, device_get_unit(sc->ciss_dev),
			      UID_ROOT, GID_OPERATOR, S_IRUSR | S_IWUSR,
			      "ciss%d", device_get_unit(sc->ciss_dev));
    sc->ciss_dev_t->si_drv1 = sc;

    /*
     * The adapter is running; synchronous commands can now sleep
     * waiting for an interrupt to signal completion.
     */
    sc->ciss_flags |= CISS_FLAG_RUNNING;

    ciss_spawn_notify_thread(sc);

    error = 0;
 out:
    if (error != 0) {
	/* ciss_free() expects the mutex to be held */
	mtx_lock(&sc->ciss_mtx);
	ciss_free(sc);
    }
    return(error);
}

/************************************************************************
 * Detach the driver from this adapter.
 */
static int
ciss_detach(device_t dev)
{
    struct ciss_softc	*sc = device_get_softc(dev);

    debug_called(1);

    mtx_lock(&sc->ciss_mtx);
    if (sc->ciss_flags & CISS_FLAG_CONTROL_OPEN) {
	mtx_unlock(&sc->ciss_mtx);
	return (EBUSY);
    }

    /* flush adapter cache */
    ciss_flush_adapter(sc);

    /* release all resources.  The mutex is released and freed here too. */
    ciss_free(sc);

    return(0);
}

/************************************************************************
 * Prepare adapter for system shutdown.
 */
static int
ciss_shutdown(device_t dev)
{
    struct ciss_softc	*sc = device_get_softc(dev);

    debug_called(1);

    mtx_lock(&sc->ciss_mtx);
    /* flush adapter cache */
    ciss_flush_adapter(sc);

    if (sc->ciss_soft_reset)
	ciss_soft_reset(sc);
    mtx_unlock(&sc->ciss_mtx);

    return(0);
}

static void
ciss_init_sysctl(struct ciss_softc *sc)
{

    SYSCTL_ADD_INT(device_get_sysctl_ctx(sc->ciss_dev),
	SYSCTL_CHILDREN(device_get_sysctl_tree(sc->ciss_dev)),
	OID_AUTO, "soft_reset", CTLFLAG_RW, &sc->ciss_soft_reset, 0, "");
}

/************************************************************************
 * Perform PCI-specific attachment actions.
 */
static int
ciss_init_pci(struct ciss_softc *sc)
{
    uintptr_t		cbase, csize, cofs;
    uint32_t		method, supported_methods;
    int			error, sqmask, i;
    void		*intr;

    debug_called(1);

    /*
     * Work out adapter type.
     */
    i = ciss_lookup(sc->ciss_dev);
    if (i < 0) {
	ciss_printf(sc, "unknown adapter type\n");
	return (ENXIO);
    }

    if (ciss_vendor_data[i].flags & CISS_BOARD_SA5) {
	sqmask = CISS_TL_SIMPLE_INTR_OPQ_SA5;
    } else if (ciss_vendor_data[i].flags & CISS_BOARD_SA5B) {
	sqmask = CISS_TL_SIMPLE_INTR_OPQ_SA5B;
    } else {
	/*
	 * XXX Big hammer, masks/unmasks all possible interrupts.  This should
	 * work on all hardware variants.  Need to add code to handle the
	 * "controller crashed" interrupt bit that this unmasks.
	 */
	sqmask = ~0;
    }

    /*
     * Allocate register window first (we need this to find the config
     * struct).
     */
    error = ENXIO;
    sc->ciss_regs_rid = CISS_TL_SIMPLE_BAR_REGS;
    if ((sc->ciss_regs_resource =
	 bus_alloc_resource_any(sc->ciss_dev, SYS_RES_MEMORY,
				&sc->ciss_regs_rid, RF_ACTIVE)) == NULL) {
	ciss_printf(sc, "can't allocate register window\n");
	return(ENXIO);
    }
    sc->ciss_regs_bhandle = rman_get_bushandle(sc->ciss_regs_resource);
    sc->ciss_regs_btag = rman_get_bustag(sc->ciss_regs_resource);

    /*
     * Find the BAR holding the config structure.  If it's not the one
     * we already mapped for registers, map it too.
     */
    sc->ciss_cfg_rid = CISS_TL_SIMPLE_READ(sc, CISS_TL_SIMPLE_CFG_BAR) & 0xffff;
    if (sc->ciss_cfg_rid != sc->ciss_regs_rid) {
	if ((sc->ciss_cfg_resource =
	     bus_alloc_resource_any(sc->ciss_dev, SYS_RES_MEMORY,
				    &sc->ciss_cfg_rid, RF_ACTIVE)) == NULL) {
	    ciss_printf(sc, "can't allocate config window\n");
	    return(ENXIO);
	}
	cbase = (uintptr_t)rman_get_virtual(sc->ciss_cfg_resource);
	csize = rman_get_end(sc->ciss_cfg_resource) -
	    rman_get_start(sc->ciss_cfg_resource) + 1;
    } else {
	cbase = (uintptr_t)rman_get_virtual(sc->ciss_regs_resource);
	csize = rman_get_end(sc->ciss_regs_resource) -
	    rman_get_start(sc->ciss_regs_resource) + 1;
    }
    cofs = CISS_TL_SIMPLE_READ(sc, CISS_TL_SIMPLE_CFG_OFF);

    /*
     * Use the base/size/offset values we just calculated to
     * sanity-check the config structure.  If it's OK, point to it.
     */
    if ((cofs + sizeof(struct ciss_config_table)) > csize) {
	ciss_printf(sc, "config table outside window\n");
	return(ENXIO);
    }
    sc->ciss_cfg = (struct ciss_config_table *)(cbase + cofs);
    debug(1, "config struct at %p", sc->ciss_cfg);

    /*
     * Calculate the number of request structures/commands we are
     * going to provide for this adapter.
     */
    sc->ciss_max_requests = min(CISS_MAX_REQUESTS, sc->ciss_cfg->max_outstanding_commands);

    /*
     * Validate the config structure.  If we supported other transport
     * methods, we could select amongst them at this point in time.
     */
    if (strncmp(sc->ciss_cfg->signature, "CISS", 4)) {
	ciss_printf(sc, "config signature mismatch (got '%c%c%c%c')\n",
		    sc->ciss_cfg->signature[0], sc->ciss_cfg->signature[1],
		    sc->ciss_cfg->signature[2], sc->ciss_cfg->signature[3]);
	return(ENXIO);
    }

    /*
     * Select the mode of operation, prefer Performant.
     */
    if (!(sc->ciss_cfg->supported_methods &
	(CISS_TRANSPORT_METHOD_SIMPLE | CISS_TRANSPORT_METHOD_PERF))) {
	ciss_printf(sc, "No supported transport layers: 0x%x\n",
	    sc->ciss_cfg->supported_methods);
    }

    switch (ciss_force_transport) {
    case 1:
	supported_methods = CISS_TRANSPORT_METHOD_SIMPLE;
	break;
    case 2:
	supported_methods = CISS_TRANSPORT_METHOD_PERF;
	break;
    default:
        /*
         * Override the capabilities of the BOARD and specify SIMPLE
         * MODE 
         */
        if (ciss_vendor_data[i].flags & CISS_BOARD_SIMPLE)
                supported_methods = CISS_TRANSPORT_METHOD_SIMPLE;
        else
                supported_methods = sc->ciss_cfg->supported_methods;
        break;
    }

setup:
    if ((supported_methods & CISS_TRANSPORT_METHOD_PERF) != 0) {
	method = CISS_TRANSPORT_METHOD_PERF;
	sc->ciss_perf = (struct ciss_perf_config *)(cbase + cofs +
	    sc->ciss_cfg->transport_offset);
	if (ciss_init_perf(sc)) {
	    supported_methods &= ~method;
	    goto setup;
	}
    } else if (supported_methods & CISS_TRANSPORT_METHOD_SIMPLE) {
	method = CISS_TRANSPORT_METHOD_SIMPLE;
    } else {
	ciss_printf(sc, "No supported transport methods: 0x%x\n",
	    sc->ciss_cfg->supported_methods);
	return(ENXIO);
    }

    /*
     * Tell it we're using the low 4GB of RAM.  Set the default interrupt
     * coalescing options.
     */
    sc->ciss_cfg->requested_method = method;
    sc->ciss_cfg->command_physlimit = 0;
    sc->ciss_cfg->interrupt_coalesce_delay = CISS_INTERRUPT_COALESCE_DELAY;
    sc->ciss_cfg->interrupt_coalesce_count = CISS_INTERRUPT_COALESCE_COUNT;

#ifdef __i386__
    sc->ciss_cfg->host_driver |= CISS_DRIVER_SCSI_PREFETCH;
#endif

    if (ciss_update_config(sc)) {
	ciss_printf(sc, "adapter refuses to accept config update (IDBR 0x%x)\n",
		    CISS_TL_SIMPLE_READ(sc, CISS_TL_SIMPLE_IDBR));
	return(ENXIO);
    }
    if ((sc->ciss_cfg->active_method & method) == 0) {
	supported_methods &= ~method;
	if (supported_methods == 0) {
	    ciss_printf(sc, "adapter refuses to go into available transports "
		"mode (0x%x, 0x%x)\n", supported_methods,
		sc->ciss_cfg->active_method);
	    return(ENXIO);
	} else 
	    goto setup;
    }

    /*
     * Wait for the adapter to come ready.
     */
    if ((error = ciss_wait_adapter(sc)) != 0)
	return(error);

    /* Prepare to possibly use MSIX and/or PERFORMANT interrupts.  Normal
     * interrupts have a rid of 0, this will be overridden if MSIX is used.
     */
    sc->ciss_irq_rid[0] = 0;
    if (method == CISS_TRANSPORT_METHOD_PERF) {
	ciss_printf(sc, "PERFORMANT Transport\n");
	if ((ciss_force_interrupt != 1) && (ciss_setup_msix(sc) == 0)) {
	    intr = ciss_perf_msi_intr;
	} else {
	    intr = ciss_perf_intr;
	}
	/* XXX The docs say that the 0x01 bit is only for SAS controllers.
	 * Unfortunately, there is no good way to know if this is a SAS
	 * controller.  Hopefully enabling this bit universally will work OK.
	 * It seems to work fine for SA6i controllers.
	 */
	sc->ciss_interrupt_mask = CISS_TL_PERF_INTR_OPQ | CISS_TL_PERF_INTR_MSI;

    } else {
	ciss_printf(sc, "SIMPLE Transport\n");
	/* MSIX doesn't seem to work in SIMPLE mode, only enable if it forced */
	if (ciss_force_interrupt == 2)
	    /* If this fails, we automatically revert to INTx */
	    ciss_setup_msix(sc);
	sc->ciss_perf = NULL;
	intr = ciss_intr;
	sc->ciss_interrupt_mask = sqmask;
    }

    /*
     * Turn off interrupts before we go routing anything.
     */
    CISS_TL_SIMPLE_DISABLE_INTERRUPTS(sc);

    /*
     * Allocate and set up our interrupt.
     */
    if ((sc->ciss_irq_resource =
	 bus_alloc_resource_any(sc->ciss_dev, SYS_RES_IRQ, &sc->ciss_irq_rid[0],
				RF_ACTIVE | RF_SHAREABLE)) == NULL) {
	ciss_printf(sc, "can't allocate interrupt\n");
	return(ENXIO);
    }

    if (bus_setup_intr(sc->ciss_dev, sc->ciss_irq_resource,
		       INTR_TYPE_CAM|INTR_MPSAFE, NULL, intr, sc,
		       &sc->ciss_intr)) {
	ciss_printf(sc, "can't set up interrupt\n");
	return(ENXIO);
    }

    /*
     * Allocate the parent bus DMA tag appropriate for our PCI
     * interface.
     *
     * Note that "simple" adapters can only address within a 32-bit
     * span.
     */
    if (bus_dma_tag_create(bus_get_dma_tag(sc->ciss_dev),/* PCI parent */
			   1, 0, 			/* alignment, boundary */
			   BUS_SPACE_MAXADDR,		/* lowaddr */
			   BUS_SPACE_MAXADDR, 		/* highaddr */
			   NULL, NULL, 			/* filter, filterarg */
			   BUS_SPACE_MAXSIZE_32BIT,	/* maxsize */
			   BUS_SPACE_UNRESTRICTED,	/* nsegments */
			   BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
			   0,				/* flags */
			   NULL, NULL,			/* lockfunc, lockarg */
			   &sc->ciss_parent_dmat)) {
	ciss_printf(sc, "can't allocate parent DMA tag\n");
	return(ENOMEM);
    }

    /*
     * Create DMA tag for mapping buffers into adapter-addressable
     * space.
     */
    if (bus_dma_tag_create(sc->ciss_parent_dmat, 	/* parent */
			   1, 0, 			/* alignment, boundary */
			   BUS_SPACE_MAXADDR,		/* lowaddr */
			   BUS_SPACE_MAXADDR, 		/* highaddr */
			   NULL, NULL, 			/* filter, filterarg */
			   (CISS_MAX_SG_ELEMENTS - 1) * PAGE_SIZE, /* maxsize */
			   CISS_MAX_SG_ELEMENTS,	/* nsegments */
			   BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
			   BUS_DMA_ALLOCNOW,		/* flags */
			   busdma_lock_mutex, &sc->ciss_mtx,	/* lockfunc, lockarg */
			   &sc->ciss_buffer_dmat)) {
	ciss_printf(sc, "can't allocate buffer DMA tag\n");
	return(ENOMEM);
    }
    return(0);
}

/************************************************************************
 * Setup MSI/MSIX operation (Performant only)
 * Four interrupts are available, but we only use 1 right now.  If MSI-X
 * isn't avaialble, try using MSI instead.
 */
static int
ciss_setup_msix(struct ciss_softc *sc)
{
    int val, i;

    /* Weed out devices that don't actually support MSI */
    i = ciss_lookup(sc->ciss_dev);
    if (ciss_vendor_data[i].flags & CISS_BOARD_NOMSI)
	return (EINVAL);

    /*
     * Only need to use the minimum number of MSI vectors, as the driver
     * doesn't support directed MSIX interrupts.
     */
    val = pci_msix_count(sc->ciss_dev);
    if (val < CISS_MSI_COUNT) {
	val = pci_msi_count(sc->ciss_dev);
	device_printf(sc->ciss_dev, "got %d MSI messages]\n", val);
	if (val < CISS_MSI_COUNT)
	    return (EINVAL);
    }
    val = MIN(val, CISS_MSI_COUNT);
    if (pci_alloc_msix(sc->ciss_dev, &val) != 0) {
	if (pci_alloc_msi(sc->ciss_dev, &val) != 0)
	    return (EINVAL);
    }

    sc->ciss_msi = val;
    if (bootverbose)
	ciss_printf(sc, "Using %d MSIX interrupt%s\n", val,
	    (val != 1) ? "s" : "");

    for (i = 0; i < val; i++)
	sc->ciss_irq_rid[i] = i + 1;

    return (0);

}

/************************************************************************
 * Setup the Performant structures.
 */
static int
ciss_init_perf(struct ciss_softc *sc)
{
    struct ciss_perf_config *pc = sc->ciss_perf;
    int reply_size;

    /*
     * Create the DMA tag for the reply queue.
     */
    reply_size = sizeof(uint64_t) * sc->ciss_max_requests;
    if (bus_dma_tag_create(sc->ciss_parent_dmat,	/* parent */
			   1, 0, 			/* alignment, boundary */
			   BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
			   BUS_SPACE_MAXADDR, 		/* highaddr */
			   NULL, NULL, 			/* filter, filterarg */
			   reply_size, 1,		/* maxsize, nsegments */
			   BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
			   0,				/* flags */
			   NULL, NULL,			/* lockfunc, lockarg */
			   &sc->ciss_reply_dmat)) {
	ciss_printf(sc, "can't allocate reply DMA tag\n");
	return(ENOMEM);
    }
    /*
     * Allocate memory and make it available for DMA.
     */
    if (bus_dmamem_alloc(sc->ciss_reply_dmat, (void **)&sc->ciss_reply,
			 BUS_DMA_NOWAIT, &sc->ciss_reply_map)) {
	ciss_printf(sc, "can't allocate reply memory\n");
	return(ENOMEM);
    }
    bus_dmamap_load(sc->ciss_reply_dmat, sc->ciss_reply_map, sc->ciss_reply,
		    reply_size, ciss_command_map_helper, &sc->ciss_reply_phys, 0);
    bzero(sc->ciss_reply, reply_size);

    sc->ciss_cycle = 0x1;
    sc->ciss_rqidx = 0;

    /*
     * Preload the fetch table with common command sizes.  This allows the
     * hardware to not waste bus cycles for typical i/o commands, but also not
     * tax the driver to be too exact in choosing sizes.  The table is optimized
     * for page-aligned i/o's, but since most i/o comes from the various pagers,
     * it's a reasonable assumption to make.
     */
    pc->fetch_count[CISS_SG_FETCH_NONE] = (sizeof(struct ciss_command) + 15) / 16;
    pc->fetch_count[CISS_SG_FETCH_1] =
	(sizeof(struct ciss_command) + sizeof(struct ciss_sg_entry) * 1 + 15) / 16;
    pc->fetch_count[CISS_SG_FETCH_2] =
	(sizeof(struct ciss_command) + sizeof(struct ciss_sg_entry) * 2 + 15) / 16;
    pc->fetch_count[CISS_SG_FETCH_4] =
	(sizeof(struct ciss_command) + sizeof(struct ciss_sg_entry) * 4 + 15) / 16;
    pc->fetch_count[CISS_SG_FETCH_8] =
	(sizeof(struct ciss_command) + sizeof(struct ciss_sg_entry) * 8 + 15) / 16;
    pc->fetch_count[CISS_SG_FETCH_16] =
	(sizeof(struct ciss_command) + sizeof(struct ciss_sg_entry) * 16 + 15) / 16;
    pc->fetch_count[CISS_SG_FETCH_32] =
	(sizeof(struct ciss_command) + sizeof(struct ciss_sg_entry) * 32 + 15) / 16;
    pc->fetch_count[CISS_SG_FETCH_MAX] = (CISS_COMMAND_ALLOC_SIZE + 15) / 16;

    pc->rq_size = sc->ciss_max_requests; /* XXX less than the card supports? */
    pc->rq_count = 1;	/* XXX Hardcode for a single queue */
    pc->rq_bank_hi = 0;
    pc->rq_bank_lo = 0;
    pc->rq[0].rq_addr_hi = 0x0;
    pc->rq[0].rq_addr_lo = sc->ciss_reply_phys;

    return(0);
}

/************************************************************************
 * Wait for the adapter to come ready.
 */
static int
ciss_wait_adapter(struct ciss_softc *sc)
{
    int		i;

    debug_called(1);

    /*
     * Wait for the adapter to come ready.
     */
    if (!(sc->ciss_cfg->active_method & CISS_TRANSPORT_METHOD_READY)) {
	ciss_printf(sc, "waiting for adapter to come ready...\n");
	for (i = 0; !(sc->ciss_cfg->active_method & CISS_TRANSPORT_METHOD_READY); i++) {
	    DELAY(1000000);	/* one second */
	    if (i > 30) {
		ciss_printf(sc, "timed out waiting for adapter to come ready\n");
		return(EIO);
	    }
	}
    }
    return(0);
}

/************************************************************************
 * Flush the adapter cache.
 */
static int
ciss_flush_adapter(struct ciss_softc *sc)
{
    struct ciss_request			*cr;
    struct ciss_bmic_flush_cache	*cbfc;
    int					error, command_status;

    debug_called(1);

    cr = NULL;
    cbfc = NULL;

    /*
     * Build a BMIC request to flush the cache.  We don't disable
     * it, as we may be going to do more I/O (eg. we are emulating
     * the Synchronise Cache command).
     */
    if ((cbfc = malloc(sizeof(*cbfc), CISS_MALLOC_CLASS, M_NOWAIT | M_ZERO)) == NULL) {
	error = ENOMEM;
	goto out;
    }
    if ((error = ciss_get_bmic_request(sc, &cr, CISS_BMIC_FLUSH_CACHE,
				       (void **)&cbfc, sizeof(*cbfc))) != 0)
	goto out;

    /*
     * Submit the request and wait for it to complete.
     */
    if ((error = ciss_synch_request(cr, 60 * 1000)) != 0) {
	ciss_printf(sc, "error sending BMIC FLUSH_CACHE command (%d)\n", error);
	goto out;
    }

    /*
     * Check response.
     */
    ciss_report_request(cr, &command_status, NULL);
    switch(command_status) {
    case CISS_CMD_STATUS_SUCCESS:
	break;
    default:
	ciss_printf(sc, "error flushing cache (%s)\n",
		    ciss_name_command_status(command_status));
	error = EIO;
	goto out;
    }

out:
    if (cbfc != NULL)
	free(cbfc, CISS_MALLOC_CLASS);
    if (cr != NULL)
	ciss_release_request(cr);
    return(error);
}

static void
ciss_soft_reset(struct ciss_softc *sc)
{
    struct ciss_request		*cr = NULL;
    struct ciss_command		*cc;
    int				i, error = 0;

    for (i = 0; i < sc->ciss_max_logical_bus; i++) {
	/* only reset proxy controllers */
	if (sc->ciss_controllers[i].physical.bus == 0)
	    continue;

	if ((error = ciss_get_request(sc, &cr)) != 0)
	    break;

	if ((error = ciss_get_bmic_request(sc, &cr, CISS_BMIC_SOFT_RESET,
					   NULL, 0)) != 0)
	    break;

	cc = cr->cr_cc;
	cc->header.address = sc->ciss_controllers[i];

	if ((error = ciss_synch_request(cr, 60 * 1000)) != 0)
	    break;

	ciss_release_request(cr);
    }

    if (error)
	ciss_printf(sc, "error resetting controller (%d)\n", error);

    if (cr != NULL)
	ciss_release_request(cr);
}

/************************************************************************
 * Allocate memory for the adapter command structures, initialise
 * the request structures.
 *
 * Note that the entire set of commands are allocated in a single
 * contiguous slab.
 */
static int
ciss_init_requests(struct ciss_softc *sc)
{
    struct ciss_request	*cr;
    int			i;

    debug_called(1);

    if (bootverbose)
	ciss_printf(sc, "using %d of %d available commands\n",
		    sc->ciss_max_requests, sc->ciss_cfg->max_outstanding_commands);

    /*
     * Create the DMA tag for commands.
     */
    if (bus_dma_tag_create(sc->ciss_parent_dmat,	/* parent */
			   32, 0, 			/* alignment, boundary */
			   BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
			   BUS_SPACE_MAXADDR, 		/* highaddr */
			   NULL, NULL, 			/* filter, filterarg */
			   CISS_COMMAND_ALLOC_SIZE *
			   sc->ciss_max_requests, 1,	/* maxsize, nsegments */
			   BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
			   0,				/* flags */
			   NULL, NULL,			/* lockfunc, lockarg */
			   &sc->ciss_command_dmat)) {
	ciss_printf(sc, "can't allocate command DMA tag\n");
	return(ENOMEM);
    }
    /*
     * Allocate memory and make it available for DMA.
     */
    if (bus_dmamem_alloc(sc->ciss_command_dmat, (void **)&sc->ciss_command,
			 BUS_DMA_NOWAIT, &sc->ciss_command_map)) {
	ciss_printf(sc, "can't allocate command memory\n");
	return(ENOMEM);
    }
    bus_dmamap_load(sc->ciss_command_dmat, sc->ciss_command_map,sc->ciss_command,
		    CISS_COMMAND_ALLOC_SIZE * sc->ciss_max_requests,
		    ciss_command_map_helper, &sc->ciss_command_phys, 0);
    bzero(sc->ciss_command, CISS_COMMAND_ALLOC_SIZE * sc->ciss_max_requests);

    /*
     * Set up the request and command structures, push requests onto
     * the free queue.
     */
    for (i = 1; i < sc->ciss_max_requests; i++) {
	cr = &sc->ciss_request[i];
	cr->cr_sc = sc;
	cr->cr_tag = i;
	cr->cr_cc = (struct ciss_command *)((uintptr_t)sc->ciss_command +
	    CISS_COMMAND_ALLOC_SIZE * i);
	cr->cr_ccphys = sc->ciss_command_phys + CISS_COMMAND_ALLOC_SIZE * i;
	bus_dmamap_create(sc->ciss_buffer_dmat, 0, &cr->cr_datamap);
	ciss_enqueue_free(cr);
    }
    return(0);
}

static void
ciss_command_map_helper(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
    uint32_t *addr;

    addr = arg;
    *addr = segs[0].ds_addr;
}

/************************************************************************
 * Identify the adapter, print some information about it.
 */
static int
ciss_identify_adapter(struct ciss_softc *sc)
{
    struct ciss_request	*cr;
    int			error, command_status;

    debug_called(1);

    cr = NULL;

    /*
     * Get a request, allocate storage for the adapter data.
     */
    if ((error = ciss_get_bmic_request(sc, &cr, CISS_BMIC_ID_CTLR,
				       (void **)&sc->ciss_id,
				       sizeof(*sc->ciss_id))) != 0)
	goto out;

    /*
     * Submit the request and wait for it to complete.
     */
    if ((error = ciss_synch_request(cr, 60 * 1000)) != 0) {
	ciss_printf(sc, "error sending BMIC ID_CTLR command (%d)\n", error);
	goto out;
    }

    /*
     * Check response.
     */
    ciss_report_request(cr, &command_status, NULL);
    switch(command_status) {
    case CISS_CMD_STATUS_SUCCESS:		/* buffer right size */
	break;
    case CISS_CMD_STATUS_DATA_UNDERRUN:
    case CISS_CMD_STATUS_DATA_OVERRUN:
	ciss_printf(sc, "data over/underrun reading adapter information\n");
    default:
	ciss_printf(sc, "error reading adapter information (%s)\n",
		    ciss_name_command_status(command_status));
	error = EIO;
	goto out;
    }

    /* sanity-check reply */
    if (!(sc->ciss_id->controller_flags & CONTROLLER_FLAGS_BIG_MAP_SUPPORT)) {
	ciss_printf(sc, "adapter does not support BIG_MAP\n");
	error = ENXIO;
	goto out;
    }

#if 0
    /* XXX later revisions may not need this */
    sc->ciss_flags |= CISS_FLAG_FAKE_SYNCH;
#endif

    /* XXX only really required for old 5300 adapters? */
    sc->ciss_flags |= CISS_FLAG_BMIC_ABORT;

    /*
     * Earlier controller specs do not contain these config
     * entries, so assume that a 0 means its old and assign
     * these values to the defaults that were established 
     * when this driver was developed for them
     */
    if (sc->ciss_cfg->max_logical_supported == 0) 
        sc->ciss_cfg->max_logical_supported = CISS_MAX_LOGICAL;
    if (sc->ciss_cfg->max_physical_supported == 0) 
	sc->ciss_cfg->max_physical_supported = CISS_MAX_PHYSICAL;
    /* print information */
    if (bootverbose) {
	ciss_printf(sc, "  %d logical drive%s configured\n",
		    sc->ciss_id->configured_logical_drives,
		    (sc->ciss_id->configured_logical_drives == 1) ? "" : "s");
	ciss_printf(sc, "  firmware %4.4s\n", sc->ciss_id->running_firmware_revision);
	ciss_printf(sc, "  %d SCSI channels\n", sc->ciss_id->scsi_chip_count);

	ciss_printf(sc, "  signature '%.4s'\n", sc->ciss_cfg->signature);
	ciss_printf(sc, "  valence %d\n", sc->ciss_cfg->valence);
	ciss_printf(sc, "  supported I/O methods 0x%b\n",
		    sc->ciss_cfg->supported_methods,
		    "\20\1READY\2simple\3performant\4MEMQ\n");
	ciss_printf(sc, "  active I/O method 0x%b\n",
		    sc->ciss_cfg->active_method, "\20\2simple\3performant\4MEMQ\n");
	ciss_printf(sc, "  4G page base 0x%08x\n",
		    sc->ciss_cfg->command_physlimit);
	ciss_printf(sc, "  interrupt coalesce delay %dus\n",
		    sc->ciss_cfg->interrupt_coalesce_delay);
	ciss_printf(sc, "  interrupt coalesce count %d\n",
		    sc->ciss_cfg->interrupt_coalesce_count);
	ciss_printf(sc, "  max outstanding commands %d\n",
		    sc->ciss_cfg->max_outstanding_commands);
	ciss_printf(sc, "  bus types 0x%b\n", sc->ciss_cfg->bus_types,
		    "\20\1ultra2\2ultra3\10fibre1\11fibre2\n");
	ciss_printf(sc, "  server name '%.16s'\n", sc->ciss_cfg->server_name);
	ciss_printf(sc, "  heartbeat 0x%x\n", sc->ciss_cfg->heartbeat);
    	ciss_printf(sc, "  max logical logical volumes: %d\n", sc->ciss_cfg->max_logical_supported);
    	ciss_printf(sc, "  max physical disks supported: %d\n", sc->ciss_cfg->max_physical_supported);
    	ciss_printf(sc, "  max physical disks per logical volume: %d\n", sc->ciss_cfg->max_physical_per_logical);
	ciss_printf(sc, "  JBOD Support is %s\n", (sc->ciss_id->uiYetMoreControllerFlags & YMORE_CONTROLLER_FLAGS_JBOD_SUPPORTED) ?
			"Available" : "Unavailable");
	ciss_printf(sc, "  JBOD Mode is %s\n", (sc->ciss_id->PowerUPNvramFlags & PWR_UP_FLAG_JBOD_ENABLED) ?
			"Enabled" : "Disabled");
    }

out:
    if (error) {
	if (sc->ciss_id != NULL) {
	    free(sc->ciss_id, CISS_MALLOC_CLASS);
	    sc->ciss_id = NULL;
	}
    }
    if (cr != NULL)
	ciss_release_request(cr);
    return(error);
}

/************************************************************************
 * Helper routine for generating a list of logical and physical luns.
 */
static struct ciss_lun_report *
ciss_report_luns(struct ciss_softc *sc, int opcode, int nunits)
{
    struct ciss_request		*cr;
    struct ciss_command		*cc;
    struct ciss_report_cdb	*crc;
    struct ciss_lun_report	*cll;
    int				command_status;
    int				report_size;
    int				error = 0;

    debug_called(1);

    cr = NULL;
    cll = NULL;

    /*
     * Get a request, allocate storage for the address list.
     */
    if ((error = ciss_get_request(sc, &cr)) != 0)
	goto out;
    report_size = sizeof(*cll) + nunits * sizeof(union ciss_device_address);
    if ((cll = malloc(report_size, CISS_MALLOC_CLASS, M_NOWAIT | M_ZERO)) == NULL) {
	ciss_printf(sc, "can't allocate memory for lun report\n");
	error = ENOMEM;
	goto out;
    }

    /*
     * Build the Report Logical/Physical LUNs command.
     */
    cc = cr->cr_cc;
    cr->cr_data = cll;
    cr->cr_length = report_size;
    cr->cr_flags = CISS_REQ_DATAIN;

    cc->header.address.physical.mode = CISS_HDR_ADDRESS_MODE_PERIPHERAL;
    cc->header.address.physical.bus = 0;
    cc->header.address.physical.target = 0;
    cc->cdb.cdb_length = sizeof(*crc);
    cc->cdb.type = CISS_CDB_TYPE_COMMAND;
    cc->cdb.attribute = CISS_CDB_ATTRIBUTE_SIMPLE;
    cc->cdb.direction = CISS_CDB_DIRECTION_READ;
    cc->cdb.timeout = 30;	/* XXX better suggestions? */

    crc = (struct ciss_report_cdb *)&(cc->cdb.cdb[0]);
    bzero(crc, sizeof(*crc));
    crc->opcode = opcode;
    crc->length = htonl(report_size);			/* big-endian field */
    cll->list_size = htonl(report_size - sizeof(*cll));	/* big-endian field */

    /*
     * Submit the request and wait for it to complete.  (timeout
     * here should be much greater than above)
     */
    if ((error = ciss_synch_request(cr, 60 * 1000)) != 0) {
	ciss_printf(sc, "error sending %d LUN command (%d)\n", opcode, error);
	goto out;
    }

    /*
     * Check response.  Note that data over/underrun is OK.
     */
    ciss_report_request(cr, &command_status, NULL);
    switch(command_status) {
    case CISS_CMD_STATUS_SUCCESS:	/* buffer right size */
    case CISS_CMD_STATUS_DATA_UNDERRUN:	/* buffer too large, not bad */
	break;
    case CISS_CMD_STATUS_DATA_OVERRUN:
	ciss_printf(sc, "WARNING: more units than driver limit (%d)\n",
		    sc->ciss_cfg->max_logical_supported);
	break;
    default:
	ciss_printf(sc, "error detecting logical drive configuration (%s)\n",
		    ciss_name_command_status(command_status));
	error = EIO;
	goto out;
    }
    ciss_release_request(cr);
    cr = NULL;

out:
    if (cr != NULL)
	ciss_release_request(cr);
    if (error && cll != NULL) {
	free(cll, CISS_MALLOC_CLASS);
	cll = NULL;
    }
    return(cll);
}

/************************************************************************
 * Find logical drives on the adapter.
 */
static int
ciss_init_logical(struct ciss_softc *sc)
{
    struct ciss_lun_report	*cll;
    int				error = 0, i, j;
    int				ndrives;

    debug_called(1);

    cll = ciss_report_luns(sc, CISS_OPCODE_REPORT_LOGICAL_LUNS,
			   sc->ciss_cfg->max_logical_supported);
    if (cll == NULL) {
	error = ENXIO;
	goto out;
    }

    /* sanity-check reply */
    ndrives = (ntohl(cll->list_size) / sizeof(union ciss_device_address));
    if ((ndrives < 0) || (ndrives > sc->ciss_cfg->max_logical_supported)) {
	ciss_printf(sc, "adapter claims to report absurd number of logical drives (%d > %d)\n",
	    	ndrives, sc->ciss_cfg->max_logical_supported);
	error = ENXIO;
	goto out;
    }

    /*
     * Save logical drive information.
     */
    if (bootverbose) {
	ciss_printf(sc, "%d logical drive%s\n",
	    ndrives, (ndrives > 1 || ndrives == 0) ? "s" : "");
    }

    sc->ciss_logical =
	malloc(sc->ciss_max_logical_bus * sizeof(struct ciss_ldrive *),
	       CISS_MALLOC_CLASS, M_NOWAIT | M_ZERO);
    if (sc->ciss_logical == NULL) {
	error = ENXIO;
	goto out;
    }

    for (i = 0; i < sc->ciss_max_logical_bus; i++) {
	sc->ciss_logical[i] =
	    malloc(sc->ciss_cfg->max_logical_supported *
		   sizeof(struct ciss_ldrive),
		   CISS_MALLOC_CLASS, M_NOWAIT | M_ZERO);
	if (sc->ciss_logical[i] == NULL) {
	    error = ENXIO;
	    goto out;
	}

	for (j = 0; j < sc->ciss_cfg->max_logical_supported; j++)
	    sc->ciss_logical[i][j].cl_status = CISS_LD_NONEXISTENT;
    }


    for (i = 0; i < sc->ciss_cfg->max_logical_supported; i++) {
	if (i < ndrives) {
	    struct ciss_ldrive	*ld;
	    int			bus, target;

	    bus		= CISS_LUN_TO_BUS(cll->lun[i].logical.lun);
	    target	= CISS_LUN_TO_TARGET(cll->lun[i].logical.lun);
	    ld		= &sc->ciss_logical[bus][target];

	    ld->cl_address	= cll->lun[i];
	    ld->cl_controller	= &sc->ciss_controllers[bus];
	    if (ciss_identify_logical(sc, ld) != 0)
		continue;
	    /*
	     * If the drive has had media exchanged, we should bring it online.
	     */
	    if (ld->cl_lstatus->media_exchanged)
		ciss_accept_media(sc, ld);

	}
    }

 out:
    if (cll != NULL)
	free(cll, CISS_MALLOC_CLASS);
    return(error);
}

static int
ciss_init_physical(struct ciss_softc *sc)
{
    struct ciss_lun_report	*cll;
    int				error = 0, i;
    int				nphys;
    int				bus, target;

    debug_called(1);

    bus = 0;
    target = 0;

    cll = ciss_report_luns(sc, CISS_OPCODE_REPORT_PHYSICAL_LUNS,
			   sc->ciss_cfg->max_physical_supported);
    if (cll == NULL) {
	error = ENXIO;
	goto out;
    }

    nphys = (ntohl(cll->list_size) / sizeof(union ciss_device_address));

    if (bootverbose) {
	ciss_printf(sc, "%d physical device%s\n",
	    nphys, (nphys > 1 || nphys == 0) ? "s" : "");
    }

    /*
     * Figure out the bus mapping.
     * Logical buses include both the local logical bus for local arrays and
     * proxy buses for remote arrays.  Physical buses are numbered by the
     * controller and represent physical buses that hold physical devices.
     * We shift these bus numbers so that everything fits into a single flat
     * numbering space for CAM.  Logical buses occupy the first 32 CAM bus
     * numbers, and the physical bus numbers are shifted to be above that.
     * This results in the various driver arrays being indexed as follows:
     *
     * ciss_controllers[] - indexed by logical bus
     * ciss_cam_sim[]     - indexed by both logical and physical, with physical
     *                      being shifted by 32.
     * ciss_logical[][]   - indexed by logical bus
     * ciss_physical[][]  - indexed by physical bus
     *
     * XXX This is getting more and more hackish.  CISS really doesn't play
     *     well with a standard SCSI model; devices are addressed via magic
     *     cookies, not via b/t/l addresses.  Since there is no way to store
     *     the cookie in the CAM device object, we have to keep these lookup
     *     tables handy so that the devices can be found quickly at the cost
     *     of wasting memory and having a convoluted lookup scheme.  This
     *     driver should probably be converted to block interface.
     */
    /*
     * If the L2 and L3 SCSI addresses are 0, this signifies a proxy
     * controller. A proxy controller is another physical controller
     * behind the primary PCI controller. We need to know about this
     * so that BMIC commands can be properly targeted.  There can be
     * proxy controllers attached to a single PCI controller, so
     * find the highest numbered one so the array can be properly
     * sized.
     */
    sc->ciss_max_logical_bus = 1;
    for (i = 0; i < nphys; i++) {
	if (cll->lun[i].physical.extra_address == 0) {
	    bus = cll->lun[i].physical.bus;
	    sc->ciss_max_logical_bus = max(sc->ciss_max_logical_bus, bus) + 1;
	} else {
	    bus = CISS_EXTRA_BUS2(cll->lun[i].physical.extra_address);
	    sc->ciss_max_physical_bus = max(sc->ciss_max_physical_bus, bus);
	}
    }

    sc->ciss_controllers =
	malloc(sc->ciss_max_logical_bus * sizeof (union ciss_device_address),
	       CISS_MALLOC_CLASS, M_NOWAIT | M_ZERO);

    if (sc->ciss_controllers == NULL) {
	ciss_printf(sc, "Could not allocate memory for controller map\n");
	error = ENOMEM;
	goto out;
    }

    /* setup a map of controller addresses */
    for (i = 0; i < nphys; i++) {
	if (cll->lun[i].physical.extra_address == 0) {
	    sc->ciss_controllers[cll->lun[i].physical.bus] = cll->lun[i];
	}
    }

    sc->ciss_physical =
	malloc(sc->ciss_max_physical_bus * sizeof(struct ciss_pdrive *),
	       CISS_MALLOC_CLASS, M_NOWAIT | M_ZERO);
    if (sc->ciss_physical == NULL) {
	ciss_printf(sc, "Could not allocate memory for physical device map\n");
	error = ENOMEM;
	goto out;
    }

    for (i = 0; i < sc->ciss_max_physical_bus; i++) {
	sc->ciss_physical[i] =
	    malloc(sizeof(struct ciss_pdrive) * CISS_MAX_PHYSTGT,
		   CISS_MALLOC_CLASS, M_NOWAIT | M_ZERO);
	if (sc->ciss_physical[i] == NULL) {
	    ciss_printf(sc, "Could not allocate memory for target map\n");
	    error = ENOMEM;
	    goto out;
	}
    }

    ciss_filter_physical(sc, cll);

out:
    if (cll != NULL)
	free(cll, CISS_MALLOC_CLASS);

    return(error);
}

static int
ciss_filter_physical(struct ciss_softc *sc, struct ciss_lun_report *cll)
{
    u_int32_t ea;
    int i, nphys;
    int	bus, target;

    nphys = (ntohl(cll->list_size) / sizeof(union ciss_device_address));
    for (i = 0; i < nphys; i++) {
	if (cll->lun[i].physical.extra_address == 0)
	    continue;

	/*
	 * Filter out devices that we don't want.  Level 3 LUNs could
	 * probably be supported, but the docs don't give enough of a
	 * hint to know how.
	 *
	 * The mode field of the physical address is likely set to have
	 * hard disks masked out.  Honor it unless the user has overridden
	 * us with the tunable.  We also munge the inquiry data for these
	 * disks so that they only show up as passthrough devices.  Keeping
	 * them visible in this fashion is useful for doing things like
	 * flashing firmware.
	 */
	ea = cll->lun[i].physical.extra_address;
	if ((CISS_EXTRA_BUS3(ea) != 0) || (CISS_EXTRA_TARGET3(ea) != 0) ||
	    (CISS_EXTRA_MODE2(ea) == 0x3))
	    continue;
	if ((ciss_expose_hidden_physical == 0) &&
	   (cll->lun[i].physical.mode == CISS_HDR_ADDRESS_MODE_MASK_PERIPHERAL))
	    continue;

	/*
	 * Note: CISS firmware numbers physical busses starting at '1', not
	 *       '0'.  This numbering is internal to the firmware and is only
	 *       used as a hint here.
	 */
	bus = CISS_EXTRA_BUS2(ea) - 1;
	target = CISS_EXTRA_TARGET2(ea);
	sc->ciss_physical[bus][target].cp_address = cll->lun[i];
	sc->ciss_physical[bus][target].cp_online = 1;
    }

    return (0);
}

static int
ciss_inquiry_logical(struct ciss_softc *sc, struct ciss_ldrive *ld)
{
    struct ciss_request			*cr;
    struct ciss_command			*cc;
    struct scsi_inquiry			*inq;
    int					error;
    int					command_status;

    cr = NULL;

    bzero(&ld->cl_geometry, sizeof(ld->cl_geometry));

    if ((error = ciss_get_request(sc, &cr)) != 0)
	goto out;

    cc = cr->cr_cc;
    cr->cr_data = &ld->cl_geometry;
    cr->cr_length = sizeof(ld->cl_geometry);
    cr->cr_flags = CISS_REQ_DATAIN;

    cc->header.address = ld->cl_address;
    cc->cdb.cdb_length = 6;
    cc->cdb.type = CISS_CDB_TYPE_COMMAND;
    cc->cdb.attribute = CISS_CDB_ATTRIBUTE_SIMPLE;
    cc->cdb.direction = CISS_CDB_DIRECTION_READ;
    cc->cdb.timeout = 30;

    inq = (struct scsi_inquiry *)&(cc->cdb.cdb[0]);
    inq->opcode = INQUIRY;
    inq->byte2 = SI_EVPD;
    inq->page_code = CISS_VPD_LOGICAL_DRIVE_GEOMETRY;
    scsi_ulto2b(sizeof(ld->cl_geometry), inq->length);

    if ((error = ciss_synch_request(cr, 60 * 1000)) != 0) {
	ciss_printf(sc, "error getting geometry (%d)\n", error);
	goto out;
    }

    ciss_report_request(cr, &command_status, NULL);
    switch(command_status) {
    case CISS_CMD_STATUS_SUCCESS:
    case CISS_CMD_STATUS_DATA_UNDERRUN:
	break;
    case CISS_CMD_STATUS_DATA_OVERRUN:
	ciss_printf(sc, "WARNING: Data overrun\n");
	break;
    default:
	ciss_printf(sc, "Error detecting logical drive geometry (%s)\n",
		    ciss_name_command_status(command_status));
	break;
    }

out:
    if (cr != NULL)
	ciss_release_request(cr);
    return(error);
}
/************************************************************************
 * Identify a logical drive, initialise state related to it.
 */
static int
ciss_identify_logical(struct ciss_softc *sc, struct ciss_ldrive *ld)
{
    struct ciss_request		*cr;
    struct ciss_command		*cc;
    struct ciss_bmic_cdb	*cbc;
    int				error, command_status;

    debug_called(1);

    cr = NULL;

    /*
     * Build a BMIC request to fetch the drive ID.
     */
    if ((error = ciss_get_bmic_request(sc, &cr, CISS_BMIC_ID_LDRIVE,
				       (void **)&ld->cl_ldrive,
				       sizeof(*ld->cl_ldrive))) != 0)
	goto out;
    cc = cr->cr_cc;
    cc->header.address = *ld->cl_controller;	/* target controller */
    cbc = (struct ciss_bmic_cdb *)&(cc->cdb.cdb[0]);
    cbc->log_drive = CISS_LUN_TO_TARGET(ld->cl_address.logical.lun);

    /*
     * Submit the request and wait for it to complete.
     */
    if ((error = ciss_synch_request(cr, 60 * 1000)) != 0) {
	ciss_printf(sc, "error sending BMIC LDRIVE command (%d)\n", error);
	goto out;
    }

    /*
     * Check response.
     */
    ciss_report_request(cr, &command_status, NULL);
    switch(command_status) {
    case CISS_CMD_STATUS_SUCCESS:		/* buffer right size */
	break;
    case CISS_CMD_STATUS_DATA_UNDERRUN:
    case CISS_CMD_STATUS_DATA_OVERRUN:
	ciss_printf(sc, "data over/underrun reading logical drive ID\n");
    default:
	ciss_printf(sc, "error reading logical drive ID (%s)\n",
		    ciss_name_command_status(command_status));
	error = EIO;
	goto out;
    }
    ciss_release_request(cr);
    cr = NULL;

    /*
     * Build a CISS BMIC command to get the logical drive status.
     */
    if ((error = ciss_get_ldrive_status(sc, ld)) != 0)
	goto out;

    /*
     * Get the logical drive geometry.
     */
    if ((error = ciss_inquiry_logical(sc, ld)) != 0)
	goto out;

    /*
     * Print the drive's basic characteristics.
     */
    if (bootverbose) {
	ciss_printf(sc, "logical drive (b%dt%d): %s, %dMB ",
		    CISS_LUN_TO_BUS(ld->cl_address.logical.lun),
		    CISS_LUN_TO_TARGET(ld->cl_address.logical.lun),
		    ciss_name_ldrive_org(ld->cl_ldrive->fault_tolerance),
		    ((ld->cl_ldrive->blocks_available / (1024 * 1024)) *
		     ld->cl_ldrive->block_size));

	ciss_print_ldrive(sc, ld);
    }
out:
    if (error != 0) {
	/* make the drive not-exist */
	ld->cl_status = CISS_LD_NONEXISTENT;
	if (ld->cl_ldrive != NULL) {
	    free(ld->cl_ldrive, CISS_MALLOC_CLASS);
	    ld->cl_ldrive = NULL;
	}
	if (ld->cl_lstatus != NULL) {
	    free(ld->cl_lstatus, CISS_MALLOC_CLASS);
	    ld->cl_lstatus = NULL;
	}
    }
    if (cr != NULL)
	ciss_release_request(cr);

    return(error);
}

/************************************************************************
 * Get status for a logical drive.
 *
 * XXX should we also do this in response to Test Unit Ready?
 */
static int
ciss_get_ldrive_status(struct ciss_softc *sc,  struct ciss_ldrive *ld)
{
    struct ciss_request		*cr;
    struct ciss_command		*cc;
    struct ciss_bmic_cdb	*cbc;
    int				error, command_status;

    /*
     * Build a CISS BMIC command to get the logical drive status.
     */
    if ((error = ciss_get_bmic_request(sc, &cr, CISS_BMIC_ID_LSTATUS,
				       (void **)&ld->cl_lstatus,
				       sizeof(*ld->cl_lstatus))) != 0)
	goto out;
    cc = cr->cr_cc;
    cc->header.address = *ld->cl_controller;	/* target controller */
    cbc = (struct ciss_bmic_cdb *)&(cc->cdb.cdb[0]);
    cbc->log_drive = CISS_LUN_TO_TARGET(ld->cl_address.logical.lun);

    /*
     * Submit the request and wait for it to complete.
     */
    if ((error = ciss_synch_request(cr, 60 * 1000)) != 0) {
	ciss_printf(sc, "error sending BMIC LSTATUS command (%d)\n", error);
	goto out;
    }

    /*
     * Check response.
     */
    ciss_report_request(cr, &command_status, NULL);
    switch(command_status) {
    case CISS_CMD_STATUS_SUCCESS:		/* buffer right size */
	break;
    case CISS_CMD_STATUS_DATA_UNDERRUN:
    case CISS_CMD_STATUS_DATA_OVERRUN:
	ciss_printf(sc, "data over/underrun reading logical drive status\n");
    default:
	ciss_printf(sc, "error reading logical drive status (%s)\n",
		    ciss_name_command_status(command_status));
	error = EIO;
	goto out;
    }

    /*
     * Set the drive's summary status based on the returned status.
     *
     * XXX testing shows that a failed JBOD drive comes back at next
     * boot in "queued for expansion" mode.  WTF?
     */
    ld->cl_status = ciss_decode_ldrive_status(ld->cl_lstatus->status);

out:
    if (cr != NULL)
	ciss_release_request(cr);
    return(error);
}

/************************************************************************
 * Notify the adapter of a config update.
 */
static int
ciss_update_config(struct ciss_softc *sc)
{
    int		i;

    debug_called(1);

    CISS_TL_SIMPLE_WRITE(sc, CISS_TL_SIMPLE_IDBR, CISS_TL_SIMPLE_IDBR_CFG_TABLE);
    for (i = 0; i < 1000; i++) {
	if (!(CISS_TL_SIMPLE_READ(sc, CISS_TL_SIMPLE_IDBR) &
	      CISS_TL_SIMPLE_IDBR_CFG_TABLE)) {
	    return(0);
	}
	DELAY(1000);
    }
    return(1);
}

/************************************************************************
 * Accept new media into a logical drive.
 *
 * XXX The drive has previously been offline; it would be good if we
 *     could make sure it's not open right now.
 */
static int
ciss_accept_media(struct ciss_softc *sc, struct ciss_ldrive *ld)
{
    struct ciss_request		*cr;
    struct ciss_command		*cc;
    struct ciss_bmic_cdb	*cbc;
    int				command_status;
    int				error = 0, ldrive;

    ldrive = CISS_LUN_TO_TARGET(ld->cl_address.logical.lun);

    debug(0, "bringing logical drive %d back online", ldrive);

    /*
     * Build a CISS BMIC command to bring the drive back online.
     */
    if ((error = ciss_get_bmic_request(sc, &cr, CISS_BMIC_ACCEPT_MEDIA,
				       NULL, 0)) != 0)
	goto out;
    cc = cr->cr_cc;
    cc->header.address = *ld->cl_controller;	/* target controller */
    cbc = (struct ciss_bmic_cdb *)&(cc->cdb.cdb[0]);
    cbc->log_drive = ldrive;

    /*
     * Submit the request and wait for it to complete.
     */
    if ((error = ciss_synch_request(cr, 60 * 1000)) != 0) {
	ciss_printf(sc, "error sending BMIC ACCEPT MEDIA command (%d)\n", error);
	goto out;
    }

    /*
     * Check response.
     */
    ciss_report_request(cr, &command_status, NULL);
    switch(command_status) {
    case CISS_CMD_STATUS_SUCCESS:		/* all OK */
	/* we should get a logical drive status changed event here */
	break;
    default:
	ciss_printf(cr->cr_sc, "error accepting media into failed logical drive (%s)\n",
		    ciss_name_command_status(command_status));
	break;
    }

out:
    if (cr != NULL)
	ciss_release_request(cr);
    return(error);
}

/************************************************************************
 * Release adapter resources.
 */
static void
ciss_free(struct ciss_softc *sc)
{
    struct ciss_request *cr;
    int			i, j;

    debug_called(1);

    /* we're going away */
    sc->ciss_flags |= CISS_FLAG_ABORTING;

    /* terminate the periodic heartbeat routine */
    callout_stop(&sc->ciss_periodic);

    /* cancel the Event Notify chain */
    ciss_notify_abort(sc);

    ciss_kill_notify_thread(sc);

    /* disconnect from CAM */
    if (sc->ciss_cam_sim) {
	for (i = 0; i < sc->ciss_max_logical_bus; i++) {
	    if (sc->ciss_cam_sim[i]) {
		xpt_bus_deregister(cam_sim_path(sc->ciss_cam_sim[i]));
		cam_sim_free(sc->ciss_cam_sim[i], 0);
	    }
	}
	for (i = CISS_PHYSICAL_BASE; i < sc->ciss_max_physical_bus +
	     CISS_PHYSICAL_BASE; i++) {
	    if (sc->ciss_cam_sim[i]) {
		xpt_bus_deregister(cam_sim_path(sc->ciss_cam_sim[i]));
		cam_sim_free(sc->ciss_cam_sim[i], 0);
	    }
	}
	free(sc->ciss_cam_sim, CISS_MALLOC_CLASS);
    }
    if (sc->ciss_cam_devq)
	cam_simq_free(sc->ciss_cam_devq);

    /* remove the control device */
    mtx_unlock(&sc->ciss_mtx);
    if (sc->ciss_dev_t != NULL)
	destroy_dev(sc->ciss_dev_t);

    /* Final cleanup of the callout. */
    callout_drain(&sc->ciss_periodic);
    mtx_destroy(&sc->ciss_mtx);

    /* free the controller data */
    if (sc->ciss_id != NULL)
	free(sc->ciss_id, CISS_MALLOC_CLASS);

    /* release I/O resources */
    if (sc->ciss_regs_resource != NULL)
	bus_release_resource(sc->ciss_dev, SYS_RES_MEMORY,
			     sc->ciss_regs_rid, sc->ciss_regs_resource);
    if (sc->ciss_cfg_resource != NULL)
	bus_release_resource(sc->ciss_dev, SYS_RES_MEMORY,
			     sc->ciss_cfg_rid, sc->ciss_cfg_resource);
    if (sc->ciss_intr != NULL)
	bus_teardown_intr(sc->ciss_dev, sc->ciss_irq_resource, sc->ciss_intr);
    if (sc->ciss_irq_resource != NULL)
	bus_release_resource(sc->ciss_dev, SYS_RES_IRQ,
			     sc->ciss_irq_rid[0], sc->ciss_irq_resource);
    if (sc->ciss_msi)
	pci_release_msi(sc->ciss_dev);

    while ((cr = ciss_dequeue_free(sc)) != NULL)
	bus_dmamap_destroy(sc->ciss_buffer_dmat, cr->cr_datamap);
    if (sc->ciss_buffer_dmat)
	bus_dma_tag_destroy(sc->ciss_buffer_dmat);

    /* destroy command memory and DMA tag */
    if (sc->ciss_command != NULL) {
	bus_dmamap_unload(sc->ciss_command_dmat, sc->ciss_command_map);
	bus_dmamem_free(sc->ciss_command_dmat, sc->ciss_command, sc->ciss_command_map);
    }
    if (sc->ciss_command_dmat)
	bus_dma_tag_destroy(sc->ciss_command_dmat);

    if (sc->ciss_reply) {
	bus_dmamap_unload(sc->ciss_reply_dmat, sc->ciss_reply_map);
	bus_dmamem_free(sc->ciss_reply_dmat, sc->ciss_reply, sc->ciss_reply_map);
    }
    if (sc->ciss_reply_dmat)
	bus_dma_tag_destroy(sc->ciss_reply_dmat);

    /* destroy DMA tags */
    if (sc->ciss_parent_dmat)
	bus_dma_tag_destroy(sc->ciss_parent_dmat);
    if (sc->ciss_logical) {
	for (i = 0; i < sc->ciss_max_logical_bus; i++) {
	    for (j = 0; j < sc->ciss_cfg->max_logical_supported; j++) {
		if (sc->ciss_logical[i][j].cl_ldrive)
		    free(sc->ciss_logical[i][j].cl_ldrive, CISS_MALLOC_CLASS);
		if (sc->ciss_logical[i][j].cl_lstatus)
		    free(sc->ciss_logical[i][j].cl_lstatus, CISS_MALLOC_CLASS);
	    }
	    free(sc->ciss_logical[i], CISS_MALLOC_CLASS);
	}
	free(sc->ciss_logical, CISS_MALLOC_CLASS);
    }

    if (sc->ciss_physical) {
	for (i = 0; i < sc->ciss_max_physical_bus; i++)
	    free(sc->ciss_physical[i], CISS_MALLOC_CLASS);
	free(sc->ciss_physical, CISS_MALLOC_CLASS);
    }

    if (sc->ciss_controllers)
	free(sc->ciss_controllers, CISS_MALLOC_CLASS);

}

/************************************************************************
 * Give a command to the adapter.
 *
 * Note that this uses the simple transport layer directly.  If we
 * want to add support for other layers, we'll need a switch of some
 * sort.
 *
 * Note that the simple transport layer has no way of refusing a
 * command; we only have as many request structures as the adapter
 * supports commands, so we don't have to check (this presumes that
 * the adapter can handle commands as fast as we throw them at it).
 */
static int
ciss_start(struct ciss_request *cr)
{
    struct ciss_command	*cc;	/* XXX debugging only */
    int			error;

    cc = cr->cr_cc;
    debug(2, "post command %d tag %d ", cr->cr_tag, cc->header.host_tag);

    /*
     * Map the request's data.
     */
    if ((error = ciss_map_request(cr)))
	return(error);

#if 0
    ciss_print_request(cr);
#endif

    return(0);
}

/************************************************************************
 * Fetch completed request(s) from the adapter, queue them for
 * completion handling.
 *
 * Note that this uses the simple transport layer directly.  If we
 * want to add support for other layers, we'll need a switch of some
 * sort.
 *
 * Note that the simple transport mechanism does not require any
 * reentrancy protection; the OPQ read is atomic.  If there is a
 * chance of a race with something else that might move the request
 * off the busy list, then we will have to lock against that
 * (eg. timeouts, etc.)
 */
static void
ciss_done(struct ciss_softc *sc, cr_qhead_t *qh)
{
    struct ciss_request	*cr;
    struct ciss_command	*cc;
    u_int32_t		tag, index;

    debug_called(3);

    /*
     * Loop quickly taking requests from the adapter and moving them
     * to the completed queue.
     */
    for (;;) {

	tag = CISS_TL_SIMPLE_FETCH_CMD(sc);
	if (tag == CISS_TL_SIMPLE_OPQ_EMPTY)
	    break;
	index = tag >> 2;
	debug(2, "completed command %d%s", index,
	      (tag & CISS_HDR_HOST_TAG_ERROR) ? " with error" : "");
	if (index >= sc->ciss_max_requests) {
	    ciss_printf(sc, "completed invalid request %d (0x%x)\n", index, tag);
	    continue;
	}
	cr = &(sc->ciss_request[index]);
	cc = cr->cr_cc;
	cc->header.host_tag = tag;	/* not updated by adapter */
	ciss_enqueue_complete(cr, qh);
    }

}

static void
ciss_perf_done(struct ciss_softc *sc, cr_qhead_t *qh)
{
    struct ciss_request	*cr;
    struct ciss_command	*cc;
    u_int32_t		tag, index;

    debug_called(3);

    /*
     * Loop quickly taking requests from the adapter and moving them
     * to the completed queue.
     */
    for (;;) {
	tag = sc->ciss_reply[sc->ciss_rqidx];
	if ((tag & CISS_CYCLE_MASK) != sc->ciss_cycle)
	    break;
	index = tag >> 2;
	debug(2, "completed command %d%s\n", index,
	      (tag & CISS_HDR_HOST_TAG_ERROR) ? " with error" : "");
	if (index < sc->ciss_max_requests) {
	    cr = &(sc->ciss_request[index]);
	    cc = cr->cr_cc;
	    cc->header.host_tag = tag;	/* not updated by adapter */
	    ciss_enqueue_complete(cr, qh);
	} else {
	    ciss_printf(sc, "completed invalid request %d (0x%x)\n", index, tag);
	}
	if (++sc->ciss_rqidx == sc->ciss_max_requests) {
	    sc->ciss_rqidx = 0;
	    sc->ciss_cycle ^= 1;
	}
    }

}

/************************************************************************
 * Take an interrupt from the adapter.
 */
static void
ciss_intr(void *arg)
{
    cr_qhead_t qh;
    struct ciss_softc	*sc = (struct ciss_softc *)arg;

    /*
     * The only interrupt we recognise indicates that there are
     * entries in the outbound post queue.
     */
    STAILQ_INIT(&qh);
    ciss_done(sc, &qh);
    mtx_lock(&sc->ciss_mtx);
    ciss_complete(sc, &qh);
    mtx_unlock(&sc->ciss_mtx);
}

static void
ciss_perf_intr(void *arg)
{
    struct ciss_softc	*sc = (struct ciss_softc *)arg;

    /* Clear the interrupt and flush the bridges.  Docs say that the flush
     * needs to be done twice, which doesn't seem right.
     */
    CISS_TL_PERF_CLEAR_INT(sc);
    CISS_TL_PERF_FLUSH_INT(sc);

    ciss_perf_msi_intr(sc);
}

static void
ciss_perf_msi_intr(void *arg)
{
    cr_qhead_t qh;
    struct ciss_softc	*sc = (struct ciss_softc *)arg;

    STAILQ_INIT(&qh);
    ciss_perf_done(sc, &qh);
    mtx_lock(&sc->ciss_mtx);
    ciss_complete(sc, &qh);
    mtx_unlock(&sc->ciss_mtx);
}


/************************************************************************
 * Process completed requests.
 *
 * Requests can be completed in three fashions:
 *
 * - by invoking a callback function (cr_complete is non-null)
 * - by waking up a sleeper (cr_flags has CISS_REQ_SLEEP set)
 * - by clearing the CISS_REQ_POLL flag in interrupt/timeout context
 */
static void
ciss_complete(struct ciss_softc *sc, cr_qhead_t *qh)
{
    struct ciss_request	*cr;

    debug_called(2);

    /*
     * Loop taking requests off the completed queue and performing
     * completion processing on them.
     */
    for (;;) {
	if ((cr = ciss_dequeue_complete(sc, qh)) == NULL)
	    break;
	ciss_unmap_request(cr);

	if ((cr->cr_flags & CISS_REQ_BUSY) == 0)
	    ciss_printf(sc, "WARNING: completing non-busy request\n");
	cr->cr_flags &= ~CISS_REQ_BUSY;

	/*
	 * If the request has a callback, invoke it.
	 */
	if (cr->cr_complete != NULL) {
	    cr->cr_complete(cr);
	    continue;
	}

	/*
	 * If someone is sleeping on this request, wake them up.
	 */
	if (cr->cr_flags & CISS_REQ_SLEEP) {
	    cr->cr_flags &= ~CISS_REQ_SLEEP;
	    wakeup(cr);
	    continue;
	}

	/*
	 * If someone is polling this request for completion, signal.
	 */
	if (cr->cr_flags & CISS_REQ_POLL) {
	    cr->cr_flags &= ~CISS_REQ_POLL;
	    continue;
	}

	/*
	 * Give up and throw the request back on the free queue.  This
	 * should never happen; resources will probably be lost.
	 */
	ciss_printf(sc, "WARNING: completed command with no submitter\n");
	ciss_enqueue_free(cr);
    }
}

/************************************************************************
 * Report on the completion status of a request, and pass back SCSI
 * and command status values.
 */
static int
_ciss_report_request(struct ciss_request *cr, int *command_status, int *scsi_status, const char *func)
{
    struct ciss_command		*cc;
    struct ciss_error_info	*ce;

    debug_called(2);

    cc = cr->cr_cc;
    ce = (struct ciss_error_info *)&(cc->sg[0]);

    /*
     * We don't consider data under/overrun an error for the Report
     * Logical/Physical LUNs commands.
     */
    if ((cc->header.host_tag & CISS_HDR_HOST_TAG_ERROR) &&
	((ce->command_status == CISS_CMD_STATUS_DATA_OVERRUN) ||
	 (ce->command_status == CISS_CMD_STATUS_DATA_UNDERRUN)) &&
	((cc->cdb.cdb[0] == CISS_OPCODE_REPORT_LOGICAL_LUNS) ||
	 (cc->cdb.cdb[0] == CISS_OPCODE_REPORT_PHYSICAL_LUNS) ||
	 (cc->cdb.cdb[0] == INQUIRY))) {
	cc->header.host_tag &= ~CISS_HDR_HOST_TAG_ERROR;
	debug(2, "ignoring irrelevant under/overrun error");
    }

    /*
     * Check the command's error bit, if clear, there's no status and
     * everything is OK.
     */
    if (!(cc->header.host_tag & CISS_HDR_HOST_TAG_ERROR)) {
	if (scsi_status != NULL)
	    *scsi_status = SCSI_STATUS_OK;
	if (command_status != NULL)
	    *command_status = CISS_CMD_STATUS_SUCCESS;
	return(0);
    } else {
	if (command_status != NULL)
	    *command_status = ce->command_status;
	if (scsi_status != NULL) {
	    if (ce->command_status == CISS_CMD_STATUS_TARGET_STATUS) {
		*scsi_status = ce->scsi_status;
	    } else {
		*scsi_status = -1;
	    }
	}
	if (bootverbose)
	    ciss_printf(cr->cr_sc, "command status 0x%x (%s) scsi status 0x%x\n",
			ce->command_status, ciss_name_command_status(ce->command_status),
			ce->scsi_status);
	if (ce->command_status == CISS_CMD_STATUS_INVALID_COMMAND) {
	    ciss_printf(cr->cr_sc, "invalid command, offense size %d at %d, value 0x%x, function %s\n",
			ce->additional_error_info.invalid_command.offense_size,
			ce->additional_error_info.invalid_command.offense_offset,
			ce->additional_error_info.invalid_command.offense_value,
			func);
	}
    }
#if 0
    ciss_print_request(cr);
#endif
    return(1);
}

/************************************************************************
 * Issue a request and don't return until it's completed.
 *
 * Depending on adapter status, we may poll or sleep waiting for
 * completion.
 */
static int
ciss_synch_request(struct ciss_request *cr, int timeout)
{
    if (cr->cr_sc->ciss_flags & CISS_FLAG_RUNNING) {
	return(ciss_wait_request(cr, timeout));
    } else {
	return(ciss_poll_request(cr, timeout));
    }
}

/************************************************************************
 * Issue a request and poll for completion.
 *
 * Timeout in milliseconds.
 */
static int
ciss_poll_request(struct ciss_request *cr, int timeout)
{
    cr_qhead_t qh;
    struct ciss_softc *sc;
    int		error;

    debug_called(2);

    STAILQ_INIT(&qh);
    sc = cr->cr_sc;
    cr->cr_flags |= CISS_REQ_POLL;
    if ((error = ciss_start(cr)) != 0)
	return(error);

    do {
	if (sc->ciss_perf)
	    ciss_perf_done(sc, &qh);
	else
	    ciss_done(sc, &qh);
	ciss_complete(sc, &qh);
	if (!(cr->cr_flags & CISS_REQ_POLL))
	    return(0);
	DELAY(1000);
    } while (timeout-- >= 0);
    return(EWOULDBLOCK);
}

/************************************************************************
 * Issue a request and sleep waiting for completion.
 *
 * Timeout in milliseconds.  Note that a spurious wakeup will reset
 * the timeout.
 */
static int
ciss_wait_request(struct ciss_request *cr, int timeout)
{
    int		error;

    debug_called(2);

    cr->cr_flags |= CISS_REQ_SLEEP;
    if ((error = ciss_start(cr)) != 0)
	return(error);

    while ((cr->cr_flags & CISS_REQ_SLEEP) && (error != EWOULDBLOCK)) {
	error = msleep_sbt(cr, &cr->cr_sc->ciss_mtx, PRIBIO, "cissREQ",
	    SBT_1MS * timeout, 0, 0);
    }
    return(error);
}

#if 0
/************************************************************************
 * Abort a request.  Note that a potential exists here to race the
 * request being completed; the caller must deal with this.
 */
static int
ciss_abort_request(struct ciss_request *ar)
{
    struct ciss_request		*cr;
    struct ciss_command		*cc;
    struct ciss_message_cdb	*cmc;
    int				error;

    debug_called(1);

    /* get a request */
    if ((error = ciss_get_request(ar->cr_sc, &cr)) != 0)
	return(error);

    /* build the abort command */
    cc = cr->cr_cc;
    cc->header.address.mode.mode = CISS_HDR_ADDRESS_MODE_PERIPHERAL;	/* addressing? */
    cc->header.address.physical.target = 0;
    cc->header.address.physical.bus = 0;
    cc->cdb.cdb_length = sizeof(*cmc);
    cc->cdb.type = CISS_CDB_TYPE_MESSAGE;
    cc->cdb.attribute = CISS_CDB_ATTRIBUTE_SIMPLE;
    cc->cdb.direction = CISS_CDB_DIRECTION_NONE;
    cc->cdb.timeout = 30;

    cmc = (struct ciss_message_cdb *)&(cc->cdb.cdb[0]);
    cmc->opcode = CISS_OPCODE_MESSAGE_ABORT;
    cmc->type = CISS_MESSAGE_ABORT_TASK;
    cmc->abort_tag = ar->cr_tag;	/* endianness?? */

    /*
     * Send the request and wait for a response.  If we believe we
     * aborted the request OK, clear the flag that indicates it's
     * running.
     */
    error = ciss_synch_request(cr, 35 * 1000);
    if (!error)
	error = ciss_report_request(cr, NULL, NULL);
    ciss_release_request(cr);

    return(error);
}
#endif


/************************************************************************
 * Fetch and initialise a request
 */
static int
ciss_get_request(struct ciss_softc *sc, struct ciss_request **crp)
{
    struct ciss_request *cr;

    debug_called(2);

    /*
     * Get a request and clean it up.
     */
    if ((cr = ciss_dequeue_free(sc)) == NULL)
	return(ENOMEM);

    cr->cr_data = NULL;
    cr->cr_flags = 0;
    cr->cr_complete = NULL;
    cr->cr_private = NULL;
    cr->cr_sg_tag = CISS_SG_MAX;	/* Backstop to prevent accidents */

    ciss_preen_command(cr);
    *crp = cr;
    return(0);
}

static void
ciss_preen_command(struct ciss_request *cr)
{
    struct ciss_command	*cc;
    u_int32_t		cmdphys;

    /*
     * Clean up the command structure.
     *
     * Note that we set up the error_info structure here, since the
     * length can be overwritten by any command.
     */
    cc = cr->cr_cc;
    cc->header.sg_in_list = 0;		/* kinda inefficient this way */
    cc->header.sg_total = 0;
    cc->header.host_tag = cr->cr_tag << 2;
    cc->header.host_tag_zeroes = 0;
    bzero(&(cc->sg[0]), CISS_COMMAND_ALLOC_SIZE - sizeof(struct ciss_command));
    cmdphys = cr->cr_ccphys;
    cc->error_info.error_info_address = cmdphys + sizeof(struct ciss_command);
    cc->error_info.error_info_length = CISS_COMMAND_ALLOC_SIZE - sizeof(struct ciss_command);
}

/************************************************************************
 * Release a request to the free list.
 */
static void
ciss_release_request(struct ciss_request *cr)
{
    struct ciss_softc	*sc;

    debug_called(2);

    sc = cr->cr_sc;

    /* release the request to the free queue */
    ciss_requeue_free(cr);
}

/************************************************************************
 * Allocate a request that will be used to send a BMIC command.  Do some
 * of the common setup here to avoid duplicating it everywhere else.
 */
static int
ciss_get_bmic_request(struct ciss_softc *sc, struct ciss_request **crp,
		      int opcode, void **bufp, size_t bufsize)
{
    struct ciss_request		*cr;
    struct ciss_command		*cc;
    struct ciss_bmic_cdb	*cbc;
    void			*buf;
    int				error;
    int				dataout;

    debug_called(2);

    cr = NULL;
    buf = NULL;

    /*
     * Get a request.
     */
    if ((error = ciss_get_request(sc, &cr)) != 0)
	goto out;

    /*
     * Allocate data storage if requested, determine the data direction.
     */
    dataout = 0;
    if ((bufsize > 0) && (bufp != NULL)) {
	if (*bufp == NULL) {
	    if ((buf = malloc(bufsize, CISS_MALLOC_CLASS, M_NOWAIT | M_ZERO)) == NULL) {
		error = ENOMEM;
		goto out;
	    }
	} else {
	    buf = *bufp;
	    dataout = 1;	/* we are given a buffer, so we are writing */
	}
    }

    /*
     * Build a CISS BMIC command to get the logical drive ID.
     */
    cr->cr_data = buf;
    cr->cr_length = bufsize;
    if (!dataout)
	cr->cr_flags = CISS_REQ_DATAIN;

    cc = cr->cr_cc;
    cc->header.address.physical.mode = CISS_HDR_ADDRESS_MODE_PERIPHERAL;
    cc->header.address.physical.bus = 0;
    cc->header.address.physical.target = 0;
    cc->cdb.cdb_length = sizeof(*cbc);
    cc->cdb.type = CISS_CDB_TYPE_COMMAND;
    cc->cdb.attribute = CISS_CDB_ATTRIBUTE_SIMPLE;
    cc->cdb.direction = dataout ? CISS_CDB_DIRECTION_WRITE : CISS_CDB_DIRECTION_READ;
    cc->cdb.timeout = 0;

    cbc = (struct ciss_bmic_cdb *)&(cc->cdb.cdb[0]);
    bzero(cbc, sizeof(*cbc));
    cbc->opcode = dataout ? CISS_ARRAY_CONTROLLER_WRITE : CISS_ARRAY_CONTROLLER_READ;
    cbc->bmic_opcode = opcode;
    cbc->size = htons((u_int16_t)bufsize);

out:
    if (error) {
	if (cr != NULL)
	    ciss_release_request(cr);
    } else {
	*crp = cr;
	if ((bufp != NULL) && (*bufp == NULL) && (buf != NULL))
	    *bufp = buf;
    }
    return(error);
}

/************************************************************************
 * Handle a command passed in from userspace.
 */
static int
ciss_user_command(struct ciss_softc *sc, IOCTL_Command_struct *ioc)
{
    struct ciss_request		*cr;
    struct ciss_command		*cc;
    struct ciss_error_info	*ce;
    int				error = 0;

    debug_called(1);

    cr = NULL;

    /*
     * Get a request.
     */
    while (ciss_get_request(sc, &cr) != 0)
	msleep(sc, &sc->ciss_mtx, PPAUSE, "cissREQ", hz);
    cc = cr->cr_cc;

    /*
     * Allocate an in-kernel databuffer if required, copy in user data.
     */
    mtx_unlock(&sc->ciss_mtx);
    cr->cr_length = ioc->buf_size;
    if (ioc->buf_size > 0) {
	if ((cr->cr_data = malloc(ioc->buf_size, CISS_MALLOC_CLASS, M_NOWAIT)) == NULL) {
	    error = ENOMEM;
	    goto out_unlocked;
	}
	if ((error = copyin(ioc->buf, cr->cr_data, ioc->buf_size))) {
	    debug(0, "copyin: bad data buffer %p/%d", ioc->buf, ioc->buf_size);
	    goto out_unlocked;
	}
    }

    /*
     * Build the request based on the user command.
     */
    bcopy(&ioc->LUN_info, &cc->header.address, sizeof(cc->header.address));
    bcopy(&ioc->Request, &cc->cdb, sizeof(cc->cdb));

    /* XXX anything else to populate here? */
    mtx_lock(&sc->ciss_mtx);

    /*
     * Run the command.
     */
    if ((error = ciss_synch_request(cr, 60 * 1000))) {
	debug(0, "request failed - %d", error);
	goto out;
    }

    /*
     * Check to see if the command succeeded.
     */
    ce = (struct ciss_error_info *)&(cc->sg[0]);
    if ((cc->header.host_tag & CISS_HDR_HOST_TAG_ERROR) == 0)
	bzero(ce, sizeof(*ce));

    /*
     * Copy the results back to the user.
     */
    bcopy(ce, &ioc->error_info, sizeof(*ce));
    mtx_unlock(&sc->ciss_mtx);
    if ((ioc->buf_size > 0) &&
	(error = copyout(cr->cr_data, ioc->buf, ioc->buf_size))) {
	debug(0, "copyout: bad data buffer %p/%d", ioc->buf, ioc->buf_size);
	goto out_unlocked;
    }

    /* done OK */
    error = 0;

out_unlocked:
    mtx_lock(&sc->ciss_mtx);

out:
    if ((cr != NULL) && (cr->cr_data != NULL))
	free(cr->cr_data, CISS_MALLOC_CLASS);
    if (cr != NULL)
	ciss_release_request(cr);
    return(error);
}

/************************************************************************
 * Map a request into bus-visible space, initialise the scatter/gather
 * list.
 */
static int
ciss_map_request(struct ciss_request *cr)
{
    struct ciss_softc	*sc;
    int			error = 0;

    debug_called(2);

    sc = cr->cr_sc;

    /* check that mapping is necessary */
    if (cr->cr_flags & CISS_REQ_MAPPED)
	return(0);

    cr->cr_flags |= CISS_REQ_MAPPED;

    bus_dmamap_sync(sc->ciss_command_dmat, sc->ciss_command_map,
		    BUS_DMASYNC_PREWRITE);

    if (cr->cr_data != NULL) {
	if (cr->cr_flags & CISS_REQ_CCB)
		error = bus_dmamap_load_ccb(sc->ciss_buffer_dmat,
					cr->cr_datamap, cr->cr_data,
					ciss_request_map_helper, cr, 0);
	else
		error = bus_dmamap_load(sc->ciss_buffer_dmat, cr->cr_datamap,
					cr->cr_data, cr->cr_length,
					ciss_request_map_helper, cr, 0);
	if (error != 0)
	    return (error);
    } else {
	/*
	 * Post the command to the adapter.
	 */
	cr->cr_sg_tag = CISS_SG_NONE;
	cr->cr_flags |= CISS_REQ_BUSY;
	if (sc->ciss_perf)
	    CISS_TL_PERF_POST_CMD(sc, cr);
	else
	    CISS_TL_SIMPLE_POST_CMD(sc, cr->cr_ccphys);
    }

    return(0);
}

static void
ciss_request_map_helper(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
    struct ciss_command	*cc;
    struct ciss_request *cr;
    struct ciss_softc	*sc;
    int			i;

    debug_called(2);

    cr = (struct ciss_request *)arg;
    sc = cr->cr_sc;
    cc = cr->cr_cc;

    for (i = 0; i < nseg; i++) {
	cc->sg[i].address = segs[i].ds_addr;
	cc->sg[i].length = segs[i].ds_len;
	cc->sg[i].extension = 0;
    }
    /* we leave the s/g table entirely within the command */
    cc->header.sg_in_list = nseg;
    cc->header.sg_total = nseg;

    if (cr->cr_flags & CISS_REQ_DATAIN)
	bus_dmamap_sync(sc->ciss_buffer_dmat, cr->cr_datamap, BUS_DMASYNC_PREREAD);
    if (cr->cr_flags & CISS_REQ_DATAOUT)
	bus_dmamap_sync(sc->ciss_buffer_dmat, cr->cr_datamap, BUS_DMASYNC_PREWRITE);

    if (nseg == 0)
	cr->cr_sg_tag = CISS_SG_NONE;
    else if (nseg == 1)
	cr->cr_sg_tag = CISS_SG_1;
    else if (nseg == 2)
	cr->cr_sg_tag = CISS_SG_2;
    else if (nseg <= 4)
	cr->cr_sg_tag = CISS_SG_4;
    else if (nseg <= 8)
	cr->cr_sg_tag = CISS_SG_8;
    else if (nseg <= 16)
	cr->cr_sg_tag = CISS_SG_16;
    else if (nseg <= 32)
	cr->cr_sg_tag = CISS_SG_32;
    else
	cr->cr_sg_tag = CISS_SG_MAX;

    /*
     * Post the command to the adapter.
     */
    cr->cr_flags |= CISS_REQ_BUSY;
    if (sc->ciss_perf)
	CISS_TL_PERF_POST_CMD(sc, cr);
    else
	CISS_TL_SIMPLE_POST_CMD(sc, cr->cr_ccphys);
}

/************************************************************************
 * Unmap a request from bus-visible space.
 */
static void
ciss_unmap_request(struct ciss_request *cr)
{
    struct ciss_softc	*sc;

    debug_called(2);

    sc = cr->cr_sc;

    /* check that unmapping is necessary */
    if ((cr->cr_flags & CISS_REQ_MAPPED) == 0)
	return;

    bus_dmamap_sync(sc->ciss_command_dmat, sc->ciss_command_map,
		    BUS_DMASYNC_POSTWRITE);

    if (cr->cr_data == NULL)
	goto out;

    if (cr->cr_flags & CISS_REQ_DATAIN)
	bus_dmamap_sync(sc->ciss_buffer_dmat, cr->cr_datamap, BUS_DMASYNC_POSTREAD);
    if (cr->cr_flags & CISS_REQ_DATAOUT)
	bus_dmamap_sync(sc->ciss_buffer_dmat, cr->cr_datamap, BUS_DMASYNC_POSTWRITE);

    bus_dmamap_unload(sc->ciss_buffer_dmat, cr->cr_datamap);
out:
    cr->cr_flags &= ~CISS_REQ_MAPPED;
}

/************************************************************************
 * Attach the driver to CAM.
 *
 * We put all the logical drives on a single SCSI bus.
 */
static int
ciss_cam_init(struct ciss_softc *sc)
{
    int			i, maxbus;

    debug_called(1);

    /*
     * Allocate a devq.  We can reuse this for the masked physical
     * devices if we decide to export these as well.
     */
    if ((sc->ciss_cam_devq = cam_simq_alloc(sc->ciss_max_requests - 2)) == NULL) {
	ciss_printf(sc, "can't allocate CAM SIM queue\n");
	return(ENOMEM);
    }

    /*
     * Create a SIM.
     *
     * This naturally wastes a bit of memory.  The alternative is to allocate
     * and register each bus as it is found, and then track them on a linked
     * list.  Unfortunately, the driver has a few places where it needs to
     * look up the SIM based solely on bus number, and it's unclear whether
     * a list traversal would work for these situations.
     */
    maxbus = max(sc->ciss_max_logical_bus, sc->ciss_max_physical_bus +
		 CISS_PHYSICAL_BASE);
    sc->ciss_cam_sim = malloc(maxbus * sizeof(struct cam_sim*),
			      CISS_MALLOC_CLASS, M_NOWAIT | M_ZERO);
    if (sc->ciss_cam_sim == NULL) {
	ciss_printf(sc, "can't allocate memory for controller SIM\n");
	return(ENOMEM);
    }

    for (i = 0; i < sc->ciss_max_logical_bus; i++) {
	if ((sc->ciss_cam_sim[i] = cam_sim_alloc(ciss_cam_action, ciss_cam_poll,
						 "ciss", sc,
						 device_get_unit(sc->ciss_dev),
						 &sc->ciss_mtx,
						 2,
						 sc->ciss_max_requests - 2,
						 sc->ciss_cam_devq)) == NULL) {
	    ciss_printf(sc, "can't allocate CAM SIM for controller %d\n", i);
	    return(ENOMEM);
	}

	/*
	 * Register bus with this SIM.
	 */
	mtx_lock(&sc->ciss_mtx);
	if (i == 0 || sc->ciss_controllers[i].physical.bus != 0) { 
	    if (xpt_bus_register(sc->ciss_cam_sim[i], sc->ciss_dev, i) != 0) {
		ciss_printf(sc, "can't register SCSI bus %d\n", i);
		mtx_unlock(&sc->ciss_mtx);
		return (ENXIO);
	    }
	}
	mtx_unlock(&sc->ciss_mtx);
    }

    for (i = CISS_PHYSICAL_BASE; i < sc->ciss_max_physical_bus +
	 CISS_PHYSICAL_BASE; i++) {
	if ((sc->ciss_cam_sim[i] = cam_sim_alloc(ciss_cam_action, ciss_cam_poll,
						 "ciss", sc,
						 device_get_unit(sc->ciss_dev),
						 &sc->ciss_mtx, 1,
						 sc->ciss_max_requests - 2,
						 sc->ciss_cam_devq)) == NULL) {
	    ciss_printf(sc, "can't allocate CAM SIM for controller %d\n", i);
	    return (ENOMEM);
	}

	mtx_lock(&sc->ciss_mtx);
	if (xpt_bus_register(sc->ciss_cam_sim[i], sc->ciss_dev, i) != 0) {
	    ciss_printf(sc, "can't register SCSI bus %d\n", i);
	    mtx_unlock(&sc->ciss_mtx);
	    return (ENXIO);
	}
	mtx_unlock(&sc->ciss_mtx);
    }

    return(0);
}

/************************************************************************
 * Initiate a rescan of the 'logical devices' SIM
 */
static void
ciss_cam_rescan_target(struct ciss_softc *sc, int bus, int target)
{
    union ccb		*ccb;

    debug_called(1);

    if ((ccb = xpt_alloc_ccb_nowait()) == NULL) {
	ciss_printf(sc, "rescan failed (can't allocate CCB)\n");
	return;
    }

    if (xpt_create_path(&ccb->ccb_h.path, NULL,
	    cam_sim_path(sc->ciss_cam_sim[bus]),
	    target, CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
	ciss_printf(sc, "rescan failed (can't create path)\n");
	xpt_free_ccb(ccb);
	return;
    }
    xpt_rescan(ccb);
    /* scan is now in progress */
}

/************************************************************************
 * Handle requests coming from CAM
 */
static void
ciss_cam_action(struct cam_sim *sim, union ccb *ccb)
{
    struct ciss_softc	*sc;
    struct ccb_scsiio	*csio;
    int			bus, target;
    int			physical;

    sc = cam_sim_softc(sim);
    bus = cam_sim_bus(sim);
    csio = (struct ccb_scsiio *)&ccb->csio;
    target = csio->ccb_h.target_id;
    physical = CISS_IS_PHYSICAL(bus);

    switch (ccb->ccb_h.func_code) {

	/* perform SCSI I/O */
    case XPT_SCSI_IO:
	if (!ciss_cam_action_io(sim, csio))
	    return;
	break;

	/* perform geometry calculations */
    case XPT_CALC_GEOMETRY:
    {
	struct ccb_calc_geometry	*ccg = &ccb->ccg;
	struct ciss_ldrive		*ld;

	debug(1, "XPT_CALC_GEOMETRY %d:%d:%d", cam_sim_bus(sim), ccb->ccb_h.target_id, ccb->ccb_h.target_lun);

	ld = NULL;
	if (!physical)
	    ld = &sc->ciss_logical[bus][target];
	    
	/*
	 * Use the cached geometry settings unless the fault tolerance
	 * is invalid.
	 */
	if (physical || ld->cl_geometry.fault_tolerance == 0xFF) {
	    u_int32_t			secs_per_cylinder;

	    ccg->heads = 255;
	    ccg->secs_per_track = 32;
	    secs_per_cylinder = ccg->heads * ccg->secs_per_track;
	    ccg->cylinders = ccg->volume_size / secs_per_cylinder;
	} else {
	    ccg->heads = ld->cl_geometry.heads;
	    ccg->secs_per_track = ld->cl_geometry.sectors;
	    ccg->cylinders = ntohs(ld->cl_geometry.cylinders);
	}
	ccb->ccb_h.status = CAM_REQ_CMP;
        break;
    }

	/* handle path attribute inquiry */
    case XPT_PATH_INQ:
    {
	struct ccb_pathinq	*cpi = &ccb->cpi;
	int			sg_length;

	debug(1, "XPT_PATH_INQ %d:%d:%d", cam_sim_bus(sim), ccb->ccb_h.target_id, ccb->ccb_h.target_lun);

	cpi->version_num = 1;
	cpi->hba_inquiry = PI_TAG_ABLE;	/* XXX is this correct? */
	cpi->target_sprt = 0;
	cpi->hba_misc = 0;
	cpi->max_target = sc->ciss_cfg->max_logical_supported;
	cpi->max_lun = 0;		/* 'logical drive' channel only */
	cpi->initiator_id = sc->ciss_cfg->max_logical_supported;
	strlcpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
	strlcpy(cpi->hba_vid, "CISS", HBA_IDLEN);
	strlcpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
	cpi->unit_number = cam_sim_unit(sim);
	cpi->bus_id = cam_sim_bus(sim);
	cpi->base_transfer_speed = 132 * 1024;	/* XXX what to set this to? */
	cpi->transport = XPORT_SPI;
	cpi->transport_version = 2;
	cpi->protocol = PROTO_SCSI;
	cpi->protocol_version = SCSI_REV_2;
	if (sc->ciss_cfg->max_sg_length == 0) {
		sg_length = 17;
	} else {
	/* XXX Fix for ZMR cards that advertise max_sg_length == 32
	 * Confusing bit here. max_sg_length is usually a power of 2. We always
	 * need to subtract 1 to account for partial pages. Then we need to 
	 * align on a valid PAGE_SIZE so we round down to the nearest power of 2. 
	 * Add 1 so we can then subtract it out in the assignment to maxio.
	 * The reason for all these shenanigans is to create a maxio value that
	 * creates IO operations to volumes that yield consistent operations
	 * with good performance.
	 */
		sg_length = sc->ciss_cfg->max_sg_length - 1;
		sg_length = (1 << (fls(sg_length) - 1)) + 1;
	}
	cpi->maxio = (min(CISS_MAX_SG_ELEMENTS, sg_length) - 1) * PAGE_SIZE;
	ccb->ccb_h.status = CAM_REQ_CMP;
	break;
    }

    case XPT_GET_TRAN_SETTINGS:
    {
	struct ccb_trans_settings	*cts = &ccb->cts;
	int				bus, target;
	struct ccb_trans_settings_spi *spi = &cts->xport_specific.spi;
	struct ccb_trans_settings_scsi *scsi = &cts->proto_specific.scsi;

	bus = cam_sim_bus(sim);
	target = cts->ccb_h.target_id;

	debug(1, "XPT_GET_TRAN_SETTINGS %d:%d", bus, target);
	/* disconnect always OK */
	cts->protocol = PROTO_SCSI;
	cts->protocol_version = SCSI_REV_2;
	cts->transport = XPORT_SPI;
	cts->transport_version = 2;

	spi->valid = CTS_SPI_VALID_DISC;
	spi->flags = CTS_SPI_FLAGS_DISC_ENB;

	scsi->valid = CTS_SCSI_VALID_TQ;
	scsi->flags = CTS_SCSI_FLAGS_TAG_ENB;

	cts->ccb_h.status = CAM_REQ_CMP;
	break;
    }

    default:		/* we can't do this */
	debug(1, "unspported func_code = 0x%x", ccb->ccb_h.func_code);
	ccb->ccb_h.status = CAM_REQ_INVALID;
	break;
    }

    xpt_done(ccb);
}

/************************************************************************
 * Handle a CAM SCSI I/O request.
 */
static int
ciss_cam_action_io(struct cam_sim *sim, struct ccb_scsiio *csio)
{
    struct ciss_softc	*sc;
    int			bus, target;
    struct ciss_request	*cr;
    struct ciss_command	*cc;
    int			error;

    sc = cam_sim_softc(sim);
    bus = cam_sim_bus(sim);
    target = csio->ccb_h.target_id;

    debug(2, "XPT_SCSI_IO %d:%d:%d", bus, target, csio->ccb_h.target_lun);

    /* check that the CDB pointer is not to a physical address */
    if ((csio->ccb_h.flags & CAM_CDB_POINTER) && (csio->ccb_h.flags & CAM_CDB_PHYS)) {
	debug(3, "  CDB pointer is to physical address");
	csio->ccb_h.status = CAM_REQ_CMP_ERR;
    }

    /* abandon aborted ccbs or those that have failed validation */
    if ((csio->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_INPROG) {
	debug(3, "abandoning CCB due to abort/validation failure");
	return(EINVAL);
    }

    /* handle emulation of some SCSI commands ourself */
    if (ciss_cam_emulate(sc, csio))
	return(0);

    /*
     * Get a request to manage this command.  If we can't, return the
     * ccb, freeze the queue and flag so that we unfreeze it when a
     * request completes.
     */
    if ((error = ciss_get_request(sc, &cr)) != 0) {
	xpt_freeze_simq(sim, 1);
	sc->ciss_flags |= CISS_FLAG_BUSY;
	csio->ccb_h.status |= CAM_REQUEUE_REQ;
	return(error);
    }

    /*
     * Build the command.
     */
    cc = cr->cr_cc;
    cr->cr_data = csio;
    cr->cr_length = csio->dxfer_len;
    cr->cr_complete = ciss_cam_complete;
    cr->cr_private = csio;

    /*
     * Target the right logical volume.
     */
    if (CISS_IS_PHYSICAL(bus))
	cc->header.address =
	    sc->ciss_physical[CISS_CAM_TO_PBUS(bus)][target].cp_address;
    else
	cc->header.address =
	    sc->ciss_logical[bus][target].cl_address;
    cc->cdb.cdb_length = csio->cdb_len;
    cc->cdb.type = CISS_CDB_TYPE_COMMAND;
    cc->cdb.attribute = CISS_CDB_ATTRIBUTE_SIMPLE;	/* XXX ordered tags? */
    if ((csio->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_OUT) {
	cr->cr_flags = CISS_REQ_DATAOUT | CISS_REQ_CCB;
	cc->cdb.direction = CISS_CDB_DIRECTION_WRITE;
    } else if ((csio->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN) {
	cr->cr_flags = CISS_REQ_DATAIN | CISS_REQ_CCB;
	cc->cdb.direction = CISS_CDB_DIRECTION_READ;
    } else {
	cr->cr_data = NULL;
	cr->cr_flags = 0;
	cc->cdb.direction = CISS_CDB_DIRECTION_NONE;
    }
    cc->cdb.timeout = (csio->ccb_h.timeout / 1000) + 1;
    if (csio->ccb_h.flags & CAM_CDB_POINTER) {
	bcopy(csio->cdb_io.cdb_ptr, &cc->cdb.cdb[0], csio->cdb_len);
    } else {
	bcopy(csio->cdb_io.cdb_bytes, &cc->cdb.cdb[0], csio->cdb_len);
    }

    /*
     * Submit the request to the adapter.
     *
     * Note that this may fail if we're unable to map the request (and
     * if we ever learn a transport layer other than simple, may fail
     * if the adapter rejects the command).
     */
    if ((error = ciss_start(cr)) != 0) {
	xpt_freeze_simq(sim, 1);
	csio->ccb_h.status |= CAM_RELEASE_SIMQ;
	if (error == EINPROGRESS) {
	    error = 0;
	} else {
	    csio->ccb_h.status |= CAM_REQUEUE_REQ;
	    ciss_release_request(cr);
	}
	return(error);
    }

    return(0);
}

/************************************************************************
 * Emulate SCSI commands the adapter doesn't handle as we might like.
 */
static int
ciss_cam_emulate(struct ciss_softc *sc, struct ccb_scsiio *csio)
{
    int		bus, target;
    u_int8_t	opcode;

    target = csio->ccb_h.target_id;
    bus = cam_sim_bus(xpt_path_sim(csio->ccb_h.path));
    opcode = (csio->ccb_h.flags & CAM_CDB_POINTER) ?
	*(u_int8_t *)csio->cdb_io.cdb_ptr : csio->cdb_io.cdb_bytes[0];

    if (CISS_IS_PHYSICAL(bus)) {
	if (sc->ciss_physical[CISS_CAM_TO_PBUS(bus)][target].cp_online != 1) {
	    csio->ccb_h.status |= CAM_SEL_TIMEOUT;
	    xpt_done((union ccb *)csio);
	    return(1);
	} else
	    return(0);
    }

    /*
     * Handle requests for volumes that don't exist or are not online.
     * A selection timeout is slightly better than an illegal request.
     * Other errors might be better.
     */
    if (sc->ciss_logical[bus][target].cl_status != CISS_LD_ONLINE) {
	csio->ccb_h.status |= CAM_SEL_TIMEOUT;
	xpt_done((union ccb *)csio);
	return(1);
    }

    /* if we have to fake Synchronise Cache */
    if (sc->ciss_flags & CISS_FLAG_FAKE_SYNCH) {
	/*
	 * If this is a Synchronise Cache command, typically issued when
	 * a device is closed, flush the adapter and complete now.
	 */
	if (((csio->ccb_h.flags & CAM_CDB_POINTER) ?
	     *(u_int8_t *)csio->cdb_io.cdb_ptr : csio->cdb_io.cdb_bytes[0]) == SYNCHRONIZE_CACHE) {
	    ciss_flush_adapter(sc);
	    csio->ccb_h.status |= CAM_REQ_CMP;
	    xpt_done((union ccb *)csio);
	    return(1);
	}
    }

    /* 
     * A CISS target can only ever have one lun per target. REPORT_LUNS requires
     * at least one LUN field to be pre created for us, so snag it and fill in
     * the least significant byte indicating 1 LUN here.  Emulate the command
     * return to shut up warning on console of a CDB error.  swb 
     */
    if (opcode == REPORT_LUNS && csio->dxfer_len > 0) {
       csio->data_ptr[3] = 8;
       csio->ccb_h.status |= CAM_REQ_CMP;
       xpt_done((union ccb *)csio);
       return(1);
    }

    return(0);
}

/************************************************************************
 * Check for possibly-completed commands.
 */
static void
ciss_cam_poll(struct cam_sim *sim)
{
    cr_qhead_t qh;
    struct ciss_softc	*sc = cam_sim_softc(sim);

    debug_called(2);

    STAILQ_INIT(&qh);
    if (sc->ciss_perf)
	ciss_perf_done(sc, &qh);
    else
	ciss_done(sc, &qh);
    ciss_complete(sc, &qh);
}

/************************************************************************
 * Handle completion of a command - pass results back through the CCB
 */
static void
ciss_cam_complete(struct ciss_request *cr)
{
    struct ciss_softc		*sc;
    struct ciss_command		*cc;
    struct ciss_error_info	*ce;
    struct ccb_scsiio		*csio;
    int				scsi_status;
    int				command_status;

    debug_called(2);

    sc = cr->cr_sc;
    cc = cr->cr_cc;
    ce = (struct ciss_error_info *)&(cc->sg[0]);
    csio = (struct ccb_scsiio *)cr->cr_private;

    /*
     * Extract status values from request.
     */
    ciss_report_request(cr, &command_status, &scsi_status);
    csio->scsi_status = scsi_status;

    /*
     * Handle specific SCSI status values.
     */
    switch(scsi_status) {
	/* no status due to adapter error */
    case -1:
	debug(0, "adapter error");
	csio->ccb_h.status |= CAM_REQ_CMP_ERR;
	break;

	/* no status due to command completed OK */
    case SCSI_STATUS_OK:		/* CISS_SCSI_STATUS_GOOD */
	debug(2, "SCSI_STATUS_OK");
	csio->ccb_h.status |= CAM_REQ_CMP;
	break;

	/* check condition, sense data included */
    case SCSI_STATUS_CHECK_COND:	/* CISS_SCSI_STATUS_CHECK_CONDITION */
	debug(0, "SCSI_STATUS_CHECK_COND  sense size %d  resid %d\n",
	      ce->sense_length, ce->residual_count);
	bzero(&csio->sense_data, SSD_FULL_SIZE);
	bcopy(&ce->sense_info[0], &csio->sense_data, ce->sense_length);
	if (csio->sense_len > ce->sense_length)
		csio->sense_resid = csio->sense_len - ce->sense_length;
	else
		csio->sense_resid = 0;
	csio->resid = ce->residual_count;
	csio->ccb_h.status |= CAM_SCSI_STATUS_ERROR | CAM_AUTOSNS_VALID;
#ifdef CISS_DEBUG
	{
	    struct scsi_sense_data	*sns = (struct scsi_sense_data *)&ce->sense_info[0];
	    debug(0, "sense key %x", scsi_get_sense_key(sns, csio->sense_len -
		  csio->sense_resid, /*show_errors*/ 1));
	}
#endif
	break;

    case SCSI_STATUS_BUSY:		/* CISS_SCSI_STATUS_BUSY */
	debug(0, "SCSI_STATUS_BUSY");
	csio->ccb_h.status |= CAM_SCSI_BUSY;
	break;

    default:
	debug(0, "unknown status 0x%x", csio->scsi_status);
	csio->ccb_h.status |= CAM_REQ_CMP_ERR;
	break;
    }

    /* handle post-command fixup */
    ciss_cam_complete_fixup(sc, csio);

    ciss_release_request(cr);
    if (sc->ciss_flags & CISS_FLAG_BUSY) {
	sc->ciss_flags &= ~CISS_FLAG_BUSY;
	if (csio->ccb_h.status & CAM_RELEASE_SIMQ)
	    xpt_release_simq(xpt_path_sim(csio->ccb_h.path), 0);
	else
	    csio->ccb_h.status |= CAM_RELEASE_SIMQ;
    }
    xpt_done((union ccb *)csio);
}

/********************************************************************************
 * Fix up the result of some commands here.
 */
static void
ciss_cam_complete_fixup(struct ciss_softc *sc, struct ccb_scsiio *csio)
{
    struct scsi_inquiry_data	*inq;
    struct ciss_ldrive		*cl;
    uint8_t			*cdb;
    int				bus, target;

    cdb = (csio->ccb_h.flags & CAM_CDB_POINTER) ?
	 (uint8_t *)csio->cdb_io.cdb_ptr : csio->cdb_io.cdb_bytes;
    if (cdb[0] == INQUIRY && 
	(cdb[1] & SI_EVPD) == 0 &&
	(csio->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN &&
	csio->dxfer_len >= SHORT_INQUIRY_LENGTH) {

	inq = (struct scsi_inquiry_data *)csio->data_ptr;
	target = csio->ccb_h.target_id;
	bus = cam_sim_bus(xpt_path_sim(csio->ccb_h.path));

	/*
	 * If the controller is in JBOD mode, there are no logical volumes.
	 * Let the disks be probed and dealt with via CAM.  Else, mask off 
	 * the physical disks and setup the parts of the inq structure for
	 * the logical volume.  swb
	 */
	if( !(sc->ciss_id->PowerUPNvramFlags & PWR_UP_FLAG_JBOD_ENABLED)){
		if (CISS_IS_PHYSICAL(bus)) {
	    		if (SID_TYPE(inq) == T_DIRECT)
				inq->device = (inq->device & 0xe0) | T_NODEVICE;
	    		return;
		}
		cl = &sc->ciss_logical[bus][target];

		padstr(inq->vendor, "HP",
	       		SID_VENDOR_SIZE);
		padstr(inq->product,
	       		ciss_name_ldrive_org(cl->cl_ldrive->fault_tolerance),
	       		SID_PRODUCT_SIZE);
		padstr(inq->revision,
	       		ciss_name_ldrive_status(cl->cl_lstatus->status),
	       		SID_REVISION_SIZE);
	}
    }
}


/********************************************************************************
 * Name the device at (target)
 *
 * XXX is this strictly correct?
 */
static int
ciss_name_device(struct ciss_softc *sc, int bus, int target)
{
    struct cam_periph	*periph;
    struct cam_path	*path;
    int			status;

    if (CISS_IS_PHYSICAL(bus))
	return (0);

    status = xpt_create_path(&path, NULL, cam_sim_path(sc->ciss_cam_sim[bus]),
			     target, 0);

    if (status == CAM_REQ_CMP) {
	xpt_path_lock(path);
	periph = cam_periph_find(path, NULL);
	xpt_path_unlock(path);
	xpt_free_path(path);
	if (periph != NULL) {
		sprintf(sc->ciss_logical[bus][target].cl_name, "%s%d",
			periph->periph_name, periph->unit_number);
		return(0);
	}
    }
    sc->ciss_logical[bus][target].cl_name[0] = 0;
    return(ENOENT);
}

/************************************************************************
 * Periodic status monitoring.
 */
static void
ciss_periodic(void *arg)
{
    struct ciss_softc	*sc;
    struct ciss_request	*cr = NULL;
    struct ciss_command	*cc = NULL;
    int			error = 0;

    debug_called(1);

    sc = (struct ciss_softc *)arg;

    /*
     * Check the adapter heartbeat.
     */
    if (sc->ciss_cfg->heartbeat == sc->ciss_heartbeat) {
	sc->ciss_heart_attack++;
	debug(0, "adapter heart attack in progress 0x%x/%d",
	      sc->ciss_heartbeat, sc->ciss_heart_attack);
	if (sc->ciss_heart_attack == 3) {
	    ciss_printf(sc, "ADAPTER HEARTBEAT FAILED\n");
	    ciss_disable_adapter(sc);
	    return;
	}
    } else {
	sc->ciss_heartbeat = sc->ciss_cfg->heartbeat;
	sc->ciss_heart_attack = 0;
	debug(3, "new heartbeat 0x%x", sc->ciss_heartbeat);
    }

    /*
     * Send the NOP message and wait for a response.
     */
    if (ciss_nop_message_heartbeat != 0 && (error = ciss_get_request(sc, &cr)) == 0) {
	cc = cr->cr_cc;
	cr->cr_complete = ciss_nop_complete;
	cc->cdb.cdb_length = 1;
	cc->cdb.type = CISS_CDB_TYPE_MESSAGE;
	cc->cdb.attribute = CISS_CDB_ATTRIBUTE_SIMPLE;
	cc->cdb.direction = CISS_CDB_DIRECTION_WRITE;
	cc->cdb.timeout = 0;
	cc->cdb.cdb[0] = CISS_OPCODE_MESSAGE_NOP;

	if ((error = ciss_start(cr)) != 0) {
	    ciss_printf(sc, "SENDING NOP MESSAGE FAILED\n");
	}
    }

    /*
     * If the notify event request has died for some reason, or has
     * not started yet, restart it.
     */
    if (!(sc->ciss_flags & CISS_FLAG_NOTIFY_OK)) {
	debug(0, "(re)starting Event Notify chain");
	ciss_notify_event(sc);
    }

    /*
     * Reschedule.
     */
    callout_reset(&sc->ciss_periodic, CISS_HEARTBEAT_RATE * hz, ciss_periodic, sc);
}

static void
ciss_nop_complete(struct ciss_request *cr)
{
    struct ciss_softc		*sc;
    static int			first_time = 1;

    sc = cr->cr_sc;
    if (ciss_report_request(cr, NULL, NULL) != 0) {
	if (first_time == 1) {
	    first_time = 0;
	    ciss_printf(sc, "SENDING NOP MESSAGE FAILED (not logging anymore)\n");
	}
    }

    ciss_release_request(cr);
}

/************************************************************************
 * Disable the adapter.
 *
 * The all requests in completed queue is failed with hardware error.
 * This will cause failover in a multipath configuration.
 */
static void
ciss_disable_adapter(struct ciss_softc *sc)
{
    cr_qhead_t			qh;
    struct ciss_request		*cr;
    struct ciss_command		*cc;
    struct ciss_error_info	*ce;
    int				i;

    CISS_TL_SIMPLE_DISABLE_INTERRUPTS(sc);
    pci_disable_busmaster(sc->ciss_dev);
    sc->ciss_flags &= ~CISS_FLAG_RUNNING;

    for (i = 1; i < sc->ciss_max_requests; i++) {
	cr = &sc->ciss_request[i];
	if ((cr->cr_flags & CISS_REQ_BUSY) == 0)
	    continue;

	cc = cr->cr_cc;
	ce = (struct ciss_error_info *)&(cc->sg[0]);
	ce->command_status = CISS_CMD_STATUS_HARDWARE_ERROR;
	ciss_enqueue_complete(cr, &qh);
    }

    for (;;) {
	if ((cr = ciss_dequeue_complete(sc, &qh)) == NULL)
	    break;
    
	/*
	 * If the request has a callback, invoke it.
	 */
	if (cr->cr_complete != NULL) {
	    cr->cr_complete(cr);
	    continue;
	}

	/*
	 * If someone is sleeping on this request, wake them up.
	 */
	if (cr->cr_flags & CISS_REQ_SLEEP) {
	    cr->cr_flags &= ~CISS_REQ_SLEEP;
	    wakeup(cr);
	    continue;
	}
    }
}

/************************************************************************
 * Request a notification response from the adapter.
 *
 * If (cr) is NULL, this is the first request of the adapter, so
 * reset the adapter's message pointer and start with the oldest
 * message available.
 */
static void
ciss_notify_event(struct ciss_softc *sc)
{
    struct ciss_request		*cr;
    struct ciss_command		*cc;
    struct ciss_notify_cdb	*cnc;
    int				error;

    debug_called(1);

    cr = sc->ciss_periodic_notify;

    /* get a request if we don't already have one */
    if (cr == NULL) {
	if ((error = ciss_get_request(sc, &cr)) != 0) {
	    debug(0, "can't get notify event request");
	    goto out;
	}
	sc->ciss_periodic_notify = cr;
	cr->cr_complete = ciss_notify_complete;
	debug(1, "acquired request %d", cr->cr_tag);
    }

    /*
     * Get a databuffer if we don't already have one, note that the
     * adapter command wants a larger buffer than the actual
     * structure.
     */
    if (cr->cr_data == NULL) {
	if ((cr->cr_data = malloc(CISS_NOTIFY_DATA_SIZE, CISS_MALLOC_CLASS, M_NOWAIT)) == NULL) {
	    debug(0, "can't get notify event request buffer");
	    error = ENOMEM;
	    goto out;
	}
	cr->cr_length = CISS_NOTIFY_DATA_SIZE;
    }

    /* re-setup the request's command (since we never release it) XXX overkill*/
    ciss_preen_command(cr);

    /* (re)build the notify event command */
    cc = cr->cr_cc;
    cc->header.address.physical.mode = CISS_HDR_ADDRESS_MODE_PERIPHERAL;
    cc->header.address.physical.bus = 0;
    cc->header.address.physical.target = 0;

    cc->cdb.cdb_length = sizeof(*cnc);
    cc->cdb.type = CISS_CDB_TYPE_COMMAND;
    cc->cdb.attribute = CISS_CDB_ATTRIBUTE_SIMPLE;
    cc->cdb.direction = CISS_CDB_DIRECTION_READ;
    cc->cdb.timeout = 0;	/* no timeout, we hope */

    cnc = (struct ciss_notify_cdb *)&(cc->cdb.cdb[0]);
    bzero(cr->cr_data, CISS_NOTIFY_DATA_SIZE);
    cnc->opcode = CISS_OPCODE_READ;
    cnc->command = CISS_COMMAND_NOTIFY_ON_EVENT;
    cnc->timeout = 0;		/* no timeout, we hope */
    cnc->synchronous = 0;
    cnc->ordered = 0;
    cnc->seek_to_oldest = 0;
    if ((sc->ciss_flags & CISS_FLAG_RUNNING) == 0)
	cnc->new_only = 1;
    else
	cnc->new_only = 0;
    cnc->length = htonl(CISS_NOTIFY_DATA_SIZE);

    /* submit the request */
    error = ciss_start(cr);

 out:
    if (error) {
	if (cr != NULL) {
	    if (cr->cr_data != NULL)
		free(cr->cr_data, CISS_MALLOC_CLASS);
	    ciss_release_request(cr);
	}
	sc->ciss_periodic_notify = NULL;
	debug(0, "can't submit notify event request");
	sc->ciss_flags &= ~CISS_FLAG_NOTIFY_OK;
    } else {
	debug(1, "notify event submitted");
	sc->ciss_flags |= CISS_FLAG_NOTIFY_OK;
    }
}

static void
ciss_notify_complete(struct ciss_request *cr)
{
    struct ciss_command	*cc;
    struct ciss_notify	*cn;
    struct ciss_softc	*sc;
    int			scsi_status;
    int			command_status;
    debug_called(1);

    cc = cr->cr_cc;
    cn = (struct ciss_notify *)cr->cr_data;
    sc = cr->cr_sc;

    /*
     * Report request results, decode status.
     */
    ciss_report_request(cr, &command_status, &scsi_status);

    /*
     * Abort the chain on a fatal error.
     *
     * XXX which of these are actually errors?
     */
    if ((command_status != CISS_CMD_STATUS_SUCCESS) &&
	(command_status != CISS_CMD_STATUS_TARGET_STATUS) &&
	(command_status != CISS_CMD_STATUS_TIMEOUT)) {	/* XXX timeout? */
	ciss_printf(sc, "fatal error in Notify Event request (%s)\n",
		    ciss_name_command_status(command_status));
	ciss_release_request(cr);
	sc->ciss_flags &= ~CISS_FLAG_NOTIFY_OK;
	return;
    }

    /*
     * If the adapter gave us a text message, print it.
     */
    if (cn->message[0] != 0)
	ciss_printf(sc, "*** %.80s\n", cn->message);

    debug(0, "notify event class %d subclass %d detail %d",
		cn->class, cn->subclass, cn->detail);

    /*
     * If the response indicates that the notifier has been aborted,
     * release the notifier command.
     */
    if ((cn->class == CISS_NOTIFY_NOTIFIER) &&
	(cn->subclass == CISS_NOTIFY_NOTIFIER_STATUS) &&
	(cn->detail == 1)) {
	debug(0, "notifier exiting");
	sc->ciss_flags &= ~CISS_FLAG_NOTIFY_OK;
	ciss_release_request(cr);
	sc->ciss_periodic_notify = NULL;
	wakeup(&sc->ciss_periodic_notify);
    } else {
	/* Handle notify events in a kernel thread */
	ciss_enqueue_notify(cr);
	sc->ciss_periodic_notify = NULL;
	wakeup(&sc->ciss_periodic_notify);
	wakeup(&sc->ciss_notify);
    }
    /*
     * Send a new notify event command, if we're not aborting.
     */
    if (!(sc->ciss_flags & CISS_FLAG_ABORTING)) {
	ciss_notify_event(sc);
    }
}

/************************************************************************
 * Abort the Notify Event chain.
 *
 * Note that we can't just abort the command in progress; we have to
 * explicitly issue an Abort Notify Event command in order for the
 * adapter to clean up correctly.
 *
 * If we are called with CISS_FLAG_ABORTING set in the adapter softc,
 * the chain will not restart itself.
 */
static int
ciss_notify_abort(struct ciss_softc *sc)
{
    struct ciss_request		*cr;
    struct ciss_command		*cc;
    struct ciss_notify_cdb	*cnc;
    int				error, command_status, scsi_status;

    debug_called(1);

    cr = NULL;
    error = 0;

    /* verify that there's an outstanding command */
    if (!(sc->ciss_flags & CISS_FLAG_NOTIFY_OK))
	goto out;

    /* get a command to issue the abort with */
    if ((error = ciss_get_request(sc, &cr)))
	goto out;

    /* get a buffer for the result */
    if ((cr->cr_data = malloc(CISS_NOTIFY_DATA_SIZE, CISS_MALLOC_CLASS, M_NOWAIT)) == NULL) {
	debug(0, "can't get notify event request buffer");
	error = ENOMEM;
	goto out;
    }
    cr->cr_length = CISS_NOTIFY_DATA_SIZE;

    /* build the CDB */
    cc = cr->cr_cc;
    cc->header.address.physical.mode = CISS_HDR_ADDRESS_MODE_PERIPHERAL;
    cc->header.address.physical.bus = 0;
    cc->header.address.physical.target = 0;
    cc->cdb.cdb_length = sizeof(*cnc);
    cc->cdb.type = CISS_CDB_TYPE_COMMAND;
    cc->cdb.attribute = CISS_CDB_ATTRIBUTE_SIMPLE;
    cc->cdb.direction = CISS_CDB_DIRECTION_READ;
    cc->cdb.timeout = 0;	/* no timeout, we hope */

    cnc = (struct ciss_notify_cdb *)&(cc->cdb.cdb[0]);
    bzero(cnc, sizeof(*cnc));
    cnc->opcode = CISS_OPCODE_WRITE;
    cnc->command = CISS_COMMAND_ABORT_NOTIFY;
    cnc->length = htonl(CISS_NOTIFY_DATA_SIZE);

    ciss_print_request(cr);

    /*
     * Submit the request and wait for it to complete.
     */
    if ((error = ciss_synch_request(cr, 60 * 1000)) != 0) {
	ciss_printf(sc, "Abort Notify Event command failed (%d)\n", error);
	goto out;
    }

    /*
     * Check response.
     */
    ciss_report_request(cr, &command_status, &scsi_status);
    switch(command_status) {
    case CISS_CMD_STATUS_SUCCESS:
	break;
    case CISS_CMD_STATUS_INVALID_COMMAND:
	/*
	 * Some older adapters don't support the CISS version of this
	 * command.  Fall back to using the BMIC version.
	 */
	error = ciss_notify_abort_bmic(sc);
	if (error != 0)
	    goto out;
	break;

    case CISS_CMD_STATUS_TARGET_STATUS:
	/*
	 * This can happen if the adapter thinks there wasn't an outstanding
	 * Notify Event command but we did.  We clean up here.
	 */
	if (scsi_status == CISS_SCSI_STATUS_CHECK_CONDITION) {
	    if (sc->ciss_periodic_notify != NULL)
		ciss_release_request(sc->ciss_periodic_notify);
	    error = 0;
	    goto out;
	}
	/* FALLTHROUGH */

    default:
	ciss_printf(sc, "Abort Notify Event command failed (%s)\n",
		    ciss_name_command_status(command_status));
	error = EIO;
	goto out;
    }

    /*
     * Sleep waiting for the notifier command to complete.  Note
     * that if it doesn't, we may end up in a bad situation, since
     * the adapter may deliver it later.  Also note that the adapter
     * requires the Notify Event command to be cancelled in order to
     * maintain internal bookkeeping.
     */
    while (sc->ciss_periodic_notify != NULL) {
	error = msleep(&sc->ciss_periodic_notify, &sc->ciss_mtx, PRIBIO, "cissNEA", hz * 5);
	if (error == EWOULDBLOCK) {
	    ciss_printf(sc, "Notify Event command failed to abort, adapter may wedge.\n");
	    break;
	}
    }

 out:
    /* release the cancel request */
    if (cr != NULL) {
	if (cr->cr_data != NULL)
	    free(cr->cr_data, CISS_MALLOC_CLASS);
	ciss_release_request(cr);
    }
    if (error == 0)
	sc->ciss_flags &= ~CISS_FLAG_NOTIFY_OK;
    return(error);
}

/************************************************************************
 * Abort the Notify Event chain using a BMIC command.
 */
static int
ciss_notify_abort_bmic(struct ciss_softc *sc)
{
    struct ciss_request			*cr;
    int					error, command_status;

    debug_called(1);

    cr = NULL;
    error = 0;

    /* verify that there's an outstanding command */
    if (!(sc->ciss_flags & CISS_FLAG_NOTIFY_OK))
	goto out;

    /*
     * Build a BMIC command to cancel the Notify on Event command.
     *
     * Note that we are sending a CISS opcode here.  Odd.
     */
    if ((error = ciss_get_bmic_request(sc, &cr, CISS_COMMAND_ABORT_NOTIFY,
				       NULL, 0)) != 0)
	goto out;

    /*
     * Submit the request and wait for it to complete.
     */
    if ((error = ciss_synch_request(cr, 60 * 1000)) != 0) {
	ciss_printf(sc, "error sending BMIC Cancel Notify on Event command (%d)\n", error);
	goto out;
    }

    /*
     * Check response.
     */
    ciss_report_request(cr, &command_status, NULL);
    switch(command_status) {
    case CISS_CMD_STATUS_SUCCESS:
	break;
    default:
	ciss_printf(sc, "error cancelling Notify on Event (%s)\n",
		    ciss_name_command_status(command_status));
	error = EIO;
	goto out;
    }

out:
    if (cr != NULL)
	ciss_release_request(cr);
    return(error);
}

/************************************************************************
 * Handle rescanning all the logical volumes when a notify event
 * causes the drives to come online or offline.
 */
static void
ciss_notify_rescan_logical(struct ciss_softc *sc)
{
    struct ciss_lun_report      *cll;
    struct ciss_ldrive		*ld;
    int                         i, j, ndrives;

    /*
     * We must rescan all logical volumes to get the right logical
     * drive address.
     */
    cll = ciss_report_luns(sc, CISS_OPCODE_REPORT_LOGICAL_LUNS,
                           sc->ciss_cfg->max_logical_supported);
    if (cll == NULL)
        return;

    ndrives = (ntohl(cll->list_size) / sizeof(union ciss_device_address));

    /*
     * Delete any of the drives which were destroyed by the
     * firmware.
     */
    for (i = 0; i < sc->ciss_max_logical_bus; i++) {
	for (j = 0; j < sc->ciss_cfg->max_logical_supported; j++) {
	    ld = &sc->ciss_logical[i][j];

	    if (ld->cl_update == 0)
		continue;

	    if (ld->cl_status != CISS_LD_ONLINE) {
		ciss_cam_rescan_target(sc, i, j);
		ld->cl_update = 0;
		if (ld->cl_ldrive)
		    free(ld->cl_ldrive, CISS_MALLOC_CLASS);
		if (ld->cl_lstatus)
		    free(ld->cl_lstatus, CISS_MALLOC_CLASS);

		ld->cl_ldrive = NULL;
		ld->cl_lstatus = NULL;
	    }
	}
    }

    /*
     * Scan for new drives.
     */
    for (i = 0; i < ndrives; i++) {
	int	bus, target;

	bus 	= CISS_LUN_TO_BUS(cll->lun[i].logical.lun);
	target	= CISS_LUN_TO_TARGET(cll->lun[i].logical.lun);
	ld	= &sc->ciss_logical[bus][target];

	if (ld->cl_update == 0)
		continue;

	ld->cl_update		= 0;
	ld->cl_address		= cll->lun[i];
	ld->cl_controller	= &sc->ciss_controllers[bus];
	if (ciss_identify_logical(sc, ld) == 0) {
	    ciss_cam_rescan_target(sc, bus, target);
	}
    }
    free(cll, CISS_MALLOC_CLASS);
}

/************************************************************************
 * Handle a notify event relating to the status of a logical drive.
 *
 * XXX need to be able to defer some of these to properly handle
 *     calling the "ID Physical drive" command, unless the 'extended'
 *     drive IDs are always in BIG_MAP format.
 */
static void
ciss_notify_logical(struct ciss_softc *sc, struct ciss_notify *cn)
{
    struct ciss_ldrive	*ld;
    int			ostatus, bus, target;

    debug_called(2);

    bus		= cn->device.physical.bus;
    target	= cn->data.logical_status.logical_drive;
    ld		= &sc->ciss_logical[bus][target];

    switch (cn->subclass) {
    case CISS_NOTIFY_LOGICAL_STATUS:
	switch (cn->detail) {
	case 0:
	    ciss_name_device(sc, bus, target);
	    ciss_printf(sc, "logical drive %d (%s) changed status %s->%s, spare status 0x%b\n",
			cn->data.logical_status.logical_drive, ld->cl_name,
			ciss_name_ldrive_status(cn->data.logical_status.previous_state),
			ciss_name_ldrive_status(cn->data.logical_status.new_state),
			cn->data.logical_status.spare_state,
			"\20\1configured\2rebuilding\3failed\4in use\5available\n");

	    /*
	     * Update our idea of the drive's status.
	     */
	    ostatus = ciss_decode_ldrive_status(cn->data.logical_status.previous_state);
	    ld->cl_status = ciss_decode_ldrive_status(cn->data.logical_status.new_state);
	    if (ld->cl_lstatus != NULL)
		ld->cl_lstatus->status = cn->data.logical_status.new_state;

	    /*
	     * Have CAM rescan the drive if its status has changed.
	     */
	    if (ostatus != ld->cl_status) {
		ld->cl_update = 1;
		ciss_notify_rescan_logical(sc);
	    }

	    break;

	case 1:	/* logical drive has recognised new media, needs Accept Media Exchange */
	    ciss_name_device(sc, bus, target);
	    ciss_printf(sc, "logical drive %d (%s) media exchanged, ready to go online\n",
			cn->data.logical_status.logical_drive, ld->cl_name);
	    ciss_accept_media(sc, ld);

	    ld->cl_update = 1;
	    ld->cl_status = ciss_decode_ldrive_status(cn->data.logical_status.new_state);
	    ciss_notify_rescan_logical(sc);
	    break;

	case 2:
	case 3:
	    ciss_printf(sc, "rebuild of logical drive %d (%s) failed due to %s error\n",
			cn->data.rebuild_aborted.logical_drive,
			ld->cl_name,
			(cn->detail == 2) ? "read" : "write");
	    break;
	}
	break;

    case CISS_NOTIFY_LOGICAL_ERROR:
	if (cn->detail == 0) {
	    ciss_printf(sc, "FATAL I/O ERROR on logical drive %d (%s), SCSI port %d ID %d\n",
			cn->data.io_error.logical_drive,
			ld->cl_name,
			cn->data.io_error.failure_bus,
			cn->data.io_error.failure_drive);
	    /* XXX should we take the drive down at this point, or will we be told? */
	}
	break;

    case CISS_NOTIFY_LOGICAL_SURFACE:
	if (cn->detail == 0)
	    ciss_printf(sc, "logical drive %d (%s) completed consistency initialisation\n",
			cn->data.consistency_completed.logical_drive,
			ld->cl_name);
	break;
    }
}

/************************************************************************
 * Handle a notify event relating to the status of a physical drive.
 */
static void
ciss_notify_physical(struct ciss_softc *sc, struct ciss_notify *cn)
{
}

/************************************************************************
 * Handle a notify event relating to the status of a physical drive.
 */
static void
ciss_notify_hotplug(struct ciss_softc *sc, struct ciss_notify *cn)
{
    struct ciss_lun_report *cll = NULL;
    int bus, target;

    switch (cn->subclass) {
    case CISS_NOTIFY_HOTPLUG_PHYSICAL:
    case CISS_NOTIFY_HOTPLUG_NONDISK:
	bus = CISS_BIG_MAP_BUS(sc, cn->data.drive.big_physical_drive_number);
	target =
	    CISS_BIG_MAP_TARGET(sc, cn->data.drive.big_physical_drive_number);

	if (cn->detail == 0) {
	    /*
	     * Mark the device offline so that it'll start producing selection
	     * timeouts to the upper layer.
	     */
	    if ((bus >= 0) && (target >= 0))
		sc->ciss_physical[bus][target].cp_online = 0;
	} else {
	    /*
	     * Rescan the physical lun list for new items
	     */
	    cll = ciss_report_luns(sc, CISS_OPCODE_REPORT_PHYSICAL_LUNS,
				   sc->ciss_cfg->max_physical_supported);
	    if (cll == NULL) {
		ciss_printf(sc, "Warning, cannot get physical lun list\n");
		break;
	    }
	    ciss_filter_physical(sc, cll);
	}
	break;

    default:
	ciss_printf(sc, "Unknown hotplug event %d\n", cn->subclass);
	return;
    }

    if (cll != NULL)
	free(cll, CISS_MALLOC_CLASS);
}

/************************************************************************
 * Handle deferred processing of notify events.  Notify events may need
 * sleep which is unsafe during an interrupt.
 */
static void
ciss_notify_thread(void *arg)
{
    struct ciss_softc		*sc;
    struct ciss_request		*cr;
    struct ciss_notify		*cn;

    sc = (struct ciss_softc *)arg;
    mtx_lock(&sc->ciss_mtx);

    for (;;) {
	if (STAILQ_EMPTY(&sc->ciss_notify) != 0 &&
	    (sc->ciss_flags & CISS_FLAG_THREAD_SHUT) == 0) {
	    msleep(&sc->ciss_notify, &sc->ciss_mtx, PUSER, "idle", 0);
	}

	if (sc->ciss_flags & CISS_FLAG_THREAD_SHUT)
	    break;

	cr = ciss_dequeue_notify(sc);

	if (cr == NULL)
		panic("cr null");
	cn = (struct ciss_notify *)cr->cr_data;

	switch (cn->class) {
	case CISS_NOTIFY_HOTPLUG:
	    ciss_notify_hotplug(sc, cn);
	    break;
	case CISS_NOTIFY_LOGICAL:
	    ciss_notify_logical(sc, cn);
	    break;
	case CISS_NOTIFY_PHYSICAL:
	    ciss_notify_physical(sc, cn);
	    break;
	}

	ciss_release_request(cr);

    }
    sc->ciss_notify_thread = NULL;
    wakeup(&sc->ciss_notify_thread);

    mtx_unlock(&sc->ciss_mtx);
    kproc_exit(0);
}

/************************************************************************
 * Start the notification kernel thread.
 */
static void
ciss_spawn_notify_thread(struct ciss_softc *sc)
{

    if (kproc_create((void(*)(void *))ciss_notify_thread, sc,
		       &sc->ciss_notify_thread, 0, 0, "ciss_notify%d",
		       device_get_unit(sc->ciss_dev)))
	panic("Could not create notify thread\n");
}

/************************************************************************
 * Kill the notification kernel thread.
 */
static void
ciss_kill_notify_thread(struct ciss_softc *sc)
{

    if (sc->ciss_notify_thread == NULL)
	return;

    sc->ciss_flags |= CISS_FLAG_THREAD_SHUT;
    wakeup(&sc->ciss_notify);
    msleep(&sc->ciss_notify_thread, &sc->ciss_mtx, PUSER, "thtrm", 0);
}

/************************************************************************
 * Print a request.
 */
static void
ciss_print_request(struct ciss_request *cr)
{
    struct ciss_softc	*sc;
    struct ciss_command	*cc;
    int			i;

    sc = cr->cr_sc;
    cc = cr->cr_cc;

    ciss_printf(sc, "REQUEST @ %p\n", cr);
    ciss_printf(sc, "  data %p/%d  tag %d  flags %b\n",
	      cr->cr_data, cr->cr_length, cr->cr_tag, cr->cr_flags,
	      "\20\1mapped\2sleep\3poll\4dataout\5datain\n");
    ciss_printf(sc, "  sg list/total %d/%d  host tag 0x%x\n",
		cc->header.sg_in_list, cc->header.sg_total, cc->header.host_tag);
    switch(cc->header.address.mode.mode) {
    case CISS_HDR_ADDRESS_MODE_PERIPHERAL:
    case CISS_HDR_ADDRESS_MODE_MASK_PERIPHERAL:
	ciss_printf(sc, "  physical bus %d target %d\n",
		    cc->header.address.physical.bus, cc->header.address.physical.target);
	break;
    case CISS_HDR_ADDRESS_MODE_LOGICAL:
	ciss_printf(sc, "  logical unit %d\n", cc->header.address.logical.lun);
	break;
    }
    ciss_printf(sc, "  %s cdb length %d type %s attribute %s\n",
		(cc->cdb.direction == CISS_CDB_DIRECTION_NONE) ? "no-I/O" :
		(cc->cdb.direction == CISS_CDB_DIRECTION_READ) ? "READ" :
		(cc->cdb.direction == CISS_CDB_DIRECTION_WRITE) ? "WRITE" : "??",
		cc->cdb.cdb_length,
		(cc->cdb.type == CISS_CDB_TYPE_COMMAND) ? "command" :
		(cc->cdb.type == CISS_CDB_TYPE_MESSAGE) ? "message" : "??",
		(cc->cdb.attribute == CISS_CDB_ATTRIBUTE_UNTAGGED) ? "untagged" :
		(cc->cdb.attribute == CISS_CDB_ATTRIBUTE_SIMPLE) ? "simple" :
		(cc->cdb.attribute == CISS_CDB_ATTRIBUTE_HEAD_OF_QUEUE) ? "head-of-queue" :
		(cc->cdb.attribute == CISS_CDB_ATTRIBUTE_ORDERED) ? "ordered" :
		(cc->cdb.attribute == CISS_CDB_ATTRIBUTE_AUTO_CONTINGENT) ? "auto-contingent" : "??");
    ciss_printf(sc, "  %*D\n", cc->cdb.cdb_length, &cc->cdb.cdb[0], " ");

    if (cc->header.host_tag & CISS_HDR_HOST_TAG_ERROR) {
	/* XXX print error info */
    } else {
	/* since we don't use chained s/g, don't support it here */
	for (i = 0; i < cc->header.sg_in_list; i++) {
	    if ((i % 4) == 0)
		ciss_printf(sc, "   ");
	    printf("0x%08x/%d ", (u_int32_t)cc->sg[i].address, cc->sg[i].length);
	    if ((((i + 1) % 4) == 0) || (i == (cc->header.sg_in_list - 1)))
		printf("\n");
	}
    }
}

/************************************************************************
 * Print information about the status of a logical drive.
 */
static void
ciss_print_ldrive(struct ciss_softc *sc, struct ciss_ldrive *ld)
{
    int		bus, target, i;

    if (ld->cl_lstatus == NULL) {
	printf("does not exist\n");
	return;
    }

    /* print drive status */
    switch(ld->cl_lstatus->status) {
    case CISS_LSTATUS_OK:
	printf("online\n");
	break;
    case CISS_LSTATUS_INTERIM_RECOVERY:
	printf("in interim recovery mode\n");
	break;
    case CISS_LSTATUS_READY_RECOVERY:
	printf("ready to begin recovery\n");
	break;
    case CISS_LSTATUS_RECOVERING:
	bus = CISS_BIG_MAP_BUS(sc, ld->cl_lstatus->drive_rebuilding);
	target = CISS_BIG_MAP_BUS(sc, ld->cl_lstatus->drive_rebuilding);
	printf("being recovered, working on physical drive %d.%d, %u blocks remaining\n",
	       bus, target, ld->cl_lstatus->blocks_to_recover);
	break;
    case CISS_LSTATUS_EXPANDING:
	printf("being expanded, %u blocks remaining\n",
	       ld->cl_lstatus->blocks_to_recover);
	break;
    case CISS_LSTATUS_QUEUED_FOR_EXPANSION:
	printf("queued for expansion\n");
	break;
    case CISS_LSTATUS_FAILED:
	printf("queued for expansion\n");
	break;
    case CISS_LSTATUS_WRONG_PDRIVE:
	printf("wrong physical drive inserted\n");
	break;
    case CISS_LSTATUS_MISSING_PDRIVE:
	printf("missing a needed physical drive\n");
	break;
    case CISS_LSTATUS_BECOMING_READY:
	printf("becoming ready\n");
	break;
    }

    /* print failed physical drives */
    for (i = 0; i < CISS_BIG_MAP_ENTRIES / 8; i++) {
	bus = CISS_BIG_MAP_BUS(sc, ld->cl_lstatus->drive_failure_map[i]);
	target = CISS_BIG_MAP_TARGET(sc, ld->cl_lstatus->drive_failure_map[i]);
	if (bus == -1)
	    continue;
	ciss_printf(sc, "physical drive %d:%d (%x) failed\n", bus, target,
		    ld->cl_lstatus->drive_failure_map[i]);
    }
}

#ifdef CISS_DEBUG
#include "opt_ddb.h"
#ifdef DDB
#include <ddb/ddb.h>
/************************************************************************
 * Print information about the controller/driver.
 */
static void
ciss_print_adapter(struct ciss_softc *sc)
{
    int		i, j;

    ciss_printf(sc, "ADAPTER:\n");
    for (i = 0; i < CISSQ_COUNT; i++) {
	ciss_printf(sc, "%s     %d/%d\n",
	    i == 0 ? "free" :
	    i == 1 ? "busy" : "complete",
	    sc->ciss_qstat[i].q_length,
	    sc->ciss_qstat[i].q_max);
    }
    ciss_printf(sc, "max_requests %d\n", sc->ciss_max_requests);
    ciss_printf(sc, "flags %b\n", sc->ciss_flags,
	"\20\1notify_ok\2control_open\3aborting\4running\21fake_synch\22bmic_abort\n");

    for (i = 0; i < sc->ciss_max_logical_bus; i++) {
	for (j = 0; j < sc->ciss_cfg->max_logical_supported; j++) {
	    ciss_printf(sc, "LOGICAL DRIVE %d:  ", i);
	    ciss_print_ldrive(sc, &sc->ciss_logical[i][j]);
	}
    }

    /* XXX Should physical drives be printed out here? */

    for (i = 1; i < sc->ciss_max_requests; i++)
	ciss_print_request(sc->ciss_request + i);
}

/* DDB hook */
DB_COMMAND(ciss_prt, db_ciss_prt)
{
    struct ciss_softc	*sc;
    devclass_t dc;
    int maxciss, i;

    dc = devclass_find("ciss");
    if ( dc == NULL ) {
        printf("%s: can't find devclass!\n", __func__);
        return;
    }
    maxciss = devclass_get_maxunit(dc);
    for (i = 0; i < maxciss; i++) {
        sc = devclass_get_softc(dc, i);
	ciss_print_adapter(sc);
    }
}
#endif
#endif

/************************************************************************
 * Return a name for a logical drive status value.
 */
static const char *
ciss_name_ldrive_status(int status)
{
    switch (status) {
    case CISS_LSTATUS_OK:
	return("OK");
    case CISS_LSTATUS_FAILED:
	return("failed");
    case CISS_LSTATUS_NOT_CONFIGURED:
	return("not configured");
    case CISS_LSTATUS_INTERIM_RECOVERY:
	return("interim recovery");
    case CISS_LSTATUS_READY_RECOVERY:
	return("ready for recovery");
    case CISS_LSTATUS_RECOVERING:
	return("recovering");
    case CISS_LSTATUS_WRONG_PDRIVE:
	return("wrong physical drive inserted");
    case CISS_LSTATUS_MISSING_PDRIVE:
	return("missing physical drive");
    case CISS_LSTATUS_EXPANDING:
	return("expanding");
    case CISS_LSTATUS_BECOMING_READY:
	return("becoming ready");
    case CISS_LSTATUS_QUEUED_FOR_EXPANSION:
	return("queued for expansion");
    }
    return("unknown status");
}

/************************************************************************
 * Return an online/offline/nonexistent value for a logical drive
 * status value.
 */
static int
ciss_decode_ldrive_status(int status)
{
    switch(status) {
    case CISS_LSTATUS_NOT_CONFIGURED:
	return(CISS_LD_NONEXISTENT);

    case CISS_LSTATUS_OK:
    case CISS_LSTATUS_INTERIM_RECOVERY:
    case CISS_LSTATUS_READY_RECOVERY:
    case CISS_LSTATUS_RECOVERING:
    case CISS_LSTATUS_EXPANDING:
    case CISS_LSTATUS_QUEUED_FOR_EXPANSION:
	return(CISS_LD_ONLINE);

    case CISS_LSTATUS_FAILED:
    case CISS_LSTATUS_WRONG_PDRIVE:
    case CISS_LSTATUS_MISSING_PDRIVE:
    case CISS_LSTATUS_BECOMING_READY:
    default:
	return(CISS_LD_OFFLINE);
    }
}


/************************************************************************
 * Return a name for a logical drive's organisation.
 */
static const char *
ciss_name_ldrive_org(int org)
{
    switch(org) {
    case CISS_LDRIVE_RAID0:
	return("RAID 0");
    case CISS_LDRIVE_RAID1:
	return("RAID 1(1+0)");
    case CISS_LDRIVE_RAID4:
	return("RAID 4");
    case CISS_LDRIVE_RAID5:
	return("RAID 5");
    case CISS_LDRIVE_RAID51:
	return("RAID 5+1");
    case CISS_LDRIVE_RAIDADG:
	return("RAID ADG");
    }
    return("unknown");
}

/************************************************************************
 * Return a name for a command status value.
 */
static const char *
ciss_name_command_status(int status)
{
    switch(status) {
    case CISS_CMD_STATUS_SUCCESS:
	return("success");
    case CISS_CMD_STATUS_TARGET_STATUS:
	return("target status");
    case CISS_CMD_STATUS_DATA_UNDERRUN:
	return("data underrun");
    case CISS_CMD_STATUS_DATA_OVERRUN:
	return("data overrun");
    case CISS_CMD_STATUS_INVALID_COMMAND:
	return("invalid command");
    case CISS_CMD_STATUS_PROTOCOL_ERROR:
	return("protocol error");
    case CISS_CMD_STATUS_HARDWARE_ERROR:
	return("hardware error");
    case CISS_CMD_STATUS_CONNECTION_LOST:
	return("connection lost");
    case CISS_CMD_STATUS_ABORTED:
	return("aborted");
    case CISS_CMD_STATUS_ABORT_FAILED:
	return("abort failed");
    case CISS_CMD_STATUS_UNSOLICITED_ABORT:
	return("unsolicited abort");
    case CISS_CMD_STATUS_TIMEOUT:
	return("timeout");
    case CISS_CMD_STATUS_UNABORTABLE:
	return("unabortable");
    }
    return("unknown status");
}

/************************************************************************
 * Handle an open on the control device.
 */
static int
ciss_open(struct cdev *dev, int flags, int fmt, struct thread *p)
{
    struct ciss_softc	*sc;

    debug_called(1);

    sc = (struct ciss_softc *)dev->si_drv1;

    /* we might want to veto if someone already has us open */

    mtx_lock(&sc->ciss_mtx);
    sc->ciss_flags |= CISS_FLAG_CONTROL_OPEN;
    mtx_unlock(&sc->ciss_mtx);
    return(0);
}

/************************************************************************
 * Handle the last close on the control device.
 */
static int
ciss_close(struct cdev *dev, int flags, int fmt, struct thread *p)
{
    struct ciss_softc	*sc;

    debug_called(1);

    sc = (struct ciss_softc *)dev->si_drv1;

    mtx_lock(&sc->ciss_mtx);
    sc->ciss_flags &= ~CISS_FLAG_CONTROL_OPEN;
    mtx_unlock(&sc->ciss_mtx);
    return (0);
}

/********************************************************************************
 * Handle adapter-specific control operations.
 *
 * Note that the API here is compatible with the Linux driver, in order to
 * simplify the porting of Compaq's userland tools.
 */
static int
ciss_ioctl(struct cdev *dev, u_long cmd, caddr_t addr, int32_t flag, struct thread *p)
{
    struct ciss_softc		*sc;
    IOCTL_Command_struct	*ioc	= (IOCTL_Command_struct *)addr;
#ifdef __amd64__
    IOCTL_Command_struct32	*ioc32	= (IOCTL_Command_struct32 *)addr;
    IOCTL_Command_struct	ioc_swab;
#endif
    int				error;

    debug_called(1);

    sc = (struct ciss_softc *)dev->si_drv1;
    error = 0;
    mtx_lock(&sc->ciss_mtx);

    switch(cmd) {
    case CCISS_GETQSTATS:
    {
	union ciss_statrequest *cr = (union ciss_statrequest *)addr;

	switch (cr->cs_item) {
	case CISSQ_FREE:
	case CISSQ_NOTIFY:
	    bcopy(&sc->ciss_qstat[cr->cs_item], &cr->cs_qstat,
		sizeof(struct ciss_qstat));
	    break;
	default:
	    error = ENOIOCTL;
	    break;
	}

	break;
    }

    case CCISS_GETPCIINFO:
    {
	cciss_pci_info_struct	*pis = (cciss_pci_info_struct *)addr;

	pis->bus = pci_get_bus(sc->ciss_dev);
	pis->dev_fn = pci_get_slot(sc->ciss_dev);
        pis->board_id = (pci_get_subvendor(sc->ciss_dev) << 16) |
                pci_get_subdevice(sc->ciss_dev);

	break;
    }

    case CCISS_GETINTINFO:
    {
	cciss_coalint_struct	*cis = (cciss_coalint_struct *)addr;

	cis->delay = sc->ciss_cfg->interrupt_coalesce_delay;
	cis->count = sc->ciss_cfg->interrupt_coalesce_count;

	break;
    }

    case CCISS_SETINTINFO:
    {
	cciss_coalint_struct	*cis = (cciss_coalint_struct *)addr;

	if ((cis->delay == 0) && (cis->count == 0)) {
	    error = EINVAL;
	    break;
	}

	/*
	 * XXX apparently this is only safe if the controller is idle,
	 *     we should suspend it before doing this.
	 */
	sc->ciss_cfg->interrupt_coalesce_delay = cis->delay;
	sc->ciss_cfg->interrupt_coalesce_count = cis->count;

	if (ciss_update_config(sc))
	    error = EIO;

	/* XXX resume the controller here */
	break;
    }

    case CCISS_GETNODENAME:
	bcopy(sc->ciss_cfg->server_name, (NodeName_type *)addr,
	      sizeof(NodeName_type));
	break;

    case CCISS_SETNODENAME:
	bcopy((NodeName_type *)addr, sc->ciss_cfg->server_name,
	      sizeof(NodeName_type));
	if (ciss_update_config(sc))
	    error = EIO;
	break;

    case CCISS_GETHEARTBEAT:
	*(Heartbeat_type *)addr = sc->ciss_cfg->heartbeat;
	break;

    case CCISS_GETBUSTYPES:
	*(BusTypes_type *)addr = sc->ciss_cfg->bus_types;
	break;

    case CCISS_GETFIRMVER:
	bcopy(sc->ciss_id->running_firmware_revision, (FirmwareVer_type *)addr,
	      sizeof(FirmwareVer_type));
	break;

    case CCISS_GETDRIVERVER:
	*(DriverVer_type *)addr = CISS_DRIVER_VERSION;
	break;

    case CCISS_REVALIDVOLS:
	/*
	 * This is a bit ugly; to do it "right" we really need
	 * to find any disks that have changed, kick CAM off them,
	 * then rescan only these disks.  It'd be nice if they
	 * a) told us which disk(s) they were going to play with,
	 * and b) which ones had arrived. 8(
	 */
	break;

#ifdef __amd64__
    case CCISS_PASSTHRU32:
	ioc_swab.LUN_info	= ioc32->LUN_info;
	ioc_swab.Request	= ioc32->Request;
	ioc_swab.error_info	= ioc32->error_info;
	ioc_swab.buf_size	= ioc32->buf_size;
	ioc_swab.buf		= (u_int8_t *)(uintptr_t)ioc32->buf;
	ioc			= &ioc_swab;
	/* FALLTHROUGH */
#endif

    case CCISS_PASSTHRU:
	error = ciss_user_command(sc, ioc);
	break;

    default:
	debug(0, "unknown ioctl 0x%lx", cmd);

	debug(1, "CCISS_GETPCIINFO:   0x%lx", CCISS_GETPCIINFO);
	debug(1, "CCISS_GETINTINFO:   0x%lx", CCISS_GETINTINFO);
	debug(1, "CCISS_SETINTINFO:   0x%lx", CCISS_SETINTINFO);
	debug(1, "CCISS_GETNODENAME:  0x%lx", CCISS_GETNODENAME);
	debug(1, "CCISS_SETNODENAME:  0x%lx", CCISS_SETNODENAME);
	debug(1, "CCISS_GETHEARTBEAT: 0x%lx", CCISS_GETHEARTBEAT);
	debug(1, "CCISS_GETBUSTYPES:  0x%lx", CCISS_GETBUSTYPES);
	debug(1, "CCISS_GETFIRMVER:   0x%lx", CCISS_GETFIRMVER);
	debug(1, "CCISS_GETDRIVERVER: 0x%lx", CCISS_GETDRIVERVER);
	debug(1, "CCISS_REVALIDVOLS:  0x%lx", CCISS_REVALIDVOLS);
	debug(1, "CCISS_PASSTHRU:     0x%lx", CCISS_PASSTHRU);

	error = ENOIOCTL;
	break;
    }

    mtx_unlock(&sc->ciss_mtx);
    return(error);
}
