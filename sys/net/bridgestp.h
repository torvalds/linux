/*	$NetBSD: if_bridgevar.h,v 1.4 2003/07/08 07:13:50 itojun Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright 2001 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Jason R. Thorpe for Wasabi Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed for the NetBSD Project by
 *	Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * Copyright (c) 1999, 2000 Jason L. Wright (jason@thought.net)
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Jason L. Wright
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 * OpenBSD: if_bridge.h,v 1.14 2001/03/22 03:48:29 jason Exp
 *
 * $FreeBSD$
 */

/*
 * Data structure and control definitions for STP interfaces.
 */

#include <sys/callout.h>
#include <sys/queue.h>

/* STP port states */
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

#ifdef _KERNEL

/* STP port flags */
#define	BSTP_PORT_CANMIGRATE	0x0001
#define	BSTP_PORT_NEWINFO	0x0002
#define	BSTP_PORT_DISPUTED	0x0004
#define	BSTP_PORT_ADMCOST	0x0008
#define	BSTP_PORT_AUTOEDGE	0x0010
#define	BSTP_PORT_AUTOPTP	0x0020
#define	BSTP_PORT_ADMEDGE	0x0040
#define	BSTP_PORT_PNDCOST	0x0080

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
 * Spanning tree defaults.
 */
#define	BSTP_DEFAULT_MAX_AGE		(20 * 256)
#define	BSTP_DEFAULT_HELLO_TIME		(2 * 256)
#define	BSTP_DEFAULT_FORWARD_DELAY	(15 * 256)
#define	BSTP_DEFAULT_HOLD_TIME		(1 * 256)
#define	BSTP_DEFAULT_MIGRATE_DELAY	(3 * 256)
#define	BSTP_DEFAULT_HOLD_COUNT		6
#define	BSTP_DEFAULT_BRIDGE_PRIORITY	0x8000
#define	BSTP_DEFAULT_PORT_PRIORITY	0x80
#define	BSTP_DEFAULT_PATH_COST		55
#define	BSTP_MIN_HELLO_TIME		(1 * 256)
#define	BSTP_MIN_MAX_AGE		(6 * 256)
#define	BSTP_MIN_FORWARD_DELAY		(4 * 256)
#define	BSTP_MIN_HOLD_COUNT		1
#define	BSTP_MAX_HELLO_TIME		(2 * 256)
#define	BSTP_MAX_MAX_AGE		(40 * 256)
#define	BSTP_MAX_FORWARD_DELAY		(30 * 256)
#define	BSTP_MAX_HOLD_COUNT		10
#define	BSTP_MAX_PRIORITY		61440
#define	BSTP_MAX_PORT_PRIORITY		240
#define	BSTP_MAX_PATH_COST		200000000

/* BPDU message types */
#define	BSTP_MSGTYPE_CFG	0x00		/* Configuration */
#define	BSTP_MSGTYPE_RSTP	0x02		/* Rapid STP */
#define	BSTP_MSGTYPE_TCN	0x80		/* Topology chg notification */

/* Protocol versions */
#define	BSTP_PROTO_ID		0x00
#define	BSTP_PROTO_STP		0x00
#define	BSTP_PROTO_RSTP		0x02
#define	BSTP_PROTO_MAX		BSTP_PROTO_RSTP

#define	BSTP_INFO_RECEIVED	1
#define	BSTP_INFO_MINE		2
#define	BSTP_INFO_AGED		3
#define	BSTP_INFO_DISABLED	4


#define	BSTP_MESSAGE_AGE_INCR	(1 * 256)	/* in 256ths of a second */
#define	BSTP_TICK_VAL		(1 * 256)	/* in 256ths of a second */
#define	BSTP_LINK_TIMER		(BSTP_TICK_VAL * 15)

/*
 * Driver callbacks for STP state changes
 */
typedef void (*bstp_state_cb_t)(struct ifnet *, int);
typedef void (*bstp_rtage_cb_t)(struct ifnet *, int);
struct bstp_cb_ops {
	bstp_state_cb_t	bcb_state;
	bstp_rtage_cb_t	bcb_rtage;
};

