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
 *
 * The devmap_hash type is a map type which interprets keys as ifindexes and
 * indexes these using a hashmap. This allows maps that use ifindex as key to be
 * densely packed instead of having holes in the lookup array for unused
 * ifindexes. The setup and packet enqueue/send code is shared between the two
 * types of devmap; only the lookup and insertion is different.
 */
#include <linux/bpf.h>
#include <net/xdp.h>
#include <linux/filter.h>
#include <trace/events/xdp.h>

#define DEV_CREATE_FLAG_MASK \
	(BPF_F_NUMA_NODE | BPF_F_RDONLY | BPF_F_WRONLY)

struct xdp_dev_bulk_queue {
	struct xdp_frame *q[DEV_MAP_BULK_SIZE];
	struct list_head flush_node;
	struct net_device *dev;
	struct net_device *dev_rx;
	unsigned int count;
};

struct bpf_dtab_netdev {
	struct net_device *dev; /* must be first member, due to tracepoint */
	struct hlist_node index_hlist;
	struct bpf_dtab *dtab;
	struct bpf_prog *xdp_prog;
	struct rcu_head rcu;
	unsigned int idx;
	struct bpf_devmap_val val;
};

struct bpf_dtab {
	struct bpf_map map;
	struct bpf_dtab_netdev **netdev_map; /* DEVMAP type only */
	struct list_head list;

	/* these are only used for DEVMAP_HASH type maps */
	struct hlist_head *dev_index_head;
	spinlock_t index_lock;
	unsigned int items;
	u32 n_buckets;
};

static DEFINE_PER_CPU(struct list_head, dev_flush_list);
static DEFINE_SPINLOCK(dev_map_lock);
static LIST_HEAD(dev_map_list);

static struct hlist_head *dev_map_create_hash(unsigned int entries,
					      int numa_node)
{
	int i;
	struct hlist_head *hash;

	hash = bpf_map_area_alloc(entries * sizeof(*hash), numa_node);
	if (hash != NULL)
		for (i = 0; i < entries; i++)
			INIT_HLIST_HEAD(&hash[i]);

	return hash;
}

static inline struct hlist_head *dev_map_index_hash(struct bpf_dtab *dtab,
						    int idx)
{
	return &dtab->dev_index_head[idx & (dtab->n_buckets - 1)];
}

static int dev_map_init_map(struct bpf_dtab *dtab, union bpf_attr *attr)
{
	u32 valsize = attr->value_size;
	u64 cost = 0;
	int err;

	/* check sanity of attributes. 2 value sizes supported:
	 * 4 bytes: ifindex
	 * 8 bytes: ifindex + prog fd
	 */
	if (attr->max_entries == 0 || attr->key_size != 4 ||
	    (valsize != offsetofend(struct bpf_devmap_val, ifindex) &&
	     valsize != offsetofend(struct bpf_devmap_val, bpf_prog.fd)) ||
	    attr->map_flags & ~DEV_CREATE_FLAG_MASK)
		return -EINVAL;

	/* Lookup returns a pointer straight to dev->ifindex, so make sure the
	 * verifier prevents writes from the BPF side
	 */
	attr->map_flags |= BPF_F_RDONLY_PROG;


	bpf_map_init_from_attr(&dtab->map, attr);

	if (attr->map_type == BPF_MAP_TYPE_DEVMAP_HASH) {
		dtab->n_buckets = roundup_pow_of_two(dtab->map.max_entries);

		if (!dtab->n_buckets) /* Overflow check */
			return -EINVAL;
		cost += (u64) sizeof(struct hlist_head) * dtab->n_buckets;
	} else {
		cost += (u64) dtab->map.max_entries * sizeof(struct bpf_dtab_netdev *);
	}

	/* if map size is larger than memlock limit, reject it */
	err = bpf_map_charge_init(&dtab->map.memory, cost);
	if (err)
		return -EINVAL;

	if (attr->map_type == BPF_MAP_TYPE_DEVMAP_HASH) {
		dtab->dev_index_head = dev_map_create_hash(dtab->n_buckets,
							   dtab->map.numa_node);
		if (!dtab->dev_index_head)
			goto free_charge;

		spin_lock_init(&dtab->index_lock);
	} else {
		dtab->netdev_map = bpf_map_area_alloc(dtab->map.max_entries *
						      sizeof(struct bpf_dtab_netdev *),
						      dtab->map.numa_node);
		if (!dtab->netdev_map)
			goto free_charge;
	}

	return 0;

free_charge:
	bpf_map_charge_finish(&dtab->map.memory);
	return -ENOMEM;
}

