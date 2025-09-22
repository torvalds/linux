/*	$OpenBSD: in_pcb.h,v 1.171 2025/07/14 09:01:52 jsg Exp $	*/
/*	$NetBSD: in_pcb.h,v 1.14 1996/02/13 23:42:00 christos Exp $	*/

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
 * Copyright (c) 1982, 1986, 1990, 1993
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
 *	@(#)in_pcb.h	8.1 (Berkeley) 6/10/93
 */

#ifndef _NETINET_IN_PCB_H_
#define _NETINET_IN_PCB_H_

#include <sys/queue.h>
#include <sys/mutex.h>
#include <sys/rwlock.h>
#include <sys/refcnt.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <netinet/ip_ipsp.h>

#include <crypto/siphash.h>

/*
 * Locks used to protect struct members in this file:
 *	I	immutable after creation
 *	N	net lock
 *	t	inpt_mtx		pcb table mutex
 *	L	pf_inp_mtx		link pf to inp mutex
 *	s	so_lock			socket rwlock
 */

/*
 * The pcb table mutex guarantees that all inpcb are consistent and
 * that bind(2) and connect(2) create unique combinations of
 * laddr/faddr/lport/fport/rtableid.  This mutex is used to protect
 * both address consistency and inpcb lookup during protocol input.
 * All writes to inp_[lf]addr take table mutex.  A per socket lock is
 * needed, so that socket layer input have a consistent view at these
 * values.
 *
 * In soconnect() and sosend() a per pcb mutex cannot be used.  They
 * eventually call IP output which takes pf lock which is a sleeping lock.
 * Also connect(2) does a route lookup for source selection.  There
 * route resolve happens, which creates a route, which sends a route
 * message, which needs route lock, which is a rw-lock.
 *
 * On the other hand a mutex should be used in protocol input.  It
 * does not make sense to do a process switch per packet.  Better spin
 * until the packet can be processed.
 *
 * So there are three locks.  Table mutex is for writing inp_[lf]addr/port
 * and lookup, socket rw-lock to separate sockets in system calls, and
 * socket buffer mutex to protect socket receive buffer.  Changing
 * inp_[lf]addr/port takes both per socket rw-lock and global table mutex.
 * Protocol input only reads inp_[lf]addr/port during lookup and is safe.
 */

struct pf_state_key;

union inpaddru {
	struct in_addr iau_addr;
	struct in6_addr iau_addr6;
};

/*
 * Common structure pcb for internet protocol implementation.
 * Here are stored pointers to local and foreign host table
 * entries, local and foreign socket numbers, and pointers
 * up (to a socket structure) and down (to a protocol-specific)
 * control block.
 */
struct inpcb {
	struct	  inpcbtable *inp_table;	/* [I] inet queue/hash table */
	TAILQ_ENTRY(inpcb) inp_queue;		/* [t] inet PCB queue */
	/* keep fields above in sync with struct inpcb_iterator */
	LIST_ENTRY(inpcb) inp_hash;		/* [t] local and foreign hash */
	LIST_ENTRY(inpcb) inp_lhash;		/* [t] local port hash */
	union	  inpaddru inp_faddru;		/* [t] Foreign address. */
	union	  inpaddru inp_laddru;		/* [t] Local address. */
#define	inp_faddr	inp_faddru.iau_addr
#define	inp_faddr6	inp_faddru.iau_addr6
#define	inp_laddr	inp_laddru.iau_addr
#define	inp_laddr6	inp_laddru.iau_addr6
	u_int16_t inp_fport;		/* [t] foreign port */
	u_int16_t inp_lport;		/* [t] local port */
	struct	  socket *inp_socket;	/* [I] back pointer to socket */
	caddr_t	  inp_ppcb;		/* [s] pointer to per-protocol pcb */
	struct    route inp_route;	/* [s] cached route */
	struct    refcnt inp_refcnt;	/* refcount PCB, delay memory free */
	int	  inp_flags;		/* generic IP/datagram flags */
	union {				/* Header prototype. */
		struct ip hu_ip;
		struct ip6_hdr hu_ipv6;
	} inp_hu;
#define	inp_ip		inp_hu.hu_ip
#define	inp_ipv6	inp_hu.hu_ipv6
	union {
		struct	mbuf *inp_options;		/* IPv4 options */
		struct	ip6_pktopts *inp_outputopts6;	/* IPv6 options */
	};
	int inp_hops;
	union {
		struct ip_moptions *mou_mo;
		struct ip6_moptions *mou_mo6;
	} inp_mou;
#define inp_moptions inp_mou.mou_mo	/* [N] IPv4 multicast options */
#define inp_moptions6 inp_mou.mou_mo6	/* [N] IPv6 multicast options */
	struct	ipsec_level   inp_seclevel;	/* [N] IPsec level of socket */
	u_char	inp_ip_minttl;		/* minimum TTL or drop */
#define inp_ip6_minhlim inp_ip_minttl	/* minimum Hop Limit or drop */
#define	inp_flowinfo	inp_hu.hu_ipv6.ip6_flow

	int	inp_cksum6;
	struct	icmp6_filter *inp_icmp6filt;
	struct	pf_state_key *inp_pf_sk; /* [L] */
	struct	mbuf *(*inp_upcall)(void *, struct mbuf *,
	    struct ip *, struct ip6_hdr *, void *, int, struct netstack *);
	void	*inp_upcall_arg;
	u_int	inp_rtableid;		/* [t] */
	int	inp_pipex;		/* pipex indication */
	uint16_t inp_flowid;		/* [s] */
};

LIST_HEAD(inpcbhead, inpcb);

struct inpcb_iterator {
	struct	  inpcbtable *inp_table;	/* [I] always NULL */
	TAILQ_ENTRY(inpcb) inp_queue;		/* [t] inet PCB queue */
	/* keep fields above in sync with struct inpcb */
};

static inline int
in_pcb_is_iterator(struct inpcb *inp)
{
	return (inp->inp_table == NULL ? 1 : 0);
}

struct inpcbtable {
	struct mutex inpt_mtx;			/* protect queue and hash */
	TAILQ_HEAD(inpthead, inpcb) inpt_queue;	/* [t] inet PCB queue */
	struct	inpcbhead *inpt_hashtbl;	/* [t] local and foreign hash */
	struct	inpcbhead *inpt_lhashtbl;	/* [t] local port hash */
	SIPHASH_KEY inpt_key, inpt_lkey;	/* [I] secrets for hashes */
	u_long	inpt_mask, inpt_lmask;		/* [t] hash masks */
	int	inpt_count, inpt_size;		/* [t] queue count, hash size */
};

/* flags in inp_flags: */
#define	INP_RECVOPTS	0x001	/* receive incoming IP options */
#define	INP_RECVRETOPTS	0x002	/* receive IP options for reply */
#define	INP_RECVDSTADDR	0x004	/* receive IP dst address */

#define	INP_RXDSTOPTS	INP_RECVOPTS
#define	INP_RXHOPOPTS	INP_RECVRETOPTS
#define	INP_RXINFO	INP_RECVDSTADDR
#define	INP_RXSRCRT	0x010
#define	INP_HOPLIMIT	0x020

#define	INP_HDRINCL	0x008	/* user supplies entire IP header */
#define	INP_HIGHPORT	0x010	/* user wants "high" port binding */
#define	INP_LOWPORT	0x020	/* user wants "low" port binding */
#define	INP_RECVIF	0x080	/* receive incoming interface */
#define	INP_RECVTTL	0x040	/* receive incoming IP TTL */
#define	INP_RECVDSTPORT	0x200	/* receive IP dst addr before rdr */
#define	INP_RECVRTABLE	0x400	/* receive routing table */
#define	INP_IPSECFLOWINFO 0x800	/* receive IPsec flow info */

#define	INP_CONTROLOPTS	(INP_RECVOPTS|INP_RECVRETOPTS|INP_RECVDSTADDR| \
	    INP_RXSRCRT|INP_HOPLIMIT|INP_RECVIF|INP_RECVTTL|INP_RECVDSTPORT| \
	    INP_RECVRTABLE)

