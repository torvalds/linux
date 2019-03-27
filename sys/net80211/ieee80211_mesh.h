/*- 
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 The FreeBSD Foundation 
 * All rights reserved. 
 * 
 * This software was developed by Rui Paulo under sponsorship from the
 * FreeBSD Foundation. 
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
 * 
 * $FreeBSD$
 */
#ifndef _NET80211_IEEE80211_MESH_H_
#define _NET80211_IEEE80211_MESH_H_

#define	IEEE80211_MESH_DEFAULT_TTL	31
#define	IEEE80211_MESH_MAX_NEIGHBORS	15

/*
 * NB: all structures are __packed  so sizeof works on arm, et. al.
 */
/*
 * 802.11s Information Elements.
*/
/* Mesh Configuration */
#define IEEE80211_MESH_CONF_SZ		(7)
struct ieee80211_meshconf_ie {
	uint8_t		conf_ie;	/* IEEE80211_ELEMID_MESHCONF */
	uint8_t		conf_len;
	uint8_t		conf_pselid;	/* Active Path Sel. Proto. ID */
	uint8_t		conf_pmetid;	/* Active Metric Identifier */
	uint8_t		conf_ccid;	/* Congestion Control Mode ID  */
	uint8_t		conf_syncid;	/* Sync. Protocol ID */
	uint8_t		conf_authid;	/* Auth. Protocol ID */
	uint8_t		conf_form;	/* Formation Information */
	uint8_t		conf_cap;
} __packed;

/* Hybrid Wireless Mesh Protocol */
enum {
	/* 0 reserved */
	IEEE80211_MESHCONF_PATH_HWMP		= 1,
	/* 2-254 reserved */
	IEEE80211_MESHCONF_PATH_VENDOR		= 255,
};

/* Airtime Link Metric */
enum {
	/* 0 reserved */
	IEEE80211_MESHCONF_METRIC_AIRTIME	= 1,
	/* 2-254 reserved */
	IEEE80211_MESHCONF_METRIC_VENDOR	= 255,
};

/* Congestion Control */
enum {
	IEEE80211_MESHCONF_CC_DISABLED		= 0,
	IEEE80211_MESHCONF_CC_SIG		= 1,
	/* 2-254 reserved */
	IEEE80211_MESHCONF_CC_VENDOR		= 255,
};

/* Neighbour Offset */
enum {
	/* 0 reserved */
	IEEE80211_MESHCONF_SYNC_NEIGHOFF	= 1,
	/* 2-254 rserved */
	IEEE80211_MESHCONF_SYNC_VENDOR		= 255,
};

/* Authentication Protocol Identifier */
enum {
	
	IEEE80211_MESHCONF_AUTH_DISABLED	= 0,
	/* Simultaneous Authenticaction of Equals */
	IEEE80211_MESHCONF_AUTH_SEA		= 1,
	IEEE80211_MESHCONF_AUTH_8021X		= 2, /* IEEE 802.1X */
	/* 3-254 reserved */
	IEEE80211_MESHCONF_AUTH_VENDOR		= 255,
};

/* Mesh Formation Info */
#define	IEEE80211_MESHCONF_FORM_GATE	0x01 	/* Connected to Gate */
#define	IEEE80211_MESHCONF_FORM_NNEIGH_MASK 0x7E /* Number of Neighbours */
#define	IEEE80211_MESHCONF_FORM_SA	0xF0 	/* indicating 802.1X auth */

/* Mesh Capability */
#define	IEEE80211_MESHCONF_CAP_AP	0x01	/* Accepting Peers */
#define	IEEE80211_MESHCONF_CAP_MCCAS	0x02	/* MCCA supported */
#define	IEEE80211_MESHCONF_CAP_MCCAE	0x04	/* MCCA enabled */
#define	IEEE80211_MESHCONF_CAP_FWRD 	0x08	/* forwarding enabled */
#define	IEEE80211_MESHCONF_CAP_BTR	0x10	/* Beacon Timing Report Enab */
#define	IEEE80211_MESHCONF_CAP_TBTT	0x20	/* TBTT Adjusting  */
#define	IEEE80211_MESHCONF_CAP_PSL	0x40	/* Power Save Level */
/* 0x80 reserved */

