/*	$OpenBSD: net_utils.c,v 1.6 2020/12/30 18:52:06 benno Exp $ */
/*-
 * Copyright (c) 2009 Internet Initiative Japan Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/* $Id: net_utils.c,v 1.6 2020/12/30 18:52:06 benno Exp $ */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>

#include "net_utils.h"

/** Get an interface name from sockaddr */
const char *
get_ifname_by_sockaddr(struct sockaddr *sa, char *ifname)
{
	struct ifaddrs *addr, *addr0;
	struct in_addr *in4a, *in4b;
	const char *ifname0 = NULL;
	struct in6_addr *in6a, *in6b;

	ifname0 = NULL;
	/* I want other way than linear search */
	getifaddrs(&addr0);
	for (addr = addr0; ifname0 == NULL && addr != NULL;
	    addr = addr->ifa_next) {
		if (addr->ifa_addr == NULL ||
		    addr->ifa_addr->sa_family != sa->sa_family ||
		    addr->ifa_addr->sa_len != sa->sa_len)
			continue;
		switch (addr->ifa_addr->sa_family) {
		default:
			continue;
		case AF_INET:
			in4a = &((struct sockaddr_in *)addr->ifa_addr)
			    ->sin_addr;
			in4b = &((struct sockaddr_in *)sa)->sin_addr;
			if (in4a->s_addr == in4b->s_addr) {
				strlcpy(ifname, addr->ifa_name, IF_NAMESIZE);
				ifname0 = ifname;
			}
			break;
		case AF_INET6:
			in6a = &((struct sockaddr_in6 *)addr->ifa_addr)
			    ->sin6_addr;
			in6b = &((struct sockaddr_in6 *)sa)->sin6_addr;
			if (IN6_ARE_ADDR_EQUAL(in6a, in6b)) {
				strlcpy(ifname, addr->ifa_name, IF_NAMESIZE);
				ifname0 = ifname;
			}
			break;
		}
	}
	freeifaddrs(addr0);

	return ifname0;
}

/**
 * Convert argument like "192.168.160.1:1723/tcp" or "[::1]:1723/tcp" to
 * match getaddrinfo(3)'s specification and pass them to getaddrinfo(3).
 */
int
addrport_parse(const char *addrport, int proto, struct addrinfo **p_ai)
{
	char buf[256];
	char *servp, *nodep, *slash;
	struct addrinfo hints;

	strlcpy(buf, addrport, sizeof(buf));
	if (buf[0] == '[' && (servp = strchr(buf, ']')) != NULL) {
		nodep = buf + 1;
		*servp++ = '\0';
		if (*servp != ':')
			servp = NULL;
	} else {
		nodep = buf;
		servp = strrchr(nodep, ':');
	}
	if (servp != NULL) {
		*servp = '\0';
		servp++;
		slash = strrchr(servp, '/');
		if (slash != NULL) {
			/*
			 * Ignore like "/tcp"
			 */
			*slash = '\0';
			slash++;
		}
	} else
		servp = NULL;
	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_NUMERICHOST;
	hints.ai_family = AF_UNSPEC;
	switch (proto) {
	case IPPROTO_TCP:
		hints.ai_socktype = SOCK_STREAM;
		break;
	case IPPROTO_UDP:
		hints.ai_socktype = SOCK_DGRAM;
		break;
	}
	hints.ai_protocol = proto;

	return getaddrinfo(nodep, servp, &hints, p_ai);
}

/**
 * Make a string like "192.168.160.1:1723" or "[::1]:1723" from a struct
 * sockaddr
 *
 * @param	buf	the buffer to be stored a string
 * @param	lbuf	the length of the buf
 */
const char *
addrport_tostring(struct sockaddr *sa, socklen_t salen, char *buf, int lbuf)
{
	char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

	if (getnameinfo(sa, salen, hbuf, sizeof(hbuf), sbuf, sizeof(sbuf),
	    NI_NUMERICHOST | NI_NUMERICSERV) != 0)
		return NULL;

	switch (sa->sa_family) {
	case AF_INET6:
		strlcpy(buf, "[", lbuf);
		strlcat(buf, hbuf, lbuf);
		strlcat(buf, "]:", lbuf);
		strlcat(buf, sbuf, lbuf);
		break;
	case AF_INET:
		strlcpy(buf, hbuf, lbuf);
		strlcat(buf, ":", lbuf);
		strlcat(buf, sbuf, lbuf);
		break;
	default:
		return NULL;
	}

	return buf;
}

/** Convert 32bit IPv4 netmask to the prefix length in host byte order */
int
netmask2prefixlen(uint32_t mask)
{
    switch(mask) {
    case 0x00000000:  return  0;
    case 0x80000000:  return  1;
    case 0xC0000000:  return  2;
    case 0xE0000000:  return  3;
    case 0xF0000000:  return  4;
    case 0xF8000000:  return  5;
    case 0xFC000000:  return  6;
    case 0xFE000000:  return  7;
    case 0xFF000000:  return  8;
    case 0xFF800000:  return  9;
    case 0xFFC00000:  return 10;
    case 0xFFE00000:  return 11;
    case 0xFFF00000:  return 12;
    case 0xFFF80000:  return 13;
    case 0xFFFC0000:  return 14;
    case 0xFFFE0000:  return 15;
    case 0xFFFF0000:  return 16;
    case 0xFFFF8000:  return 17;
    case 0xFFFFC000:  return 18;
    case 0xFFFFE000:  return 19;
    case 0xFFFFF000:  return 20;
    case 0xFFFFF800:  return 21;
    case 0xFFFFFC00:  return 22;
    case 0xFFFFFE00:  return 23;
    case 0xFFFFFF00:  return 24;
    case 0xFFFFFF80:  return 25;
    case 0xFFFFFFC0:  return 26;
    case 0xFFFFFFE0:  return 27;
    case 0xFFFFFFF0:  return 28;
    case 0xFFFFFFF8:  return 29;
    case 0xFFFFFFFC:  return 30;
    case 0xFFFFFFFE:  return 31;
    case 0xFFFFFFFF:  return 32;
    }
    return -1;
}
