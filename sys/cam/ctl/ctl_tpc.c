/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014 Alexander Motin <mav@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/malloc.h>
#include <sys/conf.h>
#include <sys/queue.h>
#include <sys/sysctl.h>
#include <sys/nv.h>
#include <sys/dnv.h>
#include <machine/atomic.h>

#include <cam/cam.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_da.h>
#include <cam/ctl/ctl_io.h>
#include <cam/ctl/ctl.h>
#include <cam/ctl/ctl_frontend.h>
#include <cam/ctl/ctl_util.h>
#include <cam/ctl/ctl_backend.h>
#include <cam/ctl/ctl_ioctl.h>
#include <cam/ctl/ctl_ha.h>
#include <cam/ctl/ctl_private.h>
#include <cam/ctl/ctl_debug.h>
#include <cam/ctl/ctl_scsi_all.h>
#include <cam/ctl/ctl_tpc.h>
#include <cam/ctl/ctl_error.h>

#define	TPC_MAX_CSCDS	64
#define	TPC_MAX_SEGS	64
#define	TPC_MAX_SEG	0
#define	TPC_MAX_LIST	8192
#define	TPC_MAX_INLINE	0
#define	TPC_MAX_LISTS	255
#define	TPC_MAX_IO_SIZE	(1024 * 1024)
#define	TPC_MAX_IOCHUNK_SIZE	(TPC_MAX_IO_SIZE * 16)
#define	TPC_MIN_TOKEN_TIMEOUT	1
#define	TPC_DFL_TOKEN_TIMEOUT	60
#define	TPC_MAX_TOKEN_TIMEOUT	600

MALLOC_DEFINE(M_CTL_TPC, "ctltpc", "CTL TPC");

typedef enum {
	TPC_ERR_RETRY		= 0x000,
	TPC_ERR_FAIL		= 0x001,
	TPC_ERR_MASK		= 0x0ff,
	TPC_ERR_NO_DECREMENT	= 0x100
} tpc_error_action;

struct tpc_list;
TAILQ_HEAD(runl, tpc_io);
struct tpc_io {
	union ctl_io		*io;
	uint8_t			 target;
	uint32_t		 cscd;
	uint64_t		 lun;
	uint8_t			*buf;
	struct tpc_list		*list;
	struct runl		 run;
	TAILQ_ENTRY(tpc_io)	 rlinks;
	TAILQ_ENTRY(tpc_io)	 links;
};

struct tpc_token {
	uint8_t			 token[512];
	uint64_t		 lun;
	uint32_t		 blocksize;
	uint8_t			*params;
	struct scsi_range_desc	*range;
	int			 nrange;
	int			 active;
	time_t			 last_active;
	uint32_t		 timeout;
	TAILQ_ENTRY(tpc_token)	 links;
};

struct tpc_list {
	uint8_t			 service_action;
	int			 init_port;
	uint32_t		 init_idx;
	uint32_t		 list_id;
	uint8_t			 flags;
	uint8_t			*params;
	struct scsi_ec_cscd	*cscd;
	struct scsi_ec_segment	*seg[TPC_MAX_SEGS];
	uint8_t			*inl;
	int			 ncscd;
	int			 nseg;
	int			 leninl;
	struct tpc_token	*token;
	struct scsi_range_desc	*range;
	int			 nrange;
	off_t			 offset_into_rod;

	int			 curseg;
	off_t			 cursectors;
	off_t			 curbytes;
	int			 curops;
	int			 stage;
	off_t			 segsectors;
	off_t			 segbytes;
	int			 tbdio;
	int			 error;
	int			 abort;
	int			 completed;
	time_t			 last_active;
	TAILQ_HEAD(, tpc_io)	 allio;
	struct scsi_sense_data	 fwd_sense_data;
	uint8_t			 fwd_sense_len;
	uint8_t			 fwd_scsi_status;
	uint8_t			 fwd_target;
	uint16_t		 fwd_cscd;
	struct scsi_sense_data	 sense_data;
	uint8_t			 sense_len;
	uint8_t			 scsi_status;
	struct ctl_scsiio	*ctsio;
	struct ctl_lun		*lun;
	int			 res_token_valid;
	uint8_t			 res_token[512];
	TAILQ_ENTRY(tpc_list)	 links;
};

static void
tpc_timeout(void *arg)
{
	struct ctl_softc *softc = arg;
	struct ctl_lun *lun;
	struct tpc_token *token, *ttoken;
	struct tpc_list *list, *tlist;

	/* Free completed lists with expired timeout. */
	STAILQ_FOREACH(lun, &softc->lun_list, links) {
		mtx_lock(&lun->lun_lock);
		TAILQ_FOREACH_SAFE(list, &lun->tpc_lists, links, tlist) {
			if (!list->completed || time_uptime < list->last_active +
			    TPC_DFL_TOKEN_TIMEOUT)
				continue;
			TAILQ_REMOVE(&lun->tpc_lists, list, links);
			free(list, M_CTL);
		}
		mtx_unlock(&lun->lun_lock);
	}

	/* Free inactive ROD tokens with expired timeout. */
	mtx_lock(&softc->tpc_lock);
	TAILQ_FOREACH_SAFE(token, &softc->tpc_tokens, links, ttoken) {
		if (token->active ||
		    time_uptime < token->last_active + token->timeout + 1)
			continue;
		TAILQ_REMOVE(&softc->tpc_tokens, token, links);
		free(token->params, M_CTL);
		free(token, M_CTL);
	}
	mtx_unlock(&softc->tpc_lock);
	callout_schedule(&softc->tpc_timeout, hz);
}

void
ctl_tpc_init(struct ctl_softc *softc)
{

	mtx_init(&softc->tpc_lock, "CTL TPC mutex", NULL, MTX_DEF);
	TAILQ_INIT(&softc->tpc_tokens);
	callout_init_mtx(&softc->tpc_timeout, &softc->ctl_lock, 0);
	callout_reset(&softc->tpc_timeout, hz, tpc_timeout, softc);
}

void
ctl_tpc_shutdown(struct ctl_softc *softc)
{
	struct tpc_token *token;

	callout_drain(&softc->tpc_timeout);

	/* Free ROD tokens. */
	mtx_lock(&softc->tpc_lock);
	while ((token = TAILQ_FIRST(&softc->tpc_tokens)) != NULL) {
		TAILQ_REMOVE(&softc->tpc_tokens, token, links);
		free(token->params, M_CTL);
		free(token, M_CTL);
	}
	mtx_unlock(&softc->tpc_lock);
	mtx_destroy(&softc->tpc_lock);
}

void
ctl_tpc_lun_init(struct ctl_lun *lun)
{

	TAILQ_INIT(&lun->tpc_lists);
}

void
ctl_tpc_lun_clear(struct ctl_lun *lun, uint32_t initidx)
{
	struct tpc_list *list, *tlist;

	TAILQ_FOREACH_SAFE(list, &lun->tpc_lists, links, tlist) {
		if (initidx != -1 && list->init_idx != initidx)
			continue;
		if (!list->completed)
			continue;
		TAILQ_REMOVE(&lun->tpc_lists, list, links);
		free(list, M_CTL);
	}
}

void
ctl_tpc_lun_shutdown(struct ctl_lun *lun)
{
	struct ctl_softc *softc = lun->ctl_softc;
	struct tpc_list *list;
	struct tpc_token *token, *ttoken;

	/* Free lists for this LUN. */
	while ((list = TAILQ_FIRST(&lun->tpc_lists)) != NULL) {
		TAILQ_REMOVE(&lun->tpc_lists, list, links);
		KASSERT(list->completed,
		    ("Not completed TPC (%p) on shutdown", list));
		free(list, M_CTL);
	}

	/* Free ROD tokens for this LUN. */
	mtx_lock(&softc->tpc_lock);
	TAILQ_FOREACH_SAFE(token, &softc->tpc_tokens, links, ttoken) {
		if (token->lun != lun->lun || token->active)
			continue;
		TAILQ_REMOVE(&softc->tpc_tokens, token, links);
		free(token->params, M_CTL);
		free(token, M_CTL);
	}
	mtx_unlock(&softc->tpc_lock);
}

