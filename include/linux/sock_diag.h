#ifndef __SOCK_DIAG_H__
#define __SOCK_DIAG_H__

#include <linux/netlink.h>
#include <linux/user_namespace.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include <uapi/linux/sock_diag.h>

struct sk_buff;
struct nlmsghdr;
struct sock;

struct sock_diag_handler {
	__u8 family;
	int (*dump)(struct sk_buff *skb, struct nlmsghdr *nlh);
	int (*get_info)(struct sk_buff *skb, struct sock *sk);
};

int sock_diag_register(const struct sock_diag_handler *h);
void sock_diag_unregister(const struct sock_diag_handler *h);

void sock_diag_register_inet_compat(int (*fn)(struct sk_buff *skb, struct nlmsghdr *nlh));
void sock_diag_unregister_inet_compat(int (*fn)(struct sk_buff *skb, struct nlmsghdr *nlh));

int sock_diag_check_cookie(struct sock *sk, const __u32 *cookie);
void sock_diag_save_cookie(struct sock *sk, __u32 *cookie);

int sock_diag_put_meminfo(struct sock *sk, struct sk_buff *skb, int attr);
int sock_diag_put_filterinfo(bool may_report_filterinfo, struct sock *sk,
			     struct sk_buff *skb, int attrtype);

static inline
enum sknetlink_groups sock_diag_destroy_group(const struct sock *sk)
{
	switch (sk->sk_family) {
	case AF_INET:
		switch (sk->sk_protocol) {
		case IPPROTO_TCP:
			return SKNLGRP_INET_TCP_DESTROY;
		case IPPROTO_UDP:
			return SKNLGRP_INET_UDP_DESTROY;
		default:
			return SKNLGRP_NONE;
		}
	case AF_INET6:
		switch (sk->sk_protocol) {
		case IPPROTO_TCP:
			return SKNLGRP_INET6_TCP_DESTROY;
		case IPPROTO_UDP:
			return SKNLGRP_INET6_UDP_DESTROY;
		default:
			return SKNLGRP_NONE;
		}
	default:
		return SKNLGRP_NONE;
	}
}

static inline
bool sock_diag_has_destroy_listeners(const struct sock *sk)
{
	const struct net *n = sock_net(sk);
	const enum sknetlink_groups group = sock_diag_destroy_group(sk);

	return group != SKNLGRP_NONE && n->diag_nlsk &&
		netlink_has_listeners(n->diag_nlsk, group);
}
void sock_diag_broadcast_destroy(struct sock *sk);

#endif
