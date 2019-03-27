/*-
 * Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2016 Broadcom, All Rights Reserved.
 * The term Broadcom refers to Broadcom Limited and/or its subsidiaries
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/endian.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/ethernet.h>
#include <net/iflib.h>

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_rss.h"

#include "bnxt.h"

/*
 * Function prototypes
 */

static int bnxt_isc_txd_encap(void *sc, if_pkt_info_t pi);
static void bnxt_isc_txd_flush(void *sc, uint16_t txqid, qidx_t pidx);
static int bnxt_isc_txd_credits_update(void *sc, uint16_t txqid, bool clear);

static void bnxt_isc_rxd_refill(void *sc, if_rxd_update_t iru);

/*				uint16_t rxqid, uint8_t flid,
    uint32_t pidx, uint64_t *paddrs, caddr_t *vaddrs, uint16_t count,
    uint16_t buf_size);
*/
static void bnxt_isc_rxd_flush(void *sc, uint16_t rxqid, uint8_t flid,
    qidx_t pidx);
static int bnxt_isc_rxd_available(void *sc, uint16_t rxqid, qidx_t idx,
    qidx_t budget);
static int bnxt_isc_rxd_pkt_get(void *sc, if_rxd_info_t ri);

static int bnxt_intr(void *sc);

struct if_txrx bnxt_txrx  = {
	.ift_txd_encap = bnxt_isc_txd_encap,
	.ift_txd_flush = bnxt_isc_txd_flush,
	.ift_txd_credits_update = bnxt_isc_txd_credits_update,
	.ift_rxd_available = bnxt_isc_rxd_available,
	.ift_rxd_pkt_get = bnxt_isc_rxd_pkt_get,
	.ift_rxd_refill = bnxt_isc_rxd_refill,
	.ift_rxd_flush = bnxt_isc_rxd_flush,
	.ift_legacy_intr = bnxt_intr
};

/*
 * Device Dependent Packet Transmit and Receive Functions
 */

static const uint16_t bnxt_tx_lhint[] = {
	TX_BD_SHORT_FLAGS_LHINT_LT512,
	TX_BD_SHORT_FLAGS_LHINT_LT1K,
	TX_BD_SHORT_FLAGS_LHINT_LT2K,
	TX_BD_SHORT_FLAGS_LHINT_LT2K,
	TX_BD_SHORT_FLAGS_LHINT_GTE2K,
};

