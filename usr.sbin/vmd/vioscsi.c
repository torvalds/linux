/*	$OpenBSD: vioscsi.c,v 1.26 2025/08/02 15:16:18 dv Exp $  */

/*
 * Copyright (c) 2017 Carlos Cardenas <ccardenas@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>

#include <dev/pci/virtio_pcireg.h>
#include <dev/pv/vioscsireg.h>
#include <scsi/scsi_all.h>
#include <scsi/scsi_disk.h>
#include <scsi/scsiconf.h>
#include <scsi/cd.h>

#include <stdlib.h>
#include <string.h>

#include "vmd.h"
#include "vioscsi.h"
#include "virtio.h"

#define VIOSCSI_DEBUG	0
#ifdef DPRINTF
#undef DPRINTF
#endif
#if VIOSCSI_DEBUG
#define DPRINTF		log_debug
#else
#define DPRINTF(x...)	do {} while(0)
#endif	/* VIOSCSI_DEBUG */

extern char *__progname;

static void
vioscsi_prepare_resp(struct virtio_scsi_res_hdr *resp, uint8_t vio_status,
    uint8_t scsi_status, uint8_t err_flags, uint8_t add_sense_code,
    uint8_t add_sense_code_qual)
{
	/* Set lower 8 bits of status and response fields */
	resp->response &= 0xFFFFFF00;
	resp->response |= vio_status;
	resp->status &= 0xFFFFFF00;
	resp->status |= scsi_status;

	resp->sense_len = 0;

	/* determine if we need to populate the sense field */
	if (scsi_status == SCSI_CHECK) {
		/*
		 * sense data is a 96 byte field.
		 * We only need to use the first 14 bytes
		 * - set the sense_len accordingly
		 * - set error_code to Current Command
		 * ref scsi/scsi_all.h:struct scsi_sense_data
		 */
		memset(resp->sense, 0, VIOSCSI_SENSE_LEN);
		resp->sense_len = RESP_SENSE_LEN;
		resp->sense[0] = SSD_ERRCODE_CURRENT;
		resp->sense[2] = err_flags;
		resp->sense[12] = add_sense_code;
		resp->sense[13] = add_sense_code_qual;
	}
}

static struct vring_desc*
vioscsi_next_ring_desc(struct virtio_vq_info *vq_info, struct vring_desc* desc,
    struct vring_desc* cur, uint16_t *idx)
{
	*idx = cur->next & vq_info->mask;
	return &desc[*idx];
}

static void
vioscsi_next_ring_item(struct virtio_vq_info *vq_info,
    struct vring_avail *avail, struct vring_used *used, struct vring_desc *desc,
    uint16_t idx)
{
	used->ring[used->idx & vq_info->mask].id = idx;
	used->ring[used->idx & vq_info->mask].len = desc->len;
	__sync_synchronize();
	used->idx++;

	vq_info->last_avail = avail->idx & vq_info->mask;
}

static const char *
vioscsi_op_names(uint8_t type)
{
	switch (type) {
	/* defined in scsi_all.h */
	case TEST_UNIT_READY: return "TEST_UNIT_READY";
	case REQUEST_SENSE: return "REQUEST_SENSE";
	case INQUIRY: return "INQUIRY";
	case MODE_SELECT: return "MODE_SELECT";
	case RESERVE: return "RESERVE";
	case RELEASE: return "RELEASE";
	case MODE_SENSE: return "MODE_SENSE";
	case START_STOP: return "START_STOP";
	case RECEIVE_DIAGNOSTIC: return "RECEIVE_DIAGNOSTIC";
	case SEND_DIAGNOSTIC: return "SEND_DIAGNOSTIC";
	case PREVENT_ALLOW: return "PREVENT_ALLOW";
	case POSITION_TO_ELEMENT: return "POSITION_TO_ELEMENT";
	case WRITE_BUFFER: return "WRITE_BUFFER";
	case READ_BUFFER: return "READ_BUFFER";
	case CHANGE_DEFINITION: return "CHANGE_DEFINITION";
	case MODE_SELECT_BIG: return "MODE_SELECT_BIG";
	case MODE_SENSE_BIG: return "MODE_SENSE_BIG";
	case REPORT_LUNS: return "REPORT_LUNS";
	/* defined in scsi_disk.h */
	case REASSIGN_BLOCKS: return "REASSIGN_BLOCKS";
	case READ_COMMAND: return "READ_COMMAND";
	case WRITE_COMMAND: return "WRITE_COMMAND";
	case READ_CAPACITY: return "READ_CAPACITY";
	case READ_CAPACITY_16: return "READ_CAPACITY_16";
	case READ_10: return "READ_10";
	case WRITE_10: return "WRITE_10";
	case READ_12: return "READ_12";
	case WRITE_12: return "WRITE_12";
	case READ_16: return "READ_16";
	case WRITE_16: return "WRITE_16";
	case SYNCHRONIZE_CACHE: return "SYNCHRONIZE_CACHE";
	case WRITE_SAME_10: return "WRITE_SAME_10";
	case WRITE_SAME_16: return "WRITE_SAME_16";
	/* defined in cd.h */
	case READ_SUBCHANNEL: return "READ_SUBCHANNEL";
	case READ_TOC: return "READ_TOC";
	case READ_HEADER: return "READ_HEADER";
	case PLAY: return "PLAY";
	case PLAY_MSF: return "PLAY_MSF";
	case PLAY_TRACK: return "PLAY_TRACK";
	case PLAY_TRACK_REL: return "PLAY_TRACK_REL";
	case PAUSE: return "PAUSE";
	case READ_TRACK_INFO: return "READ_TRACK_INFO";
	case CLOSE_TRACK: return "CLOSE_TRACK";
	case BLANK: return "BLANK";
	case PLAY_BIG: return "PLAY_BIG";
	case LOAD_UNLOAD: return "LOAD_UNLOAD";
	case PLAY_TRACK_REL_BIG: return "PLAY_TRACK_REL_BIG";
	case SET_CD_SPEED: return "SET_CD_SPEED";
	/* defined locally */
	case READ_DISC_INFORMATION: return "READ_DISC_INFORMATION";
	case GET_CONFIGURATION: return "GET_CONFIGURATION";
	case MECHANISM_STATUS: return "MECHANISM_STATUS";
	case GET_EVENT_STATUS_NOTIFICATION:
	    return "GET_EVENT_STATUS_NOTIFICATION";
	default: return "UNKNOWN";
	}
}

#if VIOSCSI_DEBUG
static const char *
vioscsi_reg_name(uint8_t reg)
{
	switch (reg) {
	case VIRTIO_SCSI_CONFIG_NUM_QUEUES: return "NUM_QUEUES";
	case VIRTIO_SCSI_CONFIG_SEG_MAX: return "SEG_MAX";
	case VIRTIO_SCSI_CONFIG_MAX_SECTORS: return "MAX_SECTORS";
	case VIRTIO_SCSI_CONFIG_CMD_PER_LUN: return "CMD_PER_LUN";
	case VIRTIO_SCSI_CONFIG_EVENT_INFO_SIZE: return "EVENT_INFO_SIZE";
	case VIRTIO_SCSI_CONFIG_SENSE_SIZE: return "SENSE_SIZE";
	case VIRTIO_SCSI_CONFIG_CDB_SIZE: return "CDB_SIZE";
	case VIRTIO_SCSI_CONFIG_MAX_CHANNEL: return "MAX_CHANNEL";
	case VIRTIO_SCSI_CONFIG_MAX_TARGET: return "MAX_TARGET";
	case VIRTIO_SCSI_CONFIG_MAX_LUN: return "MAX_LUN";
	default: return "unknown";
	}
}
#endif	/* VIOSCSI_DEBUG */

static void
vioscsi_free_info(struct ioinfo *info)
{
	if (!info)
		return;
	free(info->buf);
	free(info);
}

static struct ioinfo *
vioscsi_start_read(struct virtio_dev *dev, off_t block, size_t n_blocks)
{
	struct ioinfo *info;

	/* Limit to 64M for now */
	if (n_blocks * VIOSCSI_BLOCK_SIZE_CDROM > (1 << 26)) {
		log_warnx("%s: read size exceeded 64M", __func__);
		return (NULL);
	}

	info = calloc(1, sizeof(*info));
	if (!info)
		goto nomem;
	info->buf = malloc(n_blocks * VIOSCSI_BLOCK_SIZE_CDROM);
	if (info->buf == NULL)
		goto nomem;
	info->len = n_blocks * VIOSCSI_BLOCK_SIZE_CDROM;
	info->offset = block * VIOSCSI_BLOCK_SIZE_CDROM;

	return info;

nomem:
	free(info);
	log_warn("malloc error vioscsi read");
	return (NULL);
}

static const uint8_t *
vioscsi_finish_read(struct virtio_dev *dev, struct ioinfo *info)
{
	struct virtio_backing *f = NULL;
	struct vioscsi_dev *vioscsi = NULL;

	if (dev->device_id != PCI_PRODUCT_VIRTIO_SCSI)
		fatalx("%s: virtio device is not a scsi device", __func__);
	vioscsi = &dev->vioscsi;

	f = &vioscsi->file;
	if (f->pread(f->p, info->buf, info->len, info->offset) != info->len) {
		log_warn("vioscsi read error");
		return NULL;
	}

	return info->buf;
}

static int
vioscsi_handle_tur(struct virtio_dev *dev, struct virtio_vq_info *vq_info,
    struct virtio_scsi_req_hdr *req, struct virtio_vq_acct *acct)
{
	int ret = 0;
	struct virtio_scsi_res_hdr resp;

	memset(&resp, 0, sizeof(resp));
	/* Move index for response */
	acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
	    acct->req_desc, &(acct->resp_idx));

	vioscsi_prepare_resp(&resp, VIRTIO_SCSI_S_OK, SCSI_OK, 0, 0, 0);

	if (write_mem(acct->resp_desc->addr, &resp, sizeof(resp))) {
		log_warnx("%s: unable to write OK resp status data @ 0x%llx",
		    __func__, acct->resp_desc->addr);
	} else {
		ret = 1;
		dev->isr = 1;
		/* Move ring indexes */
		vioscsi_next_ring_item(vq_info, acct->avail, acct->used,
		    acct->req_desc, acct->req_idx);
	}

	return (ret);
}

