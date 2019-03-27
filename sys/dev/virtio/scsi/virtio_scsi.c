/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012, Bryan Venteicher <bryanv@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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

/* Driver for VirtIO SCSI devices. */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/sglist.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/callout.h>
#include <sys/queue.h>
#include <sys/sbuf.h>

#include <machine/stdarg.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_sim.h>
#include <cam/cam_periph.h>
#include <cam/cam_xpt_sim.h>
#include <cam/cam_debug.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>

#include <dev/virtio/virtio.h>
#include <dev/virtio/virtqueue.h>
#include <dev/virtio/scsi/virtio_scsi.h>
#include <dev/virtio/scsi/virtio_scsivar.h>

#include "virtio_if.h"

static int	vtscsi_modevent(module_t, int, void *);

static int	vtscsi_probe(device_t);
static int	vtscsi_attach(device_t);
static int	vtscsi_detach(device_t);
static int	vtscsi_suspend(device_t);
static int	vtscsi_resume(device_t);

static void	vtscsi_negotiate_features(struct vtscsi_softc *);
static void	vtscsi_read_config(struct vtscsi_softc *,
		    struct virtio_scsi_config *);
static int	vtscsi_maximum_segments(struct vtscsi_softc *, int);
static int	vtscsi_alloc_virtqueues(struct vtscsi_softc *);
static void	vtscsi_write_device_config(struct vtscsi_softc *);
static int	vtscsi_reinit(struct vtscsi_softc *);

static int	vtscsi_alloc_cam(struct vtscsi_softc *);
static int	vtscsi_register_cam(struct vtscsi_softc *);
static void	vtscsi_free_cam(struct vtscsi_softc *);
static void	vtscsi_cam_async(void *, uint32_t, struct cam_path *, void *);
static int	vtscsi_register_async(struct vtscsi_softc *);
static void	vtscsi_deregister_async(struct vtscsi_softc *);
static void	vtscsi_cam_action(struct cam_sim *, union ccb *);
static void	vtscsi_cam_poll(struct cam_sim *);

static void	vtscsi_cam_scsi_io(struct vtscsi_softc *, struct cam_sim *,
		    union ccb *);
static void	vtscsi_cam_get_tran_settings(struct vtscsi_softc *,
		    union ccb *);
static void	vtscsi_cam_reset_bus(struct vtscsi_softc *, union ccb *);
static void	vtscsi_cam_reset_dev(struct vtscsi_softc *, union ccb *);
static void	vtscsi_cam_abort(struct vtscsi_softc *, union ccb *);
static void	vtscsi_cam_path_inquiry(struct vtscsi_softc *,
		    struct cam_sim *, union ccb *);

static int	vtscsi_sg_append_scsi_buf(struct vtscsi_softc *,
		    struct sglist *, struct ccb_scsiio *);
static int	vtscsi_fill_scsi_cmd_sglist(struct vtscsi_softc *,
		    struct vtscsi_request *, int *, int *);
static int	vtscsi_execute_scsi_cmd(struct vtscsi_softc *,
		    struct vtscsi_request *);
static int	vtscsi_start_scsi_cmd(struct vtscsi_softc *, union ccb *);
static void	vtscsi_complete_abort_timedout_scsi_cmd(struct vtscsi_softc *,
		    struct vtscsi_request *);
static int	vtscsi_abort_timedout_scsi_cmd(struct vtscsi_softc *,
		    struct vtscsi_request *);
static void	vtscsi_timedout_scsi_cmd(void *);
static cam_status vtscsi_scsi_cmd_cam_status(struct virtio_scsi_cmd_resp *);
static cam_status vtscsi_complete_scsi_cmd_response(struct vtscsi_softc *,
		    struct ccb_scsiio *, struct virtio_scsi_cmd_resp *);
static void	vtscsi_complete_scsi_cmd(struct vtscsi_softc *,
		    struct vtscsi_request *);

static void	vtscsi_poll_ctrl_req(struct vtscsi_softc *,
		    struct vtscsi_request *);
static int	vtscsi_execute_ctrl_req(struct vtscsi_softc *,
		    struct vtscsi_request *, struct sglist *, int, int, int);
static void	vtscsi_complete_abort_task_cmd(struct vtscsi_softc *c,
		    struct vtscsi_request *);
static int	vtscsi_execute_abort_task_cmd(struct vtscsi_softc *,
		    struct vtscsi_request *);
static int	vtscsi_execute_reset_dev_cmd(struct vtscsi_softc *,
		    struct vtscsi_request *);

static void	vtscsi_get_request_lun(uint8_t [], target_id_t *, lun_id_t *);
static void	vtscsi_set_request_lun(struct ccb_hdr *, uint8_t []);
static void	vtscsi_init_scsi_cmd_req(struct ccb_scsiio *,
		    struct virtio_scsi_cmd_req *);
static void	vtscsi_init_ctrl_tmf_req(struct ccb_hdr *, uint32_t,
		    uintptr_t, struct virtio_scsi_ctrl_tmf_req *);

static void	vtscsi_freeze_simq(struct vtscsi_softc *, int);
static int	vtscsi_thaw_simq(struct vtscsi_softc *, int);

static void	vtscsi_announce(struct vtscsi_softc *, uint32_t, target_id_t,
		    lun_id_t);
static void	vtscsi_execute_rescan(struct vtscsi_softc *, target_id_t,
		    lun_id_t);
static void	vtscsi_execute_rescan_bus(struct vtscsi_softc *);

static void	vtscsi_handle_event(struct vtscsi_softc *,
		    struct virtio_scsi_event *);
static int	vtscsi_enqueue_event_buf(struct vtscsi_softc *,
		    struct virtio_scsi_event *);
static int	vtscsi_init_event_vq(struct vtscsi_softc *);
static void	vtscsi_reinit_event_vq(struct vtscsi_softc *);
static void	vtscsi_drain_event_vq(struct vtscsi_softc *);

static void	vtscsi_complete_vqs_locked(struct vtscsi_softc *);
static void	vtscsi_complete_vqs(struct vtscsi_softc *);
static void	vtscsi_drain_vqs(struct vtscsi_softc *);
static void	vtscsi_cancel_request(struct vtscsi_softc *,
		    struct vtscsi_request *);
static void	vtscsi_drain_vq(struct vtscsi_softc *, struct virtqueue *);
static void	vtscsi_stop(struct vtscsi_softc *);
static int	vtscsi_reset_bus(struct vtscsi_softc *);

static void	vtscsi_init_request(struct vtscsi_softc *,
		    struct vtscsi_request *);
static int	vtscsi_alloc_requests(struct vtscsi_softc *);
static void	vtscsi_free_requests(struct vtscsi_softc *);
static void	vtscsi_enqueue_request(struct vtscsi_softc *,
		    struct vtscsi_request *);
static struct vtscsi_request * vtscsi_dequeue_request(struct vtscsi_softc *);

static void	vtscsi_complete_request(struct vtscsi_request *);
static void	vtscsi_complete_vq(struct vtscsi_softc *, struct virtqueue *);

static void	vtscsi_control_vq_intr(void *);
static void	vtscsi_event_vq_intr(void *);
static void	vtscsi_request_vq_intr(void *);
static void	vtscsi_disable_vqs_intr(struct vtscsi_softc *);
static void	vtscsi_enable_vqs_intr(struct vtscsi_softc *);

static void	vtscsi_get_tunables(struct vtscsi_softc *);
static void	vtscsi_add_sysctl(struct vtscsi_softc *);

static void	vtscsi_printf_req(struct vtscsi_request *, const char *,
		    const char *, ...);

/* Global tunables. */
/*
 * The current QEMU VirtIO SCSI implementation does not cancel in-flight
 * IO during virtio_stop(). So in-flight requests still complete after the
 * device reset. We would have to wait for all the in-flight IO to complete,
 * which defeats the typical purpose of a bus reset. We could simulate the
 * bus reset with either I_T_NEXUS_RESET of all the targets, or with
 * LOGICAL_UNIT_RESET of all the LUNs (assuming there is space in the
 * control virtqueue). But this isn't very useful if things really go off
 * the rails, so default to disabled for now.
 */
static int vtscsi_bus_reset_disable = 1;
TUNABLE_INT("hw.vtscsi.bus_reset_disable", &vtscsi_bus_reset_disable);

static struct virtio_feature_desc vtscsi_feature_desc[] = {
	{ VIRTIO_SCSI_F_INOUT,		"InOut"		},
	{ VIRTIO_SCSI_F_HOTPLUG,	"Hotplug"	},

	{ 0, NULL }
};

static device_method_t vtscsi_methods[] = {
	/* Device methods. */
	DEVMETHOD(device_probe,		vtscsi_probe),
	DEVMETHOD(device_attach,	vtscsi_attach),
	DEVMETHOD(device_detach,	vtscsi_detach),
	DEVMETHOD(device_suspend,	vtscsi_suspend),
	DEVMETHOD(device_resume,	vtscsi_resume),

	DEVMETHOD_END
};

static driver_t vtscsi_driver = {
	"vtscsi",
	vtscsi_methods,
	sizeof(struct vtscsi_softc)
};
static devclass_t vtscsi_devclass;

DRIVER_MODULE(virtio_scsi, virtio_pci, vtscsi_driver, vtscsi_devclass,
    vtscsi_modevent, 0);
MODULE_VERSION(virtio_scsi, 1);
MODULE_DEPEND(virtio_scsi, virtio, 1, 1, 1);
MODULE_DEPEND(virtio_scsi, cam, 1, 1, 1);

static int
vtscsi_modevent(module_t mod, int type, void *unused)
{
	int error;

	switch (type) {
	case MOD_LOAD:
	case MOD_QUIESCE:
	case MOD_UNLOAD:
	case MOD_SHUTDOWN:
		error = 0;
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}

	return (error);
}

