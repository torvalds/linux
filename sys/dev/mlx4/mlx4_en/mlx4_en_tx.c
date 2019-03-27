/*
 * Copyright (c) 2007, 2014 Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#define	LINUXKPI_PARAM_PREFIX mlx4_

#include <linux/page.h>
#include <dev/mlx4/cq.h>
#include <linux/slab.h>
#include <dev/mlx4/qp.h>
#include <linux/if_vlan.h>
#include <linux/vmalloc.h>
#include <linux/moduleparam.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/tcp.h>
#include <netinet/tcp_lro.h>
#include <netinet/udp.h>

#include "en.h"

int mlx4_en_create_tx_ring(struct mlx4_en_priv *priv,
			   struct mlx4_en_tx_ring **pring, u32 size,
			   u16 stride, int node, int queue_idx)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_en_tx_ring *ring;
	uint32_t x;
	int tmp;
	int err;

	ring = kzalloc_node(sizeof(struct mlx4_en_tx_ring), GFP_KERNEL, node);
	if (!ring) {
		ring = kzalloc(sizeof(struct mlx4_en_tx_ring), GFP_KERNEL);
		if (!ring) {
			en_err(priv, "Failed allocating TX ring\n");
			return -ENOMEM;
		}
	}

	/* Create DMA descriptor TAG */
	if ((err = -bus_dma_tag_create(
	    bus_get_dma_tag(mdev->pdev->dev.bsddev),
	    1,					/* any alignment */
	    0,					/* no boundary */
	    BUS_SPACE_MAXADDR,			/* lowaddr */
	    BUS_SPACE_MAXADDR,			/* highaddr */
	    NULL, NULL,				/* filter, filterarg */
	    MLX4_EN_TX_MAX_PAYLOAD_SIZE,	/* maxsize */
	    MLX4_EN_TX_MAX_MBUF_FRAGS,		/* nsegments */
	    MLX4_EN_TX_MAX_MBUF_SIZE,		/* maxsegsize */
	    0,					/* flags */
	    NULL, NULL,				/* lockfunc, lockfuncarg */
	    &ring->dma_tag)))
		goto done;

	ring->size = size;
	ring->size_mask = size - 1;
	ring->stride = stride;
	ring->inline_thold = MAX(MIN_PKT_LEN, MIN(priv->prof->inline_thold, MAX_INLINE));
	mtx_init(&ring->tx_lock.m, "mlx4 tx", NULL, MTX_DEF);
	mtx_init(&ring->comp_lock.m, "mlx4 comp", NULL, MTX_DEF);

	tmp = size * sizeof(struct mlx4_en_tx_info);
	ring->tx_info = kzalloc_node(tmp, GFP_KERNEL, node);
	if (!ring->tx_info) {
		ring->tx_info = kzalloc(tmp, GFP_KERNEL);
		if (!ring->tx_info) {
			err = -ENOMEM;
			goto err_ring;
		}
	}

	/* Create DMA descriptor MAPs */
	for (x = 0; x != size; x++) {
		err = -bus_dmamap_create(ring->dma_tag, 0,
		    &ring->tx_info[x].dma_map);
		if (err != 0) {
			while (x--) {
				bus_dmamap_destroy(ring->dma_tag,
				    ring->tx_info[x].dma_map);
			}
			goto err_info;
		}
	}

	en_dbg(DRV, priv, "Allocated tx_info ring at addr:%p size:%d\n",
		 ring->tx_info, tmp);

	ring->buf_size = ALIGN(size * ring->stride, MLX4_EN_PAGE_SIZE);

	/* Allocate HW buffers on provided NUMA node */
	err = mlx4_alloc_hwq_res(mdev->dev, &ring->wqres, ring->buf_size,
				 2 * PAGE_SIZE);
	if (err) {
		en_err(priv, "Failed allocating hwq resources\n");
		goto err_dma_map;
	}

	err = mlx4_en_map_buffer(&ring->wqres.buf);
	if (err) {
		en_err(priv, "Failed to map TX buffer\n");
		goto err_hwq_res;
	}

	ring->buf = ring->wqres.buf.direct.buf;

	en_dbg(DRV, priv, "Allocated TX ring (addr:%p) - buf:%p size:%d "
	       "buf_size:%d dma:%llx\n", ring, ring->buf, ring->size,
	       ring->buf_size, (unsigned long long) ring->wqres.buf.direct.map);

	err = mlx4_qp_reserve_range(mdev->dev, 1, 1, &ring->qpn,
				    MLX4_RESERVE_ETH_BF_QP);
	if (err) {
		en_err(priv, "failed reserving qp for TX ring\n");
		goto err_map;
	}

	err = mlx4_qp_alloc(mdev->dev, ring->qpn, &ring->qp, GFP_KERNEL);
	if (err) {
		en_err(priv, "Failed allocating qp %d\n", ring->qpn);
		goto err_reserve;
	}
	ring->qp.event = mlx4_en_sqp_event;

	err = mlx4_bf_alloc(mdev->dev, &ring->bf, node);
	if (err) {
		en_dbg(DRV, priv, "working without blueflame (%d)", err);
		ring->bf.uar = &mdev->priv_uar;
		ring->bf.uar->map = mdev->uar_map;
		ring->bf_enabled = false;
	} else
		ring->bf_enabled = true;
	ring->queue_index = queue_idx;

	*pring = ring;
	return 0;

