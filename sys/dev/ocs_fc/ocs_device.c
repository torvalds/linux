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
 * Implement remote device state machine for target and initiator.
 */

/*!
@defgroup device_sm Node State Machine: Remote Device States
*/

#include "ocs.h"
#include "ocs_device.h"
#include "ocs_fabric.h"
#include "ocs_els.h"

static void *__ocs_d_common(const char *funcname, ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
static void *__ocs_d_wait_del_node(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
static void *__ocs_d_wait_del_ini_tgt(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
static int32_t ocs_process_abts(ocs_io_t *io, fc_header_t *hdr);

/**
 * @ingroup device_sm
 * @brief Send response to PRLI.
 *
 * <h3 class="desc">Description</h3>
 * For device nodes, this function sends a PRLI response.
 *
 * @param io Pointer to a SCSI IO object.
 * @param ox_id OX_ID of PRLI
 *
 * @return Returns None.
 */

void
ocs_d_send_prli_rsp(ocs_io_t *io, uint16_t ox_id)
{
	ocs_t *ocs = io->ocs;
	ocs_node_t *node = io->node;

	/* If the back-end doesn't support the fc-type, we send an LS_RJT */
	if (ocs->fc_type != node->fc_type) {
		node_printf(node, "PRLI rejected by target-server, fc-type not supported\n");
		ocs_send_ls_rjt(io, ox_id, FC_REASON_UNABLE_TO_PERFORM,
				FC_EXPL_REQUEST_NOT_SUPPORTED, 0, NULL, NULL);
		node->shutdown_reason = OCS_NODE_SHUTDOWN_DEFAULT;
		ocs_node_transition(node, __ocs_d_initiate_shutdown, NULL);
	}

	/* If the back-end doesn't want to talk to this initiator, we send an LS_RJT */
	if (node->sport->enable_tgt && (ocs_scsi_validate_initiator(node) == 0)) {
		node_printf(node, "PRLI rejected by target-server\n");
		ocs_send_ls_rjt(io, ox_id, FC_REASON_UNABLE_TO_PERFORM,
				FC_EXPL_NO_ADDITIONAL, 0, NULL, NULL);
		node->shutdown_reason = OCS_NODE_SHUTDOWN_DEFAULT;
		ocs_node_transition(node, __ocs_d_initiate_shutdown, NULL);
	} else {
		/*sm:  process PRLI payload, send PRLI acc */
		ocs_send_prli_acc(io, ox_id, ocs->fc_type, NULL, NULL);

		/* Immediately go to ready state to avoid window where we're
		 * waiting for the PRLI LS_ACC to complete while holding FCP_CMNDs
		 */
		ocs_node_transition(node, __ocs_d_device_ready, NULL);
	}
}

/**
 * @ingroup device_sm
 * @brief Device node state machine: Initiate node shutdown
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_d_initiate_shutdown(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_ENTER: {
		int32_t rc = OCS_SCSI_CALL_COMPLETE; /* assume no wait needed */

		ocs_scsi_io_alloc_disable(node);

		/* make necessary delete upcall(s) */
		if (node->init && !node->targ) {
			ocs_log_debug(node->ocs,
				"[%s] delete (initiator) WWPN %s WWNN %s\n",
				node->display_name, node->wwpn, node->wwnn);
			ocs_node_transition(node, __ocs_d_wait_del_node, NULL);
			if (node->sport->enable_tgt) {
				rc = ocs_scsi_del_initiator(node,
						OCS_SCSI_INITIATOR_DELETED);
			}
			if (rc == OCS_SCSI_CALL_COMPLETE) {
				ocs_node_post_event(node,
					OCS_EVT_NODE_DEL_INI_COMPLETE, NULL);
			}
		} else if (node->targ && !node->init) {
			ocs_log_debug(node->ocs,
				"[%s] delete (target)    WWPN %s WWNN %s\n",
				node->display_name, node->wwpn, node->wwnn);
			ocs_node_transition(node, __ocs_d_wait_del_node, NULL);
			if (node->sport->enable_ini) {
				rc = ocs_scsi_del_target(node,
						OCS_SCSI_TARGET_DELETED);
			}
			if (rc == OCS_SCSI_CALL_COMPLETE) {
				ocs_node_post_event(node,
					OCS_EVT_NODE_DEL_TGT_COMPLETE, NULL);
			}
		} else if (node->init && node->targ) {
			ocs_log_debug(node->ocs,
				"[%s] delete (initiator+target) WWPN %s WWNN %s\n",
				node->display_name, node->wwpn, node->wwnn);
			ocs_node_transition(node, __ocs_d_wait_del_ini_tgt, NULL);
			if (node->sport->enable_tgt) {
				rc = ocs_scsi_del_initiator(node,
						OCS_SCSI_INITIATOR_DELETED);
			}
			if (rc == OCS_SCSI_CALL_COMPLETE) {
				ocs_node_post_event(node,
					OCS_EVT_NODE_DEL_INI_COMPLETE, NULL);
			}
			rc = OCS_SCSI_CALL_COMPLETE; /* assume no wait needed */
			if (node->sport->enable_ini) {
				rc = ocs_scsi_del_target(node,
						OCS_SCSI_TARGET_DELETED);
			}
			if (rc == OCS_SCSI_CALL_COMPLETE) {
				ocs_node_post_event(node,
					OCS_EVT_NODE_DEL_TGT_COMPLETE, NULL);
			}
		}

		/* we've initiated the upcalls as needed, now kick off the node
		 * detach to precipitate the aborting of outstanding exchanges
		 * associated with said node
		 *
		 * Beware: if we've made upcall(s), we've already transitioned
		 * to a new state by the time we execute this.
		 * TODO: consider doing this before the upcalls...
		 */
		if (node->attached) {
			/* issue hw node free; don't care if succeeds right away
			 * or sometime later, will check node->attached later in
			 * shutdown process
			 */
			rc = ocs_hw_node_detach(&ocs->hw, &node->rnode);
			if (node->rnode.free_group) {
				ocs_remote_node_group_free(node->node_group);
				node->node_group = NULL;
				node->rnode.free_group = FALSE;
			}
			if (rc != OCS_HW_RTN_SUCCESS &&
				rc != OCS_HW_RTN_SUCCESS_SYNC) {
				node_printf(node,
					"Failed freeing HW node, rc=%d\n", rc);
			}
		}

		/* if neither initiator nor target, proceed to cleanup */
		if (!node->init && !node->targ){
			/*
			 * node has either been detached or is in the process
			 * of being detached, call common node's initiate
			 * cleanup function.
			 */
			ocs_node_initiate_cleanup(node);
		}
		break;
	}
	case OCS_EVT_ALL_CHILD_NODES_FREE:
		/* Ignore, this can happen if an ELS is aborted,
		 * while in a delay/retry state */
		break;
	default:
		__ocs_d_common(__func__, ctx, evt, arg);
		return NULL;
	}
	return NULL;
}

/**
 * @ingroup device_sm
 * @brief Device node state machine: Common device event handler.
 *
 * <h3 class="desc">Description</h3>
 * For device nodes, this event handler manages default and common events.
 *
 * @param funcname Function name text.
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

static void *
__ocs_d_common(const char *funcname, ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	ocs_node_t *node = NULL;
	ocs_t *ocs = NULL;
	ocs_assert(ctx, NULL);
	node = ctx->app;
	ocs_assert(node, NULL);
	ocs = node->ocs;
	ocs_assert(ocs, NULL);

	switch(evt) {

	/* Handle shutdown events */
	case OCS_EVT_SHUTDOWN:
		ocs_log_debug(ocs, "[%s] %-20s %-20s\n", node->display_name, funcname, ocs_sm_event_name(evt));
		node->shutdown_reason = OCS_NODE_SHUTDOWN_DEFAULT;
		ocs_node_transition(node, __ocs_d_initiate_shutdown, NULL);
		break;
	case OCS_EVT_SHUTDOWN_EXPLICIT_LOGO:
		ocs_log_debug(ocs, "[%s] %-20s %-20s\n", node->display_name, funcname, ocs_sm_event_name(evt));
		node->shutdown_reason = OCS_NODE_SHUTDOWN_EXPLICIT_LOGO;
		ocs_node_transition(node, __ocs_d_initiate_shutdown, NULL);
		break;
	case OCS_EVT_SHUTDOWN_IMPLICIT_LOGO:
		ocs_log_debug(ocs, "[%s] %-20s %-20s\n", node->display_name, funcname, ocs_sm_event_name(evt));
		node->shutdown_reason = OCS_NODE_SHUTDOWN_IMPLICIT_LOGO;
		ocs_node_transition(node, __ocs_d_initiate_shutdown, NULL);
		break;

	default:
		/* call default event handler common to all nodes */
		__ocs_node_common(funcname, ctx, evt, arg);
		break;
	}
	return NULL;
}

