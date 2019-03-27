/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1990, 1993
 *	The Regents of the University of California.
 * Copyright (c) 2010-2011 Juniper Networks, Inc.
 * All rights reserved.
 *
 * Portions of this software were developed by Robert N. M. Watson under
 * contract to Juniper Networks, Inc.
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
 * $FreeBSD$
 */

#ifndef _NETINET_IN_PCB_H_
#define _NETINET_IN_PCB_H_

#include <sys/queue.h>
#include <sys/epoch.h>
#include <sys/_lock.h>
#include <sys/_mutex.h>
#include <sys/_rwlock.h>
#include <net/route.h>

#ifdef _KERNEL
#include <sys/lock.h>
#include <sys/rwlock.h>
#include <net/vnet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <vm/uma.h>
#endif
#include <sys/ck.h>

#define	in6pcb		inpcb	/* for KAME src sync over BSD*'s */
#define	in6p_sp		inp_sp	/* for KAME src sync over BSD*'s */

/*
 * struct inpcb is the common protocol control block structure used in most
 * IP transport protocols.
 *
 * Pointers to local and foreign host table entries, local and foreign socket
 * numbers, and pointers up (to a socket structure) and down (to a
 * protocol-specific control block) are stored here.
 */
CK_LIST_HEAD(inpcbhead, inpcb);
CK_LIST_HEAD(inpcbporthead, inpcbport);
CK_LIST_HEAD(inpcblbgrouphead, inpcblbgroup);
typedef	uint64_t	inp_gen_t;

/*
 * PCB with AF_INET6 null bind'ed laddr can receive AF_INET input packet.
 * So, AF_INET6 null laddr is also used as AF_INET null laddr, by utilizing
 * the following structure.
 */
struct in_addr_4in6 {
	u_int32_t	ia46_pad32[3];
	struct	in_addr	ia46_addr4;
};

union in_dependaddr {
	struct in_addr_4in6 id46_addr;
	struct in6_addr	id6_addr;
};

/*
 * NOTE: ipv6 addrs should be 64-bit aligned, per RFC 2553.  in_conninfo has
 * some extra padding to accomplish this.
 * NOTE 2: tcp_syncache.c uses first 5 32-bit words, which identify fport,
 * lport, faddr to generate hash, so these fields shouldn't be moved.
 */
struct in_endpoints {
	u_int16_t	ie_fport;		/* foreign port */
	u_int16_t	ie_lport;		/* local port */
	/* protocol dependent part, local and foreign addr */
	union in_dependaddr ie_dependfaddr;	/* foreign host table entry */
	union in_dependaddr ie_dependladdr;	/* local host table entry */
#define	ie_faddr	ie_dependfaddr.id46_addr.ia46_addr4
#define	ie_laddr	ie_dependladdr.id46_addr.ia46_addr4
#define	ie6_faddr	ie_dependfaddr.id6_addr
#define	ie6_laddr	ie_dependladdr.id6_addr
	u_int32_t	ie6_zoneid;		/* scope zone id */
};

/*
 * XXX The defines for inc_* are hacks and should be changed to direct
 * references.
 */
struct in_conninfo {
	u_int8_t	inc_flags;
	u_int8_t	inc_len;
	u_int16_t	inc_fibnum;	/* XXX was pad, 16 bits is plenty */
	/* protocol dependent part */
	struct	in_endpoints inc_ie;
};

/*
 * Flags for inc_flags.
 */
#define	INC_ISIPV6	0x01
#define	INC_IPV6MINMTU	0x02

#define	inc_fport	inc_ie.ie_fport
#define	inc_lport	inc_ie.ie_lport
#define	inc_faddr	inc_ie.ie_faddr
#define	inc_laddr	inc_ie.ie_laddr
#define	inc6_faddr	inc_ie.ie6_faddr
#define	inc6_laddr	inc_ie.ie6_laddr
#define	inc6_zoneid	inc_ie.ie6_zoneid

