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
 * Generic state machine framework.
 */

#include "ocs_os.h"
#include "ocs_sm.h"

const char *ocs_sm_id[] = {
	"common",
	"domain",
	"login"
};

/**
 * @brief Post an event to a context.
 *
 * @param ctx State machine context
 * @param evt Event to post
 * @param data Event-specific data (if any)
 *
 * @return 0 if successfully posted event; -1 if state machine
 *         is disabled
 */
int
ocs_sm_post_event(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *data)
{
	if (ctx->current_state) {
		ctx->current_state(ctx, evt, data);
		return 0;
	} else {
		return -1;
	}
}

/**
 * @brief Transition to a new state.
 */
void
ocs_sm_transition(ocs_sm_ctx_t *ctx, ocs_sm_function_t state, void *data)
{
	if (ctx->current_state == state) {
		ocs_sm_post_event(ctx, OCS_EVT_REENTER, data);
	} else {
		ocs_sm_post_event(ctx, OCS_EVT_EXIT, data);
		ctx->current_state = state;
		ocs_sm_post_event(ctx, OCS_EVT_ENTER, data);
	}
}

/**
 * @brief Disable further state machine processing.
 */
void
ocs_sm_disable(ocs_sm_ctx_t *ctx)
{
	ctx->current_state = NULL;
}