/**
 * @ingroup device_sm
 * @brief Device node state machine: Wait for a domain-attach completion in loop topology.
 *
 * <h3 class="desc">Description</h3>
 * State waits for a domain-attached completion while in loop topology.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_d_wait_loop(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_ENTER:
		ocs_node_hold_frames(node);
		break;

	case OCS_EVT_EXIT:
		ocs_node_accept_frames(node);
		break;

	case OCS_EVT_DOMAIN_ATTACH_OK: {
		/* send PLOGI automatically if initiator */
		ocs_node_init_device(node, TRUE);
		break;
	}
	default:
		__ocs_d_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}




/**
 * @ingroup device_sm
 * @brief state: wait for node resume event
 *
 * State is entered when a node is in I+T mode and sends a delete initiator/target
 * call to the target-server/initiator-client and needs to wait for that work to complete.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg per event optional argument
 *
 * @return returns NULL
 */

void *
__ocs_d_wait_del_ini_tgt(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_ENTER:
		ocs_node_hold_frames(node);
		/* Fall through */

	case OCS_EVT_NODE_ACTIVE_IO_LIST_EMPTY:
	case OCS_EVT_ALL_CHILD_NODES_FREE:
		/* These are expected events. */
		break;

	case OCS_EVT_NODE_DEL_INI_COMPLETE:
	case OCS_EVT_NODE_DEL_TGT_COMPLETE:
		ocs_node_transition(node, __ocs_d_wait_del_node, NULL);
		break;

	case OCS_EVT_EXIT:
		ocs_node_accept_frames(node);
		break;

	case OCS_EVT_SRRS_ELS_REQ_FAIL:
		/* Can happen as ELS IO IO's complete */
		ocs_assert(node->els_req_cnt, NULL);
		node->els_req_cnt--;
		break;

	/* ignore shutdown events as we're already in shutdown path */
	case OCS_EVT_SHUTDOWN:
		/* have default shutdown event take precedence */
		node->shutdown_reason = OCS_NODE_SHUTDOWN_DEFAULT;
		/* fall through */
	case OCS_EVT_SHUTDOWN_EXPLICIT_LOGO:
	case OCS_EVT_SHUTDOWN_IMPLICIT_LOGO:
		node_printf(node, "%s received\n", ocs_sm_event_name(evt));
		break;
	case OCS_EVT_DOMAIN_ATTACH_OK:
		/* don't care about domain_attach_ok */
		break;
	default:
		__ocs_d_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}


/**
 * @ingroup device_sm
 * @brief state: Wait for node resume event.
 *
 * State is entered when a node sends a delete initiator/target call to the
 * target-server/initiator-client and needs to wait for that work to complete.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_d_wait_del_node(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_ENTER:
		ocs_node_hold_frames(node);
		/* Fall through */

	case OCS_EVT_NODE_ACTIVE_IO_LIST_EMPTY:
	case OCS_EVT_ALL_CHILD_NODES_FREE:
		/* These are expected events. */
		break;

	case OCS_EVT_NODE_DEL_INI_COMPLETE:
	case OCS_EVT_NODE_DEL_TGT_COMPLETE:
		/*
		 * node has either been detached or is in the process of being detached,
		 * call common node's initiate cleanup function
		 */
		ocs_node_initiate_cleanup(node);
		break;

	case OCS_EVT_EXIT:
		ocs_node_accept_frames(node);
		break;

	case OCS_EVT_SRRS_ELS_REQ_FAIL:
		/* Can happen as ELS IO IO's complete */
		ocs_assert(node->els_req_cnt, NULL);
		node->els_req_cnt--;
		break;

	/* ignore shutdown events as we're already in shutdown path */
	case OCS_EVT_SHUTDOWN:
		/* have default shutdown event take precedence */
		node->shutdown_reason = OCS_NODE_SHUTDOWN_DEFAULT;
		/* fall through */
	case OCS_EVT_SHUTDOWN_EXPLICIT_LOGO:
	case OCS_EVT_SHUTDOWN_IMPLICIT_LOGO:
		node_printf(node, "%s received\n", ocs_sm_event_name(evt));
		break;
	case OCS_EVT_DOMAIN_ATTACH_OK:
		/* don't care about domain_attach_ok */
		break;
	default:
		__ocs_d_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}



/**
 * @brief Save the OX_ID for sending LS_ACC sometime later.
 *
 * <h3 class="desc">Description</h3>
 * When deferring the response to an ELS request, the OX_ID of the request
 * is saved using this function.
 *
 * @param io Pointer to a SCSI IO object.
 * @param hdr Pointer to the FC header.
 * @param ls Defines the type of ELS to send: LS_ACC, LS_ACC for PLOGI;
 * or LSS_ACC for PRLI.
 *
 * @return None.
 */

void
ocs_send_ls_acc_after_attach(ocs_io_t *io, fc_header_t *hdr, ocs_node_send_ls_acc_e ls)
{
	ocs_node_t *node = io->node;
	uint16_t ox_id = ocs_be16toh(hdr->ox_id);

	ocs_assert(node->send_ls_acc == OCS_NODE_SEND_LS_ACC_NONE);

	node->ls_acc_oxid = ox_id;
	node->send_ls_acc = ls;
	node->ls_acc_io = io;
	node->ls_acc_did = fc_be24toh(hdr->d_id);
}

/**
 * @brief Process the PRLI payload.
 *
 * <h3 class="desc">Description</h3>
 * The PRLI payload is processed; the initiator/target capabilities of the
 * remote node are extracted and saved in the node object.
 *
 * @param node Pointer to the node object.
 * @param prli Pointer to the PRLI payload.
 *
 * @return None.
 */

void
ocs_process_prli_payload(ocs_node_t *node, fc_prli_payload_t *prli)
{
	node->init = (ocs_be16toh(prli->service_params) & FC_PRLI_INITIATOR_FUNCTION) != 0;
	node->targ = (ocs_be16toh(prli->service_params) & FC_PRLI_TARGET_FUNCTION) != 0;
	node->fcp2device = (ocs_be16toh(prli->service_params) & FC_PRLI_RETRY) != 0;
	node->fc_type = prli->type;
}

/**
 * @brief Process the ABTS.
 *
 * <h3 class="desc">Description</h3>
 * Common code to process a received ABTS. If an active IO can be found
 * that matches the OX_ID of the ABTS request, a call is made to the
 * backend. Otherwise, a BA_ACC is returned to the initiator.
 *
 * @param io Pointer to a SCSI IO object.
 * @param hdr Pointer to the FC header.
 *
 * @return Returns 0 on success, or a negative error value on failure.
 */

