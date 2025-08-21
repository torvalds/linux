/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_MROUTE6_H
#define __LINUX_MROUTE6_H


#include <linux/pim.h>
#include <linux/skbuff.h>	/* for struct sk_buff_head */
#include <net/net_namespace.h>
#include <uapi/linux/mroute6.h>
#include <linux/mroute_base.h>
#include <linux/sockptr.h>
#include <net/fib_rules.h>

#ifdef CONFIG_IPV6_MROUTE
static inline int ip6_mroute_opt(int opt)
{
	return (opt >= MRT6_BASE) && (opt <= MRT6_MAX);
}
#else
static inline int ip6_mroute_opt(int opt)
{
	return 0;
}
#endif

struct sock;

#ifdef CONFIG_IPV6_MROUTE
extern int ip6_mroute_setsockopt(struct sock *, int, sockptr_t, unsigned int);
extern int ip6_mroute_getsockopt(struct sock *, int, sockptr_t, sockptr_t);
extern int ip6_mr_input(struct sk_buff *skb);
extern int ip6mr_compat_ioctl(struct sock *sk, unsigned int cmd, void __user *arg);
extern int ip6_mr_init(void);
extern int ip6_mr_output(struct net *net, struct sock *sk, struct sk_buff *skb);
extern void ip6_mr_cleanup(void);
int ip6mr_ioctl(struct sock *sk, int cmd, void *arg);
#else
static inline int ip6_mroute_setsockopt(struct sock *sock, int optname,
		sockptr_t optval, unsigned int optlen)
{
	return -ENOPROTOOPT;
}

static inline
int ip6_mroute_getsockopt(struct sock *sock,
			  int optname, sockptr_t optval, sockptr_t optlen)
{
	return -ENOPROTOOPT;
}

static inline
int ip6mr_ioctl(struct sock *sk, int cmd, void *arg)
{
	return -ENOIOCTLCMD;
}

static inline int ip6_mr_init(void)
{
	return 0;
}

static inline int
ip6_mr_output(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	return ip6_output(net, sk, skb);
}

static inline void ip6_mr_cleanup(void)
{
	return;
}
#endif

#ifdef CONFIG_IPV6_MROUTE_MULTIPLE_TABLES
bool ip6mr_rule_default(const struct fib_rule *rule);
#else
static inline bool ip6mr_rule_default(const struct fib_rule *rule)
{
	return true;
}
#endif

#define VIFF_STATIC 0x8000

struct mfc6_cache_cmp_arg {
	struct in6_addr mf6c_mcastgrp;
	struct in6_addr mf6c_origin;
};

struct mfc6_cache {
	struct mr_mfc _c;
	union {
		struct {
			struct in6_addr mf6c_mcastgrp;
			struct in6_addr mf6c_origin;
		};
		struct mfc6_cache_cmp_arg cmparg;
	};
};

#define MFC_ASSERT_THRESH (3*HZ)		/* Maximal freq. of asserts */

struct rtmsg;
extern int ip6mr_get_route(struct net *net, struct sk_buff *skb,
			   struct rtmsg *rtm, u32 portid);

#ifdef CONFIG_IPV6_MROUTE
bool mroute6_is_socket(struct net *net, struct sk_buff *skb);
extern int ip6mr_sk_done(struct sock *sk);
static inline int ip6mr_sk_ioctl(struct sock *sk, unsigned int cmd,
				 void __user *arg)
{
	switch (cmd) {
	/* These userspace buffers will be consumed by ip6mr_ioctl() */
	case SIOCGETMIFCNT_IN6: {
		struct sioc_mif_req6 buffer;

		return sock_ioctl_inout(sk, cmd, arg, &buffer,
					sizeof(buffer));
		}
	case SIOCGETSGCNT_IN6: {
		struct sioc_sg_req6 buffer;

		return sock_ioctl_inout(sk, cmd, arg, &buffer,
					sizeof(buffer));
		}
	}

	return 1;
}
#else
static inline bool mroute6_is_socket(struct net *net, struct sk_buff *skb)
{
	return false;
}
static inline int ip6mr_sk_done(struct sock *sk)
{
	return 0;
}

static inline int ip6mr_sk_ioctl(struct sock *sk, unsigned int cmd,
				 void __user *arg)
{
	return 1;
}
#endif
#endif
