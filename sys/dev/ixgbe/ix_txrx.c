/******************************************************************************

  Copyright (c) 2001-2017, Intel Corporation
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

   3. Neither the name of the Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

******************************************************************************/
/*$FreeBSD$*/


#ifndef IXGBE_STANDALONE_BUILD
#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_rss.h"
#endif

#include "ixgbe.h"


/************************************************************************
 * Local Function prototypes
 ************************************************************************/
static int ixgbe_isc_txd_encap(void *arg, if_pkt_info_t pi);
static void ixgbe_isc_txd_flush(void *arg, uint16_t txqid, qidx_t pidx);
static int ixgbe_isc_txd_credits_update(void *arg, uint16_t txqid, bool clear);

static void ixgbe_isc_rxd_refill(void *arg, if_rxd_update_t iru);
static void ixgbe_isc_rxd_flush(void *arg, uint16_t qsidx, uint8_t flidx __unused, qidx_t pidx);
static int ixgbe_isc_rxd_available(void *arg, uint16_t qsidx, qidx_t pidx,
				   qidx_t budget);
static int ixgbe_isc_rxd_pkt_get(void *arg, if_rxd_info_t ri);

static void ixgbe_rx_checksum(u32 staterr, if_rxd_info_t ri, u32 ptype);
static int ixgbe_tx_ctx_setup(struct ixgbe_adv_tx_context_desc *, if_pkt_info_t);

extern void ixgbe_if_enable_intr(if_ctx_t ctx);
static int ixgbe_determine_rsstype(u16 pkt_info);

struct if_txrx ixgbe_txrx  = {
	.ift_txd_encap = ixgbe_isc_txd_encap,
	.ift_txd_flush = ixgbe_isc_txd_flush,
	.ift_txd_credits_update = ixgbe_isc_txd_credits_update,
	.ift_rxd_available = ixgbe_isc_rxd_available,
	.ift_rxd_pkt_get = ixgbe_isc_rxd_pkt_get,
	.ift_rxd_refill = ixgbe_isc_rxd_refill,
	.ift_rxd_flush = ixgbe_isc_rxd_flush,
	.ift_legacy_intr = NULL
};

extern if_shared_ctx_t ixgbe_sctx;

/************************************************************************
 * ixgbe_tx_ctx_setup
 *
 *   Advanced Context Descriptor setup for VLAN, CSUM or TSO
 *
 ************************************************************************/
