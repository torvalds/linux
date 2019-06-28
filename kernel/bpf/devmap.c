// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2017 Covalent IO, Inc. http://covalent.io
 */

/* Devmaps primary use is as a backend map for XDP BPF helper call
 * bpf_redirect_map(). Because XDP is mostly concerned with performance we
 * spent some effort to ensure the datapath with redirect maps does not use
 * any locking. This is a quick note on the details.
 *
 * We have three possible paths to get into the devmap control plane bpf
 * syscalls, bpf programs, and driver side xmit/flush operations. A bpf syscall
 * will invoke an update, delete, or lookup operation. To ensure updates and
 * deletes appear atomic from the datapath side xchg() is used to modify the
 * netdev_map array. Then because the datapath does a lookup into the netdev_map
 * array (read-only) from an RCU critical section we use call_rcu() to wait for
 * an rcu grace period before free'ing the old data structures. This ensures the
 * datapath always has a valid copy. However, the datapath does a "flush"
 * operation that pushes any pending packets in the driver outside the RCU
 * critical section. Each bpf_dtab_netdev tracks these pending operations using
 * a per-cpu flush list. The bpf_dtab_netdev object will not be destroyed  until
 * this list is empty, indicating outstanding flush operations have completed.
 *
 * BPF syscalls may race with BPF program calls on any of the update, delete
 * or lookup operations. As noted above the xchg() operation also keep the
 * netdev_map consistent in this case. From the devmap side BPF programs
 * calling into these operations are the same as multiple user space threads
 * making system calls.
 *
 * Finally, any of the above may race with a netdev_unregister notifier. The
 * unregister notifier must search for net devices in the map structure that
 * contain a reference to the net device and remove them. This is a two step
 * process (a) dereference the bpf_dtab_netdev object in netdev_map and (b)
 * check to see if the ifindex is the same as the net_device being removed.
 * When removing the dev a cmpxchg() is used to ensure the correct dev is
 * removed, in the case of a concurrent update or delete operation it is
 * possible that the initially referenced dev is no longer in the map. As the
 * notifier hook walks the map we know that new dev references can not be
 * added by the user because core infrastructure ensures dev_get_by_index()
 * calls will fail at this point.
 */
#include <linux/bpf.h>
#include <net/xdp.h>
#include <linux/filter.h>
#include <trace/events/xdp.h>

#define DEV_CREATE_FLAG_MASK \
	(BPF_F_NUMA_NODE | BPF_F_RDONLY | BPF_F_WRONLY)

#define DEV_MAP_BULK_SIZE 16
struct bpf_dtab_netdev;

struct xdp_bulk_queue {
	struct xdp_frame *q[DEV_MAP_BULK_SIZE];
	struct list_head flush_node;
	struct net_device *dev_rx;
	struct bpf_dtab_netdev *obj;
	unsigned int count;
};

struct bpf_dtab_netdev {
	struct net_device *dev; /* must be first member, due to tracepoint */
	struct bpf_dtab *dtab;
	unsigned int bit;
	struct xdp_bulk_queue __percpu *bulkq;
	struct rcu_head rcu;
};

struct bpf_dtab {
	struct bpf_map map;
	struct bpf_dtab_netdev **netdev_map;
	struct list_head __percpu *flush_list;
	struct list_head list;
};

static DEFINE_SPINLOCK(dev_map_lock);
static LIST_HEAD(dev_map_list);

static struct bpf_map *dev_map_alloc(union bpf_attr *attr)
{
	struct bpf_dtab *dtab;
	int err, cpu;
	u64 cost;

	if (!capable(CAP_NET_ADMIN))
		return ERR_PTR(-EPERM);

	/* check sanity of attributes */
	if (attr->max_entries == 0 || attr->key_size != 4 ||
	    attr->value_size != 4 || attr->map_flags & ~DEV_CREATE_FLAG_MASK)
		return ERR_PTR(-EINVAL);

	dtab = kzalloc(sizeof(*dtab), GFP_USER);
	if (!dtab)
		return ERR_PTR(-ENOMEM);

	bpf_map_init_from_attr(&dtab->map, attr);

	/* make sure page count doesn't overflow */
	cost = (u64) dtab->map.max_entries * sizeof(struct bpf_dtab_netdev *);
	cost += sizeof(struct list_head) * num_possible_cpus();

	/* if map size is larger than memlock limit, reject it */
	err = bpf_map_charge_init(&dtab->map.memory, cost);
	if (err)
		goto free_dtab;

	err = -ENOMEM;

	dtab->flush_list = alloc_percpu(struct list_head);
	if (!dtab->flush_list)
		goto free_charge;