int
ctl_inquiry_evpd_tpc(struct ctl_scsiio *ctsio, int alloc_len)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_vpd_tpc *tpc_ptr;
	struct scsi_vpd_tpc_descriptor *d_ptr;
	struct scsi_vpd_tpc_descriptor_bdrl *bdrl_ptr;
	struct scsi_vpd_tpc_descriptor_sc *sc_ptr;
	struct scsi_vpd_tpc_descriptor_sc_descr *scd_ptr;
	struct scsi_vpd_tpc_descriptor_pd *pd_ptr;
	struct scsi_vpd_tpc_descriptor_sd *sd_ptr;
	struct scsi_vpd_tpc_descriptor_sdid *sdid_ptr;
	struct scsi_vpd_tpc_descriptor_rtf *rtf_ptr;
	struct scsi_vpd_tpc_descriptor_rtf_block *rtfb_ptr;
	struct scsi_vpd_tpc_descriptor_srt *srt_ptr;
	struct scsi_vpd_tpc_descriptor_srtd *srtd_ptr;
	struct scsi_vpd_tpc_descriptor_gco *gco_ptr;
	int data_len;

	data_len = sizeof(struct scsi_vpd_tpc) +
	    sizeof(struct scsi_vpd_tpc_descriptor_bdrl) +
	    roundup2(sizeof(struct scsi_vpd_tpc_descriptor_sc) +
	     2 * sizeof(struct scsi_vpd_tpc_descriptor_sc_descr) + 11, 4) +
	    sizeof(struct scsi_vpd_tpc_descriptor_pd) +
	    roundup2(sizeof(struct scsi_vpd_tpc_descriptor_sd) + 4, 4) +
	    roundup2(sizeof(struct scsi_vpd_tpc_descriptor_sdid) + 2, 4) +
	    sizeof(struct scsi_vpd_tpc_descriptor_rtf) +
	     sizeof(struct scsi_vpd_tpc_descriptor_rtf_block) +
	    sizeof(struct scsi_vpd_tpc_descriptor_srt) +
	     2*sizeof(struct scsi_vpd_tpc_descriptor_srtd) +
	    sizeof(struct scsi_vpd_tpc_descriptor_gco);

	ctsio->kern_data_ptr = malloc(data_len, M_CTL, M_WAITOK | M_ZERO);
	tpc_ptr = (struct scsi_vpd_tpc *)ctsio->kern_data_ptr;
	ctsio->kern_rel_offset = 0;
	ctsio->kern_sg_entries = 0;
	ctsio->kern_data_len = min(data_len, alloc_len);
	ctsio->kern_total_len = ctsio->kern_data_len;

	/*
	 * The control device is always connected.  The disk device, on the
	 * other hand, may not be online all the time.
	 */
	if (lun != NULL)
		tpc_ptr->device = (SID_QUAL_LU_CONNECTED << 5) |
				     lun->be_lun->lun_type;
	else
		tpc_ptr->device = (SID_QUAL_LU_OFFLINE << 5) | T_DIRECT;
	tpc_ptr->page_code = SVPD_SCSI_TPC;
	scsi_ulto2b(data_len - 4, tpc_ptr->page_length);

	/* Block Device ROD Limits */
	d_ptr = (struct scsi_vpd_tpc_descriptor *)&tpc_ptr->descr[0];
	bdrl_ptr = (struct scsi_vpd_tpc_descriptor_bdrl *)d_ptr;
	scsi_ulto2b(SVPD_TPC_BDRL, bdrl_ptr->desc_type);
	scsi_ulto2b(sizeof(*bdrl_ptr) - 4, bdrl_ptr->desc_length);
	scsi_ulto2b(TPC_MAX_SEGS, bdrl_ptr->maximum_ranges);
	scsi_ulto4b(TPC_MAX_TOKEN_TIMEOUT,
	    bdrl_ptr->maximum_inactivity_timeout);
	scsi_ulto4b(TPC_DFL_TOKEN_TIMEOUT,
	    bdrl_ptr->default_inactivity_timeout);
	scsi_u64to8b(0, bdrl_ptr->maximum_token_transfer_size);
	scsi_u64to8b(0, bdrl_ptr->optimal_transfer_count);

	/* Supported commands */
	d_ptr = (struct scsi_vpd_tpc_descriptor *)
	    (&d_ptr->parameters[0] + scsi_2btoul(d_ptr->desc_length));
	sc_ptr = (struct scsi_vpd_tpc_descriptor_sc *)d_ptr;
	scsi_ulto2b(SVPD_TPC_SC, sc_ptr->desc_type);
	sc_ptr->list_length = 2 * sizeof(*scd_ptr) + 11;
	scsi_ulto2b(roundup2(1 + sc_ptr->list_length, 4), sc_ptr->desc_length);
	scd_ptr = &sc_ptr->descr[0];
	scd_ptr->opcode = EXTENDED_COPY;
	scd_ptr->sa_length = 5;
	scd_ptr->supported_service_actions[0] = EC_EC_LID1;
	scd_ptr->supported_service_actions[1] = EC_EC_LID4;
	scd_ptr->supported_service_actions[2] = EC_PT;
	scd_ptr->supported_service_actions[3] = EC_WUT;
	scd_ptr->supported_service_actions[4] = EC_COA;
	scd_ptr = (struct scsi_vpd_tpc_descriptor_sc_descr *)
	    &scd_ptr->supported_service_actions[scd_ptr->sa_length];
	scd_ptr->opcode = RECEIVE_COPY_STATUS;
	scd_ptr->sa_length = 6;
	scd_ptr->supported_service_actions[0] = RCS_RCS_LID1;
	scd_ptr->supported_service_actions[1] = RCS_RCFD;
	scd_ptr->supported_service_actions[2] = RCS_RCS_LID4;
	scd_ptr->supported_service_actions[3] = RCS_RCOP;
	scd_ptr->supported_service_actions[4] = RCS_RRTI;
	scd_ptr->supported_service_actions[5] = RCS_RART;

	/* Parameter data. */
	d_ptr = (struct scsi_vpd_tpc_descriptor *)
	    (&d_ptr->parameters[0] + scsi_2btoul(d_ptr->desc_length));
	pd_ptr = (struct scsi_vpd_tpc_descriptor_pd *)d_ptr;
	scsi_ulto2b(SVPD_TPC_PD, pd_ptr->desc_type);
	scsi_ulto2b(sizeof(*pd_ptr) - 4, pd_ptr->desc_length);
	scsi_ulto2b(TPC_MAX_CSCDS, pd_ptr->maximum_cscd_descriptor_count);
	scsi_ulto2b(TPC_MAX_SEGS, pd_ptr->maximum_segment_descriptor_count);
	scsi_ulto4b(TPC_MAX_LIST, pd_ptr->maximum_descriptor_list_length);
	scsi_ulto4b(TPC_MAX_INLINE, pd_ptr->maximum_inline_data_length);

	/* Supported Descriptors */
	d_ptr = (struct scsi_vpd_tpc_descriptor *)
	    (&d_ptr->parameters[0] + scsi_2btoul(d_ptr->desc_length));
	sd_ptr = (struct scsi_vpd_tpc_descriptor_sd *)d_ptr;
	scsi_ulto2b(SVPD_TPC_SD, sd_ptr->desc_type);
	scsi_ulto2b(roundup2(sizeof(*sd_ptr) - 4 + 4, 4), sd_ptr->desc_length);
	sd_ptr->list_length = 4;
	sd_ptr->supported_descriptor_codes[0] = EC_SEG_B2B;
	sd_ptr->supported_descriptor_codes[1] = EC_SEG_VERIFY;
	sd_ptr->supported_descriptor_codes[2] = EC_SEG_REGISTER_KEY;
	sd_ptr->supported_descriptor_codes[3] = EC_CSCD_ID;

	/* Supported CSCD Descriptor IDs */
	d_ptr = (struct scsi_vpd_tpc_descriptor *)
	    (&d_ptr->parameters[0] + scsi_2btoul(d_ptr->desc_length));
	sdid_ptr = (struct scsi_vpd_tpc_descriptor_sdid *)d_ptr;
	scsi_ulto2b(SVPD_TPC_SDID, sdid_ptr->desc_type);
	scsi_ulto2b(roundup2(sizeof(*sdid_ptr) - 4 + 2, 4), sdid_ptr->desc_length);
	scsi_ulto2b(2, sdid_ptr->list_length);
	scsi_ulto2b(0xffff, &sdid_ptr->supported_descriptor_ids[0]);

	/* ROD Token Features */
	d_ptr = (struct scsi_vpd_tpc_descriptor *)
	    (&d_ptr->parameters[0] + scsi_2btoul(d_ptr->desc_length));
	rtf_ptr = (struct scsi_vpd_tpc_descriptor_rtf *)d_ptr;
	scsi_ulto2b(SVPD_TPC_RTF, rtf_ptr->desc_type);
	scsi_ulto2b(sizeof(*rtf_ptr) - 4 + sizeof(*rtfb_ptr), rtf_ptr->desc_length);
	rtf_ptr->remote_tokens = 0;
	scsi_ulto4b(TPC_MIN_TOKEN_TIMEOUT, rtf_ptr->minimum_token_lifetime);
	scsi_ulto4b(UINT32_MAX, rtf_ptr->maximum_token_lifetime);
	scsi_ulto4b(TPC_MAX_TOKEN_TIMEOUT,
	    rtf_ptr->maximum_token_inactivity_timeout);
	scsi_ulto2b(sizeof(*rtfb_ptr), rtf_ptr->type_specific_features_length);
	rtfb_ptr = (struct scsi_vpd_tpc_descriptor_rtf_block *)
	    &rtf_ptr->type_specific_features;
	rtfb_ptr->type_format = SVPD_TPC_RTF_BLOCK;
	scsi_ulto2b(sizeof(*rtfb_ptr) - 4, rtfb_ptr->desc_length);
	scsi_ulto2b(0, rtfb_ptr->optimal_length_granularity);
	scsi_u64to8b(0, rtfb_ptr->maximum_bytes);
	scsi_u64to8b(0, rtfb_ptr->optimal_bytes);
	scsi_u64to8b(UINT64_MAX, rtfb_ptr->optimal_bytes_to_token_per_segment);
	scsi_u64to8b(TPC_MAX_IOCHUNK_SIZE,
	    rtfb_ptr->optimal_bytes_from_token_per_segment);

	/* Supported ROD Tokens */
	d_ptr = (struct scsi_vpd_tpc_descriptor *)
	    (&d_ptr->parameters[0] + scsi_2btoul(d_ptr->desc_length));
	srt_ptr = (struct scsi_vpd_tpc_descriptor_srt *)d_ptr;
	scsi_ulto2b(SVPD_TPC_SRT, srt_ptr->desc_type);
	scsi_ulto2b(sizeof(*srt_ptr) - 4 + 2*sizeof(*srtd_ptr), srt_ptr->desc_length);
	scsi_ulto2b(2*sizeof(*srtd_ptr), srt_ptr->rod_type_descriptors_length);
	srtd_ptr = (struct scsi_vpd_tpc_descriptor_srtd *)
	    &srt_ptr->rod_type_descriptors;
	scsi_ulto4b(ROD_TYPE_AUR, srtd_ptr->rod_type);
	srtd_ptr->flags = SVPD_TPC_SRTD_TIN | SVPD_TPC_SRTD_TOUT;
	scsi_ulto2b(0, srtd_ptr->preference_indicator);
	srtd_ptr++;
	scsi_ulto4b(ROD_TYPE_BLOCK_ZERO, srtd_ptr->rod_type);
	srtd_ptr->flags = SVPD_TPC_SRTD_TIN;
	scsi_ulto2b(0, srtd_ptr->preference_indicator);

	/* General Copy Operations */
	d_ptr = (struct scsi_vpd_tpc_descriptor *)
	    (&d_ptr->parameters[0] + scsi_2btoul(d_ptr->desc_length));
	gco_ptr = (struct scsi_vpd_tpc_descriptor_gco *)d_ptr;
	scsi_ulto2b(SVPD_TPC_GCO, gco_ptr->desc_type);
	scsi_ulto2b(sizeof(*gco_ptr) - 4, gco_ptr->desc_length);
	scsi_ulto4b(TPC_MAX_LISTS, gco_ptr->total_concurrent_copies);
	scsi_ulto4b(TPC_MAX_LISTS, gco_ptr->maximum_identified_concurrent_copies);
	scsi_ulto4b(TPC_MAX_SEG, gco_ptr->maximum_segment_length);
	gco_ptr->data_segment_granularity = 0;
	gco_ptr->inline_data_granularity = 0;

	ctl_set_success(ctsio);
	ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	ctsio->be_move_done = ctl_config_move_done;
	ctl_datamove((union ctl_io *)ctsio);

	return (CTL_RETVAL_COMPLETE);
}

int
ctl_receive_copy_operating_parameters(struct ctl_scsiio *ctsio)
{
	struct scsi_receive_copy_operating_parameters *cdb;
	struct scsi_receive_copy_operating_parameters_data *data;
	int retval;
	int alloc_len, total_len;

	CTL_DEBUG_PRINT(("ctl_report_supported_tmf\n"));

	cdb = (struct scsi_receive_copy_operating_parameters *)ctsio->cdb;

	retval = CTL_RETVAL_COMPLETE;

	total_len = sizeof(*data) + 4;
	alloc_len = scsi_4btoul(cdb->length);

	ctsio->kern_data_ptr = malloc(total_len, M_CTL, M_WAITOK | M_ZERO);
	ctsio->kern_sg_entries = 0;
	ctsio->kern_rel_offset = 0;
	ctsio->kern_data_len = min(total_len, alloc_len);
	ctsio->kern_total_len = ctsio->kern_data_len;

	data = (struct scsi_receive_copy_operating_parameters_data *)ctsio->kern_data_ptr;
	scsi_ulto4b(sizeof(*data) - 4 + 4, data->length);
	data->snlid = RCOP_SNLID;
	scsi_ulto2b(TPC_MAX_CSCDS, data->maximum_cscd_descriptor_count);
	scsi_ulto2b(TPC_MAX_SEGS, data->maximum_segment_descriptor_count);
	scsi_ulto4b(TPC_MAX_LIST, data->maximum_descriptor_list_length);
	scsi_ulto4b(TPC_MAX_SEG, data->maximum_segment_length);
	scsi_ulto4b(TPC_MAX_INLINE, data->maximum_inline_data_length);
	scsi_ulto4b(0, data->held_data_limit);
	scsi_ulto4b(0, data->maximum_stream_device_transfer_size);
	scsi_ulto2b(TPC_MAX_LISTS, data->total_concurrent_copies);
	data->maximum_concurrent_copies = TPC_MAX_LISTS;
	data->data_segment_granularity = 0;
	data->inline_data_granularity = 0;
	data->held_data_granularity = 0;
	data->implemented_descriptor_list_length = 4;
	data->list_of_implemented_descriptor_type_codes[0] = EC_SEG_B2B;
	data->list_of_implemented_descriptor_type_codes[1] = EC_SEG_VERIFY;
	data->list_of_implemented_descriptor_type_codes[2] = EC_SEG_REGISTER_KEY;
	data->list_of_implemented_descriptor_type_codes[3] = EC_CSCD_ID;

	ctl_set_success(ctsio);
	ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	ctsio->be_move_done = ctl_config_move_done;
	ctl_datamove((union ctl_io *)ctsio);
	return (retval);
}

static struct tpc_list *
tpc_find_list(struct ctl_lun *lun, uint32_t list_id, uint32_t init_idx)
{
	struct tpc_list *list;

	mtx_assert(&lun->lun_lock, MA_OWNED);
	TAILQ_FOREACH(list, &lun->tpc_lists, links) {
		if ((list->flags & EC_LIST_ID_USAGE_MASK) !=
		     EC_LIST_ID_USAGE_NONE && list->list_id == list_id &&
		    list->init_idx == init_idx)
			break;
	}
	return (list);
}

int
ctl_receive_copy_status_lid1(struct ctl_scsiio *ctsio)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_receive_copy_status_lid1 *cdb;
	struct scsi_receive_copy_status_lid1_data *data;
	struct tpc_list *list;
	struct tpc_list list_copy;
	int retval;
	int alloc_len, total_len;
	uint32_t list_id;

	CTL_DEBUG_PRINT(("ctl_receive_copy_status_lid1\n"));

	cdb = (struct scsi_receive_copy_status_lid1 *)ctsio->cdb;
	retval = CTL_RETVAL_COMPLETE;

	list_id = cdb->list_identifier;
	mtx_lock(&lun->lun_lock);
	list = tpc_find_list(lun, list_id,
	    ctl_get_initindex(&ctsio->io_hdr.nexus));
	if (list == NULL) {
		mtx_unlock(&lun->lun_lock);
		ctl_set_invalid_field(ctsio, /*sks_valid*/ 1,
		    /*command*/ 1, /*field*/ 2, /*bit_valid*/ 0,
		    /*bit*/ 0);
		ctl_done((union ctl_io *)ctsio);
		return (retval);
	}
	list_copy = *list;
	if (list->completed) {
		TAILQ_REMOVE(&lun->tpc_lists, list, links);
		free(list, M_CTL);
	}
	mtx_unlock(&lun->lun_lock);

	total_len = sizeof(*data);
	alloc_len = scsi_4btoul(cdb->length);

	ctsio->kern_data_ptr = malloc(total_len, M_CTL, M_WAITOK | M_ZERO);
	ctsio->kern_sg_entries = 0;
	ctsio->kern_rel_offset = 0;
	ctsio->kern_data_len = min(total_len, alloc_len);
	ctsio->kern_total_len = ctsio->kern_data_len;

	data = (struct scsi_receive_copy_status_lid1_data *)ctsio->kern_data_ptr;
	scsi_ulto4b(sizeof(*data) - 4, data->available_data);
	if (list_copy.completed) {
		if (list_copy.error || list_copy.abort)
			data->copy_command_status = RCS_CCS_ERROR;
		else
			data->copy_command_status = RCS_CCS_COMPLETED;
	} else
		data->copy_command_status = RCS_CCS_INPROG;
	scsi_ulto2b(list_copy.curseg, data->segments_processed);
	if (list_copy.curbytes <= UINT32_MAX) {
		data->transfer_count_units = RCS_TC_BYTES;
		scsi_ulto4b(list_copy.curbytes, data->transfer_count);
	} else {
		data->transfer_count_units = RCS_TC_MBYTES;
		scsi_ulto4b(list_copy.curbytes >> 20, data->transfer_count);
	}

	ctl_set_success(ctsio);
	ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	ctsio->be_move_done = ctl_config_move_done;
	ctl_datamove((union ctl_io *)ctsio);
	return (retval);
}

