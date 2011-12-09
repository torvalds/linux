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
#include <linux/inet_diag.h>
#include <linux/sock_diag.h>

static int udp_dump_one(struct udp_table *tbl, struct sk_buff *in_skb,
		const struct nlmsghdr *nlh, struct inet_diag_req *req)
{
	return 0;
}

static void udp_dump(struct udp_table *table, struct sk_buff *skb, struct netlink_callback *cb,
		struct inet_diag_req *r, struct nlattr *bc)
{
}

static void udp_diag_dump(struct sk_buff *skb, struct netlink_callback *cb,
		struct inet_diag_req *r, struct nlattr *bc)
{
	udp_dump(&udp_table, skb, cb, r, bc);
}

static int udp_diag_dump_one(struct sk_buff *in_skb, const struct nlmsghdr *nlh,
		struct inet_diag_req *req)
{
	return udp_dump_one(&udp_table, in_skb, nlh, req);
}

static const struct inet_diag_handler udp_diag_handler = {
	.dump		 = udp_diag_dump,
	.dump_one	 = udp_diag_dump_one,
	.idiag_type	 = IPPROTO_UDP,
};

static void udplite_diag_dump(struct sk_buff *skb, struct netlink_callback *cb,
		struct inet_diag_req *r, struct nlattr *bc)
{
	udp_dump(&udplite_table, skb, cb, r, bc);
}

static int udplite_diag_dump_one(struct sk_buff *in_skb, const struct nlmsghdr *nlh,
		struct inet_diag_req *req)
{
	return udp_dump_one(&udplite_table, in_skb, nlh, req);
}

static const struct inet_diag_handler udplite_diag_handler = {
	.dump		 = udplite_diag_dump,
	.dump_one	 = udplite_diag_dump_one,
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
MODULE_ALIAS_NET_PF_PROTO_TYPE(PF_NETLINK, NETLINK_SOCK_DIAG, 17);
MODULE_ALIAS_NET_PF_PROTO_TYPE(PF_NETLINK, NETLINK_SOCK_DIAG, 136);
