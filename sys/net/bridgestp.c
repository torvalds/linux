/*	$NetBSD: bridgestp.c,v 1.5 2003/11/28 08:56:48 keihan Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
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
 *
 * OpenBSD: bridgestp.c,v 1.5 2001/03/22 03:48:29 jason Exp
 */

/*
 * Implementation of the spanning tree protocol as defined in
 * ISO/IEC 802.1D-2004, June 9, 2004.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/callout.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/taskqueue.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/if_llc.h>
#include <net/if_media.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/if_ether.h>
#include <net/bridgestp.h>

#ifdef	BRIDGESTP_DEBUG
#define	DPRINTF(fmt, arg...)	printf("bstp: " fmt, ##arg)
#else
#define	DPRINTF(fmt, arg...)	(void)0
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

const uint8_t bstp_etheraddr[] = { 0x01, 0x80, 0xc2, 0x00, 0x00, 0x00 };

LIST_HEAD(, bstp_state) bstp_list;
static struct mtx	bstp_list_mtx;

static void	bstp_transmit(struct bstp_state *, struct bstp_port *);
static void	bstp_transmit_bpdu(struct bstp_state *, struct bstp_port *);
static void	bstp_transmit_tcn(struct bstp_state *, struct bstp_port *);
static void	bstp_decode_bpdu(struct bstp_port *, struct bstp_cbpdu *,
		    struct bstp_config_unit *);
static void	bstp_send_bpdu(struct bstp_state *, struct bstp_port *,
		    struct bstp_cbpdu *);
static int	bstp_pdu_flags(struct bstp_port *);
static void	bstp_received_stp(struct bstp_state *, struct bstp_port *,
		    struct mbuf **, struct bstp_tbpdu *);
static void	bstp_received_rstp(struct bstp_state *, struct bstp_port *,
		    struct mbuf **, struct bstp_tbpdu *);
static void	bstp_received_tcn(struct bstp_state *, struct bstp_port *,
		    struct bstp_tcn_unit *);
static void	bstp_received_bpdu(struct bstp_state *, struct bstp_port *,
		    struct bstp_config_unit *);
static int	bstp_pdu_rcvtype(struct bstp_port *, struct bstp_config_unit *);
static int	bstp_pdu_bettersame(struct bstp_port *, int);
static int	bstp_info_cmp(struct bstp_pri_vector *,
		    struct bstp_pri_vector *);
static int	bstp_info_superior(struct bstp_pri_vector *,
		    struct bstp_pri_vector *);
static void	bstp_assign_roles(struct bstp_state *);
static void	bstp_update_roles(struct bstp_state *, struct bstp_port *);
static void	bstp_update_state(struct bstp_state *, struct bstp_port *);
static void	bstp_update_tc(struct bstp_port *);
static void	bstp_update_info(struct bstp_port *);
static void	bstp_set_other_tcprop(struct bstp_port *);
static void	bstp_set_all_reroot(struct bstp_state *);
static void	bstp_set_all_sync(struct bstp_state *);
static void	bstp_set_port_state(struct bstp_port *, int);
static void	bstp_set_port_role(struct bstp_port *, int);
static void	bstp_set_port_proto(struct bstp_port *, int);
static void	bstp_set_port_tc(struct bstp_port *, int);
static void	bstp_set_timer_tc(struct bstp_port *);
static void	bstp_set_timer_msgage(struct bstp_port *);
static int	bstp_rerooted(struct bstp_state *, struct bstp_port *);
static uint32_t	bstp_calc_path_cost(struct bstp_port *);
static void	bstp_notify_state(void *, int);
static void	bstp_notify_rtage(void *, int);
static void	bstp_ifupdstatus(void *, int);
static void	bstp_enable_port(struct bstp_state *, struct bstp_port *);
static void	bstp_disable_port(struct bstp_state *, struct bstp_port *);
static void	bstp_tick(void *);
static void	bstp_timer_start(struct bstp_timer *, uint16_t);
static void	bstp_timer_stop(struct bstp_timer *);
static void	bstp_timer_latch(struct bstp_timer *);
static int	bstp_timer_dectest(struct bstp_timer *);
static void	bstp_hello_timer_expiry(struct bstp_state *,
		    struct bstp_port *);
static void	bstp_message_age_expiry(struct bstp_state *,
		    struct bstp_port *);
static void	bstp_migrate_delay_expiry(struct bstp_state *,
		    struct bstp_port *);
static void	bstp_edge_delay_expiry(struct bstp_state *,
		    struct bstp_port *);
static int	bstp_addr_cmp(const uint8_t *, const uint8_t *);
static int	bstp_same_bridgeid(uint64_t, uint64_t);
static void	bstp_reinit(struct bstp_state *);

static void
bstp_transmit(struct bstp_state *bs, struct bstp_port *bp)
{
	if (bs->bs_running == 0)
		return;

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

static void
bstp_transmit_bpdu(struct bstp_state *bs, struct bstp_port *bp)
{
	struct bstp_cbpdu bpdu;

	BSTP_LOCK_ASSERT(bs);

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

static void
bstp_transmit_tcn(struct bstp_state *bs, struct bstp_port *bp)
{
	struct bstp_tbpdu bpdu;
	struct ifnet *ifp = bp->bp_ifp;
	struct ether_header *eh;
	struct mbuf *m;

	KASSERT(bp == bs->bs_root_port, ("%s: bad root port\n", __func__));

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
		return;

	m = m_gethdr(M_NOWAIT, MT_DATA);
	if (m == NULL)
		return;

	m->m_pkthdr.rcvif = ifp;
	m->m_pkthdr.len = sizeof(*eh) + sizeof(bpdu);
	m->m_len = m->m_pkthdr.len;

	eh = mtod(m, struct ether_header *);

	memcpy(eh->ether_shost, IF_LLADDR(ifp), ETHER_ADDR_LEN);
	memcpy(eh->ether_dhost, bstp_etheraddr, ETHER_ADDR_LEN);
	eh->ether_type = htons(sizeof(bpdu));

	bpdu.tbu_ssap = bpdu.tbu_dsap = LLC_8021D_LSAP;
	bpdu.tbu_ctl = LLC_UI;
	bpdu.tbu_protoid = 0;
	bpdu.tbu_protover = 0;
	bpdu.tbu_bpdutype = BSTP_MSGTYPE_TCN;

	memcpy(mtod(m, caddr_t) + sizeof(*eh), &bpdu, sizeof(bpdu));

	bp->bp_txcount++;
	ifp->if_transmit(ifp, m);
}

static void
bstp_decode_bpdu(struct bstp_port *bp, struct bstp_cbpdu *cpdu,
    struct bstp_config_unit *cu)
{
	int flags;

	cu->cu_pv.pv_root_id =
	    (((uint64_t)ntohs(cpdu->cbu_rootpri)) << 48) |
	    (((uint64_t)cpdu->cbu_rootaddr[0]) << 40) |
	    (((uint64_t)cpdu->cbu_rootaddr[1]) << 32) |
	    (((uint64_t)cpdu->cbu_rootaddr[2]) << 24) |
	    (((uint64_t)cpdu->cbu_rootaddr[3]) << 16) |
	    (((uint64_t)cpdu->cbu_rootaddr[4]) << 8) |
	    (((uint64_t)cpdu->cbu_rootaddr[5]) << 0);

	cu->cu_pv.pv_dbridge_id =
	    (((uint64_t)ntohs(cpdu->cbu_bridgepri)) << 48) |
	    (((uint64_t)cpdu->cbu_bridgeaddr[0]) << 40) |
	    (((uint64_t)cpdu->cbu_bridgeaddr[1]) << 32) |
	    (((uint64_t)cpdu->cbu_bridgeaddr[2]) << 24) |
	    (((uint64_t)cpdu->cbu_bridgeaddr[3]) << 16) |
	    (((uint64_t)cpdu->cbu_bridgeaddr[4]) << 8) |
	    (((uint64_t)cpdu->cbu_bridgeaddr[5]) << 0);

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

static void
bstp_send_bpdu(struct bstp_state *bs, struct bstp_port *bp,
    struct bstp_cbpdu *bpdu)
{
	struct ifnet *ifp;
	struct mbuf *m;
	struct ether_header *eh;

	BSTP_LOCK_ASSERT(bs);

	ifp = bp->bp_ifp;

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
		return;

	m = m_gethdr(M_NOWAIT, MT_DATA);
	if (m == NULL)
		return;

	eh = mtod(m, struct ether_header *);

	bpdu->cbu_ssap = bpdu->cbu_dsap = LLC_8021D_LSAP;
	bpdu->cbu_ctl = LLC_UI;
	bpdu->cbu_protoid = htons(BSTP_PROTO_ID);

	memcpy(eh->ether_shost, IF_LLADDR(ifp), ETHER_ADDR_LEN);
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
	m->m_pkthdr.rcvif = ifp;
	m->m_len = m->m_pkthdr.len;

	bp->bp_txcount++;
	ifp->if_transmit(ifp, m);
}

static int
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
			flags |=
				(BSTP_PDU_F_ROOT << BSTP_PDU_PRSHIFT);
			break;

		case BSTP_ROLE_ALTERNATE:
		case BSTP_ROLE_BACKUP:	/* fall through */
			flags |=
				(BSTP_PDU_F_ALT << BSTP_PDU_PRSHIFT);
			break;

		case BSTP_ROLE_DESIGNATED:
			flags |=
				(BSTP_PDU_F_DESG << BSTP_PDU_PRSHIFT);
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