static int
vioscsi_handle_inquiry(struct virtio_dev *dev, struct virtio_vq_info *vq_info,
    struct virtio_scsi_req_hdr *req, struct virtio_vq_acct *acct)
{
	int ret = 0;
	struct virtio_scsi_res_hdr resp;
	struct scsi_inquiry_data *inq_data;

#if DEBUG
	struct scsi_inquiry *inq = (struct scsi_inquiry *)(req->cdb);
	log_debug("%s: INQ - EVPD %d PAGE_CODE 0x%08x LEN %d", __func__,
	    inq->flags & SI_EVPD, inq->pagecode, _2btol(inq->length));
#endif /* DEBUG */

	memset(&resp, 0, sizeof(resp));
	vioscsi_prepare_resp(&resp,
	    VIRTIO_SCSI_S_OK, SCSI_OK, 0, 0, 0);

	inq_data = calloc(1, sizeof(struct scsi_inquiry_data));

	if (inq_data == NULL) {
		log_warnx("%s: cannot alloc inq_data", __func__);
		goto inq_out;
	}

	inq_data->device = T_CDROM;
	inq_data->dev_qual2 = SID_REMOVABLE;
	/* Leave version zero to say we don't comply */
	inq_data->response_format = SID_SCSI2_RESPONSE;
	inq_data->additional_length = SID_SCSI2_ALEN;
	memcpy(inq_data->vendor, INQUIRY_VENDOR, INQUIRY_VENDOR_LEN);
	memcpy(inq_data->product, INQUIRY_PRODUCT, INQUIRY_PRODUCT_LEN);
	memcpy(inq_data->revision, INQUIRY_REVISION, INQUIRY_REVISION_LEN);

	/* Move index for response */
	acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
	    acct->req_desc, &(acct->resp_idx));

	DPRINTF("%s: writing resp to 0x%llx size %d at local "
	    "idx %d req_idx %d global_idx %d", __func__, acct->resp_desc->addr,
	    acct->resp_desc->len, acct->resp_idx, acct->req_idx, acct->idx);

	if (write_mem(acct->resp_desc->addr, &resp, sizeof(resp))) {
		log_warnx("%s: unable to write OK resp status data @ 0x%llx",
		    __func__, acct->resp_desc->addr);
		goto free_inq;
	}

	/* Move index for inquiry_data */
	acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
	    acct->resp_desc, &(acct->resp_idx));

	DPRINTF("%s: writing inq_data to 0x%llx size %d at "
	    "local idx %d req_idx %d global_idx %d",
	    __func__, acct->resp_desc->addr, acct->resp_desc->len,
	    acct->resp_idx, acct->req_idx, acct->idx);

	if (write_mem(acct->resp_desc->addr, inq_data,
		sizeof(struct scsi_inquiry_data))) {
		log_warnx("%s: unable to write inquiry"
		    " response to gpa @ 0x%llx",
		    __func__, acct->resp_desc->addr);
	} else {
		ret = 1;
		dev->isr = 1;
		/* Move ring indexes */
		vioscsi_next_ring_item(vq_info, acct->avail, acct->used,
		    acct->req_desc, acct->req_idx);
	}

free_inq:
	free(inq_data);
inq_out:
	return (ret);
}

static int
vioscsi_handle_mode_sense(struct virtio_dev *dev,
    struct virtio_vq_info *vq_info, struct virtio_scsi_req_hdr *req,
    struct virtio_vq_acct *acct)
{
	int ret = 0;
	struct virtio_scsi_res_hdr resp;
	uint8_t mode_page_ctl;
	uint8_t mode_page_code;
	uint8_t *mode_reply;
	uint8_t mode_reply_len = 0;
	struct scsi_mode_sense *mode_sense;

	memset(&resp, 0, sizeof(resp));
	mode_sense = (struct scsi_mode_sense *)(req->cdb);
	mode_page_ctl = mode_sense->page & SMS_PAGE_CTRL;
	mode_page_code = mode_sense->page & SMS_PAGE_CODE;

	DPRINTF("%s: M_SENSE - DBD %d Page Ctrl 0x%x Code 0x%x Len %u",
	    __func__, mode_sense->byte2 & SMS_DBD, mode_page_ctl,
	    mode_page_code, mode_sense->length);

	if (mode_page_ctl == SMS_PAGE_CTRL_CURRENT &&
	    (mode_page_code == ERR_RECOVERY_PAGE ||
	    mode_page_code == CDVD_CAPABILITIES_PAGE)) {
		/*
		 * mode sense header is 4 bytes followed
		 * by a variable page
		 * ERR_RECOVERY_PAGE is 12 bytes
		 * CDVD_CAPABILITIES_PAGE is 32 bytes
		 */
		switch (mode_page_code) {
		case ERR_RECOVERY_PAGE:
			mode_reply_len = 16;
			mode_reply =
			    (uint8_t*)calloc(mode_reply_len, sizeof(uint8_t));
			if (mode_reply == NULL)
				goto mode_sense_out;

			/* set the page header */
			*mode_reply = mode_reply_len - 1;
			*(mode_reply + 1) = MODE_MEDIUM_TYPE_CODE;

			/* set the page data, 7.3.2.1 mmc-5 */
			*(mode_reply + 4) = MODE_ERR_RECOVERY_PAGE_CODE;
			*(mode_reply + 5) = MODE_ERR_RECOVERY_PAGE_LEN;
			*(mode_reply + 7) = MODE_READ_RETRY_COUNT;
			break;
		case CDVD_CAPABILITIES_PAGE:
			mode_reply_len = 36;
			mode_reply =
			    (uint8_t*)calloc(mode_reply_len, sizeof(uint8_t));
			if (mode_reply == NULL)
				goto mode_sense_out;

			/* set the page header */
			*mode_reply = mode_reply_len - 1;
			*(mode_reply + 1) = MODE_MEDIUM_TYPE_CODE;

			/* set the page data, 6.3.11 mmc-3 */
			*(mode_reply + 4) = MODE_CDVD_CAP_PAGE_CODE;
			*(mode_reply + 5) = mode_reply_len - 6;
			*(mode_reply + 6) = MODE_CDVD_CAP_READ_CODE;
			_lto2b(MODE_CDVD_CAP_NUM_LEVELS, mode_reply + 14);
			break;
		default:
			goto mode_sense_error;
			break;
		}

		/* Move index for response */
		acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
		    acct->req_desc, &(acct->resp_idx));

		DPRINTF("%s: writing resp to 0x%llx size %d "
		    "at local idx %d req_idx %d global_idx %d",
		    __func__, acct->resp_desc->addr, mode_reply_len,
		    acct->resp_idx, acct->req_idx, acct->idx);

		if (write_mem(acct->resp_desc->addr, &resp, sizeof(resp))) {
			log_warnx("%s: unable to write OK"
			    " resp status data @ 0x%llx",
			    __func__, acct->resp_desc->addr);
			free(mode_reply);
			goto mode_sense_out;
		}

		/* Move index for mode_reply */
		acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
		    acct->resp_desc, &(acct->resp_idx));

		DPRINTF("%s: writing mode_reply to 0x%llx "
		    "size %d at local idx %d req_idx %d "
		    "global_idx %d", __func__, acct->resp_desc->addr,
		    mode_reply_len, acct->resp_idx, acct->req_idx, acct->idx);

		if (write_mem(acct->resp_desc->addr, mode_reply,
			mode_reply_len)) {
			log_warnx("%s: unable to write "
			    "mode_reply to gpa @ 0x%llx",
			    __func__, acct->resp_desc->addr);
			free(mode_reply);
			goto mode_sense_out;
		}

		free(mode_reply);

		ret = 1;
		dev->isr = 1;
		/* Move ring indexes */
		vioscsi_next_ring_item(vq_info, acct->avail, acct->used,
		    acct->req_desc, acct->req_idx);
	} else {
mode_sense_error:
		/* send back un-supported */
		vioscsi_prepare_resp(&resp,
		    VIRTIO_SCSI_S_OK, SCSI_CHECK, SKEY_ILLEGAL_REQUEST,
		    SENSE_ILLEGAL_CDB_FIELD, SENSE_DEFAULT_ASCQ);

		/* Move index for response */
		acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
		    acct->req_desc, &(acct->resp_idx));

		if (write_mem(acct->resp_desc->addr, &resp, sizeof(resp))) {
			log_warnx("%s: unable to set ERR status data @ 0x%llx",
			    __func__, acct->resp_desc->addr);
			goto mode_sense_out;
		}

		ret = 1;
		dev->isr = 1;
		/* Move ring indexes */
		vioscsi_next_ring_item(vq_info, acct->avail, acct->used,
		    acct->req_desc, acct->req_idx);
	}
mode_sense_out:
	return (ret);
}

static int
vioscsi_handle_mode_sense_big(struct virtio_dev *dev,
    struct virtio_vq_info *vq_info, struct virtio_scsi_req_hdr *req,
    struct virtio_vq_acct *acct)
{
	int ret = 0;
	struct virtio_scsi_res_hdr resp;
	uint8_t mode_page_ctl;
	uint8_t mode_page_code;
	uint8_t *mode_reply;
	uint8_t mode_reply_len = 0;
	struct scsi_mode_sense_big *mode_sense_10;

	memset(&resp, 0, sizeof(resp));
	mode_sense_10 = (struct scsi_mode_sense_big *)(req->cdb);
	mode_page_ctl = mode_sense_10->page & SMS_PAGE_CTRL;
	mode_page_code = mode_sense_10->page & SMS_PAGE_CODE;

