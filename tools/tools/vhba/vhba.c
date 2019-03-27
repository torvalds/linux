/*-
 * Copyright (c) 2010 by Panasas, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
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
 */
/* $FreeBSD$ */
/*
 * Virtual HBA infrastructure, to be used for testing as well as other cute hacks.
 */
#include "vhba.h"
static vhba_softc_t *vhba;

#ifndef	VHBA_MOD
#define	VHBA_MOD	"vhba"
#endif

static void vhba_action(struct cam_sim *, union ccb *);
static void vhba_poll(struct cam_sim *);

static int
vhba_attach(vhba_softc_t *vhba)
{
	TAILQ_INIT(&vhba->actv);
	TAILQ_INIT(&vhba->done);
	vhba->devq = cam_simq_alloc(VHBA_MAXCMDS);
	if (vhba->devq == NULL) {
		return (ENOMEM);
	}
	vhba->sim = cam_sim_alloc(vhba_action, vhba_poll, VHBA_MOD, vhba, 0, &vhba->lock, VHBA_MAXCMDS, VHBA_MAXCMDS, vhba->devq);
	if (vhba->sim == NULL) {
		cam_simq_free(vhba->devq);
		return (ENOMEM);
	}
	vhba_init(vhba);
	mtx_lock(&vhba->lock);
	if (xpt_bus_register(vhba->sim, 0, 0) != CAM_SUCCESS) {
		cam_sim_free(vhba->sim, TRUE);
		mtx_unlock(&vhba->lock);
		return (EIO);
	}
	mtx_unlock(&vhba->lock);
	return (0);
}

static void
vhba_detach(vhba_softc_t *vhba)
{
	/*
	 * We can't be called with anything queued up.
	 */
	vhba_fini(vhba);
	xpt_bus_deregister(cam_sim_path(vhba->sim));
	cam_sim_free(vhba->sim, TRUE);
}

static void
vhba_poll(struct cam_sim *sim)
{
	vhba_softc_t *vhba = cam_sim_softc(sim);
	vhba_kick(vhba);
}

