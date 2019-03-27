/*-
 * Copyright (c) 2016 Nicole Graziano <nicole@nextbsd.org>
 * Copyright (c) 2017 Matthew Macy <mmacy@mattmacy.io>
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
 */

/* $FreeBSD$ */
#include "if_em.h"

#ifdef RSS
#include <net/rss_config.h>
#include <netinet/in_rss.h>
#endif

#ifdef VERBOSE_DEBUG
#define DPRINTF device_printf
#else
#define DPRINTF(...)
#endif

/*********************************************************************
 *  Local Function prototypes
 *********************************************************************/
static int em_tso_setup(struct adapter *adapter, if_pkt_info_t pi, u32 *txd_upper,
    u32 *txd_lower);
static int em_transmit_checksum_setup(struct adapter *adapter, if_pkt_info_t pi,
    u32 *txd_upper, u32 *txd_lower);
static int em_isc_txd_encap(void *arg, if_pkt_info_t pi);
static void em_isc_txd_flush(void *arg, uint16_t txqid, qidx_t pidx);
static int em_isc_txd_credits_update(void *arg, uint16_t txqid, bool clear);
static void em_isc_rxd_refill(void *arg, if_rxd_update_t iru);
static void em_isc_rxd_flush(void *arg, uint16_t rxqid, uint8_t flid __unused,
    qidx_t pidx);
static int em_isc_rxd_available(void *arg, uint16_t rxqid, qidx_t idx,
    qidx_t budget);
static int em_isc_rxd_pkt_get(void *arg, if_rxd_info_t ri);

static void lem_isc_rxd_refill(void *arg, if_rxd_update_t iru);

static int lem_isc_rxd_available(void *arg, uint16_t rxqid, qidx_t idx,
   qidx_t budget);
static int lem_isc_rxd_pkt_get(void *arg, if_rxd_info_t ri);

static void lem_receive_checksum(int status, int errors, if_rxd_info_t ri);
static void em_receive_checksum(uint32_t status, if_rxd_info_t ri);
static int em_determine_rsstype(u32 pkt_info);
extern int em_intr(void *arg);

struct if_txrx em_txrx = {
	.ift_txd_encap = em_isc_txd_encap,
	.ift_txd_flush = em_isc_txd_flush,
	.ift_txd_credits_update = em_isc_txd_credits_update,
	.ift_rxd_available = em_isc_rxd_available,
	.ift_rxd_pkt_get = em_isc_rxd_pkt_get,
	.ift_rxd_refill = em_isc_rxd_refill,
	.ift_rxd_flush = em_isc_rxd_flush,
	.ift_legacy_intr = em_intr
};

struct if_txrx lem_txrx = {
	.ift_txd_encap = em_isc_txd_encap,
	.ift_txd_flush = em_isc_txd_flush,
	.ift_txd_credits_update = em_isc_txd_credits_update,
	.ift_rxd_available = lem_isc_rxd_available,
	.ift_rxd_pkt_get = lem_isc_rxd_pkt_get,
	.ift_rxd_refill = lem_isc_rxd_refill,
	.ift_rxd_flush = em_isc_rxd_flush,
	.ift_legacy_intr = em_intr
};

extern if_shared_ctx_t em_sctx;

