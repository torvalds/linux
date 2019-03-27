/*	$OpenBSD: packet.c,v 1.9 2004/05/04 18:58:50 deraadt Exp $	*/

/* Packet assembly code, originally contributed by Archie Cobbs. */

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "dhcpd.h"

#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/if_ether.h>

#define ETHER_HEADER_SIZE (ETHER_ADDR_LEN * 2 + sizeof(u_int16_t))

u_int32_t	checksum(unsigned char *, unsigned, u_int32_t);
u_int32_t	wrapsum(u_int32_t);

u_int32_t
checksum(unsigned char *buf, unsigned nbytes, u_int32_t sum)
{
	unsigned i;

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
assemble_hw_header(struct interface_info *interface, unsigned char *buf,
    int *bufix)
{
	struct ether_header eh;

	memset(eh.ether_dhost, 0xff, sizeof(eh.ether_dhost));
	if (interface->hw_address.hlen == sizeof(eh.ether_shost))
		memcpy(eh.ether_shost, interface->hw_address.haddr,
		    sizeof(eh.ether_shost));
	else
		memset(eh.ether_shost, 0x00, sizeof(eh.ether_shost));

	eh.ether_type = htons(ETHERTYPE_IP);

	memcpy(&buf[*bufix], &eh, ETHER_HEADER_SIZE);
	*bufix += ETHER_HEADER_SIZE;
}

void
assemble_udp_ip_header(unsigned char *buf, int *bufix, u_int32_t from,
    u_int32_t to, unsigned int port, unsigned char *data, int len)
{
	struct ip ip;
	struct udphdr udp;

	ip.ip_v = 4;
	ip.ip_hl = 5;
	ip.ip_tos = IPTOS_LOWDELAY;
	ip.ip_len = htons(sizeof(ip) + sizeof(udp) + len);
	ip.ip_id = 0;
	ip.ip_off = 0;
	ip.ip_ttl = 128;
	ip.ip_p = IPPROTO_UDP;
	ip.ip_sum = 0;
	ip.ip_src.s_addr = from;
	ip.ip_dst.s_addr = to;

	ip.ip_sum = wrapsum(checksum((unsigned char *)&ip, sizeof(ip), 0));
	memcpy(&buf[*bufix], &ip, sizeof(ip));
	*bufix += sizeof(ip);

	udp.uh_sport = htons(LOCAL_PORT);	/* XXX */
	udp.uh_dport = port;			/* XXX */
	udp.uh_ulen = htons(sizeof(udp) + len);
	memset(&udp.uh_sum, 0, sizeof(udp.uh_sum));

	udp.uh_sum = wrapsum(checksum((unsigned char *)&udp, sizeof(udp),
	    checksum(data, len, checksum((unsigned char *)&ip.ip_src,
	    2 * sizeof(ip.ip_src),
	    IPPROTO_UDP + (u_int32_t)ntohs(udp.uh_ulen)))));

	memcpy(&buf[*bufix], &udp, sizeof(udp));
	*bufix += sizeof(udp);
}

ssize_t
decode_hw_header(unsigned char *buf, int bufix, struct hardware *from)
{
	struct ether_header eh;

	memcpy(&eh, buf + bufix, ETHER_HEADER_SIZE);

	memcpy(from->haddr, eh.ether_shost, sizeof(eh.ether_shost));
	from->htype = ARPHRD_ETHER;
	from->hlen = sizeof(eh.ether_shost);

	return (sizeof(eh));
}

ssize_t
decode_udp_ip_header(unsigned char *buf, int bufix, struct sockaddr_in *from,
    unsigned char *data, int buflen)
{
	struct ip *ip;
	struct udphdr *udp;
	u_int32_t ip_len = (buf[bufix] & 0xf) << 2;
	u_int32_t sum, usum;
	static int ip_packets_seen;
	static int ip_packets_bad_checksum;
	static int udp_packets_seen;
	static int udp_packets_bad_checksum;
	static int udp_packets_length_checked;
	static int udp_packets_length_overflow;
	int len = 0;

	ip = (struct ip *)(buf + bufix);
	udp = (struct udphdr *)(buf + bufix + ip_len);

	/* Check the IP header checksum - it should be zero. */
	ip_packets_seen++;
	if (wrapsum(checksum(buf + bufix, ip_len, 0)) != 0) {
		ip_packets_bad_checksum++;
		if (ip_packets_seen > 4 &&
		    (ip_packets_seen / ip_packets_bad_checksum) < 2) {
			note("%d bad IP checksums seen in %d packets",
			    ip_packets_bad_checksum, ip_packets_seen);
			ip_packets_seen = ip_packets_bad_checksum = 0;
		}
		return (-1);
	}

	if (ntohs(ip->ip_len) != buflen)
		debug("ip length %d disagrees with bytes received %d.",
		    ntohs(ip->ip_len), buflen);

	memcpy(&from->sin_addr, &ip->ip_src, 4);

	/*
	 * Compute UDP checksums, including the ``pseudo-header'', the
	 * UDP header and the data.   If the UDP checksum field is zero,
	 * we're not supposed to do a checksum.
	 */
	if (!data) {
		data = buf + bufix + ip_len + sizeof(*udp);
		len = ntohs(udp->uh_ulen) - sizeof(*udp);
		udp_packets_length_checked++;
		if (len + data > buf + bufix + buflen) {
			udp_packets_length_overflow++;
			if (udp_packets_length_checked > 4 &&
			    (udp_packets_length_checked /
			    udp_packets_length_overflow) < 2) {
				note("%d udp packets in %d too long - dropped",
				    udp_packets_length_overflow,
				    udp_packets_length_checked);
				udp_packets_length_overflow =
				    udp_packets_length_checked = 0;
			}
			return (-1);
		}
		if (len + data != buf + bufix + buflen)
			debug("accepting packet with data after udp payload.");
	}

	usum = udp->uh_sum;
	udp->uh_sum = 0;

	sum = wrapsum(checksum((unsigned char *)udp, sizeof(*udp),
	    checksum(data, len, checksum((unsigned char *)&ip->ip_src,
	    2 * sizeof(ip->ip_src),
	    IPPROTO_UDP + (u_int32_t)ntohs(udp->uh_ulen)))));

	udp_packets_seen++;
	if (usum && usum != sum) {
		udp_packets_bad_checksum++;
		if (udp_packets_seen > 4 &&
		    (udp_packets_seen / udp_packets_bad_checksum) < 2) {
			note("%d bad udp checksums in %d packets",
			    udp_packets_bad_checksum, udp_packets_seen);
			udp_packets_seen = udp_packets_bad_checksum = 0;
		}
		return (-1);
	}

	memcpy(&from->sin_port, &udp->uh_sport, sizeof(udp->uh_sport));

	return (ip_len + sizeof(*udp));
}
