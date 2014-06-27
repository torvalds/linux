/* net/tipc/socket.h: Include file for TIPC socket code
 *
 * Copyright (c) 2014, Ericsson AB
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _TIPC_SOCK_H
#define _TIPC_SOCK_H

#include "port.h"
#include <net/sock.h>

#define TIPC_CONN_OK      0
#define TIPC_CONN_PROBING 1

/**
 * struct tipc_sock - TIPC socket structure
 * @sk: socket - interacts with 'port' and with user via the socket API
 * @port: port - interacts with 'sk' and with the rest of the TIPC stack
 * @peer_name: the peer of the connection, if any
 * @conn_timeout: the time we can wait for an unresponded setup request
 * @dupl_rcvcnt: number of bytes counted twice, in both backlog and rcv queue
 * @link_cong: non-zero if owner must sleep because of link congestion
 * @sent_unacked: # messages sent by socket, and not yet acked by peer
 * @rcv_unacked: # messages read by user, but not yet acked back to peer
 */

struct tipc_sock {
	struct sock sk;
	struct tipc_port port;
	unsigned int conn_timeout;
	atomic_t dupl_rcvcnt;
	int link_cong;
	uint sent_unacked;
	uint rcv_unacked;
};

static inline struct tipc_sock *tipc_sk(const struct sock *sk)
{
	return container_of(sk, struct tipc_sock, sk);
}

static inline struct tipc_sock *tipc_port_to_sock(const struct tipc_port *port)
{
	return container_of(port, struct tipc_sock, port);
}

static inline void tipc_sock_wakeup(struct tipc_sock *tsk)
{
	tsk->sk.sk_write_space(&tsk->sk);
}

static inline int tipc_sk_conn_cong(struct tipc_sock *tsk)
{
	return tsk->sent_unacked >= TIPC_FLOWCTRL_WIN;
}

int tipc_sk_rcv(struct sk_buff *buf);

#endif
