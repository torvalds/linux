/* Utility routines
 *
 * Copyright (C) 2015 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/udp.h>
#include "ar-internal.h"

/*
 * Fill out a peer address from a socket buffer containing a packet.
 */
int rxrpc_extract_addr_from_skb(struct sockaddr_rxrpc *srx, struct sk_buff *skb)
{
	memset(srx, 0, sizeof(*srx));

	switch (ntohs(skb->protocol)) {
	case ETH_P_IP:
		srx->transport_type = SOCK_DGRAM;
		srx->transport_len = sizeof(srx->transport.sin);
		srx->transport.sin.sin_family = AF_INET;
		srx->transport.sin.sin_port = udp_hdr(skb)->source;
		srx->transport.sin.sin_addr.s_addr = ip_hdr(skb)->saddr;
		return 0;

	case ETH_P_IPV6:
		srx->transport_type = SOCK_DGRAM;
		srx->transport_len = sizeof(srx->transport.sin6);
		srx->transport.sin6.sin6_family = AF_INET6;
		srx->transport.sin6.sin6_port = udp_hdr(skb)->source;
		srx->transport.sin6.sin6_addr = ipv6_hdr(skb)->saddr;
		return 0;

	default:
		pr_warn_ratelimited("AF_RXRPC: Unknown eth protocol %u\n",
				    ntohs(skb->protocol));
		return -EAFNOSUPPORT;
	}
}