#if defined(_KERNEL) || defined(_WANT_INPCB)
/*
 * struct inpcb captures the network layer state for TCP, UDP, and raw IPv4 and
 * IPv6 sockets.  In the case of TCP and UDP, further per-connection state is
 * hung off of inp_ppcb most of the time.  Almost all fields of struct inpcb
 * are static after creation or protected by a per-inpcb rwlock, inp_lock.  A
 * few fields are protected by multiple locks as indicated in the locking notes
 * below.  For these fields, all of the listed locks must be write-locked for
 * any modifications.  However, these fields can be safely read while any one of
 * the listed locks are read-locked.  This model can permit greater concurrency
 * for read operations.  For example, connections can be looked up while only
 * holding a read lock on the global pcblist lock.  This is important for
 * performance when attempting to find the connection for a packet given its IP
 * and port tuple.
 *
 * One noteworthy exception is that the global pcbinfo lock follows a different
 * set of rules in relation to the inp_list field.  Rather than being
 * write-locked for modifications and read-locked for list iterations, it must
 * be read-locked during modifications and write-locked during list iterations.
 * This ensures that the relatively rare global list iterations safely walk a
 * stable snapshot of connections while allowing more common list modifications
 * to safely grab the pcblist lock just while adding or removing a connection
 * from the global list.
 *
 * Key:
 * (b) - Protected by the hpts lock.
 * (c) - Constant after initialization
 * (e) - Protected by the net_epoch_prempt epoch
 * (g) - Protected by the pcbgroup lock
 * (i) - Protected by the inpcb lock
 * (p) - Protected by the pcbinfo lock for the inpcb
 * (l) - Protected by the pcblist lock for the inpcb
 * (h) - Protected by the pcbhash lock for the inpcb
 * (s) - Protected by another subsystem's locks
 * (x) - Undefined locking
 * 
 * Notes on the tcp_hpts:
 * 
 * First Hpts lock order is
 * 1) INP_WLOCK()
 * 2) HPTS_LOCK() i.e. hpts->pmtx 
 *
 * To insert a TCB on the hpts you *must* be holding the INP_WLOCK(). 
 * You may check the inp->inp_in_hpts flag without the hpts lock. 
 * The hpts is the only one that will clear this flag holding 
 * only the hpts lock. This means that in your tcp_output()
 * routine when you test for the inp_in_hpts flag to be 1 
 * it may be transitioning to 0 (by the hpts). 
 * That's ok since that will just mean an extra call to tcp_output 
 * that most likely will find the call you executed
 * (when the mis-match occured) will have put the TCB back 
 * on the hpts and it will return. If your
 * call did not add the inp back to the hpts then you will either
 * over-send or the cwnd will block you from sending more.
 *
 * Note you should also be holding the INP_WLOCK() when you
 * call the remove from the hpts as well. Though usually
 * you are either doing this from a timer, where you need and have
 * the INP_WLOCK() or from destroying your TCB where again
 * you should already have the INP_WLOCK().
 *
 * The inp_hpts_cpu, inp_hpts_cpu_set, inp_input_cpu and 
 * inp_input_cpu_set fields are controlled completely by
 * the hpts. Do not ever set these. The inp_hpts_cpu_set
 * and inp_input_cpu_set fields indicate if the hpts has
 * setup the respective cpu field. It is advised if this
 * field is 0, to enqueue the packet with the appropriate
 * hpts_immediate() call. If the _set field is 1, then
 * you may compare the inp_*_cpu field to the curcpu and
 * may want to again insert onto the hpts if these fields
 * are not equal (i.e. you are not on the expected CPU).
 *
 * A note on inp_hpts_calls and inp_input_calls, these
 * flags are set when the hpts calls either the output
 * or do_segment routines respectively. If the routine
 * being called wants to use this, then it needs to
 * clear the flag before returning. The hpts will not
 * clear the flag. The flags can be used to tell if
 * the hpts is the function calling the respective
 * routine.
 *
 * A few other notes:
 *
 * When a read lock is held, stability of the field is guaranteed; to write
 * to a field, a write lock must generally be held.
 *
 * netinet/netinet6-layer code should not assume that the inp_socket pointer
 * is safe to dereference without inp_lock being held, even for protocols
 * other than TCP (where the inpcb persists during TIMEWAIT even after the
 * socket has been freed), or there may be close(2)-related races.
 *
 * The inp_vflag field is overloaded, and would otherwise ideally be (c).
 *
 * TODO:  Currently only the TCP stack is leveraging the global pcbinfo lock
 * read-lock usage during modification, this model can be applied to other
 * protocols (especially SCTP).
 */
struct icmp6_filter;
struct inpcbpolicy;
struct m_snd_tag;
struct inpcb {
	/* Cache line #1 (amd64) */
	CK_LIST_ENTRY(inpcb) inp_hash;	/* [w](h/i) [r](e/i)  hash list */
	CK_LIST_ENTRY(inpcb) inp_pcbgrouphash;	/* (g/i) hash list */
	struct rwlock	inp_lock;
	/* Cache line #2 (amd64) */
#define	inp_start_zero	inp_hpts
#define	inp_zero_size	(sizeof(struct inpcb) - \
			    offsetof(struct inpcb, inp_start_zero))
	TAILQ_ENTRY(inpcb) inp_hpts;	/* pacing out queue next lock(b) */

	uint32_t inp_hpts_request;	/* Current hpts request, zero if
					 * fits in the pacing window (i&b). */
	/*
	 * Note the next fields are protected by a
	 * different lock (hpts-lock). This means that 
	 * they must correspond in size to the smallest
	 * protectable bit field (uint8_t on x86, and
	 * other platfomrs potentially uint32_t?). Also
	 * since CPU switches can occur at different times the two
	 * fields can *not* be collapsed into a signal bit field.
	 */
#if defined(__amd64__) || defined(__i386__)	
	volatile uint8_t inp_in_hpts; /* on output hpts (lock b) */
	volatile uint8_t inp_in_input; /* on input hpts (lock b) */
#else
	volatile uint32_t inp_in_hpts; /* on output hpts (lock b) */
	volatile uint32_t inp_in_input; /* on input hpts (lock b) */
