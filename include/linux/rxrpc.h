/* AF_RXRPC parameters
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_RXRPC_H
#define _LINUX_RXRPC_H

#include <linux/in.h>
#include <linux/in6.h>

/*
 * RxRPC socket address
 */
struct sockaddr_rxrpc {
	sa_family_t	srx_family;	/* address family */
	u16		srx_service;	/* service desired */
	u16		transport_type;	/* type of transport socket (SOCK_DGRAM) */
	u16		transport_len;	/* length of transport address */
	union {
		sa_family_t family;		/* transport address family */
		struct sockaddr_in sin;		/* IPv4 transport address */
		struct sockaddr_in6 sin6;	/* IPv6 transport address */
	} transport;
};

/*
 * RxRPC socket options
 */
#define RXRPC_SECURITY_KEY		1	/* [clnt] set client security key */
#define RXRPC_SECURITY_KEYRING		2	/* [srvr] set ring of server security keys */
#define RXRPC_EXCLUSIVE_CONNECTION	3	/* Deprecated; use RXRPC_EXCLUSIVE_CALL instead */
#define RXRPC_MIN_SECURITY_LEVEL	4	/* minimum security level */
#define RXRPC_UPGRADEABLE_SERVICE	5	/* Upgrade service[0] -> service[1] */

/*
 * RxRPC control messages
 * - If neither abort or accept are specified, the message is a data message.
 * - terminal messages mean that a user call ID tag can be recycled
 * - s/r/- indicate whether these are applicable to sendmsg() and/or recvmsg()
 */
#define RXRPC_USER_CALL_ID	1	/* sr: user call ID specifier */
#define RXRPC_ABORT		2	/* sr: abort request / notification [terminal] */
#define RXRPC_ACK		3	/* -r: [Service] RPC op final ACK received [terminal] */
#define RXRPC_NET_ERROR		5	/* -r: network error received [terminal] */
#define RXRPC_BUSY		6	/* -r: server busy received [terminal] */
#define RXRPC_LOCAL_ERROR	7	/* -r: local error generated [terminal] */
#define RXRPC_NEW_CALL		8	/* -r: [Service] new incoming call notification */
#define RXRPC_ACCEPT		9	/* s-: [Service] accept request */
#define RXRPC_EXCLUSIVE_CALL	10	/* s-: Call should be on exclusive connection */
#define RXRPC_UPGRADE_SERVICE	11	/* s-: Request service upgrade for client call */

/*
 * RxRPC security levels
 */
#define RXRPC_SECURITY_PLAIN	0	/* plain secure-checksummed packets only */
#define RXRPC_SECURITY_AUTH	1	/* authenticated packets */
#define RXRPC_SECURITY_ENCRYPT	2	/* encrypted packets */

/*
 * RxRPC security indices
 */
#define RXRPC_SECURITY_NONE	0	/* no security protocol */
#define RXRPC_SECURITY_RXKAD	2	/* kaserver or kerberos 4 */
#define RXRPC_SECURITY_RXGK	4	/* gssapi-based */
#define RXRPC_SECURITY_RXK5	5	/* kerberos 5 */

#endif /* _LINUX_RXRPC_H */