static int
ixgbe_tx_ctx_setup(struct ixgbe_adv_tx_context_desc *TXD, if_pkt_info_t pi)
{
	u32 vlan_macip_lens, type_tucmd_mlhl;
	u32 olinfo_status, mss_l4len_idx, pktlen, offload;
	u8  ehdrlen;

	offload = TRUE;
	olinfo_status = mss_l4len_idx = vlan_macip_lens = type_tucmd_mlhl = 0;
	/* VLAN MACLEN IPLEN */
	vlan_macip_lens |= (htole16(pi->ipi_vtag) << IXGBE_ADVTXD_VLAN_SHIFT);

	/*
	 * Some of our VF devices need a context descriptor for every
	 * packet.  That means the ehdrlen needs to be non-zero in order
	 * for the host driver not to flag a malicious event. The stack
	 * will most likely populate this for all other reasons of why
	 * this function was called.
	 */
	if (pi->ipi_ehdrlen == 0) {
		ehdrlen = ETHER_HDR_LEN;
		ehdrlen += (pi->ipi_vtag != 0) ? ETHER_VLAN_ENCAP_LEN : 0;
	} else
		ehdrlen = pi->ipi_ehdrlen;
	vlan_macip_lens |= ehdrlen << IXGBE_ADVTXD_MACLEN_SHIFT;

	pktlen = pi->ipi_len;
	/* First check if TSO is to be used */
	if (pi->ipi_csum_flags & CSUM_TSO) {
		/* This is used in the transmit desc in encap */
		pktlen = pi->ipi_len - ehdrlen - pi->ipi_ip_hlen - pi->ipi_tcp_hlen;
		mss_l4len_idx |= (pi->ipi_tso_segsz << IXGBE_ADVTXD_MSS_SHIFT);
		mss_l4len_idx |= (pi->ipi_tcp_hlen << IXGBE_ADVTXD_L4LEN_SHIFT);
	}

	olinfo_status |= pktlen << IXGBE_ADVTXD_PAYLEN_SHIFT;

	if (pi->ipi_flags & IPI_TX_IPV4) {
		type_tucmd_mlhl |= IXGBE_ADVTXD_TUCMD_IPV4;
		/* Tell transmit desc to also do IPv4 checksum. */
		if (pi->ipi_csum_flags & (CSUM_IP|CSUM_TSO))
			olinfo_status |= IXGBE_TXD_POPTS_IXSM << 8;
	} else if (pi->ipi_flags & IPI_TX_IPV6)
		type_tucmd_mlhl |= IXGBE_ADVTXD_TUCMD_IPV6;
	else
		offload = FALSE;

	vlan_macip_lens |= pi->ipi_ip_hlen;

	switch (pi->ipi_ipproto) {
	case IPPROTO_TCP:
		if (pi->ipi_csum_flags & (CSUM_IP_TCP | CSUM_IP6_TCP | CSUM_TSO))
			type_tucmd_mlhl |= IXGBE_ADVTXD_TUCMD_L4T_TCP;
		else
			offload = FALSE;
		break;
	case IPPROTO_UDP:
		if (pi->ipi_csum_flags & (CSUM_IP_UDP | CSUM_IP6_UDP))
			type_tucmd_mlhl |= IXGBE_ADVTXD_TUCMD_L4T_UDP;
		else
			offload = FALSE;
		break;
	case IPPROTO_SCTP:
		if (pi->ipi_csum_flags & (CSUM_IP_SCTP | CSUM_IP6_SCTP))
			type_tucmd_mlhl |= IXGBE_ADVTXD_TUCMD_L4T_SCTP;
		else
			offload = FALSE;
		break;
	default:
		offload = FALSE;
		break;
	}
/* Insert L4 checksum into data descriptors */
	if (offload)
		olinfo_status |= IXGBE_TXD_POPTS_TXSM << 8;

	type_tucmd_mlhl |= IXGBE_ADVTXD_DCMD_DEXT | IXGBE_ADVTXD_DTYP_CTXT;

	/* Now copy bits into descriptor */
	TXD->vlan_macip_lens = htole32(vlan_macip_lens);
	TXD->type_tucmd_mlhl = htole32(type_tucmd_mlhl);
	TXD->seqnum_seed = htole32(0);
	TXD->mss_l4len_idx = htole32(mss_l4len_idx);

	return (olinfo_status);
} /* ixgbe_tx_ctx_setup */

/************************************************************************
 * ixgbe_isc_txd_encap
 ************************************************************************/
static int
ixgbe_isc_txd_encap(void *arg, if_pkt_info_t pi)
{
	struct adapter                   *sc = arg;
	if_softc_ctx_t                   scctx = sc->shared;
	struct ix_tx_queue               *que = &sc->tx_queues[pi->ipi_qsidx];
	struct tx_ring                   *txr = &que->txr;
	int                              nsegs = pi->ipi_nsegs;
	bus_dma_segment_t                *segs = pi->ipi_segs;
	union ixgbe_adv_tx_desc          *txd = NULL;
	struct ixgbe_adv_tx_context_desc *TXD;
	int                              i, j, first, pidx_last;
	u32                              olinfo_status, cmd, flags;
	qidx_t                           ntxd;

	cmd =  (IXGBE_ADVTXD_DTYP_DATA |
		IXGBE_ADVTXD_DCMD_IFCS | IXGBE_ADVTXD_DCMD_DEXT);

	if (pi->ipi_mflags & M_VLANTAG)
		cmd |= IXGBE_ADVTXD_DCMD_VLE;

	i = first = pi->ipi_pidx;
	flags = (pi->ipi_flags & IPI_TX_INTR) ? IXGBE_TXD_CMD_RS : 0;
	ntxd = scctx->isc_ntxd[0];

	TXD = (struct ixgbe_adv_tx_context_desc *) &txr->tx_base[first];
	if ((pi->ipi_csum_flags & CSUM_OFFLOAD) ||
	    (sc->feat_en & IXGBE_FEATURE_NEEDS_CTXD) ||
	    pi->ipi_vtag) {
		/*********************************************
		 * Set up the appropriate offload context
		 * this will consume the first descriptor
		 *********************************************/
		olinfo_status = ixgbe_tx_ctx_setup(TXD, pi);
		if (pi->ipi_csum_flags & CSUM_TSO) {
			cmd |= IXGBE_ADVTXD_DCMD_TSE;
			++txr->tso_tx;
		}

		if (++i == scctx->isc_ntxd[0])
			i = 0;
	} else {
		/* Indicate the whole packet as payload when not doing TSO */
		olinfo_status = pi->ipi_len << IXGBE_ADVTXD_PAYLEN_SHIFT;
	}

	olinfo_status |= IXGBE_ADVTXD_CC;
	pidx_last = 0;
	for (j = 0; j < nsegs; j++) {
		bus_size_t seglen;

		txd = &txr->tx_base[i];
		seglen = segs[j].ds_len;

		txd->read.buffer_addr = htole64(segs[j].ds_addr);
		txd->read.cmd_type_len = htole32(cmd | seglen);
		txd->read.olinfo_status = htole32(olinfo_status);

		pidx_last = i;
		if (++i == scctx->isc_ntxd[0]) {
			i = 0;
		}
	}

	if (flags) {
		txr->tx_rsq[txr->tx_rs_pidx] = pidx_last;
		txr->tx_rs_pidx = (txr->tx_rs_pidx + 1) & (ntxd - 1);
	}
	txd->read.cmd_type_len |= htole32(IXGBE_TXD_CMD_EOP | flags);

	txr->bytes += pi->ipi_len;
	pi->ipi_new_pidx = i;

	++txr->total_packets;

	return (0);
} /* ixgbe_isc_txd_encap */