void
bstp_input(struct bstp_port *bp, struct ifnet *ifp, struct mbuf *m)
{
	struct bstp_state *bs = bp->bp_bs;
	struct ether_header *eh;
	struct bstp_tbpdu tpdu;
	uint16_t len;

	if (bp->bp_active == 0) {
		m_freem(m);
		return;
	}

	BSTP_LOCK(bs);

	eh = mtod(m, struct ether_header *);

	len = ntohs(eh->ether_type);
	if (len < sizeof(tpdu))
		goto out;

	m_adj(m, ETHER_HDR_LEN);

	if (m->m_pkthdr.len > len)
		m_adj(m, len - m->m_pkthdr.len);
	if (m->m_len < sizeof(tpdu) &&
	    (m = m_pullup(m, sizeof(tpdu))) == NULL)
		goto out;

	memcpy(&tpdu, mtod(m, caddr_t), sizeof(tpdu));

	/* basic packet checks */
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
	BSTP_UNLOCK(bs);
	if (m)
		m_freem(m);
}

static void
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

static void
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

static void
bstp_received_tcn(struct bstp_state *bs, struct bstp_port *bp,
    struct bstp_tcn_unit *tcn)
{
	bp->bp_rcvdtcn = 1;
	bstp_update_tc(bp);
}

static void
bstp_received_bpdu(struct bstp_state *bs, struct bstp_port *bp,
    struct bstp_config_unit *cu)
{
	int type;

