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
 * "Faulty" Multipath Device. Creates to devices to be set up as multipath,
 * makes one or both of them non existent (or re existent) on demand.
 */
#include "vhba.h"
#include <sys/sysctl.h>

static int vhba_stop_lun;
static int vhba_start_lun = 0;
static int vhba_notify_stop = 1;
static int vhba_notify_start = 1;
static int vhba_inject_hwerr = 0;
SYSCTL_INT(_debug, OID_AUTO, vhba_stop_lun, CTLFLAG_RW, &vhba_stop_lun, 0, "stop lun bitmap");
SYSCTL_INT(_debug, OID_AUTO, vhba_start_lun, CTLFLAG_RW, &vhba_start_lun, 0, "start lun bitmap");
SYSCTL_INT(_debug, OID_AUTO, vhba_notify_stop, CTLFLAG_RW, &vhba_notify_stop, 1, "notify when luns go away");
SYSCTL_INT(_debug, OID_AUTO, vhba_notify_start, CTLFLAG_RW, &vhba_notify_start, 1, "notify when luns arrive");
SYSCTL_INT(_debug, OID_AUTO, vhba_inject_hwerr, CTLFLAG_RW, &vhba_inject_hwerr, 0, "inject hardware error on lost luns");

#define	MAX_TGT		1
#define	MAX_LUN		2
#define	VMP_TIME	hz

#define	DISK_SIZE	32
#define	DISK_SHIFT	9
#define	DISK_NBLKS	((DISK_SIZE << 20) >> DISK_SHIFT)
#define	PSEUDO_SPT	64
#define	PSEUDO_HDS	64
#define	PSEUDO_SPC	(PSEUDO_SPT * PSEUDO_HDS)

typedef struct {
	vhba_softc_t *	vhba;
	uint8_t *	disk;
	size_t		disk_size;
	int		luns[2];
	struct callout	tick;
	struct task	qt;
	TAILQ_HEAD(, ccb_hdr)   inproc;
	int		nact, nact_high;
} mptest_t;

static timeout_t vhba_iodelay;
static timeout_t vhba_timer;
static void vhba_task(void *, int);
static void mptest_act(mptest_t *, struct ccb_scsiio *);

void
vhba_init(vhba_softc_t *vhba)
{
	static mptest_t vhbastatic;

	vhbastatic.vhba = vhba;
	vhbastatic.disk_size = DISK_SIZE << 20;
	vhbastatic.disk = malloc(vhbastatic.disk_size, M_DEVBUF, M_WAITOK|M_ZERO);
	vhba->private = &vhbastatic;
	callout_init_mtx(&vhbastatic.tick, &vhba->lock, 0);
	callout_reset(&vhbastatic.tick, VMP_TIME, vhba_timer, vhba);
	TAILQ_INIT(&vhbastatic.inproc);
	TASK_INIT(&vhbastatic.qt, 0, vhba_task, &vhbastatic);
	vhbastatic.luns[0] = 1;
	vhbastatic.luns[1] = 1;
}

void
vhba_fini(vhba_softc_t *vhba)
{
	mptest_t *vhbas = vhba->private;
	callout_stop(&vhbas->tick);
	vhba->private = NULL;
	free(vhbas->disk, M_DEVBUF);
}

void
vhba_kick(vhba_softc_t *vhba)
{
	mptest_t *vhbas = vhba->private;
	taskqueue_enqueue(taskqueue_swi, &vhbas->qt);
}

static void
vhba_task(void *arg, int pending)
{
	mptest_t *vhbas = arg;
	struct ccb_hdr *ccbh;
	int nadded = 0;

	mtx_lock(&vhbas->vhba->lock);
	while ((ccbh = TAILQ_FIRST(&vhbas->vhba->actv)) != NULL) {
		TAILQ_REMOVE(&vhbas->vhba->actv, ccbh, sim_links.tqe);
                mptest_act(vhbas, (struct ccb_scsiio *)ccbh);
		nadded++;
		ccbh->sim_priv.entries[0].ptr = vhbas;
		callout_handle_init(&ccbh->timeout_ch);
	}
	if (nadded) {
		vhba_kick(vhbas->vhba);
	} else {
		while ((ccbh = TAILQ_FIRST(&vhbas->vhba->done)) != NULL) {
			TAILQ_REMOVE(&vhbas->vhba->done, ccbh, sim_links.tqe);
			xpt_done((union ccb *)ccbh);
		}
	}
	mtx_unlock(&vhbas->vhba->lock);
}

