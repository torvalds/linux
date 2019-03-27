/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1988, 1993
 *      The Regents of the University of California.
 * Copyright (c) 2005 Andre Oppermann, Internet Business Solutions AG.
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
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
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

#include "opt_ipstealth.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/kernel.h>
#include <sys/syslog.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <net/netisr.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/in_fib.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#include <netinet/ip_var.h>
#include <netinet/ip_options.h>
#include <netinet/ip_icmp.h>
#include <machine/in_cksum.h>

#include <sys/socketvar.h>

VNET_DEFINE_STATIC(int, ip_dosourceroute);
SYSCTL_INT(_net_inet_ip, IPCTL_SOURCEROUTE, sourceroute,
    CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(ip_dosourceroute), 0,
    "Enable forwarding source routed IP packets");
#define	V_ip_dosourceroute	VNET(ip_dosourceroute)

VNET_DEFINE_STATIC(int,	ip_acceptsourceroute);
SYSCTL_INT(_net_inet_ip, IPCTL_ACCEPTSOURCEROUTE, accept_sourceroute, 
    CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(ip_acceptsourceroute), 0, 
    "Enable accepting source routed IP packets");
#define	V_ip_acceptsourceroute	VNET(ip_acceptsourceroute)

VNET_DEFINE(int, ip_doopts) = 1; /* 0 = ignore, 1 = process, 2 = reject */
SYSCTL_INT(_net_inet_ip, OID_AUTO, process_options, CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(ip_doopts), 0, "Enable IP options processing ([LS]SRR, RR, TS)");

static void	save_rte(struct mbuf *m, u_char *, struct in_addr);

/*
 * Do option processing on a datagram, possibly discarding it if bad options
 * are encountered, or forwarding it if source-routed.
 *
 * The pass argument is used when operating in the IPSTEALTH mode to tell
 * what options to process: [LS]SRR (pass 0) or the others (pass 1).  The
 * reason for as many as two passes is that when doing IPSTEALTH, non-routing
 * options should be processed only if the packet is for us.
 *
 * Returns 1 if packet has been forwarded/freed, 0 if the packet should be
 * processed further.
 */
