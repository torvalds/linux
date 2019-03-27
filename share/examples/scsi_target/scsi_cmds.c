/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * SCSI Disk Emulator
 *
 * Copyright (c) 2002 Nate Lawson.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <aio.h>
#include <unistd.h>
#include <assert.h>
#include <sys/param.h>
#include <sys/types.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_targetio.h>
#include "scsi_target.h"

typedef int targ_start_func(struct ccb_accept_tio *, struct ccb_scsiio *);
typedef void targ_done_func(struct ccb_accept_tio *, struct ccb_scsiio *,
			      io_ops);
#ifndef	REPORT_LUNS
#define	REPORT_LUNS	0xa0
#endif

struct targ_cdb_handlers {
	u_int8_t	  cmd;
	targ_start_func  *start;
	targ_done_func	 *done;
#define ILLEGAL_CDB	  0xFF
};

static targ_start_func		tcmd_inquiry;
static targ_start_func		tcmd_req_sense;
static targ_start_func		tcmd_rd_cap;
#ifdef READ_16
static targ_start_func		tcmd_rd_cap16;
#endif
static targ_start_func		tcmd_rdwr;
static targ_start_func		tcmd_rdwr_decode;
static targ_done_func		tcmd_rdwr_done;
static targ_start_func		tcmd_null_ok;
static targ_start_func		tcmd_illegal_req;
static int			start_io(struct ccb_accept_tio *atio,
					 struct ccb_scsiio *ctio, int dir);
static int init_inquiry(u_int16_t req_flags, u_int16_t sim_flags);
static struct initiator_state *
			tcmd_get_istate(u_int init_id);
static void cdb_debug(u_int8_t *cdb, const char *msg, ...);

static struct targ_cdb_handlers cdb_handlers[] = { 
	{ READ_10,		tcmd_rdwr,		tcmd_rdwr_done },
	{ WRITE_10,		tcmd_rdwr,		tcmd_rdwr_done },
	{ READ_6,		tcmd_rdwr,		tcmd_rdwr_done },
	{ WRITE_6,		tcmd_rdwr,		tcmd_rdwr_done },
	{ INQUIRY,		tcmd_inquiry,		NULL },
	{ REQUEST_SENSE,	tcmd_req_sense,		NULL },
	{ READ_CAPACITY,	tcmd_rd_cap,		NULL },
	{ TEST_UNIT_READY,	tcmd_null_ok,		NULL },
	{ START_STOP_UNIT,	tcmd_null_ok,		NULL },
	{ SYNCHRONIZE_CACHE,	tcmd_null_ok,		NULL },
	{ MODE_SENSE_6,		tcmd_illegal_req,	NULL },
	{ MODE_SELECT_6,	tcmd_illegal_req,	NULL },
	{ REPORT_LUNS,		tcmd_illegal_req,	NULL },
#ifdef READ_16
	{ READ_16,		tcmd_rdwr,		tcmd_rdwr_done },
	{ WRITE_16,		tcmd_rdwr,		tcmd_rdwr_done },
	{ SERVICE_ACTION_IN,	tcmd_rd_cap16,		NULL },
#endif
	{ ILLEGAL_CDB,		NULL,			NULL }
};

static struct scsi_inquiry_data inq_data;
static struct initiator_state istates[MAX_INITIATORS];
extern int		debug;
extern off_t		volume_size;
extern u_int		sector_size;
extern size_t		buf_size;

cam_status
tcmd_init(u_int16_t req_inq_flags, u_int16_t sim_inq_flags)
{
	struct initiator_state *istate;
	int i, ret;

	/* Initialize our inquiry data */
	ret = init_inquiry(req_inq_flags, sim_inq_flags);
	if (ret != 0)
        	return (ret);

	/* We start out life with a UA to indicate power-on/reset. */
	for (i = 0; i < MAX_INITIATORS; i++) {
		istate = tcmd_get_istate(i);
		bzero(istate, sizeof(*istate));
		istate->pending_ua = UA_POWER_ON;
	}

	return (0);
}

