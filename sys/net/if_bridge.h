/*	$OpenBSD: if_bridge.h,v 1.73 2021/11/11 10:03:10 claudio Exp $	*/

/*
 * Copyright (c) 1999, 2000 Jason L. Wright (jason@thought.net)
 * Copyright (c) 2006 Andrew Thompson (thompsa@FreeBSD.org)
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 *
 */

#ifndef _NET_IF_BRIDGE_H_
#define _NET_IF_BRIDGE_H_

#include <sys/smr.h>
#include <sys/timeout.h>
#include <net/pfvar.h>

/*
 * Bridge control request: add/delete member interfaces.
 */
struct ifbreq {
	char		ifbr_name[IFNAMSIZ];	/* bridge ifs name */
	char		ifbr_ifsname[IFNAMSIZ];	/* member ifs name */
	u_int32_t	ifbr_ifsflags;		/* member ifs flags */
	u_int32_t	ifbr_portno;		/* member port number */
	u_int32_t	ifbr_protected;		/* protected domains */

	u_int8_t	ifbr_state;		/* member stp state */
	u_int8_t	ifbr_priority;		/* member stp priority */
	u_int32_t	ifbr_path_cost;		/* member stp path cost */
	u_int32_t	ifbr_stpflags;          /* member stp flags */
	u_int8_t	ifbr_proto;		/* member stp protocol */
	u_int8_t	ifbr_role;		/* member stp role */
	u_int32_t	ifbr_fwd_trans;		/* member stp fwd transitions */
	u_int64_t	ifbr_desg_bridge;	/* member stp designated bridge */
	u_int32_t	ifbr_desg_port;		/* member stp designated port */
	u_int64_t	ifbr_root_bridge;	/* member stp root bridge */
	u_int32_t	ifbr_root_cost;		/* member stp root cost */
	u_int32_t	ifbr_root_port;		/* member stp root port */
};

/* SIOCBRDGIFFLGS, SIOCBRDGIFFLGS */
#define	IFBIF_LEARNING		0x0001	/* ifs can learn */
#define	IFBIF_DISCOVER		0x0002	/* ifs sends packets w/unknown dest */
#define	IFBIF_BLOCKNONIP	0x0004	/* ifs blocks non-IP/ARP in/out */
#define	IFBIF_STP		0x0008	/* ifs participates in spanning tree */
#define IFBIF_BSTP_EDGE		0x0010	/* member stp edge port */
#define IFBIF_BSTP_AUTOEDGE	0x0020  /* member stp autoedge enabled */
#define IFBIF_BSTP_PTP		0x0040  /* member stp ptp */
#define IFBIF_BSTP_AUTOPTP	0x0080	/* member stp autoptp enabled */
#define	IFBIF_SPAN		0x0100	/* ifs is a span port (ro) */
#define	IFBIF_LOCAL		0x1000	/* local port in switch(4) */
#define	IFBIF_RO_MASK		0x0f00	/* read only bits */

/* SIOCBRDGFLUSH */
#define	IFBF_FLUSHDYN	0x0	/* flush dynamic addresses only */
#define	IFBF_FLUSHALL	0x1	/* flush all addresses from cache */

/* port states */
#define	BSTP_IFSTATE_DISABLED	0
#define	BSTP_IFSTATE_LISTENING	1
#define	BSTP_IFSTATE_LEARNING	2
#define	BSTP_IFSTATE_FORWARDING	3
#define	BSTP_IFSTATE_BLOCKING	4
#define	BSTP_IFSTATE_DISCARDING	5

#define	BSTP_TCSTATE_ACTIVE	1
#define	BSTP_TCSTATE_DETECTED	2
#define	BSTP_TCSTATE_INACTIVE	3
#define	BSTP_TCSTATE_LEARNING	4
#define	BSTP_TCSTATE_PROPAG	5
#define	BSTP_TCSTATE_ACK	6
#define	BSTP_TCSTATE_TC		7
#define	BSTP_TCSTATE_TCN	8