static int
bnxt_isc_txd_encap(void *sc, if_pkt_info_t pi)
{
	struct bnxt_softc *softc = (struct bnxt_softc *)sc;
	struct bnxt_ring *txr = &softc->tx_rings[pi->ipi_qsidx];
	struct tx_bd_long *tbd;
	struct tx_bd_long_hi *tbdh;
	bool need_hi = false;
	uint16_t flags_type;
	uint16_t lflags;
	uint32_t cfa_meta;
	int seg = 0;

	/* If we have offloads enabled, we need to use two BDs. */
	if ((pi->ipi_csum_flags & (CSUM_OFFLOAD | CSUM_TSO | CSUM_IP)) ||
	    pi->ipi_mflags & M_VLANTAG)
		need_hi = true;

	/* TODO: Devices before Cu+B1 need to not mix long and short BDs */
	need_hi = true;

	pi->ipi_new_pidx = pi->ipi_pidx;
	tbd = &((struct tx_bd_long *)txr->vaddr)[pi->ipi_new_pidx];
	pi->ipi_ndescs = 0;
	/* No need to byte-swap the opaque value */
	tbd->opaque = ((pi->ipi_nsegs + need_hi) << 24) | pi->ipi_new_pidx;
	tbd->len = htole16(pi->ipi_segs[seg].ds_len);
	tbd->addr = htole64(pi->ipi_segs[seg++].ds_addr);
	flags_type = ((pi->ipi_nsegs + need_hi) <<
	    TX_BD_SHORT_FLAGS_BD_CNT_SFT) & TX_BD_SHORT_FLAGS_BD_CNT_MASK;
	if (pi->ipi_len >= 2048)
		flags_type |= TX_BD_SHORT_FLAGS_LHINT_GTE2K;
	else
		flags_type |= bnxt_tx_lhint[pi->ipi_len >> 9];

	if (need_hi) {
		flags_type |= TX_BD_LONG_TYPE_TX_BD_LONG;

		pi->ipi_new_pidx = RING_NEXT(txr, pi->ipi_new_pidx);
		tbdh = &((struct tx_bd_long_hi *)txr->vaddr)[pi->ipi_new_pidx];
		tbdh->mss = htole16(pi->ipi_tso_segsz);
		tbdh->hdr_size = htole16((pi->ipi_ehdrlen + pi->ipi_ip_hlen +
		    pi->ipi_tcp_hlen) >> 1);
		tbdh->cfa_action = 0;
		lflags = 0;
		cfa_meta = 0;
		if (pi->ipi_mflags & M_VLANTAG) {
			/* TODO: Do we need to byte-swap the vtag here? */
			cfa_meta = TX_BD_LONG_CFA_META_KEY_VLAN_TAG |
			    pi->ipi_vtag;
			cfa_meta |= TX_BD_LONG_CFA_META_VLAN_TPID_TPID8100;
		}
		tbdh->cfa_meta = htole32(cfa_meta);
		if (pi->ipi_csum_flags & CSUM_TSO) {
			lflags |= TX_BD_LONG_LFLAGS_LSO |
			    TX_BD_LONG_LFLAGS_T_IPID;
		}
		else if(pi->ipi_csum_flags & CSUM_OFFLOAD) {
			lflags |= TX_BD_LONG_LFLAGS_TCP_UDP_CHKSUM |
			    TX_BD_LONG_LFLAGS_IP_CHKSUM;
		}
		else if(pi->ipi_csum_flags & CSUM_IP) {
			lflags |= TX_BD_LONG_LFLAGS_IP_CHKSUM;
		}
		tbdh->lflags = htole16(lflags);
	}
	else {
		flags_type |= TX_BD_SHORT_TYPE_TX_BD_SHORT;
	}

	for (; seg < pi->ipi_nsegs; seg++) {
		tbd->flags_type = htole16(flags_type);
		pi->ipi_new_pidx = RING_NEXT(txr, pi->ipi_new_pidx);
		tbd = &((struct tx_bd_long *)txr->vaddr)[pi->ipi_new_pidx];
		tbd->len = htole16(pi->ipi_segs[seg].ds_len);
		tbd->addr = htole64(pi->ipi_segs[seg].ds_addr);
		flags_type = TX_BD_SHORT_TYPE_TX_BD_SHORT;
	}
	flags_type |= TX_BD_SHORT_FLAGS_PACKET_END;
	tbd->flags_type = htole16(flags_type);
	pi->ipi_new_pidx = RING_NEXT(txr, pi->ipi_new_pidx);

	return 0;
}

static void
bnxt_isc_txd_flush(void *sc, uint16_t txqid, qidx_t pidx)
{
	struct bnxt_softc *softc = (struct bnxt_softc *)sc;
	struct bnxt_ring *tx_ring = &softc->tx_rings[txqid];

	/* pidx is what we last set ipi_new_pidx to */
	BNXT_TX_DB(tx_ring, pidx);
	/* TODO: Cumulus+ doesn't need the double doorbell */
	BNXT_TX_DB(tx_ring, pidx);
	return;
}