void
em_dump_rs(struct adapter *adapter)
{
	if_softc_ctx_t scctx = adapter->shared;
	struct em_tx_queue *que;
	struct tx_ring *txr;
	qidx_t i, ntxd, qid, cur;
	int16_t rs_cidx;
	uint8_t status;

	printf("\n");
	ntxd = scctx->isc_ntxd[0];
	for (qid = 0; qid < adapter->tx_num_queues; qid++) {
		que = &adapter->tx_queues[qid];
		txr =  &que->txr;
		rs_cidx = txr->tx_rs_cidx;
		if (rs_cidx != txr->tx_rs_pidx) {
			cur = txr->tx_rsq[rs_cidx];
			status = txr->tx_base[cur].upper.fields.status;
			if (!(status & E1000_TXD_STAT_DD))
				printf("qid[%d]->tx_rsq[%d]: %d clear ", qid, rs_cidx, cur);
		} else {
			rs_cidx = (rs_cidx-1)&(ntxd-1);
			cur = txr->tx_rsq[rs_cidx];
			printf("qid[%d]->tx_rsq[rs_cidx-1=%d]: %d  ", qid, rs_cidx, cur);
		}
		printf("cidx_prev=%d rs_pidx=%d ",txr->tx_cidx_processed, txr->tx_rs_pidx);
		for (i = 0; i < ntxd; i++) {
			if (txr->tx_base[i].upper.fields.status & E1000_TXD_STAT_DD)
				printf("%d set ", i);
		}
		printf("\n");
	}
}

/**********************************************************************
 *
 *  Setup work for hardware segmentation offload (TSO) on
 *  adapters using advanced tx descriptors
 *
 **********************************************************************/
static int
em_tso_setup(struct adapter *adapter, if_pkt_info_t pi, u32 *txd_upper, u32 *txd_lower)
{
	if_softc_ctx_t scctx = adapter->shared;
	struct em_tx_queue *que = &adapter->tx_queues[pi->ipi_qsidx];
	struct tx_ring *txr = &que->txr;
	struct e1000_context_desc *TXD;
	int cur, hdr_len;

	hdr_len = pi->ipi_ehdrlen + pi->ipi_ip_hlen + pi->ipi_tcp_hlen;
	*txd_lower = (E1000_TXD_CMD_DEXT |	/* Extended descr type */
		      E1000_TXD_DTYP_D |	/* Data descr type */
		      E1000_TXD_CMD_TSE);	/* Do TSE on this packet */

	/* IP and/or TCP header checksum calculation and insertion. */
	*txd_upper = (E1000_TXD_POPTS_IXSM | E1000_TXD_POPTS_TXSM) << 8;

	cur = pi->ipi_pidx;
	TXD = (struct e1000_context_desc *)&txr->tx_base[cur];

	/*
	 * Start offset for header checksum calculation.
	 * End offset for header checksum calculation.
	 * Offset of place put the checksum.
	 */
	TXD->lower_setup.ip_fields.ipcss = pi->ipi_ehdrlen;
	TXD->lower_setup.ip_fields.ipcse =
	    htole16(pi->ipi_ehdrlen + pi->ipi_ip_hlen - 1);
	TXD->lower_setup.ip_fields.ipcso = pi->ipi_ehdrlen + offsetof(struct ip, ip_sum);

	/*
	 * Start offset for payload checksum calculation.
	 * End offset for payload checksum calculation.
	 * Offset of place to put the checksum.
	 */
	TXD->upper_setup.tcp_fields.tucss = pi->ipi_ehdrlen + pi->ipi_ip_hlen;
	TXD->upper_setup.tcp_fields.tucse = 0;
	TXD->upper_setup.tcp_fields.tucso =
	    pi->ipi_ehdrlen + pi->ipi_ip_hlen + offsetof(struct tcphdr, th_sum);

	/*
	 * Payload size per packet w/o any headers.
	 * Length of all headers up to payload.
	 */
	TXD->tcp_seg_setup.fields.mss = htole16(pi->ipi_tso_segsz);
	TXD->tcp_seg_setup.fields.hdr_len = hdr_len;

	TXD->cmd_and_length = htole32(adapter->txd_cmd |
				E1000_TXD_CMD_DEXT |	/* Extended descr */
				E1000_TXD_CMD_TSE |	/* TSE context */
				E1000_TXD_CMD_IP |	/* Do IP csum */
				E1000_TXD_CMD_TCP |	/* Do TCP checksum */
				      (pi->ipi_len - hdr_len)); /* Total len */
	txr->tx_tso = TRUE;

	if (++cur == scctx->isc_ntxd[0]) {
		cur = 0;
	}
	DPRINTF(iflib_get_dev(adapter->ctx), "%s: pidx: %d cur: %d\n", __FUNCTION__, pi->ipi_pidx, cur);
	return (cur);
}