	BSTP_LOCK_ASSERT(bs);

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

static int
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

static int
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

static int
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
static int
bstp_info_superior(struct bstp_pri_vector *pv,
    struct bstp_pri_vector *cpv)
{
	if (bstp_info_cmp(pv, cpv) == INFO_BETTER ||
	    (bstp_same_bridgeid(pv->pv_dbridge_id, cpv->pv_dbridge_id) &&
	    (cpv->pv_dport_id & 0xfff) == (pv->pv_dport_id & 0xfff)))
		return (1);
	return (0);
}

static void
bstp_assign_roles(struct bstp_state *bs)
{
	struct bstp_port *bp, *rbp = NULL;
	struct bstp_pri_vector pv;

	/* default to our priority vector */
	bs->bs_root_pv = bs->bs_bridge_pv;
	bs->bs_root_msg_age = 0;
	bs->bs_root_max_age = bs->bs_bridge_max_age;
	bs->bs_root_fdelay = bs->bs_bridge_fdelay;
	bs->bs_root_htime = bs->bs_bridge_htime;
	bs->bs_root_port = NULL;

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

static void
bstp_update_state(struct bstp_state *bs, struct bstp_port *bp)
{
	struct bstp_port *bp2;
	int synced;

	BSTP_LOCK_ASSERT(bs);

	/* check if all the ports have syncronised again */
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

static void
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
			DPRINTF("%s -> ALTERNATE_AGREED\n",
			    bp->bp_ifp->if_xname);
		}

		if (bp->bp_proposed && !bp->bp_agree) {
			bstp_set_all_sync(bs);
			bp->bp_proposed = 0;
			DPRINTF("%s -> ALTERNATE_PROPOSED\n",
			    bp->bp_ifp->if_xname);
		}

		/* Clear any flags if set */
		if (bp->bp_sync || !bp->bp_synced || bp->bp_reroot) {
			bp->bp_sync = 0;
			bp->bp_synced = 1;
			bp->bp_reroot = 0;
			DPRINTF("%s -> ALTERNATE_PORT\n", bp->bp_ifp->if_xname);
		}
		break;

	case BSTP_ROLE_ROOT:
		if (bp->bp_state != BSTP_IFSTATE_FORWARDING && !bp->bp_reroot) {
			bstp_set_all_reroot(bs);
			DPRINTF("%s -> ROOT_REROOT\n", bp->bp_ifp->if_xname);
		}

		if ((bs->bs_allsynced && !bp->bp_agree) ||
		    (bp->bp_proposed && bp->bp_agree)) {
			bp->bp_proposed = 0;
			bp->bp_sync = 0;
			bp->bp_agree = 1;
			bp->bp_flags |= BSTP_PORT_NEWINFO;
			DPRINTF("%s -> ROOT_AGREED\n", bp->bp_ifp->if_xname);
		}

		if (bp->bp_proposed && !bp->bp_agree) {
			bstp_set_all_sync(bs);
			bp->bp_proposed = 0;
			DPRINTF("%s -> ROOT_PROPOSED\n", bp->bp_ifp->if_xname);
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
			DPRINTF("%s -> ROOT_REROOTED\n", bp->bp_ifp->if_xname);
		}
		break;

	case BSTP_ROLE_DESIGNATED:
		if (bp->bp_recent_root_timer.active == 0 && bp->bp_reroot) {
			bp->bp_reroot = 0;
			DPRINTF("%s -> DESIGNATED_RETIRED\n",
			    bp->bp_ifp->if_xname);
		}

		if ((bp->bp_state == BSTP_IFSTATE_DISCARDING &&
		    !bp->bp_synced) || (bp->bp_agreed && !bp->bp_synced) ||
		    (bp->bp_operedge && !bp->bp_synced) ||
		    (bp->bp_sync && bp->bp_synced)) {
			bstp_timer_stop(&bp->bp_recent_root_timer);
			bp->bp_synced = 1;
			bp->bp_sync = 0;
			DPRINTF("%s -> DESIGNATED_SYNCED\n",
			    bp->bp_ifp->if_xname);
		}

		if (bp->bp_state != BSTP_IFSTATE_FORWARDING &&
		    !bp->bp_agreed && !bp->bp_proposing &&
		    !bp->bp_operedge) {
			bp->bp_proposing = 1;
			bp->bp_flags |= BSTP_PORT_NEWINFO;
			bstp_timer_start(&bp->bp_edge_delay_timer,
			    (bp->bp_ptp_link ? BSTP_DEFAULT_MIGRATE_DELAY :
			     bp->bp_desg_max_age));
			DPRINTF("%s -> DESIGNATED_PROPOSE\n",
			    bp->bp_ifp->if_xname);
		}

		if (bp->bp_state != BSTP_IFSTATE_FORWARDING &&
		    (bp->bp_forward_delay_timer.active == 0 || bp->bp_agreed ||
		    bp->bp_operedge) &&
		    (bp->bp_recent_root_timer.active == 0 || !bp->bp_reroot) &&
		    !bp->bp_sync) {
			if (bp->bp_agreed)
				DPRINTF("%s -> AGREED\n", bp->bp_ifp->if_xname);
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
			DPRINTF("%s -> DESIGNATED_DISCARD\n",
			    bp->bp_ifp->if_xname);
		}
		break;
	}

	if (bp->bp_flags & BSTP_PORT_NEWINFO)
		bstp_transmit(bs, bp);
}

static void
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
			DPRINTF("Invalid TC state for %s\n",
			    bp->bp_ifp->if_xname);
			break;
	}

}

static void
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
static void
bstp_set_other_tcprop(struct bstp_port *bp)
{
	struct bstp_state *bs = bp->bp_bs;
	struct bstp_port *bp2;

	BSTP_LOCK_ASSERT(bs);

	LIST_FOREACH(bp2, &bs->bs_bplist, bp_next) {
		if (bp2 == bp)
			continue;
		bp2->bp_tc_prop = 1;
	}
}

static void
bstp_set_all_reroot(struct bstp_state *bs)
{
	struct bstp_port *bp;

	BSTP_LOCK_ASSERT(bs);

	LIST_FOREACH(bp, &bs->bs_bplist, bp_next)
		bp->bp_reroot = 1;
}

static void
bstp_set_all_sync(struct bstp_state *bs)
{
	struct bstp_port *bp;

	BSTP_LOCK_ASSERT(bs);

	LIST_FOREACH(bp, &bs->bs_bplist, bp_next) {
		bp->bp_sync = 1;
		bp->bp_synced = 0;	/* Not explicit in spec */
	}

	bs->bs_allsynced = 0;
}

static void
bstp_set_port_state(struct bstp_port *bp, int state)
{
	if (bp->bp_state == state)
		return;

	bp->bp_state = state;

	switch (bp->bp_state) {
		case BSTP_IFSTATE_DISCARDING:
			DPRINTF("state changed to DISCARDING on %s\n",
			    bp->bp_ifp->if_xname);
			break;

		case BSTP_IFSTATE_LEARNING:
			DPRINTF("state changed to LEARNING on %s\n",
			    bp->bp_ifp->if_xname);

			bstp_timer_start(&bp->bp_forward_delay_timer,
			    bp->bp_protover == BSTP_PROTO_RSTP ?
			    bp->bp_desg_htime : bp->bp_desg_fdelay);
			break;

		case BSTP_IFSTATE_FORWARDING:
			DPRINTF("state changed to FORWARDING on %s\n",
			    bp->bp_ifp->if_xname);

			bstp_timer_stop(&bp->bp_forward_delay_timer);
			/* Record that we enabled forwarding */
			bp->bp_forward_transitions++;
			break;
	}

	/* notify the parent bridge */
	taskqueue_enqueue(taskqueue_swi, &bp->bp_statetask);
}

