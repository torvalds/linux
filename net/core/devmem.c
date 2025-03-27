// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *      Devmem TCP
 *
 *      Authors:	Mina Almasry <almasrymina@google.com>
 *			Willem de Bruijn <willemdebruijn.kernel@gmail.com>
 *			Kaiyuan Zhang <kaiyuanz@google.com
 */

#include <linux/dma-buf.h>
#include <linux/ethtool_netlink.h>
#include <linux/genalloc.h>
#include <linux/mm.h>
#include <linux/netdevice.h>
#include <linux/types.h>
#include <net/netdev_queues.h>
#include <net/netdev_rx_queue.h>
#include <net/page_pool/helpers.h>
#include <net/page_pool/memory_provider.h>
#include <trace/events/page_pool.h>

#include "devmem.h"
#include "mp_dmabuf_devmem.h"
#include "page_pool_priv.h"

/* Device memory support */

static DEFINE_XARRAY_FLAGS(net_devmem_dmabuf_bindings, XA_FLAGS_ALLOC1);

static const struct memory_provider_ops dmabuf_devmem_ops;

bool net_is_devmem_iov(struct net_iov *niov)
{
	return niov->pp->mp_ops == &dmabuf_devmem_ops;
}

static void net_devmem_dmabuf_free_chunk_owner(struct gen_pool *genpool,
					       struct gen_pool_chunk *chunk,
					       void *not_used)
{
	struct dmabuf_genpool_chunk_owner *owner = chunk->owner;

	kvfree(owner->area.niovs);
	kfree(owner);
}

static dma_addr_t net_devmem_get_dma_addr(const struct net_iov *niov)
{
	struct dmabuf_genpool_chunk_owner *owner;

	owner = net_devmem_iov_to_chunk_owner(niov);
	return owner->base_dma_addr +
	       ((dma_addr_t)net_iov_idx(niov) << PAGE_SHIFT);
}

void __net_devmem_dmabuf_binding_free(struct net_devmem_dmabuf_binding *binding)
{
	size_t size, avail;

	gen_pool_for_each_chunk(binding->chunk_pool,
				net_devmem_dmabuf_free_chunk_owner, NULL);

	size = gen_pool_size(binding->chunk_pool);
	avail = gen_pool_avail(binding->chunk_pool);

	if (!WARN(size != avail, "can't destroy genpool. size=%zu, avail=%zu",
		  size, avail))
		gen_pool_destroy(binding->chunk_pool);

	dma_buf_unmap_attachment_unlocked(binding->attachment, binding->sgt,
					  DMA_FROM_DEVICE);
	dma_buf_detach(binding->dmabuf, binding->attachment);
	dma_buf_put(binding->dmabuf);
	xa_destroy(&binding->bound_rxqs);
	kfree(binding);
}

struct net_iov *
net_devmem_alloc_dmabuf(struct net_devmem_dmabuf_binding *binding)
{
	struct dmabuf_genpool_chunk_owner *owner;
	unsigned long dma_addr;
	struct net_iov *niov;
	ssize_t offset;
	ssize_t index;

	dma_addr = gen_pool_alloc_owner(binding->chunk_pool, PAGE_SIZE,
					(void **)&owner);
	if (!dma_addr)
		return NULL;

	offset = dma_addr - owner->base_dma_addr;
	index = offset / PAGE_SIZE;
	niov = &owner->area.niovs[index];

	niov->pp_magic = 0;
	niov->pp = NULL;
	atomic_long_set(&niov->pp_ref_count, 0);

	return niov;
}

void net_devmem_free_dmabuf(struct net_iov *niov)
{
	struct net_devmem_dmabuf_binding *binding = net_devmem_iov_binding(niov);
	unsigned long dma_addr = net_devmem_get_dma_addr(niov);

	if (WARN_ON(!gen_pool_has_addr(binding->chunk_pool, dma_addr,
				       PAGE_SIZE)))
		return;

	gen_pool_free(binding->chunk_pool, dma_addr, PAGE_SIZE);
}

void net_devmem_unbind_dmabuf(struct net_devmem_dmabuf_binding *binding)
{
	struct netdev_rx_queue *rxq;
	unsigned long xa_idx;
	unsigned int rxq_idx;
	int err;

	if (binding->list.next)
		list_del(&binding->list);

	xa_for_each(&binding->bound_rxqs, xa_idx, rxq) {
		WARN_ON(rxq->mp_params.mp_priv != binding);

		rxq->mp_params.mp_priv = NULL;
		rxq->mp_params.mp_ops = NULL;

		rxq_idx = get_netdev_rx_queue_index(rxq);

		err = netdev_rx_queue_restart(binding->dev, rxq_idx);
		WARN_ON(err && err != -ENETDOWN);
	}

	xa_erase(&net_devmem_dmabuf_bindings, binding->id);

	net_devmem_dmabuf_binding_put(binding);
}