/* Caller allocates CTIO, sets its init_id
return 0 if done, 1 if more processing needed 
on 0, caller sets SEND_STATUS */
int
tcmd_handle(struct ccb_accept_tio *atio, struct ccb_scsiio *ctio, io_ops event)
{
	static struct targ_cdb_handlers *last_cmd; 
	struct initiator_state *istate;
	struct atio_descr *a_descr;
	int ret;

	if (debug) {
		warnx("tcmd_handle atio %p ctio %p atioflags %#x", atio, ctio,
		      atio->ccb_h.flags);
	}
	ret = 0;
	a_descr = (struct atio_descr *)atio->ccb_h.targ_descr;

	/* Do a full lookup if one-behind cache failed */
	if (last_cmd == NULL || last_cmd->cmd != a_descr->cdb[0]) {
		struct targ_cdb_handlers *h; 

		for (h = cdb_handlers; h->cmd != ILLEGAL_CDB; h++) {
			if (a_descr->cdb[0] == h->cmd)
				break;
		}
		last_cmd = h;
	}

	/* call completion and exit */
	if (event != ATIO_WORK) {
		if (last_cmd->done != NULL)
			last_cmd->done(atio, ctio, event);
		else
			free_ccb((union ccb *)ctio);
		return (1);
	}

	if (last_cmd->cmd == ILLEGAL_CDB) {
		if (event != ATIO_WORK) {
			warnx("no done func for %#x???", a_descr->cdb[0]);
			abort();
		}
		/* Not found, return illegal request */
		warnx("cdb %#x not handled", a_descr->cdb[0]);
		tcmd_illegal_req(atio, ctio);
		send_ccb((union ccb *)ctio, /*priority*/1);
		return (0);
	}

	istate = tcmd_get_istate(ctio->init_id);
	if (istate == NULL) {
		tcmd_illegal_req(atio, ctio);
		send_ccb((union ccb *)ctio, /*priority*/1);
		return (0);
	}

	if (istate->pending_ca == 0 && istate->pending_ua != 0 &&
	    a_descr->cdb[0] != INQUIRY) {
		tcmd_sense(ctio->init_id, ctio, SSD_KEY_UNIT_ATTENTION,
			   0x29, istate->pending_ua == UA_POWER_ON ? 1 : 2);
		istate->pending_ca = CA_UNIT_ATTN;
		if (debug) {
			cdb_debug(a_descr->cdb, "UA active for %u: ",
				  atio->init_id);
		}
		send_ccb((union ccb *)ctio, /*priority*/1);
		return (0);
	} 

	/* Store current CA and UA for later */
	istate->orig_ua = istate->pending_ua;
	istate->orig_ca = istate->pending_ca;

	/*
	 * As per SAM2, any command that occurs
	 * after a CA is reported, clears the CA.  We must
	 * also clear the UA condition, if any, that caused
	 * the CA to occur assuming the UA is not for a
	 * persistent condition.
	 */
	istate->pending_ca = CA_NONE;
	if (istate->orig_ca == CA_UNIT_ATTN)
		istate->pending_ua = UA_NONE;

	/* If we have a valid handler, call start or completion function */
	if (last_cmd->cmd != ILLEGAL_CDB) {
		ret = last_cmd->start(atio, ctio);
		/* XXX hack */
		if (last_cmd->start != tcmd_rdwr) {
			a_descr->init_req += ctio->dxfer_len;
			send_ccb((union ccb *)ctio, /*priority*/1);
		}
	}

	return (ret);
}

static struct initiator_state *
tcmd_get_istate(u_int init_id)
{
	if (init_id >= MAX_INITIATORS) {
		warnx("illegal init_id %d, max %d", init_id, MAX_INITIATORS - 1);
		return (NULL);
	} else {
		return (&istates[init_id]);
	}
}

