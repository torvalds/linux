/*-
 * Copyright (c) 2015-2018 Mellanox Technologies. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS `AS IS' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include "en.h"
#include <machine/atomic.h>

static inline bool
mlx5e_do_send_cqe(struct mlx5e_sq *sq)
{
	sq->cev_counter++;
	/* interleave the CQEs */
	if (sq->cev_counter >= sq->cev_factor) {
		sq->cev_counter = 0;
		return (1);
	}
	return (0);
}

void
mlx5e_send_nop(struct mlx5e_sq *sq, u32 ds_cnt)
{
	u16 pi = sq->pc & sq->wq.sz_m1;
	struct mlx5e_tx_wqe *wqe = mlx5_wq_cyc_get_wqe(&sq->wq, pi);

	memset(&wqe->ctrl, 0, sizeof(wqe->ctrl));

	wqe->ctrl.opmod_idx_opcode = cpu_to_be32((sq->pc << 8) | MLX5_OPCODE_NOP);
	wqe->ctrl.qpn_ds = cpu_to_be32((sq->sqn << 8) | ds_cnt);
	if (mlx5e_do_send_cqe(sq))
		wqe->ctrl.fm_ce_se = MLX5_WQE_CTRL_CQ_UPDATE;
	else
		wqe->ctrl.fm_ce_se = 0;

	/* Copy data for doorbell */
	memcpy(sq->doorbell.d32, &wqe->ctrl, sizeof(sq->doorbell.d32));

	sq->mbuf[pi].mbuf = NULL;
	sq->mbuf[pi].num_bytes = 0;
	sq->mbuf[pi].num_wqebbs = DIV_ROUND_UP(ds_cnt, MLX5_SEND_WQEBB_NUM_DS);
	sq->pc += sq->mbuf[pi].num_wqebbs;
}

#if (__FreeBSD_version >= 1100000)
static uint32_t mlx5e_hash_value;

static void
mlx5e_hash_init(void *arg)
{
	mlx5e_hash_value = m_ether_tcpip_hash_init();
}

/* Make kernel call mlx5e_hash_init after the random stack finished initializing */
SYSINIT(mlx5e_hash_init, SI_SUB_RANDOM, SI_ORDER_ANY, &mlx5e_hash_init, NULL);
#endif

static struct mlx5e_sq *
mlx5e_select_queue_by_send_tag(struct ifnet *ifp, struct mbuf *mb)
{
	struct mlx5e_snd_tag *ptag;
	struct mlx5e_sq *sq;

	/* check for route change */
	if (mb->m_pkthdr.snd_tag->ifp != ifp)
		return (NULL);

	/* get pointer to sendqueue */
	ptag = container_of(mb->m_pkthdr.snd_tag,
	    struct mlx5e_snd_tag, m_snd_tag);

	switch (ptag->type) {
#ifdef RATELIMIT
	case IF_SND_TAG_TYPE_RATE_LIMIT:
		sq = container_of(ptag,
		    struct mlx5e_rl_channel, tag)->sq;
		break;
#endif
	case IF_SND_TAG_TYPE_UNLIMITED:
		sq = &container_of(ptag,
		    struct mlx5e_channel, tag)->sq[0];
		KASSERT(({
		    struct mlx5e_priv *priv = ifp->if_softc;
		    priv->channel_refs > 0; }),
		    ("mlx5e_select_queue: Channel refs are zero for unlimited tag"));
		break;
	default:
		sq = NULL;
		break;
	}

	/* check if valid */
	if (sq != NULL && READ_ONCE(sq->running) != 0)
		return (sq);

	return (NULL);
}