int
ip_dooptions(struct mbuf *m, int pass)
{
	struct ip *ip = mtod(m, struct ip *);
	u_char *cp;
	struct in_ifaddr *ia;
	int opt, optlen, cnt, off, code, type = ICMP_PARAMPROB, forward = 0;
	struct in_addr *sin, dst;
	uint32_t ntime;
	struct nhop4_extended nh_ext;
	struct	sockaddr_in ipaddr = { sizeof(ipaddr), AF_INET };
	struct epoch_tracker et;

	/* Ignore or reject packets with IP options. */
	if (V_ip_doopts == 0)
		return 0;
	else if (V_ip_doopts == 2) {
		type = ICMP_UNREACH;
		code = ICMP_UNREACH_FILTER_PROHIB;
		goto bad_unlocked;
	}

	NET_EPOCH_ENTER(et);
	dst = ip->ip_dst;
	cp = (u_char *)(ip + 1);
	cnt = (ip->ip_hl << 2) - sizeof (struct ip);
	for (; cnt > 0; cnt -= optlen, cp += optlen) {
		opt = cp[IPOPT_OPTVAL];
		if (opt == IPOPT_EOL)
			break;
		if (opt == IPOPT_NOP)
			optlen = 1;
		else {
			if (cnt < IPOPT_OLEN + sizeof(*cp)) {
				code = &cp[IPOPT_OLEN] - (u_char *)ip;
				goto bad;
			}
			optlen = cp[IPOPT_OLEN];
			if (optlen < IPOPT_OLEN + sizeof(*cp) || optlen > cnt) {
				code = &cp[IPOPT_OLEN] - (u_char *)ip;
				goto bad;
			}
		}
		switch (opt) {

		default:
			break;

		/*
		 * Source routing with record.  Find interface with current
		 * destination address.  If none on this machine then drop if
		 * strictly routed, or do nothing if loosely routed.  Record
		 * interface address and bring up next address component.  If
		 * strictly routed make sure next address is on directly
		 * accessible net.
		 */
		case IPOPT_LSRR:
		case IPOPT_SSRR:
#ifdef IPSTEALTH
			if (V_ipstealth && pass > 0)
				break;
#endif
			if (optlen < IPOPT_OFFSET + sizeof(*cp)) {
				code = &cp[IPOPT_OLEN] - (u_char *)ip;
				goto bad;
			}
			if ((off = cp[IPOPT_OFFSET]) < IPOPT_MINOFF) {
				code = &cp[IPOPT_OFFSET] - (u_char *)ip;
				goto bad;
			}
			ipaddr.sin_addr = ip->ip_dst;
			if (ifa_ifwithaddr_check((struct sockaddr *)&ipaddr)
			    == 0) {
				if (opt == IPOPT_SSRR) {
					type = ICMP_UNREACH;
					code = ICMP_UNREACH_SRCFAIL;
					goto bad;
				}
				if (!V_ip_dosourceroute)
					goto nosourcerouting;
				/*
				 * Loose routing, and not at next destination
				 * yet; nothing to do except forward.
				 */
				break;
			}
			off--;			/* 0 origin */
			if (off > optlen - (int)sizeof(struct in_addr)) {
				/*
				 * End of source route.  Should be for us.
				 */
				if (!V_ip_acceptsourceroute)
					goto nosourcerouting;
				save_rte(m, cp, ip->ip_src);
				break;
			}
#ifdef IPSTEALTH
			if (V_ipstealth)
				goto dropit;
#endif
			if (!V_ip_dosourceroute) {
				if (V_ipforwarding) {
					char srcbuf[INET_ADDRSTRLEN];
					char dstbuf[INET_ADDRSTRLEN];

					/*
					 * Acting as a router, so generate
					 * ICMP
					 */
nosourcerouting:
					log(LOG_WARNING, 
					    "attempted source route from %s "
					    "to %s\n",
					    inet_ntoa_r(ip->ip_src, srcbuf),
					    inet_ntoa_r(ip->ip_dst, dstbuf));
					type = ICMP_UNREACH;
					code = ICMP_UNREACH_SRCFAIL;
					goto bad;
				} else {
					/*
					 * Not acting as a router, so
					 * silently drop.
					 */
#ifdef IPSTEALTH
dropit:
#endif
					IPSTAT_INC(ips_cantforward);
					m_freem(m);
					NET_EPOCH_EXIT(et);
					return (1);
				}
			}

			/*
			 * locate outgoing interface
			 */
			(void)memcpy(&ipaddr.sin_addr, cp + off,
			    sizeof(ipaddr.sin_addr));

			type = ICMP_UNREACH;
			code = ICMP_UNREACH_SRCFAIL;

			if (opt == IPOPT_SSRR) {
#define	INA	struct in_ifaddr *
#define	SA	struct sockaddr *
			    ia = (INA)ifa_ifwithdstaddr((SA)&ipaddr,
					    RT_ALL_FIBS);
			    if (ia == NULL)
				    ia = (INA)ifa_ifwithnet((SA)&ipaddr, 0,
						    RT_ALL_FIBS);
				if (ia == NULL)
					goto bad;

				memcpy(cp + off, &(IA_SIN(ia)->sin_addr),
				    sizeof(struct in_addr));
			} else {
				/* XXX MRT 0 for routing */
				if (fib4_lookup_nh_ext(M_GETFIB(m),
				    ipaddr.sin_addr, 0, 0, &nh_ext) != 0)
					goto bad;

				memcpy(cp + off, &nh_ext.nh_src,
				    sizeof(struct in_addr));
			}

			ip->ip_dst = ipaddr.sin_addr;
			cp[IPOPT_OFFSET] += sizeof(struct in_addr);
			/*
			 * Let ip_intr's mcast routing check handle mcast pkts
			 */
			forward = !IN_MULTICAST(ntohl(ip->ip_dst.s_addr));
			break;

		case IPOPT_RR:
#ifdef IPSTEALTH
			if (V_ipstealth && pass == 0)
				break;
#endif
			if (optlen < IPOPT_OFFSET + sizeof(*cp)) {
				code = &cp[IPOPT_OFFSET] - (u_char *)ip;
				goto bad;
			}
			if ((off = cp[IPOPT_OFFSET]) < IPOPT_MINOFF) {
				code = &cp[IPOPT_OFFSET] - (u_char *)ip;
				goto bad;
			}
			/*
			 * If no space remains, ignore.
			 */
			off--;			/* 0 origin */
			if (off > optlen - (int)sizeof(struct in_addr))
				break;
			(void)memcpy(&ipaddr.sin_addr, &ip->ip_dst,
			    sizeof(ipaddr.sin_addr));
			/*
			 * Locate outgoing interface; if we're the
			 * destination, use the incoming interface (should be
			 * same).
			 */
			if ((ia = (INA)ifa_ifwithaddr((SA)&ipaddr)) != NULL) {
				memcpy(cp + off, &(IA_SIN(ia)->sin_addr),
				    sizeof(struct in_addr));
			} else if (fib4_lookup_nh_ext(M_GETFIB(m),
			    ipaddr.sin_addr, 0, 0, &nh_ext) == 0) {
				memcpy(cp + off, &nh_ext.nh_src,
				    sizeof(struct in_addr));
			} else {
				type = ICMP_UNREACH;
				code = ICMP_UNREACH_HOST;
				goto bad;
			}
			cp[IPOPT_OFFSET] += sizeof(struct in_addr);
			break;

		case IPOPT_TS:
#ifdef IPSTEALTH
			if (V_ipstealth && pass == 0)
				break;
#endif
			code = cp - (u_char *)ip;
			if (optlen < 4 || optlen > 40) {
				code = &cp[IPOPT_OLEN] - (u_char *)ip;
				goto bad;
			}
			if ((off = cp[IPOPT_OFFSET]) < 5) {
				code = &cp[IPOPT_OLEN] - (u_char *)ip;
				goto bad;
			}
			if (off > optlen - (int)sizeof(int32_t)) {
				cp[IPOPT_OFFSET + 1] += (1 << 4);
				if ((cp[IPOPT_OFFSET + 1] & 0xf0) == 0) {
					code = &cp[IPOPT_OFFSET] - (u_char *)ip;
					goto bad;
				}
				break;
			}
			off--;				/* 0 origin */
			sin = (struct in_addr *)(cp + off);
			switch (cp[IPOPT_OFFSET + 1] & 0x0f) {

			case IPOPT_TS_TSONLY:
				break;

			case IPOPT_TS_TSANDADDR:
				if (off + sizeof(uint32_t) +
				    sizeof(struct in_addr) > optlen) {
					code = &cp[IPOPT_OFFSET] - (u_char *)ip;
					goto bad;
				}
				ipaddr.sin_addr = dst;
				ia = (INA)ifaof_ifpforaddr((SA)&ipaddr,
							    m->m_pkthdr.rcvif);
				if (ia == NULL)
					continue;
				(void)memcpy(sin, &IA_SIN(ia)->sin_addr,
				    sizeof(struct in_addr));
				cp[IPOPT_OFFSET] += sizeof(struct in_addr);
				off += sizeof(struct in_addr);
				break;

			case IPOPT_TS_PRESPEC:
				if (off + sizeof(uint32_t) +
				    sizeof(struct in_addr) > optlen) {
					code = &cp[IPOPT_OFFSET] - (u_char *)ip;
					goto bad;
				}
				(void)memcpy(&ipaddr.sin_addr, sin,
				    sizeof(struct in_addr));
				if (ifa_ifwithaddr_check((SA)&ipaddr) == 0)
					continue;
				cp[IPOPT_OFFSET] += sizeof(struct in_addr);
				off += sizeof(struct in_addr);
				break;

			default:
				code = &cp[IPOPT_OFFSET + 1] - (u_char *)ip;
				goto bad;
			}
			ntime = iptime();
			(void)memcpy(cp + off, &ntime, sizeof(uint32_t));
			cp[IPOPT_OFFSET] += sizeof(uint32_t);
		}
	}
	NET_EPOCH_EXIT(et);
	if (forward && V_ipforwarding) {
		ip_forward(m, 1);
		return (1);
	}
	return (0);
bad:
	NET_EPOCH_EXIT(et);
bad_unlocked:
	icmp_error(m, type, code, 0, 0);
	IPSTAT_INC(ips_badoptions);
	return (1);
}

