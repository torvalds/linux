/* SPDX-License-Identifier: GPL-2.0-only */
/* include/net/xdp.h
 *
 * Copyright (c) 2017 Jesper Dangaard Brouer, Red Hat Inc.
 */
#ifndef __LINUX_NET_XDP_H__
#define __LINUX_NET_XDP_H__

#include <linux/skbuff.h> /* skb_shared_info */

/**
 * DOC: XDP RX-queue information
 *
 * The XDP RX-queue info (xdp_rxq_info) is associated with the driver
 * level RX-ring queues.  It is information that is specific to how
 * the driver have configured a given RX-ring queue.
 *
 * Each xdp_buff frame received in the driver carry a (pointer)
 * reference to this xdp_rxq_info structure.  This provides the XDP
 * data-path read-access to RX-info for both kernel and bpf-side
 * (limited subset).
 *
 * For now, direct access is only safe while running in NAPI/softirq
 * context.  Contents is read-mostly and must not be updated during
 * driver NAPI/softirq poll.
 *
 * The driver usage API is a register and unregister API.
 *
 * The struct is not directly tied to the XDP prog.  A new XDP prog
 * can be attached as long as it doesn't change the underlying
 * RX-ring.  If the RX-ring does change significantly, the NIC driver
 * naturally need to stop the RX-ring before purging and reallocating
 * memory.  In that process the driver MUST call unregistor (which
 * also apply for driver shutdown and unload).  The register API is
 * also mandatory during RX-ring setup.
 */

enum xdp_mem_type {
	MEM_TYPE_PAGE_SHARED = 0, /* Split-page refcnt based model */
	MEM_TYPE_PAGE_ORDER0,     /* Orig XDP full page model */
	MEM_TYPE_PAGE_POOL,
	MEM_TYPE_XSK_BUFF_POOL,
	MEM_TYPE_MAX,
};

/* XDP flags for ndo_xdp_xmit */
#define XDP_XMIT_FLUSH		(1U << 0)	/* doorbell signal consumer */
#define XDP_XMIT_FLAGS_MASK	XDP_XMIT_FLUSH

struct xdp_mem_info {
	u32 type; /* enum xdp_mem_type, but known size type */
	u32 id;
};

struct page_pool;

struct xdp_rxq_info {
	struct net_device *dev;
	u32 queue_index;
	u32 reg_state;
	struct xdp_mem_info mem;
} ____cacheline_aligned; /* perf critical, avoid false-sharing */

struct xdp_txq_info {
	struct net_device *dev;
};

struct xdp_buff {
	void *data;
	void *data_end;
	void *data_meta;
	void *data_hard_start;
	struct xdp_rxq_info *rxq;
	struct xdp_txq_info *txq;
	u32 frame_sz; /* frame size to deduce data_hard_end/reserved tailroom*/
};

/* Reserve memory area at end-of data area.
 *
 * This macro reserves tailroom in the XDP buffer by limiting the
 * XDP/BPF data access to data_hard_end.  Notice same area (and size)
 * is used for XDP_PASS, when constructing the SKB via build_skb().
 */
#define xdp_data_hard_end(xdp)				\
	((xdp)->data_hard_start + (xdp)->frame_sz -	\
	 SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))

static inline struct skb_shared_info *
xdp_get_shared_info_from_buff(struct xdp_buff *xdp)
{
	return (struct skb_shared_info *)xdp_data_hard_end(xdp);
}

struct xdp_frame {
	void *data;
	u16 len;
	u16 headroom;
	u32 metasize:8;
	u32 frame_sz:24;
	/* Lifetime of xdp_rxq_info is limited to NAPI/enqueue time,
	 * while mem info is valid on remote CPU.
	 */
	struct xdp_mem_info mem;
	struct net_device *dev_rx; /* used by cpumap */
};


static inline struct skb_shared_info *
xdp_get_shared_info_from_frame(struct xdp_frame *frame)
{
	void *data_hard_start = frame->data - frame->headroom - sizeof(*frame);

	return (struct skb_shared_info *)(data_hard_start + frame->frame_sz -
				SKB_DATA_ALIGN(sizeof(struct skb_shared_info)));
}

struct xdp_cpumap_stats {
	unsigned int redirect;
	unsigned int pass;
	unsigned int drop;
};

/* Clear kernel pointers in xdp_frame */
static inline void xdp_scrub_frame(struct xdp_frame *frame)
{
	frame->data = NULL;
	frame->dev_rx = NULL;
}

/* Avoids inlining WARN macro in fast-path */
void xdp_warn(const char *msg, const char *func, const int line);
#define XDP_WARN(msg) xdp_warn(msg, __func__, __LINE__)