void
tcmd_sense(u_int init_id, struct ccb_scsiio *ctio, u_int8_t flags,
	       u_int8_t asc, u_int8_t ascq)
{
	struct initiator_state *istate;
	struct scsi_sense_data_fixed *sense;

	/* Set our initiator's istate */
	istate = tcmd_get_istate(init_id);
	if (istate == NULL)
		return;
	istate->pending_ca |= CA_CMD_SENSE; /* XXX set instead of or? */
	sense = (struct scsi_sense_data_fixed *)&istate->sense_data;
	bzero(sense, sizeof(*sense));
	sense->error_code = SSD_CURRENT_ERROR;
	sense->flags = flags;
	sense->add_sense_code = asc;
	sense->add_sense_code_qual = ascq;
	sense->extra_len =
		offsetof(struct scsi_sense_data_fixed, sense_key_spec[2]) -
		offsetof(struct scsi_sense_data_fixed, extra_len);

	/* Fill out the supplied CTIO */
	if (ctio != NULL) {
		bcopy(sense, &ctio->sense_data, sizeof(*sense));
		ctio->sense_len = sizeof(*sense);  /* XXX */
		ctio->ccb_h.flags &= ~CAM_DIR_MASK;
		ctio->ccb_h.flags |= CAM_DIR_NONE | CAM_SEND_SENSE |
				     CAM_SEND_STATUS;
		ctio->dxfer_len = 0;
		ctio->scsi_status = SCSI_STATUS_CHECK_COND;
	}
}

void
tcmd_ua(u_int init_id, ua_types new_ua)
{
	struct initiator_state *istate;
	u_int start, end;

	if (init_id == CAM_TARGET_WILDCARD) {
		start = 0;
		end = MAX_INITIATORS - 1;
	} else {
		start = end = init_id;
	}

	for (; start <= end; start++) {
		istate = tcmd_get_istate(start);
		if (istate == NULL)
			break;
		istate->pending_ua = new_ua;
	}
}

static int
tcmd_inquiry(struct ccb_accept_tio *atio, struct ccb_scsiio *ctio)
{
	struct scsi_inquiry *inq;
	struct atio_descr *a_descr;
	struct initiator_state *istate;
	struct scsi_sense_data_fixed *sense;

	a_descr = (struct atio_descr *)atio->ccb_h.targ_descr;
	inq = (struct scsi_inquiry *)a_descr->cdb;

	if (debug)
		cdb_debug(a_descr->cdb, "INQUIRY from %u: ", atio->init_id);
	/*
	 * Validate the command.  We don't support any VPD pages, so
	 * complain if EVPD or CMDDT is set.
	 */
	istate = tcmd_get_istate(ctio->init_id);
	sense = (struct scsi_sense_data_fixed *)&istate->sense_data;
	if ((inq->byte2 & SI_EVPD) != 0) {
		tcmd_illegal_req(atio, ctio);
		sense->sense_key_spec[0] = SSD_SCS_VALID | SSD_FIELDPTR_CMD |
			SSD_BITPTR_VALID | /*bit value*/1;
		sense->sense_key_spec[1] = 0;
		sense->sense_key_spec[2] =
			offsetof(struct scsi_inquiry, byte2);
	} else if (inq->page_code != 0) {
		tcmd_illegal_req(atio, ctio);
		sense->sense_key_spec[0] = SSD_SCS_VALID | SSD_FIELDPTR_CMD;
		sense->sense_key_spec[1] = 0;
		sense->sense_key_spec[2] =
			offsetof(struct scsi_inquiry, page_code);
	} else {
		bcopy(&inq_data, ctio->data_ptr, sizeof(inq_data));
		ctio->dxfer_len = inq_data.additional_length + 4;
		ctio->dxfer_len = min(ctio->dxfer_len,
				      scsi_2btoul(inq->length));
		ctio->ccb_h.flags |= CAM_DIR_IN | CAM_SEND_STATUS;
		ctio->scsi_status = SCSI_STATUS_OK;
	}
	return (0);
}

/* Initialize the inquiry response structure with the requested flags */
static int
init_inquiry(u_int16_t req_flags, u_int16_t sim_flags)
{
	struct scsi_inquiry_data *inq;

	inq = &inq_data;
	bzero(inq, sizeof(*inq));
	inq->device = T_DIRECT | (SID_QUAL_LU_CONNECTED << 5);
#ifdef SCSI_REV_SPC
	inq->version = SCSI_REV_SPC; /* was 2 */
#else
	inq->version = SCSI_REV_3; /* was 2 */
#endif

	/*
	 * XXX cpi.hba_inquiry doesn't support Addr16 so we give the
	 * user what they want if they ask for it.
	 */
	if ((req_flags & SID_Addr16) != 0) {
		sim_flags |= SID_Addr16;
		warnx("Not sure SIM supports Addr16 but enabling it anyway");
	}

	/* Advertise only what the SIM can actually support */
	req_flags &= sim_flags;
	scsi_ulto2b(req_flags, &inq->spc2_flags);

	inq->response_format = 2; /* SCSI2 Inquiry Format */
	inq->additional_length = SHORT_INQUIRY_LENGTH -
		offsetof(struct scsi_inquiry_data, additional_length);
	bcopy("FreeBSD ", inq->vendor, SID_VENDOR_SIZE);
	bcopy("Emulated Disk   ", inq->product, SID_PRODUCT_SIZE);
	bcopy("0.1 ", inq->revision, SID_REVISION_SIZE);
	return (0);
}