/* Mesh Identifier */
struct ieee80211_meshid_ie {
	uint8_t		id_ie;		/* IEEE80211_ELEMID_MESHID */
	uint8_t		id_len;
} __packed;

/* Link Metric Report */
struct ieee80211_meshlmetric_ie {
	uint8_t		lm_ie;	/* IEEE80211_ACTION_MESH_LMETRIC */
	uint8_t		lm_len;
	uint8_t		lm_flags;
#define	IEEE80211_MESH_LMETRIC_FLAGS_REQ	0x01	/* Request */
	/*
	 * XXX: this field should be variable in size and depend on
	 * the active active path selection metric identifier
	 */
	uint32_t	lm_metric;
#define	IEEE80211_MESHLMETRIC_INITIALVAL	0
} __packed;

/* Congestion Notification */
struct ieee80211_meshcngst_ie {
	uint8_t		cngst_ie;	/* IEEE80211_ELEMID_MESHCNGST */
	uint8_t		cngst_len;
	uint16_t	cngst_timer[4];	/* Expiration Timers: AC_BK,
					   AC_BE, AC_VI, AC_VO */
} __packed;

/* Peer Link Management */
#define IEEE80211_MPM_BASE_SZ	(4)
#define IEEE80211_MPM_MAX_SZ	(8)
struct ieee80211_meshpeer_ie {
	uint8_t		peer_ie;	/* IEEE80211_ELEMID_MESHPEER */
	uint8_t		peer_len;
	uint16_t	peer_proto;	/* Peer Management Protocol */
	uint16_t	peer_llinkid;	/* Local Link ID */
	uint16_t	peer_linkid;	/* Peer Link ID */
	uint16_t	peer_rcode;
} __packed;

/* Mesh Peering Protocol Identifier field value */
enum {
	IEEE80211_MPPID_MPM		= 0,	/* Mesh peering management */
	IEEE80211_MPPID_AUTH_MPM	= 1,	/* Auth. mesh peering exchange */
	/* 2-65535 reserved */
};

#ifdef notyet
/* Mesh Channel Switch Annoucement */
struct ieee80211_meshcsa_ie {
	uint8_t		csa_ie;		/* IEEE80211_ELEMID_MESHCSA */
	uint8_t		csa_len;
	uint8_t		csa_mode;
	uint8_t		csa_newclass;	/* New Regulatory Class */
	uint8_t		csa_newchan;
	uint8_t		csa_precvalue;	/* Precedence Value */
	uint8_t		csa_count;
} __packed;

/* Mesh TIM */
/* Equal to the non Mesh version */

/* Mesh Awake Window */
struct ieee80211_meshawakew_ie {
	uint8_t		awakew_ie;		/* IEEE80211_ELEMID_MESHAWAKEW */
	uint8_t		awakew_len;
	uint8_t		awakew_windowlen;	/* in TUs */
} __packed;

/* Mesh Beacon Timing */
struct ieee80211_meshbeacont_ie {
	uint8_t		beacont_ie;		/* IEEE80211_ELEMID_MESHBEACONT */
	uint8_t		beacont_len;
	struct {
		uint8_t		mp_aid;		/* Least Octet of AID */
		uint16_t	mp_btime;	/* Beacon Time */
		uint16_t	mp_bint;	/* Beacon Interval */
	} __packed mp[1];			/* NB: variable size */
} __packed;
#endif

/* Gate (GANN) Annoucement */
/*
 * NB: these macros used for the length in the IEs does not include 2 bytes
 * for _ie and _len fields as is defined by the standard.
 */
