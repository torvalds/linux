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
 *
 * This file implements remote node state machines for:
 * - Fabric logins.
 * - Fabric controller events.
 * - Name/directory services interaction.
 * - Point-to-point logins.
 */

/*!
@defgroup fabric_sm Node State Machine: Fabric States
@defgroup ns_sm Node State Machine: Name/Directory Services States
@defgroup p2p_sm Node State Machine: Point-to-Point Node States
*/

#include "ocs.h"
#include "ocs_fabric.h"
#include "ocs_els.h"
#include "ocs_device.h"

static void ocs_fabric_initiate_shutdown(ocs_node_t *node);
static void * __ocs_fabric_common(const char *funcname, ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
static int32_t ocs_start_ns_node(ocs_sport_t *sport);
static int32_t ocs_start_fabctl_node(ocs_sport_t *sport);
static int32_t ocs_process_gidpt_payload(ocs_node_t *node, fcct_gidpt_acc_t *gidpt, uint32_t gidpt_len);
static void ocs_process_rscn(ocs_node_t *node, ocs_node_cb_t *cbdata);
static uint64_t ocs_get_wwpn(fc_plogi_payload_t *sp);
static void gidpt_delay_timer_cb(void *arg);

/**
 * @ingroup fabric_sm
 * @brief Fabric node state machine: Initial state.
 *
 * @par Description
 * Send an FLOGI to a well-known fabric.
 *
 * @param ctx Remote node sm context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */
void *
__ocs_fabric_init(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_REENTER:	/* not sure why we're getting these ... */
		ocs_log_debug(node->ocs, ">>> reenter !!\n");
		/* fall through */
	case OCS_EVT_ENTER:
		/* sm: / send FLOGI */
		ocs_send_flogi(node, OCS_FC_FLOGI_TIMEOUT_SEC, OCS_FC_ELS_DEFAULT_RETRIES, NULL, NULL);
		ocs_node_transition(node, __ocs_fabric_flogi_wait_rsp, NULL);
		break;

	default:
		__ocs_fabric_common(__func__, ctx, evt, arg);
		break;
	}

	return NULL;
}

/**
 * @ingroup fabric_sm
 * @brief Set sport topology.
 *
 * @par Description
 * Set sport topology.
 *
 * @param node Pointer to the node for which the topology is set.
 * @param topology Topology to set.
 *
 * @return Returns NULL.
 */
void
ocs_fabric_set_topology(ocs_node_t *node, ocs_sport_topology_e topology)
{
	node->sport->topology = topology;
}

/**
 * @ingroup fabric_sm
 * @brief Notify sport topology.
 * @par Description
 * notify sport topology.
 * @param node Pointer to the node for which the topology is set.
 * @return Returns NULL.
 */
void
ocs_fabric_notify_topology(ocs_node_t *node)
{
	ocs_node_t *tmp_node;
	ocs_node_t *next;
	ocs_sport_topology_e topology = node->sport->topology;

	/* now loop through the nodes in the sport and send topology notification */
	ocs_sport_lock(node->sport);
	ocs_list_foreach_safe(&node->sport->node_list, tmp_node, next) {
		if (tmp_node != node) {
			ocs_node_post_event(tmp_node, OCS_EVT_SPORT_TOPOLOGY_NOTIFY, (void *)topology);
		}
	}
	ocs_sport_unlock(node->sport);
}

/**
 * @ingroup fabric_sm
 * @brief Fabric node state machine: Wait for an FLOGI response.
 *
 * @par Description
 * Wait for an FLOGI response event.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_fabric_flogi_wait_rsp(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	ocs_node_cb_t *cbdata = arg;
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_SRRS_ELS_REQ_OK: {

		if (node_check_els_req(ctx, evt, arg, FC_ELS_CMD_FLOGI, __ocs_fabric_common, __func__)) {
			return NULL;
		}
		ocs_assert(node->els_req_cnt, NULL);
		node->els_req_cnt--;

		ocs_domain_save_sparms(node->sport->domain, cbdata->els->els_rsp.virt);

		ocs_display_sparams(node->display_name, "flogi rcvd resp", 0, NULL,
			((uint8_t*)cbdata->els->els_rsp.virt) + 4);

		/* Check to see if the fabric is an F_PORT or and N_PORT */
		if (ocs_rnode_is_nport(cbdata->els->els_rsp.virt)) {
			/* sm: if nport and p2p_winner / ocs_domain_attach */
			ocs_fabric_set_topology(node, OCS_SPORT_TOPOLOGY_P2P);
			if (ocs_p2p_setup(node->sport)) {
				node_printf(node, "p2p setup failed, shutting down node\n");
				node->shutdown_reason = OCS_NODE_SHUTDOWN_DEFAULT;
				ocs_fabric_initiate_shutdown(node);
			} else {
				if (node->sport->p2p_winner) {
					ocs_node_transition(node, __ocs_p2p_wait_domain_attach, NULL);
					if (!node->sport->domain->attached) {
						node_printf(node, "p2p winner, domain not attached\n");
						ocs_domain_attach(node->sport->domain, node->sport->p2p_port_id);
					} else {
						/* already attached, just send ATTACH_OK */
						node_printf(node, "p2p winner, domain already attached\n");
						ocs_node_post_event(node, OCS_EVT_DOMAIN_ATTACH_OK, NULL);
					}
				} else {
					/* peer is p2p winner; PLOGI will be received on the
					 * remote SID=1 node; this node has served its purpose
					 */
					node->shutdown_reason = OCS_NODE_SHUTDOWN_DEFAULT;
					ocs_fabric_initiate_shutdown(node);
				}
			}
		} else {
			/* sm: if not nport / ocs_domain_attach */
			/* ext_status has the fc_id, attach domain */
			ocs_fabric_set_topology(node, OCS_SPORT_TOPOLOGY_FABRIC);
			ocs_fabric_notify_topology(node);
			ocs_assert(!node->sport->domain->attached, NULL);
			ocs_domain_attach(node->sport->domain, cbdata->ext_status);
			ocs_node_transition(node, __ocs_fabric_wait_domain_attach, NULL);
		}

		break;
	}

	case OCS_EVT_ELS_REQ_ABORTED:
	case OCS_EVT_SRRS_ELS_REQ_RJT:
	case OCS_EVT_SRRS_ELS_REQ_FAIL: {
		ocs_sport_t *sport = node->sport;
		/*
		 * with these errors, we have no recovery, so shutdown the sport, leave the link
		 * up and the domain ready
		 */
		if (node_check_els_req(ctx, evt, arg, FC_ELS_CMD_FLOGI, __ocs_fabric_common, __func__)) {
			return NULL;
		}
		node_printf(node, "FLOGI failed evt=%s, shutting down sport [%s]\n", ocs_sm_event_name(evt),
			sport->display_name);
		ocs_assert(node->els_req_cnt, NULL);
		node->els_req_cnt--;
		ocs_sm_post_event(&sport->sm, OCS_EVT_SHUTDOWN, NULL);
		break;
	}

	default:
		__ocs_fabric_common(__func__, ctx, evt, arg);
		break;
	}

	return NULL;
}