#define	BSTP_ROLE_DISABLED	0
#define	BSTP_ROLE_ROOT		1
#define	BSTP_ROLE_DESIGNATED	2
#define	BSTP_ROLE_ALTERNATE	3
#define	BSTP_ROLE_BACKUP	4

/*
 * Interface list structure
 */
struct ifbifconf {
	char		ifbic_name[IFNAMSIZ];	/* bridge ifs name */
	u_int32_t	ifbic_len;		/* buffer size */
	union {
		caddr_t	ifbicu_buf;
		struct	ifbreq *ifbicu_req;
	} ifbic_ifbicu;
#define	ifbic_buf	ifbic_ifbicu.ifbicu_buf
#define	ifbic_req	ifbic_ifbicu.ifbicu_req
};

/*
 * Bridge address request
 */
struct ifbareq {
	char			ifba_name[IFNAMSIZ];	/* bridge name */
	char			ifba_ifsname[IFNAMSIZ];	/* destination ifs */
	u_int8_t		ifba_age;		/* address age */
	u_int8_t		ifba_flags;		/* address flags */
	struct ether_addr	ifba_dst;		/* destination addr */
	struct sockaddr_storage	ifba_dstsa;		/* tunnel endpoint */
};

#define	IFBAF_TYPEMASK		0x03		/* address type mask */
#define	IFBAF_DYNAMIC		0x00		/* dynamically learned */
#define	IFBAF_STATIC		0x01		/* static address */

struct ifbaconf {
	char			ifbac_name[IFNAMSIZ];	/* bridge ifs name */
	u_int32_t		ifbac_len;		/* buffer size */
	union {
		caddr_t	ifbacu_buf;			/* buffer */
		struct ifbareq *ifbacu_req;		/* request pointer */
	} ifbac_ifbacu;
#define	ifbac_buf	ifbac_ifbacu.ifbacu_buf
#define	ifbac_req	ifbac_ifbacu.ifbacu_req
};

struct ifbrparam {
	char			ifbrp_name[IFNAMSIZ];
	union {
		u_int32_t	ifbrpu_csize;		/* cache size */
		int		ifbrpu_ctime;		/* cache time (sec) */
		u_int16_t	ifbrpu_prio;		/* bridge priority */
		u_int8_t	ifbrpu_hellotime;	/* hello time (sec) */
		u_int8_t	ifbrpu_fwddelay;	/* fwd delay (sec) */
		u_int8_t	ifbrpu_maxage;		/* max age (sec) */
		u_int8_t	ifbrpu_proto;		/* bridge protocol */
		u_int8_t	ifbrpu_txhc;		/* bpdu tx holdcount */
	} ifbrp_ifbrpu;
};
#define	ifbrp_csize	ifbrp_ifbrpu.ifbrpu_csize
#define	ifbrp_ctime	ifbrp_ifbrpu.ifbrpu_ctime
#define	ifbrp_prio	ifbrp_ifbrpu.ifbrpu_prio
#define	ifbrp_proto	ifbrp_ifbrpu.ifbrpu_proto
#define	ifbrp_txhc	ifbrp_ifbrpu.ifbrpu_txhc
#define	ifbrp_hellotime	ifbrp_ifbrpu.ifbrpu_hellotime
#define	ifbrp_fwddelay	ifbrp_ifbrpu.ifbrpu_fwddelay
#define	ifbrp_maxage	ifbrp_ifbrpu.ifbrpu_maxage

/* Protocol versions */
#define	BSTP_PROTO_ID		0x00
#define	BSTP_PROTO_STP		0x00
#define	BSTP_PROTO_RSTP		0x02
#define	BSTP_PROTO_MAX		BSTP_PROTO_RSTP

/*
 * Bridge current operational parameters structure.
 */