static int32_t
ocs_process_abts(ocs_io_t *io, fc_header_t *hdr)
{
	ocs_node_t *node = io->node;
	ocs_t *ocs = node->ocs;
	uint16_t ox_id = ocs_be16toh(hdr->ox_id);
	uint16_t rx_id = ocs_be16toh(hdr->rx_id);
	ocs_io_t *abortio;

	abortio = ocs_io_find_tgt_io(ocs, node, ox_id, rx_id);

	/* If an IO was found, attempt to take a reference on it */
	if (abortio != NULL && (ocs_ref_get_unless_zero(&abortio->ref) != 0)) {

		/* Got a reference on the IO. Hold it until backend is notified below */
		node_printf(node, "Abort request: ox_id [%04x] rx_id [%04x]\n",
			    ox_id, rx_id);

		/*
		 * Save the ox_id for the ABTS as the init_task_tag in our manufactured
		 * TMF IO object
		 */
		io->display_name = "abts";
		io->init_task_tag = ox_id;
		/* don't set tgt_task_tag, don't want to confuse with XRI */

		/*
		 * Save the rx_id from the ABTS as it is needed for the BLS response,
		 * regardless of the IO context's rx_id
		 */
		io->abort_rx_id = rx_id;

		/* Call target server command abort */
		io->tmf_cmd = OCS_SCSI_TMF_ABORT_TASK;
		ocs_scsi_recv_tmf(io, abortio->tgt_io.lun, OCS_SCSI_TMF_ABORT_TASK, abortio, 0);

		/*
		 * Backend will have taken an additional reference on the IO if needed;
		 * done with current reference.
		 */
		ocs_ref_put(&abortio->ref); /* ocs_ref_get(): same function */
	} else {
		/*
		 * Either IO was not found or it has been freed between finding it
		 * and attempting to get the reference,
		 */
		node_printf(node, "Abort request: ox_id [%04x], IO not found (exists=%d)\n",
			    ox_id, (abortio != NULL));

		/* Send a BA_ACC */
		ocs_bls_send_acc_hdr(io, hdr);
	}
	return 0;
}

/**
 * @ingroup device_sm
 * @brief Device node state machine: Wait for the PLOGI accept to complete.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_d_wait_plogi_acc_cmpl(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_ENTER:
		ocs_node_hold_frames(node);
		break;

	case OCS_EVT_EXIT:
		ocs_node_accept_frames(node);
		break;

	case OCS_EVT_SRRS_ELS_CMPL_FAIL:
		ocs_assert(node->els_cmpl_cnt, NULL);
		node->els_cmpl_cnt--;
		node->shutdown_reason = OCS_NODE_SHUTDOWN_DEFAULT;
		ocs_node_transition(node, __ocs_d_initiate_shutdown, NULL);
		break;

	case OCS_EVT_SRRS_ELS_CMPL_OK:	/* PLOGI ACC completions */
		ocs_assert(node->els_cmpl_cnt, NULL);
		node->els_cmpl_cnt--;
		ocs_node_transition(node, __ocs_d_port_logged_in, NULL);
		break;

	default:
		__ocs_d_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup device_sm
 * @brief Device node state machine: Wait for the LOGO response.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_d_wait_logo_rsp(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_ENTER:
		/* TODO: may want to remove this; 
		 * if we'll want to know about PLOGI */
		ocs_node_hold_frames(node);
		break;

	case OCS_EVT_EXIT:
		ocs_node_accept_frames(node);
		break;

	case OCS_EVT_SRRS_ELS_REQ_OK:
	case OCS_EVT_SRRS_ELS_REQ_RJT:
	case OCS_EVT_SRRS_ELS_REQ_FAIL:
		/* LOGO response received, sent shutdown */
		if (node_check_els_req(ctx, evt, arg, FC_ELS_CMD_LOGO, __ocs_d_common, __func__)) {
			return NULL;
		}
		ocs_assert(node->els_req_cnt, NULL);
		node->els_req_cnt--;
		node_printf(node, "LOGO sent (evt=%s), shutdown node\n", ocs_sm_event_name(evt));
		/* sm: post explicit logout */
		ocs_node_post_event(node, OCS_EVT_SHUTDOWN_EXPLICIT_LOGO, NULL);
		break;

	/* TODO: PLOGI: abort LOGO and process PLOGI? (SHUTDOWN_EXPLICIT/IMPLICIT_LOGO?) */

	default:
		__ocs_d_common(__func__, ctx, evt, arg);
		return NULL;
	}
	return NULL;
}


/**
 * @ingroup device_sm
 * @brief Device node state machine: Wait for the PRLO response.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_d_wait_prlo_rsp(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
		case OCS_EVT_ENTER:
			ocs_node_hold_frames(node);
			break;

		case OCS_EVT_EXIT:
			ocs_node_accept_frames(node);
			break;

		case OCS_EVT_SRRS_ELS_REQ_OK:
		case OCS_EVT_SRRS_ELS_REQ_RJT:
		case OCS_EVT_SRRS_ELS_REQ_FAIL:
			if (node_check_els_req(ctx, evt, arg, FC_ELS_CMD_PRLO, __ocs_d_common, __func__)) {
				return NULL;
			}
			ocs_assert(node->els_req_cnt, NULL);
			node->els_req_cnt--;
			node_printf(node, "PRLO sent (evt=%s)\n", ocs_sm_event_name(evt));
			ocs_node_transition(node, __ocs_d_port_logged_in, NULL);
			break;

		default:
			__ocs_node_common(__func__, ctx, evt, arg);
			return NULL;
	}
	return NULL;
}


/**
 * @brief Initialize device node.
 *
 * Initialize device node. If a node is an initiator, then send a PLOGI and transition
 * to __ocs_d_wait_plogi_rsp, otherwise transition to __ocs_d_init.
 *
 * @param node Pointer to the node object.
 * @param send_plogi Boolean indicating to send PLOGI command or not.
 *
 * @return none
 */

void
ocs_node_init_device(ocs_node_t *node, int send_plogi)
{
	node->send_plogi = send_plogi;
	if ((node->ocs->nodedb_mask & OCS_NODEDB_PAUSE_NEW_NODES) && !FC_ADDR_IS_DOMAIN_CTRL(node->rnode.fc_id)) {
		node->nodedb_state = __ocs_d_init;
		ocs_node_transition(node, __ocs_node_paused, NULL);
	} else {
		ocs_node_transition(node, __ocs_d_init, NULL);
	}
}

/**
 * @ingroup device_sm
 * @brief Device node state machine: Initial node state for an initiator or a target.
 *
 * <h3 class="desc">Description</h3>
 * This state is entered when a node is instantiated, either having been
 * discovered from a name services query, or having received a PLOGI/FLOGI.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 * - OCS_EVT_ENTER: (uint8_t *) - 1 to send a PLOGI on
 * entry (initiator-only); 0 indicates a PLOGI is
 * not sent on entry (initiator-only). Not applicable for a target.
 *
 * @return Returns NULL.
 */