static void
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
			/* fall through */
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
			DPRINTF("%s role -> ALT/BACK/DISABLED\n",
			    bp->bp_ifp->if_xname);
			bstp_set_port_state(bp, BSTP_IFSTATE_DISCARDING);
			bstp_timer_stop(&bp->bp_recent_root_timer);
			bstp_timer_latch(&bp->bp_forward_delay_timer);
			bp->bp_sync = 0;
			bp->bp_synced = 1;
			bp->bp_reroot = 0;
			break;

		case BSTP_ROLE_ROOT:
			DPRINTF("%s role -> ROOT\n",
			    bp->bp_ifp->if_xname);
			bstp_set_port_state(bp, BSTP_IFSTATE_DISCARDING);
			bstp_timer_latch(&bp->bp_recent_root_timer);
			bp->bp_proposing = 0;
			break;

		case BSTP_ROLE_DESIGNATED:
			DPRINTF("%s role -> DESIGNATED\n",
			    bp->bp_ifp->if_xname);
			bstp_timer_start(&bp->bp_hello_timer,
			    bp->bp_desg_htime);
			bp->bp_agree = 0;
			break;
	}

	/* let the TC state know that the role changed */
	bstp_update_tc(bp);
}

static void
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
			/* STP compat mode only uses 16 bits of the 32 */
			if (bp->bp_path_cost > 65535)
				bp->bp_path_cost = 65535;
			break;

		case BSTP_PROTO_RSTP:
			bstp_timer_start(&bp->bp_migrate_delay_timer,
			    bs->bs_migration_delay);
			break;

		default:
			DPRINTF("Unsupported STP version %d\n", proto);
			return;
	}

	bp->bp_protover = proto;
	bp->bp_flags &= ~BSTP_PORT_CANMIGRATE;
}

static void
bstp_set_port_tc(struct bstp_port *bp, int state)
{
	struct bstp_state *bs = bp->bp_bs;

	bp->bp_tcstate = state;

	/* initialise the new state */
	switch (bp->bp_tcstate) {
		case BSTP_TCSTATE_ACTIVE:
			DPRINTF("%s -> TC_ACTIVE\n", bp->bp_ifp->if_xname);
			/* nothing to do */
			break;

		case BSTP_TCSTATE_INACTIVE:
			bstp_timer_stop(&bp->bp_tc_timer);
			/* flush routes on the parent bridge */
			bp->bp_fdbflush = 1;
			taskqueue_enqueue(taskqueue_swi, &bp->bp_rtagetask);
			bp->bp_tc_ack = 0;
			DPRINTF("%s -> TC_INACTIVE\n", bp->bp_ifp->if_xname);
			break;

		case BSTP_TCSTATE_LEARNING:
			bp->bp_rcvdtc = 0;
			bp->bp_rcvdtcn = 0;
			bp->bp_rcvdtca = 0;
			bp->bp_tc_prop = 0;
			DPRINTF("%s -> TC_LEARNING\n", bp->bp_ifp->if_xname);
			break;

		case BSTP_TCSTATE_DETECTED:
			bstp_set_timer_tc(bp);
			bstp_set_other_tcprop(bp);
			/* send out notification */
			bp->bp_flags |= BSTP_PORT_NEWINFO;
			bstp_transmit(bs, bp);
			getmicrotime(&bs->bs_last_tc_time);
			DPRINTF("%s -> TC_DETECTED\n", bp->bp_ifp->if_xname);
			bp->bp_tcstate = BSTP_TCSTATE_ACTIVE; /* UCT */
			break;

		case BSTP_TCSTATE_TCN:
			bstp_set_timer_tc(bp);
			DPRINTF("%s -> TC_TCN\n", bp->bp_ifp->if_xname);
			/* fall through */
		case BSTP_TCSTATE_TC:
			bp->bp_rcvdtc = 0;
			bp->bp_rcvdtcn = 0;
			if (bp->bp_role == BSTP_ROLE_DESIGNATED)
				bp->bp_tc_ack = 1;

			bstp_set_other_tcprop(bp);
			DPRINTF("%s -> TC_TC\n", bp->bp_ifp->if_xname);
			bp->bp_tcstate = BSTP_TCSTATE_ACTIVE; /* UCT */
			break;

		case BSTP_TCSTATE_PROPAG:
			/* flush routes on the parent bridge */
			bp->bp_fdbflush = 1;
			taskqueue_enqueue(taskqueue_swi, &bp->bp_rtagetask);
			bp->bp_tc_prop = 0;
			bstp_set_timer_tc(bp);
			DPRINTF("%s -> TC_PROPAG\n", bp->bp_ifp->if_xname);
			bp->bp_tcstate = BSTP_TCSTATE_ACTIVE; /* UCT */
			break;

		case BSTP_TCSTATE_ACK:
			bstp_timer_stop(&bp->bp_tc_timer);
			bp->bp_rcvdtca = 0;
			DPRINTF("%s -> TC_ACK\n", bp->bp_ifp->if_xname);
			bp->bp_tcstate = BSTP_TCSTATE_ACTIVE; /* UCT */
			break;
	}
}

static void
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

static void
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

static int
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

int
bstp_set_htime(struct bstp_state *bs, int t)
{
	/* convert seconds to ticks */
	t *=  BSTP_TICK_VAL;

	/* value can only be changed in leagacy stp mode */
	if (bs->bs_protover != BSTP_PROTO_STP)
		return (EPERM);

	if (t < BSTP_MIN_HELLO_TIME || t > BSTP_MAX_HELLO_TIME)
		return (EINVAL);

	BSTP_LOCK(bs);
	bs->bs_bridge_htime = t;
	bstp_reinit(bs);
	BSTP_UNLOCK(bs);
	return (0);
}