static void
mptest_act(mptest_t *vhbas, struct ccb_scsiio *csio)
{
	char junk[128];
	cam_status camstatus;
	uint8_t *cdb, *ptr, status;
	uint32_t data_len, blkcmd;
	uint64_t off;
	    
	blkcmd = data_len = 0;
	status = SCSI_STATUS_OK;

	memset(&csio->sense_data, 0, sizeof (csio->sense_data));
	cdb = csio->cdb_io.cdb_bytes;

	if (csio->ccb_h.target_id >= MAX_TGT) {
		vhba_set_status(&csio->ccb_h, CAM_SEL_TIMEOUT);
		TAILQ_INSERT_TAIL(&vhbas->vhba->done, &csio->ccb_h, sim_links.tqe);
		return;
	}
	if (vhba_inject_hwerr && csio->ccb_h.target_lun < MAX_LUN && vhbas->luns[csio->ccb_h.target_lun] == 0) {
		vhba_fill_sense(csio, SSD_KEY_HARDWARE_ERROR, 0x44, 0x0);
		TAILQ_INSERT_TAIL(&vhbas->vhba->done, &csio->ccb_h, sim_links.tqe);
		return;
	}
	if ((csio->ccb_h.target_lun >= MAX_LUN || vhbas->luns[csio->ccb_h.target_lun] == 0) && cdb[0] != INQUIRY && cdb[0] != REPORT_LUNS && cdb[0] != REQUEST_SENSE) {
		vhba_fill_sense(csio, SSD_KEY_ILLEGAL_REQUEST, 0x25, 0x0);
		TAILQ_INSERT_TAIL(&vhbas->vhba->done, &csio->ccb_h, sim_links.tqe);
		return;
	}

	switch (cdb[0]) {
	case MODE_SENSE:
	case MODE_SENSE_10:
	{
		unsigned int nbyte;
		uint8_t page = cdb[2] & SMS_PAGE_CODE;
		uint8_t pgctl = cdb[2] & SMS_PAGE_CTRL_MASK;

		switch (page) {
		case SMS_FORMAT_DEVICE_PAGE:
		case SMS_GEOMETRY_PAGE:
		case SMS_CACHE_PAGE:
		case SMS_CONTROL_MODE_PAGE:
		case SMS_ALL_PAGES_PAGE:
			break;
		default:
			vhba_fill_sense(csio, SSD_KEY_ILLEGAL_REQUEST, 0x24, 0x0);
			TAILQ_INSERT_TAIL(&vhbas->vhba->done, &csio->ccb_h, sim_links.tqe);
			return;
		}
		memset(junk, 0, sizeof (junk));
		if (cdb[1] & SMS_DBD) {
			ptr = &junk[4];
		} else {
			ptr = junk;
			ptr[3] = 8;
			ptr[4] = ((1 << DISK_SHIFT) >> 24) & 0xff;
			ptr[5] = ((1 << DISK_SHIFT) >> 16) & 0xff;
			ptr[6] = ((1 << DISK_SHIFT) >>  8) & 0xff;
			ptr[7] = ((1 << DISK_SHIFT)) & 0xff;

			ptr[8] = (DISK_NBLKS >> 24) & 0xff;
			ptr[9] = (DISK_NBLKS >> 16) & 0xff;
			ptr[10] = (DISK_NBLKS >> 8) & 0xff;
			ptr[11] = DISK_NBLKS & 0xff;
			ptr += 12;
		}

		if (page == SMS_ALL_PAGES_PAGE || page == SMS_FORMAT_DEVICE_PAGE) {
			ptr[0] = SMS_FORMAT_DEVICE_PAGE;
			ptr[1] = 24;
			if (pgctl != SMS_PAGE_CTRL_CHANGEABLE) {
				/* tracks per zone */
				/* ptr[2] = 0; */
				/* ptr[3] = 0; */
				/* alternate sectors per zone */
				/* ptr[4] = 0; */
				/* ptr[5] = 0; */
				/* alternate tracks per zone */
				/* ptr[6] = 0; */
				/* ptr[7] = 0; */
				/* alternate tracks per logical unit */
				/* ptr[8] = 0; */
				/* ptr[9] = 0; */
				/* sectors per track */
				ptr[10] = (PSEUDO_SPT >> 8) & 0xff;
				ptr[11] = PSEUDO_SPT & 0xff;
				/* data bytes per physical sector */
				ptr[12] = ((1 << DISK_SHIFT) >> 8) & 0xff;
				ptr[13] = (1 << DISK_SHIFT) & 0xff;
				/* interleave */
				/* ptr[14] = 0; */
				/* ptr[15] = 1; */
				/* track skew factor */
				/* ptr[16] = 0; */
				/* ptr[17] = 0; */
				/* cylinder skew factor */
				/* ptr[18] = 0; */
				/* ptr[19] = 0; */
				/* SSRC, HSEC, RMB, SURF */
			}
			ptr += 26;
		}

		if (page == SMS_ALL_PAGES_PAGE || page == SMS_GEOMETRY_PAGE) {
			ptr[0] = SMS_GEOMETRY_PAGE;
			ptr[1] = 24;
			if (pgctl != SMS_PAGE_CTRL_CHANGEABLE) {
				uint32_t cyl = (DISK_NBLKS + ((PSEUDO_SPC - 1))) / PSEUDO_SPC;
				/* number of cylinders */
				ptr[2] = (cyl >> 24) & 0xff;
				ptr[3] = (cyl >> 16) & 0xff;
				ptr[4] = cyl & 0xff;
				/* number of heads */
				ptr[5] = PSEUDO_HDS;
				/* starting cylinder- write precompensation */
				/* ptr[6] = 0; */
				/* ptr[7] = 0; */
				/* ptr[8] = 0; */
				/* starting cylinder- reduced write current */
				/* ptr[9] = 0; */
				/* ptr[10] = 0; */
				/* ptr[11] = 0; */
				/* drive step rate */
				/* ptr[12] = 0; */
				/* ptr[13] = 0; */
				/* landing zone cylinder */
				/* ptr[14] = 0; */
				/* ptr[15] = 0; */
				/* ptr[16] = 0; */
				/* RPL */
				/* ptr[17] = 0; */
				/* rotational offset */
				/* ptr[18] = 0; */
				/* medium rotation rate -  7200 RPM */
				ptr[20] = 0x1c;
				ptr[21] = 0x20;
			}
			ptr += 26;
		}

		if (page == SMS_ALL_PAGES_PAGE || page == SMS_CACHE_PAGE) {
			ptr[0] = SMS_CACHE_PAGE;
			ptr[1] = 18;
			ptr[2] = 1 << 2;
			ptr += 20;
		}

		if (page == SMS_ALL_PAGES_PAGE || page == SMS_CONTROL_MODE_PAGE) {
			ptr[0] = SMS_CONTROL_MODE_PAGE;
			ptr[1] = 10;
			if (pgctl != SMS_PAGE_CTRL_CHANGEABLE) {
				ptr[3] = 1 << 4; /* unrestricted reordering allowed */
				ptr[8] = 0x75;   /* 30000 ms */
				ptr[9] = 0x30;
			}
			ptr += 12;
		}
		nbyte = (char *)ptr - &junk[0];
		ptr[0] = nbyte - 4;

		if (cdb[0] == MODE_SENSE) {
			data_len = min(cdb[4], csio->dxfer_len);
		} else {
			uint16_t tw = (cdb[7] << 8) | cdb[8];
			data_len = min(tw, csio->dxfer_len);
		}
		data_len = min(data_len, nbyte);
		if (data_len) {
			memcpy(csio->data_ptr, junk, data_len);
		}
		csio->resid = csio->dxfer_len - data_len;
		break;
	}
	case READ_6:
	case READ_10:
	case READ_12:
	case READ_16:
	case WRITE_6:
	case WRITE_10:
	case WRITE_12:
	case WRITE_16:
		if (vhba_rwparm(cdb, &off, &data_len, DISK_NBLKS, DISK_SHIFT)) {
			vhba_fill_sense(csio, SSD_KEY_ILLEGAL_REQUEST, 0x24, 0x0);
			break;
		}
		blkcmd++;
		if (++vhbas->nact > vhbas->nact_high) {
			vhbas->nact_high = vhbas->nact;
			printf("%s: high block count now %d\n", __func__, vhbas->nact);
		}
		if (data_len) {
			if ((cdb[0] & 0xf) == 8) {
				memcpy(csio->data_ptr, &vhbas->disk[off], data_len);
			} else {
				memcpy(&vhbas->disk[off], csio->data_ptr, data_len);
			}
			csio->resid = csio->dxfer_len - data_len;
		} else {
			csio->resid = csio->dxfer_len;
		}
		break;

	case READ_CAPACITY:
		if (cdb[2] || cdb[3] || cdb[4] || cdb[5]) {
			vhba_fill_sense(csio, SSD_KEY_UNIT_ATTENTION, 0x24, 0x0);
			break;
		}
		if (cdb[8] & 0x1) { /* PMI */
			csio->data_ptr[0] = 0xff;
			csio->data_ptr[1] = 0xff;
			csio->data_ptr[2] = 0xff;
			csio->data_ptr[3] = 0xff;
		} else {
			uint64_t last_blk = DISK_NBLKS - 1;
			if (last_blk < 0xffffffffULL) {
			    csio->data_ptr[0] = (last_blk >> 24) & 0xff;
			    csio->data_ptr[1] = (last_blk >> 16) & 0xff;
			    csio->data_ptr[2] = (last_blk >>  8) & 0xff;
			    csio->data_ptr[3] = (last_blk) & 0xff;
			} else {
			    csio->data_ptr[0] = 0xff;
			    csio->data_ptr[1] = 0xff;
			    csio->data_ptr[2] = 0xff;
			    csio->data_ptr[3] = 0xff;
			}
		}
		csio->data_ptr[4] = ((1 << DISK_SHIFT) >> 24) & 0xff;
		csio->data_ptr[5] = ((1 << DISK_SHIFT) >> 16) & 0xff;
		csio->data_ptr[6] = ((1 << DISK_SHIFT) >>  8) & 0xff;
		csio->data_ptr[7] = ((1 << DISK_SHIFT)) & 0xff;
		break;
	default:
		vhba_default_cmd(csio, MAX_LUN, NULL);
		break;
	}
	if (csio->scsi_status != SCSI_STATUS_OK) {
		camstatus = CAM_SCSI_STATUS_ERROR;
		if (csio->scsi_status == SCSI_STATUS_CHECK_COND) {
			camstatus |= CAM_AUTOSNS_VALID;
		}
	} else {
		csio->scsi_status = SCSI_STATUS_OK;
		camstatus = CAM_REQ_CMP;
	}
	vhba_set_status(&csio->ccb_h, camstatus);
	if (blkcmd) {
		int ticks;
		struct timeval t;

		TAILQ_INSERT_TAIL(&vhbas->inproc, &csio->ccb_h, sim_links.tqe);
		t.tv_sec = 0;
		t.tv_usec = (500 + arc4random());
		if (t.tv_usec > 10000) {
			t.tv_usec = 10000;
		}
		ticks = tvtohz(&t);
		csio->ccb_h.timeout_ch = timeout(vhba_iodelay, &csio->ccb_h, ticks);
	} else {
		TAILQ_INSERT_TAIL(&vhbas->vhba->done, &csio->ccb_h, sim_links.tqe);
	}
}