struct ifbropreq {
	char		ifbop_name[IFNAMSIZ];
	u_int8_t	ifbop_holdcount;
	u_int8_t	ifbop_maxage;
	u_int8_t	ifbop_hellotime;
	u_int8_t	ifbop_fwddelay;
	u_int8_t	ifbop_protocol;
	u_int16_t	ifbop_priority;
	u_int64_t	ifbop_root_bridge;
	u_int16_t	ifbop_root_port;
	u_int32_t	ifbop_root_path_cost;
	u_int64_t	ifbop_desg_bridge;
	struct timeval	ifbop_last_tc_time;
};

/*
 * Bridge mac rules
 */
struct ifbrarpf {
	u_int16_t		brla_flags;
	u_int16_t		brla_op;
	struct ether_addr	brla_sha;
	struct in_addr		brla_spa;
	struct ether_addr	brla_tha;
	struct in_addr		brla_tpa;
};
#define	BRLA_ARP	0x01
#define	BRLA_RARP	0x02
#define	BRLA_SHA	0x10
#define	BRLA_SPA	0x20
#define	BRLA_THA	0x40
#define	BRLA_TPA	0x80

struct ifbrlreq {
	char			ifbr_name[IFNAMSIZ];	/* bridge ifs name */
	char			ifbr_ifsname[IFNAMSIZ];	/* member ifs name */
	u_int8_t		ifbr_action;		/* disposition */
	u_int8_t		ifbr_flags;		/* flags */
	struct ether_addr	ifbr_src;		/* source mac */
	struct ether_addr	ifbr_dst;		/* destination mac */
	char			ifbr_tagname[PF_TAG_NAME_SIZE];	/* pf tagname */
	struct ifbrarpf		ifbr_arpf;		/* arp filter */
};
#define	BRL_ACTION_BLOCK	0x01			/* block frame */
#define	BRL_ACTION_PASS		0x02			/* pass frame */
#define	BRL_FLAG_IN		0x08			/* input rule */
#define	BRL_FLAG_OUT		0x04			/* output rule */
#define	BRL_FLAG_SRCVALID	0x02			/* src valid */
#define	BRL_FLAG_DSTVALID	0x01			/* dst valid */

struct ifbrlconf {
	char		ifbrl_name[IFNAMSIZ];	/* bridge ifs name */
	char		ifbrl_ifsname[IFNAMSIZ];/* member ifs name */
	u_int32_t	ifbrl_len;		/* buffer size */
	union {
		caddr_t	ifbrlu_buf;
		struct	ifbrlreq *ifbrlu_req;
	} ifbrl_ifbrlu;
#define	ifbrl_buf	ifbrl_ifbrlu.ifbrlu_buf
#define	ifbrl_req	ifbrl_ifbrlu.ifbrlu_req
};

#ifdef _KERNEL

#include <sys/mutex.h>

/* STP port flags */
#define	BSTP_PORT_CANMIGRATE	0x0001
#define	BSTP_PORT_NEWINFO	0x0002
#define	BSTP_PORT_DISPUTED	0x0004
#define	BSTP_PORT_ADMCOST	0x0008
#define	BSTP_PORT_AUTOEDGE	0x0010
#define	BSTP_PORT_AUTOPTP	0x0020

/* BPDU priority */
#define	BSTP_PDU_SUPERIOR	1
#define	BSTP_PDU_REPEATED	2
#define	BSTP_PDU_INFERIOR	3
#define	BSTP_PDU_INFERIORALT	4
#define	BSTP_PDU_OTHER		5

/* BPDU flags */
#define	BSTP_PDU_PRMASK		0x0c		/* Port Role */
#define	BSTP_PDU_PRSHIFT	2		/* Port Role offset */
#define	BSTP_PDU_F_UNKN		0x00		/* Unknown port    (00) */
#define	BSTP_PDU_F_ALT		0x01		/* Alt/Backup port (01) */
#define	BSTP_PDU_F_ROOT		0x02		/* Root port       (10) */
#define	BSTP_PDU_F_DESG		0x03		/* Designated port (11) */

