/*	$OpenBSD: logmsg.c,v 1.1 2016/09/02 14:08:50 benno Exp $ */

/*
 * Copyright (c) 2006 Claudio Jeker <claudio@openbsd.org>
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

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <netdb.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "ospf6d.h"
#include "log.h"

const char *
log_in6addr(const struct in6_addr *addr)
{
	struct sockaddr_in6	sa_in6;

	bzero(&sa_in6, sizeof(sa_in6));
	sa_in6.sin6_len = sizeof(sa_in6);
	sa_in6.sin6_family = AF_INET6;
	memcpy(&sa_in6.sin6_addr, addr, sizeof(sa_in6.sin6_addr));

	/*
	 * Destination addresses contain embedded scopes.
	 * They must be recovered for ospf6ctl show fib.
	 */
	recoverscope(&sa_in6);

	return (log_sockaddr(&sa_in6));
}

const char *
log_in6addr_scope(const struct in6_addr *addr, unsigned int ifindex)
{
	struct sockaddr_in6	sa_in6;

	bzero(&sa_in6, sizeof(sa_in6));
	sa_in6.sin6_len = sizeof(sa_in6);
	sa_in6.sin6_family = AF_INET6;
	memcpy(&sa_in6.sin6_addr, addr, sizeof(sa_in6.sin6_addr));

	addscope(&sa_in6, ifindex);

	return (log_sockaddr(&sa_in6));
}

#define NUM_LOGS	4
const char *
log_rtr_id(u_int32_t id)
{
	static char	buf[NUM_LOGS][16];
	static int	round = 0;
	struct in_addr	addr;

	round = (round + 1) % NUM_LOGS;

	addr.s_addr = id;
	if (inet_ntop(AF_INET, &addr, buf[round], 16) == NULL)
		return ("?");
	else
		return buf[round];
}

const char *
log_sockaddr(void *vp)
{
	static char	buf[NUM_LOGS][NI_MAXHOST];
	static int	round = 0;
	struct sockaddr	*sa = vp;

	round = (round + 1) % NUM_LOGS;

	if (getnameinfo(sa, sa->sa_len, buf[round], NI_MAXHOST, NULL, 0,
	    NI_NUMERICHOST))
		return ("(unknown)");
	else
		return (buf[round]);
}

/* names */
const char *
nbr_state_name(int state)
{
	switch (state) {
	case NBR_STA_DOWN:
		return ("DOWN");
	case NBR_STA_ATTEMPT:
		return ("ATTMP");
	case NBR_STA_INIT:
		return ("INIT");
	case NBR_STA_2_WAY:
		return ("2-WAY");
	case NBR_STA_XSTRT:
		return ("EXSTA");
	case NBR_STA_SNAP:
		return ("SNAP");
	case NBR_STA_XCHNG:
		return ("EXCHG");
	case NBR_STA_LOAD:
		return ("LOAD");
	case NBR_STA_FULL:
		return ("FULL");
	default:
		return ("UNKNW");
	}
}

const char *
if_state_name(int state)
{
	switch (state) {
	case IF_STA_DOWN:
		return ("DOWN");
	case IF_STA_LOOPBACK:
		return ("LOOP");
	case IF_STA_WAITING:
		return ("WAIT");
	case IF_STA_POINTTOPOINT:
		return ("P2P");
	case IF_STA_DROTHER:
		return ("OTHER");
	case IF_STA_BACKUP:
		return ("BCKUP");
	case IF_STA_DR:
		return ("DR");
	default:
		return ("UNKNW");
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
	case IF_TYPE_NBMA:
		return ("NBMA");
	case IF_TYPE_POINTOMULTIPOINT:
		return ("POINTOMULTIPOINT");
	case IF_TYPE_VIRTUALLINK:
		return ("VIRTUALLINK");
	}
	/* NOTREACHED */
	return ("UNKNOWN");
}

const char *
dst_type_name(enum dst_type type)
{
	switch (type) {
	case DT_NET:
		return ("Network");
	case DT_RTR:
		return ("Router");
	}
	/* NOTREACHED */
	return ("unknown");
}

const char *
path_type_name(enum path_type type)
{
	switch (type) {
	case PT_INTRA_AREA:
		return ("Intra-Area");
	case PT_INTER_AREA:
		return ("Inter-Area");
	case PT_TYPE1_EXT:
		return ("Type 1 ext");
	case PT_TYPE2_EXT:
		return ("Type 2 ext");
	}
	/* NOTREACHED */
	return ("unknown");
}
