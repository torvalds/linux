// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *      Devmem TCP
 *
 *      Authors:	Mina Almasry <almasrymina@google.com>
 *			Willem de Bruijn <willemdebruijn.kernel@gmail.com>
 *			Kaiyuan Zhang <kaiyuanz@google.com
 */

#include <linux/dma-buf.h>
#include <linux/dma-resv.h>
#include <linux/mm.h>
#include <linux/netdevice.h>
#include <linux/types.h>
#include <net/netdev_queues.h>
#include <net/netdev_rx_queue.h>
#include <net/page_pool/helpers.h>
#include <net/page_pool/memory_provider.h>
#include <net/sock.h>
#include <net/tcp.h>

#include "devmem.h"
#include "mp_dmabuf_devmem.h"

/* Device memory support */

static DEFINE_XARRAY_FLAGS(net_devmem_dmabuf_bindings, XA_FLAGS_ALLOC1);

static const struct memory_provider_ops dmabuf_devmem_ops;

static struct net_iov *net_iov_free_pop(struct net_iov_area *area,
					u32 *freelist, u32 *free_count)
{
	u32 idx;

	if (unlikely(!*free_count))
		return NULL;

	idx = freelist[--(*free_count)];
	return &area->niovs[idx];
}

static void net_iov_free_push(struct net_iov_area *area,
			      u32 *freelist, u32 *free_count,
			      struct net_iov *niov)
{
	if (WARN_ON_ONCE(net_iov_owner(niov) != area ||
			 *free_count >= area->num_niovs))
		return;

	freelist[(*free_count)++] = net_iov_idx(niov);
}

static const struct dma_buf_attach_ops net_devmem_dmabuf_attach_ops = {
	.allow_peer2peer = true,
};

static void net_devmem_dmabuf_binding_release(struct percpu_ref *ref)
{
	struct net_devmem_dmabuf_binding *binding =
		container_of(ref, struct net_devmem_dmabuf_binding, ref);

	INIT_WORK(&binding->unbind_w, __net_devmem_dmabuf_binding_free);
	schedule_work(&binding->unbind_w);
}

void __net_devmem_dmabuf_binding_free(struct work_struct *wq)
{
	struct net_devmem_dmabuf_binding *binding = container_of(wq, typeof(*binding), unbind_w);

	if (binding->freelist)
		WARN(binding->free_count != binding->area.num_niovs,
		     "destroying dmabuf binding with outstanding net_iovs: total=%zu, free=%u",
		     binding->area.num_niovs, binding->free_count);

	kvfree(binding->area.niovs);
	dma_resv_lock(binding->dmabuf->resv, NULL);
	dma_buf_unmap_attachment(binding->attachment, binding->sgt,
				 binding->direction);
	dma_buf_unpin(binding->attachment);
	dma_resv_unlock(binding->dmabuf->resv);
	dma_buf_detach(binding->dmabuf, binding->attachment);
	dma_buf_put(binding->dmabuf);
	xa_destroy(&binding->bound_rxqs);
	percpu_ref_exit(&binding->ref);
	kvfree(binding->freelist);
	kvfree(binding->tx_vec);
	kfree(binding);
}

static unsigned int
net_devmem_alloc_dmabuf_bulk(struct net_devmem_dmabuf_binding *binding,
			     netmem_ref *netmems, unsigned int count)
{
	struct net_iov *niov;
	unsigned int i;

	spin_lock_bh(&binding->freelist_lock);

	count = min(count, binding->free_count);
	for (i = 0; i < count; i++) {
		niov = net_iov_free_pop(&binding->area, binding->freelist,
					&binding->free_count);
		netmems[i] = net_iov_to_netmem(niov);
	}

	spin_unlock_bh(&binding->freelist_lock);

	return count;
}

void net_devmem_free_dmabuf(struct net_iov *niov)
{
	struct net_devmem_dmabuf_binding *binding = net_devmem_iov_binding(niov);

	spin_lock_bh(&binding->freelist_lock);
	net_iov_free_push(&binding->area, binding->freelist,
			  &binding->free_count, niov);
	spin_unlock_bh(&binding->freelist_lock);
}

