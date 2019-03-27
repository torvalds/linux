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

#ifndef _NETINET_SCTP_ASCONF_H_
#define _NETINET_SCTP_ASCONF_H_

#if defined(_KERNEL) || defined(__Userspace__)

/*
 * function prototypes
 */
extern void sctp_asconf_cleanup(struct sctp_tcb *, struct sctp_nets *);

extern struct mbuf *sctp_compose_asconf(struct sctp_tcb *, int *, int);

extern void
sctp_handle_asconf(struct mbuf *, unsigned int, struct sockaddr *,
    struct sctp_asconf_chunk *, struct sctp_tcb *, int);

extern void
sctp_handle_asconf_ack(struct mbuf *, int, struct sctp_asconf_ack_chunk *,
    struct sctp_tcb *, struct sctp_nets *, int *);

extern uint32_t
sctp_addr_mgmt_ep_sa(struct sctp_inpcb *, struct sockaddr *,
    uint32_t, uint32_t, struct sctp_ifa *);


extern int
sctp_asconf_iterator_ep(struct sctp_inpcb *inp, void *ptr,
    uint32_t val);
extern void
sctp_asconf_iterator_stcb(struct sctp_inpcb *inp,
    struct sctp_tcb *stcb,
    void *ptr, uint32_t type);
extern void sctp_asconf_iterator_end(void *ptr, uint32_t val);


extern int32_t
sctp_set_primary_ip_address_sa(struct sctp_tcb *,
    struct sockaddr *);

extern void
sctp_check_address_list(struct sctp_tcb *, struct mbuf *, int, int,
    struct sockaddr *, uint16_t, uint16_t, uint16_t, uint16_t);

extern void
     sctp_assoc_immediate_retrans(struct sctp_tcb *, struct sctp_nets *);
extern void
     sctp_net_immediate_retrans(struct sctp_tcb *, struct sctp_nets *);

extern void
sctp_asconf_send_nat_state_update(struct sctp_tcb *stcb,
    struct sctp_nets *net);

extern int
    sctp_is_addr_pending(struct sctp_tcb *, struct sctp_ifa *);
#endif				/* _KERNEL */

#endif				/* !_NETINET_SCTP_ASCONF_H_ */