#define	BSTP_PDU_STPMASK	0x81		/* strip unused STP flags */
#define	BSTP_PDU_RSTPMASK	0x7f		/* strip unused RSTP flags */
#define	BSTP_PDU_F_TC		0x01		/* Topology change */
#define	BSTP_PDU_F_P		0x02		/* Proposal flag */
#define	BSTP_PDU_F_L		0x10		/* Learning flag */
#define	BSTP_PDU_F_F		0x20		/* Forwarding flag */
#define	BSTP_PDU_F_A		0x40		/* Agreement flag */
#define	BSTP_PDU_F_TCA		0x80		/* Topology change ack */

/*
 * Bridge filtering rules
 */
SIMPLEQ_HEAD(brl_head, brl_node);

struct brl_node {
	SIMPLEQ_ENTRY(brl_node)	brl_next;	/* next rule */
	struct ether_addr	brl_src;	/* source mac address */
	struct ether_addr	brl_dst;	/* destination mac address */
	u_int16_t		brl_tag;	/* pf tag ID */
	u_int8_t		brl_action;	/* what to do with match */
	u_int8_t		brl_flags;	/* comparison flags */
	struct ifbrarpf		brl_arpf;	/* arp filter */
};

struct bstp_timer {
	u_int16_t	active;
	u_int16_t	value;
	u_int32_t	latched;
};

struct bstp_pri_vector {
	u_int64_t	pv_root_id;
	u_int32_t	pv_cost;
	u_int64_t	pv_dbridge_id;
	u_int16_t	pv_dport_id;
	u_int16_t	pv_port_id;
};

struct bstp_config_unit {
	struct bstp_pri_vector	cu_pv;
	u_int16_t	cu_message_age;
	u_int16_t	cu_max_age;
	u_int16_t	cu_forward_delay;
	u_int16_t	cu_hello_time;
	u_int8_t	cu_message_type;
	u_int8_t	cu_topology_change_ack;
	u_int8_t	cu_topology_change;
	u_int8_t	cu_proposal;
	u_int8_t	cu_agree;
	u_int8_t	cu_learning;
	u_int8_t	cu_forwarding;
	u_int8_t	cu_role;
};

struct bstp_tcn_unit {
	u_int8_t	tu_message_type;
};

struct bstp_port {
	LIST_ENTRY(bstp_port)	bp_next;
	unsigned int		bp_ifindex;	/* parent interface index */
	struct bstp_state	*bp_bs;
	struct task		bp_ltask;	/* if linkstate hook */
	u_int8_t		bp_active;
	u_int8_t		bp_protover;
	u_int32_t		bp_flags;
	u_int32_t		bp_path_cost;
	u_int16_t		bp_port_msg_age;
	u_int16_t		bp_port_max_age;
	u_int16_t		bp_port_fdelay;
	u_int16_t		bp_port_htime;
	u_int16_t		bp_desg_msg_age;
	u_int16_t		bp_desg_max_age;
	u_int16_t		bp_desg_fdelay;
	u_int16_t		bp_desg_htime;
	struct bstp_timer	bp_edge_delay_timer;
	struct bstp_timer	bp_forward_delay_timer;
	struct bstp_timer	bp_hello_timer;
	struct bstp_timer	bp_message_age_timer;
	struct bstp_timer	bp_migrate_delay_timer;
	struct bstp_timer	bp_recent_backup_timer;
	struct bstp_timer	bp_recent_root_timer;
	struct bstp_timer	bp_tc_timer;
	struct bstp_config_unit bp_msg_cu;
	struct bstp_pri_vector	bp_desg_pv;
	struct bstp_pri_vector	bp_port_pv;
	u_int16_t		bp_port_id;
	u_int8_t		bp_state;
	u_int8_t		bp_tcstate;
	u_int8_t		bp_role;
	u_int8_t		bp_infois;
	u_int8_t		bp_tc_ack;
	u_int8_t		bp_tc_prop;
	u_int8_t		bp_fdbflush;
	u_int8_t		bp_priority;
	u_int8_t		bp_ptp_link;
	u_int8_t		bp_agree;
	u_int8_t		bp_agreed;
	u_int8_t		bp_sync;
	u_int8_t		bp_synced;
	u_int8_t		bp_proposing;
	u_int8_t		bp_proposed;
	u_int8_t		bp_operedge;
	u_int8_t		bp_reroot;
	u_int8_t		bp_rcvdtc;
	u_int8_t		bp_rcvdtca;
	u_int8_t		bp_rcvdtcn;
	u_int32_t		bp_forward_transitions;
	u_int8_t		bp_txcount;
};