static int
vtscsi_probe(device_t dev)
{

	if (virtio_get_device_type(dev) != VIRTIO_ID_SCSI)
		return (ENXIO);

	device_set_desc(dev, "VirtIO SCSI Adapter");

	return (BUS_PROBE_DEFAULT);
}

static int
vtscsi_attach(device_t dev)
{
	struct vtscsi_softc *sc;
	struct virtio_scsi_config scsicfg;
	int error;

	sc = device_get_softc(dev);
	sc->vtscsi_dev = dev;

	VTSCSI_LOCK_INIT(sc, device_get_nameunit(dev));
	TAILQ_INIT(&sc->vtscsi_req_free);

	vtscsi_get_tunables(sc);
	vtscsi_add_sysctl(sc);

	virtio_set_feature_desc(dev, vtscsi_feature_desc);
	vtscsi_negotiate_features(sc);

	if (virtio_with_feature(dev, VIRTIO_RING_F_INDIRECT_DESC))
		sc->vtscsi_flags |= VTSCSI_FLAG_INDIRECT;
	if (virtio_with_feature(dev, VIRTIO_SCSI_F_INOUT))
		sc->vtscsi_flags |= VTSCSI_FLAG_BIDIRECTIONAL;
	if (virtio_with_feature(dev, VIRTIO_SCSI_F_HOTPLUG))
		sc->vtscsi_flags |= VTSCSI_FLAG_HOTPLUG;

	vtscsi_read_config(sc, &scsicfg);

	sc->vtscsi_max_channel = scsicfg.max_channel;
	sc->vtscsi_max_target = scsicfg.max_target;
	sc->vtscsi_max_lun = scsicfg.max_lun;
	sc->vtscsi_event_buf_size = scsicfg.event_info_size;

	vtscsi_write_device_config(sc);

	sc->vtscsi_max_nsegs = vtscsi_maximum_segments(sc, scsicfg.seg_max);
	sc->vtscsi_sglist = sglist_alloc(sc->vtscsi_max_nsegs, M_NOWAIT);
	if (sc->vtscsi_sglist == NULL) {
		error = ENOMEM;
		device_printf(dev, "cannot allocate sglist\n");
		goto fail;
	}

	error = vtscsi_alloc_virtqueues(sc);
	if (error) {
		device_printf(dev, "cannot allocate virtqueues\n");
		goto fail;
	}

	error = vtscsi_init_event_vq(sc);
	if (error) {
		device_printf(dev, "cannot populate the eventvq\n");
		goto fail;
	}

	error = vtscsi_alloc_requests(sc);
	if (error) {
		device_printf(dev, "cannot allocate requests\n");
		goto fail;
	}

	error = vtscsi_alloc_cam(sc);
	if (error) {
		device_printf(dev, "cannot allocate CAM structures\n");
		goto fail;
	}

	error = virtio_setup_intr(dev, INTR_TYPE_CAM);
	if (error) {
		device_printf(dev, "cannot setup virtqueue interrupts\n");
		goto fail;
	}

	vtscsi_enable_vqs_intr(sc);

	/*
	 * Register with CAM after interrupts are enabled so we will get
	 * notified of the probe responses.
	 */
	error = vtscsi_register_cam(sc);
	if (error) {
		device_printf(dev, "cannot register with CAM\n");
		goto fail;
	}

fail:
	if (error)
		vtscsi_detach(dev);

	return (error);
}

static int
vtscsi_detach(device_t dev)
{
	struct vtscsi_softc *sc;

	sc = device_get_softc(dev);

	VTSCSI_LOCK(sc);
	sc->vtscsi_flags |= VTSCSI_FLAG_DETACH;
	if (device_is_attached(dev))
		vtscsi_stop(sc);
	VTSCSI_UNLOCK(sc);

	vtscsi_complete_vqs(sc);
	vtscsi_drain_vqs(sc);

	vtscsi_free_cam(sc);
	vtscsi_free_requests(sc);

	if (sc->vtscsi_sglist != NULL) {
		sglist_free(sc->vtscsi_sglist);
		sc->vtscsi_sglist = NULL;
	}

	VTSCSI_LOCK_DESTROY(sc);

	return (0);
}

static int
vtscsi_suspend(device_t dev)
{

	return (0);
}

static int
vtscsi_resume(device_t dev)
{

	return (0);
}

static void
vtscsi_negotiate_features(struct vtscsi_softc *sc)
{
	device_t dev;
	uint64_t features;

	dev = sc->vtscsi_dev;
	features = virtio_negotiate_features(dev, VTSCSI_FEATURES);
	sc->vtscsi_features = features;
}

#define VTSCSI_GET_CONFIG(_dev, _field, _cfg)			\
	virtio_read_device_config(_dev,				\
	    offsetof(struct virtio_scsi_config, _field),	\
	    &(_cfg)->_field, sizeof((_cfg)->_field))		\

static void
vtscsi_read_config(struct vtscsi_softc *sc,
    struct virtio_scsi_config *scsicfg)
{
	device_t dev;

	dev = sc->vtscsi_dev;

	bzero(scsicfg, sizeof(struct virtio_scsi_config));

	VTSCSI_GET_CONFIG(dev, num_queues, scsicfg);
	VTSCSI_GET_CONFIG(dev, seg_max, scsicfg);
	VTSCSI_GET_CONFIG(dev, max_sectors, scsicfg);
	VTSCSI_GET_CONFIG(dev, cmd_per_lun, scsicfg);
	VTSCSI_GET_CONFIG(dev, event_info_size, scsicfg);
	VTSCSI_GET_CONFIG(dev, sense_size, scsicfg);
	VTSCSI_GET_CONFIG(dev, cdb_size, scsicfg);
	VTSCSI_GET_CONFIG(dev, max_channel, scsicfg);
	VTSCSI_GET_CONFIG(dev, max_target, scsicfg);
	VTSCSI_GET_CONFIG(dev, max_lun, scsicfg);
}

#undef VTSCSI_GET_CONFIG

static int
vtscsi_maximum_segments(struct vtscsi_softc *sc, int seg_max)
{
	int nsegs;

	nsegs = VTSCSI_MIN_SEGMENTS;

	if (seg_max > 0) {
		nsegs += MIN(seg_max, MAXPHYS / PAGE_SIZE + 1);
		if (sc->vtscsi_flags & VTSCSI_FLAG_INDIRECT)
			nsegs = MIN(nsegs, VIRTIO_MAX_INDIRECT);
	} else
		nsegs += 1;

	return (nsegs);
}

static int
vtscsi_alloc_virtqueues(struct vtscsi_softc *sc)
{
	device_t dev;
	struct vq_alloc_info vq_info[3];
	int nvqs;

	dev = sc->vtscsi_dev;
	nvqs = 3;

	VQ_ALLOC_INFO_INIT(&vq_info[0], 0, vtscsi_control_vq_intr, sc,
	    &sc->vtscsi_control_vq, "%s control", device_get_nameunit(dev));

	VQ_ALLOC_INFO_INIT(&vq_info[1], 0, vtscsi_event_vq_intr, sc,
	    &sc->vtscsi_event_vq, "%s event", device_get_nameunit(dev));

	VQ_ALLOC_INFO_INIT(&vq_info[2], sc->vtscsi_max_nsegs,
	    vtscsi_request_vq_intr, sc, &sc->vtscsi_request_vq,
	    "%s request", device_get_nameunit(dev));

	return (virtio_alloc_virtqueues(dev, 0, nvqs, vq_info));
}

static void
vtscsi_write_device_config(struct vtscsi_softc *sc)
{

	virtio_write_dev_config_4(sc->vtscsi_dev,
	    offsetof(struct virtio_scsi_config, sense_size),
	    VIRTIO_SCSI_SENSE_SIZE);

	/*
	 * This is the size in the virtio_scsi_cmd_req structure. Note
	 * this value (32) is larger than the maximum CAM CDB size (16).
	 */
	virtio_write_dev_config_4(sc->vtscsi_dev,
	    offsetof(struct virtio_scsi_config, cdb_size),
	    VIRTIO_SCSI_CDB_SIZE);
}

static int
vtscsi_reinit(struct vtscsi_softc *sc)
{
	device_t dev;
	int error;

	dev = sc->vtscsi_dev;

	error = virtio_reinit(dev, sc->vtscsi_features);
	if (error == 0) {
		vtscsi_write_device_config(sc);
		vtscsi_reinit_event_vq(sc);
		virtio_reinit_complete(dev);

		vtscsi_enable_vqs_intr(sc);
	}

	vtscsi_dprintf(sc, VTSCSI_TRACE, "error=%d\n", error);

	return (error);
}

static int
vtscsi_alloc_cam(struct vtscsi_softc *sc)
{
	device_t dev;
	struct cam_devq *devq;
	int openings;

	dev = sc->vtscsi_dev;
	openings = sc->vtscsi_nrequests - VTSCSI_RESERVED_REQUESTS;

	devq = cam_simq_alloc(openings);
	if (devq == NULL) {
		device_printf(dev, "cannot allocate SIM queue\n");
		return (ENOMEM);
	}

	sc->vtscsi_sim = cam_sim_alloc(vtscsi_cam_action, vtscsi_cam_poll,
	    "vtscsi", sc, device_get_unit(dev), VTSCSI_MTX(sc), 1,
	    openings, devq);
	if (sc->vtscsi_sim == NULL) {
		cam_simq_free(devq);
		device_printf(dev, "cannot allocate SIM\n");
		return (ENOMEM);
	}

	return (0);
}