#endif
	volatile uint16_t  inp_hpts_cpu; /* Lock (i) */
	u_int	inp_refcount;		/* (i) refcount */
	int	inp_flags;		/* (i) generic IP/datagram flags */
	int	inp_flags2;		/* (i) generic IP/datagram flags #2*/
	volatile uint16_t  inp_input_cpu; /* Lock (i) */
	volatile uint8_t inp_hpts_cpu_set :1,  /* on output hpts (i) */
			 inp_input_cpu_set : 1,	/* on input hpts (i) */
			 inp_hpts_calls :1,	/* (i) from output hpts */
			 inp_input_calls :1,	/* (i) from input hpts */
			 inp_spare_bits2 : 4;
	uint8_t inp_spare_byte;		/* Compiler hole */
	void	*inp_ppcb;		/* (i) pointer to per-protocol pcb */
	struct	socket *inp_socket;	/* (i) back pointer to socket */
	uint32_t 	 inp_hptsslot;	/* Hpts wheel slot this tcb is Lock(i&b) */
	uint32_t         inp_hpts_drop_reas;	/* reason we are dropping the PCB (lock i&b) */
	TAILQ_ENTRY(inpcb) inp_input;	/* pacing in  queue next lock(b) */
	struct	inpcbinfo *inp_pcbinfo;	/* (c) PCB list info */
	struct	inpcbgroup *inp_pcbgroup; /* (g/i) PCB group list */
	CK_LIST_ENTRY(inpcb) inp_pcbgroup_wild; /* (g/i/h) group wildcard entry */
	struct	ucred	*inp_cred;	/* (c) cache of socket cred */
	u_int32_t inp_flow;		/* (i) IPv6 flow information */
	u_char	inp_vflag;		/* (i) IP version flag (v4/v6) */
	u_char	inp_ip_ttl;		/* (i) time to live proto */
	u_char	inp_ip_p;		/* (c) protocol proto */
	u_char	inp_ip_minttl;		/* (i) minimum TTL or drop */
	uint32_t inp_flowid;		/* (x) flow id / queue id */
	struct m_snd_tag *inp_snd_tag;	/* (i) send tag for outgoing mbufs */
	uint32_t inp_flowtype;		/* (x) M_HASHTYPE value */
	uint32_t inp_rss_listen_bucket;	/* (x) overridden RSS listen bucket */

	/* Local and foreign ports, local and foreign addr. */
	struct	in_conninfo inp_inc;	/* (i) list for PCB's local port */

	/* MAC and IPSEC policy information. */
	struct	label *inp_label;	/* (i) MAC label */
	struct	inpcbpolicy *inp_sp;    /* (s) for IPSEC */

	/* Protocol-dependent part; options. */
	struct {
		u_char	inp_ip_tos;		/* (i) type of service proto */
		struct mbuf		*inp_options;	/* (i) IP options */
		struct ip_moptions	*inp_moptions;	/* (i) mcast options */
	};
	struct {
		/* (i) IP options */
		struct mbuf		*in6p_options;
		/* (i) IP6 options for outgoing packets */
		struct ip6_pktopts	*in6p_outputopts;
		/* (i) IP multicast options */
		struct ip6_moptions	*in6p_moptions;
		/* (i) ICMPv6 code type filter */
		struct icmp6_filter	*in6p_icmp6filt;
		/* (i) IPV6_CHECKSUM setsockopt */
		int	in6p_cksum;
		short	in6p_hops;
	};
	CK_LIST_ENTRY(inpcb) inp_portlist;	/* (i/h) */
	struct	inpcbport *inp_phd;	/* (i/h) head of this list */
	inp_gen_t	inp_gencnt;	/* (c) generation count */
	void		*spare_ptr;	/* Spare pointer. */
	rt_gen_t	inp_rt_cookie;	/* generation for route entry */
	union {				/* cached L3 information */
		struct route inp_route;
		struct route_in6 inp_route6;
	};
	CK_LIST_ENTRY(inpcb) inp_list;	/* (p/l) list for all PCBs for proto */
	                                /* (e[r]) for list iteration */
	                                /* (p[w]/l) for addition/removal */
	struct epoch_context inp_epoch_ctx;
};
#endif	/* _KERNEL */

#define	inp_fport	inp_inc.inc_fport
#define	inp_lport	inp_inc.inc_lport
#define	inp_faddr	inp_inc.inc_faddr
#define	inp_laddr	inp_inc.inc_laddr

#define	in6p_faddr	inp_inc.inc6_faddr
#define	in6p_laddr	inp_inc.inc6_laddr
#define	in6p_zoneid	inp_inc.inc6_zoneid
#define	in6p_flowinfo	inp_flow

#define	inp_vnet	inp_pcbinfo->ipi_vnet

/*
 * The range of the generation count, as used in this implementation, is 9e19.
 * We would have to create 300 billion connections per second for this number
 * to roll over in a year.  This seems sufficiently unlikely that we simply
 * don't concern ourselves with that possibility.
 */

/*
 * Interface exported to userland by various protocols which use inpcbs.  Hack
 * alert -- only define if struct xsocket is in scope.
 * Fields prefixed with "xi_" are unique to this structure, and the rest
 * match fields in the struct inpcb, to ease coding and porting.
 *
 * Legend:
 * (s) - used by userland utilities in src
 * (p) - used by utilities in ports
 * (3) - is known to be used by third party software not in ports
 * (n) - no known usage
 */
#ifdef _SYS_SOCKETVAR_H_
struct xinpcb {
	ksize_t		xi_len;			/* length of this structure */
	struct xsocket	xi_socket;		/* (s,p) */
	struct in_conninfo inp_inc;		/* (s,p) */
	uint64_t	inp_gencnt;		/* (s,p) */
	kvaddr_t	inp_ppcb;		/* (s) netstat(1) */
	int64_t		inp_spare64[4];
	uint32_t	inp_flow;		/* (s) */
	uint32_t	inp_flowid;		/* (s) */
	uint32_t	inp_flowtype;		/* (s) */
	int32_t		inp_flags;		/* (s,p) */
	int32_t		inp_flags2;		/* (s) */
	int32_t		inp_rss_listen_bucket;	/* (n) */
	int32_t		in6p_cksum;		/* (n) */
	int32_t		inp_spare32[4];
	uint16_t	in6p_hops;		/* (n) */
	uint8_t		inp_ip_tos;		/* (n) */
	int8_t		pad8;
	uint8_t		inp_vflag;		/* (s,p) */
	uint8_t		inp_ip_ttl;		/* (n) */
	uint8_t		inp_ip_p;		/* (n) */
	uint8_t		inp_ip_minttl;		/* (n) */
	int8_t		inp_spare8[4];
} __aligned(8);