	DPRINTF("%s: M_SENSE_10 - DBD %d Page Ctrl 0x%x Code 0x%x Len %u",
	    __func__, mode_sense_10->byte2 & SMS_DBD, mode_page_ctl,
	    mode_page_code, (uint16_t)_2btol(mode_sense_10->length));

	if (mode_page_ctl == SMS_PAGE_CTRL_CURRENT &&
	    (mode_page_code == ERR_RECOVERY_PAGE ||
	    mode_page_code == CDVD_CAPABILITIES_PAGE)) {
		/*
		 * mode sense header is 8 bytes followed
		 * by a variable page
		 * ERR_RECOVERY_PAGE is 12 bytes
		 * CDVD_CAPABILITIES_PAGE is 32 bytes
		 */
		switch (mode_page_code) {
		case ERR_RECOVERY_PAGE:
			mode_reply_len = 20;
			mode_reply =
			    (uint8_t*)calloc(mode_reply_len, sizeof(uint8_t));
			if (mode_reply == NULL)
				goto mode_sense_big_out;

			/* set the page header */
			_lto2b(mode_reply_len - 2, mode_reply);
			*(mode_reply + 2) = MODE_MEDIUM_TYPE_CODE;

			/* set the page data, 7.3.2.1 mmc-5 */
			*(mode_reply + 8) = MODE_ERR_RECOVERY_PAGE_CODE;
			*(mode_reply + 9) = MODE_ERR_RECOVERY_PAGE_LEN;
			*(mode_reply + 11) = MODE_READ_RETRY_COUNT;
			break;
		case CDVD_CAPABILITIES_PAGE:
			mode_reply_len = 40;
			mode_reply =
			    (uint8_t*)calloc(mode_reply_len, sizeof(uint8_t));
			if (mode_reply == NULL)
				goto mode_sense_big_out;

			/* set the page header */
			_lto2b(mode_reply_len - 2, mode_reply);
			*(mode_reply + 2) = MODE_MEDIUM_TYPE_CODE;

			/* set the page data, 6.3.11 mmc-3 */
			*(mode_reply + 8) = MODE_CDVD_CAP_PAGE_CODE;
			*(mode_reply + 9) = mode_reply_len - 6;
			*(mode_reply + 10) = MODE_CDVD_CAP_READ_CODE;
			_lto2b(MODE_CDVD_CAP_NUM_LEVELS, mode_reply + 18);
			break;
		default:
			goto mode_sense_big_error;
			break;
		}

		/* Move index for response */
		acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
		    acct->req_desc, &(acct->resp_idx));

		DPRINTF("%s: writing resp to 0x%llx size %d "
		    "at local idx %d req_idx %d global_idx %d",
		    __func__, acct->resp_desc->addr, acct->resp_desc->len,
		    acct->resp_idx, acct->req_idx, acct->idx);

		if (write_mem(acct->resp_desc->addr, &resp, sizeof(resp))) {
			log_warnx("%s: unable to write OK"
			    " resp status data @ 0x%llx",
			    __func__, acct->resp_desc->addr);
			free(mode_reply);
			goto mode_sense_big_out;
		}

		/* Move index for mode_reply */
		acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
		    acct->resp_desc, &(acct->resp_idx));

		DPRINTF("%s: writing mode_reply to 0x%llx "
		    "size %d at local idx %d req_idx %d global_idx %d",
		    __func__, acct->resp_desc->addr, mode_reply_len,
		    acct->resp_idx, acct->req_idx, acct->idx);

		if (write_mem(acct->resp_desc->addr, mode_reply,
			mode_reply_len)) {
			log_warnx("%s: unable to write "
			    "mode_reply to gpa @ 0x%llx",
			    __func__, acct->resp_desc->addr);
			free(mode_reply);
			goto mode_sense_big_out;
		}

		free(mode_reply);

		ret = 1;
		dev->isr = 1;
		/* Move ring indexes */
		vioscsi_next_ring_item(vq_info, acct->avail, acct->used,
		    acct->req_desc, acct->req_idx);
	} else {
mode_sense_big_error:
		/* send back un-supported */
		vioscsi_prepare_resp(&resp,
		    VIRTIO_SCSI_S_OK, SCSI_CHECK, SKEY_ILLEGAL_REQUEST,
		    SENSE_ILLEGAL_CDB_FIELD, SENSE_DEFAULT_ASCQ);

		/* Move index for response */
		acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
		    acct->req_desc, &(acct->resp_idx));

		if (write_mem(acct->resp_desc->addr, &resp, sizeof(resp))) {
			log_warnx("%s: unable to set ERR status data @ 0x%llx",
			    __func__, acct->resp_desc->addr);
			goto mode_sense_big_out;
		}

		ret = 1;
		dev->isr = 1;
		/* Move ring indexes */
		vioscsi_next_ring_item(vq_info, acct->avail, acct->used,
		    acct->req_desc, acct->req_idx);
	}
mode_sense_big_out:
	return (ret);
}

static int
vioscsi_handle_read_capacity(struct virtio_dev *dev,
    struct virtio_vq_info *vq_info,struct virtio_scsi_req_hdr *req,
    struct virtio_vq_acct *acct)
{
	int ret = 0;
	struct virtio_scsi_res_hdr resp;
	struct scsi_read_cap_data *r_cap_data;
	struct vioscsi_dev *vioscsi = NULL;

	if (dev->device_id != PCI_PRODUCT_VIRTIO_SCSI)
		fatalx("%s: virtio device is not a scsi device", __func__);
	vioscsi = &dev->vioscsi;

#if DEBUG
	struct scsi_read_capacity *r_cap =
	    (struct scsi_read_capacity *)(req->cdb);
	log_debug("%s: %s - Addr 0x%08x", __func__,
	    vioscsi_op_names(r_cap->opcode), _4btol(r_cap->addr));
#endif /* DEBUG */

	memset(&resp, 0, sizeof(resp));
	vioscsi_prepare_resp(&resp,
	    VIRTIO_SCSI_S_OK, SCSI_OK, 0, 0, 0);

	r_cap_data = calloc(1, sizeof(struct scsi_read_cap_data));

	if (r_cap_data == NULL) {
		log_warnx("%s: cannot alloc r_cap_data", __func__);
		goto read_capacity_out;
	}

	DPRINTF("%s: ISO has %lld bytes and %lld blocks",
	    __func__, vioscsi->sz, vioscsi->n_blocks);

	/*
	 * determine if num blocks of iso image > UINT32_MAX
	 * if it is, set addr to UINT32_MAX (0xffffffff)
	 * indicating to hosts that READ_CAPACITY_16 should
	 * be called to retrieve the full size
	 */
	if (vioscsi->n_blocks >= UINT32_MAX) {
		_lto4b(UINT32_MAX, r_cap_data->addr);
		_lto4b(VIOSCSI_BLOCK_SIZE_CDROM, r_cap_data->length);
		log_warnx("%s: ISO sz %lld is bigger than "
		    "UINT32_MAX %u, all data may not be read",
		    __func__, vioscsi->sz, UINT32_MAX);
	} else {
		_lto4b(vioscsi->n_blocks - 1, r_cap_data->addr);
		_lto4b(VIOSCSI_BLOCK_SIZE_CDROM, r_cap_data->length);
	}

	/* Move index for response */
	acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
	    acct->req_desc, &(acct->resp_idx));

	DPRINTF("%s: writing resp to 0x%llx size %d at local "
	    "idx %d req_idx %d global_idx %d",
	    __func__, acct->resp_desc->addr, acct->resp_desc->len,
	    acct->resp_idx, acct->req_idx, acct->idx);

	if (write_mem(acct->resp_desc->addr, &resp, sizeof(resp))) {
		log_warnx("%s: unable to write OK resp status data @ 0x%llx",
		    __func__, acct->resp_desc->addr);
		goto free_read_capacity;
	}

	/* Move index for r_cap_data */
	acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
	    acct->resp_desc, &(acct->resp_idx));

	DPRINTF("%s: writing r_cap_data to 0x%llx size %d at "
	    "local idx %d req_idx %d global_idx %d",
	    __func__, acct->resp_desc->addr, acct->resp_desc->len,
	    acct->resp_idx, acct->req_idx, acct->idx);

	if (write_mem(acct->resp_desc->addr, r_cap_data,
		sizeof(struct scsi_read_cap_data))) {
		log_warnx("%s: unable to write read_cap_data"
		    " response to gpa @ 0x%llx",
		    __func__, acct->resp_desc->addr);
	} else {
		ret = 1;
		dev->isr = 1;
		/* Move ring indexes */
		vioscsi_next_ring_item(vq_info, acct->avail, acct->used,
		    acct->req_desc, acct->req_idx);
	}

free_read_capacity:
	free(r_cap_data);
read_capacity_out:
	return (ret);
}

static int
vioscsi_handle_read_capacity_16(struct virtio_dev *dev,
    struct virtio_vq_info *vq_info, struct virtio_scsi_req_hdr *req,
    struct virtio_vq_acct *acct)
{
	int ret = 0;
	struct virtio_scsi_res_hdr resp;
	struct scsi_read_cap_data_16 *r_cap_data_16;
	struct vioscsi_dev *vioscsi = NULL;

	if (dev->device_id != PCI_PRODUCT_VIRTIO_SCSI)
		fatalx("%s: virtio device is not a scsi device", __func__);
	vioscsi = &dev->vioscsi;

#if DEBUG
	struct scsi_read_capacity_16 *r_cap_16 =
	    (struct scsi_read_capacity_16 *)(req->cdb);
	log_debug("%s: %s - Addr 0x%016llx", __func__,
	    vioscsi_op_names(r_cap_16->opcode), _8btol(r_cap_16->addr));
#endif /* DEBUG */

	memset(&resp, 0, sizeof(resp));
	vioscsi_prepare_resp(&resp, VIRTIO_SCSI_S_OK, SCSI_OK, 0, 0, 0);

	r_cap_data_16 = calloc(1, sizeof(struct scsi_read_cap_data_16));