err_reserve:
	mlx4_qp_release_range(mdev->dev, ring->qpn, 1);
err_map:
	mlx4_en_unmap_buffer(&ring->wqres.buf);
err_hwq_res:
	mlx4_free_hwq_res(mdev->dev, &ring->wqres, ring->buf_size);
err_dma_map:
	for (x = 0; x != size; x++)
		bus_dmamap_destroy(ring->dma_tag, ring->tx_info[x].dma_map);
err_info:
	vfree(ring->tx_info);
err_ring:
	bus_dma_tag_destroy(ring->dma_tag);
done:
	kfree(ring);
	return err;
}

void mlx4_en_destroy_tx_ring(struct mlx4_en_priv *priv,
			     struct mlx4_en_tx_ring **pring)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_en_tx_ring *ring = *pring;
	uint32_t x;
	en_dbg(DRV, priv, "Destroying tx ring, qpn: %d\n", ring->qpn);

	if (ring->bf_enabled)
		mlx4_bf_free(mdev->dev, &ring->bf);
	mlx4_qp_remove(mdev->dev, &ring->qp);
	mlx4_qp_free(mdev->dev, &ring->qp);
	mlx4_qp_release_range(priv->mdev->dev, ring->qpn, 1);
	mlx4_en_unmap_buffer(&ring->wqres.buf);
	mlx4_free_hwq_res(mdev->dev, &ring->wqres, ring->buf_size);
	for (x = 0; x != ring->size; x++)
		bus_dmamap_destroy(ring->dma_tag, ring->tx_info[x].dma_map);
	vfree(ring->tx_info);
	mtx_destroy(&ring->tx_lock.m);
	mtx_destroy(&ring->comp_lock.m);
	bus_dma_tag_destroy(ring->dma_tag);
	kfree(ring);
	*pring = NULL;
}

int mlx4_en_activate_tx_ring(struct mlx4_en_priv *priv,
			     struct mlx4_en_tx_ring *ring,
			     int cq, int user_prio)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	int err;

	ring->cqn = cq;
	ring->prod = 0;
	ring->cons = 0xffffffff;
	ring->last_nr_txbb = 1;
	ring->poll_cnt = 0;
	memset(ring->buf, 0, ring->buf_size);
	ring->watchdog_time = 0;

	ring->qp_state = MLX4_QP_STATE_RST;
	ring->doorbell_qpn = ring->qp.qpn << 8;

	mlx4_en_fill_qp_context(priv, ring->size, ring->stride, 1, 0, ring->qpn,
				ring->cqn, user_prio, &ring->context);
	if (ring->bf_enabled)
		ring->context.usr_page = cpu_to_be32(ring->bf.uar->index);

	err = mlx4_qp_to_ready(mdev->dev, &ring->wqres.mtt, &ring->context,
			       &ring->qp, &ring->qp_state);
	return err;
}

void mlx4_en_deactivate_tx_ring(struct mlx4_en_priv *priv,
				struct mlx4_en_tx_ring *ring)
{
	struct mlx4_en_dev *mdev = priv->mdev;

	mlx4_qp_modify(mdev->dev, NULL, ring->qp_state,
		       MLX4_QP_STATE_RST, NULL, 0, 0, &ring->qp);
}

static volatile struct mlx4_wqe_data_seg *
mlx4_en_store_inline_lso_data(volatile struct mlx4_wqe_data_seg *dseg,
    struct mbuf *mb, int len, __be32 owner_bit)
{
	uint8_t *inl = __DEVOLATILE(uint8_t *, dseg);

	/* copy data into place */
	m_copydata(mb, 0, len, inl + 4);
	dseg += DIV_ROUND_UP(4 + len, DS_SIZE_ALIGNMENT);
	return (dseg);
}

static void
mlx4_en_store_inline_lso_header(volatile struct mlx4_wqe_data_seg *dseg,
    int len, __be32 owner_bit)
{
}

