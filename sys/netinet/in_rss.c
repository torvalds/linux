/*-
 * Copyright (c) 2010-2011 Juniper Networks, Inc.
 * All rights reserved.
 *
 * This software was developed by Robert N. M. Watson under contract
 * to Juniper Networks, Inc.
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

#include "opt_inet6.h"
#include "opt_pcbgroup.h"

#ifndef PCBGROUP
#error "options RSS depends on options PCBGROUP"
#endif

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/priv.h>
#include <sys/kernel.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/sbuf.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/netisr.h>
#include <net/rss_config.h>

#include <netinet/in.h>
#include <netinet/in_pcb.h>
#include <netinet/in_rss.h>
#include <netinet/in_var.h>

/* for software rss hash support */
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>

/*
 * Hash an IPv4 2-tuple.
 */
uint32_t
rss_hash_ip4_2tuple(struct in_addr src, struct in_addr dst)
{
	uint8_t data[sizeof(src) + sizeof(dst)];
	u_int datalen;

	datalen = 0;
	bcopy(&src, &data[datalen], sizeof(src));
	datalen += sizeof(src);
	bcopy(&dst, &data[datalen], sizeof(dst));
	datalen += sizeof(dst);
	return (rss_hash(datalen, data));
}

/*
 * Hash an IPv4 4-tuple.
 */
uint32_t
rss_hash_ip4_4tuple(struct in_addr src, u_short srcport, struct in_addr dst,
    u_short dstport)
{
	uint8_t data[sizeof(src) + sizeof(dst) + sizeof(srcport) +
	    sizeof(dstport)];
	u_int datalen;

	datalen = 0;
	bcopy(&src, &data[datalen], sizeof(src));
	datalen += sizeof(src);
	bcopy(&dst, &data[datalen], sizeof(dst));
	datalen += sizeof(dst);
	bcopy(&srcport, &data[datalen], sizeof(srcport));
	datalen += sizeof(srcport);
	bcopy(&dstport, &data[datalen], sizeof(dstport));
	datalen += sizeof(dstport);
	return (rss_hash(datalen, data));
}

/*
 * Calculate an appropriate ipv4 2-tuple or 4-tuple given the given
 * IPv4 source/destination address, UDP or TCP source/destination ports
 * and the protocol type.
 *
 * The protocol code may wish to do a software hash of the given
 * tuple.  This depends upon the currently configured RSS hash types.
 *
 * This assumes that the packet in question isn't a fragment.
 *
 * It also assumes the packet source/destination address
 * are in "incoming" packet order (ie, source is "far" address.)
 */
int
rss_proto_software_hash_v4(struct in_addr s, struct in_addr d,
    u_short sp, u_short dp, int proto,
    uint32_t *hashval, uint32_t *hashtype)
{
	uint32_t hash;

	/*
	 * Next, choose the hash type depending upon the protocol
	 * identifier.
	 */
	if ((proto == IPPROTO_TCP) &&
	    (rss_gethashconfig() & RSS_HASHTYPE_RSS_TCP_IPV4)) {
		hash = rss_hash_ip4_4tuple(s, sp, d, dp);
		*hashval = hash;
		*hashtype = M_HASHTYPE_RSS_TCP_IPV4;
		return (0);
	} else if ((proto == IPPROTO_UDP) &&
	    (rss_gethashconfig() & RSS_HASHTYPE_RSS_UDP_IPV4)) {
		hash = rss_hash_ip4_4tuple(s, sp, d, dp);
		*hashval = hash;
		*hashtype = M_HASHTYPE_RSS_UDP_IPV4;
		return (0);
	} else if (rss_gethashconfig() & RSS_HASHTYPE_RSS_IPV4) {
		/* RSS doesn't hash on other protocols like SCTP; so 2-tuple */
		hash = rss_hash_ip4_2tuple(s, d);
		*hashval = hash;
		*hashtype = M_HASHTYPE_RSS_IPV4;
		return (0);
	}

	/* No configured available hashtypes! */
	RSS_DEBUG("no available hashtypes!\n");
	return (-1);
}