	if (r_cap_data_16 == NULL) {
		log_warnx("%s: cannot alloc r_cap_data_16",
		    __func__);
		goto read_capacity_16_out;
	}

	DPRINTF("%s: ISO has %lld bytes and %lld blocks", __func__,
	    dev->vioscsi.sz, dev->vioscsi.n_blocks);

	_lto8b(vioscsi->n_blocks - 1, r_cap_data_16->addr);
	_lto4b(VIOSCSI_BLOCK_SIZE_CDROM, r_cap_data_16->length);

	/* Move index for response */
	acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
	    acct->req_desc, &(acct->resp_idx));

	DPRINTF("%s: writing resp to 0x%llx size %d at local "
	    "idx %d req_idx %d global_idx %d",
	    __func__, acct->resp_desc->addr, acct->resp_desc->len,
	    acct->resp_idx, acct->req_idx, acct->idx);

	if (write_mem(acct->resp_desc->addr, &resp, sizeof(resp))) {
		log_warnx("%s: unable to write OK resp status "
		    "data @ 0x%llx", __func__, acct->resp_desc->addr);
		goto free_read_capacity_16;
	}

	/* Move index for r_cap_data_16 */
	acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
	    acct->resp_desc, &(acct->resp_idx));

	DPRINTF("%s: writing r_cap_data_16 to 0x%llx size %d "
	    "at local idx %d req_idx %d global_idx %d",
	    __func__, acct->resp_desc->addr, acct->resp_desc->len,
	    acct->resp_idx, acct->req_idx, acct->idx);

	if (write_mem(acct->resp_desc->addr, r_cap_data_16,
		sizeof(struct scsi_read_cap_data_16))) {
		log_warnx("%s: unable to write read_cap_data_16"
		    " response to gpa @ 0x%llx",
		    __func__, acct->resp_desc->addr);
	} else {
		ret = 1;
		dev->isr = 1;
		/* Move ring indexes */
		vioscsi_next_ring_item(vq_info, acct->avail, acct->used,
		    acct->req_desc, acct->req_idx);
	}

free_read_capacity_16:
	free(r_cap_data_16);
read_capacity_16_out:
	return (ret);
}

static int
vioscsi_handle_report_luns(struct virtio_dev *dev,
    struct virtio_vq_info *vq_info, struct virtio_scsi_req_hdr *req,
    struct virtio_vq_acct *acct)
{
	int ret = 0;
	struct virtio_scsi_res_hdr resp;
	uint32_t rpl_length;
	struct scsi_report_luns *rpl;
	struct vioscsi_report_luns_data *reply_rpl;

	memset(&resp, 0, sizeof(resp));
	rpl = (struct scsi_report_luns *)(req->cdb);
	rpl_length = _4btol(rpl->length);

	DPRINTF("%s: REPORT_LUNS Report 0x%x Length %d", __func__,
	    rpl->selectreport, rpl_length);

	if (rpl_length < RPL_MIN_SIZE) {
		DPRINTF("%s: RPL_Length %d < %d (RPL_MIN_SIZE)", __func__,
		    rpl_length, RPL_MIN_SIZE);

		vioscsi_prepare_resp(&resp,
		    VIRTIO_SCSI_S_OK, SCSI_CHECK, SKEY_ILLEGAL_REQUEST,
		    SENSE_ILLEGAL_CDB_FIELD, SENSE_DEFAULT_ASCQ);

		/* Move index for response */
		acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
		    acct->req_desc, &(acct->resp_idx));

		if (write_mem(acct->resp_desc->addr, &resp, sizeof(resp))) {
			log_warnx("%s: unable to set ERR "
			    "status data @ 0x%llx", __func__,
			    acct->resp_desc->addr);
		} else {
			ret = 1;
			dev->isr = 1;
			/* Move ring indexes */
			vioscsi_next_ring_item(vq_info, acct->avail, acct->used,
			    acct->req_desc, acct->req_idx);
		}
		goto rpl_out;

	}

	reply_rpl = calloc(1, sizeof(struct vioscsi_report_luns_data));

	if (reply_rpl == NULL) {
		log_warnx("%s: cannot alloc reply_rpl", __func__);
		goto rpl_out;
	}

	_lto4b(RPL_SINGLE_LUN, reply_rpl->length);
	memcpy(reply_rpl->lun, req->lun, RPL_SINGLE_LUN);

	vioscsi_prepare_resp(&resp,
	    VIRTIO_SCSI_S_OK, SCSI_OK, 0, 0, 0);

	/* Move index for response */
	acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
	    acct->req_desc, &(acct->resp_idx));

	DPRINTF("%s: writing resp to 0x%llx size %d at local "
	    "idx %d req_idx %d global_idx %d", __func__, acct->resp_desc->addr,
	    acct->resp_desc->len, acct->resp_idx, acct->req_idx, acct->idx);

	if (write_mem(acct->resp_desc->addr, &resp, sizeof(resp))) {
		log_warnx("%s: unable to write OK resp status data @ 0x%llx",
		    __func__, acct->resp_desc->addr);
		goto free_rpl;
	}

	/* Move index for reply_rpl */
	acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
	    acct->resp_desc, &(acct->resp_idx));

	DPRINTF("%s: writing reply_rpl to 0x%llx size %d at "
	    "local idx %d req_idx %d global_idx %d",
	    __func__, acct->resp_desc->addr, acct->resp_desc->len,
	    acct->resp_idx, acct->req_idx, acct->idx);

	if (write_mem(acct->resp_desc->addr, reply_rpl,
		sizeof(struct vioscsi_report_luns_data))) {
		log_warnx("%s: unable to write reply_rpl"
		    " response to gpa @ 0x%llx",
		    __func__, acct->resp_desc->addr);
	} else {
		ret = 1;
		dev->isr = 1;
		/* Move ring indexes */
		vioscsi_next_ring_item(vq_info, acct->avail, acct->used,
		    acct->req_desc, acct->req_idx);
	}

free_rpl:
	free(reply_rpl);
rpl_out:
	return (ret);
}

static int
vioscsi_handle_read_6(struct virtio_dev *dev,
    struct virtio_vq_info *vq_info, struct virtio_scsi_req_hdr *req,
    struct virtio_vq_acct *acct)
{
	int ret = 0;
	struct virtio_scsi_res_hdr resp;
	const uint8_t *read_buf;
	uint32_t read_lba;
	struct ioinfo *info;
	struct scsi_rw *read_6;
	struct vioscsi_dev *vioscsi = NULL;

	if (dev->device_id != PCI_PRODUCT_VIRTIO_SCSI)
		fatalx("%s: virtio device is not a scsi device", __func__);
	vioscsi = &dev->vioscsi;

	memset(&resp, 0, sizeof(resp));
	read_6 = (struct scsi_rw *)(req->cdb);
	read_lba = ((read_6->addr[0] & SRW_TOPADDR) << 16 ) |
	    (read_6->addr[1] << 8) | read_6->addr[2];

	DPRINTF("%s: READ Addr 0x%08x Len %d (%d)",
	    __func__, read_lba, read_6->length,
	    read_6->length * dev->vioscsi.max_xfer);

	/* check if lba is in range */
	if (read_lba > vioscsi->n_blocks - 1) {
		DPRINTF("%s: requested block out of range req: %ud max: %lld",
		    __func__, read_lba, vioscsi->n_blocks);

		vioscsi_prepare_resp(&resp,
		    VIRTIO_SCSI_S_OK, SCSI_CHECK, SKEY_ILLEGAL_REQUEST,
		    SENSE_LBA_OUT_OF_RANGE, SENSE_DEFAULT_ASCQ);

		/* Move index for response */
		acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
		    acct->req_desc, &(acct->resp_idx));

		if (write_mem(acct->resp_desc->addr, &resp, sizeof(resp))) {
			log_warnx("%s: unable to set ERR "
			    "status data @ 0x%llx", __func__,
			    acct->resp_desc->addr);
		} else {
			ret = 1;
			dev->isr = 1;
			/* Move ring indexes */
			vioscsi_next_ring_item(vq_info, acct->avail, acct->used,
			    acct->req_desc, acct->req_idx);
		}
		goto read_6_out;
	}

	info = vioscsi_start_read(dev, read_lba, read_6->length);

	if (info == NULL) {
		log_warnx("%s: cannot alloc for read", __func__);
		goto read_6_out;
	}

	/* read block */
	read_buf = vioscsi_finish_read(dev, info);

	if (read_buf == NULL) {
		log_warnx("%s: error reading position %ud",
		    __func__, read_lba);
		vioscsi_prepare_resp(&resp,
		    VIRTIO_SCSI_S_OK, SCSI_CHECK, SKEY_MEDIUM_ERROR,
		    SENSE_MEDIUM_NOT_PRESENT, SENSE_DEFAULT_ASCQ);

		/* Move index for response */
		acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
		    acct->req_desc, &(acct->resp_idx));

		if (write_mem(acct->resp_desc->addr, &resp, sizeof(resp))) {
			log_warnx("%s: unable to set ERR "
			    "status data @ 0x%llx", __func__,
			    acct->resp_desc->addr);
		} else {
			ret = 1;
			dev->isr = 1;
			/* Move ring indexes */
			vioscsi_next_ring_item(vq_info, acct->avail, acct->used,
			    acct->req_desc, acct->req_idx);
		}

		goto free_read_6;
	}

	vioscsi_prepare_resp(&resp, VIRTIO_SCSI_S_OK, SCSI_OK, 0, 0, 0);

	/* Move index for response */
	acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
	    acct->req_desc, &(acct->resp_idx));

	DPRINTF("%s: writing resp to 0x%llx size %d at local "
	    "idx %d req_idx %d global_idx %d",
	    __func__, acct->resp_desc->addr, acct->resp_desc->len,
	    acct->resp_idx, acct->req_idx, acct->idx);

	if (write_mem(acct->resp_desc->addr, &resp, sizeof(resp))) {
		log_warnx("%s: unable to write OK resp status "
		    "data @ 0x%llx", __func__, acct->resp_desc->addr);
		goto free_read_6;
	}

	/* Move index for read_buf */
	acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
	    acct->resp_desc, &(acct->resp_idx));

	DPRINTF("%s: writing read_buf to 0x%llx size %d at "
	    "local idx %d req_idx %d global_idx %d",
	    __func__, acct->resp_desc->addr, acct->resp_desc->len,
	    acct->resp_idx, acct->req_idx, acct->idx);

	if (write_mem(acct->resp_desc->addr, read_buf, info->len)) {
		log_warnx("%s: unable to write read_buf to gpa @ 0x%llx",
		    __func__, acct->resp_desc->addr);
	} else {
		ret = 1;
		dev->isr = 1;
		/* Move ring indexes */
		vioscsi_next_ring_item(vq_info, acct->avail, acct->used,
		    acct->req_desc, acct->req_idx);
	}

