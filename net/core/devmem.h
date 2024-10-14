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

struct netlink_ext_ack;

struct net_devmem_dmabuf_binding {
	struct dma_buf *dmabuf;
	struct dma_buf_attachment *attachment;
	struct sg_table *sgt;
	struct net_device *dev;
	struct gen_pool *chunk_pool;

	/* The user holds a ref (via the netlink API) for as long as they want
	 * the binding to remain alive. Each page pool using this binding holds
	 * a ref to keep the binding alive. Each allocated net_iov holds a
	 * ref.
	 *
	 * The binding undos itself and unmaps the underlying dmabuf once all
	 * those refs are dropped and the binding is no longer desired or in
	 * use.
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
};

#if defined(CONFIG_NET_DEVMEM)
/* Owner of the dma-buf chunks inserted into the gen pool. Each scatterlist
 * entry from the dmabuf is inserted into the genpool as a chunk, and needs
 * this owner struct to keep track of some metadata necessary to create
 * allocations from this chunk.
 */
struct dmabuf_genpool_chunk_owner {
	/* Offset into the dma-buf where this chunk starts.  */
	unsigned long base_virtual;

	/* dma_addr of the start of the chunk.  */
	dma_addr_t base_dma_addr;

	/* Array of net_iovs for this chunk. */
	struct net_iov *niovs;
	size_t num_niovs;

	struct net_devmem_dmabuf_binding *binding;
};

void __net_devmem_dmabuf_binding_free(struct net_devmem_dmabuf_binding *binding);
struct net_devmem_dmabuf_binding *
net_devmem_bind_dmabuf(struct net_device *dev, unsigned int dmabuf_fd,
		       struct netlink_ext_ack *extack);
void net_devmem_unbind_dmabuf(struct net_devmem_dmabuf_binding *binding);
int net_devmem_bind_dmabuf_to_queue(struct net_device *dev, u32 rxq_idx,
				    struct net_devmem_dmabuf_binding *binding,
				    struct netlink_ext_ack *extack);
void dev_dmabuf_uninstall(struct net_device *dev);

static inline struct dmabuf_genpool_chunk_owner *
net_iov_owner(const struct net_iov *niov)
{
	return niov->owner;
}

static inline unsigned int net_iov_idx(const struct net_iov *niov)
{
	return niov - net_iov_owner(niov)->niovs;
}

static inline struct net_devmem_dmabuf_binding *
net_iov_binding(const struct net_iov *niov)
{
	return net_iov_owner(niov)->binding;
}

static inline unsigned long net_iov_virtual_addr(const struct net_iov *niov)
{
	struct dmabuf_genpool_chunk_owner *owner = net_iov_owner(niov);

	return owner->base_virtual +
	       ((unsigned long)net_iov_idx(niov) << PAGE_SHIFT);
}

static inline u32 net_iov_binding_id(const struct net_iov *niov)
{
	return net_iov_owner(niov)->binding->id;
}

static inline void
net_devmem_dmabuf_binding_get(struct net_devmem_dmabuf_binding *binding)
{
	refcount_inc(&binding->ref);
}

static inline void
net_devmem_dmabuf_binding_put(struct net_devmem_dmabuf_binding *binding)
{
	if (!refcount_dec_and_test(&binding->ref))
		return;

	__net_devmem_dmabuf_binding_free(binding);
}

struct net_iov *
net_devmem_alloc_dmabuf(struct net_devmem_dmabuf_binding *binding);
void net_devmem_free_dmabuf(struct net_iov *ppiov);

#else
struct net_devmem_dmabuf_binding;

static inline void
__net_devmem_dmabuf_binding_free(struct net_devmem_dmabuf_binding *binding)
{
}

static inline struct net_devmem_dmabuf_binding *
net_devmem_bind_dmabuf(struct net_device *dev, unsigned int dmabuf_fd,
		       struct netlink_ext_ack *extack)
{
	return ERR_PTR(-EOPNOTSUPP);
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

static inline void dev_dmabuf_uninstall(struct net_device *dev)
{
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

static inline u32 net_iov_binding_id(const struct net_iov *niov)
{
	return 0;
}
#endif

#endif /* _NET_DEVMEM_H */
