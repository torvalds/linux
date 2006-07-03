/*
 *              Weighted random policy for multipath.
 *
 *
 * Version:	$Id: multipath_wrandom.c,v 1.1.2.3 2004/09/22 07:51:40 elueck Exp $
 *
 * Authors:	Einar Lueck <elueck@de.ibm.com><lkml@einar-lueck.de>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <asm/system.h>
#include <asm/uaccess.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/igmp.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/module.h>
#include <linux/mroute.h>
#include <linux/init.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/icmp.h>
#include <net/udp.h>
#include <net/raw.h>
#include <linux/notifier.h>
#include <linux/if_arp.h>
#include <linux/netfilter_ipv4.h>
#include <net/ipip.h>
#include <net/checksum.h>
#include <net/ip_fib.h>
#include <net/ip_mp_alg.h>

#define MULTIPATH_STATE_SIZE 15

struct multipath_candidate {
	struct multipath_candidate	*next;
	int				power;
	struct rtable			*rt;
};

struct multipath_dest {
	struct list_head	list;

	const struct fib_nh	*nh_info;
	__u32			netmask;
	__u32			network;
	unsigned char		prefixlen;

	struct rcu_head		rcu;
};

struct multipath_bucket {
	struct list_head	head;
	spinlock_t		lock;
};

struct multipath_route {
	struct list_head	list;

	int			oif;
	__u32			gw;
	struct list_head	dests;

	struct rcu_head		rcu;
};

/* state: primarily weight per route information */
static struct multipath_bucket state[MULTIPATH_STATE_SIZE];

/* interface to random number generation */
static unsigned int RANDOM_SEED = 93186752;

static inline unsigned int random(unsigned int ubound)
{
	static unsigned int a = 1588635695,
		q = 2,
		r = 1117695901;
	RANDOM_SEED = a*(RANDOM_SEED % q) - r*(RANDOM_SEED / q);
	return RANDOM_SEED % ubound;
}

static unsigned char __multipath_lookup_weight(const struct flowi *fl,
					       const struct rtable *rt)
{
	const int state_idx = rt->idev->dev->ifindex % MULTIPATH_STATE_SIZE;
	struct multipath_route *r;
	struct multipath_route *target_route = NULL;
	struct multipath_dest *d;
	int weight = 1;

	/* lookup the weight information for a certain route */
	rcu_read_lock();

	/* find state entry for gateway or add one if necessary */
	list_for_each_entry_rcu(r, &state[state_idx].head, list) {
		if (r->gw == rt->rt_gateway &&
		    r->oif == rt->idev->dev->ifindex) {
			target_route = r;
			break;
		}
	}

	if (!target_route) {
		/* this should not happen... but we are prepared */
		printk( KERN_CRIT"%s: missing state for gateway: %u and " \
			"device %d\n", __FUNCTION__, rt->rt_gateway,
			rt->idev->dev->ifindex);
		goto out;
	}

	/* find state entry for destination */
	list_for_each_entry_rcu(d, &target_route->dests, list) {
		__u32 targetnetwork = fl->fl4_dst & 
			(0xFFFFFFFF >> (32 - d->prefixlen));

		if ((targetnetwork & d->netmask) == d->network) {
			weight = d->nh_info->nh_weight;
			goto out;
		}
	}

out:
	rcu_read_unlock();
	return weight;
}

static void wrandom_init_state(void) 
{
	int i;

	for (i = 0; i < MULTIPATH_STATE_SIZE; ++i) {
		INIT_LIST_HEAD(&state[i].head);
		spin_lock_init(&state[i].lock);
	}
}

static void wrandom_select_route(const struct flowi *flp,
				 struct rtable *first,
				 struct rtable **rp)
{
	struct rtable *rt;
	struct rtable *decision;
	struct multipath_candidate *first_mpc = NULL;
	struct multipath_candidate *mpc, *last_mpc = NULL;
	int power = 0;
	int last_power;
	int selector;
	const size_t size_mpc = sizeof(struct multipath_candidate);

	/* collect all candidates and identify their weights */
	for (rt = rcu_dereference(first); rt;
	     rt = rcu_dereference(rt->u.rt_next)) {
		if ((rt->u.dst.flags & DST_BALANCED) != 0 &&
		    multipath_comparekeys(&rt->fl, flp)) {
			struct multipath_candidate* mpc =
				(struct multipath_candidate*)
				kmalloc(size_mpc, GFP_ATOMIC);

			if (!mpc)
				return;

			power += __multipath_lookup_weight(flp, rt) * 10000;

			mpc->power = power;
			mpc->rt = rt;
			mpc->next = NULL;

			if (!first_mpc)
				first_mpc = mpc;
			else
				last_mpc->next = mpc;

			last_mpc = mpc;
		}
	}