int
bstp_set_fdelay(struct bstp_state *bs, int t)
{
	/* convert seconds to ticks */
	t *= BSTP_TICK_VAL;

	if (t < BSTP_MIN_FORWARD_DELAY || t > BSTP_MAX_FORWARD_DELAY)
		return (EINVAL);

	BSTP_LOCK(bs);
	bs->bs_bridge_fdelay = t;
	bstp_reinit(bs);
	BSTP_UNLOCK(bs);
	return (0);
}

int
bstp_set_maxage(struct bstp_state *bs, int t)
{
	/* convert seconds to ticks */
	t *= BSTP_TICK_VAL;

	if (t < BSTP_MIN_MAX_AGE || t > BSTP_MAX_MAX_AGE)
		return (EINVAL);

	BSTP_LOCK(bs);
	bs->bs_bridge_max_age = t;
	bstp_reinit(bs);
	BSTP_UNLOCK(bs);
	return (0);
}

int
bstp_set_holdcount(struct bstp_state *bs, int count)
{
	struct bstp_port *bp;

	if (count < BSTP_MIN_HOLD_COUNT ||
	    count > BSTP_MAX_HOLD_COUNT)
		return (EINVAL);

	BSTP_LOCK(bs);
	bs->bs_txholdcount = count;
	LIST_FOREACH(bp, &bs->bs_bplist, bp_next)
		bp->bp_txcount = 0;
	BSTP_UNLOCK(bs);
	return (0);
}

int
bstp_set_protocol(struct bstp_state *bs, int proto)
{
	struct bstp_port *bp;

	switch (proto) {
		/* Supported protocol versions */
		case BSTP_PROTO_STP:
		case BSTP_PROTO_RSTP:
			break;

		default:
			return (EINVAL);
	}

	BSTP_LOCK(bs);
	bs->bs_protover = proto;
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
	bstp_reinit(bs);
	BSTP_UNLOCK(bs);
	return (0);
}

int
bstp_set_priority(struct bstp_state *bs, int pri)
{
	if (pri < 0 || pri > BSTP_MAX_PRIORITY)
		return (EINVAL);

	/* Limit to steps of 4096 */
	pri -= pri % 4096;

	BSTP_LOCK(bs);
	bs->bs_bridge_priority = pri;
	bstp_reinit(bs);
	BSTP_UNLOCK(bs);
	return (0);
}

int
bstp_set_port_priority(struct bstp_port *bp, int pri)
{
	struct bstp_state *bs = bp->bp_bs;

	if (pri < 0 || pri > BSTP_MAX_PORT_PRIORITY)
		return (EINVAL);

	/* Limit to steps of 16 */
	pri -= pri % 16;

	BSTP_LOCK(bs);
	bp->bp_priority = pri;
	bstp_reinit(bs);
	BSTP_UNLOCK(bs);
	return (0);
}

int
bstp_set_path_cost(struct bstp_port *bp, uint32_t path_cost)
{
	struct bstp_state *bs = bp->bp_bs;

	if (path_cost > BSTP_MAX_PATH_COST)
		return (EINVAL);

	/* STP compat mode only uses 16 bits of the 32 */
	if (bp->bp_protover == BSTP_PROTO_STP && path_cost > 65535)
		path_cost = 65535;

	BSTP_LOCK(bs);

	if (path_cost == 0) {	/* use auto */
		bp->bp_flags &= ~BSTP_PORT_ADMCOST;
		bp->bp_path_cost = bstp_calc_path_cost(bp);
	} else {
		bp->bp_path_cost = path_cost;
		bp->bp_flags |= BSTP_PORT_ADMCOST;
	}
	bstp_reinit(bs);
	BSTP_UNLOCK(bs);
	return (0);
}

int
bstp_set_edge(struct bstp_port *bp, int set)
{
	struct bstp_state *bs = bp->bp_bs;

	BSTP_LOCK(bs);
	if ((bp->bp_operedge = set) == 0)
		bp->bp_flags &= ~BSTP_PORT_ADMEDGE;
	else
		bp->bp_flags |= BSTP_PORT_ADMEDGE;
	BSTP_UNLOCK(bs);
	return (0);
}

int
bstp_set_autoedge(struct bstp_port *bp, int set)
{
	struct bstp_state *bs = bp->bp_bs;

	BSTP_LOCK(bs);
	if (set) {
		bp->bp_flags |= BSTP_PORT_AUTOEDGE;
		/* we may be able to transition straight to edge */
		if (bp->bp_edge_delay_timer.active == 0)
			bstp_edge_delay_expiry(bs, bp);
	} else
		bp->bp_flags &= ~BSTP_PORT_AUTOEDGE;
	BSTP_UNLOCK(bs);
	return (0);
}

int
bstp_set_ptp(struct bstp_port *bp, int set)
{
	struct bstp_state *bs = bp->bp_bs;

	BSTP_LOCK(bs);
	bp->bp_ptp_link = set;
	BSTP_UNLOCK(bs);
	return (0);
}

int
bstp_set_autoptp(struct bstp_port *bp, int set)
{
	struct bstp_state *bs = bp->bp_bs;

	BSTP_LOCK(bs);
	if (set) {
		bp->bp_flags |= BSTP_PORT_AUTOPTP;
		if (bp->bp_role != BSTP_ROLE_DISABLED)
			taskqueue_enqueue(taskqueue_swi, &bp->bp_mediatask);
	} else
		bp->bp_flags &= ~BSTP_PORT_AUTOPTP;
	BSTP_UNLOCK(bs);
	return (0);
}

/*
 * Calculate the path cost according to the link speed.
 */
static uint32_t
bstp_calc_path_cost(struct bstp_port *bp)
{
	struct ifnet *ifp = bp->bp_ifp;
	uint32_t path_cost;

	/* If the priority has been manually set then retain the value */
	if (bp->bp_flags & BSTP_PORT_ADMCOST)
		return bp->bp_path_cost;

	if (ifp->if_link_state == LINK_STATE_DOWN) {
		/* Recalc when the link comes up again */
		bp->bp_flags |= BSTP_PORT_PNDCOST;
		return (BSTP_DEFAULT_PATH_COST);
	}

	if (ifp->if_baudrate < 1000)
		return (BSTP_DEFAULT_PATH_COST);

 	/* formula from section 17.14, IEEE Std 802.1D-2004 */
	path_cost = 20000000000ULL / (ifp->if_baudrate / 1000);

	if (path_cost > BSTP_MAX_PATH_COST)
		path_cost = BSTP_MAX_PATH_COST;

	/* STP compat mode only uses 16 bits of the 32 */
	if (bp->bp_protover == BSTP_PROTO_STP && path_cost > 65535)
		path_cost = 65535;

	return (path_cost);
}

