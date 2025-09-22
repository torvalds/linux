/*	$OpenBSD: bridgestp.c,v 1.78 2025/07/07 02:28:50 jsg Exp $	*/

/*
 * Copyright (c) 2000 Jason L. Wright (jason@thought.net)
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
 */

/*
 * Implementation of the spanning tree protocol as defined in
 * ISO/IEC 802.1D-2004, June 9, 2004.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/ioctl.h>
#include <sys/timeout.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/if_dl.h>
#include <net/if_llc.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net/if_bridge.h>

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

/* STP port flags */
#define	BSTP_PORT_CANMIGRATE	0x0001
#define	BSTP_PORT_NEWINFO	0x0002
#define	BSTP_PORT_DISPUTED	0x0004
#define	BSTP_PORT_ADMCOST	0x0008
#define	BSTP_PORT_AUTOEDGE	0x0010

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

#define	BSTP_INFO_RECEIVED	1
#define	BSTP_INFO_MINE		2
#define	BSTP_INFO_AGED		3
#define	BSTP_INFO_DISABLED	4

#define	BSTP_MESSAGE_AGE_INCR	(1 * 256)	/* in 256ths of a second */
#define	BSTP_TICK_VAL		(1 * 256)	/* in 256ths of a second */
#define	BSTP_LINK_TIMER		(BSTP_TICK_VAL * 15)

#ifdef	BRIDGESTP_DEBUG
#define	DPRINTF(bp, fmt, arg...) \
do { \
	struct ifnet *__ifp = if_get((bp)->bp_ifindex); \
	printf("bstp: %s" fmt, __ifp? __ifp->if_xname : "Unknown", ##arg); \
	if_put(__ifp); \
} while (0)
#else
#define	DPRINTF(bp, fmt, arg...)
#endif

#define	PV2ADDR(pv, eaddr)	do {		\
	eaddr[0] = pv >> 40;			\
	eaddr[1] = pv >> 32;			\
	eaddr[2] = pv >> 24;			\
	eaddr[3] = pv >> 16;			\
	eaddr[4] = pv >> 8;			\
	eaddr[5] = pv >> 0;			\
} while (0)

#define	INFO_BETTER	1
#define	INFO_SAME	0
#define	INFO_WORSE	-1

#define	BSTP_IFQ_PRIO	6

/*
 * Because BPDU's do not make nicely aligned structures, two different
 * declarations are used: bstp_?bpdu (wire representation, packed) and
 * bstp_*_unit (internal, nicely aligned version).
 */

/* configuration bridge protocol data unit */
struct bstp_cbpdu {
	u_int8_t	cbu_dsap;		/* LLC: destination sap */
	u_int8_t	cbu_ssap;		/* LLC: source sap */
	u_int8_t	cbu_ctl;		/* LLC: control */
	u_int16_t	cbu_protoid;		/* protocol id */
	u_int8_t	cbu_protover;		/* protocol version */
	u_int8_t	cbu_bpdutype;		/* message type */
	u_int8_t	cbu_flags;		/* flags (below) */

	/* root id */
	u_int16_t	cbu_rootpri;		/* root priority */
	u_int8_t	cbu_rootaddr[6];	/* root address */

	u_int32_t	cbu_rootpathcost;	/* root path cost */

	/* bridge id */
	u_int16_t	cbu_bridgepri;		/* bridge priority */
	u_int8_t	cbu_bridgeaddr[6];	/* bridge address */

	u_int16_t	cbu_portid;		/* port id */
	u_int16_t	cbu_messageage;		/* current message age */
	u_int16_t	cbu_maxage;		/* maximum age */
	u_int16_t	cbu_hellotime;		/* hello time */
	u_int16_t	cbu_forwarddelay;	/* forwarding delay */
	u_int8_t	cbu_versionlen;		/* version 1 length */
} __packed;

#define	BSTP_BPDU_STP_LEN	(3 + 35)	/* LLC + STP pdu */
#define	BSTP_BPDU_RSTP_LEN	(3 + 36)	/* LLC + RSTP pdu */

/* topology change notification bridge protocol data unit */
struct bstp_tbpdu {
	u_int8_t	tbu_dsap;		/* LLC: destination sap */
	u_int8_t	tbu_ssap;		/* LLC: source sap */
	u_int8_t	tbu_ctl;		/* LLC: control */
	u_int16_t	tbu_protoid;		/* protocol id */
	u_int8_t	tbu_protover;		/* protocol version */
	u_int8_t	tbu_bpdutype;		/* message type */
} __packed;

const u_int8_t bstp_etheraddr[] = { 0x01, 0x80, 0xc2, 0x00, 0x00, 0x00 };


void	bstp_transmit(struct bstp_state *, struct bstp_port *);
void	bstp_transmit_bpdu(struct bstp_state *, struct bstp_port *);
void	bstp_transmit_tcn(struct bstp_state *, struct bstp_port *);
void	bstp_decode_bpdu(struct bstp_port *, struct bstp_cbpdu *,
	    struct bstp_config_unit *);
void	bstp_send_bpdu(struct bstp_state *, struct bstp_port *,
	    struct bstp_cbpdu *);
int	bstp_pdu_flags(struct bstp_port *);
void	bstp_received_stp(struct bstp_state *, struct bstp_port *,
	    struct mbuf **, struct bstp_tbpdu *);
void	bstp_received_rstp(struct bstp_state *, struct bstp_port *,
	    struct mbuf **, struct bstp_tbpdu *);
void	bstp_received_tcn(struct bstp_state *, struct bstp_port *,
	    struct bstp_tcn_unit *);
void	bstp_received_bpdu(struct bstp_state *, struct bstp_port *,
	    struct bstp_config_unit *);
int	bstp_pdu_rcvtype(struct bstp_port *, struct bstp_config_unit *);
int	bstp_pdu_bettersame(struct bstp_port *, int);
int	bstp_info_cmp(struct bstp_pri_vector *,
	    struct bstp_pri_vector *);
int	bstp_info_superior(struct bstp_pri_vector *,
	    struct bstp_pri_vector *);
void	bstp_assign_roles(struct bstp_state *);
void	bstp_update_roles(struct bstp_state *, struct bstp_port *);
void	bstp_update_state(struct bstp_state *, struct bstp_port *);
void	bstp_update_tc(struct bstp_port *);
void	bstp_update_info(struct bstp_port *);
void	bstp_set_other_tcprop(struct bstp_port *);
void	bstp_set_all_reroot(struct bstp_state *);
void	bstp_set_all_sync(struct bstp_state *);
void	bstp_set_port_state(struct bstp_port *, int);
void	bstp_set_port_role(struct bstp_port *, int);
void	bstp_set_port_proto(struct bstp_port *, int);
void	bstp_set_port_tc(struct bstp_port *, int);
void	bstp_set_timer_tc(struct bstp_port *);
void	bstp_set_timer_msgage(struct bstp_port *);
void	bstp_reset(struct bstp_state *);
int	bstp_rerooted(struct bstp_state *, struct bstp_port *);
u_int32_t	bstp_calc_path_cost(struct bstp_port *);
void	bstp_notify_rtage(struct bstp_port *, int);
void	bstp_ifupdstatus(struct bstp_state *, struct bstp_port *);
void	bstp_enable_port(struct bstp_state *, struct bstp_port *);
void	bstp_disable_port(struct bstp_state *, struct bstp_port *);
void	bstp_tick(void *);
void	bstp_timer_start(struct bstp_timer *, u_int16_t);
void	bstp_timer_stop(struct bstp_timer *);
void	bstp_timer_latch(struct bstp_timer *);
int	bstp_timer_expired(struct bstp_timer *);
void	bstp_hello_timer_expiry(struct bstp_state *,
		    struct bstp_port *);
void	bstp_message_age_expiry(struct bstp_state *,
		    struct bstp_port *);
void	bstp_migrate_delay_expiry(struct bstp_state *,
		    struct bstp_port *);
void	bstp_edge_delay_expiry(struct bstp_state *,
		    struct bstp_port *);
int	bstp_addr_cmp(const u_int8_t *, const u_int8_t *);
int	bstp_same_bridgeid(u_int64_t, u_int64_t);


void
bstp_transmit(struct bstp_state *bs, struct bstp_port *bp)
{
	struct ifnet *ifp;

	if ((ifp = if_get(bs->bs_ifindex)) == NULL)
		return;

	if ((ifp->if_flags & IFF_RUNNING) == 0 || bp == NULL) {
		if_put(ifp);
		return;
	}
	if_put(ifp);

	/*
	 * a PDU can only be sent if we have tx quota left and the
	 * hello timer is running.
	 */
	if (bp->bp_hello_timer.active == 0) {
		/* Test if it needs to be reset */
		bstp_hello_timer_expiry(bs, bp);
		return;
	}
	if (bp->bp_txcount > bs->bs_txholdcount)
		/* Ran out of karma */
		return;

	if (bp->bp_protover == BSTP_PROTO_RSTP) {
		bstp_transmit_bpdu(bs, bp);
		bp->bp_tc_ack = 0;
	} else { /* STP */
		switch (bp->bp_role) {
		case BSTP_ROLE_DESIGNATED:
			bstp_transmit_bpdu(bs, bp);
			bp->bp_tc_ack = 0;
			break;

		case BSTP_ROLE_ROOT:
			bstp_transmit_tcn(bs, bp);
			break;
		}
	}
	bstp_timer_start(&bp->bp_hello_timer, bp->bp_desg_htime);
	bp->bp_flags &= ~BSTP_PORT_NEWINFO;
}