free_read_6:
	vioscsi_free_info(info);
read_6_out:
	return (ret);
}

static int
vioscsi_handle_read_10(struct virtio_dev *dev,
    struct virtio_vq_info *vq_info, struct virtio_scsi_req_hdr *req,
    struct virtio_vq_acct *acct)
{
	int ret = 0;
	struct virtio_scsi_res_hdr resp;
	const uint8_t *read_buf;
	uint32_t read_lba;
	uint16_t read_10_len;
	off_t chunk_offset;
	struct ioinfo *info;
	struct scsi_rw_10 *read_10;
	size_t chunk_len = 0;
	struct vioscsi_dev *vioscsi = NULL;

	if (dev->device_id != PCI_PRODUCT_VIRTIO_SCSI)
		fatalx("%s: virtio device is not a scsi device", __func__);
	vioscsi = &dev->vioscsi;

	memset(&resp, 0, sizeof(resp));
	read_10 = (struct scsi_rw_10 *)(req->cdb);
	read_lba = _4btol(read_10->addr);
	read_10_len = _2btol(read_10->length);
	chunk_offset = 0;

	DPRINTF("%s: READ_10 Addr 0x%08x Len %d (%d)",
	    __func__, read_lba, read_10_len, read_10_len * vioscsi->max_xfer);

	/* check if lba is in range */
	if (read_lba > vioscsi->n_blocks - 1) {
		DPRINTF("%s: requested block out of range req: %ud max: %lld",
		    __func__, read_lba, vioscsi->n_blocks);

		vioscsi_prepare_resp(&resp,
		    VIRTIO_SCSI_S_OK, SCSI_CHECK, SKEY_ILLEGAL_REQUEST,
		    SENSE_LBA_OUT_OF_RANGE, SENSE_DEFAULT_ASCQ);

		/* Move index for response */
		acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
		    acct->req_desc, &(acct->resp_idx));

		if (write_mem(acct->resp_desc->addr, &resp, sizeof(resp))) {
			log_warnx("%s: unable to set ERR status data @ 0x%llx",
			    __func__, acct->resp_desc->addr);
		} else {
			ret = 1;
			dev->isr = 1;
			/* Move ring indexes */
			vioscsi_next_ring_item(vq_info, acct->avail, acct->used,
			    acct->req_desc, acct->req_idx);
		}

		goto read_10_out;
	}

	info = vioscsi_start_read(dev, read_lba, read_10_len);

	if (info == NULL) {
		log_warnx("%s: cannot alloc for read", __func__);
		goto read_10_out;
	}

	/* read block */
	read_buf = vioscsi_finish_read(dev, info);

	if (read_buf == NULL) {
		log_warnx("%s: error reading position %ud", __func__, read_lba);
		vioscsi_prepare_resp(&resp,
		    VIRTIO_SCSI_S_OK, SCSI_CHECK, SKEY_MEDIUM_ERROR,
		    SENSE_MEDIUM_NOT_PRESENT, SENSE_DEFAULT_ASCQ);

		/* Move index for response */
		acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
		    acct->req_desc, &(acct->resp_idx));

		if (write_mem(acct->resp_desc->addr, &resp, sizeof(resp))) {
			log_warnx("%s: unable to set ERR status data @ 0x%llx",
			    __func__, acct->resp_desc->addr);
		} else {
			ret = 1;
			dev->isr = 1;
			/* Move ring indexes */
			vioscsi_next_ring_item(vq_info, acct->avail, acct->used,
			    acct->req_desc, acct->req_idx);
		}

		goto free_read_10;
	}

	vioscsi_prepare_resp(&resp, VIRTIO_SCSI_S_OK, SCSI_OK, 0, 0, 0);

	/* Move index for response */
	acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
	    acct->req_desc, &(acct->resp_idx));

	DPRINTF("%s: writing resp to 0x%llx size %d at local "
	    "idx %d req_idx %d global_idx %d",
	    __func__, acct->resp_desc->addr, acct->resp_desc->len,
	    acct->resp_idx, acct->req_idx, acct->idx);

	if (write_mem(acct->resp_desc->addr, &resp, sizeof(resp))) {
		log_warnx("%s: unable to write OK resp status "
		    "data @ 0x%llx", __func__, acct->resp_desc->addr);
		goto free_read_10;
	}

	/*
	 * Perform possible chunking of writes of read_buf
	 * based on the segment length allocated by the host.
	 * At least one write will be performed.
	 * If chunk_offset == info->len, no more writes
	 */
	do {
		/* Move index for read_buf */
		acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
		    acct->resp_desc, &(acct->resp_idx));

		DPRINTF("%s: writing read_buf to 0x%llx size "
		    "%d at local idx %d req_idx %d global_idx %d",
		    __func__, acct->resp_desc->addr, acct->resp_desc->len,
		    acct->resp_idx, acct->req_idx, acct->idx);

		/* Check we don't read beyond read_buf boundaries. */
		if (acct->resp_desc->len > info->len - chunk_offset) {
			log_warnx("%s: descriptor length beyond read_buf len",
			    __func__);
			chunk_len = info->len - chunk_offset;
		} else
			chunk_len = acct->resp_desc->len;

		if (write_mem(acct->resp_desc->addr, read_buf + chunk_offset,
			chunk_len)) {
			log_warnx("%s: unable to write read_buf"
			    " to gpa @ 0x%llx", __func__,
			    acct->resp_desc->addr);
			goto free_read_10;
		}
		chunk_offset += acct->resp_desc->len;
	} while (chunk_offset < info->len);

	ret = 1;
	dev->isr = 1;
	/* Move ring indexes */
	vioscsi_next_ring_item(vq_info, acct->avail, acct->used, acct->req_desc,
	    acct->req_idx);

free_read_10:
	vioscsi_free_info(info);
read_10_out:
	return (ret);
}

static int
vioscsi_handle_prevent_allow(struct virtio_dev *dev,
    struct virtio_vq_info *vq_info, struct virtio_scsi_req_hdr *req,
    struct virtio_vq_acct *acct)
{
	int ret = 0;
	struct vioscsi_dev *vioscsi = NULL;
	struct virtio_scsi_res_hdr resp;

	if (dev->device_id != PCI_PRODUCT_VIRTIO_SCSI)
		fatalx("%s: virtio device is not a scsi device", __func__);
	vioscsi = &dev->vioscsi;

	memset(&resp, 0, sizeof(resp));
	/* Move index for response */
	acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
	    acct->req_desc, &(acct->resp_idx));

	vioscsi_prepare_resp(&resp, VIRTIO_SCSI_S_OK, SCSI_OK, 0, 0, 0);

	if (vioscsi->locked) {
		DPRINTF("%s: unlocking medium", __func__);
	} else {
		DPRINTF("%s: locking medium", __func__);
	}

	vioscsi->locked = vioscsi->locked ? 0 : 1;

	if (write_mem(acct->resp_desc->addr, &resp, sizeof(resp))) {
		log_warnx("%s: unable to write OK resp status data @ 0x%llx",
		    __func__, acct->resp_desc->addr);
	} else {
		ret = 1;
		dev->isr = 1;
		/* Move ring indexes */
		vioscsi_next_ring_item(vq_info, acct->avail, acct->used,
		    acct->req_desc, acct->req_idx);
	}

	return (ret);
}

static int
vioscsi_handle_mechanism_status(struct virtio_dev *dev,
    struct virtio_vq_info *vq_info, struct virtio_scsi_req_hdr *req,
    struct virtio_vq_acct *acct)
{
	int ret = 0;
	struct virtio_scsi_res_hdr resp;
	struct scsi_mechanism_status_header *mech_status_header;

	DPRINTF("%s: MECH_STATUS Len %u", __func__,
	    _2btol(((struct scsi_mechanism_status *)(req->cdb))->length));

	mech_status_header = calloc(1,
	    sizeof(struct scsi_mechanism_status_header));

	if (mech_status_header == NULL)
		goto mech_out;

	/* return a 0 header since we are not a changer */
	memset(&resp, 0, sizeof(resp));
	vioscsi_prepare_resp(&resp,
	    VIRTIO_SCSI_S_OK, SCSI_OK, 0, 0, 0);

	/* Move index for response */
	acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
	    acct->req_desc, &(acct->resp_idx));

	if (write_mem(acct->resp_desc->addr, &resp, sizeof(resp))) {
		log_warnx("%s: unable to set ERR status data @ 0x%llx",
		    __func__, acct->resp_desc->addr);
		goto free_mech;
	}

	/* Move index for mech_status_header */
	acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
	    acct->resp_desc, &(acct->resp_idx));

	if (write_mem(acct->resp_desc->addr, mech_status_header,
		sizeof(struct scsi_mechanism_status_header))) {
		log_warnx("%s: unable to write "
		    "mech_status_header response to "
		    "gpa @ 0x%llx",
		    __func__, acct->resp_desc->addr);
	} else {
		ret = 1;
		dev->isr = 1;
		/* Move ring indexes */
		vioscsi_next_ring_item(vq_info, acct->avail, acct->used,
		    acct->req_desc, acct->req_idx);
	}

free_mech:
	free(mech_status_header);
