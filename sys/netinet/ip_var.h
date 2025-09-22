/*	$OpenBSD: ip_var.h,v 1.123 2025/07/31 09:05:11 mvs Exp $	*/
/*	$NetBSD: ip_var.h,v 1.16 1996/02/13 23:43:20 christos Exp $	*/

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

#ifndef _NETINET_IP_VAR_H_
#define _NETINET_IP_VAR_H_

/*
 * Structure stored in mbuf in inpcb.ip_options
 * and passed to ip_output when ip options are in use.
 * The actual length of the options (including ipopt_dst)
 * is in m_len.
 */
#define	MAX_IPOPTLEN	40

/*
 * Overlay for ip header used by other protocols (tcp, udp).
 */
struct ipovly {
	u_int8_t  ih_x1[9];		/* (unused) */
	u_int8_t  ih_pr;		/* protocol */
	u_int16_t ih_len;		/* protocol length */
	struct	  in_addr ih_src;	/* source internet address */
	struct	  in_addr ih_dst;	/* destination internet address */
};

struct	ipstat {
	u_long	ips_total;		/* total packets received */
	u_long	ips_badsum;		/* checksum bad */
	u_long	ips_tooshort;		/* packet too short */
	u_long	ips_toosmall;		/* not enough data */
	u_long	ips_badhlen;		/* ip header length < data size */
	u_long	ips_badlen;		/* ip length < ip header length */
	u_long	ips_fragments;		/* fragments received */
	u_long	ips_fragdropped;	/* frags dropped (dups, out of space) */
	u_long	ips_fragtimeout;	/* fragments timed out */
	u_long	ips_forward;		/* packets forwarded */
	u_long	ips_cantforward;	/* packets rcvd for unreachable dest */
	u_long	ips_redirectsent;	/* packets forwarded on same net */
	u_long	ips_noproto;		/* unknown or unsupported protocol */
	u_long	ips_delivered;		/* datagrams delivered to upper level*/
	u_long	ips_localout;		/* total ip packets generated here */
	u_long	ips_odropped;		/* lost output due to nobufs, etc. */
	u_long	ips_reassembled;	/* total packets reassembled ok */
	u_long	ips_fragmented;		/* datagrams successfully fragmented */
	u_long	ips_ofragments;		/* output fragments created */
	u_long	ips_cantfrag;		/* don't fragment flag was set, etc. */
	u_long	ips_badoptions;		/* error in option processing */
	u_long	ips_noroute;		/* packets discarded due to no route */
	u_long	ips_badvers;		/* ip version != 4 */
	u_long	ips_rawout;		/* total raw ip packets generated */
	u_long	ips_badfrags;		/* malformed fragments (bad length) */
	u_long	ips_rcvmemdrop;		/* frags dropped for lack of memory */
	u_long	ips_toolong;		/* ip length > max ip packet size */
	u_long	ips_nogif;		/* no match gif found */
	u_long	ips_badaddr;		/* invalid address on header */
	u_long	ips_inswcsum;		/* software checksummed on input */
	u_long	ips_outswcsum;		/* software checksummed on output */
	u_long	ips_notmember;		/* multicasts for unregistered groups */
	u_long	ips_rtcachehit;		/* valid route found in cache */
	u_long	ips_rtcachemiss;	/* route cache with new destination */
	u_long	ips_wrongif;		/* packet received on wrong interface */
	u_long	ips_idropped;		/* lost input due to nobufs, etc. */
};

struct ipoption {
	struct	in_addr ipopt_dst;	/* first-hop dst if source routed */
	int8_t	ipopt_list[MAX_IPOPTLEN];	/* options proper */
};

#ifdef _KERNEL

#include <sys/percpu.h>