static int
vtscsi_register_cam(struct vtscsi_softc *sc)
{
	device_t dev;
	int registered, error;

	dev = sc->vtscsi_dev;
	registered = 0;

	VTSCSI_LOCK(sc);

	if (xpt_bus_register(sc->vtscsi_sim, dev, 0) != CAM_SUCCESS) {
		error = ENOMEM;
		device_printf(dev, "cannot register XPT bus\n");
		goto fail;
	}

	registered = 1;

	if (xpt_create_path(&sc->vtscsi_path, NULL,
	    cam_sim_path(sc->vtscsi_sim), CAM_TARGET_WILDCARD,
	    CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
		error = ENOMEM;
		device_printf(dev, "cannot create bus path\n");
		goto fail;
	}

	if (vtscsi_register_async(sc) != CAM_REQ_CMP) {
		error = EIO;
		device_printf(dev, "cannot register async callback\n");
		goto fail;
	}

	VTSCSI_UNLOCK(sc);

	return (0);

fail:
	if (sc->vtscsi_path != NULL) {
		xpt_free_path(sc->vtscsi_path);
		sc->vtscsi_path = NULL;
	}

	if (registered != 0)
		xpt_bus_deregister(cam_sim_path(sc->vtscsi_sim));

	VTSCSI_UNLOCK(sc);

	return (error);
}

static void
vtscsi_free_cam(struct vtscsi_softc *sc)
{

	VTSCSI_LOCK(sc);

	if (sc->vtscsi_path != NULL) {
		vtscsi_deregister_async(sc);

		xpt_free_path(sc->vtscsi_path);
		sc->vtscsi_path = NULL;

		xpt_bus_deregister(cam_sim_path(sc->vtscsi_sim));
	}

	if (sc->vtscsi_sim != NULL) {
		cam_sim_free(sc->vtscsi_sim, 1);
		sc->vtscsi_sim = NULL;
	}

	VTSCSI_UNLOCK(sc);
}

static void
vtscsi_cam_async(void *cb_arg, uint32_t code, struct cam_path *path, void *arg)
{
	struct cam_sim *sim;
	struct vtscsi_softc *sc;

	sim = cb_arg;
	sc = cam_sim_softc(sim);

	vtscsi_dprintf(sc, VTSCSI_TRACE, "code=%u\n", code);

	/*
	 * TODO Once QEMU supports event reporting, we should
	 *      (un)subscribe to events here.
	 */
	switch (code) {
	case AC_FOUND_DEVICE:
		break;
	case AC_LOST_DEVICE:
		break;
	}
}

static int
vtscsi_register_async(struct vtscsi_softc *sc)
{
	struct ccb_setasync csa;

	xpt_setup_ccb(&csa.ccb_h, sc->vtscsi_path, 5);
	csa.ccb_h.func_code = XPT_SASYNC_CB;
	csa.event_enable = AC_LOST_DEVICE | AC_FOUND_DEVICE;
	csa.callback = vtscsi_cam_async;
	csa.callback_arg = sc->vtscsi_sim;

	xpt_action((union ccb *) &csa);

	return (csa.ccb_h.status);
}

static void
vtscsi_deregister_async(struct vtscsi_softc *sc)
{
	struct ccb_setasync csa;

	xpt_setup_ccb(&csa.ccb_h, sc->vtscsi_path, 5);
	csa.ccb_h.func_code = XPT_SASYNC_CB;
	csa.event_enable = 0;
	csa.callback = vtscsi_cam_async;
	csa.callback_arg = sc->vtscsi_sim;

	xpt_action((union ccb *) &csa);
}

static void
vtscsi_cam_action(struct cam_sim *sim, union ccb *ccb)
{
	struct vtscsi_softc *sc;
	struct ccb_hdr *ccbh;

	sc = cam_sim_softc(sim);
	ccbh = &ccb->ccb_h;

	VTSCSI_LOCK_OWNED(sc);

	if (sc->vtscsi_flags & VTSCSI_FLAG_DETACH) {
		/*
		 * The VTSCSI_MTX is briefly dropped between setting
		 * VTSCSI_FLAG_DETACH and deregistering with CAM, so
		 * drop any CCBs that come in during that window.
		 */
		ccbh->status = CAM_NO_HBA;
		xpt_done(ccb);
		return;
	}

	switch (ccbh->func_code) {
	case XPT_SCSI_IO:
		vtscsi_cam_scsi_io(sc, sim, ccb);
		break;

	case XPT_SET_TRAN_SETTINGS:
		ccbh->status = CAM_FUNC_NOTAVAIL;
		xpt_done(ccb);
		break;

	case XPT_GET_TRAN_SETTINGS:
		vtscsi_cam_get_tran_settings(sc, ccb);
		break;

	case XPT_RESET_BUS:
		vtscsi_cam_reset_bus(sc, ccb);
		break;

	case XPT_RESET_DEV:
		vtscsi_cam_reset_dev(sc, ccb);
		break;

	case XPT_ABORT:
		vtscsi_cam_abort(sc, ccb);
		break;

	case XPT_CALC_GEOMETRY:
		cam_calc_geometry(&ccb->ccg, 1);
		xpt_done(ccb);
		break;

	case XPT_PATH_INQ:
		vtscsi_cam_path_inquiry(sc, sim, ccb);
		break;

	default:
		vtscsi_dprintf(sc, VTSCSI_ERROR,
		    "invalid ccb=%p func=%#x\n", ccb, ccbh->func_code);

		ccbh->status = CAM_REQ_INVALID;
		xpt_done(ccb);
		break;
	}
}

static void
vtscsi_cam_poll(struct cam_sim *sim)
{
	struct vtscsi_softc *sc;

	sc = cam_sim_softc(sim);

	vtscsi_complete_vqs_locked(sc);
}

static void
vtscsi_cam_scsi_io(struct vtscsi_softc *sc, struct cam_sim *sim,
    union ccb *ccb)
{
	struct ccb_hdr *ccbh;
	struct ccb_scsiio *csio;
	int error;

	ccbh = &ccb->ccb_h;
	csio = &ccb->csio;

	if (csio->cdb_len > VIRTIO_SCSI_CDB_SIZE) {
		error = EINVAL;
		ccbh->status = CAM_REQ_INVALID;
		goto done;
	}

	if ((ccbh->flags & CAM_DIR_MASK) == CAM_DIR_BOTH &&
	    (sc->vtscsi_flags & VTSCSI_FLAG_BIDIRECTIONAL) == 0) {
		error = EINVAL;
		ccbh->status = CAM_REQ_INVALID;
		goto done;
	}

	error = vtscsi_start_scsi_cmd(sc, ccb);

done:
	if (error) {
		vtscsi_dprintf(sc, VTSCSI_ERROR,
		    "error=%d ccb=%p status=%#x\n", error, ccb, ccbh->status);
		xpt_done(ccb);
	}
}

static void
vtscsi_cam_get_tran_settings(struct vtscsi_softc *sc, union ccb *ccb)
{
	struct ccb_trans_settings *cts;
	struct ccb_trans_settings_scsi *scsi;

	cts = &ccb->cts;
	scsi = &cts->proto_specific.scsi;

	cts->protocol = PROTO_SCSI;
	cts->protocol_version = SCSI_REV_SPC3;
	cts->transport = XPORT_SAS;
	cts->transport_version = 0;

	scsi->valid = CTS_SCSI_VALID_TQ;
	scsi->flags = CTS_SCSI_FLAGS_TAG_ENB;

	ccb->ccb_h.status = CAM_REQ_CMP;
	xpt_done(ccb);
}

static void
vtscsi_cam_reset_bus(struct vtscsi_softc *sc, union ccb *ccb)
{
	int error;

	error = vtscsi_reset_bus(sc);
	if (error == 0)
		ccb->ccb_h.status = CAM_REQ_CMP;
	else
		ccb->ccb_h.status = CAM_REQ_CMP_ERR;

	vtscsi_dprintf(sc, VTSCSI_TRACE, "error=%d ccb=%p status=%#x\n",
	    error, ccb, ccb->ccb_h.status);

	xpt_done(ccb);
}

static void
vtscsi_cam_reset_dev(struct vtscsi_softc *sc, union ccb *ccb)
{
	struct ccb_hdr *ccbh;
	struct vtscsi_request *req;
	int error;

	ccbh = &ccb->ccb_h;

	req = vtscsi_dequeue_request(sc);
	if (req == NULL) {
		error = EAGAIN;
		vtscsi_freeze_simq(sc, VTSCSI_REQUEST);
		goto fail;
	}

	req->vsr_ccb = ccb;

	error = vtscsi_execute_reset_dev_cmd(sc, req);
	if (error == 0)
		return;

	vtscsi_enqueue_request(sc, req);

fail:
	vtscsi_dprintf(sc, VTSCSI_ERROR, "error=%d req=%p ccb=%p\n",
	    error, req, ccb);

	if (error == EAGAIN)
		ccbh->status = CAM_RESRC_UNAVAIL;
	else
		ccbh->status = CAM_REQ_CMP_ERR;

	xpt_done(ccb);
}

static void
vtscsi_cam_abort(struct vtscsi_softc *sc, union ccb *ccb)
{
	struct vtscsi_request *req;
	struct ccb_hdr *ccbh;
	int error;

	ccbh = &ccb->ccb_h;

	req = vtscsi_dequeue_request(sc);
	if (req == NULL) {
		error = EAGAIN;
		vtscsi_freeze_simq(sc, VTSCSI_REQUEST);
		goto fail;
	}

	req->vsr_ccb = ccb;

	error = vtscsi_execute_abort_task_cmd(sc, req);
	if (error == 0)
		return;

	vtscsi_enqueue_request(sc, req);

fail:
	vtscsi_dprintf(sc, VTSCSI_ERROR, "error=%d req=%p ccb=%p\n",
	    error, req, ccb);

	if (error == EAGAIN)
		ccbh->status = CAM_RESRC_UNAVAIL;
	else
		ccbh->status = CAM_REQ_CMP_ERR;

	xpt_done(ccb);
}

static void
vtscsi_cam_path_inquiry(struct vtscsi_softc *sc, struct cam_sim *sim,
    union ccb *ccb)
{
	device_t dev;
	struct ccb_pathinq *cpi;

	dev = sc->vtscsi_dev;
	cpi = &ccb->cpi;

	vtscsi_dprintf(sc, VTSCSI_TRACE, "sim=%p ccb=%p\n", sim, ccb);

	cpi->version_num = 1;
	cpi->hba_inquiry = PI_TAG_ABLE;
	cpi->target_sprt = 0;
	cpi->hba_misc = PIM_SEQSCAN | PIM_UNMAPPED;
	if (vtscsi_bus_reset_disable != 0)
		cpi->hba_misc |= PIM_NOBUSRESET;
	cpi->hba_eng_cnt = 0;

	cpi->max_target = sc->vtscsi_max_target;
	cpi->max_lun = sc->vtscsi_max_lun;
	cpi->initiator_id = VTSCSI_INITIATOR_ID;

	strlcpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
	strlcpy(cpi->hba_vid, "VirtIO", HBA_IDLEN);
	strlcpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);

	cpi->unit_number = cam_sim_unit(sim);
	cpi->bus_id = cam_sim_bus(sim);

	cpi->base_transfer_speed = 300000;

	cpi->protocol = PROTO_SCSI;
	cpi->protocol_version = SCSI_REV_SPC3;
	cpi->transport = XPORT_SAS;
	cpi->transport_version = 0;

	cpi->maxio = (sc->vtscsi_max_nsegs - VTSCSI_MIN_SEGMENTS - 1) *
	    PAGE_SIZE;

	cpi->hba_vendor = virtio_get_vendor(dev);
	cpi->hba_device = virtio_get_device(dev);
	cpi->hba_subvendor = virtio_get_subvendor(dev);
	cpi->hba_subdevice = virtio_get_subdevice(dev);

	ccb->ccb_h.status = CAM_REQ_CMP;
	xpt_done(ccb);
}

