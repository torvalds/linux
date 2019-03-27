/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Charles Mott <cm@linktel.net>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/* file: alias_proxy.c

    This file encapsulates special operations related to transparent
    proxy redirection.  This is where packets with a particular destination,
    usually tcp port 80, are redirected to a proxy server.

    When packets are proxied, the destination address and port are
    modified.  In certain cases, it is necessary to somehow encode
    the original address/port info into the packet.  Two methods are
    presently supported: addition of a [DEST addr port] string at the
    beginning of a tcp stream, or inclusion of an optional field
    in the IP header.

    There is one public API function:

	PacketAliasProxyRule()    -- Adds and deletes proxy
				     rules.

    Rules are stored in a linear linked list, so lookup efficiency
    won't be too good for large lists.


    Initial development: April, 1998 (cjm)
*/


/* System includes */
#ifdef _KERNEL
#include <sys/param.h>
#include <sys/ctype.h>
#include <sys/libkern.h>
#include <sys/limits.h>
#else
#include <sys/types.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#endif

#include <netinet/tcp.h>

#ifdef _KERNEL
#include <netinet/libalias/alias.h>
#include <netinet/libalias/alias_local.h>
#include <netinet/libalias/alias_mod.h>
#else
#include <arpa/inet.h>
#include "alias.h"		/* Public API functions for libalias */
#include "alias_local.h"	/* Functions used by alias*.c */
#endif

/*
    Data structures
 */

/*
 * A linked list of arbitrary length, based on struct proxy_entry is
 * used to store proxy rules.
 */
struct proxy_entry {
	struct libalias *la;
#define PROXY_TYPE_ENCODE_NONE      1
#define PROXY_TYPE_ENCODE_TCPSTREAM 2
#define PROXY_TYPE_ENCODE_IPHDR     3
	int		rule_index;
	int		proxy_type;
	u_char		proto;
	u_short		proxy_port;
	u_short		server_port;

	struct in_addr	server_addr;

	struct in_addr	src_addr;
	struct in_addr	src_mask;

	struct in_addr	dst_addr;
	struct in_addr	dst_mask;

	struct proxy_entry *next;
	struct proxy_entry *last;
};



/*
    File scope variables
*/



/* Local (static) functions:

    IpMask()                 -- Utility function for creating IP
				masks from integer (1-32) specification.
    IpAddr()                 -- Utility function for converting string
				to IP address
    IpPort()                 -- Utility function for converting string
				to port number
    RuleAdd()                -- Adds an element to the rule list.
    RuleDelete()             -- Removes an element from the rule list.
    RuleNumberDelete()       -- Removes all elements from the rule list
				having a certain rule number.
    ProxyEncodeTcpStream()   -- Adds [DEST x.x.x.x xxxx] to the beginning
				of a TCP stream.
    ProxyEncodeIpHeader()    -- Adds an IP option indicating the true
				destination of a proxied IP packet
*/

static int	IpMask(int, struct in_addr *);
static int	IpAddr(char *, struct in_addr *);
static int	IpPort(char *, int, int *);
static void	RuleAdd(struct libalias *la, struct proxy_entry *);
static void	RuleDelete(struct proxy_entry *);
static int	RuleNumberDelete(struct libalias *la, int);
static void	ProxyEncodeTcpStream(struct alias_link *, struct ip *, int);
static void	ProxyEncodeIpHeader(struct ip *, int);

static int
IpMask(int nbits, struct in_addr *mask)
{
	int i;
	u_int imask;

	if (nbits < 0 || nbits > 32)
		return (-1);

	imask = 0;
	for (i = 0; i < nbits; i++)
		imask = (imask >> 1) + 0x80000000;
	mask->s_addr = htonl(imask);

	return (0);
}

static int
IpAddr(char *s, struct in_addr *addr)
{
	if (inet_aton(s, addr) == 0)
		return (-1);
	else
		return (0);
}

static int
IpPort(char *s, int proto, int *port)
{
	int n;

	n = sscanf(s, "%d", port);
	if (n != 1)
#ifndef _KERNEL	/* XXX: we accept only numeric ports in kernel */
	{
		struct servent *se;

		if (proto == IPPROTO_TCP)
			se = getservbyname(s, "tcp");
		else if (proto == IPPROTO_UDP)
			se = getservbyname(s, "udp");
		else
			return (-1);

		if (se == NULL)
			return (-1);

		*port = (u_int) ntohs(se->s_port);
	}
#else
		return (-1);
#endif
	return (0);
}

