/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2001-2007, by Cisco Systems, Inc. All rights reserved.
 * Copyright (c) 2008-2012, by Randall Stewart. All rights reserved.
 * Copyright (c) 2008-2012, by Michael Tuexen. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * a) Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * b) Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the distribution.
 *
 * c) Neither the name of Cisco Systems, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifndef _NETINET_SCTP_INDATA_H_
#define _NETINET_SCTP_INDATA_H_

#if defined(_KERNEL) || defined(__Userspace__)

struct sctp_queued_to_read *
sctp_build_readq_entry(struct sctp_tcb *stcb,
    struct sctp_nets *net,
    uint32_t tsn, uint32_t ppid,
    uint32_t context, uint16_t sid,
    uint32_t mid, uint8_t flags,
    struct mbuf *dm);


#define sctp_build_readq_entry_mac(_ctl, in_it, context, net, tsn, ppid, sid, flags, dm, tfsn, mid) do { \
	if (_ctl) { \
		atomic_add_int(&((net)->ref_count), 1); \
		memset(_ctl, 0, sizeof(struct sctp_queued_to_read)); \
		(_ctl)->sinfo_stream = sid; \
		TAILQ_INIT(&_ctl->reasm); \
		(_ctl)->top_fsn = tfsn; \
		(_ctl)->mid = mid; \
		(_ctl)->sinfo_flags = (flags << 8); \
		(_ctl)->sinfo_ppid = ppid; \
		(_ctl)->sinfo_context = context; \
		(_ctl)->fsn_included = 0xffffffff; \
		(_ctl)->top_fsn = 0xffffffff; \
		(_ctl)->sinfo_tsn = tsn; \
		(_ctl)->sinfo_cumtsn = tsn; \
		(_ctl)->sinfo_assoc_id = sctp_get_associd((in_it)); \
		(_ctl)->whoFrom = net; \
		(_ctl)->data = dm; \
		(_ctl)->stcb = (in_it); \
		(_ctl)->port_from = (in_it)->rport; \
	} \
} while (0)



struct mbuf *
sctp_build_ctl_nchunk(struct sctp_inpcb *inp,
    struct sctp_sndrcvinfo *sinfo);

void sctp_set_rwnd(struct sctp_tcb *, struct sctp_association *);

uint32_t
         sctp_calc_rwnd(struct sctp_tcb *stcb, struct sctp_association *asoc);

void
sctp_express_handle_sack(struct sctp_tcb *stcb, uint32_t cumack,
    uint32_t rwnd, int *abort_now, int ecne_seen);

void
sctp_handle_sack(struct mbuf *m, int offset_seg, int offset_dup,
    struct sctp_tcb *stcb,
    uint16_t num_seg, uint16_t num_nr_seg, uint16_t num_dup,
    int *abort_now, uint8_t flags,
    uint32_t cum_ack, uint32_t rwnd, int ecne_seen);

/* draft-ietf-tsvwg-usctp */
void
sctp_handle_forward_tsn(struct sctp_tcb *,
    struct sctp_forward_tsn_chunk *, int *, struct mbuf *, int);

struct sctp_tmit_chunk *sctp_try_advance_peer_ack_point(struct sctp_tcb *, struct sctp_association *);

void sctp_service_queues(struct sctp_tcb *, struct sctp_association *);

void
     sctp_update_acked(struct sctp_tcb *, struct sctp_shutdown_chunk *, int *);

int
sctp_process_data(struct mbuf **, int, int *, int,
    struct sctp_inpcb *, struct sctp_tcb *,
    struct sctp_nets *, uint32_t *);

void sctp_slide_mapping_arrays(struct sctp_tcb *stcb);

void sctp_sack_check(struct sctp_tcb *, int);

#endif
#endif