/*
 * Do a software calculation of the RSS for the given mbuf.
 *
 * This is typically used by the input path to recalculate the RSS after
 * some form of packet processing (eg de-capsulation, IP fragment reassembly.)
 *
 * dir is the packet direction - RSS_HASH_PKT_INGRESS for incoming and
 * RSS_HASH_PKT_EGRESS for outgoing.
 *
 * Returns 0 if a hash was done, -1 if no hash was done, +1 if
 * the mbuf already had a valid RSS flowid.
 *
 * This function doesn't modify the mbuf.  It's up to the caller to
 * assign flowid/flowtype as appropriate.
 */
int
rss_mbuf_software_hash_v4(const struct mbuf *m, int dir, uint32_t *hashval,
    uint32_t *hashtype)
{
	const struct ip *ip;
	const struct tcphdr *th;
	const struct udphdr *uh;
	uint32_t flowid;
	uint32_t flowtype;
	uint8_t proto;
	int iphlen;
	int is_frag = 0;

	/*
	 * XXX For now this only handles hashing on incoming mbufs.
	 */
	if (dir != RSS_HASH_PKT_INGRESS) {
		RSS_DEBUG("called on EGRESS packet!\n");
		return (-1);
	}

	/*
	 * First, validate that the mbuf we have is long enough
	 * to have an IPv4 header in it.
	 */
	if (m->m_pkthdr.len < (sizeof(struct ip))) {
		RSS_DEBUG("short mbuf pkthdr\n");
		return (-1);
	}
	if (m->m_len < (sizeof(struct ip))) {
		RSS_DEBUG("short mbuf len\n");
		return (-1);
	}

	/* Ok, let's dereference that */
	ip = mtod(m, struct ip *);
	proto = ip->ip_p;
	iphlen = ip->ip_hl << 2;

	/*
	 * If this is a fragment then it shouldn't be four-tuple
	 * hashed just yet.  Once it's reassembled into a full
	 * frame it should be re-hashed.
	 */
	if (ip->ip_off & htons(IP_MF | IP_OFFMASK))
		is_frag = 1;

	/*
	 * If the mbuf flowid/flowtype matches the packet type,
	 * and we don't support the 4-tuple version of the given protocol,
	 * then signal to the owner that it can trust the flowid/flowtype
	 * details.
	 *
	 * This is a little picky - eg, if TCPv4 / UDPv4 hashing
	 * is supported but we got a TCP/UDP frame only 2-tuple hashed,
	 * then we shouldn't just "trust" the 2-tuple hash.  We need
	 * a 4-tuple hash.
	 */
	flowid = m->m_pkthdr.flowid;
	flowtype = M_HASHTYPE_GET(m);

	if (flowtype != M_HASHTYPE_NONE) {
		switch (proto) {
		case IPPROTO_UDP:
			if ((rss_gethashconfig() & RSS_HASHTYPE_RSS_UDP_IPV4) &&
			    (flowtype == M_HASHTYPE_RSS_UDP_IPV4) &&
			    (is_frag == 0)) {
				return (1);
			}
			/*
			 * Only allow 2-tuple for UDP frames if we don't also
			 * support 4-tuple for UDP.
			 */
			if ((rss_gethashconfig() & RSS_HASHTYPE_RSS_IPV4) &&
			    ((rss_gethashconfig() & RSS_HASHTYPE_RSS_UDP_IPV4) == 0) &&
			    flowtype == M_HASHTYPE_RSS_IPV4) {
				return (1);
			}
			break;
		case IPPROTO_TCP:
			if ((rss_gethashconfig() & RSS_HASHTYPE_RSS_TCP_IPV4) &&
			    (flowtype == M_HASHTYPE_RSS_TCP_IPV4) &&
			    (is_frag == 0)) {
				return (1);
			}
			/*
			 * Only allow 2-tuple for TCP frames if we don't also
			 * support 2-tuple for TCP.
			 */
			if ((rss_gethashconfig() & RSS_HASHTYPE_RSS_IPV4) &&
			    ((rss_gethashconfig() & RSS_HASHTYPE_RSS_TCP_IPV4) == 0) &&
			    flowtype == M_HASHTYPE_RSS_IPV4) {
				return (1);
			}
			break;
		default:
			if ((rss_gethashconfig() & RSS_HASHTYPE_RSS_IPV4) &&
			    flowtype == M_HASHTYPE_RSS_IPV4) {
				return (1);
			}
			break;
		}
	}

	/*
	 * Decode enough information to make a hash decision.
	 *
	 * XXX TODO: does the hardware hash on 4-tuple if IP
	 *    options are present?
	 */
	if ((rss_gethashconfig() & RSS_HASHTYPE_RSS_TCP_IPV4) &&
	    (proto == IPPROTO_TCP) &&
	    (is_frag == 0)) {
		if (m->m_len < iphlen + sizeof(struct tcphdr)) {
			RSS_DEBUG("short TCP frame?\n");
			return (-1);
		}
		th = (const struct tcphdr *)((c_caddr_t)ip + iphlen);
		return rss_proto_software_hash_v4(ip->ip_src, ip->ip_dst,
		    th->th_sport,
		    th->th_dport,
		    proto,
		    hashval,
		    hashtype);
	} else if ((rss_gethashconfig() & RSS_HASHTYPE_RSS_UDP_IPV4) &&
	    (proto == IPPROTO_UDP) &&
	    (is_frag == 0)) {
		uh = (const struct udphdr *)((c_caddr_t)ip + iphlen);
		if (m->m_len < iphlen + sizeof(struct udphdr)) {
			RSS_DEBUG("short UDP frame?\n");
			return (-1);
		}
		return rss_proto_software_hash_v4(ip->ip_src, ip->ip_dst,
		    uh->uh_sport,
		    uh->uh_dport,
		    proto,
		    hashval,
		    hashtype);
	} else if (rss_gethashconfig() & RSS_HASHTYPE_RSS_IPV4) {
		/* Default to 2-tuple hash */
		return rss_proto_software_hash_v4(ip->ip_src, ip->ip_dst,
		    0,	/* source port */
		    0,	/* destination port */
		    0,	/* IPPROTO_IP */
		    hashval,
		    hashtype);
	} else {
		RSS_DEBUG("no available hashtypes!\n");
		return (-1);
	}
}