void net_devmem_unbind_dmabuf(struct net_devmem_dmabuf_binding *binding)
{
	struct netdev_rx_queue *rxq;
	unsigned long xa_idx;
	unsigned int rxq_idx;

	xa_erase(&net_devmem_dmabuf_bindings, binding->id);

	/* Ensure no tx net_devmem_lookup_dmabuf() are in flight after the
	 * erase.
	 */
	synchronize_net();

	if (binding->list.next)
		list_del(&binding->list);

	xa_for_each(&binding->bound_rxqs, xa_idx, rxq) {
		const struct pp_memory_provider_params mp_params = {
			.mp_priv	= binding,
			.mp_ops		= &dmabuf_devmem_ops,
		};

		rxq_idx = get_netdev_rx_queue_index(rxq);

		netif_mp_close_rxq(binding->dev, rxq_idx, &mp_params);
	}

	percpu_ref_kill(&binding->ref);
}

int net_devmem_bind_dmabuf_to_queue(struct net_device *dev, u32 rxq_idx,
				    struct net_devmem_dmabuf_binding *binding,
				    struct netlink_ext_ack *extack)
{
	struct pp_memory_provider_params mp_params = {
		.mp_priv	= binding,
		.mp_ops		= &dmabuf_devmem_ops,
	};
	struct netdev_rx_queue *rxq;
	u32 xa_idx;
	int err;

	if (binding->niov_shift != PAGE_SHIFT)
		mp_params.rx_page_size = 1U << binding->niov_shift;

	err = netif_mp_open_rxq(dev, rxq_idx, &mp_params, extack);
	if (err)
		return err;

	rxq = __netif_get_rx_queue(dev, rxq_idx);
	err = xa_alloc(&binding->bound_rxqs, &xa_idx, rxq, xa_limit_32b,
		       GFP_KERNEL);
	if (err)
		goto err_close_rxq;

	return 0;

err_close_rxq:
	netif_mp_close_rxq(dev, rxq_idx, &mp_params);
	return err;
}

struct net_devmem_dmabuf_binding *
net_devmem_bind_dmabuf(struct net_device *dev, void *vdev,
		       struct device *dma_dev,
		       enum dma_data_direction direction,
		       unsigned int dmabuf_fd, unsigned int niov_shift,
		       struct netdev_nl_sock *priv,
		       const struct sock *nl_sk,
		       struct netlink_ext_ack *extack)
{
	struct net_devmem_dmabuf_binding *binding;
	size_t niov_size = 1UL << niov_shift;
	static u32 id_alloc_next;
	struct scatterlist *sg;
	struct dma_buf *dmabuf;
	unsigned int sg_idx;
	size_t niov_idx;
	size_t i;
	long err;

	if (!dma_dev) {
		NL_SET_ERR_MSG(extack, "Device doesn't support DMA");
		return ERR_PTR(-EOPNOTSUPP);
	}

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
	binding->vdev = vdev;
	write_pnet(&binding->net, sock_net(nl_sk));
	binding->uid = sk_uid(nl_sk);
	binding->niov_shift = niov_shift;
	xa_init_flags(&binding->bound_rxqs, XA_FLAGS_ALLOC);

	err = percpu_ref_init(&binding->ref,
			      net_devmem_dmabuf_binding_release,
			      0, GFP_KERNEL);
	if (err < 0)
		goto err_free_binding;

	mutex_init(&binding->lock);

	binding->dmabuf = dmabuf;
	binding->direction = direction;

	binding->attachment =
		dma_buf_dynamic_attach(binding->dmabuf, dma_dev,
				       &net_devmem_dmabuf_attach_ops, binding);
	if (IS_ERR(binding->attachment)) {
		err = PTR_ERR(binding->attachment);
		NL_SET_ERR_MSG(extack, "Failed to bind dmabuf to device");
		goto err_exit_ref;
	}

	dma_resv_lock(dmabuf->resv, NULL);
	err = dma_buf_pin(binding->attachment);
	if (err) {
		NL_SET_ERR_MSG(extack, "Failed to pin dmabuf");
		goto err_unlock;
	}

	binding->sgt = dma_buf_map_attachment(binding->attachment, direction);
	if (IS_ERR(binding->sgt)) {
		err = PTR_ERR(binding->sgt);
		NL_SET_ERR_MSG(extack, "Failed to map dmabuf attachment");
		goto err_unpin;
	}
	dma_resv_unlock(dmabuf->resv);