static struct mlx5e_sq *
mlx5e_select_queue(struct ifnet *ifp, struct mbuf *mb)
{
	struct mlx5e_priv *priv = ifp->if_softc;
	struct mlx5e_sq *sq;
	u32 ch;
	u32 tc;

	/* obtain VLAN information if present */
	if (mb->m_flags & M_VLANTAG) {
		tc = (mb->m_pkthdr.ether_vtag >> 13);
		if (tc >= priv->num_tc)
			tc = priv->default_vlan_prio;
	} else {
		tc = priv->default_vlan_prio;
	}

	ch = priv->params.num_channels;

	/* check if flowid is set */
	if (M_HASHTYPE_GET(mb) != M_HASHTYPE_NONE) {
#ifdef RSS
		u32 temp;

		if (rss_hash2bucket(mb->m_pkthdr.flowid,
		    M_HASHTYPE_GET(mb), &temp) == 0)
			ch = temp % ch;
		else
#endif
			ch = (mb->m_pkthdr.flowid % 128) % ch;
	} else {
#if (__FreeBSD_version >= 1100000)
		ch = m_ether_tcpip_hash(MBUF_HASHFLAG_L3 |
		    MBUF_HASHFLAG_L4, mb, mlx5e_hash_value) % ch;
#else
		/*
		 * m_ether_tcpip_hash not present in stable, so just
		 * throw unhashed mbufs on queue 0
		 */
		ch = 0;
#endif
	}

	/* check if send queue is running */
	sq = &priv->channel[ch].sq[tc];
	if (likely(READ_ONCE(sq->running) != 0))
		return (sq);
	return (NULL);
}

static inline u16
mlx5e_get_l2_header_size(struct mlx5e_sq *sq, struct mbuf *mb)
{
	struct ether_vlan_header *eh;
	uint16_t eth_type;
	int min_inline;

	eh = mtod(mb, struct ether_vlan_header *);
	if (unlikely(mb->m_len < ETHER_HDR_LEN)) {
		goto max_inline;
	} else if (eh->evl_encap_proto == htons(ETHERTYPE_VLAN)) {
		if (unlikely(mb->m_len < (ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN)))
			goto max_inline;
		eth_type = ntohs(eh->evl_proto);
		min_inline = ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN;
	} else {
		eth_type = ntohs(eh->evl_encap_proto);
		min_inline = ETHER_HDR_LEN;
	}

	switch (eth_type) {
	case ETHERTYPE_IP:
	case ETHERTYPE_IPV6:
		/*
		 * Make sure the TOS(IPv4) or traffic class(IPv6)
		 * field gets inlined. Else the SQ may stall.
		 */
		min_inline += 4;
		break;
	default:
		goto max_inline;
	}

	/*
	 * m_copydata() will be used on the remaining header which
	 * does not need to reside within the first m_len bytes of
	 * data:
	 */
	if (mb->m_pkthdr.len < min_inline)
		goto max_inline;
	return (min_inline);

max_inline:
	return (MIN(mb->m_pkthdr.len, sq->max_inline));
}

static int
mlx5e_get_full_header_size(struct mbuf *mb)
{
	struct ether_vlan_header *eh;
	struct tcphdr *th;
	struct ip *ip;
	int ip_hlen, tcp_hlen;
	struct ip6_hdr *ip6;
	uint16_t eth_type;
	int eth_hdr_len;

	eh = mtod(mb, struct ether_vlan_header *);
	if (mb->m_len < ETHER_HDR_LEN)
		return (0);
	if (eh->evl_encap_proto == htons(ETHERTYPE_VLAN)) {
		if (mb->m_len < (ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN))
			return (0);
		eth_type = ntohs(eh->evl_proto);
		eth_hdr_len = ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN;
	} else {
		eth_type = ntohs(eh->evl_encap_proto);
		eth_hdr_len = ETHER_HDR_LEN;
	}
	switch (eth_type) {
	case ETHERTYPE_IP:
		ip = (struct ip *)(mb->m_data + eth_hdr_len);
		if (mb->m_len < eth_hdr_len + sizeof(*ip))
			return (0);
		switch (ip->ip_p) {
		case IPPROTO_TCP:
			ip_hlen = ip->ip_hl << 2;
			eth_hdr_len += ip_hlen;
			break;
		case IPPROTO_UDP:
			ip_hlen = ip->ip_hl << 2;
			eth_hdr_len += ip_hlen + 8;
			goto done;
		default:
			return (0);
		}
		break;
	case ETHERTYPE_IPV6:
		ip6 = (struct ip6_hdr *)(mb->m_data + eth_hdr_len);
		if (mb->m_len < eth_hdr_len + sizeof(*ip6))
			return (0);
		switch (ip6->ip6_nxt) {
		case IPPROTO_TCP:
			eth_hdr_len += sizeof(*ip6);
			break;
		case IPPROTO_UDP:
			eth_hdr_len += sizeof(*ip6) + 8;
			goto done;
		default:
			return (0);
		}
		break;
	default:
		return (0);
	}
	if (mb->m_len < eth_hdr_len + sizeof(*th))
		return (0);
	th = (struct tcphdr *)(mb->m_data + eth_hdr_len);
	tcp_hlen = th->th_off << 2;
	eth_hdr_len += tcp_hlen;
done:
	/*
	 * m_copydata() will be used on the remaining header which
	 * does not need to reside within the first m_len bytes of
	 * data:
	 */
	if (mb->m_pkthdr.len < eth_hdr_len)
		return (0);
	return (eth_hdr_len);
}