static int
vtscsi_sg_append_scsi_buf(struct vtscsi_softc *sc, struct sglist *sg,
    struct ccb_scsiio *csio)
{
	struct ccb_hdr *ccbh;
	struct bus_dma_segment *dseg;
	int i, error;

	ccbh = &csio->ccb_h;
	error = 0;

	switch ((ccbh->flags & CAM_DATA_MASK)) {
	case CAM_DATA_VADDR:
		error = sglist_append(sg, csio->data_ptr, csio->dxfer_len);
		break;
	case CAM_DATA_PADDR:
		error = sglist_append_phys(sg,
		    (vm_paddr_t)(vm_offset_t) csio->data_ptr, csio->dxfer_len);
		break;
	case CAM_DATA_SG:
		for (i = 0; i < csio->sglist_cnt && error == 0; i++) {
			dseg = &((struct bus_dma_segment *)csio->data_ptr)[i];
			error = sglist_append(sg,
			    (void *)(vm_offset_t) dseg->ds_addr, dseg->ds_len);
		}
		break;
	case CAM_DATA_SG_PADDR:
		for (i = 0; i < csio->sglist_cnt && error == 0; i++) {
			dseg = &((struct bus_dma_segment *)csio->data_ptr)[i];
			error = sglist_append_phys(sg,
			    (vm_paddr_t) dseg->ds_addr, dseg->ds_len);
		}
		break;
	case CAM_DATA_BIO:
		error = sglist_append_bio(sg, (struct bio *) csio->data_ptr);
		break;
	default:
		error = EINVAL;
		break;
	}

	return (error);
}

static int
vtscsi_fill_scsi_cmd_sglist(struct vtscsi_softc *sc, struct vtscsi_request *req,
    int *readable, int *writable)
{
	struct sglist *sg;
	struct ccb_hdr *ccbh;
	struct ccb_scsiio *csio;
	struct virtio_scsi_cmd_req *cmd_req;
	struct virtio_scsi_cmd_resp *cmd_resp;
	int error;

	sg = sc->vtscsi_sglist;
	csio = &req->vsr_ccb->csio;
	ccbh = &csio->ccb_h;
	cmd_req = &req->vsr_cmd_req;
	cmd_resp = &req->vsr_cmd_resp;

	sglist_reset(sg);

	sglist_append(sg, cmd_req, sizeof(struct virtio_scsi_cmd_req));
	if ((ccbh->flags & CAM_DIR_MASK) == CAM_DIR_OUT) {
		error = vtscsi_sg_append_scsi_buf(sc, sg, csio);
		/* At least one segment must be left for the response. */
		if (error || sg->sg_nseg == sg->sg_maxseg)
			goto fail;
	}

	*readable = sg->sg_nseg;

	sglist_append(sg, cmd_resp, sizeof(struct virtio_scsi_cmd_resp));
	if ((ccbh->flags & CAM_DIR_MASK) == CAM_DIR_IN) {
		error = vtscsi_sg_append_scsi_buf(sc, sg, csio);
		if (error)
			goto fail;
	}

	*writable = sg->sg_nseg - *readable;

	vtscsi_dprintf(sc, VTSCSI_TRACE, "req=%p ccb=%p readable=%d "
	    "writable=%d\n", req, ccbh, *readable, *writable);

	return (0);

fail:
	/*
	 * This should never happen unless maxio was incorrectly set.
	 */
	vtscsi_set_ccb_status(ccbh, CAM_REQ_TOO_BIG, 0);

	vtscsi_dprintf(sc, VTSCSI_ERROR, "error=%d req=%p ccb=%p "
	    "nseg=%d maxseg=%d\n",
	    error, req, ccbh, sg->sg_nseg, sg->sg_maxseg);

	return (EFBIG);
}

static int
vtscsi_execute_scsi_cmd(struct vtscsi_softc *sc, struct vtscsi_request *req)
{
	struct sglist *sg;
	struct virtqueue *vq;
	struct ccb_scsiio *csio;
	struct ccb_hdr *ccbh;
	struct virtio_scsi_cmd_req *cmd_req;
	struct virtio_scsi_cmd_resp *cmd_resp;
	int readable, writable, error;

	sg = sc->vtscsi_sglist;
	vq = sc->vtscsi_request_vq;
	csio = &req->vsr_ccb->csio;
	ccbh = &csio->ccb_h;
	cmd_req = &req->vsr_cmd_req;
	cmd_resp = &req->vsr_cmd_resp;

	vtscsi_init_scsi_cmd_req(csio, cmd_req);

	error = vtscsi_fill_scsi_cmd_sglist(sc, req, &readable, &writable);
	if (error)
		return (error);

	req->vsr_complete = vtscsi_complete_scsi_cmd;
	cmd_resp->response = -1;

	error = virtqueue_enqueue(vq, req, sg, readable, writable);
	if (error) {
		vtscsi_dprintf(sc, VTSCSI_ERROR,
		    "enqueue error=%d req=%p ccb=%p\n", error, req, ccbh);

		ccbh->status = CAM_REQUEUE_REQ;
		vtscsi_freeze_simq(sc, VTSCSI_REQUEST_VQ);
		return (error);
	}

	ccbh->status |= CAM_SIM_QUEUED;
	ccbh->ccbh_vtscsi_req = req;

	virtqueue_notify(vq);

	if (ccbh->timeout != CAM_TIME_INFINITY) {
		req->vsr_flags |= VTSCSI_REQ_FLAG_TIMEOUT_SET;
		callout_reset_sbt(&req->vsr_callout, SBT_1MS * ccbh->timeout,
		    0, vtscsi_timedout_scsi_cmd, req, 0);
	}

	vtscsi_dprintf_req(req, VTSCSI_TRACE, "enqueued req=%p ccb=%p\n",
	    req, ccbh);

	return (0);
}

static int
vtscsi_start_scsi_cmd(struct vtscsi_softc *sc, union ccb *ccb)
{
	struct vtscsi_request *req;
	int error;

	req = vtscsi_dequeue_request(sc);
	if (req == NULL) {
		ccb->ccb_h.status = CAM_REQUEUE_REQ;
		vtscsi_freeze_simq(sc, VTSCSI_REQUEST);
		return (ENOBUFS);
	}

	req->vsr_ccb = ccb;

	error = vtscsi_execute_scsi_cmd(sc, req);
	if (error)
		vtscsi_enqueue_request(sc, req);

	return (error);
}

static void
vtscsi_complete_abort_timedout_scsi_cmd(struct vtscsi_softc *sc,
    struct vtscsi_request *req)
{
	struct virtio_scsi_ctrl_tmf_resp *tmf_resp;
	struct vtscsi_request *to_req;
	uint8_t response;

	tmf_resp = &req->vsr_tmf_resp;
	response = tmf_resp->response;
	to_req = req->vsr_timedout_req;

	vtscsi_dprintf(sc, VTSCSI_TRACE, "req=%p to_req=%p response=%d\n",
	    req, to_req, response);

	vtscsi_enqueue_request(sc, req);

	/*
	 * The timedout request could have completed between when the
	 * abort task was sent and when the host processed it.
	 */
	if (to_req->vsr_state != VTSCSI_REQ_STATE_TIMEDOUT)
		return;

	/* The timedout request was successfully aborted. */
	if (response == VIRTIO_SCSI_S_FUNCTION_COMPLETE)
		return;

	/* Don't bother if the device is going away. */
	if (sc->vtscsi_flags & VTSCSI_FLAG_DETACH)
		return;

	/* The timedout request will be aborted by the reset. */
	if (sc->vtscsi_flags & VTSCSI_FLAG_RESET)
		return;

	vtscsi_reset_bus(sc);
}

static int
vtscsi_abort_timedout_scsi_cmd(struct vtscsi_softc *sc,
    struct vtscsi_request *to_req)
{
	struct sglist *sg;
	struct ccb_hdr *to_ccbh;
	struct vtscsi_request *req;
	struct virtio_scsi_ctrl_tmf_req *tmf_req;
	struct virtio_scsi_ctrl_tmf_resp *tmf_resp;
	int error;