void
RuleAdd(struct libalias *la, struct proxy_entry *entry)
{
	int rule_index;
	struct proxy_entry *ptr;
	struct proxy_entry *ptr_last;

	LIBALIAS_LOCK_ASSERT(la);

	entry->la = la;
	if (la->proxyList == NULL) {
		la->proxyList = entry;
		entry->last = NULL;
		entry->next = NULL;
		return;
	}

	rule_index = entry->rule_index;
	ptr = la->proxyList;
	ptr_last = NULL;
	while (ptr != NULL) {
		if (ptr->rule_index >= rule_index) {
			if (ptr_last == NULL) {
				entry->next = la->proxyList;
				entry->last = NULL;
				la->proxyList->last = entry;
				la->proxyList = entry;
				return;
			}
			ptr_last->next = entry;
			ptr->last = entry;
			entry->last = ptr->last;
			entry->next = ptr;
			return;
		}
		ptr_last = ptr;
		ptr = ptr->next;
	}

	ptr_last->next = entry;
	entry->last = ptr_last;
	entry->next = NULL;
}

static void
RuleDelete(struct proxy_entry *entry)
{
	struct libalias *la;

	la = entry->la;
	LIBALIAS_LOCK_ASSERT(la);
	if (entry->last != NULL)
		entry->last->next = entry->next;
	else
		la->proxyList = entry->next;

	if (entry->next != NULL)
		entry->next->last = entry->last;

	free(entry);
}

static int
RuleNumberDelete(struct libalias *la, int rule_index)
{
	int err;
	struct proxy_entry *ptr;

	LIBALIAS_LOCK_ASSERT(la);
	err = -1;
	ptr = la->proxyList;
	while (ptr != NULL) {
		struct proxy_entry *ptr_next;

		ptr_next = ptr->next;
		if (ptr->rule_index == rule_index) {
			err = 0;
			RuleDelete(ptr);
		}
		ptr = ptr_next;
	}

	return (err);
}

static void
ProxyEncodeTcpStream(struct alias_link *lnk,
    struct ip *pip,
    int maxpacketsize)
{
	int slen;
	char buffer[40];
	struct tcphdr *tc;
	char addrbuf[INET_ADDRSTRLEN];

/* Compute pointer to tcp header */
	tc = (struct tcphdr *)ip_next(pip);

/* Don't modify if once already modified */

	if (GetAckModified(lnk))
		return;

/* Translate destination address and port to string form */
	snprintf(buffer, sizeof(buffer) - 2, "[DEST %s %d]",
	    inet_ntoa_r(GetProxyAddress(lnk), INET_NTOA_BUF(addrbuf)),
	    (u_int) ntohs(GetProxyPort(lnk)));

/* Pad string out to a multiple of two in length */
	slen = strlen(buffer);
	switch (slen % 2) {
	case 0:
		strcat(buffer, " \n");
		slen += 2;
		break;
	case 1:
		strcat(buffer, "\n");
		slen += 1;
	}

/* Check for packet overflow */
	if ((int)(ntohs(pip->ip_len) + strlen(buffer)) > maxpacketsize)
		return;

/* Shift existing TCP data and insert destination string */
	{
		int dlen;
		int hlen;
		char *p;

		hlen = (pip->ip_hl + tc->th_off) << 2;
		dlen = ntohs(pip->ip_len) - hlen;

/* Modify first packet that has data in it */

		if (dlen == 0)
			return;

		p = (char *)pip;
		p += hlen;

		bcopy(p, p + slen, dlen);
		memcpy(p, buffer, slen);
	}

/* Save information about modfied sequence number */
	{
		int delta;

		SetAckModified(lnk);
		tc = (struct tcphdr *)ip_next(pip);			
		delta = GetDeltaSeqOut(tc->th_seq, lnk);
		AddSeq(lnk, delta + slen, pip->ip_hl, pip->ip_len, tc->th_seq,
		    tc->th_off);
	}

/* Update IP header packet length and checksum */
	{
		int accumulate;

		accumulate = pip->ip_len;
		pip->ip_len = htons(ntohs(pip->ip_len) + slen);
		accumulate -= pip->ip_len;

		ADJUST_CHECKSUM(accumulate, pip->ip_sum);
	}

/* Update TCP checksum, Use TcpChecksum since so many things have
   already changed. */

	tc->th_sum = 0;
#ifdef _KERNEL
	tc->th_x2 = 1;
#else
	tc->th_sum = TcpChecksum(pip);
#endif
}

