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

#ifndef _NETINET_SCTP_TIMER_H_
#define _NETINET_SCTP_TIMER_H_

#if defined(_KERNEL) || defined(__Userspace__)

#define SCTP_RTT_SHIFT 3
#define SCTP_RTT_VAR_SHIFT 2

struct sctp_nets *
sctp_find_alternate_net(struct sctp_tcb *,
    struct sctp_nets *, int mode);

int
sctp_t3rxt_timer(struct sctp_inpcb *, struct sctp_tcb *,
    struct sctp_nets *);
int
sctp_t1init_timer(struct sctp_inpcb *, struct sctp_tcb *,
    struct sctp_nets *);
int
sctp_shutdown_timer(struct sctp_inpcb *, struct sctp_tcb *,
    struct sctp_nets *);
int
sctp_heartbeat_timer(struct sctp_inpcb *, struct sctp_tcb *,
    struct sctp_nets *);

int
sctp_cookie_timer(struct sctp_inpcb *, struct sctp_tcb *,
    struct sctp_nets *);

void
sctp_pathmtu_timer(struct sctp_inpcb *, struct sctp_tcb *,
    struct sctp_nets *);

int
sctp_shutdownack_timer(struct sctp_inpcb *, struct sctp_tcb *,
    struct sctp_nets *);
int
sctp_strreset_timer(struct sctp_inpcb *inp, struct sctp_tcb *stcb,
    struct sctp_nets *net);

int
sctp_asconf_timer(struct sctp_inpcb *, struct sctp_tcb *,
    struct sctp_nets *);

void
sctp_delete_prim_timer(struct sctp_inpcb *, struct sctp_tcb *,
    struct sctp_nets *);

void
sctp_autoclose_timer(struct sctp_inpcb *, struct sctp_tcb *,
    struct sctp_nets *net);

void sctp_audit_retranmission_queue(struct sctp_association *);

void sctp_iterator_timer(struct sctp_iterator *it);


#endif
#endif