struct xinpgen {
	ksize_t	xig_len;	/* length of this structure */
	u_int		xig_count;	/* number of PCBs at this time */
	uint32_t	_xig_spare32;
	inp_gen_t	xig_gen;	/* generation count at this time */
	so_gen_t	xig_sogen;	/* socket generation count this time */
	uint64_t	_xig_spare64[4];
} __aligned(8);
#ifdef	_KERNEL
void	in_pcbtoxinpcb(const struct inpcb *, struct xinpcb *);
#endif
#endif /* _SYS_SOCKETVAR_H_ */

struct inpcbport {
	struct epoch_context phd_epoch_ctx;
	CK_LIST_ENTRY(inpcbport) phd_hash;
	struct inpcbhead phd_pcblist;
	u_short phd_port;
};

struct in_pcblist {
	int il_count;
	struct epoch_context il_epoch_ctx;
	struct inpcbinfo *il_pcbinfo;
	struct inpcb *il_inp_list[0];
};

/*-
 * Global data structure for each high-level protocol (UDP, TCP, ...) in both
 * IPv4 and IPv6.  Holds inpcb lists and information for managing them.
 *
 * Each pcbinfo is protected by three locks: ipi_lock, ipi_hash_lock and
 * ipi_list_lock:
 *  - ipi_lock covering the global pcb list stability during loop iteration,
 *  - ipi_hash_lock covering the hashed lookup tables,
 *  - ipi_list_lock covering mutable global fields (such as the global
 *    pcb list)
 *
 * The lock order is:
 *
 *    ipi_lock (before)
 *        inpcb locks (before)
 *            ipi_list locks (before)
 *                {ipi_hash_lock, pcbgroup locks}
 *
 * Locking key:
 *
 * (c) Constant or nearly constant after initialisation
 * (e) - Protected by the net_epoch_prempt epoch
 * (g) Locked by ipi_lock
 * (l) Locked by ipi_list_lock
 * (h) Read using either net_epoch_preempt or inpcb lock; write requires both ipi_hash_lock and inpcb lock
 * (p) Protected by one or more pcbgroup locks
 * (x) Synchronisation properties poorly defined
 */
struct inpcbinfo {
	/*
	 * Global lock protecting inpcb list modification
	 */
	struct mtx		 ipi_lock;

	/*
	 * Global list of inpcbs on the protocol.
	 */
	struct inpcbhead	*ipi_listhead;		/* [r](e) [w](g/l) */
	u_int			 ipi_count;		/* (l) */

	/*
	 * Generation count -- incremented each time a connection is allocated
	 * or freed.
	 */
	u_quad_t		 ipi_gencnt;		/* (l) */

	/*
	 * Fields associated with port lookup and allocation.
	 */
	u_short			 ipi_lastport;		/* (x) */
	u_short			 ipi_lastlow;		/* (x) */
	u_short			 ipi_lasthi;		/* (x) */

	/*
	 * UMA zone from which inpcbs are allocated for this protocol.
	 */
	struct	uma_zone	*ipi_zone;		/* (c) */

	/*
	 * Connection groups associated with this protocol.  These fields are
	 * constant, but pcbgroup structures themselves are protected by
	 * per-pcbgroup locks.
	 */
	struct inpcbgroup	*ipi_pcbgroups;		/* (c) */
	u_int			 ipi_npcbgroups;	/* (c) */
	u_int			 ipi_hashfields;	/* (c) */

	/*
	 * Global lock protecting modification non-pcbgroup hash lookup tables.
	 */
	struct mtx		 ipi_hash_lock;

	/*
	 * Global hash of inpcbs, hashed by local and foreign addresses and
	 * port numbers.
	 */
	struct inpcbhead	*ipi_hashbase;		/* (h) */
	u_long			 ipi_hashmask;		/* (h) */

	/*
	 * Global hash of inpcbs, hashed by only local port number.
	 */
	struct inpcbporthead	*ipi_porthashbase;	/* (h) */
	u_long			 ipi_porthashmask;	/* (h) */

	/*
	 * List of wildcard inpcbs for use with pcbgroups.  In the past, was
	 * per-pcbgroup but is now global.  All pcbgroup locks must be held
	 * to modify the list, so any is sufficient to read it.
	 */
	struct inpcbhead	*ipi_wildbase;		/* (p) */
	u_long			 ipi_wildmask;		/* (p) */

	/*
	 * Load balance groups used for the SO_REUSEPORT_LB option,
	 * hashed by local port.
	 */
	struct	inpcblbgrouphead *ipi_lbgrouphashbase;	/* (h) */
	u_long			 ipi_lbgrouphashmask;	/* (h) */

	/*
	 * Pointer to network stack instance
	 */
	struct vnet		*ipi_vnet;		/* (c) */

	/*
	 * general use 2
	 */
	void 			*ipi_pspare[2];

	/*
	 * Global lock protecting global inpcb list, inpcb count, etc.
	 */
	struct rwlock		 ipi_list_lock;
};

#ifdef _KERNEL
/*
 * Connection groups hold sets of connections that have similar CPU/thread
 * affinity.  Each connection belongs to exactly one connection group.
 */
struct inpcbgroup {
	/*
	 * Per-connection group hash of inpcbs, hashed by local and foreign
	 * addresses and port numbers.
	 */
	struct inpcbhead	*ipg_hashbase;		/* (c) */
	u_long			 ipg_hashmask;		/* (c) */

	/*
	 * Notional affinity of this pcbgroup.
	 */
	u_int			 ipg_cpu;		/* (p) */

	/*
	 * Per-connection group lock, not to be confused with ipi_lock.
	 * Protects the hash table hung off the group, but also the global
	 * wildcard list in inpcbinfo.
	 */
	struct mtx		 ipg_lock;
} __aligned(CACHE_LINE_SIZE);

