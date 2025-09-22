/*	$OpenBSD: if_var.h,v 1.139 2025/07/19 16:40:40 mvs Exp $	*/
/*	$NetBSD: if.h,v 1.23 1996/05/07 02:40:27 thorpej Exp $	*/

/*
 * Copyright (c) 2012-2013 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 1982, 1986, 1989, 1993
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
 *	@(#)if.h	8.1 (Berkeley) 6/10/93
 */

#ifndef _NET_IF_VAR_H_
#define _NET_IF_VAR_H_

#ifdef _KERNEL

#include <sys/queue.h>
#include <sys/mbuf.h>
#include <sys/srp.h>
#include <sys/refcnt.h>
#include <sys/task.h>
#include <sys/timeout.h>

#include <net/ifq.h>
#include <net/route.h>

/*
 * Structures defining a network interface, providing a packet
 * transport mechanism (ala level 0 of the PUP protocols).
 *
 * Each interface accepts output datagrams of a specified maximum
 * length, and provides higher level routines with input datagrams
 * received from its medium.
 *
 * Output occurs when the routine if_output is called, with four parameters:
 *	(*ifp->if_output)(ifp, m, dst, rt)
 * Here m is the mbuf chain to be sent and dst is the destination address.
 * The output routine encapsulates the supplied datagram if necessary,
 * and then transmits it on its medium.
 *
 * On input, each interface unwraps the data received by it, and either
 * places it on the input queue of an internetwork datagram routine
 * and posts the associated software interrupt, or passes the datagram to a raw
 * packet input routine.
 *
 * Routines exist for locating interfaces by their addresses
 * or for locating an interface on a certain network, as well as more general
 * routing and gateway routines maintaining information used to locate
 * interfaces.  These routines live in the files if.c and route.c
 */

/*
 *  Locks used to protect struct members in this file:
 *	I	immutable after creation
 *	d	protection left to the driver
 *	c	only used in ioctl or routing socket contexts (kernel lock)
 *	K	kernel lock
 *	N	net lock
 *	T	if_tmplist_lock
 *
 *  For SRP related structures that allow lock-free reads, the write lock
 *  is indicated below.
 */

struct rtentry;
struct ifnet;
struct task;
struct cpumem;

struct netstack {
	struct route		ns_route;
	struct mbuf_list	ns_tcp_ml;
	struct mbuf_list	ns_tcp6_ml;
};

/*
 * Structure describing a `cloning' interface.
 */
struct if_clone {
	LIST_ENTRY(if_clone)	 ifc_list;	/* [I] on list of cloners */
	const char		*ifc_name;	/* name of device, e.g. `gif' */
	size_t			 ifc_namelen;	/* length of name */

	int			(*ifc_create)(struct if_clone *, int);
	int			(*ifc_destroy)(struct ifnet *);
};

#define	IF_CLONE_INITIALIZER(name, create, destroy)			\
{									\
  .ifc_list	= { NULL, NULL },					\
  .ifc_name	= name,							\
  .ifc_namelen	= sizeof(name) - 1,					\
  .ifc_create	= create,						\
  .ifc_destroy	= destroy,						\
}

enum if_counters {
	ifc_ipackets,		/* packets received on interface */
	ifc_ierrors,		/* input errors on interface */
	ifc_opackets,		/* packets sent on interface */
	ifc_oerrors,		/* output errors on interface */
	ifc_collisions,		/* collisions on csma interfaces */
	ifc_ibytes,		/* total number of octets received */
	ifc_obytes,		/* total number of octets sent */
	ifc_imcasts,		/* packets received via multicast */
	ifc_omcasts,		/* packets sent via multicast */
	ifc_iqdrops,		/* dropped on input, this interface */
	ifc_oqdrops,		/* dropped on output, this interface */
	ifc_noproto,		/* destined for unsupported protocol */

	ifc_ncounters
};

/*
 * Structure defining a queue for a network interface.
 *
 * (Would like to call this struct ``if'', but C isn't PL/1.)
 */
TAILQ_HEAD(ifnet_head, ifnet);		/* the actual queue head */

