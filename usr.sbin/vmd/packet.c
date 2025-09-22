/*	$OpenBSD: packet.c,v 1.4 2021/06/16 16:55:02 dv Exp $	*/

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

#include <arpa/inet.h>

#include <net/if.h>
#include <net/if_enc.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/if_ether.h>

#include <string.h>

#include "dhcp.h"
#include "vmd.h"

u_int32_t	checksum(unsigned char *, u_int32_t, u_int32_t);
u_int32_t	wrapsum(u_int32_t);

u_int32_t
checksum(unsigned char *buf, u_int32_t nbytes, u_int32_t sum)
{
	u_int32_t i;

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

ssize_t
assemble_hw_header(unsigned char *buf, size_t buflen,
    size_t offset, struct packet_ctx *pc, unsigned int intfhtype)
{
	struct ether_header eh;

	switch (intfhtype) {
	case HTYPE_ETHER:
		if (buflen < offset + ETHER_HDR_LEN)
			return (-1);

		/* Use the supplied address or let the kernel fill it. */
		memcpy(eh.ether_shost, pc->pc_smac, ETHER_ADDR_LEN);
		memcpy(eh.ether_dhost, pc->pc_dmac, ETHER_ADDR_LEN);

		eh.ether_type = htons(ETHERTYPE_IP);

		memcpy(&buf[offset], &eh, ETHER_HDR_LEN);
		offset += ETHER_HDR_LEN;
		break;
	default:
		return (-1);
	}

	return (offset);
}

ssize_t
assemble_udp_ip_header(unsigned char *buf, size_t buflen, size_t offset,
    struct packet_ctx *pc, unsigned char *data, size_t datalen)
{
	struct ip ip;
	struct udphdr udp;

	if (buflen < offset + sizeof(ip) + sizeof(udp))
		return (-1);

	ip.ip_v = 4;
	ip.ip_hl = 5;
	ip.ip_tos = IPTOS_LOWDELAY;
	ip.ip_len = htons(sizeof(ip) + sizeof(udp) + datalen);
	ip.ip_id = 0;
	ip.ip_off = 0;
	ip.ip_ttl = 16;
	ip.ip_p = IPPROTO_UDP;
	ip.ip_sum = 0;
	ip.ip_src.s_addr = ss2sin(&pc->pc_src)->sin_addr.s_addr;
	ip.ip_dst.s_addr = ss2sin(&pc->pc_dst)->sin_addr.s_addr;

	ip.ip_sum = wrapsum(checksum((unsigned char *)&ip, sizeof(ip), 0));
	memcpy(&buf[offset], &ip, sizeof(ip));
	offset += sizeof(ip);

	udp.uh_sport = ss2sin(&pc->pc_src)->sin_port;
	udp.uh_dport = ss2sin(&pc->pc_dst)->sin_port;
	udp.uh_ulen = htons(sizeof(udp) + datalen);
	memset(&udp.uh_sum, 0, sizeof(udp.uh_sum));

	udp.uh_sum = wrapsum(checksum((unsigned char *)&udp, sizeof(udp),
	    checksum(data, datalen, checksum((unsigned char *)&ip.ip_src,
	    2 * sizeof(ip.ip_src),
	    IPPROTO_UDP + (u_int32_t)ntohs(udp.uh_ulen)))));

	memcpy(&buf[offset], &udp, sizeof(udp));
	offset += sizeof(udp);

	return (offset);
}

ssize_t
decode_hw_header(unsigned char *buf, size_t buflen,
    size_t offset, struct packet_ctx *pc, unsigned int intfhtype)
{
	u_int32_t ip_len;
	u_int16_t ether_type;
	struct ether_header *eh;
	struct ip *ip;

	switch (intfhtype) {
	case HTYPE_IPSEC_TUNNEL:
		if (buflen < offset + ENC_HDRLEN + sizeof(*ip))
			return (-1);
		offset += ENC_HDRLEN;
		ip_len = (buf[offset] & 0xf) << 2;
		if (buflen < offset + ip_len)
			return (-1);

		ip = (struct ip *)(buf + offset);

		/* Encapsulated IP */
		if (ip->ip_p != IPPROTO_IPIP)
			return (-1);

		memset(pc->pc_dmac, 0xff, ETHER_ADDR_LEN);
		offset += ip_len;

		pc->pc_htype = ARPHRD_ETHER;
		pc->pc_hlen = ETHER_ADDR_LEN;
		break;
	case HTYPE_ETHER:
		if (buflen < offset + ETHER_HDR_LEN)
			return (-1);

		eh = (struct ether_header *)(buf + offset);
		memcpy(pc->pc_dmac, eh->ether_dhost, ETHER_ADDR_LEN);
		memcpy(pc->pc_smac, eh->ether_shost, ETHER_ADDR_LEN);
		memcpy(&ether_type, &eh->ether_type, sizeof(ether_type));

		if (ether_type != htons(ETHERTYPE_IP))
			return (-1);

		offset += ETHER_HDR_LEN;

		pc->pc_htype = ARPHRD_ETHER;
		pc->pc_hlen = ETHER_ADDR_LEN;
		break;
	default:
		return (-1);
	}

	return (offset);
}

ssize_t
decode_udp_ip_header(unsigned char *buf, size_t buflen,
    size_t offset, struct packet_ctx *pc)
{
	struct ip *ip;
	struct udphdr *udp;
	unsigned char *data;
	u_int32_t ip_len;
	u_int32_t sum, usum;
	int len;

	/* Assure that an entire IP header is within the buffer. */
	if (buflen < offset + sizeof(*ip))
		return (-1);
	ip = (struct ip *)(buf + offset);
	if (ip->ip_v != IPVERSION)
		return (-1);
	ip_len = ip->ip_hl << 2;
	if (ip_len < sizeof(struct ip) ||
	    buflen < offset + ip_len)
		return (-1);

	if (ip->ip_p != IPPROTO_UDP)
		return (-1);

	/* Check the IP header checksum - it should be zero. */
	if (wrapsum(checksum(buf + offset, ip_len, 0)) != 0)
		return (-1);

	pc->pc_src.ss_len = sizeof(struct sockaddr_in);
	pc->pc_src.ss_family = AF_INET;
	memcpy(&ss2sin(&pc->pc_src)->sin_addr, &ip->ip_src,
	    sizeof(ss2sin(&pc->pc_src)->sin_addr));

	pc->pc_dst.ss_len = sizeof(struct sockaddr_in);
	pc->pc_dst.ss_family = AF_INET;
	memcpy(&ss2sin(&pc->pc_dst)->sin_addr, &ip->ip_dst,
	    sizeof(ss2sin(&pc->pc_dst)->sin_addr));

#ifdef DEBUG
	if (buflen != offset + ntohs(ip->ip_len))
		log_debug("ip length %d disagrees with bytes received %zd.",
		    ntohs(ip->ip_len), buflen - offset);
#endif

	/* Assure that the entire IP packet is within the buffer. */
	if (buflen < offset + ntohs(ip->ip_len))
		return (-1);

	/* Assure that the UDP header is within the buffer. */
	if (buflen < offset + ip_len + sizeof(*udp))
		return (-1);
	udp = (struct udphdr *)(buf + offset + ip_len);

	/* Assure that the entire UDP packet is within the buffer. */
	if (buflen < offset + ip_len + ntohs(udp->uh_ulen))
		return (-1);
	data = buf + offset + ip_len + sizeof(*udp);

	/*
	 * Compute UDP checksums, including the ``pseudo-header'', the
	 * UDP header and the data. If the UDP checksum field is zero,
	 * we're not supposed to do a checksum.
	 */
	len = ntohs(udp->uh_ulen) - sizeof(*udp);
	if ((len < 0) || (len + data > buf + buflen)) {
		return (-1);
	}
	if (len + data != buf + buflen)
		log_debug("accepting packet with data after udp payload.");

	usum = udp->uh_sum;
	udp->uh_sum = 0;

	sum = wrapsum(checksum((unsigned char *)udp, sizeof(*udp),
	    checksum(data, len, checksum((unsigned char *)&ip->ip_src,
	    2 * sizeof(ip->ip_src),
	    IPPROTO_UDP + (u_int32_t)ntohs(udp->uh_ulen)))));

	if (usum && usum != sum)
		return (-1);

	ss2sin(&pc->pc_src)->sin_port = udp->uh_sport;
	ss2sin(&pc->pc_dst)->sin_port = udp->uh_dport;

	return (offset + ip_len + sizeof(*udp));
}