/*
 * Save incoming source route for use in replies, to be picked up later by
 * ip_srcroute if the receiver is interested.
 */
static void
save_rte(struct mbuf *m, u_char *option, struct in_addr dst)
{
	unsigned olen;
	struct ipopt_tag *opts;

	opts = (struct ipopt_tag *)m_tag_get(PACKET_TAG_IPOPTIONS,
	    sizeof(struct ipopt_tag), M_NOWAIT);
	if (opts == NULL)
		return;

	olen = option[IPOPT_OLEN];
	if (olen > sizeof(opts->ip_srcrt) - (1 + sizeof(dst))) {
		m_tag_free((struct m_tag *)opts);
		return;
	}
	bcopy(option, opts->ip_srcrt.srcopt, olen);
	opts->ip_nhops = (olen - IPOPT_OFFSET - 1) / sizeof(struct in_addr);
	opts->ip_srcrt.dst = dst;
	m_tag_prepend(m, (struct m_tag *)opts);
}

/*
 * Retrieve incoming source route for use in replies, in the same form used
 * by setsockopt.  The first hop is placed before the options, will be
 * removed later.
 */
struct mbuf *
ip_srcroute(struct mbuf *m0)
{
	struct in_addr *p, *q;
	struct mbuf *m;
	struct ipopt_tag *opts;

	opts = (struct ipopt_tag *)m_tag_find(m0, PACKET_TAG_IPOPTIONS, NULL);
	if (opts == NULL)
		return (NULL);

	if (opts->ip_nhops == 0)
		return (NULL);
	m = m_get(M_NOWAIT, MT_DATA);
	if (m == NULL)
		return (NULL);

#define OPTSIZ	(sizeof(opts->ip_srcrt.nop) + sizeof(opts->ip_srcrt.srcopt))

	/* length is (nhops+1)*sizeof(addr) + sizeof(nop + srcrt header) */
	m->m_len = opts->ip_nhops * sizeof(struct in_addr) +
	    sizeof(struct in_addr) + OPTSIZ;

	/*
	 * First, save first hop for return route.
	 */
	p = &(opts->ip_srcrt.route[opts->ip_nhops - 1]);
	*(mtod(m, struct in_addr *)) = *p--;

	/*
	 * Copy option fields and padding (nop) to mbuf.
	 */
	opts->ip_srcrt.nop = IPOPT_NOP;
	opts->ip_srcrt.srcopt[IPOPT_OFFSET] = IPOPT_MINOFF;
	(void)memcpy(mtod(m, caddr_t) + sizeof(struct in_addr),
	    &(opts->ip_srcrt.nop), OPTSIZ);
	q = (struct in_addr *)(mtod(m, caddr_t) +
	    sizeof(struct in_addr) + OPTSIZ);
#undef OPTSIZ
	/*
	 * Record return path as an IP source route, reversing the path
	 * (pointers are now aligned).
	 */
	while (p >= opts->ip_srcrt.route) {
		*q++ = *p--;
	}
	/*
	 * Last hop goes to final destination.
	 */
	*q = opts->ip_srcrt.dst;
	m_tag_delete(m0, (struct m_tag *)opts);
	return (m);
}

