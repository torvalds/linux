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
 * Functions to build and send ELS/CT/BLS commands and responses.
 */

/*!
@defgroup els_api ELS/BLS/CT Command and Response Functions
*/

#include "ocs.h"
#include "ocs_els.h"
#include "ocs_scsi.h"
#include "ocs_device.h"

#define ELS_IOFMT "[i:%04x t:%04x h:%04x]"
#define ELS_IOFMT_ARGS(els) els->init_task_tag, els->tgt_task_tag, els->hw_tag

#define node_els_trace()  \
	do { \
		if (OCS_LOG_ENABLE_ELS_TRACE(ocs)) \
			ocs_log_info(ocs, "[%s] %-20s\n", node->display_name, __func__); \
	} while (0)

#define els_io_printf(els, fmt, ...) \
	ocs_log_debug(els->node->ocs, "[%s]" ELS_IOFMT " %-8s " fmt, els->node->display_name, ELS_IOFMT_ARGS(els), els->display_name, ##__VA_ARGS__);

static int32_t ocs_els_send(ocs_io_t *els, uint32_t reqlen, uint32_t timeout_sec, ocs_hw_srrs_cb_t cb);
static int32_t ocs_els_send_rsp(ocs_io_t *els, uint32_t rsplen);
static int32_t ocs_els_acc_cb(ocs_hw_io_t *hio, ocs_remote_node_t *rnode, uint32_t length, int32_t status, uint32_t ext_status, void *arg);
static ocs_io_t *ocs_bls_send_acc(ocs_io_t *io, uint32_t s_id, uint16_t ox_id, uint16_t rx_id);
static int32_t ocs_bls_send_acc_cb(ocs_hw_io_t *hio, ocs_remote_node_t *rnode, uint32_t length,
	int32_t status, uint32_t ext_status, void *app);
static void ocs_io_transition(ocs_io_t *els, ocs_sm_function_t state, void *data);
static ocs_io_t *ocs_els_abort_io(ocs_io_t *els, int send_abts);
static void _ocs_els_io_free(void *arg);
static void ocs_els_delay_timer_cb(void *arg);


/**
 * @ingroup els_api
 * @brief ELS state machine transition wrapper.
 *
 * <h3 class="desc">Description</h3>
 * This function is the transition wrapper for the ELS state machine. It grabs
 * the node lock prior to making the transition to protect
 * against multiple threads accessing a particular ELS. For example,
 * one thread transitioning from __els_init to
 * __ocs_els_wait_resp and another thread (tasklet) handling the
 * completion of that ELS request.
 *
 * @param els Pointer to the IO context.
 * @param state State to transition to.
 * @param data Data to pass in with the transition.
 *
 * @return None.
 */
static void
ocs_io_transition(ocs_io_t *els, ocs_sm_function_t state, void *data)
{
	/* protect ELS events with node lock */
	ocs_node_t *node = els->node;
	ocs_node_lock(node);
		ocs_sm_transition(&els->els_sm, state, data);
	ocs_node_unlock(node);
}

/**
 * @ingroup els_api
 * @brief ELS state machine post event wrapper.
 *
 * <h3 class="desc">Description</h3>
 * Post an event wrapper for the ELS state machine. This function grabs
 * the node lock prior to posting the event.
 *
 * @param els Pointer to the IO context.
 * @param evt Event to process.
 * @param data Data to pass in with the transition.
 *
 * @return None.
 */
void
ocs_els_post_event(ocs_io_t *els, ocs_sm_event_t evt, void *data)
{
	/* protect ELS events with node lock */
	ocs_node_t *node = els->node;
	ocs_node_lock(node);
		els->els_evtdepth ++;
		ocs_sm_post_event(&els->els_sm, evt, data);
		els->els_evtdepth --;
	ocs_node_unlock(node);
	if (els->els_evtdepth == 0 && els->els_req_free) {
		ocs_els_io_free(els);
	}
}

/**
 * @ingroup els_api
 * @brief Allocate an IO structure for an ELS IO context.
 *
 * <h3 class="desc">Description</h3>
 * Allocate an IO for an ELS context.  Uses OCS_ELS_RSP_LEN as response size.
 *
 * @param node node to associate ELS IO with
 * @param reqlen Length of ELS request
 * @param role Role of ELS (originator/responder)
 *
 * @return pointer to IO structure allocated
 */

ocs_io_t *
ocs_els_io_alloc(ocs_node_t *node, uint32_t reqlen, ocs_els_role_e role)
{
	return ocs_els_io_alloc_size(node, reqlen, OCS_ELS_RSP_LEN, role);
}

/**
 * @ingroup els_api
 * @brief Allocate an IO structure for an ELS IO context.
 *
 * <h3 class="desc">Description</h3>
 * Allocate an IO for an ELS context, allowing the caller to specify the size of the response.
 *
 * @param node node to associate ELS IO with
 * @param reqlen Length of ELS request
 * @param rsplen Length of ELS response
 * @param role Role of ELS (originator/responder)
 *
 * @return pointer to IO structure allocated
 */

ocs_io_t *
ocs_els_io_alloc_size(ocs_node_t *node, uint32_t reqlen, uint32_t rsplen, ocs_els_role_e role)
{

	ocs_t *ocs;
	ocs_xport_t *xport;
	ocs_io_t *els;
	ocs_assert(node, NULL);
	ocs_assert(node->ocs, NULL);
	ocs = node->ocs;
	ocs_assert(ocs->xport, NULL);
	xport = ocs->xport;

	ocs_lock(&node->active_ios_lock);
		if (!node->io_alloc_enabled) {
			ocs_log_debug(ocs, "called with io_alloc_enabled = FALSE\n");
			ocs_unlock(&node->active_ios_lock);
			return NULL;
		}

		els = ocs_io_alloc(ocs);
		if (els == NULL) {
			ocs_atomic_add_return(&xport->io_alloc_failed_count, 1);
			ocs_unlock(&node->active_ios_lock);
			return NULL;
		}

		/* initialize refcount */
		ocs_ref_init(&els->ref, _ocs_els_io_free, els);

		switch (role) {
		case OCS_ELS_ROLE_ORIGINATOR:
			els->cmd_ini = TRUE;
			els->cmd_tgt = FALSE;
			break;
		case OCS_ELS_ROLE_RESPONDER:
			els->cmd_ini = FALSE;
			els->cmd_tgt = TRUE;
			break;
		}

		/* IO should not have an associated HW IO yet.  Assigned below. */
		if (els->hio != NULL) {
			ocs_log_err(ocs, "assertion failed.  HIO is not null\n");
			ocs_io_free(ocs, els);
			ocs_unlock(&node->active_ios_lock);
			return NULL;
		}

		/* populate generic io fields */
		els->ocs = ocs;
		els->node = node;

		/* set type and ELS-specific fields */
		els->io_type = OCS_IO_TYPE_ELS;
		els->display_name = "pending";

		if (reqlen > OCS_ELS_REQ_LEN) {
			ocs_log_err(ocs, "ELS command request len greater than allocated\n");
			ocs_io_free(ocs, els);
			ocs_unlock(&node->active_ios_lock);
			return NULL;
		}

		if (rsplen > OCS_ELS_GID_PT_RSP_LEN) {
			ocs_log_err(ocs, "ELS command response len: %d "
				"greater than allocated\n", rsplen);
			ocs_io_free(ocs, els);
			ocs_unlock(&node->active_ios_lock);
			return NULL;
		}

		els->els_req.size = reqlen;
		els->els_rsp.size = rsplen;

		if (els != NULL) {
			ocs_memset(&els->els_sm, 0, sizeof(els->els_sm));
			els->els_sm.app = els;

			/* initialize fields */
			els->els_retries_remaining = OCS_FC_ELS_DEFAULT_RETRIES;
			els->els_evtdepth = 0;
			els->els_pend = 0;
			els->els_active = 0;

			/* add els structure to ELS IO list */
			ocs_list_add_tail(&node->els_io_pend_list, els);
			els->els_pend = 1;
		}
	ocs_unlock(&node->active_ios_lock);
	return els;
}

/**
 * @ingroup els_api
 * @brief Free IO structure for an ELS IO context.
 *
 * <h3 class="desc">Description</h3> Free IO for an ELS
 * IO context
 *
 * @param els ELS IO structure for which IO is allocated
 *
 * @return None
 */

void
ocs_els_io_free(ocs_io_t *els)
{
	ocs_ref_put(&els->ref);
}

/**
 * @ingroup els_api
 * @brief Free IO structure for an ELS IO context.
 *
 * <h3 class="desc">Description</h3> Free IO for an ELS
 * IO context
 *
 * @param arg ELS IO structure for which IO is allocated
 *
 * @return None
 */

static void
_ocs_els_io_free(void *arg)
{
	ocs_io_t *els = (ocs_io_t *)arg;
	ocs_t *ocs;
	ocs_node_t *node;
	int send_empty_event = FALSE;

	ocs_assert(els);
	ocs_assert(els->node);
	ocs_assert(els->node->ocs);
	ocs = els->node->ocs;

	node = els->node;
	ocs = node->ocs;

	ocs_lock(&node->active_ios_lock);
		if (els->els_active) {
			/* if active, remove from active list and check empty */
			ocs_list_remove(&node->els_io_active_list, els);
			/* Send list empty event if the IO allocator is disabled, and the list is empty
			 * If node->io_alloc_enabled was not checked, the event would be posted continually
			 */
			send_empty_event = (!node->io_alloc_enabled) && ocs_list_empty(&node->els_io_active_list);
			els->els_active = 0;
		} else if (els->els_pend) {
			/* if pending, remove from pending list; node shutdown isn't
			 * gated off the pending list (only the active list), so no
			 * need to check if pending list is empty
			 */
			ocs_list_remove(&node->els_io_pend_list, els);
			els->els_pend = 0;
		} else {
			ocs_log_err(ocs, "assertion failed: niether els->els_pend nor els->active set\n");
			ocs_unlock(&node->active_ios_lock);
			return;
		}

	ocs_unlock(&node->active_ios_lock);

	ocs_io_free(ocs, els);

	if (send_empty_event) {
		ocs_node_post_event(node, OCS_EVT_ALL_CHILD_NODES_FREE, NULL);
	}

	ocs_scsi_check_pending(ocs);
}

/**
 * @ingroup els_api
 * @brief Make ELS IO active
 *
 * @param els Pointer to the IO context to make active.
 *
 * @return Returns 0 on success; or a negative error code value on failure.
 */

static void
ocs_els_make_active(ocs_io_t *els)
{
	ocs_node_t *node = els->node;

	/* move ELS from pending list to active list */
	ocs_lock(&node->active_ios_lock);
		if (els->els_pend) {
			if (els->els_active) {
				ocs_log_err(node->ocs, "assertion failed: both els->els_pend and els->active set\n");
				ocs_unlock(&node->active_ios_lock);
				return;
			} else {

				/* remove from pending list */
				ocs_list_remove(&node->els_io_pend_list, els);
				els->els_pend = 0;

				/* add els structure to ELS IO list */
				ocs_list_add_tail(&node->els_io_active_list, els);
				els->els_active = 1;
			}
		} else {
			/* must be retrying; make sure it's already active */
			if (!els->els_active) {
				ocs_log_err(node->ocs, "assertion failed: niether els->els_pend nor els->active set\n");
			}
		}
	ocs_unlock(&node->active_ios_lock);
}

/**
 * @ingroup els_api
 * @brief Send the ELS command.
 *
 * <h3 class="desc">Description</h3>
 * The command, given by the \c els IO context, is sent to the node that the IO was
 * configured with, using ocs_hw_srrs_send(). Upon completion,
 * the \c cb callback is invoked,
 * with the application-specific argument set to the \c els IO context.
 *
 * @param els Pointer to the IO context.
 * @param reqlen Byte count in the payload to send.
 * @param timeout_sec Command timeout, in seconds (0 -> 2*R_A_TOV).
 * @param cb Completion callback.
 *
 * @return Returns 0 on success; or a negative error code value on failure.
 */

static int32_t
ocs_els_send(ocs_io_t *els, uint32_t reqlen, uint32_t timeout_sec, ocs_hw_srrs_cb_t cb)
{
	ocs_node_t *node = els->node;

	/* update ELS request counter */
	node->els_req_cnt++;

	/* move ELS from pending list to active list */
	ocs_els_make_active(els);

	els->wire_len = reqlen;
	return ocs_scsi_io_dispatch(els, cb);
}

/**
 * @ingroup els_api
 * @brief Send the ELS response.
 *
 * <h3 class="desc">Description</h3>
 * The ELS response, given by the \c els IO context, is sent to the node
 * that the IO was configured with, using ocs_hw_srrs_send().
 *
 * @param els Pointer to the IO context.
 * @param rsplen Byte count in the payload to send.
 *
 * @return Returns 0 on success; or a negative error value on failure.
 */

static int32_t
ocs_els_send_rsp(ocs_io_t *els, uint32_t rsplen)
{
	ocs_node_t *node = els->node;

	/* increment ELS completion counter */
	node->els_cmpl_cnt++;

	/* move ELS from pending list to active list */
	ocs_els_make_active(els);

	els->wire_len = rsplen;
	return ocs_scsi_io_dispatch(els, ocs_els_acc_cb);
}

/**
 * @ingroup els_api
 * @brief Handle ELS IO request completions.
 *
 * <h3 class="desc">Description</h3>
 * This callback is used for several ELS send operations.
 *
 * @param hio Pointer to the HW IO context that completed.
 * @param rnode Pointer to the remote node.
 * @param length Length of the returned payload data.
 * @param status Status of the completion.
 * @param ext_status Extended status of the completion.
 * @param arg Application-specific argument (generally a pointer to the ELS IO context).
 *
 * @return Returns 0 on success; or a negative error value on failure.
 */

static int32_t
ocs_els_req_cb(ocs_hw_io_t *hio, ocs_remote_node_t *rnode, uint32_t length, int32_t status, uint32_t ext_status, void *arg)
{
	ocs_io_t *els;
	ocs_node_t *node;
	ocs_t *ocs;
	ocs_node_cb_t cbdata;
	ocs_io_t *io;

	ocs_assert(arg, -1);
	io = arg;
	els = io;
	ocs_assert(els, -1);
	ocs_assert(els->node, -1);
	node = els->node;
	ocs_assert(node->ocs, -1);
	ocs = node->ocs;

	ocs_assert(io->hio, -1);
	ocs_assert(hio == io->hio, -1);

	if (status != 0) {
		els_io_printf(els, "status x%x ext x%x\n", status, ext_status);
	}

	/* set the response len element of els->rsp */
	els->els_rsp.len = length;

	cbdata.status = status;
	cbdata.ext_status = ext_status;
	cbdata.header = NULL;
	cbdata.els = els;

	/* FW returns the number of bytes received on the link in
	 * the WCQE, not the amount placed in the buffer; use this info to
	 * check if there was an overrun.
	 */
	if (length > els->els_rsp.size) {
		ocs_log_warn(ocs, "ELS response returned len=%d > buflen=%zu\n",
				length, els->els_rsp.size);
		ocs_els_post_event(els, OCS_EVT_SRRS_ELS_REQ_FAIL, &cbdata);
		return 0;
	}

	/* Post event to ELS IO object */
	switch (status) {
	case SLI4_FC_WCQE_STATUS_SUCCESS:
		ocs_els_post_event(els, OCS_EVT_SRRS_ELS_REQ_OK, &cbdata);
		break;

	case SLI4_FC_WCQE_STATUS_LS_RJT:
		ocs_els_post_event(els, OCS_EVT_SRRS_ELS_REQ_RJT, &cbdata);
		break;


	case SLI4_FC_WCQE_STATUS_LOCAL_REJECT:
		switch (ext_status) {
		case SLI4_FC_LOCAL_REJECT_SEQUENCE_TIMEOUT:
			ocs_els_post_event(els, OCS_EVT_ELS_REQ_TIMEOUT, &cbdata);
			break;
		case SLI4_FC_LOCAL_REJECT_ABORT_REQUESTED:
			ocs_els_post_event(els, OCS_EVT_ELS_REQ_ABORTED, &cbdata);
			break;
		default:
			ocs_els_post_event(els, OCS_EVT_SRRS_ELS_REQ_FAIL, &cbdata);
			break;
		}
		break;
	default:
		ocs_log_warn(ocs, "els req complete: failed status x%x, ext_status, x%x\n", status, ext_status);
		ocs_els_post_event(els, OCS_EVT_SRRS_ELS_REQ_FAIL, &cbdata);
		break;
	}

	return 0;
}

/**
 * @ingroup els_api
 * @brief Handle ELS IO accept/response completions.
 *
 * <h3 class="desc">Description</h3>
 * This callback is used for several ELS send operations.
 *
 * @param hio Pointer to the HW IO context that completed.
 * @param rnode Pointer to the remote node.
 * @param length Length of the returned payload data.
 * @param status Status of the completion.
 * @param ext_status Extended status of the completion.
 * @param arg Application-specific argument (generally a pointer to the ELS IO context).
 *
 * @return Returns 0 on success; or a negative error value on failure.
 */

static int32_t
ocs_els_acc_cb(ocs_hw_io_t *hio, ocs_remote_node_t *rnode, uint32_t length, int32_t status, uint32_t ext_status, void *arg)
{
	ocs_io_t *els;
	ocs_node_t *node;
	ocs_t *ocs;
	ocs_node_cb_t cbdata;
	ocs_io_t *io;

	ocs_assert(arg, -1);
	io = arg;
	els = io;
	ocs_assert(els, -1);
	ocs_assert(els->node, -1);
	node = els->node;
	ocs_assert(node->ocs, -1);
	ocs = node->ocs;

	ocs_assert(io->hio, -1);
	ocs_assert(hio == io->hio, -1);

	cbdata.status = status;
	cbdata.ext_status = ext_status;
	cbdata.header = NULL;
	cbdata.els = els;

	/* Post node event */
	switch (status) {
	case SLI4_FC_WCQE_STATUS_SUCCESS:
		ocs_node_post_event(node, OCS_EVT_SRRS_ELS_CMPL_OK, &cbdata);
		break;

	default:
		ocs_log_warn(ocs, "[%s] %-8s failed status x%x, ext_status x%x\n",
			node->display_name, els->display_name, status, ext_status);
		ocs_log_warn(ocs, "els acc complete: failed status x%x, ext_status, x%x\n", status, ext_status);
		ocs_node_post_event(node, OCS_EVT_SRRS_ELS_CMPL_FAIL, &cbdata);
		break;
	}

	/* If this IO has a callback, invoke it */
	if (els->els_callback) {
		(*els->els_callback)(node, &cbdata, els->els_callback_arg);
	}

	ocs_els_io_free(els);

	return 0;
}

/**
 * @ingroup els_api
 * @brief Format and send a PLOGI ELS command.
 *
 * <h3 class="desc">Description</h3>
 * Construct a PLOGI payload using the domain SLI port service parameters,
 * and send to the \c node.
 *
 * @param node Node to which the PLOGI is sent.
 * @param timeout_sec Command timeout, in seconds.
 * @param retries Number of times to retry errors before reporting a failure.
 * @param cb Callback function.
 * @param cbarg Callback function argument.
 *
 * @return Returns pointer to IO object, or NULL if error.
 */

ocs_io_t *
ocs_send_plogi(ocs_node_t *node, uint32_t timeout_sec, uint32_t retries,
	void (*cb)(ocs_node_t *node, ocs_node_cb_t *cbdata, void *arg), void *cbarg)
{
	ocs_io_t *els;
	ocs_t *ocs = node->ocs;
	fc_plogi_payload_t *plogi;

	node_els_trace();

	els = ocs_els_io_alloc(node, sizeof(*plogi), OCS_ELS_ROLE_ORIGINATOR);
	if (els == NULL) {
		ocs_log_err(ocs, "IO alloc failed\n");
	} else {
		els->els_timeout_sec = timeout_sec;
		els->els_retries_remaining = retries;
		els->els_callback = cb;
		els->els_callback_arg = cbarg;
		els->display_name = "plogi";

		/* Build PLOGI request */
		plogi = els->els_req.virt;

		ocs_memcpy(plogi, node->sport->service_params, sizeof(*plogi));

		plogi->command_code = FC_ELS_CMD_PLOGI;
		plogi->resv1 = 0;

		ocs_display_sparams(node->display_name, "plogi send req", 0, NULL, plogi->common_service_parameters);

		els->hio_type = OCS_HW_ELS_REQ;
		els->iparam.els.timeout = timeout_sec;

		ocs_io_transition(els, __ocs_els_init, NULL);

	}
	return els;
}

/**
 * @ingroup els_api
 * @brief Format and send a FLOGI ELS command.
 *
 * <h3 class="desc">Description</h3>
 * Construct an FLOGI payload, and send to the \c node.
 *
 * @param node Node to which the FLOGI is sent.
 * @param timeout_sec Command timeout, in seconds.
 * @param retries Number of times to retry errors before reporting a failure.
 * @param cb Callback function.
 * @param cbarg Callback function argument.
 *
 * @return Returns pointer to IO object, or NULL if error.
 */

ocs_io_t *
ocs_send_flogi(ocs_node_t *node, uint32_t timeout_sec, uint32_t retries,
	els_cb_t cb, void *cbarg)
{
	ocs_io_t *els;
	ocs_t *ocs;
	fc_plogi_payload_t *flogi;

	ocs_assert(node, NULL);
	ocs_assert(node->ocs, NULL);
	ocs_assert(node->sport, NULL);
	ocs = node->ocs;

	node_els_trace();

	els = ocs_els_io_alloc(node, sizeof(*flogi), OCS_ELS_ROLE_ORIGINATOR);
	if (els == NULL) {
		ocs_log_err(ocs, "IO alloc failed\n");
	} else {
		els->els_timeout_sec = timeout_sec;
		els->els_retries_remaining = retries;
		els->els_callback = cb;
		els->els_callback_arg = cbarg;
		els->display_name = "flogi";

		/* Build FLOGI request */
		flogi = els->els_req.virt;

		ocs_memcpy(flogi, node->sport->service_params, sizeof(*flogi));
		flogi->command_code = FC_ELS_CMD_FLOGI;
		flogi->resv1 = 0;

		/* Priority tagging support */
		flogi->common_service_parameters[1] |= ocs_htobe32(1U << 23);

		ocs_display_sparams(node->display_name, "flogi send req", 0, NULL, flogi->common_service_parameters);

		els->hio_type = OCS_HW_ELS_REQ;
		els->iparam.els.timeout = timeout_sec;
		ocs_io_transition(els, __ocs_els_init, NULL);
	}
	return els;
}

/**
 * @ingroup els_api
 * @brief Format and send a FDISC ELS command.
 *
 * <h3 class="desc">Description</h3>
 * Construct an FDISC payload, and send to the \c node.
 *
 * @param node Node to which the FDISC is sent.
 * @param timeout_sec Command timeout, in seconds.
 * @param retries Number of times to retry errors before reporting a failure.
 * @param cb Callback function.
 * @param cbarg Callback function argument.
 *
 * @return Returns pointer to IO object, or NULL if error.
 */

ocs_io_t *
ocs_send_fdisc(ocs_node_t *node, uint32_t timeout_sec, uint32_t retries,
	els_cb_t cb, void *cbarg)
{
	ocs_io_t *els;
	ocs_t *ocs;
	fc_plogi_payload_t *fdisc;

	ocs_assert(node, NULL);
	ocs_assert(node->ocs, NULL);
	ocs = node->ocs;

	node_els_trace();

	els = ocs_els_io_alloc(node, sizeof(*fdisc), OCS_ELS_ROLE_ORIGINATOR);
	if (els == NULL) {
		ocs_log_err(ocs, "IO alloc failed\n");
	} else {
		els->els_timeout_sec = timeout_sec;
		els->els_retries_remaining = retries;
		els->els_callback = cb;
		els->els_callback_arg = cbarg;
		els->display_name = "fdisc";

		/* Build FDISC request */
		fdisc = els->els_req.virt;

		ocs_memcpy(fdisc, node->sport->service_params, sizeof(*fdisc));
		fdisc->command_code = FC_ELS_CMD_FDISC;
		fdisc->resv1 = 0;

		ocs_display_sparams(node->display_name, "fdisc send req", 0, NULL, fdisc->common_service_parameters);

		els->hio_type = OCS_HW_ELS_REQ;
		els->iparam.els.timeout = timeout_sec;
		ocs_io_transition(els, __ocs_els_init, NULL);
	}
	return els;
}

/**
 * @ingroup els_api
 * @brief Send a PRLI ELS command.
 *
 * <h3 class="desc">Description</h3>
 * Construct a PRLI ELS command, and send to the \c node.
 *
 * @param node Node to which the PRLI is sent.
 * @param timeout_sec Command timeout, in seconds.
 * @param retries Number of times to retry errors before reporting a failure.
 * @param cb Callback function.
 * @param cbarg Callback function argument.
 *
 * @return Returns pointer to IO object, or NULL if error.
 */

ocs_io_t *
ocs_send_prli(ocs_node_t *node, uint32_t timeout_sec, uint32_t retries,
	els_cb_t cb, void *cbarg)
{
	ocs_t *ocs = node->ocs;
	ocs_io_t *els;
	fc_prli_payload_t *prli;

	node_els_trace();

	els = ocs_els_io_alloc(node, sizeof(*prli), OCS_ELS_ROLE_ORIGINATOR);
	if (els == NULL) {
		ocs_log_err(ocs, "IO alloc failed\n");
	} else {
		els->els_timeout_sec = timeout_sec;
		els->els_retries_remaining = retries;
		els->els_callback = cb;
		els->els_callback_arg = cbarg;
		els->display_name = "prli";

		/* Build PRLI request */
		prli = els->els_req.virt;

		ocs_memset(prli, 0, sizeof(*prli));

		prli->command_code = FC_ELS_CMD_PRLI;
		prli->page_length = 16;
		prli->payload_length = ocs_htobe16(sizeof(fc_prli_payload_t));
		prli->type = FC_TYPE_FCP;
		prli->type_ext = 0;
		prli->flags = ocs_htobe16(FC_PRLI_ESTABLISH_IMAGE_PAIR);
		prli->service_params = ocs_htobe16(FC_PRLI_READ_XRDY_DISABLED |
			(node->sport->enable_ini ? FC_PRLI_INITIATOR_FUNCTION : 0) |
			(node->sport->enable_tgt ? FC_PRLI_TARGET_FUNCTION : 0)); 

		/* For Tape Drive support */
		prli->service_params |= ocs_htobe16(FC_PRLI_CONFIRMED_COMPLETION | FC_PRLI_RETRY |
				 FC_PRLI_TASK_RETRY_ID_REQ| FC_PRLI_REC_SUPPORT);

		els->hio_type = OCS_HW_ELS_REQ;
		els->iparam.els.timeout = timeout_sec;
		ocs_io_transition(els, __ocs_els_init, NULL);
	}

	return els;
}

/**
 * @ingroup els_api
 * @brief Send a PRLO ELS command.
 *
 * <h3 class="desc">Description</h3>
 * Construct a PRLO ELS command, and send to the \c node.
 *
 * @param node Node to which the PRLO is sent.
 * @param timeout_sec Command timeout, in seconds.
 * @param retries Number of times to retry errors before reporting a failure.
 * @param cb Callback function.
 * @param cbarg Callback function argument.
 *
 * @return Returns pointer to IO object, or NULL if error.
 */

ocs_io_t *
ocs_send_prlo(ocs_node_t *node, uint32_t timeout_sec, uint32_t retries,
	els_cb_t cb, void *cbarg)
{
	ocs_t *ocs = node->ocs;
	ocs_io_t *els;
	fc_prlo_payload_t *prlo;

	node_els_trace();

	els = ocs_els_io_alloc(node, sizeof(*prlo), OCS_ELS_ROLE_ORIGINATOR);
	if (els == NULL) {
		ocs_log_err(ocs, "IO alloc failed\n");
	} else {
		els->els_timeout_sec = timeout_sec;
		els->els_retries_remaining = retries;
		els->els_callback = cb;
		els->els_callback_arg = cbarg;
		els->display_name = "prlo";

		/* Build PRLO request */
		prlo = els->els_req.virt;

		ocs_memset(prlo, 0, sizeof(*prlo));
		prlo->command_code = FC_ELS_CMD_PRLO;
		prlo->page_length = 16;
		prlo->payload_length = ocs_htobe16(sizeof(fc_prlo_payload_t));
		prlo->type = FC_TYPE_FCP;
		prlo->type_ext = 0;

		els->hio_type = OCS_HW_ELS_REQ;
		els->iparam.els.timeout = timeout_sec;
		ocs_io_transition(els, __ocs_els_init, NULL);
	}
	return els;
}

/**
 * @ingroup els_api
 * @brief Send a LOGO ELS command.
 *
 * <h3 class="desc">Description</h3>
 * Format a LOGO, and send to the \c node.
 *
 * @param node Node to which the LOGO is sent.
 * @param timeout_sec Command timeout, in seconds.
 * @param retries Number of times to retry errors before reporting a failure.
 * @param cb Callback function.
 * @param cbarg Callback function argument.
 *
 * @return Returns pointer to IO object, or NULL if error.
 */

ocs_io_t *
ocs_send_logo(ocs_node_t *node, uint32_t timeout_sec, uint32_t retries,
	els_cb_t cb, void *cbarg)
{
	ocs_io_t *els;
	ocs_t *ocs;
	fc_logo_payload_t *logo;
	fc_plogi_payload_t *sparams;


	ocs = node->ocs;

	node_els_trace();

	sparams = (fc_plogi_payload_t*) node->sport->service_params;

	els = ocs_els_io_alloc(node, sizeof(*logo), OCS_ELS_ROLE_ORIGINATOR);
	if (els == NULL) {
		ocs_log_err(ocs, "IO alloc failed\n");
	} else {
		els->els_timeout_sec = timeout_sec;
		els->els_retries_remaining = retries;
		els->els_callback = cb;
		els->els_callback_arg = cbarg;
		els->display_name = "logo";

		/* Build LOGO request */

		logo = els->els_req.virt;

		ocs_memset(logo, 0, sizeof(*logo));
		logo->command_code = FC_ELS_CMD_LOGO;
		logo->resv1 = 0;
		logo->port_id = fc_htobe24(node->rnode.sport->fc_id);
		logo->port_name_hi = sparams->port_name_hi;
		logo->port_name_lo = sparams->port_name_lo;

		els->hio_type = OCS_HW_ELS_REQ;
		els->iparam.els.timeout = timeout_sec;
		ocs_io_transition(els, __ocs_els_init, NULL);
	}
	return els;
}

/**
 * @ingroup els_api
 * @brief Send an ADISC ELS command.
 *
 * <h3 class="desc">Description</h3>
 * Construct an ADISC ELS command, and send to the \c node.
 *
 * @param node Node to which the ADISC is sent.
 * @param timeout_sec Command timeout, in seconds.
 * @param retries Number of times to retry errors before reporting a failure.
 * @param cb Callback function.
 * @param cbarg Callback function argument.
 *
 * @return Returns pointer to IO object, or NULL if error.
 */

ocs_io_t *
ocs_send_adisc(ocs_node_t *node, uint32_t timeout_sec, uint32_t retries,
	els_cb_t cb, void *cbarg)
{
	ocs_io_t *els;
	ocs_t *ocs;
	fc_adisc_payload_t *adisc;
	fc_plogi_payload_t *sparams;
	ocs_sport_t *sport = node->sport;

	ocs = node->ocs;

	node_els_trace();

	sparams = (fc_plogi_payload_t*) node->sport->service_params;

	els = ocs_els_io_alloc(node, sizeof(*adisc), OCS_ELS_ROLE_ORIGINATOR);
	if (els == NULL) {
		ocs_log_err(ocs, "IO alloc failed\n");
	} else {
		els->els_timeout_sec = timeout_sec;
		els->els_retries_remaining = retries;
		els->els_callback = cb;
		els->els_callback_arg = cbarg;
		els->display_name = "adisc";

		/* Build ADISC request */

		adisc = els->els_req.virt;
		sparams = (fc_plogi_payload_t*) node->sport->service_params;

		ocs_memset(adisc, 0, sizeof(*adisc));
		adisc->command_code = FC_ELS_CMD_ADISC;
		adisc->hard_address = fc_htobe24(sport->fc_id);
		adisc->port_name_hi = sparams->port_name_hi;
		adisc->port_name_lo = sparams->port_name_lo;
		adisc->node_name_hi = sparams->node_name_hi;
		adisc->node_name_lo = sparams->node_name_lo;
		adisc->port_id = fc_htobe24(node->rnode.sport->fc_id);

		els->hio_type = OCS_HW_ELS_REQ;
		els->iparam.els.timeout = timeout_sec;
		ocs_io_transition(els, __ocs_els_init, NULL);
	}
	return els;
}

/**
 * @ingroup els_api
 * @brief Send a PDISC ELS command.
 *
 * <h3 class="desc">Description</h3>
 * Construct a PDISC ELS command, and send to the \c node.
 *
 * @param node Node to which the PDISC is sent.
 * @param timeout_sec Command timeout, in seconds.
 * @param retries Number of times to retry errors before reporting a failure.
 * @param cb Callback function.
 * @param cbarg Callback function argument.
 *
 * @return Returns pointer to IO object, or NULL if error.
 */

ocs_io_t *
ocs_send_pdisc(ocs_node_t *node, uint32_t timeout_sec, uint32_t retries,
	els_cb_t cb, void *cbarg)
{
	ocs_io_t *els;
	ocs_t *ocs = node->ocs;
	fc_plogi_payload_t *pdisc;

	node_els_trace();

	els = ocs_els_io_alloc(node, sizeof(*pdisc), OCS_ELS_ROLE_ORIGINATOR);
	if (els == NULL) {
		ocs_log_err(ocs, "IO alloc failed\n");
	} else {
		els->els_timeout_sec = timeout_sec;
		els->els_retries_remaining = retries;
		els->els_callback = cb;
		els->els_callback_arg = cbarg;
		els->display_name = "pdisc";

		pdisc = els->els_req.virt;

		ocs_memcpy(pdisc, node->sport->service_params, sizeof(*pdisc));

		pdisc->command_code = FC_ELS_CMD_PDISC;
		pdisc->resv1 = 0;

		els->hio_type = OCS_HW_ELS_REQ;
		els->iparam.els.timeout = timeout_sec;
		ocs_io_transition(els, __ocs_els_init, NULL);
	}
	return els;
}

/**
 * @ingroup els_api
 * @brief Send an SCR ELS command.
 *
 * <h3 class="desc">Description</h3>
 * Format an SCR, and send to the \c node.
 *
 * @param node Node to which the SCR is sent.
 * @param timeout_sec Command timeout, in seconds.
 * @param retries Number of times to retry errors before reporting a failure.
 * @param cb Callback function
 * @param cbarg Callback function arg
 *
 * @return Returns pointer to IO object, or NULL if error.
 */

ocs_io_t *
ocs_send_scr(ocs_node_t *node, uint32_t timeout_sec, uint32_t retries,
	els_cb_t cb, void *cbarg)
{
	ocs_io_t *els;
	ocs_t *ocs = node->ocs;
	fc_scr_payload_t *req;

	node_els_trace();

	els = ocs_els_io_alloc(node, sizeof(*req), OCS_ELS_ROLE_ORIGINATOR);
	if (els == NULL) {
		ocs_log_err(ocs, "IO alloc failed\n");
	} else {
		els->els_timeout_sec = timeout_sec;
		els->els_retries_remaining = retries;
		els->els_callback = cb;
		els->els_callback_arg = cbarg;
		els->display_name = "scr";

		req = els->els_req.virt;

		ocs_memset(req, 0, sizeof(*req));
		req->command_code = FC_ELS_CMD_SCR;
		req->function = FC_SCR_REG_FULL;

		els->hio_type = OCS_HW_ELS_REQ;
		els->iparam.els.timeout = timeout_sec;
		ocs_io_transition(els, __ocs_els_init, NULL);
	}
	return els;
}

/**
 * @ingroup els_api
 * @brief Send an RRQ ELS command.
 *
 * <h3 class="desc">Description</h3>
 * Format an RRQ, and send to the \c node.
 *
 * @param node Node to which the RRQ is sent.
 * @param timeout_sec Command timeout, in seconds.
 * @param retries Number of times to retry errors before reporting a failure.
 * @param cb Callback function
 * @param cbarg Callback function arg
 *
 * @return Returns pointer to IO object, or NULL if error.
 */

ocs_io_t *
ocs_send_rrq(ocs_node_t *node, uint32_t timeout_sec, uint32_t retries,
	els_cb_t cb, void *cbarg)
{
	ocs_io_t *els;
	ocs_t *ocs = node->ocs;
	fc_scr_payload_t *req;

	node_els_trace();

	els = ocs_els_io_alloc(node, sizeof(*req), OCS_ELS_ROLE_ORIGINATOR);
	if (els == NULL) {
		ocs_log_err(ocs, "IO alloc failed\n");
	} else {
		els->els_timeout_sec = timeout_sec;
		els->els_retries_remaining = retries;
		els->els_callback = cb;
		els->els_callback_arg = cbarg;
		els->display_name = "scr";

		req = els->els_req.virt;

		ocs_memset(req, 0, sizeof(*req));
		req->command_code = FC_ELS_CMD_RRQ;
		req->function = FC_SCR_REG_FULL;

		els->hio_type = OCS_HW_ELS_REQ;
		els->iparam.els.timeout = timeout_sec;
		ocs_io_transition(els, __ocs_els_init, NULL);
	}
	return els;
}

/**
 * @ingroup els_api
 * @brief Send an RSCN ELS command.
 *
 * <h3 class="desc">Description</h3>
 * Format an RSCN, and send to the \c node.
 *
 * @param node Node to which the RRQ is sent.
 * @param timeout_sec Command timeout, in seconds.
 * @param retries Number of times to retry errors before reporting a failure.
 * @param port_ids Pointer to port IDs
 * @param port_ids_count Count of port IDs
 * @param cb Callback function
 * @param cbarg Callback function arg
 *
 * @return Returns pointer to IO object, or NULL if error.
 */
ocs_io_t *
ocs_send_rscn(ocs_node_t *node, uint32_t timeout_sec, uint32_t retries,
	void *port_ids, uint32_t port_ids_count, els_cb_t cb, void *cbarg)
{
	ocs_io_t *els;
	ocs_t *ocs = node->ocs;
	fc_rscn_payload_t *req;
	uint32_t payload_length = sizeof(fc_rscn_affected_port_id_page_t)*(port_ids_count - 1) +
		sizeof(fc_rscn_payload_t);

	node_els_trace();

	els = ocs_els_io_alloc(node, payload_length, OCS_ELS_ROLE_ORIGINATOR);
	if (els == NULL) {
		ocs_log_err(ocs, "IO alloc failed\n");
	} else {
		els->els_timeout_sec = timeout_sec;
		els->els_retries_remaining = retries;
		els->els_callback = cb;
		els->els_callback_arg = cbarg;
		els->display_name = "rscn";

		req = els->els_req.virt;

		req->command_code = FC_ELS_CMD_RSCN;
		req->page_length = sizeof(fc_rscn_affected_port_id_page_t);
		req->payload_length = ocs_htobe16(sizeof(*req) +
			sizeof(fc_rscn_affected_port_id_page_t)*(port_ids_count-1));

		els->hio_type = OCS_HW_ELS_REQ;
		els->iparam.els.timeout = timeout_sec;

		/* copy in the payload */
		ocs_memcpy(req->port_list, port_ids, port_ids_count*sizeof(fc_rscn_affected_port_id_page_t));

		/* Submit the request */
		ocs_io_transition(els, __ocs_els_init, NULL);
	}
	return els;
}

/**
 * @brief Send an LS_RJT ELS response.
 *
 * <h3 class="desc">Description</h3>
 * Send an LS_RJT ELS response.
 *
 * @param io Pointer to a SCSI IO object.
 * @param ox_id Originator exchange ID being responded to.
 * @param reason_code Reason code value for LS_RJT.
 * @param reason_code_expl Reason code explanation value for LS_RJT.
 * @param vendor_unique Vendor-unique value for LS_RJT.
 * @param cb Callback function.
 * @param cbarg Callback function argument.
 *
 * @return Returns pointer to IO object, or NULL if error.
 */

ocs_io_t *
ocs_send_ls_rjt(ocs_io_t *io, uint32_t ox_id, uint32_t reason_code, uint32_t reason_code_expl,
		uint32_t vendor_unique, els_cb_t cb, void *cbarg)
{
	ocs_node_t *node = io->node;
	int32_t rc;
	ocs_t *ocs = node->ocs;
	fc_ls_rjt_payload_t *rjt;

	node_els_trace();

	io->els_callback = cb;
	io->els_callback_arg = cbarg;
	io->display_name = "ls_rjt";
	io->init_task_tag = ox_id;

	ocs_memset(&io->iparam, 0, sizeof(io->iparam));
	io->iparam.els.ox_id = ox_id;

	rjt = io->els_req.virt;
	ocs_memset(rjt, 0, sizeof(*rjt));

	rjt->command_code = FC_ELS_CMD_RJT;
	rjt->reason_code = reason_code;
	rjt->reason_code_exp = reason_code_expl;

	io->hio_type = OCS_HW_ELS_RSP;
	if ((rc = ocs_els_send_rsp(io, sizeof(*rjt)))) {
		ocs_els_io_free(io);
		io = NULL;
	}

	return io;
}

/**
 * @ingroup els_api
 * @brief Send a PLOGI accept response.
 *
 * <h3 class="desc">Description</h3>
 * Construct a PLOGI LS_ACC, and send to the \c node, using the originator exchange ID
 * \c ox_id.
 *
 * @param io Pointer to a SCSI IO object.
 * @param ox_id Originator exchange ID being responsed to.
 * @param cb Callback function.
 * @param cbarg Callback function argument.
 *
 * @return Returns pointer to IO object, or NULL if error.
 */
ocs_io_t *
ocs_send_plogi_acc(ocs_io_t *io, uint32_t ox_id, els_cb_t cb, void *cbarg)
{
	ocs_node_t *node = io->node;
	int32_t rc;
	ocs_t *ocs = node->ocs;
	fc_plogi_payload_t *plogi;
	fc_plogi_payload_t *req = (fc_plogi_payload_t *)node->service_params;

	node_els_trace();

	io->els_callback = cb;
	io->els_callback_arg = cbarg;
	io->display_name = "plog_acc";
	io->init_task_tag = ox_id;

	ocs_memset(&io->iparam, 0, sizeof(io->iparam));
	io->iparam.els.ox_id = ox_id;

	plogi = io->els_req.virt;

	/* copy our port's service parameters to payload */
	ocs_memcpy(plogi, node->sport->service_params, sizeof(*plogi));
	plogi->command_code = FC_ELS_CMD_ACC;
	plogi->resv1 = 0;
	
	/* Set Application header support bit if requested */
	if (req->common_service_parameters[1] & ocs_htobe32(1U << 24)) {
		plogi->common_service_parameters[1] |= ocs_htobe32(1U << 24);
	}

	/* Priority tagging support. */
	if (req->common_service_parameters[1] & ocs_htobe32(1U << 23)) {
		plogi->common_service_parameters[1] |= ocs_htobe32(1U << 23);
	}

	ocs_display_sparams(node->display_name, "plogi send resp", 0, NULL, plogi->common_service_parameters);

	io->hio_type = OCS_HW_ELS_RSP;
	if ((rc = ocs_els_send_rsp(io, sizeof(*plogi)))) {
		ocs_els_io_free(io);
		io = NULL;
	}
	return io;
}

/**
 * @ingroup els_api
 * @brief Send an FLOGI accept response for point-to-point negotiation.
 *
 * <h3 class="desc">Description</h3>
 * Construct an FLOGI accept response, and send to the \c node using the originator
 * exchange id \c ox_id. The \c s_id is used for the response frame source FC ID.
 *
 * @param io Pointer to a SCSI IO object.
 * @param ox_id Originator exchange ID for the response.
 * @param s_id Source FC ID to be used in the response frame.
 * @param cb Callback function.
 * @param cbarg Callback function argument.
 *
 * @return Returns pointer to IO object, or NULL if error.
 */
ocs_io_t *
ocs_send_flogi_p2p_acc(ocs_io_t *io, uint32_t ox_id, uint32_t s_id, els_cb_t cb, void *cbarg)
{
	ocs_node_t *node = io->node;
	int32_t rc;
	ocs_t *ocs = node->ocs;
	fc_plogi_payload_t *flogi;

	node_els_trace();

	io->els_callback = cb;
	io->els_callback_arg = cbarg;
	io->display_name = "flogi_p2p_acc";
	io->init_task_tag = ox_id;

	ocs_memset(&io->iparam, 0, sizeof(io->iparam));
	io->iparam.els_sid.ox_id = ox_id;
	io->iparam.els_sid.s_id = s_id;

	flogi = io->els_req.virt;

	/* copy our port's service parameters to payload */
	ocs_memcpy(flogi, node->sport->service_params, sizeof(*flogi));
	flogi->command_code = FC_ELS_CMD_ACC;
	flogi->resv1 = 0;
	ocs_memset(flogi->class1_service_parameters, 0, sizeof(flogi->class1_service_parameters));
	ocs_memset(flogi->class2_service_parameters, 0, sizeof(flogi->class1_service_parameters));
	ocs_memset(flogi->class3_service_parameters, 0, sizeof(flogi->class1_service_parameters));
	ocs_memset(flogi->class4_service_parameters, 0, sizeof(flogi->class1_service_parameters));

	io->hio_type = OCS_HW_ELS_RSP_SID;
	if ((rc = ocs_els_send_rsp(io, sizeof(*flogi)))) {
		ocs_els_io_free(io);
		io = NULL;
	}

	return io;
}

ocs_io_t *
ocs_send_flogi_acc(ocs_io_t *io, uint32_t ox_id, uint32_t is_fport, els_cb_t cb, void *cbarg)
{
	ocs_node_t *node = io->node;
	int32_t rc;
	ocs_t *ocs = node->ocs;
	fc_plogi_payload_t *flogi;

	node_els_trace();

	io->els_callback = cb;
	io->els_callback_arg = cbarg;
	io->display_name = "flogi_acc";
	io->init_task_tag = ox_id;

	ocs_memset(&io->iparam, 0, sizeof(io->iparam));
	io->iparam.els_sid.ox_id = ox_id;
	io->iparam.els_sid.s_id = io->node->sport->fc_id;

	flogi = io->els_req.virt;

	/* copy our port's service parameters to payload */
	ocs_memcpy(flogi, node->sport->service_params, sizeof(*flogi));

	/* Set F_port */
	if (is_fport) {
		/* Set F_PORT and Multiple N_PORT_ID Assignment */
		flogi->common_service_parameters[1] |= ocs_be32toh(3U << 28);
	}

	flogi->command_code = FC_ELS_CMD_ACC;
	flogi->resv1 = 0;

	ocs_display_sparams(node->display_name, "flogi send resp", 0, NULL, flogi->common_service_parameters);

	ocs_memset(flogi->class1_service_parameters, 0, sizeof(flogi->class1_service_parameters));
	ocs_memset(flogi->class2_service_parameters, 0, sizeof(flogi->class1_service_parameters));
	ocs_memset(flogi->class3_service_parameters, 0, sizeof(flogi->class1_service_parameters));
	ocs_memset(flogi->class4_service_parameters, 0, sizeof(flogi->class1_service_parameters));

	io->hio_type = OCS_HW_ELS_RSP_SID;
	if ((rc = ocs_els_send_rsp(io, sizeof(*flogi)))) {
		ocs_els_io_free(io);
		io = NULL;
	}

	return io;
}

/**
 * @ingroup els_api
 * @brief Send a PRLI accept response
 *
 * <h3 class="desc">Description</h3>
 * Construct a PRLI LS_ACC response, and send to the \c node, using the originator
 * \c ox_id exchange ID.
 *
 * @param io Pointer to a SCSI IO object.
 * @param ox_id Originator exchange ID.
 * @param cb Callback function.
 * @param cbarg Callback function argument.
 *
 * @return Returns pointer to IO object, or NULL if error.
 */

ocs_io_t *
ocs_send_prli_acc(ocs_io_t *io, uint32_t ox_id, uint8_t fc_type, els_cb_t cb, void *cbarg)
{
	ocs_node_t *node = io->node;
	int32_t rc;
	ocs_t *ocs = node->ocs;
	fc_prli_payload_t *prli;

	node_els_trace();

	io->els_callback = cb;
	io->els_callback_arg = cbarg;
	io->display_name = "prli_acc";
	io->init_task_tag = ox_id;

	ocs_memset(&io->iparam, 0, sizeof(io->iparam));
	io->iparam.els.ox_id = ox_id;

	prli = io->els_req.virt;
	ocs_memset(prli, 0, sizeof(*prli));

	prli->command_code = FC_ELS_CMD_ACC;
	prli->page_length = 16;
	prli->payload_length = ocs_htobe16(sizeof(fc_prli_payload_t));
	prli->type = fc_type;
	prli->type_ext = 0;
	prli->flags = ocs_htobe16(FC_PRLI_ESTABLISH_IMAGE_PAIR | FC_PRLI_REQUEST_EXECUTED);

	prli->service_params = ocs_htobe16(FC_PRLI_READ_XRDY_DISABLED |
				(node->sport->enable_ini ? FC_PRLI_INITIATOR_FUNCTION : 0) |
				(node->sport->enable_tgt ? FC_PRLI_TARGET_FUNCTION : 0)); 

	io->hio_type = OCS_HW_ELS_RSP;
	if ((rc = ocs_els_send_rsp(io, sizeof(*prli)))) {
		ocs_els_io_free(io);
		io = NULL;
	}

	return io;
}

/**
 * @ingroup els_api
 * @brief Send a PRLO accept response.
 *
 * <h3 class="desc">Description</h3>
 * Construct a PRLO LS_ACC response, and send to the \c node, using the originator
 * exchange ID \c ox_id.
 *
 * @param io Pointer to a SCSI IO object.
 * @param ox_id Originator exchange ID.
 * @param cb Callback function.
 * @param cbarg Callback function argument.
 *
 * @return Returns pointer to IO object, or NULL if error.
 */

ocs_io_t *
ocs_send_prlo_acc(ocs_io_t *io, uint32_t ox_id, uint8_t fc_type, els_cb_t cb, void *cbarg)
{
	ocs_node_t *node = io->node;
	int32_t rc;
	ocs_t *ocs = node->ocs;
	fc_prlo_acc_payload_t *prlo_acc;

	node_els_trace();

	io->els_callback = cb;
	io->els_callback_arg = cbarg;
	io->display_name = "prlo_acc";
	io->init_task_tag = ox_id;

	ocs_memset(&io->iparam, 0, sizeof(io->iparam));
	io->iparam.els.ox_id = ox_id;

	prlo_acc = io->els_req.virt;
	ocs_memset(prlo_acc, 0, sizeof(*prlo_acc));

	prlo_acc->command_code = FC_ELS_CMD_ACC;
	prlo_acc->page_length = 16;
	prlo_acc->payload_length = ocs_htobe16(sizeof(fc_prlo_acc_payload_t));
	prlo_acc->type = fc_type;
	prlo_acc->type_ext = 0;
	prlo_acc->response_code = FC_PRLO_REQUEST_EXECUTED;

	io->hio_type = OCS_HW_ELS_RSP;
	if ((rc = ocs_els_send_rsp(io, sizeof(*prlo_acc)))) {
		ocs_els_io_free(io);
		io = NULL;
	}

	return io;
}

/**
 * @ingroup els_api
 * @brief Send a generic LS_ACC response without a payload.
 *
 * <h3 class="desc">Description</h3>
 * A generic LS_ACC response is sent to the \c node using the originator exchange ID
 * \c ox_id.
 *
 * @param io Pointer to a SCSI IO object.
 * @param ox_id Originator exchange id.
 * @param cb Callback function.
 * @param cbarg Callback function argument.
 *
 * @return Returns pointer to IO object, or NULL if error.
 */
ocs_io_t *
ocs_send_ls_acc(ocs_io_t *io, uint32_t ox_id, els_cb_t cb, void *cbarg)
{
	ocs_node_t *node = io->node;
	int32_t rc;
	ocs_t *ocs = node->ocs;
	fc_acc_payload_t *acc;

	node_els_trace();

	io->els_callback = cb;
	io->els_callback_arg = cbarg;
	io->display_name = "ls_acc";
	io->init_task_tag = ox_id;

	ocs_memset(&io->iparam, 0, sizeof(io->iparam));
	io->iparam.els.ox_id = ox_id;

	acc = io->els_req.virt;
	ocs_memset(acc, 0, sizeof(*acc));

	acc->command_code = FC_ELS_CMD_ACC;

	io->hio_type = OCS_HW_ELS_RSP;
	if ((rc = ocs_els_send_rsp(io, sizeof(*acc)))) {
		ocs_els_io_free(io);
		io = NULL;
	}

	return io;
}

/**
 * @ingroup els_api
 * @brief Send a LOGO accept response.
 *
 * <h3 class="desc">Description</h3>
 * Construct a LOGO LS_ACC response, and send to the \c node, using the originator
 * exchange ID \c ox_id.
 *
 * @param io Pointer to a SCSI IO object.
 * @param ox_id Originator exchange ID.
 * @param cb Callback function.
 * @param cbarg Callback function argument.
 *
 * @return Returns pointer to IO object, or NULL if error.
 */
ocs_io_t *
ocs_send_logo_acc(ocs_io_t *io, uint32_t ox_id, els_cb_t cb, void *cbarg)
{
	ocs_node_t *node = io->node;
	int32_t rc;
	ocs_t *ocs = node->ocs;
	fc_acc_payload_t *logo;

	node_els_trace();

	io->els_callback = cb;
	io->els_callback_arg = cbarg;
	io->display_name = "logo_acc";
	io->init_task_tag = ox_id;

	ocs_memset(&io->iparam, 0, sizeof(io->iparam));
	io->iparam.els.ox_id = ox_id;

	logo = io->els_req.virt;
	ocs_memset(logo, 0, sizeof(*logo));

	logo->command_code = FC_ELS_CMD_ACC;
	logo->resv1 = 0;

	io->hio_type = OCS_HW_ELS_RSP;
	if ((rc = ocs_els_send_rsp(io, sizeof(*logo)))) {
		ocs_els_io_free(io);
		io = NULL;
	}

	return io;
}

/**
 * @ingroup els_api
 * @brief Send an ADISC accept response.
 *
 * <h3 class="desc">Description</h3>
 * Construct an ADISC LS__ACC, and send to the \c node, using the originator
 * exchange id \c ox_id.
 *
 * @param io Pointer to a SCSI IO object.
 * @param ox_id Originator exchange ID.
 * @param cb Callback function.
 * @param cbarg Callback function argument.
 *
 * @return Returns pointer to IO object, or NULL if error.
 */

ocs_io_t *
ocs_send_adisc_acc(ocs_io_t *io, uint32_t ox_id, els_cb_t cb, void *cbarg)
{
	ocs_node_t *node = io->node;
	int32_t rc;
	fc_adisc_payload_t *adisc;
	fc_plogi_payload_t *sparams;
	ocs_t *ocs;

	ocs_assert(node, NULL);
	ocs_assert(node->ocs, NULL);
	ocs = node->ocs;

	node_els_trace();

	io->els_callback = cb;
	io->els_callback_arg = cbarg;
	io->display_name = "adisc_acc";
	io->init_task_tag = ox_id;

	/* Go ahead and send the ELS_ACC */
	ocs_memset(&io->iparam, 0, sizeof(io->iparam));
	io->iparam.els.ox_id = ox_id;

	sparams = (fc_plogi_payload_t*) node->sport->service_params;
	adisc = io->els_req.virt;
	ocs_memset(adisc, 0, sizeof(fc_adisc_payload_t));
	adisc->command_code = FC_ELS_CMD_ACC;
	adisc->hard_address = 0;
	adisc->port_name_hi = sparams->port_name_hi;
	adisc->port_name_lo = sparams->port_name_lo;
	adisc->node_name_hi = sparams->node_name_hi;
	adisc->node_name_lo = sparams->node_name_lo;
	adisc->port_id = fc_htobe24(node->rnode.sport->fc_id);

	io->hio_type = OCS_HW_ELS_RSP;
	if ((rc = ocs_els_send_rsp(io, sizeof(*adisc)))) {
		ocs_els_io_free(io);
		io = NULL;
	}

	return io;
}

/**
 * @ingroup els_api
 * @brief Send a RFTID CT request.
 *
 * <h3 class="desc">Description</h3>
 * Construct an RFTID CT request, and send to the \c node.
 *
 * @param node Node to which the RFTID request is sent.
 * @param timeout_sec Time, in seconds, to wait before timing out the ELS.
 * @param retries Number of times to retry errors before reporting a failure.
 * @param cb Callback function.
 * @param cbarg Callback function argument.
 *
 * @return Returns pointer to IO object, or NULL if error.
 */
ocs_io_t *
ocs_ns_send_rftid(ocs_node_t *node, uint32_t timeout_sec, uint32_t retries,
	els_cb_t cb, void *cbarg)
{
	ocs_io_t *els;
	ocs_t *ocs = node->ocs;
	fcct_rftid_req_t *rftid;

	node_els_trace();

	els = ocs_els_io_alloc(node, sizeof(*rftid), OCS_ELS_ROLE_ORIGINATOR);
	if (els == NULL) {
		ocs_log_err(ocs, "IO alloc failed\n");
	} else {

		els->iparam.fc_ct.r_ctl = FC_RCTL_ELS;
		els->iparam.fc_ct.type = FC_TYPE_GS;
		els->iparam.fc_ct.df_ctl = 0;
		els->iparam.fc_ct.timeout = timeout_sec;

		els->els_callback = cb;
		els->els_callback_arg = cbarg;
		els->display_name = "rftid";

		rftid = els->els_req.virt;

		ocs_memset(rftid, 0, sizeof(*rftid));
		fcct_build_req_header(&rftid->hdr, FC_GS_NAMESERVER_RFT_ID, (OCS_ELS_RSP_LEN - sizeof(rftid->hdr)));
		rftid->port_id = ocs_htobe32(node->rnode.sport->fc_id);
		rftid->fc4_types[FC_GS_TYPE_WORD(FC_TYPE_FCP)] = ocs_htobe32(1 << FC_GS_TYPE_BIT(FC_TYPE_FCP));

		els->hio_type = OCS_HW_FC_CT;

		ocs_io_transition(els, __ocs_els_init, NULL);
	}
	return els;
}

/**
 * @ingroup els_api
 * @brief Send a RFFID CT request.
 *
 * <h3 class="desc">Description</h3>
 * Construct an RFFID CT request, and send to the \c node.
 *
 * @param node Node to which the RFFID request is sent.
 * @param timeout_sec Time, in seconds, to wait before timing out the ELS.
 * @param retries Number of times to retry errors before reporting a failure.
 * @param cb Callback function
 * @param cbarg Callback function argument.
 *
 * @return Returns pointer to IO object, or NULL if error.
 */
ocs_io_t *
ocs_ns_send_rffid(ocs_node_t *node, uint32_t timeout_sec, uint32_t retries,
	els_cb_t cb, void *cbarg)
{
	ocs_io_t *els;
	ocs_t *ocs = node->ocs;
	fcct_rffid_req_t *rffid;

	node_els_trace();

	els = ocs_els_io_alloc(node, sizeof(*rffid), OCS_ELS_ROLE_ORIGINATOR);
	if (els == NULL) {
		ocs_log_err(ocs, "IO alloc failed\n");
	} else {

		els->iparam.fc_ct.r_ctl = FC_RCTL_ELS;
		els->iparam.fc_ct.type = FC_TYPE_GS;
		els->iparam.fc_ct.df_ctl = 0;
		els->iparam.fc_ct.timeout = timeout_sec;

		els->els_callback = cb;
		els->els_callback_arg = cbarg;
		els->display_name = "rffid";

		rffid = els->els_req.virt;

		ocs_memset(rffid, 0, sizeof(*rffid));

		fcct_build_req_header(&rffid->hdr, FC_GS_NAMESERVER_RFF_ID, (OCS_ELS_RSP_LEN - sizeof(rffid->hdr)));
		rffid->port_id = ocs_htobe32(node->rnode.sport->fc_id);
		if (node->sport->enable_ini) {
			rffid->fc4_feature_bits |= FC4_FEATURE_INITIATOR;
		}
		if (node->sport->enable_tgt) {
			rffid->fc4_feature_bits |= FC4_FEATURE_TARGET;
		}
		rffid->type = FC_TYPE_FCP;

		els->hio_type = OCS_HW_FC_CT;

		ocs_io_transition(els, __ocs_els_init, NULL);
	}
	return els;
}


/**
 * @ingroup els_api
 * @brief Send a GIDPT CT request.
 *
 * <h3 class="desc">Description</h3>
 * Construct a GIDPT CT request, and send to the \c node.
 *
 * @param node Node to which the GIDPT request is sent.
 * @param timeout_sec Time, in seconds, to wait before timing out the ELS.
 * @param retries Number of times to retry errors before reporting a failure.
 * @param cb Callback function.
 * @param cbarg Callback function argument.
 *
 * @return Returns pointer to IO object, or NULL if error.
 */

ocs_io_t *
ocs_ns_send_gidpt(ocs_node_t *node, uint32_t timeout_sec, uint32_t retries,
	els_cb_t cb, void *cbarg)
{
	ocs_io_t *els;
	ocs_t *ocs = node->ocs;
	fcct_gidpt_req_t *gidpt;

	node_els_trace();

	els = ocs_els_io_alloc_size(node, sizeof(*gidpt), OCS_ELS_GID_PT_RSP_LEN, OCS_ELS_ROLE_ORIGINATOR);
	if (els == NULL) {
		ocs_log_err(ocs, "IO alloc failed\n");
	} else {

		els->iparam.fc_ct.r_ctl = FC_RCTL_ELS;
		els->iparam.fc_ct.type = FC_TYPE_GS;
		els->iparam.fc_ct.df_ctl = 0;
		els->iparam.fc_ct.timeout = timeout_sec;

		els->els_callback = cb;
		els->els_callback_arg = cbarg;
		els->display_name = "gidpt";

		gidpt = els->els_req.virt;

		ocs_memset(gidpt, 0, sizeof(*gidpt));
		fcct_build_req_header(&gidpt->hdr, FC_GS_NAMESERVER_GID_PT, (OCS_ELS_GID_PT_RSP_LEN - sizeof(gidpt->hdr)) );
		gidpt->domain_id_scope = 0;
		gidpt->area_id_scope = 0;
		gidpt->port_type = 0x7f;

		els->hio_type = OCS_HW_FC_CT;

		ocs_io_transition(els, __ocs_els_init, NULL);
	}
	return els;
}

/**
 * @ingroup els_api
 * @brief Send a BA_ACC given the request's FC header
 *
 * <h3 class="desc">Description</h3>
 * Using the S_ID/D_ID from the request's FC header, generate a BA_ACC.
 *
 * @param io Pointer to a SCSI IO object.
 * @param hdr Pointer to the FC header.
 *
 * @return Returns pointer to IO object, or NULL if error.
 */

ocs_io_t *
ocs_bls_send_acc_hdr(ocs_io_t *io, fc_header_t *hdr)
{
	uint16_t ox_id = ocs_be16toh(hdr->ox_id);
	uint16_t rx_id = ocs_be16toh(hdr->rx_id);
	uint32_t d_id = fc_be24toh(hdr->d_id);

	return ocs_bls_send_acc(io, d_id, ox_id, rx_id);
}

/**
 * @ingroup els_api
 * @brief Send a BLS BA_ACC response.
 *
 * <h3 class="desc">Description</h3>
 * Construct a BLS BA_ACC response, and send to the \c node.
 *
 * @param io Pointer to a SCSI IO object.
 * @param s_id S_ID to use for the response. If UINT32_MAX, then use our SLI port
 * (sport) S_ID.
 * @param ox_id Originator exchange ID.
 * @param rx_id Responder exchange ID.
 *
 * @return Returns pointer to IO object, or NULL if error.
 */

static ocs_io_t *
ocs_bls_send_acc(ocs_io_t *io, uint32_t s_id, uint16_t ox_id, uint16_t rx_id)
{
	ocs_node_t *node = io->node;
	int32_t rc;
	fc_ba_acc_payload_t *acc;
	ocs_t *ocs;

	ocs_assert(node, NULL);
	ocs_assert(node->ocs, NULL);
	ocs = node->ocs;

	if (node->rnode.sport->fc_id == s_id) {
		s_id = UINT32_MAX;
	}

	/* fill out generic fields */
	io->ocs = ocs;
	io->node = node;
	io->cmd_tgt = TRUE;

	/* fill out BLS Response-specific fields */
	io->io_type = OCS_IO_TYPE_BLS_RESP;
	io->display_name = "ba_acc";
	io->hio_type = OCS_HW_BLS_ACC_SID;
	io->init_task_tag = ox_id;

	/* fill out iparam fields */
	ocs_memset(&io->iparam, 0, sizeof(io->iparam));
	io->iparam.bls_sid.s_id = s_id;
	io->iparam.bls_sid.ox_id = ox_id;
	io->iparam.bls_sid.rx_id = rx_id;

	acc = (void *)io->iparam.bls_sid.payload;

	ocs_memset(io->iparam.bls_sid.payload, 0, sizeof(io->iparam.bls_sid.payload));
	acc->ox_id = io->iparam.bls_sid.ox_id;
	acc->rx_id = io->iparam.bls_sid.rx_id;
	acc->high_seq_cnt = UINT16_MAX;

	if ((rc = ocs_scsi_io_dispatch(io, ocs_bls_send_acc_cb))) {
		ocs_log_err(ocs, "ocs_scsi_io_dispatch() failed: %d\n", rc);
		ocs_scsi_io_free(io);
		io = NULL;
	}
	return io;
}

/**
 * @brief Handle the BLS accept completion.
 *
 * <h3 class="desc">Description</h3>
 * Upon completion of sending a BA_ACC, this callback is invoked by the HW.
 *
 * @param hio Pointer to the HW IO object.
 * @param rnode Pointer to the HW remote node.
 * @param length Length of the response payload, in bytes.
 * @param status Completion status.
 * @param ext_status Extended completion status.
 * @param app Callback private argument.
 *
 * @return Returns 0 on success; or a negative error value on failure.
 */

static int32_t
ocs_bls_send_acc_cb(ocs_hw_io_t *hio, ocs_remote_node_t *rnode, uint32_t length, int32_t status, uint32_t ext_status, void *app)
{
	ocs_io_t *io = app;

	ocs_assert(io, -1);

	ocs_scsi_io_free(io);
	return 0;
}

/**
 * @brief ELS abort callback.
 *
 * <h3 class="desc">Description</h3>
 * This callback is invoked by the HW when an ELS IO is aborted.
 *
 * @param hio Pointer to the HW IO object.
 * @param rnode Pointer to the HW remote node.
 * @param length Length of the response payload, in bytes.
 * @param status Completion status.
 * @param ext_status Extended completion status.
 * @param app Callback private argument.
 *
 * @return Returns 0 on success; or a negative error value on failure.
 */

static int32_t
ocs_els_abort_cb(ocs_hw_io_t *hio, ocs_remote_node_t *rnode, uint32_t length, int32_t status, uint32_t ext_status, void *app)
{
	ocs_io_t *els;
	ocs_io_t *abort_io = NULL; /* IO structure used to abort ELS */
	ocs_t *ocs;

	ocs_assert(app, -1);
	abort_io = app;
	els = abort_io->io_to_abort;
	ocs_assert(els->node, -1);
	ocs_assert(els->node->ocs, -1);

	ocs = els->node->ocs;

	if (status != 0) {
		ocs_log_warn(ocs, "status x%x ext x%x\n", status, ext_status);
	}

	/* now free the abort IO */
	ocs_io_free(ocs, abort_io);

	/* send completion event to indicate abort process is complete
	 * Note: The ELS SM will already be receiving ELS_REQ_OK/FAIL/RJT/ABORTED
	 */
	ocs_els_post_event(els, OCS_EVT_ELS_ABORT_CMPL, NULL);

	/* done with ELS IO to abort */
	ocs_ref_put(&els->ref); /* ocs_ref_get(): ocs_els_abort_io() */
	return 0;
}

/**
 * @brief Abort an ELS IO.
 *
 * <h3 class="desc">Description</h3>
 * The ELS IO is aborted by making a HW abort IO request,
 * optionally requesting that an ABTS is sent.
 *
 * \b Note: This function allocates a HW IO, and associates the HW IO
 * with the ELS IO that it is aborting. It does not associate
 * the HW IO with the node directly, like for ELS requests. The
 * abort completion is propagated up to the node once the
 * original WQE and the abort WQE are complete (the original WQE
 * completion is not propagated up to node).
 *
 * @param els Pointer to the ELS IO.
 * @param send_abts Boolean to indicate if hardware will automatically generate an ABTS.
 *
 * @return Returns pointer to Abort IO object, or NULL if error.
 */

static ocs_io_t *
ocs_els_abort_io(ocs_io_t *els, int send_abts)
{
	ocs_t *ocs;
	ocs_xport_t *xport;
	int32_t rc;
	ocs_io_t *abort_io = NULL;

	ocs_assert(els, NULL);
	ocs_assert(els->node, NULL);
	ocs_assert(els->node->ocs, NULL);

	ocs = els->node->ocs;
	ocs_assert(ocs->xport, NULL);
	xport = ocs->xport;

	/* take a reference on IO being aborted */
	if ((ocs_ref_get_unless_zero(&els->ref) == 0)) {
		/* command no longer active */
		ocs_log_debug(ocs, "els no longer active\n");
		return NULL;
	}

	/* allocate IO structure to send abort */
	abort_io = ocs_io_alloc(ocs);
	if (abort_io == NULL) {
		ocs_atomic_add_return(&xport->io_alloc_failed_count, 1);
	} else {
		ocs_assert(abort_io->hio == NULL, NULL);

		/* set generic fields */
		abort_io->ocs = ocs;
		abort_io->node = els->node;
		abort_io->cmd_ini = TRUE;

		/* set type and ABORT-specific fields */
		abort_io->io_type = OCS_IO_TYPE_ABORT;
		abort_io->display_name = "abort_els";
		abort_io->io_to_abort = els;
		abort_io->send_abts = send_abts;

		/* now dispatch IO */
		if ((rc = ocs_scsi_io_dispatch_abort(abort_io, ocs_els_abort_cb))) {
			ocs_log_err(ocs, "ocs_scsi_io_dispatch failed: %d\n", rc);
			ocs_io_free(ocs, abort_io);
			abort_io = NULL;
		}
	}

	/* if something failed, put reference on ELS to abort */
	if (abort_io == NULL) {
		ocs_ref_put(&els->ref); /* ocs_ref_get(): same function */
	}
	return abort_io;
}


/*
 * ELS IO State Machine
 */

#define std_els_state_decl(...) \
	ocs_io_t *els = NULL; \
	ocs_node_t *node = NULL; \
	ocs_t *ocs = NULL; \
	ocs_assert(ctx != NULL, NULL); \
	els = ctx->app; \
	ocs_assert(els != NULL, NULL); \
	node = els->node; \
	ocs_assert(node != NULL, NULL); \
	ocs = node->ocs; \
	ocs_assert(ocs != NULL, NULL);

#define els_sm_trace(...) \
	do { \
		if (OCS_LOG_ENABLE_ELS_TRACE(ocs)) \
			ocs_log_info(ocs, "[%s] %-8s %-20s %-20s\n", node->display_name, els->display_name, \
				__func__, ocs_sm_event_name(evt)); \
	} while (0)


/**
 * @brief Cleanup an ELS IO
 *
 * <h3 class="desc">Description</h3>
 * Cleans up an ELS IO by posting the requested event to the owning node object;
 * invoking the callback, if one is provided; and then freeing the
 * ELS IO object.
 *
 * @param els Pointer to the ELS IO.
 * @param node_evt Node SM event to post.
 * @param arg Node SM event argument.
 *
 * @return None.
 */

void
ocs_els_io_cleanup(ocs_io_t *els, ocs_sm_event_t node_evt, void *arg)
{
	ocs_assert(els);

	/* don't want further events that could come; e.g. abort requests
	 * from the node state machine; thus, disable state machine
	 */
	ocs_sm_disable(&els->els_sm);
	ocs_node_post_event(els->node, node_evt, arg);

	/* If this IO has a callback, invoke it */
	if (els->els_callback) {
		(*els->els_callback)(els->node, arg, els->els_callback_arg);
	}
	els->els_req_free = 1;
}


/**
 * @brief Common event handler for the ELS IO state machine.
 *
 * <h3 class="desc">Description</h3>
 * Provide handler for events for which default actions are desired.
 *
 * @param funcname Name of the calling function (for logging).
 * @param ctx Remote node SM context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_els_common(const char *funcname, ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_els_state_decl();

	switch(evt) {
	case OCS_EVT_ENTER:
	case OCS_EVT_REENTER:
	case OCS_EVT_EXIT:
		break;

	/* If ELS_REQ_FAIL is not handled in state, then we'll terminate this ELS and
	 * pass the event to the node
	 */
	case OCS_EVT_SRRS_ELS_REQ_FAIL:
		ocs_log_warn(els->node->ocs, "[%s] %-20s %-20s not handled - terminating ELS\n", node->display_name, funcname,
			ocs_sm_event_name(evt));
		ocs_els_io_cleanup(els, OCS_EVT_SRRS_ELS_REQ_FAIL, arg);
		break;
	default:
		ocs_log_warn(els->node->ocs, "[%s] %-20s %-20s not handled\n", node->display_name, funcname,
			ocs_sm_event_name(evt));
		break;
	}
	return NULL;
}