void
bstp_transmit_bpdu(struct bstp_state *bs, struct bstp_port *bp)
{
	struct bstp_cbpdu bpdu;

	bpdu.cbu_rootpri = htons(bp->bp_desg_pv.pv_root_id >> 48);
	PV2ADDR(bp->bp_desg_pv.pv_root_id, bpdu.cbu_rootaddr);

	bpdu.cbu_rootpathcost = htonl(bp->bp_desg_pv.pv_cost);

	bpdu.cbu_bridgepri = htons(bp->bp_desg_pv.pv_dbridge_id >> 48);
	PV2ADDR(bp->bp_desg_pv.pv_dbridge_id, bpdu.cbu_bridgeaddr);

	bpdu.cbu_portid = htons(bp->bp_port_id);
	bpdu.cbu_messageage = htons(bp->bp_desg_msg_age);
	bpdu.cbu_maxage = htons(bp->bp_desg_max_age);
	bpdu.cbu_hellotime = htons(bp->bp_desg_htime);
	bpdu.cbu_forwarddelay = htons(bp->bp_desg_fdelay);

	bpdu.cbu_flags = bstp_pdu_flags(bp);

	switch (bp->bp_protover) {
	case BSTP_PROTO_STP:
		bpdu.cbu_bpdutype = BSTP_MSGTYPE_CFG;
		break;
	case BSTP_PROTO_RSTP:
		bpdu.cbu_bpdutype = BSTP_MSGTYPE_RSTP;
		break;
	}

	bstp_send_bpdu(bs, bp, &bpdu);
}

void
bstp_transmit_tcn(struct bstp_state *bs, struct bstp_port *bp)
{
	struct bstp_tbpdu bpdu;
	struct ifnet *ifp;
	struct ether_header *eh;
	struct mbuf *m;

	if ((ifp = if_get(bp->bp_ifindex)) == NULL)
		return;
	if ((ifp->if_flags & IFF_RUNNING) == 0)
		goto rele;

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL)
		goto rele;
	m->m_pkthdr.ph_ifidx = ifp->if_index;
	m->m_pkthdr.len = sizeof(*eh) + sizeof(bpdu);
	m->m_pkthdr.pf.prio = BSTP_IFQ_PRIO;
	m->m_len = m->m_pkthdr.len;

	eh = mtod(m, struct ether_header *);
	bcopy(LLADDR(ifp->if_sadl), eh->ether_shost, ETHER_ADDR_LEN);
	bcopy(bstp_etheraddr, eh->ether_dhost, ETHER_ADDR_LEN);
	eh->ether_type = htons(sizeof(bpdu));

	bpdu.tbu_ssap = bpdu.tbu_dsap = LLC_8021D_LSAP;
	bpdu.tbu_ctl = LLC_UI;
	bpdu.tbu_protoid = 0;
	bpdu.tbu_protover = 0;
	bpdu.tbu_bpdutype = BSTP_MSGTYPE_TCN;
	bcopy(&bpdu, mtod(m, caddr_t) + sizeof(*eh), sizeof(bpdu));

	bp->bp_txcount++;
	if_enqueue(ifp, m);
rele:
	if_put(ifp);
}

void
bstp_decode_bpdu(struct bstp_port *bp, struct bstp_cbpdu *cpdu,
    struct bstp_config_unit *cu)
{
	int flags;

	cu->cu_pv.pv_root_id =
	    (((u_int64_t)ntohs(cpdu->cbu_rootpri)) << 48) |
	    (((u_int64_t)cpdu->cbu_rootaddr[0]) << 40) |
	    (((u_int64_t)cpdu->cbu_rootaddr[1]) << 32) |
	    (((u_int64_t)cpdu->cbu_rootaddr[2]) << 24) |
	    (((u_int64_t)cpdu->cbu_rootaddr[3]) << 16) |
	    (((u_int64_t)cpdu->cbu_rootaddr[4]) << 8) |
	    (((u_int64_t)cpdu->cbu_rootaddr[5]) << 0);

	cu->cu_pv.pv_dbridge_id =
	    (((u_int64_t)ntohs(cpdu->cbu_bridgepri)) << 48) |
	    (((u_int64_t)cpdu->cbu_bridgeaddr[0]) << 40) |
	    (((u_int64_t)cpdu->cbu_bridgeaddr[1]) << 32) |
	    (((u_int64_t)cpdu->cbu_bridgeaddr[2]) << 24) |
	    (((u_int64_t)cpdu->cbu_bridgeaddr[3]) << 16) |
	    (((u_int64_t)cpdu->cbu_bridgeaddr[4]) << 8) |
	    (((u_int64_t)cpdu->cbu_bridgeaddr[5]) << 0);

	cu->cu_pv.pv_cost = ntohl(cpdu->cbu_rootpathcost);
	cu->cu_message_age = ntohs(cpdu->cbu_messageage);
	cu->cu_max_age = ntohs(cpdu->cbu_maxage);
	cu->cu_hello_time = ntohs(cpdu->cbu_hellotime);
	cu->cu_forward_delay = ntohs(cpdu->cbu_forwarddelay);
	cu->cu_pv.pv_dport_id = ntohs(cpdu->cbu_portid);
	cu->cu_pv.pv_port_id = bp->bp_port_id;
	cu->cu_message_type = cpdu->cbu_bpdutype;

	/* Strip off unused flags in STP mode */
	flags = cpdu->cbu_flags;
	switch (cpdu->cbu_protover) {
	case BSTP_PROTO_STP:
		flags &= BSTP_PDU_STPMASK;
		/* A STP BPDU explicitly conveys a Designated Port */
		cu->cu_role = BSTP_ROLE_DESIGNATED;
		break;
	case BSTP_PROTO_RSTP:
		flags &= BSTP_PDU_RSTPMASK;
		break;
	}

	cu->cu_topology_change_ack =
		(flags & BSTP_PDU_F_TCA) ? 1 : 0;
	cu->cu_proposal =
		(flags & BSTP_PDU_F_P) ? 1 : 0;
	cu->cu_agree =
		(flags & BSTP_PDU_F_A) ? 1 : 0;
	cu->cu_learning =
		(flags & BSTP_PDU_F_L) ? 1 : 0;
	cu->cu_forwarding =
		(flags & BSTP_PDU_F_F) ? 1 : 0;
	cu->cu_topology_change =
		(flags & BSTP_PDU_F_TC) ? 1 : 0;

	switch ((flags & BSTP_PDU_PRMASK) >> BSTP_PDU_PRSHIFT) {
	case BSTP_PDU_F_ROOT:
		cu->cu_role = BSTP_ROLE_ROOT;
		break;
	case BSTP_PDU_F_ALT:
		cu->cu_role = BSTP_ROLE_ALTERNATE;
		break;
	case BSTP_PDU_F_DESG:
		cu->cu_role = BSTP_ROLE_DESIGNATED;
		break;
	}
}

void
bstp_send_bpdu(struct bstp_state *bs, struct bstp_port *bp,
    struct bstp_cbpdu *bpdu)
{
	struct ifnet *ifp;
	struct mbuf *m;
	struct ether_header *eh;
	int s;

	s = splnet();
	if ((ifp = if_get(bp->bp_ifindex)) == NULL)
		goto done;
	if ((ifp->if_flags & IFF_RUNNING) == 0)
		goto rele;

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL)
		goto rele;

	eh = mtod(m, struct ether_header *);

	bpdu->cbu_ssap = bpdu->cbu_dsap = LLC_8021D_LSAP;
	bpdu->cbu_ctl = LLC_UI;
	bpdu->cbu_protoid = htons(BSTP_PROTO_ID);

	memcpy(eh->ether_shost, LLADDR(ifp->if_sadl), ETHER_ADDR_LEN);
	memcpy(eh->ether_dhost, bstp_etheraddr, ETHER_ADDR_LEN);

	switch (bpdu->cbu_bpdutype) {
	case BSTP_MSGTYPE_CFG:
		bpdu->cbu_protover = BSTP_PROTO_STP;
		m->m_pkthdr.len = sizeof(*eh) + BSTP_BPDU_STP_LEN;
		eh->ether_type = htons(BSTP_BPDU_STP_LEN);
		memcpy(mtod(m, caddr_t) + sizeof(*eh), bpdu,
		    BSTP_BPDU_STP_LEN);
		break;
	case BSTP_MSGTYPE_RSTP:
		bpdu->cbu_protover = BSTP_PROTO_RSTP;
		bpdu->cbu_versionlen = htons(0);
		m->m_pkthdr.len = sizeof(*eh) + BSTP_BPDU_RSTP_LEN;
		eh->ether_type = htons(BSTP_BPDU_RSTP_LEN);
		memcpy(mtod(m, caddr_t) + sizeof(*eh), bpdu,
		    BSTP_BPDU_RSTP_LEN);
		break;
	default:
		panic("not implemented");
	}
	m->m_pkthdr.ph_ifidx = ifp->if_index;
	m->m_len = m->m_pkthdr.len;
	m->m_pkthdr.pf.prio = BSTP_IFQ_PRIO;

	bp->bp_txcount++;
	if_enqueue(ifp, m);
rele:
	if_put(ifp);
done:
	splx(s);
}

int
bstp_pdu_flags(struct bstp_port *bp)
{
	int flags = 0;

	if (bp->bp_proposing && bp->bp_state != BSTP_IFSTATE_FORWARDING)
		flags |= BSTP_PDU_F_P;

	if (bp->bp_agree)
		flags |= BSTP_PDU_F_A;

	if (bp->bp_tc_timer.active)
		flags |= BSTP_PDU_F_TC;

	if (bp->bp_tc_ack)
		flags |= BSTP_PDU_F_TCA;

	switch (bp->bp_state) {
	case BSTP_IFSTATE_LEARNING:
		flags |= BSTP_PDU_F_L;
		break;
	case BSTP_IFSTATE_FORWARDING:
		flags |= (BSTP_PDU_F_L | BSTP_PDU_F_F);
		break;
	}

	switch (bp->bp_role) {
	case BSTP_ROLE_ROOT:
		flags |= (BSTP_PDU_F_ROOT << BSTP_PDU_PRSHIFT);
		break;
	case BSTP_ROLE_ALTERNATE:
	case BSTP_ROLE_BACKUP:
		flags |= (BSTP_PDU_F_ALT << BSTP_PDU_PRSHIFT);
		break;
	case BSTP_ROLE_DESIGNATED:
		flags |= (BSTP_PDU_F_DESG << BSTP_PDU_PRSHIFT);
		break;
	}

	/* Strip off unused flags in either mode */
	switch (bp->bp_protover) {
	case BSTP_PROTO_STP:
		flags &= BSTP_PDU_STPMASK;
		break;
	case BSTP_PROTO_RSTP:
		flags &= BSTP_PDU_RSTPMASK;
		break;
	}
	return (flags);
}