#define	IEEE80211_MESHGANN_BASE_SZ 	(15)
struct ieee80211_meshgann_ie {
	uint8_t		gann_ie;		/* IEEE80211_ELEMID_MESHGANN */
	uint8_t		gann_len;
	uint8_t		gann_flags;
	uint8_t		gann_hopcount;
	uint8_t		gann_ttl;
	uint8_t		gann_addr[IEEE80211_ADDR_LEN];
	uint32_t	gann_seq;		/* GANN Sequence Number */
	uint16_t	gann_interval;		/* GANN Interval */
} __packed;

/* Root (MP) Annoucement */
#define	IEEE80211_MESHRANN_BASE_SZ 	(21)
struct ieee80211_meshrann_ie {
	uint8_t		rann_ie;		/* IEEE80211_ELEMID_MESHRANN */
	uint8_t		rann_len;
	uint8_t		rann_flags;
#define	IEEE80211_MESHRANN_FLAGS_GATE	0x01	/* Mesh Gate */
	uint8_t		rann_hopcount;
	uint8_t		rann_ttl;
	uint8_t		rann_addr[IEEE80211_ADDR_LEN];
	uint32_t	rann_seq;		/* HWMP Sequence Number */
	uint32_t	rann_interval;
	uint32_t	rann_metric;
} __packed;

/* Mesh Path Request */
#define	IEEE80211_MESHPREQ_BASE_SZ 		(26)
#define	IEEE80211_MESHPREQ_BASE_SZ_AE 		(32)
#define	IEEE80211_MESHPREQ_TRGT_SZ 		(11)
#define	IEEE80211_MESHPREQ_TCNT_OFFSET		(27)
#define	IEEE80211_MESHPREQ_TCNT_OFFSET_AE	(33)
struct ieee80211_meshpreq_ie {
	uint8_t		preq_ie;	/* IEEE80211_ELEMID_MESHPREQ */
	uint8_t		preq_len;
	uint8_t		preq_flags;
#define	IEEE80211_MESHPREQ_FLAGS_GATE	0x01	/* Mesh Gate */
#define	IEEE80211_MESHPREQ_FLAGS_AM	0x02	/* 0 = bcast / 1 = ucast */
#define	IEEE80211_MESHPREQ_FLAGS_PP	0x04	/* Proactive PREP */
#define	IEEE80211_MESHPREQ_FLAGS_AE	0x40	/* Address Extension */
	uint8_t		preq_hopcount;
	uint8_t		preq_ttl;
	uint32_t	preq_id;
	uint8_t		preq_origaddr[IEEE80211_ADDR_LEN];
	uint32_t	preq_origseq;	/* HWMP Sequence Number */
	/* NB: may have Originator External Address */
	uint8_t		preq_orig_ext_addr[IEEE80211_ADDR_LEN];
	uint32_t	preq_lifetime;
	uint32_t	preq_metric;
	uint8_t		preq_tcount;	/* target count */
	struct {
		uint8_t		target_flags;
#define	IEEE80211_MESHPREQ_TFLAGS_TO	0x01	/* Target Only */
#define	IEEE80211_MESHPREQ_TFLAGS_USN	0x04	/* Unknown HWMP seq number */
		uint8_t		target_addr[IEEE80211_ADDR_LEN];
		uint32_t	target_seq;	/* HWMP Sequence Number */
	} __packed preq_targets[1];		/* NB: variable size */
} __packed;

/* Mesh Path Reply */
#define	IEEE80211_MESHPREP_BASE_SZ 	(31)
#define	IEEE80211_MESHPREP_BASE_SZ_AE 	(37)
struct ieee80211_meshprep_ie {
	uint8_t		prep_ie;	/* IEEE80211_ELEMID_MESHPREP */
	uint8_t		prep_len;
	uint8_t		prep_flags;
#define	IEEE80211_MESHPREP_FLAGS_AE	0x40	/* Address Extension */
	uint8_t		prep_hopcount;
	uint8_t		prep_ttl;
	uint8_t		prep_targetaddr[IEEE80211_ADDR_LEN];
	uint32_t	prep_targetseq;
	/* NB: May have Target External Address */
	uint8_t		prep_target_ext_addr[IEEE80211_ADDR_LEN];
	uint32_t	prep_lifetime;
	uint32_t	prep_metric;
	uint8_t		prep_origaddr[IEEE80211_ADDR_LEN];
	uint32_t	prep_origseq;	/* HWMP Sequence Number */
} __packed;

