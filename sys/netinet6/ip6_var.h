/*	$OpenBSD: ip6_var.h,v 1.128 2025/09/16 09:19:16 florian Exp $	*/
/*	$KAME: ip6_var.h,v 1.33 2000/06/11 14:59:20 jinmei Exp $	*/

/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Copyright (c) 1982, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *
 *	@(#)ip_var.h	8.1 (Berkeley) 6/10/93
 */

#ifndef _NETINET6_IP6_VAR_H_
#define _NETINET6_IP6_VAR_H_

struct	ip6stat {
	u_int64_t ip6s_total;		/* total packets received */
	u_int64_t ip6s_tooshort;	/* packet too short */
	u_int64_t ip6s_toosmall;	/* not enough data */
	u_int64_t ip6s_fragments;	/* fragments received */
	u_int64_t ip6s_fragdropped;	/* frags dropped(dups, out of space) */
	u_int64_t ip6s_fragtimeout;	/* fragments timed out */
	u_int64_t ip6s_fragoverflow;	/* fragments that exceeded limit */
	u_int64_t ip6s_forward;		/* packets forwarded */
	u_int64_t ip6s_cantforward;	/* packets rcvd for unreachable dest */
	u_int64_t ip6s_redirectsent;	/* packets forwarded on same net */
	u_int64_t ip6s_delivered;	/* datagrams delivered to upper level*/
	u_int64_t ip6s_localout;	/* total ip packets generated here */
	u_int64_t ip6s_odropped;	/* lost output due to nobufs, etc. */
	u_int64_t ip6s_reassembled;	/* total packets reassembled ok */
	u_int64_t ip6s_fragmented;	/* datagrams successfully fragmented */
	u_int64_t ip6s_ofragments;	/* output fragments created */
	u_int64_t ip6s_cantfrag;	/* don't fragment flag was set, etc. */
	u_int64_t ip6s_badoptions;	/* error in option processing */
	u_int64_t ip6s_noroute;		/* packets discarded due to no route */
	u_int64_t ip6s_badvers;		/* ip6 version != 6 */
	u_int64_t ip6s_rawout;		/* total raw ip packets generated */
	u_int64_t ip6s_badscope;	/* scope error */
	u_int64_t ip6s_notmember;	/* don't join this multicast group */
	u_int64_t ip6s_nxthist[256];	/* next header history */
	u_int64_t ip6s_m1;		/* one mbuf */
	u_int64_t ip6s_m2m[32];		/* two or more mbuf */
	u_int64_t ip6s_mext1;		/* one ext mbuf */
	u_int64_t ip6s_mext2m;		/* two or more ext mbuf */
	u_int64_t ip6s_nogif;		/* no match gif found */
	u_int64_t ip6s_toomanyhdr;	/* discarded due to too many headers */

	/*
	 * statistics for improvement of the source address selection
	 * algorithm:
	 * XXX: hardcoded 16 = # of ip6 multicast scope types + 1
	 */
	/* number of times that address selection fails */
	u_int64_t ip6s_sources_none;
	/* number of times that an address on the outgoing I/F is chosen */
	u_int64_t ip6s_sources_sameif[16];
	/* number of times that an address on a non-outgoing I/F is chosen */
	u_int64_t ip6s_sources_otherif[16];
	/*
	 * number of times that an address that has the same scope
	 * from the destination is chosen.
	 */
	u_int64_t ip6s_sources_samescope[16];
	/*
	 * number of times that an address that has a different scope
	 * from the destination is chosen.
	 */
	u_int64_t ip6s_sources_otherscope[16];
	/* number of times that an deprecated address is chosen */
	u_int64_t ip6s_sources_deprecated[16];

	u_int64_t ip6s_rtcachehit;	/* valid route found in cache */
	u_int64_t ip6s_rtcachemiss;	/* route cache with new destination */
	u_int64_t ip6s_wrongif;		/* packet received on wrong interface */
	u_int64_t ip6s_idropped;	/* lost input due to nobufs, etc. */
};

#ifdef _KERNEL

/*
 * IP6 reassembly queue structure.  Each fragment
 * being reassembled is attached to one of these structures.
 */
struct	ip6q {
	TAILQ_ENTRY(ip6q) ip6q_queue;
	LIST_HEAD(ip6asfrag_list, ip6asfrag) ip6q_asfrag;
	struct in6_addr	ip6q_src, ip6q_dst;
	int		ip6q_unfrglen;	/* len of unfragmentable part */
	int		ip6q_nfrag;	/* # of fragments */
	u_int32_t	ip6q_ident;	/* fragment identification */
	u_int8_t	ip6q_nxt;	/* ip6f_nxt in first fragment */
	u_int8_t	ip6q_ecn;
	u_int8_t	ip6q_ttl;	/* time to live in slowtimo units */
};

struct	ip6asfrag {
	LIST_ENTRY(ip6asfrag) ip6af_list;
	struct mbuf	*ip6af_m;
	int		ip6af_offset;	/* offset in ip6af_m to next header */
	int		ip6af_frglen;	/* fragmentable part length */
	int		ip6af_off;	/* fragment offset */
	u_int16_t	ip6af_mff;	/* more fragment bit in frag off */
};

struct	ip6_moptions {
	LIST_HEAD(, in6_multi_mship) im6o_memberships;
	unsigned short im6o_ifidx;	/* ifp index for outgoing multicasts */
	u_char	im6o_hlim;	/* hoplimit for outgoing multicasts */
	u_char	im6o_loop;	/* 1 >= hear sends if a member */
};

/*
 * Control options for outgoing packets
 */

/* Routing header related info */
struct	ip6po_rhinfo {
	struct	ip6_rthdr *ip6po_rhi_rthdr; /* Routing header */
	struct	route ip6po_rhi_route; /* Route to the 1st hop */
};
#define ip6po_rthdr	ip6po_rhinfo.ip6po_rhi_rthdr
#define ip6po_route	ip6po_rhinfo.ip6po_rhi_route

struct	ip6_pktopts {
	/* Hoplimit for outgoing packets */
	int	ip6po_hlim;

	/* Outgoing IF/address information */
	struct in6_pktinfo *ip6po_pktinfo;

	/* Hop-by-Hop options header */
	struct	ip6_hbh *ip6po_hbh;

	/* Destination options header (before a routing header) */
	struct	ip6_dest *ip6po_dest1;

	/* Routing header related info. */
	struct	ip6po_rhinfo ip6po_rhinfo;

	/* Destination options header (after a routing header) */
	struct	ip6_dest *ip6po_dest2;

	/* traffic class */
	int	ip6po_tclass;

	/* fragment vs PMTU discovery policy */
	int	ip6po_minmtu;
#define IP6PO_MINMTU_MCASTONLY	-1 /* default: send at min MTU for multicast */
#define IP6PO_MINMTU_DISABLE	0  /* always perform pmtu disc */
#define IP6PO_MINMTU_ALL	1  /* always send at min MTU */

	int	ip6po_flags;
#define	IP6PO_DONTFRAG	0x04	/* disable fragmentation (IPV6_DONTFRAG) */
};

#include <sys/percpu.h>

enum ip6stat_counters {
	ip6s_total,
	ip6s_tooshort,
	ip6s_toosmall,
	ip6s_fragments,
	ip6s_fragdropped,
	ip6s_fragtimeout,
	ip6s_fragoverflow,
	ip6s_forward,
	ip6s_cantforward,
	ip6s_redirectsent,
	ip6s_delivered,
	ip6s_localout,
	ip6s_odropped,
	ip6s_reassembled,
	ip6s_fragmented,
	ip6s_ofragments,
	ip6s_cantfrag,
	ip6s_badoptions,
	ip6s_noroute,
	ip6s_badvers,
	ip6s_rawout,
	ip6s_badscope,
	ip6s_notmember,
	ip6s_nxthist,
	ip6s_m1 = ip6s_nxthist + 256,
	ip6s_m2m,
	ip6s_mext1 = ip6s_m2m + 32,
	ip6s_mext2m,
	ip6s_nogif,
	ip6s_toomanyhdr,
	ip6s_sources_none,
	ip6s_sources_sameif,
	ip6s_sources_otherif = ip6s_sources_sameif + 16,
	ip6s_sources_samescope = ip6s_sources_otherif + 16,
	ip6s_sources_otherscope = ip6s_sources_samescope + 16,
	ip6s_sources_deprecated = ip6s_sources_otherscope + 16,
	ip6s_rtcachehit = ip6s_sources_deprecated + 16,
	ip6s_rtcachemiss,
	ip6s_wrongif,
	ip6s_idropped,

	ip6s_ncounters,
};

extern struct cpumem *ip6counters;

static inline void
ip6stat_inc(enum ip6stat_counters c)
{
	counters_inc(ip6counters, c);
}

static inline void
ip6stat_add(enum ip6stat_counters c, uint64_t v)
{
	counters_add(ip6counters, c, v);
}

/* flags passed to ip6_output or ip6_forward as last parameter */
#define IPV6_UNSPECSRC		0x01	/* allow :: as the source address */
#define IPV6_FORWARDING		0x02	/* most of IPv6 header exists */
#define IPV6_MINMTU		0x04	/* use minimum MTU (IPV6_USE_MIN_MTU) */
#define IPV6_REDIRECT		0x08	/* redirected by pf */
#define IPV6_FORWARDING_IPSEC	0x10	/* only packets processed by IPsec */

extern int ip6_mtudisc_timeout;		/* mtu discovery */
extern struct rttimer_queue icmp6_mtudisc_timeout_q;

extern int	ip6_defhlim;		/* default hop limit */
extern int	ip6_defmcasthlim;	/* default multicast hop limit */
extern int	ip6_forwarding;		/* act as router? */
extern int	ip6_mforwarding;	/* act as multicast router? */
extern int	ip6_multipath;		/* use multipath routes */
extern int	ip6_sendredirect;	/* send ICMPv6 redirect? */
extern int	ip6_mcast_pmtu;		/* path MTU discovery for multicast */
extern int	ip6_neighborgcthresh; /* Threshold # of NDP entries for GC */
extern int	ip6_maxdynroutes; /* Max # of routes created via redirect */

extern struct socket *ip6_mrouter[RT_TABLEID_MAX + 1]; /* multicast routing daemon */
extern int	ip6_sendredirects;	/* send IP redirects when forwarding? */
extern int	ip6_maxfragpackets; /* Maximum packets in reassembly queue */
extern int	ip6_maxfrags;	/* Maximum fragments in reassembly queue */
extern int	ip6_hdrnestlimit; /* upper limit of # of extension headers */
extern int	ip6_dad_count;		/* DupAddrDetectionTransmits */
extern int	ip6_dad_pending;	/* number of currently running DADs */

extern const struct pr_usrreqs rip6_usrreqs;

struct inpcb;
struct ipsec_level;

int	icmp6_ctloutput(int, struct socket *, int, int, struct mbuf *);

void	ip6_init(void);
void	ip6intr(void);
int	ip6_input_if(struct mbuf **, int *, int, int, struct ifnet *,
	    struct netstack *);
int	ip6_ours_enqueue(struct mbuf **, int *, int);
void	ip6_freepcbopts(struct ip6_pktopts *);
void	ip6_freemoptions(struct ip6_moptions *);
int	ip6_unknown_opt(struct mbuf **, u_int8_t *, int);
int	ip6_get_prevhdr(struct mbuf *, int);
int	ip6_nexthdr(struct mbuf *, int, int, int *);
int	ip6_lasthdr(struct mbuf *, int, int, int *);
int	ip6_mforward(struct ip6_hdr *, struct ifnet *, struct mbuf *, int);
int	ip6_process_hopopts(struct mbuf **, u_int8_t *, int, u_int32_t *,
	     u_int32_t *);
void	ip6_savecontrol(struct inpcb *, struct mbuf *, struct mbuf **);
int	ip6_sysctl(int *, u_int, void *, size_t *, void *, size_t);

void	ip6_forward(struct mbuf *, struct route *, int);

void	ip6_mloopback(struct ifnet *, struct mbuf *, struct sockaddr_in6 *);
int	ip6_output(struct mbuf *, struct ip6_pktopts *, struct route *, int,
	    struct ip6_moptions *, const struct ipsec_level *);
int	ip6_fragment(struct mbuf *, struct mbuf_list *, int, u_char, u_long);
int	ip6_ctloutput(int, struct socket *, int, int, struct mbuf *);
int	ip6_raw_ctloutput(int, struct socket *, int, int, struct mbuf *);
void	ip6_initpktopts(struct ip6_pktopts *);
int	ip6_setpktopts(struct mbuf *, struct ip6_pktopts *,
	    struct ip6_pktopts *, int, int);
void	ip6_clearpktopts(struct ip6_pktopts *, int);
void	ip6_randomid_init(void);
u_int32_t ip6_randomid(void);
void	ip6_send(struct mbuf *);

int	route6_input(struct mbuf **, int *, int, int, struct netstack *);

void	frag6_init(void);
int	frag6_input(struct mbuf **, int *, int, int, struct netstack *);
int	frag6_deletefraghdr(struct mbuf *, int);
void	frag6_slowtimo(void);

void	rip6_init(void);
int	rip6_input(struct mbuf **, int *, int, int, struct netstack *);
void	rip6_ctlinput(int, struct sockaddr *, u_int, void *);
int	rip6_ctloutput(int, struct socket *, int, int, struct mbuf *);
int	rip6_output(struct mbuf *, struct socket *, struct sockaddr *,
	    struct mbuf *);
int	rip6_attach(struct socket *, int, int);
int	rip6_detach(struct socket *);
int	rip6_bind(struct socket *, struct mbuf *, struct proc *);
int	rip6_connect(struct socket *, struct mbuf *);
int	rip6_disconnect(struct socket *);
int	rip6_shutdown(struct socket *);
int	rip6_send(struct socket *, struct mbuf *, struct mbuf *,
	    struct mbuf *);
int	rip6_sysctl(int *, u_int, void *, size_t *, void *, size_t);

int	dest6_input(struct mbuf **, int *, int, int, struct netstack *);

int	in6_pcbselsrc(const struct in6_addr **, const struct sockaddr_in6 *,
	    struct inpcb *, struct ip6_pktopts *);
int	in6_selectsrc(const struct in6_addr **, const struct sockaddr_in6 *,
	    struct ip6_moptions *, unsigned int);
struct rtentry *in6_selectroute(const struct in6_addr *, struct ip6_pktopts *,
	    struct route *, unsigned int rtableid);

u_int32_t ip6_randomflowlabel(void);

#ifdef IPSEC
struct tdb;
int	ip6_output_ipsec_lookup(struct mbuf *, const struct ipsec_level *,
	    struct tdb **);
int	ip6_output_ipsec_send(struct tdb *, struct mbuf *, struct route *,
	    u_int, int, int);
#endif /* IPSEC */

#endif /* _KERNEL */

#endif /* !_NETINET6_IP6_VAR_H_ */
