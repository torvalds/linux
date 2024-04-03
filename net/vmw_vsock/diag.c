// SPDX-License-Identifier: GPL-2.0-only
/*
 * vsock sock_diag(7) module
 *
 * Copyright (C) 2017 Red Hat, Inc.
 * Author: Stefan Hajnoczi <stefanha@redhat.com>
 */

#include <linux/module.h>
#include <linux/sock_diag.h>
#include <linux/vm_sockets_diag.h>
#include <net/af_vsock.h>

static int sk_diag_fill(struct sock *sk, struct sk_buff *skb,
			u32 portid, u32 seq, u32 flags)
{
	struct vsock_sock *vsk = vsock_sk(sk);
	struct vsock_diag_msg *rep;
	struct nlmsghdr *nlh;

	nlh = nlmsg_put(skb, portid, seq, SOCK_DIAG_BY_FAMILY, sizeof(*rep),
			flags);
	if (!nlh)
		return -EMSGSIZE;

	rep = nlmsg_data(nlh);
	rep->vdiag_family = AF_VSOCK;

	/* Lock order dictates that sk_lock is acquired before
	 * vsock_table_lock, so we cannot lock here.  Simply don't take
	 * sk_lock; sk is guaranteed to stay alive since vsock_table_lock is
	 * held.
	 */
	rep->vdiag_type = sk->sk_type;
	rep->vdiag_state = sk->sk_state;
	rep->vdiag_shutdown = sk->sk_shutdown;
	rep->vdiag_src_cid = vsk->local_addr.svm_cid;
	rep->vdiag_src_port = vsk->local_addr.svm_port;
	rep->vdiag_dst_cid = vsk->remote_addr.svm_cid;
	rep->vdiag_dst_port = vsk->remote_addr.svm_port;
	rep->vdiag_ino = sock_i_ino(sk);

	sock_diag_save_cookie(sk, rep->vdiag_cookie);

	return 0;
}

static int vsock_diag_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct vsock_diag_req *req;
	struct vsock_sock *vsk;
	unsigned int bucket;
	unsigned int last_i;
	unsigned int table;
	struct net *net;
	unsigned int i;

	req = nlmsg_data(cb->nlh);
	net = sock_net(skb->sk);

	/* State saved between calls: */
	table = cb->args[0];
	bucket = cb->args[1];
	i = last_i = cb->args[2];

	/* TODO VMCI pending sockets? */

	spin_lock_bh(&vsock_table_lock);

	/* Bind table (locally created sockets) */
	if (table == 0) {
		while (bucket < ARRAY_SIZE(vsock_bind_table)) {
			struct list_head *head = &vsock_bind_table[bucket];

			i = 0;
			list_for_each_entry(vsk, head, bound_table) {
				struct sock *sk = sk_vsock(vsk);

				if (!net_eq(sock_net(sk), net))
					continue;
				if (i < last_i)
					goto next_bind;
				if (!(req->vdiag_states & (1 << sk->sk_state)))
					goto next_bind;
				if (sk_diag_fill(sk, skb,
						 NETLINK_CB(cb->skb).portid,
						 cb->nlh->nlmsg_seq,
						 NLM_F_MULTI) < 0)
					goto done;
next_bind:
				i++;
			}
			last_i = 0;
			bucket++;
		}

		table++;
		bucket = 0;
	}

	/* Connected table (accepted connections) */
	while (bucket < ARRAY_SIZE(vsock_connected_table)) {
		struct list_head *head = &vsock_connected_table[bucket];

		i = 0;
		list_for_each_entry(vsk, head, connected_table) {
			struct sock *sk = sk_vsock(vsk);

			/* Skip sockets we've already seen above */
			if (__vsock_in_bound_table(vsk))
				continue;

			if (!net_eq(sock_net(sk), net))
				continue;
			if (i < last_i)
				goto next_connected;
			if (!(req->vdiag_states & (1 << sk->sk_state)))
				goto next_connected;
			if (sk_diag_fill(sk, skb,
					 NETLINK_CB(cb->skb).portid,
					 cb->nlh->nlmsg_seq,
					 NLM_F_MULTI) < 0)
				goto done;
next_connected:
			i++;
		}
		last_i = 0;
		bucket++;
	}

done:
	spin_unlock_bh(&vsock_table_lock);

	cb->args[0] = table;
	cb->args[1] = bucket;
	cb->args[2] = i;

	return skb->len;
}

static int vsock_diag_handler_dump(struct sk_buff *skb, struct nlmsghdr *h)
{
	int hdrlen = sizeof(struct vsock_diag_req);
	struct net *net = sock_net(skb->sk);

	if (nlmsg_len(h) < hdrlen)
		return -EINVAL;

	if (h->nlmsg_flags & NLM_F_DUMP) {
		struct netlink_dump_control c = {
			.dump = vsock_diag_dump,
		};
		return netlink_dump_start(net->diag_nlsk, skb, h, &c);
	}

	return -EOPNOTSUPP;
}

static const struct sock_diag_handler vsock_diag_handler = {
	.owner = THIS_MODULE,
	.family = AF_VSOCK,
	.dump = vsock_diag_handler_dump,
};

static int __init vsock_diag_init(void)
{
	return sock_diag_register(&vsock_diag_handler);
}

static void __exit vsock_diag_exit(void)
{
	sock_diag_unregister(&vsock_diag_handler);
}

module_init(vsock_diag_init);
module_exit(vsock_diag_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("VMware Virtual Sockets monitoring via SOCK_DIAG");
MODULE_ALIAS_NET_PF_PROTO_TYPE(PF_NETLINK, NETLINK_SOCK_DIAG,
			       40 /* AF_VSOCK */);