/* Mesh Path Error */
#define	IEEE80211_MESHPERR_MAXDEST	(19)
#define	IEEE80211_MESHPERR_NDEST_OFFSET	(3)
#define	IEEE80211_MESHPERR_BASE_SZ 	(2)
#define	IEEE80211_MESHPERR_DEST_SZ 	(13)
#define	IEEE80211_MESHPERR_DEST_SZ_AE 	(19)
struct ieee80211_meshperr_ie {
	uint8_t		perr_ie;	/* IEEE80211_ELEMID_MESHPERR */
	uint8_t		perr_len;
	uint8_t		perr_ttl;
	uint8_t		perr_ndests;	/* Number of Destinations */
	struct {
		uint8_t		dest_flags;
#define	IEEE80211_MESHPERR_DFLAGS_USN	0x01	/* XXX: not part of standard */
#define	IEEE80211_MESHPERR_DFLAGS_RC	0x02	/* XXX: not part of standard */
#define	IEEE80211_MESHPERR_FLAGS_AE	0x40	/* Address Extension */
		uint8_t		dest_addr[IEEE80211_ADDR_LEN];
		uint32_t	dest_seq;	/* HWMP Sequence Number */
		/* NB: May have Destination External Address */
		uint8_t		dest_ext_addr[IEEE80211_ADDR_LEN];
		uint16_t	dest_rcode;
	} __packed perr_dests[1];		/* NB: variable size */
} __packed;

#ifdef notyet
/* Mesh Proxy Update */
struct ieee80211_meshpu_ie {
	uint8_t		pu_ie;		/* IEEE80211_ELEMID_MESHPU */
	uint8_t		pu_len;
	uint8_t		pu_flags;
#define	IEEE80211_MESHPU_FLAGS_MASK		0x1
#define	IEEE80211_MESHPU_FLAGS_DEL		0x0
#define	IEEE80211_MESHPU_FLAGS_ADD		0x1
	uint8_t		pu_seq;		/* PU Sequence Number */
	uint8_t		pu_addr[IEEE80211_ADDR_LEN];
	uint8_t		pu_naddr;	/* Number of Proxied Addresses */
	/* NB: proxied address follows */
} __packed;

/* Mesh Proxy Update Confirmation */
struct ieee80211_meshpuc_ie {
	uint8_t		puc_ie;		/* IEEE80211_ELEMID_MESHPUC */
	uint8_t		puc_len;
	uint8_t		puc_flags;
	uint8_t		puc_seq;	/* PU Sequence Number */
	uint8_t		puc_daddr[IEEE80211_ADDR_LEN];
} __packed;
#endif

/*
 * 802.11s Action Frames
 * XXX: these are wrong, and some of them should be
 * under MESH category while PROXY is under MULTIHOP category.
 */
#define	IEEE80211_ACTION_CAT_INTERWORK		15
#define	IEEE80211_ACTION_CAT_RESOURCE		16
#define	IEEE80211_ACTION_CAT_PROXY		17

/*
 * Mesh Peering Action codes.
 */
enum {
	/* 0 reserved */
	IEEE80211_ACTION_MESHPEERING_OPEN	= 1,
	IEEE80211_ACTION_MESHPEERING_CONFIRM	= 2,
	IEEE80211_ACTION_MESHPEERING_CLOSE	= 3,
	/* 4-255 reserved */
};

/*
 * Mesh Action code.
 */