struct mbuf *
bstp_input(struct bstp_state *bs, struct bstp_port *bp,
    struct ether_header *eh, struct mbuf *m)
{
	struct bstp_tbpdu tpdu;
	u_int16_t len;

	if (bs == NULL || bp == NULL || bp->bp_active == 0)
		return (m);

	len = ntohs(eh->ether_type);
	if (len < sizeof(tpdu))
		goto out;

	m_adj(m, ETHER_HDR_LEN);

	if (m->m_pkthdr.len > len)
		m_adj(m, len - m->m_pkthdr.len);
	if ((m = m_pullup(m, sizeof(tpdu))) == NULL)
		goto out;
	bcopy(mtod(m, struct tpdu *), &tpdu, sizeof(tpdu));

	if (tpdu.tbu_dsap != LLC_8021D_LSAP ||
	    tpdu.tbu_ssap != LLC_8021D_LSAP ||
	    tpdu.tbu_ctl != LLC_UI)
		goto out;
	if (tpdu.tbu_protoid != BSTP_PROTO_ID)
		goto out;

	/*
	 * We can treat later versions of the PDU as the same as the maximum
	 * version we implement. All additional parameters/flags are ignored.
	 */
	if (tpdu.tbu_protover > BSTP_PROTO_MAX)
		tpdu.tbu_protover = BSTP_PROTO_MAX;

	if (tpdu.tbu_protover != bp->bp_protover) {
		/*
		 * Wait for the migration delay timer to expire before changing
		 * protocol version to avoid flip-flops.
		 */
		if (bp->bp_flags & BSTP_PORT_CANMIGRATE)
			bstp_set_port_proto(bp, tpdu.tbu_protover);
		else
			goto out;
	}

	/* Clear operedge upon receiving a PDU on the port */
	bp->bp_operedge = 0;
	bstp_timer_start(&bp->bp_edge_delay_timer,
	    BSTP_DEFAULT_MIGRATE_DELAY);

	switch (tpdu.tbu_protover) {
	case BSTP_PROTO_STP:
		bstp_received_stp(bs, bp, &m, &tpdu);
		break;
	case BSTP_PROTO_RSTP:
		bstp_received_rstp(bs, bp, &m, &tpdu);
		break;
	}
 out:
	m_freem(m);
	return (NULL);
}

void
bstp_received_stp(struct bstp_state *bs, struct bstp_port *bp,
    struct mbuf **mp, struct bstp_tbpdu *tpdu)
{
	struct bstp_cbpdu cpdu;
	struct bstp_config_unit *cu = &bp->bp_msg_cu;
	struct bstp_tcn_unit tu;

	switch (tpdu->tbu_bpdutype) {
	case BSTP_MSGTYPE_TCN:
		tu.tu_message_type = tpdu->tbu_bpdutype;
		bstp_received_tcn(bs, bp, &tu);
		break;
	case BSTP_MSGTYPE_CFG:
		if ((*mp)->m_len < BSTP_BPDU_STP_LEN &&
		    (*mp = m_pullup(*mp, BSTP_BPDU_STP_LEN)) == NULL)
			return;
		memcpy(&cpdu, mtod(*mp, caddr_t), BSTP_BPDU_STP_LEN);

		bstp_decode_bpdu(bp, &cpdu, cu);
		bstp_received_bpdu(bs, bp, cu);
		break;
	}
}

void
bstp_received_rstp(struct bstp_state *bs, struct bstp_port *bp,
    struct mbuf **mp, struct bstp_tbpdu *tpdu)
{
	struct bstp_cbpdu cpdu;
	struct bstp_config_unit *cu = &bp->bp_msg_cu;

	if (tpdu->tbu_bpdutype != BSTP_MSGTYPE_RSTP)
		return;

	if ((*mp)->m_len < BSTP_BPDU_RSTP_LEN &&
	    (*mp = m_pullup(*mp, BSTP_BPDU_RSTP_LEN)) == NULL)
		return;
	memcpy(&cpdu, mtod(*mp, caddr_t), BSTP_BPDU_RSTP_LEN);

	bstp_decode_bpdu(bp, &cpdu, cu);
	bstp_received_bpdu(bs, bp, cu);
}

void
bstp_received_tcn(struct bstp_state *bs, struct bstp_port *bp,
    struct bstp_tcn_unit *tcn)
{
	bp->bp_rcvdtcn = 1;
	bstp_update_tc(bp);
}

void
bstp_received_bpdu(struct bstp_state *bs, struct bstp_port *bp,
    struct bstp_config_unit *cu)
{
	int type;

	/* We need to have transitioned to INFO_MINE before proceeding */
	switch (bp->bp_infois) {
	case BSTP_INFO_DISABLED:
	case BSTP_INFO_AGED:
		return;
	}

	type = bstp_pdu_rcvtype(bp, cu);

	switch (type) {
	case BSTP_PDU_SUPERIOR:
		bs->bs_allsynced = 0;
		bp->bp_agreed = 0;
		bp->bp_proposing = 0;

		if (cu->cu_proposal && cu->cu_forwarding == 0)
			bp->bp_proposed = 1;
		if (cu->cu_topology_change)
			bp->bp_rcvdtc = 1;
		if (cu->cu_topology_change_ack)
			bp->bp_rcvdtca = 1;

		if (bp->bp_agree &&
		    !bstp_pdu_bettersame(bp, BSTP_INFO_RECEIVED))
			bp->bp_agree = 0;

		/* copy the received priority and timers to the port */
		bp->bp_port_pv = cu->cu_pv;
		bp->bp_port_msg_age = cu->cu_message_age;
		bp->bp_port_max_age = cu->cu_max_age;
		bp->bp_port_fdelay = cu->cu_forward_delay;
		bp->bp_port_htime =
		    (cu->cu_hello_time > BSTP_MIN_HELLO_TIME ?
		     cu->cu_hello_time : BSTP_MIN_HELLO_TIME);

		/* set expiry for the new info */
		bstp_set_timer_msgage(bp);

		bp->bp_infois = BSTP_INFO_RECEIVED;
		bstp_assign_roles(bs);
		break;

	case BSTP_PDU_REPEATED:
		if (cu->cu_proposal && cu->cu_forwarding == 0)
			bp->bp_proposed = 1;
		if (cu->cu_topology_change)
			bp->bp_rcvdtc = 1;
		if (cu->cu_topology_change_ack)
			bp->bp_rcvdtca = 1;

		/* rearm the age timer */
		bstp_set_timer_msgage(bp);
		break;

	case BSTP_PDU_INFERIOR:
		if (cu->cu_learning) {
			bp->bp_agreed = 1;
			bp->bp_proposing = 0;
		}
		break;

	case BSTP_PDU_INFERIORALT:
		/*
		 * only point to point links are allowed fast
		 * transitions to forwarding.
		 */
		if (cu->cu_agree && bp->bp_ptp_link) {
			bp->bp_agreed = 1;
			bp->bp_proposing = 0;
		} else
			bp->bp_agreed = 0;

		if (cu->cu_topology_change)
			bp->bp_rcvdtc = 1;
		if (cu->cu_topology_change_ack)
			bp->bp_rcvdtca = 1;
		break;

	case BSTP_PDU_OTHER:
		return;	/* do nothing */
	}

	/* update the state machines with the new data */
	bstp_update_state(bs, bp);
}

int
bstp_pdu_rcvtype(struct bstp_port *bp, struct bstp_config_unit *cu)
{
	int type;

	/* default return type */
	type = BSTP_PDU_OTHER;

	switch (cu->cu_role) {
	case BSTP_ROLE_DESIGNATED:
		if (bstp_info_superior(&bp->bp_port_pv, &cu->cu_pv))
			/* bpdu priority is superior */
			type = BSTP_PDU_SUPERIOR;
		else if (bstp_info_cmp(&bp->bp_port_pv, &cu->cu_pv) ==
		    INFO_SAME) {
			if (bp->bp_port_msg_age != cu->cu_message_age ||
			    bp->bp_port_max_age != cu->cu_max_age ||
			    bp->bp_port_fdelay != cu->cu_forward_delay ||
			    bp->bp_port_htime != cu->cu_hello_time)
				/* bpdu priority is equal and timers differ */
				type = BSTP_PDU_SUPERIOR;
			else
				/* bpdu is equal */
				type = BSTP_PDU_REPEATED;
		} else
			/* bpdu priority is worse */
			type = BSTP_PDU_INFERIOR;

		break;

	case BSTP_ROLE_ROOT:
	case BSTP_ROLE_ALTERNATE:
	case BSTP_ROLE_BACKUP:
		if (bstp_info_cmp(&bp->bp_port_pv, &cu->cu_pv) <= INFO_SAME)
			/*
			 * not a designated port and priority is the same or
			 * worse
			 */
			type = BSTP_PDU_INFERIORALT;
		break;
	}

	return (type);
}

int
bstp_pdu_bettersame(struct bstp_port *bp, int newinfo)
{
	if (newinfo == BSTP_INFO_RECEIVED &&
	    bp->bp_infois == BSTP_INFO_RECEIVED &&
	    bstp_info_cmp(&bp->bp_port_pv, &bp->bp_msg_cu.cu_pv) >= INFO_SAME)
		return (1);

	if (newinfo == BSTP_INFO_MINE &&
	    bp->bp_infois == BSTP_INFO_MINE &&
	    bstp_info_cmp(&bp->bp_port_pv, &bp->bp_desg_pv) >= INFO_SAME)
		return (1);

	return (0);
}

