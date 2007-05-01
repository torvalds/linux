/*
 *              Device round robin policy for multipath.
 *
 *
 * Version:	$Id: multipath_drr.c,v 1.1.2.1 2004/09/16 07:42:34 elueck Exp $
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
#include <net/ip_mp_alg.h>

struct multipath_device {
	int		ifi; /* interface index of device */
	atomic_t	usecount;
	int 		allocated;
};

#define MULTIPATH_MAX_DEVICECANDIDATES 10

static struct multipath_device state[MULTIPATH_MAX_DEVICECANDIDATES];
static DEFINE_SPINLOCK(state_lock);

static int inline __multipath_findslot(void)
{
	int i;

	for (i = 0; i < MULTIPATH_MAX_DEVICECANDIDATES; i++) {
		if (state[i].allocated == 0)
			return i;
	}
	return -1;
}

static int inline __multipath_finddev(int ifindex)
{
	int i;

	for (i = 0; i < MULTIPATH_MAX_DEVICECANDIDATES; i++) {
		if (state[i].allocated != 0 &&
		    state[i].ifi == ifindex)
			return i;
	}
	return -1;
}

static int drr_dev_event(struct notifier_block *this,
			 unsigned long event, void *ptr)
{
	struct net_device *dev = ptr;
	int devidx;

	switch (event) {
	case NETDEV_UNREGISTER:
	case NETDEV_DOWN:
		spin_lock_bh(&state_lock);

		devidx = __multipath_finddev(dev->ifindex);
		if (devidx != -1) {
			state[devidx].allocated = 0;
			state[devidx].ifi = 0;
			atomic_set(&state[devidx].usecount, 0);
		}

		spin_unlock_bh(&state_lock);
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block drr_dev_notifier = {
	.notifier_call	= drr_dev_event,
};


static void drr_safe_inc(atomic_t *usecount)
{
	int n;

	atomic_inc(usecount);

	n = atomic_read(usecount);
	if (n <= 0) {
		int i;

		spin_lock_bh(&state_lock);

		for (i = 0; i < MULTIPATH_MAX_DEVICECANDIDATES; i++)
			atomic_set(&state[i].usecount, 0);

		spin_unlock_bh(&state_lock);
	}
}

static void drr_select_route(const struct flowi *flp,
			     struct rtable *first, struct rtable **rp)
{
	struct rtable *nh, *result, *cur_min;
	int min_usecount = -1;
	int devidx = -1;
	int cur_min_devidx = -1;

	/* 1. make sure all alt. nexthops have the same GC related data */
	/* 2. determine the new candidate to be returned */
	result = NULL;
	cur_min = NULL;
	for (nh = rcu_dereference(first); nh;
	     nh = rcu_dereference(nh->u.dst.rt_next)) {
		if ((nh->u.dst.flags & DST_BALANCED) != 0 &&
		    multipath_comparekeys(&nh->fl, flp)) {
			int nh_ifidx = nh->u.dst.dev->ifindex;

			nh->u.dst.lastuse = jiffies;
			nh->u.dst.__use++;
			if (result != NULL)
				continue;

			/* search for the output interface */

			/* this is not SMP safe, only add/remove are
			 * SMP safe as wrong usecount updates have no big
			 * impact
			 */
			devidx = __multipath_finddev(nh_ifidx);
			if (devidx == -1) {
				/* add the interface to the array
				 * SMP safe
				 */
				spin_lock_bh(&state_lock);

				/* due to SMP: search again */
				devidx = __multipath_finddev(nh_ifidx);
				if (devidx == -1) {
					/* add entry for device */
					devidx = __multipath_findslot();
					if (devidx == -1) {
						/* unlikely but possible */
						continue;
					}

					state[devidx].allocated = 1;
					state[devidx].ifi = nh_ifidx;
					atomic_set(&state[devidx].usecount, 0);
					min_usecount = 0;
				}

				spin_unlock_bh(&state_lock);
			}

			if (min_usecount == 0) {
				/* if the device has not been used it is
				 * the primary target
				 */
				drr_safe_inc(&state[devidx].usecount);
				result = nh;
			} else {
				int count =
					atomic_read(&state[devidx].usecount);

				if (min_usecount == -1 ||
				    count < min_usecount) {
					cur_min = nh;
					cur_min_devidx = devidx;
					min_usecount = count;
				}
			}
		}
	}

	if (!result) {
		if (cur_min) {
			drr_safe_inc(&state[cur_min_devidx].usecount);
			result = cur_min;
		} else {
			result = first;
		}
	}

	*rp = result;
}

static struct ip_mp_alg_ops drr_ops = {
	.mp_alg_select_route	=	drr_select_route,
};

static int __init drr_init(void)
{
	int err = register_netdevice_notifier(&drr_dev_notifier);

	if (err)
		return err;

	err = multipath_alg_register(&drr_ops, IP_MP_ALG_DRR);
	if (err)
		goto fail;

	return 0;

fail:
	unregister_netdevice_notifier(&drr_dev_notifier);
	return err;
}

static void __exit drr_exit(void)
{
	unregister_netdevice_notifier(&drr_dev_notifier);
	multipath_alg_unregister(&drr_ops, IP_MP_ALG_DRR);
}

module_init(drr_init);
module_exit(drr_exit);
MODULE_LICENSE("GPL");
