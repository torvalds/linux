/******************************************************************************

  Copyright (c) 2013-2018, Intel Corporation
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

/*
**	IXL driver TX/RX Routines:
**	    This was seperated to allow usage by
** 	    both the PF and VF drivers.
*/

#ifndef IXL_STANDALONE_BUILD
#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_rss.h"
#endif

#include "ixl.h"

#ifdef RSS
#include <net/rss_config.h>
#endif

/* Local Prototypes */
static void	ixl_rx_checksum(if_rxd_info_t ri, u32 status, u32 error, u8 ptype);

static int	ixl_isc_txd_encap(void *arg, if_pkt_info_t pi);
static void	ixl_isc_txd_flush(void *arg, uint16_t txqid, qidx_t pidx);
static int	ixl_isc_txd_credits_update_hwb(void *arg, uint16_t txqid, bool clear);
static int	ixl_isc_txd_credits_update_dwb(void *arg, uint16_t txqid, bool clear);

static void	ixl_isc_rxd_refill(void *arg, if_rxd_update_t iru);
static void	ixl_isc_rxd_flush(void *arg, uint16_t rxqid, uint8_t flid __unused,
				  qidx_t pidx);
static int	ixl_isc_rxd_available(void *arg, uint16_t rxqid, qidx_t idx,
				      qidx_t budget);
static int	ixl_isc_rxd_pkt_get(void *arg, if_rxd_info_t ri);

struct if_txrx ixl_txrx_hwb = {
	ixl_isc_txd_encap,
	ixl_isc_txd_flush,
	ixl_isc_txd_credits_update_hwb,
	ixl_isc_rxd_available,
	ixl_isc_rxd_pkt_get,
	ixl_isc_rxd_refill,
	ixl_isc_rxd_flush,
	NULL
};

struct if_txrx ixl_txrx_dwb = {
	ixl_isc_txd_encap,
	ixl_isc_txd_flush,
	ixl_isc_txd_credits_update_dwb,
	ixl_isc_rxd_available,
	ixl_isc_rxd_pkt_get,
	ixl_isc_rxd_refill,
	ixl_isc_rxd_flush,
	NULL
};

/*
 * @key key is saved into this parameter
 */
void
ixl_get_default_rss_key(u32 *key)
{
	MPASS(key != NULL);

	u32 rss_seed[IXL_RSS_KEY_SIZE_REG] = {0x41b01687,
	    0x183cfd8c, 0xce880440, 0x580cbc3c,
	    0x35897377, 0x328b25e1, 0x4fa98922,
	    0xb7d90c14, 0xd5bad70d, 0xcd15a2c1,
	    0x0, 0x0, 0x0};

	bcopy(rss_seed, key, IXL_RSS_KEY_SIZE);
}

/**
 * i40e_vc_stat_str - convert virtchnl status err code to a string
 * @hw: pointer to the HW structure
 * @stat_err: the status error code to convert
 **/
const char *
i40e_vc_stat_str(struct i40e_hw *hw, enum virtchnl_status_code stat_err)
{
	switch (stat_err) {
	case VIRTCHNL_STATUS_SUCCESS:
		return "OK";
	case VIRTCHNL_ERR_PARAM:
		return "VIRTCHNL_ERR_PARAM";
	case VIRTCHNL_STATUS_ERR_OPCODE_MISMATCH:
		return "VIRTCHNL_STATUS_ERR_OPCODE_MISMATCH";
	case VIRTCHNL_STATUS_ERR_CQP_COMPL_ERROR:
		return "VIRTCHNL_STATUS_ERR_CQP_COMPL_ERROR";
	case VIRTCHNL_STATUS_ERR_INVALID_VF_ID:
		return "VIRTCHNL_STATUS_ERR_INVALID_VF_ID";
	case VIRTCHNL_STATUS_NOT_SUPPORTED:
		return "VIRTCHNL_STATUS_NOT_SUPPORTED";
	}

	snprintf(hw->err_str, sizeof(hw->err_str), "%d", stat_err);
	return hw->err_str;
}

void
ixl_debug_core(device_t dev, u32 enabled_mask, u32 mask, char *fmt, ...)
{
	va_list args;

	if (!(mask & enabled_mask))
		return;

	/* Re-implement device_printf() */
	device_print_prettyname(dev);
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
}

static bool
ixl_is_tx_desc_done(struct tx_ring *txr, int idx)
{
	return (((txr->tx_base[idx].cmd_type_offset_bsz >> I40E_TXD_QW1_DTYPE_SHIFT)
	    & I40E_TXD_QW1_DTYPE_MASK) == I40E_TX_DESC_DTYPE_DESC_DONE);
}

