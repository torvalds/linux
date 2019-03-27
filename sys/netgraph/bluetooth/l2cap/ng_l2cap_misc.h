/*
 * ng_l2cap_misc.h
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
 * $Id: ng_l2cap_misc.h,v 1.3 2003/09/08 19:11:45 max Exp $
 * $FreeBSD$
 */

#ifndef _NETGRAPH_L2CAP_MISC_H_
#define _NETGRAPH_L2CAP_MISC_H_

void           ng_l2cap_send_hook_info (node_p, hook_p, void *, int);

/*
 * ACL Connections
 */

ng_l2cap_con_p ng_l2cap_new_con       (ng_l2cap_p, bdaddr_p, int);
void           ng_l2cap_con_ref       (ng_l2cap_con_p);
void           ng_l2cap_con_unref     (ng_l2cap_con_p);
ng_l2cap_con_p ng_l2cap_con_by_addr   (ng_l2cap_p, bdaddr_p, unsigned int);
ng_l2cap_con_p ng_l2cap_con_by_handle (ng_l2cap_p, u_int16_t);
void           ng_l2cap_free_con      (ng_l2cap_con_p);

/*
 * L2CAP channels
 */

ng_l2cap_chan_p ng_l2cap_new_chan     (ng_l2cap_p, ng_l2cap_con_p, u_int16_t, int);
ng_l2cap_chan_p ng_l2cap_chan_by_scid (ng_l2cap_p, u_int16_t, int);
ng_l2cap_chan_p ng_l2cap_chan_by_conhandle(ng_l2cap_p , uint16_t , u_int16_t);

void            ng_l2cap_free_chan    (ng_l2cap_chan_p);

/*
 * L2CAP command descriptors
 */

#define ng_l2cap_link_cmd(con, cmd) \
do { \
	TAILQ_INSERT_TAIL(&(con)->cmd_list, (cmd), next); \
	ng_l2cap_con_ref((con)); \
} while (0)

#define ng_l2cap_unlink_cmd(cmd) \
do { \
	TAILQ_REMOVE(&((cmd)->con->cmd_list), (cmd), next); \
	ng_l2cap_con_unref((cmd)->con); \
} while (0)

#define ng_l2cap_free_cmd(cmd) \
do { \
	KASSERT(!callout_pending(&(cmd)->timo), ("Pending callout!")); \
	NG_FREE_M((cmd)->aux); \
	bzero((cmd), sizeof(*(cmd))); \
	free((cmd), M_NETGRAPH_L2CAP); \
} while (0)

ng_l2cap_cmd_p ng_l2cap_new_cmd      (ng_l2cap_con_p, ng_l2cap_chan_p,
						u_int8_t, u_int8_t, u_int32_t);
ng_l2cap_cmd_p ng_l2cap_cmd_by_ident (ng_l2cap_con_p, u_int8_t);
u_int8_t       ng_l2cap_get_ident    (ng_l2cap_con_p);

/*
 * Timeout
 */

int ng_l2cap_discon_timeout    (ng_l2cap_con_p);
int ng_l2cap_discon_untimeout  (ng_l2cap_con_p);
int ng_l2cap_lp_timeout        (ng_l2cap_con_p);
int ng_l2cap_lp_untimeout      (ng_l2cap_con_p);
int ng_l2cap_command_timeout   (ng_l2cap_cmd_p, int);
int ng_l2cap_command_untimeout (ng_l2cap_cmd_p);

/*
 * Other stuff
 */

struct mbuf *   ng_l2cap_prepend      (struct mbuf *, int);
ng_l2cap_flow_p ng_l2cap_default_flow (void);

#endif /* ndef _NETGRAPH_L2CAP_MISC_H_ */