/*
 * Load balance groups used for the SO_REUSEPORT_LB socket option. Each group
 * (or unique address:port combination) can be re-used at most
 * INPCBLBGROUP_SIZMAX (256) times. The inpcbs are stored in il_inp which
 * is dynamically resized as processes bind/unbind to that specific group.
 */
struct inpcblbgroup {
	CK_LIST_ENTRY(inpcblbgroup) il_list;
	struct epoch_context il_epoch_ctx;
	uint16_t	il_lport;			/* (c) */
	u_char		il_vflag;			/* (c) */
	u_char		il_pad;
	uint32_t	il_pad2;
	union in_dependaddr il_dependladdr;		/* (c) */
#define	il_laddr	il_dependladdr.id46_addr.ia46_addr4
#define	il6_laddr	il_dependladdr.id6_addr
	uint32_t	il_inpsiz; /* max count in il_inp[] (h) */
	uint32_t	il_inpcnt; /* cur count in il_inp[] (h) */
	struct inpcb	*il_inp[];			/* (h) */
};

#define INP_LOCK_INIT(inp, d, t) \
	rw_init_flags(&(inp)->inp_lock, (t), RW_RECURSE |  RW_DUPOK)
#define INP_LOCK_DESTROY(inp)	rw_destroy(&(inp)->inp_lock)
#define INP_RLOCK(inp)		rw_rlock(&(inp)->inp_lock)
#define INP_WLOCK(inp)		rw_wlock(&(inp)->inp_lock)
#define INP_TRY_RLOCK(inp)	rw_try_rlock(&(inp)->inp_lock)
#define INP_TRY_WLOCK(inp)	rw_try_wlock(&(inp)->inp_lock)
#define INP_RUNLOCK(inp)	rw_runlock(&(inp)->inp_lock)
#define INP_WUNLOCK(inp)	rw_wunlock(&(inp)->inp_lock)
#define	INP_TRY_UPGRADE(inp)	rw_try_upgrade(&(inp)->inp_lock)
#define	INP_DOWNGRADE(inp)	rw_downgrade(&(inp)->inp_lock)
#define	INP_WLOCKED(inp)	rw_wowned(&(inp)->inp_lock)
#define	INP_LOCK_ASSERT(inp)	rw_assert(&(inp)->inp_lock, RA_LOCKED)
#define	INP_RLOCK_ASSERT(inp)	rw_assert(&(inp)->inp_lock, RA_RLOCKED)
#define	INP_WLOCK_ASSERT(inp)	rw_assert(&(inp)->inp_lock, RA_WLOCKED)
#define	INP_UNLOCK_ASSERT(inp)	rw_assert(&(inp)->inp_lock, RA_UNLOCKED)

/*
 * These locking functions are for inpcb consumers outside of sys/netinet,
 * more specifically, they were added for the benefit of TOE drivers. The
 * macros are reserved for use by the stack.
 */
void inp_wlock(struct inpcb *);
void inp_wunlock(struct inpcb *);
void inp_rlock(struct inpcb *);
void inp_runlock(struct inpcb *);

#ifdef INVARIANT_SUPPORT
void inp_lock_assert(struct inpcb *);
void inp_unlock_assert(struct inpcb *);
#else
#define	inp_lock_assert(inp)	do {} while (0)
#define	inp_unlock_assert(inp)	do {} while (0)
#endif

void	inp_apply_all(void (*func)(struct inpcb *, void *), void *arg);
int 	inp_ip_tos_get(const struct inpcb *inp);
void 	inp_ip_tos_set(struct inpcb *inp, int val);
struct socket *
	inp_inpcbtosocket(struct inpcb *inp);
struct tcpcb *
	inp_inpcbtotcpcb(struct inpcb *inp);
void 	inp_4tuple_get(struct inpcb *inp, uint32_t *laddr, uint16_t *lp,
		uint32_t *faddr, uint16_t *fp);
int	inp_so_options(const struct inpcb *inp);

#endif /* _KERNEL */

#define INP_INFO_LOCK_INIT(ipi, d) \
	mtx_init(&(ipi)->ipi_lock, (d), NULL, MTX_DEF| MTX_RECURSE)
#define INP_INFO_LOCK_DESTROY(ipi)  mtx_destroy(&(ipi)->ipi_lock)
#define INP_INFO_RLOCK_ET(ipi, et)	NET_EPOCH_ENTER((et))
#define INP_INFO_WLOCK(ipi) mtx_lock(&(ipi)->ipi_lock)
#define INP_INFO_TRY_WLOCK(ipi)	mtx_trylock(&(ipi)->ipi_lock)
#define INP_INFO_WLOCKED(ipi)	mtx_owned(&(ipi)->ipi_lock)
#define INP_INFO_RUNLOCK_ET(ipi, et)	NET_EPOCH_EXIT((et))
#define INP_INFO_RUNLOCK_TP(ipi, tp)	NET_EPOCH_EXIT(*(tp)->t_inpcb->inp_et)
#define INP_INFO_WUNLOCK(ipi)	mtx_unlock(&(ipi)->ipi_lock)
#define	INP_INFO_LOCK_ASSERT(ipi)	MPASS(in_epoch(net_epoch_preempt) || mtx_owned(&(ipi)->ipi_lock))
#define INP_INFO_RLOCK_ASSERT(ipi)	MPASS(in_epoch(net_epoch_preempt))
#define INP_INFO_WLOCK_ASSERT(ipi)	mtx_assert(&(ipi)->ipi_lock, MA_OWNED)
#define INP_INFO_WUNLOCK_ASSERT(ipi)	\
	mtx_assert(&(ipi)->ipi_lock, MA_NOTOWNED)