static int
ixl_tso_detect_sparse(bus_dma_segment_t *segs, int nsegs, if_pkt_info_t pi)
{
	int	count, curseg, i, hlen, segsz, seglen, tsolen;

	if (nsegs <= IXL_MAX_TX_SEGS-2)
		return (0);
	segsz = pi->ipi_tso_segsz;
	curseg = count = 0;

	hlen = pi->ipi_ehdrlen + pi->ipi_ip_hlen + pi->ipi_tcp_hlen;
	tsolen = pi->ipi_len - hlen;

	i = 0;
	curseg = segs[0].ds_len;
	while (hlen > 0) {
		count++;
		if (count > IXL_MAX_TX_SEGS - 2)
			return (1);
		if (curseg == 0) {
			i++;
			if (__predict_false(i == nsegs))
				return (1);

			curseg = segs[i].ds_len;
		}
		seglen = min(curseg, hlen);
		curseg -= seglen;
		hlen -= seglen;
		// printf("H:seglen = %d, count=%d\n", seglen, count);
	}
	while (tsolen > 0) {
		segsz = pi->ipi_tso_segsz;
		while (segsz > 0 && tsolen != 0) {
			count++;
			if (count > IXL_MAX_TX_SEGS - 2) {
				// printf("bad: count = %d\n", count);
				return (1);
			}
			if (curseg == 0) {
				i++;
				if (__predict_false(i == nsegs)) {
					// printf("bad: tsolen = %d", tsolen);
					return (1);
				}
				curseg = segs[i].ds_len;
			}
			seglen = min(curseg, segsz);
			segsz -= seglen;
			curseg -= seglen;
			tsolen -= seglen;
			// printf("D:seglen = %d, count=%d\n", seglen, count);
		}
		count = 0;
	}

 	return (0);
}

/*********************************************************************
 *
 *  Setup descriptor for hw offloads 
 *
 **********************************************************************/

static void
ixl_tx_setup_offload(struct ixl_tx_queue *que,
    if_pkt_info_t pi, u32 *cmd, u32 *off)
{
	switch (pi->ipi_etype) {
#ifdef INET
		case ETHERTYPE_IP:
			if (pi->ipi_csum_flags & IXL_CSUM_IPV4)
				*cmd |= I40E_TX_DESC_CMD_IIPT_IPV4_CSUM;
			else
				*cmd |= I40E_TX_DESC_CMD_IIPT_IPV4;
			break;
#endif
#ifdef INET6
		case ETHERTYPE_IPV6:
			*cmd |= I40E_TX_DESC_CMD_IIPT_IPV6;
			break;
#endif
		default:
			break;
	}

	*off |= (pi->ipi_ehdrlen >> 1) << I40E_TX_DESC_LENGTH_MACLEN_SHIFT;
	*off |= (pi->ipi_ip_hlen >> 2) << I40E_TX_DESC_LENGTH_IPLEN_SHIFT;

	switch (pi->ipi_ipproto) {
		case IPPROTO_TCP:
			if (pi->ipi_csum_flags & IXL_CSUM_TCP) {
				*cmd |= I40E_TX_DESC_CMD_L4T_EOFT_TCP;
				*off |= (pi->ipi_tcp_hlen >> 2) <<
				    I40E_TX_DESC_LENGTH_L4_FC_LEN_SHIFT;
				/* Check for NO_HEAD MDD event */
				MPASS(pi->ipi_tcp_hlen != 0);
			}
			break;
		case IPPROTO_UDP:
			if (pi->ipi_csum_flags & IXL_CSUM_UDP) {
				*cmd |= I40E_TX_DESC_CMD_L4T_EOFT_UDP;
				*off |= (sizeof(struct udphdr) >> 2) <<
				    I40E_TX_DESC_LENGTH_L4_FC_LEN_SHIFT;
			}
			break;
		case IPPROTO_SCTP:
			if (pi->ipi_csum_flags & IXL_CSUM_SCTP) {
				*cmd |= I40E_TX_DESC_CMD_L4T_EOFT_SCTP;
				*off |= (sizeof(struct sctphdr) >> 2) <<
				    I40E_TX_DESC_LENGTH_L4_FC_LEN_SHIFT;
			}
			/* Fall Thru */
		default:
			break;
	}
}

/**********************************************************************
 *
 *  Setup context for hardware segmentation offload (TSO)
 *
 **********************************************************************/