/*
 * Strip out IP options, at higher level protocol in the kernel.
 */
void
ip_stripoptions(struct mbuf *m)
{
	struct ip *ip = mtod(m, struct ip *);
	int olen;

	olen = (ip->ip_hl << 2) - sizeof(struct ip);
	m->m_len -= olen;
	if (m->m_flags & M_PKTHDR)
		m->m_pkthdr.len -= olen;
	ip->ip_len = htons(ntohs(ip->ip_len) - olen);
	ip->ip_hl = sizeof(struct ip) >> 2;

	bcopy((char *)ip + sizeof(struct ip) + olen, (ip + 1),
	    (size_t )(m->m_len - sizeof(struct ip)));
}

/*
 * Insert IP options into preformed packet.  Adjust IP destination as
 * required for IP source routing, as indicated by a non-zero in_addr at the
 * start of the options.
 *
 * XXX This routine assumes that the packet has no options in place.
 */
struct mbuf *
ip_insertoptions(struct mbuf *m, struct mbuf *opt, int *phlen)
{
	struct ipoption *p = mtod(opt, struct ipoption *);
	struct mbuf *n;
	struct ip *ip = mtod(m, struct ip *);
	unsigned optlen;

	optlen = opt->m_len - sizeof(p->ipopt_dst);
	if (optlen + ntohs(ip->ip_len) > IP_MAXPACKET) {
		*phlen = 0;
		return (m);		/* XXX should fail */
	}
	if (p->ipopt_dst.s_addr)
		ip->ip_dst = p->ipopt_dst;
	if (!M_WRITABLE(m) || M_LEADINGSPACE(m) < optlen) {
		n = m_gethdr(M_NOWAIT, MT_DATA);
		if (n == NULL) {
			*phlen = 0;
			return (m);
		}
		m_move_pkthdr(n, m);
		n->m_pkthdr.rcvif = NULL;
		n->m_pkthdr.len += optlen;
		m->m_len -= sizeof(struct ip);
		m->m_data += sizeof(struct ip);
		n->m_next = m;
		m = n;
		m->m_len = optlen + sizeof(struct ip);
		m->m_data += max_linkhdr;
		bcopy(ip, mtod(m, void *), sizeof(struct ip));
	} else {
		m->m_data -= optlen;
		m->m_len += optlen;
		m->m_pkthdr.len += optlen;
		bcopy(ip, mtod(m, void *), sizeof(struct ip));
	}
	ip = mtod(m, struct ip *);
	bcopy(p->ipopt_list, ip + 1, optlen);
	*phlen = sizeof(struct ip) + optlen;
	ip->ip_v = IPVERSION;
	ip->ip_hl = *phlen >> 2;
	ip->ip_len = htons(ntohs(ip->ip_len) + optlen);
	return (m);
}