static int
tcmd_req_sense(struct ccb_accept_tio *atio, struct ccb_scsiio *ctio)
{
	struct scsi_request_sense *rsense;
	struct scsi_sense_data_fixed *sense;
	struct initiator_state *istate;
	size_t dlen;
	struct atio_descr *a_descr;

	a_descr = (struct atio_descr *)atio->ccb_h.targ_descr;
	rsense = (struct scsi_request_sense *)a_descr->cdb;
	
	istate = tcmd_get_istate(ctio->init_id);
	sense = (struct scsi_sense_data_fixed *)&istate->sense_data;

	if (debug) {
		cdb_debug(a_descr->cdb, "REQ SENSE from %u: ", atio->init_id);
		warnx("Sending sense: %#x %#x %#x", sense->flags,
		      sense->add_sense_code, sense->add_sense_code_qual);
	}

	if (istate->orig_ca == 0) {
		tcmd_sense(ctio->init_id, NULL, SSD_KEY_NO_SENSE, 0, 0);
		warnx("REQUEST SENSE from %u but no pending CA!",
		      ctio->init_id);
	}

	bcopy(sense, ctio->data_ptr, sizeof(struct scsi_sense_data));
	dlen = offsetof(struct scsi_sense_data_fixed, extra_len) +
			sense->extra_len + 1;
	ctio->dxfer_len = min(dlen, SCSI_CDB6_LEN(rsense->length));
	ctio->ccb_h.flags |= CAM_DIR_IN | CAM_SEND_STATUS;
	ctio->scsi_status = SCSI_STATUS_OK;
	return (0);
}

static int
tcmd_rd_cap(struct ccb_accept_tio *atio, struct ccb_scsiio *ctio)
{
	struct scsi_read_capacity_data *srp;
	struct atio_descr *a_descr;
	uint32_t vsize;

	a_descr = (struct atio_descr *)atio->ccb_h.targ_descr;
	srp = (struct scsi_read_capacity_data *)ctio->data_ptr;

	if (volume_size > 0xffffffff)
		vsize = 0xffffffff;
	else
		vsize = (uint32_t)(volume_size - 1);

	if (debug) {
		cdb_debug(a_descr->cdb, "READ CAP from %u (%u, %u): ",
			  atio->init_id, vsize, sector_size);
	}

	bzero(srp, sizeof(*srp));
	scsi_ulto4b(vsize, srp->addr);
	scsi_ulto4b(sector_size, srp->length);

	ctio->dxfer_len = sizeof(*srp);
	ctio->ccb_h.flags |= CAM_DIR_IN | CAM_SEND_STATUS;
	ctio->scsi_status = SCSI_STATUS_OK;
	return (0);
}

#ifdef READ_16
static int
tcmd_rd_cap16(struct ccb_accept_tio *atio, struct ccb_scsiio *ctio)
{
	struct scsi_read_capacity_16 *scsi_cmd;
	struct scsi_read_capacity_data_long *srp;
	struct atio_descr *a_descr;

	a_descr = (struct atio_descr *)atio->ccb_h.targ_descr;
	scsi_cmd = (struct scsi_read_capacity_16 *)a_descr->cdb;
	srp = (struct scsi_read_capacity_data_long *)ctio->data_ptr;

	if (scsi_cmd->service_action != SRC16_SERVICE_ACTION) {
		tcmd_illegal_req(atio, ctio);
		return (0);
	}

	if (debug) {
		cdb_debug(a_descr->cdb, "READ CAP16 from %u (%u, %u): ",
			  atio->init_id, volume_size - 1, sector_size);
	}

	bzero(srp, sizeof(*srp));
	scsi_u64to8b(volume_size - 1, srp->addr);
	scsi_ulto4b(sector_size, srp->length);

	ctio->dxfer_len = sizeof(*srp);
	ctio->ccb_h.flags |= CAM_DIR_IN | CAM_SEND_STATUS;
	ctio->scsi_status = SCSI_STATUS_OK;
	return (0);
}
#endif