int
bstp_info_cmp(struct bstp_pri_vector *pv,
    struct bstp_pri_vector *cpv)
{
	if (cpv->pv_root_id < pv->pv_root_id)
		return (INFO_BETTER);
	if (cpv->pv_root_id > pv->pv_root_id)
		return (INFO_WORSE);

	if (cpv->pv_cost < pv->pv_cost)
		return (INFO_BETTER);
	if (cpv->pv_cost > pv->pv_cost)
		return (INFO_WORSE);

	if (cpv->pv_dbridge_id < pv->pv_dbridge_id)
		return (INFO_BETTER);
	if (cpv->pv_dbridge_id > pv->pv_dbridge_id)
		return (INFO_WORSE);

	if (cpv->pv_dport_id < pv->pv_dport_id)
		return (INFO_BETTER);
	if (cpv->pv_dport_id > pv->pv_dport_id)
		return (INFO_WORSE);

	return (INFO_SAME);
}

/*
 * This message priority vector is superior to the port priority vector and
 * will replace it if, and only if, the message priority vector is better than
 * the port priority vector, or the message has been transmitted from the same
 * designated bridge and designated port as the port priority vector.
 */
int
bstp_info_superior(struct bstp_pri_vector *pv,
    struct bstp_pri_vector *cpv)
{
	if (bstp_info_cmp(pv, cpv) == INFO_BETTER ||
	    (bstp_same_bridgeid(pv->pv_dbridge_id, cpv->pv_dbridge_id) &&
	    (cpv->pv_dport_id & 0xfff) == (pv->pv_dport_id & 0xfff)))
		return (1);
	return (0);
}

void
bstp_reset(struct bstp_state *bs)
{
	/* default to our priority vector */
	bs->bs_root_pv = bs->bs_bridge_pv;
	bs->bs_root_msg_age = 0;
	bs->bs_root_max_age = bs->bs_bridge_max_age;
	bs->bs_root_fdelay = bs->bs_bridge_fdelay;
	bs->bs_root_htime = bs->bs_bridge_htime;
	bs->bs_root_port = NULL;
}

void
bstp_assign_roles(struct bstp_state *bs)
{
	struct bstp_port *bp, *rbp = NULL;
	struct bstp_pri_vector pv;

	bstp_reset(bs);

	/* check if any received info supersedes us */
	LIST_FOREACH(bp, &bs->bs_bplist, bp_next) {
		if (bp->bp_infois != BSTP_INFO_RECEIVED)
			continue;

		pv = bp->bp_port_pv;
		pv.pv_cost += bp->bp_path_cost;

		/*
		 * The root priority vector is the best of the set comprising
		 * the bridge priority vector plus all root path priority
		 * vectors whose bridge address is not equal to us.
		 */
		if (bstp_same_bridgeid(pv.pv_dbridge_id,
		    bs->bs_bridge_pv.pv_dbridge_id) == 0 &&
		    bstp_info_cmp(&bs->bs_root_pv, &pv) == INFO_BETTER) {
			/* the port vector replaces the root */
			bs->bs_root_pv = pv;
			bs->bs_root_msg_age = bp->bp_port_msg_age +
			    BSTP_MESSAGE_AGE_INCR;
			bs->bs_root_max_age = bp->bp_port_max_age;
			bs->bs_root_fdelay = bp->bp_port_fdelay;
			bs->bs_root_htime = bp->bp_port_htime;
			rbp = bp;
		}
	}

	LIST_FOREACH(bp, &bs->bs_bplist, bp_next) {
		/* calculate the port designated vector */
		bp->bp_desg_pv.pv_root_id = bs->bs_root_pv.pv_root_id;
		bp->bp_desg_pv.pv_cost = bs->bs_root_pv.pv_cost;
		bp->bp_desg_pv.pv_dbridge_id = bs->bs_bridge_pv.pv_dbridge_id;
		bp->bp_desg_pv.pv_dport_id = bp->bp_port_id;
		bp->bp_desg_pv.pv_port_id = bp->bp_port_id;

		/* calculate designated times */
		bp->bp_desg_msg_age = bs->bs_root_msg_age;
		bp->bp_desg_max_age = bs->bs_root_max_age;
		bp->bp_desg_fdelay = bs->bs_root_fdelay;
		bp->bp_desg_htime = bs->bs_bridge_htime;


		switch (bp->bp_infois) {
		case BSTP_INFO_DISABLED:
			bstp_set_port_role(bp, BSTP_ROLE_DISABLED);
			break;

		case BSTP_INFO_AGED:
			bstp_set_port_role(bp, BSTP_ROLE_DESIGNATED);
			bstp_update_info(bp);
			break;

		case BSTP_INFO_MINE:
			bstp_set_port_role(bp, BSTP_ROLE_DESIGNATED);
			/* update the port info if stale */
			if (bstp_info_cmp(&bp->bp_port_pv,
			    &bp->bp_desg_pv) != INFO_SAME ||
			    (rbp != NULL &&
			    (bp->bp_port_msg_age != rbp->bp_port_msg_age ||
			    bp->bp_port_max_age != rbp->bp_port_max_age ||
			    bp->bp_port_fdelay != rbp->bp_port_fdelay ||
			    bp->bp_port_htime != rbp->bp_port_htime)))
				bstp_update_info(bp);
			break;

		case BSTP_INFO_RECEIVED:
			if (bp == rbp) {
				/*
				 * root priority is derived from this
				 * port, make it the root port.
				 */
				bstp_set_port_role(bp, BSTP_ROLE_ROOT);
				bs->bs_root_port = bp;
			} else if (bstp_info_cmp(&bp->bp_port_pv,
				    &bp->bp_desg_pv) == INFO_BETTER) {
				/*
				 * the port priority is lower than the root
				 * port.
				 */
				bstp_set_port_role(bp, BSTP_ROLE_DESIGNATED);
				bstp_update_info(bp);
			} else {
				if (bstp_same_bridgeid(
				    bp->bp_port_pv.pv_dbridge_id,
				    bs->bs_bridge_pv.pv_dbridge_id)) {
					/*
					 * the designated bridge refers to
					 * another port on this bridge.
					 */
					bstp_set_port_role(bp,
					    BSTP_ROLE_BACKUP);
				} else {
					/*
					 * the port is an inferior path to the
					 * root bridge.
					 */
					bstp_set_port_role(bp,
					    BSTP_ROLE_ALTERNATE);
				}
			}
			break;
		}
	}
}

void
bstp_update_state(struct bstp_state *bs, struct bstp_port *bp)
{
	struct bstp_port *bp2;
	int synced;

	/* check if all the ports have synchronized again */
	if (!bs->bs_allsynced) {
		synced = 1;
		LIST_FOREACH(bp2, &bs->bs_bplist, bp_next) {
			if (!(bp2->bp_synced ||
			     bp2->bp_role == BSTP_ROLE_ROOT)) {
				synced = 0;
				break;
			}
		}
		bs->bs_allsynced = synced;
	}

	bstp_update_roles(bs, bp);
	bstp_update_tc(bp);
}

