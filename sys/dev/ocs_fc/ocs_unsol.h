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
 * Declarations for the interface exported by ocs_unsol.c
 */

#if !defined(__OSC_UNSOL_H__)
#define __OSC_UNSOL_H__

extern int32_t ocs_unsol_rq_thread(ocs_thread_t *mythread);
extern int32_t ocs_unsolicited_cb(void *arg, ocs_hw_sequence_t *seq);
extern int32_t ocs_node_purge_pending(ocs_node_t *node);
extern int32_t ocs_process_node_pending(ocs_node_t *node);
extern int32_t ocs_domain_process_pending(ocs_domain_t *domain);
extern int32_t ocs_domain_purge_pending(ocs_domain_t *domain);
extern int32_t ocs_dispatch_unsolicited_bls(ocs_node_t *node, ocs_hw_sequence_t *seq);
extern void ocs_domain_hold_frames(ocs_domain_t *domain);
extern void ocs_domain_accept_frames(ocs_domain_t *domain);
extern void ocs_seq_coalesce_cleanup(ocs_hw_io_t *hio, uint8_t abort_io);
extern int32_t ocs_sframe_send_bls_acc(ocs_node_t *node,  ocs_hw_sequence_t *seq);
#endif 
