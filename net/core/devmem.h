/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Device memory TCP support
 *
 * Authors:	Mina Almasry <almasrymina@google.com>
 *		Willem de Bruijn <willemb@google.com>
 *		Kaiyuan Zhang <kaiyuanz@google.com>
 *
 */
#ifndef _NET_DEVMEM_H
#define _NET_DEVMEM_H

#include <net/netmem.h>
#include <net/netdev_netlink.h>

struct netlink_ext_ack;

struct net_devmem_dmabuf_binding {
	struct dma_buf *dmabuf;
	struct dma_buf_attachment *attachment;
	struct sg_table *sgt;
	struct net_device *dev;
	struct gen_pool *chunk_pool;
	/* Protect dev */
	struct mutex lock;

	/* The user holds a ref (via the netlink API) for as long as they want
	 * the binding to remain alive. Each page pool using this binding holds
	 * a ref to keep the binding alive. The page_pool does not release the
	 * ref until all the net_iovs allocated from this binding are released
	 * back to the page_pool.
	 *
	 * The binding undos itself and unmaps the underlying dmabuf once all
	 * those refs are dropped and the binding is no longer desired or in
	 * use.
	 *
	 * net_devmem_get_net_iov() on dmabuf net_iovs will increment this
	 * reference, making sure that the binding remains alive until all the
	 * net_iovs are no longer used. net_iovs allocated from this binding
	 * that are stuck in the TX path for any reason (such as awaiting
	 * retransmits) hold a reference to the binding until the skb holding
	 * them is freed.
	 */
	refcount_t ref;

	/* The list of bindings currently active. Used for netlink to notify us
	 * of the user dropping the bind.
	 */
	struct list_head list;

	/* rxq's this binding is active on. */
	struct xarray bound_rxqs;

	/* ID of this binding. Globally unique to all bindings currently
	 * active.
	 */
	u32 id;

	/* DMA direction, FROM_DEVICE for Rx binding, TO_DEVICE for Tx. */
	enum dma_data_direction direction;

	/* Array of net_iov pointers for this binding, sorted by virtual
	 * address. This array is convenient to map the virtual addresses to
	 * net_iovs in the TX path.
	 */
	struct net_iov **tx_vec;

	struct work_struct unbind_w;
};

#if defined(CONFIG_NET_DEVMEM)
/* Owner of the dma-buf chunks inserted into the gen pool. Each scatterlist
 * entry from the dmabuf is inserted into the genpool as a chunk, and needs
 * this owner struct to keep track of some metadata necessary to create
 * allocations from this chunk.
 */
struct dmabuf_genpool_chunk_owner {
	struct net_iov_area area;
	struct net_devmem_dmabuf_binding *binding;

	/* dma_addr of the start of the chunk.  */
	dma_addr_t base_dma_addr;
};

void __net_devmem_dmabuf_binding_free(struct work_struct *wq);
struct net_devmem_dmabuf_binding *
net_devmem_bind_dmabuf(struct net_device *dev,
		       struct device *dma_dev,
		       enum dma_data_direction direction,
		       unsigned int dmabuf_fd, struct netdev_nl_sock *priv,
		       struct netlink_ext_ack *extack);
struct net_devmem_dmabuf_binding *net_devmem_lookup_dmabuf(u32 id);
void net_devmem_unbind_dmabuf(struct net_devmem_dmabuf_binding *binding);
int net_devmem_bind_dmabuf_to_queue(struct net_device *dev, u32 rxq_idx,
				    struct net_devmem_dmabuf_binding *binding,
				    struct netlink_ext_ack *extack);
void net_devmem_bind_tx_release(struct sock *sk);

static inline struct dmabuf_genpool_chunk_owner *
net_devmem_iov_to_chunk_owner(const struct net_iov *niov)
{
	struct net_iov_area *owner = net_iov_owner(niov);

	return container_of(owner, struct dmabuf_genpool_chunk_owner, area);
}