	err = dma_resv_wait_timeout(dmabuf->resv, DMA_RESV_USAGE_KERNEL, true,
				    MAX_SCHEDULE_TIMEOUT);
	if (err < 0) {
		NL_SET_ERR_MSG(extack, "Failed to wait for dmabuf fences");
		goto err_unmap;
	}

	if (!IS_ALIGNED(dmabuf->size, niov_size)) {
		err = -EINVAL;
		NL_SET_ERR_MSG_FMT(extack,
				   "dmabuf size %zu not aligned to niov size %zu",
				   dmabuf->size, niov_size);
		goto err_unmap;
	}

	if ((dmabuf->size >> niov_shift) > U32_MAX) {
		err = -E2BIG;
		NL_SET_ERR_MSG(extack, "dmabuf contains too many net_iovs");
		goto err_unmap;
	}

	binding->area.num_niovs = dmabuf->size >> niov_shift;
	if (direction == DMA_TO_DEVICE) {
		binding->tx_vec = kvmalloc_objs(struct net_iov *,
						binding->area.num_niovs);
		if (!binding->tx_vec) {
			err = -ENOMEM;
			goto err_unmap;
		}
	} else {
		spin_lock_init(&binding->freelist_lock);
		binding->freelist = kvmalloc_array(binding->area.num_niovs,
						   sizeof(binding->freelist[0]),
						   GFP_KERNEL);
		if (!binding->freelist) {
			err = -ENOMEM;
			goto err_unmap;
		}
	}
	binding->area.niovs = kvzalloc_objs(*binding->area.niovs,
					    binding->area.num_niovs);
	if (!binding->area.niovs) {
		err = -ENOMEM;
		goto err_free_freelist;
	}

	niov_idx = 0;
	for_each_sgtable_dma_sg(binding->sgt, sg, sg_idx) {
		dma_addr_t dma_addr = sg_dma_address(sg);
		size_t len = sg_dma_len(sg);
		struct net_iov *niov;
		size_t nr_niovs;

		if (!IS_ALIGNED(dma_addr, niov_size) ||
		    !IS_ALIGNED(len, niov_size)) {
			err = -EINVAL;
			NL_SET_ERR_MSG_FMT(extack,
					   "dmabuf sg entry (addr=%pad, len=%zu) not aligned to niov size %zu",
					   &dma_addr, len, niov_size);
			goto err_free_niovs;
		}

		nr_niovs = len >> niov_shift;
		for (i = 0; i < nr_niovs; i++, niov_idx++) {
			niov = &binding->area.niovs[niov_idx];
			net_iov_init(niov, &binding->area, NET_IOV_DMABUF);
			if (net_mp_niov_set_dma_addr(niov, dma_addr)) {
				err = -EFAULT;
				NL_SET_ERR_MSG(extack,
					       "dmabuf DMA address cannot be represented");
				goto err_free_niovs;
			}
			if (direction == DMA_TO_DEVICE)
				binding->tx_vec[niov_idx] = niov;
			else
				net_iov_free_push(&binding->area,
						  binding->freelist,
						  &binding->free_count, niov);
			dma_addr += niov_size;
		}
	}

	err = xa_alloc_cyclic(&net_devmem_dmabuf_bindings, &binding->id,
			      binding, xa_limit_32b, &id_alloc_next,
			      GFP_KERNEL);
	if (err < 0)
		goto err_free_niovs;

	list_add(&binding->list, &priv->bindings);

	return binding;

err_free_niovs:
	kvfree(binding->area.niovs);
err_free_freelist:
	kvfree(binding->freelist);
	kvfree(binding->tx_vec);
err_unmap:
	dma_resv_lock(dmabuf->resv, NULL);
	dma_buf_unmap_attachment(binding->attachment, binding->sgt, direction);
err_unpin:
	dma_buf_unpin(binding->attachment);
err_unlock:
	dma_resv_unlock(dmabuf->resv);
	dma_buf_detach(dmabuf, binding->attachment);
err_exit_ref:
	percpu_ref_exit(&binding->ref);
err_free_binding:
	kfree(binding);
err_put_dmabuf:
	dma_buf_put(dmabuf);
	return ERR_PTR(err);
}

