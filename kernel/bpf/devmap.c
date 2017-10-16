/* Copyright (c) 2017 Covalent IO, Inc. http://covalent.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
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
 * an atomic per-cpu bitmap. The bpf_dtab_netdev object will not be destroyed
 * until all bits are cleared indicating outstanding flush operations have
 * completed.
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
#include <linux/filter.h>

struct bpf_dtab_netdev {
	struct net_device *dev;
	struct bpf_dtab *dtab;
	unsigned int bit;
	struct rcu_head rcu;
};

struct bpf_dtab {
	struct bpf_map map;
	struct bpf_dtab_netdev **netdev_map;
	unsigned long __percpu *flush_needed;
	struct list_head list;
};

static DEFINE_SPINLOCK(dev_map_lock);
static LIST_HEAD(dev_map_list);

static u64 dev_map_bitmap_size(const union bpf_attr *attr)
{
	return BITS_TO_LONGS(attr->max_entries) * sizeof(unsigned long);
}

static struct bpf_map *dev_map_alloc(union bpf_attr *attr)
{
	struct bpf_dtab *dtab;
	int err = -EINVAL;
	u64 cost;

	/* check sanity of attributes */
	if (attr->max_entries == 0 || attr->key_size != 4 ||
	    attr->value_size != 4 || attr->map_flags & ~BPF_F_NUMA_NODE)
		return ERR_PTR(-EINVAL);

	dtab = kzalloc(sizeof(*dtab), GFP_USER);
	if (!dtab)
		return ERR_PTR(-ENOMEM);

	/* mandatory map attributes */
	dtab->map.map_type = attr->map_type;
	dtab->map.key_size = attr->key_size;
	dtab->map.value_size = attr->value_size;
	dtab->map.max_entries = attr->max_entries;
	dtab->map.map_flags = attr->map_flags;
	dtab->map.numa_node = bpf_map_attr_numa_node(attr);

	/* make sure page count doesn't overflow */
	cost = (u64) dtab->map.max_entries * sizeof(struct bpf_dtab_netdev *);
	cost += dev_map_bitmap_size(attr) * num_possible_cpus();
	if (cost >= U32_MAX - PAGE_SIZE)
		goto free_dtab;

	dtab->map.pages = round_up(cost, PAGE_SIZE) >> PAGE_SHIFT;

	/* if map size is larger than memlock limit, reject it early */
	err = bpf_map_precharge_memlock(dtab->map.pages);
	if (err)
		goto free_dtab;

	err = -ENOMEM;

	/* A per cpu bitfield with a bit per possible net device */
	dtab->flush_needed = __alloc_percpu(dev_map_bitmap_size(attr),
					    __alignof__(unsigned long));
	if (!dtab->flush_needed)
		goto free_dtab;

	dtab->netdev_map = bpf_map_area_alloc(dtab->map.max_entries *
					      sizeof(struct bpf_dtab_netdev *),
					      dtab->map.numa_node);
	if (!dtab->netdev_map)
		goto free_dtab;

	spin_lock(&dev_map_lock);
	list_add_tail_rcu(&dtab->list, &dev_map_list);
	spin_unlock(&dev_map_lock);

	return &dtab->map;
free_dtab:
	free_percpu(dtab->flush_needed);
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

	synchronize_rcu();

	/* To ensure all pending flush operations have completed wait for flush
	 * bitmap to indicate all flush_needed bits to be zero on _all_ cpus.
	 * Because the above synchronize_rcu() ensures the map is disconnected
	 * from the program we can assume no new bits will be set.
	 */
	for_each_online_cpu(cpu) {
		unsigned long *bitmap = per_cpu_ptr(dtab->flush_needed, cpu);

		while (!bitmap_empty(bitmap, dtab->map.max_entries))
			cond_resched();
	}

	for (i = 0; i < dtab->map.max_entries; i++) {
		struct bpf_dtab_netdev *dev;

		dev = dtab->netdev_map[i];
		if (!dev)
			continue;

		dev_put(dev->dev);
		kfree(dev);
	}

	free_percpu(dtab->flush_needed);
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