static int
bnxt_isc_txd_credits_update(void *sc, uint16_t txqid, bool clear)
{
	struct bnxt_softc *softc = (struct bnxt_softc *)sc;
	struct bnxt_cp_ring *cpr = &softc->tx_cp_rings[txqid];
	struct tx_cmpl *cmpl = (struct tx_cmpl *)cpr->ring.vaddr;
	int avail = 0;
	uint32_t cons = cpr->cons;
	bool v_bit = cpr->v_bit;
	bool last_v_bit;
	uint32_t last_cons;
	uint16_t type;
	uint16_t err;

	for (;;) {
		last_cons = cons;
		last_v_bit = v_bit;
		NEXT_CP_CONS_V(&cpr->ring, cons, v_bit);
		CMPL_PREFETCH_NEXT(cpr, cons);

		if (!CMP_VALID(&cmpl[cons], v_bit))
			goto done;

		type = cmpl[cons].flags_type & TX_CMPL_TYPE_MASK;
		switch (type) {
		case TX_CMPL_TYPE_TX_L2:
			err = (le16toh(cmpl[cons].errors_v) &
			    TX_CMPL_ERRORS_BUFFER_ERROR_MASK) >>
			    TX_CMPL_ERRORS_BUFFER_ERROR_SFT;
			if (err)
				device_printf(softc->dev,
				    "TX completion error %u\n", err);
			/* No need to byte-swap the opaque value */
			avail += cmpl[cons].opaque >> 24;
			/*
			 * If we're not clearing, iflib only cares if there's
			 * at least one buffer.  Don't scan the whole ring in
			 * this case.
			 */
			if (!clear)
				goto done;
			break;
		default:
			if (type & 1) {
				NEXT_CP_CONS_V(&cpr->ring, cons, v_bit);
				if (!CMP_VALID(&cmpl[cons], v_bit))
					goto done;
			}
			device_printf(softc->dev,
			    "Unhandled TX completion type %u\n", type);
			break;
		}
	}
done:

	if (clear && avail) {
		cpr->cons = last_cons;
		cpr->v_bit = last_v_bit;
		BNXT_CP_IDX_DISABLE_DB(&cpr->ring, cpr->cons);
	}

	return avail;
}

static void
bnxt_isc_rxd_refill(void *sc, if_rxd_update_t iru)
{
	struct bnxt_softc *softc = (struct bnxt_softc *)sc;
	struct bnxt_ring *rx_ring;
	struct rx_prod_pkt_bd *rxbd;
	uint16_t type;
	uint16_t i;
	uint16_t rxqid;
	uint16_t count, len;
	uint32_t pidx;
	uint8_t flid;
	uint64_t *paddrs;
	qidx_t	*frag_idxs;

	rxqid = iru->iru_qsidx;
	count = iru->iru_count;
	len = iru->iru_buf_size;
	pidx = iru->iru_pidx;
	flid = iru->iru_flidx;
	paddrs = iru->iru_paddrs;
	frag_idxs = iru->iru_idxs;

	if (flid == 0) {
		rx_ring = &softc->rx_rings[rxqid];
		type = RX_PROD_PKT_BD_TYPE_RX_PROD_PKT;
	}
	else {
		rx_ring = &softc->ag_rings[rxqid];
		type = RX_PROD_AGG_BD_TYPE_RX_PROD_AGG;
	}
	rxbd = (void *)rx_ring->vaddr;

	for (i=0; i<count; i++) {
		rxbd[pidx].flags_type = htole16(type);
		rxbd[pidx].len = htole16(len);
		/* No need to byte-swap the opaque value */
		rxbd[pidx].opaque = (((rxqid & 0xff) << 24) | (flid << 16)
		    | (frag_idxs[i]));
		rxbd[pidx].addr = htole64(paddrs[i]);
		if (++pidx == rx_ring->ring_size)
			pidx = 0;
	}
	return;
}

static void
bnxt_isc_rxd_flush(void *sc, uint16_t rxqid, uint8_t flid,
    qidx_t pidx)
{
	struct bnxt_softc *softc = (struct bnxt_softc *)sc;
	struct bnxt_ring *rx_ring;

	if (flid == 0)
		rx_ring = &softc->rx_rings[rxqid];
	else
		rx_ring = &softc->ag_rings[rxqid];

	/*
	 * We *must* update the completion ring before updating the RX ring
	 * or we will overrun the completion ring and the device will wedge for
	 * RX.
	 */
	if (softc->rx_cp_rings[rxqid].cons != UINT32_MAX)
		BNXT_CP_IDX_DISABLE_DB(&softc->rx_cp_rings[rxqid].ring,
		    softc->rx_cp_rings[rxqid].cons);
	/* We're given the last filled RX buffer here, not the next empty one */
	BNXT_RX_DB(rx_ring, RING_NEXT(rx_ring, pidx));
	/* TODO: Cumulus+ doesn't need the double doorbell */
	BNXT_RX_DB(rx_ring, RING_NEXT(rx_ring, pidx));
	return;
}

