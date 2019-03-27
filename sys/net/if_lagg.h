/*	$OpenBSD: if_trunk.h,v 1.11 2007/01/31 06:20:19 reyk Exp $	*/

/*
 * Copyright (c) 2005, 2006 Reyk Floeter <reyk@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $FreeBSD$
 */

#ifndef _NET_LAGG_H
#define _NET_LAGG_H

/*
 * Global definitions
 */

#define	LAGG_MAX_PORTS		32	/* logically */
#define	LAGG_MAX_NAMESIZE	32	/* name of a protocol */
#define	LAGG_MAX_STACKING	4	/* maximum number of stacked laggs */

/* Lagg flags */
#define	LAGG_F_HASHL2		0x00000001	/* hash layer 2 */
#define	LAGG_F_HASHL3		0x00000002	/* hash layer 3 */
#define	LAGG_F_HASHL4		0x00000004	/* hash layer 4 */
#define	LAGG_F_HASHMASK		0x00000007

/* Port flags */
#define	LAGG_PORT_SLAVE		0x00000000	/* normal enslaved port */
#define	LAGG_PORT_MASTER	0x00000001	/* primary port */
#define	LAGG_PORT_STACK		0x00000002	/* stacked lagg port */
#define	LAGG_PORT_ACTIVE	0x00000004	/* port is active */
#define	LAGG_PORT_COLLECTING	0x00000008	/* port is receiving frames */
#define	LAGG_PORT_DISTRIBUTING	0x00000010	/* port is sending frames */
#define	LAGG_PORT_BITS		"\20\01MASTER\02STACK\03ACTIVE\04COLLECTING" \
				  "\05DISTRIBUTING"

/* Supported lagg PROTOs */
typedef enum {
	LAGG_PROTO_NONE = 0,	/* no lagg protocol defined */
	LAGG_PROTO_ROUNDROBIN,	/* simple round robin */
	LAGG_PROTO_FAILOVER,	/* active failover */
	LAGG_PROTO_LOADBALANCE,	/* loadbalance */
	LAGG_PROTO_LACP,	/* 802.3ad lacp */
	LAGG_PROTO_BROADCAST,	/* broadcast */
	LAGG_PROTO_MAX,
} lagg_proto;

struct lagg_protos {
	const char		*lpr_name;
	lagg_proto		lpr_proto;
};

#define	LAGG_PROTO_DEFAULT	LAGG_PROTO_FAILOVER
#define LAGG_PROTOS	{						\
	{ "failover",		LAGG_PROTO_FAILOVER },		\
	{ "lacp",		LAGG_PROTO_LACP },			\
	{ "loadbalance",	LAGG_PROTO_LOADBALANCE },		\
	{ "roundrobin",	LAGG_PROTO_ROUNDROBIN },		\
	{ "broadcast",	LAGG_PROTO_BROADCAST },		\
	{ "none",		LAGG_PROTO_NONE },			\
	{ "default",		LAGG_PROTO_DEFAULT }			\
}

/*
 * lagg ioctls.
 */

/*
 * LACP current operational parameters structure.
 */
struct lacp_opreq {
	uint16_t		actor_prio;
	uint8_t			actor_mac[ETHER_ADDR_LEN];
	uint16_t		actor_key;
	uint16_t		actor_portprio;
	uint16_t		actor_portno;
	uint8_t			actor_state;
	uint16_t		partner_prio;
	uint8_t			partner_mac[ETHER_ADDR_LEN];
	uint16_t		partner_key;
	uint16_t		partner_portprio;
	uint16_t		partner_portno;
	uint8_t			partner_state;
};

/* lagg port settings */
struct lagg_reqport {
	char			rp_ifname[IFNAMSIZ];	/* name of the lagg */
	char			rp_portname[IFNAMSIZ];	/* name of the port */
	u_int32_t		rp_prio;		/* port priority */
	u_int32_t		rp_flags;		/* port flags */
	union {
		struct lacp_opreq rpsc_lacp;
	} rp_psc;
#define rp_lacpreq	rp_psc.rpsc_lacp
};

#define	SIOCGLAGGPORT		_IOWR('i', 140, struct lagg_reqport)
#define	SIOCSLAGGPORT		 _IOW('i', 141, struct lagg_reqport)
#define	SIOCSLAGGDELPORT	 _IOW('i', 142, struct lagg_reqport)

/* lagg, ports and options */
struct lagg_reqall {
	char			ra_ifname[IFNAMSIZ];	/* name of the lagg */
	u_int			ra_proto;		/* lagg protocol */

	size_t			ra_size;		/* size of buffer */
	struct lagg_reqport	*ra_port;		/* allocated buffer */
	int			ra_ports;		/* total port count */
	union {
		struct lacp_opreq rpsc_lacp;
	} ra_psc;
#define ra_lacpreq	ra_psc.rpsc_lacp
};

#define	SIOCGLAGG		_IOWR('i', 143, struct lagg_reqall)
#define	SIOCSLAGG		 _IOW('i', 144, struct lagg_reqall)

struct lagg_reqflags {
	char			rf_ifname[IFNAMSIZ];	/* name of the lagg */
	uint32_t		rf_flags;		/* lagg protocol */
};

#define	SIOCGLAGGFLAGS		_IOWR('i', 145, struct lagg_reqflags)
#define	SIOCSLAGGHASH		 _IOW('i', 146, struct lagg_reqflags)

struct lagg_reqopts {
	char			ro_ifname[IFNAMSIZ];	/* name of the lagg */