/**
 * @brief Initial ELS IO state
 *
 * <h3 class="desc">Description</h3>
 * This is the initial ELS IO state. Upon entry, the requested ELS/CT is submitted to
 * the hardware.
 *
 * @param ctx Remote node SM context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_els_init(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	int32_t rc = 0;
	std_els_state_decl();

	els_sm_trace();

	switch(evt) {
	case OCS_EVT_ENTER: {
		rc = ocs_els_send(els, els->els_req.size, els->els_timeout_sec, ocs_els_req_cb);
		if (rc) {
			ocs_node_cb_t cbdata;
			cbdata.status = cbdata.ext_status = (~0);
			cbdata.els = els;
			ocs_log_err(ocs, "ocs_els_send failed: %d\n", rc);
			ocs_els_io_cleanup(els, OCS_EVT_SRRS_ELS_REQ_FAIL, &cbdata);
		} else {
			ocs_io_transition(els, __ocs_els_wait_resp, NULL);
		}
		break;
	}
	default:
		__ocs_els_common(__func__, ctx, evt, arg);
		break;
	}

	return NULL;
}

/**
 * @brief Wait for the ELS request to complete.
 *
 * <h3 class="desc">Description</h3>
 * This is the ELS IO state that waits for the submitted ELS event to complete.
 * If an error completion event is received, the requested ELS is aborted.
 *
 * @param ctx Remote node SM context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_els_wait_resp(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	ocs_io_t *io;
	std_els_state_decl();

	els_sm_trace();

	switch(evt) {
	case OCS_EVT_SRRS_ELS_REQ_OK: {
		ocs_els_io_cleanup(els, OCS_EVT_SRRS_ELS_REQ_OK, arg);
		break;
	}

	case OCS_EVT_SRRS_ELS_REQ_FAIL: {
		ocs_els_io_cleanup(els, OCS_EVT_SRRS_ELS_REQ_FAIL, arg);
		break;
	}

	case OCS_EVT_ELS_REQ_TIMEOUT: {
		els_io_printf(els, "Timed out, retry (%d tries remaining)\n",
				els->els_retries_remaining-1);
		ocs_io_transition(els, __ocs_els_retry, NULL);
		break;
	}

	case OCS_EVT_SRRS_ELS_REQ_RJT: {
		ocs_node_cb_t *cbdata = arg;
		uint32_t reason_code = (cbdata->ext_status >> 16) & 0xff;

		/* delay and retry if reason code is Logical Busy */
		switch (reason_code) {
		case FC_REASON_LOGICAL_BUSY:
			els->node->els_req_cnt--;
			els_io_printf(els, "LS_RJT Logical Busy response, delay and retry\n");
			ocs_io_transition(els, __ocs_els_delay_retry, NULL);
			break;
		default:
			ocs_els_io_cleanup(els, evt, arg);
			break;
		}
		break;
	}

	case OCS_EVT_ABORT_ELS: {
		/* request to abort this ELS without an ABTS */
		els_io_printf(els, "ELS abort requested\n");
		els->els_retries_remaining = 0;		/* Set retries to zero, we are done */
		io = ocs_els_abort_io(els, FALSE);
		if (io == NULL) {
			ocs_log_err(ocs, "ocs_els_send failed\n");
			ocs_els_io_cleanup(els, OCS_EVT_SRRS_ELS_REQ_FAIL, arg);
		} else {
			ocs_io_transition(els, __ocs_els_aborting, NULL);
		}
		break;
	}

	default:
		__ocs_els_common(__func__, ctx, evt, arg);
		break;
	}
	return NULL;
}