/************************************************************************
 * ixgbe_isc_txd_flush
 ************************************************************************/
static void
ixgbe_isc_txd_flush(void *arg, uint16_t txqid, qidx_t pidx)
{
	struct adapter     *sc = arg;
	struct ix_tx_queue *que = &sc->tx_queues[txqid];
	struct tx_ring     *txr = &que->txr;

	IXGBE_WRITE_REG(&sc->hw, txr->tail, pidx);
} /* ixgbe_isc_txd_flush */

/************************************************************************
 * ixgbe_isc_txd_credits_update
 ************************************************************************/
static int
ixgbe_isc_txd_credits_update(void *arg, uint16_t txqid, bool clear)
{
	struct adapter     *sc = arg;
	if_softc_ctx_t     scctx = sc->shared;
	struct ix_tx_queue *que = &sc->tx_queues[txqid];
	struct tx_ring     *txr = &que->txr;
	qidx_t             processed = 0;
	int                updated;
	qidx_t             cur, prev, ntxd, rs_cidx;
	int32_t            delta;
	uint8_t            status;

	rs_cidx = txr->tx_rs_cidx;
	if (rs_cidx == txr->tx_rs_pidx)
		return (0);

	cur = txr->tx_rsq[rs_cidx];
	status = txr->tx_base[cur].wb.status;
	updated = !!(status & IXGBE_TXD_STAT_DD);

	if (!updated)
		return (0);

	/* If clear is false just let caller know that there
	 * are descriptors to reclaim */
	if (!clear)
		return (1);

	prev = txr->tx_cidx_processed;
	ntxd = scctx->isc_ntxd[0];
	do {
		MPASS(prev != cur);
		delta = (int32_t)cur - (int32_t)prev;
		if (delta < 0)
			delta += ntxd;
		MPASS(delta > 0);

		processed += delta;
		prev = cur;
		rs_cidx = (rs_cidx + 1) & (ntxd - 1);
		if (rs_cidx == txr->tx_rs_pidx)
			break;

		cur = txr->tx_rsq[rs_cidx];
		status = txr->tx_base[cur].wb.status;
	} while ((status & IXGBE_TXD_STAT_DD));

	txr->tx_rs_cidx = rs_cidx;
	txr->tx_cidx_processed = prev;

	return (processed);
} /* ixgbe_isc_txd_credits_update */

/************************************************************************
 * ixgbe_isc_rxd_refill
 ************************************************************************/