static int
bnxt_isc_rxd_available(void *sc, uint16_t rxqid, qidx_t idx, qidx_t budget)
{
	struct bnxt_softc *softc = (struct bnxt_softc *)sc;
	struct bnxt_cp_ring *cpr = &softc->rx_cp_rings[rxqid];
	struct rx_pkt_cmpl *rcp;
	struct rx_tpa_end_cmpl *rtpae;
	struct cmpl_base *cmp = (struct cmpl_base *)cpr->ring.vaddr;
	int avail = 0;
	uint32_t cons = cpr->cons;
	bool v_bit = cpr->v_bit;
	uint8_t ags;
	int i;
	uint16_t type;

	for (;;) {
		NEXT_CP_CONS_V(&cpr->ring, cons, v_bit);
		CMPL_PREFETCH_NEXT(cpr, cons);

		if (!CMP_VALID(&cmp[cons], v_bit))
			goto cmpl_invalid;

		type = le16toh(cmp[cons].type) & CMPL_BASE_TYPE_MASK;
		switch (type) {
		case CMPL_BASE_TYPE_RX_L2:
			rcp = (void *)&cmp[cons];
			ags = (rcp->agg_bufs_v1 & RX_PKT_CMPL_AGG_BUFS_MASK) >>
			    RX_PKT_CMPL_AGG_BUFS_SFT;
			NEXT_CP_CONS_V(&cpr->ring, cons, v_bit);
			CMPL_PREFETCH_NEXT(cpr, cons);

			if (!CMP_VALID(&cmp[cons], v_bit))
				goto cmpl_invalid;

			/* Now account for all the AG completions */
			for (i=0; i<ags; i++) {
				NEXT_CP_CONS_V(&cpr->ring, cons, v_bit);
				CMPL_PREFETCH_NEXT(cpr, cons);
				if (!CMP_VALID(&cmp[cons], v_bit))
					goto cmpl_invalid;
			}
			avail++;
			break;
		case CMPL_BASE_TYPE_RX_TPA_END:
			rtpae = (void *)&cmp[cons];
			ags = (rtpae->agg_bufs_v1 &
			    RX_TPA_END_CMPL_AGG_BUFS_MASK) >>
			    RX_TPA_END_CMPL_AGG_BUFS_SFT;
			NEXT_CP_CONS_V(&cpr->ring, cons, v_bit);
			CMPL_PREFETCH_NEXT(cpr, cons);

			if (!CMP_VALID(&cmp[cons], v_bit))
				goto cmpl_invalid;
			/* Now account for all the AG completions */
			for (i=0; i<ags; i++) {
				NEXT_CP_CONS_V(&cpr->ring, cons, v_bit);
				CMPL_PREFETCH_NEXT(cpr, cons);
				if (!CMP_VALID(&cmp[cons], v_bit))
					goto cmpl_invalid;
			}
			avail++;
			break;
		case CMPL_BASE_TYPE_RX_TPA_START:
			NEXT_CP_CONS_V(&cpr->ring, cons, v_bit);
			CMPL_PREFETCH_NEXT(cpr, cons);

			if (!CMP_VALID(&cmp[cons], v_bit))
				goto cmpl_invalid;
			break;
		case CMPL_BASE_TYPE_RX_AGG:
			break;
		default:
			device_printf(softc->dev,
			    "Unhandled completion type %d on RXQ %d\n",
			    type, rxqid);

			/* Odd completion types use two completions */
			if (type & 1) {
				NEXT_CP_CONS_V(&cpr->ring, cons, v_bit);
				CMPL_PREFETCH_NEXT(cpr, cons);

				if (!CMP_VALID(&cmp[cons], v_bit))
					goto cmpl_invalid;
			}
			break;
		}
		if (avail > budget)
			break;
	}
cmpl_invalid:

	return avail;
}