#define INP_INFO_UNLOCK_ASSERT(ipi)	MPASS(!in_epoch(net_epoch_preempt) && !mtx_owned(&(ipi)->ipi_lock))

#define INP_LIST_LOCK_INIT(ipi, d) \
        rw_init_flags(&(ipi)->ipi_list_lock, (d), 0)
#define INP_LIST_LOCK_DESTROY(ipi)  rw_destroy(&(ipi)->ipi_list_lock)
#define INP_LIST_RLOCK(ipi)     rw_rlock(&(ipi)->ipi_list_lock)
#define INP_LIST_WLOCK(ipi)     rw_wlock(&(ipi)->ipi_list_lock)
#define INP_LIST_TRY_RLOCK(ipi) rw_try_rlock(&(ipi)->ipi_list_lock)
#define INP_LIST_TRY_WLOCK(ipi) rw_try_wlock(&(ipi)->ipi_list_lock)
#define INP_LIST_TRY_UPGRADE(ipi)       rw_try_upgrade(&(ipi)->ipi_list_lock)
#define INP_LIST_RUNLOCK(ipi)   rw_runlock(&(ipi)->ipi_list_lock)
#define INP_LIST_WUNLOCK(ipi)   rw_wunlock(&(ipi)->ipi_list_lock)
#define INP_LIST_LOCK_ASSERT(ipi) \
	rw_assert(&(ipi)->ipi_list_lock, RA_LOCKED)
#define INP_LIST_RLOCK_ASSERT(ipi) \
	rw_assert(&(ipi)->ipi_list_lock, RA_RLOCKED)
#define INP_LIST_WLOCK_ASSERT(ipi) \
	rw_assert(&(ipi)->ipi_list_lock, RA_WLOCKED)
#define INP_LIST_UNLOCK_ASSERT(ipi) \
	rw_assert(&(ipi)->ipi_list_lock, RA_UNLOCKED)

#define	INP_HASH_LOCK_INIT(ipi, d) mtx_init(&(ipi)->ipi_hash_lock, (d), NULL, MTX_DEF)
#define	INP_HASH_LOCK_DESTROY(ipi)	mtx_destroy(&(ipi)->ipi_hash_lock)
#define	INP_HASH_RLOCK(ipi)		struct epoch_tracker inp_hash_et; epoch_enter_preempt(net_epoch_preempt, &inp_hash_et)
#define	INP_HASH_RLOCK_ET(ipi, et)		epoch_enter_preempt(net_epoch_preempt, &(et))
#define	INP_HASH_WLOCK(ipi)		mtx_lock(&(ipi)->ipi_hash_lock)
#define	INP_HASH_RUNLOCK(ipi)		NET_EPOCH_EXIT(inp_hash_et)
#define	INP_HASH_RUNLOCK_ET(ipi, et)	NET_EPOCH_EXIT((et))
#define	INP_HASH_WUNLOCK(ipi)		mtx_unlock(&(ipi)->ipi_hash_lock)
#define	INP_HASH_LOCK_ASSERT(ipi)	MPASS(in_epoch(net_epoch_preempt) || mtx_owned(&(ipi)->ipi_hash_lock))
#define	INP_HASH_WLOCK_ASSERT(ipi)	mtx_assert(&(ipi)->ipi_hash_lock, MA_OWNED);

#define	INP_GROUP_LOCK_INIT(ipg, d)	mtx_init(&(ipg)->ipg_lock, (d), NULL, \
					    MTX_DEF | MTX_DUPOK)
#define	INP_GROUP_LOCK_DESTROY(ipg)	mtx_destroy(&(ipg)->ipg_lock)

#define	INP_GROUP_LOCK(ipg)		mtx_lock(&(ipg)->ipg_lock)
#define	INP_GROUP_LOCK_ASSERT(ipg)	mtx_assert(&(ipg)->ipg_lock, MA_OWNED)
#define	INP_GROUP_UNLOCK(ipg)		mtx_unlock(&(ipg)->ipg_lock)

#define INP_PCBHASH(faddr, lport, fport, mask) \
	(((faddr) ^ ((faddr) >> 16) ^ ntohs((lport) ^ (fport))) & (mask))
#define INP_PCBPORTHASH(lport, mask) \
	(ntohs((lport)) & (mask))
#define	INP_PCBLBGROUP_PKTHASH(faddr, lport, fport) \
	((faddr) ^ ((faddr) >> 16) ^ ntohs((lport) ^ (fport)))
#define	INP6_PCBHASHKEY(faddr)	((faddr)->s6_addr32[3])

/*
 * Flags for inp_vflags -- historically version flags only
 */
#define	INP_IPV4	0x1
#define	INP_IPV6	0x2
#define	INP_IPV6PROTO	0x4		/* opened under IPv6 protocol */

/*
 * Flags for inp_flags.
 */
#define	INP_RECVOPTS		0x00000001 /* receive incoming IP options */
#define	INP_RECVRETOPTS		0x00000002 /* receive IP options for reply */
#define	INP_RECVDSTADDR		0x00000004 /* receive IP dst address */
#define	INP_HDRINCL		0x00000008 /* user supplies entire IP header */
#define	INP_HIGHPORT		0x00000010 /* user wants "high" port binding */
#define	INP_LOWPORT		0x00000020 /* user wants "low" port binding */
#define	INP_ANONPORT		0x00000040 /* port chosen for user */
#define	INP_RECVIF		0x00000080 /* receive incoming interface */
#define	INP_MTUDISC		0x00000100 /* user can do MTU discovery */
				   	   /* 0x000200 unused: was INP_FAITH */
