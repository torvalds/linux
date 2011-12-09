/*
 * tcp_diag.c	Module for monitoring TCP transport protocols sockets.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */


#include <linux/module.h>
#include <linux/inet_diag.h>

#include <linux/tcp.h>

#include <net/tcp.h>

static void tcp_diag_get_info(struct sock *sk, struct inet_diag_msg *r,
			      void *_info)
{
	const struct tcp_sock *tp = tcp_sk(sk);
	struct tcp_info *info = _info;

	if (sk->sk_state == TCP_LISTEN) {
		r->idiag_rqueue = sk->sk_ack_backlog;
		r->idiag_wqueue = sk->sk_max_ack_backlog;
	} else {
		r->idiag_rqueue = max_t(int, tp->rcv_nxt - tp->copied_seq, 0);
		r->idiag_wqueue = tp->write_seq - tp->snd_una;
	}
	if (info != NULL)
		tcp_get_info(sk, info);
}

static void tcp_diag_dump(struct sk_buff *skb, struct netlink_callback *cb,
		struct inet_diag_req *r, struct nlattr *bc)
{
	inet_diag_dump_icsk(&tcp_hashinfo, skb, cb, r, bc);
}

static int tcp_diag_dump_one(struct sk_buff *in_skb, const struct nlmsghdr *nlh,
		struct inet_diag_req *req)
{
	return inet_diag_dump_one_icsk(&tcp_hashinfo, in_skb, nlh, req);
}

static const struct inet_diag_handler tcp_diag_handler = {
	.dump		 = tcp_diag_dump,
	.dump_one	 = tcp_diag_dump_one,
	.idiag_get_info	 = tcp_diag_get_info,
	.idiag_type	 = IPPROTO_TCP,
};

static int __init tcp_diag_init(void)
{
	return inet_diag_register(&tcp_diag_handler);
}

static void __exit tcp_diag_exit(void)
{
	inet_diag_unregister(&tcp_diag_handler);
}

module_init(tcp_diag_init);
module_exit(tcp_diag_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_NET_PF_PROTO_TYPE(PF_NETLINK, NETLINK_SOCK_DIAG, 6);