struct xdp_frame *xdp_convert_zc_to_xdp_frame(struct xdp_buff *xdp);

static inline
void xdp_convert_frame_to_buff(struct xdp_frame *frame, struct xdp_buff *xdp)
{
	xdp->data_hard_start = frame->data - frame->headroom - sizeof(*frame);
	xdp->data = frame->data;
	xdp->data_end = frame->data + frame->len;
	xdp->data_meta = frame->data - frame->metasize;
	xdp->frame_sz = frame->frame_sz;
}

static inline
int xdp_update_frame_from_buff(struct xdp_buff *xdp,
			       struct xdp_frame *xdp_frame)
{
	int metasize, headroom;

	/* Assure headroom is available for storing info */
	headroom = xdp->data - xdp->data_hard_start;
	metasize = xdp->data - xdp->data_meta;
	metasize = metasize > 0 ? metasize : 0;
	if (unlikely((headroom - metasize) < sizeof(*xdp_frame)))
		return -ENOSPC;

	/* Catch if driver didn't reserve tailroom for skb_shared_info */
	if (unlikely(xdp->data_end > xdp_data_hard_end(xdp))) {
		XDP_WARN("Driver BUG: missing reserved tailroom");
		return -ENOSPC;
	}

	xdp_frame->data = xdp->data;
	xdp_frame->len  = xdp->data_end - xdp->data;
	xdp_frame->headroom = headroom - sizeof(*xdp_frame);
	xdp_frame->metasize = metasize;
	xdp_frame->frame_sz = xdp->frame_sz;

	return 0;
}

/* Convert xdp_buff to xdp_frame */
static inline
struct xdp_frame *xdp_convert_buff_to_frame(struct xdp_buff *xdp)
{
	struct xdp_frame *xdp_frame;

	if (xdp->rxq->mem.type == MEM_TYPE_XSK_BUFF_POOL)
		return xdp_convert_zc_to_xdp_frame(xdp);

	/* Store info in top of packet */
	xdp_frame = xdp->data_hard_start;
	if (unlikely(xdp_update_frame_from_buff(xdp, xdp_frame) < 0))
		return NULL;

	/* rxq only valid until napi_schedule ends, convert to xdp_mem_info */
	xdp_frame->mem = xdp->rxq->mem;

	return xdp_frame;
}

void xdp_return_frame(struct xdp_frame *xdpf);
void xdp_return_frame_rx_napi(struct xdp_frame *xdpf);
void xdp_return_buff(struct xdp_buff *xdp);

/* When sending xdp_frame into the network stack, then there is no
 * return point callback, which is needed to release e.g. DMA-mapping
 * resources with page_pool.  Thus, have explicit function to release
 * frame resources.
 */
void __xdp_release_frame(void *data, struct xdp_mem_info *mem);
static inline void xdp_release_frame(struct xdp_frame *xdpf)
{
	struct xdp_mem_info *mem = &xdpf->mem;

	/* Curr only page_pool needs this */
	if (mem->type == MEM_TYPE_PAGE_POOL)
		__xdp_release_frame(xdpf->data, mem);
}

int xdp_rxq_info_reg(struct xdp_rxq_info *xdp_rxq,
		     struct net_device *dev, u32 queue_index);
void xdp_rxq_info_unreg(struct xdp_rxq_info *xdp_rxq);
void xdp_rxq_info_unused(struct xdp_rxq_info *xdp_rxq);
bool xdp_rxq_info_is_reg(struct xdp_rxq_info *xdp_rxq);
int xdp_rxq_info_reg_mem_model(struct xdp_rxq_info *xdp_rxq,
			       enum xdp_mem_type type, void *allocator);
void xdp_rxq_info_unreg_mem_model(struct xdp_rxq_info *xdp_rxq);

/* Drivers not supporting XDP metadata can use this helper, which
 * rejects any room expansion for metadata as a result.
 */
static __always_inline void
xdp_set_data_meta_invalid(struct xdp_buff *xdp)
{
	xdp->data_meta = xdp->data + 1;
}

static __always_inline bool
xdp_data_meta_unsupported(const struct xdp_buff *xdp)
{
	return unlikely(xdp->data_meta > xdp->data);
}

struct xdp_attachment_info {
	struct bpf_prog *prog;
	u32 flags;
};

struct netdev_bpf;
void xdp_attachment_setup(struct xdp_attachment_info *info,
			  struct netdev_bpf *bpf);

#define DEV_MAP_BULK_SIZE 16

#endif /* __LINUX_NET_XDP_H__ */