	for_each_possible_cpu(cpu)
		INIT_LIST_HEAD(per_cpu_ptr(dtab->flush_list, cpu));

	dtab->netdev_map = bpf_map_area_alloc(dtab->map.max_entries *
					      sizeof(struct bpf_dtab_netdev *),
					      dtab->map.numa_node);
	if (!dtab->netdev_map)
		goto free_percpu;

	spin_lock(&dev_map_lock);
	list_add_tail_rcu(&dtab->list, &dev_map_list);
	spin_unlock(&dev_map_lock);

	return &dtab->map;

free_percpu:
	free_percpu(dtab->flush_list);
free_charge:
	bpf_map_charge_finish(&dtab->map.memory);
free_dtab:
	kfree(dtab);
	return ERR_PTR(err);
}

static void dev_map_free(struct bpf_map *map)
{
	struct bpf_dtab *dtab = container_of(map, struct bpf_dtab, map);
	int i, cpu;

	/* At this point bpf_prog->aux->refcnt == 0 and this map->refcnt == 0,
	 * so the programs (can be more than one that used this map) were
	 * disconnected from events. Wait for outstanding critical sections in
	 * these programs to complete. The rcu critical section only guarantees
	 * no further reads against netdev_map. It does __not__ ensure pending
	 * flush operations (if any) are complete.
	 */

	spin_lock(&dev_map_lock);
	list_del_rcu(&dtab->list);
	spin_unlock(&dev_map_lock);

	bpf_clear_redirect_map(map);
	synchronize_rcu();

	/* Make sure prior __dev_map_entry_free() have completed. */
	rcu_barrier();

	/* To ensure all pending flush operations have completed wait for flush
	 * list to empty on _all_ cpus.
	 * Because the above synchronize_rcu() ensures the map is disconnected
	 * from the program we can assume no new items will be added.
	 */
	for_each_online_cpu(cpu) {
		struct list_head *flush_list = per_cpu_ptr(dtab->flush_list, cpu);

		while (!list_empty(flush_list))
			cond_resched();
	}

	for (i = 0; i < dtab->map.max_entries; i++) {
		struct bpf_dtab_netdev *dev;

		dev = dtab->netdev_map[i];
		if (!dev)
			continue;

		free_percpu(dev->bulkq);
		dev_put(dev->dev);
		kfree(dev);
	}

	free_percpu(dtab->flush_list);
	bpf_map_area_free(dtab->netdev_map);
	kfree(dtab);
}

static int dev_map_get_next_key(struct bpf_map *map, void *key, void *next_key)
{
	struct bpf_dtab *dtab = container_of(map, struct bpf_dtab, map);
	u32 index = key ? *(u32 *)key : U32_MAX;
	u32 *next = next_key;

	if (index >= dtab->map.max_entries) {
		*next = 0;
		return 0;
	}

	if (index == dtab->map.max_entries - 1)
		return -ENOENT;
	*next = index + 1;
	return 0;
}

static int bq_xmit_all(struct xdp_bulk_queue *bq, u32 flags,
		       bool in_napi_ctx)
{
	struct bpf_dtab_netdev *obj = bq->obj;
	struct net_device *dev = obj->dev;
	int sent = 0, drops = 0, err = 0;
	int i;

	if (unlikely(!bq->count))
		return 0;

	for (i = 0; i < bq->count; i++) {
		struct xdp_frame *xdpf = bq->q[i];

		prefetch(xdpf);
	}

	sent = dev->netdev_ops->ndo_xdp_xmit(dev, bq->count, bq->q, flags);
	if (sent < 0) {
		err = sent;
		sent = 0;
		goto error;
	}
	drops = bq->count - sent;
out:
	bq->count = 0;

	trace_xdp_devmap_xmit(&obj->dtab->map, obj->bit,
			      sent, drops, bq->dev_rx, dev, err);
	bq->dev_rx = NULL;
	__list_del_clearprev(&bq->flush_node);
	return 0;
error:
	/* If ndo_xdp_xmit fails with an errno, no frames have been
	 * xmit'ed and it's our responsibility to them free all.
	 */
	for (i = 0; i < bq->count; i++) {
		struct xdp_frame *xdpf = bq->q[i];

		/* RX path under NAPI protection, can return frames faster */
		if (likely(in_napi_ctx))
			xdp_return_frame_rx_napi(xdpf);
		else
			xdp_return_frame(xdpf);
		drops++;
	}
	goto out;
}

/* __dev_map_flush is called from xdp_do_flush_map() which _must_ be signaled
 * from the driver before returning from its napi->poll() routine. The poll()
 * routine is called either from busy_poll context or net_rx_action signaled
 * from NET_RX_SOFTIRQ. Either way the poll routine must complete before the
 * net device can be torn down. On devmap tear down we ensure the flush list
 * is empty before completing to ensure all flush operations have completed.
 */