enum ipstat_counters {
	ips_total,		/* total packets received */
	ips_badsum,		/* checksum bad */
	ips_tooshort,		/* packet too short */
	ips_toosmall,		/* not enough data */
	ips_badhlen,		/* ip header length < data size */
	ips_badlen,		/* ip length < ip header length */
	ips_fragments,		/* fragments received */
	ips_fragdropped,	/* frags dropped (dups, out of space) */
	ips_fragtimeout,	/* fragments timed out */
	ips_forward,		/* packets forwarded */
	ips_cantforward,	/* packets rcvd for unreachable dest */
	ips_redirectsent,	/* packets forwarded on same net */
	ips_noproto,		/* unknown or unsupported protocol */
	ips_delivered,		/* datagrams delivered to upper level*/
	ips_localout,		/* total ip packets generated here */
	ips_odropped,		/* lost output packets due to nobufs, etc. */
	ips_reassembled,	/* total packets reassembled ok */
	ips_fragmented,		/* datagrams successfully fragmented */
	ips_ofragments,		/* output fragments created */
	ips_cantfrag,		/* don't fragment flag was set, etc. */
	ips_badoptions,		/* error in option processing */
	ips_noroute,		/* packets discarded due to no route */
	ips_badvers,		/* ip version != 4 */
	ips_rawout,		/* total raw ip packets generated */
	ips_badfrags,		/* malformed fragments (bad length) */
	ips_rcvmemdrop,		/* frags dropped for lack of memory */
	ips_toolong,		/* ip length > max ip packet size */
	ips_nogif,		/* no match gif found */
	ips_badaddr,		/* invalid address on header */
	ips_inswcsum,		/* software checksummed on input */
	ips_outswcsum,		/* software checksummed on output */
	ips_notmember,		/* multicasts for unregistered groups */
	ips_rtcachehit,		/* valid route to destination found in cache */
	ips_rtcachemiss,	/* route cache filled with new destination */
	ips_wrongif,		/* packet received on wrong interface */
	ips_idropped,		/* lost input packets due to nobufs, etc. */

	ips_ncounters
};

extern struct cpumem *ipcounters;

static inline void
ipstat_inc(enum ipstat_counters c)
{
	counters_inc(ipcounters, c);
}

static inline void
ipstat_add(enum ipstat_counters c, uint64_t v)
{
	counters_add(ipcounters, c, v);
}

/*
 * Structure attached to inpcb.ip_moptions and
 * passed to ip_output when IP multicast options are in use.
 */
struct ip_moptions {
	struct in_multi **imo_membership; /* group memberships */
	unsigned short imo_ifidx;	/* ifp index for outgoing multicasts */
	u_int8_t  imo_ttl;	/* TTL for outgoing multicasts */
	u_int8_t  imo_loop;	/* 1 => hear sends if a member */
	u_int16_t imo_num_memberships;	/* no. memberships this socket */
	u_int16_t imo_max_memberships;	/* max memberships this socket */
};

#include <sys/queue.h>

/*
 * Ip reassembly queue structures.
 */
LIST_HEAD(ipqehead, ipqent);
struct ipqent {
	LIST_ENTRY(ipqent) ipqe_q;
	struct ip	*ipqe_ip;
	struct mbuf	*ipqe_m;	/* mbuf contains packet */
	uint16_t	 ipqe_mff;	/* for IP fragmentation */
};

/*
 * Ip reassembly queue structure.  Each fragment
 * being reassembled is attached to one of these structures.
 * They are timed out after ipq_ttl drops to 0, and may also
 * be reclaimed if memory becomes tight.
 */
struct ipq {
	LIST_ENTRY(ipq) ipq_q;		/* to other reass headers */
	u_int8_t  ipq_ttl;		/* time for reass q to live */
	u_int8_t  ipq_p;		/* protocol of this fragment */
	u_int16_t ipq_id;		/* sequence id for reassembly */
	struct	  ipqehead ipq_fragq;	/* to ip fragment queue */
	struct	  in_addr ipq_src, ipq_dst;
};

struct ipoffnxt {
	int	ion_off;
	int	ion_nxt;
};