#define TSO_WORKAROUND 4
#define DONT_FORCE_CTX 1


/*********************************************************************
 *  The offload context is protocol specific (TCP/UDP) and thus
 *  only needs to be set when the protocol changes. The occasion
 *  of a context change can be a performance detriment, and
 *  might be better just disabled. The reason arises in the way
 *  in which the controller supports pipelined requests from the
 *  Tx data DMA. Up to four requests can be pipelined, and they may
 *  belong to the same packet or to multiple packets. However all
 *  requests for one packet are issued before a request is issued
 *  for a subsequent packet and if a request for the next packet
 *  requires a context change, that request will be stalled
 *  until the previous request completes. This means setting up
 *  a new context effectively disables pipelined Tx data DMA which
 *  in turn greatly slow down performance to send small sized
 *  frames.
 **********************************************************************/

static int
em_transmit_checksum_setup(struct adapter *adapter, if_pkt_info_t pi, u32 *txd_upper, u32 *txd_lower)
{
	 struct e1000_context_desc *TXD = NULL;
	if_softc_ctx_t scctx = adapter->shared;
	struct em_tx_queue *que = &adapter->tx_queues[pi->ipi_qsidx];
	struct tx_ring *txr = &que->txr;
	int csum_flags = pi->ipi_csum_flags;
	int cur, hdr_len;
	u32 cmd;

	cur = pi->ipi_pidx;
	hdr_len = pi->ipi_ehdrlen + pi->ipi_ip_hlen;
	cmd = adapter->txd_cmd;

	/*
	 * The 82574L can only remember the *last* context used
	 * regardless of queue that it was use for.  We cannot reuse
	 * contexts on this hardware platform and must generate a new
	 * context every time.  82574L hardware spec, section 7.2.6,
	 * second note.
	 */
	if (DONT_FORCE_CTX &&
	    adapter->tx_num_queues == 1 &&
	    txr->csum_lhlen == pi->ipi_ehdrlen &&
	    txr->csum_iphlen == pi->ipi_ip_hlen &&
	    txr->csum_flags == csum_flags) {
		/*
		 * Same csum offload context as the previous packets;
		 * just return.
		 */
		*txd_upper = txr->csum_txd_upper;
		*txd_lower = txr->csum_txd_lower;
		return (cur);
	}

	TXD = (struct e1000_context_desc *)&txr->tx_base[cur];
	if (csum_flags & CSUM_IP) {
		*txd_upper |= E1000_TXD_POPTS_IXSM << 8;
		/*
		 * Start offset for header checksum calculation.
		 * End offset for header checksum calculation.
		 * Offset of place to put the checksum.
		 */
		TXD->lower_setup.ip_fields.ipcss = pi->ipi_ehdrlen;
		TXD->lower_setup.ip_fields.ipcse = htole16(hdr_len);
		TXD->lower_setup.ip_fields.ipcso = pi->ipi_ehdrlen + offsetof(struct ip, ip_sum);
		cmd |= E1000_TXD_CMD_IP;
	}

	if (csum_flags & (CSUM_TCP|CSUM_UDP)) {
		uint8_t tucso;

		*txd_upper |= E1000_TXD_POPTS_TXSM << 8;
		*txd_lower = E1000_TXD_CMD_DEXT | E1000_TXD_DTYP_D;

		if (csum_flags & CSUM_TCP) {
			tucso = hdr_len + offsetof(struct tcphdr, th_sum);
			cmd |= E1000_TXD_CMD_TCP;
		} else
			tucso = hdr_len + offsetof(struct udphdr, uh_sum);
		TXD->upper_setup.tcp_fields.tucss = hdr_len;
		TXD->upper_setup.tcp_fields.tucse = htole16(0);
		TXD->upper_setup.tcp_fields.tucso = tucso;
	}

	txr->csum_lhlen = pi->ipi_ehdrlen;
	txr->csum_iphlen = pi->ipi_ip_hlen;
	txr->csum_flags = csum_flags;
	txr->csum_txd_upper = *txd_upper;
	txr->csum_txd_lower = *txd_lower;

	TXD->tcp_seg_setup.data = htole32(0);
	TXD->cmd_and_length =
		htole32(E1000_TXD_CMD_IFCS | E1000_TXD_CMD_DEXT | cmd);

	if (++cur == scctx->isc_ntxd[0]) {
		cur = 0;
	}
	DPRINTF(iflib_get_dev(adapter->ctx), "checksum_setup csum_flags=%x txd_upper=%x txd_lower=%x hdr_len=%d cmd=%x\n",
		      csum_flags, *txd_upper, *txd_lower, hdr_len, cmd);
	return (cur);
}