void __dev_map_flush(struct bpf_map *map)
{
	struct bpf_dtab *dtab = container_of(map, struct bpf_dtab, map);
	struct list_head *flush_list = this_cpu_ptr(dtab->flush_list);
	struct xdp_bulk_queue *bq, *tmp;

	rcu_read_lock();
	list_for_each_entry_safe(bq, tmp, flush_list, flush_node)
		bq_xmit_all(bq, XDP_XMIT_FLUSH, true);
	rcu_read_unlock();
}

/* rcu_read_lock (from syscall and BPF contexts) ensures that if a delete and/or
 * update happens in parallel here a dev_put wont happen until after reading the
 * ifindex.
 */
struct bpf_dtab_netdev *__dev_map_lookup_elem(struct bpf_map *map, u32 key)
{
	struct bpf_dtab *dtab = container_of(map, struct bpf_dtab, map);
	struct bpf_dtab_netdev *obj;

	if (key >= map->max_entries)
		return NULL;

	obj = READ_ONCE(dtab->netdev_map[key]);
	return obj;
}

/* Runs under RCU-read-side, plus in softirq under NAPI protection.
 * Thus, safe percpu variable access.
 */
static int bq_enqueue(struct bpf_dtab_netdev *obj, struct xdp_frame *xdpf,
		      struct net_device *dev_rx)

{
	struct list_head *flush_list = this_cpu_ptr(obj->dtab->flush_list);
	struct xdp_bulk_queue *bq = this_cpu_ptr(obj->bulkq);

	if (unlikely(bq->count == DEV_MAP_BULK_SIZE))
		bq_xmit_all(bq, 0, true);

	/* Ingress dev_rx will be the same for all xdp_frame's in
	 * bulk_queue, because bq stored per-CPU and must be flushed
	 * from net_device drivers NAPI func end.
	 */
	if (!bq->dev_rx)
		bq->dev_rx = dev_rx;

	bq->q[bq->count++] = xdpf;

	if (!bq->flush_node.prev)
		list_add(&bq->flush_node, flush_list);

	return 0;
}

int dev_map_enqueue(struct bpf_dtab_netdev *dst, struct xdp_buff *xdp,
		    struct net_device *dev_rx)
{
	struct net_device *dev = dst->dev;
	struct xdp_frame *xdpf;
	int err;

	if (!dev->netdev_ops->ndo_xdp_xmit)
		return -EOPNOTSUPP;

	err = xdp_ok_fwd_dev(dev, xdp->data_end - xdp->data);
	if (unlikely(err))
		return err;

	xdpf = convert_to_xdp_frame(xdp);
	if (unlikely(!xdpf))
		return -EOVERFLOW;

	return bq_enqueue(dst, xdpf, dev_rx);
}

int dev_map_generic_redirect(struct bpf_dtab_netdev *dst, struct sk_buff *skb,
			     struct bpf_prog *xdp_prog)
{
	int err;

	err = xdp_ok_fwd_dev(dst->dev, skb->len);
	if (unlikely(err))
		return err;
	skb->dev = dst->dev;
	generic_xdp_tx(skb, xdp_prog);

	return 0;
}

static void *dev_map_lookup_elem(struct bpf_map *map, void *key)
{
	struct bpf_dtab_netdev *obj = __dev_map_lookup_elem(map, *(u32 *)key);
	struct net_device *dev = obj ? obj->dev : NULL;

	return dev ? &dev->ifindex : NULL;
}

static void dev_map_flush_old(struct bpf_dtab_netdev *dev)
{
	if (dev->dev->netdev_ops->ndo_xdp_xmit) {
		struct xdp_bulk_queue *bq;
		int cpu;

		rcu_read_lock();
		for_each_online_cpu(cpu) {
			bq = per_cpu_ptr(dev->bulkq, cpu);
			bq_xmit_all(bq, XDP_XMIT_FLUSH, false);
		}
		rcu_read_unlock();
	}
}

static void __dev_map_entry_free(struct rcu_head *rcu)
{
	struct bpf_dtab_netdev *dev;

	dev = container_of(rcu, struct bpf_dtab_netdev, rcu);
	dev_map_flush_old(dev);
	free_percpu(dev->bulkq);
	dev_put(dev->dev);
	kfree(dev);
}