/*
 * These flags' values should be determined by either the transport
 * protocol at PRU_BIND, PRU_LISTEN, PRU_CONNECT, etc, or by in_pcb*().
 */
#define INP_IPV6	0x100	/* socket, proto, domain, family is PF_INET6 */

/*
 * Flags in inp_flags for IPV6
 */
#define IN6P_HIGHPORT		INP_HIGHPORT	/* user wants "high" port */
#define IN6P_LOWPORT		INP_LOWPORT	/* user wants "low" port */
#define IN6P_RECVDSTPORT	INP_RECVDSTPORT	/* receive IP dst addr before rdr */
#define IN6P_PKTINFO		0x010000 /* receive IP6 dst and I/F */
#define IN6P_HOPLIMIT		0x020000 /* receive hoplimit */
#define IN6P_HOPOPTS		0x040000 /* receive hop-by-hop options */
#define IN6P_DSTOPTS		0x080000 /* receive dst options after rthdr */
#define IN6P_RTHDR		0x100000 /* receive routing header */
#define IN6P_TCLASS		0x400000 /* receive traffic class value */
#define IN6P_AUTOFLOWLABEL	0x800000 /* attach flowlabel automatically */

#define IN6P_ANONPORT		0x4000000 /* port chosen for user */
#define IN6P_RFC2292		0x40000000 /* used RFC2292 API on the socket */
#define IN6P_MTU		0x80000000 /* receive path MTU */

