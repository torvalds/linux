/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2024 Intel Corporation */

#ifndef __LIBETH_RX_H
#define __LIBETH_RX_H

#include <linux/if_vlan.h>

#include <net/page_pool/helpers.h>
#include <net/xdp.h>

/* Rx buffer management */

/* Space reserved in front of each frame */
#define LIBETH_SKB_HEADROOM	(NET_SKB_PAD + NET_IP_ALIGN)
/* Maximum headroom for worst-case calculations */
#define LIBETH_MAX_HEADROOM	LIBETH_SKB_HEADROOM
/* Link layer / L2 overhead: Ethernet, 2 VLAN tags (C + S), FCS */
#define LIBETH_RX_LL_LEN	(ETH_HLEN + 2 * VLAN_HLEN + ETH_FCS_LEN)
/* Maximum supported L2-L4 header length */
#define LIBETH_MAX_HEAD		roundup_pow_of_two(max(MAX_HEADER, 256))

/* Always use order-0 pages */
#define LIBETH_RX_PAGE_ORDER	0
/* Pick a sane buffer stride and align to a cacheline boundary */
#define LIBETH_RX_BUF_STRIDE	SKB_DATA_ALIGN(128)
/* HW-writeable space in one buffer: truesize - headroom/tailroom, aligned */
#define LIBETH_RX_PAGE_LEN(hr)						  \
	ALIGN_DOWN(SKB_MAX_ORDER(hr, LIBETH_RX_PAGE_ORDER),		  \
		   LIBETH_RX_BUF_STRIDE)

/**
 * struct libeth_fqe - structure representing an Rx buffer (fill queue element)
 * @page: page holding the buffer
 * @offset: offset from the page start (to the headroom)
 * @truesize: total space occupied by the buffer (w/ headroom and tailroom)
 *
 * Depending on the MTU, API switches between one-page-per-frame and shared
 * page model (to conserve memory on bigger-page platforms). In case of the
 * former, @offset is always 0 and @truesize is always ```PAGE_SIZE```.
 */
struct libeth_fqe {
	struct page		*page;
	u32			offset;
	u32			truesize;
} __aligned_largest;

/**
 * enum libeth_fqe_type - enum representing types of Rx buffers
 * @LIBETH_FQE_MTU: buffer size is determined by MTU
 * @LIBETH_FQE_SHORT: buffer size is smaller than MTU, for short frames
 * @LIBETH_FQE_HDR: buffer size is ```LIBETH_MAX_HEAD```-sized, for headers
 */
enum libeth_fqe_type {
	LIBETH_FQE_MTU		= 0U,
	LIBETH_FQE_SHORT,
	LIBETH_FQE_HDR,
};

/**
 * struct libeth_fq - structure representing a buffer (fill) queue
 * @fp: hotpath part of the structure
 * @pp: &page_pool for buffer management
 * @fqes: array of Rx buffers
 * @truesize: size to allocate per buffer, w/overhead
 * @count: number of descriptors/buffers the queue has
 * @type: type of the buffers this queue has
 * @hsplit: flag whether header split is enabled
 * @buf_len: HW-writeable length per each buffer
 * @nid: ID of the closest NUMA node with memory
 */
struct libeth_fq {
	struct_group_tagged(libeth_fq_fp, fp,
		struct page_pool	*pp;
		struct libeth_fqe	*fqes;

		u32			truesize;
		u32			count;
	);

	/* Cold fields */
	enum libeth_fqe_type	type:2;
	bool			hsplit:1;

	u32			buf_len;
	int			nid;
};

int libeth_rx_fq_create(struct libeth_fq *fq, struct napi_struct *napi);
void libeth_rx_fq_destroy(struct libeth_fq *fq);

/**
 * libeth_rx_alloc - allocate a new Rx buffer
 * @fq: fill queue to allocate for
 * @i: index of the buffer within the queue
 *
 * Return: DMA address to be passed to HW for Rx on successful allocation,
 * ```DMA_MAPPING_ERROR``` otherwise.
 */
static inline dma_addr_t libeth_rx_alloc(const struct libeth_fq_fp *fq, u32 i)
{
	struct libeth_fqe *buf = &fq->fqes[i];

	buf->truesize = fq->truesize;
	buf->page = page_pool_dev_alloc(fq->pp, &buf->offset, &buf->truesize);
	if (unlikely(!buf->page))
		return DMA_MAPPING_ERROR;

	return page_pool_get_dma_addr(buf->page) + buf->offset +
	       fq->pp->p.offset;
}

void libeth_rx_recycle_slow(struct page *page);

/**
 * libeth_rx_sync_for_cpu - synchronize or recycle buffer post DMA
 * @fqe: buffer to process
 * @len: frame length from the descriptor
 *
 * Process the buffer after it's written by HW. The regular path is to
 * synchronize DMA for CPU, but in case of no data it will be immediately
 * recycled back to its PP.
 *
 * Return: true when there's data to process, false otherwise.
 */