static int
em_isc_txd_encap(void *arg, if_pkt_info_t pi)
{
	struct adapter *sc = arg;
	if_softc_ctx_t scctx = sc->shared;
	struct em_tx_queue *que = &sc->tx_queues[pi->ipi_qsidx];
	struct tx_ring *txr = &que->txr;
	bus_dma_segment_t *segs = pi->ipi_segs;
	int nsegs = pi->ipi_nsegs;
	int csum_flags = pi->ipi_csum_flags;
	int i, j, first, pidx_last;
	u32 txd_flags, txd_upper = 0, txd_lower = 0;

	struct e1000_tx_desc *ctxd = NULL;
	bool do_tso, tso_desc;
	qidx_t ntxd;

	txd_flags = pi->ipi_flags & IPI_TX_INTR ? E1000_TXD_CMD_RS : 0;
	i = first = pi->ipi_pidx;
	do_tso = (csum_flags & CSUM_TSO);
	tso_desc = FALSE;
	ntxd = scctx->isc_ntxd[0];
	/*
	 * TSO Hardware workaround, if this packet is not
	 * TSO, and is only a single descriptor long, and
	 * it follows a TSO burst, then we need to add a
	 * sentinel descriptor to prevent premature writeback.
	 */
	if ((!do_tso) && (txr->tx_tso == TRUE)) {
		if (nsegs == 1)
			tso_desc = TRUE;
		txr->tx_tso = FALSE;
	}

	/* Do hardware assists */
	if (do_tso) {
		i = em_tso_setup(sc, pi, &txd_upper, &txd_lower);
		tso_desc = TRUE;
	} else if (csum_flags & EM_CSUM_OFFLOAD) {
		i = em_transmit_checksum_setup(sc, pi, &txd_upper, &txd_lower);
	}

	if (pi->ipi_mflags & M_VLANTAG) {
		/* Set the vlan id. */
		txd_upper |= htole16(pi->ipi_vtag) << 16;
		/* Tell hardware to add tag */
		txd_lower |= htole32(E1000_TXD_CMD_VLE);
	}

	DPRINTF(iflib_get_dev(sc->ctx), "encap: set up tx: nsegs=%d first=%d i=%d\n", nsegs, first, i);
	/* XXX adapter->pcix_82544 -- lem_fill_descriptors */

	/* Set up our transmit descriptors */
	for (j = 0; j < nsegs; j++) {
		bus_size_t seg_len;
		bus_addr_t seg_addr;
		uint32_t cmd;

		ctxd = &txr->tx_base[i];
		seg_addr = segs[j].ds_addr;
		seg_len = segs[j].ds_len;
		cmd = E1000_TXD_CMD_IFCS | sc->txd_cmd;

		/*
		 * TSO Workaround:
		 * If this is the last descriptor, we want to
		 * split it so we have a small final sentinel
		 */
		if (tso_desc && (j == (nsegs - 1)) && (seg_len > 8)) {
			seg_len -= TSO_WORKAROUND;
			ctxd->buffer_addr = htole64(seg_addr);
			ctxd->lower.data = htole32(cmd | txd_lower | seg_len);
			ctxd->upper.data = htole32(txd_upper);

			if (++i == scctx->isc_ntxd[0])
				i = 0;

			/* Now make the sentinel */
			ctxd = &txr->tx_base[i];
			ctxd->buffer_addr = htole64(seg_addr + seg_len);
			ctxd->lower.data = htole32(cmd | txd_lower | TSO_WORKAROUND);
			ctxd->upper.data = htole32(txd_upper);
			pidx_last = i;
			if (++i == scctx->isc_ntxd[0])
				i = 0;
			DPRINTF(iflib_get_dev(sc->ctx), "TSO path pidx_last=%d i=%d ntxd[0]=%d\n", pidx_last, i, scctx->isc_ntxd[0]);
		} else {
			ctxd->buffer_addr = htole64(seg_addr);
			ctxd->lower.data = htole32(cmd | txd_lower | seg_len);
			ctxd->upper.data = htole32(txd_upper);
			pidx_last = i;
			if (++i == scctx->isc_ntxd[0])
				i = 0;
			DPRINTF(iflib_get_dev(sc->ctx), "pidx_last=%d i=%d ntxd[0]=%d\n", pidx_last, i, scctx->isc_ntxd[0]);
		}
	}

	/*
	 * Last Descriptor of Packet
	 * needs End Of Packet (EOP)
	 * and Report Status (RS)
	 */
	if (txd_flags && nsegs) {
		txr->tx_rsq[txr->tx_rs_pidx] = pidx_last;
		DPRINTF(iflib_get_dev(sc->ctx), "setting to RS on %d rs_pidx %d first: %d\n", pidx_last, txr->tx_rs_pidx, first);
		txr->tx_rs_pidx = (txr->tx_rs_pidx+1) & (ntxd-1);
		MPASS(txr->tx_rs_pidx != txr->tx_rs_cidx);
	}
	ctxd->lower.data |= htole32(E1000_TXD_CMD_EOP | txd_flags);
	DPRINTF(iflib_get_dev(sc->ctx), "tx_buffers[%d]->eop = %d ipi_new_pidx=%d\n", first, pidx_last, i);
	pi->ipi_new_pidx = i;

	return (0);
}

