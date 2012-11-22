/*
 * net/core/netprio_cgroup.c	Priority Control Group
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Neil Horman <nhorman@tuxdriver.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/cgroup.h>
#include <linux/rcupdate.h>
#include <linux/atomic.h>
#include <net/rtnetlink.h>
#include <net/pkt_cls.h>
#include <net/sock.h>
#include <net/netprio_cgroup.h>

#include <linux/fdtable.h>

#define PRIOMAP_MIN_SZ		128
#define PRIOIDX_SZ 128

static unsigned long prioidx_map[PRIOIDX_SZ];
static DEFINE_SPINLOCK(prioidx_map_lock);

static inline struct cgroup_netprio_state *cgrp_netprio_state(struct cgroup *cgrp)
{
	return container_of(cgroup_subsys_state(cgrp, net_prio_subsys_id),
			    struct cgroup_netprio_state, css);
}

static int get_prioidx(u32 *prio)
{
	unsigned long flags;
	u32 prioidx;

	spin_lock_irqsave(&prioidx_map_lock, flags);
	prioidx = find_first_zero_bit(prioidx_map, sizeof(unsigned long) * PRIOIDX_SZ);
	if (prioidx == sizeof(unsigned long) * PRIOIDX_SZ) {
		spin_unlock_irqrestore(&prioidx_map_lock, flags);
		return -ENOSPC;
	}
	set_bit(prioidx, prioidx_map);
	spin_unlock_irqrestore(&prioidx_map_lock, flags);
	*prio = prioidx;
	return 0;
}

static void put_prioidx(u32 idx)
{
	unsigned long flags;

	spin_lock_irqsave(&prioidx_map_lock, flags);
	clear_bit(idx, prioidx_map);
	spin_unlock_irqrestore(&prioidx_map_lock, flags);
}

/*
 * Extend @dev->priomap so that it's large enough to accomodate
 * @target_idx.  @dev->priomap.priomap_len > @target_idx after successful
 * return.  Must be called under rtnl lock.
 */
static int extend_netdev_table(struct net_device *dev, u32 target_idx)
{
	struct netprio_map *old, *new;
	size_t new_sz, new_len;

	/* is the existing priomap large enough? */
	old = rtnl_dereference(dev->priomap);
	if (old && old->priomap_len > target_idx)
		return 0;

	/*
	 * Determine the new size.  Let's keep it power-of-two.  We start
	 * from PRIOMAP_MIN_SZ and double it until it's large enough to
	 * accommodate @target_idx.
	 */
	new_sz = PRIOMAP_MIN_SZ;
	while (true) {
		new_len = (new_sz - offsetof(struct netprio_map, priomap)) /
			sizeof(new->priomap[0]);
		if (new_len > target_idx)
			break;
		new_sz *= 2;
		/* overflowed? */
		if (WARN_ON(new_sz < PRIOMAP_MIN_SZ))
			return -ENOSPC;
	}

	/* allocate & copy */
	new = kzalloc(new_sz, GFP_KERNEL);
	if (!new) {
		pr_warn("Unable to alloc new priomap!\n");
		return -ENOMEM;
	}

	if (old)
		memcpy(new->priomap, old->priomap,
		       old->priomap_len * sizeof(old->priomap[0]));

	new->priomap_len = new_len;

	/* install the new priomap */
	rcu_assign_pointer(dev->priomap, new);
	if (old)
		kfree_rcu(old, rcu);
	return 0;
}

static struct cgroup_subsys_state *cgrp_css_alloc(struct cgroup *cgrp)
{
	struct cgroup_netprio_state *cs;
	int ret = -EINVAL;

	cs = kzalloc(sizeof(*cs), GFP_KERNEL);
	if (!cs)
		return ERR_PTR(-ENOMEM);

	if (cgrp->parent && cgrp_netprio_state(cgrp->parent)->prioidx)
		goto out;

	ret = get_prioidx(&cs->prioidx);
	if (ret < 0) {
		pr_warn("No space in priority index array\n");
		goto out;
	}

	return &cs->css;
out:
	kfree(cs);
	return ERR_PTR(ret);
}

static void cgrp_css_free(struct cgroup *cgrp)
{
	struct cgroup_netprio_state *cs;
	struct net_device *dev;
	struct netprio_map *map;

	cs = cgrp_netprio_state(cgrp);
	rtnl_lock();
	for_each_netdev(&init_net, dev) {
		map = rtnl_dereference(dev->priomap);
		if (map && cs->prioidx < map->priomap_len)
			map->priomap[cs->prioidx] = 0;
	}
	rtnl_unlock();
	put_prioidx(cs->prioidx);
	kfree(cs);
}