void
bstp_update_roles(struct bstp_state *bs, struct bstp_port *bp)
{
	switch (bp->bp_role) {
	case BSTP_ROLE_DISABLED:
		/* Clear any flags if set */
		if (bp->bp_sync || !bp->bp_synced || bp->bp_reroot) {
			bp->bp_sync = 0;
			bp->bp_synced = 1;
			bp->bp_reroot = 0;
		}
		break;

	case BSTP_ROLE_ALTERNATE:
	case BSTP_ROLE_BACKUP:
		if ((bs->bs_allsynced && !bp->bp_agree) ||
		    (bp->bp_proposed && bp->bp_agree)) {
			bp->bp_proposed = 0;
			bp->bp_agree = 1;
			bp->bp_flags |= BSTP_PORT_NEWINFO;
			DPRINTF(bp, "-> ALTERNATE_AGREED\n");
		}

		if (bp->bp_proposed && !bp->bp_agree) {
			bstp_set_all_sync(bs);
			bp->bp_proposed = 0;
			DPRINTF(bp, "-> ALTERNATE_PROPOSED\n");
		}

		/* Clear any flags if set */
		if (bp->bp_sync || !bp->bp_synced || bp->bp_reroot) {
			bp->bp_sync = 0;
			bp->bp_synced = 1;
			bp->bp_reroot = 0;
			DPRINTF(bp, "-> ALTERNATE_PORT\n");
		}
		break;

	case BSTP_ROLE_ROOT:
		if (bp->bp_state != BSTP_IFSTATE_FORWARDING && !bp->bp_reroot) {
			bstp_set_all_reroot(bs);
			DPRINTF(bp, "-> ROOT_REROOT\n");
		}

		if ((bs->bs_allsynced && !bp->bp_agree) ||
		    (bp->bp_proposed && bp->bp_agree)) {
			bp->bp_proposed = 0;
			bp->bp_sync = 0;
			bp->bp_agree = 1;
			bp->bp_flags |= BSTP_PORT_NEWINFO;
			DPRINTF(bp, "-> ROOT_AGREED\n");
		}

		if (bp->bp_proposed && !bp->bp_agree) {
			bstp_set_all_sync(bs);
			bp->bp_proposed = 0;
			DPRINTF(bp, "-> ROOT_PROPOSED\n");
		}

		if (bp->bp_state != BSTP_IFSTATE_FORWARDING &&
		    (bp->bp_forward_delay_timer.active == 0 ||
		    (bstp_rerooted(bs, bp) &&
		    bp->bp_recent_backup_timer.active == 0 &&
		    bp->bp_protover == BSTP_PROTO_RSTP))) {
			switch (bp->bp_state) {
			case BSTP_IFSTATE_DISCARDING:
				bstp_set_port_state(bp, BSTP_IFSTATE_LEARNING);
				break;
			case BSTP_IFSTATE_LEARNING:
				bstp_set_port_state(bp,
				    BSTP_IFSTATE_FORWARDING);
				break;
			}
		}

		if (bp->bp_state == BSTP_IFSTATE_FORWARDING && bp->bp_reroot) {
			bp->bp_reroot = 0;
			DPRINTF(bp, "-> ROOT_REROOTED\n");
		}
		break;

	case BSTP_ROLE_DESIGNATED:
		if (bp->bp_recent_root_timer.active == 0 && bp->bp_reroot) {
			bp->bp_reroot = 0;
			DPRINTF(bp, "-> DESIGNATED_RETIRED\n");
		}

		if ((bp->bp_state == BSTP_IFSTATE_DISCARDING &&
		    !bp->bp_synced) || (bp->bp_agreed && !bp->bp_synced) ||
		    (bp->bp_operedge && !bp->bp_synced) ||
		    (bp->bp_sync && bp->bp_synced)) {
			bstp_timer_stop(&bp->bp_recent_root_timer);
			bp->bp_synced = 1;
			bp->bp_sync = 0;
			DPRINTF(bp, "-> DESIGNATED_SYNCED\n");
		}

		if (bp->bp_state != BSTP_IFSTATE_FORWARDING &&
		    !bp->bp_agreed && !bp->bp_proposing &&
		    !bp->bp_operedge) {
			bp->bp_proposing = 1;
			bp->bp_flags |= BSTP_PORT_NEWINFO;
			bstp_timer_start(&bp->bp_edge_delay_timer,
			    (bp->bp_ptp_link ? BSTP_DEFAULT_MIGRATE_DELAY :
			     bp->bp_desg_max_age));
			DPRINTF(bp, "-> DESIGNATED_PROPOSE\n");
		}

		if (bp->bp_state != BSTP_IFSTATE_FORWARDING &&
		    (bp->bp_forward_delay_timer.active == 0 || bp->bp_agreed ||
		    bp->bp_operedge) &&
		    (bp->bp_recent_root_timer.active == 0 || !bp->bp_reroot) &&
		    !bp->bp_sync) {
			if (bp->bp_agreed)
				DPRINTF(bp, "-> AGREED\n");
			/*
			 * If agreed|operedge then go straight to forwarding,
			 * otherwise follow discard -> learn -> forward.
			 */
			if (bp->bp_agreed || bp->bp_operedge ||
			    bp->bp_state == BSTP_IFSTATE_LEARNING) {
				bstp_set_port_state(bp,
				    BSTP_IFSTATE_FORWARDING);
				bp->bp_agreed = bp->bp_protover;
			} else if (bp->bp_state == BSTP_IFSTATE_DISCARDING)
				bstp_set_port_state(bp, BSTP_IFSTATE_LEARNING);
		}

		if (((bp->bp_sync && !bp->bp_synced) ||
		    (bp->bp_reroot && bp->bp_recent_root_timer.active) ||
		    (bp->bp_flags & BSTP_PORT_DISPUTED)) && !bp->bp_operedge &&
		    bp->bp_state != BSTP_IFSTATE_DISCARDING) {
			bstp_set_port_state(bp, BSTP_IFSTATE_DISCARDING);
			bp->bp_flags &= ~BSTP_PORT_DISPUTED;
			bstp_timer_start(&bp->bp_forward_delay_timer,
			    bp->bp_protover == BSTP_PROTO_RSTP ?
			    bp->bp_desg_htime : bp->bp_desg_fdelay);
			DPRINTF(bp, "-> DESIGNATED_DISCARD\n");
		}
		break;
	}

	if (bp->bp_flags & BSTP_PORT_NEWINFO)
		bstp_transmit(bs, bp);
}

void
bstp_update_tc(struct bstp_port *bp)
{
	switch (bp->bp_tcstate) {
	case BSTP_TCSTATE_ACTIVE:
		if ((bp->bp_role != BSTP_ROLE_DESIGNATED &&
		    bp->bp_role != BSTP_ROLE_ROOT) || bp->bp_operedge)
			bstp_set_port_tc(bp, BSTP_TCSTATE_LEARNING);

		if (bp->bp_rcvdtcn)
			bstp_set_port_tc(bp, BSTP_TCSTATE_TCN);
		if (bp->bp_rcvdtc)
			bstp_set_port_tc(bp, BSTP_TCSTATE_TC);

		if (bp->bp_tc_prop && !bp->bp_operedge)
			bstp_set_port_tc(bp, BSTP_TCSTATE_PROPAG);

		if (bp->bp_rcvdtca)
			bstp_set_port_tc(bp, BSTP_TCSTATE_ACK);
		break;

	case BSTP_TCSTATE_INACTIVE:
		if ((bp->bp_state == BSTP_IFSTATE_LEARNING ||
		    bp->bp_state == BSTP_IFSTATE_FORWARDING) &&
		    bp->bp_fdbflush == 0)
			bstp_set_port_tc(bp, BSTP_TCSTATE_LEARNING);
		break;

	case BSTP_TCSTATE_LEARNING:
		if (bp->bp_rcvdtc || bp->bp_rcvdtcn || bp->bp_rcvdtca ||
		    bp->bp_tc_prop)
			bstp_set_port_tc(bp, BSTP_TCSTATE_LEARNING);
		else if (bp->bp_role != BSTP_ROLE_DESIGNATED &&
			 bp->bp_role != BSTP_ROLE_ROOT &&
			 bp->bp_state == BSTP_IFSTATE_DISCARDING)
			bstp_set_port_tc(bp, BSTP_TCSTATE_INACTIVE);

		if ((bp->bp_role == BSTP_ROLE_DESIGNATED ||
		    bp->bp_role == BSTP_ROLE_ROOT) &&
		    bp->bp_state == BSTP_IFSTATE_FORWARDING &&
		    !bp->bp_operedge)
			bstp_set_port_tc(bp, BSTP_TCSTATE_DETECTED);
		break;

	/* these are transient states and go straight back to ACTIVE */
	case BSTP_TCSTATE_DETECTED:
	case BSTP_TCSTATE_TCN:
	case BSTP_TCSTATE_TC:
	case BSTP_TCSTATE_PROPAG:
	case BSTP_TCSTATE_ACK:
		DPRINTF(bp, "Invalid TC state\n");
		break;
	}

}

void
bstp_update_info(struct bstp_port *bp)
{
	struct bstp_state *bs = bp->bp_bs;

	bp->bp_proposing = 0;
	bp->bp_proposed = 0;

	if (bp->bp_agreed && !bstp_pdu_bettersame(bp, BSTP_INFO_MINE))
		bp->bp_agreed = 0;

	if (bp->bp_synced && !bp->bp_agreed) {
		bp->bp_synced = 0;
		bs->bs_allsynced = 0;
	}

	/* copy the designated pv to the port */
	bp->bp_port_pv = bp->bp_desg_pv;
	bp->bp_port_msg_age = bp->bp_desg_msg_age;
	bp->bp_port_max_age = bp->bp_desg_max_age;
	bp->bp_port_fdelay = bp->bp_desg_fdelay;
	bp->bp_port_htime = bp->bp_desg_htime;
	bp->bp_infois = BSTP_INFO_MINE;

	/* Set transmit flag but do not immediately send */
	bp->bp_flags |= BSTP_PORT_NEWINFO;
}

/* set tcprop on every port other than the caller */
void
bstp_set_other_tcprop(struct bstp_port *bp)
{
	struct bstp_state *bs = bp->bp_bs;
	struct bstp_port *bp2;

	LIST_FOREACH(bp2, &bs->bs_bplist, bp_next) {
		if (bp2 == bp)
			continue;
		bp2->bp_tc_prop = 1;
	}
}

void
bstp_set_all_reroot(struct bstp_state *bs)
{
	struct bstp_port *bp;

	LIST_FOREACH(bp, &bs->bs_bplist, bp_next)
		bp->bp_reroot = 1;
}

void
bstp_set_all_sync(struct bstp_state *bs)
{
	struct bstp_port *bp;

	LIST_FOREACH(bp, &bs->bs_bplist, bp_next) {
		bp->bp_sync = 1;
		bp->bp_synced = 0;	/* Not explicit in spec */
	}

	bs->bs_allsynced = 0;
}

void
bstp_set_port_state(struct bstp_port *bp, int state)
{
	if (bp->bp_state == state)
		return;

	bp->bp_state = state;

	switch (bp->bp_state) {
	case BSTP_IFSTATE_DISCARDING:
		DPRINTF(bp, "state changed to DISCARDING\n");
		break;

	case BSTP_IFSTATE_LEARNING:
		DPRINTF(bp, "state changed to LEARNING\n");
		bstp_timer_start(&bp->bp_forward_delay_timer,
		    bp->bp_protover == BSTP_PROTO_RSTP ?
		    bp->bp_desg_htime : bp->bp_desg_fdelay);
		break;

	case BSTP_IFSTATE_FORWARDING:
		DPRINTF(bp, "state changed to FORWARDING\n");
		bstp_timer_stop(&bp->bp_forward_delay_timer);
		/* Record that we enabled forwarding */
		bp->bp_forward_transitions++;
		break;
	}
}