struct ifnet {				/* and the entries */
	void	*if_softc;		/* [I] lower-level data for this if */
	struct	refcnt if_refcnt;
	TAILQ_ENTRY(ifnet) if_list;	/* [NK] all struct ifnets are chained */
	TAILQ_ENTRY(ifnet) if_tmplist;	/* [T] temporary list */
	TAILQ_HEAD(, ifaddr) if_addrlist; /* [N] list of addresses per if */
	TAILQ_HEAD(, ifmaddr) if_maddrlist; /* [N] list of multicast records */
	TAILQ_HEAD(, ifg_list) if_groups; /* [N] list of groups per if */
	struct task_list if_addrhooks;	/* [I] address change callbacks */
	struct task_list if_linkstatehooks; /* [I] link change callbacks*/
	struct task_list if_detachhooks; /* [I] detach callbacks */
				/* [I] check or clean routes (+ or -)'d */
	void	(*if_rtrequest)(struct ifnet *, int, struct rtentry *);
	char	if_xname[IFNAMSIZ];	/* [I] external name (name + unit) */
	int	if_pcount;		/* [N] # of promiscuous listeners */
	unsigned int if_bridgeidx;	/* [K] used by bridge ports */
	caddr_t	if_bpf;			/* packet filter structure */
	caddr_t if_mcast;		/* used by multicast code */
	caddr_t if_mcast6;		/* used by IPv6 multicast code */
	caddr_t	if_pf_kif;		/* pf interface abstraction */
	union {
		struct srpl carp_s;	/* carp if list (used by !carp ifs) */
		unsigned int carp_idx;	/* index of carpdev (used by carp
						ifs) */
	} if_carp_ptr;
#define if_carp		if_carp_ptr.carp_s
#define if_carpdevidx	if_carp_ptr.carp_idx
	unsigned int if_index;		/* [I] unique index for this if */
	short	if_timer;		/* time 'til if_watchdog called */
	unsigned short if_flags;	/* [N] up/down, broadcast, etc. */
	int	if_xflags;		/* [N] extra softnet flags */

	/* Stats and other data about if. Should be in sync with if_data. */
	u_char if_type;
	u_char if_addrlen;
	u_char if_hdrlen;
	u_char if_link_state;
	uint32_t if_mtu;
	uint32_t if_metric;
	uint64_t if_baudrate;
	uint32_t if_capabilities;
	uint32_t if_rdomain;
	struct  timeval if_lastchange;	/* [c] last op. state change */
	uint64_t if_data_counters[ifc_ncounters];

	struct	cpumem *if_counters;	/* per cpu stats */
	uint32_t if_hardmtu;		/* [d] maximum MTU device supports */
	char	if_description[IFDESCRSIZE]; /* [c] interface description */
	u_short	if_rtlabelid;		/* [c] next route label */
	uint8_t if_priority;		/* [c] route priority offset */
	uint8_t if_llprio;		/* [N] link layer priority */
	struct	timeout if_slowtimo;	/* [I] watchdog timeout */
	struct	task if_watchdogtask;	/* [I] watchdog task */
	struct	task if_linkstatetask;	/* [I] task to do route updates */

	/* procedure handles */
	void	(*if_input)(struct ifnet *, struct mbuf *, struct netstack *);
	int	(*if_bpf_mtap)(caddr_t, const struct mbuf *, u_int);
	int	(*if_output)(struct ifnet *, struct mbuf *, struct sockaddr *,
		     struct rtentry *);	/* output routine (enqueue) */
					/* link level output function */
	int	(*if_ll_output)(struct ifnet *, struct mbuf *,
		    struct sockaddr *, struct rtentry *);
	int	(*if_enqueue)(struct ifnet *, struct mbuf *);
	void	(*if_start)(struct ifnet *);	/* initiate output */
	int	(*if_ioctl)(struct ifnet *, u_long, caddr_t); /* ioctl hook */
	void	(*if_watchdog)(struct ifnet *);	/* timer routine */
	int	(*if_wol)(struct ifnet *, int);	/* WoL routine **/

	/* queues */
	struct	ifqueue if_snd;		/* transmit queue */
	struct	ifqueue **if_ifqs;	/* [I] pointer to an array of sndqs */
	void	(*if_qstart)(struct ifqueue *);
	unsigned int if_nifqs;		/* [I] number of output queues */
	unsigned int if_txmit;		/* [c] txmitigation amount */

	struct	ifiqueue if_rcv;	/* rx/input queue */
	struct	ifiqueue **if_iqs;	/* [I] pointer to the array of iqs */
	unsigned int if_niqs;		/* [I] number of input queues */