static void
ProxyEncodeIpHeader(struct ip *pip,
    int maxpacketsize)
{
#define OPTION_LEN_BYTES  8
#define OPTION_LEN_INT16  4
#define OPTION_LEN_INT32  2
	u_char option[OPTION_LEN_BYTES];

#ifdef LIBALIAS_DEBUG
	fprintf(stdout, " ip cksum 1 = %x\n", (u_int) IpChecksum(pip));
	fprintf(stdout, "tcp cksum 1 = %x\n", (u_int) TcpChecksum(pip));
#endif

	(void)maxpacketsize;

/* Check to see that there is room to add an IP option */
	if (pip->ip_hl > (0x0f - OPTION_LEN_INT32))
		return;

/* Build option and copy into packet */
	{
		u_char *ptr;
		struct tcphdr *tc;

		ptr = (u_char *) pip;
		ptr += 20;
		memcpy(ptr + OPTION_LEN_BYTES, ptr, ntohs(pip->ip_len) - 20);

		option[0] = 0x64;	/* class: 3 (reserved), option 4 */
		option[1] = OPTION_LEN_BYTES;

		memcpy(&option[2], (u_char *) & pip->ip_dst, 4);

		tc = (struct tcphdr *)ip_next(pip);
		memcpy(&option[6], (u_char *) & tc->th_sport, 2);

		memcpy(ptr, option, 8);
	}

/* Update checksum, header length and packet length */
	{
		int i;
		int accumulate;
		u_short *sptr;

		sptr = (u_short *) option;
		accumulate = 0;
		for (i = 0; i < OPTION_LEN_INT16; i++)
			accumulate -= *(sptr++);

		sptr = (u_short *) pip;
		accumulate += *sptr;
		pip->ip_hl += OPTION_LEN_INT32;
		accumulate -= *sptr;

		accumulate += pip->ip_len;
		pip->ip_len = htons(ntohs(pip->ip_len) + OPTION_LEN_BYTES);
		accumulate -= pip->ip_len;

		ADJUST_CHECKSUM(accumulate, pip->ip_sum);
	}
#undef OPTION_LEN_BYTES
#undef OPTION_LEN_INT16
#undef OPTION_LEN_INT32
#ifdef LIBALIAS_DEBUG
	fprintf(stdout, " ip cksum 2 = %x\n", (u_int) IpChecksum(pip));
	fprintf(stdout, "tcp cksum 2 = %x\n", (u_int) TcpChecksum(pip));
#endif
}


/* Functions by other packet alias source files

    ProxyCheck()         -- Checks whether an outgoing packet should
			    be proxied.
    ProxyModify()        -- Encodes the original destination address/port
			    for a packet which is to be redirected to
			    a proxy server.
*/

int
ProxyCheck(struct libalias *la, struct in_addr *proxy_server_addr,
    u_short * proxy_server_port, struct in_addr src_addr, 
    struct in_addr dst_addr, u_short dst_port, u_char ip_p)
{
	struct proxy_entry *ptr;

	LIBALIAS_LOCK_ASSERT(la);

	ptr = la->proxyList;
	while (ptr != NULL) {
		u_short proxy_port;

		proxy_port = ptr->proxy_port;
		if ((dst_port == proxy_port || proxy_port == 0)
		    && ip_p == ptr->proto
		    && src_addr.s_addr != ptr->server_addr.s_addr) {
			struct in_addr src_addr_masked;
			struct in_addr dst_addr_masked;

			src_addr_masked.s_addr = src_addr.s_addr & ptr->src_mask.s_addr;
			dst_addr_masked.s_addr = dst_addr.s_addr & ptr->dst_mask.s_addr;

			if ((src_addr_masked.s_addr == ptr->src_addr.s_addr)
			    && (dst_addr_masked.s_addr == ptr->dst_addr.s_addr)) {
				if ((*proxy_server_port = ptr->server_port) == 0)
					*proxy_server_port = dst_port;
				*proxy_server_addr = ptr->server_addr;
				return (ptr->proxy_type);
			}
		}
		ptr = ptr->next;
	}

	return (0);
}

void
ProxyModify(struct libalias *la, struct alias_link *lnk,
    struct ip *pip,
    int maxpacketsize,
    int proxy_type)
{

	LIBALIAS_LOCK_ASSERT(la);
	(void)la;

	switch (proxy_type) {
		case PROXY_TYPE_ENCODE_IPHDR:
		ProxyEncodeIpHeader(pip, maxpacketsize);
		break;

	case PROXY_TYPE_ENCODE_TCPSTREAM:
		ProxyEncodeTcpStream(lnk, pip, maxpacketsize);
		break;
	}
}


/*
    Public API functions
*/