mech_out:
	return (ret);
}

static int
vioscsi_handle_read_toc(struct virtio_dev *dev,
    struct virtio_vq_info *vq_info, struct virtio_scsi_req_hdr *req,
    struct virtio_vq_acct *acct)
{
	int ret = 0;
	struct virtio_scsi_res_hdr resp;
	uint16_t toc_data_len;
	uint8_t toc_data[TOC_DATA_SIZE];
	uint8_t *toc_data_p;
	struct scsi_read_toc *toc = (struct scsi_read_toc *)(req->cdb);
	struct vioscsi_dev *vioscsi = NULL;

	if (dev->device_id != PCI_PRODUCT_VIRTIO_SCSI)
		fatalx("%s: virtio device is not a scsi device", __func__);
	vioscsi = &dev->vioscsi;

	DPRINTF("%s: %s - MSF %d Track 0x%02x Addr 0x%04x",
	    __func__, vioscsi_op_names(toc->opcode), ((toc->byte2 >> 1) & 1),
	    toc->from_track, _2btol(toc->data_len));

	/* Tracks should be 0, 1, or LEAD_OUT_TRACK, 0xaa */
	if (toc->from_track > 1 &&
	    toc->from_track != READ_TOC_LEAD_OUT_TRACK) {
		/* illegal request */
		log_warnx("%s: illegal request Track 0x%02x",
		    __func__, toc->from_track);

		memset(&resp, 0, sizeof(resp));
		vioscsi_prepare_resp(&resp,
		    VIRTIO_SCSI_S_OK, SCSI_CHECK, SKEY_ILLEGAL_REQUEST,
		    SENSE_ILLEGAL_CDB_FIELD, SENSE_DEFAULT_ASCQ);

		/* Move index for response */
		acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
		    acct->req_desc, &(acct->resp_idx));

		if (write_mem(acct->resp_desc->addr, &resp, sizeof(resp))) {
			log_warnx("%s: unable to set ERR status data @ 0x%llx",
			    __func__, acct->resp_desc->addr);
			goto read_toc_out;
		}

		ret = 1;
		dev->isr = 1;
		/* Move ring indexes */
		vioscsi_next_ring_item(vq_info, acct->avail, acct->used,
		    acct->req_desc, acct->req_idx);

		goto read_toc_out;
	}

	/*
	 * toc_data is defined as:
	 * [0-1]: TOC Data Length, typically 0x1a
	 * [2]: First Track, 1
	 * [3]: Last Track, 1
	 *
	 * Track 1 Descriptor
	 * [0]: Reserved, 0
	 * [1]: ADR,Control, 0x14
	 * [2]: Track #, 1
	 * [3]: Reserved, 0
	 * [4-7]: Track Start Address, LBA
	 *
	 * Track 0xaa (Lead Out) Descriptor
	 * [0]: Reserved, 0
	 * [1]: ADR,Control, 0x14
	 * [2]: Track #, 0xaa
	 * [3]: Reserved, 0
	 * [4-7]: Track Start Address, LBA
	 */
	memset(toc_data, 0, sizeof(toc_data));
	toc_data_p = toc_data + 2;
	*toc_data_p++ = READ_TOC_START_TRACK;
	*toc_data_p++ = READ_TOC_LAST_TRACK;
	if (toc->from_track <= 1) {
		/* first track descriptor */
		*toc_data_p++ = 0x0;
		*toc_data_p++ = READ_TOC_ADR_CTL;
		*toc_data_p++ = READ_TOC_START_TRACK;
		*toc_data_p++ = 0x0;
		/* start addr for first track is 0 */
		*toc_data_p++ = 0x0;
		*toc_data_p++ = 0x0;
		*toc_data_p++ = 0x0;
		*toc_data_p++ = 0x0;
	}

	/* last track descriptor */
	*toc_data_p++ = 0x0;
	*toc_data_p++ = READ_TOC_ADR_CTL;
	*toc_data_p++ = READ_TOC_LEAD_OUT_TRACK;
	*toc_data_p++ = 0x0;

	_lto4b((uint32_t)vioscsi->n_blocks, toc_data_p);
	toc_data_p += 4;

	toc_data_len = toc_data_p - toc_data;
	_lto2b((uint32_t)toc_data_len - 2, toc_data);

	memset(&resp, 0, sizeof(resp));
	vioscsi_prepare_resp(&resp, VIRTIO_SCSI_S_OK, SCSI_OK, 0, 0, 0);

	/* Move index for response */
	acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
	    acct->req_desc, &(acct->resp_idx));

	DPRINTF("%s: writing resp to 0x%llx size %d at local "
	    "idx %d req_idx %d global_idx %d",
	    __func__, acct->resp_desc->addr, acct->resp_desc->len,
	    acct->resp_idx, acct->req_idx, acct->idx);

	if (write_mem(acct->resp_desc->addr, &resp, sizeof(resp))) {
		log_warnx("%s: unable to write OK resp status data @ 0x%llx",
		    __func__, acct->resp_desc->addr);
		goto read_toc_out;
	}

	/* Move index for toc descriptor */
	acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
	    acct->resp_desc, &(acct->resp_idx));

	DPRINTF("%s: writing toc_data to 0x%llx size %d at "
	    "local idx %d req_idx %d global_idx %d",
	    __func__, acct->resp_desc->addr, acct->resp_desc->len,
	    acct->resp_idx, acct->req_idx, acct->idx);

	if (write_mem(acct->resp_desc->addr, toc_data, sizeof(toc_data))) {
		log_warnx("%s: unable to write toc descriptor data @ 0x%llx",
		    __func__, acct->resp_desc->addr);
	} else {
		ret = 1;
		dev->isr = 1;
		/* Move ring indexes */
		vioscsi_next_ring_item(vq_info, acct->avail, acct->used,
		    acct->req_desc, acct->req_idx);
	}

read_toc_out:
	return (ret);
}

static int
vioscsi_handle_read_disc_info(struct virtio_dev *dev,
    struct virtio_vq_info *vq_info, struct virtio_scsi_req_hdr *req,
    struct virtio_vq_acct *acct)
{
	int ret = 0;
	struct virtio_scsi_res_hdr resp;

	DPRINTF("%s: Disc Info %x", __func__,
		((struct scsi_read_disc_information *)(req->cdb))->byte2);

	/* send back unsupported */
	memset(&resp, 0, sizeof(resp));
	vioscsi_prepare_resp(&resp,
	    VIRTIO_SCSI_S_OK, SCSI_CHECK, SKEY_ILLEGAL_REQUEST,
	    SENSE_ILLEGAL_CDB_FIELD, SENSE_DEFAULT_ASCQ);

	/* Move index for response */
	acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
	    acct->req_desc, &(acct->resp_idx));

	if (write_mem(acct->resp_desc->addr, &resp, sizeof(resp))) {
		log_warnx("%s: unable to set ERR status data @ 0x%llx",
		    __func__, acct->resp_desc->addr);
	} else {
		ret = 1;
		dev->isr = 1;
		/* Move ring indexes */
		vioscsi_next_ring_item(vq_info, acct->avail, acct->used,
		    acct->req_desc, acct->req_idx);
	}

	return (ret);
}

static int
vioscsi_handle_gesn(struct virtio_dev *dev,
    struct virtio_vq_info *vq_info, struct virtio_scsi_req_hdr *req,
    struct virtio_vq_acct *acct)
{
	int ret = 0;
	struct virtio_scsi_res_hdr resp;
	uint8_t gesn_reply[GESN_SIZE];
	struct scsi_gesn *gesn;
	struct scsi_gesn_event_header *gesn_event_header;
	struct scsi_gesn_power_event *gesn_power_event;
	struct vioscsi_dev *vioscsi = NULL;

	if (dev->device_id != PCI_PRODUCT_VIRTIO_SCSI)
		fatalx("%s: virtio device is not a scsi device", __func__);
	vioscsi = &dev->vioscsi;

	memset(&resp, 0, sizeof(resp));
	gesn = (struct scsi_gesn *)(req->cdb);
	DPRINTF("%s: GESN Method %s", __func__,
	    gesn->byte2 ? "Polling" : "Asynchronous");

	if (gesn->byte2 == 0) {
		/* we don't support asynchronous */
		vioscsi_prepare_resp(&resp,
		    VIRTIO_SCSI_S_OK, SCSI_CHECK, SKEY_ILLEGAL_REQUEST,
		    SENSE_ILLEGAL_CDB_FIELD, SENSE_DEFAULT_ASCQ);

		/* Move index for response */
		acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
		    acct->req_desc, &(acct->resp_idx));

		if (write_mem(acct->resp_desc->addr, &resp, sizeof(resp))) {
			log_warnx("%s: unable to set ERR status  data @ 0x%llx",
			    __func__, acct->resp_desc->addr);
			goto gesn_out;
		}

		ret = 1;
		dev->isr = 1;
		/* Move ring indexes */
		vioscsi_next_ring_item(vq_info, acct->avail, acct->used,
		    acct->req_desc, acct->req_idx);