void
bstp_set_port_role(struct bstp_port *bp, int role)
{
	struct bstp_state *bs = bp->bp_bs;

	if (bp->bp_role == role)
		return;

	/* perform pre-change tasks */
	switch (bp->bp_role) {
	case BSTP_ROLE_DISABLED:
		bstp_timer_start(&bp->bp_forward_delay_timer,
		    bp->bp_desg_max_age);
		break;

	case BSTP_ROLE_BACKUP:
		bstp_timer_start(&bp->bp_recent_backup_timer,
		    bp->bp_desg_htime * 2);
		/* FALLTHROUGH */
	case BSTP_ROLE_ALTERNATE:
		bstp_timer_start(&bp->bp_forward_delay_timer,
		    bp->bp_desg_fdelay);
		bp->bp_sync = 0;
		bp->bp_synced = 1;
		bp->bp_reroot = 0;
		break;

	case BSTP_ROLE_ROOT:
		bstp_timer_start(&bp->bp_recent_root_timer,
		    BSTP_DEFAULT_FORWARD_DELAY);
		break;
	}

	bp->bp_role = role;
	/* clear values not carried between roles */
	bp->bp_proposing = 0;
	bs->bs_allsynced = 0;

	/* initialise the new role */
	switch (bp->bp_role) {
	case BSTP_ROLE_DISABLED:
	case BSTP_ROLE_ALTERNATE:
	case BSTP_ROLE_BACKUP:
		DPRINTF(bp, "role -> ALT/BACK/DISABLED\n");
		bstp_set_port_state(bp, BSTP_IFSTATE_DISCARDING);
		bstp_timer_stop(&bp->bp_recent_root_timer);
		bstp_timer_latch(&bp->bp_forward_delay_timer);
		bp->bp_sync = 0;
		bp->bp_synced = 1;
		bp->bp_reroot = 0;
		break;

	case BSTP_ROLE_ROOT:
		DPRINTF(bp, "role -> ROOT\n");
		bstp_set_port_state(bp, BSTP_IFSTATE_DISCARDING);
		bstp_timer_latch(&bp->bp_recent_root_timer);
		bp->bp_proposing = 0;
		break;

	case BSTP_ROLE_DESIGNATED:
		DPRINTF(bp, "role -> DESIGNATED\n");
		bstp_timer_start(&bp->bp_hello_timer,
		    bp->bp_desg_htime);
		bp->bp_agree = 0;
		break;
	}

	/* let the TC state know that the role changed */
	bstp_update_tc(bp);
}

void
bstp_set_port_proto(struct bstp_port *bp, int proto)
{
	struct bstp_state *bs = bp->bp_bs;

	/* supported protocol versions */
	switch (proto) {
	case BSTP_PROTO_STP:
		/* we can downgrade protocols only */
		bstp_timer_stop(&bp->bp_migrate_delay_timer);
		/* clear unsupported features */
		bp->bp_operedge = 0;
		break;

	case BSTP_PROTO_RSTP:
		bstp_timer_start(&bp->bp_migrate_delay_timer,
		    bs->bs_migration_delay);
		break;

	default:
		DPRINTF(bp, "Unsupported STP version %d\n", proto);
		return;
	}

	bp->bp_protover = proto;
	bp->bp_flags &= ~BSTP_PORT_CANMIGRATE;
}

void
bstp_set_port_tc(struct bstp_port *bp, int state)
{
	struct bstp_state *bs = bp->bp_bs;

	bp->bp_tcstate = state;

	/* initialise the new state */
	switch (bp->bp_tcstate) {
	case BSTP_TCSTATE_ACTIVE:
		DPRINTF(bp, "-> TC_ACTIVE\n");
		/* nothing to do */
		break;

	case BSTP_TCSTATE_INACTIVE:
		bstp_timer_stop(&bp->bp_tc_timer);
		/* flush routes on the parent bridge */
		bp->bp_fdbflush = 1;
		bstp_notify_rtage(bp, 0);
		bp->bp_tc_ack = 0;
		DPRINTF(bp, "-> TC_INACTIVE\n");
		break;

	case BSTP_TCSTATE_LEARNING:
		bp->bp_rcvdtc = 0;
		bp->bp_rcvdtcn = 0;
		bp->bp_rcvdtca = 0;
		bp->bp_tc_prop = 0;
		DPRINTF(bp, "-> TC_LEARNING\n");
		break;

	case BSTP_TCSTATE_DETECTED:
		bstp_set_timer_tc(bp);
		bstp_set_other_tcprop(bp);
		/* send out notification */
		bp->bp_flags |= BSTP_PORT_NEWINFO;
		bstp_transmit(bs, bp);
		getmicrotime(&bs->bs_last_tc_time);
		DPRINTF(bp, "-> TC_DETECTED\n");
		bp->bp_tcstate = BSTP_TCSTATE_ACTIVE; /* UCT */
		break;

	case BSTP_TCSTATE_TCN:
		bstp_set_timer_tc(bp);
		DPRINTF(bp, "-> TC_TCN\n");
		/* FALLTHROUGH */
	case BSTP_TCSTATE_TC:
		bp->bp_rcvdtc = 0;
		bp->bp_rcvdtcn = 0;
		if (bp->bp_role == BSTP_ROLE_DESIGNATED)
			bp->bp_tc_ack = 1;

		bstp_set_other_tcprop(bp);
		DPRINTF(bp, "-> TC_TC\n");
		bp->bp_tcstate = BSTP_TCSTATE_ACTIVE; /* UCT */
		break;

	case BSTP_TCSTATE_PROPAG:
		/* flush routes on the parent bridge */
		bp->bp_fdbflush = 1;
		bstp_notify_rtage(bp, 0);
		bp->bp_tc_prop = 0;
		bstp_set_timer_tc(bp);
		DPRINTF(bp, "-> TC_PROPAG\n");
		bp->bp_tcstate = BSTP_TCSTATE_ACTIVE; /* UCT */
		break;

	case BSTP_TCSTATE_ACK:
		bstp_timer_stop(&bp->bp_tc_timer);
		bp->bp_rcvdtca = 0;
		DPRINTF(bp, "-> TC_ACK\n");
		bp->bp_tcstate = BSTP_TCSTATE_ACTIVE; /* UCT */
		break;
	}
}

void
bstp_set_timer_tc(struct bstp_port *bp)
{
	struct bstp_state *bs = bp->bp_bs;

	if (bp->bp_tc_timer.active)
		return;

	switch (bp->bp_protover) {
	case BSTP_PROTO_RSTP:
		bstp_timer_start(&bp->bp_tc_timer,
		    bp->bp_desg_htime + BSTP_TICK_VAL);
		bp->bp_flags |= BSTP_PORT_NEWINFO;
		break;
	case BSTP_PROTO_STP:
		bstp_timer_start(&bp->bp_tc_timer,
		    bs->bs_root_max_age + bs->bs_root_fdelay);
		break;
	}
}

void
bstp_set_timer_msgage(struct bstp_port *bp)
{
	if (bp->bp_port_msg_age + BSTP_MESSAGE_AGE_INCR <=
	    bp->bp_port_max_age) {
		bstp_timer_start(&bp->bp_message_age_timer,
		    bp->bp_port_htime * 3);
	} else
		/* expires immediately */
		bstp_timer_start(&bp->bp_message_age_timer, 0);
}

int
bstp_rerooted(struct bstp_state *bs, struct bstp_port *bp)
{
	struct bstp_port *bp2;
	int rr_set = 0;

	LIST_FOREACH(bp2, &bs->bs_bplist, bp_next) {
		if (bp2 == bp)
			continue;
		if (bp2->bp_recent_root_timer.active) {
			rr_set = 1;
			break;
		}
	}
	return (!rr_set);
}

/*
 * Calculate the path cost according to the link speed.
 */
u_int32_t
bstp_calc_path_cost(struct bstp_port *bp)
{
	struct ifnet *ifp;
	u_int32_t path_cost;

	/* If the priority has been manually set then retain the value */
	if (bp->bp_flags & BSTP_PORT_ADMCOST)
		return bp->bp_path_cost;

	if ((ifp = if_get(bp->bp_ifindex)) == NULL) {
		return bp->bp_path_cost;
	}

	if (ifp->if_baudrate < 1000) {
		if_put(ifp);
		return (BSTP_DEFAULT_PATH_COST);
	}

 	/* formula from section 17.14, IEEE Std 802.1D-2004 */
	path_cost = 20000000000ULL / (ifp->if_baudrate / 1000);
	if_put(ifp);

	if (path_cost > BSTP_MAX_PATH_COST)
		path_cost = BSTP_MAX_PATH_COST;

	/* STP compat mode only uses 16 bits of the 32 */
	if (bp->bp_protover == BSTP_PROTO_STP && path_cost > 65535)
		path_cost = 65535;

	return (path_cost);
}

void
bstp_notify_rtage(struct bstp_port *bp, int pending)
{
	int age = 0;

	KERNEL_ASSERT_LOCKED();

	switch (bp->bp_protover) {
	case BSTP_PROTO_STP:
		/* convert to seconds */
		age = bp->bp_desg_fdelay / BSTP_TICK_VAL;
		break;
	case BSTP_PROTO_RSTP:
		age = 0;
		break;
	}

	if (bp->bp_active == 1) {
		struct ifnet *ifp;

		if ((ifp = if_get(bp->bp_ifindex)) != NULL)
			bridge_rtagenode(ifp, age);
		if_put(ifp);
	}

	/* flush is complete */
	bp->bp_fdbflush = 0;
}

void
bstp_ifstate(void *arg)
{
	struct ifnet *ifp = (struct ifnet *)arg;
	struct bridge_iflist *bif;
	struct bstp_port *bp;
	struct bstp_state *bs;
	int s;

	if (ifp->if_type == IFT_BRIDGE)
		return;

	s = splnet();
	if ((bif = bridge_getbif(ifp)) == NULL)
		goto done;
	if ((bif->bif_flags & IFBIF_STP) == 0)
		goto done;
	if ((bp = bif->bif_stp) == NULL)
		goto done;
	if ((bs = bp->bp_bs) == NULL)
		goto done;

	/* update the link state */
	bstp_ifupdstatus(bs, bp);
	bstp_update_state(bs, bp);
 done:
	splx(s);
}

void
bstp_ifupdstatus(struct bstp_state *bs, struct bstp_port *bp)
{
	struct ifnet *ifp;

	if ((ifp = if_get(bp->bp_ifindex)) == NULL)
		return;

	bp->bp_path_cost = bstp_calc_path_cost(bp);

	if ((ifp->if_flags & IFF_UP) &&
	    ifp->if_link_state != LINK_STATE_DOWN) {
		if (bp->bp_flags & BSTP_PORT_AUTOPTP) {
			/* A full-duplex link is assumed to be ptp */
			bp->bp_ptp_link = ifp->if_link_state ==
			    LINK_STATE_FULL_DUPLEX ? 1 : 0;
		}

		if (bp->bp_infois == BSTP_INFO_DISABLED)
			bstp_enable_port(bs, bp);
	} else {
		if (bp->bp_infois != BSTP_INFO_DISABLED)
			bstp_disable_port(bs, bp);
	}

	if_put(ifp);
}