static void
bnxt_set_rsstype(if_rxd_info_t ri, uint8_t rss_hash_type)
{
	uint8_t rss_profile_id;

	rss_profile_id = BNXT_GET_RSS_PROFILE_ID(rss_hash_type);
	switch (rss_profile_id) {
	case BNXT_RSS_HASH_TYPE_TCPV4:
		ri->iri_rsstype = M_HASHTYPE_RSS_TCP_IPV4;
		break;
	case BNXT_RSS_HASH_TYPE_UDPV4:
		ri->iri_rsstype = M_HASHTYPE_RSS_UDP_IPV4;
		break;
	case BNXT_RSS_HASH_TYPE_IPV4:
		ri->iri_rsstype = M_HASHTYPE_RSS_IPV4;
		break;
	case BNXT_RSS_HASH_TYPE_TCPV6:
		ri->iri_rsstype = M_HASHTYPE_RSS_TCP_IPV6;
		break;
	case BNXT_RSS_HASH_TYPE_UDPV6:
		ri->iri_rsstype = M_HASHTYPE_RSS_UDP_IPV6;
		break;
	case BNXT_RSS_HASH_TYPE_IPV6:
		ri->iri_rsstype = M_HASHTYPE_RSS_IPV6;
		break;
	default:
		ri->iri_rsstype = M_HASHTYPE_OPAQUE_HASH;
		break;
	}
}

static int
bnxt_pkt_get_l2(struct bnxt_softc *softc, if_rxd_info_t ri,
    struct bnxt_cp_ring *cpr, uint16_t flags_type)
{
	struct rx_pkt_cmpl *rcp;
	struct rx_pkt_cmpl_hi *rcph;
	struct rx_abuf_cmpl *acp;
	uint32_t flags2;
	uint32_t errors;
	uint8_t	ags;
	int i;

	rcp = &((struct rx_pkt_cmpl *)cpr->ring.vaddr)[cpr->cons];

	/* Extract from the first 16-byte BD */
	if (flags_type & RX_PKT_CMPL_FLAGS_RSS_VALID) {
		ri->iri_flowid = le32toh(rcp->rss_hash);
		bnxt_set_rsstype(ri, rcp->rss_hash_type);
	}
	else {
		ri->iri_rsstype = M_HASHTYPE_NONE;
	}
	ags = (rcp->agg_bufs_v1 & RX_PKT_CMPL_AGG_BUFS_MASK) >>
	    RX_PKT_CMPL_AGG_BUFS_SFT;
	ri->iri_nfrags = ags + 1;
	/* No need to byte-swap the opaque value */
	ri->iri_frags[0].irf_flid = (rcp->opaque >> 16) & 0xff;
	ri->iri_frags[0].irf_idx = rcp->opaque & 0xffff;
	ri->iri_frags[0].irf_len = le16toh(rcp->len);
	ri->iri_len = le16toh(rcp->len);

	/* Now the second 16-byte BD */
	NEXT_CP_CONS_V(&cpr->ring, cpr->cons, cpr->v_bit);
	ri->iri_cidx = RING_NEXT(&cpr->ring, ri->iri_cidx);
	rcph = &((struct rx_pkt_cmpl_hi *)cpr->ring.vaddr)[cpr->cons];

	flags2 = le32toh(rcph->flags2);
	errors = le16toh(rcph->errors_v2);
	if ((flags2 & RX_PKT_CMPL_FLAGS2_META_FORMAT_MASK) ==
	    RX_PKT_CMPL_FLAGS2_META_FORMAT_VLAN) {
		ri->iri_flags |= M_VLANTAG;
		/* TODO: Should this be the entire 16-bits? */
		ri->iri_vtag = le32toh(rcph->metadata) &
		    (RX_PKT_CMPL_METADATA_VID_MASK | RX_PKT_CMPL_METADATA_DE |
		    RX_PKT_CMPL_METADATA_PRI_MASK);
	}
	if (flags2 & RX_PKT_CMPL_FLAGS2_IP_CS_CALC) {
		ri->iri_csum_flags |= CSUM_IP_CHECKED;
		if (!(errors & RX_PKT_CMPL_ERRORS_IP_CS_ERROR))
			ri->iri_csum_flags |= CSUM_IP_VALID;
	}
	if (flags2 & (RX_PKT_CMPL_FLAGS2_L4_CS_CALC |
		      RX_PKT_CMPL_FLAGS2_T_L4_CS_CALC)) {
		ri->iri_csum_flags |= CSUM_L4_CALC;
		if (!(errors & (RX_PKT_CMPL_ERRORS_L4_CS_ERROR |
				RX_PKT_CMPL_ERRORS_T_L4_CS_ERROR))) {
			ri->iri_csum_flags |= CSUM_L4_VALID;
			ri->iri_csum_data = 0xffff;
		}
	}

