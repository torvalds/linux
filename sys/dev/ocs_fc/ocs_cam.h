/*-
 * Copyright (c) 2017 Broadcom. All rights reserved.
 * The term "Broadcom" refers to Broadcom Limited and/or its subsidiaries.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef __OCS_CAM_H__
#define __OCS_CAM_H__

#include <cam/cam.h>
#include <cam/cam_sim.h>
#include <cam/cam_ccb.h>
#include <cam/cam_periph.h>
#include <cam/cam_xpt_sim.h>

#include <cam/scsi/scsi_message.h>


#define ccb_ocs_ptr     spriv_ptr0
#define ccb_io_ptr      spriv_ptr1

typedef STAILQ_HEAD(ocs_hdr_list_s, ccb_hdr) ocs_hdr_list_t;

typedef struct ocs_tgt_resource_s {
	ocs_hdr_list_t	atio;
	ocs_hdr_list_t	inot;
	uint8_t		enabled;

	lun_id_t 	lun;
} ocs_tgt_resource_t;

/* Common SCSI Domain structure declarations */

typedef struct {
} ocs_scsi_tgt_domain_t;

typedef struct {
} ocs_scsi_ini_domain_t;

/* Common SCSI SLI port structure declarations */

typedef struct {
} ocs_scsi_tgt_sport_t;

typedef struct {
} ocs_scsi_ini_sport_t;

/* Common IO structure declarations */

typedef enum {
	OCS_CAM_IO_FREE,	/* IO unused		(SIM) */
	OCS_CAM_IO_COMMAND,	/* ATIO returned to BE	(CTL) */
	OCS_CAM_IO_DATA,	/* data phase		(SIM) */
	OCS_CAM_IO_DATA_DONE,	/* CTIO returned to BE	(CTL) */
	OCS_CAM_IO_RESP,	/* send response	(SIM) */
	OCS_CAM_IO_MAX
} ocs_cam_io_state_t;

typedef struct {
	bus_dmamap_t	dmap;
	uint64_t lun;		/* target_lun */
	void		*app;	/** application specific pointer */
	ocs_cam_io_state_t state;
        bool            sendresp;
	uint32_t	flags;
#define OCS_CAM_IO_F_DMAPPED		BIT(0)	/* associated buffer bus_dmamap'd */
#define OCS_CAM_IO_F_ABORT_RECV		BIT(1)	/* received ABORT TASK */
#define OCS_CAM_IO_F_ABORT_DEV		BIT(2)	/* abort WQE pending */
#define OCS_CAM_IO_F_ABORT_TMF   	BIT(3)	/* TMF response sent */
#define OCS_CAM_IO_F_ABORT_NOTIFY	BIT(4)	/* XPT_NOTIFY sent to CTL */
#define OCS_CAM_IO_F_ABORT_CAM		BIT(5)	/* received ABORT or CTIO from CAM */
} ocs_scsi_tgt_io_t;

typedef struct {
} ocs_scsi_ini_io_t;

struct ocs_lun_crn {
        uint64_t lun;                   /* target_lun */
        uint8_t crnseed;                /* next command reference number */
};

/* Common NODE structure declarations */
typedef struct {
	struct ocs_lun_crn *lun_crn[OCS_MAX_LUN];
} ocs_scsi_ini_node_t;

typedef struct {
	uint32_t	busy_sent;
} ocs_scsi_tgt_node_t;

extern int32_t ocs_cam_attach(ocs_t *ocs);
extern int32_t ocs_cam_detach(ocs_t *ocs);

#endif /* __OCS_CAM_H__ */