int
ctl_receive_copy_failure_details(struct ctl_scsiio *ctsio)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_receive_copy_failure_details *cdb;
	struct scsi_receive_copy_failure_details_data *data;
	struct tpc_list *list;
	struct tpc_list list_copy;
	int retval;
	int alloc_len, total_len;
	uint32_t list_id;

	CTL_DEBUG_PRINT(("ctl_receive_copy_failure_details\n"));

	cdb = (struct scsi_receive_copy_failure_details *)ctsio->cdb;
	retval = CTL_RETVAL_COMPLETE;

	list_id = cdb->list_identifier;
	mtx_lock(&lun->lun_lock);
	list = tpc_find_list(lun, list_id,
	    ctl_get_initindex(&ctsio->io_hdr.nexus));
	if (list == NULL || !list->completed) {
		mtx_unlock(&lun->lun_lock);
		ctl_set_invalid_field(ctsio, /*sks_valid*/ 1,
		    /*command*/ 1, /*field*/ 2, /*bit_valid*/ 0,
		    /*bit*/ 0);
		ctl_done((union ctl_io *)ctsio);
		return (retval);
	}
	list_copy = *list;
	TAILQ_REMOVE(&lun->tpc_lists, list, links);
	free(list, M_CTL);
	mtx_unlock(&lun->lun_lock);

	total_len = sizeof(*data) + list_copy.sense_len;
	alloc_len = scsi_4btoul(cdb->length);

	ctsio->kern_data_ptr = malloc(total_len, M_CTL, M_WAITOK | M_ZERO);
	ctsio->kern_sg_entries = 0;
	ctsio->kern_rel_offset = 0;
	ctsio->kern_data_len = min(total_len, alloc_len);
	ctsio->kern_total_len = ctsio->kern_data_len;

	data = (struct scsi_receive_copy_failure_details_data *)ctsio->kern_data_ptr;
	if (list_copy.completed && (list_copy.error || list_copy.abort)) {
		scsi_ulto4b(sizeof(*data) - 4 + list_copy.sense_len,
		    data->available_data);
		data->copy_command_status = RCS_CCS_ERROR;
	} else
		scsi_ulto4b(0, data->available_data);
	scsi_ulto2b(list_copy.sense_len, data->sense_data_length);
	memcpy(data->sense_data, &list_copy.sense_data, list_copy.sense_len);

	ctl_set_success(ctsio);
	ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	ctsio->be_move_done = ctl_config_move_done;
	ctl_datamove((union ctl_io *)ctsio);
	return (retval);
}

int
ctl_receive_copy_status_lid4(struct ctl_scsiio *ctsio)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_receive_copy_status_lid4 *cdb;
	struct scsi_receive_copy_status_lid4_data *data;
	struct tpc_list *list;
	struct tpc_list list_copy;
	int retval;
	int alloc_len, total_len;
	uint32_t list_id;

	CTL_DEBUG_PRINT(("ctl_receive_copy_status_lid4\n"));

	cdb = (struct scsi_receive_copy_status_lid4 *)ctsio->cdb;
	retval = CTL_RETVAL_COMPLETE;

	list_id = scsi_4btoul(cdb->list_identifier);
	mtx_lock(&lun->lun_lock);
	list = tpc_find_list(lun, list_id,
	    ctl_get_initindex(&ctsio->io_hdr.nexus));
	if (list == NULL) {
		mtx_unlock(&lun->lun_lock);
		ctl_set_invalid_field(ctsio, /*sks_valid*/ 1,
		    /*command*/ 1, /*field*/ 2, /*bit_valid*/ 0,
		    /*bit*/ 0);
		ctl_done((union ctl_io *)ctsio);
		return (retval);
	}
	list_copy = *list;
	if (list->completed) {
		TAILQ_REMOVE(&lun->tpc_lists, list, links);
		free(list, M_CTL);
	}
	mtx_unlock(&lun->lun_lock);

	total_len = sizeof(*data) + list_copy.sense_len;
	alloc_len = scsi_4btoul(cdb->length);

	ctsio->kern_data_ptr = malloc(total_len, M_CTL, M_WAITOK | M_ZERO);
	ctsio->kern_sg_entries = 0;
	ctsio->kern_rel_offset = 0;
	ctsio->kern_data_len = min(total_len, alloc_len);
	ctsio->kern_total_len = ctsio->kern_data_len;

	data = (struct scsi_receive_copy_status_lid4_data *)ctsio->kern_data_ptr;
	scsi_ulto4b(sizeof(*data) - 4 + list_copy.sense_len,
	    data->available_data);
	data->response_to_service_action = list_copy.service_action;
	if (list_copy.completed) {
		if (list_copy.error)
			data->copy_command_status = RCS_CCS_ERROR;
		else if (list_copy.abort)
			data->copy_command_status = RCS_CCS_ABORTED;
		else
			data->copy_command_status = RCS_CCS_COMPLETED;
	} else
		data->copy_command_status = RCS_CCS_INPROG_FG;
	scsi_ulto2b(list_copy.curops, data->operation_counter);
	scsi_ulto4b(UINT32_MAX, data->estimated_status_update_delay);
	data->transfer_count_units = RCS_TC_BYTES;
	scsi_u64to8b(list_copy.curbytes, data->transfer_count);
	scsi_ulto2b(list_copy.curseg, data->segments_processed);
	data->length_of_the_sense_data_field = list_copy.sense_len;
	data->sense_data_length = list_copy.sense_len;
	memcpy(data->sense_data, &list_copy.sense_data, list_copy.sense_len);

	ctl_set_success(ctsio);
	ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	ctsio->be_move_done = ctl_config_move_done;
	ctl_datamove((union ctl_io *)ctsio);
	return (retval);
}

int
ctl_copy_operation_abort(struct ctl_scsiio *ctsio)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_copy_operation_abort *cdb;
	struct tpc_list *list;
	int retval;
	uint32_t list_id;

	CTL_DEBUG_PRINT(("ctl_copy_operation_abort\n"));

	cdb = (struct scsi_copy_operation_abort *)ctsio->cdb;
	retval = CTL_RETVAL_COMPLETE;

	list_id = scsi_4btoul(cdb->list_identifier);
	mtx_lock(&lun->lun_lock);
	list = tpc_find_list(lun, list_id,
	    ctl_get_initindex(&ctsio->io_hdr.nexus));
	if (list == NULL) {
		mtx_unlock(&lun->lun_lock);
		ctl_set_invalid_field(ctsio, /*sks_valid*/ 1,
		    /*command*/ 1, /*field*/ 2, /*bit_valid*/ 0,
		    /*bit*/ 0);
		ctl_done((union ctl_io *)ctsio);
		return (retval);
	}
	list->abort = 1;
	mtx_unlock(&lun->lun_lock);

	ctl_set_success(ctsio);
	ctl_done((union ctl_io *)ctsio);
	return (retval);
}

static uint64_t
tpc_resolve(struct tpc_list *list, uint16_t idx, uint32_t *ss,
    uint32_t *pb, uint32_t *pbo)
{

	if (idx == 0xffff) {
		if (ss && list->lun->be_lun)
			*ss = list->lun->be_lun->blocksize;
		if (pb && list->lun->be_lun)
			*pb = list->lun->be_lun->blocksize <<
			    list->lun->be_lun->pblockexp;
		if (pbo && list->lun->be_lun)
			*pbo = list->lun->be_lun->blocksize *
			    list->lun->be_lun->pblockoff;
		return (list->lun->lun);
	}
	if (idx >= list->ncscd)
		return (UINT64_MAX);
	return (tpcl_resolve(list->lun->ctl_softc,
	    list->init_port, &list->cscd[idx], ss, pb, pbo));
}

static void
tpc_set_io_error_sense(struct tpc_list *list)
{
	int flen;
	uint8_t csi[4];
	uint8_t sks[3];
	uint8_t fbuf[4 + 64];

	scsi_ulto4b(list->curseg, csi);
	if (list->fwd_cscd <= 0x07ff) {
		sks[0] = SSD_SKS_SEGMENT_VALID;
		scsi_ulto2b((uint8_t *)&list->cscd[list->fwd_cscd] -
		    list->params, &sks[1]);
	} else
		sks[0] = 0;
	if (list->fwd_scsi_status) {
		fbuf[0] = 0x0c;
		fbuf[2] = list->fwd_target;
		flen = list->fwd_sense_len;
		if (flen > 64) {
			flen = 64;
			fbuf[2] |= SSD_FORWARDED_FSDT;
		}
		fbuf[1] = 2 + flen;
		fbuf[3] = list->fwd_scsi_status;
		bcopy(&list->fwd_sense_data, &fbuf[4], flen);
		flen += 4;
	} else
		flen = 0;
	ctl_set_sense(list->ctsio, /*current_error*/ 1,
	    /*sense_key*/ SSD_KEY_COPY_ABORTED,
	    /*asc*/ 0x0d, /*ascq*/ 0x01,
	    SSD_ELEM_COMMAND, sizeof(csi), csi,
	    sks[0] ? SSD_ELEM_SKS : SSD_ELEM_SKIP, sizeof(sks), sks,
	    flen ? SSD_ELEM_DESC : SSD_ELEM_SKIP, flen, fbuf,
	    SSD_ELEM_NONE);
}

static int
tpc_process_b2b(struct tpc_list *list)
{
	struct scsi_ec_segment_b2b *seg;
	struct scsi_ec_cscd_dtsp *sdstp, *ddstp;
	struct tpc_io *tior, *tiow;
	struct runl run;
	uint64_t sl, dl;
	off_t srclba, dstlba, numbytes, donebytes, roundbytes;
	int numlba;
	uint32_t srcblock, dstblock, pb, pbo, adj;
	uint16_t scscd, dcscd;
	uint8_t csi[4];

	scsi_ulto4b(list->curseg, csi);
	if (list->stage == 1) {
		while ((tior = TAILQ_FIRST(&list->allio)) != NULL) {
			TAILQ_REMOVE(&list->allio, tior, links);
			ctl_free_io(tior->io);
			free(tior->buf, M_CTL);
			free(tior, M_CTL);
		}
		if (list->abort) {
			ctl_set_task_aborted(list->ctsio);
			return (CTL_RETVAL_ERROR);
		} else if (list->error) {
			tpc_set_io_error_sense(list);
			return (CTL_RETVAL_ERROR);
		}
		list->cursectors += list->segsectors;
		list->curbytes += list->segbytes;
		return (CTL_RETVAL_COMPLETE);
	}

	TAILQ_INIT(&list->allio);
	seg = (struct scsi_ec_segment_b2b *)list->seg[list->curseg];
	scscd = scsi_2btoul(seg->src_cscd);
	dcscd = scsi_2btoul(seg->dst_cscd);
	sl = tpc_resolve(list, scscd, &srcblock, NULL, NULL);
	dl = tpc_resolve(list, dcscd, &dstblock, &pb, &pbo);
	if (sl == UINT64_MAX || dl == UINT64_MAX) {
		ctl_set_sense(list->ctsio, /*current_error*/ 1,
		    /*sense_key*/ SSD_KEY_COPY_ABORTED,
		    /*asc*/ 0x08, /*ascq*/ 0x04,
		    SSD_ELEM_COMMAND, sizeof(csi), csi,
		    SSD_ELEM_NONE);
		return (CTL_RETVAL_ERROR);
	}
	if (pbo > 0)
		pbo = pb - pbo;
	sdstp = &list->cscd[scscd].dtsp;
	if (scsi_3btoul(sdstp->block_length) != 0)
		srcblock = scsi_3btoul(sdstp->block_length);
	ddstp = &list->cscd[dcscd].dtsp;
	if (scsi_3btoul(ddstp->block_length) != 0)
		dstblock = scsi_3btoul(ddstp->block_length);
	numlba = scsi_2btoul(seg->number_of_blocks);
	if (seg->flags & EC_SEG_DC)
		numbytes = (off_t)numlba * dstblock;
	else
		numbytes = (off_t)numlba * srcblock;
	srclba = scsi_8btou64(seg->src_lba);
	dstlba = scsi_8btou64(seg->dst_lba);

//	printf("Copy %ju bytes from %ju @ %ju to %ju @ %ju\n",
//	    (uintmax_t)numbytes, sl, scsi_8btou64(seg->src_lba),
//	    dl, scsi_8btou64(seg->dst_lba));

	if (numbytes == 0)
		return (CTL_RETVAL_COMPLETE);

	if (numbytes % srcblock != 0 || numbytes % dstblock != 0) {
		ctl_set_sense(list->ctsio, /*current_error*/ 1,
		    /*sense_key*/ SSD_KEY_COPY_ABORTED,
		    /*asc*/ 0x26, /*ascq*/ 0x0A,
		    SSD_ELEM_COMMAND, sizeof(csi), csi,
		    SSD_ELEM_NONE);
		return (CTL_RETVAL_ERROR);
	}

	list->segbytes = numbytes;
	list->segsectors = numbytes / dstblock;
	donebytes = 0;
	TAILQ_INIT(&run);
	list->tbdio = 0;
	while (donebytes < numbytes) {
		roundbytes = numbytes - donebytes;
		if (roundbytes > TPC_MAX_IO_SIZE) {
			roundbytes = TPC_MAX_IO_SIZE;
			roundbytes -= roundbytes % dstblock;
			if (pb > dstblock) {
				adj = (dstlba * dstblock + roundbytes - pbo) % pb;
				if (roundbytes > adj)
					roundbytes -= adj;
			}
		}

		tior = malloc(sizeof(*tior), M_CTL, M_WAITOK | M_ZERO);
		TAILQ_INIT(&tior->run);
		tior->buf = malloc(roundbytes, M_CTL, M_WAITOK);
		tior->list = list;
		TAILQ_INSERT_TAIL(&list->allio, tior, links);
		tior->io = tpcl_alloc_io();
		ctl_scsi_read_write(tior->io,
				    /*data_ptr*/ tior->buf,
				    /*data_len*/ roundbytes,
				    /*read_op*/ 1,
				    /*byte2*/ 0,
				    /*minimum_cdb_size*/ 0,
				    /*lba*/ srclba,
				    /*num_blocks*/ roundbytes / srcblock,
				    /*tag_type*/ CTL_TAG_SIMPLE,
				    /*control*/ 0);
		tior->io->io_hdr.retries = 3;
		tior->target = SSD_FORWARDED_SDS_EXSRC;
		tior->cscd = scscd;
		tior->lun = sl;
		tior->io->io_hdr.ctl_private[CTL_PRIV_FRONTEND].ptr = tior;

		tiow = malloc(sizeof(*tior), M_CTL, M_WAITOK | M_ZERO);
		TAILQ_INIT(&tiow->run);
		tiow->list = list;
		TAILQ_INSERT_TAIL(&list->allio, tiow, links);
		tiow->io = tpcl_alloc_io();
		ctl_scsi_read_write(tiow->io,
				    /*data_ptr*/ tior->buf,
				    /*data_len*/ roundbytes,
				    /*read_op*/ 0,
				    /*byte2*/ 0,
				    /*minimum_cdb_size*/ 0,
				    /*lba*/ dstlba,
				    /*num_blocks*/ roundbytes / dstblock,
				    /*tag_type*/ CTL_TAG_SIMPLE,
				    /*control*/ 0);
		tiow->io->io_hdr.retries = 3;
		tiow->target = SSD_FORWARDED_SDS_EXDST;
		tiow->cscd = dcscd;
		tiow->lun = dl;
		tiow->io->io_hdr.ctl_private[CTL_PRIV_FRONTEND].ptr = tiow;

		TAILQ_INSERT_TAIL(&tior->run, tiow, rlinks);
		TAILQ_INSERT_TAIL(&run, tior, rlinks);
		list->tbdio++;
		donebytes += roundbytes;
		srclba += roundbytes / srcblock;
		dstlba += roundbytes / dstblock;
	}

	while ((tior = TAILQ_FIRST(&run)) != NULL) {
		TAILQ_REMOVE(&run, tior, rlinks);
		if (tpcl_queue(tior->io, tior->lun) != CTL_RETVAL_COMPLETE)
			panic("tpcl_queue() error");
	}

	list->stage++;
	return (CTL_RETVAL_QUEUED);
}