int net_devmem_bind_dmabuf_to_queue(struct net_device *dev, u32 rxq_idx,
				    struct net_devmem_dmabuf_binding *binding,
				    struct netlink_ext_ack *extack)
{
	struct netdev_rx_queue *rxq;
	u32 xa_idx;
	int err;

	if (rxq_idx >= dev->real_num_rx_queues) {
		NL_SET_ERR_MSG(extack, "rx queue index out of range");
		return -ERANGE;
	}

	if (dev->cfg->hds_config != ETHTOOL_TCP_DATA_SPLIT_ENABLED) {
		NL_SET_ERR_MSG(extack, "tcp-data-split is disabled");
		return -EINVAL;
	}

	if (dev->cfg->hds_thresh) {
		NL_SET_ERR_MSG(extack, "hds-thresh is not zero");
		return -EINVAL;
	}

	rxq = __netif_get_rx_queue(dev, rxq_idx);
	if (rxq->mp_params.mp_ops) {
		NL_SET_ERR_MSG(extack, "designated queue already memory provider bound");
		return -EEXIST;
	}

#ifdef CONFIG_XDP_SOCKETS
	if (rxq->pool) {
		NL_SET_ERR_MSG(extack, "designated queue already in use by AF_XDP");
		return -EBUSY;
	}
#endif

	err = xa_alloc(&binding->bound_rxqs, &xa_idx, rxq, xa_limit_32b,
		       GFP_KERNEL);
	if (err)
		return err;

	rxq->mp_params.mp_priv = binding;
	rxq->mp_params.mp_ops = &dmabuf_devmem_ops;

	err = netdev_rx_queue_restart(dev, rxq_idx);
	if (err)
		goto err_xa_erase;

	return 0;

err_xa_erase:
	rxq->mp_params.mp_priv = NULL;
	rxq->mp_params.mp_ops = NULL;
	xa_erase(&binding->bound_rxqs, xa_idx);

	return err;
}

struct net_devmem_dmabuf_binding *
net_devmem_bind_dmabuf(struct net_device *dev, unsigned int dmabuf_fd,
		       struct netlink_ext_ack *extack)
{
	struct net_devmem_dmabuf_binding *binding;
	static u32 id_alloc_next;
	struct scatterlist *sg;
	struct dma_buf *dmabuf;
	unsigned int sg_idx, i;
	unsigned long virtual;
	int err;

	dmabuf = dma_buf_get(dmabuf_fd);
	if (IS_ERR(dmabuf))
		return ERR_CAST(dmabuf);

	binding = kzalloc_node(sizeof(*binding), GFP_KERNEL,
			       dev_to_node(&dev->dev));
	if (!binding) {
		err = -ENOMEM;
		goto err_put_dmabuf;
	}

	binding->dev = dev;

	err = xa_alloc_cyclic(&net_devmem_dmabuf_bindings, &binding->id,
			      binding, xa_limit_32b, &id_alloc_next,
			      GFP_KERNEL);
	if (err < 0)
		goto err_free_binding;

	xa_init_flags(&binding->bound_rxqs, XA_FLAGS_ALLOC);

	refcount_set(&binding->ref, 1);

	binding->dmabuf = dmabuf;

	binding->attachment = dma_buf_attach(binding->dmabuf, dev->dev.parent);
	if (IS_ERR(binding->attachment)) {
		err = PTR_ERR(binding->attachment);
		NL_SET_ERR_MSG(extack, "Failed to bind dmabuf to device");
		goto err_free_id;
	}

	binding->sgt = dma_buf_map_attachment_unlocked(binding->attachment,
						       DMA_FROM_DEVICE);
	if (IS_ERR(binding->sgt)) {
		err = PTR_ERR(binding->sgt);
		NL_SET_ERR_MSG(extack, "Failed to map dmabuf attachment");
		goto err_detach;
	}

	/* For simplicity we expect to make PAGE_SIZE allocations, but the
	 * binding can be much more flexible than that. We may be able to
	 * allocate MTU sized chunks here. Leave that for future work...
	 */
	binding->chunk_pool =
		gen_pool_create(PAGE_SHIFT, dev_to_node(&dev->dev));
	if (!binding->chunk_pool) {
		err = -ENOMEM;
		goto err_unmap;
	}

