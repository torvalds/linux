/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Silicon Graphics International Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $Id: //depot/users/kenm/FreeBSD-test2/sys/cam/ctl/ctl_frontend_cam_sim.c#4 $
 */
/*
 * CTL frontend to CAM SIM interface.  This allows access to CTL LUNs via
 * the da(4) and pass(4) drivers from inside the system.
 *
 * Author: Ken Merry <ken@FreeBSD.org>
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/types.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/queue.h>
#include <sys/bus.h>
#include <sys/sysctl.h>
#include <machine/bus.h>
#include <sys/sbuf.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>
#include <cam/cam_xpt.h>
#include <cam/cam_periph.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>
#include <cam/ctl/ctl_io.h>
#include <cam/ctl/ctl.h>
#include <cam/ctl/ctl_frontend.h>
#include <cam/ctl/ctl_debug.h>

#define	io_ptr		spriv_ptr1

struct cfcs_io {
	union ccb *ccb;
};

struct cfcs_softc {
	struct ctl_port port;
	char port_name[32];
	struct cam_sim *sim;
	struct cam_devq *devq;
	struct cam_path *path;
	struct mtx lock;
	uint64_t wwnn;
	uint64_t wwpn;
	uint32_t cur_tag_num;
	int online;
};

/*
 * We can't handle CCBs with these flags.  For the most part, we just don't
 * handle physical addresses yet.  That would require mapping things in
 * order to do the copy.
 */
#define	CFCS_BAD_CCB_FLAGS (CAM_DATA_ISPHYS | CAM_MSG_BUF_PHYS |	\
	CAM_SNS_BUF_PHYS | CAM_CDB_PHYS | CAM_SENSE_PTR |		\
	CAM_SENSE_PHYS)

static int cfcs_init(void);
static int cfcs_shutdown(void);
static void cfcs_poll(struct cam_sim *sim);
static void cfcs_online(void *arg);
static void cfcs_offline(void *arg);
static void cfcs_datamove(union ctl_io *io);
static void cfcs_done(union ctl_io *io);
void cfcs_action(struct cam_sim *sim, union ccb *ccb);

struct cfcs_softc cfcs_softc;
/*
 * This is primarily intended to allow for error injection to test the CAM
 * sense data and sense residual handling code.  This sets the maximum
 * amount of SCSI sense data that we will report to CAM.
 */
static int cfcs_max_sense = sizeof(struct scsi_sense_data);

SYSCTL_NODE(_kern_cam, OID_AUTO, ctl2cam, CTLFLAG_RD, 0,
	    "CAM Target Layer SIM frontend");
SYSCTL_INT(_kern_cam_ctl2cam, OID_AUTO, max_sense, CTLFLAG_RW,
           &cfcs_max_sense, 0, "Maximum sense data size");

static struct ctl_frontend cfcs_frontend =
{
	.name = "camsim",
	.init = cfcs_init,
	.shutdown = cfcs_shutdown,
};
CTL_FRONTEND_DECLARE(ctlcfcs, cfcs_frontend);