/* flags passed to ip_output */
#define IP_FORWARDING		0x0001	/* most of ip header exists */
#define IP_RAWOUTPUT		0x0002	/* raw ip header exists */
#define IP_REDIRECT		0x0004	/* redirected by pf or source route */
#define IP_FORWARDING_IPSEC	0x0008	/* only packets processed by IPsec */
#define IP_ALLOWBROADCAST	SO_BROADCAST	/* can send broadcast packets */
#define IP_MTUDISC		0x0800	/* pmtu discovery, set DF */

extern struct ipstat ipstat;
extern int ip_defttl;			/* default IP ttl */

#define IPMTUDISCTIMEOUT (10 * 60)	/* as per RFC 1191 */

extern int ip_mtudisc;			/* mtu discovery */
extern int ip_mtudisc_timeout;		/* seconds to timeout mtu discovery */

extern int ipport_firstauto;		/* min port for port allocation */
extern int ipport_lastauto;		/* max port for port allocation */
extern int ipport_hifirstauto;		/* min dynamic/private port number */
extern int ipport_hilastauto;		/* max dynamic/private port number */
extern int ip_forwarding;		/* enable IP forwarding */
#ifdef MROUTING
extern int ipmforwarding;		/* enable multicast forwarding */
#endif
extern int ipmultipath;			/* enable multipath routing */
extern int ip_directedbcast;		/* accept all broadcast packets */
extern unsigned int la_hold_total;

extern const struct pr_usrreqs rip_usrreqs;

extern struct rttimer_queue ip_mtudisc_timeout_q;
extern struct pool ipqent_pool;
struct rtentry;
struct route;
struct inpcb;
struct ipsec_level;

int	 ip_ctloutput(int, struct socket *, int, int, struct mbuf *);
int	 ip_fragment(struct mbuf *, struct mbuf_list *, struct ifnet *, u_long);
void	 ip_freemoptions(struct ip_moptions *);
int	 ip_getmoptions(int, struct ip_moptions *, struct mbuf *);
void	 ip_init(void);
struct mbuf*
	 ip_insertoptions(struct mbuf *, struct mbuf *, int *);
int	 ip_mforward(struct mbuf *, struct ifnet *, int);
int	 ip_optcopy(struct ip *, struct ip *);
int	 ip_output(struct mbuf *, struct mbuf *, struct route *, int,
	    struct ip_moptions *, const struct ipsec_level *, u_int32_t);
u_int16_t
	 ip_randomid(void);
void	 ip_send(struct mbuf *);
void	 ip_send_raw(struct mbuf *);
void	 ip_slowtimo(void);
struct mbuf *
	 ip_srcroute(struct mbuf *);
void	 ip_stripoptions(struct mbuf *);
int	 ip_sysctl(int *, u_int, void *, size_t *, void *, size_t);
void	 ip_savecontrol(struct inpcb *, struct mbuf **, struct ip *,
	    struct mbuf *);
int	 ip_input_if(struct mbuf **, int *, int, int, struct ifnet *,
	    struct netstack *);
int	 ip_deliver(struct mbuf **, int *, int, int, int, struct netstack *);
void	 ip_forward(struct mbuf *, struct ifnet *, struct route *, int);
int	 rip_ctloutput(int, struct socket *, int, int, struct mbuf *);
void	 rip_init(void);
int	 rip_input(struct mbuf **, int *, int, int, struct netstack *);
int	 rip_output(struct mbuf *, struct socket *, struct sockaddr *,
	    struct mbuf *);
struct mbuf *
	 rip_chkhdr(struct mbuf *, struct mbuf *);
int	 rip_attach(struct socket *, int, int);
int	 rip_detach(struct socket *);
int	 rip_bind(struct socket *, struct mbuf *, struct proc *);
int	 rip_connect(struct socket *, struct mbuf *);
int	 rip_disconnect(struct socket *);
int	 rip_shutdown(struct socket *);
int	 rip_send(struct socket *, struct mbuf *, struct mbuf *,
	     struct mbuf *);
#ifdef MROUTING
extern struct socket *ip_mrouter[];	/* multicast routing daemon */
#endif

#endif /* _KERNEL */
#endif /* _NETINET_IP_VAR_H_ */
