/*	$OpenBSD: packet.c,v 1.15 2025/02/07 21:48:26 bluhm Exp $	*/

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

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/if_ether.h>

#include <stdio.h>
#include <string.h>

#include "dhcp.h"
#include "tree.h"
#include "dhcpd.h"
#include "log.h"

u_int32_t
checksum(unsigned char *buf, u_int32_t nbytes, u_int32_t sum)
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
assemble_hw_header(struct interface_info *interface, unsigned char *buf,
    int *bufix, struct hardware *to)
{
	struct ether_header eh;

	if (to != NULL && to->hlen == 6) /* XXX */
		memcpy(eh.ether_dhost, to->haddr, sizeof(eh.ether_dhost));
	else
		memset(eh.ether_dhost, 0xff, sizeof(eh.ether_dhost));

	/* source address is filled in by the kernel */
	memset(eh.ether_shost, 0x00, sizeof(eh.ether_shost));

	eh.ether_type = htons(ETHERTYPE_IP);

	memcpy(&buf[*bufix], &eh, ETHER_HDR_LEN);
	*bufix += ETHER_HDR_LEN;
}

void
assemble_udp_ip_header(struct interface_info *interface, unsigned char *buf,
    int *bufix, u_int32_t from, u_int32_t to, unsigned int port,
    unsigned char *data, int len)
{
	struct ip ip;
	struct udphdr udp;

	ip.ip_v = 4;
	ip.ip_hl = 5;
	ip.ip_tos = IPTOS_LOWDELAY;
	ip.ip_len = htons(sizeof(ip) + sizeof(udp) + len);
	ip.ip_id = 0;
	ip.ip_off = 0;
	ip.ip_ttl = 16;
	ip.ip_p = IPPROTO_UDP;
	ip.ip_sum = 0;
	ip.ip_src.s_addr = from;
	ip.ip_dst.s_addr = to;

	ip.ip_sum = wrapsum(checksum((unsigned char *)&ip, sizeof(ip), 0));
	memcpy(&buf[*bufix], &ip, sizeof(ip));
	*bufix += sizeof(ip);

	udp.uh_sport = server_port;	/* XXX */
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
decode_hw_header(unsigned char *buf, u_int32_t buflen, struct hardware *from)
{
	struct ether_header eh;

	if (buflen < sizeof(eh))
		return (-1);

	memcpy(&eh, buf, sizeof(eh));

	memcpy(from->haddr, eh.ether_shost, sizeof(eh.ether_shost));
	from->htype = ARPHRD_ETHER;
	from->hlen = sizeof(eh.ether_shost);

	return (sizeof(eh));
}

ssize_t
decode_udp_ip_header(unsigned char *buf, u_int32_t buflen,
    struct sockaddr_in *from, u_int16_t csumflags)
{
	struct ip *ip;
	struct udphdr *udp;
	unsigned char *data;
	u_int32_t ip_len;
	static unsigned int ip_packets_seen;
	static unsigned int ip_packets_bad_checksum;
	static unsigned int udp_packets_seen;
	static unsigned int udp_packets_bad_checksum;
	static unsigned int udp_packets_length_checked;
	static unsigned int udp_packets_length_overflow;
	int len;

	/* Assure that an entire IP header is within the buffer. */
	if (sizeof(*ip) > buflen)
		return (-1);
	ip_len = (*buf & 0xf) << 2;
	if (ip_len > buflen)
		return (-1);

	ip = (struct ip *)buf;
	ip_packets_seen++;

	/* Check the IP header checksum - it should be zero. */
	if ((csumflags & M_IPV4_CSUM_IN_OK) == 0 &&
	    wrapsum(checksum((unsigned char *)ip, ip_len, 0)) != 0) {
		ip_packets_bad_checksum++;
		if (ip_packets_seen > 4 && ip_packets_bad_checksum != 0 &&
		    (ip_packets_seen / ip_packets_bad_checksum) < 2) {
			log_info("%u bad IP checksums seen in %u packets",
			    ip_packets_bad_checksum, ip_packets_seen);
			ip_packets_seen = ip_packets_bad_checksum = 0;
		}
		return (-1);
	}

	memcpy(&from->sin_addr, &ip->ip_src, sizeof(from->sin_addr));

#ifdef DEBUG
	if (ntohs(ip->ip_len) != buflen)
		log_debug("ip length %d disagrees with bytes received %d.",
		    ntohs(ip->ip_len), buflen);
#endif


	/* Assure that the entire IP packet is within the buffer. */
	if (ntohs(ip->ip_len) > buflen)
		return (-1);

	/* Assure that the UDP header is within the buffer. */
	if (ip_len + sizeof(*udp) > buflen)
		return (-1);
	udp = (struct udphdr *)(buf + ip_len);
	udp_packets_seen++;

	/* Assure that the entire UDP packet is within the buffer. */
	if (ip_len + ntohs(udp->uh_ulen) > buflen)
		return (-1);
	data = buf + ip_len + sizeof(*udp);

	/*
	 * Compute UDP checksums, including the ``pseudo-header'', the
	 * UDP header and the data. If the UDP checksum field is zero,
	 * we're not supposed to do a checksum.
	 */
	udp_packets_length_checked++;
	len = ntohs(udp->uh_ulen) - sizeof(*udp);
	if ((len < 0) || (len + data > buf + buflen)) {
		udp_packets_length_overflow++;
		if (udp_packets_length_checked > 4 &&
		    udp_packets_length_overflow != 0 &&
		    (udp_packets_length_checked /
		    udp_packets_length_overflow) < 2) {
			log_info("%u udp packets in %u too long - dropped",
			    udp_packets_length_overflow,
			    udp_packets_length_checked);
			udp_packets_length_overflow =
			    udp_packets_length_checked = 0;
		}
		return (-1);
	}
	if (len + data != buf + buflen)
		log_debug("accepting packet with data after udp payload.");

	udp_packets_seen++;

	if ((csumflags & M_UDP_CSUM_IN_OK) == 0 &&
	    udp->uh_sum != 0) {
		udp->uh_sum = wrapsum(checksum((uint8_t *)udp, sizeof(*udp),
		    checksum(data, len,
		    checksum((uint8_t *)&ip->ip_src, 2 * sizeof(ip->ip_src),
		    IPPROTO_UDP + ntohs(udp->uh_ulen)))));

		if (udp->uh_sum != 0) {
			udp_packets_bad_checksum++;
			if (udp_packets_seen > 4 &&
			    udp_packets_bad_checksum != 0 &&
			    (udp_packets_seen / udp_packets_bad_checksum) < 2) {
				log_info("%u bad udp checksums in %u packets",
				    udp_packets_bad_checksum, udp_packets_seen);
				udp_packets_seen = udp_packets_bad_checksum = 0;
			}
			return (-1);
		}
	}

	memcpy(&from->sin_port, &udp->uh_sport, sizeof(udp->uh_sport));

	return (ip_len + sizeof(*udp));
}