static int
mlx5e_sq_xmit(struct mlx5e_sq *sq, struct mbuf **mbp)
{
	bus_dma_segment_t segs[MLX5E_MAX_TX_MBUF_FRAGS];
	struct mlx5_wqe_data_seg *dseg;
	struct mlx5e_tx_wqe *wqe;
	struct ifnet *ifp;
	int nsegs;
	int err;
	int x;
	struct mbuf *mb = *mbp;
	u16 ds_cnt;
	u16 ihs;
	u16 pi;
	u8 opcode;

	/* Return ENOBUFS if the queue is full */
	if (unlikely(!mlx5e_sq_has_room_for(sq, 2 * MLX5_SEND_WQE_MAX_WQEBBS)))
		return (ENOBUFS);

	/* Align SQ edge with NOPs to avoid WQE wrap around */
	pi = ((~sq->pc) & sq->wq.sz_m1);
	if (pi < (MLX5_SEND_WQE_MAX_WQEBBS - 1)) {
		/* Send one multi NOP message instead of many */
		mlx5e_send_nop(sq, (pi + 1) * MLX5_SEND_WQEBB_NUM_DS);
		pi = ((~sq->pc) & sq->wq.sz_m1);
		if (pi < (MLX5_SEND_WQE_MAX_WQEBBS - 1))
			return (ENOMEM);
	}

	/* Setup local variables */
	pi = sq->pc & sq->wq.sz_m1;
	wqe = mlx5_wq_cyc_get_wqe(&sq->wq, pi);
	ifp = sq->ifp;

	memset(wqe, 0, sizeof(*wqe));

	/* Send a copy of the frame to the BPF listener, if any */
	if (ifp != NULL && ifp->if_bpf != NULL)
		ETHER_BPF_MTAP(ifp, mb);

	if (mb->m_pkthdr.csum_flags & (CSUM_IP | CSUM_TSO)) {
		wqe->eth.cs_flags |= MLX5_ETH_WQE_L3_CSUM;
	}
	if (mb->m_pkthdr.csum_flags & (CSUM_TCP | CSUM_UDP | CSUM_UDP_IPV6 | CSUM_TCP_IPV6 | CSUM_TSO)) {
		wqe->eth.cs_flags |= MLX5_ETH_WQE_L4_CSUM;
	}
	if (wqe->eth.cs_flags == 0) {
		sq->stats.csum_offload_none++;
	}
	if (mb->m_pkthdr.csum_flags & CSUM_TSO) {
		u32 payload_len;
		u32 mss = mb->m_pkthdr.tso_segsz;
		u32 num_pkts;

		wqe->eth.mss = cpu_to_be16(mss);
		opcode = MLX5_OPCODE_LSO;
		ihs = mlx5e_get_full_header_size(mb);
		if (unlikely(ihs == 0)) {
			err = EINVAL;
			goto tx_drop;
		}
		payload_len = mb->m_pkthdr.len - ihs;
		if (payload_len == 0)
			num_pkts = 1;
		else
			num_pkts = DIV_ROUND_UP(payload_len, mss);
		sq->mbuf[pi].num_bytes = payload_len + (num_pkts * ihs);

		sq->stats.tso_packets++;
		sq->stats.tso_bytes += payload_len;
	} else {
		opcode = MLX5_OPCODE_SEND;

		switch (sq->min_inline_mode) {
		case MLX5_INLINE_MODE_IP:
		case MLX5_INLINE_MODE_TCP_UDP:
			ihs = mlx5e_get_full_header_size(mb);
			if (unlikely(ihs == 0))
				ihs = mlx5e_get_l2_header_size(sq, mb);
			break;
		case MLX5_INLINE_MODE_L2:
			ihs = mlx5e_get_l2_header_size(sq, mb);
			break;
		case MLX5_INLINE_MODE_NONE:
			/* FALLTHROUGH */
		default:
			if ((mb->m_flags & M_VLANTAG) != 0 &&
			    (sq->min_insert_caps & MLX5E_INSERT_VLAN) != 0) {
				/* inlining VLAN data is not required */
				wqe->eth.vlan_cmd = htons(0x8000); /* bit 0 CVLAN */
				wqe->eth.vlan_hdr = htons(mb->m_pkthdr.ether_vtag);
				ihs = 0;
			} else if ((mb->m_flags & M_VLANTAG) == 0 &&
				   (sq->min_insert_caps & MLX5E_INSERT_NON_VLAN) != 0) {
				/* inlining non-VLAN data is not required */
				ihs = 0;
			} else {
				/* we are forced to inlining L2 header, if any */
				ihs = mlx5e_get_l2_header_size(sq, mb);
			}
			break;
		}
		sq->mbuf[pi].num_bytes = max_t (unsigned int,
		    mb->m_pkthdr.len, ETHER_MIN_LEN - ETHER_CRC_LEN);
	}

	if (likely(ihs == 0)) {
		/* nothing to inline */
	} else if (unlikely(ihs > sq->max_inline)) {
		/* inline header size is too big */
		err = EINVAL;
		goto tx_drop;
	} else if ((mb->m_flags & M_VLANTAG) != 0) {
		struct ether_vlan_header *eh = (struct ether_vlan_header *)
		    wqe->eth.inline_hdr_start;

		/* Range checks */
		if (unlikely(ihs > (MLX5E_MAX_TX_INLINE - ETHER_VLAN_ENCAP_LEN)))
			ihs = (MLX5E_MAX_TX_INLINE - ETHER_VLAN_ENCAP_LEN);
		else if (unlikely(ihs < ETHER_HDR_LEN)) {
			err = EINVAL;
			goto tx_drop;
		}
		m_copydata(mb, 0, ETHER_HDR_LEN, (caddr_t)eh);
		m_adj(mb, ETHER_HDR_LEN);
		/* Insert 4 bytes VLAN tag into data stream */
		eh->evl_proto = eh->evl_encap_proto;
		eh->evl_encap_proto = htons(ETHERTYPE_VLAN);
		eh->evl_tag = htons(mb->m_pkthdr.ether_vtag);
		/* Copy rest of header data, if any */
		m_copydata(mb, 0, ihs - ETHER_HDR_LEN, (caddr_t)(eh + 1));
		m_adj(mb, ihs - ETHER_HDR_LEN);
		/* Extend header by 4 bytes */
		ihs += ETHER_VLAN_ENCAP_LEN;
		wqe->eth.inline_hdr_sz = cpu_to_be16(ihs);
	} else {
		m_copydata(mb, 0, ihs, wqe->eth.inline_hdr_start);
		m_adj(mb, ihs);
		wqe->eth.inline_hdr_sz = cpu_to_be16(ihs);
	}

	ds_cnt = sizeof(*wqe) / MLX5_SEND_WQE_DS;
	if (ihs > sizeof(wqe->eth.inline_hdr_start)) {
		ds_cnt += DIV_ROUND_UP(ihs - sizeof(wqe->eth.inline_hdr_start),
		    MLX5_SEND_WQE_DS);
	}
	dseg = ((struct mlx5_wqe_data_seg *)&wqe->ctrl) + ds_cnt;

	err = bus_dmamap_load_mbuf_sg(sq->dma_tag, sq->mbuf[pi].dma_map,
	    mb, segs, &nsegs, BUS_DMA_NOWAIT);
	if (err == EFBIG) {
		/* Update statistics */
		sq->stats.defragged++;
		/* Too many mbuf fragments */
		mb = m_defrag(*mbp, M_NOWAIT);
		if (mb == NULL) {
			mb = *mbp;
			goto tx_drop;
		}
		/* Try again */
		err = bus_dmamap_load_mbuf_sg(sq->dma_tag, sq->mbuf[pi].dma_map,
		    mb, segs, &nsegs, BUS_DMA_NOWAIT);
	}
	/* Catch errors */
	if (err != 0)
		goto tx_drop;

	/* Make sure all mbuf data, if any, is written to RAM */
	if (nsegs != 0) {
		bus_dmamap_sync(sq->dma_tag, sq->mbuf[pi].dma_map,
		    BUS_DMASYNC_PREWRITE);
	} else {
		/* All data was inlined, free the mbuf. */
		bus_dmamap_unload(sq->dma_tag, sq->mbuf[pi].dma_map);
		m_freem(mb);
		mb = NULL;
	}

	for (x = 0; x != nsegs; x++) {
		if (segs[x].ds_len == 0)
			continue;
		dseg->addr = cpu_to_be64((uint64_t)segs[x].ds_addr);
		dseg->lkey = sq->mkey_be;
		dseg->byte_count = cpu_to_be32((uint32_t)segs[x].ds_len);
		dseg++;
	}

	ds_cnt = (dseg - ((struct mlx5_wqe_data_seg *)&wqe->ctrl));

	wqe->ctrl.opmod_idx_opcode = cpu_to_be32((sq->pc << 8) | opcode);
	wqe->ctrl.qpn_ds = cpu_to_be32((sq->sqn << 8) | ds_cnt);
	if (mlx5e_do_send_cqe(sq))
		wqe->ctrl.fm_ce_se = MLX5_WQE_CTRL_CQ_UPDATE;
	else
		wqe->ctrl.fm_ce_se = 0;

	/* Copy data for doorbell */
	memcpy(sq->doorbell.d32, &wqe->ctrl, sizeof(sq->doorbell.d32));

	/* Store pointer to mbuf */
	sq->mbuf[pi].mbuf = mb;
	sq->mbuf[pi].num_wqebbs = DIV_ROUND_UP(ds_cnt, MLX5_SEND_WQEBB_NUM_DS);
	sq->pc += sq->mbuf[pi].num_wqebbs;

	/* Count all traffic going out */
	sq->stats.packets++;
	sq->stats.bytes += sq->mbuf[pi].num_bytes;

	*mbp = NULL;	/* safety clear */
	return (0);

tx_drop:
	sq->stats.dropped++;
	*mbp = NULL;
	m_freem(mb);
	return err;
}