static void
vhba_iodelay(void *arg)
{
	struct ccb_hdr *ccbh = arg;
	mptest_t *vhbas = ccbh->sim_priv.entries[0].ptr;

	mtx_lock(&vhbas->vhba->lock);
	TAILQ_REMOVE(&vhbas->inproc, ccbh, sim_links.tqe);
	TAILQ_INSERT_TAIL(&vhbas->vhba->done, ccbh, sim_links.tqe);
	vhbas->nact -= 1;
	vhba_kick(vhbas->vhba);
	mtx_unlock(&vhbas->vhba->lock);
}

static void
vhba_timer(void *arg)
{
	int lun;
	vhba_softc_t *vhba = arg;
	mptest_t *vhbas = vhba->private;
	if (vhba_stop_lun) {
		lun = (vhba_stop_lun & 1)? 0 : 1;
		if (lun == 0 || lun == 1) {
			if (vhbas->luns[lun]) {
				struct cam_path *tp;
				if (vhba_notify_stop) {
					if (xpt_create_path(&tp, xpt_periph, cam_sim_path(vhba->sim), 0, lun) != CAM_REQ_CMP) {
						goto out;
					}
					vhbas->luns[lun] = 0;
					xpt_async(AC_LOST_DEVICE, tp, NULL);
					xpt_free_path(tp);
				} else {
					vhbas->luns[lun] = 0;
				}
			}
		}
		vhba_stop_lun &= ~(1 << lun);
	} else if (vhba_start_lun) {
		lun = (vhba_start_lun & 1)? 0 : 1;
		if (lun == 0 || lun == 1) {
			if (vhbas->luns[lun] == 0) {
				if (vhba_notify_start) {
					union ccb *ccb;
					ccb = xpt_alloc_ccb_nowait();
					if (ccb == NULL) {
						goto out;
					}
					if (xpt_create_path(&ccb->ccb_h.path, xpt_periph, cam_sim_path(vhba->sim), CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
						xpt_free_ccb(ccb);
						goto out;
					}
					vhbas->luns[lun] = 1;
					xpt_rescan(ccb);
				} else {
					vhbas->luns[lun] = 1;
				}
			}
		}
		vhba_start_lun &= ~(1 << lun);
	}
out:
	callout_reset(&vhbas->tick, VMP_TIME, vhba_timer, vhba);
}
DEV_MODULE(vhba_mptest, vhba_modprobe, NULL);