		goto gesn_out;
	}
	memset(gesn_reply, 0, sizeof(gesn_reply));
	gesn_event_header = (struct scsi_gesn_event_header *)(gesn_reply);
	gesn_power_event = (struct scsi_gesn_power_event *)(gesn_reply + 4);
	/* set event header length and notification */
	_lto2b(GESN_HEADER_LEN, gesn_event_header->length);
	gesn_event_header->notification = GESN_NOTIFY_POWER_MGMT;
	gesn_event_header->supported_event = GESN_EVENT_POWER_MGMT;

	/* set event descriptor */
	gesn_power_event->event_code = GESN_CODE_NOCHG;
	if (vioscsi->locked)
		gesn_power_event->status = GESN_STATUS_ACTIVE;
	else
		gesn_power_event->status = GESN_STATUS_IDLE;

	/* Move index for response */
	acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
	    acct->req_desc, &(acct->resp_idx));

	DPRINTF("%s: writing resp to 0x%llx size %d at local "
	    "idx %d req_idx %d global_idx %d",
	    __func__, acct->resp_desc->addr, acct->resp_desc->len,
	    acct->resp_idx, acct->req_idx, acct->idx);

	if (write_mem(acct->resp_desc->addr, &resp, sizeof(resp))) {
		log_warnx("%s: unable to write OK resp status "
		    "data @ 0x%llx", __func__, acct->resp_desc->addr);
		goto gesn_out;
	}

	/* Move index for gesn_reply */
	acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
	    acct->resp_desc, &(acct->resp_idx));

	DPRINTF("%s: writing gesn_reply to 0x%llx size %d at "
	    "local idx %d req_idx %d global_idx %d",
	    __func__, acct->resp_desc->addr, acct->resp_desc->len,
	    acct->resp_idx, acct->req_idx, acct->idx);

	if (write_mem(acct->resp_desc->addr, gesn_reply, sizeof(gesn_reply))) {
		log_warnx("%s: unable to write gesn_reply"
		    " response to gpa @ 0x%llx",
		    __func__, acct->resp_desc->addr);
	} else {
		ret = 1;
		dev->isr = 1;
		/* Move ring indexes */
		vioscsi_next_ring_item(vq_info, acct->avail, acct->used,
		    acct->req_desc, acct->req_idx);
	}

gesn_out:
	return (ret);
}

static int
vioscsi_handle_get_config(struct virtio_dev *dev,
    struct virtio_vq_info *vq_info, struct virtio_scsi_req_hdr *req,
    struct virtio_vq_acct *acct)
{
	int ret = 0;
	struct virtio_scsi_res_hdr resp;
	uint8_t *get_conf_reply;
	struct scsi_config_feature_header *config_feature_header;
	struct scsi_config_generic_descriptor *config_generic_desc;
	struct scsi_config_profile_descriptor *config_profile_desc;
	struct scsi_config_core_descriptor *config_core_desc;
	struct scsi_config_morphing_descriptor *config_morphing_desc;
	struct scsi_config_remove_media_descriptor *config_remove_media_desc;
	struct scsi_config_random_read_descriptor *config_random_read_desc;
	struct vioscsi_dev *vioscsi = NULL;

#if DEBUG
	struct scsi_get_configuration *get_configuration =
	    (struct scsi_get_configuration *)(req->cdb);
	log_debug("%s: Conf RT %x Feature %d Len %d", __func__,
	    get_configuration->byte2, _2btol(get_configuration->feature),
	    _2btol(get_configuration->length));
#endif /* DEBUG */

	if (dev->device_id != PCI_PRODUCT_VIRTIO_SCSI)
		fatalx("%s: virtio device is not a scsi device", __func__);
	vioscsi = &dev->vioscsi;

	get_conf_reply = (uint8_t*)calloc(G_CONFIG_REPLY_SIZE, sizeof(uint8_t));

	if (get_conf_reply == NULL)
		goto get_config_out;

	/*
	 * Use MMC-5 6.6 for structure and
	 * MMC-5 5.2 to send back:
	 * feature header - 8 bytes
	 * feature descriptor for profile list - 8 bytes
	 * feature descriptor for core feature - 12 bytes
	 * feature descriptor for morphing feature - 8 bytes
	 * feature descriptor for removable media - 8 bytes
	 * feature descriptor for random read feature - 12 bytes
	 */

	config_feature_header =
	    (struct scsi_config_feature_header *)(get_conf_reply);
	config_generic_desc =
	    (struct scsi_config_generic_descriptor *)(get_conf_reply + 8);
	config_profile_desc =
	    (struct scsi_config_profile_descriptor *)(get_conf_reply + 12);
	config_core_desc =
	    (struct scsi_config_core_descriptor *)(get_conf_reply + 16);
	config_morphing_desc =
	    (struct scsi_config_morphing_descriptor *)(get_conf_reply + 28);
	config_remove_media_desc =
	    (struct scsi_config_remove_media_descriptor *)(get_conf_reply + 36);
	config_random_read_desc =
	    (struct scsi_config_random_read_descriptor *)(get_conf_reply + 44);

	/* set size to be get_conf_reply - size field */
	_lto4b(G_CONFIG_REPLY_SIZE_HEX, config_feature_header->length);
	/* set current profile to be non-conforming */
	_lto2b(CONFIG_PROFILE_NON_CONFORM,
	    config_feature_header->current_profile);

	/* fill out profile list feature */
	_lto2b(CONFIG_FEATURE_CODE_PROFILE, config_generic_desc->feature_code);
	config_generic_desc->byte3 = CONFIG_PROFILELIST_BYTE3;
	config_generic_desc->length = CONFIG_PROFILELIST_LENGTH;
	/* fill out profile descriptor for NON_COFORM */
	_lto2b(CONFIG_PROFILE_NON_CONFORM, config_profile_desc->profile_number);
	config_profile_desc->byte3 = CONFIG_PROFILE_BYTE3;

	/* fill out core feature */
	_lto2b(CONFIG_FEATURE_CODE_CORE, config_core_desc->feature_code);
	config_core_desc->byte3 = CONFIG_CORE_BYTE3;
	config_core_desc->length = CONFIG_CORE_LENGTH;
	_lto4b(CONFIG_CORE_PHY_SCSI, config_core_desc->phy_std);

	/* fill out morphing feature */
	_lto2b(CONFIG_FEATURE_CODE_MORPHING,
	    config_morphing_desc->feature_code);
	config_morphing_desc->byte3 = CONFIG_MORPHING_BYTE3;
	config_morphing_desc->length = CONFIG_MORPHING_LENGTH;
	config_morphing_desc->byte5 = CONFIG_MORPHING_BYTE5;

	/* fill out removable media feature */
	_lto2b(CONFIG_FEATURE_CODE_REMOVE_MEDIA,
	    config_remove_media_desc->feature_code);
	config_remove_media_desc->byte3 = CONFIG_REMOVE_MEDIA_BYTE3;
	config_remove_media_desc->length = CONFIG_REMOVE_MEDIA_LENGTH;
	config_remove_media_desc->byte5 = CONFIG_REMOVE_MEDIA_BYTE5;

	/* fill out random read feature */
	_lto2b(CONFIG_FEATURE_CODE_RANDOM_READ,
	    config_random_read_desc->feature_code);
	config_random_read_desc->byte3 = CONFIG_RANDOM_READ_BYTE3;
	config_random_read_desc->length = CONFIG_RANDOM_READ_LENGTH;
	if (vioscsi->n_blocks >= UINT32_MAX)
		_lto4b(UINT32_MAX, config_random_read_desc->block_size);
	else
		_lto4b(vioscsi->n_blocks - 1,
		    config_random_read_desc->block_size);
	_lto2b(CONFIG_RANDOM_READ_BLOCKING_TYPE,
	    config_random_read_desc->blocking_type);

	memset(&resp, 0, sizeof(resp));
	vioscsi_prepare_resp(&resp, VIRTIO_SCSI_S_OK, SCSI_OK, 0, 0, 0);

	/* Move index for response */
	acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
	    acct->req_desc, &(acct->resp_idx));

	DPRINTF("%s: writing resp to 0x%llx size %d at local "
	    "idx %d req_idx %d global_idx %d",
	    __func__, acct->resp_desc->addr, acct->resp_desc->len,
	    acct->resp_idx, acct->req_idx, acct->idx);

	if (write_mem(acct->resp_desc->addr, &resp, sizeof(resp))) {
		log_warnx("%s: unable to set Ok status data @ 0x%llx",
		    __func__, acct->resp_desc->addr);
		goto free_get_config;
	}

	/* Move index for get_conf_reply */
	acct->resp_desc = vioscsi_next_ring_desc(vq_info, acct->desc,
	    acct->resp_desc, &(acct->resp_idx));

	DPRINTF("%s: writing get_conf_reply to 0x%llx size %d "
	    "at local idx %d req_idx %d global_idx %d",
	    __func__, acct->resp_desc->addr, acct->resp_desc->len,
	    acct->resp_idx, acct->req_idx, acct->idx);

	if (write_mem(acct->resp_desc->addr, get_conf_reply,
	    G_CONFIG_REPLY_SIZE)) {
		log_warnx("%s: unable to write get_conf_reply"
		    " response to gpa @ 0x%llx",
		    __func__, acct->resp_desc->addr);
	} else {
		ret = 1;
		dev->isr = 1;
		/* Move ring indexes */
		vioscsi_next_ring_item(vq_info, acct->avail, acct->used,
		    acct->req_desc, acct->req_idx);
	}

free_get_config:
	free(get_conf_reply);
get_config_out:
	return (ret);
}