	struct sockaddr_dl *if_sadl;	/* [N] pointer to our sockaddr_dl */

	struct	nd_ifinfo *if_nd;	/* [I] IPv6 Neighbor Discovery info */
};

#define if_ipackets	if_data_counters[ifc_ipackets]
#define if_ierrors	if_data_counters[ifc_ierrors]
#define if_opackets	if_data_counters[ifc_opackets]
#define if_oerrors	if_data_counters[ifc_oerrors]
#define if_collisions	if_data_counters[ifc_collisions]
#define if_ibytes	if_data_counters[ifc_ibytes]
#define if_obytes	if_data_counters[ifc_obytes]
#define if_imcasts	if_data_counters[ifc_imcasts]
#define if_omcasts	if_data_counters[ifc_omcasts]
#define if_iqdrops	if_data_counters[ifc_iqdrops]
#define if_oqdrops	if_data_counters[ifc_oqdrops]
#define if_noproto	if_data_counters[ifc_noproto]

/*
 * The ifaddr structure contains information about one address
 * of an interface.  They are maintained by the different address families,
 * are allocated and attached when an address is set, and are linked
 * together so all addresses for an interface can be located.
 */
struct ifaddr {
	struct	sockaddr *ifa_addr;	/* address of interface */
	struct	sockaddr *ifa_dstaddr;	/* other end of p-to-p link */
#define	ifa_broadaddr	ifa_dstaddr	/* broadcast address interface */
	struct	sockaddr *ifa_netmask;	/* used to determine subnet */
	struct	ifnet *ifa_ifp;		/* back-pointer to interface */
	TAILQ_ENTRY(ifaddr) ifa_list;	/* [N] list of addresses for
					    interface */
	u_int	ifa_flags;		/* interface flags, see below */
	struct	refcnt ifa_refcnt;	/* number of `rt_ifa` references */
	int	ifa_metric;		/* cost of going out this interface */
};

#define	IFA_ROUTE		0x01	/* Auto-magically installed route */

/*
 * Interface multicast address.
 */
struct ifmaddr {
	struct sockaddr		*ifma_addr;	/* Protocol address */
	unsigned int		 ifma_ifidx;	/* Index of the interface */
	struct refcnt		 ifma_refcnt;	/* Count of references */
	TAILQ_ENTRY(ifmaddr)	 ifma_list;	/* Per-interface list */
};

/*
 * interface groups
 */

struct ifg_group {
	char			 ifg_group[IFNAMSIZ]; /* [I] group name */
	u_int			 ifg_refcnt;  /* [N] group reference count */
	caddr_t			 ifg_pf_kif;  /* [I] pf interface group */
	int			 ifg_carp_demoted; /* [K] carp demotion counter */
	TAILQ_HEAD(, ifg_member) ifg_members; /* [N] list of members per group */
	TAILQ_ENTRY(ifg_group)	 ifg_next;    /* [N] all groups are chained */

	struct refcnt		 ifg_tmprefcnt;
	TAILQ_ENTRY(ifg_group)	 ifg_tmplist;   /* [T] temporary list */
};

struct ifg_member {
	TAILQ_ENTRY(ifg_member)	 ifgm_next; /* [N] all members are chained */
	struct ifnet		*ifgm_ifp;  /* [I] member interface */
};

struct ifg_list {
	struct ifg_group	*ifgl_group; /* [I] interface group */
	TAILQ_ENTRY(ifg_list)	 ifgl_next;  /* [N] all groups are chained */
};

#define	IFNET_SLOWTIMO	1		/* granularity is 1 second */

#define IF_TXMIT_MIN			1
#define IF_TXMIT_DEFAULT		16

/* default interface priorities */
#define IF_WIRED_DEFAULT_PRIORITY	0
#define IF_WIRELESS_DEFAULT_PRIORITY	4
#define IF_WWAN_DEFAULT_PRIORITY	6
#define IF_CARP_DEFAULT_PRIORITY	15

/*
 * Network stack input queues.
 */
struct	niqueue {
	struct mbuf_queue	ni_q;
	u_int			ni_isr;
};

#define NIQUEUE_INITIALIZER(_len, _isr) \
    { MBUF_QUEUE_INITIALIZER((_len), IPL_NET), (_isr) }

int		niq_enqueue(struct niqueue *, struct mbuf *);