static int
tpc_process_verify(struct tpc_list *list)
{
	struct scsi_ec_segment_verify *seg;
	struct tpc_io *tio;
	uint64_t sl;
	uint16_t cscd;
	uint8_t csi[4];

	scsi_ulto4b(list->curseg, csi);
	if (list->stage == 1) {
		while ((tio = TAILQ_FIRST(&list->allio)) != NULL) {
			TAILQ_REMOVE(&list->allio, tio, links);
			ctl_free_io(tio->io);
			free(tio, M_CTL);
		}
		if (list->abort) {
			ctl_set_task_aborted(list->ctsio);
			return (CTL_RETVAL_ERROR);
		} else if (list->error) {
			tpc_set_io_error_sense(list);
			return (CTL_RETVAL_ERROR);
		} else
			return (CTL_RETVAL_COMPLETE);
	}

	TAILQ_INIT(&list->allio);
	seg = (struct scsi_ec_segment_verify *)list->seg[list->curseg];
	cscd = scsi_2btoul(seg->src_cscd);
	sl = tpc_resolve(list, cscd, NULL, NULL, NULL);
	if (sl == UINT64_MAX) {
		ctl_set_sense(list->ctsio, /*current_error*/ 1,
		    /*sense_key*/ SSD_KEY_COPY_ABORTED,
		    /*asc*/ 0x08, /*ascq*/ 0x04,
		    SSD_ELEM_COMMAND, sizeof(csi), csi,
		    SSD_ELEM_NONE);
		return (CTL_RETVAL_ERROR);
	}

//	printf("Verify %ju\n", sl);

	if ((seg->tur & 0x01) == 0)
		return (CTL_RETVAL_COMPLETE);

	list->tbdio = 1;
	tio = malloc(sizeof(*tio), M_CTL, M_WAITOK | M_ZERO);
	TAILQ_INIT(&tio->run);
	tio->list = list;
	TAILQ_INSERT_TAIL(&list->allio, tio, links);
	tio->io = tpcl_alloc_io();
	ctl_scsi_tur(tio->io, /*tag_type*/ CTL_TAG_SIMPLE, /*control*/ 0);
	tio->io->io_hdr.retries = 3;
	tio->target = SSD_FORWARDED_SDS_EXSRC;
	tio->cscd = cscd;
	tio->lun = sl;
	tio->io->io_hdr.ctl_private[CTL_PRIV_FRONTEND].ptr = tio;
	list->stage++;
	if (tpcl_queue(tio->io, tio->lun) != CTL_RETVAL_COMPLETE)
		panic("tpcl_queue() error");
	return (CTL_RETVAL_QUEUED);
}

static int
tpc_process_register_key(struct tpc_list *list)
{
	struct scsi_ec_segment_register_key *seg;
	struct tpc_io *tio;
	uint64_t dl;
	int datalen;
	uint16_t cscd;
	uint8_t csi[4];

	scsi_ulto4b(list->curseg, csi);
	if (list->stage == 1) {
		while ((tio = TAILQ_FIRST(&list->allio)) != NULL) {
			TAILQ_REMOVE(&list->allio, tio, links);
			ctl_free_io(tio->io);
			free(tio->buf, M_CTL);
			free(tio, M_CTL);
		}
		if (list->abort) {
			ctl_set_task_aborted(list->ctsio);
			return (CTL_RETVAL_ERROR);
		} else if (list->error) {
			tpc_set_io_error_sense(list);
			return (CTL_RETVAL_ERROR);
		} else
			return (CTL_RETVAL_COMPLETE);
	}

	TAILQ_INIT(&list->allio);
	seg = (struct scsi_ec_segment_register_key *)list->seg[list->curseg];
	cscd = scsi_2btoul(seg->dst_cscd);
	dl = tpc_resolve(list, cscd, NULL, NULL, NULL);
	if (dl == UINT64_MAX) {
		ctl_set_sense(list->ctsio, /*current_error*/ 1,
		    /*sense_key*/ SSD_KEY_COPY_ABORTED,
		    /*asc*/ 0x08, /*ascq*/ 0x04,
		    SSD_ELEM_COMMAND, sizeof(csi), csi,
		    SSD_ELEM_NONE);
		return (CTL_RETVAL_ERROR);
	}

//	printf("Register Key %ju\n", dl);

	list->tbdio = 1;
	tio = malloc(sizeof(*tio), M_CTL, M_WAITOK | M_ZERO);
	TAILQ_INIT(&tio->run);
	tio->list = list;
	TAILQ_INSERT_TAIL(&list->allio, tio, links);
	tio->io = tpcl_alloc_io();
	datalen = sizeof(struct scsi_per_res_out_parms);
	tio->buf = malloc(datalen, M_CTL, M_WAITOK);
	ctl_scsi_persistent_res_out(tio->io,
	    tio->buf, datalen, SPRO_REGISTER, -1,
	    scsi_8btou64(seg->res_key), scsi_8btou64(seg->sa_res_key),
	    /*tag_type*/ CTL_TAG_SIMPLE, /*control*/ 0);
	tio->io->io_hdr.retries = 3;
	tio->target = SSD_FORWARDED_SDS_EXDST;
	tio->cscd = cscd;
	tio->lun = dl;
	tio->io->io_hdr.ctl_private[CTL_PRIV_FRONTEND].ptr = tio;
	list->stage++;
	if (tpcl_queue(tio->io, tio->lun) != CTL_RETVAL_COMPLETE)
		panic("tpcl_queue() error");
	return (CTL_RETVAL_QUEUED);
}

static off_t
tpc_ranges_length(struct scsi_range_desc *range, int nrange)
{
	off_t length = 0;
	int r;

	for (r = 0; r < nrange; r++)
		length += scsi_4btoul(range[r].length);
	return (length);
}

static int
tpc_check_ranges_l(struct scsi_range_desc *range, int nrange, uint64_t maxlba,
    uint64_t *lba)
{
	uint64_t b1;
	uint32_t l1;
	int i;

	for (i = 0; i < nrange; i++) {
		b1 = scsi_8btou64(range[i].lba);
		l1 = scsi_4btoul(range[i].length);
		if (b1 + l1 < b1 || b1 + l1 > maxlba + 1) {
			*lba = MAX(b1, maxlba + 1);
			return (-1);
		}
	}
	return (0);
}

static int
tpc_check_ranges_x(struct scsi_range_desc *range, int nrange)
{
	uint64_t b1, b2;
	uint32_t l1, l2;
	int i, j;

	for (i = 0; i < nrange - 1; i++) {
		b1 = scsi_8btou64(range[i].lba);
		l1 = scsi_4btoul(range[i].length);
		for (j = i + 1; j < nrange; j++) {
			b2 = scsi_8btou64(range[j].lba);
			l2 = scsi_4btoul(range[j].length);
			if (b1 + l1 > b2 && b2 + l2 > b1)
				return (-1);
		}
	}
	return (0);
}

static int
tpc_skip_ranges(struct scsi_range_desc *range, int nrange, off_t skip,
    int *srange, off_t *soffset)
{
	off_t off;
	int r;

	r = 0;
	off = 0;
	while (r < nrange) {
		if (skip - off < scsi_4btoul(range[r].length)) {
			*srange = r;
			*soffset = skip - off;
			return (0);
		}
		off += scsi_4btoul(range[r].length);
		r++;
	}
	return (-1);
}

