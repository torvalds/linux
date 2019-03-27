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
 * Node state machine functions for remote device node sm
 */

#if !defined(__OCS_DEVICE_H__)
#define __OCS_DEVICE_H__

/***************************************************************************
 * Receive queue configuration
 */

#ifndef OCS_FC_RQ_SIZE_DEFAULT
#define OCS_FC_RQ_SIZE_DEFAULT			1024
#endif


/***************************************************************************
 * IO Configuration
 */

/**
 * @brief Defines the number of SGLs allocated on each IO object
 */
#ifndef OCS_FC_MAX_SGL
#define OCS_FC_MAX_SGL		128
#endif


/***************************************************************************
 * DIF Configuration
 */

/**
 * @brief Defines the DIF seed value used for the CRC calculation.
 */
#ifndef OCS_FC_DIF_SEED
#define OCS_FC_DIF_SEED		0
#endif

/***************************************************************************
 * Timeouts
 */
#ifndef OCS_FC_ELS_SEND_DEFAULT_TIMEOUT
#define OCS_FC_ELS_SEND_DEFAULT_TIMEOUT		0
#endif

#ifndef OCS_FC_ELS_CT_SEND_DEFAULT_TIMEOUT
#define OCS_FC_ELS_CT_SEND_DEFAULT_TIMEOUT	5
#endif

#ifndef OCS_FC_ELS_DEFAULT_RETRIES
#define OCS_FC_ELS_DEFAULT_RETRIES		3
#endif

#ifndef OCS_FC_FLOGI_TIMEOUT_SEC
#define OCS_FC_FLOGI_TIMEOUT_SEC		5 /* shorter than default */
#endif

#ifndef OCS_FC_DOMAIN_SHUTDOWN_TIMEOUT_USEC
#define OCS_FC_DOMAIN_SHUTDOWN_TIMEOUT_USEC	30000000 /* 30 seconds */
#endif

/***************************************************************************
 * Watermark
 */
#ifndef OCS_WATERMARK_HIGH_PCT
#define OCS_WATERMARK_HIGH_PCT			90
#endif
#ifndef OCS_WATERMARK_LOW_PCT
#define OCS_WATERMARK_LOW_PCT			80
#endif
#ifndef OCS_IO_WATERMARK_PER_INITIATOR
#define OCS_IO_WATERMARK_PER_INITIATOR		8
#endif

extern void ocs_node_init_device(ocs_node_t *node, int send_plogi);
extern void ocs_process_prli_payload(ocs_node_t *node, fc_prli_payload_t *prli);
extern void ocs_d_send_prli_rsp(ocs_io_t *io, uint16_t ox_id);
extern void ocs_send_ls_acc_after_attach(ocs_io_t *io, fc_header_t *hdr, ocs_node_send_ls_acc_e ls);

extern void*__ocs_d_wait_loop(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
extern void*__ocs_d_wait_plogi_acc_cmpl(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
extern void*__ocs_d_init(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
extern void*__ocs_d_wait_plogi_rsp(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
extern void*__ocs_d_wait_plogi_rsp_recvd_prli(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
extern void*__ocs_d_wait_domain_attach(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
extern void*__ocs_d_wait_topology_notify(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
extern void*__ocs_d_wait_node_attach(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
extern void*__ocs_d_wait_attach_evt_shutdown(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
extern void*__ocs_d_initiate_shutdown(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
extern void*__ocs_d_port_logged_in(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
extern void*__ocs_d_wait_logo_acc_cmpl(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
extern void*__ocs_d_device_ready(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
extern void*__ocs_d_device_gone(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
extern void*__ocs_d_wait_adisc_rsp(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
extern void*__ocs_d_wait_logo_rsp(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);
extern void*__ocs_d_wait_prlo_rsp(ocs_sm_ctx_t *ctx, ocs_sm_event_t evt, void *arg);

#endif /* __OCS_DEVICE_H__ */