/**
 * @brief Wait for the ELS IO abort request to complete, and retry the ELS.
 *
 * <h3 class="desc">Description</h3>
 * This state is entered when waiting for an abort of an ELS
 * request to complete so the request can be retried.
 *
 * @param ctx Remote node SM context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_els_retry(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	int32_t rc = 0;
	std_els_state_decl();

	els_sm_trace();

	switch(evt) {
	case OCS_EVT_ENTER: {
		/* handle event for ABORT_XRI WQE
		 * once abort is complete, retry if retries left;
		 * don't need to wait for OCS_EVT_SRRS_ELS_REQ_* event because we got
		 * by receiving OCS_EVT_ELS_REQ_TIMEOUT
		 */
		ocs_node_cb_t node_cbdata;
		node_cbdata.status = node_cbdata.ext_status = (~0);
		node_cbdata.els = els;
		if (els->els_retries_remaining && --els->els_retries_remaining) {
			/* Use a different XRI for the retry (would like a new oxid),
			 * so free the HW IO (dispatch will allocate a new one). It's an
			 * optimization to only free the HW IO here and not the ocs_io_t;
			 * Freeing the ocs_io_t object would require copying all the necessary
			 * info from the old ocs_io_t object to the * new one; and allocating
			 * a new ocs_io_t could fail.
			 */
			ocs_assert(els->hio, NULL);
			ocs_hw_io_free(&ocs->hw, els->hio);
			els->hio = NULL;

			/* result isn't propagated up to node sm, need to decrement req cnt */
			ocs_assert(els->node->els_req_cnt, NULL);
			els->node->els_req_cnt--;
			rc = ocs_els_send(els, els->els_req.size, els->els_timeout_sec, ocs_els_req_cb);
			if (rc) {
				ocs_log_err(ocs, "ocs_els_send failed: %d\n", rc);
				ocs_els_io_cleanup(els, OCS_EVT_SRRS_ELS_REQ_FAIL, &node_cbdata);
			}
			ocs_io_transition(els, __ocs_els_wait_resp, NULL);
		} else {
			els_io_printf(els, "Retries exhausted\n");
			ocs_els_io_cleanup(els, OCS_EVT_SRRS_ELS_REQ_FAIL, &node_cbdata);
		}
		break;
	}

	default:
		__ocs_els_common(__func__, ctx, evt, arg);
		break;
	}
	return NULL;
}