int
LibAliasProxyRule(struct libalias *la, const char *cmd)
{
/*
 * This function takes command strings of the form:
 *
 *   server <addr>[:<port>]
 *   [port <port>]
 *   [rule n]
 *   [proto tcp|udp]
 *   [src <addr>[/n]]
 *   [dst <addr>[/n]]
 *   [type encode_tcp_stream|encode_ip_hdr|no_encode]
 *
 *   delete <rule number>
 *
 * Subfields can be in arbitrary order.  Port numbers and addresses
 * must be in either numeric or symbolic form. An optional rule number
 * is used to control the order in which rules are searched.  If two
 * rules have the same number, then search order cannot be guaranteed,
 * and the rules should be disjoint.  If no rule number is specified,
 * then 0 is used, and group 0 rules are always checked before any
 * others.
 */
	int i, n, len, ret;
	int cmd_len;
	int token_count;
	int state;
	char *token;
	char buffer[256];
	char str_port[sizeof(buffer)];
	char str_server_port[sizeof(buffer)];
	char *res = buffer;

	int rule_index;
	int proto;
	int proxy_type;
	int proxy_port;
	int server_port;
	struct in_addr server_addr;
	struct in_addr src_addr, src_mask;
	struct in_addr dst_addr, dst_mask;
	struct proxy_entry *proxy_entry;

	LIBALIAS_LOCK(la);
	ret = 0;
/* Copy command line into a buffer */
	cmd += strspn(cmd, " \t");
	cmd_len = strlen(cmd);
	if (cmd_len > (int)(sizeof(buffer) - 1)) {
		ret = -1;
		goto getout;
	}
	strcpy(buffer, cmd);

/* Convert to lower case */
	len = strlen(buffer);
	for (i = 0; i < len; i++)
		buffer[i] = tolower((unsigned char)buffer[i]);

/* Set default proxy type */

/* Set up default values */
	rule_index = 0;
	proxy_type = PROXY_TYPE_ENCODE_NONE;
	proto = IPPROTO_TCP;
	proxy_port = 0;
	server_addr.s_addr = 0;
	server_port = 0;
	src_addr.s_addr = 0;
	IpMask(0, &src_mask);
	dst_addr.s_addr = 0;
	IpMask(0, &dst_mask);

	str_port[0] = 0;
	str_server_port[0] = 0;

/* Parse command string with state machine */
#define STATE_READ_KEYWORD    0
#define STATE_READ_TYPE       1
#define STATE_READ_PORT       2
#define STATE_READ_SERVER     3
#define STATE_READ_RULE       4
#define STATE_READ_DELETE     5
#define STATE_READ_PROTO      6
#define STATE_READ_SRC        7
#define STATE_READ_DST        8
	state = STATE_READ_KEYWORD;
	token = strsep(&res, " \t");
	token_count = 0;
	while (token != NULL) {
		token_count++;
		switch (state) {
		case STATE_READ_KEYWORD:
			if (strcmp(token, "type") == 0)
				state = STATE_READ_TYPE;
			else if (strcmp(token, "port") == 0)
				state = STATE_READ_PORT;
			else if (strcmp(token, "server") == 0)
				state = STATE_READ_SERVER;
			else if (strcmp(token, "rule") == 0)
				state = STATE_READ_RULE;
			else if (strcmp(token, "delete") == 0)
				state = STATE_READ_DELETE;
			else if (strcmp(token, "proto") == 0)
				state = STATE_READ_PROTO;
			else if (strcmp(token, "src") == 0)
				state = STATE_READ_SRC;
			else if (strcmp(token, "dst") == 0)
				state = STATE_READ_DST;
			else {
				ret = -1;
				goto getout;
			}
			break;

		case STATE_READ_TYPE:
			if (strcmp(token, "encode_ip_hdr") == 0)
				proxy_type = PROXY_TYPE_ENCODE_IPHDR;
			else if (strcmp(token, "encode_tcp_stream") == 0)
				proxy_type = PROXY_TYPE_ENCODE_TCPSTREAM;
			else if (strcmp(token, "no_encode") == 0)
				proxy_type = PROXY_TYPE_ENCODE_NONE;
			else {
				ret = -1;
				goto getout;
			}
			state = STATE_READ_KEYWORD;
			break;

		case STATE_READ_PORT:
			strcpy(str_port, token);
			state = STATE_READ_KEYWORD;
			break;

		case STATE_READ_SERVER:
			{
				int err;
				char *p;
				char s[sizeof(buffer)];

				p = token;
				while (*p != ':' && *p != 0)
					p++;

				if (*p != ':') {
					err = IpAddr(token, &server_addr);
					if (err) {
						ret = -1;
						goto getout;
					}
				} else {
					*p = ' ';

					n = sscanf(token, "%s %s", s, str_server_port);
					if (n != 2) {
						ret = -1;
						goto getout;
					}

					err = IpAddr(s, &server_addr);
					if (err) {
						ret = -1;
						goto getout;
					}
				}
			}
			state = STATE_READ_KEYWORD;
			break;

		case STATE_READ_RULE:
			n = sscanf(token, "%d", &rule_index);
			if (n != 1 || rule_index < 0) {
				ret = -1;
				goto getout;
			}
			state = STATE_READ_KEYWORD;
			break;

		case STATE_READ_DELETE:
			{
				int err;
				int rule_to_delete;

				if (token_count != 2) {
					ret = -1;
					goto getout;
				}

				n = sscanf(token, "%d", &rule_to_delete);
				if (n != 1) {
					ret = -1;
					goto getout;
				}
				err = RuleNumberDelete(la, rule_to_delete);
				if (err)
					ret = -1;
				else
					ret = 0;
				goto getout;
			}

		case STATE_READ_PROTO:
			if (strcmp(token, "tcp") == 0)
				proto = IPPROTO_TCP;
			else if (strcmp(token, "udp") == 0)
				proto = IPPROTO_UDP;
			else {
				ret = -1;
				goto getout;
			}
			state = STATE_READ_KEYWORD;
			break;

		case STATE_READ_SRC:
		case STATE_READ_DST:
			{
				int err;
				char *p;
				struct in_addr mask;
				struct in_addr addr;

				p = token;
				while (*p != '/' && *p != 0)
					p++;

				if (*p != '/') {
					IpMask(32, &mask);
					err = IpAddr(token, &addr);
					if (err) {
						ret = -1;
						goto getout;
					}
				} else {
					int nbits;
					char s[sizeof(buffer)];

					*p = ' ';
					n = sscanf(token, "%s %d", s, &nbits);
					if (n != 2) {
						ret = -1;
						goto getout;
					}

					err = IpAddr(s, &addr);
					if (err) {
						ret = -1;
						goto getout;
					}

					err = IpMask(nbits, &mask);
					if (err) {
						ret = -1;
						goto getout;
					}
				}

				if (state == STATE_READ_SRC) {
					src_addr = addr;
					src_mask = mask;
				} else {
					dst_addr = addr;
					dst_mask = mask;
				}
			}
			state = STATE_READ_KEYWORD;
			break;

		default:
			ret = -1;
			goto getout;
			break;
		}

		do {
			token = strsep(&res, " \t");
		} while (token != NULL && !*token);
	}
#undef STATE_READ_KEYWORD
#undef STATE_READ_TYPE
#undef STATE_READ_PORT
#undef STATE_READ_SERVER
#undef STATE_READ_RULE
#undef STATE_READ_DELETE
#undef STATE_READ_PROTO
#undef STATE_READ_SRC
#undef STATE_READ_DST

/* Convert port strings to numbers.  This needs to be done after
   the string is parsed, because the prototype might not be designated
   before the ports (which might be symbolic entries in /etc/services) */

	if (strlen(str_port) != 0) {
		int err;

		err = IpPort(str_port, proto, &proxy_port);
		if (err) {
			ret = -1;
			goto getout;
		}
	} else {
		proxy_port = 0;
	}

	if (strlen(str_server_port) != 0) {
		int err;

		err = IpPort(str_server_port, proto, &server_port);
		if (err) {
			ret = -1;
			goto getout;
		}
	} else {
		server_port = 0;
	}

/* Check that at least the server address has been defined */
	if (server_addr.s_addr == 0) {
		ret = -1;
		goto getout;
	}

/* Add to linked list */
	proxy_entry = malloc(sizeof(struct proxy_entry));
	if (proxy_entry == NULL) {
		ret = -1;
		goto getout;
	}

	proxy_entry->proxy_type = proxy_type;
	proxy_entry->rule_index = rule_index;
	proxy_entry->proto = proto;
	proxy_entry->proxy_port = htons(proxy_port);
	proxy_entry->server_port = htons(server_port);
	proxy_entry->server_addr = server_addr;
	proxy_entry->src_addr.s_addr = src_addr.s_addr & src_mask.s_addr;
	proxy_entry->dst_addr.s_addr = dst_addr.s_addr & dst_mask.s_addr;
	proxy_entry->src_mask = src_mask;
	proxy_entry->dst_mask = dst_mask;

	RuleAdd(la, proxy_entry);

getout:
	LIBALIAS_UNLOCK(la);
	return (ret);
}
