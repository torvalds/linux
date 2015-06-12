/*
 * udp_diag.c	Module for monitoring UDP transport protocols sockets.
 *
 * Authors:	Pavel Emelyanov, <xemul@parallels.com>
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */


#include <linux/module.h>
#include <linux/inet_diag.h>
#include <linux/udp.h>
#include <net/udp.h>
#include <net/udplite.h>
#include <linux/sock_diag.h>

static int sk_diag_dump(struct sock *sk, struct sk_buff *skb,
			struct netlink_callback *cb,
			const struct inet_diag_req_v2 *req,
			struct nlattr *bc)
{
	if (!inet_diag_bc_sk(bc, sk))
		return 0;

	return inet_sk_diag_fill(sk, NULL, skb, req,
			sk_user_ns(NETLINK_CB(cb->skb).sk),
			NETLINK_CB(cb->skb).portid,
			cb->nlh->nlmsg_seq, NLM_F_MULTI, cb->nlh);
}

static int udp_dump_one(struct udp_table *tbl, struct sk_buff *in_skb,
			const struct nlmsghdr *nlh,
			const struct inet_diag_req_v2 *req)
{
	int err = -EINVAL;
	struct sock *sk;
	struct sk_buff *rep;
	struct net *net = sock_net(in_skb->sk);

	if (req->sdiag_family == AF_INET)
		sk = __udp4_lib_lookup(net,
				req->id.idiag_src[0], req->id.idiag_sport,
				req->id.idiag_dst[0], req->id.idiag_dport,
				req->id.idiag_if, tbl);
#if IS_ENABLED(CONFIG_IPV6)
	else if (req->sdiag_family == AF_INET6)
		sk = __udp6_lib_lookup(net,
				(struct in6_addr *)req->id.idiag_src,
				req->id.idiag_sport,
				(struct in6_addr *)req->id.idiag_dst,
				req->id.idiag_dport,
				req->id.idiag_if, tbl);
#endif
	else
		goto out_nosk;

	err = -ENOENT;
	if (!sk)
		goto out_nosk;

	err = sock_diag_check_cookie(sk, req->id.idiag_cookie);
	if (err)
		goto out;

	err = -ENOMEM;
	rep = nlmsg_new(sizeof(struct inet_diag_msg) +
			sizeof(struct inet_diag_meminfo) + 64,
			GFP_KERNEL);
	if (!rep)
		goto out;

	err = inet_sk_diag_fill(sk, NULL, rep, req,
			   sk_user_ns(NETLINK_CB(in_skb).sk),
			   NETLINK_CB(in_skb).portid,
			   nlh->nlmsg_seq, 0, nlh);
	if (err < 0) {
		WARN_ON(err == -EMSGSIZE);
		kfree_skb(rep);
		goto out;
	}
	err = netlink_unicast(net->diag_nlsk, rep, NETLINK_CB(in_skb).portid,
			      MSG_DONTWAIT);
	if (err > 0)
		err = 0;
out:
	if (sk)
		sock_put(sk);
out_nosk:
	return err;
}

static void udp_dump(struct udp_table *table, struct sk_buff *skb,
		     struct netlink_callback *cb,
		     const struct inet_diag_req_v2 *r, struct nlattr *bc)
{
	int num, s_num, slot, s_slot;
	struct net *net = sock_net(skb->sk);

	s_slot = cb->args[0];
	num = s_num = cb->args[1];

	for (slot = s_slot; slot <= table->mask; s_num = 0, slot++) {
		struct sock *sk;
		struct hlist_nulls_node *node;
		struct udp_hslot *hslot = &table->hash[slot];

		num = 0;

		if (hlist_nulls_empty(&hslot->head))
			continue;

		spin_lock_bh(&hslot->lock);
		sk_nulls_for_each(sk, node, &hslot->head) {
			struct inet_sock *inet = inet_sk(sk);

			if (!net_eq(sock_net(sk), net))
				continue;
			if (num < s_num)
				goto next;
			if (!(r->idiag_states & (1 << sk->sk_state)))
				goto next;
			if (r->sdiag_family != AF_UNSPEC &&
					sk->sk_family != r->sdiag_family)
				goto next;
			if (r->id.idiag_sport != inet->inet_sport &&
			    r->id.idiag_sport)
				goto next;
			if (r->id.idiag_dport != inet->inet_dport &&
			    r->id.idiag_dport)
				goto next;

			if (sk_diag_dump(sk, skb, cb, r, bc) < 0) {
				spin_unlock_bh(&hslot->lock);
				goto done;
			}
next:
			num++;
		}
		spin_unlock_bh(&hslot->lock);
	}
done:
	cb->args[0] = slot;
	cb->args[1] = num;
}

static void udp_diag_dump(struct sk_buff *skb, struct netlink_callback *cb,
			  const struct inet_diag_req_v2 *r, struct nlattr *bc)
{
	udp_dump(&udp_table, skb, cb, r, bc);
}

static int udp_diag_dump_one(struct sk_buff *in_skb, const struct nlmsghdr *nlh,
			     const struct inet_diag_req_v2 *req)
{
	return udp_dump_one(&udp_table, in_skb, nlh, req);
}

static void udp_diag_get_info(struct sock *sk, struct inet_diag_msg *r,
		void *info)
{
	r->idiag_rqueue = sk_rmem_alloc_get(sk);
	r->idiag_wqueue = sk_wmem_alloc_get(sk);
}

static const struct inet_diag_handler udp_diag_handler = {
	.dump		 = udp_diag_dump,
	.dump_one	 = udp_diag_dump_one,
	.idiag_get_info  = udp_diag_get_info,
	.idiag_type	 = IPPROTO_UDP,
};

static void udplite_diag_dump(struct sk_buff *skb, struct netlink_callback *cb,
			      const struct inet_diag_req_v2 *r,
			      struct nlattr *bc)
{
	udp_dump(&udplite_table, skb, cb, r, bc);
}

static int udplite_diag_dump_one(struct sk_buff *in_skb, const struct nlmsghdr *nlh,
				 const struct inet_diag_req_v2 *req)
{
	return udp_dump_one(&udplite_table, in_skb, nlh, req);
}

static const struct inet_diag_handler udplite_diag_handler = {
	.dump		 = udplite_diag_dump,
	.dump_one	 = udplite_diag_dump_one,
	.idiag_get_info  = udp_diag_get_info,
	.idiag_type	 = IPPROTO_UDPLITE,
};

static int __init udp_diag_init(void)
{
	int err;

	err = inet_diag_register(&udp_diag_handler);
	if (err)
		goto out;
	err = inet_diag_register(&udplite_diag_handler);
	if (err)
		goto out_lite;
out:
	return err;
out_lite:
	inet_diag_unregister(&udp_diag_handler);
	goto out;
}

static void __exit udp_diag_exit(void)
{
	inet_diag_unregister(&udplite_diag_handler);
	inet_diag_unregister(&udp_diag_handler);
}

module_init(udp_diag_init);
module_exit(udp_diag_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_NET_PF_PROTO_TYPE(PF_NETLINK, NETLINK_SOCK_DIAG, 2-17 /* AF_INET - IPPROTO_UDP */);
MODULE_ALIAS_NET_PF_PROTO_TYPE(PF_NETLINK, NETLINK_SOCK_DIAG, 2-136 /* AF_INET - IPPROTO_UDPLITE */);