/**
 * @brief Wait for a retry timer to expire having received an abort request
 *
 * <h3 class="desc">Description</h3>
 * This state is entered when waiting for a timer event, after having received
 * an abort request, to avoid a race condition with the timer handler
 *
 * @param ctx Remote node SM context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */
void *
__ocs_els_aborted_delay_retry(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_els_state_decl();

	els_sm_trace();

	switch(evt) {
	case OCS_EVT_ENTER:
		/* mod/resched the timer for a short duration */
		ocs_mod_timer(&els->delay_timer, 1);
		break;
	case OCS_EVT_TIMER_EXPIRED:
		/* Cancel the timer, skip post node event, and free the io */
		node->els_req_cnt++;
		ocs_els_io_cleanup(els, OCS_EVT_SRRS_ELS_REQ_FAIL, arg);
		break;
	default:
		__ocs_els_common(__func__, ctx, evt, arg);
		break;
	}
	return NULL;
}

/**
 * @brief Wait for a retry timer to expire
 *
 * <h3 class="desc">Description</h3>
 * This state is entered when waiting for a timer event, so that
 * the ELS request can be retried.
 *
 * @param ctx Remote node SM context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */
void *
__ocs_els_delay_retry(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_els_state_decl();

	els_sm_trace();

	switch(evt) {
	case OCS_EVT_ENTER:
		ocs_setup_timer(ocs, &els->delay_timer, ocs_els_delay_timer_cb, els, 5000);
		break;
	case OCS_EVT_TIMER_EXPIRED:
		/* Retry delay timer expired, retry the ELS request, Free the HW IO so
		 * that a new oxid is used.
		 */
		if (els->hio != NULL) {
			ocs_hw_io_free(&ocs->hw, els->hio);
			els->hio = NULL;
		}
		ocs_io_transition(els, __ocs_els_init, NULL);
		break;
	case OCS_EVT_ABORT_ELS:
		ocs_io_transition(els, __ocs_els_aborted_delay_retry, NULL);
		break;
	default:
		__ocs_els_common(__func__, ctx, evt, arg);
		break;
	}
	return NULL;
}

