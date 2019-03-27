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
 *
 * $FreeBSD$
 */

#ifndef _VIRTIO_SCSIVAR_H
#define _VIRTIO_SCSIVAR_H

struct vtscsi_softc;
struct vtscsi_request;

typedef void vtscsi_request_cb_t(struct vtscsi_softc *,
    struct vtscsi_request *);

struct vtscsi_statistics {
	unsigned long		scsi_cmd_timeouts;
	unsigned long		dequeue_no_requests;
};

struct vtscsi_softc {
	device_t		 vtscsi_dev;
	struct mtx		 vtscsi_mtx;
	uint64_t		 vtscsi_features;

	uint16_t		 vtscsi_flags;
#define VTSCSI_FLAG_INDIRECT		0x0001
#define VTSCSI_FLAG_BIDIRECTIONAL	0x0002
#define VTSCSI_FLAG_HOTPLUG		0x0004
#define VTSCSI_FLAG_RESET		0x0008
#define VTSCSI_FLAG_DETACH		0x0010

	uint16_t		 vtscsi_frozen;
#define VTSCSI_FROZEN_NO_REQUESTS	0x01
#define VTSCSI_FROZEN_REQUEST_VQ_FULL	0x02

	struct sglist		*vtscsi_sglist;

	struct virtqueue	*vtscsi_control_vq;
	struct virtqueue	*vtscsi_event_vq;
	struct virtqueue	*vtscsi_request_vq;

	struct cam_sim		*vtscsi_sim;
	struct cam_path		*vtscsi_path;

	int			 vtscsi_debug;
	int			 vtscsi_nrequests;
	int			 vtscsi_max_nsegs;
	int			 vtscsi_event_buf_size;

	TAILQ_HEAD(,vtscsi_request)
				 vtscsi_req_free;

	uint16_t		 vtscsi_max_channel;
	uint16_t		 vtscsi_max_target;
	uint32_t		 vtscsi_max_lun;

#define VTSCSI_NUM_EVENT_BUFS	4
	struct virtio_scsi_event
				 vtscsi_event_bufs[VTSCSI_NUM_EVENT_BUFS];

	struct vtscsi_statistics vtscsi_stats;
};

enum vtscsi_request_state {
	VTSCSI_REQ_STATE_FREE,
	VTSCSI_REQ_STATE_INUSE,
	VTSCSI_REQ_STATE_ABORTED,
	VTSCSI_REQ_STATE_TIMEDOUT
};

struct vtscsi_request {
	struct vtscsi_softc			*vsr_softc;
	union ccb				*vsr_ccb;
	vtscsi_request_cb_t			*vsr_complete;

	void					*vsr_ptr0;
/* Request when aborting a timedout command. */
#define vsr_timedout_req	vsr_ptr0

	enum vtscsi_request_state		 vsr_state;

	uint16_t				 vsr_flags;
#define VTSCSI_REQ_FLAG_POLLED		0x01
#define VTSCSI_REQ_FLAG_COMPLETE	0x02
#define VTSCSI_REQ_FLAG_TIMEOUT_SET	0x04

	union {
		struct virtio_scsi_cmd_req	 cmd;
		struct virtio_scsi_ctrl_tmf_req	 tmf;
		struct virtio_scsi_ctrl_an_req	 an;
	} vsr_ureq;
#define vsr_cmd_req 	vsr_ureq.cmd
#define vsr_tmf_req 	vsr_ureq.tmf
#define vsr_an_req	vsr_ureq.an

	/* Make request and response non-contiguous. */
	uint32_t				 vsr_pad;

	union {
		struct virtio_scsi_cmd_resp	 cmd;
		struct virtio_scsi_ctrl_tmf_resp tmf;
		struct virtio_scsi_ctrl_an_resp	 an;
	} vsr_uresp;
#define vsr_cmd_resp	vsr_uresp.cmd
#define vsr_tmf_resp	vsr_uresp.tmf
#define vsr_an_resp	vsr_uresp.an