static void
mlx5e_poll_tx_cq(struct mlx5e_sq *sq, int budget)
{
	u16 sqcc;

	/*
	 * sq->cc must be updated only after mlx5_cqwq_update_db_record(),
	 * otherwise a cq overrun may occur
	 */
	sqcc = sq->cc;

	while (budget > 0) {
		struct mlx5_cqe64 *cqe;
		struct mbuf *mb;
		u16 x;
		u16 ci;

		cqe = mlx5e_get_cqe(&sq->cq);
		if (!cqe)
			break;

		mlx5_cqwq_pop(&sq->cq.wq);

		/* update budget according to the event factor */
		budget -= sq->cev_factor;

		for (x = 0; x != sq->cev_factor; x++) {
			ci = sqcc & sq->wq.sz_m1;
			mb = sq->mbuf[ci].mbuf;
			sq->mbuf[ci].mbuf = NULL;	/* Safety clear */

			if (mb == NULL) {
				if (sq->mbuf[ci].num_bytes == 0) {
					/* NOP */
					sq->stats.nop++;
				}
			} else {
				bus_dmamap_sync(sq->dma_tag, sq->mbuf[ci].dma_map,
				    BUS_DMASYNC_POSTWRITE);
				bus_dmamap_unload(sq->dma_tag, sq->mbuf[ci].dma_map);

				/* Free transmitted mbuf */
				m_freem(mb);
			}
			sqcc += sq->mbuf[ci].num_wqebbs;
		}
	}

	mlx5_cqwq_update_db_record(&sq->cq.wq);

	/* Ensure cq space is freed before enabling more cqes */
	atomic_thread_fence_rel();

	sq->cc = sqcc;
}