int
vioscsi_io(int dir, uint16_t reg, uint32_t *data, uint8_t *intr,
    void *cookie, uint8_t sz)
{
	struct virtio_dev *dev = (struct virtio_dev *)cookie;
	struct vioscsi_dev *vioscsi = NULL;

	if (dev->device_id != PCI_PRODUCT_VIRTIO_SCSI)
		fatalx("%s: virtio device is not a scsi device", __func__);
	vioscsi = &dev->vioscsi;

	*intr = 0xFF;

	DPRINTF("%s: request %s reg %s sz %u", __func__,
	    dir ? "READ" : "WRITE", vioscsi_reg_name(reg), sz);

	if (dir == VEI_DIR_OUT) {
		switch (reg) {
		case VIRTIO_SCSI_CONFIG_SENSE_SIZE:
			/* Support writing to sense size register. */
			if (*data != VIOSCSI_SENSE_LEN)
				log_warnx("%s: guest write to sense size "
				    "register ignored", __func__);
			break;
		case VIRTIO_SCSI_CONFIG_CDB_SIZE:
			/* Support writing CDB size. */
			if (*data != VIOSCSI_CDB_LEN)
				log_warnx("%s: guest write to cdb size "
				    "register ignored", __func__);
			break;
		default:
			log_warnx("%s: invalid register 0x%04x", __func__, reg);
			break;
		}
	} else {
		switch (reg) {
		case VIRTIO_SCSI_CONFIG_NUM_QUEUES:
			/* Number of request queues, not number of all queues. */
			if (sz == 4)
				*data = (uint32_t)(VIOSCSI_NUM_REQ_QUEUES);
			else
				log_warnx("%s: unaligned read of num queues "
				    "register", __func__);
			break;
		case VIRTIO_SCSI_CONFIG_SEG_MAX:
			if (sz == 4)
				*data = (uint32_t)(VIOSCSI_SEG_MAX);
			else
				log_warnx("%s: unaligned read of seg max "
				    "register", __func__);
			break;
		case VIRTIO_SCSI_CONFIG_MAX_SECTORS:
			if (sz == 4)
				*data = (uint32_t)(vioscsi->max_xfer);
			else
				log_warnx("%s: unaligned read of max sectors "
				    "register", __func__);
			break;
		case VIRTIO_SCSI_CONFIG_CMD_PER_LUN:
			if (sz == 4)
				*data = (uint32_t)(VIOSCSI_CMD_PER_LUN);
			else
				log_warnx("%s: unaligned read of cmd per lun "
				    "register", __func__);
			break;
		case VIRTIO_SCSI_CONFIG_EVENT_INFO_SIZE:
			*data = 0;
			break;
		case VIRTIO_SCSI_CONFIG_SENSE_SIZE:
			if (sz == 4)
				*data = (uint32_t)(VIOSCSI_SENSE_LEN);
			else
				log_warnx("%s: unaligned read of sense size "
				    "register", __func__);
			break;
		case VIRTIO_SCSI_CONFIG_CDB_SIZE:
			if (sz == 4)
				*data = (uint32_t)(VIOSCSI_CDB_LEN);
			else
				log_warnx("%s: unaligned read of cdb size "
				    "register", __func__);
			break;
		case VIRTIO_SCSI_CONFIG_MAX_CHANNEL:
			/* defined by standard to be zero */
			*data = 0;
			break;
		case VIRTIO_SCSI_CONFIG_MAX_TARGET:
			if (sz == 2)
				*data = (uint32_t)(VIOSCSI_MAX_TARGET);
			else
				log_warnx("%s: unaligned read of max target "
				    "register", __func__);
			break;
		case VIRTIO_SCSI_CONFIG_MAX_LUN:
			if (sz == 4)
				*data = (uint32_t)(VIOSCSI_MAX_LUN);
			else
				log_warnx("%s: unaligned read of max lun "
				    "register", __func__);
			break;
		default:
			log_warnx("%s: invalid register 0x%04x", __func__, reg);
			*data = (uint32_t)(-1);
		}
	}

	return (0);
}

/*
 * Process message(s) in the queue(s)
 * vioscsi driver will be placing the following in the queue for each iteration
 * virtio_scsi_req_hdr with a possible SCSI_DATA_OUT buffer
 * along with a virtio_scsi_res_hdr with a possible SCSI_DATA_IN buffer
 * for consumption.
 *
 * Return 1 if an interrupt should be generated (response written)
 *        0 otherwise
 */
int
vioscsi_notifyq(struct virtio_dev *dev, uint16_t vq_idx)
{
	size_t cnt;
	int ret = 0;
	char *vr;
	struct virtio_scsi_req_hdr req;
	struct virtio_scsi_res_hdr resp;
	struct virtio_vq_acct acct;
	struct virtio_vq_info *vq_info;

	vq_info = &dev->vq[vq_idx];
	vr = vq_info->q_hva;
	if (vr == NULL)
		fatalx("%s: null vring", __func__);

	/* Compute offsets in ring of descriptors, avail ring, and used ring */
	acct.desc = (struct vring_desc *)(vr);
	acct.avail = (struct vring_avail *)(vr + vq_info->vq_availoffset);
	acct.used = (struct vring_used *)(vr + vq_info->vq_usedoffset);

	acct.idx = vq_info->last_avail & vq_info->mask;

	if ((acct.avail->idx & vq_info->mask) == acct.idx) {
		log_debug("%s - nothing to do?", __func__);
		return (0);
	}

	cnt = 0;
	while (acct.idx != (acct.avail->idx & vq_info->mask)) {

		/* Guard against infinite descriptor chains */
		if (++cnt >= vq_info->qs) {
			log_warnx("%s: invalid descriptor table", __func__);
			goto out;
		}

		acct.req_idx = acct.avail->ring[acct.idx] & vq_info->mask;
		acct.req_desc = &(acct.desc[acct.req_idx]);

		/* Clear resp for next message */
		memset(&resp, 0, sizeof(resp));

		if ((acct.req_desc->flags & VRING_DESC_F_NEXT) == 0) {
			log_warnx("%s: unchained req descriptor received "
			    "(idx %d)", __func__, acct.req_idx);
			goto out;
		}

		/* Read command from descriptor ring */
		if (read_mem(acct.req_desc->addr, &req, sizeof(req))) {
			log_warnx("%s: command read_mem error @ 0x%llx",
			    __func__, acct.req_desc->addr);
			goto out;
		}

		/*
		 * req.lun is defined by virtio as
		 * lun[0] - Always set to 1
		 * lun[1] - Target, negotiated as VIOSCSI_MAX_TARGET
		 * lun[2-3] - represent single level LUN structure
		 * lun[4-7] - Zero
		 * At this current time, we are only servicing one device per
		 * bus (1:0:X:0).
		 *
		 * Various implementations will attempt to scan all possible
		 * targets (256) looking for devices or scan for all possible
		 * LUNs in a single level.  When Target is greater than
		 * VIOSCSI_MAX_TARGET or when lun[3] is greater than zero,
		 * respond with a BAD_TARGET response.
		 */
		if (req.lun[1] >= VIOSCSI_MAX_TARGET || req.lun[3] > 0) {
			DPRINTF("%s: Ignore CMD 0x%02x,%s on lun %u:%u:%u:%u",
			    __func__, req.cdb[0], vioscsi_op_names(req.cdb[0]),
			    req.lun[0], req.lun[1], req.lun[2], req.lun[3]);
			/* Move index for response */
			acct.resp_desc = vioscsi_next_ring_desc(vq_info,
			    acct.desc, acct.req_desc, &(acct.resp_idx));

			vioscsi_prepare_resp(&resp,
			    VIRTIO_SCSI_S_BAD_TARGET, SCSI_OK, 0, 0, 0);

			if (acct.resp_desc->len > sizeof(resp)) {
				log_warnx("%s: invalid descriptor length",
				    __func__);
				goto out;
			}
			if (write_mem(acct.resp_desc->addr, &resp,
				sizeof(resp))) {
				log_warnx("%s: unable to write BAD_TARGET"
				    " resp status data @ 0x%llx",
				    __func__, acct.resp_desc->addr);
				goto out;
			}

			ret = 1;
			dev->isr = 1;

			/* Move ring indexes (updates the used ring index) */
			vioscsi_next_ring_item(vq_info, acct.avail, acct.used,
			    acct.req_desc, acct.req_idx);
			goto next_msg;
		}

		DPRINTF("%s: Queue %d id 0x%llx lun %u:%u:%u:%u"
		    " cdb OP 0x%02x,%s",
		    __func__, vq_idx, req.id, req.lun[0], req.lun[1],
		    req.lun[2], req.lun[3],req.cdb[0],
		    vioscsi_op_names(req.cdb[0]));

		/* opcode is first byte */
		switch (req.cdb[0]) {
		case TEST_UNIT_READY:
		case START_STOP:
			ret = vioscsi_handle_tur(dev, vq_info, &req, &acct);
			break;
		case PREVENT_ALLOW:
			ret = vioscsi_handle_prevent_allow(dev, vq_info, &req,
			    &acct);
			break;
		case READ_TOC:
			ret = vioscsi_handle_read_toc(dev, vq_info, &req,
			    &acct);
			break;
		case READ_CAPACITY:
			ret = vioscsi_handle_read_capacity(dev, vq_info, &req,
			    &acct);
			break;
		case READ_CAPACITY_16:
			ret = vioscsi_handle_read_capacity_16(dev, vq_info,
			    &req, &acct);
			break;
		case READ_COMMAND:
			ret = vioscsi_handle_read_6(dev, vq_info, &req, &acct);
			break;
		case READ_10:
			ret = vioscsi_handle_read_10(dev, vq_info, &req, &acct);
			break;
		case INQUIRY:
			ret = vioscsi_handle_inquiry(dev, vq_info, &req, &acct);
			break;
		case MODE_SENSE:
			ret = vioscsi_handle_mode_sense(dev, vq_info, &req,
			    &acct);
			break;
		case MODE_SENSE_BIG:
			ret = vioscsi_handle_mode_sense_big(dev, vq_info, &req,
			    &acct);
			break;
		case GET_EVENT_STATUS_NOTIFICATION:
			ret = vioscsi_handle_gesn(dev, vq_info, &req, &acct);
			break;
		case READ_DISC_INFORMATION:
			ret = vioscsi_handle_read_disc_info(dev, vq_info, &req,
			    &acct);
			break;
		case GET_CONFIGURATION:
			ret = vioscsi_handle_get_config(dev, vq_info, &req,
			    &acct);
			break;
		case MECHANISM_STATUS:
			ret = vioscsi_handle_mechanism_status(dev, vq_info,
			    &req, &acct);
			break;
		case REPORT_LUNS:
			ret = vioscsi_handle_report_luns(dev, vq_info, &req,
			    &acct);
			break;
		default:
			log_warnx("%s: unsupported opcode 0x%02x,%s",
			    __func__, req.cdb[0], vioscsi_op_names(req.cdb[0]));
			/* Move ring indexes */
			vioscsi_next_ring_item(vq_info, acct.avail, acct.used,
			    acct.req_desc, acct.req_idx);
			break;
		}
next_msg:
		/* Increment to the next queue slot */
		acct.idx = (acct.idx + 1) & vq_info->mask;
	}
out:
	return (ret);
}
