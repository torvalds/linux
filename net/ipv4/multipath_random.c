/*
 *              Random policy for multipath.
 *
 *
 * Version:	$Id: multipath_random.c,v 1.1.2.3 2004/09/21 08:42:11 elueck Exp $
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
#include <linux/random.h>
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

#define MULTIPATH_MAX_CANDIDATES 40

static void random_select_route(const struct flowi *flp,
				struct rtable *first,
				struct rtable **rp)
{
	struct rtable *rt;
	struct rtable *decision;
	unsigned char candidate_count = 0;

	/* count all candidate */
	for (rt = rcu_dereference(first); rt;
	     rt = rcu_dereference(rt->u.dst.rt_next)) {
		if ((rt->u.dst.flags & DST_BALANCED) != 0 &&
		    multipath_comparekeys(&rt->fl, flp))
			++candidate_count;
	}

	/* choose a random candidate */
	decision = first;
	if (candidate_count > 1) {
		unsigned char i = 0;
		unsigned char candidate_no = (unsigned char)
			(random32() % candidate_count);

		/* find chosen candidate and adjust GC data for all candidates
		 * to ensure they stay in cache
		 */
		for (rt = first; rt; rt = rt->u.dst.rt_next) {
			if ((rt->u.dst.flags & DST_BALANCED) != 0 &&
			    multipath_comparekeys(&rt->fl, flp)) {
				rt->u.dst.lastuse = jiffies;

				if (i == candidate_no)
					decision = rt;

				if (i >= candidate_count)
					break;

				i++;
			}
		}
	}

	decision->u.dst.__use++;
	*rp = decision;
}

static struct ip_mp_alg_ops random_ops = {
	.mp_alg_select_route	=	random_select_route,
};

static int __init random_init(void)
{
	return multipath_alg_register(&random_ops, IP_MP_ALG_RANDOM);
}

static void __exit random_exit(void)
{
	multipath_alg_unregister(&random_ops, IP_MP_ALG_RANDOM);
}

module_init(random_init);
module_exit(random_exit);
MODULE_LICENSE("GPL");