static struct bpf_map *dev_map_alloc(union bpf_attr *attr)
{
	struct bpf_dtab *dtab;
	int err;

	if (!capable(CAP_NET_ADMIN))
		return ERR_PTR(-EPERM);

	dtab = kzalloc(sizeof(*dtab), GFP_USER);
	if (!dtab)
		return ERR_PTR(-ENOMEM);

	err = dev_map_init_map(dtab, attr);
	if (err) {
		kfree(dtab);
		return ERR_PTR(err);
	}

	spin_lock(&dev_map_lock);
	list_add_tail_rcu(&dtab->list, &dev_map_list);
	spin_unlock(&dev_map_lock);

	return &dtab->map;
}

static void dev_map_free(struct bpf_map *map)
{
	struct bpf_dtab *dtab = container_of(map, struct bpf_dtab, map);
	int i;

	/* At this point bpf_prog->aux->refcnt == 0 and this map->refcnt == 0,
	 * so the programs (can be more than one that used this map) were
	 * disconnected from events. The following synchronize_rcu() guarantees
	 * both rcu read critical sections complete and waits for
	 * preempt-disable regions (NAPI being the relevant context here) so we
	 * are certain there will be no further reads against the netdev_map and
	 * all flush operations are complete. Flush operations can only be done
	 * from NAPI context for this reason.
	 */

	spin_lock(&dev_map_lock);
	list_del_rcu(&dtab->list);
	spin_unlock(&dev_map_lock);

	bpf_clear_redirect_map(map);
	synchronize_rcu();

	/* Make sure prior __dev_map_entry_free() have completed. */
	rcu_barrier();

	if (dtab->map.map_type == BPF_MAP_TYPE_DEVMAP_HASH) {
		for (i = 0; i < dtab->n_buckets; i++) {
			struct bpf_dtab_netdev *dev;
			struct hlist_head *head;
			struct hlist_node *next;

			head = dev_map_index_hash(dtab, i);

			hlist_for_each_entry_safe(dev, next, head, index_hlist) {
				hlist_del_rcu(&dev->index_hlist);
				if (dev->xdp_prog)
					bpf_prog_put(dev->xdp_prog);
				dev_put(dev->dev);
				kfree(dev);
			}
		}

		bpf_map_area_free(dtab->dev_index_head);
	} else {
		for (i = 0; i < dtab->map.max_entries; i++) {
			struct bpf_dtab_netdev *dev;

			dev = dtab->netdev_map[i];
			if (!dev)
				continue;

			if (dev->xdp_prog)
				bpf_prog_put(dev->xdp_prog);
			dev_put(dev->dev);
			kfree(dev);
		}

		bpf_map_area_free(dtab->netdev_map);
	}

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

struct bpf_dtab_netdev *__dev_map_hash_lookup_elem(struct bpf_map *map, u32 key)
{
	struct bpf_dtab *dtab = container_of(map, struct bpf_dtab, map);
	struct hlist_head *head = dev_map_index_hash(dtab, key);
	struct bpf_dtab_netdev *dev;

	hlist_for_each_entry_rcu(dev, head, index_hlist,
				 lockdep_is_held(&dtab->index_lock))
		if (dev->idx == key)
			return dev;

	return NULL;
}

static int dev_map_hash_get_next_key(struct bpf_map *map, void *key,
				    void *next_key)
{
	struct bpf_dtab *dtab = container_of(map, struct bpf_dtab, map);
	u32 idx, *next = next_key;
	struct bpf_dtab_netdev *dev, *next_dev;
	struct hlist_head *head;
	int i = 0;

	if (!key)
		goto find_first;

	idx = *(u32 *)key;

	dev = __dev_map_hash_lookup_elem(map, idx);
	if (!dev)
		goto find_first;

	next_dev = hlist_entry_safe(rcu_dereference_raw(hlist_next_rcu(&dev->index_hlist)),
				    struct bpf_dtab_netdev, index_hlist);

	if (next_dev) {
		*next = next_dev->idx;
		return 0;
	}

	i = idx & (dtab->n_buckets - 1);
	i++;

 find_first:
	for (; i < dtab->n_buckets; i++) {
		head = dev_map_index_hash(dtab, i);

		next_dev = hlist_entry_safe(rcu_dereference_raw(hlist_first_rcu(head)),
					    struct bpf_dtab_netdev,
					    index_hlist);
		if (next_dev) {
			*next = next_dev->idx;
			return 0;
		}
	}

	return -ENOENT;
}

bool dev_map_can_have_prog(struct bpf_map *map)
{
	if ((map->map_type == BPF_MAP_TYPE_DEVMAP ||
	     map->map_type == BPF_MAP_TYPE_DEVMAP_HASH) &&
	    map->value_size != offsetofend(struct bpf_devmap_val, ifindex))
		return true;

	return false;
}

static void bq_xmit_all(struct xdp_dev_bulk_queue *bq, u32 flags)
{
	struct net_device *dev = bq->dev;
	int sent = 0, drops = 0, err = 0;
	int i;

	if (unlikely(!bq->count))
		return;

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

	trace_xdp_devmap_xmit(bq->dev_rx, dev, sent, drops, err);
	bq->dev_rx = NULL;
	__list_del_clearprev(&bq->flush_node);
	return;
error:
	/* If ndo_xdp_xmit fails with an errno, no frames have been
	 * xmit'ed and it's our responsibility to them free all.
	 */
	for (i = 0; i < bq->count; i++) {
		struct xdp_frame *xdpf = bq->q[i];

		xdp_return_frame_rx_napi(xdpf);
		drops++;
	}
	goto out;
}

/* __dev_flush is called from xdp_do_flush() which _must_ be signaled
 * from the driver before returning from its napi->poll() routine. The poll()
 * routine is called either from busy_poll context or net_rx_action signaled
 * from NET_RX_SOFTIRQ. Either way the poll routine must complete before the
 * net device can be torn down. On devmap tear down we ensure the flush list
 * is empty before completing to ensure all flush operations have completed.
 * When drivers update the bpf program they may need to ensure any flush ops
 * are also complete. Using synchronize_rcu or call_rcu will suffice for this
 * because both wait for napi context to exit.
 */
void __dev_flush(void)
{
	struct list_head *flush_list = this_cpu_ptr(&dev_flush_list);
	struct xdp_dev_bulk_queue *bq, *tmp;

	list_for_each_entry_safe(bq, tmp, flush_list, flush_node)
		bq_xmit_all(bq, XDP_XMIT_FLUSH);
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
static void bq_enqueue(struct net_device *dev, struct xdp_frame *xdpf,
		       struct net_device *dev_rx)
{
	struct list_head *flush_list = this_cpu_ptr(&dev_flush_list);
	struct xdp_dev_bulk_queue *bq = this_cpu_ptr(dev->xdp_bulkq);

	if (unlikely(bq->count == DEV_MAP_BULK_SIZE))
		bq_xmit_all(bq, 0);

	/* Ingress dev_rx will be the same for all xdp_frame's in
	 * bulk_queue, because bq stored per-CPU and must be flushed
	 * from net_device drivers NAPI func end.
	 */
	if (!bq->dev_rx)
		bq->dev_rx = dev_rx;

	bq->q[bq->count++] = xdpf;

	if (!bq->flush_node.prev)
		list_add(&bq->flush_node, flush_list);
}

static inline int __xdp_enqueue(struct net_device *dev, struct xdp_buff *xdp,
			       struct net_device *dev_rx)
{
	struct xdp_frame *xdpf;
	int err;

	if (!dev->netdev_ops->ndo_xdp_xmit)
		return -EOPNOTSUPP;

	err = xdp_ok_fwd_dev(dev, xdp->data_end - xdp->data);
	if (unlikely(err))
		return err;

	xdpf = xdp_convert_buff_to_frame(xdp);
	if (unlikely(!xdpf))
		return -EOVERFLOW;

	bq_enqueue(dev, xdpf, dev_rx);
	return 0;
}

static struct xdp_buff *dev_map_run_prog(struct net_device *dev,
					 struct xdp_buff *xdp,
					 struct bpf_prog *xdp_prog)
{
	struct xdp_txq_info txq = { .dev = dev };
	u32 act;

	xdp_set_data_meta_invalid(xdp);
	xdp->txq = &txq;

	act = bpf_prog_run_xdp(xdp_prog, xdp);
	switch (act) {
	case XDP_PASS:
		return xdp;
	case XDP_DROP:
		break;
	default:
		bpf_warn_invalid_xdp_action(act);
		fallthrough;
	case XDP_ABORTED:
		trace_xdp_exception(dev, xdp_prog, act);
		break;
	}

	xdp_return_buff(xdp);
	return NULL;
}

int dev_xdp_enqueue(struct net_device *dev, struct xdp_buff *xdp,
		    struct net_device *dev_rx)
{
	return __xdp_enqueue(dev, xdp, dev_rx);
}

int dev_map_enqueue(struct bpf_dtab_netdev *dst, struct xdp_buff *xdp,
		    struct net_device *dev_rx)
{
	struct net_device *dev = dst->dev;

	if (dst->xdp_prog) {
		xdp = dev_map_run_prog(dev, xdp, dst->xdp_prog);
		if (!xdp)
			return 0;
	}
	return __xdp_enqueue(dev, xdp, dev_rx);
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

	return obj ? &obj->val : NULL;
}

static void *dev_map_hash_lookup_elem(struct bpf_map *map, void *key)
{
	struct bpf_dtab_netdev *obj = __dev_map_hash_lookup_elem(map,
								*(u32 *)key);
	return obj ? &obj->val : NULL;
}

static void __dev_map_entry_free(struct rcu_head *rcu)
{
	struct bpf_dtab_netdev *dev;

	dev = container_of(rcu, struct bpf_dtab_netdev, rcu);
	if (dev->xdp_prog)
		bpf_prog_put(dev->xdp_prog);
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
	 * completed as well as any flush operations because call_rcu
	 * will wait for preempt-disable region to complete, NAPI in this
	 * context.  And additionally, the driver tear down ensures all
	 * soft irqs are complete before removing the net device in the
	 * case of dev_put equals zero.
	 */
	old_dev = xchg(&dtab->netdev_map[k], NULL);
	if (old_dev)
		call_rcu(&old_dev->rcu, __dev_map_entry_free);
	return 0;
}

static int dev_map_hash_delete_elem(struct bpf_map *map, void *key)
{
	struct bpf_dtab *dtab = container_of(map, struct bpf_dtab, map);
	struct bpf_dtab_netdev *old_dev;
	int k = *(u32 *)key;
	unsigned long flags;
	int ret = -ENOENT;

	spin_lock_irqsave(&dtab->index_lock, flags);

	old_dev = __dev_map_hash_lookup_elem(map, k);
	if (old_dev) {
		dtab->items--;
		hlist_del_init_rcu(&old_dev->index_hlist);
		call_rcu(&old_dev->rcu, __dev_map_entry_free);
		ret = 0;
	}
	spin_unlock_irqrestore(&dtab->index_lock, flags);

	return ret;
}

static struct bpf_dtab_netdev *__dev_map_alloc_node(struct net *net,
						    struct bpf_dtab *dtab,
						    struct bpf_devmap_val *val,
						    unsigned int idx)
{
	struct bpf_prog *prog = NULL;
	struct bpf_dtab_netdev *dev;

	dev = kmalloc_node(sizeof(*dev), GFP_ATOMIC | __GFP_NOWARN,
			   dtab->map.numa_node);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	dev->dev = dev_get_by_index(net, val->ifindex);
	if (!dev->dev)
		goto err_out;

	if (val->bpf_prog.fd > 0) {
		prog = bpf_prog_get_type_dev(val->bpf_prog.fd,
					     BPF_PROG_TYPE_XDP, false);
		if (IS_ERR(prog))
			goto err_put_dev;
		if (prog->expected_attach_type != BPF_XDP_DEVMAP)
			goto err_put_prog;
	}

	dev->idx = idx;
	dev->dtab = dtab;
	if (prog) {
		dev->xdp_prog = prog;
		dev->val.bpf_prog.id = prog->aux->id;
	} else {
		dev->xdp_prog = NULL;
		dev->val.bpf_prog.id = 0;
	}
	dev->val.ifindex = val->ifindex;

	return dev;
err_put_prog:
	bpf_prog_put(prog);
err_put_dev:
	dev_put(dev->dev);
err_out:
	kfree(dev);
	return ERR_PTR(-EINVAL);
}

static int __dev_map_update_elem(struct net *net, struct bpf_map *map,
				 void *key, void *value, u64 map_flags)
{
	struct bpf_dtab *dtab = container_of(map, struct bpf_dtab, map);
	struct bpf_dtab_netdev *dev, *old_dev;
	struct bpf_devmap_val val = {};
	u32 i = *(u32 *)key;

	if (unlikely(map_flags > BPF_EXIST))
		return -EINVAL;
	if (unlikely(i >= dtab->map.max_entries))
		return -E2BIG;
	if (unlikely(map_flags == BPF_NOEXIST))
		return -EEXIST;

	/* already verified value_size <= sizeof val */
	memcpy(&val, value, map->value_size);

	if (!val.ifindex) {
		dev = NULL;
		/* can not specify fd if ifindex is 0 */
		if (val.bpf_prog.fd > 0)
			return -EINVAL;
	} else {
		dev = __dev_map_alloc_node(net, dtab, &val, i);
		if (IS_ERR(dev))
			return PTR_ERR(dev);
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

static int dev_map_update_elem(struct bpf_map *map, void *key, void *value,
			       u64 map_flags)
{
	return __dev_map_update_elem(current->nsproxy->net_ns,
				     map, key, value, map_flags);
}

static int __dev_map_hash_update_elem(struct net *net, struct bpf_map *map,
				     void *key, void *value, u64 map_flags)
{
	struct bpf_dtab *dtab = container_of(map, struct bpf_dtab, map);
	struct bpf_dtab_netdev *dev, *old_dev;
	struct bpf_devmap_val val = {};
	u32 idx = *(u32 *)key;
	unsigned long flags;
	int err = -EEXIST;

	/* already verified value_size <= sizeof val */
	memcpy(&val, value, map->value_size);

	if (unlikely(map_flags > BPF_EXIST || !val.ifindex))
		return -EINVAL;

	spin_lock_irqsave(&dtab->index_lock, flags);

	old_dev = __dev_map_hash_lookup_elem(map, idx);
	if (old_dev && (map_flags & BPF_NOEXIST))
		goto out_err;

	dev = __dev_map_alloc_node(net, dtab, &val, idx);
	if (IS_ERR(dev)) {
		err = PTR_ERR(dev);
		goto out_err;
	}

	if (old_dev) {
		hlist_del_rcu(&old_dev->index_hlist);
	} else {
		if (dtab->items >= dtab->map.max_entries) {
			spin_unlock_irqrestore(&dtab->index_lock, flags);
			call_rcu(&dev->rcu, __dev_map_entry_free);
			return -E2BIG;
		}
		dtab->items++;
	}

	hlist_add_head_rcu(&dev->index_hlist,
			   dev_map_index_hash(dtab, idx));
	spin_unlock_irqrestore(&dtab->index_lock, flags);

	if (old_dev)
		call_rcu(&old_dev->rcu, __dev_map_entry_free);

	return 0;

out_err:
	spin_unlock_irqrestore(&dtab->index_lock, flags);
	return err;
}

static int dev_map_hash_update_elem(struct bpf_map *map, void *key, void *value,
				   u64 map_flags)
{
	return __dev_map_hash_update_elem(current->nsproxy->net_ns,
					 map, key, value, map_flags);
}

static int dev_map_btf_id;
const struct bpf_map_ops dev_map_ops = {
	.map_meta_equal = bpf_map_meta_equal,
	.map_alloc = dev_map_alloc,
	.map_free = dev_map_free,
	.map_get_next_key = dev_map_get_next_key,
	.map_lookup_elem = dev_map_lookup_elem,
	.map_update_elem = dev_map_update_elem,
	.map_delete_elem = dev_map_delete_elem,
	.map_check_btf = map_check_no_btf,
	.map_btf_name = "bpf_dtab",
	.map_btf_id = &dev_map_btf_id,
};

static int dev_map_hash_map_btf_id;
const struct bpf_map_ops dev_map_hash_ops = {
	.map_meta_equal = bpf_map_meta_equal,
	.map_alloc = dev_map_alloc,
	.map_free = dev_map_free,
	.map_get_next_key = dev_map_hash_get_next_key,
	.map_lookup_elem = dev_map_hash_lookup_elem,
	.map_update_elem = dev_map_hash_update_elem,
	.map_delete_elem = dev_map_hash_delete_elem,
	.map_check_btf = map_check_no_btf,
	.map_btf_name = "bpf_dtab",
	.map_btf_id = &dev_map_hash_map_btf_id,
};

static void dev_map_hash_remove_netdev(struct bpf_dtab *dtab,
				       struct net_device *netdev)
{
	unsigned long flags;
	u32 i;

	spin_lock_irqsave(&dtab->index_lock, flags);
	for (i = 0; i < dtab->n_buckets; i++) {
		struct bpf_dtab_netdev *dev;
		struct hlist_head *head;
		struct hlist_node *next;

		head = dev_map_index_hash(dtab, i);

		hlist_for_each_entry_safe(dev, next, head, index_hlist) {
			if (netdev != dev->dev)
				continue;

			dtab->items--;
			hlist_del_rcu(&dev->index_hlist);
			call_rcu(&dev->rcu, __dev_map_entry_free);
		}
	}
	spin_unlock_irqrestore(&dtab->index_lock, flags);
}

static int dev_map_notification(struct notifier_block *notifier,
				ulong event, void *ptr)
{
	struct net_device *netdev = netdev_notifier_info_to_dev(ptr);
	struct bpf_dtab *dtab;
	int i, cpu;

	switch (event) {
	case NETDEV_REGISTER:
		if (!netdev->netdev_ops->ndo_xdp_xmit || netdev->xdp_bulkq)
			break;

		/* will be freed in free_netdev() */
		netdev->xdp_bulkq = alloc_percpu(struct xdp_dev_bulk_queue);
		if (!netdev->xdp_bulkq)
			return NOTIFY_BAD;

		for_each_possible_cpu(cpu)
			per_cpu_ptr(netdev->xdp_bulkq, cpu)->dev = netdev;
		break;
	case NETDEV_UNREGISTER:
		/* This rcu_read_lock/unlock pair is needed because
		 * dev_map_list is an RCU list AND to ensure a delete
		 * operation does not free a netdev_map entry while we
		 * are comparing it against the netdev being unregistered.
		 */
		rcu_read_lock();
		list_for_each_entry_rcu(dtab, &dev_map_list, list) {
			if (dtab->map.map_type == BPF_MAP_TYPE_DEVMAP_HASH) {
				dev_map_hash_remove_netdev(dtab, netdev);
				continue;
			}

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
	int cpu;

	/* Assure tracepoint shadow struct _bpf_dtab_netdev is in sync */
	BUILD_BUG_ON(offsetof(struct bpf_dtab_netdev, dev) !=
		     offsetof(struct _bpf_dtab_netdev, dev));
	register_netdevice_notifier(&dev_map_notifier);

	for_each_possible_cpu(cpu)
		INIT_LIST_HEAD(&per_cpu(dev_flush_list, cpu));
	return 0;
}

subsys_initcall(dev_map_init);