#define IN6P_MINMTU		0x20000000 /* use minimum MTU */

#define IN6P_CONTROLOPTS	(IN6P_PKTINFO|IN6P_HOPLIMIT|IN6P_HOPOPTS|\
				 IN6P_DSTOPTS|IN6P_RTHDR|\
				 IN6P_TCLASS|IN6P_AUTOFLOWLABEL|IN6P_RFC2292|\
				 IN6P_MTU|IN6P_RECVDSTPORT)

#define	INPLOOKUP_WILDCARD	1
#define	INPLOOKUP_SETLOCAL	2
#define	INPLOOKUP_IPV6		4

#define	sotoinpcb(so)	((struct inpcb *)(so)->so_pcb)

/* macros for handling bitmap of ports not to allocate dynamically */
#define	DP_MAPBITS	(sizeof(u_int32_t) * NBBY)
#define	DP_MAPSIZE	(howmany(65536, DP_MAPBITS))
#define	DP_SET(m, p)	((m)[(p) / DP_MAPBITS] |= (1U << ((p) % DP_MAPBITS)))
#define	DP_CLR(m, p)	((m)[(p) / DP_MAPBITS] &= ~(1U << ((p) % DP_MAPBITS)))
#define	DP_ISSET(m, p)	((m)[(p) / DP_MAPBITS] & (1U << ((p) % DP_MAPBITS)))

/* default values for baddynamicports [see ip_init()] */
#define	DEFBADDYNAMICPORTS_TCP	{ \
	587, 749, 750, 751, 853, 871, 2049, \
	6000, 6001, 6002, 6003, 6004, 6005, 6006, 6007, 6008, 6009, 6010, \
	0 }
#define	DEFBADDYNAMICPORTS_UDP	{ 623, 664, 749, 750, 751, 2049, \
	3784, 3785, 7784, /* BFD/S-BFD ports */ \
	 0 }

#define DEFROOTONLYPORTS_TCP { \
	2049, \
	0 }
#define DEFROOTONLYPORTS_UDP { \
	2049, \
	0 }

struct baddynamicports {
	u_int32_t tcp[DP_MAPSIZE];
	u_int32_t udp[DP_MAPSIZE];
};

#ifdef _KERNEL

#define IN_PCBLOCK_HOLD	1
#define IN_PCBLOCK_GRAB	2

extern struct inpcbtable rawcbtable, rawin6pcbtable;
extern struct baddynamicports baddynamicports;
extern struct baddynamicports rootonlyports;
extern int in_pcbnotifymiss;

void	 in_init(void);
void	 in_losing(struct inpcb *);
int	 in_pcballoc(struct socket *, struct inpcbtable *, int);
int	 in_pcbbind_locked(struct inpcb *, struct mbuf *, const void *,
	    struct proc *);
int	 in_pcbbind(struct inpcb *, struct mbuf *, struct proc *);
int	 in_pcbaddrisavail(const struct inpcb *, struct sockaddr_in *, int,
	    struct proc *);