	virtual = 0;
	for_each_sgtable_dma_sg(binding->sgt, sg, sg_idx) {
		dma_addr_t dma_addr = sg_dma_address(sg);
		struct dmabuf_genpool_chunk_owner *owner;
		size_t len = sg_dma_len(sg);
		struct net_iov *niov;

		owner = kzalloc_node(sizeof(*owner), GFP_KERNEL,
				     dev_to_node(&dev->dev));
		if (!owner) {
			err = -ENOMEM;
			goto err_free_chunks;
		}

		owner->area.base_virtual = virtual;
		owner->base_dma_addr = dma_addr;
		owner->area.num_niovs = len / PAGE_SIZE;
		owner->binding = binding;

		err = gen_pool_add_owner(binding->chunk_pool, dma_addr,
					 dma_addr, len, dev_to_node(&dev->dev),
					 owner);
		if (err) {
			kfree(owner);
			err = -EINVAL;
			goto err_free_chunks;
		}

		owner->area.niovs = kvmalloc_array(owner->area.num_niovs,
						   sizeof(*owner->area.niovs),
						   GFP_KERNEL);
		if (!owner->area.niovs) {
			err = -ENOMEM;
			goto err_free_chunks;
		}

		for (i = 0; i < owner->area.num_niovs; i++) {
			niov = &owner->area.niovs[i];
			niov->owner = &owner->area;
			page_pool_set_dma_addr_netmem(net_iov_to_netmem(niov),
						      net_devmem_get_dma_addr(niov));
		}

		virtual += len;
	}

	return binding;

err_free_chunks:
	gen_pool_for_each_chunk(binding->chunk_pool,
				net_devmem_dmabuf_free_chunk_owner, NULL);
	gen_pool_destroy(binding->chunk_pool);
err_unmap:
	dma_buf_unmap_attachment_unlocked(binding->attachment, binding->sgt,
					  DMA_FROM_DEVICE);
err_detach:
	dma_buf_detach(dmabuf, binding->attachment);
err_free_id:
	xa_erase(&net_devmem_dmabuf_bindings, binding->id);
err_free_binding:
	kfree(binding);
err_put_dmabuf:
	dma_buf_put(dmabuf);
	return ERR_PTR(err);
}

/*** "Dmabuf devmem memory provider" ***/

int mp_dmabuf_devmem_init(struct page_pool *pool)
{
	struct net_devmem_dmabuf_binding *binding = pool->mp_priv;

	if (!binding)
		return -EINVAL;

	/* dma-buf dma addresses do not need and should not be used with
	 * dma_sync_for_cpu/device. Force disable dma_sync.
	 */
	pool->dma_sync = false;
	pool->dma_sync_for_cpu = false;

	if (pool->p.order != 0)
		return -E2BIG;

	net_devmem_dmabuf_binding_get(binding);
	return 0;
}

netmem_ref mp_dmabuf_devmem_alloc_netmems(struct page_pool *pool, gfp_t gfp)
{
	struct net_devmem_dmabuf_binding *binding = pool->mp_priv;
	struct net_iov *niov;
	netmem_ref netmem;

	niov = net_devmem_alloc_dmabuf(binding);
	if (!niov)
		return 0;

	netmem = net_iov_to_netmem(niov);

	page_pool_set_pp_info(pool, netmem);

	pool->pages_state_hold_cnt++;
	trace_page_pool_state_hold(pool, netmem, pool->pages_state_hold_cnt);
	return netmem;
}

void mp_dmabuf_devmem_destroy(struct page_pool *pool)
{
	struct net_devmem_dmabuf_binding *binding = pool->mp_priv;

	net_devmem_dmabuf_binding_put(binding);
}

bool mp_dmabuf_devmem_release_page(struct page_pool *pool, netmem_ref netmem)
{
	long refcount = atomic_long_read(netmem_get_pp_ref_count_ref(netmem));

	if (WARN_ON_ONCE(!netmem_is_net_iov(netmem)))
		return false;

	if (WARN_ON_ONCE(refcount != 1))
		return false;

	page_pool_clear_pp_info(netmem);

	net_devmem_free_dmabuf(netmem_to_net_iov(netmem));

	/* We don't want the page pool put_page()ing our net_iovs. */
	return false;
}

static int mp_dmabuf_devmem_nl_fill(void *mp_priv, struct sk_buff *rsp,
				    struct netdev_rx_queue *rxq)
{
	const struct net_devmem_dmabuf_binding *binding = mp_priv;
	int type = rxq ? NETDEV_A_QUEUE_DMABUF : NETDEV_A_PAGE_POOL_DMABUF;

	return nla_put_u32(rsp, type, binding->id);
}

static void mp_dmabuf_devmem_uninstall(void *mp_priv,
				       struct netdev_rx_queue *rxq)
{
	struct net_devmem_dmabuf_binding *binding = mp_priv;
	struct netdev_rx_queue *bound_rxq;
	unsigned long xa_idx;

	xa_for_each(&binding->bound_rxqs, xa_idx, bound_rxq) {
		if (bound_rxq == rxq) {
			xa_erase(&binding->bound_rxqs, xa_idx);
			break;
		}
	}
}

static const struct memory_provider_ops dmabuf_devmem_ops = {
	.init			= mp_dmabuf_devmem_init,
	.destroy		= mp_dmabuf_devmem_destroy,
	.alloc_netmems		= mp_dmabuf_devmem_alloc_netmems,
	.release_netmem		= mp_dmabuf_devmem_release_page,
	.nl_fill		= mp_dmabuf_devmem_nl_fill,
	.uninstall		= mp_dmabuf_devmem_uninstall,
};
