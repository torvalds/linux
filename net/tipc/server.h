/*
 * net/tipc/server.h: Include file for TIPC server code
 *
 * Copyright (c) 2012-2013, Wind River Systems
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

#ifndef _TIPC_SERVER_H
#define _TIPC_SERVER_H

#include <linux/idr.h>
#include <linux/tipc.h>
#include <net/net_namespace.h>

#define TIPC_SERVER_NAME_LEN	32

/**
 * struct tipc_server - TIPC server structure
 * @conn_idr: identifier set of connection
 * @idr_lock: protect the connection identifier set
 * @idr_in_use: amount of allocated identifier entry
 * @net: network namspace instance
 * @rcvbuf_cache: memory cache of server receive buffer
 * @rcv_wq: receive workqueue
 * @send_wq: send workqueue
 * @max_rcvbuf_size: maximum permitted receive message length
 * @tipc_conn_new: callback will be called when new connection is incoming
 * @tipc_conn_release: callback will be called before releasing the connection
 * @tipc_conn_recvmsg: callback will be called when message arrives
 * @saddr: TIPC server address
 * @name: server name
 * @imp: message importance
 * @type: socket type
 */
struct tipc_server {
	struct idr conn_idr;
	spinlock_t idr_lock;
	int idr_in_use;
	struct net *net;
	struct kmem_cache *rcvbuf_cache;
	struct workqueue_struct *rcv_wq;
	struct workqueue_struct *send_wq;
	int max_rcvbuf_size;
	void *(*tipc_conn_new)(int conid);
	void (*tipc_conn_release)(int conid, void *usr_data);
	void (*tipc_conn_recvmsg)(struct net *net, int conid,
				  struct sockaddr_tipc *addr, void *usr_data,
				  void *buf, size_t len);
	struct sockaddr_tipc *saddr;
	char name[TIPC_SERVER_NAME_LEN];
	int imp;
	int type;
};

int tipc_conn_sendmsg(struct tipc_server *s, int conid,
		      struct sockaddr_tipc *addr, void *data, size_t len);

/**
 * tipc_conn_terminate - terminate connection with server
 *
 * Note: Must call it in process context since it might sleep
 */
void tipc_conn_terminate(struct tipc_server *s, int conid);

int tipc_server_start(struct tipc_server *s);

void tipc_server_stop(struct tipc_server *s);

#endif