int	 in_pcbconnect(struct inpcb *, struct mbuf *);
void	 in_pcbdetach(struct inpcb *);
struct socket *
	 in_pcbsolock(struct inpcb *);
void	 in_pcbsounlock(struct inpcb *, struct socket *);
struct inpcb *
	 in_pcbref(struct inpcb *);
void	 in_pcbunref(struct inpcb *);
void	 in_pcbdisconnect(struct inpcb *);
struct inpcb *
	 in_pcb_iterator(struct inpcbtable *, struct inpcb *,
	    struct inpcb_iterator *);
void	 in_pcb_iterator_abort(struct inpcbtable *, struct inpcb *,
	    struct inpcb_iterator *);
struct inpcb *
	 in_pcblookup(struct inpcbtable *, struct in_addr, u_int,
	    struct in_addr, u_int, u_int);
struct inpcb *
	 in_pcblookup_listen(struct inpcbtable *, struct in_addr, u_int,
	    struct mbuf *, u_int);
#ifdef INET6
uint64_t in6_pcbhash(struct inpcbtable *, u_int, const struct in6_addr *,
	    u_short, const struct in6_addr *, u_short);
struct inpcb *
	 in6_pcblookup(struct inpcbtable *, const struct in6_addr *,
	    u_int, const struct in6_addr *, u_int, u_int);
struct inpcb *
	 in6_pcblookup_listen(struct inpcbtable *, struct in6_addr *, u_int,
	    struct mbuf *, u_int);
int	 in6_pcbaddrisavail_lock(const struct inpcb *, struct sockaddr_in6 *,
	    int, struct proc *, int);
int	 in6_pcbaddrisavail(const struct inpcb *, struct sockaddr_in6 *, int,
	    struct proc *);
int	 in6_pcbconnect(struct inpcb *, struct mbuf *);
void	 in6_setsockaddr(struct inpcb *, struct mbuf *);
void	 in6_setpeeraddr(struct inpcb *, struct mbuf *);
int	 in6_sockaddr(struct socket *, struct mbuf *);
int	 in6_peeraddr(struct socket *, struct mbuf *);
#endif /* INET6 */
void	 in_pcbinit(struct inpcbtable *, int);
struct inpcb *
	 in_pcblookup_local_lock(struct inpcbtable *, const void *, u_int, int,
	    u_int, int);
void	 in_pcbnotifyall(struct inpcbtable *, const struct sockaddr_in *,
	    u_int, int, void (*)(struct inpcb *, int));
void	 in_pcbrehash(struct inpcb *);
void	 in_pcbrtchange(struct inpcb *, int);
void	 in_setpeeraddr(struct inpcb *, struct mbuf *);
void	 in_setsockaddr(struct inpcb *, struct mbuf *);
int	 in_sockaddr(struct socket *, struct mbuf *);
int	 in_peeraddr(struct socket *, struct mbuf *);
int	 in_baddynamic(u_int16_t, u_int16_t);
int	 in_rootonly(u_int16_t, u_int16_t);
int	 in_pcbselsrc(struct in_addr *, const struct sockaddr_in *,
	    struct inpcb *);
struct rtentry *
	in_pcbrtentry(struct inpcb *);

/* INET6 stuff */
struct rtentry *
	in6_pcbrtentry(struct inpcb *);
void	in6_pcbnotify(struct inpcbtable *, const struct sockaddr_in6 *,
	u_int, const struct sockaddr_in6 *, u_int, u_int, int, void *,
	void (*)(struct inpcb *, int));
int	in6_selecthlim(const struct inpcb *);
int	in_pcbset_rtableid(struct inpcb *, u_int);
int	in_pcbset_addr(struct inpcb *, const struct sockaddr *,
	    const struct sockaddr *, u_int);
int	in6_pcbset_addr(struct inpcb *, const struct sockaddr_in6 *,
	    const struct sockaddr_in6 *, u_int);
void	in_pcbunset_faddr(struct inpcb *);
void	in_pcbunset_laddr(struct inpcb *);

#endif /* _KERNEL */
#endif /* _NETINET_IN_PCB_H_ */