/*
 * Copy options from ip to jp, omitting those not copied during
 * fragmentation.
 */
int
ip_optcopy(struct ip *ip, struct ip *jp)
{
	u_char *cp, *dp;
	int opt, optlen, cnt;

	cp = (u_char *)(ip + 1);
	dp = (u_char *)(jp + 1);
	cnt = (ip->ip_hl << 2) - sizeof (struct ip);
	for (; cnt > 0; cnt -= optlen, cp += optlen) {
		opt = cp[0];
		if (opt == IPOPT_EOL)
			break;
		if (opt == IPOPT_NOP) {
			/* Preserve for IP mcast tunnel's LSRR alignment. */
			*dp++ = IPOPT_NOP;
			optlen = 1;
			continue;
		}

		KASSERT(cnt >= IPOPT_OLEN + sizeof(*cp),
		    ("ip_optcopy: malformed ipv4 option"));
		optlen = cp[IPOPT_OLEN];
		KASSERT(optlen >= IPOPT_OLEN + sizeof(*cp) && optlen <= cnt,
		    ("ip_optcopy: malformed ipv4 option"));

		/* Bogus lengths should have been caught by ip_dooptions. */
		if (optlen > cnt)
			optlen = cnt;
		if (IPOPT_COPIED(opt)) {
			bcopy(cp, dp, optlen);
			dp += optlen;
		}
	}
	for (optlen = dp - (u_char *)(jp+1); optlen & 0x3; optlen++)
		*dp++ = IPOPT_EOL;
	return (optlen);
}

/*
 * Set up IP options in pcb for insertion in output packets.  Store in mbuf
 * with pointer in pcbopt, adding pseudo-option with destination address if
 * source routed.
 */