static int
tpc_process_wut(struct tpc_list *list)
{
	struct tpc_io *tio, *tior, *tiow;
	struct runl run;
	int drange, srange;
	off_t doffset, soffset;
	off_t srclba, dstlba, numbytes, donebytes, roundbytes;
	uint32_t srcblock, dstblock, pb, pbo, adj;

	if (list->stage > 0) {
		/* Cleanup after previous rounds. */
		while ((tio = TAILQ_FIRST(&list->allio)) != NULL) {
			TAILQ_REMOVE(&list->allio, tio, links);
			ctl_free_io(tio->io);
			free(tio->buf, M_CTL);
			free(tio, M_CTL);
		}
		if (list->abort) {
			ctl_set_task_aborted(list->ctsio);
			return (CTL_RETVAL_ERROR);
		} else if (list->error) {
			if (list->fwd_scsi_status) {
				list->ctsio->io_hdr.status =
				    CTL_SCSI_ERROR | CTL_AUTOSENSE;
				list->ctsio->scsi_status = list->fwd_scsi_status;
				list->ctsio->sense_data = list->fwd_sense_data;
				list->ctsio->sense_len = list->fwd_sense_len;
			} else {
				ctl_set_invalid_field(list->ctsio,
				    /*sks_valid*/ 0, /*command*/ 0,
				    /*field*/ 0, /*bit_valid*/ 0, /*bit*/ 0);
			}
			return (CTL_RETVAL_ERROR);
		}
		list->cursectors += list->segsectors;
		list->curbytes += list->segbytes;
	}

	/* Check where we are on destination ranges list. */
	if (tpc_skip_ranges(list->range, list->nrange, list->cursectors,
	    &drange, &doffset) != 0)
		return (CTL_RETVAL_COMPLETE);
	dstblock = list->lun->be_lun->blocksize;
	pb = dstblock << list->lun->be_lun->pblockexp;
	if (list->lun->be_lun->pblockoff > 0)
		pbo = pb - dstblock * list->lun->be_lun->pblockoff;
	else
		pbo = 0;

	/* Check where we are on source ranges list. */
	srcblock = list->token->blocksize;
	if (tpc_skip_ranges(list->token->range, list->token->nrange,
	    list->offset_into_rod + list->cursectors * dstblock / srcblock,
	    &srange, &soffset) != 0) {
		ctl_set_invalid_field(list->ctsio, /*sks_valid*/ 0,
		    /*command*/ 0, /*field*/ 0, /*bit_valid*/ 0, /*bit*/ 0);
		return (CTL_RETVAL_ERROR);
	}

	srclba = scsi_8btou64(list->token->range[srange].lba) + soffset;
	dstlba = scsi_8btou64(list->range[drange].lba) + doffset;
	numbytes = srcblock *
	    (scsi_4btoul(list->token->range[srange].length) - soffset);
	numbytes = omin(numbytes, dstblock *
	    (scsi_4btoul(list->range[drange].length) - doffset));
	if (numbytes > TPC_MAX_IOCHUNK_SIZE) {
		numbytes = TPC_MAX_IOCHUNK_SIZE;
		numbytes -= numbytes % dstblock;
		if (pb > dstblock) {
			adj = (dstlba * dstblock + numbytes - pbo) % pb;
			if (numbytes > adj)
				numbytes -= adj;
		}
	}

	if (numbytes % srcblock != 0 || numbytes % dstblock != 0) {
		ctl_set_invalid_field(list->ctsio, /*sks_valid*/ 0,
		    /*command*/ 0, /*field*/ 0, /*bit_valid*/ 0, /*bit*/ 0);
		return (CTL_RETVAL_ERROR);
	}

	list->segbytes = numbytes;
	list->segsectors = numbytes / dstblock;
//printf("Copy chunk of %ju sectors from %ju to %ju\n", list->segsectors,
//    srclba, dstlba);
	donebytes = 0;
	TAILQ_INIT(&run);
	list->tbdio = 0;
	TAILQ_INIT(&list->allio);
	while (donebytes < numbytes) {
		roundbytes = numbytes - donebytes;
		if (roundbytes > TPC_MAX_IO_SIZE) {
			roundbytes = TPC_MAX_IO_SIZE;
			roundbytes -= roundbytes % dstblock;
			if (pb > dstblock) {
				adj = (dstlba * dstblock + roundbytes - pbo) % pb;
				if (roundbytes > adj)
					roundbytes -= adj;
			}
		}

		tior = malloc(sizeof(*tior), M_CTL, M_WAITOK | M_ZERO);
		TAILQ_INIT(&tior->run);
		tior->buf = malloc(roundbytes, M_CTL, M_WAITOK);
		tior->list = list;
		TAILQ_INSERT_TAIL(&list->allio, tior, links);
		tior->io = tpcl_alloc_io();
		ctl_scsi_read_write(tior->io,
				    /*data_ptr*/ tior->buf,
				    /*data_len*/ roundbytes,
				    /*read_op*/ 1,
				    /*byte2*/ 0,
				    /*minimum_cdb_size*/ 0,
				    /*lba*/ srclba,
				    /*num_blocks*/ roundbytes / srcblock,
				    /*tag_type*/ CTL_TAG_SIMPLE,
				    /*control*/ 0);
		tior->io->io_hdr.retries = 3;
		tior->lun = list->token->lun;
		tior->io->io_hdr.ctl_private[CTL_PRIV_FRONTEND].ptr = tior;

		tiow = malloc(sizeof(*tiow), M_CTL, M_WAITOK | M_ZERO);
		TAILQ_INIT(&tiow->run);
		tiow->list = list;
		TAILQ_INSERT_TAIL(&list->allio, tiow, links);
		tiow->io = tpcl_alloc_io();
		ctl_scsi_read_write(tiow->io,
				    /*data_ptr*/ tior->buf,
				    /*data_len*/ roundbytes,
				    /*read_op*/ 0,
				    /*byte2*/ 0,
				    /*minimum_cdb_size*/ 0,
				    /*lba*/ dstlba,
				    /*num_blocks*/ roundbytes / dstblock,
				    /*tag_type*/ CTL_TAG_SIMPLE,
				    /*control*/ 0);
		tiow->io->io_hdr.retries = 3;
		tiow->lun = list->lun->lun;
		tiow->io->io_hdr.ctl_private[CTL_PRIV_FRONTEND].ptr = tiow;

		TAILQ_INSERT_TAIL(&tior->run, tiow, rlinks);
		TAILQ_INSERT_TAIL(&run, tior, rlinks);
		list->tbdio++;
		donebytes += roundbytes;
		srclba += roundbytes / srcblock;
		dstlba += roundbytes / dstblock;
	}

	while ((tior = TAILQ_FIRST(&run)) != NULL) {
		TAILQ_REMOVE(&run, tior, rlinks);
		if (tpcl_queue(tior->io, tior->lun) != CTL_RETVAL_COMPLETE)
			panic("tpcl_queue() error");
	}

	list->stage++;
	return (CTL_RETVAL_QUEUED);
}

static int
tpc_process_zero_wut(struct tpc_list *list)
{
	struct tpc_io *tio, *tiow;
	struct runl run, *prun;
	int r;
	uint32_t dstblock, len;

	if (list->stage > 0) {
complete:
		/* Cleanup after previous rounds. */
		while ((tio = TAILQ_FIRST(&list->allio)) != NULL) {
			TAILQ_REMOVE(&list->allio, tio, links);
			ctl_free_io(tio->io);
			free(tio, M_CTL);
		}
		if (list->abort) {
			ctl_set_task_aborted(list->ctsio);
			return (CTL_RETVAL_ERROR);
		} else if (list->error) {
			if (list->fwd_scsi_status) {
				list->ctsio->io_hdr.status =
				    CTL_SCSI_ERROR | CTL_AUTOSENSE;
				list->ctsio->scsi_status = list->fwd_scsi_status;
				list->ctsio->sense_data = list->fwd_sense_data;
				list->ctsio->sense_len = list->fwd_sense_len;
			} else {
				ctl_set_invalid_field(list->ctsio,
				    /*sks_valid*/ 0, /*command*/ 0,
				    /*field*/ 0, /*bit_valid*/ 0, /*bit*/ 0);
			}
			return (CTL_RETVAL_ERROR);
		}
		list->cursectors += list->segsectors;
		list->curbytes += list->segbytes;
		return (CTL_RETVAL_COMPLETE);
	}

	dstblock = list->lun->be_lun->blocksize;
	TAILQ_INIT(&run);
	prun = &run;
	list->tbdio = 1;
	TAILQ_INIT(&list->allio);
	list->segsectors = 0;
	for (r = 0; r < list->nrange; r++) {
		len = scsi_4btoul(list->range[r].length);
		if (len == 0)
			continue;

		tiow = malloc(sizeof(*tiow), M_CTL, M_WAITOK | M_ZERO);
		TAILQ_INIT(&tiow->run);
		tiow->list = list;
		TAILQ_INSERT_TAIL(&list->allio, tiow, links);
		tiow->io = tpcl_alloc_io();
		ctl_scsi_write_same(tiow->io,
				    /*data_ptr*/ NULL,
				    /*data_len*/ 0,
				    /*byte2*/ SWS_NDOB,
				    /*lba*/ scsi_8btou64(list->range[r].lba),
				    /*num_blocks*/ len,
				    /*tag_type*/ CTL_TAG_SIMPLE,
				    /*control*/ 0);
		tiow->io->io_hdr.retries = 3;
		tiow->lun = list->lun->lun;
		tiow->io->io_hdr.ctl_private[CTL_PRIV_FRONTEND].ptr = tiow;

		TAILQ_INSERT_TAIL(prun, tiow, rlinks);
		prun = &tiow->run;
		list->segsectors += len;
	}
	list->segbytes = list->segsectors * dstblock;

	if (TAILQ_EMPTY(&run))
		goto complete;

	while ((tiow = TAILQ_FIRST(&run)) != NULL) {
		TAILQ_REMOVE(&run, tiow, rlinks);
		if (tpcl_queue(tiow->io, tiow->lun) != CTL_RETVAL_COMPLETE)
			panic("tpcl_queue() error");
	}

	list->stage++;
	return (CTL_RETVAL_QUEUED);
}

static void
tpc_process(struct tpc_list *list)
{
	struct ctl_lun *lun = list->lun;
	struct ctl_softc *softc = lun->ctl_softc;
	struct scsi_ec_segment *seg;
	struct ctl_scsiio *ctsio = list->ctsio;
	int retval = CTL_RETVAL_COMPLETE;
	uint8_t csi[4];

	if (list->service_action == EC_WUT) {
		if (list->token != NULL)
			retval = tpc_process_wut(list);
		else
			retval = tpc_process_zero_wut(list);
		if (retval == CTL_RETVAL_QUEUED)
			return;
		if (retval == CTL_RETVAL_ERROR) {
			list->error = 1;
			goto done;
		}
	} else {
//printf("ZZZ %d cscd, %d segs\n", list->ncscd, list->nseg);
		while (list->curseg < list->nseg) {
			seg = list->seg[list->curseg];
			switch (seg->type_code) {
			case EC_SEG_B2B:
				retval = tpc_process_b2b(list);
				break;
			case EC_SEG_VERIFY:
				retval = tpc_process_verify(list);
				break;
			case EC_SEG_REGISTER_KEY:
				retval = tpc_process_register_key(list);
				break;
			default:
				scsi_ulto4b(list->curseg, csi);
				ctl_set_sense(ctsio, /*current_error*/ 1,
				    /*sense_key*/ SSD_KEY_COPY_ABORTED,
				    /*asc*/ 0x26, /*ascq*/ 0x09,
				    SSD_ELEM_COMMAND, sizeof(csi), csi,
				    SSD_ELEM_NONE);
				goto done;
			}
			if (retval == CTL_RETVAL_QUEUED)
				return;
			if (retval == CTL_RETVAL_ERROR) {
				list->error = 1;
				goto done;
			}
			list->curseg++;
			list->stage = 0;
		}
	}

	ctl_set_success(ctsio);

done:
//printf("ZZZ done\n");
	free(list->params, M_CTL);
	list->params = NULL;
	if (list->token) {
		mtx_lock(&softc->tpc_lock);
		if (--list->token->active == 0)
			list->token->last_active = time_uptime;
		mtx_unlock(&softc->tpc_lock);
		list->token = NULL;
	}
	mtx_lock(&lun->lun_lock);
	if ((list->flags & EC_LIST_ID_USAGE_MASK) == EC_LIST_ID_USAGE_NONE) {
		TAILQ_REMOVE(&lun->tpc_lists, list, links);
		free(list, M_CTL);
	} else {
		list->completed = 1;
		list->last_active = time_uptime;
		list->sense_data = ctsio->sense_data;
		list->sense_len = ctsio->sense_len;
		list->scsi_status = ctsio->scsi_status;
	}
	mtx_unlock(&lun->lun_lock);

	ctl_done((union ctl_io *)ctsio);
}

/*
 * For any sort of check condition, busy, etc., we just retry.  We do not
 * decrement the retry count for unit attention type errors.  These are
 * normal, and we want to save the retry count for "real" errors.  Otherwise,
 * we could end up with situations where a command will succeed in some
 * situations and fail in others, depending on whether a unit attention is
 * pending.  Also, some of our error recovery actions, most notably the
 * LUN reset action, will cause a unit attention.
 *
 * We can add more detail here later if necessary.
 */
static tpc_error_action
tpc_checkcond_parse(union ctl_io *io)
{
	tpc_error_action error_action;
	int error_code, sense_key, asc, ascq;

	/*
	 * Default to retrying the command.
	 */
	error_action = TPC_ERR_RETRY;

	scsi_extract_sense_len(&io->scsiio.sense_data,
			       io->scsiio.sense_len,
			       &error_code,
			       &sense_key,
			       &asc,
			       &ascq,
			       /*show_errors*/ 1);

	switch (error_code) {
	case SSD_DEFERRED_ERROR:
	case SSD_DESC_DEFERRED_ERROR:
		error_action |= TPC_ERR_NO_DECREMENT;
		break;
	case SSD_CURRENT_ERROR:
	case SSD_DESC_CURRENT_ERROR:
	default:
		switch (sense_key) {
		case SSD_KEY_UNIT_ATTENTION:
			error_action |= TPC_ERR_NO_DECREMENT;
			break;
		case SSD_KEY_HARDWARE_ERROR:
			/*
			 * This is our generic "something bad happened"
			 * error code.  It often isn't recoverable.
			 */
			if ((asc == 0x44) && (ascq == 0x00))
				error_action = TPC_ERR_FAIL;
			break;
		case SSD_KEY_NOT_READY:
			/*
			 * If the LUN is powered down, there likely isn't
			 * much point in retrying right now.
			 */
			if ((asc == 0x04) && (ascq == 0x02))
				error_action = TPC_ERR_FAIL;
			/*
			 * If the LUN is offline, there probably isn't much
			 * point in retrying, either.
			 */
			if ((asc == 0x04) && (ascq == 0x03))
				error_action = TPC_ERR_FAIL;
			break;
		}
	}
	return (error_action);
}

static tpc_error_action
tpc_error_parse(union ctl_io *io)
{
	tpc_error_action error_action = TPC_ERR_RETRY;

	switch (io->io_hdr.io_type) {
	case CTL_IO_SCSI:
		switch (io->io_hdr.status & CTL_STATUS_MASK) {
		case CTL_SCSI_ERROR:
			switch (io->scsiio.scsi_status) {
			case SCSI_STATUS_CHECK_COND:
				error_action = tpc_checkcond_parse(io);
				break;
			default:
				break;
			}
			break;
		default:
			break;
		}
		break;
	case CTL_IO_TASK:
		break;
	default:
		panic("%s: invalid ctl_io type %d\n", __func__,
		      io->io_hdr.io_type);
		break;
	}
	return (error_action);
}

