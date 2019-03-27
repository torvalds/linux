/*-
 * Copyright (c) 2016 Andrey V. Elsukov <ae@FreeBSD.org>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_ipsec.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/sockopt.h>
#include <sys/sysctl.h>

#include <netinet/in.h>
#include <netinet/in_pcb.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>

#include <net/vnet.h>

#include <netipsec/ipsec.h>
#include <netipsec/esp.h>
#include <netipsec/esp_var.h>
#include <netipsec/xform.h>

#include <netipsec/key.h>
#include <netipsec/key_debug.h>
#include <netipsec/ipsec_support.h>
#include <machine/in_cksum.h>

/*
 * Handle UDP_ENCAP socket option. Always return with released INP_WLOCK.
 */
int
udp_ipsec_pcbctl(struct inpcb *inp, struct sockopt *sopt)
{
	struct udpcb *up;
	int error, optval;

	INP_WLOCK_ASSERT(inp);
	if (sopt->sopt_name != UDP_ENCAP) {
		INP_WUNLOCK(inp);
		return (ENOPROTOOPT);
	}

	up = intoudpcb(inp);
	if (sopt->sopt_dir == SOPT_GET) {
		if (up->u_flags & UF_ESPINUDP)
			optval = UDP_ENCAP_ESPINUDP;
		else
			optval = 0;
		INP_WUNLOCK(inp);
		return (sooptcopyout(sopt, &optval, sizeof(optval)));
	}
	INP_WUNLOCK(inp);

	error = sooptcopyin(sopt, &optval, sizeof(optval), sizeof(optval));
	if (error != 0)
		return (error);

	INP_WLOCK(inp);
	switch (optval) {
	case 0:
		up->u_flags &= ~UF_ESPINUDP;
		break;
	case UDP_ENCAP_ESPINUDP:
		up->u_flags |= UF_ESPINUDP;
		break;
	default:
		error = EINVAL;
	}
	INP_WUNLOCK(inp);
	return (error);
}

/*
 * Potentially decap ESP in UDP frame.  Check for an ESP header.
 * If present, strip the UDP header and push the result through IPSec.
 *
 * Returns error if mbuf consumed and/or processed, otherwise 0.
 */
int
udp_ipsec_input(struct mbuf *m, int off, int af)
{
	union sockaddr_union dst;
	struct secasvar *sav;
	struct udphdr *udp;
	struct ip *ip;
	uint32_t spi;
	int hlen;

	/*
	 * Just return if packet doesn't have enough data.
	 * We need at least [IP header + UDP header + ESP header].
	 * NAT-Keepalive packet has only one byte of payload, so it
	 * by default will not be processed.
	 */
	if (m->m_pkthdr.len < off + sizeof(struct esp))
		return (0);

	m_copydata(m, off, sizeof(uint32_t), (caddr_t)&spi);
	if (spi == 0)	/* Non-ESP marker. */
		return (0);

	/*
	 * Find SA and check that it is configured for UDP
	 * encapsulation.
	 */
	bzero(&dst, sizeof(dst));
	dst.sa.sa_family = af;
	switch (af) {
#ifdef INET
	case AF_INET:
		dst.sin.sin_len = sizeof(struct sockaddr_in);
		ip = mtod(m, struct ip *);
		ip->ip_p = IPPROTO_ESP;
		off = offsetof(struct ip, ip_p);
		hlen = ip->ip_hl << 2;
		dst.sin.sin_addr = ip->ip_dst;
		break;
#endif
#ifdef INET6
	case AF_INET6:
		/* Not yet */
		/* FALLTHROUGH */
#endif
	default:
		ESPSTAT_INC(esps_nopf);
		m_freem(m);
		return (EPFNOSUPPORT);
	}

	sav = key_allocsa(&dst, IPPROTO_ESP, spi);
	if (sav == NULL) {
		ESPSTAT_INC(esps_notdb);
		m_freem(m);
		return (ENOENT);
	}
	udp = mtodo(m, hlen);
	if (sav->natt == NULL ||
	    sav->natt->sport != udp->uh_sport ||
	    sav->natt->dport != udp->uh_dport) {
		/* XXXAE: should we check source address? */
		ESPSTAT_INC(esps_notdb);
		key_freesav(&sav);
		m_freem(m);
		return (ENOENT);
	}
	/*
	 * Remove the UDP header
	 * Before:
	 *   <--- off --->
	 *   +----+------+-----+
	 *   | IP |  UDP | ESP |
	 *   +----+------+-----+
	 *        <-skip->
	 * After:
	 *          +----+-----+
	 *          | IP | ESP |
	 *          +----+-----+
	 *   <-skip->
	 */
	m_striphdr(m, hlen, sizeof(*udp));
	/*
	 * We cannot yet update the cksums so clear any h/w cksum flags
	 * as they are no longer valid.
	 */
	if (m->m_pkthdr.csum_flags & CSUM_DATA_VALID)
		m->m_pkthdr.csum_flags &= ~(CSUM_DATA_VALID | CSUM_PSEUDO_HDR);
	/*
	 * We can update ip_len and ip_sum here, but ipsec4_input_cb()
	 * will do this anyway, so don't touch them here.
	 */
	ESPSTAT_INC(esps_input);
	(*sav->tdb_xform->xf_input)(m, sav, hlen, off);
	return (EINPROGRESS);	/* Consumed by IPsec. */
}