	sg = sc->vtscsi_sglist;
	to_ccbh = &to_req->vsr_ccb->ccb_h;

	req = vtscsi_dequeue_request(sc);
	if (req == NULL) {
		error = ENOBUFS;
		goto fail;
	}

	tmf_req = &req->vsr_tmf_req;
	tmf_resp = &req->vsr_tmf_resp;

	vtscsi_init_ctrl_tmf_req(to_ccbh, VIRTIO_SCSI_T_TMF_ABORT_TASK,
	    (uintptr_t) to_ccbh, tmf_req);

	sglist_reset(sg);
	sglist_append(sg, tmf_req, sizeof(struct virtio_scsi_ctrl_tmf_req));
	sglist_append(sg, tmf_resp, sizeof(struct virtio_scsi_ctrl_tmf_resp));

	req->vsr_timedout_req = to_req;
	req->vsr_complete = vtscsi_complete_abort_timedout_scsi_cmd;
	tmf_resp->response = -1;

	error = vtscsi_execute_ctrl_req(sc, req, sg, 1, 1,
	    VTSCSI_EXECUTE_ASYNC);
	if (error == 0)
		return (0);

	vtscsi_enqueue_request(sc, req);

fail:
	vtscsi_dprintf(sc, VTSCSI_ERROR, "error=%d req=%p "
	    "timedout req=%p ccb=%p\n", error, req, to_req, to_ccbh);

	return (error);
}

static void
vtscsi_timedout_scsi_cmd(void *xreq)
{
	struct vtscsi_softc *sc;
	struct vtscsi_request *to_req;

	to_req = xreq;
	sc = to_req->vsr_softc;

	vtscsi_dprintf(sc, VTSCSI_INFO, "timedout req=%p ccb=%p state=%#x\n",
	    to_req, to_req->vsr_ccb, to_req->vsr_state);

	/* Don't bother if the device is going away. */
	if (sc->vtscsi_flags & VTSCSI_FLAG_DETACH)
		return;

	/*
	 * Bail if the request is not in use. We likely raced when
	 * stopping the callout handler or it has already been aborted.
	 */
	if (to_req->vsr_state != VTSCSI_REQ_STATE_INUSE ||
	    (to_req->vsr_flags & VTSCSI_REQ_FLAG_TIMEOUT_SET) == 0)
		return;

	/*
	 * Complete the request queue in case the timedout request is
	 * actually just pending.
	 */
	vtscsi_complete_vq(sc, sc->vtscsi_request_vq);
	if (to_req->vsr_state == VTSCSI_REQ_STATE_FREE)
		return;

	sc->vtscsi_stats.scsi_cmd_timeouts++;
	to_req->vsr_state = VTSCSI_REQ_STATE_TIMEDOUT;

	if (vtscsi_abort_timedout_scsi_cmd(sc, to_req) == 0)
		return;

	vtscsi_dprintf(sc, VTSCSI_ERROR, "resetting bus\n");
	vtscsi_reset_bus(sc);
}

static cam_status
vtscsi_scsi_cmd_cam_status(struct virtio_scsi_cmd_resp *cmd_resp)
{
	cam_status status;

	switch (cmd_resp->response) {
	case VIRTIO_SCSI_S_OK:
		status = CAM_REQ_CMP;
		break;
	case VIRTIO_SCSI_S_OVERRUN:
		status = CAM_DATA_RUN_ERR;
		break;
	case VIRTIO_SCSI_S_ABORTED:
		status = CAM_REQ_ABORTED;
		break;
	case VIRTIO_SCSI_S_BAD_TARGET:
		status = CAM_SEL_TIMEOUT;
		break;
	case VIRTIO_SCSI_S_RESET:
		status = CAM_SCSI_BUS_RESET;
		break;
	case VIRTIO_SCSI_S_BUSY:
		status = CAM_SCSI_BUSY;
		break;
	case VIRTIO_SCSI_S_TRANSPORT_FAILURE:
	case VIRTIO_SCSI_S_TARGET_FAILURE:
	case VIRTIO_SCSI_S_NEXUS_FAILURE:
		status = CAM_SCSI_IT_NEXUS_LOST;
		break;
	default: /* VIRTIO_SCSI_S_FAILURE */
		status = CAM_REQ_CMP_ERR;
		break;
	}

	return (status);
}

static cam_status
vtscsi_complete_scsi_cmd_response(struct vtscsi_softc *sc,
    struct ccb_scsiio *csio, struct virtio_scsi_cmd_resp *cmd_resp)
{
	cam_status status;

	csio->scsi_status = cmd_resp->status;
	csio->resid = cmd_resp->resid;

	if (csio->scsi_status == SCSI_STATUS_OK)
		status = CAM_REQ_CMP;
	else
		status = CAM_SCSI_STATUS_ERROR;

	if (cmd_resp->sense_len > 0) {
		status |= CAM_AUTOSNS_VALID;

		if (cmd_resp->sense_len < csio->sense_len)
			csio->sense_resid = csio->sense_len -
			    cmd_resp->sense_len;
		else
			csio->sense_resid = 0;

		bzero(&csio->sense_data, sizeof(csio->sense_data));
		memcpy(cmd_resp->sense, &csio->sense_data,
		    csio->sense_len - csio->sense_resid);
	}

	vtscsi_dprintf(sc, status == CAM_REQ_CMP ? VTSCSI_TRACE : VTSCSI_ERROR,
	    "ccb=%p scsi_status=%#x resid=%u sense_resid=%u\n",
	    csio, csio->scsi_status, csio->resid, csio->sense_resid);

	return (status);
}