static int
tcmd_rdwr(struct ccb_accept_tio *atio, struct ccb_scsiio *ctio)
{
	struct atio_descr *a_descr;
	struct ctio_descr *c_descr;
	int ret;

	a_descr = (struct atio_descr *)atio->ccb_h.targ_descr;
	c_descr = (struct ctio_descr *)ctio->ccb_h.targ_descr;

	/* Command needs to be decoded */
	if ((a_descr->flags & CAM_DIR_MASK) == CAM_DIR_BOTH) {
		if (debug)
			warnx("Calling rdwr_decode");
		ret = tcmd_rdwr_decode(atio, ctio);
		if (ret == 0) {
			send_ccb((union ccb *)ctio, /*priority*/1);
			return (0);
		}
	}
	ctio->ccb_h.flags |= a_descr->flags;

	/* Call appropriate work function */
	if ((a_descr->flags & CAM_DIR_IN) != 0) {
		ret = start_io(atio, ctio, CAM_DIR_IN);
		if (debug)
			warnx("Starting %p DIR_IN @" OFF_FMT ":%u",
			    a_descr, c_descr->offset, a_descr->targ_req);
	} else {
		ret = start_io(atio, ctio, CAM_DIR_OUT);
		if (debug)
			warnx("Starting %p DIR_OUT @" OFF_FMT ":%u",
			    a_descr, c_descr->offset, a_descr->init_req);
	}

	return (ret);
}

static int
tcmd_rdwr_decode(struct ccb_accept_tio *atio, struct ccb_scsiio *ctio)
{
	uint64_t blkno;
	uint32_t count;
	struct atio_descr *a_descr;
	u_int8_t *cdb;

	a_descr = (struct atio_descr *)atio->ccb_h.targ_descr;
	cdb = a_descr->cdb;
	if (debug)
		cdb_debug(cdb, "R/W from %u: ", atio->init_id);

	switch (cdb[0]) {
	case READ_6:
	case WRITE_6:
	{
		struct scsi_rw_6 *rw_6 = (struct scsi_rw_6 *)cdb;
		blkno = scsi_3btoul(rw_6->addr);
		count = rw_6->length;
		break;
	}
	case READ_10:
	case WRITE_10:
	{
		struct scsi_rw_10 *rw_10 = (struct scsi_rw_10 *)cdb;
		blkno = scsi_4btoul(rw_10->addr);
		count = scsi_2btoul(rw_10->length);
		break;
	}
#ifdef READ_16
	case READ_16:
	case WRITE_16:
	{
		struct scsi_rw_16 *rw_16 = (struct scsi_rw_16 *)cdb;
		blkno = scsi_8btou64(rw_16->addr);
		count = scsi_4btoul(rw_16->length);
		break;
	}
#endif
	default:
		tcmd_illegal_req(atio, ctio);
		return (0);
	}
	if (blkno + count > volume_size) {
		warnx("Attempt to access past end of volume");
		tcmd_sense(ctio->init_id, ctio,
			   SSD_KEY_ILLEGAL_REQUEST, 0x21, 0);
		return (0);
	}

	/* Get an (overall) data length and set direction */
	a_descr->base_off = ((off_t)blkno) * sector_size;
	a_descr->total_len = count * sector_size;
	if (a_descr->total_len == 0) {
		if (debug)
			warnx("r/w 0 blocks @ blkno " OFF_FMT, blkno);
		tcmd_null_ok(atio, ctio);
		return (0);
	} else if (cdb[0] == WRITE_6 || cdb[0] == WRITE_10) {
		a_descr->flags |= CAM_DIR_OUT;
		if (debug)
			warnx("write %u blocks @ blkno " OFF_FMT, count, blkno);
	} else {
		a_descr->flags |= CAM_DIR_IN;
		if (debug)
			warnx("read %u blocks @ blkno " OFF_FMT,  count, blkno);
	}
	return (1);
}