void
bstp_enable_port(struct bstp_state *bs, struct bstp_port *bp)
{
	bp->bp_infois = BSTP_INFO_AGED;
	bstp_assign_roles(bs);
}

void
bstp_disable_port(struct bstp_state *bs, struct bstp_port *bp)
{
	bp->bp_infois = BSTP_INFO_DISABLED;
	bstp_set_port_state(bp, BSTP_IFSTATE_DISCARDING);
	bstp_assign_roles(bs);
}

void
bstp_tick(void *arg)
{
	struct bstp_state *bs = (struct bstp_state *)arg;
	struct ifnet *ifp;
	struct bstp_port *bp;
	int s;

	if ((ifp = if_get(bs->bs_ifindex)) == NULL)
		return;

	s = splnet();
	if ((ifp->if_flags & IFF_RUNNING) == 0) {
		splx(s);
		if_put(ifp);
		return;
	}

	/* slow timer to catch missed link events */
	if (bstp_timer_expired(&bs->bs_link_timer)) {
		LIST_FOREACH(bp, &bs->bs_bplist, bp_next)
			bstp_ifupdstatus(bs, bp);
		bstp_timer_start(&bs->bs_link_timer, BSTP_LINK_TIMER);
	}

	LIST_FOREACH(bp, &bs->bs_bplist, bp_next) {
		/* no events need to happen for these */
		bstp_timer_expired(&bp->bp_tc_timer);
		bstp_timer_expired(&bp->bp_recent_root_timer);
		bstp_timer_expired(&bp->bp_forward_delay_timer);
		bstp_timer_expired(&bp->bp_recent_backup_timer);

		if (bstp_timer_expired(&bp->bp_hello_timer))
			bstp_hello_timer_expiry(bs, bp);

		if (bstp_timer_expired(&bp->bp_message_age_timer))
			bstp_message_age_expiry(bs, bp);

		if (bstp_timer_expired(&bp->bp_migrate_delay_timer))
			bstp_migrate_delay_expiry(bs, bp);

		if (bstp_timer_expired(&bp->bp_edge_delay_timer))
			bstp_edge_delay_expiry(bs, bp);

		/* update the various state machines for the port */
		bstp_update_state(bs, bp);

		if (bp->bp_txcount > 0)
			bp->bp_txcount--;
	}

	if (ifp->if_flags & IFF_RUNNING)
		timeout_add_sec(&bs->bs_bstptimeout, 1);

	splx(s);
	if_put(ifp);
}

void
bstp_timer_start(struct bstp_timer *t, u_int16_t v)
{
	t->value = v;
	t->active = 1;
	t->latched = 0;
}

void
bstp_timer_stop(struct bstp_timer *t)
{
	t->value = 0;
	t->active = 0;
	t->latched = 0;
}

void
bstp_timer_latch(struct bstp_timer *t)
{
	t->latched = 1;
	t->active = 1;
}

int
bstp_timer_expired(struct bstp_timer *t)
{
	if (t->active == 0 || t->latched)
		return (0);
	t->value -= BSTP_TICK_VAL;
	if (t->value <= 0) {
		bstp_timer_stop(t);
		return (1);
	}
	return (0);
}

void
bstp_hello_timer_expiry(struct bstp_state *bs, struct bstp_port *bp)
{
	if ((bp->bp_flags & BSTP_PORT_NEWINFO) ||
	    bp->bp_role == BSTP_ROLE_DESIGNATED ||
	    (bp->bp_role == BSTP_ROLE_ROOT &&
	     bp->bp_tc_timer.active == 1)) {
		bstp_timer_start(&bp->bp_hello_timer, bp->bp_desg_htime);
		bp->bp_flags |= BSTP_PORT_NEWINFO;
		bstp_transmit(bs, bp);
	}
}

void
bstp_message_age_expiry(struct bstp_state *bs, struct bstp_port *bp)
{
	if (bp->bp_infois == BSTP_INFO_RECEIVED) {
		bp->bp_infois = BSTP_INFO_AGED;
		bstp_assign_roles(bs);
		DPRINTF(bp, "aged info\n");
	}
}

void
bstp_migrate_delay_expiry(struct bstp_state *bs, struct bstp_port *bp)
{
	bp->bp_flags |= BSTP_PORT_CANMIGRATE;
}

void
bstp_edge_delay_expiry(struct bstp_state *bs, struct bstp_port *bp)
{
	if ((bp->bp_flags & BSTP_PORT_AUTOEDGE) &&
	    bp->bp_protover == BSTP_PROTO_RSTP && bp->bp_proposing &&
	    bp->bp_role == BSTP_ROLE_DESIGNATED)
		bp->bp_operedge = 1;
}

int
bstp_addr_cmp(const u_int8_t *a, const u_int8_t *b)
{
	int i, d;

	for (i = 0, d = 0; i < ETHER_ADDR_LEN && d == 0; i++) {
		d = ((int)a[i]) - ((int)b[i]);
	}

	return (d);
}

/*
 * compare the bridge address component of the bridgeid
 */
int
bstp_same_bridgeid(u_int64_t id1, u_int64_t id2)
{
	u_char addr1[ETHER_ADDR_LEN];
	u_char addr2[ETHER_ADDR_LEN];

	PV2ADDR(id1, addr1);
	PV2ADDR(id2, addr2);

	if (bstp_addr_cmp(addr1, addr2) == 0)
		return (1);

	return (0);
}

void
bstp_initialization(struct bstp_state *bs)
{
	struct bstp_port *bp;
	struct ifnet *mif = NULL;
	u_char *e_addr;

	/*
	 * Search through the Ethernet interfaces and find the one
	 * with the lowest value.
	 * Make sure we take the address from an interface that is
	 * part of the bridge to make sure two bridges on the system
	 * will not use the same one. It is not possible for mif to be
	 * null, at this point we have at least one STP port and hence
	 * at least one NIC.
	 */
	LIST_FOREACH(bp, &bs->bs_bplist, bp_next) {
		struct ifnet *ifp;

		if (mif == NULL) {
			mif = if_get(bp->bp_ifindex);
			continue;
		}

		if ((ifp = if_get(bp->bp_ifindex)) == NULL)
			continue;

		if (bstp_addr_cmp(LLADDR(ifp->if_sadl),
		    LLADDR(mif->if_sadl)) < 0) {
			if_put(mif);
			mif = ifp;
			continue;
		}
		if_put(ifp);
	}

	if (mif == NULL) {
		bstp_stop(bs);
		bstp_reset(bs);
		return;
	}

	e_addr = LLADDR(mif->if_sadl);
	if_put(mif);
	bs->bs_bridge_pv.pv_dbridge_id =
	    (((u_int64_t)bs->bs_bridge_priority) << 48) |
	    (((u_int64_t)e_addr[0]) << 40) |
	    (((u_int64_t)e_addr[1]) << 32) |
	    (((u_int64_t)e_addr[2]) << 24) |
	    (((u_int64_t)e_addr[3]) << 16) |
	    (((u_int64_t)e_addr[4]) << 8) |
	    (((u_int64_t)e_addr[5]));

	bs->bs_bridge_pv.pv_root_id = bs->bs_bridge_pv.pv_dbridge_id;
	bs->bs_bridge_pv.pv_cost = 0;
	bs->bs_bridge_pv.pv_dport_id = 0;
	bs->bs_bridge_pv.pv_port_id = 0;

	if (!timeout_initialized(&bs->bs_bstptimeout))
		timeout_set(&bs->bs_bstptimeout, bstp_tick, bs);
	if (!timeout_pending(&bs->bs_bstptimeout))
		timeout_add_sec(&bs->bs_bstptimeout, 1);

	LIST_FOREACH(bp, &bs->bs_bplist, bp_next) {
		bp->bp_port_id = (bp->bp_priority << 8) |
		    (bp->bp_ifindex & 0xfff);
		bstp_ifupdstatus(bs, bp);
	}

	bstp_assign_roles(bs);
	bstp_timer_start(&bs->bs_link_timer, BSTP_LINK_TIMER);
}

struct bstp_state *
bstp_create(void)
{
	struct bstp_state *bs;

	bs = malloc(sizeof(*bs), M_DEVBUF, M_WAITOK|M_ZERO);
	LIST_INIT(&bs->bs_bplist);

	bs->bs_bridge_max_age = BSTP_DEFAULT_MAX_AGE;
	bs->bs_bridge_htime = BSTP_DEFAULT_HELLO_TIME;
	bs->bs_bridge_fdelay = BSTP_DEFAULT_FORWARD_DELAY;
	bs->bs_bridge_priority = BSTP_DEFAULT_BRIDGE_PRIORITY;
	bs->bs_hold_time = BSTP_DEFAULT_HOLD_TIME;
	bs->bs_migration_delay = BSTP_DEFAULT_MIGRATE_DELAY;
	bs->bs_txholdcount = BSTP_DEFAULT_HOLD_COUNT;
	bs->bs_protover = BSTP_PROTO_RSTP;	/* STP instead of RSTP? */

	getmicrotime(&bs->bs_last_tc_time);

	return (bs);
}

void
bstp_destroy(struct bstp_state *bs)
{
	if (bs == NULL)
		return;

	if (!LIST_EMPTY(&bs->bs_bplist))
		panic("bstp still active");

	free(bs, M_DEVBUF, sizeof *bs);
}

void
bstp_enable(struct bstp_state *bs, unsigned int ifindex)
{
	bs->bs_ifindex = ifindex;
	bstp_initialization(bs);
}

void
bstp_disable(struct bstp_state *bs)
{
	if (timeout_initialized(&bs->bs_bstptimeout))
		timeout_del(&bs->bs_bstptimeout);
	bs->bs_ifindex = 0;
}

void
bstp_stop(struct bstp_state *bs)
{
	struct bstp_port *bp;

	LIST_FOREACH(bp, &bs->bs_bplist, bp_next)
		bstp_set_port_state(bp, BSTP_IFSTATE_DISCARDING);

	if (timeout_initialized(&bs->bs_bstptimeout))
		timeout_del(&bs->bs_bstptimeout);
}