static int
ixl_tso_setup(struct tx_ring *txr, if_pkt_info_t pi)
{
	if_softc_ctx_t			scctx;
	struct i40e_tx_context_desc	*TXD;
	u32				cmd, mss, type, tsolen;
	int				idx, total_hdr_len;
	u64				type_cmd_tso_mss;

	idx = pi->ipi_pidx;
	TXD = (struct i40e_tx_context_desc *) &txr->tx_base[idx];
	total_hdr_len = pi->ipi_ehdrlen + pi->ipi_ip_hlen + pi->ipi_tcp_hlen;
	tsolen = pi->ipi_len - total_hdr_len;
	scctx = txr->que->vsi->shared;

	type = I40E_TX_DESC_DTYPE_CONTEXT;
	cmd = I40E_TX_CTX_DESC_TSO;
	/*
	 * TSO MSS must not be less than 64; this prevents a
	 * BAD_LSO_MSS MDD event when the MSS is too small.
	 */
	if (pi->ipi_tso_segsz < IXL_MIN_TSO_MSS) {
		txr->mss_too_small++;
		pi->ipi_tso_segsz = IXL_MIN_TSO_MSS;
	}
	mss = pi->ipi_tso_segsz;

	/* Check for BAD_LS0_MSS MDD event (mss too large) */
	MPASS(mss <= IXL_MAX_TSO_MSS);
	/* Check for NO_HEAD MDD event (header lengths are 0) */
	MPASS(pi->ipi_ehdrlen != 0);
	MPASS(pi->ipi_ip_hlen != 0);
	/* Partial check for BAD_LSO_LEN MDD event */
	MPASS(tsolen != 0);
	/* Partial check for WRONG_SIZE MDD event (during TSO) */
	MPASS(total_hdr_len + mss <= IXL_MAX_FRAME);

	type_cmd_tso_mss = ((u64)type << I40E_TXD_CTX_QW1_DTYPE_SHIFT) |
	    ((u64)cmd << I40E_TXD_CTX_QW1_CMD_SHIFT) |
	    ((u64)tsolen << I40E_TXD_CTX_QW1_TSO_LEN_SHIFT) |
	    ((u64)mss << I40E_TXD_CTX_QW1_MSS_SHIFT);
	TXD->type_cmd_tso_mss = htole64(type_cmd_tso_mss);

	TXD->tunneling_params = htole32(0);
	txr->que->tso++;

	return ((idx + 1) & (scctx->isc_ntxd[0]-1));
}

/*********************************************************************
  *
 *  This routine maps the mbufs to tx descriptors, allowing the
 *  TX engine to transmit the packets. 
 *  	- return 0 on success, positive on failure
  *
  **********************************************************************/
#define IXL_TXD_CMD (I40E_TX_DESC_CMD_EOP | I40E_TX_DESC_CMD_RS)

static int
ixl_isc_txd_encap(void *arg, if_pkt_info_t pi)
{
	struct ixl_vsi		*vsi = arg;
	if_softc_ctx_t		scctx = vsi->shared;
	struct ixl_tx_queue	*que = &vsi->tx_queues[pi->ipi_qsidx];
 	struct tx_ring		*txr = &que->txr;
	int			nsegs = pi->ipi_nsegs;
	bus_dma_segment_t *segs = pi->ipi_segs;
	struct i40e_tx_desc	*txd = NULL;
	int             	i, j, mask, pidx_last;
	u32			cmd, off, tx_intr;

	cmd = off = 0;
	i = pi->ipi_pidx;

	tx_intr = (pi->ipi_flags & IPI_TX_INTR);

	/* Set up the TSO/CSUM offload */
	if (pi->ipi_csum_flags & CSUM_OFFLOAD) {
		/* Set up the TSO context descriptor if required */
		if (pi->ipi_csum_flags & CSUM_TSO) {
			/* Prevent MAX_BUFF MDD event (for TSO) */
			if (ixl_tso_detect_sparse(segs, nsegs, pi))
				return (EFBIG);
			i = ixl_tso_setup(txr, pi);
		}
		ixl_tx_setup_offload(que, pi, &cmd, &off);
	}
	if (pi->ipi_mflags & M_VLANTAG)
		cmd |= I40E_TX_DESC_CMD_IL2TAG1;

	cmd |= I40E_TX_DESC_CMD_ICRC;
	mask = scctx->isc_ntxd[0] - 1;
	/* Check for WRONG_SIZE MDD event */
	MPASS(pi->ipi_len >= IXL_MIN_FRAME);
#ifdef INVARIANTS
	if (!(pi->ipi_csum_flags & CSUM_TSO))
		MPASS(pi->ipi_len <= IXL_MAX_FRAME);
#endif
	for (j = 0; j < nsegs; j++) {
		bus_size_t seglen;

		txd = &txr->tx_base[i];
		seglen = segs[j].ds_len;

		/* Check for ZERO_BSIZE MDD event */
		MPASS(seglen != 0);

		txd->buffer_addr = htole64(segs[j].ds_addr);
		txd->cmd_type_offset_bsz =
		    htole64(I40E_TX_DESC_DTYPE_DATA
		    | ((u64)cmd  << I40E_TXD_QW1_CMD_SHIFT)
		    | ((u64)off << I40E_TXD_QW1_OFFSET_SHIFT)
		    | ((u64)seglen  << I40E_TXD_QW1_TX_BUF_SZ_SHIFT)
	            | ((u64)htole16(pi->ipi_vtag) << I40E_TXD_QW1_L2TAG1_SHIFT));

		txr->tx_bytes += seglen;
		pidx_last = i;
		i = (i+1) & mask;
	}
	/* Set the last descriptor for report */
	txd->cmd_type_offset_bsz |=
	    htole64(((u64)IXL_TXD_CMD << I40E_TXD_QW1_CMD_SHIFT));
	/* Add to report status array (if using TX interrupts) */
	if (!vsi->enable_head_writeback && tx_intr) {
		txr->tx_rsq[txr->tx_rs_pidx] = pidx_last;
		txr->tx_rs_pidx = (txr->tx_rs_pidx+1) & mask;
		MPASS(txr->tx_rs_pidx != txr->tx_rs_cidx);
 	}
	pi->ipi_new_pidx = i;

	++txr->tx_packets;
	return (0);
}