static void
mlx4_en_stamp_wqe(struct mlx4_en_priv *priv,
    struct mlx4_en_tx_ring *ring, u32 index, u8 owner)
{
	struct mlx4_en_tx_info *tx_info = &ring->tx_info[index];
	struct mlx4_en_tx_desc *tx_desc = (struct mlx4_en_tx_desc *)
	    (ring->buf + (index * TXBB_SIZE));
	volatile __be32 *ptr = (__be32 *)tx_desc;
	const __be32 stamp = cpu_to_be32(STAMP_VAL |
	    ((u32)owner << STAMP_SHIFT));
	u32 i;

	/* Stamp the freed descriptor */
	for (i = 0; i < tx_info->nr_txbb * TXBB_SIZE; i += STAMP_STRIDE) {
		*ptr = stamp;
		ptr += STAMP_DWORDS;
	}
}

static u32
mlx4_en_free_tx_desc(struct mlx4_en_priv *priv,
    struct mlx4_en_tx_ring *ring, u32 index)
{
	struct mlx4_en_tx_info *tx_info;
	struct mbuf *mb;

	tx_info = &ring->tx_info[index];
	mb = tx_info->mb;

	if (mb == NULL)
		goto done;

	bus_dmamap_sync(ring->dma_tag, tx_info->dma_map,
	    BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(ring->dma_tag, tx_info->dma_map);

        m_freem(mb);
done:
	return (tx_info->nr_txbb);
}

int mlx4_en_free_tx_buf(struct net_device *dev, struct mlx4_en_tx_ring *ring)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	int cnt = 0;

	/* Skip last polled descriptor */
	ring->cons += ring->last_nr_txbb;
	en_dbg(DRV, priv, "Freeing Tx buf - cons:0x%x prod:0x%x\n",
		 ring->cons, ring->prod);

	if ((u32) (ring->prod - ring->cons) > ring->size) {
                en_warn(priv, "Tx consumer passed producer!\n");
		return 0;
	}

	while (ring->cons != ring->prod) {
		ring->last_nr_txbb = mlx4_en_free_tx_desc(priv, ring,
		    ring->cons & ring->size_mask);
		ring->cons += ring->last_nr_txbb;
		cnt++;
	}

	if (cnt)
		en_dbg(DRV, priv, "Freed %d uncompleted tx descriptors\n", cnt);

	return cnt;
}

static bool
mlx4_en_tx_ring_is_full(struct mlx4_en_tx_ring *ring)
{
	int wqs;
	wqs = ring->size - (ring->prod - ring->cons);
	return (wqs < (HEADROOM + (2 * MLX4_EN_TX_WQE_MAX_WQEBBS)));
}

static int mlx4_en_process_tx_cq(struct net_device *dev,
				 struct mlx4_en_cq *cq)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_cq *mcq = &cq->mcq;
	struct mlx4_en_tx_ring *ring = priv->tx_ring[cq->ring];
	struct mlx4_cqe *cqe;
	u16 index;
	u16 new_index, ring_index, stamp_index;
	u32 txbbs_skipped = 0;
	u32 txbbs_stamp = 0;
	u32 cons_index = mcq->cons_index;
	int size = cq->size;
	u32 size_mask = ring->size_mask;
	struct mlx4_cqe *buf = cq->buf;
	int factor = priv->cqe_factor;

	if (!priv->port_up)
		return 0;

	index = cons_index & size_mask;
	cqe = &buf[(index << factor) + factor];
	ring_index = ring->cons & size_mask;
	stamp_index = ring_index;

	/* Process all completed CQEs */
	while (XNOR(cqe->owner_sr_opcode & MLX4_CQE_OWNER_MASK,
			cons_index & size)) {
		/*
		 * make sure we read the CQE after we read the
		 * ownership bit
		 */
		rmb();

		if (unlikely((cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) ==
			     MLX4_CQE_OPCODE_ERROR)) {
			en_err(priv, "CQE completed in error - vendor syndrom: 0x%x syndrom: 0x%x\n",
			       ((struct mlx4_err_cqe *)cqe)->
				       vendor_err_syndrome,
			       ((struct mlx4_err_cqe *)cqe)->syndrome);
		}

		/* Skip over last polled CQE */
		new_index = be16_to_cpu(cqe->wqe_index) & size_mask;

		do {
			txbbs_skipped += ring->last_nr_txbb;
			ring_index = (ring_index + ring->last_nr_txbb) & size_mask;
			/* free next descriptor */
			ring->last_nr_txbb = mlx4_en_free_tx_desc(
			    priv, ring, ring_index);
			mlx4_en_stamp_wqe(priv, ring, stamp_index,
					  !!((ring->cons + txbbs_stamp) &
						ring->size));
			stamp_index = ring_index;
			txbbs_stamp = txbbs_skipped;
		} while (ring_index != new_index);

		++cons_index;
		index = cons_index & size_mask;
		cqe = &buf[(index << factor) + factor];
	}


	/*
	 * To prevent CQ overflow we first update CQ consumer and only then
	 * the ring consumer.
	 */
	mcq->cons_index = cons_index;
	mlx4_cq_set_ci(mcq);
	wmb();
	ring->cons += txbbs_skipped;

	return (0);
}