	struct callout				 vsr_callout;

	TAILQ_ENTRY(vtscsi_request)		 vsr_link;
};

/* Private field in the CCB header that points to our request. */
#define ccbh_vtscsi_req	spriv_ptr0

/* Features desired/implemented by this driver. */
#define VTSCSI_FEATURES \
    (VIRTIO_SCSI_F_HOTPLUG		| \
     VIRTIO_RING_F_INDIRECT_DESC)

#define VTSCSI_MTX(_sc)			&(_sc)->vtscsi_mtx
#define VTSCSI_LOCK_INIT(_sc, _name) 	mtx_init(VTSCSI_MTX(_sc), _name, \
					    "VTSCSI Lock", MTX_DEF)
#define VTSCSI_LOCK(_sc)		mtx_lock(VTSCSI_MTX(_sc))
#define VTSCSI_UNLOCK(_sc)		mtx_unlock(VTSCSI_MTX(_sc))
#define VTSCSI_LOCK_OWNED(_sc)		mtx_assert(VTSCSI_MTX(_sc), MA_OWNED)
#define VTSCSI_LOCK_NOTOWNED(_sc) 	mtx_assert(VTSCSI_MTX(_sc), MA_NOTOWNED)
#define VTSCSI_LOCK_DESTROY(_sc)	mtx_destroy(VTSCSI_MTX(_sc))

/*
 * Reasons for either freezing or thawing the SIMQ.
 *
 * VirtIO SCSI is a bit unique in the sense that SCSI and TMF
 * commands go over different queues. Both queues are fed by
 * the same SIMQ, but we only freeze the SIMQ when the request
 * (SCSI) virtqueue is full, not caring if the control (TMF)
 * virtqueue unlikely gets full. However, both queues share the
 * same pool of requests, so the completion of a TMF command
 * could cause the SIMQ to be unfrozen.
 */
#define VTSCSI_REQUEST		0x01
#define VTSCSI_REQUEST_VQ	0x02

/* Debug trace levels. */
#define VTSCSI_INFO	0x01
#define VTSCSI_ERROR	0x02
#define VTSCSI_TRACE	0x04

#define vtscsi_dprintf(_sc, _level, _msg, _args ...) do { 		\
	if ((_sc)->vtscsi_debug & (_level))				\
		device_printf((_sc)->vtscsi_dev, "%s: "_msg,		\
		    __FUNCTION__, ##_args);				\
} while (0)

#define vtscsi_dprintf_req(_req, _level, _msg, _args ...) do {		\
	struct vtscsi_softc *__sc = (_req)->vsr_softc;			\
	if ((__sc)->vtscsi_debug & (_level))				\
		vtscsi_printf_req(_req, __FUNCTION__, _msg, ##_args);	\
} while (0)

/*
 * Set the status field in a CCB, optionally clearing non CCB_STATUS_* flags.
 */
#define vtscsi_set_ccb_status(_ccbh, _status, _mask) do {		\
	KASSERT(((_mask) & CAM_STATUS_MASK) == 0,			\
	    ("%s:%d bad mask: 0x%x", __FUNCTION__, __LINE__, (_mask)));	\
	(_ccbh)->status &= ~(CAM_STATUS_MASK | (_mask));		\
	(_ccbh)->status |= (_status);					\
} while (0)

/*
 * One segment each for the request and the response.
 */
#define VTSCSI_MIN_SEGMENTS	2

/*
 * Allocate additional requests for internal use such
 * as TM commands (e.g. aborting timedout commands).
 */
#define VTSCSI_RESERVED_REQUESTS	10

/*
 * Specification doesn't say, use traditional SCSI default.
 */
#define VTSCSI_INITIATOR_ID	7

/*
 * How to wait (or not) for request completion.
 */
#define VTSCSI_EXECUTE_ASYNC	0
#define VTSCSI_EXECUTE_POLL	1

#endif /* _VIRTIO_SCSIVAR_H */