static void
ixl_isc_txd_flush(void *arg, uint16_t txqid, qidx_t pidx)
{
	struct ixl_vsi *vsi = arg;
	struct tx_ring *txr = &vsi->tx_queues[txqid].txr;

 	/*
	 * Advance the Transmit Descriptor Tail (Tdt), this tells the
	 * hardware that this frame is available to transmit.
 	 */
	/* Check for ENDLESS_TX MDD event */
	MPASS(pidx < vsi->shared->isc_ntxd[0]);
	wr32(vsi->hw, txr->tail, pidx);
}


/*********************************************************************
 *
 *  (Re)Initialize a queue transmit ring by clearing its memory.
 *
 **********************************************************************/
void
ixl_init_tx_ring(struct ixl_vsi *vsi, struct ixl_tx_queue *que)
{
	struct tx_ring *txr = &que->txr;

	/* Clear the old ring contents */
	bzero((void *)txr->tx_base,
	      (sizeof(struct i40e_tx_desc)) *
	      (vsi->shared->isc_ntxd[0] + (vsi->enable_head_writeback ? 1 : 0)));

	wr32(vsi->hw, txr->tail, 0);
}

/*
 * ixl_get_tx_head - Retrieve the value from the
 *    location the HW records its HEAD index
 */
static inline u32
ixl_get_tx_head(struct ixl_tx_queue *que)
{
	if_softc_ctx_t          scctx = que->vsi->shared;
	struct tx_ring  *txr = &que->txr;
	void *head = &txr->tx_base[scctx->isc_ntxd[0]];

	return LE32_TO_CPU(*(volatile __le32 *)head);
}

static int
ixl_isc_txd_credits_update_hwb(void *arg, uint16_t qid, bool clear)
{
	struct ixl_vsi          *vsi = arg;
	if_softc_ctx_t          scctx = vsi->shared;
	struct ixl_tx_queue     *que = &vsi->tx_queues[qid];
	struct tx_ring		*txr = &que->txr;
	int			 head, credits;

	/* Get the Head WB value */
	head = ixl_get_tx_head(que);

	credits = head - txr->tx_cidx_processed;
	if (credits < 0)
		credits += scctx->isc_ntxd[0];
	if (clear)
		txr->tx_cidx_processed = head;

	return (credits);
}