enum {
	IEEE80211_ACTION_MESH_LMETRIC	= 0,	/* Mesh Link Metric Report */
	IEEE80211_ACTION_MESH_HWMP	= 1,	/* HWMP Mesh Path Selection */
	IEEE80211_ACTION_MESH_GANN	= 2,	/* Gate Announcement */
	IEEE80211_ACTION_MESH_CC	= 3,	/* Congestion Control */
	IEEE80211_ACTION_MESH_MCCA_SREQ	= 4,	/* MCCA Setup Request */
	IEEE80211_ACTION_MESH_MCCA_SREP	= 5,	/* MCCA Setup Reply */
	IEEE80211_ACTION_MESH_MCCA_AREQ	= 6,	/* MCCA Advertisement Req. */
	IEEE80211_ACTION_MESH_MCCA_ADVER =7,	/* MCCA Advertisement */
	IEEE80211_ACTION_MESH_MCCA_TRDOWN = 8,	/* MCCA Teardown */
	IEEE80211_ACTION_MESH_TBTT_REQ	= 9,	/* TBTT Adjustment Request */
	IEEE80211_ACTION_MESH_TBTT_RES	= 10,	/* TBTT Adjustment Response */
	/* 11-255 reserved */
};

/*
 * Different mesh control structures based on the AE
 * (Address Extension) bits.
 */
struct ieee80211_meshcntl {
	uint8_t		mc_flags;	/* Address Extension 00 */
	uint8_t		mc_ttl;		/* TTL */
	uint8_t		mc_seq[4];	/* Sequence No. */
	/* NB: more addresses may follow */
} __packed;

struct ieee80211_meshcntl_ae01 {
	uint8_t		mc_flags;	/* Address Extension 01 */
	uint8_t		mc_ttl;		/* TTL */
	uint8_t		mc_seq[4];	/* Sequence No. */
	uint8_t		mc_addr4[IEEE80211_ADDR_LEN];
} __packed;

struct ieee80211_meshcntl_ae10 {
	uint8_t		mc_flags;	/* Address Extension 10 */
	uint8_t		mc_ttl;		/* TTL */
	uint8_t		mc_seq[4];	/* Sequence No. */
	uint8_t		mc_addr5[IEEE80211_ADDR_LEN];
	uint8_t		mc_addr6[IEEE80211_ADDR_LEN];
} __packed;

#define IEEE80211_MESH_AE_MASK		0x03
enum {
	IEEE80211_MESH_AE_00		= 0,	/* MC has no AE subfield */
	IEEE80211_MESH_AE_01		= 1,	/* MC contain addr4 */
	IEEE80211_MESH_AE_10		= 2,	/* MC contain addr5 & addr6 */
	IEEE80211_MESH_AE_11		= 3,	/* RESERVED */
};

#ifdef _KERNEL
MALLOC_DECLARE(M_80211_MESH_PREQ);
MALLOC_DECLARE(M_80211_MESH_PREP);
MALLOC_DECLARE(M_80211_MESH_PERR);

MALLOC_DECLARE(M_80211_MESH_RT);
MALLOC_DECLARE(M_80211_MESH_GT_RT);
/*
 * Basic forwarding information:
 * o Destination MAC
 * o Next-hop MAC
 * o Precursor list (not implemented yet)
 * o Path timeout
 * The rest is part of the active Mesh path selection protocol.
 * XXX: to be moved out later.
 */
struct ieee80211_mesh_route {
	TAILQ_ENTRY(ieee80211_mesh_route)	rt_next;
	struct ieee80211vap	*rt_vap;
	ieee80211_rte_lock_t	rt_lock;	/* fine grained route lock */
	struct callout		rt_discovery;	/* discovery timeout */
	int			rt_updtime;	/* last update time */
	uint8_t			rt_dest[IEEE80211_ADDR_LEN];
	uint8_t			rt_mesh_gate[IEEE80211_ADDR_LEN]; /* meshDA */
	uint8_t			rt_nexthop[IEEE80211_ADDR_LEN];
	uint32_t		rt_metric;	/* path metric */
	uint16_t		rt_nhops;	/* number of hops */
	uint16_t		rt_flags;
#define	IEEE80211_MESHRT_FLAGS_DISCOVER	0x01	/* path discovery */
#define	IEEE80211_MESHRT_FLAGS_VALID	0x02	/* path discovery complete */
#define	IEEE80211_MESHRT_FLAGS_PROXY	0x04	/* proxy entry */
#define	IEEE80211_MESHRT_FLAGS_GATE	0x08	/* mesh gate entry */
	uint32_t		rt_lifetime;	/* route timeout */
	uint32_t		rt_lastmseq;	/* last seq# seen dest */
	uint32_t		rt_ext_seq;	/* proxy seq number */
	void			*rt_priv;	/* private data */
};
#define	IEEE80211_MESH_ROUTE_PRIV(rt, cast)	((cast *)rt->rt_priv)

