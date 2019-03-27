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

/**
 * @file
 * Code to handle unsolicited received FC frames.
 */

/*!
 * @defgroup unsol Unsolicited Frame Handling
 */

#include "ocs.h"
#include "ocs_els.h"
#include "ocs_fabric.h"
#include "ocs_device.h"


#define frame_printf(ocs, hdr, fmt, ...) \
	do { \
		char s_id_text[16]; \
		ocs_node_fcid_display(fc_be24toh((hdr)->s_id), s_id_text, sizeof(s_id_text)); \
		ocs_log_debug(ocs, "[%06x.%s] %02x/%04x/%04x: " fmt, fc_be24toh((hdr)->d_id), s_id_text, \
			(hdr)->r_ctl, ocs_be16toh((hdr)->ox_id), ocs_be16toh((hdr)->rx_id), ##__VA_ARGS__); \
	} while(0)

static int32_t ocs_unsol_process(ocs_t *ocs, ocs_hw_sequence_t *seq);
static int32_t ocs_dispatch_fcp_cmd(ocs_node_t *node, ocs_hw_sequence_t *seq);
static int32_t ocs_dispatch_fcp_cmd_auto_xfer_rdy(ocs_node_t *node, ocs_hw_sequence_t *seq);
static int32_t ocs_dispatch_fcp_data(ocs_node_t *node, ocs_hw_sequence_t *seq);
static int32_t ocs_domain_dispatch_frame(void *arg, ocs_hw_sequence_t *seq);
static int32_t ocs_node_dispatch_frame(void *arg, ocs_hw_sequence_t *seq);
static int32_t ocs_fc_tmf_rejected_cb(ocs_io_t *io, ocs_scsi_io_status_e scsi_status, uint32_t flags, void *arg);
static ocs_hw_sequence_t *ocs_frame_next(ocs_list_t *pend_list, ocs_lock_t *list_lock);
static uint8_t ocs_node_frames_held(void *arg);
static uint8_t ocs_domain_frames_held(void *arg);
static int32_t ocs_purge_pending(ocs_t *ocs, ocs_list_t *pend_list, ocs_lock_t *list_lock);
static int32_t ocs_sframe_send_task_set_full_or_busy(ocs_node_t *node, ocs_hw_sequence_t *seq);

#define OCS_MAX_FRAMES_BEFORE_YEILDING 10000

/**
 * @brief Process the RQ circular buffer and process the incoming frames.
 *
 * @param mythread Pointer to thread object.
 *
 * @return Returns 0 on success, or a non-zero value on failure.
 */
int32_t
ocs_unsol_rq_thread(ocs_thread_t *mythread)
{
	ocs_xport_rq_thread_info_t *thread_data = mythread->arg;
	ocs_t *ocs = thread_data->ocs;
	ocs_hw_sequence_t *seq;
	uint32_t yield_count = OCS_MAX_FRAMES_BEFORE_YEILDING;

	ocs_log_debug(ocs, "%s running\n", mythread->name);
	while (!ocs_thread_terminate_requested(mythread)) {
		seq = ocs_cbuf_get(thread_data->seq_cbuf, 100000);
		if (seq == NULL) {
			/* Prevent soft lockups by yielding the CPU */
			ocs_thread_yield(&thread_data->thread);
			yield_count = OCS_MAX_FRAMES_BEFORE_YEILDING;
			continue;
		}
		/* Note: Always returns 0 */
		ocs_unsol_process((ocs_t*)seq->hw->os, seq);

		/* We have to prevent CPU soft lockups, so just yield the CPU after x frames. */
		if (--yield_count == 0) {
			ocs_thread_yield(&thread_data->thread);
			yield_count = OCS_MAX_FRAMES_BEFORE_YEILDING;
		}
	}
	ocs_log_debug(ocs, "%s exiting\n", mythread->name);
	thread_data->thread_started = FALSE;
	return 0;
}

/**
 * @ingroup unsol
 * @brief Callback function when aborting a port owned XRI
 * exchanges.
 *
 * @return Returns 0.
 */
static int32_t
ocs_unsol_abort_cb (ocs_hw_io_t *hio, ocs_remote_node_t *rnode, uint32_t len, int32_t status, uint32_t ext, void *arg)
{
	ocs_t *ocs = arg;
	ocs_assert(hio, -1);
	ocs_assert(arg, -1);
	ocs_log_debug(ocs, "xri=0x%x tag=0x%x\n", hio->indicator, hio->reqtag);
	ocs_hw_io_free(&ocs->hw, hio);
	return 0;
}


/**
 * @ingroup unsol
 * @brief Abort either a RQ Pair auto XFER RDY XRI.
 * @return Returns None.
 */
static void
ocs_port_owned_abort(ocs_t *ocs, ocs_hw_io_t *hio)
{
	ocs_hw_rtn_e hw_rc;
	hw_rc = ocs_hw_io_abort(&ocs->hw, hio, FALSE,
				  ocs_unsol_abort_cb, ocs);
	if((hw_rc == OCS_HW_RTN_IO_ABORT_IN_PROGRESS) ||
	   (hw_rc == OCS_HW_RTN_IO_PORT_OWNED_ALREADY_ABORTED)) {
		ocs_log_debug(ocs, "already aborted XRI 0x%x\n", hio->indicator);
	} else if(hw_rc != OCS_HW_RTN_SUCCESS) {
		ocs_log_debug(ocs, "Error aborting XRI 0x%x status %d\n",
			      hio->indicator, hw_rc);
	}
}

/**
 * @ingroup unsol
 * @brief Handle unsolicited FC frames.
 *
 * <h3 class="desc">Description</h3>
 * This function is called from the HW with unsolicited FC frames (FCP, ELS, BLS, etc.).
 *
 * @param arg Application-specified callback data.
 * @param seq Header/payload sequence buffers.
 *
 * @return Returns 0 on success; or a negative error value on failure.
 */

int32_t
ocs_unsolicited_cb(void *arg, ocs_hw_sequence_t *seq)
{
	ocs_t *ocs = arg;
	ocs_xport_t *xport = ocs->xport;
	int32_t rc;

	CPUTRACE("");

	if (ocs->rq_threads == 0) {
		rc = ocs_unsol_process(ocs, seq);
	} else {
		/* use the ox_id to dispatch this IO to a thread */
		fc_header_t *hdr = seq->header->dma.virt;
		uint32_t ox_id =  ocs_be16toh(hdr->ox_id);
		uint32_t thr_index = ox_id % ocs->rq_threads;

		rc = ocs_cbuf_put(xport->rq_thread_info[thr_index].seq_cbuf, seq);
	}

	if (rc) {
		ocs_hw_sequence_free(&ocs->hw, seq);
	}

	return 0;
}

/**
 * @ingroup unsol
 * @brief Handle unsolicited FC frames.
 *
 * <h3 class="desc">Description</h3>
 * This function is called either from ocs_unsolicited_cb() or ocs_unsol_rq_thread().
 *
 * @param ocs Pointer to the ocs structure.
 * @param seq Header/payload sequence buffers.
 *
 * @return Returns 0 on success, or a negative error value on failure.
 */
static int32_t
ocs_unsol_process(ocs_t *ocs, ocs_hw_sequence_t *seq)
{
	ocs_xport_fcfi_t *xport_fcfi = NULL;
	ocs_domain_t *domain;
	uint8_t seq_fcfi = seq->fcfi;

	/* HW_WORKAROUND_OVERRIDE_FCFI_IN_SRB */
	if (ocs->hw.workaround.override_fcfi) {
		if (ocs->hw.first_domain_idx > -1) {
			seq_fcfi = ocs->hw.first_domain_idx;
		}
	}

	/* Range check seq->fcfi */
	if (seq_fcfi < ARRAY_SIZE(ocs->xport->fcfi)) {
		xport_fcfi = &ocs->xport->fcfi[seq_fcfi];
	}

	/* If the transport FCFI entry is NULL, then drop the frame */
	if (xport_fcfi == NULL) {
		ocs_log_test(ocs, "FCFI %d is not valid, dropping frame\n", seq->fcfi);
		if (seq->hio != NULL) {
			ocs_port_owned_abort(ocs, seq->hio);
		}

		ocs_hw_sequence_free(&ocs->hw, seq);
		return 0;
	}
	domain = ocs_hw_domain_get(&ocs->hw, seq_fcfi);

	/*
	 * If we are holding frames or the domain is not yet registered or
	 * there's already frames on the pending list,
	 * then add the new frame to pending list
	 */
	if (domain == NULL ||
	    xport_fcfi->hold_frames ||
	    !ocs_list_empty(&xport_fcfi->pend_frames)) {
		ocs_lock(&xport_fcfi->pend_frames_lock);
			ocs_list_add_tail(&xport_fcfi->pend_frames, seq);
		ocs_unlock(&xport_fcfi->pend_frames_lock);

		if (domain != NULL) {
			/* immediately process pending frames */
			ocs_domain_process_pending(domain);
		}
	} else {
		/*
		 * We are not holding frames and pending list is empty, just process frame.
		 * A non-zero return means the frame was not handled - so cleanup
		 */
		if (ocs_domain_dispatch_frame(domain, seq)) {
			if (seq->hio != NULL) {
				ocs_port_owned_abort(ocs, seq->hio);
			}
			ocs_hw_sequence_free(&ocs->hw, seq);
		}
	}
	return 0;
}

/**
 * @ingroup unsol
 * @brief Process pending frames queued to the given node.
 *
 * <h3 class="desc">Description</h3>
 * Frames that are queued for the \c node are dispatched and returned
 * to the RQ.
 *
 * @param node Node of the queued frames that are to be dispatched.
 *
 * @return Returns 0 on success, or a negative error value on failure.
 */

int32_t
ocs_process_node_pending(ocs_node_t *node)
{
	ocs_t *ocs = node->ocs;
	ocs_hw_sequence_t *seq = NULL;
	uint32_t pend_frames_processed = 0;

	for (;;) {
		/* need to check for hold frames condition after each frame processed
		 * because any given frame could cause a transition to a state that
		 * holds frames
		 */
		if (ocs_node_frames_held(node)) {
			break;
		}

		/* Get next frame/sequence */
		ocs_lock(&node->pend_frames_lock);
			seq = ocs_list_remove_head(&node->pend_frames);
			if (seq == NULL) {
				pend_frames_processed = node->pend_frames_processed;
				node->pend_frames_processed = 0;
				ocs_unlock(&node->pend_frames_lock);
				break;
			}
			node->pend_frames_processed++;
		ocs_unlock(&node->pend_frames_lock);

		/* now dispatch frame(s) to dispatch function */
		if (ocs_node_dispatch_frame(node, seq)) {
			if (seq->hio != NULL) {
				ocs_port_owned_abort(ocs, seq->hio);
			}
			ocs_hw_sequence_free(&ocs->hw, seq);
		}
	}

	if (pend_frames_processed != 0) {
		ocs_log_debug(ocs, "%u node frames held and processed\n", pend_frames_processed);
	}

	return 0;
}

/**
 * @ingroup unsol
 * @brief Process pending frames queued to the given domain.
 *
 * <h3 class="desc">Description</h3>
 * Frames that are queued for the \c domain are dispatched and
 * returned to the RQ.
 *
 * @param domain Domain of the queued frames that are to be
 *		 dispatched.
 *
 * @return Returns 0 on success, or a negative error value on failure.
 */

int32_t
ocs_domain_process_pending(ocs_domain_t *domain)
{
	ocs_t *ocs = domain->ocs;
	ocs_xport_fcfi_t *xport_fcfi;
	ocs_hw_sequence_t *seq = NULL;
	uint32_t pend_frames_processed = 0;

	ocs_assert(domain->fcf_indicator < SLI4_MAX_FCFI, -1);
	xport_fcfi = &ocs->xport->fcfi[domain->fcf_indicator];

	for (;;) {
		/* need to check for hold frames condition after each frame processed
		 * because any given frame could cause a transition to a state that
		 * holds frames
		 */
		if (ocs_domain_frames_held(domain)) {
			break;
		}

		/* Get next frame/sequence */
		ocs_lock(&xport_fcfi->pend_frames_lock);
			seq = ocs_list_remove_head(&xport_fcfi->pend_frames);
			if (seq == NULL) {
				pend_frames_processed = xport_fcfi->pend_frames_processed;
				xport_fcfi->pend_frames_processed = 0;
				ocs_unlock(&xport_fcfi->pend_frames_lock);
				break;
			}
			xport_fcfi->pend_frames_processed++;
		ocs_unlock(&xport_fcfi->pend_frames_lock);

		/* now dispatch frame(s) to dispatch function */
		if (ocs_domain_dispatch_frame(domain, seq)) {
			if (seq->hio != NULL) {
				ocs_port_owned_abort(ocs, seq->hio);
			}
			ocs_hw_sequence_free(&ocs->hw, seq);
		}
	}
	if (pend_frames_processed != 0) {
		ocs_log_debug(ocs, "%u domain frames held and processed\n", pend_frames_processed);
	}
	return 0;
}

/**
 * @ingroup unsol
 * @brief Purge given pending list
 *
 * <h3 class="desc">Description</h3>
 * Frames that are queued on the given pending list are
 * discarded and returned to the RQ.
 *
 * @param ocs Pointer to ocs object.
 * @param pend_list Pending list to be purged.
 * @param list_lock Lock that protects pending list.
 *
 * @return Returns 0 on success, or a negative error value on failure.
 */

static int32_t
ocs_purge_pending(ocs_t *ocs, ocs_list_t *pend_list, ocs_lock_t *list_lock)
{
	ocs_hw_sequence_t *frame;

	for (;;) {
		frame = ocs_frame_next(pend_list, list_lock);
		if (frame == NULL) {
			break;
		}

		frame_printf(ocs, (fc_header_t*) frame->header->dma.virt, "Discarding held frame\n");
		if (frame->hio != NULL) {
			ocs_port_owned_abort(ocs, frame->hio);
		}
		ocs_hw_sequence_free(&ocs->hw, frame);
	}

	return 0;
}

/**
 * @ingroup unsol
 * @brief Purge node's pending (queued) frames.
 *
 * <h3 class="desc">Description</h3>
 * Frames that are queued for the \c node are discarded and returned
 * to the RQ.
 *
 * @param node Node of the queued frames that are to be discarded.
 *
 * @return Returns 0 on success, or a negative error value on failure.
 */

int32_t
ocs_node_purge_pending(ocs_node_t *node)
{
	return ocs_purge_pending(node->ocs, &node->pend_frames, &node->pend_frames_lock);
}

/**
 * @ingroup unsol
 * @brief Purge xport's pending (queued) frames.
 *
 * <h3 class="desc">Description</h3>
 * Frames that are queued for the \c xport are discarded and
 * returned to the RQ.
 *
 * @param domain Pointer to domain object.
 *
 * @return Returns 0 on success; or a negative error value on failure.
 */

int32_t
ocs_domain_purge_pending(ocs_domain_t *domain)
{
	ocs_t *ocs = domain->ocs;
	ocs_xport_fcfi_t *xport_fcfi;

	ocs_assert(domain->fcf_indicator < SLI4_MAX_FCFI, -1);
	xport_fcfi = &ocs->xport->fcfi[domain->fcf_indicator];
	return ocs_purge_pending(domain->ocs,
				 &xport_fcfi->pend_frames,
				 &xport_fcfi->pend_frames_lock);
}

/**
 * @ingroup unsol
 * @brief Check if node's pending frames are held.
 *
 * @param arg Node for which the pending frame hold condition is
 * checked.
 *
 * @return Returns 1 if node is holding pending frames, or 0
 * if not.
 */

static uint8_t
ocs_node_frames_held(void *arg)
{
	ocs_node_t *node = (ocs_node_t *)arg;
	return node->hold_frames;
}

/**
 * @ingroup unsol
 * @brief Check if domain's pending frames are held.
 *
 * @param arg Domain for which the pending frame hold condition is
 * checked.
 *
 * @return Returns 1 if domain is holding pending frames, or 0
 * if not.
 */

static uint8_t
ocs_domain_frames_held(void *arg)
{
	ocs_domain_t *domain = (ocs_domain_t *)arg;
	ocs_t *ocs = domain->ocs;
	ocs_xport_fcfi_t *xport_fcfi;

	ocs_assert(domain != NULL, 1);
	ocs_assert(domain->fcf_indicator < SLI4_MAX_FCFI, 1);
	xport_fcfi = &ocs->xport->fcfi[domain->fcf_indicator];
	return xport_fcfi->hold_frames;
}

/**
 * @ingroup unsol
 * @brief Globally (at xport level) hold unsolicited frames.
 *
 * <h3 class="desc">Description</h3>
 * This function places a hold on processing unsolicited FC
 * frames queued to the xport pending list.
 *
 * @param domain Pointer to domain object.
 *
 * @return Returns None.
 */

void
ocs_domain_hold_frames(ocs_domain_t *domain)
{
	ocs_t *ocs = domain->ocs;
	ocs_xport_fcfi_t *xport_fcfi;

	ocs_assert(domain->fcf_indicator < SLI4_MAX_FCFI);
	xport_fcfi = &ocs->xport->fcfi[domain->fcf_indicator];
	if (!xport_fcfi->hold_frames) {
		ocs_log_debug(domain->ocs, "hold frames set for FCFI %d\n",
			      domain->fcf_indicator);
		xport_fcfi->hold_frames = 1;
	}
}

/**
 * @ingroup unsol
 * @brief Clear hold on unsolicited frames.
 *
 * <h3 class="desc">Description</h3>
 * This function clears the hold on processing unsolicited FC
 * frames queued to the domain pending list.
 *
 * @param domain Pointer to domain object.
 *
 * @return Returns None.
 */

void
ocs_domain_accept_frames(ocs_domain_t *domain)
{
	ocs_t *ocs = domain->ocs;
	ocs_xport_fcfi_t *xport_fcfi;

	ocs_assert(domain->fcf_indicator < SLI4_MAX_FCFI);
	xport_fcfi = &ocs->xport->fcfi[domain->fcf_indicator];
	if (xport_fcfi->hold_frames == 1) {
		ocs_log_debug(domain->ocs, "hold frames cleared for FCFI %d\n",
			      domain->fcf_indicator);
	}
	xport_fcfi->hold_frames = 0;
	ocs_domain_process_pending(domain);
}


/**
 * @ingroup unsol
 * @brief Dispatch unsolicited FC frame.
 *
 * <h3 class="desc">Description</h3>
 * This function processes an unsolicited FC frame queued at the
 * domain level.
 *
 * @param arg Pointer to ocs object.
 * @param seq Header/payload sequence buffers.
 *
 * @return Returns 0 if frame processed and RX buffers cleaned
 * up appropriately, -1 if frame not handled.
 */

static __inline int32_t
ocs_domain_dispatch_frame(void *arg, ocs_hw_sequence_t *seq)
{
	ocs_domain_t *domain = (ocs_domain_t *)arg;
	ocs_t *ocs = domain->ocs;
	fc_header_t *hdr;
	uint32_t s_id;
	uint32_t d_id;
	ocs_node_t *node = NULL;
	ocs_sport_t *sport = NULL;

	ocs_assert(seq->header, -1);
	ocs_assert(seq->header->dma.virt, -1);
	ocs_assert(seq->payload->dma.virt, -1);

	hdr = seq->header->dma.virt;

	/* extract the s_id and d_id */
	s_id = fc_be24toh(hdr->s_id);
	d_id = fc_be24toh(hdr->d_id);

	sport = domain->sport;
	if (sport == NULL) {
		frame_printf(ocs, hdr, "phy sport for FC ID 0x%06x is NULL, dropping frame\n", d_id);
		return -1;
	}

	if (sport->fc_id != d_id) {
		/* Not a physical port IO lookup sport associated with the npiv port */
		sport = ocs_sport_find(domain, d_id); /* Look up without lock */
		if (sport == NULL) {
			if (hdr->type == FC_TYPE_FCP) {
				/* Drop frame */
				ocs_log_warn(ocs, "unsolicited FCP frame with invalid d_id x%x, dropping\n",
					     d_id);
				return -1;
			} else {
				/* p2p will use this case */
				sport = domain->sport;
			}
		}
	}

	/* Lookup the node given the remote s_id */
	node = ocs_node_find(sport, s_id);

	/* If not found, then create a new node */
	if (node == NULL) {
		/* If this is solicited data or control based on R_CTL and there is no node context,
		 * then we can drop the frame
		 */
		if ((hdr->r_ctl == FC_RCTL_FC4_DATA) && (
		    (hdr->info == FC_RCTL_INFO_SOL_DATA) || (hdr->info == FC_RCTL_INFO_SOL_CTRL))) {
			ocs_log_debug(ocs, "solicited data/ctrl frame without node, dropping\n");
			return -1;
		}
		node = ocs_node_alloc(sport, s_id, FALSE, FALSE);
		if (node == NULL) {
			ocs_log_err(ocs, "ocs_node_alloc() failed\n");
			return -1;
		}
		/* don't send PLOGI on ocs_d_init entry */
		ocs_node_init_device(node, FALSE);
	}

	if (node->hold_frames || !ocs_list_empty((&node->pend_frames))) {
		/* TODO: info log level
		frame_printf(ocs, hdr, "Holding frame\n");
		*/
		/* add frame to node's pending list */
		ocs_lock(&node->pend_frames_lock);
			ocs_list_add_tail(&node->pend_frames, seq);
		ocs_unlock(&node->pend_frames_lock);

		return 0;
	}

	/* now dispatch frame to the node frame handler */
	return ocs_node_dispatch_frame(node, seq);
}

/**
 * @ingroup unsol
 * @brief Dispatch a frame.
 *
 * <h3 class="desc">Description</h3>
 * A frame is dispatched from the \c node to the handler.
 *
 * @param arg Node that originated the frame.
 * @param seq Header/payload sequence buffers.
 *
 * @return Returns 0 if frame processed and RX buffers cleaned
 * up appropriately, -1 if frame not handled.
 */
static int32_t
ocs_node_dispatch_frame(void *arg, ocs_hw_sequence_t *seq)
{

	fc_header_t *hdr = seq->header->dma.virt;
	uint32_t port_id;
	ocs_node_t *node = (ocs_node_t *)arg;
	int32_t rc = -1;
	int32_t sit_set = 0;

	port_id = fc_be24toh(hdr->s_id);
	ocs_assert(port_id == node->rnode.fc_id, -1);

	if (fc_be24toh(hdr->f_ctl) & FC_FCTL_END_SEQUENCE) {
		/*if SIT is set */
		if (fc_be24toh(hdr->f_ctl) & FC_FCTL_SEQUENCE_INITIATIVE) {
			sit_set = 1;
		}
		switch (hdr->r_ctl) {
		case FC_RCTL_ELS:
			if (sit_set) {
				rc = ocs_node_recv_els_frame(node, seq);
			}
			break;

		case FC_RCTL_BLS:
			if (sit_set) {
				rc = ocs_node_recv_abts_frame(node, seq);
			}else {
				rc = ocs_node_recv_bls_no_sit(node, seq);
			}
			break;

		case FC_RCTL_FC4_DATA:
			switch(hdr->type) {
			case FC_TYPE_FCP:
				if (hdr->info == FC_RCTL_INFO_UNSOL_CMD) {
					if (node->fcp_enabled) {
						if (sit_set) {
							rc = ocs_dispatch_fcp_cmd(node, seq);
						}else {
							/* send the auto xfer ready command */
							rc = ocs_dispatch_fcp_cmd_auto_xfer_rdy(node, seq);
						}
					} else {
						rc = ocs_node_recv_fcp_cmd(node, seq);
					}
				} else if (hdr->info == FC_RCTL_INFO_SOL_DATA) {
					if (sit_set) {
						rc = ocs_dispatch_fcp_data(node, seq);
					}
				}
				break;
			case FC_TYPE_GS:
				if (sit_set) {
					rc = ocs_node_recv_ct_frame(node, seq);
				}
				break;
			default:
				break;
			}
			break;
		}
	} else {
		node_printf(node, "Dropping frame hdr = %08x %08x %08x %08x %08x %08x\n",
			    ocs_htobe32(((uint32_t *)hdr)[0]),
			    ocs_htobe32(((uint32_t *)hdr)[1]),
			    ocs_htobe32(((uint32_t *)hdr)[2]),
			    ocs_htobe32(((uint32_t *)hdr)[3]),
			    ocs_htobe32(((uint32_t *)hdr)[4]),
			    ocs_htobe32(((uint32_t *)hdr)[5]));
	}
	return rc;
}

/**
 * @ingroup unsol
 * @brief Dispatch unsolicited FCP frames (RQ Pair).
 *
 * <h3 class="desc">Description</h3>
 * Dispatch unsolicited FCP frames (called from the device node state machine).
 *
 * @param io Pointer to the IO context.
 * @param task_management_flags Task management flags from the FCP_CMND frame.
 * @param node Node that originated the frame.
 * @param lun 32-bit LUN from FCP_CMND frame.
 *
 * @return Returns None.
 */

static void
ocs_dispatch_unsolicited_tmf(ocs_io_t *io, uint8_t task_management_flags, ocs_node_t *node, uint64_t lun)
{
	uint32_t i;
	struct {
		uint32_t mask;
		ocs_scsi_tmf_cmd_e cmd;
	} tmflist[] = {
		{FCP_QUERY_TASK_SET,		OCS_SCSI_TMF_QUERY_TASK_SET},
		{FCP_ABORT_TASK_SET,		OCS_SCSI_TMF_ABORT_TASK_SET},
		{FCP_CLEAR_TASK_SET,		OCS_SCSI_TMF_CLEAR_TASK_SET},
		{FCP_QUERY_ASYNCHRONOUS_EVENT,	OCS_SCSI_TMF_QUERY_ASYNCHRONOUS_EVENT},
		{FCP_LOGICAL_UNIT_RESET,	OCS_SCSI_TMF_LOGICAL_UNIT_RESET},
		{FCP_TARGET_RESET,		OCS_SCSI_TMF_TARGET_RESET},
		{FCP_CLEAR_ACA,			OCS_SCSI_TMF_CLEAR_ACA}};

	io->exp_xfer_len = 0; /* BUG 32235 */

	for (i = 0; i < ARRAY_SIZE(tmflist); i ++) {
		if (tmflist[i].mask & task_management_flags) {
			io->tmf_cmd = tmflist[i].cmd;
			ocs_scsi_recv_tmf(io, lun, tmflist[i].cmd, NULL, 0);
			break;
		}
	}
	if (i == ARRAY_SIZE(tmflist)) {
		/* Not handled */
		node_printf(node, "TMF x%x rejected\n", task_management_flags);
		ocs_scsi_send_tmf_resp(io, OCS_SCSI_TMF_FUNCTION_REJECTED, NULL, ocs_fc_tmf_rejected_cb, NULL);
	}
}

static int32_t
ocs_validate_fcp_cmd(ocs_t *ocs, ocs_hw_sequence_t *seq)
{
	size_t		exp_payload_len = 0;
	fcp_cmnd_iu_t *cmnd = seq->payload->dma.virt;
	exp_payload_len = sizeof(fcp_cmnd_iu_t) - 16 + cmnd->additional_fcp_cdb_length;

	/*
	 * If we received less than FCP_CMND_IU bytes, assume that the frame is
	 * corrupted in some way and drop it. This was seen when jamming the FCTL
	 * fill bytes field.
	 */
	if (seq->payload->dma.len < exp_payload_len) {
		fc_header_t	*fchdr = seq->header->dma.virt;
		ocs_log_debug(ocs, "dropping ox_id %04x with payload length (%zd) less than expected (%zd)\n",
			      ocs_be16toh(fchdr->ox_id), seq->payload->dma.len,
			      exp_payload_len);
		return -1;
	}
	return 0;

}

static void
ocs_populate_io_fcp_cmd(ocs_io_t *io, fcp_cmnd_iu_t *cmnd, fc_header_t *fchdr, uint8_t sit)
{
	uint32_t	*fcp_dl;
	io->init_task_tag = ocs_be16toh(fchdr->ox_id);
	/* note, tgt_task_tag, hw_tag  set when HW io is allocated */
	fcp_dl = (uint32_t*)(&(cmnd->fcp_cdb_and_dl));
	fcp_dl += cmnd->additional_fcp_cdb_length;
	io->exp_xfer_len = ocs_be32toh(*fcp_dl);
	io->transferred = 0;

	/* The upper 7 bits of CS_CTL is the frame priority thru the SAN.
	 * Our assertion here is, the priority given to a frame containing
	 * the FCP cmd should be the priority given to ALL frames contained
	 * in that IO. Thus we need to save the incoming CS_CTL here.
	 */
	if (fc_be24toh(fchdr->f_ctl) & FC_FCTL_PRIORITY_ENABLE) {
		io->cs_ctl = fchdr->cs_ctl;
	} else {
		io->cs_ctl = 0;
	}
	io->seq_init = sit;
}

static uint32_t
ocs_get_flags_fcp_cmd(fcp_cmnd_iu_t *cmnd)
{
	uint32_t flags = 0;
	switch (cmnd->task_attribute) {
	case FCP_TASK_ATTR_SIMPLE:
		flags |= OCS_SCSI_CMD_SIMPLE;
		break;
	case FCP_TASK_ATTR_HEAD_OF_QUEUE:
		flags |= OCS_SCSI_CMD_HEAD_OF_QUEUE;
		break;
	case FCP_TASK_ATTR_ORDERED:
		flags |= OCS_SCSI_CMD_ORDERED;
		break;
	case FCP_TASK_ATTR_ACA:
		flags |= OCS_SCSI_CMD_ACA;
		break;
	case FCP_TASK_ATTR_UNTAGGED:
		flags |= OCS_SCSI_CMD_UNTAGGED;
		break;
	}
	if (cmnd->wrdata)
		flags |= OCS_SCSI_CMD_DIR_IN;
	if (cmnd->rddata)
		flags |= OCS_SCSI_CMD_DIR_OUT;

	return flags;
}

/**
 * @ingroup unsol
 * @brief Dispatch unsolicited FCP_CMND frame.
 *
 * <h3 class="desc">Description</h3>
 * Dispatch unsolicited FCP_CMND frame. RQ Pair mode - always
 * used for RQ Pair mode since first burst is not supported.
 *
 * @param node Node that originated the frame.
 * @param seq Header/payload sequence buffers.
 *
 * @return Returns 0 if frame processed and RX buffers cleaned
 * up appropriately, -1 if frame not handled and RX buffers need
 * to be returned.
 */
static int32_t
ocs_dispatch_fcp_cmd(ocs_node_t *node, ocs_hw_sequence_t *seq)
{
	ocs_t *ocs = node->ocs;
	fc_header_t	*fchdr = seq->header->dma.virt;
	fcp_cmnd_iu_t	*cmnd = NULL;
	ocs_io_t	*io = NULL;
	fc_vm_header_t 	*vhdr;
	uint8_t 	df_ctl;
	uint64_t	lun = UINT64_MAX;
	int32_t		rc = 0;

	ocs_assert(seq->payload, -1);
	cmnd = seq->payload->dma.virt;

	/* perform FCP_CMND validation check(s) */
	if (ocs_validate_fcp_cmd(ocs, seq)) {
		return -1;
	}

	lun = CAM_EXTLUN_BYTE_SWIZZLE(be64dec(cmnd->fcp_lun));
	if (lun == UINT64_MAX) {
		return -1;
	}

	io = ocs_scsi_io_alloc(node, OCS_SCSI_IO_ROLE_RESPONDER);
	if (io == NULL) {
		uint32_t send_frame_capable;

		/* If we have SEND_FRAME capability, then use it to send task set full or busy */
		rc = ocs_hw_get(&ocs->hw, OCS_HW_SEND_FRAME_CAPABLE, &send_frame_capable);
		if ((rc == 0) && send_frame_capable) {
			rc = ocs_sframe_send_task_set_full_or_busy(node, seq);
			if (rc) {
				ocs_log_test(ocs, "ocs_sframe_send_task_set_full_or_busy failed: %d\n", rc);
			}
			return rc;
		}

		ocs_log_err(ocs, "IO allocation failed ox_id %04x\n", ocs_be16toh(fchdr->ox_id));
		return -1;
	}
	io->hw_priv = seq->hw_priv;

	/* Check if the CMD has vmheader. */
	io->app_id = 0;
	df_ctl = fchdr->df_ctl;
	if (df_ctl & FC_DFCTL_DEVICE_HDR_16_MASK) {
		uint32_t vmhdr_offset = 0;
		/* Presence of VMID. Get the vm header offset. */
		if (df_ctl & FC_DFCTL_ESP_HDR_MASK) {
			vmhdr_offset += FC_DFCTL_ESP_HDR_SIZE;
			ocs_log_err(ocs, "ESP Header present. Fix ESP Size.\n");
		}

		if (df_ctl & FC_DFCTL_NETWORK_HDR_MASK) {
			vmhdr_offset += FC_DFCTL_NETWORK_HDR_SIZE;
		}
		vhdr = (fc_vm_header_t *) ((char *)fchdr + sizeof(fc_header_t) + vmhdr_offset);
		io->app_id = ocs_be32toh(vhdr->src_vmid);
	}

	/* RQ pair, if we got here, SIT=1 */
	ocs_populate_io_fcp_cmd(io, cmnd, fchdr, TRUE);

	if (cmnd->task_management_flags) {
		ocs_dispatch_unsolicited_tmf(io, cmnd->task_management_flags, node, lun);
	} else {
		uint32_t flags = ocs_get_flags_fcp_cmd(cmnd);

		/* can return failure for things like task set full and UAs,
		 * no need to treat as a dropped frame if rc != 0
		 */
		ocs_scsi_recv_cmd(io, lun, cmnd->fcp_cdb,
				  sizeof(cmnd->fcp_cdb) +
				  (cmnd->additional_fcp_cdb_length * sizeof(uint32_t)),
				  flags);
	}

	/* successfully processed, now return RX buffer to the chip */
	ocs_hw_sequence_free(&ocs->hw, seq);
	return 0;
}

/**
 * @ingroup unsol
 * @brief Dispatch unsolicited FCP_CMND frame (auto xfer rdy).
 *
 * <h3 class="desc">Description</h3>
 * Dispatch unsolicited FCP_CMND frame that is assisted with auto xfer ready.
 *
 * @param node Node that originated the frame.
 * @param seq Header/payload sequence buffers.
 *
 * @return Returns 0 if frame processed and RX buffers cleaned
 * up appropriately, -1 if frame not handled and RX buffers need
 * to be returned.
 */
static int32_t
ocs_dispatch_fcp_cmd_auto_xfer_rdy(ocs_node_t *node, ocs_hw_sequence_t *seq)
{
	ocs_t *ocs = node->ocs;
	fc_header_t	*fchdr = seq->header->dma.virt;
	fcp_cmnd_iu_t	*cmnd = NULL;
	ocs_io_t	*io = NULL;
	uint64_t	lun = UINT64_MAX;
	int32_t		rc = 0;

	ocs_assert(seq->payload, -1);
	cmnd = seq->payload->dma.virt;

	/* perform FCP_CMND validation check(s) */
	if (ocs_validate_fcp_cmd(ocs, seq)) {
		return -1;
	}

	/* make sure first burst or auto xfer_rdy is enabled */
	if (!seq->auto_xrdy) {
		node_printf(node, "IO is not Auto Xfr Rdy assisted, dropping FCP_CMND\n");
		return -1;
	}

	lun = CAM_EXTLUN_BYTE_SWIZZLE(be64dec(cmnd->fcp_lun));

	/* TODO should there be a check here for an error? Why do any of the
	 * below if the LUN decode failed? */
	io = ocs_scsi_io_alloc(node, OCS_SCSI_IO_ROLE_RESPONDER);
	if (io == NULL) {
		uint32_t send_frame_capable;

		/* If we have SEND_FRAME capability, then use it to send task set full or busy */
		rc = ocs_hw_get(&ocs->hw, OCS_HW_SEND_FRAME_CAPABLE, &send_frame_capable);
		if ((rc == 0) && send_frame_capable) {
			rc = ocs_sframe_send_task_set_full_or_busy(node, seq);
			if (rc) {
				ocs_log_test(ocs, "ocs_sframe_send_task_set_full_or_busy failed: %d\n", rc);
			}
			return rc;
		}

		ocs_log_err(ocs, "IO allocation failed ox_id %04x\n", ocs_be16toh(fchdr->ox_id));
		return -1;
	}
	io->hw_priv = seq->hw_priv;

	/* RQ pair, if we got here, SIT=0 */
	ocs_populate_io_fcp_cmd(io, cmnd, fchdr, FALSE);

	if (cmnd->task_management_flags) {
		/* first burst command better not be a TMF */
		ocs_log_err(ocs, "TMF flags set 0x%x\n", cmnd->task_management_flags);
		ocs_scsi_io_free(io);
		return -1;
	} else {
		uint32_t flags = ocs_get_flags_fcp_cmd(cmnd);

		/* activate HW IO */
		ocs_hw_io_activate_port_owned(&ocs->hw, seq->hio);
		io->hio = seq->hio;
		seq->hio->ul_io = io;
		io->tgt_task_tag = seq->hio->indicator;

		/* Note: Data buffers are received in another call */
		ocs_scsi_recv_cmd_first_burst(io, lun, cmnd->fcp_cdb,
					      sizeof(cmnd->fcp_cdb) +
					      (cmnd->additional_fcp_cdb_length * sizeof(uint32_t)),
					      flags, NULL, 0);
	}

	/* FCP_CMND processed, return RX buffer to the chip */
	ocs_hw_sequence_free(&ocs->hw, seq);
	return 0;
}

/**
 * @ingroup unsol
 * @brief Dispatch FCP data frames for auto xfer ready.
 *
 * <h3 class="desc">Description</h3>
 * Dispatch unsolicited FCP data frames (auto xfer ready)
 * containing sequence initiative transferred (SIT=1).
 *
 * @param node Node that originated the frame.
 * @param seq Header/payload sequence buffers.
 *
 * @return Returns 0 if frame processed and RX buffers cleaned
 * up appropriately, -1 if frame not handled.
 */

static int32_t
ocs_dispatch_fcp_data(ocs_node_t *node, ocs_hw_sequence_t *seq)
{
	ocs_t *ocs = node->ocs;
	ocs_hw_t *hw = &ocs->hw;
	ocs_hw_io_t *hio = seq->hio;
	ocs_io_t	*io;
	ocs_dma_t fburst[1];

	ocs_assert(seq->payload, -1);
	ocs_assert(hio, -1);

	io = hio->ul_io;
	if (io == NULL) {
		ocs_log_err(ocs, "data received for NULL io, xri=0x%x\n",
			    hio->indicator);
		return -1;
	}

	/*
	 * We only support data completions for auto xfer ready. Make sure
	 * this is a port owned XRI.
	 */
	if (!ocs_hw_is_io_port_owned(hw, seq->hio)) {
		ocs_log_err(ocs, "data received for host owned XRI, xri=0x%x\n",
			    hio->indicator);
		return -1;
	}

	/* For error statuses, pass the error to the target back end */
	if (seq->status != OCS_HW_UNSOL_SUCCESS) {
		ocs_log_err(ocs, "data with status 0x%x received, xri=0x%x\n",
			    seq->status, hio->indicator);

		/*
		 * In this case, there is an existing, in-use HW IO that
		 * first may need to be aborted. Then, the backend will be
		 * notified of the error while waiting for the data.
		 */
		ocs_port_owned_abort(ocs, seq->hio);

		/*
		 * HW IO has already been allocated and is waiting for data.
		 * Need to tell backend that an error has occurred.
		 */
		ocs_scsi_recv_cmd_first_burst(io, 0, NULL, 0, OCS_SCSI_FIRST_BURST_ERR, NULL, 0);
		return -1;
	}

	/* sequence initiative has been transferred */
	io->seq_init = 1;

	/* convert the array of pointers to the correct type, to send to backend */
	fburst[0] = seq->payload->dma;

	/* the amount of first burst data was saved as "acculated sequence length" */
	io->transferred = seq->payload->dma.len;

	if (ocs_scsi_recv_cmd_first_burst(io, 0, NULL, 0, 0,
					  fburst, io->transferred)) {
		ocs_log_err(ocs, "error passing first burst, xri=0x%x, oxid=0x%x\n",
			    hio->indicator, io->init_task_tag);
	}

	/* Free the header and all the accumulated payload buffers */
	ocs_hw_sequence_free(&ocs->hw, seq);
	return 0;
}


/**
 * @ingroup unsol
 * @brief Handle the callback for the TMF FUNCTION_REJECTED response.
 *
 * <h3 class="desc">Description</h3>
 * Handle the callback of a send TMF FUNCTION_REJECTED response request.
 *
 * @param io Pointer to the IO context.
 * @param scsi_status Status of the response.
 * @param flags Callback flags.
 * @param arg Callback argument.
 *
 * @return Returns 0 on success, or a negative error value on failure.
 */

static int32_t
ocs_fc_tmf_rejected_cb(ocs_io_t *io, ocs_scsi_io_status_e scsi_status, uint32_t flags, void *arg)
{
	ocs_scsi_io_free(io);
	return 0;
}

/**
 * @brief Return next FC frame on node->pend_frames list
 *
 * The next FC frame on the node->pend_frames list is returned, or NULL
 * if the list is empty.
 *
 * @param pend_list Pending list to be purged.
 * @param list_lock Lock that protects pending list.
 *
 * @return Returns pointer to the next FC frame, or NULL if the pending frame list
 * is empty.
 */
static ocs_hw_sequence_t *
ocs_frame_next(ocs_list_t *pend_list, ocs_lock_t *list_lock)
{
	ocs_hw_sequence_t *frame = NULL;

	ocs_lock(list_lock);
		frame = ocs_list_remove_head(pend_list);
	ocs_unlock(list_lock);
	return frame;
}

/**
 * @brief Process send fcp response frame callback
 *
 * The function is called when the send FCP response posting has completed. Regardless
 * of the outcome, the sequence is freed.
 *
 * @param arg Pointer to originator frame sequence.
 * @param cqe Pointer to completion queue entry.
 * @param status Status of operation.
 *
 * @return None.
 */
static void
ocs_sframe_common_send_cb(void *arg, uint8_t *cqe, int32_t status)
{
	ocs_hw_send_frame_context_t *ctx = arg;
	ocs_hw_t *hw = ctx->hw;

	/* Free WQ completion callback */
	ocs_hw_reqtag_free(hw, ctx->wqcb);

	/* Free sequence */
	ocs_hw_sequence_free(hw, ctx->seq);
}

/**
 * @brief Send a frame, common code
 *
 * A frame is sent using SEND_FRAME, the R_CTL/F_CTL/TYPE may be specified, the payload is
 * sent as a single frame.
 *
 * Memory resources are allocated from RQ buffers contained in the passed in sequence data.
 *
 * @param node Pointer to node object.
 * @param seq Pointer to sequence object.
 * @param r_ctl R_CTL value to place in FC header.
 * @param info INFO value to place in FC header.
 * @param f_ctl F_CTL value to place in FC header.
 * @param type TYPE value to place in FC header.
 * @param payload Pointer to payload data
 * @param payload_len Length of payload in bytes.
 *
 * @return Returns 0 on success, or a negative error code value on failure.
 */
static int32_t
ocs_sframe_common_send(ocs_node_t *node, ocs_hw_sequence_t *seq, uint8_t r_ctl, uint8_t info, uint32_t f_ctl,
		       uint8_t type, void *payload, uint32_t payload_len)
{
	ocs_t *ocs = node->ocs;
	ocs_hw_t *hw = &ocs->hw;
	ocs_hw_rtn_e rc = 0;
	fc_header_t *behdr = seq->header->dma.virt;
	fc_header_le_t hdr;
	uint32_t s_id = fc_be24toh(behdr->s_id);
	uint32_t d_id = fc_be24toh(behdr->d_id);
	uint16_t ox_id = ocs_be16toh(behdr->ox_id);
	uint16_t rx_id = ocs_be16toh(behdr->rx_id);
	ocs_hw_send_frame_context_t *ctx;

	uint32_t heap_size = seq->payload->dma.size;
	uintptr_t heap_phys_base = seq->payload->dma.phys;
	uint8_t *heap_virt_base = seq->payload->dma.virt;
	uint32_t heap_offset = 0;

	/* Build the FC header reusing the RQ header DMA buffer */
	ocs_memset(&hdr, 0, sizeof(hdr));
	hdr.d_id = s_id;			/* send it back to whomever sent it to us */
	hdr.r_ctl = r_ctl;
	hdr.info = info;
	hdr.s_id = d_id;
	hdr.cs_ctl = 0;
	hdr.f_ctl = f_ctl;
	hdr.type = type;
	hdr.seq_cnt = 0;
	hdr.df_ctl = 0;

	/*
	 * send_frame_seq_id is an atomic, we just let it increment, while storing only
	 * the low 8 bits to hdr->seq_id
	 */
	hdr.seq_id = (uint8_t) ocs_atomic_add_return(&hw->send_frame_seq_id, 1);

	hdr.rx_id = rx_id;
	hdr.ox_id = ox_id;
	hdr.parameter = 0;

	/* Allocate and fill in the send frame request context */
	ctx = (void*)(heap_virt_base + heap_offset);
	heap_offset += sizeof(*ctx);
	ocs_assert(heap_offset < heap_size, -1);
	ocs_memset(ctx, 0, sizeof(*ctx));

	/* Save sequence */
	ctx->seq = seq;

	/* Allocate a response payload DMA buffer from the heap */
	ctx->payload.phys = heap_phys_base + heap_offset;
	ctx->payload.virt = heap_virt_base + heap_offset;
	ctx->payload.size = payload_len;
	ctx->payload.len = payload_len;
	heap_offset += payload_len;
	ocs_assert(heap_offset <= heap_size, -1);

	/* Copy the payload in */
	ocs_memcpy(ctx->payload.virt, payload, payload_len);

	/* Send */
	rc = ocs_hw_send_frame(&ocs->hw, (void*)&hdr, FC_SOFI3, FC_EOFT, &ctx->payload, ctx,
				ocs_sframe_common_send_cb, ctx);
	if (rc) {
		ocs_log_test(ocs, "ocs_hw_send_frame failed: %d\n", rc);
	}

	return rc ? -1 : 0;
}

/**
 * @brief Send FCP response using SEND_FRAME
 *
 * The FCP response is send using the SEND_FRAME function.
 *
 * @param node Pointer to node object.
 * @param seq Pointer to inbound sequence.
 * @param rsp Pointer to response data.
 * @param rsp_len Length of response data, in bytes.
 *
 * @return Returns 0 on success, or a negative error code value on failure.
 */
static int32_t
ocs_sframe_send_fcp_rsp(ocs_node_t *node, ocs_hw_sequence_t *seq, void *rsp, uint32_t rsp_len)
{
	return ocs_sframe_common_send(node, seq,
				      FC_RCTL_FC4_DATA,
				      FC_RCTL_INFO_CMD_STATUS,
				      FC_FCTL_EXCHANGE_RESPONDER |
					      FC_FCTL_LAST_SEQUENCE |
					      FC_FCTL_END_SEQUENCE |
					      FC_FCTL_SEQUENCE_INITIATIVE,
				      FC_TYPE_FCP,
				      rsp, rsp_len);
}

/**
 * @brief Send task set full response
 *
 * Return a task set full or busy response using send frame.
 *
 * @param node Pointer to node object.
 * @param seq Pointer to originator frame sequence.
 *
 * @return Returns 0 on success, or a negative error code value on failure.
 */
static int32_t
ocs_sframe_send_task_set_full_or_busy(ocs_node_t *node, ocs_hw_sequence_t *seq)
{
	fcp_rsp_iu_t fcprsp;
	fcp_cmnd_iu_t *fcpcmd = seq->payload->dma.virt;
	uint32_t *fcp_dl_ptr;
	uint32_t fcp_dl;
	int32_t rc = 0;

	/* extract FCP_DL from FCP command*/
	fcp_dl_ptr = (uint32_t*)(&(fcpcmd->fcp_cdb_and_dl));
	fcp_dl_ptr += fcpcmd->additional_fcp_cdb_length;
	fcp_dl = ocs_be32toh(*fcp_dl_ptr);

	/* construct task set full or busy response */
	ocs_memset(&fcprsp, 0, sizeof(fcprsp));
	ocs_lock(&node->active_ios_lock);
		fcprsp.scsi_status = ocs_list_empty(&node->active_ios) ? SCSI_STATUS_BUSY : SCSI_STATUS_TASK_SET_FULL;
	ocs_unlock(&node->active_ios_lock);
	*((uint32_t*)&fcprsp.fcp_resid) = fcp_dl;

	/* send it using send_frame */
	rc = ocs_sframe_send_fcp_rsp(node, seq, &fcprsp, sizeof(fcprsp) - sizeof(fcprsp.data));
	if (rc) {
		ocs_log_test(node->ocs, "ocs_sframe_send_fcp_rsp failed: %d\n", rc);
	}
	return rc;
}

/**
 * @brief Send BA_ACC using sent frame
 *
 * A BA_ACC is sent using SEND_FRAME
 *
 * @param node Pointer to node object.
 * @param seq Pointer to originator frame sequence.
 *
 * @return Returns 0 on success, or a negative error code value on failure.
 */
int32_t
ocs_sframe_send_bls_acc(ocs_node_t *node,  ocs_hw_sequence_t *seq)
{
	fc_header_t *behdr = seq->header->dma.virt;
	uint16_t ox_id = ocs_be16toh(behdr->ox_id);
	uint16_t rx_id = ocs_be16toh(behdr->rx_id);
	fc_ba_acc_payload_t acc = {0};

	acc.ox_id = ocs_htobe16(ox_id);
	acc.rx_id = ocs_htobe16(rx_id);
	acc.low_seq_cnt = UINT16_MAX;
	acc.high_seq_cnt = UINT16_MAX;

	return ocs_sframe_common_send(node, seq,
				      FC_RCTL_BLS,
				      FC_RCTL_INFO_UNSOL_DATA,
				      FC_FCTL_EXCHANGE_RESPONDER |
					      FC_FCTL_LAST_SEQUENCE |
					      FC_FCTL_END_SEQUENCE,
				      FC_TYPE_BASIC_LINK,
				      &acc, sizeof(acc));
}
