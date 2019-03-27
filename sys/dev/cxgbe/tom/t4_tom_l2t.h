/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Chelsio Communications, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#ifndef __T4_TOM_L2T_H
#define __T4_TOM_L2T_H

#include "t4_l2t.h"

int t4_l2t_send_slow(struct adapter *, struct wrqe *, struct l2t_entry *);
struct l2t_entry *t4_l2t_get(struct port_info *, struct ifnet *,
    struct sockaddr *);
void t4_l2_update(struct toedev *, struct ifnet *, struct sockaddr *,
    uint8_t *, uint16_t);
int do_l2t_write_rpl2(struct sge_iq *, const struct rss_header *,
    struct mbuf *);

static inline int
t4_l2t_send(struct adapter *sc, struct wrqe *wr, struct l2t_entry *e)
{
	if (__predict_true(e->state == L2T_STATE_VALID)) {
		t4_wrq_tx(sc, wr);
		return (0);
	} else
		return (t4_l2t_send_slow(sc, wr, e));
}

#endif  /* __T4_TOM_L2T_H */