/*
 * Notify the bridge that a port state has changed, we need to do this from a
 * taskqueue to avoid a LOR.
 */
static void
bstp_notify_state(void *arg, int pending)
{
	struct bstp_port *bp = (struct bstp_port *)arg;
	struct bstp_state *bs = bp->bp_bs;

	if (bp->bp_active == 1 && bs->bs_state_cb != NULL)
		(*bs->bs_state_cb)(bp->bp_ifp, bp->bp_state);
}

/*
 * Flush the routes on the bridge port, we need to do this from a
 * taskqueue to avoid a LOR.
 */
static void
bstp_notify_rtage(void *arg, int pending)
{
	struct bstp_port *bp = (struct bstp_port *)arg;
	struct bstp_state *bs = bp->bp_bs;
	int age = 0;

	BSTP_LOCK(bs);
	switch (bp->bp_protover) {
		case BSTP_PROTO_STP:
			/* convert to seconds */
			age = bp->bp_desg_fdelay / BSTP_TICK_VAL;
			break;

		case BSTP_PROTO_RSTP:
			age = 0;
			break;
	}
	BSTP_UNLOCK(bs);

	if (bp->bp_active == 1 && bs->bs_rtage_cb != NULL)
		(*bs->bs_rtage_cb)(bp->bp_ifp, age);

	/* flush is complete */
	BSTP_LOCK(bs);
	bp->bp_fdbflush = 0;
	BSTP_UNLOCK(bs);
}

void
bstp_linkstate(struct bstp_port *bp)
{
	struct bstp_state *bs = bp->bp_bs;

	if (!bp->bp_active)
		return;

	bstp_ifupdstatus(bp, 0);
	BSTP_LOCK(bs);
	bstp_update_state(bs, bp);
	BSTP_UNLOCK(bs);
}

static void
bstp_ifupdstatus(void *arg, int pending)
{
	struct bstp_port *bp = (struct bstp_port *)arg;
	struct bstp_state *bs = bp->bp_bs;
	struct ifnet *ifp = bp->bp_ifp;
	struct ifmediareq ifmr;
	int error, changed;

	if (!bp->bp_active)
		return;

	bzero((char *)&ifmr, sizeof(ifmr));
	error = (*ifp->if_ioctl)(ifp, SIOCGIFMEDIA, (caddr_t)&ifmr);

	BSTP_LOCK(bs);
	changed = 0;
	if ((error == 0) && (ifp->if_flags & IFF_UP)) {
		if (ifmr.ifm_status & IFM_ACTIVE) {
			/* A full-duplex link is assumed to be point to point */
			if (bp->bp_flags & BSTP_PORT_AUTOPTP) {
				int fdx;

				fdx = ifmr.ifm_active & IFM_FDX ? 1 : 0;
				if (bp->bp_ptp_link ^ fdx) {
					bp->bp_ptp_link = fdx;
					changed = 1;
				}
			}

			/* Calc the cost if the link was down previously */
			if (bp->bp_flags & BSTP_PORT_PNDCOST) {
				uint32_t cost;

				cost = bstp_calc_path_cost(bp);
				if (bp->bp_path_cost != cost) {
					bp->bp_path_cost = cost;
					changed = 1;
				}
				bp->bp_flags &= ~BSTP_PORT_PNDCOST;
			}

			if (bp->bp_role == BSTP_ROLE_DISABLED) {
				bstp_enable_port(bs, bp);
				changed = 1;
			}
		} else {
			if (bp->bp_role != BSTP_ROLE_DISABLED) {
				bstp_disable_port(bs, bp);
				changed = 1;
				if ((bp->bp_flags & BSTP_PORT_ADMEDGE) &&
				    bp->bp_protover == BSTP_PROTO_RSTP)
					bp->bp_operedge = 1;
			}
		}
	} else if (bp->bp_infois != BSTP_INFO_DISABLED) {
		bstp_disable_port(bs, bp);
		changed = 1;
	}
	if (changed)
		bstp_assign_roles(bs);
	BSTP_UNLOCK(bs);
}

static void
bstp_enable_port(struct bstp_state *bs, struct bstp_port *bp)
{
	bp->bp_infois = BSTP_INFO_AGED;
}

static void
bstp_disable_port(struct bstp_state *bs, struct bstp_port *bp)
{
	bp->bp_infois = BSTP_INFO_DISABLED;
}

static void
bstp_tick(void *arg)
{
	struct bstp_state *bs = arg;
	struct bstp_port *bp;

	BSTP_LOCK_ASSERT(bs);

	if (bs->bs_running == 0)
		return;

	CURVNET_SET(bs->bs_vnet);

	/* poll link events on interfaces that do not support linkstate */
	if (bstp_timer_dectest(&bs->bs_link_timer)) {
		LIST_FOREACH(bp, &bs->bs_bplist, bp_next) {
			if (!(bp->bp_ifp->if_capabilities & IFCAP_LINKSTATE))
				taskqueue_enqueue(taskqueue_swi, &bp->bp_mediatask);
		}
		bstp_timer_start(&bs->bs_link_timer, BSTP_LINK_TIMER);
	}

	LIST_FOREACH(bp, &bs->bs_bplist, bp_next) {
		/* no events need to happen for these */
		bstp_timer_dectest(&bp->bp_tc_timer);
		bstp_timer_dectest(&bp->bp_recent_root_timer);
		bstp_timer_dectest(&bp->bp_forward_delay_timer);
		bstp_timer_dectest(&bp->bp_recent_backup_timer);

		if (bstp_timer_dectest(&bp->bp_hello_timer))
			bstp_hello_timer_expiry(bs, bp);

		if (bstp_timer_dectest(&bp->bp_message_age_timer))
			bstp_message_age_expiry(bs, bp);

		if (bstp_timer_dectest(&bp->bp_migrate_delay_timer))
			bstp_migrate_delay_expiry(bs, bp);

		if (bstp_timer_dectest(&bp->bp_edge_delay_timer))
			bstp_edge_delay_expiry(bs, bp);

		/* update the various state machines for the port */
		bstp_update_state(bs, bp);

		if (bp->bp_txcount > 0)
			bp->bp_txcount--;
	}

	CURVNET_RESTORE();

	callout_reset(&bs->bs_bstpcallout, hz, bstp_tick, bs);
}