/*
 * Similar to rss_m2cpuid, but designed to be used by the IP NETISR
 * on incoming frames.
 *
 * If an existing RSS hash exists and it matches what the configured
 * hashing is, then use it.
 *
 * If there's an existing RSS hash but the desired hash is different,
 * or if there's no useful RSS hash, then calculate it via
 * the software path.
 *
 * XXX TODO: definitely want statistics here!
 */
struct mbuf *
rss_soft_m2cpuid_v4(struct mbuf *m, uintptr_t source, u_int *cpuid)
{
	uint32_t hash_val, hash_type;
	int ret;

	M_ASSERTPKTHDR(m);

	ret = rss_mbuf_software_hash_v4(m, RSS_HASH_PKT_INGRESS,
	    &hash_val, &hash_type);
	if (ret > 0) {
		/* mbuf has a valid hash already; don't need to modify it */
		*cpuid = rss_hash2cpuid(m->m_pkthdr.flowid, M_HASHTYPE_GET(m));
	} else if (ret == 0) {
		/* hash was done; update */
		m->m_pkthdr.flowid = hash_val;
		M_HASHTYPE_SET(m, hash_type);
		*cpuid = rss_hash2cpuid(m->m_pkthdr.flowid, M_HASHTYPE_GET(m));
	} else { /* ret < 0 */
		/* no hash was done */
		*cpuid = NETISR_CPUID_NONE;
	}
	return (m);
}
