/* AF_RXRPC local endpoint management
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

#include <linux/module.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include <generated/utsrelease.h>
#include "ar-internal.h"

static const char rxrpc_version_string[65] = "linux-" UTS_RELEASE " AF_RXRPC";

/*
 * Reply to a version request
 */
static void rxrpc_send_version_request(struct rxrpc_local *local,
				       struct rxrpc_host_header *hdr,
				       struct sk_buff *skb)
{
	struct rxrpc_wire_header whdr;
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	struct sockaddr_rxrpc srx;
	struct msghdr msg;
	struct kvec iov[2];
	size_t len;
	int ret;

	_enter("");

	if (rxrpc_extract_addr_from_skb(local, &srx, skb) < 0)
		return;

	msg.msg_name	= &srx.transport;
	msg.msg_namelen	= srx.transport_len;
	msg.msg_control	= NULL;
	msg.msg_controllen = 0;
	msg.msg_flags	= 0;

	whdr.epoch	= htonl(sp->hdr.epoch);
	whdr.cid	= htonl(sp->hdr.cid);
	whdr.callNumber	= htonl(sp->hdr.callNumber);
	whdr.seq	= 0;
	whdr.serial	= 0;
	whdr.type	= RXRPC_PACKET_TYPE_VERSION;
	whdr.flags	= RXRPC_LAST_PACKET | (~hdr->flags & RXRPC_CLIENT_INITIATED);
	whdr.userStatus	= 0;
	whdr.securityIndex = 0;
	whdr._rsvd	= 0;
	whdr.serviceId	= htons(sp->hdr.serviceId);

	iov[0].iov_base	= &whdr;
	iov[0].iov_len	= sizeof(whdr);
	iov[1].iov_base	= (char *)rxrpc_version_string;
	iov[1].iov_len	= sizeof(rxrpc_version_string);

	len = iov[0].iov_len + iov[1].iov_len;

	_proto("Tx VERSION (reply)");

	ret = kernel_sendmsg(local->socket, &msg, iov, 2, len);
	if (ret < 0)
		_debug("sendmsg failed: %d", ret);

	_leave("");
}

/*
 * Process event packets targetted at a local endpoint.
 */
void rxrpc_process_local_events(struct rxrpc_local *local)
{
	struct sk_buff *skb;
	char v;

	_enter("");

	skb = skb_dequeue(&local->event_queue);
	if (skb) {
		struct rxrpc_skb_priv *sp = rxrpc_skb(skb);

		rxrpc_see_skb(skb, rxrpc_skb_rx_seen);
		_debug("{%d},{%u}", local->debug_id, sp->hdr.type);

		switch (sp->hdr.type) {
		case RXRPC_PACKET_TYPE_VERSION:
			if (skb_copy_bits(skb, sizeof(struct rxrpc_wire_header),
					  &v, 1) < 0)
				return;
			_proto("Rx VERSION { %02x }", v);
			if (v == 0)
				rxrpc_send_version_request(local, &sp->hdr, skb);
			break;

		default:
			/* Just ignore anything we don't understand */
			break;
		}

		rxrpc_free_skb(skb, rxrpc_skb_rx_freed);
	}

	_leave("");
}