static int
cfcs_init(void)
{
	struct cfcs_softc *softc;
	struct ctl_port *port;
	int retval;

	softc = &cfcs_softc;
	bzero(softc, sizeof(*softc));
	mtx_init(&softc->lock, "ctl2cam", NULL, MTX_DEF);
	port = &softc->port;

	port->frontend = &cfcs_frontend;
	port->port_type = CTL_PORT_INTERNAL;
	/* XXX KDM what should the real number be here? */
	port->num_requested_ctl_io = 4096;
	snprintf(softc->port_name, sizeof(softc->port_name), "camsim");
	port->port_name = softc->port_name;
	port->port_online = cfcs_online;
	port->port_offline = cfcs_offline;
	port->onoff_arg = softc;
	port->fe_datamove = cfcs_datamove;
	port->fe_done = cfcs_done;
	port->targ_port = -1;

	retval = ctl_port_register(port);
	if (retval != 0) {
		printf("%s: ctl_port_register() failed with error %d!\n",
		       __func__, retval);
		mtx_destroy(&softc->lock);
		return (retval);
	}

	/*
	 * If the CTL frontend didn't tell us what our WWNN/WWPN is, go
	 * ahead and set something random.
	 */
	if (port->wwnn == 0) {
		uint64_t random_bits;

		arc4rand(&random_bits, sizeof(random_bits), 0);
		softc->wwnn = (random_bits & 0x0000000fffffff00ULL) |
			/* Company ID */ 0x5000000000000000ULL |
			/* NL-Port */    0x0300;
		softc->wwpn = softc->wwnn + port->targ_port + 1;
		ctl_port_set_wwns(port, true, softc->wwnn, true, softc->wwpn);
	} else {
		softc->wwnn = port->wwnn;
		softc->wwpn = port->wwpn;
	}

	mtx_lock(&softc->lock);
	softc->devq = cam_simq_alloc(port->num_requested_ctl_io);
	if (softc->devq == NULL) {
		printf("%s: error allocating devq\n", __func__);
		retval = ENOMEM;
		goto bailout;
	}

	softc->sim = cam_sim_alloc(cfcs_action, cfcs_poll, softc->port_name,
				   softc, /*unit*/ 0, &softc->lock, 1,
				   port->num_requested_ctl_io, softc->devq);
	if (softc->sim == NULL) {
		printf("%s: error allocating SIM\n", __func__);
		retval = ENOMEM;
		goto bailout;
	}

	if (xpt_bus_register(softc->sim, NULL, 0) != CAM_SUCCESS) {
		printf("%s: error registering SIM\n", __func__);
		retval = ENOMEM;
		goto bailout;
	}

	if (xpt_create_path(&softc->path, /*periph*/NULL,
			    cam_sim_path(softc->sim),
			    CAM_TARGET_WILDCARD,
			    CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
		printf("%s: error creating path\n", __func__);
		xpt_bus_deregister(cam_sim_path(softc->sim));
		retval = EINVAL;
		goto bailout;
	}

	mtx_unlock(&softc->lock);

	return (retval);

bailout:
	if (softc->sim)
		cam_sim_free(softc->sim, /*free_devq*/ TRUE);
	else if (softc->devq)
		cam_simq_free(softc->devq);
	mtx_unlock(&softc->lock);
	mtx_destroy(&softc->lock);

	return (retval);
}

static int
cfcs_shutdown(void)
{
	struct cfcs_softc *softc = &cfcs_softc;
	struct ctl_port *port = &softc->port;
	int error;

	ctl_port_offline(port);

	mtx_lock(&softc->lock);
	xpt_free_path(softc->path);
	xpt_bus_deregister(cam_sim_path(softc->sim));
	cam_sim_free(softc->sim, /*free_devq*/ TRUE);
	mtx_unlock(&softc->lock);
	mtx_destroy(&softc->lock);

	if ((error = ctl_port_deregister(port)) != 0)
		printf("%s: cam_sim port deregistration failed\n", __func__);
	return (error);
}

static void
cfcs_poll(struct cam_sim *sim)
{

}

static void
cfcs_onoffline(void *arg, int online)
{
	struct cfcs_softc *softc;
	union ccb *ccb;

	softc = (struct cfcs_softc *)arg;

	mtx_lock(&softc->lock);
	softc->online = online;

	ccb = xpt_alloc_ccb_nowait();
	if (ccb == NULL) {
		printf("%s: unable to allocate CCB for rescan\n", __func__);
		goto bailout;
	}

	if (xpt_create_path(&ccb->ccb_h.path, NULL,
			    cam_sim_path(softc->sim), CAM_TARGET_WILDCARD,
			    CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
		printf("%s: can't allocate path for rescan\n", __func__);
		xpt_free_ccb(ccb);
		goto bailout;
	}
	xpt_rescan(ccb);

bailout:
	mtx_unlock(&softc->lock);
}

static void
cfcs_online(void *arg)
{
	cfcs_onoffline(arg, /*online*/ 1);
}

static void
cfcs_offline(void *arg)
{
	cfcs_onoffline(arg, /*online*/ 0);
}

/*
 * This function is very similar to ctl_ioctl_do_datamove().  Is there a
 * way to combine the functionality?
 *
 * XXX KDM may need to move this into a thread.  We're doing a bcopy in the
 * caller's context, which will usually be the backend.  That may not be a
 * good thing.
 */
static void
cfcs_datamove(union ctl_io *io)
{
	union ccb *ccb;
	bus_dma_segment_t cam_sg_entry, *cam_sglist;
	struct ctl_sg_entry ctl_sg_entry, *ctl_sglist;
	int cam_sg_count, ctl_sg_count, cam_sg_start;
	int cam_sg_offset;
	int len_to_copy;
	int ctl_watermark, cam_watermark;
	int i, j;

	ccb = io->io_hdr.ctl_private[CTL_PRIV_FRONTEND].ptr;

	/*
	 * Note that we have a check in cfcs_action() to make sure that any
	 * CCBs with "bad" flags are returned with CAM_REQ_INVALID.  This
	 * is just to make sure no one removes that check without updating
	 * this code to provide the additional functionality necessary to
	 * support those modes of operation.
	 */
	KASSERT(((ccb->ccb_h.flags & CFCS_BAD_CCB_FLAGS) == 0), ("invalid "
		  "CAM flags %#x", (ccb->ccb_h.flags & CFCS_BAD_CCB_FLAGS)));

	/*
	 * Simplify things on both sides by putting single buffers into a
	 * single entry S/G list.
	 */
	switch ((ccb->ccb_h.flags & CAM_DATA_MASK)) {
	case CAM_DATA_SG: {
		int len_seen;

		cam_sglist = (bus_dma_segment_t *)ccb->csio.data_ptr;
		cam_sg_count = ccb->csio.sglist_cnt;
		cam_sg_start = cam_sg_count;
		cam_sg_offset = 0;

		for (i = 0, len_seen = 0; i < cam_sg_count; i++) {
			if ((len_seen + cam_sglist[i].ds_len) >=
			     io->scsiio.kern_rel_offset) {
				cam_sg_start = i;
				cam_sg_offset = io->scsiio.kern_rel_offset -
					len_seen;
				break;
			}
			len_seen += cam_sglist[i].ds_len;
		}
		break;
	}
	case CAM_DATA_VADDR:
		cam_sglist = &cam_sg_entry;
		cam_sglist[0].ds_len = ccb->csio.dxfer_len;
		cam_sglist[0].ds_addr = (bus_addr_t)(uintptr_t)ccb->csio.data_ptr;
		cam_sg_count = 1;
		cam_sg_start = 0;
		cam_sg_offset = io->scsiio.kern_rel_offset;
		break;
	default:
		panic("Invalid CAM flags %#x", ccb->ccb_h.flags);
	}

	if (io->scsiio.kern_sg_entries > 0) {
		ctl_sglist = (struct ctl_sg_entry *)io->scsiio.kern_data_ptr;
		ctl_sg_count = io->scsiio.kern_sg_entries;
	} else {
		ctl_sglist = &ctl_sg_entry;
		ctl_sglist->addr = io->scsiio.kern_data_ptr;
		ctl_sglist->len = io->scsiio.kern_data_len;
		ctl_sg_count = 1;
	}

	ctl_watermark = 0;
	cam_watermark = cam_sg_offset;
	for (i = cam_sg_start, j = 0;
	     i < cam_sg_count && j < ctl_sg_count;) {
		uint8_t *cam_ptr, *ctl_ptr;

		len_to_copy = MIN(cam_sglist[i].ds_len - cam_watermark,
				  ctl_sglist[j].len - ctl_watermark);

		cam_ptr = (uint8_t *)(uintptr_t)cam_sglist[i].ds_addr;
		cam_ptr = cam_ptr + cam_watermark;
		if (io->io_hdr.flags & CTL_FLAG_BUS_ADDR) {
			/*
			 * XXX KDM fix this!
			 */
			panic("need to implement bus address support");
#if 0
			kern_ptr = bus_to_virt(kern_sglist[j].addr);
#endif
		} else
			ctl_ptr = (uint8_t *)ctl_sglist[j].addr;
		ctl_ptr = ctl_ptr + ctl_watermark;

		if ((io->io_hdr.flags & CTL_FLAG_DATA_MASK) ==
		     CTL_FLAG_DATA_IN) {
			CTL_DEBUG_PRINT(("%s: copying %d bytes to CAM\n",
					 __func__, len_to_copy));
			CTL_DEBUG_PRINT(("%s: from %p to %p\n", ctl_ptr,
					 __func__, cam_ptr));
			bcopy(ctl_ptr, cam_ptr, len_to_copy);
		} else {
			CTL_DEBUG_PRINT(("%s: copying %d bytes from CAM\n",
					 __func__, len_to_copy));
			CTL_DEBUG_PRINT(("%s: from %p to %p\n", cam_ptr,
					 __func__, ctl_ptr));
			bcopy(cam_ptr, ctl_ptr, len_to_copy);
		}

		io->scsiio.ext_data_filled += len_to_copy;
		io->scsiio.kern_data_resid -= len_to_copy;

		cam_watermark += len_to_copy;
		if (cam_sglist[i].ds_len == cam_watermark) {
			i++;
			cam_watermark = 0;
		}

		ctl_watermark += len_to_copy;
		if (ctl_sglist[j].len == ctl_watermark) {
			j++;
			ctl_watermark = 0;
		}
	}

	if ((io->io_hdr.status & CTL_STATUS_MASK) == CTL_SUCCESS) {
		io->io_hdr.ctl_private[CTL_PRIV_FRONTEND].ptr = NULL;
		io->io_hdr.flags |= CTL_FLAG_STATUS_SENT;
		ccb->csio.resid = ccb->csio.dxfer_len -
		    io->scsiio.ext_data_filled;
		ccb->ccb_h.status &= ~CAM_STATUS_MASK;
		ccb->ccb_h.status |= CAM_REQ_CMP;
		xpt_done(ccb);
	}

	io->scsiio.be_move_done(io);
}

static void
cfcs_done(union ctl_io *io)
{
	union ccb *ccb;

	ccb = io->io_hdr.ctl_private[CTL_PRIV_FRONTEND].ptr;
	if (ccb == NULL) {
		ctl_free_io(io);
		return;
	}

	/*
	 * At this point we should have status.  If we don't, that's a bug.
	 */
	KASSERT(((io->io_hdr.status & CTL_STATUS_MASK) != CTL_STATUS_NONE),
		("invalid CTL status %#x", io->io_hdr.status));

	/*
	 * Translate CTL status to CAM status.
	 */
	if (ccb->ccb_h.func_code == XPT_SCSI_IO) {
		ccb->csio.resid = ccb->csio.dxfer_len -
		    io->scsiio.ext_data_filled;
	}
	ccb->ccb_h.status &= ~CAM_STATUS_MASK;
	switch (io->io_hdr.status & CTL_STATUS_MASK) {
	case CTL_SUCCESS:
		ccb->ccb_h.status |= CAM_REQ_CMP;
		break;
	case CTL_SCSI_ERROR:
		ccb->ccb_h.status |= CAM_SCSI_STATUS_ERROR | CAM_AUTOSNS_VALID;
		ccb->csio.scsi_status = io->scsiio.scsi_status;
		bcopy(&io->scsiio.sense_data, &ccb->csio.sense_data,
		      min(io->scsiio.sense_len, ccb->csio.sense_len));
		if (ccb->csio.sense_len > io->scsiio.sense_len)
			ccb->csio.sense_resid = ccb->csio.sense_len -
						io->scsiio.sense_len;
		else
			ccb->csio.sense_resid = 0;
		if ((ccb->csio.sense_len - ccb->csio.sense_resid) >
		     cfcs_max_sense) {
			ccb->csio.sense_resid = ccb->csio.sense_len -
						cfcs_max_sense;
		}
		break;
	case CTL_CMD_ABORTED:
		ccb->ccb_h.status |= CAM_REQ_ABORTED;
		break;
	case CTL_ERROR:
	default:
		ccb->ccb_h.status |= CAM_REQ_CMP_ERR;
		break;
	}
	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP &&
	    (ccb->ccb_h.status & CAM_DEV_QFRZN) == 0) {
		xpt_freeze_devq(ccb->ccb_h.path, 1);
		ccb->ccb_h.status |= CAM_DEV_QFRZN;
	}
	xpt_done(ccb);
	ctl_free_io(io);
}

void
cfcs_action(struct cam_sim *sim, union ccb *ccb)
{
	struct cfcs_softc *softc;
	int err;

	softc = (struct cfcs_softc *)cam_sim_softc(sim);
	mtx_assert(&softc->lock, MA_OWNED);

	switch (ccb->ccb_h.func_code) {
	case XPT_SCSI_IO: {
		union ctl_io *io;
		struct ccb_scsiio *csio;

		csio = &ccb->csio;

		/*
		 * Catch CCB flags, like physical address flags, that
	 	 * indicate situations we currently can't handle.
		 */
		if (ccb->ccb_h.flags & CFCS_BAD_CCB_FLAGS) {
			ccb->ccb_h.status = CAM_REQ_INVALID;
			printf("%s: bad CCB flags %#x (all flags %#x)\n",
			       __func__, ccb->ccb_h.flags & CFCS_BAD_CCB_FLAGS,
			       ccb->ccb_h.flags);
			xpt_done(ccb);
			return;
		}

		/*
		 * If we aren't online, there are no devices to see.
		 */
		if (softc->online == 0) {
			ccb->ccb_h.status = CAM_DEV_NOT_THERE;
			xpt_done(ccb);
			return;
		}

		io = ctl_alloc_io_nowait(softc->port.ctl_pool_ref);
		if (io == NULL) {
			printf("%s: can't allocate ctl_io\n", __func__);
			ccb->ccb_h.status = CAM_BUSY | CAM_DEV_QFRZN;
			xpt_freeze_devq(ccb->ccb_h.path, 1);
			xpt_done(ccb);
			return;
		}
		ctl_zero_io(io);
		/* Save pointers on both sides */
		io->io_hdr.ctl_private[CTL_PRIV_FRONTEND].ptr = ccb;
		ccb->ccb_h.io_ptr = io;

		/*
		 * Only SCSI I/O comes down this path, resets, etc. come
		 * down via the XPT_RESET_BUS/LUN CCBs below.
		 */
		io->io_hdr.io_type = CTL_IO_SCSI;
		io->io_hdr.nexus.initid = 1;
		io->io_hdr.nexus.targ_port = softc->port.targ_port;
		io->io_hdr.nexus.targ_lun = ctl_decode_lun(
		    CAM_EXTLUN_BYTE_SWIZZLE(ccb->ccb_h.target_lun));
		/*
		 * This tag scheme isn't the best, since we could in theory
		 * have a very long-lived I/O and tag collision, especially
		 * in a high I/O environment.  But it should work well
		 * enough for now.  Since we're using unsigned ints,
		 * they'll just wrap around.
		 */
		io->scsiio.tag_num = softc->cur_tag_num++;
		csio->tag_id = io->scsiio.tag_num;
		switch (csio->tag_action) {
		case CAM_TAG_ACTION_NONE:
			io->scsiio.tag_type = CTL_TAG_UNTAGGED;
			break;
		case MSG_SIMPLE_TASK:
			io->scsiio.tag_type = CTL_TAG_SIMPLE;
			break;
		case MSG_HEAD_OF_QUEUE_TASK:
        		io->scsiio.tag_type = CTL_TAG_HEAD_OF_QUEUE;
			break;
		case MSG_ORDERED_TASK:
        		io->scsiio.tag_type = CTL_TAG_ORDERED;
			break;
		case MSG_ACA_TASK:
			io->scsiio.tag_type = CTL_TAG_ACA;
			break;
		default:
			io->scsiio.tag_type = CTL_TAG_UNTAGGED;
			printf("%s: unhandled tag type %#x!!\n", __func__,
			       csio->tag_action);
			break;
		}
		if (csio->cdb_len > sizeof(io->scsiio.cdb)) {
			printf("%s: WARNING: CDB len %d > ctl_io space %zd\n",
			       __func__, csio->cdb_len, sizeof(io->scsiio.cdb));
		}
		io->scsiio.cdb_len = min(csio->cdb_len, sizeof(io->scsiio.cdb));
		bcopy(scsiio_cdb_ptr(csio), io->scsiio.cdb, io->scsiio.cdb_len);

		ccb->ccb_h.status |= CAM_SIM_QUEUED;
		err = ctl_queue(io);
		if (err != CTL_RETVAL_COMPLETE) {
			printf("%s: func %d: error %d returned by "
			       "ctl_queue()!\n", __func__,
			       ccb->ccb_h.func_code, err);
			ctl_free_io(io);
			ccb->ccb_h.status = CAM_REQ_INVALID;
			xpt_done(ccb);
			return;
		}
		break;
	}
	case XPT_ABORT: {
		union ctl_io *io;
		union ccb *abort_ccb;

		abort_ccb = ccb->cab.abort_ccb;

		if (abort_ccb->ccb_h.func_code != XPT_SCSI_IO) {
			ccb->ccb_h.status = CAM_REQ_INVALID;
			xpt_done(ccb);
		}

		/*
		 * If we aren't online, there are no devices to talk to.
		 */
		if (softc->online == 0) {
			ccb->ccb_h.status = CAM_DEV_NOT_THERE;
			xpt_done(ccb);
			return;
		}

		io = ctl_alloc_io_nowait(softc->port.ctl_pool_ref);
		if (io == NULL) {
			ccb->ccb_h.status = CAM_BUSY | CAM_DEV_QFRZN;
			xpt_freeze_devq(ccb->ccb_h.path, 1);
			xpt_done(ccb);
			return;
		}

		ctl_zero_io(io);
		/* Save pointers on both sides */
		io->io_hdr.ctl_private[CTL_PRIV_FRONTEND].ptr = ccb;
		ccb->ccb_h.io_ptr = io;

		io->io_hdr.io_type = CTL_IO_TASK;
		io->io_hdr.nexus.initid = 1;
		io->io_hdr.nexus.targ_port = softc->port.targ_port;
		io->io_hdr.nexus.targ_lun = ctl_decode_lun(
		    CAM_EXTLUN_BYTE_SWIZZLE(ccb->ccb_h.target_lun));
		io->taskio.task_action = CTL_TASK_ABORT_TASK;
		io->taskio.tag_num = abort_ccb->csio.tag_id;
		switch (abort_ccb->csio.tag_action) {
		case CAM_TAG_ACTION_NONE:
			io->taskio.tag_type = CTL_TAG_UNTAGGED;
			break;
		case MSG_SIMPLE_TASK:
			io->taskio.tag_type = CTL_TAG_SIMPLE;
			break;
		case MSG_HEAD_OF_QUEUE_TASK:
        		io->taskio.tag_type = CTL_TAG_HEAD_OF_QUEUE;
			break;
		case MSG_ORDERED_TASK:
        		io->taskio.tag_type = CTL_TAG_ORDERED;
			break;
		case MSG_ACA_TASK:
			io->taskio.tag_type = CTL_TAG_ACA;
			break;
		default:
			io->taskio.tag_type = CTL_TAG_UNTAGGED;
			printf("%s: unhandled tag type %#x!!\n", __func__,
			       abort_ccb->csio.tag_action);
			break;
		}
		err = ctl_queue(io);
		if (err != CTL_RETVAL_COMPLETE) {
			printf("%s func %d: error %d returned by "
			       "ctl_queue()!\n", __func__,
			       ccb->ccb_h.func_code, err);
			ctl_free_io(io);
		}
		break;
	}
	case XPT_GET_TRAN_SETTINGS: {
		struct ccb_trans_settings *cts;
		struct ccb_trans_settings_scsi *scsi;
		struct ccb_trans_settings_fc *fc;

		cts = &ccb->cts;
		scsi = &cts->proto_specific.scsi;
		fc = &cts->xport_specific.fc;

		
		cts->protocol = PROTO_SCSI;
		cts->protocol_version = SCSI_REV_SPC2;
		cts->transport = XPORT_FC;
		cts->transport_version = 0;

		scsi->valid = CTS_SCSI_VALID_TQ;
		scsi->flags = CTS_SCSI_FLAGS_TAG_ENB;
		fc->valid = CTS_FC_VALID_SPEED;
		fc->bitrate = 800000;
		fc->wwnn = softc->wwnn;
		fc->wwpn = softc->wwpn;
		fc->port = softc->port.targ_port;
		fc->valid |= CTS_FC_VALID_WWNN | CTS_FC_VALID_WWPN |
			CTS_FC_VALID_PORT; 
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	case XPT_SET_TRAN_SETTINGS:
		/* XXX KDM should we actually do something here? */
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	case XPT_RESET_BUS:
	case XPT_RESET_DEV: {
		union ctl_io *io;

		/*
		 * If we aren't online, there are no devices to talk to.
		 */
		if (softc->online == 0) {
			ccb->ccb_h.status = CAM_DEV_NOT_THERE;
			xpt_done(ccb);
			return;
		}

		io = ctl_alloc_io_nowait(softc->port.ctl_pool_ref);
		if (io == NULL) {
			ccb->ccb_h.status = CAM_BUSY | CAM_DEV_QFRZN;
			xpt_freeze_devq(ccb->ccb_h.path, 1);
			xpt_done(ccb);
			return;
		}

		ctl_zero_io(io);
		/* Save pointers on both sides */
		if (ccb->ccb_h.func_code == XPT_RESET_DEV)
			io->io_hdr.ctl_private[CTL_PRIV_FRONTEND].ptr = ccb;
		ccb->ccb_h.io_ptr = io;

		io->io_hdr.io_type = CTL_IO_TASK;
		io->io_hdr.nexus.initid = 1;
		io->io_hdr.nexus.targ_port = softc->port.targ_port;
		io->io_hdr.nexus.targ_lun = ctl_decode_lun(
		    CAM_EXTLUN_BYTE_SWIZZLE(ccb->ccb_h.target_lun));
		if (ccb->ccb_h.func_code == XPT_RESET_BUS)
			io->taskio.task_action = CTL_TASK_BUS_RESET;
		else
			io->taskio.task_action = CTL_TASK_LUN_RESET;

		err = ctl_queue(io);
		if (err != CTL_RETVAL_COMPLETE) {
			printf("%s func %d: error %d returned by "
			      "ctl_queue()!\n", __func__,
			      ccb->ccb_h.func_code, err);
			ctl_free_io(io);
		}
		break;
	}
	case XPT_CALC_GEOMETRY:
		cam_calc_geometry(&ccb->ccg, 1);
		xpt_done(ccb);
		break;
	case XPT_PATH_INQ: {
		struct ccb_pathinq *cpi;

		cpi = &ccb->cpi;

		cpi->version_num = 0;
		cpi->hba_inquiry = PI_TAG_ABLE;
		cpi->target_sprt = 0;
		cpi->hba_misc = PIM_EXTLUNS;
		cpi->hba_eng_cnt = 0;
		cpi->max_target = 1;
		cpi->max_lun = 1024;
		/* Do we really have a limit? */
		cpi->maxio = 1024 * 1024;
		cpi->async_flags = 0;
		cpi->hpath_id = 0;
		cpi->initiator_id = 0;

		strlcpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
		strlcpy(cpi->hba_vid, "FreeBSD", HBA_IDLEN);
		strlcpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
		cpi->unit_number = 0;
		cpi->bus_id = 0;
		cpi->base_transfer_speed = 800000;
		cpi->protocol = PROTO_SCSI;
		cpi->protocol_version = SCSI_REV_SPC2;
		/*
		 * Pretend to be Fibre Channel.
		 */
		cpi->transport = XPORT_FC;
		cpi->transport_version = 0;
		cpi->xport_specific.fc.wwnn = softc->wwnn;
		cpi->xport_specific.fc.wwpn = softc->wwpn;
		cpi->xport_specific.fc.port = softc->port.targ_port;
		cpi->xport_specific.fc.bitrate = 8 * 1000 * 1000;
		cpi->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	default:
		ccb->ccb_h.status = CAM_PROVIDE_FAIL;
		printf("%s: unsupported CCB type %#x\n", __func__,
		       ccb->ccb_h.func_code);
		xpt_done(ccb);
		break;
	}
}