/**
 * @ingroup fabric_sm
 * @brief Fabric node state machine: Initial state for a virtual port.
 *
 * @par Description
 * State entered when a virtual port is created. Send FDISC.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */
void *
__ocs_vport_fabric_init(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_ENTER:
		/* sm: send FDISC */
		ocs_send_fdisc(node, OCS_FC_FLOGI_TIMEOUT_SEC, OCS_FC_ELS_DEFAULT_RETRIES, NULL, NULL);
		ocs_node_transition(node, __ocs_fabric_fdisc_wait_rsp, NULL);
		break;

	default:
		__ocs_fabric_common(__func__, ctx, evt, arg);
		break;
	}

	return NULL;
}

/**
 * @ingroup fabric_sm
 * @brief Fabric node state machine: Wait for an FDISC response
 *
 * @par Description
 * Used for a virtual port. Waits for an FDISC response. If OK, issue a HW port attach.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */
void *
__ocs_fabric_fdisc_wait_rsp(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	ocs_node_cb_t *cbdata = arg;
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_SRRS_ELS_REQ_OK: {
		/* fc_id is in ext_status */
		if (node_check_els_req(ctx, evt, arg, FC_ELS_CMD_FDISC, __ocs_fabric_common, __func__)) {
			return NULL;
		}

		ocs_display_sparams(node->display_name, "fdisc rcvd resp", 0, NULL,
			((uint8_t*)cbdata->els->els_rsp.virt) + 4);

		ocs_assert(node->els_req_cnt, NULL);
		node->els_req_cnt--;
		/* sm: ocs_sport_attach */
		ocs_sport_attach(node->sport, cbdata->ext_status);
		ocs_node_transition(node, __ocs_fabric_wait_domain_attach, NULL);
		break;

	}

	case OCS_EVT_SRRS_ELS_REQ_RJT:
	case OCS_EVT_SRRS_ELS_REQ_FAIL: {
		if (node_check_els_req(ctx, evt, arg, FC_ELS_CMD_FDISC, __ocs_fabric_common, __func__)) {
			return NULL;
		}
		ocs_assert(node->els_req_cnt, NULL);
		node->els_req_cnt--;
		ocs_log_err(ocs, "FDISC failed, shutting down sport\n");
		/* sm: shutdown sport */
		ocs_sm_post_event(&node->sport->sm, OCS_EVT_SHUTDOWN, NULL);
		break;
	}

	default:
		__ocs_fabric_common(__func__, ctx, evt, arg);
		break;
	}

	return NULL;
}

/**
 * @ingroup fabric_sm
 * @brief Fabric node state machine: Wait for a domain/sport attach event.
 *
 * @par Description
 * Waits for a domain/sport attach event.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */
void *
__ocs_fabric_wait_domain_attach(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
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
	case OCS_EVT_DOMAIN_ATTACH_OK:
	case OCS_EVT_SPORT_ATTACH_OK: {
		int rc;

		rc = ocs_start_ns_node(node->sport);
		if (rc)
			return NULL;

		/* sm: if enable_ini / start fabctl node
		 * Instantiate the fabric controller (sends SCR) */
		if (node->sport->enable_rscn) {
			rc = ocs_start_fabctl_node(node->sport);
			if (rc)
				return NULL;
		}
		ocs_node_transition(node, __ocs_fabric_idle, NULL);
		break;
	}
	default:
		__ocs_fabric_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup fabric_sm
 * @brief Fabric node state machine: Fabric node is idle.
 *
 * @par Description
 * Wait for fabric node events.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */
void *
__ocs_fabric_idle(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_DOMAIN_ATTACH_OK:
		break;
	default:
		__ocs_fabric_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup ns_sm
 * @brief Name services node state machine: Initialize.
 *
 * @par Description
 * A PLOGI is sent to the well-known name/directory services node.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */
void *
__ocs_ns_init(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_ENTER:
		/* sm: send PLOGI */
		ocs_send_plogi(node, OCS_FC_ELS_SEND_DEFAULT_TIMEOUT, OCS_FC_ELS_DEFAULT_RETRIES, NULL, NULL);
		ocs_node_transition(node, __ocs_ns_plogi_wait_rsp, NULL);
		break;
	default:
		__ocs_fabric_common(__func__, ctx, evt, arg);
		break;
	}

	return NULL;
}

/**
 * @ingroup ns_sm
 * @brief Name services node state machine: Wait for a PLOGI response.
 *
 * @par Description
 * Waits for a response from PLOGI to name services node, then issues a
 * node attach request to the HW.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */
void *
__ocs_ns_plogi_wait_rsp(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	int32_t rc;
	ocs_node_cb_t *cbdata = arg;
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_SRRS_ELS_REQ_OK: {
		/* Save service parameters */
		if (node_check_els_req(ctx, evt, arg, FC_ELS_CMD_PLOGI, __ocs_fabric_common, __func__)) {
			return NULL;
		}
		ocs_assert(node->els_req_cnt, NULL);
		node->els_req_cnt--;
		/* sm: save sparams, ocs_node_attach */
		ocs_node_save_sparms(node, cbdata->els->els_rsp.virt);
		ocs_display_sparams(node->display_name, "plogi rcvd resp", 0, NULL,
			((uint8_t*)cbdata->els->els_rsp.virt) + 4);
		rc = ocs_node_attach(node);
		ocs_node_transition(node, __ocs_ns_wait_node_attach, NULL);
		if (rc == OCS_HW_RTN_SUCCESS_SYNC) {
			ocs_node_post_event(node, OCS_EVT_NODE_ATTACH_OK, NULL);
		}
		break;
	}
	default:
		__ocs_fabric_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup ns_sm
 * @brief Name services node state machine: Wait for a node attach completion.
 *
 * @par Description
 * Waits for a node attach completion, then issues an RFTID name services
 * request.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */
void *
__ocs_ns_wait_node_attach(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
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
		/* sm: send RFTID */
		ocs_ns_send_rftid(node, OCS_FC_ELS_CT_SEND_DEFAULT_TIMEOUT,
				 OCS_FC_ELS_DEFAULT_RETRIES, NULL, NULL);
		ocs_node_transition(node, __ocs_ns_rftid_wait_rsp, NULL);
		break;

	case OCS_EVT_NODE_ATTACH_FAIL:
		/* node attach failed, shutdown the node */
		node->attached = FALSE;
		node_printf(node, "Node attach failed\n");
		node->shutdown_reason = OCS_NODE_SHUTDOWN_DEFAULT;
		ocs_fabric_initiate_shutdown(node);
		break;

	case OCS_EVT_SHUTDOWN:
		node_printf(node, "Shutdown event received\n");
		node->shutdown_reason = OCS_NODE_SHUTDOWN_DEFAULT;
		ocs_node_transition(node, __ocs_fabric_wait_attach_evt_shutdown, NULL);
		break;

	/* if receive RSCN just ignore, 
	 * we haven't sent GID_PT yet (ACC sent by fabctl node) */
	case OCS_EVT_RSCN_RCVD:
		break;

	default:
		__ocs_fabric_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup ns_sm
 * @brief Wait for a domain/sport/node attach completion, then
 * shutdown.
 *
 * @par Description
 * Waits for a domain/sport/node attach completion, then shuts
 * node down.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */
void *
__ocs_fabric_wait_attach_evt_shutdown(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
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
		ocs_fabric_initiate_shutdown(node);
		break;

	case OCS_EVT_NODE_ATTACH_FAIL:
		node->attached = FALSE;
		node_printf(node, "Attach evt=%s, proceed to shutdown\n", ocs_sm_event_name(evt));
		ocs_fabric_initiate_shutdown(node);
		break;

	/* ignore shutdown event as we're already in shutdown path */
	case OCS_EVT_SHUTDOWN:
		node_printf(node, "Shutdown event received\n");
		break;

	default:
		__ocs_fabric_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup ns_sm
 * @brief Name services node state machine: Wait for an RFTID response event.
 *
 * @par Description
 * Waits for an RFTID response event; if configured for an initiator operation,
 * a GIDPT name services request is issued.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */
void *
__ocs_ns_rftid_wait_rsp(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_SRRS_ELS_REQ_OK:
		if (node_check_ns_req(ctx, evt, arg, FC_GS_NAMESERVER_RFT_ID, __ocs_fabric_common, __func__)) {
			return NULL;
		}
		ocs_assert(node->els_req_cnt, NULL);
		node->els_req_cnt--;
		/*sm: send RFFID */
		ocs_ns_send_rffid(node, OCS_FC_ELS_CT_SEND_DEFAULT_TIMEOUT,
				OCS_FC_ELS_DEFAULT_RETRIES, NULL, NULL);
		ocs_node_transition(node, __ocs_ns_rffid_wait_rsp, NULL);
		break;

	/* if receive RSCN just ignore,
	 * we haven't sent GID_PT yet (ACC sent by fabctl node) */
	case OCS_EVT_RSCN_RCVD:
		break;

	default:
		__ocs_fabric_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup ns_sm
 * @brief Fabric node state machine: Wait for RFFID response event.
 *
 * @par Description
 * Waits for an RFFID response event; if configured for an initiator operation,
 * a GIDPT name services request is issued.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */
void *
__ocs_ns_rffid_wait_rsp(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_SRRS_ELS_REQ_OK:	{
		if (node_check_ns_req(ctx, evt, arg, FC_GS_NAMESERVER_RFF_ID, __ocs_fabric_common, __func__)) {
			return NULL;
		}
		ocs_assert(node->els_req_cnt, NULL);
		node->els_req_cnt--;
		if (node->sport->enable_rscn) {
			/* sm: if enable_rscn / send GIDPT */
			ocs_ns_send_gidpt(node, OCS_FC_ELS_CT_SEND_DEFAULT_TIMEOUT,
					OCS_FC_ELS_DEFAULT_RETRIES, NULL, NULL);
			ocs_node_transition(node, __ocs_ns_gidpt_wait_rsp, NULL);
		} else {
			/* if 'T' only, we're done, go to idle */
			ocs_node_transition(node, __ocs_ns_idle, NULL);
		}
		break;
	}
	/* if receive RSCN just ignore, 
	 * we haven't sent GID_PT yet (ACC sent by fabctl node) */
	case OCS_EVT_RSCN_RCVD:
		break;

	default:
		__ocs_fabric_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup ns_sm
 * @brief Name services node state machine: Wait for a GIDPT response.
 *
 * @par Description
 * Wait for a GIDPT response from the name server. Process the FC_IDs that are
 * reported by creating new remote ports, as needed.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */
void *
__ocs_ns_gidpt_wait_rsp(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	ocs_node_cb_t *cbdata = arg;
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_SRRS_ELS_REQ_OK:	{
		if (node_check_ns_req(ctx, evt, arg, FC_GS_NAMESERVER_GID_PT, __ocs_fabric_common, __func__)) {
			return NULL;
		}
		ocs_assert(node->els_req_cnt, NULL);
		node->els_req_cnt--;
		/* sm: / process GIDPT payload */
		ocs_process_gidpt_payload(node, cbdata->els->els_rsp.virt, cbdata->els->els_rsp.len);
		/* TODO: should we logout at this point or just go idle */
		ocs_node_transition(node, __ocs_ns_idle, NULL);
		break;
	}

	case OCS_EVT_SRRS_ELS_REQ_FAIL:	{
		/* not much we can do; will retry with the next RSCN */
		node_printf(node, "GID_PT failed to complete\n");
		ocs_assert(node->els_req_cnt, NULL);
		node->els_req_cnt--;
		ocs_node_transition(node, __ocs_ns_idle, NULL);
		break;
	}

	/* if receive RSCN here, queue up another discovery processing */
	case OCS_EVT_RSCN_RCVD: {
		node_printf(node, "RSCN received during GID_PT processing\n");
		node->rscn_pending = 1;
		break;
	}

	default:
		__ocs_fabric_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}


/**
 * @ingroup ns_sm
 * @brief Name services node state machine: Idle state.
 *
 * @par Description
 * Idle. Waiting for RSCN received events (posted from the fabric controller), and
 * restarts the GIDPT name services query and processing.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */
void *
__ocs_ns_idle(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_ENTER:
		if (!node->rscn_pending) {
			break;
		}
		node_printf(node, "RSCN pending, restart discovery\n");
		node->rscn_pending = 0;

			/* fall through */

	case OCS_EVT_RSCN_RCVD: {
		/* sm: / send GIDPT
		 * If target RSCN processing is enabled, and this is target only
		 * (not initiator), and tgt_rscn_delay is non-zero,
		 * then we delay issuing the GID_PT
		 */
		if ((ocs->tgt_rscn_delay_msec != 0) && !node->sport->enable_ini && node->sport->enable_tgt &&
			enable_target_rscn(ocs)) {
			ocs_node_transition(node, __ocs_ns_gidpt_delay, NULL);
		} else {
			ocs_ns_send_gidpt(node, OCS_FC_ELS_CT_SEND_DEFAULT_TIMEOUT,
					OCS_FC_ELS_DEFAULT_RETRIES, NULL, NULL);
			ocs_node_transition(node, __ocs_ns_gidpt_wait_rsp, NULL);
		}
		break;
	}

	default:
		__ocs_fabric_common(__func__, ctx, evt, arg);
		break;
	}

	return NULL;
}

/**
 * @brief Handle GIDPT delay timer callback
 *
 * @par Description
 * Post an OCS_EVT_GIDPT_DEIALY_EXPIRED event to the passed in node.
 *
 * @param arg Pointer to node.
 *
 * @return None.
 */
static void
gidpt_delay_timer_cb(void *arg)
{
	ocs_node_t *node = arg;
	int32_t rc;

	ocs_del_timer(&node->gidpt_delay_timer);
	rc = ocs_xport_control(node->ocs->xport, OCS_XPORT_POST_NODE_EVENT, node, OCS_EVT_GIDPT_DELAY_EXPIRED, NULL);
	if (rc) {
		ocs_log_err(node->ocs, "ocs_xport_control(OCS_XPORT_POST_NODE_EVENT) failed: %d\n", rc);
	}
}

/**
 * @ingroup ns_sm
 * @brief Name services node state machine: Delayed GIDPT.
 *
 * @par Description
 * Waiting for GIDPT delay to expire before submitting GIDPT to name server.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */
void *
__ocs_ns_gidpt_delay(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_ENTER: {
		time_t delay_msec;

		ocs_assert(ocs->tgt_rscn_delay_msec != 0, NULL);

		/*
		 * Compute the delay time.   Set to tgt_rscn_delay, if the time since last GIDPT
		 * is less than tgt_rscn_period, then use tgt_rscn_period.
		 */
		delay_msec = ocs->tgt_rscn_delay_msec;
		if ((ocs_msectime() - node->time_last_gidpt_msec) < ocs->tgt_rscn_period_msec) {
			delay_msec = ocs->tgt_rscn_period_msec;
		}

		ocs_setup_timer(ocs, &node->gidpt_delay_timer, gidpt_delay_timer_cb, node, delay_msec);

		break;
	}

	case OCS_EVT_GIDPT_DELAY_EXPIRED:
		node->time_last_gidpt_msec = ocs_msectime();
		ocs_ns_send_gidpt(node, OCS_FC_ELS_CT_SEND_DEFAULT_TIMEOUT,
				OCS_FC_ELS_DEFAULT_RETRIES, NULL, NULL);
		ocs_node_transition(node, __ocs_ns_gidpt_wait_rsp, NULL);
		break;

	case OCS_EVT_RSCN_RCVD: {
		ocs_log_debug(ocs, "RSCN received while in GIDPT delay - no action\n");
		break;
	}

	default:
		__ocs_fabric_common(__func__, ctx, evt, arg);
		break;
	}

	return NULL;
}

/**
 * @ingroup fabric_sm
 * @brief Fabric controller node state machine: Initial state.
 *
 * @par Description
 * Issue a PLOGI to a well-known fabric controller address.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */
void *
__ocs_fabctl_init(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	ocs_node_t *node = ctx->app;

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_ENTER:
		/* no need to login to fabric controller, just send SCR */
		ocs_send_scr(node, OCS_FC_ELS_SEND_DEFAULT_TIMEOUT, OCS_FC_ELS_DEFAULT_RETRIES, NULL, NULL);
		ocs_node_transition(node, __ocs_fabctl_wait_scr_rsp, NULL);
		break;

	case OCS_EVT_NODE_ATTACH_OK:
		node->attached = TRUE;
		break;

	default:
		__ocs_fabric_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup fabric_sm
 * @brief Fabric controller node state machine: Wait for a node attach request
 * to complete.
 *
 * @par Description
 * Wait for a node attach to complete. If successful, issue an SCR
 * to the fabric controller, subscribing to all RSCN.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 *
 */
void *
__ocs_fabctl_wait_node_attach(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
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
		/* sm: / send SCR */
		ocs_send_scr(node, OCS_FC_ELS_SEND_DEFAULT_TIMEOUT, OCS_FC_ELS_DEFAULT_RETRIES, NULL, NULL);
		ocs_node_transition(node, __ocs_fabctl_wait_scr_rsp, NULL);
		break;

	case OCS_EVT_NODE_ATTACH_FAIL:
		/* node attach failed, shutdown the node */
		node->attached = FALSE;
		node_printf(node, "Node attach failed\n");
		node->shutdown_reason = OCS_NODE_SHUTDOWN_DEFAULT;
		ocs_fabric_initiate_shutdown(node);
		break;

	case OCS_EVT_SHUTDOWN:
		node_printf(node, "Shutdown event received\n");
		node->shutdown_reason = OCS_NODE_SHUTDOWN_DEFAULT;
		ocs_node_transition(node, __ocs_fabric_wait_attach_evt_shutdown, NULL);
		break;

	default:
		__ocs_fabric_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup fabric_sm
 * @brief Fabric controller node state machine: Wait for an SCR response from the
 * fabric controller.
 *
 * @par Description
 * Waits for an SCR response from the fabric controller.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */
void *
__ocs_fabctl_wait_scr_rsp(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_SRRS_ELS_REQ_OK:
		if (node_check_els_req(ctx, evt, arg, FC_ELS_CMD_SCR, __ocs_fabric_common, __func__)) {
			return NULL;
		}
		ocs_assert(node->els_req_cnt, NULL);
		node->els_req_cnt--;
		ocs_node_transition(node, __ocs_fabctl_ready, NULL);
		break;

	default:
		__ocs_fabric_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup fabric_sm
 * @brief Fabric controller node state machine: Ready.
 *
 * @par Description
 * In this state, the fabric controller sends a RSCN, which is received
 * by this node and is forwarded to the name services node object; and
 * the RSCN LS_ACC is sent.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_fabctl_ready(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	ocs_node_cb_t *cbdata = arg;
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_RSCN_RCVD: {
		fc_header_t *hdr = cbdata->header->dma.virt;

		/* sm: / process RSCN (forward to name services node),
		 * send LS_ACC */
		ocs_process_rscn(node, cbdata);
		ocs_send_ls_acc(cbdata->io, ocs_be16toh(hdr->ox_id), NULL, NULL);
		ocs_node_transition(node, __ocs_fabctl_wait_ls_acc_cmpl, NULL);
		break;
	}

	default:
		__ocs_fabric_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup fabric_sm
 * @brief Fabric controller node state machine: Wait for LS_ACC.
 *
 * @par Description
 * Waits for the LS_ACC from the fabric controller.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_fabctl_wait_ls_acc_cmpl(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
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
		ocs_assert(node->els_cmpl_cnt, NULL);
		node->els_cmpl_cnt--;
		ocs_node_transition(node, __ocs_fabctl_ready, NULL);
		break;

	default:
		__ocs_fabric_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup fabric_sm
 * @brief Initiate fabric node shutdown.
 *
 * @param node Node for which shutdown is initiated.
 *
 * @return Returns None.
 */

static void
ocs_fabric_initiate_shutdown(ocs_node_t *node)
{
	ocs_hw_rtn_e rc;
	ocs_t *ocs = node->ocs;
	ocs_scsi_io_alloc_disable(node);

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
		if (rc != OCS_HW_RTN_SUCCESS && rc != OCS_HW_RTN_SUCCESS_SYNC) {
			node_printf(node, "Failed freeing HW node, rc=%d\n", rc);
		}
	}
	/*
	 * node has either been detached or is in the process of being detached,
	 * call common node's initiate cleanup function
	 */
	ocs_node_initiate_cleanup(node);
}

/**
 * @ingroup fabric_sm
 * @brief Fabric node state machine: Handle the common fabric node events.
 *
 * @param funcname Function name text.
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

static void *
__ocs_fabric_common(const char *funcname, ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	ocs_node_t *node = NULL;
	ocs_assert(ctx, NULL);
	ocs_assert(ctx->app, NULL);
	node = ctx->app;

	switch(evt) {
	case OCS_EVT_DOMAIN_ATTACH_OK:
		break;
	case OCS_EVT_SHUTDOWN:
		node->shutdown_reason = OCS_NODE_SHUTDOWN_DEFAULT;
		ocs_fabric_initiate_shutdown(node);
		break;

	default:
		/* call default event handler common to all nodes */
		__ocs_node_common(funcname, ctx, evt, arg);
		break;
	}
	return NULL;
}

/**
 * @brief Return TRUE if the remote node is an NPORT.
 *
 * @par Description
 * Examines the service parameters. Returns TRUE if the node reports itself as
 * an NPORT.
 *
 * @param remote_sparms Remote node service parameters.
 *
 * @return Returns TRUE if NPORT.
 */

int32_t
ocs_rnode_is_nport(fc_plogi_payload_t *remote_sparms)
{
	return (ocs_be32toh(remote_sparms->common_service_parameters[1]) & (1U << 28)) == 0;
}

/**
 * @brief Return the node's WWPN as an uint64_t.
 *
 * @par Description
 * The WWPN is computed from service parameters, and returned as a uint64_t.
 *
 * @param sp Pointer to service parameters.
 *
 * @return Returns WWPN.
 *
 */

static uint64_t
ocs_get_wwpn(fc_plogi_payload_t *sp)
{
	return (((uint64_t)ocs_be32toh(sp->port_name_hi) << 32ll) | (ocs_be32toh(sp->port_name_lo)));
}

/**
 * @brief Return TRUE if the remote node is the point-to-point winner.
 *
 * @par Description
 * Compares WWPNs. Returns TRUE if the remote node's WWPN is numerically
 * higher than the local node's WWPN.
 *
 * @param sport Pointer to the sport object.
 *
 * @return
 * - 0, if the remote node is the loser.
 * - 1, if the remote node is the winner.
 * - (-1), if remote node is neither the loser nor the winner
 *   (WWPNs match)
 */

static int32_t
ocs_rnode_is_winner(ocs_sport_t *sport)
{
	fc_plogi_payload_t *remote_sparms = (fc_plogi_payload_t*) sport->domain->flogi_service_params;
	uint64_t remote_wwpn = ocs_get_wwpn(remote_sparms);
	uint64_t local_wwpn = sport->wwpn;
	char prop_buf[32];
	uint64_t wwn_bump = 0;

	if (ocs_get_property("wwn_bump", prop_buf, sizeof(prop_buf)) == 0) {
		wwn_bump = ocs_strtoull(prop_buf, 0, 0);
	}
	local_wwpn ^= wwn_bump;

	remote_wwpn = ocs_get_wwpn(remote_sparms);

	ocs_log_debug(sport->ocs, "r: %08x %08x\n", ocs_be32toh(remote_sparms->port_name_hi), ocs_be32toh(remote_sparms->port_name_lo));
	ocs_log_debug(sport->ocs, "l: %08x %08x\n", (uint32_t) (local_wwpn >> 32ll), (uint32_t) local_wwpn);

	if (remote_wwpn == local_wwpn) {
		ocs_log_warn(sport->ocs, "WWPN of remote node [%08x %08x] matches local WWPN\n",
			(uint32_t) (local_wwpn >> 32ll), (uint32_t) local_wwpn);
		return (-1);
	}

	return (remote_wwpn > local_wwpn);
}

/**
 * @ingroup p2p_sm
 * @brief Point-to-point state machine: Wait for the domain attach to complete.
 *
 * @par Description
 * Once the domain attach has completed, a PLOGI is sent (if we're the
 * winning point-to-point node).
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_p2p_wait_domain_attach(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
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
		ocs_sport_t *sport = node->sport;
		ocs_node_t *rnode;

		/* this transient node (SID=0 (recv'd FLOGI) or DID=fabric (sent FLOGI))
		 * is the p2p winner, will use a separate node to send PLOGI to peer
		 */
		ocs_assert (node->sport->p2p_winner, NULL);

		rnode = ocs_node_find(sport, node->sport->p2p_remote_port_id);
		if (rnode != NULL) {
			/* the "other" transient p2p node has already kicked off the
			 * new node from which PLOGI is sent */
			node_printf(node, "Node with fc_id x%x already exists\n", rnode->rnode.fc_id);
			ocs_assert (rnode != node, NULL);
		} else {
			/* create new node (SID=1, DID=2) from which to send PLOGI */
			rnode = ocs_node_alloc(sport, sport->p2p_remote_port_id, FALSE, FALSE);
			if (rnode == NULL) {
				ocs_log_err(ocs, "node alloc failed\n");
				return NULL;
			}

			ocs_fabric_notify_topology(node);
			/* sm: allocate p2p remote node */
			ocs_node_transition(rnode, __ocs_p2p_rnode_init, NULL);
		}

		/* the transient node (SID=0 or DID=fabric) has served its purpose */
		if (node->rnode.fc_id == 0) {
			/* if this is the SID=0 node, move to the init state in case peer
			 * has restarted FLOGI discovery and FLOGI is pending
			 */
			/* don't send PLOGI on ocs_d_init entry */
			ocs_node_init_device(node, FALSE);
		} else {
			/* if this is the DID=fabric node (we initiated FLOGI), shut it down */
			node->shutdown_reason = OCS_NODE_SHUTDOWN_DEFAULT;
			ocs_fabric_initiate_shutdown(node);
		}
		break;
	}

	default:
		__ocs_fabric_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup p2p_sm
 * @brief Point-to-point state machine: Remote node initialization state.
 *
 * @par Description
 * This state is entered after winning point-to-point, and the remote node
 * is instantiated.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_p2p_rnode_init(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	ocs_node_cb_t *cbdata = arg;
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_ENTER:
		/* sm: / send PLOGI */
		ocs_send_plogi(node, OCS_FC_ELS_SEND_DEFAULT_TIMEOUT, OCS_FC_ELS_DEFAULT_RETRIES, NULL, NULL);
		ocs_node_transition(node, __ocs_p2p_wait_plogi_rsp, NULL);
		break;

	case OCS_EVT_ABTS_RCVD:
		/* sm: send BA_ACC */
		ocs_bls_send_acc_hdr(cbdata->io, cbdata->header->dma.virt);
		break;

	default:
		__ocs_fabric_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup p2p_sm
 * @brief Point-to-point node state machine: Wait for the FLOGI accept completion.
 *
 * @par Description
 * Wait for the FLOGI accept completion.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_p2p_wait_flogi_acc_cmpl(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	ocs_node_cb_t *cbdata = arg;
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
		ocs_assert(node->els_cmpl_cnt, NULL);
		node->els_cmpl_cnt--;

		/* sm: if p2p_winner / domain_attach */
		if (node->sport->p2p_winner) {
			ocs_node_transition(node, __ocs_p2p_wait_domain_attach, NULL);
			if (node->sport->domain->attached &&
			    !(node->sport->domain->domain_notify_pend)) {
				node_printf(node, "Domain already attached\n");
				ocs_node_post_event(node, OCS_EVT_DOMAIN_ATTACH_OK, NULL);
			}
		} else {
			/* this node has served its purpose; we'll expect a PLOGI on a separate
			 * node (remote SID=0x1); return this node to init state in case peer
			 * restarts discovery -- it may already have (pending frames may exist).
			 */
			/* don't send PLOGI on ocs_d_init entry */
			ocs_node_init_device(node, FALSE);
		}
		break;

	case OCS_EVT_SRRS_ELS_CMPL_FAIL:
		/* LS_ACC failed, possibly due to link down; shutdown node and wait
		 * for FLOGI discovery to restart */
		node_printf(node, "FLOGI LS_ACC failed, shutting down\n");
		ocs_assert(node->els_cmpl_cnt, NULL);
		node->els_cmpl_cnt--;
		node->shutdown_reason = OCS_NODE_SHUTDOWN_DEFAULT;
		ocs_fabric_initiate_shutdown(node);
		break;

	case OCS_EVT_ABTS_RCVD: {
		/* sm: / send BA_ACC */
		ocs_bls_send_acc_hdr(cbdata->io, cbdata->header->dma.virt);
		break;
	}

	default:
		__ocs_fabric_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}


/**
 * @ingroup p2p_sm
 * @brief Point-to-point node state machine: Wait for a PLOGI response
 * as a point-to-point winner.
 *
 * @par Description
 * Wait for a PLOGI response from the remote node as a point-to-point winner.
 * Submit node attach request to the HW.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_p2p_wait_plogi_rsp(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	int32_t rc;
	ocs_node_cb_t *cbdata = arg;
	std_node_state_decl();

	node_sm_trace();

	switch(evt) {
	case OCS_EVT_SRRS_ELS_REQ_OK: {
		if (node_check_els_req(ctx, evt, arg, FC_ELS_CMD_PLOGI, __ocs_fabric_common, __func__)) {
			return NULL;
		}
		ocs_assert(node->els_req_cnt, NULL);
		node->els_req_cnt--;
		/* sm: / save sparams, ocs_node_attach */
		ocs_node_save_sparms(node, cbdata->els->els_rsp.virt);
		rc = ocs_node_attach(node);
		ocs_node_transition(node, __ocs_p2p_wait_node_attach, NULL);
		if (rc == OCS_HW_RTN_SUCCESS_SYNC) {
			ocs_node_post_event(node, OCS_EVT_NODE_ATTACH_OK, NULL);
		}
		break;
	}
	case OCS_EVT_SRRS_ELS_REQ_FAIL: {
		if (node_check_els_req(ctx, evt, arg, FC_ELS_CMD_PLOGI, __ocs_fabric_common, __func__)) {
			return NULL;
		}
		node_printf(node, "PLOGI failed, shutting down\n");
		ocs_assert(node->els_req_cnt, NULL);
		node->els_req_cnt--;
		node->shutdown_reason = OCS_NODE_SHUTDOWN_DEFAULT;
		ocs_fabric_initiate_shutdown(node);
		break;
	}

	case OCS_EVT_PLOGI_RCVD: {
		fc_header_t *hdr = cbdata->header->dma.virt;
		/* if we're in external loopback mode, just send LS_ACC */
		if (node->ocs->external_loopback) {
			ocs_send_plogi_acc(cbdata->io, ocs_be16toh(hdr->ox_id), NULL, NULL);
			break;
		} else{
			/* if this isn't external loopback, pass to default handler */
			__ocs_fabric_common(__func__, ctx, evt, arg);
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
		ocs_send_ls_acc_after_attach(cbdata->io, cbdata->header->dma.virt, OCS_NODE_SEND_LS_ACC_PRLI);
		ocs_node_transition(node, __ocs_p2p_wait_plogi_rsp_recvd_prli, NULL);
		break;
	default:
		__ocs_fabric_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup p2p_sm
 * @brief Point-to-point node state machine: Waiting on a response for a
 *	sent PLOGI.
 *
 * @par Description
 * State is entered when the point-to-point winner has sent
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
__ocs_p2p_wait_plogi_rsp_recvd_prli(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
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
		if (node_check_els_req(ctx, evt, arg, FC_ELS_CMD_PLOGI, __ocs_fabric_common, __func__)) {
			return NULL;
		}
		ocs_assert(node->els_req_cnt, NULL);
		node->els_req_cnt--;
		/* sm: / save sparams, ocs_node_attach */
		ocs_node_save_sparms(node, cbdata->els->els_rsp.virt);
		ocs_display_sparams(node->display_name, "plogi rcvd resp", 0, NULL,
			((uint8_t*)cbdata->els->els_rsp.virt) + 4);
		rc = ocs_node_attach(node);
		ocs_node_transition(node, __ocs_p2p_wait_node_attach, NULL);
		if (rc == OCS_HW_RTN_SUCCESS_SYNC) {
			ocs_node_post_event(node, OCS_EVT_NODE_ATTACH_OK, NULL);
		}
		break;

	case OCS_EVT_SRRS_ELS_REQ_FAIL:	/* PLOGI response received */
	case OCS_EVT_SRRS_ELS_REQ_RJT:
		/* PLOGI failed, shutdown the node */
		if (node_check_els_req(ctx, evt, arg, FC_ELS_CMD_PLOGI, __ocs_fabric_common, __func__)) {
			return NULL;
		}
		ocs_assert(node->els_req_cnt, NULL);
		node->els_req_cnt--;
		node->shutdown_reason = OCS_NODE_SHUTDOWN_DEFAULT;
		ocs_fabric_initiate_shutdown(node);
		break;

	default:
		__ocs_fabric_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @ingroup p2p_sm
 * @brief Point-to-point node state machine: Wait for a point-to-point node attach
 * to complete.
 *
 * @par Description
 * Waits for the point-to-point node attach to complete.
 *
 * @param ctx Remote node state machine context.
 * @param evt Event to process.
 * @param arg Per event optional argument.
 *
 * @return Returns NULL.
 */

void *
__ocs_p2p_wait_node_attach(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg)
{
	ocs_node_cb_t *cbdata = arg;
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
		case OCS_NODE_SEND_LS_ACC_PRLI: {
			ocs_d_send_prli_rsp(node->ls_acc_io, node->ls_acc_oxid);
			node->send_ls_acc = OCS_NODE_SEND_LS_ACC_NONE;
			node->ls_acc_io = NULL;
			break;
		}
		case OCS_NODE_SEND_LS_ACC_PLOGI: /* Can't happen in P2P */
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
		node_printf(node, "Node attach failed\n");
		node->shutdown_reason = OCS_NODE_SHUTDOWN_DEFAULT;
		ocs_fabric_initiate_shutdown(node);
		break;

	case OCS_EVT_SHUTDOWN:
		node_printf(node, "%s received\n", ocs_sm_event_name(evt));
		node->shutdown_reason = OCS_NODE_SHUTDOWN_DEFAULT;
		ocs_node_transition(node, __ocs_fabric_wait_attach_evt_shutdown, NULL);
		break;
	case OCS_EVT_PRLI_RCVD:
		node_printf(node, "%s: PRLI received before node is attached\n", ocs_sm_event_name(evt));
		ocs_process_prli_payload(node, cbdata->payload->dma.virt);
		ocs_send_ls_acc_after_attach(cbdata->io, cbdata->header->dma.virt, OCS_NODE_SEND_LS_ACC_PRLI);
		break;
	default:
		__ocs_fabric_common(__func__, ctx, evt, arg);
		return NULL;
	}

	return NULL;
}

/**
 * @brief Start up the name services node.
 *
 * @par Description
 * Allocates and starts up the name services node.
 *
 * @param sport Pointer to the sport structure.
 *
 * @return Returns 0 on success, or a negative error value on failure.
 */

static int32_t
ocs_start_ns_node(ocs_sport_t *sport)
{
	ocs_node_t *ns;

	/* Instantiate a name services node */
	ns = ocs_node_find(sport, FC_ADDR_NAMESERVER);
	if (ns == NULL) {
		ns = ocs_node_alloc(sport, FC_ADDR_NAMESERVER, FALSE, FALSE);
		if (ns == NULL) {
			return -1;
		}
	}
	/* TODO: for found ns, should we be transitioning from here?
	 * breaks transition only 1. from within state machine or
	 * 2. if after alloc 
	 */
	if (ns->ocs->nodedb_mask & OCS_NODEDB_PAUSE_NAMESERVER) {
		ocs_node_pause(ns, __ocs_ns_init);
	} else {
		ocs_node_transition(ns, __ocs_ns_init, NULL);
	}
	return 0;
}

/**
 * @brief Start up the fabric controller node.
 *
 * @par Description
 * Allocates and starts up the fabric controller node.
 *
 * @param sport Pointer to the sport structure.
 *
 * @return Returns 0 on success, or a negative error value on failure.
 */

static int32_t
ocs_start_fabctl_node(ocs_sport_t *sport)
{
	ocs_node_t *fabctl;

	fabctl = ocs_node_find(sport, FC_ADDR_CONTROLLER);
	if (fabctl == NULL) {
		fabctl = ocs_node_alloc(sport, FC_ADDR_CONTROLLER, FALSE, FALSE);
		if (fabctl == NULL) {
			return -1;
		}
	}
	/* TODO: for found ns, should we be transitioning from here?
	 * breaks transition only 1. from within state machine or
	 * 2. if after alloc
	 */
	ocs_node_transition(fabctl, __ocs_fabctl_init, NULL);
	return 0;
}

/**
 * @brief Process the GIDPT payload.
 *
 * @par Description
 * The GIDPT payload is parsed, and new nodes are created, as needed.
 *
 * @param node Pointer to the node structure.
 * @param gidpt Pointer to the GIDPT payload.
 * @param gidpt_len Payload length
 *
 * @return Returns 0 on success, or a negative error value on failure.
 */

static int32_t
ocs_process_gidpt_payload(ocs_node_t *node, fcct_gidpt_acc_t *gidpt, uint32_t gidpt_len)
{
	uint32_t i;
	uint32_t j;
	ocs_node_t *newnode;
	ocs_sport_t *sport = node->sport;
	ocs_t *ocs = node->ocs;
	uint32_t port_id;
	uint32_t port_count;
	ocs_node_t *n;
	ocs_node_t **active_nodes;
	uint32_t portlist_count;
	uint16_t residual;

	residual = ocs_be16toh(gidpt->hdr.max_residual_size);

	if (residual != 0) {
		ocs_log_debug(node->ocs, "residual is %u words\n", residual);
	}

	if (ocs_be16toh(gidpt->hdr.cmd_rsp_code) == FCCT_HDR_CMDRSP_REJECT) {
		node_printf(node, "GIDPT request failed: rsn x%x rsn_expl x%x\n",
			gidpt->hdr.reason_code, gidpt->hdr.reason_code_explanation);
		return -1;
	}

	portlist_count = (gidpt_len - sizeof(fcct_iu_header_t)) / sizeof(gidpt->port_list);

	/* Count the number of nodes */
	port_count = 0;
	ocs_sport_lock(sport);
		ocs_list_foreach(&sport->node_list, n) {
			port_count ++;
		}

		/* Allocate a buffer for all nodes */
		active_nodes = ocs_malloc(node->ocs, port_count * sizeof(*active_nodes), OCS_M_NOWAIT | OCS_M_ZERO);
		if (active_nodes == NULL) {
			node_printf(node, "ocs_malloc failed\n");
			ocs_sport_unlock(sport);
			return -1;
		}

		/* Fill buffer with fc_id of active nodes */
		i = 0;
		ocs_list_foreach(&sport->node_list, n) {
			port_id = n->rnode.fc_id;
			switch (port_id) {
			case FC_ADDR_FABRIC:
			case FC_ADDR_CONTROLLER:
			case FC_ADDR_NAMESERVER:
				break;
			default:
				if (!FC_ADDR_IS_DOMAIN_CTRL(port_id)) {
					active_nodes[i++] = n;
				}
				break;
			}
		}

		/* update the active nodes buffer */
		for (i = 0; i < portlist_count; i ++) {
			port_id = fc_be24toh(gidpt->port_list[i].port_id);

			for (j = 0; j < port_count; j ++) {
				if ((active_nodes[j] != NULL) && (port_id == active_nodes[j]->rnode.fc_id)) {
					active_nodes[j] = NULL;
				}
			}

			if (gidpt->port_list[i].ctl & FCCT_GID_PT_LAST_ID)
				break;
		}

		/* Those remaining in the active_nodes[] are now gone ! */
		for (i = 0; i < port_count; i ++) {
			/* if we're an initiator and the remote node is a target, then
			 * post the node missing event.   if we're target and we have enabled
			 * target RSCN, then post the node missing event.
			 */
			if (active_nodes[i] != NULL) {
				if ((node->sport->enable_ini && active_nodes[i]->targ) ||
				    (node->sport->enable_tgt && enable_target_rscn(ocs))) {
					ocs_node_post_event(active_nodes[i], OCS_EVT_NODE_MISSING, NULL);
				} else {
					node_printf(node, "GID_PT: skipping non-tgt port_id x%06x\n",
						active_nodes[i]->rnode.fc_id);
				}
			}
		}
		ocs_free(ocs, active_nodes, port_count * sizeof(*active_nodes));

		for(i = 0; i < portlist_count; i ++) {
			uint32_t port_id = fc_be24toh(gidpt->port_list[i].port_id);

			/* node_printf(node, "GID_PT: port_id x%06x\n", port_id); */

			/* Don't create node for ourselves or the associated NPIV ports */
			if (port_id != node->rnode.sport->fc_id && !ocs_sport_find(sport->domain, port_id)) {
				newnode = ocs_node_find(sport, port_id);
				if (newnode) {
					/* TODO: what if node deleted here?? */
					if (node->sport->enable_ini && newnode->targ) {
						ocs_node_post_event(newnode, OCS_EVT_NODE_REFOUND, NULL);
					}
					/* original code sends ADISC, has notion of "refound" */
				} else {
					if (node->sport->enable_ini) {
						newnode = ocs_node_alloc(sport, port_id, 0, 0);
						if (newnode == NULL) {
							ocs_log_err(ocs, "ocs_node_alloc() failed\n");
							ocs_sport_unlock(sport);
							return -1;
						}
						/* send PLOGI automatically if initiator */
						ocs_node_init_device(newnode, TRUE);
					}
				}
			}

			if (gidpt->port_list[i].ctl & FCCT_GID_PT_LAST_ID) {
				break;
			}
		}
	ocs_sport_unlock(sport);
	return 0;
}

/**
 * @brief Set up the domain point-to-point parameters.
 *
 * @par Description
 * The remote node service parameters are examined, and various point-to-point
 * variables are set.
 *
 * @param sport Pointer to the sport object.
 *
 * @return Returns 0 on success, or a negative error value on failure.
 */

int32_t
ocs_p2p_setup(ocs_sport_t *sport)
{
	ocs_t *ocs = sport->ocs;
	int32_t rnode_winner;
	rnode_winner = ocs_rnode_is_winner(sport);

	/* set sport flags to indicate p2p "winner" */
	if (rnode_winner == 1) {
		sport->p2p_remote_port_id = 0;
		sport->p2p_port_id = 0;
		sport->p2p_winner = FALSE;
	} else if (rnode_winner == 0) {
		sport->p2p_remote_port_id = 2;
		sport->p2p_port_id = 1;
		sport->p2p_winner = TRUE;
	} else {
		/* no winner; only okay if external loopback enabled */
		if (sport->ocs->external_loopback) {
			/*
			 * External loopback mode enabled; local sport and remote node
			 * will be registered with an NPortID = 1;
			 */
			ocs_log_debug(ocs, "External loopback mode enabled\n");
			sport->p2p_remote_port_id = 1;
			sport->p2p_port_id = 1;
			sport->p2p_winner = TRUE;
		} else {
			ocs_log_warn(ocs, "failed to determine p2p winner\n");
			return rnode_winner;
		}
	}
	return 0;
}

/**
 * @brief Process the FABCTL node RSCN.
 *
 * <h3 class="desc">Description</h3>
 * Processes the FABCTL node RSCN payload, simply passes the event to the name server.
 *
 * @param node Pointer to the node structure.
 * @param cbdata Callback data to pass forward.
 *
 * @return None.
 */

static void
ocs_process_rscn(ocs_node_t *node, ocs_node_cb_t *cbdata)
{
	ocs_t *ocs = node->ocs;
	ocs_sport_t *sport = node->sport;
	ocs_node_t *ns;

	/* Forward this event to the name-services node */
	ns = ocs_node_find(sport, FC_ADDR_NAMESERVER);
	if (ns != NULL)  {
		ocs_node_post_event(ns, OCS_EVT_RSCN_RCVD, cbdata);
	} else {
		ocs_log_warn(ocs, "can't find name server node\n");
	}
}