struct net_devmem_dmabuf_binding *net_devmem_lookup_dmabuf(u32 id)
{
	struct net_devmem_dmabuf_binding *binding;

	rcu_read_lock();
	binding = xa_load(&net_devmem_dmabuf_bindings, id);
	if (binding) {
		if (!net_devmem_dmabuf_binding_get(binding))
			binding = NULL;
	}
	rcu_read_unlock();

	return binding;
}

void net_devmem_get_net_iov(struct net_iov *niov)
{
	net_devmem_dmabuf_binding_get(net_devmem_iov_binding(niov));
}

void net_devmem_put_net_iov(struct net_iov *niov)
{
	net_devmem_dmabuf_binding_put(net_devmem_iov_binding(niov));
}

struct net_devmem_dmabuf_binding *net_devmem_get_binding(struct sock *sk,
							 unsigned int dmabuf_id)
{
	struct net_devmem_dmabuf_binding *binding;
	struct net_device *dst_dev;
	struct dst_entry *dst;
	int err = 0;

	binding = net_devmem_lookup_dmabuf(dmabuf_id);
	if (!binding || !binding->tx_vec ||
	    !net_eq(sock_net(sk), read_pnet(&binding->net)) ||
	    !uid_eq(sk_uid(sk), binding->uid)) {
		err = -EINVAL;
		goto out_err;
	}

	rcu_read_lock();
	dst = __sk_dst_get(sk);
	/* If dst is NULL (route expired), attempt to rebuild it. */
	if (unlikely(!dst)) {
		if (inet_csk(sk)->icsk_af_ops->rebuild_header(sk)) {
			err = -EHOSTUNREACH;
			goto out_unlock;
		}
		dst = __sk_dst_get(sk);
		if (unlikely(!dst)) {
			err = -ENODEV;
			goto out_unlock;
		}
	}

	/* The dma-addrs in this binding are only reachable to the corresponding
	 * net_device.
	 */
	dst_dev = dst_dev_rcu(dst);
	if (unlikely(!dst_dev) ||
	    unlikely(dst_dev != READ_ONCE(binding->dev) &&
		     dst_dev != READ_ONCE(binding->vdev))) {
		err = -ENODEV;
		goto out_unlock;
	}

	rcu_read_unlock();
	return binding;

out_unlock:
	rcu_read_unlock();
out_err:
	if (binding)
		net_devmem_dmabuf_binding_put(binding);

	return ERR_PTR(err);
}

struct net_iov *
net_devmem_get_niov_at(struct net_devmem_dmabuf_binding *binding,
		       size_t virt_addr, size_t *off, size_t *size)
{
	if (virt_addr >= binding->dmabuf->size)
		return NULL;

	*off = virt_addr % PAGE_SIZE;
	*size = PAGE_SIZE - *off;

	return binding->tx_vec[virt_addr / PAGE_SIZE];
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

	if (pool->p.order != binding->niov_shift - PAGE_SHIFT)
		return -E2BIG;

	net_devmem_dmabuf_binding_get(binding);
	return 0;
}

netmem_ref mp_dmabuf_devmem_alloc_netmems(struct page_pool *pool, gfp_t gfp)
{
	struct net_devmem_dmabuf_binding *binding = pool->mp_priv;
	netmem_ref *netmems = pool->alloc.cache;
	unsigned int allocated, i;

	if (WARN_ON_ONCE(pool->alloc.count))
		return 0;

	allocated = net_devmem_alloc_dmabuf_bulk(binding, netmems,
						 PP_ALLOC_CACHE_REFILL);
	if (unlikely(!allocated))
		return 0;

	for (i = 0; i < allocated; i++)
		net_mp_niov_set_page_pool(pool,
					  netmem_to_net_iov(netmems[i]));

	/* Return the last one, the rest stay in the page_pool cache. */
	allocated--;
	pool->alloc.count = allocated;
	return netmems[allocated];
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

	net_mp_niov_clear_page_pool(netmem_to_net_iov(netmem));
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
			if (xa_empty(&binding->bound_rxqs)) {
				mutex_lock(&binding->lock);
				ASSERT_EXCLUSIVE_WRITER(binding->dev);
				WRITE_ONCE(binding->dev, NULL);
				mutex_unlock(&binding->lock);
			}
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