static int dev_map_delete_elem(struct bpf_map *map, void *key)
{
	struct bpf_dtab *dtab = container_of(map, struct bpf_dtab, map);
	struct bpf_dtab_netdev *old_dev;
	int k = *(u32 *)key;

	if (k >= map->max_entries)
		return -EINVAL;

	/* Use call_rcu() here to ensure any rcu critical sections have
	 * completed, but this does not guarantee a flush has happened
	 * yet. Because driver side rcu_read_lock/unlock only protects the
	 * running XDP program. However, for pending flush operations the
	 * dev and ctx are stored in another per cpu map. And additionally,
	 * the driver tear down ensures all soft irqs are complete before
	 * removing the net device in the case of dev_put equals zero.
	 */
	old_dev = xchg(&dtab->netdev_map[k], NULL);
	if (old_dev)
		call_rcu(&old_dev->rcu, __dev_map_entry_free);
	return 0;
}

static int dev_map_update_elem(struct bpf_map *map, void *key, void *value,
				u64 map_flags)
{
	struct bpf_dtab *dtab = container_of(map, struct bpf_dtab, map);
	struct net *net = current->nsproxy->net_ns;
	gfp_t gfp = GFP_ATOMIC | __GFP_NOWARN;
	struct bpf_dtab_netdev *dev, *old_dev;
	u32 ifindex = *(u32 *)value;
	struct xdp_bulk_queue *bq;
	u32 i = *(u32 *)key;
	int cpu;

	if (unlikely(map_flags > BPF_EXIST))
		return -EINVAL;
	if (unlikely(i >= dtab->map.max_entries))
		return -E2BIG;
	if (unlikely(map_flags == BPF_NOEXIST))
		return -EEXIST;

	if (!ifindex) {
		dev = NULL;
	} else {
		dev = kmalloc_node(sizeof(*dev), gfp, map->numa_node);
		if (!dev)
			return -ENOMEM;

		dev->bulkq = __alloc_percpu_gfp(sizeof(*dev->bulkq),
						sizeof(void *), gfp);
		if (!dev->bulkq) {
			kfree(dev);
			return -ENOMEM;
		}

		for_each_possible_cpu(cpu) {
			bq = per_cpu_ptr(dev->bulkq, cpu);
			bq->obj = dev;
		}

		dev->dev = dev_get_by_index(net, ifindex);
		if (!dev->dev) {
			free_percpu(dev->bulkq);
			kfree(dev);
			return -EINVAL;
		}

		dev->bit = i;
		dev->dtab = dtab;
	}

	/* Use call_rcu() here to ensure rcu critical sections have completed
	 * Remembering the driver side flush operation will happen before the
	 * net device is removed.
	 */
	old_dev = xchg(&dtab->netdev_map[i], dev);
	if (old_dev)
		call_rcu(&old_dev->rcu, __dev_map_entry_free);

	return 0;
}

const struct bpf_map_ops dev_map_ops = {
	.map_alloc = dev_map_alloc,
	.map_free = dev_map_free,
	.map_get_next_key = dev_map_get_next_key,
	.map_lookup_elem = dev_map_lookup_elem,
	.map_update_elem = dev_map_update_elem,
	.map_delete_elem = dev_map_delete_elem,
	.map_check_btf = map_check_no_btf,
};

static int dev_map_notification(struct notifier_block *notifier,
				ulong event, void *ptr)
{
	struct net_device *netdev = netdev_notifier_info_to_dev(ptr);
	struct bpf_dtab *dtab;
	int i;

	switch (event) {
	case NETDEV_UNREGISTER:
		/* This rcu_read_lock/unlock pair is needed because
		 * dev_map_list is an RCU list AND to ensure a delete
		 * operation does not free a netdev_map entry while we
		 * are comparing it against the netdev being unregistered.
		 */
		rcu_read_lock();
		list_for_each_entry_rcu(dtab, &dev_map_list, list) {
			for (i = 0; i < dtab->map.max_entries; i++) {
				struct bpf_dtab_netdev *dev, *odev;

				dev = READ_ONCE(dtab->netdev_map[i]);
				if (!dev || netdev != dev->dev)
					continue;
				odev = cmpxchg(&dtab->netdev_map[i], dev, NULL);
				if (dev == odev)
					call_rcu(&dev->rcu,
						 __dev_map_entry_free);
			}
		}
		rcu_read_unlock();
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block dev_map_notifier = {
	.notifier_call = dev_map_notification,
};

static int __init dev_map_init(void)
{
	/* Assure tracepoint shadow struct _bpf_dtab_netdev is in sync */
	BUILD_BUG_ON(offsetof(struct bpf_dtab_netdev, dev) !=
		     offsetof(struct _bpf_dtab_netdev, dev));
	register_netdevice_notifier(&dev_map_notifier);
	return 0;
}

subsys_initcall(dev_map_init);