static void
em_isc_txd_flush(void *arg, uint16_t txqid, qidx_t pidx)
{
	struct adapter *adapter = arg;
	struct em_tx_queue *que = &adapter->tx_queues[txqid];
	struct tx_ring *txr = &que->txr;

	E1000_WRITE_REG(&adapter->hw, E1000_TDT(txr->me), pidx);
}

static int
em_isc_txd_credits_update(void *arg, uint16_t txqid, bool clear)
{
	struct adapter *adapter = arg;
	if_softc_ctx_t scctx = adapter->shared;
	struct em_tx_queue *que = &adapter->tx_queues[txqid];
	struct tx_ring *txr = &que->txr;

	qidx_t processed = 0;
	int updated;
	qidx_t cur, prev, ntxd, rs_cidx;
	int32_t delta;
	uint8_t status;

	rs_cidx = txr->tx_rs_cidx;
	if (rs_cidx == txr->tx_rs_pidx)
		return (0);
	cur = txr->tx_rsq[rs_cidx];
	MPASS(cur != QIDX_INVALID);
	status = txr->tx_base[cur].upper.fields.status;
	updated = !!(status & E1000_TXD_STAT_DD);

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
		DPRINTF(iflib_get_dev(adapter->ctx),
			      "%s: cidx_processed=%u cur=%u clear=%d delta=%d\n",
			      __FUNCTION__, prev, cur, clear, delta);

		processed += delta;
		prev  = cur;
		rs_cidx = (rs_cidx + 1) & (ntxd-1);
		if (rs_cidx  == txr->tx_rs_pidx)
			break;
		cur = txr->tx_rsq[rs_cidx];
		MPASS(cur != QIDX_INVALID);
		status = txr->tx_base[cur].upper.fields.status;
	} while ((status & E1000_TXD_STAT_DD));

	txr->tx_rs_cidx = rs_cidx;
	txr->tx_cidx_processed = prev;
	return(processed);
}