static int
ixl_isc_txd_credits_update_dwb(void *arg, uint16_t txqid, bool clear)
{
	struct ixl_vsi *vsi = arg;
	struct ixl_tx_queue *tx_que = &vsi->tx_queues[txqid];
	if_softc_ctx_t scctx = vsi->shared;
	struct tx_ring *txr = &tx_que->txr;

	qidx_t processed = 0;
	qidx_t cur, prev, ntxd, rs_cidx;
	int32_t delta;
	bool is_done;

	rs_cidx = txr->tx_rs_cidx;
#if 0
	device_printf(iflib_get_dev(vsi->ctx), "%s: (q%d) rs_cidx %d, txr->tx_rs_pidx %d\n", __func__,
	    txr->me, rs_cidx, txr->tx_rs_pidx);
#endif
	if (rs_cidx == txr->tx_rs_pidx)
		return (0);
	cur = txr->tx_rsq[rs_cidx];
	MPASS(cur != QIDX_INVALID);
	is_done = ixl_is_tx_desc_done(txr, cur);

	if (!is_done)
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
#if 0
		device_printf(iflib_get_dev(vsi->ctx),
			      "%s: (q%d) cidx_processed=%u cur=%u clear=%d delta=%d\n",
			      __func__, txr->me, prev, cur, clear, delta);
#endif
		processed += delta;
		prev = cur;
		rs_cidx = (rs_cidx + 1) & (ntxd-1);
		if (rs_cidx == txr->tx_rs_pidx)
			break;
		cur = txr->tx_rsq[rs_cidx];
		MPASS(cur != QIDX_INVALID);
		is_done = ixl_is_tx_desc_done(txr, cur);
	} while (is_done);

	txr->tx_rs_cidx = rs_cidx;
	txr->tx_cidx_processed = prev;

#if 0
	device_printf(iflib_get_dev(vsi->ctx), "%s: (q%d) processed %d\n", __func__, txr->me, processed);
#endif
	return (processed);
}

static void
ixl_isc_rxd_refill(void *arg, if_rxd_update_t iru)
{
	struct ixl_vsi *vsi = arg;
	if_softc_ctx_t scctx = vsi->shared;
	struct rx_ring *rxr = &((vsi->rx_queues[iru->iru_qsidx]).rxr);
	uint64_t *paddrs;
	uint32_t next_pidx, pidx;
	uint16_t count;
	int i;

	paddrs = iru->iru_paddrs;
	pidx = iru->iru_pidx;
	count = iru->iru_count;

	for (i = 0, next_pidx = pidx; i < count; i++) {
		rxr->rx_base[next_pidx].read.pkt_addr = htole64(paddrs[i]);
		if (++next_pidx == scctx->isc_nrxd[0])
			next_pidx = 0;
 	}
}

static void
ixl_isc_rxd_flush(void * arg, uint16_t rxqid, uint8_t flid __unused, qidx_t pidx)
{
	struct ixl_vsi		*vsi = arg;
	struct rx_ring		*rxr = &vsi->rx_queues[rxqid].rxr;

	wr32(vsi->hw, rxr->tail, pidx);
}

static int
ixl_isc_rxd_available(void *arg, uint16_t rxqid, qidx_t idx, qidx_t budget)
{
	struct ixl_vsi *vsi = arg;
	struct rx_ring *rxr = &vsi->rx_queues[rxqid].rxr;
	union i40e_rx_desc *rxd;
	u64 qword;
	uint32_t status;
	int cnt, i, nrxd;

	nrxd = vsi->shared->isc_nrxd[0];

	for (cnt = 0, i = idx; cnt < nrxd - 1 && cnt <= budget;) {
		rxd = &rxr->rx_base[i];
		qword = le64toh(rxd->wb.qword1.status_error_len);
		status = (qword & I40E_RXD_QW1_STATUS_MASK)
			>> I40E_RXD_QW1_STATUS_SHIFT;

		if ((status & (1 << I40E_RX_DESC_STATUS_DD_SHIFT)) == 0)
			break;
		if (++i == nrxd)
			i = 0;
		if (status & (1 << I40E_RX_DESC_STATUS_EOF_SHIFT))
			cnt++;
	}

	return (cnt);
}

/*
** i40e_ptype_to_hash: parse the packet type
** to determine the appropriate hash.
*/
static inline int
ixl_ptype_to_hash(u8 ptype)
{
        struct i40e_rx_ptype_decoded	decoded;

	decoded = decode_rx_desc_ptype(ptype);

	if (!decoded.known)
		return M_HASHTYPE_OPAQUE;

	if (decoded.outer_ip == I40E_RX_PTYPE_OUTER_L2)
		return M_HASHTYPE_OPAQUE;

	/* Note: anything that gets to this point is IP */
        if (decoded.outer_ip_ver == I40E_RX_PTYPE_OUTER_IPV6) {
		switch (decoded.inner_prot) {
		case I40E_RX_PTYPE_INNER_PROT_TCP:
			return M_HASHTYPE_RSS_TCP_IPV6;
		case I40E_RX_PTYPE_INNER_PROT_UDP:
			return M_HASHTYPE_RSS_UDP_IPV6;
		default:
			return M_HASHTYPE_RSS_IPV6;
		}
	}
        if (decoded.outer_ip_ver == I40E_RX_PTYPE_OUTER_IPV4) {
		switch (decoded.inner_prot) {
		case I40E_RX_PTYPE_INNER_PROT_TCP:
			return M_HASHTYPE_RSS_TCP_IPV4;
		case I40E_RX_PTYPE_INNER_PROT_UDP:
			return M_HASHTYPE_RSS_UDP_IPV4;
		default:
			return M_HASHTYPE_RSS_IPV4;
		}
	}
	/* We should never get here!! */
	return M_HASHTYPE_OPAQUE;
}

