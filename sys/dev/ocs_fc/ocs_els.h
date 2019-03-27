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
 * Declarations for the interface exported by ocs_els.
 */

#if !defined(__OCS_ELS_H__)
#define __OCS_ELS_H__
#include "ocs.h"

#define OCS_ELS_RSP_LEN		1024
#define OCS_ELS_GID_PT_RSP_LEN	8096 /* Enough for 2K remote target nodes */

#define OCS_ELS_REQ_LEN		116 /*Max request length*/

typedef enum {
	OCS_ELS_ROLE_ORIGINATOR,
	OCS_ELS_ROLE_RESPONDER,
} ocs_els_role_e;

extern ocs_io_t *ocs_els_io_alloc(ocs_node_t *node, uint32_t reqlen, ocs_els_role_e role);
extern ocs_io_t *ocs_els_io_alloc_size(ocs_node_t *node, uint32_t reqlen, uint32_t rsplen, ocs_els_role_e role);
extern void ocs_els_io_free(ocs_io_t *els);

/* ELS command send */
typedef void (*els_cb_t)(ocs_node_t *node, ocs_node_cb_t *cbdata, void *arg);
extern ocs_io_t *ocs_send_plogi(ocs_node_t *node, uint32_t timeout_sec, uint32_t retries, els_cb_t cb, void *cbarg);
extern ocs_io_t *ocs_send_flogi(ocs_node_t *node, uint32_t timeout_sec, uint32_t retries, els_cb_t cb, void *cbarg);
extern ocs_io_t *ocs_send_fdisc(ocs_node_t *node, uint32_t timeout_sec, uint32_t retries, els_cb_t cb, void *cbarg);
extern ocs_io_t *ocs_send_prli(ocs_node_t *node, uint32_t timeout_sec, uint32_t retries, els_cb_t cb, void *cbarg);
extern ocs_io_t *ocs_send_prlo(ocs_node_t *node, uint32_t timeout_sec, uint32_t retries, els_cb_t cb, void *cbarg);
extern ocs_io_t *ocs_send_logo(ocs_node_t *node, uint32_t timeout_sec, uint32_t retries, els_cb_t cb, void *cbarg);
extern ocs_io_t *ocs_send_adisc(ocs_node_t *node, uint32_t timeout_sec, uint32_t retries, els_cb_t cb, void *cbarg);
extern ocs_io_t *ocs_send_pdisc(ocs_node_t *node, uint32_t timeout_sec, uint32_t retries, els_cb_t cb, void *cbarg);
extern ocs_io_t *ocs_send_scr(ocs_node_t *node, uint32_t timeout_sec, uint32_t retries, els_cb_t cb, void *cbarg);
extern ocs_io_t *ocs_send_rrq(ocs_node_t *node, uint32_t timeout_sec, uint32_t retries, els_cb_t cb, void *cbarg);
extern ocs_io_t *ocs_ns_send_rftid(ocs_node_t *node, uint32_t timeout_sec, uint32_t retries, els_cb_t cb, void *cbarg);
extern ocs_io_t *ocs_ns_send_rffid(ocs_node_t *node, uint32_t timeout_sec, uint32_t retries, els_cb_t cb, void *cbarg);
extern ocs_io_t *ocs_ns_send_gidpt(ocs_node_t *node, uint32_t timeout_sec, uint32_t retries, els_cb_t cb, void *cbarg);
extern ocs_io_t *ocs_send_rscn(ocs_node_t *node, uint32_t timeout_sec, uint32_t retries,
	void *port_ids, uint32_t port_ids_count, els_cb_t cb, void *cbarg);
extern void ocs_els_io_cleanup(ocs_io_t *els, ocs_sm_event_t node_evt, void *arg);

/* ELS acc send */
extern ocs_io_t *ocs_send_ls_acc(ocs_io_t *io, uint32_t ox_id, els_cb_t cb, void *cbarg);
extern ocs_io_t *ocs_send_ls_rjt(ocs_io_t *io, uint32_t ox_id, uint32_t reason_cod, uint32_t reason_code_expl,
		uint32_t vendor_unique, els_cb_t cb, void *cbarg);
extern ocs_io_t *ocs_send_flogi_p2p_acc(ocs_io_t *io, uint32_t ox_id, uint32_t s_id, els_cb_t cb, void *cbarg);
extern ocs_io_t *ocs_send_flogi_acc(ocs_io_t *io, uint32_t ox_id, uint32_t is_fport, els_cb_t cb, void *cbarg);
extern ocs_io_t *ocs_send_plogi_acc(ocs_io_t *io, uint32_t ox_id, els_cb_t cb, void *cbarg);
extern ocs_io_t *ocs_send_prli_acc(ocs_io_t *io, uint32_t ox_id, uint8_t fc_type, els_cb_t cb, void *cbarg);
extern ocs_io_t *ocs_send_logo_acc(ocs_io_t *io, uint32_t ox_id, els_cb_t cb, void *cbarg);
extern ocs_io_t *ocs_send_prlo_acc(ocs_io_t *io, uint32_t ox_id, uint8_t fc_type, els_cb_t cb, void *cbarg);
extern ocs_io_t *ocs_send_adisc_acc(ocs_io_t *io, uint32_t ox_id, els_cb_t cb, void *cbarg);
extern void ocs_ddump_els(ocs_textbuf_t *textbuf, ocs_io_t *els);

/* BLS acc send */
extern ocs_io_t *ocs_bls_send_acc_hdr(ocs_io_t *io, fc_header_t *hdr);

/* ELS IO state machine */
extern void ocs_els_post_event(ocs_io_t *els, ocs_sm_event_t evt, void *data);
extern void *__ocs_els_common(const char *funcname, ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
extern void *__ocs_els_init(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
extern void *__ocs_els_wait_resp(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
extern void *__ocs_els_aborting(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
extern void *__ocs_els_aborting_wait_req_cmpl(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
extern void *__ocs_els_aborting_wait_abort_cmpl(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
extern void *__ocs_els_retry(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
extern void * __ocs_els_aborted_delay_retry(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
extern void *__ocs_els_delay_retry(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);

/* Misc */
extern int32_t ocs_els_io_list_empty(ocs_node_t *node, ocs_list_t *list);

/* CT */
extern int32_t ocs_send_ct_rsp(ocs_io_t *io, uint32_t ox_id, fcct_iu_header_t *ct_hdr, uint32_t cmd_rsp_code, uint32_t reason_code, uint32_t reason_code_explanation);

#endif 