static void
lem_isc_rxd_refill(void *arg, if_rxd_update_t iru)
{
	struct adapter *sc = arg;
	if_softc_ctx_t scctx = sc->shared;
	struct em_rx_queue *que = &sc->rx_queues[iru->iru_qsidx];
	struct rx_ring *rxr = &que->rxr;
	struct e1000_rx_desc *rxd;
	uint64_t *paddrs;
	uint32_t next_pidx, pidx;
	uint16_t count;
	int i;

	paddrs = iru->iru_paddrs;
	pidx = iru->iru_pidx;
	count = iru->iru_count;

	for (i = 0, next_pidx = pidx; i < count; i++) {
		rxd = (struct e1000_rx_desc *)&rxr->rx_base[next_pidx];
		rxd->buffer_addr = htole64(paddrs[i]);
		/* status bits must be cleared */
		rxd->status = 0;

		if (++next_pidx == scctx->isc_nrxd[0])
			next_pidx = 0;
	}
}

static void
em_isc_rxd_refill(void *arg, if_rxd_update_t iru)
{
	struct adapter *sc = arg;
	if_softc_ctx_t scctx = sc->shared;
	uint16_t rxqid = iru->iru_qsidx;
	struct em_rx_queue *que = &sc->rx_queues[rxqid];
	struct rx_ring *rxr = &que->rxr;
	union e1000_rx_desc_extended *rxd;
	uint64_t *paddrs;
	uint32_t next_pidx, pidx;
	uint16_t count;
	int i;

	paddrs = iru->iru_paddrs;
	pidx = iru->iru_pidx;
	count = iru->iru_count;

	for (i = 0, next_pidx = pidx; i < count; i++) {
		rxd = &rxr->rx_base[next_pidx];
		rxd->read.buffer_addr = htole64(paddrs[i]);
		/* DD bits must be cleared */
		rxd->wb.upper.status_error = 0;

		if (++next_pidx == scctx->isc_nrxd[0])
			next_pidx = 0;
	}
}

static void
em_isc_rxd_flush(void *arg, uint16_t rxqid, uint8_t flid __unused, qidx_t pidx)
{
	struct adapter *sc = arg;
	struct em_rx_queue *que = &sc->rx_queues[rxqid];
	struct rx_ring *rxr = &que->rxr;

	E1000_WRITE_REG(&sc->hw, E1000_RDT(rxr->me), pidx);
}

static int
lem_isc_rxd_available(void *arg, uint16_t rxqid, qidx_t idx, qidx_t budget)
{
	struct adapter *sc = arg;
	if_softc_ctx_t scctx = sc->shared;
	struct em_rx_queue *que = &sc->rx_queues[rxqid];
	struct rx_ring *rxr = &que->rxr;
	struct e1000_rx_desc *rxd;
	u32 staterr = 0;
	int cnt, i;

	for (cnt = 0, i = idx; cnt < scctx->isc_nrxd[0] && cnt <= budget;) {
		rxd = (struct e1000_rx_desc *)&rxr->rx_base[i];
		staterr = rxd->status;

		if ((staterr & E1000_RXD_STAT_DD) == 0)
			break;
		if (++i == scctx->isc_nrxd[0])
			i = 0;
		if (staterr & E1000_RXD_STAT_EOP)
			cnt++;
	}
	return (cnt);
}

static int
em_isc_rxd_available(void *arg, uint16_t rxqid, qidx_t idx, qidx_t budget)
{
	struct adapter *sc = arg;
	if_softc_ctx_t scctx = sc->shared;
	struct em_rx_queue *que = &sc->rx_queues[rxqid];
	struct rx_ring *rxr = &que->rxr;
	union e1000_rx_desc_extended *rxd;
	u32 staterr = 0;
	int cnt, i;

	for (cnt = 0, i = idx; cnt < scctx->isc_nrxd[0] && cnt <= budget;) {
		rxd = &rxr->rx_base[i];
		staterr = le32toh(rxd->wb.upper.status_error);

		if ((staterr & E1000_RXD_STAT_DD) == 0)
			break;
		if (++i == scctx->isc_nrxd[0])
			i = 0;
		if (staterr & E1000_RXD_STAT_EOP)
			cnt++;
	}
	return (cnt);
}