struct bstp_port *
bstp_add(struct bstp_state *bs, struct ifnet *ifp)
{
	struct bstp_port *bp;

	switch (ifp->if_type) {
	case IFT_ETHER:	/* These can do spanning tree. */
		break;
	default:
		/* Nothing else can. */
		return (NULL);
	}

	bp = malloc(sizeof(*bp), M_DEVBUF, M_NOWAIT|M_ZERO);
	if (bp == NULL)
		return (NULL);

	bp->bp_ifindex = ifp->if_index;
	bp->bp_bs = bs;
	bp->bp_priority = BSTP_DEFAULT_PORT_PRIORITY;
	bp->bp_txcount = 0;

	/* Init state */
	bp->bp_infois = BSTP_INFO_DISABLED;
	bp->bp_flags = BSTP_PORT_AUTOEDGE | BSTP_PORT_AUTOPTP;
	bstp_set_port_state(bp, BSTP_IFSTATE_DISCARDING);
	bstp_set_port_proto(bp, bs->bs_protover);
	bstp_set_port_role(bp, BSTP_ROLE_DISABLED);
	bstp_set_port_tc(bp, BSTP_TCSTATE_INACTIVE);
	bp->bp_path_cost = bstp_calc_path_cost(bp);

	LIST_INSERT_HEAD(&bs->bs_bplist, bp, bp_next);

	bp->bp_active = 1;
	bp->bp_flags |= BSTP_PORT_NEWINFO;
	bstp_initialization(bs);
	bstp_update_roles(bs, bp);

	/* Register callback for physical link state changes */
	task_set(&bp->bp_ltask, bstp_ifstate, ifp);
	if_linkstatehook_add(ifp, &bp->bp_ltask);

	return (bp);
}

void
bstp_delete(struct bstp_port *bp)
{
	struct bstp_state *bs = bp->bp_bs;
	struct ifnet *ifp;

	if (!bp->bp_active)
		panic("not a bstp member");

	if ((ifp = if_get(bp->bp_ifindex)) != NULL)
		if_linkstatehook_del(ifp, &bp->bp_ltask);
	if_put(ifp);

	LIST_REMOVE(bp, bp_next);
	free(bp, M_DEVBUF, sizeof *bp);
	bstp_initialization(bs);
}

u_int8_t
bstp_getstate(struct bstp_state *bs, struct bstp_port *bp)
{
	u_int8_t state = bp->bp_state;

	if (bs->bs_protover != BSTP_PROTO_STP)
		return (state);

	/*
	 * Translate RSTP roles and states to STP port states
	 * (IEEE Std 802.1D-2004 Table 17-1).
	 */
	if (bp->bp_role == BSTP_ROLE_DISABLED)
		state = BSTP_IFSTATE_DISABLED;
	else if (bp->bp_role == BSTP_ROLE_ALTERNATE ||
	    bp->bp_role == BSTP_ROLE_BACKUP)
		state = BSTP_IFSTATE_BLOCKING;
	else if (state == BSTP_IFSTATE_DISCARDING)
		state = BSTP_IFSTATE_LISTENING;

	return (state);
}

void
bstp_ifsflags(struct bstp_port *bp, u_int flags)
{
	struct bstp_state *bs;

	if ((flags & IFBIF_STP) == 0)
		return;
	bs = bp->bp_bs;

	/*
	 * Set edge status
	 */
	if (flags & IFBIF_BSTP_AUTOEDGE) {
		if ((bp->bp_flags & BSTP_PORT_AUTOEDGE) == 0) {
			bp->bp_flags |= BSTP_PORT_AUTOEDGE;

			/* we may be able to transition straight to edge */
			if (bp->bp_edge_delay_timer.active == 0)
				bstp_edge_delay_expiry(bs, bp);
		}
	} else
		bp->bp_flags &= ~BSTP_PORT_AUTOEDGE;

	if (flags & IFBIF_BSTP_EDGE)
		bp->bp_operedge = 1;
	else
		bp->bp_operedge = 0;

	/*
	 * Set point to point status
	 */
	if (flags & IFBIF_BSTP_AUTOPTP) {
		if ((bp->bp_flags & BSTP_PORT_AUTOPTP) == 0) {
			bp->bp_flags |= BSTP_PORT_AUTOPTP;

			bstp_ifupdstatus(bs, bp);
		}
	} else
		bp->bp_flags &= ~BSTP_PORT_AUTOPTP;

	if (flags & IFBIF_BSTP_PTP)
		bp->bp_ptp_link = 1;
	else
		bp->bp_ptp_link = 0;
}

int
bstp_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct bridge_softc *sc = (struct bridge_softc *)ifp->if_softc;
	struct bstp_state *bs = sc->sc_stp;
	struct ifbrparam *ifbp = (struct ifbrparam *)data;
	struct ifbreq *ifbr = (struct ifbreq *)data;
	struct bridge_iflist *bif;
	struct bstp_port *bp;
	int r = 0, err = 0, val;

	switch (cmd) {
	case SIOCBRDGSIFPRIO:
	case SIOCBRDGSIFCOST:
		err = bridge_findbif(sc, ifbr->ifbr_ifsname, &bif);
		if (err != 0)
			break;
		if ((bif->bif_flags & IFBIF_STP) == 0) {
			err = EINVAL;
			break;
		}
		bp = bif->bif_stp;
		break;
	default:
		break;
	}
	if (err)
		return (err);

	switch (cmd) {
	case SIOCBRDGGPRI:
		ifbp->ifbrp_prio = bs->bs_bridge_priority;
		break;
	case SIOCBRDGSPRI:
		val = ifbp->ifbrp_prio;
		if (val < 0 || val > BSTP_MAX_PRIORITY) {
			err = EINVAL;
			break;
		}

		/* Limit to steps of 4096 */
		val -= val % 4096;
		bs->bs_bridge_priority = val;
		r = 1;
		break;
	case SIOCBRDGGMA:
		ifbp->ifbrp_maxage = bs->bs_bridge_max_age >> 8;
		break;
	case SIOCBRDGSMA:
		val = ifbp->ifbrp_maxage;

		/* convert seconds to ticks */
		val *= BSTP_TICK_VAL;

		if (val < BSTP_MIN_MAX_AGE || val > BSTP_MAX_MAX_AGE) {
			err = EINVAL;
			break;
		}
		bs->bs_bridge_max_age = val;
		r = 1;
		break;
	case SIOCBRDGGHT:
		ifbp->ifbrp_hellotime = bs->bs_bridge_htime >> 8;
		break;
	case SIOCBRDGSHT:
		val = ifbp->ifbrp_hellotime;

		/* convert seconds to ticks */
		val *=  BSTP_TICK_VAL;

		/* value can only be changed in legacy stp mode */
		if (bs->bs_protover != BSTP_PROTO_STP) {
			err = EPERM;
			break;
		}
		if (val < BSTP_MIN_HELLO_TIME || val > BSTP_MAX_HELLO_TIME) {
			err = EINVAL;
			break;
		}
		bs->bs_bridge_htime = val;
		r = 1;
		break;
	case SIOCBRDGGFD:
		ifbp->ifbrp_fwddelay = bs->bs_bridge_fdelay >> 8;
		break;
	case SIOCBRDGSFD:
		val = ifbp->ifbrp_fwddelay;

		/* convert seconds to ticks */
		val *= BSTP_TICK_VAL;

		if (val < BSTP_MIN_FORWARD_DELAY ||
		    val > BSTP_MAX_FORWARD_DELAY) {
			err = EINVAL;
			break;
		}
		bs->bs_bridge_fdelay = val;
		r = 1;
		break;
	case SIOCBRDGSTXHC:
		val = ifbp->ifbrp_txhc;

		if (val < BSTP_MIN_HOLD_COUNT || val > BSTP_MAX_HOLD_COUNT) {
			err = EINVAL;
			break;
		}
		bs->bs_txholdcount = val;
		LIST_FOREACH(bp, &bs->bs_bplist, bp_next)
			bp->bp_txcount = 0;
		break;
	case SIOCBRDGSIFPRIO:
		val = ifbr->ifbr_priority;
		if (val < 0 || val > BSTP_MAX_PORT_PRIORITY)
			return (EINVAL);

		/* Limit to steps of 16 */
		val -= val % 16;
		bp->bp_priority = val;
		r = 1;
		break;
	case SIOCBRDGSIFCOST:
		val = ifbr->ifbr_path_cost;
		if (val > BSTP_MAX_PATH_COST) {
			err = EINVAL;
			break;
		}
		if (val == 0) {	/* use auto */
			bp->bp_flags &= ~BSTP_PORT_ADMCOST;
			bp->bp_path_cost = bstp_calc_path_cost(bp);
		} else {
			bp->bp_path_cost = val;
			bp->bp_flags |= BSTP_PORT_ADMCOST;
		}
		r = 1;
		break;
	case SIOCBRDGSPROTO:
		val = ifbp->ifbrp_proto;

		/* Supported protocol versions */
		switch (val) {
		case BSTP_PROTO_STP:
		case BSTP_PROTO_RSTP:
			bs->bs_protover = val;
			bs->bs_bridge_htime = BSTP_DEFAULT_HELLO_TIME;
			LIST_FOREACH(bp, &bs->bs_bplist, bp_next) {
				/* reinit state */
				bp->bp_infois = BSTP_INFO_DISABLED;
				bp->bp_txcount = 0;
				bstp_set_port_proto(bp, bs->bs_protover);
				bstp_set_port_role(bp, BSTP_ROLE_DISABLED);
				bstp_set_port_tc(bp, BSTP_TCSTATE_INACTIVE);
				bstp_timer_stop(&bp->bp_recent_backup_timer);
			}
			r = 1;
			break;
		default:
			err = EINVAL;
		}
		break;
	default:
		break;
	}

	if (r)
		bstp_initialization(bs);

	return (err);
}