/*********************************************************************
 *
 *  This routine executes in ithread context. It sends data which has been
 *  dma'ed into host memory to upper layer.
 *
 *  Returns 0 upon success, errno on failure
 *
 *********************************************************************/
static int
ixl_isc_rxd_pkt_get(void *arg, if_rxd_info_t ri)
{
	struct ixl_vsi		*vsi = arg;
	struct ixl_rx_queue	*que = &vsi->rx_queues[ri->iri_qsidx];
	struct rx_ring		*rxr = &que->rxr;
	union i40e_rx_desc	*cur;
	u32		status, error;
	u16		plen, vtag;
	u64		qword;
	u8		ptype;
	bool		eop;
	int i, cidx;

	cidx = ri->iri_cidx;
	i = 0;
	do {
		/* 5 descriptor receive limit */
		MPASS(i < IXL_MAX_RX_SEGS);

		cur = &rxr->rx_base[cidx];
		qword = le64toh(cur->wb.qword1.status_error_len);
		status = (qword & I40E_RXD_QW1_STATUS_MASK)
		    >> I40E_RXD_QW1_STATUS_SHIFT;
		error = (qword & I40E_RXD_QW1_ERROR_MASK)
		    >> I40E_RXD_QW1_ERROR_SHIFT;
		plen = (qword & I40E_RXD_QW1_LENGTH_PBUF_MASK)
		    >> I40E_RXD_QW1_LENGTH_PBUF_SHIFT;
		ptype = (qword & I40E_RXD_QW1_PTYPE_MASK)
		    >> I40E_RXD_QW1_PTYPE_SHIFT;

		/* we should never be called without a valid descriptor */
		MPASS((status & (1 << I40E_RX_DESC_STATUS_DD_SHIFT)) != 0);

		ri->iri_len += plen;
		rxr->rx_bytes += plen;

		cur->wb.qword1.status_error_len = 0;
		eop = (status & (1 << I40E_RX_DESC_STATUS_EOF_SHIFT));
		if (status & (1 << I40E_RX_DESC_STATUS_L2TAG1P_SHIFT))
			vtag = le16toh(cur->wb.qword0.lo_dword.l2tag1);
		else
			vtag = 0;

		/*
		** Make sure bad packets are discarded,
		** note that only EOP descriptor has valid
		** error results.
		*/
		if (eop && (error & (1 << I40E_RX_DESC_ERROR_RXE_SHIFT))) {
			rxr->desc_errs++;
			return (EBADMSG);
		}
		ri->iri_frags[i].irf_flid = 0;
		ri->iri_frags[i].irf_idx = cidx;
		ri->iri_frags[i].irf_len = plen;
		if (++cidx == vsi->shared->isc_nrxd[0])
			cidx = 0;
		i++;
	} while (!eop);

	/* capture data for dynamic ITR adjustment */
	rxr->packets++;
	rxr->rx_packets++;

	if ((if_getcapenable(vsi->ifp) & IFCAP_RXCSUM) != 0)
		ixl_rx_checksum(ri, status, error, ptype);
	ri->iri_flowid = le32toh(cur->wb.qword0.hi_dword.rss);
	ri->iri_rsstype = ixl_ptype_to_hash(ptype);
	ri->iri_vtag = vtag;
	ri->iri_nfrags = i;
	if (vtag)
		ri->iri_flags |= M_VLANTAG;
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
ixl_rx_checksum(if_rxd_info_t ri, u32 status, u32 error, u8 ptype)
{
	struct i40e_rx_ptype_decoded decoded;

	ri->iri_csum_flags = 0;

	/* No L3 or L4 checksum was calculated */
	if (!(status & (1 << I40E_RX_DESC_STATUS_L3L4P_SHIFT)))
		return;

	decoded = decode_rx_desc_ptype(ptype);

	/* IPv6 with extension headers likely have bad csum */
	if (decoded.outer_ip == I40E_RX_PTYPE_OUTER_IP &&
	    decoded.outer_ip_ver == I40E_RX_PTYPE_OUTER_IPV6) {
		if (status &
		    (1 << I40E_RX_DESC_STATUS_IPV6EXADD_SHIFT)) {
			ri->iri_csum_flags = 0;
			return;
		}
	}

	ri->iri_csum_flags |= CSUM_L3_CALC;

	/* IPv4 checksum error */
	if (error & (1 << I40E_RX_DESC_ERROR_IPE_SHIFT))
		return;

	ri->iri_csum_flags |= CSUM_L3_VALID;
	ri->iri_csum_flags |= CSUM_L4_CALC;

	/* L4 checksum error */
	if (error & (1 << I40E_RX_DESC_ERROR_L4E_SHIFT))
		return;

	ri->iri_csum_flags |= CSUM_L4_VALID;
	ri->iri_csum_data |= htons(0xffff);
}

/* Set Report Status queue fields to 0 */
void
ixl_init_tx_rsqs(struct ixl_vsi *vsi)
{
	if_softc_ctx_t scctx = vsi->shared;
	struct ixl_tx_queue *tx_que;
	int i, j;

	for (i = 0, tx_que = vsi->tx_queues; i < vsi->num_tx_queues; i++, tx_que++) {
		struct tx_ring *txr = &tx_que->txr;

		txr->tx_rs_cidx = txr->tx_rs_pidx;

		/* Initialize the last processed descriptor to be the end of
		 * the ring, rather than the start, so that we avoid an
		 * off-by-one error when calculating how many descriptors are
		 * done in the credits_update function.
		 */
		txr->tx_cidx_processed = scctx->isc_ntxd[0] - 1;

		for (j = 0; j < scctx->isc_ntxd[0]; j++)
			txr->tx_rsq[j] = QIDX_INVALID;
	}
}

void
ixl_init_tx_cidx(struct ixl_vsi *vsi)
{
	if_softc_ctx_t scctx = vsi->shared;
	struct ixl_tx_queue *tx_que;
	int i;
	
	for (i = 0, tx_que = vsi->tx_queues; i < vsi->num_tx_queues; i++, tx_que++) {
		struct tx_ring *txr = &tx_que->txr;

		txr->tx_cidx_processed = scctx->isc_ntxd[0] - 1;
	}
}

/*
 * Input: bitmap of enum virtchnl_link_speed
 */
u64
ixl_max_vc_speed_to_value(u8 link_speeds)
{
	if (link_speeds & VIRTCHNL_LINK_SPEED_40GB)
		return IF_Gbps(40);
	if (link_speeds & VIRTCHNL_LINK_SPEED_25GB)
		return IF_Gbps(25);
	if (link_speeds & VIRTCHNL_LINK_SPEED_20GB)
		return IF_Gbps(20);
	if (link_speeds & VIRTCHNL_LINK_SPEED_10GB)
		return IF_Gbps(10);
	if (link_speeds & VIRTCHNL_LINK_SPEED_1GB)
		return IF_Gbps(1);
	if (link_speeds & VIRTCHNL_LINK_SPEED_100MB)
		return IF_Mbps(100);
	else
		/* Minimum supported link speed */
		return IF_Mbps(100);
}

void
ixl_add_vsi_sysctls(device_t dev, struct ixl_vsi *vsi,
    struct sysctl_ctx_list *ctx, const char *sysctl_name)
{
	struct sysctl_oid *tree;
	struct sysctl_oid_list *child;
	struct sysctl_oid_list *vsi_list;

	tree = device_get_sysctl_tree(dev);
	child = SYSCTL_CHILDREN(tree);
	vsi->vsi_node = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, sysctl_name,
				   CTLFLAG_RD, NULL, "VSI Number");
	vsi_list = SYSCTL_CHILDREN(vsi->vsi_node);

	ixl_add_sysctls_eth_stats(ctx, vsi_list, &vsi->eth_stats);
}