static void
ixgbe_isc_rxd_refill(void *arg, if_rxd_update_t iru)
{
	struct adapter *sc       = arg;
	struct ix_rx_queue *que  = &sc->rx_queues[iru->iru_qsidx];
	struct rx_ring *rxr      = &que->rxr;
	uint64_t *paddrs;
	int i;
	uint32_t next_pidx, pidx;
	uint16_t count;

	paddrs = iru->iru_paddrs;
	pidx = iru->iru_pidx;
	count = iru->iru_count;

	for (i = 0, next_pidx = pidx; i < count; i++) {
		rxr->rx_base[next_pidx].read.pkt_addr = htole64(paddrs[i]);
		if (++next_pidx == sc->shared->isc_nrxd[0])
			next_pidx = 0;
	}
} /* ixgbe_isc_rxd_refill */

/************************************************************************
 * ixgbe_isc_rxd_flush
 ************************************************************************/
static void
ixgbe_isc_rxd_flush(void *arg, uint16_t qsidx, uint8_t flidx __unused, qidx_t pidx)
{
	struct adapter     *sc  = arg;
	struct ix_rx_queue *que = &sc->rx_queues[qsidx];
	struct rx_ring     *rxr = &que->rxr;

	IXGBE_WRITE_REG(&sc->hw, rxr->tail, pidx);
} /* ixgbe_isc_rxd_flush */

/************************************************************************
 * ixgbe_isc_rxd_available
 ************************************************************************/
static int
ixgbe_isc_rxd_available(void *arg, uint16_t qsidx, qidx_t pidx, qidx_t budget)
{
	struct adapter          *sc = arg;
	struct ix_rx_queue      *que = &sc->rx_queues[qsidx];
	struct rx_ring          *rxr = &que->rxr;
	union ixgbe_adv_rx_desc *rxd;
	u32                      staterr;
	int                      cnt, i, nrxd;

	nrxd = sc->shared->isc_nrxd[0];
	for (cnt = 0, i = pidx; cnt < nrxd && cnt <= budget;) {
		rxd = &rxr->rx_base[i];
		staterr = le32toh(rxd->wb.upper.status_error);

		if ((staterr & IXGBE_RXD_STAT_DD) == 0)
			break;
		if (++i == nrxd)
			i = 0;
		if (staterr & IXGBE_RXD_STAT_EOP)
			cnt++;
	}
	return (cnt);
} /* ixgbe_isc_rxd_available */

/************************************************************************
 * ixgbe_isc_rxd_pkt_get
 *
 *   Routine sends data which has been dma'ed into host memory
 *   to upper layer. Initialize ri structure.
 *
 *   Returns 0 upon success, errno on failure
 ************************************************************************/

static int
ixgbe_isc_rxd_pkt_get(void *arg, if_rxd_info_t ri)
{
	struct adapter           *adapter = arg;
	struct ix_rx_queue       *que = &adapter->rx_queues[ri->iri_qsidx];
	struct rx_ring           *rxr = &que->rxr;
	struct ifnet             *ifp = iflib_get_ifp(adapter->ctx);
	union ixgbe_adv_rx_desc  *rxd;

	u16                      pkt_info, len, cidx, i;
	u16                      vtag = 0;
	u32                      ptype;
	u32                      staterr = 0;
	bool                     eop;

	i = 0;
	cidx = ri->iri_cidx;
	do {
		rxd = &rxr->rx_base[cidx];
		staterr = le32toh(rxd->wb.upper.status_error);
		pkt_info = le16toh(rxd->wb.lower.lo_dword.hs_rss.pkt_info);

		/* Error Checking then decrement count */
		MPASS ((staterr & IXGBE_RXD_STAT_DD) != 0);

		len = le16toh(rxd->wb.upper.length);
		ptype = le32toh(rxd->wb.lower.lo_dword.data) &
			IXGBE_RXDADV_PKTTYPE_MASK;

		ri->iri_len += len;
		rxr->bytes += len;

		rxd->wb.upper.status_error = 0;
		eop = ((staterr & IXGBE_RXD_STAT_EOP) != 0);

		if ( (rxr->vtag_strip) && (staterr & IXGBE_RXD_STAT_VP) ) {
			vtag = le16toh(rxd->wb.upper.vlan);
		} else {
			vtag = 0;
		}

		/* Make sure bad packets are discarded */
		if (eop && (staterr & IXGBE_RXDADV_ERR_FRAME_ERR_MASK) != 0) {

#if __FreeBSD_version >= 1100036
			if (adapter->feat_en & IXGBE_FEATURE_VF)
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
#endif

			rxr->rx_discarded++;
			return (EBADMSG);
		}
		ri->iri_frags[i].irf_flid = 0;
		ri->iri_frags[i].irf_idx = cidx;
		ri->iri_frags[i].irf_len = len;
		if (++cidx == adapter->shared->isc_nrxd[0])
			cidx = 0;
		i++;
		/* even a 16K packet shouldn't consume more than 8 clusters */
		MPASS(i < 9);
	} while (!eop);

	rxr->rx_packets++;
	rxr->packets++;
	rxr->rx_bytes += ri->iri_len;

	if ((ifp->if_capenable & IFCAP_RXCSUM) != 0)
		ixgbe_rx_checksum(staterr, ri,  ptype);

	ri->iri_flowid = le32toh(rxd->wb.lower.hi_dword.rss);
	ri->iri_rsstype = ixgbe_determine_rsstype(pkt_info);
	ri->iri_vtag = vtag;
	ri->iri_nfrags = i;
	if (vtag)
		ri->iri_flags |= M_VLANTAG;
	return (0);
} /* ixgbe_isc_rxd_pkt_get */