void __dev_map_insert_ctx(struct bpf_map *map, u32 bit)
{
	struct bpf_dtab *dtab = container_of(map, struct bpf_dtab, map);
	unsigned long *bitmap = this_cpu_ptr(dtab->flush_needed);

	__set_bit(bit, bitmap);
}

/* __dev_map_flush is called from xdp_do_flush_map() which _must_ be signaled
 * from the driver before returning from its napi->poll() routine. The poll()
 * routine is called either from busy_poll context or net_rx_action signaled
 * from NET_RX_SOFTIRQ. Either way the poll routine must complete before the
 * net device can be torn down. On devmap tear down we ensure the ctx bitmap
 * is zeroed before completing to ensure all flush operations have completed.
 */
void __dev_map_flush(struct bpf_map *map)
{
	struct bpf_dtab *dtab = container_of(map, struct bpf_dtab, map);
	unsigned long *bitmap = this_cpu_ptr(dtab->flush_needed);
	u32 bit;

	for_each_set_bit(bit, bitmap, map->max_entries) {
		struct bpf_dtab_netdev *dev = READ_ONCE(dtab->netdev_map[bit]);
		struct net_device *netdev;

		/* This is possible if the dev entry is removed by user space
		 * between xdp redirect and flush op.
		 */
		if (unlikely(!dev))
			continue;

		__clear_bit(bit, bitmap);
		netdev = dev->dev;
		if (likely(netdev->netdev_ops->ndo_xdp_flush))
			netdev->netdev_ops->ndo_xdp_flush(netdev);
	}
}

/* rcu_read_lock (from syscall and BPF contexts) ensures that if a delete and/or
 * update happens in parallel here a dev_put wont happen until after reading the
 * ifindex.
 */
struct net_device  *__dev_map_lookup_elem(struct bpf_map *map, u32 key)
{
	struct bpf_dtab *dtab = container_of(map, struct bpf_dtab, map);
	struct bpf_dtab_netdev *dev;

	if (key >= map->max_entries)
		return NULL;

	dev = READ_ONCE(dtab->netdev_map[key]);
	return dev ? dev->dev : NULL;
}

static void *dev_map_lookup_elem(struct bpf_map *map, void *key)
{
	struct net_device *dev = __dev_map_lookup_elem(map, *(u32 *)key);

	return dev ? &dev->ifindex : NULL;
}

static void dev_map_flush_old(struct bpf_dtab_netdev *dev)
{
	if (dev->dev->netdev_ops->ndo_xdp_flush) {
		struct net_device *fl = dev->dev;
		unsigned long *bitmap;
		int cpu;

		for_each_online_cpu(cpu) {
			bitmap = per_cpu_ptr(dev->dtab->flush_needed, cpu);
			__clear_bit(dev->bit, bitmap);

			fl->netdev_ops->ndo_xdp_flush(dev->dev);
		}
	}
}

static void __dev_map_entry_free(struct rcu_head *rcu)
{
	struct bpf_dtab_netdev *dev;

	dev = container_of(rcu, struct bpf_dtab_netdev, rcu);
	dev_map_flush_old(dev);
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
	struct bpf_dtab_netdev *dev, *old_dev;
	u32 i = *(u32 *)key;
	u32 ifindex = *(u32 *)value;

	if (unlikely(map_flags > BPF_EXIST))
		return -EINVAL;
	if (unlikely(i >= dtab->map.max_entries))
		return -E2BIG;
	if (unlikely(map_flags == BPF_NOEXIST))
		return -EEXIST;

	if (!ifindex) {
		dev = NULL;
	} else {
		dev = kmalloc_node(sizeof(*dev), GFP_ATOMIC | __GFP_NOWARN,
				   map->numa_node);
		if (!dev)
			return -ENOMEM;

		dev->dev = dev_get_by_index(net, ifindex);
		if (!dev->dev) {
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
				if (!dev ||
				    dev->dev->ifindex != netdev->ifindex)
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
	register_netdevice_notifier(&dev_map_notifier);
	return 0;
}

subsys_initcall(dev_map_init);