/*
 * Because BPDU's do not make nicely aligned structures, two different
 * declarations are used: bstp_?bpdu (wire representation, packed) and
 * bstp_*_unit (internal, nicely aligned version).
 */

/* configuration bridge protocol data unit */
struct bstp_cbpdu {
	uint8_t		cbu_dsap;		/* LLC: destination sap */
	uint8_t		cbu_ssap;		/* LLC: source sap */
	uint8_t		cbu_ctl;		/* LLC: control */
	uint16_t	cbu_protoid;		/* protocol id */
	uint8_t		cbu_protover;		/* protocol version */
	uint8_t		cbu_bpdutype;		/* message type */
	uint8_t		cbu_flags;		/* flags (below) */

	/* root id */
	uint16_t	cbu_rootpri;		/* root priority */
	uint8_t		cbu_rootaddr[6];	/* root address */

	uint32_t	cbu_rootpathcost;	/* root path cost */

	/* bridge id */
	uint16_t	cbu_bridgepri;		/* bridge priority */
	uint8_t		cbu_bridgeaddr[6];	/* bridge address */

	uint16_t	cbu_portid;		/* port id */
	uint16_t	cbu_messageage;		/* current message age */
	uint16_t	cbu_maxage;		/* maximum age */
	uint16_t	cbu_hellotime;		/* hello time */
	uint16_t	cbu_forwarddelay;	/* forwarding delay */
	uint8_t		cbu_versionlen;		/* version 1 length */
} __packed;
#define	BSTP_BPDU_STP_LEN	(3 + 35)	/* LLC + STP pdu */
#define	BSTP_BPDU_RSTP_LEN	(3 + 36)	/* LLC + RSTP pdu */

/* topology change notification bridge protocol data unit */
struct bstp_tbpdu {
	uint8_t		tbu_dsap;		/* LLC: destination sap */
	uint8_t		tbu_ssap;		/* LLC: source sap */
	uint8_t		tbu_ctl;		/* LLC: control */
	uint16_t	tbu_protoid;		/* protocol id */
	uint8_t		tbu_protover;		/* protocol version */
	uint8_t		tbu_bpdutype;		/* message type */
} __packed;

/*
 * Timekeeping structure used in spanning tree code.
 */
struct bstp_timer {
	int		active;
	int		latched;
	int		value;
};

struct bstp_pri_vector {
	uint64_t		pv_root_id;
	uint32_t		pv_cost;
	uint64_t		pv_dbridge_id;
	uint16_t		pv_dport_id;
	uint16_t		pv_port_id;
};

struct bstp_config_unit {
	struct bstp_pri_vector	cu_pv;
	uint16_t	cu_message_age;
	uint16_t	cu_max_age;
	uint16_t	cu_forward_delay;
	uint16_t	cu_hello_time;
	uint8_t		cu_message_type;
	uint8_t		cu_topology_change_ack;
	uint8_t		cu_topology_change;
	uint8_t		cu_proposal;
	uint8_t		cu_agree;
	uint8_t		cu_learning;
	uint8_t		cu_forwarding;
	uint8_t		cu_role;
};

struct bstp_tcn_unit {
	uint8_t		tu_message_type;
};

struct bstp_port {
	LIST_ENTRY(bstp_port)	bp_next;
	struct ifnet		*bp_ifp;	/* parent if */
	struct bstp_state	*bp_bs;
	uint8_t			bp_active;
	uint8_t			bp_protover;
	uint32_t		bp_flags;
	uint32_t		bp_path_cost;
	uint16_t		bp_port_msg_age;
	uint16_t		bp_port_max_age;
	uint16_t		bp_port_fdelay;
	uint16_t		bp_port_htime;
	uint16_t		bp_desg_msg_age;
	uint16_t		bp_desg_max_age;
	uint16_t		bp_desg_fdelay;
	uint16_t		bp_desg_htime;
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
	uint16_t		bp_port_id;
	uint8_t			bp_state;
	uint8_t			bp_tcstate;
	uint8_t			bp_role;
	uint8_t			bp_infois;
	uint8_t			bp_tc_ack;
	uint8_t			bp_tc_prop;
	uint8_t			bp_fdbflush;
	uint8_t			bp_priority;
	uint8_t			bp_ptp_link;
	uint8_t			bp_agree;
	uint8_t			bp_agreed;
	uint8_t			bp_sync;
	uint8_t			bp_synced;
	uint8_t			bp_proposing;
	uint8_t			bp_proposed;
	uint8_t			bp_operedge;
	uint8_t			bp_reroot;
	uint8_t			bp_rcvdtc;
	uint8_t			bp_rcvdtca;
	uint8_t			bp_rcvdtcn;
	uint32_t		bp_forward_transitions;
	uint8_t			bp_txcount;
	struct task		bp_statetask;
	struct task		bp_rtagetask;
	struct task		bp_mediatask;
};