static int
lem_isc_rxd_pkt_get(void *arg, if_rxd_info_t ri)
{
	struct adapter *adapter = arg;
	if_softc_ctx_t scctx = adapter->shared;
	struct em_rx_queue *que = &adapter->rx_queues[ri->iri_qsidx];
	struct rx_ring *rxr = &que->rxr;
	struct e1000_rx_desc *rxd;
	u16 len;
	u32 status, errors;
	bool eop;
	int i, cidx;

	status = errors = i = 0;
	cidx = ri->iri_cidx;

	do {
		rxd = (struct e1000_rx_desc *)&rxr->rx_base[cidx];
		status = rxd->status;
		errors = rxd->errors;

		/* Error Checking then decrement count */
		MPASS ((status & E1000_RXD_STAT_DD) != 0);

		len = le16toh(rxd->length);
		ri->iri_len += len;

		eop = (status & E1000_RXD_STAT_EOP) != 0;

		/* Make sure bad packets are discarded */
		if (errors & E1000_RXD_ERR_FRAME_ERR_MASK) {
			adapter->dropped_pkts++;
			/* XXX fixup if common */
			return (EBADMSG);
		}

		ri->iri_frags[i].irf_flid = 0;
		ri->iri_frags[i].irf_idx = cidx;
		ri->iri_frags[i].irf_len = len;
		/* Zero out the receive descriptors status. */
		rxd->status = 0;

		if (++cidx == scctx->isc_nrxd[0])
			cidx = 0;
		i++;
	} while (!eop);

	/* XXX add a faster way to look this up */
	if (adapter->hw.mac.type >= e1000_82543 && !(status & E1000_RXD_STAT_IXSM))
		lem_receive_checksum(status, errors, ri);

	if (status & E1000_RXD_STAT_VP) {
		ri->iri_vtag = le16toh(rxd->special);
		ri->iri_flags |= M_VLANTAG;
	}

	ri->iri_nfrags = i;

	return (0);
}

static int
em_isc_rxd_pkt_get(void *arg, if_rxd_info_t ri)
{
	struct adapter *adapter = arg;
	if_softc_ctx_t scctx = adapter->shared;
	struct em_rx_queue *que = &adapter->rx_queues[ri->iri_qsidx];
	struct rx_ring *rxr = &que->rxr;
	union e1000_rx_desc_extended *rxd;

	u16 len;
	u32 pkt_info;
	u32 staterr = 0;
	bool eop;
	int i, cidx, vtag;

	i = vtag = 0;
	cidx = ri->iri_cidx;

	do {
		rxd = &rxr->rx_base[cidx];
		staterr = le32toh(rxd->wb.upper.status_error);
		pkt_info = le32toh(rxd->wb.lower.mrq);

		/* Error Checking then decrement count */
		MPASS ((staterr & E1000_RXD_STAT_DD) != 0);

		len = le16toh(rxd->wb.upper.length);
		ri->iri_len += len;

		eop = (staterr & E1000_RXD_STAT_EOP) != 0;

		/* Make sure bad packets are discarded */
		if (staterr & E1000_RXDEXT_ERR_FRAME_ERR_MASK) {
			adapter->dropped_pkts++;
			return EBADMSG;
		}

		ri->iri_frags[i].irf_flid = 0;
		ri->iri_frags[i].irf_idx = cidx;
		ri->iri_frags[i].irf_len = len;
		/* Zero out the receive descriptors status. */
		rxd->wb.upper.status_error &= htole32(~0xFF);

		if (++cidx == scctx->isc_nrxd[0])
			cidx = 0;
		i++;
	} while (!eop);

	/* XXX add a faster way to look this up */
	if (adapter->hw.mac.type >= e1000_82543)
		em_receive_checksum(staterr, ri);

	if (staterr & E1000_RXD_STAT_VP) {
		vtag = le16toh(rxd->wb.upper.vlan);
	}

	ri->iri_vtag = vtag;
	if (vtag)
		ri->iri_flags |= M_VLANTAG;

	ri->iri_flowid = le32toh(rxd->wb.lower.hi_dword.rss);
	ri->iri_rsstype = em_determine_rsstype(pkt_info);

	ri->iri_nfrags = i;
	return (0);
}