static int
start_io(struct ccb_accept_tio *atio, struct ccb_scsiio *ctio, int dir)
{
	struct atio_descr *a_descr;
	struct ctio_descr *c_descr;
	int ret;

	/* Set up common structures */
	a_descr = (struct atio_descr *)atio->ccb_h.targ_descr;
	c_descr = (struct ctio_descr *)ctio->ccb_h.targ_descr;

	if (dir == CAM_DIR_IN) {
		c_descr->offset = a_descr->base_off + a_descr->targ_req;
		ctio->dxfer_len = a_descr->total_len - a_descr->targ_req;
	} else {
		c_descr->offset = a_descr->base_off + a_descr->init_req;
		ctio->dxfer_len = a_descr->total_len - a_descr->init_req;
	}
	ctio->dxfer_len = min(ctio->dxfer_len, buf_size);
	assert(ctio->dxfer_len >= 0);

	c_descr->aiocb.aio_offset = c_descr->offset;
	c_descr->aiocb.aio_nbytes = ctio->dxfer_len;

	/* If DIR_IN, start read from target, otherwise begin CTIO xfer. */
	ret = 1;
	if (dir == CAM_DIR_IN) {
		if (notaio) {
			if (debug)
				warnx("read sync %lu @ block " OFF_FMT,
				    (unsigned long)
				    (ctio->dxfer_len / sector_size),
				    c_descr->offset / sector_size);
			if (lseek(c_descr->aiocb.aio_fildes,
			    c_descr->aiocb.aio_offset, SEEK_SET) < 0) {
				perror("lseek");
				err(1, "lseek");
			}
			if (read(c_descr->aiocb.aio_fildes,
			    (void *)c_descr->aiocb.aio_buf,
			    ctio->dxfer_len) != ctio->dxfer_len) {
				err(1, "read");
			}
		} else {
			if (debug)
				warnx("read async %lu @ block " OFF_FMT,
				    (unsigned long)
				    (ctio->dxfer_len / sector_size),
				    c_descr->offset / sector_size);
			if (aio_read(&c_descr->aiocb) < 0) {
				err(1, "aio_read"); /* XXX */
			}
		}
		a_descr->targ_req += ctio->dxfer_len;
		/* if we're done, we can mark the CCB as to send status */
		if (a_descr->targ_req == a_descr->total_len) {
			ctio->ccb_h.flags |= CAM_SEND_STATUS;
			ctio->scsi_status = SCSI_STATUS_OK;
			ret = 0;
		}
		if (notaio)
			tcmd_rdwr_done(atio, ctio, AIO_DONE);
	} else {
		if (a_descr->targ_ack == a_descr->total_len)
			tcmd_null_ok(atio, ctio);
		a_descr->init_req += ctio->dxfer_len;
		if (a_descr->init_req == a_descr->total_len &&
		    ctio->dxfer_len > 0) {
			/*
			 * If data phase done, remove atio from workq.
			 * The completion handler will call work_atio to
			 * send the final status.
			 */
			ret = 0;
		}
		send_ccb((union ccb *)ctio, /*priority*/1);
	}

	return (ret);
}