static void
vtscsi_complete_scsi_cmd(struct vtscsi_softc *sc, struct vtscsi_request *req)
{
	struct ccb_hdr *ccbh;
	struct ccb_scsiio *csio;
	struct virtio_scsi_cmd_resp *cmd_resp;
	cam_status status;

	csio = &req->vsr_ccb->csio;
	ccbh = &csio->ccb_h;
	cmd_resp = &req->vsr_cmd_resp;

	KASSERT(ccbh->ccbh_vtscsi_req == req,
	    ("ccb %p req mismatch %p/%p", ccbh, ccbh->ccbh_vtscsi_req, req));

	if (req->vsr_flags & VTSCSI_REQ_FLAG_TIMEOUT_SET)
		callout_stop(&req->vsr_callout);

	status = vtscsi_scsi_cmd_cam_status(cmd_resp);
	if (status == CAM_REQ_ABORTED) {
		if (req->vsr_state == VTSCSI_REQ_STATE_TIMEDOUT)
			status = CAM_CMD_TIMEOUT;
	} else if (status == CAM_REQ_CMP)
		status = vtscsi_complete_scsi_cmd_response(sc, csio, cmd_resp);

	if ((status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		status |= CAM_DEV_QFRZN;
		xpt_freeze_devq(ccbh->path, 1);
	}

	if (vtscsi_thaw_simq(sc, VTSCSI_REQUEST | VTSCSI_REQUEST_VQ) != 0)
		status |= CAM_RELEASE_SIMQ;

	vtscsi_dprintf(sc, VTSCSI_TRACE, "req=%p ccb=%p status=%#x\n",
	    req, ccbh, status);

	ccbh->status = status;
	xpt_done(req->vsr_ccb);
	vtscsi_enqueue_request(sc, req);
}

static void
vtscsi_poll_ctrl_req(struct vtscsi_softc *sc, struct vtscsi_request *req)
{

	/* XXX We probably shouldn't poll forever. */
	req->vsr_flags |= VTSCSI_REQ_FLAG_POLLED;
	do
		vtscsi_complete_vq(sc, sc->vtscsi_control_vq);
	while ((req->vsr_flags & VTSCSI_REQ_FLAG_COMPLETE) == 0);

	req->vsr_flags &= ~VTSCSI_REQ_FLAG_POLLED;
}

static int
vtscsi_execute_ctrl_req(struct vtscsi_softc *sc, struct vtscsi_request *req,
    struct sglist *sg, int readable, int writable, int flag)
{
	struct virtqueue *vq;
	int error;

	vq = sc->vtscsi_control_vq;

	MPASS(flag == VTSCSI_EXECUTE_POLL || req->vsr_complete != NULL);

	error = virtqueue_enqueue(vq, req, sg, readable, writable);
	if (error) {
		/*
		 * Return EAGAIN when the virtqueue does not have enough
		 * descriptors available.
		 */
		if (error == ENOSPC || error == EMSGSIZE)
			error = EAGAIN;

		return (error);
	}

	virtqueue_notify(vq);
	if (flag == VTSCSI_EXECUTE_POLL)
		vtscsi_poll_ctrl_req(sc, req);

	return (0);
}

static void
vtscsi_complete_abort_task_cmd(struct vtscsi_softc *sc,
    struct vtscsi_request *req)
{
	union ccb *ccb;
	struct ccb_hdr *ccbh;
	struct virtio_scsi_ctrl_tmf_resp *tmf_resp;

	ccb = req->vsr_ccb;
	ccbh = &ccb->ccb_h;
	tmf_resp = &req->vsr_tmf_resp;

	switch (tmf_resp->response) {
	case VIRTIO_SCSI_S_FUNCTION_COMPLETE:
		ccbh->status = CAM_REQ_CMP;
		break;
	case VIRTIO_SCSI_S_FUNCTION_REJECTED:
		ccbh->status = CAM_UA_ABORT;
		break;
	default:
		ccbh->status = CAM_REQ_CMP_ERR;
		break;
	}

	xpt_done(ccb);
	vtscsi_enqueue_request(sc, req);
}

static int
vtscsi_execute_abort_task_cmd(struct vtscsi_softc *sc,
    struct vtscsi_request *req)
{
	struct sglist *sg;
	struct ccb_abort *cab;
	struct ccb_hdr *ccbh;
	struct ccb_hdr *abort_ccbh;
	struct vtscsi_request *abort_req;
	struct virtio_scsi_ctrl_tmf_req *tmf_req;
	struct virtio_scsi_ctrl_tmf_resp *tmf_resp;
	int error;

	sg = sc->vtscsi_sglist;
	cab = &req->vsr_ccb->cab;
	ccbh = &cab->ccb_h;
	tmf_req = &req->vsr_tmf_req;
	tmf_resp = &req->vsr_tmf_resp;

	/* CCB header and request that's to be aborted. */
	abort_ccbh = &cab->abort_ccb->ccb_h;
	abort_req = abort_ccbh->ccbh_vtscsi_req;

	if (abort_ccbh->func_code != XPT_SCSI_IO || abort_req == NULL) {
		error = EINVAL;
		goto fail;
	}

	/* Only attempt to abort requests that could be in-flight. */
	if (abort_req->vsr_state != VTSCSI_REQ_STATE_INUSE) {
		error = EALREADY;
		goto fail;
	}

	abort_req->vsr_state = VTSCSI_REQ_STATE_ABORTED;
	if (abort_req->vsr_flags & VTSCSI_REQ_FLAG_TIMEOUT_SET)
		callout_stop(&abort_req->vsr_callout);

	vtscsi_init_ctrl_tmf_req(ccbh, VIRTIO_SCSI_T_TMF_ABORT_TASK,
	    (uintptr_t) abort_ccbh, tmf_req);

	sglist_reset(sg);
	sglist_append(sg, tmf_req, sizeof(struct virtio_scsi_ctrl_tmf_req));
	sglist_append(sg, tmf_resp, sizeof(struct virtio_scsi_ctrl_tmf_resp));

	req->vsr_complete = vtscsi_complete_abort_task_cmd;
	tmf_resp->response = -1;

	error = vtscsi_execute_ctrl_req(sc, req, sg, 1, 1,
	    VTSCSI_EXECUTE_ASYNC);

fail:
	vtscsi_dprintf(sc, VTSCSI_TRACE, "error=%d req=%p abort_ccb=%p "
	    "abort_req=%p\n", error, req, abort_ccbh, abort_req);

	return (error);
}

static void
vtscsi_complete_reset_dev_cmd(struct vtscsi_softc *sc,
    struct vtscsi_request *req)
{
	union ccb *ccb;
	struct ccb_hdr *ccbh;
	struct virtio_scsi_ctrl_tmf_resp *tmf_resp;

	ccb = req->vsr_ccb;
	ccbh = &ccb->ccb_h;
	tmf_resp = &req->vsr_tmf_resp;

	vtscsi_dprintf(sc, VTSCSI_TRACE, "req=%p ccb=%p response=%d\n",
	    req, ccb, tmf_resp->response);

	if (tmf_resp->response == VIRTIO_SCSI_S_FUNCTION_COMPLETE) {
		ccbh->status = CAM_REQ_CMP;
		vtscsi_announce(sc, AC_SENT_BDR, ccbh->target_id,
		    ccbh->target_lun);
	} else
		ccbh->status = CAM_REQ_CMP_ERR;

	xpt_done(ccb);
	vtscsi_enqueue_request(sc, req);
}

static int
vtscsi_execute_reset_dev_cmd(struct vtscsi_softc *sc,
    struct vtscsi_request *req)
{
	struct sglist *sg;
	struct ccb_resetdev *crd;
	struct ccb_hdr *ccbh;
	struct virtio_scsi_ctrl_tmf_req *tmf_req;
	struct virtio_scsi_ctrl_tmf_resp *tmf_resp;
	uint32_t subtype;
	int error;

	sg = sc->vtscsi_sglist;
	crd = &req->vsr_ccb->crd;
	ccbh = &crd->ccb_h;
	tmf_req = &req->vsr_tmf_req;
	tmf_resp = &req->vsr_tmf_resp;

	if (ccbh->target_lun == CAM_LUN_WILDCARD)
		subtype = VIRTIO_SCSI_T_TMF_I_T_NEXUS_RESET;
	else
		subtype = VIRTIO_SCSI_T_TMF_LOGICAL_UNIT_RESET;

	vtscsi_init_ctrl_tmf_req(ccbh, subtype, 0, tmf_req);

	sglist_reset(sg);
	sglist_append(sg, tmf_req, sizeof(struct virtio_scsi_ctrl_tmf_req));
	sglist_append(sg, tmf_resp, sizeof(struct virtio_scsi_ctrl_tmf_resp));

	req->vsr_complete = vtscsi_complete_reset_dev_cmd;
	tmf_resp->response = -1;

	error = vtscsi_execute_ctrl_req(sc, req, sg, 1, 1,
	    VTSCSI_EXECUTE_ASYNC);

	vtscsi_dprintf(sc, VTSCSI_TRACE, "error=%d req=%p ccb=%p\n",
	    error, req, ccbh);

	return (error);
}

static void
vtscsi_get_request_lun(uint8_t lun[], target_id_t *target_id, lun_id_t *lun_id)
{

	*target_id = lun[1];
	*lun_id = (lun[2] << 8) | lun[3];
}

static void
vtscsi_set_request_lun(struct ccb_hdr *ccbh, uint8_t lun[])
{

	lun[0] = 1;
	lun[1] = ccbh->target_id;
	lun[2] = 0x40 | ((ccbh->target_lun >> 8) & 0x3F);
	lun[3] = ccbh->target_lun & 0xFF;
}

static void
vtscsi_init_scsi_cmd_req(struct ccb_scsiio *csio,
    struct virtio_scsi_cmd_req *cmd_req)
{
	uint8_t attr;

	switch (csio->tag_action) {
	case MSG_HEAD_OF_Q_TAG:
		attr = VIRTIO_SCSI_S_HEAD;
		break;
	case MSG_ORDERED_Q_TAG:
		attr = VIRTIO_SCSI_S_ORDERED;
		break;
	case MSG_ACA_TASK:
		attr = VIRTIO_SCSI_S_ACA;
		break;
	default: /* MSG_SIMPLE_Q_TAG */
		attr = VIRTIO_SCSI_S_SIMPLE;
		break;
	}

	vtscsi_set_request_lun(&csio->ccb_h, cmd_req->lun);
	cmd_req->tag = (uintptr_t) csio;
	cmd_req->task_attr = attr;

	memcpy(cmd_req->cdb,
	    csio->ccb_h.flags & CAM_CDB_POINTER ?
	        csio->cdb_io.cdb_ptr : csio->cdb_io.cdb_bytes,
	    csio->cdb_len);
}

static void
vtscsi_init_ctrl_tmf_req(struct ccb_hdr *ccbh, uint32_t subtype,
    uintptr_t tag, struct virtio_scsi_ctrl_tmf_req *tmf_req)
{

	vtscsi_set_request_lun(ccbh, tmf_req->lun);

	tmf_req->type = VIRTIO_SCSI_T_TMF;
	tmf_req->subtype = subtype;
	tmf_req->tag = tag;
}

static void
vtscsi_freeze_simq(struct vtscsi_softc *sc, int reason)
{
	int frozen;

	frozen = sc->vtscsi_frozen;

	if (reason & VTSCSI_REQUEST &&
	    (sc->vtscsi_frozen & VTSCSI_FROZEN_NO_REQUESTS) == 0)
		sc->vtscsi_frozen |= VTSCSI_FROZEN_NO_REQUESTS;

	if (reason & VTSCSI_REQUEST_VQ &&
	    (sc->vtscsi_frozen & VTSCSI_FROZEN_REQUEST_VQ_FULL) == 0)
		sc->vtscsi_frozen |= VTSCSI_FROZEN_REQUEST_VQ_FULL;

	/* Freeze the SIMQ if transitioned to frozen. */
	if (frozen == 0 && sc->vtscsi_frozen != 0) {
		vtscsi_dprintf(sc, VTSCSI_INFO, "SIMQ frozen\n");
		xpt_freeze_simq(sc->vtscsi_sim, 1);
	}
}

static int
vtscsi_thaw_simq(struct vtscsi_softc *sc, int reason)
{
	int thawed;

	if (sc->vtscsi_frozen == 0 || reason == 0)
		return (0);

	if (reason & VTSCSI_REQUEST &&
	    sc->vtscsi_frozen & VTSCSI_FROZEN_NO_REQUESTS)
		sc->vtscsi_frozen &= ~VTSCSI_FROZEN_NO_REQUESTS;

	if (reason & VTSCSI_REQUEST_VQ &&
	    sc->vtscsi_frozen & VTSCSI_FROZEN_REQUEST_VQ_FULL)
		sc->vtscsi_frozen &= ~VTSCSI_FROZEN_REQUEST_VQ_FULL;

	thawed = sc->vtscsi_frozen == 0;
	if (thawed != 0)
		vtscsi_dprintf(sc, VTSCSI_INFO, "SIMQ thawed\n");

	return (thawed);
}

static void
vtscsi_announce(struct vtscsi_softc *sc, uint32_t ac_code,
    target_id_t target_id, lun_id_t lun_id)
{
	struct cam_path *path;

	/* Use the wildcard path from our softc for bus announcements. */
	if (target_id == CAM_TARGET_WILDCARD && lun_id == CAM_LUN_WILDCARD) {
		xpt_async(ac_code, sc->vtscsi_path, NULL);
		return;
	}

	if (xpt_create_path(&path, NULL, cam_sim_path(sc->vtscsi_sim),
	    target_id, lun_id) != CAM_REQ_CMP) {
		vtscsi_dprintf(sc, VTSCSI_ERROR, "cannot create path\n");
		return;
	}

	xpt_async(ac_code, path, NULL);
	xpt_free_path(path);
}

static void
vtscsi_execute_rescan(struct vtscsi_softc *sc, target_id_t target_id,
    lun_id_t lun_id)
{
	union ccb *ccb;
	cam_status status;

	ccb = xpt_alloc_ccb_nowait();
	if (ccb == NULL) {
		vtscsi_dprintf(sc, VTSCSI_ERROR, "cannot allocate CCB\n");
		return;
	}

	status = xpt_create_path(&ccb->ccb_h.path, NULL,
	    cam_sim_path(sc->vtscsi_sim), target_id, lun_id);
	if (status != CAM_REQ_CMP) {
		xpt_free_ccb(ccb);
		return;
	}

	xpt_rescan(ccb);
}

static void
vtscsi_execute_rescan_bus(struct vtscsi_softc *sc)
{

	vtscsi_execute_rescan(sc, CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD);
}

static void
vtscsi_transport_reset_event(struct vtscsi_softc *sc,
    struct virtio_scsi_event *event)
{
	target_id_t target_id;
	lun_id_t lun_id;

	vtscsi_get_request_lun(event->lun, &target_id, &lun_id);

	switch (event->reason) {
	case VIRTIO_SCSI_EVT_RESET_RESCAN:
	case VIRTIO_SCSI_EVT_RESET_REMOVED:
		vtscsi_execute_rescan(sc, target_id, lun_id);
		break;
	default:
		device_printf(sc->vtscsi_dev,
		    "unhandled transport event reason: %d\n", event->reason);
		break;
	}
}

static void
vtscsi_handle_event(struct vtscsi_softc *sc, struct virtio_scsi_event *event)
{
	int error;

	if ((event->event & VIRTIO_SCSI_T_EVENTS_MISSED) == 0) {
		switch (event->event) {
		case VIRTIO_SCSI_T_TRANSPORT_RESET:
			vtscsi_transport_reset_event(sc, event);
			break;
		default:
			device_printf(sc->vtscsi_dev,
			    "unhandled event: %d\n", event->event);
			break;
		}
	} else
		vtscsi_execute_rescan_bus(sc);

	/*
	 * This should always be successful since the buffer
	 * was just dequeued.
	 */
	error = vtscsi_enqueue_event_buf(sc, event);
	KASSERT(error == 0,
	    ("cannot requeue event buffer: %d", error));
}

static int
vtscsi_enqueue_event_buf(struct vtscsi_softc *sc,
    struct virtio_scsi_event *event)
{
	struct sglist *sg;
	struct virtqueue *vq;
	int size, error;

	sg = sc->vtscsi_sglist;
	vq = sc->vtscsi_event_vq;
	size = sc->vtscsi_event_buf_size;

	bzero(event, size);

	sglist_reset(sg);
	error = sglist_append(sg, event, size);
	if (error)
		return (error);

	error = virtqueue_enqueue(vq, event, sg, 0, sg->sg_nseg);
	if (error)
		return (error);

	virtqueue_notify(vq);

	return (0);
}

static int
vtscsi_init_event_vq(struct vtscsi_softc *sc)
{
	struct virtio_scsi_event *event;
	int i, size, error;

	/*
	 * The first release of QEMU with VirtIO SCSI support would crash
	 * when attempting to notify the event virtqueue. This was fixed
	 * when hotplug support was added.
	 */
	if (sc->vtscsi_flags & VTSCSI_FLAG_HOTPLUG)
		size = sc->vtscsi_event_buf_size;
	else
		size = 0;

	if (size < sizeof(struct virtio_scsi_event))
		return (0);

	for (i = 0; i < VTSCSI_NUM_EVENT_BUFS; i++) {
		event = &sc->vtscsi_event_bufs[i];

		error = vtscsi_enqueue_event_buf(sc, event);
		if (error)
			break;
	}

	/*
	 * Even just one buffer is enough. Missed events are
	 * denoted with the VIRTIO_SCSI_T_EVENTS_MISSED flag.
	 */
	if (i > 0)
		error = 0;

	return (error);
}

static void
vtscsi_reinit_event_vq(struct vtscsi_softc *sc)
{
	struct virtio_scsi_event *event;
	int i, error;

	if ((sc->vtscsi_flags & VTSCSI_FLAG_HOTPLUG) == 0 ||
	    sc->vtscsi_event_buf_size < sizeof(struct virtio_scsi_event))
		return;

	for (i = 0; i < VTSCSI_NUM_EVENT_BUFS; i++) {
		event = &sc->vtscsi_event_bufs[i];

		error = vtscsi_enqueue_event_buf(sc, event);
		if (error)
			break;
	}

	KASSERT(i > 0, ("cannot reinit event vq: %d", error));
}

static void
vtscsi_drain_event_vq(struct vtscsi_softc *sc)
{
	struct virtqueue *vq;
	int last;

	vq = sc->vtscsi_event_vq;
	last = 0;

	while (virtqueue_drain(vq, &last) != NULL)
		;

	KASSERT(virtqueue_empty(vq), ("eventvq not empty"));
}

static void
vtscsi_complete_vqs_locked(struct vtscsi_softc *sc)
{

	VTSCSI_LOCK_OWNED(sc);

	if (sc->vtscsi_request_vq != NULL)
		vtscsi_complete_vq(sc, sc->vtscsi_request_vq);
	if (sc->vtscsi_control_vq != NULL)
		vtscsi_complete_vq(sc, sc->vtscsi_control_vq);
}

static void
vtscsi_complete_vqs(struct vtscsi_softc *sc)
{

	VTSCSI_LOCK(sc);
	vtscsi_complete_vqs_locked(sc);
	VTSCSI_UNLOCK(sc);
}

static void
vtscsi_cancel_request(struct vtscsi_softc *sc, struct vtscsi_request *req)
{
	union ccb *ccb;
	int detach;

	ccb = req->vsr_ccb;

	vtscsi_dprintf(sc, VTSCSI_TRACE, "req=%p ccb=%p\n", req, ccb);

	/*
	 * The callout must be drained when detaching since the request is
	 * about to be freed. The VTSCSI_MTX must not be held for this in
	 * case the callout is pending because there is a deadlock potential.
	 * Otherwise, the virtqueue is being drained because of a bus reset
	 * so we only need to attempt to stop the callouts.
	 */
	detach = (sc->vtscsi_flags & VTSCSI_FLAG_DETACH) != 0;
	if (detach != 0)
		VTSCSI_LOCK_NOTOWNED(sc);
	else
		VTSCSI_LOCK_OWNED(sc);

	if (req->vsr_flags & VTSCSI_REQ_FLAG_TIMEOUT_SET) {
		if (detach != 0)
			callout_drain(&req->vsr_callout);
		else
			callout_stop(&req->vsr_callout);
	}

	if (ccb != NULL) {
		if (detach != 0) {
			VTSCSI_LOCK(sc);
			ccb->ccb_h.status = CAM_NO_HBA;
		} else
			ccb->ccb_h.status = CAM_REQUEUE_REQ;
		xpt_done(ccb);
		if (detach != 0)
			VTSCSI_UNLOCK(sc);
	}

	vtscsi_enqueue_request(sc, req);
}

static void
vtscsi_drain_vq(struct vtscsi_softc *sc, struct virtqueue *vq)
{
	struct vtscsi_request *req;
	int last;

	last = 0;

	vtscsi_dprintf(sc, VTSCSI_TRACE, "vq=%p\n", vq);

	while ((req = virtqueue_drain(vq, &last)) != NULL)
		vtscsi_cancel_request(sc, req);

	KASSERT(virtqueue_empty(vq), ("virtqueue not empty"));
}

static void
vtscsi_drain_vqs(struct vtscsi_softc *sc)
{

	if (sc->vtscsi_control_vq != NULL)
		vtscsi_drain_vq(sc, sc->vtscsi_control_vq);
	if (sc->vtscsi_request_vq != NULL)
		vtscsi_drain_vq(sc, sc->vtscsi_request_vq);
	if (sc->vtscsi_event_vq != NULL)
		vtscsi_drain_event_vq(sc);
}

static void
vtscsi_stop(struct vtscsi_softc *sc)
{

	vtscsi_disable_vqs_intr(sc);
	virtio_stop(sc->vtscsi_dev);
}

static int
vtscsi_reset_bus(struct vtscsi_softc *sc)
{
	int error;

	VTSCSI_LOCK_OWNED(sc);

	if (vtscsi_bus_reset_disable != 0) {
		device_printf(sc->vtscsi_dev, "bus reset disabled\n");
		return (0);
	}

	sc->vtscsi_flags |= VTSCSI_FLAG_RESET;

	/*
	 * vtscsi_stop() will cause the in-flight requests to be canceled.
	 * Those requests are then completed here so CAM will retry them
	 * after the reset is complete.
	 */
	vtscsi_stop(sc);
	vtscsi_complete_vqs_locked(sc);

	/* Rid the virtqueues of any remaining requests. */
	vtscsi_drain_vqs(sc);

	/*
	 * Any resource shortage that froze the SIMQ cannot persist across
	 * a bus reset so ensure it gets thawed here.
	 */
	if (vtscsi_thaw_simq(sc, VTSCSI_REQUEST | VTSCSI_REQUEST_VQ) != 0)
		xpt_release_simq(sc->vtscsi_sim, 0);

	error = vtscsi_reinit(sc);
	if (error) {
		device_printf(sc->vtscsi_dev,
		    "reinitialization failed, stopping device...\n");
		vtscsi_stop(sc);
	} else
		vtscsi_announce(sc, AC_BUS_RESET, CAM_TARGET_WILDCARD,
		    CAM_LUN_WILDCARD);

	sc->vtscsi_flags &= ~VTSCSI_FLAG_RESET;

	return (error);
}

static void
vtscsi_init_request(struct vtscsi_softc *sc, struct vtscsi_request *req)
{

#ifdef INVARIANTS
	int req_nsegs, resp_nsegs;

	req_nsegs = sglist_count(&req->vsr_ureq, sizeof(req->vsr_ureq));
	resp_nsegs = sglist_count(&req->vsr_uresp, sizeof(req->vsr_uresp));

	KASSERT(req_nsegs == 1, ("request crossed page boundary"));
	KASSERT(resp_nsegs == 1, ("response crossed page boundary"));
#endif

	req->vsr_softc = sc;
	callout_init_mtx(&req->vsr_callout, VTSCSI_MTX(sc), 0);
}

static int
vtscsi_alloc_requests(struct vtscsi_softc *sc)
{
	struct vtscsi_request *req;
	int i, nreqs;

	/*
	 * Commands destined for either the request or control queues come
	 * from the same SIM queue. Use the size of the request virtqueue
	 * as it (should) be much more frequently used. Some additional
	 * requests are allocated for internal (TMF) use.
	 */
	nreqs = virtqueue_size(sc->vtscsi_request_vq);
	if ((sc->vtscsi_flags & VTSCSI_FLAG_INDIRECT) == 0)
		nreqs /= VTSCSI_MIN_SEGMENTS;
	nreqs += VTSCSI_RESERVED_REQUESTS;

	for (i = 0; i < nreqs; i++) {
		req = malloc(sizeof(struct vtscsi_request), M_DEVBUF,
		    M_NOWAIT);
		if (req == NULL)
			return (ENOMEM);

		vtscsi_init_request(sc, req);

		sc->vtscsi_nrequests++;
		vtscsi_enqueue_request(sc, req);
	}

	return (0);
}

static void
vtscsi_free_requests(struct vtscsi_softc *sc)
{
	struct vtscsi_request *req;

	while ((req = vtscsi_dequeue_request(sc)) != NULL) {
		KASSERT(callout_active(&req->vsr_callout) == 0,
		    ("request callout still active"));

		sc->vtscsi_nrequests--;
		free(req, M_DEVBUF);
	}

	KASSERT(sc->vtscsi_nrequests == 0, ("leaked requests: %d",
	    sc->vtscsi_nrequests));
}

static void
vtscsi_enqueue_request(struct vtscsi_softc *sc, struct vtscsi_request *req)
{

	KASSERT(req->vsr_softc == sc,
	    ("non-matching request vsr_softc %p/%p", req->vsr_softc, sc));

	vtscsi_dprintf(sc, VTSCSI_TRACE, "req=%p\n", req);

	/* A request is available so the SIMQ could be released. */
	if (vtscsi_thaw_simq(sc, VTSCSI_REQUEST) != 0)
		xpt_release_simq(sc->vtscsi_sim, 1);

	req->vsr_ccb = NULL;
	req->vsr_complete = NULL;
	req->vsr_ptr0 = NULL;
	req->vsr_state = VTSCSI_REQ_STATE_FREE;
	req->vsr_flags = 0;

	bzero(&req->vsr_ureq, sizeof(req->vsr_ureq));
	bzero(&req->vsr_uresp, sizeof(req->vsr_uresp));

	/*
	 * We insert at the tail of the queue in order to make it
	 * very unlikely a request will be reused if we race with
	 * stopping its callout handler.
	 */
	TAILQ_INSERT_TAIL(&sc->vtscsi_req_free, req, vsr_link);
}

static struct vtscsi_request *
vtscsi_dequeue_request(struct vtscsi_softc *sc)
{
	struct vtscsi_request *req;

	req = TAILQ_FIRST(&sc->vtscsi_req_free);
	if (req != NULL) {
		req->vsr_state = VTSCSI_REQ_STATE_INUSE;
		TAILQ_REMOVE(&sc->vtscsi_req_free, req, vsr_link);
	} else
		sc->vtscsi_stats.dequeue_no_requests++;

	vtscsi_dprintf(sc, VTSCSI_TRACE, "req=%p\n", req);

	return (req);
}

static void
vtscsi_complete_request(struct vtscsi_request *req)
{

	if (req->vsr_flags & VTSCSI_REQ_FLAG_POLLED)
		req->vsr_flags |= VTSCSI_REQ_FLAG_COMPLETE;

	if (req->vsr_complete != NULL)
		req->vsr_complete(req->vsr_softc, req);
}

static void
vtscsi_complete_vq(struct vtscsi_softc *sc, struct virtqueue *vq)
{
	struct vtscsi_request *req;

	VTSCSI_LOCK_OWNED(sc);

	while ((req = virtqueue_dequeue(vq, NULL)) != NULL)
		vtscsi_complete_request(req);
}

static void
vtscsi_control_vq_intr(void *xsc)
{
	struct vtscsi_softc *sc;
	struct virtqueue *vq;

	sc = xsc;
	vq = sc->vtscsi_control_vq;

again:
	VTSCSI_LOCK(sc);

	vtscsi_complete_vq(sc, sc->vtscsi_control_vq);

	if (virtqueue_enable_intr(vq) != 0) {
		virtqueue_disable_intr(vq);
		VTSCSI_UNLOCK(sc);
		goto again;
	}

	VTSCSI_UNLOCK(sc);
}

static void
vtscsi_event_vq_intr(void *xsc)
{
	struct vtscsi_softc *sc;
	struct virtqueue *vq;
	struct virtio_scsi_event *event;

	sc = xsc;
	vq = sc->vtscsi_event_vq;

again:
	VTSCSI_LOCK(sc);

	while ((event = virtqueue_dequeue(vq, NULL)) != NULL)
		vtscsi_handle_event(sc, event);

	if (virtqueue_enable_intr(vq) != 0) {
		virtqueue_disable_intr(vq);
		VTSCSI_UNLOCK(sc);
		goto again;
	}

	VTSCSI_UNLOCK(sc);
}

static void
vtscsi_request_vq_intr(void *xsc)
{
	struct vtscsi_softc *sc;
	struct virtqueue *vq;

	sc = xsc;
	vq = sc->vtscsi_request_vq;

again:
	VTSCSI_LOCK(sc);

	vtscsi_complete_vq(sc, sc->vtscsi_request_vq);

	if (virtqueue_enable_intr(vq) != 0) {
		virtqueue_disable_intr(vq);
		VTSCSI_UNLOCK(sc);
		goto again;
	}

	VTSCSI_UNLOCK(sc);
}

static void
vtscsi_disable_vqs_intr(struct vtscsi_softc *sc)
{

	virtqueue_disable_intr(sc->vtscsi_control_vq);
	virtqueue_disable_intr(sc->vtscsi_event_vq);
	virtqueue_disable_intr(sc->vtscsi_request_vq);
}

static void
vtscsi_enable_vqs_intr(struct vtscsi_softc *sc)
{

	virtqueue_enable_intr(sc->vtscsi_control_vq);
	virtqueue_enable_intr(sc->vtscsi_event_vq);
	virtqueue_enable_intr(sc->vtscsi_request_vq);
}

static void
vtscsi_get_tunables(struct vtscsi_softc *sc)
{
	char tmpstr[64];

	TUNABLE_INT_FETCH("hw.vtscsi.debug_level", &sc->vtscsi_debug);

	snprintf(tmpstr, sizeof(tmpstr), "dev.vtscsi.%d.debug_level",
	    device_get_unit(sc->vtscsi_dev));
	TUNABLE_INT_FETCH(tmpstr, &sc->vtscsi_debug);
}

static void
vtscsi_add_sysctl(struct vtscsi_softc *sc)
{
	device_t dev;
	struct vtscsi_statistics *stats;
        struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;
	struct sysctl_oid_list *child;

	dev = sc->vtscsi_dev;
	stats = &sc->vtscsi_stats;
	ctx = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);
	child = SYSCTL_CHILDREN(tree);

	SYSCTL_ADD_INT(ctx, child, OID_AUTO, "debug_level",
	    CTLFLAG_RW, &sc->vtscsi_debug, 0,
	    "Debug level");

	SYSCTL_ADD_ULONG(ctx, child, OID_AUTO, "scsi_cmd_timeouts",
	    CTLFLAG_RD, &stats->scsi_cmd_timeouts,
	    "SCSI command timeouts");
	SYSCTL_ADD_ULONG(ctx, child, OID_AUTO, "dequeue_no_requests",
	    CTLFLAG_RD, &stats->dequeue_no_requests,
	    "No available requests to dequeue");
}