void *
__ocs_d_init(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	int32_t rc;
	ocs_node_cb_t *cbdata = arg;
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_ENTER:
		/* check if we need to send PLOGI */
		if (node->send_plogi) {
			/* only send if we have initiator capability, and domain is attached */
			if (node->sport->enable_ini && node->sport->domain->attached) {
				ocs_send_plogi(node, OCS_FC_ELS_SEND_DEFAULT_TIMEOUT,
						OCS_FC_ELS_DEFAULT_RETRIES, NULL, NULL);
				ocs_node_transition(node, __ocs_d_wait_plogi_rsp, NULL);
			} else {
				node_printf(node, "not sending plogi sport.ini=%d, domain attached=%d\n",
					    node->sport->enable_ini, node->sport->domain->attached);
			}
		}
		break;
	case OCS_EVT_PLOGI_RCVD: {
		/* T, or I+T */
		fc_header_t *hdr = cbdata->header->dma.virt;
		uint32_t d_id = fc_be24toh(hdr->d_id);

		ocs_node_save_sparms(node, cbdata->payload->dma.virt);
		ocs_send_ls_acc_after_attach(cbdata->io, cbdata->header->dma.virt, OCS_NODE_SEND_LS_ACC_PLOGI);

		/* domain already attached */
		if (node->sport->domain->attached) {
			rc = ocs_node_attach(node);
			ocs_node_transition(node, __ocs_d_wait_node_attach, NULL);
			if (rc == OCS_HW_RTN_SUCCESS_SYNC) {
				ocs_node_post_event(node, OCS_EVT_NODE_ATTACH_OK, NULL);
			}
			break;
		}

		/* domain not attached; several possibilities: */
		switch (node->sport->topology) {
		case OCS_SPORT_TOPOLOGY_P2P:
			/* we're not attached and sport is p2p, need to attach */
			ocs_domain_attach(node->sport->domain, d_id);
			ocs_node_transition(node, __ocs_d_wait_domain_attach, NULL);
			break;
		case OCS_SPORT_TOPOLOGY_FABRIC:
			/* we're not attached and sport is fabric, domain attach should have
			 * already been requested as part of the fabric state machine, wait for it
			 */
			ocs_node_transition(node, __ocs_d_wait_domain_attach, NULL);
			break;
		case OCS_SPORT_TOPOLOGY_UNKNOWN:
			/* Two possibilities:
			 * 1. received a PLOGI before our FLOGI has completed (possible since
			 *    completion comes in on another CQ), thus we don't know what we're
			 *    connected to yet; transition to a state to wait for the fabric
			 *    node to tell us;
			 * 2. PLOGI received before link went down and we haven't performed
			 *    domain attach yet.
			 * Note: we cannot distinguish between 1. and 2. so have to assume PLOGI
			 * was received after link back up.
			 */
			node_printf(node, "received PLOGI, with unknown topology did=0x%x\n", d_id);
			ocs_node_transition(node, __ocs_d_wait_topology_notify, NULL);
			break;
		default:
			node_printf(node, "received PLOGI, with unexpectd topology %d\n",
				    node->sport->topology);
			ocs_assert(FALSE, NULL);
			break;
		}
		break;
	}

	case OCS_EVT_FDISC_RCVD: {
		__ocs_d_common(__func__, ctx, evt, arg);
		break;
	}

	case OCS_EVT_FLOGI_RCVD: {
		fc_header_t *hdr = cbdata->header->dma.virt;

		/* this better be coming from an NPort */
		ocs_assert(ocs_rnode_is_nport(cbdata->payload->dma.virt), NULL);

		/* sm: save sparams, send FLOGI acc */
		ocs_domain_save_sparms(node->sport->domain, cbdata->payload->dma.virt);

		/* send FC LS_ACC response, override s_id */
		ocs_fabric_set_topology(node, OCS_SPORT_TOPOLOGY_P2P);
		ocs_send_flogi_p2p_acc(cbdata->io, ocs_be16toh(hdr->ox_id), fc_be24toh(hdr->d_id), NULL, NULL);
		if (ocs_p2p_setup(node->sport)) {
			node_printf(node, "p2p setup failed, shutting down node\n");
			ocs_node_post_event(node, OCS_EVT_SHUTDOWN, NULL);
		} else {
			ocs_node_transition(node, __ocs_p2p_wait_flogi_acc_cmpl, NULL);
		}

		break;
	}

	case OCS_EVT_LOGO_RCVD: {
		fc_header_t *hdr = cbdata->header->dma.virt;

		if (!node->sport->domain->attached) {
			 /* most likely a frame left over from before a link down; drop and
			  * shut node down w/ "explicit logout" so pending frames are processed */
			node_printf(node, "%s domain not attached, dropping\n", ocs_sm_event_name(evt));
			ocs_node_post_event(node, OCS_EVT_SHUTDOWN_EXPLICIT_LOGO, NULL);
			break;
		}
		ocs_send_logo_acc(cbdata->io, ocs_be16toh(hdr->ox_id), NULL, NULL);
		ocs_node_transition(node, __ocs_d_wait_logo_acc_cmpl, NULL);
		break;
	}

	case OCS_EVT_PRLI_RCVD:
	case OCS_EVT_PRLO_RCVD:
	case OCS_EVT_PDISC_RCVD:
	case OCS_EVT_ADISC_RCVD:
	case OCS_EVT_RSCN_RCVD: {
		fc_header_t *hdr = cbdata->header->dma.virt;
		if (!node->sport->domain->attached) {
			 /* most likely a frame left over from before a link down; drop and
			  * shut node down w/ "explicit logout" so pending frames are processed */
			node_printf(node, "%s domain not attached, dropping\n", ocs_sm_event_name(evt));
			ocs_node_post_event(node, OCS_EVT_SHUTDOWN_EXPLICIT_LOGO, NULL);
			break;
		}
		node_printf(node, "%s received, sending reject\n", ocs_sm_event_name(evt));
		ocs_send_ls_rjt(cbdata->io, ocs_be16toh(hdr->ox_id),
			FC_REASON_UNABLE_TO_PERFORM, FC_EXPL_NPORT_LOGIN_REQUIRED, 0,
			NULL, NULL);

		break;
	}

	case OCS_EVT_FCP_CMD_RCVD: {
		/* note: problem, we're now expecting an ELS REQ completion 
		 * from both the LOGO and PLOGI */
		if (!node->sport->domain->attached) {
			 /* most likely a frame left over from before a link down; drop and
			  * shut node down w/ "explicit logout" so pending frames are processed */
			node_printf(node, "%s domain not attached, dropping\n", ocs_sm_event_name(evt));
			ocs_node_post_event(node, OCS_EVT_SHUTDOWN_EXPLICIT_LOGO, NULL);
			break;
		}

		/* Send LOGO */
		node_printf(node, "FCP_CMND received, send LOGO\n");
		if (ocs_send_logo(node, OCS_FC_ELS_SEND_DEFAULT_TIMEOUT, 0, NULL, NULL) == NULL) {
			/* failed to send LOGO, go ahead and cleanup node anyways */
			node_printf(node, "Failed to send LOGO\n");
			ocs_node_post_event(node, OCS_EVT_SHUTDOWN_EXPLICIT_LOGO, NULL);
		} else {
			/* sent LOGO, wait for response */
			ocs_node_transition(node, __ocs_d_wait_logo_rsp, NULL);
		}
		break;
	}
	case OCS_EVT_DOMAIN_ATTACH_OK:
		/* don't care about domain_attach_ok */
		break;

	default:
		__ocs_d_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup device_sm
 * @brief Device node state machine: Wait on a response for a sent PLOGI.
 *
 * <h3 class="desc">Description</h3>
 * State is entered when an initiator-capable node has sent
 * a PLOGI and is waiting for a response.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_d_wait_plogi_rsp(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	int32_t rc;
	ocs_node_cb_t *cbdata = arg;
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_PLOGI_RCVD: {
		/* T, or I+T */
		/* received PLOGI with svc parms, go ahead and attach node
		 * when PLOGI that was sent ultimately completes, it'll be a no-op
		 */

		/* TODO: there is an outstanding PLOGI sent, can we set a flag
		 * to indicate that we don't want to retry it if it times out? */
		ocs_node_save_sparms(node, cbdata->payload->dma.virt);
		ocs_send_ls_acc_after_attach(cbdata->io, cbdata->header->dma.virt, OCS_NODE_SEND_LS_ACC_PLOGI);
		/* sm: domain->attached / ocs_node_attach */
		rc = ocs_node_attach(node);
		ocs_node_transition(node, __ocs_d_wait_node_attach, NULL);
		if (rc == OCS_HW_RTN_SUCCESS_SYNC) {
			ocs_node_post_event(node, OCS_EVT_NODE_ATTACH_OK, NULL);
		}
		break;
	}

	case OCS_EVT_PRLI_RCVD:
		/* I, or I+T */
		/* sent PLOGI and before completion was seen, received the
		 * PRLI from the remote node (WCQEs and RCQEs come in on
		 * different queues and order of processing cannot be assumed)
		 * Save OXID so PRLI can be sent after the attach and continue
		 * to wait for PLOGI response
		 */
		ocs_process_prli_payload(node, cbdata->payload->dma.virt);
		if (ocs->fc_type == node->fc_type) {
			ocs_send_ls_acc_after_attach(cbdata->io, cbdata->header->dma.virt, OCS_NODE_SEND_LS_ACC_PRLI);
			ocs_node_transition(node, __ocs_d_wait_plogi_rsp_recvd_prli, NULL);
		} else {
			/* TODO this need to be looked at. What do we do here ? */
		}
		break;

	/* TODO this need to be looked at. we could very well be logged in */
	case OCS_EVT_LOGO_RCVD: /* why don't we do a shutdown here?? */
	case OCS_EVT_PRLO_RCVD:
	case OCS_EVT_PDISC_RCVD:
	case OCS_EVT_FDISC_RCVD:
	case OCS_EVT_ADISC_RCVD:
	case OCS_EVT_RSCN_RCVD:
	case OCS_EVT_SCR_RCVD: {
		fc_header_t *hdr = cbdata->header->dma.virt;
		node_printf(node, "%s received, sending reject\n", ocs_sm_event_name(evt));
		ocs_send_ls_rjt(cbdata->io, ocs_be16toh(hdr->ox_id),
			FC_REASON_UNABLE_TO_PERFORM, FC_EXPL_NPORT_LOGIN_REQUIRED, 0,
			NULL, NULL);

		break;
	}

	case OCS_EVT_SRRS_ELS_REQ_OK:	/* PLOGI response received */
		/* Completion from PLOGI sent */
		if (node_check_els_req(ctx, evt, arg, FC_ELS_CMD_PLOGI, __ocs_d_common, __func__)) {
			return NULL;
		}
		ocs_assert(node->els_req_cnt, NULL);
		node->els_req_cnt--;
		/* sm:  save sparams, ocs_node_attach */
		ocs_node_save_sparms(node, cbdata->els->els_rsp.virt);
		ocs_display_sparams(node->display_name, "plogi rcvd resp", 0, NULL,
			((uint8_t*)cbdata->els->els_rsp.virt) + 4);
		rc = ocs_node_attach(node);
		ocs_node_transition(node, __ocs_d_wait_node_attach, NULL);
		if (rc == OCS_HW_RTN_SUCCESS_SYNC) {
			ocs_node_post_event(node, OCS_EVT_NODE_ATTACH_OK, NULL);
		}
		break;

	case OCS_EVT_SRRS_ELS_REQ_FAIL:	/* PLOGI response received */
		/* PLOGI failed, shutdown the node */
		if (node_check_els_req(ctx, evt, arg, FC_ELS_CMD_PLOGI, __ocs_d_common, __func__)) {
			return NULL;
		}
		ocs_assert(node->els_req_cnt, NULL);
		node->els_req_cnt--;
		ocs_node_post_event(node, OCS_EVT_SHUTDOWN, NULL);
		break;

	case OCS_EVT_SRRS_ELS_REQ_RJT:	/* Our PLOGI was rejected, this is ok in some cases */
		if (node_check_els_req(ctx, evt, arg, FC_ELS_CMD_PLOGI, __ocs_d_common, __func__)) {
			return NULL;
		}
		ocs_assert(node->els_req_cnt, NULL);
		node->els_req_cnt--;
		break;

	case OCS_EVT_FCP_CMD_RCVD: {
		/* not logged in yet and outstanding PLOGI so don't send LOGO,
		 * just drop
		 */
		node_printf(node, "FCP_CMND received, drop\n");
		break;
	}

	default:
		__ocs_d_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup device_sm
 * @brief Device node state machine: Waiting on a response for a
 *	sent PLOGI.
 *
 * <h3 class="desc">Description</h3>
 * State is entered when an initiator-capable node has sent
 * a PLOGI and is waiting for a response. Before receiving the
 * response, a PRLI was received, implying that the PLOGI was
 * successful.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_d_wait_plogi_rsp_recvd_prli(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	int32_t rc;
	ocs_node_cb_t *cbdata = arg;
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_ENTER:
		/*
		 * Since we've received a PRLI, we have a port login and will
		 * just need to wait for the PLOGI response to do the node
		 * attach and then we can send the LS_ACC for the PRLI. If,
		 * during this time, we receive FCP_CMNDs (which is possible
		 * since we've already sent a PRLI and our peer may have accepted).
		 * At this time, we are not waiting on any other unsolicited
		 * frames to continue with the login process. Thus, it will not
		 * hurt to hold frames here.
		 */
		ocs_node_hold_frames(node);
		break;

	case OCS_EVT_EXIT:
		ocs_node_accept_frames(node);
		break;

	case OCS_EVT_SRRS_ELS_REQ_OK:	/* PLOGI response received */
		/* Completion from PLOGI sent */
		if (node_check_els_req(ctx, evt, arg, FC_ELS_CMD_PLOGI, __ocs_d_common, __func__)) {
			return NULL;
		}
		ocs_assert(node->els_req_cnt, NULL);
		node->els_req_cnt--;
		/* sm:  save sparams, ocs_node_attach */
		ocs_node_save_sparms(node, cbdata->els->els_rsp.virt);
		ocs_display_sparams(node->display_name, "plogi rcvd resp", 0, NULL,
			((uint8_t*)cbdata->els->els_rsp.virt) + 4);
		rc = ocs_node_attach(node);
		ocs_node_transition(node, __ocs_d_wait_node_attach, NULL);
		if (rc == OCS_HW_RTN_SUCCESS_SYNC) {
			ocs_node_post_event(node, OCS_EVT_NODE_ATTACH_OK, NULL);
		}
		break;

	case OCS_EVT_SRRS_ELS_REQ_FAIL:	/* PLOGI response received */
	case OCS_EVT_SRRS_ELS_REQ_RJT:
		/* PLOGI failed, shutdown the node */
		if (node_check_els_req(ctx, evt, arg, FC_ELS_CMD_PLOGI, __ocs_d_common, __func__)) {
			return NULL;
		}
		ocs_assert(node->els_req_cnt, NULL);
		node->els_req_cnt--;
		ocs_node_post_event(node, OCS_EVT_SHUTDOWN, NULL);
		break;

	default:
		__ocs_d_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup device_sm
 * @brief Device node state machine: Wait for a domain attach.
 *
 * <h3 class="desc">Description</h3>
 * Waits for a domain-attach complete ok event.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_d_wait_domain_attach(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	int32_t rc;
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_ENTER:
		ocs_node_hold_frames(node);
		break;

	case OCS_EVT_EXIT:
		ocs_node_accept_frames(node);
		break;

	case OCS_EVT_DOMAIN_ATTACH_OK:
		ocs_assert(node->sport->domain->attached, NULL);
		/* sm: ocs_node_attach */
		rc = ocs_node_attach(node);
		ocs_node_transition(node, __ocs_d_wait_node_attach, NULL);
		if (rc == OCS_HW_RTN_SUCCESS_SYNC) {
			ocs_node_post_event(node, OCS_EVT_NODE_ATTACH_OK, NULL);
		}
		break;

	default:
		__ocs_d_common(__func__, ctx, evt, arg);
		return NULL;
	}
	return NULL;
}

/**
 * @ingroup device_sm
 * @brief Device node state machine: Wait for topology
 *	notification
 *
 * <h3 class="desc">Description</h3>
 * Waits for topology notification from fabric node, then
 * attaches domain and node.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_d_wait_topology_notify(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	int32_t rc;
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_ENTER:
		ocs_node_hold_frames(node);
		break;

	case OCS_EVT_EXIT:
		ocs_node_accept_frames(node);
		break;

	case OCS_EVT_SPORT_TOPOLOGY_NOTIFY: {
		ocs_sport_topology_e topology = (ocs_sport_topology_e)arg;
		ocs_assert(!node->sport->domain->attached, NULL);
		ocs_assert(node->send_ls_acc == OCS_NODE_SEND_LS_ACC_PLOGI, NULL);
		node_printf(node, "topology notification, topology=%d\n", topology);

		/* At the time the PLOGI was received, the topology was unknown,
		 * so we didn't know which node would perform the domain attach:
		 * 1. The node from which the PLOGI was sent (p2p) or
		 * 2. The node to which the FLOGI was sent (fabric).
		 */
		if (topology == OCS_SPORT_TOPOLOGY_P2P) {
			/* if this is p2p, need to attach to the domain using the
			 * d_id from the PLOGI received
			 */
			ocs_domain_attach(node->sport->domain, node->ls_acc_did);
		}
		/* else, if this is fabric, the domain attach should be performed
		 * by the fabric node (node sending FLOGI); just wait for attach
		 * to complete
		 */

		ocs_node_transition(node, __ocs_d_wait_domain_attach, NULL);
		break;
	}
	case OCS_EVT_DOMAIN_ATTACH_OK:
		ocs_assert(node->sport->domain->attached, NULL);
		node_printf(node, "domain attach ok\n");
		/*sm:  ocs_node_attach */
		rc = ocs_node_attach(node);
		ocs_node_transition(node, __ocs_d_wait_node_attach, NULL);
		if (rc == OCS_HW_RTN_SUCCESS_SYNC) {
			ocs_node_post_event(node, OCS_EVT_NODE_ATTACH_OK, NULL);
		}
		break;

	default:
		__ocs_d_common(__func__, ctx, evt, arg);
		return NULL;
	}
	return NULL;
}