	/* choose a weighted random candidate */
	decision = first;
	selector = random(power);
	last_power = 0;

	/* select candidate, adjust GC data and cleanup local state */
	decision = first;
	last_mpc = NULL;
	for (mpc = first_mpc; mpc; mpc = mpc->next) {
		mpc->rt->u.dst.lastuse = jiffies;
		if (last_power <= selector && selector < mpc->power)
			decision = mpc->rt;

		last_power = mpc->power;
		kfree(last_mpc);
		last_mpc = mpc;
	}

	/* concurrent __multipath_flush may lead to !last_mpc */
	kfree(last_mpc);

	decision->u.dst.__use++;
	*rp = decision;
}

static void wrandom_set_nhinfo(__u32 network,
			       __u32 netmask,
			       unsigned char prefixlen,
			       const struct fib_nh *nh)
{
	const int state_idx = nh->nh_oif % MULTIPATH_STATE_SIZE;
	struct multipath_route *r, *target_route = NULL;
	struct multipath_dest *d, *target_dest = NULL;

	/* store the weight information for a certain route */
	spin_lock_bh(&state[state_idx].lock);

	/* find state entry for gateway or add one if necessary */
	list_for_each_entry_rcu(r, &state[state_idx].head, list) {
		if (r->gw == nh->nh_gw && r->oif == nh->nh_oif) {
			target_route = r;
			break;
		}
	}

	if (!target_route) {
		const size_t size_rt = sizeof(struct multipath_route);
		target_route = (struct multipath_route *)
			kmalloc(size_rt, GFP_ATOMIC);

		target_route->gw = nh->nh_gw;
		target_route->oif = nh->nh_oif;
		memset(&target_route->rcu, 0, sizeof(struct rcu_head));
		INIT_LIST_HEAD(&target_route->dests);

		list_add_rcu(&target_route->list, &state[state_idx].head);
	}

	/* find state entry for destination or add one if necessary */
	list_for_each_entry_rcu(d, &target_route->dests, list) {
		if (d->nh_info == nh) {
			target_dest = d;
			break;
		}
	}

	if (!target_dest) {
		const size_t size_dst = sizeof(struct multipath_dest);
		target_dest = (struct multipath_dest*)
			kmalloc(size_dst, GFP_ATOMIC);

		target_dest->nh_info = nh;
		target_dest->network = network;
		target_dest->netmask = netmask;
		target_dest->prefixlen = prefixlen;
		memset(&target_dest->rcu, 0, sizeof(struct rcu_head));

		list_add_rcu(&target_dest->list, &target_route->dests);
	}
	/* else: we already stored this info for another destination =>
	 * we are finished
	 */

	spin_unlock_bh(&state[state_idx].lock);
}

static void __multipath_free(struct rcu_head *head)
{
	struct multipath_route *rt = container_of(head, struct multipath_route,
						  rcu);
	kfree(rt);
}

static void __multipath_free_dst(struct rcu_head *head)
{
  	struct multipath_dest *dst = container_of(head,
						  struct multipath_dest,
						  rcu);
	kfree(dst);
}

static void wrandom_flush(void)
{
	int i;

	/* defere delete to all entries */
	for (i = 0; i < MULTIPATH_STATE_SIZE; ++i) {
		struct multipath_route *r;

		spin_lock_bh(&state[i].lock);
		list_for_each_entry_rcu(r, &state[i].head, list) {
			struct multipath_dest *d;
			list_for_each_entry_rcu(d, &r->dests, list) {
				list_del_rcu(&d->list);
				call_rcu(&d->rcu,
					 __multipath_free_dst);
			}
			list_del_rcu(&r->list);
			call_rcu(&r->rcu,
				 __multipath_free);
		}

		spin_unlock_bh(&state[i].lock);
	}
}

static struct ip_mp_alg_ops wrandom_ops = {
	.mp_alg_select_route	=	wrandom_select_route,
	.mp_alg_flush		=	wrandom_flush,
	.mp_alg_set_nhinfo	=	wrandom_set_nhinfo,
};

static int __init wrandom_init(void)
{
	wrandom_init_state();

	return multipath_alg_register(&wrandom_ops, IP_MP_ALG_WRANDOM);
}

static void __exit wrandom_exit(void)
{
	multipath_alg_unregister(&wrandom_ops, IP_MP_ALG_WRANDOM);
}

module_init(wrandom_init);
module_exit(wrandom_exit);
MODULE_LICENSE("GPL");