/*********************************************************************
 *
 *  Verify that the hardware indicated that the checksum is valid.
 *  Inform the stack about the status of checksum so that stack
 *  doesn't spend time verifying the checksum.
 *
 *********************************************************************/
static void
lem_receive_checksum(int status, int errors, if_rxd_info_t ri)
{
	/* Did it pass? */
	if (status & E1000_RXD_STAT_IPCS && !(errors & E1000_RXD_ERR_IPE))
		ri->iri_csum_flags = (CSUM_IP_CHECKED|CSUM_IP_VALID);

	if (status & E1000_RXD_STAT_TCPCS) {
		/* Did it pass? */
		if (!(errors & E1000_RXD_ERR_TCPE)) {
			ri->iri_csum_flags |=
			(CSUM_DATA_VALID | CSUM_PSEUDO_HDR);
			ri->iri_csum_data = htons(0xffff);
		}
	}
}

/********************************************************************
 *
 *  Parse the packet type to determine the appropriate hash
 *
 ******************************************************************/
static int
em_determine_rsstype(u32 pkt_info)
{
	switch (pkt_info & E1000_RXDADV_RSSTYPE_MASK) {
	case E1000_RXDADV_RSSTYPE_IPV4_TCP:
		return M_HASHTYPE_RSS_TCP_IPV4;
	case E1000_RXDADV_RSSTYPE_IPV4:
		return M_HASHTYPE_RSS_IPV4;
	case E1000_RXDADV_RSSTYPE_IPV6_TCP:
		return M_HASHTYPE_RSS_TCP_IPV6;
	case E1000_RXDADV_RSSTYPE_IPV6_EX: 
		return M_HASHTYPE_RSS_IPV6_EX;
	case E1000_RXDADV_RSSTYPE_IPV6:
		return M_HASHTYPE_RSS_IPV6;
	case E1000_RXDADV_RSSTYPE_IPV6_TCP_EX:
		return M_HASHTYPE_RSS_TCP_IPV6_EX;
	default:
		return M_HASHTYPE_OPAQUE;
	}
}

static void
em_receive_checksum(uint32_t status, if_rxd_info_t ri)
{
	ri->iri_csum_flags = 0;

	/* Ignore Checksum bit is set */
	if (status & E1000_RXD_STAT_IXSM)
		return;

	/* If the IP checksum exists and there is no IP Checksum error */
	if ((status & (E1000_RXD_STAT_IPCS | E1000_RXDEXT_STATERR_IPE)) ==
	    E1000_RXD_STAT_IPCS) {
		ri->iri_csum_flags = (CSUM_IP_CHECKED | CSUM_IP_VALID);
	}

	/* TCP or UDP checksum */
	if ((status & (E1000_RXD_STAT_TCPCS | E1000_RXDEXT_STATERR_TCPE)) ==
	    E1000_RXD_STAT_TCPCS) {
		ri->iri_csum_flags |= (CSUM_DATA_VALID | CSUM_PSEUDO_HDR);
		ri->iri_csum_data = htons(0xffff);
	}
	if (status & E1000_RXD_STAT_UDPCS) {
		ri->iri_csum_flags |= (CSUM_DATA_VALID | CSUM_PSEUDO_HDR);
		ri->iri_csum_data = htons(0xffff);
	}
}