void mlx4_en_tx_irq(struct mlx4_cq *mcq)
{
	struct mlx4_en_cq *cq = container_of(mcq, struct mlx4_en_cq, mcq);
	struct mlx4_en_priv *priv = netdev_priv(cq->dev);
	struct mlx4_en_tx_ring *ring = priv->tx_ring[cq->ring];

	if (priv->port_up == 0 || !spin_trylock(&ring->comp_lock))
		return;
	mlx4_en_process_tx_cq(cq->dev, cq);
	mod_timer(&cq->timer, jiffies + 1);
	spin_unlock(&ring->comp_lock);
}

void mlx4_en_poll_tx_cq(unsigned long data)
{
	struct mlx4_en_cq *cq = (struct mlx4_en_cq *) data;
	struct mlx4_en_priv *priv = netdev_priv(cq->dev);
	struct mlx4_en_tx_ring *ring = priv->tx_ring[cq->ring];
	u32 inflight;

	INC_PERF_COUNTER(priv->pstats.tx_poll);

	if (priv->port_up == 0)
		return;
	if (!spin_trylock(&ring->comp_lock)) {
		mod_timer(&cq->timer, jiffies + MLX4_EN_TX_POLL_TIMEOUT);
		return;
	}
	mlx4_en_process_tx_cq(cq->dev, cq);
	inflight = (u32) (ring->prod - ring->cons - ring->last_nr_txbb);

	/* If there are still packets in flight and the timer has not already
	 * been scheduled by the Tx routine then schedule it here to guarantee
	 * completion processing of these packets */
	if (inflight && priv->port_up)
		mod_timer(&cq->timer, jiffies + MLX4_EN_TX_POLL_TIMEOUT);

	spin_unlock(&ring->comp_lock);
}

static inline void mlx4_en_xmit_poll(struct mlx4_en_priv *priv, int tx_ind)
{
	struct mlx4_en_cq *cq = priv->tx_cq[tx_ind];
	struct mlx4_en_tx_ring *ring = priv->tx_ring[tx_ind];

	if (priv->port_up == 0)
		return;

	/* If we don't have a pending timer, set one up to catch our recent
	   post in case the interface becomes idle */
	if (!timer_pending(&cq->timer))
		mod_timer(&cq->timer, jiffies + MLX4_EN_TX_POLL_TIMEOUT);

	/* Poll the CQ every mlx4_en_TX_MODER_POLL packets */
	if ((++ring->poll_cnt & (MLX4_EN_TX_POLL_MODER - 1)) == 0)
		if (spin_trylock(&ring->comp_lock)) {
			mlx4_en_process_tx_cq(priv->dev, cq);
			spin_unlock(&ring->comp_lock);
		}
}

static u16
mlx4_en_get_inline_hdr_size(struct mlx4_en_tx_ring *ring, struct mbuf *mb)
{
	u16 retval;

	/* only copy from first fragment, if possible */
	retval = MIN(ring->inline_thold, mb->m_len);

	/* check for too little data */
	if (unlikely(retval < MIN_PKT_LEN))
		retval = MIN(ring->inline_thold, mb->m_pkthdr.len);
	return (retval);
}

static int
mlx4_en_get_header_size(struct mbuf *mb)
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
		eth_type = ntohs(eh->evl_proto);
		eth_hdr_len = ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN;
	} else {
		eth_type = ntohs(eh->evl_encap_proto);
		eth_hdr_len = ETHER_HDR_LEN;
	}
	if (mb->m_len < eth_hdr_len)
		return (0);
	switch (eth_type) {
	case ETHERTYPE_IP:
		ip = (struct ip *)(mb->m_data + eth_hdr_len);
		if (mb->m_len < eth_hdr_len + sizeof(*ip))
			return (0);
		if (ip->ip_p != IPPROTO_TCP)
			return (0);
		ip_hlen = ip->ip_hl << 2;
		eth_hdr_len += ip_hlen;
		break;
	case ETHERTYPE_IPV6:
		ip6 = (struct ip6_hdr *)(mb->m_data + eth_hdr_len);
		if (mb->m_len < eth_hdr_len + sizeof(*ip6))
			return (0);
		if (ip6->ip6_nxt != IPPROTO_TCP)
			return (0);
		eth_hdr_len += sizeof(*ip6);
		break;
	default:
		return (0);
	}
	if (mb->m_len < eth_hdr_len + sizeof(*th))
		return (0);
	th = (struct tcphdr *)(mb->m_data + eth_hdr_len);
	tcp_hlen = th->th_off << 2;
	eth_hdr_len += tcp_hlen;
	if (mb->m_len < eth_hdr_len)
		return (0);
	return (eth_hdr_len);
}