void
ixl_add_sysctls_eth_stats(struct sysctl_ctx_list *ctx,
	struct sysctl_oid_list *child,
	struct i40e_eth_stats *eth_stats)
{
	struct ixl_sysctl_info ctls[] =
	{
		{&eth_stats->rx_bytes, "good_octets_rcvd", "Good Octets Received"},
		{&eth_stats->rx_unicast, "ucast_pkts_rcvd",
			"Unicast Packets Received"},
		{&eth_stats->rx_multicast, "mcast_pkts_rcvd",
			"Multicast Packets Received"},
		{&eth_stats->rx_broadcast, "bcast_pkts_rcvd",
			"Broadcast Packets Received"},
		{&eth_stats->rx_discards, "rx_discards", "Discarded RX packets"},
		{&eth_stats->tx_bytes, "good_octets_txd", "Good Octets Transmitted"},
		{&eth_stats->tx_unicast, "ucast_pkts_txd", "Unicast Packets Transmitted"},
		{&eth_stats->tx_multicast, "mcast_pkts_txd",
			"Multicast Packets Transmitted"},
		{&eth_stats->tx_broadcast, "bcast_pkts_txd",
			"Broadcast Packets Transmitted"},
		// end
		{0,0,0}
	};

	struct ixl_sysctl_info *entry = ctls;
	while (entry->stat != 0)
	{
		SYSCTL_ADD_UQUAD(ctx, child, OID_AUTO, entry->name,
				CTLFLAG_RD, entry->stat,
				entry->description);
		entry++;
	}
}