/*
 * Stored information about known mesh gates.
 */
struct ieee80211_mesh_gate_route {
	TAILQ_ENTRY(ieee80211_mesh_gate_route)	gr_next;
	uint8_t				gr_addr[IEEE80211_ADDR_LEN];
	uint32_t			gr_lastseq;
	struct ieee80211_mesh_route 	*gr_route;
};

#define	IEEE80211_MESH_PROTO_DSZ	12	/* description size */
/*
 * Mesh Path Selection Protocol.
 */
enum ieee80211_state;
struct ieee80211_mesh_proto_path {
	uint8_t		mpp_active;
	char 		mpp_descr[IEEE80211_MESH_PROTO_DSZ];
	uint8_t		mpp_ie;
	struct ieee80211_node *
	    		(*mpp_discover)(struct ieee80211vap *,
				const uint8_t [IEEE80211_ADDR_LEN],
				struct mbuf *);
	void		(*mpp_peerdown)(struct ieee80211_node *);
	void		(*mpp_senderror)(struct ieee80211vap *,
				const uint8_t [IEEE80211_ADDR_LEN],
				struct ieee80211_mesh_route *, int);
	void		(*mpp_vattach)(struct ieee80211vap *);
	void		(*mpp_vdetach)(struct ieee80211vap *);
	int		(*mpp_newstate)(struct ieee80211vap *,
			    enum ieee80211_state, int);
	const size_t	mpp_privlen;	/* size required in the routing table
					   for private data */
	int		mpp_inact;	/* inact. timeout for invalid routes
					   (ticks) */
};

/*
 * Mesh Link Metric Report Protocol.
 */
struct ieee80211_mesh_proto_metric {
	uint8_t		mpm_active;
	char		mpm_descr[IEEE80211_MESH_PROTO_DSZ];
	uint8_t		mpm_ie;
	uint32_t	(*mpm_metric)(struct ieee80211_node *);
};

#ifdef notyet
/*
 * Mesh Authentication Protocol.
 */
struct ieee80211_mesh_proto_auth {
	uint8_t		mpa_ie[4];
};

struct ieee80211_mesh_proto_congestion {
};

struct ieee80211_mesh_proto_sync {
};
#endif

typedef uint32_t ieee80211_mesh_seq;
#define	IEEE80211_MESH_SEQ_LEQ(a, b)	((int32_t)((a)-(b)) <= 0)
#define	IEEE80211_MESH_SEQ_GEQ(a, b)	((int32_t)((a)-(b)) >= 0)

struct ieee80211_mesh_state {
	int				ms_idlen;
	uint8_t				ms_id[IEEE80211_MESHID_LEN];
	ieee80211_mesh_seq		ms_seq;	/* seq no for meshcntl */
	uint16_t			ms_neighbors;
	uint8_t				ms_ttl;	/* mesh ttl set in packets */
#define IEEE80211_MESHFLAGS_AP		0x01	/* accept peers */
#define IEEE80211_MESHFLAGS_GATE	0x02	/* mesh gate role */
#define IEEE80211_MESHFLAGS_FWD		0x04	/* forward packets */
#define IEEE80211_MESHFLAGS_ROOT	0x08	/* configured as root */
	uint8_t				ms_flags;
	ieee80211_rt_lock_t		ms_rt_lock;
	struct callout			ms_cleantimer;
	struct callout			ms_gatetimer;
	ieee80211_mesh_seq		ms_gateseq;
	TAILQ_HEAD(, ieee80211_mesh_gate_route) ms_known_gates;
	TAILQ_HEAD(, ieee80211_mesh_route)  ms_routes;
	struct ieee80211_mesh_proto_metric *ms_pmetric;
	struct ieee80211_mesh_proto_path   *ms_ppath;
};
void		ieee80211_mesh_attach(struct ieee80211com *);
void		ieee80211_mesh_detach(struct ieee80211com *);