/*
 * Software state for each bridge STP.
 */
struct bstp_state {
	unsigned int		bs_ifindex;
	struct bstp_pri_vector	bs_bridge_pv;
	struct bstp_pri_vector	bs_root_pv;
	struct bstp_port	*bs_root_port;
	u_int8_t		bs_protover;
	u_int16_t		bs_migration_delay;
	u_int16_t		bs_edge_delay;
	u_int16_t		bs_bridge_max_age;
	u_int16_t		bs_bridge_fdelay;
	u_int16_t		bs_bridge_htime;
	u_int16_t		bs_root_msg_age;
	u_int16_t		bs_root_max_age;
	u_int16_t		bs_root_fdelay;
	u_int16_t		bs_root_htime;
	u_int16_t		bs_hold_time;
	u_int16_t		bs_bridge_priority;
	u_int8_t		bs_txholdcount;
	u_int8_t		bs_allsynced;
	struct timeout		bs_bstptimeout;		/* stp timeout */
	struct bstp_timer	bs_link_timer;
	struct timeval		bs_last_tc_time;
	LIST_HEAD(, bstp_port)	bs_bplist;
};

/*
 * Bridge interface list
 *
 *  Locks used to protect struct members in this file:
 *	I	immutable after creation
 *	k	kernel lock
 */
struct bridge_iflist {
	SMR_SLIST_ENTRY(bridge_iflist)	bif_next;	/* [k] next in list */
	struct bridge_softc		*bridge_sc;	/* [I] sc backpointer */
	struct bstp_port		*bif_stp;	/* [I] STP port state */
	struct brl_head			bif_brlin;	/* [k] input rules */
	struct brl_head			bif_brlout;	/* [k] output rules */
	struct ifnet			*ifp;		/* [I] net interface */
	u_int32_t			bif_flags;	/* member flags */
	u_int32_t			bif_protected;	/* protected domains */
	struct task			bif_dtask;
};
#define bif_state			bif_stp->bp_state

/*
 * XXX ip_ipsp.h's sockaddr_union should be converted to sockaddr *
 * passing with correct sa_len, then a good approach for cleaning this
 * will become more clear.
 */
union brsockaddr_union {
	struct sockaddr		sa;
	struct sockaddr_in	sin;
	struct sockaddr_in6	sin6;
};

/*
 * Bridge tunnel tagging
 */
struct bridge_tunneltag {
	union brsockaddr_union		brtag_peer;
	union brsockaddr_union		brtag_local;
	u_int32_t			brtag_id;
};

/*
 * Bridge route node
 */
struct bridge_rtnode {
	LIST_ENTRY(bridge_rtnode)	brt_next;	/* next in list */
	unsigned int			brt_ifidx;	/* destination ifs */
	u_int8_t			brt_flags;	/* address flags */
	u_int8_t			brt_age;	/* age counter */
	struct ether_addr		brt_addr;	/* dst addr */
	struct bridge_tunneltag		brt_tunnel;	/* tunnel endpoint */
};
#define brt_family brt_tunnel.brtag_peer.sa.sa_family