/**
 * @brief Wait for the ELS IO abort request to complete.
 *
 * <h3 class="desc">Description</h3>
 * This state is entered after we abort an ELS WQE and are
 * waiting for either the original ELS WQE request or the abort
 * to complete.
 *
 * @param ctx Remote node SM context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_els_aborting(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_els_state_decl();

	els_sm_trace();

	switch(evt) {
	case OCS_EVT_SRRS_ELS_REQ_OK:
	case OCS_EVT_SRRS_ELS_REQ_FAIL:
	case OCS_EVT_SRRS_ELS_REQ_RJT:
	case OCS_EVT_ELS_REQ_TIMEOUT:
	case OCS_EVT_ELS_REQ_ABORTED: {
		/* completion for ELS received first, transition to wait for abort cmpl */
		els_io_printf(els, "request cmpl evt=%s\n", ocs_sm_event_name(evt));
		ocs_io_transition(els, __ocs_els_aborting_wait_abort_cmpl, NULL);
		break;
	}
	case OCS_EVT_ELS_ABORT_CMPL: {
		/* completion for abort was received first, transition to wait for req cmpl */
		els_io_printf(els, "abort cmpl evt=%s\n", ocs_sm_event_name(evt));
		ocs_io_transition(els, __ocs_els_aborting_wait_req_cmpl, NULL);
		break;
	}
	case OCS_EVT_ABORT_ELS:
		/* nothing we can do but wait */
		break;

	default:
		__ocs_els_common(__func__, ctx, evt, arg);
		break;
	}
	return NULL;
}