void
ixl_add_queues_sysctls(device_t dev, struct ixl_vsi *vsi)
{
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(dev);
	struct sysctl_oid_list *vsi_list, *queue_list;
	struct sysctl_oid *queue_node;
	char queue_namebuf[32];

	struct ixl_rx_queue *rx_que;
	struct ixl_tx_queue *tx_que;
	struct tx_ring *txr;
	struct rx_ring *rxr;

	vsi_list = SYSCTL_CHILDREN(vsi->vsi_node);

	/* Queue statistics */
	for (int q = 0; q < vsi->num_rx_queues; q++) {
		bzero(queue_namebuf, sizeof(queue_namebuf));
		snprintf(queue_namebuf, QUEUE_NAME_LEN, "rxq%02d", q);
		queue_node = SYSCTL_ADD_NODE(ctx, vsi_list,
		    OID_AUTO, queue_namebuf, CTLFLAG_RD, NULL, "RX Queue #");
		queue_list = SYSCTL_CHILDREN(queue_node);

		rx_que = &(vsi->rx_queues[q]);
		rxr = &(rx_que->rxr);

		SYSCTL_ADD_UQUAD(ctx, queue_list, OID_AUTO, "irqs",
				CTLFLAG_RD, &(rx_que->irqs),
				"irqs on this queue (both Tx and Rx)");

		SYSCTL_ADD_UQUAD(ctx, queue_list, OID_AUTO, "packets",
				CTLFLAG_RD, &(rxr->rx_packets),
				"Queue Packets Received");
		SYSCTL_ADD_UQUAD(ctx, queue_list, OID_AUTO, "bytes",
				CTLFLAG_RD, &(rxr->rx_bytes),
				"Queue Bytes Received");
		SYSCTL_ADD_UQUAD(ctx, queue_list, OID_AUTO, "desc_err",
				CTLFLAG_RD, &(rxr->desc_errs),
				"Queue Rx Descriptor Errors");
		SYSCTL_ADD_UINT(ctx, queue_list, OID_AUTO, "itr",
				CTLFLAG_RD, &(rxr->itr), 0,
				"Queue Rx ITR Interval");
	}
	for (int q = 0; q < vsi->num_tx_queues; q++) {
		bzero(queue_namebuf, sizeof(queue_namebuf));
		snprintf(queue_namebuf, QUEUE_NAME_LEN, "txq%02d", q);
		queue_node = SYSCTL_ADD_NODE(ctx, vsi_list,
		    OID_AUTO, queue_namebuf, CTLFLAG_RD, NULL, "TX Queue #");
		queue_list = SYSCTL_CHILDREN(queue_node);

		tx_que = &(vsi->tx_queues[q]);
		txr = &(tx_que->txr);

		SYSCTL_ADD_UQUAD(ctx, queue_list, OID_AUTO, "tso",
				CTLFLAG_RD, &(tx_que->tso),
				"TSO");
		SYSCTL_ADD_UQUAD(ctx, queue_list, OID_AUTO, "mss_too_small",
				CTLFLAG_RD, &(txr->mss_too_small),
				"TSO sends with an MSS less than 64");
		SYSCTL_ADD_UQUAD(ctx, queue_list, OID_AUTO, "packets",
				CTLFLAG_RD, &(txr->tx_packets),
				"Queue Packets Transmitted");
		SYSCTL_ADD_UQUAD(ctx, queue_list, OID_AUTO, "bytes",
				CTLFLAG_RD, &(txr->tx_bytes),
				"Queue Bytes Transmitted");
		SYSCTL_ADD_UINT(ctx, queue_list, OID_AUTO, "itr",
				CTLFLAG_RD, &(txr->itr), 0,
				"Queue Tx ITR Interval");
	}
}