static volatile struct mlx4_wqe_data_seg *
mlx4_en_store_inline_data(volatile struct mlx4_wqe_data_seg *dseg,
    struct mbuf *mb, int len, __be32 owner_bit)
{
	uint8_t *inl = __DEVOLATILE(uint8_t *, dseg);
	const int spc = MLX4_INLINE_ALIGN - CTRL_SIZE - 4;

	if (unlikely(len < MIN_PKT_LEN)) {
		m_copydata(mb, 0, len, inl + 4);
		memset(inl + 4 + len, 0, MIN_PKT_LEN - len);
		dseg += DIV_ROUND_UP(4 + MIN_PKT_LEN, DS_SIZE_ALIGNMENT);
	} else if (len <= spc) {
		m_copydata(mb, 0, len, inl + 4);
		dseg += DIV_ROUND_UP(4 + len, DS_SIZE_ALIGNMENT);
	} else {
		m_copydata(mb, 0, spc, inl + 4);
		m_copydata(mb, spc, len - spc, inl + 8 + spc);
		dseg += DIV_ROUND_UP(8 + len, DS_SIZE_ALIGNMENT);
	}
	return (dseg);
}

static void
mlx4_en_store_inline_header(volatile struct mlx4_wqe_data_seg *dseg,
    int len, __be32 owner_bit)
{
	uint8_t *inl = __DEVOLATILE(uint8_t *, dseg);
	const int spc = MLX4_INLINE_ALIGN - CTRL_SIZE - 4;

	if (unlikely(len < MIN_PKT_LEN)) {
		*(volatile uint32_t *)inl =
		    SET_BYTE_COUNT((1U << 31) | MIN_PKT_LEN);
	} else if (len <= spc) {
		*(volatile uint32_t *)inl =
		    SET_BYTE_COUNT((1U << 31) | len);
	} else {
		*(volatile uint32_t *)(inl + 4 + spc) =
		    SET_BYTE_COUNT((1U << 31) | (len - spc));
		wmb();
		*(volatile uint32_t *)inl =
		    SET_BYTE_COUNT((1U << 31) | spc);
	}
}

static uint32_t hashrandom;
static void hashrandom_init(void *arg)
{
	/*
	 * It is assumed that the random subsystem has been
	 * initialized when this function is called:
	 */
	hashrandom = m_ether_tcpip_hash_init();
}
SYSINIT(hashrandom_init, SI_SUB_RANDOM, SI_ORDER_ANY, &hashrandom_init, NULL);

u16 mlx4_en_select_queue(struct net_device *dev, struct mbuf *mb)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	u32 rings_p_up = priv->num_tx_rings_p_up;
	u32 up = 0;
	u32 queue_index;

#if (MLX4_EN_NUM_UP > 1)
	/* Obtain VLAN information if present */
	if (mb->m_flags & M_VLANTAG) {
		u32 vlan_tag = mb->m_pkthdr.ether_vtag;
	        up = (vlan_tag >> 13) % MLX4_EN_NUM_UP;
	}
#endif
	queue_index = m_ether_tcpip_hash(MBUF_HASHFLAG_L3 | MBUF_HASHFLAG_L4, mb, hashrandom);

	return ((queue_index % rings_p_up) + (up * rings_p_up));
}

static void mlx4_bf_copy(void __iomem *dst, volatile unsigned long *src, unsigned bytecnt)
{
	__iowrite64_copy(dst, __DEVOLATILE(void *, src), bytecnt / 8);
}

