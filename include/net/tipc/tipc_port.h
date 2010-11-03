/*
 * include/net/tipc/tipc_port.h: Include file for privileged access to TIPC ports
 * 
 * Copyright (c) 1994-2007, Ericsson AB
 * Copyright (c) 2005-2008, Wind River Systems
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

#ifndef _NET_TIPC_PORT_H_
#define _NET_TIPC_PORT_H_

#ifdef __KERNEL__

#include <linux/tipc.h>
#include <linux/skbuff.h>
#include <net/tipc/tipc_msg.h>

#define TIPC_FLOW_CONTROL_WIN 512

/**
 * struct tipc_port - native TIPC port info available to privileged users
 * @usr_handle: pointer to additional user-defined information about port
 * @lock: pointer to spinlock for controlling access to port
 * @connected: non-zero if port is currently connected to a peer port
 * @conn_type: TIPC type used when connection was established
 * @conn_instance: TIPC instance used when connection was established
 * @conn_unacked: number of unacknowledged messages received from peer port
 * @published: non-zero if port has one or more associated names
 * @congested: non-zero if cannot send because of link or port congestion
 * @max_pkt: maximum packet size "hint" used when building messages sent by port
 * @ref: unique reference to port in TIPC object registry
 * @phdr: preformatted message header used when sending messages
 */

struct tipc_port {
        void *usr_handle;
        spinlock_t *lock;
	int connected;
        u32 conn_type;
        u32 conn_instance;
	u32 conn_unacked;
	int published;
	u32 congested;
	u32 max_pkt;
	u32 ref;
	struct tipc_msg phdr;
};


struct tipc_port *tipc_createport_raw(void *usr_handle,
			u32 (*dispatcher)(struct tipc_port *, struct sk_buff *),
			void (*wakeup)(struct tipc_port *),
			const u32 importance);

int tipc_reject_msg(struct sk_buff *buf, u32 err);

int tipc_send_buf_fast(struct sk_buff *buf, u32 destnode);

void tipc_acknowledge(u32 port_ref,u32 ack);

struct tipc_port *tipc_get_port(const u32 ref);

/*
 * The following routines require that the port be locked on entry
 */

int tipc_disconnect_port(struct tipc_port *tp_ptr);


#endif

#endif

