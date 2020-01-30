// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * net/core/netprio_cgroup.c	Priority Control Group
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
#include <linux/sched/task.h>

#include <net/rtnetlink.h>
#include <net/pkt_cls.h>
#include <net/sock.h>
#include <net/netprio_cgroup.h>

#include <linux/fdtable.h>

/*
 * netprio allocates per-net_device priomap array which is indexed by
 * css->id.  Limiting css ID to 16bits doesn't lose anything.
 */
#define NETPRIO_ID_MAX		USHRT_MAX

#define PRIOMAP_MIN_SZ		128

/*
 * Extend @dev->priomap so that it's large enough to accommodate
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
	if (!new)
		return -ENOMEM;

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

/**
 * netprio_prio - return the effective netprio of a cgroup-net_device pair
 * @css: css part of the target pair
 * @dev: net_device part of the target pair
 *
 * Should be called under RCU read or rtnl lock.
 */
static u32 netprio_prio(struct cgroup_subsys_state *css, struct net_device *dev)
{
	struct netprio_map *map = rcu_dereference_rtnl(dev->priomap);
	int id = css->id;

	if (map && id < map->priomap_len)
		return map->priomap[id];
	return 0;
}

/**
 * netprio_set_prio - set netprio on a cgroup-net_device pair
 * @css: css part of the target pair
 * @dev: net_device part of the target pair
 * @prio: prio to set
 *
 * Set netprio to @prio on @css-@dev pair.  Should be called under rtnl
 * lock and may fail under memory pressure for non-zero @prio.
 */
static int netprio_set_prio(struct cgroup_subsys_state *css,
			    struct net_device *dev, u32 prio)
{
	struct netprio_map *map;
	int id = css->id;
	int ret;

	/* avoid extending priomap for zero writes */
	map = rtnl_dereference(dev->priomap);
	if (!prio && (!map || map->priomap_len <= id))
		return 0;

	ret = extend_netdev_table(dev, id);
	if (ret)
		return ret;

	map = rtnl_dereference(dev->priomap);
	map->priomap[id] = prio;
	return 0;
}

static struct cgroup_subsys_state *
cgrp_css_alloc(struct cgroup_subsys_state *parent_css)
{
	struct cgroup_subsys_state *css;

	css = kzalloc(sizeof(*css), GFP_KERNEL);
	if (!css)
		return ERR_PTR(-ENOMEM);

	return css;
}

static int cgrp_css_online(struct cgroup_subsys_state *css)
{
	struct cgroup_subsys_state *parent_css = css->parent;
	struct net_device *dev;
	int ret = 0;

	if (css->id > NETPRIO_ID_MAX)
		return -ENOSPC;

	if (!parent_css)
		return 0;

	rtnl_lock();
	/*
	 * Inherit prios from the parent.  As all prios are set during
	 * onlining, there is no need to clear them on offline.
	 */
	for_each_netdev(&init_net, dev) {
		u32 prio = netprio_prio(parent_css, dev);

		ret = netprio_set_prio(css, dev, prio);
		if (ret)
			break;
	}
	rtnl_unlock();
	return ret;
}

static void cgrp_css_free(struct cgroup_subsys_state *css)
{
	kfree(css);
}

static u64 read_prioidx(struct cgroup_subsys_state *css, struct cftype *cft)
{
	return css->id;
}

static int read_priomap(struct seq_file *sf, void *v)
{
	struct net_device *dev;

	rcu_read_lock();
	for_each_netdev_rcu(&init_net, dev)
		seq_printf(sf, "%s %u\n", dev->name,
			   netprio_prio(seq_css(sf), dev));
	rcu_read_unlock();
	return 0;
}

static ssize_t write_priomap(struct kernfs_open_file *of,
			     char *buf, size_t nbytes, loff_t off)
{
	char devname[IFNAMSIZ + 1];
	struct net_device *dev;
	u32 prio;
	int ret;

	if (sscanf(buf, "%"__stringify(IFNAMSIZ)"s %u", devname, &prio) != 2)
		return -EINVAL;

	dev = dev_get_by_name(&init_net, devname);
	if (!dev)
		return -ENODEV;

	cgroup_sk_alloc_disable();

	rtnl_lock();

	ret = netprio_set_prio(of_css(of), dev, prio);

	rtnl_unlock();
	dev_put(dev);
	return ret ?: nbytes;
}

static int update_netprio(const void *v, struct file *file, unsigned n)
{
	int err;
	struct socket *sock = sock_from_file(file, &err);
	if (sock) {
		spin_lock(&cgroup_sk_update_lock);
		sock_cgroup_set_prioidx(&sock->sk->sk_cgrp_data,
					(unsigned long)v);
		spin_unlock(&cgroup_sk_update_lock);
	}
	return 0;
}

static void net_prio_attach(struct cgroup_taskset *tset)
{
	struct task_struct *p;
	struct cgroup_subsys_state *css;

	cgroup_taskset_for_each(p, css, tset) {
		void *v = (void *)(unsigned long)css->id;

		task_lock(p);
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
		.seq_show = read_priomap,
		.write = write_priomap,
	},
	{ }	/* terminate */
};

struct cgroup_subsys net_prio_cgrp_subsys = {
	.css_alloc	= cgrp_css_alloc,
	.css_online	= cgrp_css_online,
	.css_free	= cgrp_css_free,
	.attach		= net_prio_attach,
	.legacy_cftypes	= ss_files,
};

static int netprio_device_event(struct notifier_block *unused,
				unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
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
	register_netdevice_notifier(&netprio_device_notifier);
	return 0;
}
subsys_initcall(init_cgroup_netprio);
