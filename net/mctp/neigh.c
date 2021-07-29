// SPDX-License-Identifier: GPL-2.0
/*
 * Management Component Transport Protocol (MCTP) - routing
 * implementation.
 *
 * This is currently based on a simple routing table, with no dst cache. The
 * number of routes should stay fairly small, so the lookup cost is small.
 *
 * Copyright (c) 2021 Code Construct
 * Copyright (c) 2021 Google
 */

#include <linux/idr.h>
#include <linux/mctp.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>

#include <net/mctp.h>
#include <net/mctpdevice.h>
#include <net/netlink.h>
#include <net/sock.h>

static int __always_unused mctp_neigh_add(struct mctp_dev *mdev, mctp_eid_t eid,
					  enum mctp_neigh_source source,
					  size_t lladdr_len, const void *lladdr)
{
	struct net *net = dev_net(mdev->dev);
	struct mctp_neigh *neigh;
	int rc;

	mutex_lock(&net->mctp.neigh_lock);
	if (mctp_neigh_lookup(mdev, eid, NULL) == 0) {
		rc = -EEXIST;
		goto out;
	}

	if (lladdr_len > sizeof(neigh->ha)) {
		rc = -EINVAL;
		goto out;
	}

	neigh = kzalloc(sizeof(*neigh), GFP_KERNEL);
	if (!neigh) {
		rc = -ENOMEM;
		goto out;
	}
	INIT_LIST_HEAD(&neigh->list);
	neigh->dev = mdev;
	dev_hold(neigh->dev->dev);
	neigh->eid = eid;
	neigh->source = source;
	memcpy(neigh->ha, lladdr, lladdr_len);

	list_add_rcu(&neigh->list, &net->mctp.neighbours);
	rc = 0;
out:
	mutex_unlock(&net->mctp.neigh_lock);
	return rc;
}

static void __mctp_neigh_free(struct rcu_head *rcu)
{
	struct mctp_neigh *neigh = container_of(rcu, struct mctp_neigh, rcu);

	dev_put(neigh->dev->dev);
	kfree(neigh);
}

/* Removes all neighbour entries referring to a device */
void mctp_neigh_remove_dev(struct mctp_dev *mdev)
{
	struct net *net = dev_net(mdev->dev);
	struct mctp_neigh *neigh, *tmp;

	mutex_lock(&net->mctp.neigh_lock);
	list_for_each_entry_safe(neigh, tmp, &net->mctp.neighbours, list) {
		if (neigh->dev == mdev) {
			list_del_rcu(&neigh->list);
			/* TODO: immediate RTM_DELNEIGH */
			call_rcu(&neigh->rcu, __mctp_neigh_free);
		}
	}

	mutex_unlock(&net->mctp.neigh_lock);
}

int mctp_neigh_lookup(struct mctp_dev *mdev, mctp_eid_t eid, void *ret_hwaddr)
{
	struct net *net = dev_net(mdev->dev);
	struct mctp_neigh *neigh;
	int rc = -EHOSTUNREACH; // TODO: or ENOENT?

	rcu_read_lock();
	list_for_each_entry_rcu(neigh, &net->mctp.neighbours, list) {
		if (mdev == neigh->dev && eid == neigh->eid) {
			if (ret_hwaddr)
				memcpy(ret_hwaddr, neigh->ha,
				       sizeof(neigh->ha));
			rc = 0;
			break;
		}
	}
	rcu_read_unlock();
	return rc;
}

/* namespace registration */
static int __net_init mctp_neigh_net_init(struct net *net)
{
	struct netns_mctp *ns = &net->mctp;

	INIT_LIST_HEAD(&ns->neighbours);
	return 0;
}

static void __net_exit mctp_neigh_net_exit(struct net *net)
{
	struct netns_mctp *ns = &net->mctp;
	struct mctp_neigh *neigh;

	list_for_each_entry(neigh, &ns->neighbours, list)
		call_rcu(&neigh->rcu, __mctp_neigh_free);
}

/* net namespace implementation */

static struct pernet_operations mctp_net_ops = {
	.init = mctp_neigh_net_init,
	.exit = mctp_neigh_net_exit,
};

int __init mctp_neigh_init(void)
{
	return register_pernet_subsys(&mctp_net_ops);
}

void __exit mctp_neigh_exit(void)
{
	unregister_pernet_subsys(&mctp_net_ops);
}
