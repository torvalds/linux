/*	$OpenBSD: packet.c,v 1.2 2025/02/07 23:08:48 bluhm Exp $	*/

/* Packet assembly code, originally contributed by Archie Cobbs. */

/*
 * Copyright (c) 1995, 1996, 1999 The Internet Software Consortium.
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
#include <sys/mbuf.h>

#include <arpa/inet.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/udp.h>
#include <netinet/if_ether.h>

#include <string.h>

#include "dhcp.h"
#include "dhcpd.h"
#include "log.h"


u_int32_t	checksum(unsigned char *, unsigned, u_int32_t);
u_int32_t	wrapsum(u_int32_t);

u_int32_t
checksum(unsigned char *buf, unsigned nbytes, u_int32_t sum)
{
	unsigned int i;

	/* Checksum all the pairs of bytes first... */
	for (i = 0; i < (nbytes & ~1U); i += 2) {
		sum += (u_int16_t)ntohs(*((u_int16_t *)(buf + i)));
		if (sum > 0xFFFF)
			sum -= 0xFFFF;
	}

	/*
	 * If there's a single byte left over, checksum it, too.
	 * Network byte order is big-endian, so the remaining byte is
	 * the high byte.
	 */
	if (i < nbytes) {
		sum += buf[i] << 8;
		if (sum > 0xFFFF)
			sum -= 0xFFFF;
	}

	return (sum);
}

u_int32_t
wrapsum(u_int32_t sum)
{
	sum = ~sum & 0xFFFF;
	return (htons(sum));
}

void
assemble_hw_header(unsigned char *buf, int *bufix, struct packet_ctx *pc)
{
	struct ether_header eh;

	memcpy(eh.ether_shost, pc->pc_smac, ETHER_ADDR_LEN);
	memcpy(eh.ether_dhost, pc->pc_dmac, ETHER_ADDR_LEN);
	eh.ether_type = htons(pc->pc_ethertype);

	memcpy(&buf[*bufix], &eh, ETHER_HDR_LEN);
	*bufix += ETHER_HDR_LEN;
}

void
assemble_udp_ip6_header(unsigned char *p, int *off, struct packet_ctx *pc,
    unsigned char *payload, int plen)
{
	struct ip6_hdr		 ip6;
	struct udphdr		 uh;

	memset(&ip6, 0, sizeof(ip6));
	ip6.ip6_vfc = IPV6_VERSION;
	ip6.ip6_nxt = IPPROTO_UDP;
	ip6.ip6_src = ss2sin6(&pc->pc_src)->sin6_addr;
	ip6.ip6_dst = ss2sin6(&pc->pc_dst)->sin6_addr;
	ip6.ip6_plen = htons(sizeof(uh) + plen);
	ip6.ip6_hlim = 64;
	memcpy(&p[*off], &ip6, sizeof(ip6));
	*off += sizeof(ip6);

	memset(&uh, 0, sizeof(uh));
	uh.uh_ulen = ip6.ip6_plen;
	uh.uh_sport = ss2sin6(&pc->pc_src)->sin6_port;
	uh.uh_dport = ss2sin6(&pc->pc_dst)->sin6_port;
	uh.uh_sum = wrapsum(
	    checksum((unsigned char *)&uh, sizeof(uh),
	    checksum(payload, plen,
	    checksum((unsigned char *)&ip6.ip6_src, sizeof(ip6.ip6_src),
	    checksum((unsigned char *)&ip6.ip6_dst, sizeof(ip6.ip6_dst),
	    IPPROTO_UDP + ntohs(ip6.ip6_plen)
	    ))))
	);
	memcpy(&p[*off], &uh, sizeof(uh));
	*off += sizeof(uh);
}

ssize_t
decode_hw_header(unsigned char *buf, int bufix, struct packet_ctx *pc)
{
	struct ether_header *ether;

	ether = (struct ether_header *)(buf + bufix);
	memcpy(pc->pc_dmac, ether->ether_dhost, ETHER_ADDR_LEN);
	memcpy(pc->pc_smac, ether->ether_shost, ETHER_ADDR_LEN);
	pc->pc_ethertype = ntohs(ether->ether_type);

	pc->pc_htype = ARPHRD_ETHER;
	pc->pc_hlen = ETHER_ADDR_LEN;

	return sizeof(struct ether_header);
}

ssize_t
decode_udp_ip6_header(unsigned char *p, int off, struct packet_ctx *pc,
   size_t plen, u_int16_t csumflags)
{
	struct ip6_hdr		*ip6;
	struct udphdr		*uh;
	struct in6_addr		*asrc, *adst;
	size_t			 ptotal, poff = 0;

	/* Check the IPv6 header. */
	if (plen < sizeof(*ip6)) {
		log_debug("package too small (%ld)", plen);
		return -1;
	}

	ip6 = (struct ip6_hdr *)(p + off);
	if ((ip6->ip6_vfc & IPV6_VERSION_MASK) != IPV6_VERSION) {
		log_debug("invalid IPv6 version");
		return -1;
	}

	poff += sizeof(*ip6);

	ptotal = ntohs(ip6->ip6_plen);
	if (ptotal > plen) {
		log_debug("expected %ld bytes, but got %ld", ptotal, plen);
		return (-1);
	}

	pc->pc_src.ss_len = sizeof(struct sockaddr_in6);
	pc->pc_src.ss_family = AF_INET6;
	asrc = &ss2sin6(&pc->pc_src)->sin6_addr;
	memcpy(asrc, &ip6->ip6_src, sizeof(*asrc));

	pc->pc_dst.ss_len = sizeof(struct sockaddr_in6);
	pc->pc_dst.ss_family = AF_INET6;
	adst = &ss2sin6(&pc->pc_dst)->sin6_addr;
	memcpy(adst, &ip6->ip6_dst, sizeof(*adst));

	/* Deal with the UDP header. */
	if (ip6->ip6_nxt != IPPROTO_UDP) {
		/* We don't support skipping extensions yet. */
		log_debug("expected UDP header, got %#02X", ip6->ip6_nxt);
		return -1;
	}

	uh = (struct udphdr *)((uint8_t *)ip6 + sizeof(*ip6));
	ss2sin6(&pc->pc_src)->sin6_port = uh->uh_sport;
	ss2sin6(&pc->pc_dst)->sin6_port = uh->uh_dport;
	poff += sizeof(*uh);

	/* Validate the packet. */
	if ((csumflags & M_UDP_CSUM_IN_OK) == 0) {
		uh->uh_sum = wrapsum(
		    checksum((unsigned char *)asrc, sizeof(*asrc),
		    checksum((unsigned char *)adst, sizeof(*adst),
		    checksum((unsigned char *)uh, sizeof(*uh),
		    checksum(p + off + poff, ptotal - sizeof(*uh),
		    IPPROTO_UDP + ntohs(uh->uh_ulen))))));

		if (uh->uh_sum != 0) {
			log_debug("checksum invalid");
			return -1;
		}
	}

	return poff;
}
