/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_MROUTE_H
#define __LINUX_MROUTE_H

#include <linux/in.h>
#include <linux/pim.h>
#include <net/fib_rules.h>
#include <net/fib_notifier.h>
#include <uapi/linux/mroute.h>
#include <linux/mroute_base.h>

#ifdef CONFIG_IP_MROUTE
static inline int ip_mroute_opt(int opt)
{
	return opt >= MRT_BASE && opt <= MRT_MAX;
}

int ip_mroute_setsockopt(struct sock *, int, char __user *, unsigned int);
int ip_mroute_getsockopt(struct sock *, int, char __user *, int __user *);
int ipmr_ioctl(struct sock *sk, int cmd, void __user *arg);
int ipmr_compat_ioctl(struct sock *sk, unsigned int cmd, void __user *arg);
int ip_mr_init(void);
bool ipmr_rule_default(const struct fib_rule *rule);
#else
static inline int ip_mroute_setsockopt(struct sock *sock, int optname,
				       char __user *optval, unsigned int optlen)
{
	return -ENOPROTOOPT;
}

static inline int ip_mroute_getsockopt(struct sock *sock, int optname,
				       char __user *optval, int __user *optlen)
{
	return -ENOPROTOOPT;
}

static inline int ipmr_ioctl(struct sock *sk, int cmd, void __user *arg)
{
	return -ENOIOCTLCMD;
}

static inline int ip_mr_init(void)
{
	return 0;
}

static inline int ip_mroute_opt(int opt)
{
	return 0;
}

static inline bool ipmr_rule_default(const struct fib_rule *rule)
{
	return true;
}
#endif

#define VIFF_STATIC 0x8000

struct mfc_cache_cmp_arg {
	__be32 mfc_mcastgrp;
	__be32 mfc_origin;
};

/**
 * struct mfc_cache - multicast routing entries
 * @_c: Common multicast routing information; has to be first [for casting]
 * @mfc_mcastgrp: destination multicast group address
 * @mfc_origin: source address
 * @cmparg: used for rhashtable comparisons
 */
struct mfc_cache {
	struct mr_mfc _c;
	union {
		struct {
			__be32 mfc_mcastgrp;
			__be32 mfc_origin;
		};
		struct mfc_cache_cmp_arg cmparg;
	};
};

struct rtmsg;
int ipmr_get_route(struct net *net, struct sk_buff *skb,
		   __be32 saddr, __be32 daddr,
		   struct rtmsg *rtm, u32 portid);
#endif