int
udp_ipsec_output(struct mbuf *m, struct secasvar *sav)
{
	struct udphdr *udp;
	struct mbuf *n;
	struct ip *ip;
	int hlen, off;

	IPSEC_ASSERT(sav->natt != NULL, ("UDP encapsulation isn't required."));

	if (sav->sah->saidx.dst.sa.sa_family == AF_INET6)
		return (EAFNOSUPPORT);

	ip = mtod(m, struct ip *);
	hlen = ip->ip_hl << 2;
	n = m_makespace(m, hlen, sizeof(*udp), &off);
	if (n == NULL) {
		DPRINTF(("%s: m_makespace for udphdr failed\n", __func__));
		return (ENOBUFS);
	}

	udp = mtodo(n, off);
	udp->uh_dport = sav->natt->dport;
	udp->uh_sport = sav->natt->sport;
	udp->uh_sum = 0;
	udp->uh_ulen = htons(m->m_pkthdr.len - hlen);

	ip = mtod(m, struct ip *);
	ip->ip_len = htons(m->m_pkthdr.len);
	ip->ip_p = IPPROTO_UDP;
	return (0);
}

void
udp_ipsec_adjust_cksum(struct mbuf *m, struct secasvar *sav, int proto,
    int skip)
{
	struct ip *ip;
	uint16_t cksum, off;

	IPSEC_ASSERT(sav->natt != NULL, ("NAT-T isn't required"));
	IPSEC_ASSERT(proto == IPPROTO_UDP || proto == IPPROTO_TCP,
	    ("unexpected protocol %u", proto));

	if (proto == IPPROTO_UDP)
		off = offsetof(struct udphdr, uh_sum);
	else
		off = offsetof(struct tcphdr, th_sum);

	if (V_natt_cksum_policy == 0) {	/* auto */
		if (sav->natt->cksum != 0) {
			/* Incrementally recompute. */
			m_copydata(m, skip + off, sizeof(cksum),
			    (caddr_t)&cksum);
			/* Do not adjust UDP checksum if it is zero. */
			if (proto == IPPROTO_UDP && cksum == 0)
				return;
			cksum = in_addword(cksum, sav->natt->cksum);
		} else {
			/* No OA from IKEd. */
			if (proto == IPPROTO_TCP) {
				/* Ignore for TCP. */
				m->m_pkthdr.csum_data = 0xffff;
				m->m_pkthdr.csum_flags |= (CSUM_DATA_VALID |
				    CSUM_PSEUDO_HDR);
				return;
			}
			cksum = 0; /* Reset for UDP. */
		}
		m_copyback(m, skip + off, sizeof(cksum), (caddr_t)&cksum);
	} else { /* Fully recompute */
		ip = mtod(m, struct ip *);
		cksum = in_pseudo(ip->ip_src.s_addr, ip->ip_dst.s_addr,
		    htons(m->m_pkthdr.len - skip + proto));
		m_copyback(m, skip + off, sizeof(cksum), (caddr_t)&cksum);
		m->m_pkthdr.csum_flags =
		    (proto == IPPROTO_UDP) ? CSUM_UDP: CSUM_TCP;
		m->m_pkthdr.csum_data = off;
		in_delayed_cksum(m);
		m->m_pkthdr.csum_flags &= ~CSUM_DELAY_DATA;
	}
}