static void
vhba_action(struct cam_sim *sim, union ccb *ccb)
{
	struct ccb_trans_settings *cts;
	vhba_softc_t *vhba;

	vhba = cam_sim_softc(sim);
	if (vhba->private == NULL) {
		ccb->ccb_h.status = CAM_REQ_CMP_ERR;
		xpt_done(ccb);
		return;
	}
	switch (ccb->ccb_h.func_code) {
	case XPT_SCSI_IO:
		ccb->ccb_h.status &= ~CAM_STATUS_MASK;
		ccb->ccb_h.status |= CAM_REQ_INPROG;
		TAILQ_INSERT_TAIL(&vhba->actv, &ccb->ccb_h, sim_links.tqe);
		vhba_kick(vhba);
		return;

	case XPT_RESET_DEV:
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;

	case XPT_GET_TRAN_SETTINGS:
		cts = &ccb->cts;
		cts->protocol_version = SCSI_REV_SPC3;
		cts->protocol = PROTO_SCSI;
		cts->transport_version = 0;
		cts->transport = XPORT_PPB;
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;

	case XPT_CALC_GEOMETRY:
		cam_calc_geometry(&ccb->ccg, 1);
		break;

	case XPT_RESET_BUS:		/* Reset the specified bus */
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;

	case XPT_PATH_INQ:		/* Path routing inquiry */
	{
		struct ccb_pathinq *cpi = &ccb->cpi;

		cpi->version_num = 1;
		cpi->max_target = VHBA_MAXTGT - 1;
		cpi->max_lun = 16383;
		cpi->hba_misc = PIM_NOBUSRESET;
		cpi->initiator_id = cpi->max_target + 1;
		cpi->transport = XPORT_PPB;
		cpi->base_transfer_speed = 1000000;
		cpi->protocol = PROTO_SCSI;
		cpi->protocol_version = SCSI_REV_SPC3;
		strlcpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
		strlcpy(cpi->hba_vid, "FakeHBA", HBA_IDLEN);
		strlcpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
		cpi->unit_number = cam_sim_unit(sim);
		cpi->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	default:
		ccb->ccb_h.status = CAM_REQ_INVALID;
		break;
	}
	xpt_done(ccb);
}

/*
 * Common support
 */
void
vhba_fill_sense(struct ccb_scsiio *csio, uint8_t key, uint8_t asc, uint8_t ascq)
{
	csio->ccb_h.status = CAM_SCSI_STATUS_ERROR|CAM_AUTOSNS_VALID;
	csio->scsi_status = SCSI_STATUS_CHECK_COND;
	csio->sense_data.error_code = SSD_ERRCODE_VALID|SSD_CURRENT_ERROR;
	csio->sense_data.flags = key;
	csio->sense_data.extra_len = 10;
	csio->sense_data.add_sense_code = asc;
	csio->sense_data.add_sense_code_qual = ascq;
	csio->sense_len = sizeof (csio->sense_data);
}

int
vhba_rwparm(uint8_t *cdb, uint64_t *offset, uint32_t *tl, uint64_t nblks, uint32_t blk_shift)
{
	uint32_t cnt;
	uint64_t lba;

	switch (cdb[0]) {
	case WRITE_16:
	case READ_16:
		cnt =	(((uint32_t)cdb[10]) <<  24) |
			(((uint32_t)cdb[11]) <<  16) |
			(((uint32_t)cdb[12]) <<   8) |
			((uint32_t)cdb[13]);

		lba =	(((uint64_t)cdb[2]) << 56) |
			(((uint64_t)cdb[3]) << 48) |
			(((uint64_t)cdb[4]) << 40) |
			(((uint64_t)cdb[5]) << 32) |
			(((uint64_t)cdb[6]) << 24) |
			(((uint64_t)cdb[7]) << 16) |
			(((uint64_t)cdb[8]) <<  8) |
			((uint64_t)cdb[9]);
		break;
	case WRITE_12:
	case READ_12:
		cnt =	(((uint32_t)cdb[6]) <<  16) |
			(((uint32_t)cdb[7]) <<   8) |
			((u_int32_t)cdb[8]);

		lba =	(((uint32_t)cdb[2]) << 24) |
			(((uint32_t)cdb[3]) << 16) |
			(((uint32_t)cdb[4]) <<  8) |
			((uint32_t)cdb[5]);
		break;
	case WRITE_10:
	case READ_10:
		cnt =	(((uint32_t)cdb[7]) <<  8) |
			((u_int32_t)cdb[8]);

		lba =	(((uint32_t)cdb[2]) << 24) |
			(((uint32_t)cdb[3]) << 16) |
			(((uint32_t)cdb[4]) <<  8) |
			((uint32_t)cdb[5]);
		break;
	case WRITE_6:
	case READ_6:
		cnt = cdb[4];
		if (cnt == 0) {
			cnt = 256;
		}
		lba =	(((uint32_t)cdb[1] & 0x1f) << 16) |
			(((uint32_t)cdb[2]) << 8) |
			((uint32_t)cdb[3]);
		break;
	default:
		return (-1);
	}

	if (lba + cnt > nblks) {
		return (-1);
	}
	*tl = cnt << blk_shift;
	*offset = lba << blk_shift;
	return (0);
}

void
vhba_default_cmd(struct ccb_scsiio *csio, lun_id_t max_lun, uint8_t *sparse_lun_map)
{
	char junk[128];
	const uint8_t niliqd[SHORT_INQUIRY_LENGTH] = {
		0x7f, 0x0, SCSI_REV_SPC3, 0x2, 32, 0, 0, 0x32,
		'P', 'A', 'N', 'A', 'S', 'A', 'S', ' ',
		'N', 'U', 'L', 'L', ' ', 'D', 'E', 'V',
		'I', 'C', 'E', ' ', ' ', ' ', ' ', ' ',
		'0', '0', '0', '1'
	};
	const uint8_t iqd[SHORT_INQUIRY_LENGTH] = {
		0, 0x0, SCSI_REV_SPC3, 0x2, 32, 0, 0, 0x32,
		'P', 'A', 'N', 'A', 'S', 'A', 'S', ' ',
		'V', 'I', 'R', 'T', ' ', 'M', 'E', 'M',
		'O', 'R', 'Y', ' ', 'D', 'I', 'S', 'K',
		'0', '0', '0', '1'
	};
	const uint8_t vp0data[6] = { 0, 0, 0, 0x2, 0, 0x80 };
	const uint8_t vp80data[36] = { 0, 0x80, 0, 0x20 };
	int i, attached_lun;
	uint8_t *cdb, *ptr, status;
	uint32_t data_len, nlun;

	data_len = 0;
	status = SCSI_STATUS_OK;

	memset(&csio->sense_data, 0, sizeof (csio->sense_data));
	cdb = csio->cdb_io.cdb_bytes;

	attached_lun = 1;
	if (csio->ccb_h.target_lun >= max_lun) {
		attached_lun = 0;
	} else if (sparse_lun_map) {
		i = csio->ccb_h.target_lun & 0x7;
		if ((sparse_lun_map[csio->ccb_h.target_lun >> 3] & (1 << i)) == 0) {
			attached_lun = 0;
		}
	}
	if (attached_lun == 0 && cdb[0] != INQUIRY && cdb[0] != REPORT_LUNS && cdb[0] != REQUEST_SENSE) {
		vhba_fill_sense(csio, SSD_KEY_ILLEGAL_REQUEST, 0x25, 0x0);
		return;
	}

	switch (cdb[0]) {
	case REQUEST_SENSE:
		data_len = csio->dxfer_len;
		if (cdb[4] < csio->dxfer_len)
			data_len = cdb[4];
		if (data_len) {
			memset(junk, 0, sizeof (junk));
			junk[0] = SSD_ERRCODE_VALID|SSD_CURRENT_ERROR;
			junk[2] = SSD_KEY_NO_SENSE;
			junk[7] = 10;
			memcpy(csio->data_ptr, junk,
			    (data_len > sizeof junk)? sizeof junk : data_len);
		}
		csio->resid = csio->dxfer_len - data_len;
		break;
	case INQUIRY:
		i = 0;
		if ((cdb[1] & 0x1f) == SI_EVPD) {
			if ((cdb[2] != 0 && cdb[2] != 0x80) || cdb[3] || cdb[5]) {
				i = 1;
			}
		} else if ((cdb[1] & 0x1f) || cdb[2] || cdb[3] || cdb[5]) {
			i = 1;
		}
		if (i) {
			vhba_fill_sense(csio, SSD_KEY_ILLEGAL_REQUEST, 0x24, 0x0);
			break;
		}
		if (attached_lun == 0) {
			if (cdb[1] & 0x1f) {
				vhba_fill_sense(csio, SSD_KEY_ILLEGAL_REQUEST, 0x24, 0x0);
				break;
			}
			memcpy(junk, niliqd, sizeof (niliqd));
			data_len = sizeof (niliqd);
		} else if (cdb[1] & 0x1f) {
			if (cdb[2] == 0) {
				memcpy(junk, vp0data, sizeof (vp0data));
				data_len = sizeof (vp0data);
			} else {
				memcpy(junk, vp80data, sizeof (vp80data));
				snprintf(&junk[4], sizeof (vp80data) - 4, "TGT%dLUN%d", csio->ccb_h.target_id, csio->ccb_h.target_lun);
				for (i = 0; i < sizeof (vp80data); i++) {
					if (junk[i] == 0) {
						junk[i] = ' ';
					}
                                }
                        }
			data_len = sizeof (vp80data);
		} else {
			memcpy(junk, iqd, sizeof (iqd));
			data_len = sizeof (iqd);
		}
		if (data_len > cdb[4]) {
			data_len = cdb[4];
		}
		if (data_len) {
			memcpy(csio->data_ptr, junk, data_len);
		}
		csio->resid = csio->dxfer_len - data_len;
		break;
	case TEST_UNIT_READY:
	case SYNCHRONIZE_CACHE:
	case START_STOP:
	case RESERVE:
	case RELEASE:
		break;

	case REPORT_LUNS:
		if (csio->dxfer_len) {
			memset(csio->data_ptr, 0, csio->dxfer_len);
		}
		ptr = NULL;
		for (nlun = i = 0; i < max_lun; i++) {
			if (sparse_lun_map) {
				if ((sparse_lun_map[i >> 3] & (1 << (i & 0x7))) == 0) {
					continue;
				}
			}
			ptr = &csio->data_ptr[8 + ((nlun++) << 3)];
			if ((ptr + 8) > &csio->data_ptr[csio->dxfer_len]) {
				continue;
			}
			if (i >= 256) {
				ptr[0] = 0x40 | ((i >> 8) & 0x3f);
			}
			ptr[1] = i;
		}
		junk[0] = (nlun << 3) >> 24;
		junk[1] = (nlun << 3) >> 16;
		junk[2] = (nlun << 3) >> 8;
		junk[3] = (nlun << 3);
		memset(junk+4, 0, 4);
		if (csio->dxfer_len) {
			u_int amt;

			amt = MIN(csio->dxfer_len, 8);
			memcpy(csio->data_ptr, junk, amt);
			amt = MIN((nlun << 3) + 8,  csio->dxfer_len);
			csio->resid = csio->dxfer_len - amt;
		}
		break;

	default:
		vhba_fill_sense(csio, SSD_KEY_ILLEGAL_REQUEST, 0x20, 0x0);
		break;
	}
}

void
vhba_set_status(struct ccb_hdr *ccbh, cam_status status)
{
	ccbh->status &= ~CAM_STATUS_MASK;
	ccbh->status |= status;
	if (status != CAM_REQ_CMP) {
		if ((ccbh->status & CAM_DEV_QFRZN) == 0) {
			ccbh->status |= CAM_DEV_QFRZN;
			xpt_freeze_devq(ccbh->path, 1);
		}
	}
}

int
vhba_modprobe(module_t mod, int cmd, void *arg)
{
	int error = 0;

	switch (cmd) {
	case MOD_LOAD:
		vhba = malloc(sizeof (*vhba), M_DEVBUF, M_WAITOK|M_ZERO);
		mtx_init(&vhba->lock, "vhba", NULL, MTX_DEF);
		error = vhba_attach(vhba);
		if (error) {
			mtx_destroy(&vhba->lock);
			free(vhba, M_DEVBUF);
		}
		break;
	case MOD_UNLOAD:
        	mtx_lock(&vhba->lock);
		if (TAILQ_FIRST(&vhba->done) || TAILQ_FIRST(&vhba->actv)) {
			error = EBUSY;
			mtx_unlock(&vhba->lock);
			break;
		}
		vhba_detach(vhba);
		mtx_unlock(&vhba->lock);
		mtx_destroy(&vhba->lock);
		free(vhba, M_DEVBUF);
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
	return (error);
}