static inline struct net_devmem_dmabuf_binding *
net_devmem_iov_binding(const struct net_iov *niov)
{
	return net_devmem_iov_to_chunk_owner(niov)->binding;
}

static inline u32 net_devmem_iov_binding_id(const struct net_iov *niov)
{
	return net_devmem_iov_binding(niov)->id;
}

static inline unsigned long net_iov_virtual_addr(const struct net_iov *niov)
{
	struct net_iov_area *owner = net_iov_owner(niov);

	return owner->base_virtual +
	       ((unsigned long)net_iov_idx(niov) << PAGE_SHIFT);
}

static inline bool
net_devmem_dmabuf_binding_get(struct net_devmem_dmabuf_binding *binding)
{
	return refcount_inc_not_zero(&binding->ref);
}

static inline void
net_devmem_dmabuf_binding_put(struct net_devmem_dmabuf_binding *binding)
{
	if (!refcount_dec_and_test(&binding->ref))
		return;

	INIT_WORK(&binding->unbind_w, __net_devmem_dmabuf_binding_free);
	schedule_work(&binding->unbind_w);
}

void net_devmem_get_net_iov(struct net_iov *niov);
void net_devmem_put_net_iov(struct net_iov *niov);

struct net_iov *
net_devmem_alloc_dmabuf(struct net_devmem_dmabuf_binding *binding);
void net_devmem_free_dmabuf(struct net_iov *ppiov);

bool net_is_devmem_iov(struct net_iov *niov);
struct net_devmem_dmabuf_binding *
net_devmem_get_binding(struct sock *sk, unsigned int dmabuf_id);
struct net_iov *
net_devmem_get_niov_at(struct net_devmem_dmabuf_binding *binding, size_t addr,
		       size_t *off, size_t *size);

#else
struct net_devmem_dmabuf_binding;

static inline void
net_devmem_dmabuf_binding_put(struct net_devmem_dmabuf_binding *binding)
{
}

static inline void net_devmem_get_net_iov(struct net_iov *niov)
{
}

static inline void net_devmem_put_net_iov(struct net_iov *niov)
{
}

static inline struct net_devmem_dmabuf_binding *
net_devmem_bind_dmabuf(struct net_device *dev,
		       struct device *dma_dev,
		       enum dma_data_direction direction,
		       unsigned int dmabuf_fd,
		       struct netdev_nl_sock *priv,
		       struct netlink_ext_ack *extack)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline struct net_devmem_dmabuf_binding *net_devmem_lookup_dmabuf(u32 id)
{
	return NULL;
}

static inline void
net_devmem_unbind_dmabuf(struct net_devmem_dmabuf_binding *binding)
{
}

static inline int
net_devmem_bind_dmabuf_to_queue(struct net_device *dev, u32 rxq_idx,
				struct net_devmem_dmabuf_binding *binding,
				struct netlink_ext_ack *extack)

{
	return -EOPNOTSUPP;
}

static inline struct net_iov *
net_devmem_alloc_dmabuf(struct net_devmem_dmabuf_binding *binding)
{
	return NULL;
}

static inline void net_devmem_free_dmabuf(struct net_iov *ppiov)
{
}

static inline unsigned long net_iov_virtual_addr(const struct net_iov *niov)
{
	return 0;
}

static inline u32 net_devmem_iov_binding_id(const struct net_iov *niov)
{
	return 0;
}

static inline bool net_is_devmem_iov(struct net_iov *niov)
{
	return false;
}

static inline struct net_devmem_dmabuf_binding *
net_devmem_get_binding(struct sock *sk, unsigned int dmabuf_id)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline struct net_iov *
net_devmem_get_niov_at(struct net_devmem_dmabuf_binding *binding, size_t addr,
		       size_t *off, size_t *size)
{
	return NULL;
}

static inline struct net_devmem_dmabuf_binding *
net_devmem_iov_binding(const struct net_iov *niov)
{
	return NULL;
}
#endif

#endif /* _NET_DEVMEM_H */