static void
tcmd_rdwr_done(struct ccb_accept_tio *atio, struct ccb_scsiio *ctio,
	       io_ops event)
{
	struct atio_descr *a_descr;
	struct ctio_descr *c_descr;

	a_descr = (struct atio_descr *)atio->ccb_h.targ_descr;
	c_descr = (struct ctio_descr *)ctio->ccb_h.targ_descr;

	switch (event) {
	case AIO_DONE:
		if (!notaio && aio_return(&c_descr->aiocb) < 0) {
			warn("aio_return error");
			/* XXX */
			tcmd_sense(ctio->init_id, ctio,
				   SSD_KEY_MEDIUM_ERROR, 0, 0);
			send_ccb((union ccb *)ctio, /*priority*/1);
			break;
		}
		a_descr->targ_ack += ctio->dxfer_len;
		if ((a_descr->flags & CAM_DIR_IN) != 0) {
			if (debug) {
				if (notaio)
					warnx("sending CTIO for AIO read");
				else
					warnx("sending CTIO for sync read");
			}
			a_descr->init_req += ctio->dxfer_len;
			send_ccb((union ccb *)ctio, /*priority*/1);
		} else {
			/* Use work function to send final status */
			if (a_descr->init_req == a_descr->total_len)
				work_atio(atio);
			if (debug)
				warnx("AIO done freeing CTIO");
			free_ccb((union ccb *)ctio);
		}
		break;
	case CTIO_DONE:
		switch (ctio->ccb_h.status & CAM_STATUS_MASK) {
		case CAM_REQ_CMP:
			break;
		case CAM_REQUEUE_REQ:
			warnx("requeueing request");
			if ((a_descr->flags & CAM_DIR_MASK) == CAM_DIR_OUT) {
				if (aio_write(&c_descr->aiocb) < 0) {
					err(1, "aio_write"); /* XXX */
				}
			} else {
				if (aio_read(&c_descr->aiocb) < 0) {
					err(1, "aio_read"); /* XXX */
				}
			}
			return;
		default:
			errx(1, "CTIO failed, status %#x", ctio->ccb_h.status);
		}
		a_descr->init_ack += ctio->dxfer_len;
		if ((a_descr->flags & CAM_DIR_MASK) == CAM_DIR_OUT &&
		    ctio->dxfer_len > 0) {
			a_descr->targ_req += ctio->dxfer_len;
			if (notaio) {
				if (debug)
					warnx("write sync %lu @ block "
					    OFF_FMT, (unsigned long)
					    (ctio->dxfer_len / sector_size),
					    c_descr->offset / sector_size);
				if (lseek(c_descr->aiocb.aio_fildes,
				    c_descr->aiocb.aio_offset, SEEK_SET) < 0) {
					perror("lseek");
					err(1, "lseek");
				}
				if (write(c_descr->aiocb.aio_fildes,
				    (void *) c_descr->aiocb.aio_buf,
				    ctio->dxfer_len) != ctio->dxfer_len) {
					err(1, "write");
				}
				tcmd_rdwr_done(atio, ctio, AIO_DONE);
			} else {
				if (debug)
					warnx("write async %lu @ block "
					    OFF_FMT, (unsigned long)
					    (ctio->dxfer_len / sector_size),
					    c_descr->offset / sector_size);
				if (aio_write(&c_descr->aiocb) < 0) {
					err(1, "aio_write"); /* XXX */
				}
			}
		} else {
			if (debug)
				warnx("CTIO done freeing CTIO");
			free_ccb((union ccb *)ctio);
		}
		break;
	default:
		warnx("Unknown completion code %d", event);
		abort();
		/* NOTREACHED */
	}
}

/* Simple ok message used by TUR, SYNC_CACHE, etc. */
static int
tcmd_null_ok(struct ccb_accept_tio *atio, struct ccb_scsiio *ctio)
{
	if (debug) {
		struct atio_descr *a_descr;

		a_descr = (struct atio_descr *)atio->ccb_h.targ_descr;
		cdb_debug(a_descr->cdb, "Sending null ok to %u : ", atio->init_id);
	}

	ctio->dxfer_len = 0;
	ctio->ccb_h.flags &= ~CAM_DIR_MASK;
	ctio->ccb_h.flags |= CAM_DIR_NONE | CAM_SEND_STATUS;
	ctio->scsi_status = SCSI_STATUS_OK;
	return (0);
}

/* Simple illegal request message used by MODE SENSE, etc. */
static int
tcmd_illegal_req(struct ccb_accept_tio *atio, struct ccb_scsiio *ctio)
{
	if (debug) {
		struct atio_descr *a_descr;

		a_descr = (struct atio_descr *)atio->ccb_h.targ_descr;
		cdb_debug(a_descr->cdb, "Sending ill req to %u: ", atio->init_id);
	}
	
	tcmd_sense(atio->init_id, ctio, SSD_KEY_ILLEGAL_REQUEST,
		   /*asc*/0x24, /*ascq*/0);
	return (0);
}

static void
cdb_debug(u_int8_t *cdb, const char *msg, ...)
{
	char msg_buf[512];
	int len;
	va_list ap;

	va_start(ap, msg);
	vsnprintf(msg_buf, sizeof(msg_buf), msg, ap);
	va_end(ap);
	len = strlen(msg_buf);
	scsi_cdb_string(cdb, msg_buf + len, sizeof(msg_buf) - len);
	warnx("%s", msg_buf);
}