static void
bstp_timer_start(struct bstp_timer *t, uint16_t v)
{
	t->value = v;
	t->active = 1;
	t->latched = 0;
}

static void
bstp_timer_stop(struct bstp_timer *t)
{
	t->value = 0;
	t->active = 0;
	t->latched = 0;
}

static void
bstp_timer_latch(struct bstp_timer *t)
{
	t->latched = 1;
	t->active = 1;
}

static int
bstp_timer_dectest(struct bstp_timer *t)
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

static void
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

static void
bstp_message_age_expiry(struct bstp_state *bs, struct bstp_port *bp)
{
	if (bp->bp_infois == BSTP_INFO_RECEIVED) {
		bp->bp_infois = BSTP_INFO_AGED;
		bstp_assign_roles(bs);
		DPRINTF("aged info on %s\n", bp->bp_ifp->if_xname);
	}
}

static void
bstp_migrate_delay_expiry(struct bstp_state *bs, struct bstp_port *bp)
{
	bp->bp_flags |= BSTP_PORT_CANMIGRATE;
}

static void
bstp_edge_delay_expiry(struct bstp_state *bs, struct bstp_port *bp)
{
	if ((bp->bp_flags & BSTP_PORT_AUTOEDGE) &&
	    bp->bp_protover == BSTP_PROTO_RSTP && bp->bp_proposing &&
	    bp->bp_role == BSTP_ROLE_DESIGNATED) {
		bp->bp_operedge = 1;
		DPRINTF("%s -> edge port\n", bp->bp_ifp->if_xname);
	}
}

static int
bstp_addr_cmp(const uint8_t *a, const uint8_t *b)
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
static int
bstp_same_bridgeid(uint64_t id1, uint64_t id2)
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
bstp_reinit(struct bstp_state *bs)
{
	struct epoch_tracker et;
	struct bstp_port *bp;
	struct ifnet *ifp, *mif;
	u_char *e_addr;
	void *bridgeptr;
	static const u_char llzero[ETHER_ADDR_LEN];	/* 00:00:00:00:00:00 */

	BSTP_LOCK_ASSERT(bs);

	if (LIST_EMPTY(&bs->bs_bplist))
		goto disablestp;

	mif = NULL;
	bridgeptr = LIST_FIRST(&bs->bs_bplist)->bp_ifp->if_bridge;
	KASSERT(bridgeptr != NULL, ("Invalid bridge pointer"));
	/*
	 * Search through the Ethernet adapters and find the one with the
	 * lowest value. Make sure the adapter which we take the MAC address
	 * from is part of this bridge, so we can have more than one independent
	 * bridges in the same STP domain.
	 */
	NET_EPOCH_ENTER(et);
	CK_STAILQ_FOREACH(ifp, &V_ifnet, if_link) {
		if (ifp->if_type != IFT_ETHER)
			continue;	/* Not Ethernet */

		if (ifp->if_bridge != bridgeptr)
			continue;	/* Not part of our bridge */

		if (bstp_addr_cmp(IF_LLADDR(ifp), llzero) == 0)
			continue;	/* No mac address set */

		if (mif == NULL) {
			mif = ifp;
			continue;
		}
		if (bstp_addr_cmp(IF_LLADDR(ifp), IF_LLADDR(mif)) < 0) {
			mif = ifp;
			continue;
		}
	}
	NET_EPOCH_EXIT(et);
	if (mif == NULL)
		goto disablestp;

	e_addr = IF_LLADDR(mif);
	bs->bs_bridge_pv.pv_dbridge_id =
	    (((uint64_t)bs->bs_bridge_priority) << 48) |
	    (((uint64_t)e_addr[0]) << 40) |
	    (((uint64_t)e_addr[1]) << 32) |
	    (((uint64_t)e_addr[2]) << 24) |
	    (((uint64_t)e_addr[3]) << 16) |
	    (((uint64_t)e_addr[4]) << 8) |
	    (((uint64_t)e_addr[5]));

	bs->bs_bridge_pv.pv_root_id = bs->bs_bridge_pv.pv_dbridge_id;
	bs->bs_bridge_pv.pv_cost = 0;
	bs->bs_bridge_pv.pv_dport_id = 0;
	bs->bs_bridge_pv.pv_port_id = 0;

	if (bs->bs_running && callout_pending(&bs->bs_bstpcallout) == 0)
		callout_reset(&bs->bs_bstpcallout, hz, bstp_tick, bs);

	LIST_FOREACH(bp, &bs->bs_bplist, bp_next) {
		bp->bp_port_id = (bp->bp_priority << 8) |
		    (bp->bp_ifp->if_index  & 0xfff);
		taskqueue_enqueue(taskqueue_swi, &bp->bp_mediatask);
	}

	bstp_assign_roles(bs);
	bstp_timer_start(&bs->bs_link_timer, BSTP_LINK_TIMER);
	return;

disablestp:
	/* Set the bridge and root id (lower bits) to zero */
	bs->bs_bridge_pv.pv_dbridge_id =
	    ((uint64_t)bs->bs_bridge_priority) << 48;
	bs->bs_bridge_pv.pv_root_id = bs->bs_bridge_pv.pv_dbridge_id;
	bs->bs_root_pv = bs->bs_bridge_pv;
	/* Disable any remaining ports, they will have no MAC address */
	LIST_FOREACH(bp, &bs->bs_bplist, bp_next) {
		bp->bp_infois = BSTP_INFO_DISABLED;
		bstp_set_port_role(bp, BSTP_ROLE_DISABLED);
	}
	callout_stop(&bs->bs_bstpcallout);
}