void
tpc_done(union ctl_io *io)
{
	struct tpc_io *tio, *tior;

	/*
	 * Very minimal retry logic.  We basically retry if we got an error
	 * back, and the retry count is greater than 0.  If we ever want
	 * more sophisticated initiator type behavior, the CAM error
	 * recovery code in ../common might be helpful.
	 */
	tio = io->io_hdr.ctl_private[CTL_PRIV_FRONTEND].ptr;
	if (((io->io_hdr.status & CTL_STATUS_MASK) != CTL_SUCCESS)
	 && (io->io_hdr.retries > 0)) {
		ctl_io_status old_status;
		tpc_error_action error_action;

		error_action = tpc_error_parse(io);
		switch (error_action & TPC_ERR_MASK) {
		case TPC_ERR_FAIL:
			break;
		case TPC_ERR_RETRY:
		default:
			if ((error_action & TPC_ERR_NO_DECREMENT) == 0)
				io->io_hdr.retries--;
			old_status = io->io_hdr.status;
			io->io_hdr.status = CTL_STATUS_NONE;
			io->io_hdr.flags &= ~CTL_FLAG_ABORT;
			io->io_hdr.flags &= ~CTL_FLAG_SENT_2OTHER_SC;
			if (tpcl_queue(io, tio->lun) != CTL_RETVAL_COMPLETE) {
				printf("%s: error returned from ctl_queue()!\n",
				       __func__);
				io->io_hdr.status = old_status;
			} else
				return;
		}
	}

	if ((io->io_hdr.status & CTL_STATUS_MASK) != CTL_SUCCESS) {
		tio->list->error = 1;
		if (io->io_hdr.io_type == CTL_IO_SCSI &&
		    (io->io_hdr.status & CTL_STATUS_MASK) == CTL_SCSI_ERROR) {
			tio->list->fwd_scsi_status = io->scsiio.scsi_status;
			tio->list->fwd_sense_data = io->scsiio.sense_data;
			tio->list->fwd_sense_len = io->scsiio.sense_len;
			tio->list->fwd_target = tio->target;
			tio->list->fwd_cscd = tio->cscd;
		}
	} else
		atomic_add_int(&tio->list->curops, 1);
	if (!tio->list->error && !tio->list->abort) {
		while ((tior = TAILQ_FIRST(&tio->run)) != NULL) {
			TAILQ_REMOVE(&tio->run, tior, rlinks);
			atomic_add_int(&tio->list->tbdio, 1);
			if (tpcl_queue(tior->io, tior->lun) != CTL_RETVAL_COMPLETE)
				panic("tpcl_queue() error");
		}
	}
	if (atomic_fetchadd_int(&tio->list->tbdio, -1) == 1)
		tpc_process(tio->list);
}

int
ctl_extended_copy_lid1(struct ctl_scsiio *ctsio)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_extended_copy *cdb;
	struct scsi_extended_copy_lid1_data *data;
	struct scsi_ec_cscd *cscd;
	struct scsi_ec_segment *seg;
	struct tpc_list *list, *tlist;
	uint8_t *ptr;
	const char *value;
	int len, off, lencscd, lenseg, leninl, nseg;

	CTL_DEBUG_PRINT(("ctl_extended_copy_lid1\n"));

	cdb = (struct scsi_extended_copy *)ctsio->cdb;
	len = scsi_4btoul(cdb->length);

	if (len == 0) {
		ctl_set_success(ctsio);
		goto done;
	}
	if (len < sizeof(struct scsi_extended_copy_lid1_data) ||
	    len > sizeof(struct scsi_extended_copy_lid1_data) +
	    TPC_MAX_LIST + TPC_MAX_INLINE) {
		ctl_set_invalid_field(ctsio, /*sks_valid*/ 1, /*command*/ 1,
		    /*field*/ 9, /*bit_valid*/ 0, /*bit*/ 0);
		goto done;
	}

	/*
	 * If we've got a kernel request that hasn't been malloced yet,
	 * malloc it and tell the caller the data buffer is here.
	 */
	if ((ctsio->io_hdr.flags & CTL_FLAG_ALLOCATED) == 0) {
		ctsio->kern_data_ptr = malloc(len, M_CTL, M_WAITOK);
		ctsio->kern_data_len = len;
		ctsio->kern_total_len = len;
		ctsio->kern_rel_offset = 0;
		ctsio->kern_sg_entries = 0;
		ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
		ctsio->be_move_done = ctl_config_move_done;
		ctl_datamove((union ctl_io *)ctsio);

		return (CTL_RETVAL_COMPLETE);
	}

	data = (struct scsi_extended_copy_lid1_data *)ctsio->kern_data_ptr;
	lencscd = scsi_2btoul(data->cscd_list_length);
	lenseg = scsi_4btoul(data->segment_list_length);
	leninl = scsi_4btoul(data->inline_data_length);
	if (lencscd > TPC_MAX_CSCDS * sizeof(struct scsi_ec_cscd)) {
		ctl_set_sense(ctsio, /*current_error*/ 1,
		    /*sense_key*/ SSD_KEY_ILLEGAL_REQUEST,
		    /*asc*/ 0x26, /*ascq*/ 0x06, SSD_ELEM_NONE);
		goto done;
	}
	if (lenseg > TPC_MAX_SEGS * sizeof(struct scsi_ec_segment)) {
		ctl_set_sense(ctsio, /*current_error*/ 1,
		    /*sense_key*/ SSD_KEY_ILLEGAL_REQUEST,
		    /*asc*/ 0x26, /*ascq*/ 0x08, SSD_ELEM_NONE);
		goto done;
	}
	if (lencscd + lenseg > TPC_MAX_LIST ||
	    leninl > TPC_MAX_INLINE ||
	    len < sizeof(struct scsi_extended_copy_lid1_data) +
	     lencscd + lenseg + leninl) {
		ctl_set_param_len_error(ctsio);
		goto done;
	}

	list = malloc(sizeof(struct tpc_list), M_CTL, M_WAITOK | M_ZERO);
	list->service_action = cdb->service_action;
	value = dnvlist_get_string(lun->be_lun->options, "insecure_tpc", NULL);
	if (value != NULL && strcmp(value, "on") == 0)
		list->init_port = -1;
	else
		list->init_port = ctsio->io_hdr.nexus.targ_port;
	list->init_idx = ctl_get_initindex(&ctsio->io_hdr.nexus);
	list->list_id = data->list_identifier;
	list->flags = data->flags;
	list->params = ctsio->kern_data_ptr;
	list->cscd = (struct scsi_ec_cscd *)&data->data[0];
	ptr = &data->data[0];
	for (off = 0; off < lencscd; off += sizeof(struct scsi_ec_cscd)) {
		cscd = (struct scsi_ec_cscd *)(ptr + off);
		if (cscd->type_code != EC_CSCD_ID) {
			free(list, M_CTL);
			ctl_set_sense(ctsio, /*current_error*/ 1,
			    /*sense_key*/ SSD_KEY_ILLEGAL_REQUEST,
			    /*asc*/ 0x26, /*ascq*/ 0x07, SSD_ELEM_NONE);
			goto done;
		}
	}
	ptr = &data->data[lencscd];
	for (nseg = 0, off = 0; off < lenseg; nseg++) {
		if (nseg >= TPC_MAX_SEGS) {
			free(list, M_CTL);
			ctl_set_sense(ctsio, /*current_error*/ 1,
			    /*sense_key*/ SSD_KEY_ILLEGAL_REQUEST,
			    /*asc*/ 0x26, /*ascq*/ 0x08, SSD_ELEM_NONE);
			goto done;
		}
		seg = (struct scsi_ec_segment *)(ptr + off);
		if (seg->type_code != EC_SEG_B2B &&
		    seg->type_code != EC_SEG_VERIFY &&
		    seg->type_code != EC_SEG_REGISTER_KEY) {
			free(list, M_CTL);
			ctl_set_sense(ctsio, /*current_error*/ 1,
			    /*sense_key*/ SSD_KEY_ILLEGAL_REQUEST,
			    /*asc*/ 0x26, /*ascq*/ 0x09, SSD_ELEM_NONE);
			goto done;
		}
		list->seg[nseg] = seg;
		off += sizeof(struct scsi_ec_segment) +
		    scsi_2btoul(seg->descr_length);
	}
	list->inl = &data->data[lencscd + lenseg];
	list->ncscd = lencscd / sizeof(struct scsi_ec_cscd);
	list->nseg = nseg;
	list->leninl = leninl;
	list->ctsio = ctsio;
	list->lun = lun;
	mtx_lock(&lun->lun_lock);
	if ((list->flags & EC_LIST_ID_USAGE_MASK) != EC_LIST_ID_USAGE_NONE) {
		tlist = tpc_find_list(lun, list->list_id, list->init_idx);
		if (tlist != NULL && !tlist->completed) {
			mtx_unlock(&lun->lun_lock);
			free(list, M_CTL);
			ctl_set_invalid_field(ctsio, /*sks_valid*/ 1,
			    /*command*/ 0, /*field*/ 0, /*bit_valid*/ 0,
			    /*bit*/ 0);
			goto done;
		}
		if (tlist != NULL) {
			TAILQ_REMOVE(&lun->tpc_lists, tlist, links);
			free(tlist, M_CTL);
		}
	}
	TAILQ_INSERT_TAIL(&lun->tpc_lists, list, links);
	mtx_unlock(&lun->lun_lock);

	tpc_process(list);
	return (CTL_RETVAL_COMPLETE);

done:
	if (ctsio->io_hdr.flags & CTL_FLAG_ALLOCATED) {
		free(ctsio->kern_data_ptr, M_CTL);
		ctsio->io_hdr.flags &= ~CTL_FLAG_ALLOCATED;
	}
	ctl_done((union ctl_io *)ctsio);
	return (CTL_RETVAL_COMPLETE);
}