static u64 read_prioidx(struct cgroup *cgrp, struct cftype *cft)
{
	return (u64)cgrp_netprio_state(cgrp)->prioidx;
}

static int read_priomap(struct cgroup *cont, struct cftype *cft,
			struct cgroup_map_cb *cb)
{
	struct net_device *dev;
	u32 prioidx = cgrp_netprio_state(cont)->prioidx;
	u32 priority;
	struct netprio_map *map;

	rcu_read_lock();
	for_each_netdev_rcu(&init_net, dev) {
		map = rcu_dereference(dev->priomap);
		priority = (map && prioidx < map->priomap_len) ? map->priomap[prioidx] : 0;
		cb->fill(cb, dev->name, priority);
	}
	rcu_read_unlock();
	return 0;
}

static int write_priomap(struct cgroup *cgrp, struct cftype *cft,
			 const char *buffer)
{
	u32 prioidx = cgrp_netprio_state(cgrp)->prioidx;
	char devname[IFNAMSIZ + 1];
	struct net_device *dev;
	struct netprio_map *map;
	u32 prio;
	int ret;

	if (sscanf(buffer, "%"__stringify(IFNAMSIZ)"s %u", devname, &prio) != 2)
		return -EINVAL;

	dev = dev_get_by_name(&init_net, devname);
	if (!dev)
		return -ENODEV;

	rtnl_lock();

	ret = extend_netdev_table(dev, prioidx);
	if (ret)
		goto out_unlock;

	map = rtnl_dereference(dev->priomap);
	if (map)
		map->priomap[prioidx] = prio;
out_unlock:
	rtnl_unlock();
	dev_put(dev);
	return ret;
}

static int update_netprio(const void *v, struct file *file, unsigned n)
{
	int err;
	struct socket *sock = sock_from_file(file, &err);
	if (sock)
		sock->sk->sk_cgrp_prioidx = (u32)(unsigned long)v;
	return 0;
}

void net_prio_attach(struct cgroup *cgrp, struct cgroup_taskset *tset)
{
	struct task_struct *p;
	void *v;

	cgroup_taskset_for_each(p, cgrp, tset) {
		task_lock(p);
		v = (void *)(unsigned long)task_netprioidx(p);
		iterate_fd(p->files, 0, update_netprio, v);
		task_unlock(p);
	}
}

static struct cftype ss_files[] = {
	{
		.name = "prioidx",
		.read_u64 = read_prioidx,
	},
	{
		.name = "ifpriomap",
		.read_map = read_priomap,
		.write_string = write_priomap,
	},
	{ }	/* terminate */
};

struct cgroup_subsys net_prio_subsys = {
	.name		= "net_prio",
	.css_alloc	= cgrp_css_alloc,
	.css_free	= cgrp_css_free,
	.attach		= net_prio_attach,
	.subsys_id	= net_prio_subsys_id,
	.base_cftypes	= ss_files,
	.module		= THIS_MODULE,

	/*
	 * net_prio has artificial limit on the number of cgroups and
	 * disallows nesting making it impossible to co-mount it with other
	 * hierarchical subsystems.  Remove the artificially low PRIOIDX_SZ
	 * limit and properly nest configuration such that children follow
	 * their parents' configurations by default and are allowed to
	 * override and remove the following.
	 */
	.broken_hierarchy = true,
};

static int netprio_device_event(struct notifier_block *unused,
				unsigned long event, void *ptr)
{
	struct net_device *dev = ptr;
	struct netprio_map *old;

	/*
	 * Note this is called with rtnl_lock held so we have update side
	 * protection on our rcu assignments
	 */

	switch (event) {
	case NETDEV_UNREGISTER:
		old = rtnl_dereference(dev->priomap);
		RCU_INIT_POINTER(dev->priomap, NULL);
		if (old)
			kfree_rcu(old, rcu);
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block netprio_device_notifier = {
	.notifier_call = netprio_device_event
};

static int __init init_cgroup_netprio(void)
{
	int ret;

	ret = cgroup_load_subsys(&net_prio_subsys);
	if (ret)
		goto out;

	register_netdevice_notifier(&netprio_device_notifier);

out:
	return ret;
}

static void __exit exit_cgroup_netprio(void)
{
	struct netprio_map *old;
	struct net_device *dev;

	unregister_netdevice_notifier(&netprio_device_notifier);

	cgroup_unload_subsys(&net_prio_subsys);

	rtnl_lock();
	for_each_netdev(&init_net, dev) {
		old = rtnl_dereference(dev->priomap);
		RCU_INIT_POINTER(dev->priomap, NULL);
		if (old)
			kfree_rcu(old, rcu);
	}
	rtnl_unlock();
}

module_init(init_cgroup_netprio);
module_exit(exit_cgroup_netprio);
MODULE_LICENSE("GPL v2");