/************************************************************************
 * ixgbe_rx_checksum
 *
 *   Verify that the hardware indicated that the checksum is valid.
 *   Inform the stack about the status of checksum so that stack
 *   doesn't spend time verifying the checksum.
 ************************************************************************/
static void
ixgbe_rx_checksum(u32 staterr, if_rxd_info_t ri, u32 ptype)
{
	u16  status = (u16)staterr;
	u8   errors = (u8)(staterr >> 24);
	bool sctp = false;

	if ((ptype & IXGBE_RXDADV_PKTTYPE_ETQF) == 0 &&
	    (ptype & IXGBE_RXDADV_PKTTYPE_SCTP) != 0)
		sctp = TRUE;

	/* IPv4 checksum */
	if (status & IXGBE_RXD_STAT_IPCS) {
		if (!(errors & IXGBE_RXD_ERR_IPE)) {
			/* IP Checksum Good */
			ri->iri_csum_flags = CSUM_IP_CHECKED | CSUM_IP_VALID;
		} else
			ri->iri_csum_flags = 0;
	}
	/* TCP/UDP/SCTP checksum */
	if (status & IXGBE_RXD_STAT_L4CS) {
		u64 type = (CSUM_DATA_VALID | CSUM_PSEUDO_HDR);
#if __FreeBSD_version >= 800000
		if (sctp)
			type = CSUM_SCTP_VALID;
#endif
		if (!(errors & IXGBE_RXD_ERR_TCPE)) {
			ri->iri_csum_flags |= type;
			if (!sctp)
				ri->iri_csum_data = htons(0xffff);
		}
	}
} /* ixgbe_rx_checksum */

/************************************************************************
 * ixgbe_determine_rsstype
 *
 *   Parse the packet type to determine the appropriate hash
 ************************************************************************/
static int
ixgbe_determine_rsstype(u16 pkt_info)
{
	switch (pkt_info & IXGBE_RXDADV_RSSTYPE_MASK) {
	case IXGBE_RXDADV_RSSTYPE_IPV4_TCP:
		return M_HASHTYPE_RSS_TCP_IPV4;
	case IXGBE_RXDADV_RSSTYPE_IPV4:
		return M_HASHTYPE_RSS_IPV4;
	case IXGBE_RXDADV_RSSTYPE_IPV6_TCP:
		return M_HASHTYPE_RSS_TCP_IPV6;
	case IXGBE_RXDADV_RSSTYPE_IPV6_EX:
		return M_HASHTYPE_RSS_IPV6_EX;
	case IXGBE_RXDADV_RSSTYPE_IPV6:
		return M_HASHTYPE_RSS_IPV6;
	case IXGBE_RXDADV_RSSTYPE_IPV6_TCP_EX:
		return M_HASHTYPE_RSS_TCP_IPV6_EX;
	case IXGBE_RXDADV_RSSTYPE_IPV4_UDP:
		return M_HASHTYPE_RSS_UDP_IPV4;
	case IXGBE_RXDADV_RSSTYPE_IPV6_UDP:
		return M_HASHTYPE_RSS_UDP_IPV6;
	case IXGBE_RXDADV_RSSTYPE_IPV6_UDP_EX:
		return M_HASHTYPE_RSS_UDP_IPV6_EX;
	default:
		return M_HASHTYPE_OPAQUE;
	}
} /* ixgbe_determine_rsstype */
