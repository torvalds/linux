/*	$OpenBSD: icmp.c,v 1.19 2019/10/03 13:36:15 claudio Exp $ */

/*
 * Copyright (c) 1997, 1998 The Internet Software Consortium.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of The Internet Software Consortium nor the names
 *    of its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INTERNET SOFTWARE CONSORTIUM AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE INTERNET SOFTWARE CONSORTIUM OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This software has been written for the Internet Software Consortium
 * by Ted Lemon <mellon@fugue.com> in cooperation with Vixie
 * Enterprises.  To learn more about the Internet Software Consortium,
 * see ``http://www.vix.com/isc''.  To learn more about Vixie
 * Enterprises, see ``http://www.vix.com''.
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <arpa/inet.h>

#include <net/if.h>

#include <netinet/ip.h>
#include <netinet/ip_icmp.h>

#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "dhcp.h"
#include "tree.h"
#include "dhcpd.h"
#include "log.h"

static int icmp_protocol_initialized;
static int icmp_protocol_fd;

/* Initialize the ICMP protocol. */

void
icmp_startup(int routep, void (*handler)(struct iaddr, u_int8_t *, int))
{
	struct protoent *proto;
	int protocol = 1, state;

	/* Only initialize icmp once. */
	if (icmp_protocol_initialized)
		fatalx("attempted to reinitialize icmp protocol");
	icmp_protocol_initialized = 1;

	/* Get the protocol number (should be 1). */
	if ((proto = getprotobyname("icmp")) != NULL)
		protocol = proto->p_proto;

	/* Get a raw socket for the ICMP protocol. */
	if ((icmp_protocol_fd = socket(AF_INET, SOCK_RAW, protocol)) == -1)
		fatal("unable to create icmp socket");

	/* Make sure it does routing... */
	state = 0;
	if (setsockopt(icmp_protocol_fd, SOL_SOCKET, SO_DONTROUTE,
	    &state, sizeof(state)) == -1)
		fatal("Unable to disable SO_DONTROUTE on ICMP socket");

	add_protocol("icmp", icmp_protocol_fd, icmp_echoreply,
	    (void *)handler);
}

int
icmp_echorequest(struct iaddr *addr)
{
	struct sockaddr_in to;
	struct icmp icmp;
	int status;

	if (!icmp_protocol_initialized)
		fatalx("attempt to use ICMP protocol before initialization.");

	memset(&to, 0, sizeof(to));
	to.sin_len = sizeof to;
	to.sin_family = AF_INET;
	memcpy(&to.sin_addr, addr->iabuf, sizeof to.sin_addr);	/* XXX */

	memset(&icmp, 0, sizeof(icmp));
	icmp.icmp_type = ICMP_ECHO;
	icmp.icmp_id = getpid() & 0xffff;

	icmp.icmp_cksum = wrapsum(checksum((unsigned char *)&icmp,
	    sizeof(icmp), 0));

	/* Send the ICMP packet... */
	status = sendto(icmp_protocol_fd, &icmp, sizeof(icmp), 0,
	    (struct sockaddr *)&to, sizeof(to));
	if (status == -1)
		log_warn("icmp_echorequest %s", inet_ntoa(to.sin_addr));

	if (status != sizeof icmp)
		return 0;
	return 1;
}

void
icmp_echoreply(struct protocol *protocol)
{
	void (*handler)(struct iaddr, u_int8_t *, int);
	struct sockaddr_in from;
	u_int8_t icbuf[1500];
	struct icmp *icfrom;
	int status, len;
	socklen_t salen;
	struct iaddr ia;

	salen = sizeof from;
	status = recvfrom(protocol->fd, icbuf, sizeof(icbuf), 0,
	    (struct sockaddr *)&from, &salen);
	if (status == -1) {
		log_warn("icmp_echoreply");
		return;
	}

	/* Probably not for us. */
	if (status < (sizeof(struct ip)) + (sizeof *icfrom))
		return;

	len = status - sizeof(struct ip);
	icfrom = (struct icmp *)(icbuf + sizeof(struct ip));

	/* Silently discard ICMP packets that aren't echoreplies. */
	if (icfrom->icmp_type != ICMP_ECHOREPLY)
		return;

	/* If we were given a second-stage handler, call it. */
	if (protocol->local) {
		handler = ((void (*)(struct iaddr, u_int8_t *, int))
		    protocol->local);
		memcpy(ia.iabuf, &from.sin_addr, sizeof from.sin_addr);
		ia.len = sizeof from.sin_addr;
		(*handler)(ia, icbuf, len);
	}
}