int mlx4_en_xmit(struct mlx4_en_priv *priv, int tx_ind, struct mbuf **mbp)
{
	enum {
		DS_FACT = TXBB_SIZE / DS_SIZE_ALIGNMENT,
		CTRL_FLAGS = cpu_to_be32(MLX4_WQE_CTRL_CQ_UPDATE |
		    MLX4_WQE_CTRL_SOLICITED),
	};
	bus_dma_segment_t segs[MLX4_EN_TX_MAX_MBUF_FRAGS];
	volatile struct mlx4_wqe_data_seg *dseg;
	volatile struct mlx4_wqe_data_seg *dseg_inline;
	volatile struct mlx4_en_tx_desc *tx_desc;
	struct mlx4_en_tx_ring *ring = priv->tx_ring[tx_ind];
	struct ifnet *ifp = priv->dev;
	struct mlx4_en_tx_info *tx_info;
	struct mbuf *mb = *mbp;
	struct mbuf *m;
	__be32 owner_bit;
	int nr_segs;
	int pad;
	int err;
	u32 bf_size;
	u32 bf_prod;
	u32 opcode;
	u16 index;
	u16 ds_cnt;
	u16 ihs;

	if (unlikely(!priv->port_up)) {
		err = EINVAL;
		goto tx_drop;
	}

	/* check if TX ring is full */
	if (unlikely(mlx4_en_tx_ring_is_full(ring))) {
		/* Use interrupts to find out when queue opened */
		mlx4_en_arm_cq(priv, priv->tx_cq[tx_ind]);
		return (ENOBUFS);
	}

	/* sanity check we are not wrapping around */
	KASSERT(((~ring->prod) & ring->size_mask) >=
	    (MLX4_EN_TX_WQE_MAX_WQEBBS - 1), ("Wrapping around TX ring"));

	/* Track current inflight packets for performance analysis */
	AVG_PERF_COUNTER(priv->pstats.inflight_avg,
			 (u32) (ring->prod - ring->cons - 1));

	/* Track current mbuf packet header length */
	AVG_PERF_COUNTER(priv->pstats.tx_pktsz_avg, mb->m_pkthdr.len);

	/* Grab an index and try to transmit packet */
	owner_bit = (ring->prod & ring->size) ?
		cpu_to_be32(MLX4_EN_BIT_DESC_OWN) : 0;
	index = ring->prod & ring->size_mask;
	tx_desc = (volatile struct mlx4_en_tx_desc *)
	    (ring->buf + index * TXBB_SIZE);
	tx_info = &ring->tx_info[index];
	dseg = &tx_desc->data;

	/* send a copy of the frame to the BPF listener, if any */
	if (ifp != NULL && ifp->if_bpf != NULL)
		ETHER_BPF_MTAP(ifp, mb);

	/* get default flags */
	tx_desc->ctrl.srcrb_flags = CTRL_FLAGS;

	if (mb->m_pkthdr.csum_flags & (CSUM_IP | CSUM_TSO))
		tx_desc->ctrl.srcrb_flags |= cpu_to_be32(MLX4_WQE_CTRL_IP_CSUM);

	if (mb->m_pkthdr.csum_flags & (CSUM_TCP | CSUM_UDP |
	    CSUM_UDP_IPV6 | CSUM_TCP_IPV6 | CSUM_TSO))
		tx_desc->ctrl.srcrb_flags |= cpu_to_be32(MLX4_WQE_CTRL_TCP_UDP_CSUM);

	/* do statistics */
	if (likely(tx_desc->ctrl.srcrb_flags != CTRL_FLAGS)) {
		priv->port_stats.tx_chksum_offload++;
		ring->tx_csum++;
	}

	/* check for VLAN tag */
	if (mb->m_flags & M_VLANTAG) {
		tx_desc->ctrl.vlan_tag = cpu_to_be16(mb->m_pkthdr.ether_vtag);
		tx_desc->ctrl.ins_vlan = MLX4_WQE_CTRL_INS_CVLAN;
	} else {
		tx_desc->ctrl.vlan_tag = 0;
		tx_desc->ctrl.ins_vlan = 0;
	}

	if (unlikely(mlx4_is_mfunc(priv->mdev->dev) || priv->validate_loopback)) {
		/*
		 * Copy destination MAC address to WQE. This allows
		 * loopback in eSwitch, so that VFs and PF can
		 * communicate with each other:
		 */
		m_copydata(mb, 0, 2, __DEVOLATILE(void *, &tx_desc->ctrl.srcrb_flags16[0]));
		m_copydata(mb, 2, 4, __DEVOLATILE(void *, &tx_desc->ctrl.imm));
	} else {
		/* clear immediate field */
		tx_desc->ctrl.imm = 0;
	}

	/* Handle LSO (TSO) packets */
	if (mb->m_pkthdr.csum_flags & CSUM_TSO) {
		u32 payload_len;
		u32 mss = mb->m_pkthdr.tso_segsz;
		u32 num_pkts;

		opcode = cpu_to_be32(MLX4_OPCODE_LSO | MLX4_WQE_CTRL_RR) |
		    owner_bit;
		ihs = mlx4_en_get_header_size(mb);
		if (unlikely(ihs > MAX_INLINE)) {
			ring->oversized_packets++;
			err = EINVAL;
			goto tx_drop;
		}
		tx_desc->lso.mss_hdr_size = cpu_to_be32((mss << 16) | ihs);
		payload_len = mb->m_pkthdr.len - ihs;
		if (unlikely(payload_len == 0))
			num_pkts = 1;
		else
			num_pkts = DIV_ROUND_UP(payload_len, mss);
		ring->bytes += payload_len + (num_pkts * ihs);
		ring->packets += num_pkts;
		ring->tso_packets++;
		/* store pointer to inline header */
		dseg_inline = dseg;
		/* copy data inline */
		dseg = mlx4_en_store_inline_lso_data(dseg,
		    mb, ihs, owner_bit);
	} else {
		opcode = cpu_to_be32(MLX4_OPCODE_SEND) |
		    owner_bit;
		ihs = mlx4_en_get_inline_hdr_size(ring, mb);
		ring->bytes += max_t (unsigned int,
		    mb->m_pkthdr.len, ETHER_MIN_LEN - ETHER_CRC_LEN);
		ring->packets++;
		/* store pointer to inline header */
		dseg_inline = dseg;
		/* copy data inline */
		dseg = mlx4_en_store_inline_data(dseg,
		    mb, ihs, owner_bit);
	}
	m_adj(mb, ihs);

	err = bus_dmamap_load_mbuf_sg(ring->dma_tag, tx_info->dma_map,
	    mb, segs, &nr_segs, BUS_DMA_NOWAIT);
	if (unlikely(err == EFBIG)) {
		/* Too many mbuf fragments */
		ring->defrag_attempts++;
		m = m_defrag(mb, M_NOWAIT);
		if (m == NULL) {
			ring->oversized_packets++;
			goto tx_drop;
		}
		mb = m;
		/* Try again */
		err = bus_dmamap_load_mbuf_sg(ring->dma_tag, tx_info->dma_map,
		    mb, segs, &nr_segs, BUS_DMA_NOWAIT);
	}
	/* catch errors */
	if (unlikely(err != 0)) {
		ring->oversized_packets++;
		goto tx_drop;
	}
	/* If there were no errors and we didn't load anything, don't sync. */
	if (nr_segs != 0) {
		/* make sure all mbuf data is written to RAM */
		bus_dmamap_sync(ring->dma_tag, tx_info->dma_map,
		    BUS_DMASYNC_PREWRITE);
	} else {
		/* All data was inlined, free the mbuf. */
		bus_dmamap_unload(ring->dma_tag, tx_info->dma_map);
		m_freem(mb);
		mb = NULL;
	}

	/* compute number of DS needed */
	ds_cnt = (dseg - ((volatile struct mlx4_wqe_data_seg *)tx_desc)) + nr_segs;

	/*
	 * Check if the next request can wrap around and fill the end
	 * of the current request with zero immediate data:
	 */
	pad = DIV_ROUND_UP(ds_cnt, DS_FACT);
	pad = (~(ring->prod + pad)) & ring->size_mask;

	if (unlikely(pad < (MLX4_EN_TX_WQE_MAX_WQEBBS - 1))) {
		/*
		 * Compute the least number of DS blocks we need to
		 * pad in order to achieve a TX ring wraparound:
		 */
		pad = (DS_FACT * (pad + 1));
	} else {
		/*
		 * The hardware will automatically jump to the next
		 * TXBB. No need for padding.
		 */
		pad = 0;
	}

	/* compute total number of DS blocks */
	ds_cnt += pad;
	/*
	 * When modifying this code, please ensure that the following
	 * computation is always less than or equal to 0x3F:
	 *
	 * ((MLX4_EN_TX_WQE_MAX_WQEBBS - 1) * DS_FACT) +
	 * (MLX4_EN_TX_WQE_MAX_WQEBBS * DS_FACT)
	 *
	 * Else the "ds_cnt" variable can become too big.
	 */
	tx_desc->ctrl.fence_size = (ds_cnt & 0x3f);

	/* store pointer to mbuf */
	tx_info->mb = mb;
	tx_info->nr_txbb = DIV_ROUND_UP(ds_cnt, DS_FACT);
	bf_size = ds_cnt * DS_SIZE_ALIGNMENT;
	bf_prod = ring->prod;

	/* compute end of "dseg" array */
	dseg += nr_segs + pad;

	/* pad using zero immediate dseg */
	while (pad--) {
		dseg--;
		dseg->addr = 0;
		dseg->lkey = 0;
		wmb();
		dseg->byte_count = SET_BYTE_COUNT((1U << 31)|0);
	}

	/* fill segment list */
	while (nr_segs--) {
		if (unlikely(segs[nr_segs].ds_len == 0)) {
			dseg--;
			dseg->addr = 0;
			dseg->lkey = 0;
			wmb();
			dseg->byte_count = SET_BYTE_COUNT((1U << 31)|0);
		} else {
			dseg--;
			dseg->addr = cpu_to_be64((uint64_t)segs[nr_segs].ds_addr);
			dseg->lkey = cpu_to_be32(priv->mdev->mr.key);
			wmb();
			dseg->byte_count = SET_BYTE_COUNT((uint32_t)segs[nr_segs].ds_len);
		}
	}

	wmb();

	/* write owner bits in reverse order */
	if ((opcode & cpu_to_be32(0x1F)) == cpu_to_be32(MLX4_OPCODE_LSO))
		mlx4_en_store_inline_lso_header(dseg_inline, ihs, owner_bit);
	else
		mlx4_en_store_inline_header(dseg_inline, ihs, owner_bit);

	/* update producer counter */
	ring->prod += tx_info->nr_txbb;

	if (ring->bf_enabled && bf_size <= MAX_BF &&
	    (tx_desc->ctrl.ins_vlan != MLX4_WQE_CTRL_INS_CVLAN)) {

		/* store doorbell number */
		*(volatile __be32 *) (&tx_desc->ctrl.vlan_tag) |= cpu_to_be32(ring->doorbell_qpn);

		/* or in producer number for this WQE */
		opcode |= cpu_to_be32((bf_prod & 0xffff) << 8);

		/*
		 * Ensure the new descriptor hits memory before
		 * setting ownership of this descriptor to HW:
		 */
		wmb();
		tx_desc->ctrl.owner_opcode = opcode;
		wmb();
		mlx4_bf_copy(((u8 *)ring->bf.reg) + ring->bf.offset,
		     (volatile unsigned long *) &tx_desc->ctrl, bf_size);
		wmb();
		ring->bf.offset ^= ring->bf.buf_size;
	} else {
		/*
		 * Ensure the new descriptor hits memory before
		 * setting ownership of this descriptor to HW:
		 */
		wmb();
		tx_desc->ctrl.owner_opcode = opcode;
		wmb();
		writel(cpu_to_be32(ring->doorbell_qpn),
		    ((u8 *)ring->bf.uar->map) + MLX4_SEND_DOORBELL);
	}

	return (0);
tx_drop:
	*mbp = NULL;
	m_freem(mb);
	return (err);
}