/*
 * Software state for each bridge STP.
 */
struct bstp_state {
	LIST_ENTRY(bstp_state)	bs_list;
	uint8_t			bs_running;
	struct mtx		bs_mtx;
	struct bstp_pri_vector	bs_bridge_pv;
	struct bstp_pri_vector	bs_root_pv;
	struct bstp_port	*bs_root_port;
	uint8_t			bs_protover;
	uint16_t		bs_migration_delay;
	uint16_t		bs_edge_delay;
	uint16_t		bs_bridge_max_age;
	uint16_t		bs_bridge_fdelay;
	uint16_t		bs_bridge_htime;
	uint16_t		bs_root_msg_age;
	uint16_t		bs_root_max_age;
	uint16_t		bs_root_fdelay;
	uint16_t		bs_root_htime;
	uint16_t		bs_hold_time;
	uint16_t		bs_bridge_priority;
	uint8_t			bs_txholdcount;
	uint8_t			bs_allsynced;
	struct callout		bs_bstpcallout;	/* STP callout */
	struct bstp_timer	bs_link_timer;
	struct timeval		bs_last_tc_time;
	LIST_HEAD(, bstp_port)	bs_bplist;
	bstp_state_cb_t		bs_state_cb;
	bstp_rtage_cb_t		bs_rtage_cb;
	struct vnet		*bs_vnet;
};

#define	BSTP_LOCK_INIT(_bs)	mtx_init(&(_bs)->bs_mtx, "bstp", NULL, MTX_DEF)
#define	BSTP_LOCK_DESTROY(_bs)	mtx_destroy(&(_bs)->bs_mtx)
#define	BSTP_LOCK(_bs)		mtx_lock(&(_bs)->bs_mtx)
#define	BSTP_UNLOCK(_bs)	mtx_unlock(&(_bs)->bs_mtx)
#define	BSTP_LOCK_ASSERT(_bs)	mtx_assert(&(_bs)->bs_mtx, MA_OWNED)

extern const uint8_t bstp_etheraddr[];

void	bstp_attach(struct bstp_state *, struct bstp_cb_ops *);
void	bstp_detach(struct bstp_state *);
void	bstp_init(struct bstp_state *);
void	bstp_stop(struct bstp_state *);
int	bstp_create(struct bstp_state *, struct bstp_port *, struct ifnet *);
int	bstp_enable(struct bstp_port *);
void	bstp_disable(struct bstp_port *);
void	bstp_destroy(struct bstp_port *);
void	bstp_linkstate(struct bstp_port *);
int	bstp_set_htime(struct bstp_state *, int);
int	bstp_set_fdelay(struct bstp_state *, int);
int	bstp_set_maxage(struct bstp_state *, int);
int	bstp_set_holdcount(struct bstp_state *, int);
int	bstp_set_protocol(struct bstp_state *, int);
int	bstp_set_priority(struct bstp_state *, int);
int	bstp_set_port_priority(struct bstp_port *, int);
int	bstp_set_path_cost(struct bstp_port *, uint32_t);
int	bstp_set_edge(struct bstp_port *, int);
int	bstp_set_autoedge(struct bstp_port *, int);
int	bstp_set_ptp(struct bstp_port *, int);
int	bstp_set_autoptp(struct bstp_port *, int);
void	bstp_input(struct bstp_port *, struct ifnet *, struct mbuf *);

#endif /* _KERNEL */