struct ieee80211_mesh_route *
		ieee80211_mesh_rt_find(struct ieee80211vap *,
		    const uint8_t [IEEE80211_ADDR_LEN]);
struct ieee80211_mesh_route *
                ieee80211_mesh_rt_add(struct ieee80211vap *,
		    const uint8_t [IEEE80211_ADDR_LEN]);
void		ieee80211_mesh_rt_del(struct ieee80211vap *,
		    const uint8_t [IEEE80211_ADDR_LEN]);
void		ieee80211_mesh_rt_flush(struct ieee80211vap *);
void		ieee80211_mesh_rt_flush_peer(struct ieee80211vap *,
		    const uint8_t [IEEE80211_ADDR_LEN]);
int		ieee80211_mesh_rt_update(struct ieee80211_mesh_route *rt, int);
void		ieee80211_mesh_proxy_check(struct ieee80211vap *,
		    const uint8_t [IEEE80211_ADDR_LEN]);

int		ieee80211_mesh_register_proto_path(const
		    struct ieee80211_mesh_proto_path *);
int		ieee80211_mesh_register_proto_metric(const
		    struct ieee80211_mesh_proto_metric *);

uint8_t *	ieee80211_add_meshid(uint8_t *, struct ieee80211vap *);
uint8_t *	ieee80211_add_meshconf(uint8_t *, struct ieee80211vap *);
uint8_t *	ieee80211_add_meshpeer(uint8_t *, uint8_t, uint16_t, uint16_t,
		    uint16_t);
uint8_t *	ieee80211_add_meshlmetric(uint8_t *, uint8_t, uint32_t);
uint8_t *	ieee80211_add_meshgate(uint8_t *,
		    struct ieee80211_meshgann_ie *);

void		ieee80211_mesh_node_init(struct ieee80211vap *,
		    struct ieee80211_node *);
void		ieee80211_mesh_node_cleanup(struct ieee80211_node *);
void		ieee80211_parse_meshid(struct ieee80211_node *,
		    const uint8_t *);
struct ieee80211_scanparams;
void		ieee80211_mesh_init_neighbor(struct ieee80211_node *,
		   const struct ieee80211_frame *,
		   const struct ieee80211_scanparams *);
void		ieee80211_mesh_update_beacon(struct ieee80211vap *,
		    struct ieee80211_beacon_offsets *);
struct ieee80211_mesh_gate_route *
		ieee80211_mesh_mark_gate(struct ieee80211vap *,
		    const uint8_t *, struct ieee80211_mesh_route *);
void		ieee80211_mesh_forward_to_gates(struct ieee80211vap *,
		    struct ieee80211_mesh_route *);
struct ieee80211_node *
		ieee80211_mesh_find_txnode(struct ieee80211vap *,
		    const uint8_t [IEEE80211_ADDR_LEN]);

/*
 * Return non-zero if proxy operation is enabled.
 */
static __inline int
ieee80211_mesh_isproxyena(struct ieee80211vap *vap)
{
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	return (ms->ms_flags &
	    (IEEE80211_MESHFLAGS_AP | IEEE80211_MESHFLAGS_GATE)) != 0;
}

/*
 * Process an outbound frame: if a path is known to the
 * destination then return a reference to the next hop
 * for immediate transmission.  Otherwise initiate path
 * discovery and, if possible queue the packet to be
 * sent when path discovery completes.
 */
static __inline struct ieee80211_node *
ieee80211_mesh_discover(struct ieee80211vap *vap,
    const uint8_t dest[IEEE80211_ADDR_LEN], struct mbuf *m)
{
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	return ms->ms_ppath->mpp_discover(vap, dest, m);
}

#endif /* _KERNEL */
#endif /* !_NET80211_IEEE80211_MESH_H_ */