static int
mlx5e_xmit_locked(struct ifnet *ifp, struct mlx5e_sq *sq, struct mbuf *mb)
{
	int err = 0;

	if (unlikely((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0 ||
	    READ_ONCE(sq->running) == 0)) {
		m_freem(mb);
		return (ENETDOWN);
	}

	/* Do transmit */
	if (mlx5e_sq_xmit(sq, &mb) != 0) {
		/* NOTE: m_freem() is NULL safe */
		m_freem(mb);
		err = ENOBUFS;
	}

	/* Check if we need to write the doorbell */
	if (likely(sq->doorbell.d64 != 0)) {
		mlx5e_tx_notify_hw(sq, sq->doorbell.d32, 0);
		sq->doorbell.d64 = 0;
	}

	/*
	 * Check if we need to start the event timer which flushes the
	 * transmit ring on timeout:
	 */
	if (unlikely(sq->cev_next_state == MLX5E_CEV_STATE_INITIAL &&
	    sq->cev_factor != 1)) {
		/* start the timer */
		mlx5e_sq_cev_timeout(sq);
	} else {
		/* don't send NOPs yet */
		sq->cev_next_state = MLX5E_CEV_STATE_HOLD_NOPS;
	}
	return (err);
}

int
mlx5e_xmit(struct ifnet *ifp, struct mbuf *mb)
{
	struct mlx5e_sq *sq;
	int ret;

	if (mb->m_pkthdr.snd_tag != NULL) {
		sq = mlx5e_select_queue_by_send_tag(ifp, mb);
		if (unlikely(sq == NULL)) {
			/* Check for route change */
			if (mb->m_pkthdr.snd_tag->ifp != ifp) {
				/* Free mbuf */
				m_freem(mb);

				/*
				 * Tell upper layers about route
				 * change and to re-transmit this
				 * packet:
				 */
				return (EAGAIN);
			}
			goto select_queue;
		}
	} else {
select_queue:
		sq = mlx5e_select_queue(ifp, mb);
		if (unlikely(sq == NULL)) {
			/* Free mbuf */
			m_freem(mb);

			/* Invalid send queue */
			return (ENXIO);
		}
	}

	mtx_lock(&sq->lock);
	ret = mlx5e_xmit_locked(ifp, sq, mb);
	mtx_unlock(&sq->lock);

	return (ret);
}

void
mlx5e_tx_cq_comp(struct mlx5_core_cq *mcq)
{
	struct mlx5e_sq *sq = container_of(mcq, struct mlx5e_sq, cq.mcq);

	mtx_lock(&sq->comp_lock);
	mlx5e_poll_tx_cq(sq, MLX5E_BUDGET_MAX);
	mlx5e_cq_arm(&sq->cq, MLX5_GET_DOORBELL_LOCK(&sq->priv->doorbell_lock));
	mtx_unlock(&sq->comp_lock);
}