	/* And finally the ag ring stuff. */
	for (i=1; i < ri->iri_nfrags; i++) {
		NEXT_CP_CONS_V(&cpr->ring, cpr->cons, cpr->v_bit);
		ri->iri_cidx = RING_NEXT(&cpr->ring, ri->iri_cidx);
		acp = &((struct rx_abuf_cmpl *)cpr->ring.vaddr)[cpr->cons];

		/* No need to byte-swap the opaque value */
		ri->iri_frags[i].irf_flid = (acp->opaque >> 16 & 0xff);
		ri->iri_frags[i].irf_idx = acp->opaque & 0xffff;
		ri->iri_frags[i].irf_len = le16toh(acp->len);
		ri->iri_len += le16toh(acp->len);
	}

	return 0;
}

static int
bnxt_pkt_get_tpa(struct bnxt_softc *softc, if_rxd_info_t ri,
    struct bnxt_cp_ring *cpr, uint16_t flags_type)
{
	struct rx_tpa_end_cmpl *agend =
	    &((struct rx_tpa_end_cmpl *)cpr->ring.vaddr)[cpr->cons];
	struct rx_abuf_cmpl *acp;
	struct bnxt_full_tpa_start *tpas;
	uint32_t flags2;
	uint8_t	ags;
	uint8_t agg_id;
	int i;

	/* Get the agg_id */
	agg_id = (agend->agg_id & RX_TPA_END_CMPL_AGG_ID_MASK) >>
	    RX_TPA_END_CMPL_AGG_ID_SFT;
	tpas = &(softc->rx_rings[ri->iri_qsidx].tpa_start[agg_id]);

	/* Extract from the first 16-byte BD */
	if (le16toh(tpas->low.flags_type) & RX_TPA_START_CMPL_FLAGS_RSS_VALID) {
		ri->iri_flowid = le32toh(tpas->low.rss_hash);
		bnxt_set_rsstype(ri, tpas->low.rss_hash_type);
	}
	else {
		ri->iri_rsstype = M_HASHTYPE_NONE;
	}
	ags = (agend->agg_bufs_v1 & RX_TPA_END_CMPL_AGG_BUFS_MASK) >>
	    RX_TPA_END_CMPL_AGG_BUFS_SFT;
	ri->iri_nfrags = ags + 1;
	/* No need to byte-swap the opaque value */
	ri->iri_frags[0].irf_flid = ((tpas->low.opaque >> 16) & 0xff);
	ri->iri_frags[0].irf_idx = (tpas->low.opaque & 0xffff);
	ri->iri_frags[0].irf_len = le16toh(tpas->low.len);
	ri->iri_len = le16toh(tpas->low.len);

	/* Now the second 16-byte BD */
	NEXT_CP_CONS_V(&cpr->ring, cpr->cons, cpr->v_bit);
	ri->iri_cidx = RING_NEXT(&cpr->ring, ri->iri_cidx);

	flags2 = le32toh(tpas->high.flags2);
	if ((flags2 & RX_TPA_START_CMPL_FLAGS2_META_FORMAT_MASK) ==
	    RX_TPA_START_CMPL_FLAGS2_META_FORMAT_VLAN) {
		ri->iri_flags |= M_VLANTAG;
		/* TODO: Should this be the entire 16-bits? */
		ri->iri_vtag = le32toh(tpas->high.metadata) &
		    (RX_TPA_START_CMPL_METADATA_VID_MASK |
		    RX_TPA_START_CMPL_METADATA_DE |
		    RX_TPA_START_CMPL_METADATA_PRI_MASK);
	}
	if (flags2 & RX_TPA_START_CMPL_FLAGS2_IP_CS_CALC) {
		ri->iri_csum_flags |= CSUM_IP_CHECKED;
		ri->iri_csum_flags |= CSUM_IP_VALID;
	}
	if (flags2 & RX_TPA_START_CMPL_FLAGS2_L4_CS_CALC) {
		ri->iri_csum_flags |= CSUM_L4_CALC;
		ri->iri_csum_flags |= CSUM_L4_VALID;
		ri->iri_csum_data = 0xffff;
	}