#define	INP_RECVTTL		0x00000400 /* receive incoming IP TTL */
#define	INP_DONTFRAG		0x00000800 /* don't fragment packet */
#define	INP_BINDANY		0x00001000 /* allow bind to any address */
#define	INP_INHASHLIST		0x00002000 /* in_pcbinshash() has been called */
#define	INP_RECVTOS		0x00004000 /* receive incoming IP TOS */
#define	IN6P_IPV6_V6ONLY	0x00008000 /* restrict AF_INET6 socket for v6 */
#define	IN6P_PKTINFO		0x00010000 /* receive IP6 dst and I/F */
#define	IN6P_HOPLIMIT		0x00020000 /* receive hoplimit */
#define	IN6P_HOPOPTS		0x00040000 /* receive hop-by-hop options */
#define	IN6P_DSTOPTS		0x00080000 /* receive dst options after rthdr */
#define	IN6P_RTHDR		0x00100000 /* receive routing header */
#define	IN6P_RTHDRDSTOPTS	0x00200000 /* receive dstoptions before rthdr */
#define	IN6P_TCLASS		0x00400000 /* receive traffic class value */
#define	IN6P_AUTOFLOWLABEL	0x00800000 /* attach flowlabel automatically */
#define	INP_TIMEWAIT		0x01000000 /* in TIMEWAIT, ppcb is tcptw */
#define	INP_ONESBCAST		0x02000000 /* send all-ones broadcast */
#define	INP_DROPPED		0x04000000 /* protocol drop flag */
#define	INP_SOCKREF		0x08000000 /* strong socket reference */
#define	INP_RESERVED_0          0x10000000 /* reserved field */
#define	INP_RESERVED_1          0x20000000 /* reserved field */
#define	IN6P_RFC2292		0x40000000 /* used RFC2292 API on the socket */
#define	IN6P_MTU		0x80000000 /* receive path MTU */

#define	INP_CONTROLOPTS		(INP_RECVOPTS|INP_RECVRETOPTS|INP_RECVDSTADDR|\
				 INP_RECVIF|INP_RECVTTL|INP_RECVTOS|\
				 IN6P_PKTINFO|IN6P_HOPLIMIT|IN6P_HOPOPTS|\
				 IN6P_DSTOPTS|IN6P_RTHDR|IN6P_RTHDRDSTOPTS|\
				 IN6P_TCLASS|IN6P_AUTOFLOWLABEL|IN6P_RFC2292|\
				 IN6P_MTU)

/*
 * Flags for inp_flags2.
 */
#define	INP_2UNUSED1		0x00000001
#define	INP_2UNUSED2		0x00000002
#define	INP_PCBGROUPWILD	0x00000004 /* in pcbgroup wildcard list */
#define	INP_REUSEPORT		0x00000008 /* SO_REUSEPORT option is set */
#define	INP_FREED		0x00000010 /* inp itself is not valid */
#define	INP_REUSEADDR		0x00000020 /* SO_REUSEADDR option is set */
#define	INP_BINDMULTI		0x00000040 /* IP_BINDMULTI option is set */
#define	INP_RSS_BUCKET_SET	0x00000080 /* IP_RSS_LISTEN_BUCKET is set */
#define	INP_RECVFLOWID		0x00000100 /* populate recv datagram with flow info */
#define	INP_RECVRSSBUCKETID	0x00000200 /* populate recv datagram with bucket id */
#define	INP_RATE_LIMIT_CHANGED	0x00000400 /* rate limit needs attention */
#define	INP_ORIGDSTADDR		0x00000800 /* receive IP dst address/port */
#define INP_CANNOT_DO_ECN	0x00001000 /* The stack does not do ECN */
#define	INP_REUSEPORT_LB	0x00002000 /* SO_REUSEPORT_LB option is set */

/*
 * Flags passed to in_pcblookup*() functions.
 */
#define	INPLOOKUP_WILDCARD	0x00000001	/* Allow wildcard sockets. */
#define	INPLOOKUP_RLOCKPCB	0x00000002	/* Return inpcb read-locked. */
#define	INPLOOKUP_WLOCKPCB	0x00000004	/* Return inpcb write-locked. */

#define	INPLOOKUP_MASK	(INPLOOKUP_WILDCARD | INPLOOKUP_RLOCKPCB | \
			    INPLOOKUP_WLOCKPCB)

#define	sotoinpcb(so)	((struct inpcb *)(so)->so_pcb)
#define	sotoin6pcb(so)	sotoinpcb(so) /* for KAME src sync over BSD*'s */

#define	INP_SOCKAF(so) so->so_proto->pr_domain->dom_family

#define	INP_CHECK_SOCKAF(so, af)	(INP_SOCKAF(so) == af)

/*
 * Constants for pcbinfo.ipi_hashfields.
 */
#define	IPI_HASHFIELDS_NONE	0
#define	IPI_HASHFIELDS_2TUPLE	1
#define	IPI_HASHFIELDS_4TUPLE	2

#ifdef _KERNEL
VNET_DECLARE(int, ipport_reservedhigh);
VNET_DECLARE(int, ipport_reservedlow);
VNET_DECLARE(int, ipport_lowfirstauto);
VNET_DECLARE(int, ipport_lowlastauto);
VNET_DECLARE(int, ipport_firstauto);
VNET_DECLARE(int, ipport_lastauto);
VNET_DECLARE(int, ipport_hifirstauto);
VNET_DECLARE(int, ipport_hilastauto);
VNET_DECLARE(int, ipport_randomized);
VNET_DECLARE(int, ipport_randomcps);
VNET_DECLARE(int, ipport_randomtime);
VNET_DECLARE(int, ipport_stoprandom);
VNET_DECLARE(int, ipport_tcpallocs);