#ifndef BRIDGE_RTABLE_SIZE
#define BRIDGE_RTABLE_SIZE	1024
#endif
#define BRIDGE_RTABLE_MASK	(BRIDGE_RTABLE_SIZE - 1)

/*
 * Software state for each bridge
 *
 *  Locks used to protect struct members in this file:
 *	I	immutable after creation
 *	m	per-softc mutex
 *	k	kernel lock
 */
struct bridge_softc {
	struct ifnet			sc_if;	/* the interface */
	uint32_t			sc_brtmax;	/* [m] max # addresses */
	uint32_t			sc_brtcnt;	/* [m] current # addrs */
	int				sc_brttimeout;	/* timeout ticks */
	uint64_t			sc_hashkey[2];	/* [I] siphash key */
	struct timeout			sc_brtimeout;	/* timeout state */
	struct bstp_state		*sc_stp;	/* stp state */
	SMR_SLIST_HEAD(, bridge_iflist)	sc_iflist;	/* [k] interface list */
	SMR_SLIST_HEAD(, bridge_iflist)	sc_spanlist;	/* [k] span ports */
	struct mutex			sc_mtx;		/* mutex */
	LIST_HEAD(, bridge_rtnode)	sc_rts[BRIDGE_RTABLE_SIZE];	/* [m] hash table */
};

extern const u_int8_t bstp_etheraddr[];
struct llc;

int	bridge_enqueue(struct ifnet *, struct mbuf *);
void	bridge_update(struct ifnet *, struct ether_addr *, int);
void	bridge_rtdelete(struct bridge_softc *, struct ifnet *, int);
void	bridge_rtagenode(struct ifnet *, int);
struct bridge_tunneltag *bridge_tunnel(struct mbuf *);
struct bridge_tunneltag *bridge_tunneltag(struct mbuf *);
void	bridge_tunneluntag(struct mbuf *);
void	bridge_copyaddr(struct sockaddr *, struct sockaddr *);
void	bridge_copytag(struct bridge_tunneltag *, struct bridge_tunneltag *);

struct bstp_state *bstp_create(void);
void	bstp_enable(struct bstp_state *bs, unsigned int);
void	bstp_disable(struct bstp_state *bs);
void	bstp_destroy(struct bstp_state *);
void	bstp_initialization(struct bstp_state *);
void	bstp_stop(struct bstp_state *);
int	bstp_ioctl(struct ifnet *, u_long, caddr_t);
struct bstp_port *bstp_add(struct bstp_state *, struct ifnet *);
void	bstp_delete(struct bstp_port *);
struct mbuf *bstp_input(struct bstp_state *, struct bstp_port *,
    struct ether_header *, struct mbuf *);
void	bstp_ifstate(void *);
u_int8_t bstp_getstate(struct bstp_state *, struct bstp_port *);
void	bstp_ifsflags(struct bstp_port *, u_int);

int	bridgectl_ioctl(struct ifnet *, u_long, caddr_t);
int	bridge_rtupdate(struct bridge_softc *,
    struct ether_addr *, struct ifnet *, int, u_int8_t, struct mbuf *);
unsigned int bridge_rtlookup(struct ifnet *,
    struct ether_addr *, struct mbuf *);
void	bridge_rtflush(struct bridge_softc *, int);
void	bridge_rtage(void *);

u_int8_t bridge_filterrule(struct brl_head *, struct ether_header *,
    struct mbuf *);
void	bridge_flushrule(struct bridge_iflist *);

void	bridge_fragment(struct ifnet *, struct ifnet *, struct ether_header *,
    struct mbuf *);
struct bridge_iflist *bridge_getbif(struct ifnet *);
int	bridge_findbif(struct bridge_softc *, const char *,
    struct bridge_iflist **);

#endif /* _KERNEL */
#endif /* _NET_IF_BRIDGE_H_ */