/**
 * @brief cleanup ELS after abort
 *
 * @param els ELS IO to cleanup
 *
 * @return Returns None.
 */

static void
ocs_els_abort_cleanup(ocs_io_t *els)
{
	/* handle event for ABORT_WQE
	 * whatever state ELS happened to be in, propagate aborted event up
	 * to node state machine in lieu of OCS_EVT_SRRS_ELS_* event
	 */
	ocs_node_cb_t cbdata;
	cbdata.status = cbdata.ext_status = 0;
	cbdata.els = els;
	els_io_printf(els, "Request aborted\n");
	ocs_els_io_cleanup(els, OCS_EVT_ELS_REQ_ABORTED, &cbdata);
}

/**
 * @brief Wait for the ELS IO abort request to complete.
 *
 * <h3 class="desc">Description</h3>
 * This state is entered after we abort an ELS WQE, we received
 * the abort completion first and are waiting for the original
 * ELS WQE request to complete.
 *
 * @param ctx Remote node SM context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_els_aborting_wait_req_cmpl(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_els_state_decl();

	els_sm_trace();

	switch(evt) {
	case OCS_EVT_SRRS_ELS_REQ_OK:
	case OCS_EVT_SRRS_ELS_REQ_FAIL:
	case OCS_EVT_SRRS_ELS_REQ_RJT:
	case OCS_EVT_ELS_REQ_TIMEOUT:
	case OCS_EVT_ELS_REQ_ABORTED: {
		/* completion for ELS that was aborted */
		ocs_els_abort_cleanup(els);
		break;
	}
	case OCS_EVT_ABORT_ELS:
		/* nothing we can do but wait */
		break;

	default:
		__ocs_els_common(__func__, ctx, evt, arg);
		break;
	}
	return NULL;
}