static int
mlx4_en_transmit_locked(struct ifnet *ifp, int tx_ind, struct mbuf *mb)
{
	struct mlx4_en_priv *priv = netdev_priv(ifp);
	struct mlx4_en_tx_ring *ring = priv->tx_ring[tx_ind];
	int err = 0;

	if (unlikely((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0 ||
	    READ_ONCE(priv->port_up) == 0)) {
		m_freem(mb);
		return (ENETDOWN);
	}

	if (mlx4_en_xmit(priv, tx_ind, &mb) != 0) {
		/* NOTE: m_freem() is NULL safe */
		m_freem(mb);
		err = ENOBUFS;
		if (ring->watchdog_time == 0)
			ring->watchdog_time = ticks + MLX4_EN_WATCHDOG_TIMEOUT;
	} else {
		ring->watchdog_time = 0;
	}
	return (err);
}

int
mlx4_en_transmit(struct ifnet *dev, struct mbuf *m)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_tx_ring *ring;
	int i, err = 0;

	if (priv->port_up == 0) {
		m_freem(m);
		return (ENETDOWN);
	}

	/* Compute which queue to use */
	if (M_HASHTYPE_GET(m) != M_HASHTYPE_NONE) {
		i = (m->m_pkthdr.flowid % 128) % priv->tx_ring_num;
	}
	else {
		i = mlx4_en_select_queue(dev, m);
	}

	ring = priv->tx_ring[i];

	spin_lock(&ring->tx_lock);

	err = mlx4_en_transmit_locked(dev, i, m);
	spin_unlock(&ring->tx_lock);

	/* Poll CQ here */
	mlx4_en_xmit_poll(priv, i);

#if __FreeBSD_version >= 1100000
	if (unlikely(err != 0))
		if_inc_counter(dev, IFCOUNTER_IQDROPS, 1);
#endif
	return (err);
}

/*
 * Flush ring buffers.
 */
void
mlx4_en_qflush(struct ifnet *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);

	if (priv->port_up == 0)
		return;

	if_qflush(dev);
}
