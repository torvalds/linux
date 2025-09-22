/*	$OpenBSD: logmsg.c,v 1.1 2016/09/02 17:59:58 benno Exp $ */

/*
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "eigrpd.h"
#include "rde.h"
#include "log.h"

#define NUM_LOGS	4
const char *
log_sockaddr(void *vp)
{
	static char	 buf[NUM_LOGS][NI_MAXHOST];
	static int	 round = 0;
	struct sockaddr	*sa = vp;

	round = (round + 1) % NUM_LOGS;

	if (getnameinfo(sa, sa->sa_len, buf[round], NI_MAXHOST, NULL, 0,
	    NI_NUMERICHOST))
		return ("(unknown)");
	else
		return (buf[round]);
}

const char *
log_in6addr(const struct in6_addr *addr)
{
	struct sockaddr_in6	sa_in6;

	memset(&sa_in6, 0, sizeof(sa_in6));
	sa_in6.sin6_len = sizeof(sa_in6);
	sa_in6.sin6_family = AF_INET6;
	sa_in6.sin6_addr = *addr;

	recoverscope(&sa_in6);

	return (log_sockaddr(&sa_in6));
}

const char *
log_in6addr_scope(const struct in6_addr *addr, unsigned int ifindex)
{
	struct sockaddr_in6	sa_in6;

	memset(&sa_in6, 0, sizeof(sa_in6));
	sa_in6.sin6_len = sizeof(sa_in6);
	sa_in6.sin6_family = AF_INET6;
	sa_in6.sin6_addr = *addr;

	addscope(&sa_in6, ifindex);

	return (log_sockaddr(&sa_in6));
}

const char *
log_addr(int af, union eigrpd_addr *addr)
{
	static char	 buf[NUM_LOGS][INET6_ADDRSTRLEN];
	static int	 round = 0;

	switch (af) {
	case AF_INET:
		round = (round + 1) % NUM_LOGS;
		if (inet_ntop(AF_INET, &addr->v4, buf[round],
		    sizeof(buf[round])) == NULL)
			return ("???");
		return (buf[round]);
	case AF_INET6:
		return (log_in6addr(&addr->v6));
	default:
		break;
	}

	return ("???");
}

const char *
log_prefix(struct rt_node *rn)
{
	static char	buf[64];

	if (snprintf(buf, sizeof(buf), "%s/%u", log_addr(rn->eigrp->af,
	    &rn->prefix), rn->prefixlen) == -1)
		return ("???");

	return (buf);
}

const char *
log_route_origin(int af, struct rde_nbr *nbr)
{
	if (nbr->flags & F_RDE_NBR_SELF) {
		if (nbr->flags & F_RDE_NBR_REDIST)
			return ("redistribute");
		if (nbr->flags & F_RDE_NBR_SUMMARY)
			return ("summary");
		else
			return ("connected");
	}

	return (log_addr(af, &nbr->addr));
}

const char *
opcode_name(uint8_t opcode)
{
	switch (opcode) {
	case EIGRP_OPC_UPDATE:
		return ("UPDATE");
	case EIGRP_OPC_REQUEST:
		return ("REQUEST");
	case EIGRP_OPC_QUERY:
		return ("QUERY");
	case EIGRP_OPC_REPLY:
		return ("REPLY");
	case EIGRP_OPC_HELLO:
		return ("HELLO");
	case EIGRP_OPC_PROBE:
		return ("PROBE");
	case EIGRP_OPC_SIAQUERY:
		return ("SIAQUERY");
	case EIGRP_OPC_SIAREPLY:
		return ("SIAREPLY");
	default:
		return ("UNKNOWN");
	}
}

const char *
af_name(int af)
{
	switch (af) {
	case AF_INET:
		return ("ipv4");
	case AF_INET6:
		return ("ipv6");
	default:
		return ("UNKNOWN");
	}
}

const char *
if_type_name(enum iface_type type)
{
	switch (type) {
	case IF_TYPE_POINTOPOINT:
		return ("POINTOPOINT");
	case IF_TYPE_BROADCAST:
		return ("BROADCAST");
	default:
		return ("UNKNOWN");
	}
}

const char *
dual_state_name(int state)
{
	switch (state) {
	case DUAL_STA_PASSIVE:
		return ("PASSIVE");
	case DUAL_STA_ACTIVE0:
		return ("ACTIVE(Oij=0)");
	case DUAL_STA_ACTIVE1:
		return ("ACTIVE(Oij=1)");
	case DUAL_STA_ACTIVE2:
		return ("ACTIVE(Oij=2)");
	case DUAL_STA_ACTIVE3:
		return ("ACTIVE(Oij=3)");
	default:
		return ("UNKNOWN");
	}
}

const char *
ext_proto_name(int proto)
{
	switch (proto) {
	case EIGRP_EXT_PROTO_IGRP:
		return ("IGRP");
	case EIGRP_EXT_PROTO_EIGRP:
		return ("EIGRP");
	case EIGRP_EXT_PROTO_STATIC:
		return ("Static");
	case EIGRP_EXT_PROTO_RIP:
		return ("RIP");
	case EIGRP_EXT_PROTO_HELLO:
		return ("HELLO");
	case EIGRP_EXT_PROTO_OSPF:
		return ("OSPF");
	case EIGRP_EXT_PROTO_ISIS:
		return ("ISIS");
	case EIGRP_EXT_PROTO_EGP:
		return ("EGP");
	case EIGRP_EXT_PROTO_BGP:
		return ("BGP");
	case EIGRP_EXT_PROTO_IDRP:
		return ("IDRP");
	case EIGRP_EXT_PROTO_CONN:
		return ("Connected");
	default:
		return ("UNKNOWN");
	}
}