int
ctl_extended_copy_lid4(struct ctl_scsiio *ctsio)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_extended_copy *cdb;
	struct scsi_extended_copy_lid4_data *data;
	struct scsi_ec_cscd *cscd;
	struct scsi_ec_segment *seg;
	struct tpc_list *list, *tlist;
	uint8_t *ptr;
	const char *value;
	int len, off, lencscd, lenseg, leninl, nseg;

	CTL_DEBUG_PRINT(("ctl_extended_copy_lid4\n"));

	cdb = (struct scsi_extended_copy *)ctsio->cdb;
	len = scsi_4btoul(cdb->length);

	if (len == 0) {
		ctl_set_success(ctsio);
		goto done;
	}
	if (len < sizeof(struct scsi_extended_copy_lid4_data) ||
	    len > sizeof(struct scsi_extended_copy_lid4_data) +
	    TPC_MAX_LIST + TPC_MAX_INLINE) {
		ctl_set_invalid_field(ctsio, /*sks_valid*/ 1, /*command*/ 1,
		    /*field*/ 9, /*bit_valid*/ 0, /*bit*/ 0);
		goto done;
	}

	/*
	 * If we've got a kernel request that hasn't been malloced yet,
	 * malloc it and tell the caller the data buffer is here.
	 */
	if ((ctsio->io_hdr.flags & CTL_FLAG_ALLOCATED) == 0) {
		ctsio->kern_data_ptr = malloc(len, M_CTL, M_WAITOK);
		ctsio->kern_data_len = len;
		ctsio->kern_total_len = len;
		ctsio->kern_rel_offset = 0;
		ctsio->kern_sg_entries = 0;
		ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
		ctsio->be_move_done = ctl_config_move_done;
		ctl_datamove((union ctl_io *)ctsio);

		return (CTL_RETVAL_COMPLETE);
	}

	data = (struct scsi_extended_copy_lid4_data *)ctsio->kern_data_ptr;
	lencscd = scsi_2btoul(data->cscd_list_length);
	lenseg = scsi_2btoul(data->segment_list_length);
	leninl = scsi_2btoul(data->inline_data_length);
	if (lencscd > TPC_MAX_CSCDS * sizeof(struct scsi_ec_cscd)) {
		ctl_set_sense(ctsio, /*current_error*/ 1,
		    /*sense_key*/ SSD_KEY_ILLEGAL_REQUEST,
		    /*asc*/ 0x26, /*ascq*/ 0x06, SSD_ELEM_NONE);
		goto done;
	}
	if (lenseg > TPC_MAX_SEGS * sizeof(struct scsi_ec_segment)) {
		ctl_set_sense(ctsio, /*current_error*/ 1,
		    /*sense_key*/ SSD_KEY_ILLEGAL_REQUEST,
		    /*asc*/ 0x26, /*ascq*/ 0x08, SSD_ELEM_NONE);
		goto done;
	}
	if (lencscd + lenseg > TPC_MAX_LIST ||
	    leninl > TPC_MAX_INLINE ||
	    len < sizeof(struct scsi_extended_copy_lid1_data) +
	     lencscd + lenseg + leninl) {
		ctl_set_param_len_error(ctsio);
		goto done;
	}

	list = malloc(sizeof(struct tpc_list), M_CTL, M_WAITOK | M_ZERO);
	list->service_action = cdb->service_action;
	value = dnvlist_get_string(lun->be_lun->options, "insecure_tpc", NULL);
	if (value != NULL && strcmp(value, "on") == 0)
		list->init_port = -1;
	else
		list->init_port = ctsio->io_hdr.nexus.targ_port;
	list->init_idx = ctl_get_initindex(&ctsio->io_hdr.nexus);
	list->list_id = scsi_4btoul(data->list_identifier);
	list->flags = data->flags;
	list->params = ctsio->kern_data_ptr;
	list->cscd = (struct scsi_ec_cscd *)&data->data[0];
	ptr = &data->data[0];
	for (off = 0; off < lencscd; off += sizeof(struct scsi_ec_cscd)) {
		cscd = (struct scsi_ec_cscd *)(ptr + off);
		if (cscd->type_code != EC_CSCD_ID) {
			free(list, M_CTL);
			ctl_set_sense(ctsio, /*current_error*/ 1,
			    /*sense_key*/ SSD_KEY_ILLEGAL_REQUEST,
			    /*asc*/ 0x26, /*ascq*/ 0x07, SSD_ELEM_NONE);
			goto done;
		}
	}
	ptr = &data->data[lencscd];
	for (nseg = 0, off = 0; off < lenseg; nseg++) {
		if (nseg >= TPC_MAX_SEGS) {
			free(list, M_CTL);
			ctl_set_sense(ctsio, /*current_error*/ 1,
			    /*sense_key*/ SSD_KEY_ILLEGAL_REQUEST,
			    /*asc*/ 0x26, /*ascq*/ 0x08, SSD_ELEM_NONE);
			goto done;
		}
		seg = (struct scsi_ec_segment *)(ptr + off);
		if (seg->type_code != EC_SEG_B2B &&
		    seg->type_code != EC_SEG_VERIFY &&
		    seg->type_code != EC_SEG_REGISTER_KEY) {
			free(list, M_CTL);
			ctl_set_sense(ctsio, /*current_error*/ 1,
			    /*sense_key*/ SSD_KEY_ILLEGAL_REQUEST,
			    /*asc*/ 0x26, /*ascq*/ 0x09, SSD_ELEM_NONE);
			goto done;
		}
		list->seg[nseg] = seg;
		off += sizeof(struct scsi_ec_segment) +
		    scsi_2btoul(seg->descr_length);
	}
	list->inl = &data->data[lencscd + lenseg];
	list->ncscd = lencscd / sizeof(struct scsi_ec_cscd);
	list->nseg = nseg;
	list->leninl = leninl;
	list->ctsio = ctsio;
	list->lun = lun;
	mtx_lock(&lun->lun_lock);
	if ((list->flags & EC_LIST_ID_USAGE_MASK) != EC_LIST_ID_USAGE_NONE) {
		tlist = tpc_find_list(lun, list->list_id, list->init_idx);
		if (tlist != NULL && !tlist->completed) {
			mtx_unlock(&lun->lun_lock);
			free(list, M_CTL);
			ctl_set_invalid_field(ctsio, /*sks_valid*/ 1,
			    /*command*/ 0, /*field*/ 0, /*bit_valid*/ 0,
			    /*bit*/ 0);
			goto done;
		}
		if (tlist != NULL) {
			TAILQ_REMOVE(&lun->tpc_lists, tlist, links);
			free(tlist, M_CTL);
		}
	}
	TAILQ_INSERT_TAIL(&lun->tpc_lists, list, links);
	mtx_unlock(&lun->lun_lock);

	tpc_process(list);
	return (CTL_RETVAL_COMPLETE);

done:
	if (ctsio->io_hdr.flags & CTL_FLAG_ALLOCATED) {
		free(ctsio->kern_data_ptr, M_CTL);
		ctsio->io_hdr.flags &= ~CTL_FLAG_ALLOCATED;
	}
	ctl_done((union ctl_io *)ctsio);
	return (CTL_RETVAL_COMPLETE);
}

static void
tpc_create_token(struct ctl_lun *lun, struct ctl_port *port, off_t len,
    struct scsi_token *token)
{
	static int id = 0;
	struct scsi_vpd_id_descriptor *idd = NULL;
	struct scsi_ec_cscd_id *cscd;
	struct scsi_read_capacity_data_long *dtsd;
	int targid_len;

	scsi_ulto4b(ROD_TYPE_AUR, token->type);
	scsi_ulto2b(0x01f8, token->length);
	scsi_u64to8b(atomic_fetchadd_int(&id, 1), &token->body[0]);
	if (lun->lun_devid)
		idd = scsi_get_devid_desc((struct scsi_vpd_id_descriptor *)
		    lun->lun_devid->data, lun->lun_devid->len,
		    scsi_devid_is_lun_naa);
	if (idd == NULL && lun->lun_devid)
		idd = scsi_get_devid_desc((struct scsi_vpd_id_descriptor *)
		    lun->lun_devid->data, lun->lun_devid->len,
		    scsi_devid_is_lun_eui64);
	if (idd != NULL) {
		cscd = (struct scsi_ec_cscd_id *)&token->body[8];
		cscd->type_code = EC_CSCD_ID;
		cscd->luidt_pdt = T_DIRECT;
		memcpy(&cscd->codeset, idd, 4 + idd->length);
		scsi_ulto3b(lun->be_lun->blocksize, cscd->dtsp.block_length);
	}
	scsi_u64to8b(0, &token->body[40]); /* XXX: Should be 128bit value. */
	scsi_u64to8b(len, &token->body[48]);

	/* ROD token device type specific data (RC16 without first field) */
	dtsd = (struct scsi_read_capacity_data_long *)&token->body[88 - 8];
	scsi_ulto4b(lun->be_lun->blocksize, dtsd->length);
	dtsd->prot_lbppbe = lun->be_lun->pblockexp & SRC16_LBPPBE;
	scsi_ulto2b(lun->be_lun->pblockoff & SRC16_LALBA_A, dtsd->lalba_lbp);
	if (lun->be_lun->flags & CTL_LUN_FLAG_UNMAP)
		dtsd->lalba_lbp[0] |= SRC16_LBPME | SRC16_LBPRZ;

	if (port->target_devid) {
		targid_len = port->target_devid->len;
		memcpy(&token->body[120], port->target_devid->data, targid_len);
	} else
		targid_len = 32;
	arc4rand(&token->body[120 + targid_len], 384 - targid_len, 0);
};

int
ctl_populate_token(struct ctl_scsiio *ctsio)
{
	struct ctl_softc *softc = CTL_SOFTC(ctsio);
	struct ctl_port *port = CTL_PORT(ctsio);
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_populate_token *cdb;
	struct scsi_populate_token_data *data;
	struct tpc_list *list, *tlist;
	struct tpc_token *token;
	uint64_t lba;
	int len, lendata, lendesc;

	CTL_DEBUG_PRINT(("ctl_populate_token\n"));

	cdb = (struct scsi_populate_token *)ctsio->cdb;
	len = scsi_4btoul(cdb->length);

	if (len < sizeof(struct scsi_populate_token_data) ||
	    len > sizeof(struct scsi_populate_token_data) +
	     TPC_MAX_SEGS * sizeof(struct scsi_range_desc)) {
		ctl_set_invalid_field(ctsio, /*sks_valid*/ 1, /*command*/ 1,
		    /*field*/ 9, /*bit_valid*/ 0, /*bit*/ 0);
		goto done;
	}

	/*
	 * If we've got a kernel request that hasn't been malloced yet,
	 * malloc it and tell the caller the data buffer is here.
	 */
	if ((ctsio->io_hdr.flags & CTL_FLAG_ALLOCATED) == 0) {
		ctsio->kern_data_ptr = malloc(len, M_CTL, M_WAITOK);
		ctsio->kern_data_len = len;
		ctsio->kern_total_len = len;
		ctsio->kern_rel_offset = 0;
		ctsio->kern_sg_entries = 0;
		ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
		ctsio->be_move_done = ctl_config_move_done;
		ctl_datamove((union ctl_io *)ctsio);

		return (CTL_RETVAL_COMPLETE);
	}

	data = (struct scsi_populate_token_data *)ctsio->kern_data_ptr;
	lendata = scsi_2btoul(data->length);
	if (lendata < sizeof(struct scsi_populate_token_data) - 2 +
	    sizeof(struct scsi_range_desc)) {
		ctl_set_invalid_field(ctsio, /*sks_valid*/ 1, /*command*/ 0,
		    /*field*/ 0, /*bit_valid*/ 0, /*bit*/ 0);
		goto done;
	}
	lendesc = scsi_2btoul(data->range_descriptor_length);
	if (lendesc < sizeof(struct scsi_range_desc) ||
	    len < sizeof(struct scsi_populate_token_data) + lendesc ||
	    lendata < sizeof(struct scsi_populate_token_data) - 2 + lendesc) {
		ctl_set_invalid_field(ctsio, /*sks_valid*/ 1, /*command*/ 0,
		    /*field*/ 14, /*bit_valid*/ 0, /*bit*/ 0);
		goto done;
	}
/*
	printf("PT(list=%u) flags=%x to=%d rt=%x len=%x\n",
	    scsi_4btoul(cdb->list_identifier),
	    data->flags, scsi_4btoul(data->inactivity_timeout),
	    scsi_4btoul(data->rod_type),
	    scsi_2btoul(data->range_descriptor_length));
*/

	/* Validate INACTIVITY TIMEOUT field */
	if (scsi_4btoul(data->inactivity_timeout) > TPC_MAX_TOKEN_TIMEOUT) {
		ctl_set_invalid_field(ctsio, /*sks_valid*/ 1,
		    /*command*/ 0, /*field*/ 4, /*bit_valid*/ 0,
		    /*bit*/ 0);
		goto done;
	}

	/* Validate ROD TYPE field */
	if ((data->flags & EC_PT_RTV) &&
	    scsi_4btoul(data->rod_type) != ROD_TYPE_AUR) {
		ctl_set_invalid_field(ctsio, /*sks_valid*/ 1, /*command*/ 0,
		    /*field*/ 8, /*bit_valid*/ 0, /*bit*/ 0);
		goto done;
	}

	/* Validate list of ranges */
	if (tpc_check_ranges_l(&data->desc[0],
	    scsi_2btoul(data->range_descriptor_length) /
	    sizeof(struct scsi_range_desc),
	    lun->be_lun->maxlba, &lba) != 0) {
		ctl_set_lba_out_of_range(ctsio, lba);
		goto done;
	}
	if (tpc_check_ranges_x(&data->desc[0],
	    scsi_2btoul(data->range_descriptor_length) /
	    sizeof(struct scsi_range_desc)) != 0) {
		ctl_set_invalid_field(ctsio, /*sks_valid*/ 0,
		    /*command*/ 0, /*field*/ 0, /*bit_valid*/ 0,
		    /*bit*/ 0);
		goto done;
	}

	list = malloc(sizeof(struct tpc_list), M_CTL, M_WAITOK | M_ZERO);
	list->service_action = cdb->service_action;
	list->init_port = ctsio->io_hdr.nexus.targ_port;
	list->init_idx = ctl_get_initindex(&ctsio->io_hdr.nexus);
	list->list_id = scsi_4btoul(cdb->list_identifier);
	list->flags = data->flags;
	list->ctsio = ctsio;
	list->lun = lun;
	mtx_lock(&lun->lun_lock);
	tlist = tpc_find_list(lun, list->list_id, list->init_idx);
	if (tlist != NULL && !tlist->completed) {
		mtx_unlock(&lun->lun_lock);
		free(list, M_CTL);
		ctl_set_invalid_field(ctsio, /*sks_valid*/ 1,
		    /*command*/ 0, /*field*/ 0, /*bit_valid*/ 0,
		    /*bit*/ 0);
		goto done;
	}
	if (tlist != NULL) {
		TAILQ_REMOVE(&lun->tpc_lists, tlist, links);
		free(tlist, M_CTL);
	}
	TAILQ_INSERT_TAIL(&lun->tpc_lists, list, links);
	mtx_unlock(&lun->lun_lock);

	token = malloc(sizeof(*token), M_CTL, M_WAITOK | M_ZERO);
	token->lun = lun->lun;
	token->blocksize = lun->be_lun->blocksize;
	token->params = ctsio->kern_data_ptr;
	token->range = &data->desc[0];
	token->nrange = scsi_2btoul(data->range_descriptor_length) /
	    sizeof(struct scsi_range_desc);
	list->cursectors = tpc_ranges_length(token->range, token->nrange);
	list->curbytes = (off_t)list->cursectors * lun->be_lun->blocksize;
	tpc_create_token(lun, port, list->curbytes,
	    (struct scsi_token *)token->token);
	token->active = 0;
	token->last_active = time_uptime;
	token->timeout = scsi_4btoul(data->inactivity_timeout);
	if (token->timeout == 0)
		token->timeout = TPC_DFL_TOKEN_TIMEOUT;
	else if (token->timeout < TPC_MIN_TOKEN_TIMEOUT)
		token->timeout = TPC_MIN_TOKEN_TIMEOUT;
	memcpy(list->res_token, token->token, sizeof(list->res_token));
	list->res_token_valid = 1;
	list->curseg = 0;
	list->completed = 1;
	list->last_active = time_uptime;
	mtx_lock(&softc->tpc_lock);
	TAILQ_INSERT_TAIL(&softc->tpc_tokens, token, links);
	mtx_unlock(&softc->tpc_lock);
	ctl_set_success(ctsio);
	ctl_done((union ctl_io *)ctsio);
	return (CTL_RETVAL_COMPLETE);

done:
	if (ctsio->io_hdr.flags & CTL_FLAG_ALLOCATED) {
		free(ctsio->kern_data_ptr, M_CTL);
		ctsio->io_hdr.flags &= ~CTL_FLAG_ALLOCATED;
	}
	ctl_done((union ctl_io *)ctsio);
	return (CTL_RETVAL_COMPLETE);
}