int
ip_pcbopts(struct inpcb *inp, int optname, struct mbuf *m)
{
	int cnt, optlen;
	u_char *cp;
	struct mbuf **pcbopt;
	u_char opt;

	INP_WLOCK_ASSERT(inp);

	pcbopt = &inp->inp_options;

	/* turn off any old options */
	if (*pcbopt)
		(void)m_free(*pcbopt);
	*pcbopt = NULL;
	if (m == NULL || m->m_len == 0) {
		/*
		 * Only turning off any previous options.
		 */
		if (m != NULL)
			(void)m_free(m);
		return (0);
	}

	if (m->m_len % sizeof(int32_t))
		goto bad;
	/*
	 * IP first-hop destination address will be stored before actual
	 * options; move other options back and clear it when none present.
	 */
	if (m->m_data + m->m_len + sizeof(struct in_addr) >= &m->m_dat[MLEN])
		goto bad;
	cnt = m->m_len;
	m->m_len += sizeof(struct in_addr);
	cp = mtod(m, u_char *) + sizeof(struct in_addr);
	bcopy(mtod(m, void *), cp, (unsigned)cnt);
	bzero(mtod(m, void *), sizeof(struct in_addr));

	for (; cnt > 0; cnt -= optlen, cp += optlen) {
		opt = cp[IPOPT_OPTVAL];
		if (opt == IPOPT_EOL)
			break;
		if (opt == IPOPT_NOP)
			optlen = 1;
		else {
			if (cnt < IPOPT_OLEN + sizeof(*cp))
				goto bad;
			optlen = cp[IPOPT_OLEN];
			if (optlen < IPOPT_OLEN + sizeof(*cp) || optlen > cnt)
				goto bad;
		}
		switch (opt) {

		default:
			break;

		case IPOPT_LSRR:
		case IPOPT_SSRR:
			/*
			 * User process specifies route as:
			 *
			 *	->A->B->C->D
			 *
			 * D must be our final destination (but we can't
			 * check that since we may not have connected yet).
			 * A is first hop destination, which doesn't appear
			 * in actual IP option, but is stored before the
			 * options.
			 */
			/* XXX-BZ PRIV_NETINET_SETHDROPTS? */
			if (optlen < IPOPT_MINOFF - 1 + sizeof(struct in_addr))
				goto bad;
			m->m_len -= sizeof(struct in_addr);
			cnt -= sizeof(struct in_addr);
			optlen -= sizeof(struct in_addr);
			cp[IPOPT_OLEN] = optlen;
			/*
			 * Move first hop before start of options.
			 */
			bcopy((caddr_t)&cp[IPOPT_OFFSET+1], mtod(m, caddr_t),
			    sizeof(struct in_addr));
			/*
			 * Then copy rest of options back
			 * to close up the deleted entry.
			 */
			bcopy((&cp[IPOPT_OFFSET+1] + sizeof(struct in_addr)),
			    &cp[IPOPT_OFFSET+1],
			    (unsigned)cnt - (IPOPT_MINOFF - 1));
			break;
		}
	}
	if (m->m_len > MAX_IPOPTLEN + sizeof(struct in_addr))
		goto bad;
	*pcbopt = m;
	return (0);

bad:
	(void)m_free(m);
	return (EINVAL);
}

/*
 * Check for the presence of the IP Router Alert option [RFC2113]
 * in the header of an IPv4 datagram.
 *
 * This call is not intended for use from the forwarding path; it is here
 * so that protocol domains may check for the presence of the option.
 * Given how FreeBSD's IPv4 stack is currently structured, the Router Alert
 * option does not have much relevance to the implementation, though this
 * may change in future.
 * Router alert options SHOULD be passed if running in IPSTEALTH mode and
 * we are not the endpoint.
 * Length checks on individual options should already have been performed
 * by ip_dooptions() therefore they are folded under INVARIANTS here.
 *
 * Return zero if not present or options are invalid, non-zero if present.
 */
int
ip_checkrouteralert(struct mbuf *m)
{
	struct ip *ip = mtod(m, struct ip *);
	u_char *cp;
	int opt, optlen, cnt, found_ra;

	found_ra = 0;
	cp = (u_char *)(ip + 1);
	cnt = (ip->ip_hl << 2) - sizeof (struct ip);
	for (; cnt > 0; cnt -= optlen, cp += optlen) {
		opt = cp[IPOPT_OPTVAL];
		if (opt == IPOPT_EOL)
			break;
		if (opt == IPOPT_NOP)
			optlen = 1;
		else {
#ifdef INVARIANTS
			if (cnt < IPOPT_OLEN + sizeof(*cp))
				break;
#endif
			optlen = cp[IPOPT_OLEN];
#ifdef INVARIANTS
			if (optlen < IPOPT_OLEN + sizeof(*cp) || optlen > cnt)
				break;
#endif
		}
		switch (opt) {
		case IPOPT_RA:
#ifdef INVARIANTS
			if (optlen != IPOPT_OFFSET + sizeof(uint16_t) ||
			    (*((uint16_t *)&cp[IPOPT_OFFSET]) != 0))
			    break;
			else
#endif
			found_ra = 1;
			break;
		default:
			break;
		}
	}

	return (found_ra);
}