const char *ocs_sm_event_name(ocs_sm_event_t evt)
{
	switch (evt) {
	#define RETEVT(x)	case x:		return #x;
	RETEVT(OCS_EVT_ENTER)
	RETEVT(OCS_EVT_REENTER)
	RETEVT(OCS_EVT_EXIT)
	RETEVT(OCS_EVT_SHUTDOWN)
	RETEVT(OCS_EVT_RESPONSE)
	RETEVT(OCS_EVT_RESUME)
	RETEVT(OCS_EVT_TIMER_EXPIRED)
	RETEVT(OCS_EVT_ERROR)
	RETEVT(OCS_EVT_SRRS_ELS_REQ_OK)
	RETEVT(OCS_EVT_SRRS_ELS_CMPL_OK)
	RETEVT(OCS_EVT_SRRS_ELS_REQ_FAIL)
	RETEVT(OCS_EVT_SRRS_ELS_CMPL_FAIL)
	RETEVT(OCS_EVT_SRRS_ELS_REQ_RJT)
	RETEVT(OCS_EVT_NODE_ATTACH_OK)
	RETEVT(OCS_EVT_NODE_ATTACH_FAIL)
	RETEVT(OCS_EVT_NODE_FREE_OK)
	RETEVT(OCS_EVT_ELS_REQ_TIMEOUT)
	RETEVT(OCS_EVT_ELS_REQ_ABORTED)
	RETEVT(OCS_EVT_ABORT_ELS)
	RETEVT(OCS_EVT_ELS_ABORT_CMPL)

	RETEVT(OCS_EVT_DOMAIN_FOUND)
	RETEVT(OCS_EVT_DOMAIN_ALLOC_OK)
	RETEVT(OCS_EVT_DOMAIN_ALLOC_FAIL)
	RETEVT(OCS_EVT_DOMAIN_REQ_ATTACH)
	RETEVT(OCS_EVT_DOMAIN_ATTACH_OK)
	RETEVT(OCS_EVT_DOMAIN_ATTACH_FAIL)
	RETEVT(OCS_EVT_DOMAIN_LOST)
	RETEVT(OCS_EVT_DOMAIN_FREE_OK)
	RETEVT(OCS_EVT_DOMAIN_FREE_FAIL)
	RETEVT(OCS_EVT_HW_DOMAIN_REQ_ATTACH)
	RETEVT(OCS_EVT_HW_DOMAIN_REQ_FREE)
	RETEVT(OCS_EVT_ALL_CHILD_NODES_FREE)

	RETEVT(OCS_EVT_SPORT_ALLOC_OK)
	RETEVT(OCS_EVT_SPORT_ALLOC_FAIL)
	RETEVT(OCS_EVT_SPORT_ATTACH_OK)
	RETEVT(OCS_EVT_SPORT_ATTACH_FAIL)
	RETEVT(OCS_EVT_SPORT_FREE_OK)
	RETEVT(OCS_EVT_SPORT_FREE_FAIL)
	RETEVT(OCS_EVT_SPORT_TOPOLOGY_NOTIFY)
	RETEVT(OCS_EVT_HW_PORT_ALLOC_OK)
	RETEVT(OCS_EVT_HW_PORT_ALLOC_FAIL)
	RETEVT(OCS_EVT_HW_PORT_ATTACH_OK)
	RETEVT(OCS_EVT_HW_PORT_REQ_ATTACH)
	RETEVT(OCS_EVT_HW_PORT_REQ_FREE)
	RETEVT(OCS_EVT_HW_PORT_FREE_OK)

	RETEVT(OCS_EVT_NODE_FREE_FAIL)

	RETEVT(OCS_EVT_ABTS_RCVD)

	RETEVT(OCS_EVT_NODE_MISSING)
	RETEVT(OCS_EVT_NODE_REFOUND)
	RETEVT(OCS_EVT_SHUTDOWN_IMPLICIT_LOGO)
	RETEVT(OCS_EVT_SHUTDOWN_EXPLICIT_LOGO)

	RETEVT(OCS_EVT_ELS_FRAME)
	RETEVT(OCS_EVT_PLOGI_RCVD)
	RETEVT(OCS_EVT_FLOGI_RCVD)
	RETEVT(OCS_EVT_LOGO_RCVD)
	RETEVT(OCS_EVT_PRLI_RCVD)
	RETEVT(OCS_EVT_PRLO_RCVD)
	RETEVT(OCS_EVT_PDISC_RCVD)
	RETEVT(OCS_EVT_FDISC_RCVD)
	RETEVT(OCS_EVT_ADISC_RCVD)
	RETEVT(OCS_EVT_RSCN_RCVD)
	RETEVT(OCS_EVT_SCR_RCVD)
	RETEVT(OCS_EVT_ELS_RCVD)
	RETEVT(OCS_EVT_LAST)
	RETEVT(OCS_EVT_FCP_CMD_RCVD)

	RETEVT(OCS_EVT_RFT_ID_RCVD)
	RETEVT(OCS_EVT_RFF_ID_RCVD)
	RETEVT(OCS_EVT_GNN_ID_RCVD)
	RETEVT(OCS_EVT_GPN_ID_RCVD)
	RETEVT(OCS_EVT_GFPN_ID_RCVD)
	RETEVT(OCS_EVT_GFF_ID_RCVD)
	RETEVT(OCS_EVT_GID_FT_RCVD)
	RETEVT(OCS_EVT_GID_PT_RCVD)
	RETEVT(OCS_EVT_RPN_ID_RCVD)
	RETEVT(OCS_EVT_RNN_ID_RCVD)
	RETEVT(OCS_EVT_RCS_ID_RCVD)
	RETEVT(OCS_EVT_RSNN_NN_RCVD)
	RETEVT(OCS_EVT_RSPN_ID_RCVD)
	RETEVT(OCS_EVT_RHBA_RCVD)
	RETEVT(OCS_EVT_RPA_RCVD)

	RETEVT(OCS_EVT_GIDPT_DELAY_EXPIRED)

	RETEVT(OCS_EVT_ABORT_IO)
	RETEVT(OCS_EVT_ABORT_IO_NO_RESP)
	RETEVT(OCS_EVT_IO_CMPL)
	RETEVT(OCS_EVT_IO_CMPL_ERRORS)
	RETEVT(OCS_EVT_RESP_CMPL)
	RETEVT(OCS_EVT_ABORT_CMPL)
	RETEVT(OCS_EVT_NODE_ACTIVE_IO_LIST_EMPTY)
	RETEVT(OCS_EVT_NODE_DEL_INI_COMPLETE)
	RETEVT(OCS_EVT_NODE_DEL_TGT_COMPLETE)
	RETEVT(OCS_EVT_IO_ABORTED_BY_TMF)
	RETEVT(OCS_EVT_IO_ABORT_IGNORED)
	RETEVT(OCS_EVT_IO_FIRST_BURST)
	RETEVT(OCS_EVT_IO_FIRST_BURST_ERR)
	RETEVT(OCS_EVT_IO_FIRST_BURST_ABORTED)

	default:
		break;
	#undef RETEVT
	}
	return "unknown";
}
