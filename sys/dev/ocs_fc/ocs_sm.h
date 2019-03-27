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
 * Generic state machine framework declarations.
 */

#ifndef _OCS_SM_H
#define _OCS_SM_H

/**
 * State Machine (SM) IDs.
 */
enum {
	OCS_SM_COMMON = 0,
	OCS_SM_DOMAIN,
	OCS_SM_PORT,
	OCS_SM_LOGIN,
	OCS_SM_LAST
};

#define OCS_SM_EVENT_SHIFT		24
#define OCS_SM_EVENT_START(id)		((id) << OCS_SM_EVENT_SHIFT)

extern const char *ocs_sm_id[];		/**< String format of the above enums. */

/**
 * State Machine events.
 */
typedef enum {
	/* Common Events */
	OCS_EVT_ENTER = OCS_SM_EVENT_START(OCS_SM_COMMON),
	OCS_EVT_REENTER,
	OCS_EVT_EXIT,
	OCS_EVT_SHUTDOWN,
	OCS_EVT_ALL_CHILD_NODES_FREE,
	OCS_EVT_RESUME,
	OCS_EVT_TIMER_EXPIRED,

	/* Domain Events */
	OCS_EVT_RESPONSE = OCS_SM_EVENT_START(OCS_SM_DOMAIN),
	OCS_EVT_ERROR,

	OCS_EVT_DOMAIN_FOUND,
	OCS_EVT_DOMAIN_ALLOC_OK,
	OCS_EVT_DOMAIN_ALLOC_FAIL,
	OCS_EVT_DOMAIN_REQ_ATTACH,
	OCS_EVT_DOMAIN_ATTACH_OK,
	OCS_EVT_DOMAIN_ATTACH_FAIL,
	OCS_EVT_DOMAIN_LOST,
	OCS_EVT_DOMAIN_FREE_OK,
	OCS_EVT_DOMAIN_FREE_FAIL,
	OCS_EVT_HW_DOMAIN_REQ_ATTACH,
	OCS_EVT_HW_DOMAIN_REQ_FREE,

	/* Sport Events */
	OCS_EVT_SPORT_ALLOC_OK = OCS_SM_EVENT_START(OCS_SM_PORT),
	OCS_EVT_SPORT_ALLOC_FAIL,
	OCS_EVT_SPORT_ATTACH_OK,
	OCS_EVT_SPORT_ATTACH_FAIL,
	OCS_EVT_SPORT_FREE_OK,
	OCS_EVT_SPORT_FREE_FAIL,
	OCS_EVT_SPORT_TOPOLOGY_NOTIFY,
	OCS_EVT_HW_PORT_ALLOC_OK,
	OCS_EVT_HW_PORT_ALLOC_FAIL,
	OCS_EVT_HW_PORT_ATTACH_OK,
	OCS_EVT_HW_PORT_REQ_ATTACH,
	OCS_EVT_HW_PORT_REQ_FREE,
	OCS_EVT_HW_PORT_FREE_OK,

	/* Login Events */
	OCS_EVT_SRRS_ELS_REQ_OK = OCS_SM_EVENT_START(OCS_SM_LOGIN),
	OCS_EVT_SRRS_ELS_CMPL_OK,
	OCS_EVT_SRRS_ELS_REQ_FAIL,
	OCS_EVT_SRRS_ELS_CMPL_FAIL,
	OCS_EVT_SRRS_ELS_REQ_RJT,
	OCS_EVT_NODE_ATTACH_OK,
	OCS_EVT_NODE_ATTACH_FAIL,
	OCS_EVT_NODE_FREE_OK,
	OCS_EVT_NODE_FREE_FAIL,
	OCS_EVT_ELS_FRAME,
	OCS_EVT_ELS_REQ_TIMEOUT,
	OCS_EVT_ELS_REQ_ABORTED,
	OCS_EVT_ABORT_ELS,		/**< request an ELS IO be aborted */
	OCS_EVT_ELS_ABORT_CMPL,	        /**< ELS abort process complete */

	OCS_EVT_ABTS_RCVD,

	OCS_EVT_NODE_MISSING,		/**< node is not in the GID_PT payload */
	OCS_EVT_NODE_REFOUND,		/**< node is allocated and in the GID_PT payload */
	OCS_EVT_SHUTDOWN_IMPLICIT_LOGO,	/**< node shutting down due to PLOGI recvd (implicit logo) */
	OCS_EVT_SHUTDOWN_EXPLICIT_LOGO,	/**< node shutting down due to LOGO recvd/sent (explicit logo) */

	OCS_EVT_PLOGI_RCVD,
	OCS_EVT_FLOGI_RCVD,
	OCS_EVT_LOGO_RCVD,
	OCS_EVT_RRQ_RCVD,
	OCS_EVT_PRLI_RCVD,
	OCS_EVT_PRLO_RCVD,
	OCS_EVT_PDISC_RCVD,
	OCS_EVT_FDISC_RCVD,
	OCS_EVT_ADISC_RCVD,
	OCS_EVT_RSCN_RCVD,
	OCS_EVT_SCR_RCVD,
	OCS_EVT_ELS_RCVD,

	OCS_EVT_FCP_CMD_RCVD,

	/* Used by fabric emulation */
	OCS_EVT_RFT_ID_RCVD,
	OCS_EVT_RFF_ID_RCVD,
	OCS_EVT_GNN_ID_RCVD,
	OCS_EVT_GPN_ID_RCVD,
	OCS_EVT_GFPN_ID_RCVD,
	OCS_EVT_GFF_ID_RCVD,
	OCS_EVT_GID_FT_RCVD,
	OCS_EVT_GID_PT_RCVD,
	OCS_EVT_RPN_ID_RCVD,
	OCS_EVT_RNN_ID_RCVD,
	OCS_EVT_RCS_ID_RCVD,
	OCS_EVT_RSNN_NN_RCVD,
	OCS_EVT_RSPN_ID_RCVD,
	OCS_EVT_RHBA_RCVD,
	OCS_EVT_RPA_RCVD,

	OCS_EVT_GIDPT_DELAY_EXPIRED,

	/* SCSI Target Server events */
	OCS_EVT_ABORT_IO,
	OCS_EVT_ABORT_IO_NO_RESP,
	OCS_EVT_IO_CMPL,
	OCS_EVT_IO_CMPL_ERRORS,
	OCS_EVT_RESP_CMPL,
	OCS_EVT_ABORT_CMPL,
	OCS_EVT_NODE_ACTIVE_IO_LIST_EMPTY,
	OCS_EVT_NODE_DEL_INI_COMPLETE,
	OCS_EVT_NODE_DEL_TGT_COMPLETE,
	OCS_EVT_IO_ABORTED_BY_TMF,
	OCS_EVT_IO_ABORT_IGNORED,
	OCS_EVT_IO_FIRST_BURST,
	OCS_EVT_IO_FIRST_BURST_ERR,
	OCS_EVT_IO_FIRST_BURST_ABORTED,

	/* Must be last */
	OCS_EVT_LAST
} ocs_sm_event_t;

/* Declare ocs_sm_ctx_s */
typedef struct ocs_sm_ctx_s ocs_sm_ctx_t;

/* State machine state function */
typedef void *(*ocs_sm_function_t)(ocs_sm_ctx_t *, ocs_sm_event_t, void *);

/* State machine context header  */
struct ocs_sm_ctx_s {
	ocs_sm_function_t current_state;
	const char *description;
	void	*app;			/** Application-specific handle. */
};

extern int ocs_sm_post_event(ocs_sm_ctx_t *, ocs_sm_event_t, void *);
extern void ocs_sm_transition(ocs_sm_ctx_t *, ocs_sm_function_t, void *);
extern void ocs_sm_disable(ocs_sm_ctx_t *ctx);
extern const char *ocs_sm_event_name(ocs_sm_event_t evt);

#if 0
#define smtrace(sm)	ocs_log_debug(NULL, "%s: %-20s -->   %s\n", sm, ocs_sm_event_name(evt), __func__)
#else
#define smtrace(...)
#endif

#endif /* ! _OCS_SM_H */
