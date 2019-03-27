/*
 * ng_l2cap_ulpi.h
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
 * $Id: ng_l2cap_ulpi.h,v 1.1 2002/11/24 19:47:06 max Exp $
 * $FreeBSD$
 */

#ifndef _NETGRAPH_L2CAP_ULPI_H_
#define _NETGRAPH_L2CAP_ULPI_H_

int ng_l2cap_l2ca_con_req     (ng_l2cap_p, struct ng_mesg *);
int ng_l2cap_l2ca_con_rsp    (ng_l2cap_chan_p, u_int32_t, u_int16_t, u_int16_t);
int ng_l2cap_l2ca_con_rsp_req (ng_l2cap_p, struct ng_mesg *);
int ng_l2cap_l2ca_con_rsp_rsp (ng_l2cap_chan_p, u_int32_t, u_int16_t);
int ng_l2cap_l2ca_con_ind     (ng_l2cap_chan_p);

int ng_l2cap_l2ca_cfg_req     (ng_l2cap_p, struct ng_mesg *);
int ng_l2cap_l2ca_cfg_rsp     (ng_l2cap_chan_p, u_int32_t, u_int16_t);
int ng_l2cap_l2ca_cfg_rsp_req (ng_l2cap_p, struct ng_mesg *);
int ng_l2cap_l2ca_cfg_rsp_rsp (ng_l2cap_chan_p, u_int32_t, u_int16_t);
int ng_l2cap_l2ca_cfg_ind     (ng_l2cap_chan_p);

int ng_l2cap_l2ca_write_req   (ng_l2cap_p, struct mbuf *);
int ng_l2cap_l2ca_write_rsp  (ng_l2cap_chan_p, u_int32_t, u_int16_t, u_int16_t);

int ng_l2cap_l2ca_receive     (ng_l2cap_con_p);
int ng_l2cap_l2ca_clt_receive (ng_l2cap_con_p);

int ng_l2cap_l2ca_qos_ind     (ng_l2cap_chan_p);

int ng_l2cap_l2ca_discon_req  (ng_l2cap_p, struct ng_mesg *);
int ng_l2cap_l2ca_discon_rsp  (ng_l2cap_chan_p, u_int32_t, u_int16_t);
int ng_l2cap_l2ca_discon_ind  (ng_l2cap_chan_p);

int ng_l2cap_l2ca_grp_create  (ng_l2cap_p, struct ng_mesg *);
int ng_l2cap_l2ca_grp_close   (ng_l2cap_p, struct ng_mesg *);
int ng_l2cap_l2ca_grp_add_member_req (ng_l2cap_p, struct ng_mesg *);
int ng_l2cap_l2ca_grp_add_member_rsp (ng_l2cap_chan_p, u_int32_t, u_int16_t);
int ng_l2cap_l2ca_grp_rem_member  (ng_l2cap_p, struct ng_mesg *);
int ng_l2cap_l2ca_grp_get_members (ng_l2cap_p, struct ng_mesg *);

int ng_l2cap_l2ca_ping_req     (ng_l2cap_p, struct ng_mesg *);
int ng_l2cap_l2ca_ping_rsp     (ng_l2cap_con_p, u_int32_t, u_int16_t, 
					struct mbuf *);

int ng_l2cap_l2ca_get_info_req (ng_l2cap_p, struct ng_mesg *);
int ng_l2cap_l2ca_get_info_rsp (ng_l2cap_con_p, u_int32_t, u_int16_t, 
					struct mbuf *);

int ng_l2cap_l2ca_enable_clt   (ng_l2cap_p, struct ng_mesg *);
int ng_l2cap_l2ca_encryption_change(ng_l2cap_chan_p , uint16_t );
#endif /* ndef _NETGRAPH_L2CAP_ULPI_H_ */