#define	V_ipport_reservedhigh	VNET(ipport_reservedhigh)
#define	V_ipport_reservedlow	VNET(ipport_reservedlow)
#define	V_ipport_lowfirstauto	VNET(ipport_lowfirstauto)
#define	V_ipport_lowlastauto	VNET(ipport_lowlastauto)
#define	V_ipport_firstauto	VNET(ipport_firstauto)
#define	V_ipport_lastauto	VNET(ipport_lastauto)
#define	V_ipport_hifirstauto	VNET(ipport_hifirstauto)
#define	V_ipport_hilastauto	VNET(ipport_hilastauto)
#define	V_ipport_randomized	VNET(ipport_randomized)
#define	V_ipport_randomcps	VNET(ipport_randomcps)
#define	V_ipport_randomtime	VNET(ipport_randomtime)
#define	V_ipport_stoprandom	VNET(ipport_stoprandom)
#define	V_ipport_tcpallocs	VNET(ipport_tcpallocs)

void	in_pcbinfo_destroy(struct inpcbinfo *);
void	in_pcbinfo_init(struct inpcbinfo *, const char *, struct inpcbhead *,
	    int, int, char *, uma_init, u_int);

int	in_pcbbind_check_bindmulti(const struct inpcb *ni,
	    const struct inpcb *oi);

struct inpcbgroup *
	in_pcbgroup_byhash(struct inpcbinfo *, u_int, uint32_t);
struct inpcbgroup *
	in_pcbgroup_byinpcb(struct inpcb *);
struct inpcbgroup *
	in_pcbgroup_bytuple(struct inpcbinfo *, struct in_addr, u_short,
	    struct in_addr, u_short);
void	in_pcbgroup_destroy(struct inpcbinfo *);
int	in_pcbgroup_enabled(struct inpcbinfo *);
void	in_pcbgroup_init(struct inpcbinfo *, u_int, int);
void	in_pcbgroup_remove(struct inpcb *);
void	in_pcbgroup_update(struct inpcb *);
void	in_pcbgroup_update_mbuf(struct inpcb *, struct mbuf *);

void	in_pcbpurgeif0(struct inpcbinfo *, struct ifnet *);
int	in_pcballoc(struct socket *, struct inpcbinfo *);
int	in_pcbbind(struct inpcb *, struct sockaddr *, struct ucred *);
int	in_pcb_lport(struct inpcb *, struct in_addr *, u_short *,
	    struct ucred *, int);
int	in_pcbbind_setup(struct inpcb *, struct sockaddr *, in_addr_t *,
	    u_short *, struct ucred *);
int	in_pcbconnect(struct inpcb *, struct sockaddr *, struct ucred *);
int	in_pcbconnect_mbuf(struct inpcb *, struct sockaddr *, struct ucred *,
	    struct mbuf *);
int	in_pcbconnect_setup(struct inpcb *, struct sockaddr *, in_addr_t *,
	    u_short *, in_addr_t *, u_short *, struct inpcb **,
	    struct ucred *);
void	in_pcbdetach(struct inpcb *);
void	in_pcbdisconnect(struct inpcb *);
void	in_pcbdrop(struct inpcb *);
void	in_pcbfree(struct inpcb *);
int	in_pcbinshash(struct inpcb *);
int	in_pcbinshash_nopcbgroup(struct inpcb *);
int	in_pcbladdr(struct inpcb *, struct in_addr *, struct in_addr *,
	    struct ucred *);
struct inpcb *
	in_pcblookup_local(struct inpcbinfo *,
	    struct in_addr, u_short, int, struct ucred *);
struct inpcb *
	in_pcblookup(struct inpcbinfo *, struct in_addr, u_int,
	    struct in_addr, u_int, int, struct ifnet *);
struct inpcb *
	in_pcblookup_mbuf(struct inpcbinfo *, struct in_addr, u_int,
	    struct in_addr, u_int, int, struct ifnet *, struct mbuf *);
void	in_pcbnotifyall(struct inpcbinfo *pcbinfo, struct in_addr,
	    int, struct inpcb *(*)(struct inpcb *, int));
void	in_pcbref(struct inpcb *);
void	in_pcbrehash(struct inpcb *);
void	in_pcbrehash_mbuf(struct inpcb *, struct mbuf *);
int	in_pcbrele(struct inpcb *);
int	in_pcbrele_rlocked(struct inpcb *);
int	in_pcbrele_wlocked(struct inpcb *);
void	in_pcblist_rele_rlocked(epoch_context_t ctx);
void	in_losing(struct inpcb *);
void	in_pcbsetsolabel(struct socket *so);
int	in_getpeeraddr(struct socket *so, struct sockaddr **nam);
int	in_getsockaddr(struct socket *so, struct sockaddr **nam);
struct sockaddr *
	in_sockaddr(in_port_t port, struct in_addr *addr);
void	in_pcbsosetlabel(struct socket *so);
#ifdef RATELIMIT
int	in_pcbattach_txrtlmt(struct inpcb *, struct ifnet *, uint32_t, uint32_t, uint32_t);
void	in_pcbdetach_txrtlmt(struct inpcb *);
int	in_pcbmodify_txrtlmt(struct inpcb *, uint32_t);
int	in_pcbquery_txrtlmt(struct inpcb *, uint32_t *);
int	in_pcbquery_txrlevel(struct inpcb *, uint32_t *);
void	in_pcboutput_txrtlmt(struct inpcb *, struct ifnet *, struct mbuf *);
void	in_pcboutput_eagain(struct inpcb *);
#endif
#endif /* _KERNEL */

#endif /* !_NETINET_IN_PCB_H_ */
