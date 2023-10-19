// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>

#include <linux/inet_diag.h>
#include <linux/sock_diag.h>

#include <net/inet_sock.h>
#include <net/raw.h>
#include <net/rawv6.h>

#ifdef pr_fmt
# undef pr_fmt
#endif

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

static struct raw_hashinfo *
raw_get_hashinfo(const struct inet_diag_req_v2 *r)
{
	if (r->sdiag_family == AF_INET) {
		return &raw_v4_hashinfo;
#if IS_ENABLED(CONFIG_IPV6)
	} else if (r->sdiag_family == AF_INET6) {
		return &raw_v6_hashinfo;
#endif
	} else {
		return ERR_PTR(-EINVAL);
	}
}

/*
 * Due to requirement of not breaking user API we can't simply
 * rename @pad field in inet_diag_req_v2 structure, instead
 * use helper to figure it out.
 */

static bool raw_lookup(struct net *net, struct sock *sk,
		       const struct inet_diag_req_v2 *req)
{
	struct inet_diag_req_raw *r = (void *)req;

	if (r->sdiag_family == AF_INET)
		return raw_v4_match(net, sk, r->sdiag_raw_protocol,
				    r->id.idiag_dst[0],
				    r->id.idiag_src[0],
				    r->id.idiag_if, 0);
#if IS_ENABLED(CONFIG_IPV6)
	else
		return raw_v6_match(net, sk, r->sdiag_raw_protocol,
				    (const struct in6_addr *)r->id.idiag_src,
				    (const struct in6_addr *)r->id.idiag_dst,
				    r->id.idiag_if, 0);
#endif
	return false;
}

static struct sock *raw_sock_get(struct net *net, const struct inet_diag_req_v2 *r)
{
	struct raw_hashinfo *hashinfo = raw_get_hashinfo(r);
	struct hlist_head *hlist;
	struct sock *sk;
	int slot;

	if (IS_ERR(hashinfo))
		return ERR_CAST(hashinfo);

	rcu_read_lock();
	for (slot = 0; slot < RAW_HTABLE_SIZE; slot++) {
		hlist = &hashinfo->ht[slot];
		sk_for_each_rcu(sk, hlist) {
			if (raw_lookup(net, sk, r)) {
				/*
				 * Grab it and keep until we fill
				 * diag message to be reported, so
				 * caller should call sock_put then.
				 */
				if (refcount_inc_not_zero(&sk->sk_refcnt))
					goto out_unlock;
			}
		}
	}
	sk = ERR_PTR(-ENOENT);
out_unlock:
	rcu_read_unlock();

	return sk;
}

static int raw_diag_dump_one(struct netlink_callback *cb,
			     const struct inet_diag_req_v2 *r)
{
	struct sk_buff *in_skb = cb->skb;
	struct sk_buff *rep;
	struct sock *sk;
	struct net *net;
	int err;

	net = sock_net(in_skb->sk);
	sk = raw_sock_get(net, r);
	if (IS_ERR(sk))
		return PTR_ERR(sk);

	rep = nlmsg_new(nla_total_size(sizeof(struct inet_diag_msg)) +
			inet_diag_msg_attrs_size() +
			nla_total_size(sizeof(struct inet_diag_meminfo)) + 64,
			GFP_KERNEL);
	if (!rep) {
		sock_put(sk);
		return -ENOMEM;
	}

	err = inet_sk_diag_fill(sk, NULL, rep, cb, r, 0,
				netlink_net_capable(in_skb, CAP_NET_ADMIN));
	sock_put(sk);

	if (err < 0) {
		kfree_skb(rep);
		return err;
	}

	err = nlmsg_unicast(net->diag_nlsk, rep, NETLINK_CB(in_skb).portid);

	return err;
}

static int sk_diag_dump(struct sock *sk, struct sk_buff *skb,
			struct netlink_callback *cb,
			const struct inet_diag_req_v2 *r,
			struct nlattr *bc, bool net_admin)
{
	if (!inet_diag_bc_sk(bc, sk))
		return 0;

	return inet_sk_diag_fill(sk, NULL, skb, cb, r, NLM_F_MULTI, net_admin);
}