	int			ro_opts;		/* Option bitmap */
#define	LAGG_OPT_NONE			0x00
#define	LAGG_OPT_USE_FLOWID		0x01		/* enable use of flowid */
/* Pseudo flags which are used in ro_opts but not stored into sc_opts. */
#define	LAGG_OPT_FLOWIDSHIFT		0x02		/* set flowid shift */
#define	LAGG_OPT_FLOWIDSHIFT_MASK	0x1f		/* flowid is uint32_t */
#define	LAGG_OPT_LACP_STRICT		0x10		/* LACP strict mode */
#define	LAGG_OPT_LACP_TXTEST		0x20		/* LACP debug: txtest */
#define	LAGG_OPT_LACP_RXTEST		0x40		/* LACP debug: rxtest */
#define	LAGG_OPT_LACP_TIMEOUT		0x80		/* LACP timeout */
	u_int			ro_count;		/* number of ports */
	u_int			ro_active;		/* active port count */
	u_int			ro_flapping;		/* number of flapping */
	int			ro_flowid_shift;	/* shift the flowid */
	uint32_t		ro_bkt;			/* packet bucket for roundrobin */
};

#define	SIOCGLAGGOPTS		_IOWR('i', 152, struct lagg_reqopts)
#define	SIOCSLAGGOPTS		 _IOW('i', 153, struct lagg_reqopts)

#define	LAGG_OPT_BITS		"\020\001USE_FLOWID\005LACP_STRICT" \
				"\006LACP_TXTEST\007LACP_RXTEST"

#ifdef _KERNEL

/*
 * Internal kernel part
 */

#define	LAGG_PORTACTIVE(_tp)	(					\
	((_tp)->lp_ifp->if_link_state == LINK_STATE_UP) &&		\
	((_tp)->lp_ifp->if_flags & IFF_UP)				\
)

struct lagg_ifreq {
	union {
		struct ifreq ifreq;
		struct {
			char ifr_name[IFNAMSIZ];
			struct sockaddr_storage ifr_ss;
		} ifreq_storage;
	} ifreq;
};

#define	sc_ifflags		sc_ifp->if_flags		/* flags */
#define	sc_ifname		sc_ifp->if_xname		/* name */

/* Private data used by the loadbalancing protocol */
struct lagg_lb {
	u_int32_t		lb_key;
	struct lagg_port	*lb_ports[LAGG_MAX_PORTS];
};

struct lagg_mc {
	struct sockaddr_dl	mc_addr;
	struct ifmultiaddr      *mc_ifma;
	SLIST_ENTRY(lagg_mc)	mc_entries;
};

struct lagg_counters {
	uint64_t	val[IFCOUNTERS];
};

struct lagg_softc {
	struct ifnet			*sc_ifp;	/* virtual interface */
	struct rmlock			sc_mtx;
	struct sx			sc_sx;
	int				sc_proto;	/* lagg protocol */
	u_int				sc_count;	/* number of ports */
	u_int				sc_active;	/* active port count */
	u_int				sc_flapping;	/* number of flapping
							 * events */
	struct lagg_port		*sc_primary;	/* primary port */
	struct ifmedia			sc_media;	/* media config */
	void				*sc_psc;	/* protocol data */
	uint32_t			sc_seq;		/* sequence counter */
	uint32_t			sc_flags;
	int				sc_destroying;	/* destroying lagg */

	CK_SLIST_HEAD(__tplhd, lagg_port)	sc_ports;	/* list of interfaces */
	SLIST_ENTRY(lagg_softc)	sc_entries;

	eventhandler_tag vlan_attach;
	eventhandler_tag vlan_detach;
	struct callout			sc_callout;
	u_int				sc_opts;
	int				flowid_shift;	/* shift the flowid */
	uint32_t			sc_bkt;		/* packates bucket for roundrobin */
	uint32_t			sc_bkt_count;	/* packates bucket count for roundrobin */
	struct lagg_counters		detached_counters; /* detached ports sum */
};

struct lagg_port {
	struct ifnet			*lp_ifp;	/* physical interface */
	struct lagg_softc		*lp_softc;	/* parent lagg */
	uint8_t				lp_lladdr[ETHER_ADDR_LEN];

	u_char				lp_iftype;	/* interface type */
	uint32_t			lp_prio;	/* port priority */
	uint32_t			lp_flags;	/* port flags */
	int				lp_ifflags;	/* saved ifp flags */
	int				lp_ifcapenable;	/* saved ifp capenable */
	void				*lh_cookie;	/* if state hook */
	void				*lp_psc;	/* protocol data */
	int				lp_detaching;	/* ifnet is detaching */
	SLIST_HEAD(__mclhd, lagg_mc)	lp_mc_head;	/* multicast addresses */

	/* Redirected callbacks */
	int	(*lp_ioctl)(struct ifnet *, u_long, caddr_t);
	int	(*lp_output)(struct ifnet *, struct mbuf *,
		     const struct sockaddr *, struct route *);
	struct lagg_counters		port_counters;	/* ifp counters copy */

	CK_SLIST_ENTRY(lagg_port)		lp_entries;
	struct epoch_context	lp_epoch_ctx;
};

extern struct mbuf *(*lagg_input_p)(struct ifnet *, struct mbuf *);
extern void	(*lagg_linkstate_p)(struct ifnet *, int );

int		lagg_enqueue(struct ifnet *, struct mbuf *);

SYSCTL_DECL(_net_link_lagg);

#endif /* _KERNEL */

#endif /* _NET_LAGG_H */