/**
 * @brief Wait for the ELS IO abort request to complete.
 *
 * <h3 class="desc">Description</h3>
 * This state is entered after we abort an ELS WQE, we received
 * the original ELS WQE request completion first and are waiting
 * for the abort to complete.
 *
 * @param ctx Remote node SM context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_els_aborting_wait_abort_cmpl(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_els_state_decl();

	els_sm_trace();

	switch(evt) {
	case OCS_EVT_ELS_ABORT_CMPL: {
		ocs_els_abort_cleanup(els);
		break;
	}
	case OCS_EVT_ABORT_ELS:
		/* nothing we can do but wait */
		break;

	default:
		__ocs_els_common(__func__, ctx, evt, arg);
		break;
	}
	return NULL;
}

/**
 * @brief Generate ELS context ddump data.
 *
 * <h3 class="desc">Description</h3>
 * Generate the ddump data for an ELS context.
 *
 * @param textbuf Pointer to the text buffer.
 * @param els Pointer to the ELS context.
 *
 * @return None.
 */

void
ocs_ddump_els(ocs_textbuf_t *textbuf, ocs_io_t *els)
{
	ocs_ddump_section(textbuf, "els", -1);
	ocs_ddump_value(textbuf, "req_free", "%d", els->els_req_free);
	ocs_ddump_value(textbuf, "evtdepth", "%d", els->els_evtdepth);
	ocs_ddump_value(textbuf, "pend", "%d", els->els_pend);
	ocs_ddump_value(textbuf, "active", "%d", els->els_active);
	ocs_ddump_io(textbuf, els);
	ocs_ddump_endsection(textbuf, "els", -1);
}


