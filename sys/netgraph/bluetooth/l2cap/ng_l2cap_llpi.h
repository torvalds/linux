/*
 * ng_l2cap_llpi.h
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: ng_l2cap_llpi.h,v 1.2 2003/04/28 21:44:59 max Exp $
 * $FreeBSD$
 */

#ifndef _NETGRAPH_L2CAP_LLPI_H_
#define _NETGRAPH_L2CAP_LLPI_H_

int  ng_l2cap_lp_con_req             (ng_l2cap_p, bdaddr_p, int);
int  ng_l2cap_lp_con_cfm             (ng_l2cap_p, struct ng_mesg *);
int  ng_l2cap_lp_con_ind             (ng_l2cap_p, struct ng_mesg *);
int  ng_l2cap_lp_discon_ind          (ng_l2cap_p, struct ng_mesg *);
int  ng_l2cap_lp_qos_req             (ng_l2cap_p, u_int16_t, ng_l2cap_flow_p);
int  ng_l2cap_lp_qos_cfm             (ng_l2cap_p, struct ng_mesg *);
int  ng_l2cap_lp_qos_ind             (ng_l2cap_p, struct ng_mesg *);
int  ng_l2cap_lp_enc_change             (ng_l2cap_p, struct ng_mesg *);
int  ng_l2cap_lp_send                (ng_l2cap_con_p, u_int16_t,struct mbuf *);
int  ng_l2cap_lp_receive             (ng_l2cap_p, struct mbuf *);
void ng_l2cap_lp_deliver             (ng_l2cap_con_p);
void ng_l2cap_process_lp_timeout     (node_p, hook_p, void *, int);
void ng_l2cap_process_discon_timeout (node_p, hook_p, void *, int);

#endif /* ndef _NETGRAPH_L2CAP_LLPI_H_ */