int
ctl_write_using_token(struct ctl_scsiio *ctsio)
{
	struct ctl_softc *softc = CTL_SOFTC(ctsio);
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_write_using_token *cdb;
	struct scsi_write_using_token_data *data;
	struct tpc_list *list, *tlist;
	struct tpc_token *token;
	uint64_t lba;
	int len, lendata, lendesc;

	CTL_DEBUG_PRINT(("ctl_write_using_token\n"));

	cdb = (struct scsi_write_using_token *)ctsio->cdb;
	len = scsi_4btoul(cdb->length);

	if (len < sizeof(struct scsi_write_using_token_data) ||
	    len > sizeof(struct scsi_write_using_token_data) +
	     TPC_MAX_SEGS * sizeof(struct scsi_range_desc)) {
		ctl_set_invalid_field(ctsio, /*sks_valid*/ 1, /*command*/ 1,
		    /*field*/ 9, /*bit_valid*/ 0, /*bit*/ 0);
		goto done;
	}

	/*
	 * If we've got a kernel request that hasn't been malloced yet,
	 * malloc it and tell the caller the data buffer is here.
	 */
	if ((ctsio->io_hdr.flags & CTL_FLAG_ALLOCATED) == 0) {
		ctsio->kern_data_ptr = malloc(len, M_CTL, M_WAITOK);
		ctsio->kern_data_len = len;
		ctsio->kern_total_len = len;
		ctsio->kern_rel_offset = 0;
		ctsio->kern_sg_entries = 0;
		ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
		ctsio->be_move_done = ctl_config_move_done;
		ctl_datamove((union ctl_io *)ctsio);

		return (CTL_RETVAL_COMPLETE);
	}

	data = (struct scsi_write_using_token_data *)ctsio->kern_data_ptr;
	lendata = scsi_2btoul(data->length);
	if (lendata < sizeof(struct scsi_write_using_token_data) - 2 +
	    sizeof(struct scsi_range_desc)) {
		ctl_set_invalid_field(ctsio, /*sks_valid*/ 1, /*command*/ 0,
		    /*field*/ 0, /*bit_valid*/ 0, /*bit*/ 0);
		goto done;
	}
	lendesc = scsi_2btoul(data->range_descriptor_length);
	if (lendesc < sizeof(struct scsi_range_desc) ||
	    len < sizeof(struct scsi_write_using_token_data) + lendesc ||
	    lendata < sizeof(struct scsi_write_using_token_data) - 2 + lendesc) {
		ctl_set_invalid_field(ctsio, /*sks_valid*/ 1, /*command*/ 0,
		    /*field*/ 534, /*bit_valid*/ 0, /*bit*/ 0);
		goto done;
	}
/*
	printf("WUT(list=%u) flags=%x off=%ju len=%x\n",
	    scsi_4btoul(cdb->list_identifier),
	    data->flags, scsi_8btou64(data->offset_into_rod),
	    scsi_2btoul(data->range_descriptor_length));
*/

	/* Validate list of ranges */
	if (tpc_check_ranges_l(&data->desc[0],
	    scsi_2btoul(data->range_descriptor_length) /
	    sizeof(struct scsi_range_desc),
	    lun->be_lun->maxlba, &lba) != 0) {
		ctl_set_lba_out_of_range(ctsio, lba);
		goto done;
	}
	if (tpc_check_ranges_x(&data->desc[0],
	    scsi_2btoul(data->range_descriptor_length) /
	    sizeof(struct scsi_range_desc)) != 0) {
		ctl_set_invalid_field(ctsio, /*sks_valid*/ 0,
		    /*command*/ 0, /*field*/ 0, /*bit_valid*/ 0,
		    /*bit*/ 0);
		goto done;
	}

	list = malloc(sizeof(struct tpc_list), M_CTL, M_WAITOK | M_ZERO);
	list->service_action = cdb->service_action;
	list->init_port = ctsio->io_hdr.nexus.targ_port;
	list->init_idx = ctl_get_initindex(&ctsio->io_hdr.nexus);
	list->list_id = scsi_4btoul(cdb->list_identifier);
	list->flags = data->flags;
	list->params = ctsio->kern_data_ptr;
	list->range = &data->desc[0];
	list->nrange = scsi_2btoul(data->range_descriptor_length) /
	    sizeof(struct scsi_range_desc);
	list->offset_into_rod = scsi_8btou64(data->offset_into_rod);
	list->ctsio = ctsio;
	list->lun = lun;
	mtx_lock(&lun->lun_lock);
	tlist = tpc_find_list(lun, list->list_id, list->init_idx);
	if (tlist != NULL && !tlist->completed) {
		mtx_unlock(&lun->lun_lock);
		free(list, M_CTL);
		ctl_set_invalid_field(ctsio, /*sks_valid*/ 1,
		    /*command*/ 0, /*field*/ 0, /*bit_valid*/ 0,
		    /*bit*/ 0);
		goto done;
	}
	if (tlist != NULL) {
		TAILQ_REMOVE(&lun->tpc_lists, tlist, links);
		free(tlist, M_CTL);
	}
	TAILQ_INSERT_TAIL(&lun->tpc_lists, list, links);
	mtx_unlock(&lun->lun_lock);

	/* Block device zero ROD token -> no token. */
	if (scsi_4btoul(data->rod_token) == ROD_TYPE_BLOCK_ZERO) {
		tpc_process(list);
		return (CTL_RETVAL_COMPLETE);
	}

	mtx_lock(&softc->tpc_lock);
	TAILQ_FOREACH(token, &softc->tpc_tokens, links) {
		if (memcmp(token->token, data->rod_token,
		    sizeof(data->rod_token)) == 0)
			break;
	}
	if (token != NULL) {
		token->active++;
		list->token = token;
		if (data->flags & EC_WUT_DEL_TKN)
			token->timeout = 0;
	}
	mtx_unlock(&softc->tpc_lock);
	if (token == NULL) {
		mtx_lock(&lun->lun_lock);
		TAILQ_REMOVE(&lun->tpc_lists, list, links);
		mtx_unlock(&lun->lun_lock);
		free(list, M_CTL);
		ctl_set_sense(ctsio, /*current_error*/ 1,
		    /*sense_key*/ SSD_KEY_ILLEGAL_REQUEST,
		    /*asc*/ 0x23, /*ascq*/ 0x04, SSD_ELEM_NONE);
		goto done;
	}

	tpc_process(list);
	return (CTL_RETVAL_COMPLETE);

done:
	if (ctsio->io_hdr.flags & CTL_FLAG_ALLOCATED) {
		free(ctsio->kern_data_ptr, M_CTL);
		ctsio->io_hdr.flags &= ~CTL_FLAG_ALLOCATED;
	}
	ctl_done((union ctl_io *)ctsio);
	return (CTL_RETVAL_COMPLETE);
}

int
ctl_receive_rod_token_information(struct ctl_scsiio *ctsio)
{
	struct ctl_lun *lun = CTL_LUN(ctsio);
	struct scsi_receive_rod_token_information *cdb;
	struct scsi_receive_copy_status_lid4_data *data;
	struct tpc_list *list;
	struct tpc_list list_copy;
	uint8_t *ptr;
	int retval;
	int alloc_len, total_len, token_len;
	uint32_t list_id;

	CTL_DEBUG_PRINT(("ctl_receive_rod_token_information\n"));

	cdb = (struct scsi_receive_rod_token_information *)ctsio->cdb;
	retval = CTL_RETVAL_COMPLETE;

	list_id = scsi_4btoul(cdb->list_identifier);
	mtx_lock(&lun->lun_lock);
	list = tpc_find_list(lun, list_id,
	    ctl_get_initindex(&ctsio->io_hdr.nexus));
	if (list == NULL) {
		mtx_unlock(&lun->lun_lock);
		ctl_set_invalid_field(ctsio, /*sks_valid*/ 1,
		    /*command*/ 1, /*field*/ 2, /*bit_valid*/ 0,
		    /*bit*/ 0);
		ctl_done((union ctl_io *)ctsio);
		return (retval);
	}
	list_copy = *list;
	if (list->completed) {
		TAILQ_REMOVE(&lun->tpc_lists, list, links);
		free(list, M_CTL);
	}
	mtx_unlock(&lun->lun_lock);

	token_len = list_copy.res_token_valid ? 2 + sizeof(list_copy.res_token) : 0;
	total_len = sizeof(*data) + list_copy.sense_len + 4 + token_len;
	alloc_len = scsi_4btoul(cdb->length);

	ctsio->kern_data_ptr = malloc(total_len, M_CTL, M_WAITOK | M_ZERO);
	ctsio->kern_sg_entries = 0;
	ctsio->kern_rel_offset = 0;
	ctsio->kern_data_len = min(total_len, alloc_len);
	ctsio->kern_total_len = ctsio->kern_data_len;

	data = (struct scsi_receive_copy_status_lid4_data *)ctsio->kern_data_ptr;
	scsi_ulto4b(sizeof(*data) - 4 + list_copy.sense_len +
	    4 + token_len, data->available_data);
	data->response_to_service_action = list_copy.service_action;
	if (list_copy.completed) {
		if (list_copy.error)
			data->copy_command_status = RCS_CCS_ERROR;
		else if (list_copy.abort)
			data->copy_command_status = RCS_CCS_ABORTED;
		else
			data->copy_command_status = RCS_CCS_COMPLETED;
	} else
		data->copy_command_status = RCS_CCS_INPROG_FG;
	scsi_ulto2b(list_copy.curops, data->operation_counter);
	scsi_ulto4b(UINT32_MAX, data->estimated_status_update_delay);
	data->transfer_count_units = RCS_TC_LBAS;
	scsi_u64to8b(list_copy.cursectors, data->transfer_count);
	scsi_ulto2b(list_copy.curseg, data->segments_processed);
	data->length_of_the_sense_data_field = list_copy.sense_len;
	data->sense_data_length = list_copy.sense_len;
	memcpy(data->sense_data, &list_copy.sense_data, list_copy.sense_len);

	ptr = &data->sense_data[data->length_of_the_sense_data_field];
	scsi_ulto4b(token_len, &ptr[0]);
	if (list_copy.res_token_valid) {
		scsi_ulto2b(0, &ptr[4]);
		memcpy(&ptr[6], list_copy.res_token, sizeof(list_copy.res_token));
	}
/*
	printf("RRTI(list=%u) valid=%d\n",
	    scsi_4btoul(cdb->list_identifier), list_copy.res_token_valid);
*/
	ctl_set_success(ctsio);
	ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	ctsio->be_move_done = ctl_config_move_done;
	ctl_datamove((union ctl_io *)ctsio);
	return (retval);
}

int
ctl_report_all_rod_tokens(struct ctl_scsiio *ctsio)
{
	struct ctl_softc *softc = CTL_SOFTC(ctsio);
	struct scsi_report_all_rod_tokens *cdb;
	struct scsi_report_all_rod_tokens_data *data;
	struct tpc_token *token;
	int retval;
	int alloc_len, total_len, tokens, i;

	CTL_DEBUG_PRINT(("ctl_receive_rod_token_information\n"));

	cdb = (struct scsi_report_all_rod_tokens *)ctsio->cdb;
	retval = CTL_RETVAL_COMPLETE;

	tokens = 0;
	mtx_lock(&softc->tpc_lock);
	TAILQ_FOREACH(token, &softc->tpc_tokens, links)
		tokens++;
	mtx_unlock(&softc->tpc_lock);
	if (tokens > 512)
		tokens = 512;

	total_len = sizeof(*data) + tokens * 96;
	alloc_len = scsi_4btoul(cdb->length);

	ctsio->kern_data_ptr = malloc(total_len, M_CTL, M_WAITOK | M_ZERO);
	ctsio->kern_sg_entries = 0;
	ctsio->kern_rel_offset = 0;
	ctsio->kern_data_len = min(total_len, alloc_len);
	ctsio->kern_total_len = ctsio->kern_data_len;

	data = (struct scsi_report_all_rod_tokens_data *)ctsio->kern_data_ptr;
	i = 0;
	mtx_lock(&softc->tpc_lock);
	TAILQ_FOREACH(token, &softc->tpc_tokens, links) {
		if (i >= tokens)
			break;
		memcpy(&data->rod_management_token_list[i * 96],
		    token->token, 96);
		i++;
	}
	mtx_unlock(&softc->tpc_lock);
	scsi_ulto4b(sizeof(*data) - 4 + i * 96, data->available_data);
/*
	printf("RART tokens=%d\n", i);
*/
	ctl_set_success(ctsio);
	ctsio->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	ctsio->be_move_done = ctl_config_move_done;
	ctl_datamove((union ctl_io *)ctsio);
	return (retval);
}