static void
vtscsi_printf_req(struct vtscsi_request *req, const char *func,
    const char *fmt, ...)
{
	struct vtscsi_softc *sc;
	union ccb *ccb;
	struct sbuf sb;
	va_list ap;
	char str[192];
	char path_str[64];

	if (req == NULL)
		return;

	sc = req->vsr_softc;
	ccb = req->vsr_ccb;

	va_start(ap, fmt);
	sbuf_new(&sb, str, sizeof(str), 0);

	if (ccb == NULL) {
		sbuf_printf(&sb, "(noperiph:%s%d:%u): ",
		    cam_sim_name(sc->vtscsi_sim), cam_sim_unit(sc->vtscsi_sim),
		    cam_sim_bus(sc->vtscsi_sim));
	} else {
		xpt_path_string(ccb->ccb_h.path, path_str, sizeof(path_str));
		sbuf_cat(&sb, path_str);
		if (ccb->ccb_h.func_code == XPT_SCSI_IO) {
			scsi_command_string(&ccb->csio, &sb);
			sbuf_printf(&sb, "length %d ", ccb->csio.dxfer_len);
		}
	}

	sbuf_vprintf(&sb, fmt, ap);
	va_end(ap);

	sbuf_finish(&sb);
	printf("%s: %s: %s", device_get_nameunit(sc->vtscsi_dev), func,
	    sbuf_data(&sb));
}