/**
 * @brief return TRUE if given ELS list is empty (while taking proper locks)
 *
 * Test if given ELS list is empty while holding the node->active_ios_lock.
 *
 * @param node pointer to node object
 * @param list pointer to list
 *
 * @return TRUE if els_io_list is empty
 */

int32_t
ocs_els_io_list_empty(ocs_node_t *node, ocs_list_t *list)
{
	int empty;
	ocs_lock(&node->active_ios_lock);
		empty = ocs_list_empty(list);
	ocs_unlock(&node->active_ios_lock);
	return empty;
}

/**
 * @brief Handle CT send response completion
 *
 * Called when CT response completes, free IO
 *
 * @param hio Pointer to the HW IO context that completed.
 * @param rnode Pointer to the remote node.
 * @param length Length of the returned payload data.
 * @param status Status of the completion.
 * @param ext_status Extended status of the completion.
 * @param arg Application-specific argument (generally a pointer to the ELS IO context).
 *
 * @return returns 0
 */
static int32_t
ocs_ct_acc_cb(ocs_hw_io_t *hio, ocs_remote_node_t *rnode, uint32_t length, int32_t status, uint32_t ext_status, void *arg)
{
	ocs_io_t *io = arg;

	ocs_els_io_free(io);

	return 0;
}

/**
 * @brief Send CT response
 *
 * Sends a CT response frame with payload
 *
 * @param io Pointer to the IO context.
 * @param ox_id Originator exchange ID
 * @param ct_hdr Pointer to the CT IU
 * @param cmd_rsp_code CT response code
 * @param reason_code Reason code
 * @param reason_code_explanation Reason code explanation
 *
 * @return returns 0 for success, a negative error code value for failure.
 */
int32_t
ocs_send_ct_rsp(ocs_io_t *io, uint32_t ox_id, fcct_iu_header_t *ct_hdr, uint32_t cmd_rsp_code, uint32_t reason_code, uint32_t reason_code_explanation)
{
	fcct_iu_header_t *rsp = io->els_rsp.virt;

	io->io_type = OCS_IO_TYPE_CT_RESP;

	*rsp = *ct_hdr;

	fcct_build_req_header(rsp, cmd_rsp_code, 0);
	rsp->reason_code = reason_code;
	rsp->reason_code_explanation = reason_code_explanation;

	io->display_name = "ct response";
	io->init_task_tag = ox_id;
	io->wire_len += sizeof(*rsp);

	ocs_memset(&io->iparam, 0, sizeof(io->iparam));

	io->io_type = OCS_IO_TYPE_CT_RESP;
	io->hio_type = OCS_HW_FC_CT_RSP;
	io->iparam.fc_ct_rsp.ox_id = ocs_htobe16(ox_id);
	io->iparam.fc_ct_rsp.r_ctl = 3;
	io->iparam.fc_ct_rsp.type = FC_TYPE_GS;
	io->iparam.fc_ct_rsp.df_ctl = 0;
	io->iparam.fc_ct_rsp.timeout = 5;

	if (ocs_scsi_io_dispatch(io, ocs_ct_acc_cb) < 0) {
		ocs_els_io_free(io);
		return -1;
	}
	return 0;
}


/**
 * @brief Handle delay retry timeout
 *
 * Callback is invoked when the delay retry timer expires.
 *
 * @param arg pointer to the ELS IO object
 *
 * @return none
 */
static void
ocs_els_delay_timer_cb(void *arg)
{
	ocs_io_t *els = arg;
	ocs_node_t *node = els->node;

	/*
	 * There is a potential deadlock here since is Linux executes timers
	 * in a soft IRQ context. The lock may be aready locked by the interrupt
	 * thread. Handle this case by attempting to take the node lock and reset the
	 * timer if we fail to acquire the lock.
	 *
	 * Note: This code relies on the fact that the node lock is recursive.
	 */
	if (ocs_node_lock_try(node)) {
		ocs_els_post_event(els, OCS_EVT_TIMER_EXPIRED, NULL);
		ocs_node_unlock(node);
	} else {
		ocs_setup_timer(els->ocs, &els->delay_timer, ocs_els_delay_timer_cb, els, 1);
	}
}