static int
bstp_modevent(module_t mod, int type, void *data)
{
	switch (type) {
	case MOD_LOAD:
		mtx_init(&bstp_list_mtx, "bridgestp list", NULL, MTX_DEF);
		LIST_INIT(&bstp_list);
		break;
	case MOD_UNLOAD:
		mtx_destroy(&bstp_list_mtx);
		break;
	default:
		return (EOPNOTSUPP);
	}
	return (0);
}

static moduledata_t bstp_mod = {
	"bridgestp",
	bstp_modevent,
	0
};

DECLARE_MODULE(bridgestp, bstp_mod, SI_SUB_PSEUDO, SI_ORDER_ANY);
MODULE_VERSION(bridgestp, 1);

void
bstp_attach(struct bstp_state *bs, struct bstp_cb_ops *cb)
{
	BSTP_LOCK_INIT(bs);
	callout_init_mtx(&bs->bs_bstpcallout, &bs->bs_mtx, 0);
	LIST_INIT(&bs->bs_bplist);

	bs->bs_bridge_max_age = BSTP_DEFAULT_MAX_AGE;
	bs->bs_bridge_htime = BSTP_DEFAULT_HELLO_TIME;
	bs->bs_bridge_fdelay = BSTP_DEFAULT_FORWARD_DELAY;
	bs->bs_bridge_priority = BSTP_DEFAULT_BRIDGE_PRIORITY;
	bs->bs_hold_time = BSTP_DEFAULT_HOLD_TIME;
	bs->bs_migration_delay = BSTP_DEFAULT_MIGRATE_DELAY;
	bs->bs_txholdcount = BSTP_DEFAULT_HOLD_COUNT;
	bs->bs_protover = BSTP_PROTO_RSTP;
	bs->bs_state_cb = cb->bcb_state;
	bs->bs_rtage_cb = cb->bcb_rtage;
	bs->bs_vnet = curvnet;

	getmicrotime(&bs->bs_last_tc_time);

	mtx_lock(&bstp_list_mtx);
	LIST_INSERT_HEAD(&bstp_list, bs, bs_list);
	mtx_unlock(&bstp_list_mtx);
}

void
bstp_detach(struct bstp_state *bs)
{
	KASSERT(LIST_EMPTY(&bs->bs_bplist), ("bstp still active"));

	mtx_lock(&bstp_list_mtx);
	LIST_REMOVE(bs, bs_list);
	mtx_unlock(&bstp_list_mtx);
	callout_drain(&bs->bs_bstpcallout);
	BSTP_LOCK_DESTROY(bs);
}

void
bstp_init(struct bstp_state *bs)
{
	BSTP_LOCK(bs);
	callout_reset(&bs->bs_bstpcallout, hz, bstp_tick, bs);
	bs->bs_running = 1;
	bstp_reinit(bs);
	BSTP_UNLOCK(bs);
}

void
bstp_stop(struct bstp_state *bs)
{
	struct bstp_port *bp;

	BSTP_LOCK(bs);

	LIST_FOREACH(bp, &bs->bs_bplist, bp_next)
		bstp_set_port_state(bp, BSTP_IFSTATE_DISCARDING);

	bs->bs_running = 0;
	callout_stop(&bs->bs_bstpcallout);
	BSTP_UNLOCK(bs);
}

int
bstp_create(struct bstp_state *bs, struct bstp_port *bp, struct ifnet *ifp)
{
	bzero(bp, sizeof(struct bstp_port));

	BSTP_LOCK(bs);
	bp->bp_ifp = ifp;
	bp->bp_bs = bs;
	bp->bp_priority = BSTP_DEFAULT_PORT_PRIORITY;
	TASK_INIT(&bp->bp_statetask, 0, bstp_notify_state, bp);
	TASK_INIT(&bp->bp_rtagetask, 0, bstp_notify_rtage, bp);
	TASK_INIT(&bp->bp_mediatask, 0, bstp_ifupdstatus, bp);

	/* Init state */
	bp->bp_infois = BSTP_INFO_DISABLED;
	bp->bp_flags = BSTP_PORT_AUTOEDGE|BSTP_PORT_AUTOPTP;
	bstp_set_port_state(bp, BSTP_IFSTATE_DISCARDING);
	bstp_set_port_proto(bp, bs->bs_protover);
	bstp_set_port_role(bp, BSTP_ROLE_DISABLED);
	bstp_set_port_tc(bp, BSTP_TCSTATE_INACTIVE);
	bp->bp_path_cost = bstp_calc_path_cost(bp);
	BSTP_UNLOCK(bs);
	return (0);
}

int
bstp_enable(struct bstp_port *bp)
{
	struct bstp_state *bs = bp->bp_bs;
	struct ifnet *ifp = bp->bp_ifp;

	KASSERT(bp->bp_active == 0, ("already a bstp member"));

	switch (ifp->if_type) {
		case IFT_ETHER:	/* These can do spanning tree. */
			break;
		default:
			/* Nothing else can. */
			return (EINVAL);
	}

	BSTP_LOCK(bs);
	LIST_INSERT_HEAD(&bs->bs_bplist, bp, bp_next);
	bp->bp_active = 1;
	bp->bp_flags |= BSTP_PORT_NEWINFO;
	bstp_reinit(bs);
	bstp_update_roles(bs, bp);
	BSTP_UNLOCK(bs);
	return (0);
}

void
bstp_disable(struct bstp_port *bp)
{
	struct bstp_state *bs = bp->bp_bs;

	KASSERT(bp->bp_active == 1, ("not a bstp member"));

	BSTP_LOCK(bs);
	bstp_disable_port(bs, bp);
	LIST_REMOVE(bp, bp_next);
	bp->bp_active = 0;
	bstp_reinit(bs);
	BSTP_UNLOCK(bs);
}

/*
 * The bstp_port structure is about to be freed by the parent bridge.
 */
void
bstp_destroy(struct bstp_port *bp)
{
	KASSERT(bp->bp_active == 0, ("port is still attached"));
	taskqueue_drain(taskqueue_swi, &bp->bp_statetask);
	taskqueue_drain(taskqueue_swi, &bp->bp_rtagetask);
	taskqueue_drain(taskqueue_swi, &bp->bp_mediatask);

	if (bp->bp_bs->bs_root_port == bp)
		bstp_assign_roles(bp->bp_bs);
}
