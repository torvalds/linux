/* RxRPC packet transmission
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/net.h>
#include <linux/gfp.h>
#include <linux/skbuff.h>
#include <linux/export.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include "ar-internal.h"

/*
 * send a packet through the transport endpoint
 */
int rxrpc_send_data_packet(struct rxrpc_connection *conn, struct sk_buff *skb)
{
	struct kvec iov[1];
	struct msghdr msg;
	int ret, opt;

	_enter(",{%d}", skb->len);

	iov[0].iov_base = skb->head;
	iov[0].iov_len = skb->len;

	msg.msg_name = &conn->params.peer->srx.transport;
	msg.msg_namelen = conn->params.peer->srx.transport_len;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = 0;

	/* send the packet with the don't fragment bit set if we currently
	 * think it's small enough */
	if (skb->len - sizeof(struct rxrpc_wire_header) < conn->params.peer->maxdata) {
		down_read(&conn->params.local->defrag_sem);
		/* send the packet by UDP
		 * - returns -EMSGSIZE if UDP would have to fragment the packet
		 *   to go out of the interface
		 *   - in which case, we'll have processed the ICMP error
		 *     message and update the peer record
		 */
		ret = kernel_sendmsg(conn->params.local->socket, &msg, iov, 1,
				     iov[0].iov_len);

		up_read(&conn->params.local->defrag_sem);
		if (ret == -EMSGSIZE)
			goto send_fragmentable;

		_leave(" = %d [%u]", ret, conn->params.peer->maxdata);
		return ret;
	}

send_fragmentable:
	/* attempt to send this message with fragmentation enabled */
	_debug("send fragment");

	down_write(&conn->params.local->defrag_sem);

	switch (conn->params.local->srx.transport.family) {
	case AF_INET:
		opt = IP_PMTUDISC_DONT;
		ret = kernel_setsockopt(conn->params.local->socket,
					SOL_IP, IP_MTU_DISCOVER,
					(char *)&opt, sizeof(opt));
		if (ret == 0) {
			ret = kernel_sendmsg(conn->params.local->socket, &msg, iov, 1,
					     iov[0].iov_len);

			opt = IP_PMTUDISC_DO;
			kernel_setsockopt(conn->params.local->socket, SOL_IP,
					  IP_MTU_DISCOVER,
					  (char *)&opt, sizeof(opt));
		}
		break;
	}

	up_write(&conn->params.local->defrag_sem);
	_leave(" = %d [frag %u]", ret, conn->params.peer->maxdata);
	return ret;
}
