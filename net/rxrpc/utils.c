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
#include <linux/udp.h>
#include "ar-internal.h"

/*
 * Set up an RxRPC address from a socket buffer.
 */
void rxrpc_get_addr_from_skb(struct rxrpc_local *local,
			     const struct sk_buff *skb,
			     struct sockaddr_rxrpc *srx)
{
	memset(srx, 0, sizeof(*srx));
	srx->transport_type = local->srx.transport_type;
	srx->transport.family = local->srx.transport.family;

	/* Can we see an ipv4 UDP packet on an ipv6 UDP socket?  and vice
	 * versa?
	 */
	switch (srx->transport.family) {
	case AF_INET:
		srx->transport.sin.sin_port = udp_hdr(skb)->source;
		srx->transport_len = sizeof(struct sockaddr_in);
		memcpy(&srx->transport.sin.sin_addr, &ip_hdr(skb)->saddr,
		       sizeof(struct in_addr));
		break;

	default:
		BUG();
	}
}