	/* Now the ag ring stuff. */
	for (i=1; i < ri->iri_nfrags; i++) {
		NEXT_CP_CONS_V(&cpr->ring, cpr->cons, cpr->v_bit);
		ri->iri_cidx = RING_NEXT(&cpr->ring, ri->iri_cidx);
		acp = &((struct rx_abuf_cmpl *)cpr->ring.vaddr)[cpr->cons];

		/* No need to byte-swap the opaque value */
		ri->iri_frags[i].irf_flid = ((acp->opaque >> 16) & 0xff);
		ri->iri_frags[i].irf_idx = (acp->opaque & 0xffff);
		ri->iri_frags[i].irf_len = le16toh(acp->len);
		ri->iri_len += le16toh(acp->len);
	}

	/* And finally, the empty BD at the end... */
	ri->iri_nfrags++;
	/* No need to byte-swap the opaque value */
	ri->iri_frags[i].irf_flid = ((agend->opaque >> 16) & 0xff);
	ri->iri_frags[i].irf_idx = (agend->opaque & 0xffff);
	ri->iri_frags[i].irf_len = le16toh(agend->len);
	ri->iri_len += le16toh(agend->len);

	return 0;
}

/* If we return anything but zero, iflib will assert... */
static int
bnxt_isc_rxd_pkt_get(void *sc, if_rxd_info_t ri)
{
	struct bnxt_softc *softc = (struct bnxt_softc *)sc;
	struct bnxt_cp_ring *cpr = &softc->rx_cp_rings[ri->iri_qsidx];
	struct cmpl_base *cmp_q = (struct cmpl_base *)cpr->ring.vaddr;
	struct cmpl_base *cmp;
	struct rx_tpa_start_cmpl *rtpa;
	uint16_t flags_type;
	uint16_t type;
	uint8_t agg_id;

	for (;;) {
		NEXT_CP_CONS_V(&cpr->ring, cpr->cons, cpr->v_bit);
		ri->iri_cidx = RING_NEXT(&cpr->ring, ri->iri_cidx);
		CMPL_PREFETCH_NEXT(cpr, cpr->cons);
		cmp = &((struct cmpl_base *)cpr->ring.vaddr)[cpr->cons];

		flags_type = le16toh(cmp->type);
		type = flags_type & CMPL_BASE_TYPE_MASK;

		switch (type) {
		case CMPL_BASE_TYPE_RX_L2:
			return bnxt_pkt_get_l2(softc, ri, cpr, flags_type);
		case CMPL_BASE_TYPE_RX_TPA_END:
			return bnxt_pkt_get_tpa(softc, ri, cpr, flags_type);
		case CMPL_BASE_TYPE_RX_TPA_START:
			rtpa = (void *)&cmp_q[cpr->cons];
			agg_id = (rtpa->agg_id &
			    RX_TPA_START_CMPL_AGG_ID_MASK) >>
			    RX_TPA_START_CMPL_AGG_ID_SFT;
			softc->rx_rings[ri->iri_qsidx].tpa_start[agg_id].low = *rtpa;

			NEXT_CP_CONS_V(&cpr->ring, cpr->cons, cpr->v_bit);
			ri->iri_cidx = RING_NEXT(&cpr->ring, ri->iri_cidx);
			CMPL_PREFETCH_NEXT(cpr, cpr->cons);

			softc->rx_rings[ri->iri_qsidx].tpa_start[agg_id].high =
			    ((struct rx_tpa_start_cmpl_hi *)cmp_q)[cpr->cons];
			break;
		default:
			device_printf(softc->dev,
			    "Unhandled completion type %d on RXQ %d get\n",
			    type, ri->iri_qsidx);
			if (type & 1) {
				NEXT_CP_CONS_V(&cpr->ring, cpr->cons,
				    cpr->v_bit);
				ri->iri_cidx = RING_NEXT(&cpr->ring,
				    ri->iri_cidx);
				CMPL_PREFETCH_NEXT(cpr, cpr->cons);
			}
			break;
		}
	}

	return 0;
}

static int
bnxt_intr(void *sc)
{
	struct bnxt_softc *softc = (struct bnxt_softc *)sc;

	device_printf(softc->dev, "STUB: %s @ %s:%d\n", __func__, __FILE__, __LINE__);
	return ENOSYS;
}