static inline bool libeth_rx_sync_for_cpu(const struct libeth_fqe *fqe,
					  u32 len)
{
	struct page *page = fqe->page;

	/* Very rare, but possible case. The most common reason:
	 * the last fragment contained FCS only, which was then
	 * stripped by the HW.
	 */
	if (unlikely(!len)) {
		libeth_rx_recycle_slow(page);
		return false;
	}

	page_pool_dma_sync_for_cpu(page->pp, page, fqe->offset, len);

	return true;
}

/* Converting abstract packet type numbers into a software structure with
 * the packet parameters to do O(1) lookup on Rx.
 */

enum {
	LIBETH_RX_PT_OUTER_L2			= 0U,
	LIBETH_RX_PT_OUTER_IPV4,
	LIBETH_RX_PT_OUTER_IPV6,
};

enum {
	LIBETH_RX_PT_NOT_FRAG			= 0U,
	LIBETH_RX_PT_FRAG,
};

enum {
	LIBETH_RX_PT_TUNNEL_IP_NONE		= 0U,
	LIBETH_RX_PT_TUNNEL_IP_IP,
	LIBETH_RX_PT_TUNNEL_IP_GRENAT,
	LIBETH_RX_PT_TUNNEL_IP_GRENAT_MAC,
	LIBETH_RX_PT_TUNNEL_IP_GRENAT_MAC_VLAN,
};

enum {
	LIBETH_RX_PT_TUNNEL_END_NONE		= 0U,
	LIBETH_RX_PT_TUNNEL_END_IPV4,
	LIBETH_RX_PT_TUNNEL_END_IPV6,
};

enum {
	LIBETH_RX_PT_INNER_NONE			= 0U,
	LIBETH_RX_PT_INNER_UDP,
	LIBETH_RX_PT_INNER_TCP,
	LIBETH_RX_PT_INNER_SCTP,
	LIBETH_RX_PT_INNER_ICMP,
	LIBETH_RX_PT_INNER_TIMESYNC,
};

#define LIBETH_RX_PT_PAYLOAD_NONE		PKT_HASH_TYPE_NONE
#define LIBETH_RX_PT_PAYLOAD_L2			PKT_HASH_TYPE_L2
#define LIBETH_RX_PT_PAYLOAD_L3			PKT_HASH_TYPE_L3
#define LIBETH_RX_PT_PAYLOAD_L4			PKT_HASH_TYPE_L4

struct libeth_rx_pt {
	u32					outer_ip:2;
	u32					outer_frag:1;
	u32					tunnel_type:3;
	u32					tunnel_end_prot:2;
	u32					tunnel_end_frag:1;
	u32					inner_prot:3;
	enum pkt_hash_types			payload_layer:2;

	u32					pad:2;
	enum xdp_rss_hash_type			hash_type:16;
};

void libeth_rx_pt_gen_hash_type(struct libeth_rx_pt *pt);

/**
 * libeth_rx_pt_get_ip_ver - get IP version from a packet type structure
 * @pt: packet type params
 *
 * Wrapper to compile out the IPv6 code from the drivers when not supported
 * by the kernel.
 *
 * Return: @pt.outer_ip or stub for IPv6 when not compiled-in.
 */
static inline u32 libeth_rx_pt_get_ip_ver(struct libeth_rx_pt pt)
{
#if !IS_ENABLED(CONFIG_IPV6)
	switch (pt.outer_ip) {
	case LIBETH_RX_PT_OUTER_IPV4:
		return LIBETH_RX_PT_OUTER_IPV4;
	default:
		return LIBETH_RX_PT_OUTER_L2;
	}
#else
	return pt.outer_ip;
#endif
}

/* libeth_has_*() can be used to quickly check whether the HW metadata is
 * available to avoid further expensive processing such as descriptor reads.
 * They already check for the corresponding netdev feature to be enabled,
 * thus can be used as drop-in replacements.
 */

static inline bool libeth_rx_pt_has_checksum(const struct net_device *dev,
					     struct libeth_rx_pt pt)
{
	/* Non-zero _INNER* is only possible when _OUTER_IPV* is set,
	 * it is enough to check only for the L4 type.
	 */
	return likely(pt.inner_prot > LIBETH_RX_PT_INNER_NONE &&
		      (dev->features & NETIF_F_RXCSUM));
}

static inline bool libeth_rx_pt_has_hash(const struct net_device *dev,
					 struct libeth_rx_pt pt)
{
	return likely(pt.payload_layer > LIBETH_RX_PT_PAYLOAD_NONE &&
		      (dev->features & NETIF_F_RXHASH));
}

/**
 * libeth_rx_pt_set_hash - fill in skb hash value basing on the PT
 * @skb: skb to fill the hash in
 * @hash: 32-bit hash value from the descriptor
 * @pt: packet type
 */
static inline void libeth_rx_pt_set_hash(struct sk_buff *skb, u32 hash,
					 struct libeth_rx_pt pt)
{
	skb_set_hash(skb, hash, pt.payload_layer);
}

#endif /* __LIBETH_RX_H */