static void raw_diag_dump(struct sk_buff *skb, struct netlink_callback *cb,
			  const struct inet_diag_req_v2 *r)
{
	bool net_admin = netlink_net_capable(cb->skb, CAP_NET_ADMIN);
	struct raw_hashinfo *hashinfo = raw_get_hashinfo(r);
	struct net *net = sock_net(skb->sk);
	struct inet_diag_dump_data *cb_data;
	int num, s_num, slot, s_slot;
	struct hlist_head *hlist;
	struct sock *sk = NULL;
	struct nlattr *bc;

	if (IS_ERR(hashinfo))
		return;

	cb_data = cb->data;
	bc = cb_data->inet_diag_nla_bc;
	s_slot = cb->args[0];
	num = s_num = cb->args[1];

	rcu_read_lock();
	for (slot = s_slot; slot < RAW_HTABLE_SIZE; s_num = 0, slot++) {
		num = 0;

		hlist = &hashinfo->ht[slot];
		sk_for_each_rcu(sk, hlist) {
			struct inet_sock *inet = inet_sk(sk);

			if (!net_eq(sock_net(sk), net))
				continue;
			if (num < s_num)
				goto next;
			if (sk->sk_family != r->sdiag_family)
				goto next;
			if (r->id.idiag_sport != inet->inet_sport &&
			    r->id.idiag_sport)
				goto next;
			if (r->id.idiag_dport != inet->inet_dport &&
			    r->id.idiag_dport)
				goto next;
			if (sk_diag_dump(sk, skb, cb, r, bc, net_admin) < 0)
				goto out_unlock;
next:
			num++;
		}
	}

out_unlock:
	rcu_read_unlock();

	cb->args[0] = slot;
	cb->args[1] = num;
}

static void raw_diag_get_info(struct sock *sk, struct inet_diag_msg *r,
			      void *info)
{
	r->idiag_rqueue = sk_rmem_alloc_get(sk);
	r->idiag_wqueue = sk_wmem_alloc_get(sk);
}

#ifdef CONFIG_INET_DIAG_DESTROY
static int raw_diag_destroy(struct sk_buff *in_skb,
			    const struct inet_diag_req_v2 *r)
{
	struct net *net = sock_net(in_skb->sk);
	struct sock *sk;
	int err;

	sk = raw_sock_get(net, r);
	if (IS_ERR(sk))
		return PTR_ERR(sk);
	err = sock_diag_destroy(sk, ECONNABORTED);
	sock_put(sk);
	return err;
}
#endif

static const struct inet_diag_handler raw_diag_handler = {
	.dump			= raw_diag_dump,
	.dump_one		= raw_diag_dump_one,
	.idiag_get_info		= raw_diag_get_info,
	.idiag_type		= IPPROTO_RAW,
	.idiag_info_size	= 0,
#ifdef CONFIG_INET_DIAG_DESTROY
	.destroy		= raw_diag_destroy,
#endif
};

static void __always_unused __check_inet_diag_req_raw(void)
{
	/*
	 * Make sure the two structures are identical,
	 * except the @pad field.
	 */
#define __offset_mismatch(m1, m2)			\
	(offsetof(struct inet_diag_req_v2, m1) !=	\
	 offsetof(struct inet_diag_req_raw, m2))

	BUILD_BUG_ON(sizeof(struct inet_diag_req_v2) !=
		     sizeof(struct inet_diag_req_raw));
	BUILD_BUG_ON(__offset_mismatch(sdiag_family, sdiag_family));
	BUILD_BUG_ON(__offset_mismatch(sdiag_protocol, sdiag_protocol));
	BUILD_BUG_ON(__offset_mismatch(idiag_ext, idiag_ext));
	BUILD_BUG_ON(__offset_mismatch(pad, sdiag_raw_protocol));
	BUILD_BUG_ON(__offset_mismatch(idiag_states, idiag_states));
	BUILD_BUG_ON(__offset_mismatch(id, id));
#undef __offset_mismatch
}

static int __init raw_diag_init(void)
{
	return inet_diag_register(&raw_diag_handler);
}

static void __exit raw_diag_exit(void)
{
	inet_diag_unregister(&raw_diag_handler);
}

module_init(raw_diag_init);
module_exit(raw_diag_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_NET_PF_PROTO_TYPE(PF_NETLINK, NETLINK_SOCK_DIAG, 2-255 /* AF_INET - IPPROTO_RAW */);
MODULE_ALIAS_NET_PF_PROTO_TYPE(PF_NETLINK, NETLINK_SOCK_DIAG, 10-255 /* AF_INET6 - IPPROTO_RAW */);