/**
 * @ingroup device_sm
 * @brief Device node state machine: Wait for a node attach when found by a remote node.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_d_wait_node_attach(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_ENTER:
		ocs_node_hold_frames(node);
		break;

	case OCS_EVT_EXIT:
		ocs_node_accept_frames(node);
		break;

	case OCS_EVT_NODE_ATTACH_OK:
		node->attached = TRUE;
		switch (node->send_ls_acc) {
		case OCS_NODE_SEND_LS_ACC_PLOGI: {
			/* sm: send_plogi_acc is set / send PLOGI acc */
			/* Normal case for T, or I+T */
			ocs_send_plogi_acc(node->ls_acc_io, node->ls_acc_oxid, NULL, NULL);
			ocs_node_transition(node, __ocs_d_wait_plogi_acc_cmpl, NULL);
			node->send_ls_acc = OCS_NODE_SEND_LS_ACC_NONE;
			node->ls_acc_io = NULL;
			break;
		}
		case OCS_NODE_SEND_LS_ACC_PRLI: {
			ocs_d_send_prli_rsp(node->ls_acc_io, node->ls_acc_oxid);
			node->send_ls_acc = OCS_NODE_SEND_LS_ACC_NONE;
			node->ls_acc_io = NULL;
			break;
		}
		case OCS_NODE_SEND_LS_ACC_NONE:
		default:
			/* Normal case for I */
			/* sm: send_plogi_acc is not set / send PLOGI acc */
			ocs_node_transition(node, __ocs_d_port_logged_in, NULL);
			break;
		}
		break;

	case OCS_EVT_NODE_ATTACH_FAIL:
		/* node attach failed, shutdown the node */
		node->attached = FALSE;
		node_printf(node, "node attach failed\n");
		node->shutdown_reason = OCS_NODE_SHUTDOWN_DEFAULT;
		ocs_node_transition(node, __ocs_d_initiate_shutdown, NULL);
		break;

	/* Handle shutdown events */
	case OCS_EVT_SHUTDOWN:
		node_printf(node, "%s received\n", ocs_sm_event_name(evt));
		node->shutdown_reason = OCS_NODE_SHUTDOWN_DEFAULT;
		ocs_node_transition(node, __ocs_d_wait_attach_evt_shutdown, NULL);
		break;
	case OCS_EVT_SHUTDOWN_EXPLICIT_LOGO:
		node_printf(node, "%s received\n", ocs_sm_event_name(evt));
		node->shutdown_reason = OCS_NODE_SHUTDOWN_EXPLICIT_LOGO;
		ocs_node_transition(node, __ocs_d_wait_attach_evt_shutdown, NULL);
		break;
	case OCS_EVT_SHUTDOWN_IMPLICIT_LOGO:
		node_printf(node, "%s received\n", ocs_sm_event_name(evt));
		node->shutdown_reason = OCS_NODE_SHUTDOWN_IMPLICIT_LOGO;
		ocs_node_transition(node, __ocs_d_wait_attach_evt_shutdown, NULL);
		break;
	default:
		__ocs_d_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup device_sm
 * @brief Device node state machine: Wait for a node/domain
 * attach then shutdown node.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_d_wait_attach_evt_shutdown(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_ENTER:
		ocs_node_hold_frames(node);
		break;

	case OCS_EVT_EXIT:
		ocs_node_accept_frames(node);
		break;

	/* wait for any of these attach events and then shutdown */
	case OCS_EVT_NODE_ATTACH_OK:
		node->attached = TRUE;
		node_printf(node, "Attach evt=%s, proceed to shutdown\n", ocs_sm_event_name(evt));
		ocs_node_transition(node, __ocs_d_initiate_shutdown, NULL);
		break;

	case OCS_EVT_NODE_ATTACH_FAIL:
		/* node attach failed, shutdown the node */
		node->attached = FALSE;
		node_printf(node, "Attach evt=%s, proceed to shutdown\n", ocs_sm_event_name(evt));
		ocs_node_transition(node, __ocs_d_initiate_shutdown, NULL);
		break;

	/* ignore shutdown events as we're already in shutdown path */
	case OCS_EVT_SHUTDOWN:
		/* have default shutdown event take precedence */
		node->shutdown_reason = OCS_NODE_SHUTDOWN_DEFAULT;
		/* fall through */
	case OCS_EVT_SHUTDOWN_EXPLICIT_LOGO:
	case OCS_EVT_SHUTDOWN_IMPLICIT_LOGO:
		node_printf(node, "%s received\n", ocs_sm_event_name(evt));
		break;

	default:
		__ocs_d_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup device_sm
 * @brief Device node state machine: Port is logged in.
 *
 * <h3 class="desc">Description</h3>
 * This state is entered when a remote port has completed port login (PLOGI).
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process
 * @param arg Per event optional argument
 *
 * @return Returns NULL.
 */
void *
__ocs_d_port_logged_in(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	ocs_node_cb_t *cbdata = arg;
	std_node_state_decl();

	node_sm_trace();

	/* TODO: I+T: what if PLOGI response not yet received ? */

	switch(evt) {
	case OCS_EVT_ENTER:
		/* Normal case for I or I+T */
		if (node->sport->enable_ini && !FC_ADDR_IS_DOMAIN_CTRL(node->rnode.fc_id)
				&& !node->sent_prli) {
			/* sm: if enable_ini / send PRLI */
			ocs_send_prli(node, OCS_FC_ELS_SEND_DEFAULT_TIMEOUT, OCS_FC_ELS_DEFAULT_RETRIES, NULL, NULL);
			node->sent_prli = TRUE;
			/* can now expect ELS_REQ_OK/FAIL/RJT */
		}
		break;

	case OCS_EVT_FCP_CMD_RCVD: {
		/* For target functionality send PRLO and drop the CMD frame. */
		if (node->sport->enable_tgt) {
			if (ocs_send_prlo(node, OCS_FC_ELS_SEND_DEFAULT_TIMEOUT,
				OCS_FC_ELS_DEFAULT_RETRIES, NULL, NULL)) {
				ocs_node_transition(node, __ocs_d_wait_prlo_rsp, NULL);
			}
		}
		break;
	}

	case OCS_EVT_PRLI_RCVD: {
		fc_header_t *hdr = cbdata->header->dma.virt;

		/* Normal for T or I+T */

		ocs_process_prli_payload(node, cbdata->payload->dma.virt);
		ocs_d_send_prli_rsp(cbdata->io, ocs_be16toh(hdr->ox_id));
		break;
	}

	case OCS_EVT_SRRS_ELS_REQ_OK: {	/* PRLI response */
		/* Normal case for I or I+T */
		if (node_check_els_req(ctx, evt, arg, FC_ELS_CMD_PRLI, __ocs_d_common, __func__)) {
			return NULL;
		}
		ocs_assert(node->els_req_cnt, NULL);
		node->els_req_cnt--;
		/* sm: process PRLI payload */
		ocs_process_prli_payload(node, cbdata->els->els_rsp.virt);
		ocs_node_transition(node, __ocs_d_device_ready, NULL);
		break;
	}

	case OCS_EVT_SRRS_ELS_REQ_FAIL: {	/* PRLI response failed */
		/* I, I+T, assume some link failure, shutdown node */
		if (node_check_els_req(ctx, evt, arg, FC_ELS_CMD_PRLI, __ocs_d_common, __func__)) {
			return NULL;
		}
		ocs_assert(node->els_req_cnt, NULL);
		node->els_req_cnt--;
		ocs_node_post_event(node, OCS_EVT_SHUTDOWN, NULL);
		break;
	}

	case OCS_EVT_SRRS_ELS_REQ_RJT: {/* PRLI rejected by remote */
		/* Normal for I, I+T (connected to an I) */
		/* Node doesn't want to be a target, stay here and wait for a PRLI from the remote node
		 * if it really wants to connect to us as target */
		if (node_check_els_req(ctx, evt, arg, FC_ELS_CMD_PRLI, __ocs_d_common, __func__)) {
			return NULL;
		}
		ocs_assert(node->els_req_cnt, NULL);
		node->els_req_cnt--;
		break;
	}

	case OCS_EVT_SRRS_ELS_CMPL_OK: {
		/* Normal T, I+T, target-server rejected the process login */
		/* This would be received only in the case where we sent LS_RJT for the PRLI, so
		 * do nothing.   (note: as T only we could shutdown the node)
		 */
		ocs_assert(node->els_cmpl_cnt, NULL);
		node->els_cmpl_cnt--;
		break;
	}

	case OCS_EVT_PLOGI_RCVD: {
		/* sm: save sparams, set send_plogi_acc, post implicit logout
		 * Save plogi parameters */
		ocs_node_save_sparms(node, cbdata->payload->dma.virt);
		ocs_send_ls_acc_after_attach(cbdata->io, cbdata->header->dma.virt, OCS_NODE_SEND_LS_ACC_PLOGI);

		/* Restart node attach with new service parameters, and send ACC */
		ocs_node_post_event(node, OCS_EVT_SHUTDOWN_IMPLICIT_LOGO, NULL);
		break;
	}

	case OCS_EVT_LOGO_RCVD: {
		/* I, T, I+T */
		fc_header_t *hdr = cbdata->header->dma.virt;
		node_printf(node, "%s received attached=%d\n", ocs_sm_event_name(evt), node->attached);
		ocs_send_logo_acc(cbdata->io, ocs_be16toh(hdr->ox_id), NULL, NULL);
		ocs_node_transition(node, __ocs_d_wait_logo_acc_cmpl, NULL);
		break;
	}

	default:
		__ocs_d_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup device_sm
 * @brief Device node state machine: Wait for a LOGO accept.
 *
 * <h3 class="desc">Description</h3>
 * Waits for a LOGO accept completion.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process
 * @param arg Per event optional argument
 *
 * @return Returns NULL.
 */

void *
__ocs_d_wait_logo_acc_cmpl(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_ENTER:
		ocs_node_hold_frames(node);
		break;

	case OCS_EVT_EXIT:
		ocs_node_accept_frames(node);
		break;

	case OCS_EVT_SRRS_ELS_CMPL_OK:
	case OCS_EVT_SRRS_ELS_CMPL_FAIL:
		/* sm: / post explicit logout */
		ocs_assert(node->els_cmpl_cnt, NULL);
		node->els_cmpl_cnt--;
		ocs_node_post_event(node, OCS_EVT_SHUTDOWN_EXPLICIT_LOGO, NULL);
		break;
	default:
		__ocs_d_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup device_sm
 * @brief Device node state machine: Device is ready.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_d_device_ready(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	ocs_node_cb_t *cbdata = arg;
	std_node_state_decl();

	if (evt != OCS_EVT_FCP_CMD_RCVD) {
		node_sm_trace();
	}

	switch(evt) {
	case OCS_EVT_ENTER:
		node->fcp_enabled = TRUE;
		if (node->init) {
			device_printf(ocs->dev, "[%s] found  (initiator) WWPN %s WWNN %s\n", node->display_name,
				node->wwpn, node->wwnn);
			if (node->sport->enable_tgt)
				ocs_scsi_new_initiator(node);
		}
		if (node->targ) {
			device_printf(ocs->dev, "[%s] found  (target)    WWPN %s WWNN %s\n", node->display_name,
				node->wwpn, node->wwnn);
			if (node->sport->enable_ini)
				ocs_scsi_new_target(node);
		}
		break;

	case OCS_EVT_EXIT:
		node->fcp_enabled = FALSE;
		break;

	case OCS_EVT_PLOGI_RCVD: {
		/* sm: save sparams, set send_plogi_acc, post implicit logout
		 * Save plogi parameters */
		ocs_node_save_sparms(node, cbdata->payload->dma.virt);
		ocs_send_ls_acc_after_attach(cbdata->io, cbdata->header->dma.virt, OCS_NODE_SEND_LS_ACC_PLOGI);

		/* Restart node attach with new service parameters, and send ACC */
		ocs_node_post_event(node, OCS_EVT_SHUTDOWN_IMPLICIT_LOGO, NULL);
		break;
	}


	case OCS_EVT_PDISC_RCVD: {
		fc_header_t *hdr = cbdata->header->dma.virt;
		ocs_send_plogi_acc(cbdata->io, ocs_be16toh(hdr->ox_id), NULL, NULL);
		break;
	}
	
	case OCS_EVT_PRLI_RCVD: {
		/* T, I+T: remote initiator is slow to get started */
		fc_header_t *hdr = cbdata->header->dma.virt;

		ocs_process_prli_payload(node, cbdata->payload->dma.virt);

		/* sm: send PRLI acc/reject */
		if (ocs->fc_type == node->fc_type)
			ocs_send_prli_acc(cbdata->io, ocs_be16toh(hdr->ox_id), ocs->fc_type, NULL, NULL);
		else
			ocs_send_ls_rjt(cbdata->io, ocs_be16toh(hdr->ox_id), FC_REASON_UNABLE_TO_PERFORM,
				FC_EXPL_REQUEST_NOT_SUPPORTED, 0, NULL, NULL);
		break;
	}

	case OCS_EVT_PRLO_RCVD: {
		fc_header_t *hdr = cbdata->header->dma.virt;
		fc_prlo_payload_t *prlo = cbdata->payload->dma.virt;

		/* sm: send PRLO acc/reject */
		if (ocs->fc_type == prlo->type)
			ocs_send_prlo_acc(cbdata->io, ocs_be16toh(hdr->ox_id), ocs->fc_type, NULL, NULL);
		else
			ocs_send_ls_rjt(cbdata->io, ocs_be16toh(hdr->ox_id), FC_REASON_UNABLE_TO_PERFORM,
				FC_EXPL_REQUEST_NOT_SUPPORTED, 0, NULL, NULL);
		/*TODO: need implicit logout */
		break;
	}

	case OCS_EVT_LOGO_RCVD: {
		fc_header_t *hdr = cbdata->header->dma.virt;
		node_printf(node, "%s received attached=%d\n", ocs_sm_event_name(evt), node->attached);
		ocs_send_logo_acc(cbdata->io, ocs_be16toh(hdr->ox_id), NULL, NULL);
		ocs_node_transition(node, __ocs_d_wait_logo_acc_cmpl, NULL);
		break;
	}

	case OCS_EVT_ADISC_RCVD: {
		fc_header_t *hdr = cbdata->header->dma.virt;
		ocs_send_adisc_acc(cbdata->io, ocs_be16toh(hdr->ox_id), NULL, NULL);
		break;
	}

	case OCS_EVT_RRQ_RCVD: {
		fc_header_t *hdr = cbdata->header->dma.virt;
		/* Send LS_ACC */
		ocs_send_ls_acc(cbdata->io, ocs_be16toh(hdr->ox_id), NULL, NULL);
		break;
	}

	case OCS_EVT_ABTS_RCVD:
		ocs_process_abts(cbdata->io, cbdata->header->dma.virt);
		break;

	case OCS_EVT_NODE_ACTIVE_IO_LIST_EMPTY:
		break;

	case OCS_EVT_NODE_REFOUND:
		break;

	case OCS_EVT_NODE_MISSING:
		if (node->sport->enable_rscn) {
			ocs_node_transition(node, __ocs_d_device_gone, NULL);
		}
		break;

	case OCS_EVT_SRRS_ELS_CMPL_OK:
		/* T, or I+T, PRLI accept completed ok */
		ocs_assert(node->els_cmpl_cnt, NULL);
		node->els_cmpl_cnt--;
		break;

	case OCS_EVT_SRRS_ELS_CMPL_FAIL:
		/* T, or I+T, PRLI accept failed to complete */
		ocs_assert(node->els_cmpl_cnt, NULL);
		node->els_cmpl_cnt--;
		node_printf(node, "Failed to send PRLI LS_ACC\n");
		break;

	default:
		__ocs_d_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup device_sm
 * @brief Device node state machine: Node is gone (absent from GID_PT).
 *
 * <h3 class="desc">Description</h3>
 * State entered when a node is detected as being gone (absent from GID_PT).
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process
 * @param arg Per event optional argument
 *
 * @return Returns NULL.
 */

void *
__ocs_d_device_gone(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	int32_t rc = OCS_SCSI_CALL_COMPLETE;
	int32_t rc_2 = OCS_SCSI_CALL_COMPLETE;
	ocs_node_cb_t *cbdata = arg;
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_ENTER: {
		const char *labels[] = {"none", "initiator", "target", "initiator+target"};

		device_printf(ocs->dev, "[%s] missing (%s)    WWPN %s WWNN %s\n", node->display_name,
				labels[(node->targ << 1) | (node->init)], node->wwpn, node->wwnn);

		switch(ocs_node_get_enable(node)) {
		case OCS_NODE_ENABLE_T_TO_T:
		case OCS_NODE_ENABLE_I_TO_T:
		case OCS_NODE_ENABLE_IT_TO_T:
			rc = ocs_scsi_del_target(node, OCS_SCSI_TARGET_MISSING);
			break;

		case OCS_NODE_ENABLE_T_TO_I:
		case OCS_NODE_ENABLE_I_TO_I:
		case OCS_NODE_ENABLE_IT_TO_I:
			rc = ocs_scsi_del_initiator(node, OCS_SCSI_INITIATOR_MISSING);
			break;

		case OCS_NODE_ENABLE_T_TO_IT:
			rc = ocs_scsi_del_initiator(node, OCS_SCSI_INITIATOR_MISSING);
			break;

		case OCS_NODE_ENABLE_I_TO_IT:
			rc = ocs_scsi_del_target(node, OCS_SCSI_TARGET_MISSING);
			break;

		case OCS_NODE_ENABLE_IT_TO_IT:
			rc = ocs_scsi_del_initiator(node, OCS_SCSI_INITIATOR_MISSING);
			rc_2 = ocs_scsi_del_target(node, OCS_SCSI_TARGET_MISSING);
			break;

		default:
			rc = OCS_SCSI_CALL_COMPLETE;
			break;

		}

		if ((rc == OCS_SCSI_CALL_COMPLETE) && (rc_2 == OCS_SCSI_CALL_COMPLETE)) {
			ocs_node_post_event(node, OCS_EVT_SHUTDOWN, NULL);
		}

		break;
	}
	case OCS_EVT_NODE_REFOUND:
		/* two approaches, reauthenticate with PLOGI/PRLI, or ADISC */

		/* reauthenticate with PLOGI/PRLI */
		/* ocs_node_transition(node, __ocs_d_discovered, NULL); */

		/* reauthenticate with ADISC 
		 * sm: send ADISC */
		ocs_send_adisc(node, OCS_FC_ELS_SEND_DEFAULT_TIMEOUT, OCS_FC_ELS_DEFAULT_RETRIES, NULL, NULL);
		ocs_node_transition(node, __ocs_d_wait_adisc_rsp, NULL);
		break;

	case OCS_EVT_PLOGI_RCVD: {
		/* sm: save sparams, set send_plogi_acc, post implicit logout
		 * Save plogi parameters */
		ocs_node_save_sparms(node, cbdata->payload->dma.virt);
		ocs_send_ls_acc_after_attach(cbdata->io, cbdata->header->dma.virt, OCS_NODE_SEND_LS_ACC_PLOGI);

		/* Restart node attach with new service parameters, and send ACC */
		ocs_node_post_event(node, OCS_EVT_SHUTDOWN_IMPLICIT_LOGO, NULL);
		break;
	}

	case OCS_EVT_FCP_CMD_RCVD: {
		/* most likely a stale frame (received prior to link down), if attempt
		 * to send LOGO, will probably timeout and eat up 20s; thus, drop FCP_CMND
		 */
		node_printf(node, "FCP_CMND received, drop\n");
		break;
	}
	case OCS_EVT_LOGO_RCVD: {
		/* I, T, I+T */
		fc_header_t *hdr = cbdata->header->dma.virt;
		node_printf(node, "%s received attached=%d\n", ocs_sm_event_name(evt), node->attached);
		/* sm: send LOGO acc */
		ocs_send_logo_acc(cbdata->io, ocs_be16toh(hdr->ox_id), NULL, NULL);
		ocs_node_transition(node, __ocs_d_wait_logo_acc_cmpl, NULL);
		break;
	}
	default:
		__ocs_d_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup device_sm
 * @brief Device node state machine: Wait for the ADISC response.
 *
 * <h3 class="desc">Description</h3>
 * Waits for the ADISC response from the remote node.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_d_wait_adisc_rsp(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	ocs_node_cb_t *cbdata = arg;
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_SRRS_ELS_REQ_OK:
		if (node_check_els_req(ctx, evt, arg, FC_ELS_CMD_ADISC, __ocs_d_common, __func__)) {
			return NULL;
		}
		ocs_assert(node->els_req_cnt, NULL);
		node->els_req_cnt--;
		ocs_node_transition(node, __ocs_d_device_ready, NULL);
		break;

	case OCS_EVT_SRRS_ELS_REQ_RJT:
		/* received an LS_RJT, in this case, send shutdown (explicit logo)
		 * event which will unregister the node, and start over with PLOGI
		 */
		if (node_check_els_req(ctx, evt, arg, FC_ELS_CMD_ADISC, __ocs_d_common, __func__)) {
			return NULL;
		}
		ocs_assert(node->els_req_cnt, NULL);
		node->els_req_cnt--;
		/*sm: post explicit logout */
		ocs_node_post_event(node, OCS_EVT_SHUTDOWN_EXPLICIT_LOGO, NULL);
		break;

	case OCS_EVT_LOGO_RCVD: {
		/* In this case, we have the equivalent of an LS_RJT for the ADISC,
		 * so we need to abort the ADISC, and re-login with PLOGI
		 */
		/*sm: request abort, send LOGO acc */
		fc_header_t *hdr = cbdata->header->dma.virt;
		node_printf(node, "%s received attached=%d\n", ocs_sm_event_name(evt), node->attached);
		ocs_send_logo_acc(cbdata->io, ocs_be16toh(hdr->ox_id), NULL, NULL);
		ocs_node_transition(node, __ocs_d_wait_logo_acc_cmpl, NULL);
		break;
	}
	default:
		__ocs_d_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}
