/* ip_mp_alg.h: IPV4 multipath algorithm support.
 *
 * Copyright (C) 2004, 2005 Einar Lueck <elueck@de.ibm.com>
 * Copyright (C) 2005 David S. Miller <davem@davemloft.net>
 */

#ifndef _NET_IP_MP_ALG_H
#define _NET_IP_MP_ALG_H

#include <linux/config.h>
#include <linux/ip_mp_alg.h>
#include <net/flow.h>
#include <net/route.h>

struct fib_nh;

struct ip_mp_alg_ops {
	void	(*mp_alg_select_route)(const struct flowi *flp,
				       struct rtable *rth, struct rtable **rp);
	void	(*mp_alg_flush)(void);
	void	(*mp_alg_set_nhinfo)(__u32 network, __u32 netmask,
				     unsigned char prefixlen,
				     const struct fib_nh *nh);
	void	(*mp_alg_remove)(struct rtable *rth);
};

extern int multipath_alg_register(struct ip_mp_alg_ops *, enum ip_mp_alg);
extern void multipath_alg_unregister(struct ip_mp_alg_ops *, enum ip_mp_alg);

extern struct ip_mp_alg_ops *ip_mp_alg_table[];

static inline int multipath_select_route(const struct flowi *flp,
					 struct rtable *rth,
					 struct rtable **rp)
{
#ifdef CONFIG_IP_ROUTE_MULTIPATH_CACHED
	struct ip_mp_alg_ops *ops = ip_mp_alg_table[rth->rt_multipath_alg];

	/* mp_alg_select_route _MUST_ be implemented */
	if (ops && (rth->u.dst.flags & DST_BALANCED)) {
		ops->mp_alg_select_route(flp, rth, rp);
		return 1;
	}
#endif
	return 0;
}

static inline void multipath_flush(void)
{
#ifdef CONFIG_IP_ROUTE_MULTIPATH_CACHED
	int i;

	for (i = IP_MP_ALG_NONE; i <= IP_MP_ALG_MAX; i++) {
		struct ip_mp_alg_ops *ops = ip_mp_alg_table[i];

		if (ops && ops->mp_alg_flush)
			ops->mp_alg_flush();
	}
#endif
}

static inline void multipath_set_nhinfo(struct rtable *rth,
					__u32 network, __u32 netmask,
					unsigned char prefixlen,
					const struct fib_nh *nh)
{
#ifdef CONFIG_IP_ROUTE_MULTIPATH_CACHED
	struct ip_mp_alg_ops *ops = ip_mp_alg_table[rth->rt_multipath_alg];

	if (ops && ops->mp_alg_set_nhinfo)
		ops->mp_alg_set_nhinfo(network, netmask, prefixlen, nh);
#endif
}

static inline void multipath_remove(struct rtable *rth)
{
#ifdef CONFIG_IP_ROUTE_MULTIPATH_CACHED
	struct ip_mp_alg_ops *ops = ip_mp_alg_table[rth->rt_multipath_alg];

	if (ops && ops->mp_alg_remove &&
	    (rth->u.dst.flags & DST_BALANCED))
		ops->mp_alg_remove(rth);
#endif
}

static inline int multipath_comparekeys(const struct flowi *flp1,
					const struct flowi *flp2)
{
	return flp1->fl4_dst == flp2->fl4_dst &&
		flp1->fl4_src == flp2->fl4_src &&
		flp1->oif == flp2->oif &&
#ifdef CONFIG_IP_ROUTE_FWMARK
		flp1->fl4_fwmark == flp2->fl4_fwmark &&
#endif
		!((flp1->fl4_tos ^ flp2->fl4_tos) &
		  (IPTOS_RT_MASK | RTO_ONLINK));
}

#endif /* _NET_IP_MP_ALG_H */