#define niq_dequeue(_q)			mq_dequeue(&(_q)->ni_q)
#define niq_delist(_q, _ml)		mq_delist(&(_q)->ni_q, (_ml))
#define niq_len(_q)			mq_len(&(_q)->ni_q)
#define niq_drops(_q)			mq_drops(&(_q)->ni_q)
#define sysctl_niq(_n, _l, _op, _olp, _np, _nl, _niq) \
    sysctl_mq((_n), (_l), (_op), (_olp), (_np), (_nl), &(_niq)->ni_q)

extern struct rwlock if_tmplist_lock;
extern struct ifnet_head ifnetlist;

void	if_start(struct ifnet *);
int	if_enqueue(struct ifnet *, struct mbuf *);
int	if_enqueue_ifq(struct ifnet *, struct mbuf *);
void	if_input(struct ifnet *, struct mbuf_list *);
void	if_vinput(struct ifnet *, struct mbuf *, struct netstack *);
void	if_input_process(struct ifnet *, struct mbuf_list *, unsigned int);
int	if_input_local(struct ifnet *, struct mbuf *, sa_family_t,
	    struct netstack *);
int	if_output_ml(struct ifnet *, struct mbuf_list *,
	    struct sockaddr *, struct rtentry *);
int	if_output_mq(struct ifnet *, struct mbuf_queue *, unsigned int *,
	    struct sockaddr *, struct rtentry *);
int	if_output_tso(struct ifnet *, struct mbuf **, struct sockaddr *,
	    struct rtentry *, u_int);
int	if_output_local(struct ifnet *, struct mbuf *, sa_family_t);
void	if_rtrequest_dummy(struct ifnet *, int, struct rtentry *);
void	p2p_rtrequest(struct ifnet *, int, struct rtentry *);
void	p2p_input(struct ifnet *, struct mbuf *, struct netstack *);
int	p2p_bpf_mtap(caddr_t, const struct mbuf *, u_int);

struct	ifaddr *ifa_ifwithaddr(const struct sockaddr *, u_int);
struct	ifaddr *ifa_ifwithdstaddr(const struct sockaddr *, u_int);
struct	ifaddr *ifaof_ifpforaddr(const struct sockaddr *, struct ifnet *);
struct	ifaddr *ifaref(struct ifaddr *);
void	ifafree(struct ifaddr *);

int	if_isconnected(const struct ifnet *, unsigned int);

void	if_clone_attach(struct if_clone *);

int	if_clone_create(const char *, int);
int	if_clone_destroy(const char *);

struct if_clone *
	if_clone_lookup(const char *, int *);

void	ifa_add(struct ifnet *, struct ifaddr *);
void	ifa_del(struct ifnet *, struct ifaddr *);
void	ifa_update_broadaddr(struct ifnet *, struct ifaddr *,
	    struct sockaddr *);

void	if_addrhook_add(struct ifnet *, struct task *);
void	if_addrhook_del(struct ifnet *, struct task *);
void	if_addrhooks_run(struct ifnet *);
void	if_linkstatehook_add(struct ifnet *, struct task *);
void	if_linkstatehook_del(struct ifnet *, struct task *);
void	if_detachhook_add(struct ifnet *, struct task *);
void	if_detachhook_del(struct ifnet *, struct task *);

void	if_rxr_livelocked(struct if_rxring *);
void	if_rxr_init(struct if_rxring *, u_int, u_int);
u_int	if_rxr_get(struct if_rxring *, u_int);

#define if_rxr_put(_r, _c)	do { (_r)->rxr_alive -= (_c); } while (0)
#define if_rxr_needrefill(_r)	((_r)->rxr_alive < (_r)->rxr_lwm)
#define if_rxr_inuse(_r)	((_r)->rxr_alive)
#define if_rxr_cwm(_r)		((_r)->rxr_cwm)

int	if_rxr_info_ioctl(struct if_rxrinfo *, u_int, struct if_rxring_info *);
int	if_rxr_ioctl(struct if_rxrinfo *, const char *, u_int,
	    struct if_rxring *);

void	if_counters_alloc(struct ifnet *);
void	if_counters_free(struct ifnet *);

int	if_txhprio_l2_check(int);
int	if_txhprio_l3_check(int);
int	if_rxhprio_l2_check(int);
int	if_rxhprio_l3_check(int);

#endif /* _KERNEL */

#endif /* _NET_IF_VAR_H_ */
