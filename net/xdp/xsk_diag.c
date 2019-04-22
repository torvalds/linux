// SPDX-License-Identifier: GPL-2.0
/* XDP sockets monitoring support
 *
 * Copyright(c) 2019 Intel Corporation.
 *
 * Author: Björn Töpel <bjorn.topel@intel.com>
 */

#include <linux/module.h>
#include <net/xdp_sock.h>
#include <linux/xdp_diag.h>
#include <linux/sock_diag.h>

#include "xsk_queue.h"
#include "xsk.h"

static int xsk_diag_put_info(const struct xdp_sock *xs, struct sk_buff *nlskb)
{
	struct xdp_diag_info di = {};

	di.ifindex = xs->dev ? xs->dev->ifindex : 0;
	di.queue_id = xs->queue_id;
	return nla_put(nlskb, XDP_DIAG_INFO, sizeof(di), &di);
}

static int xsk_diag_put_ring(const struct xsk_queue *queue, int nl_type,
			     struct sk_buff *nlskb)
{
	struct xdp_diag_ring dr = {};

	dr.entries = queue->nentries;
	return nla_put(nlskb, nl_type, sizeof(dr), &dr);
}

static int xsk_diag_put_rings_cfg(const struct xdp_sock *xs,
				  struct sk_buff *nlskb)
{
	int err = 0;

	if (xs->rx)
		err = xsk_diag_put_ring(xs->rx, XDP_DIAG_RX_RING, nlskb);
	if (!err && xs->tx)
		err = xsk_diag_put_ring(xs->tx, XDP_DIAG_TX_RING, nlskb);
	return err;
}

static int xsk_diag_put_umem(const struct xdp_sock *xs, struct sk_buff *nlskb)
{
	struct xdp_umem *umem = xs->umem;
	struct xdp_diag_umem du = {};
	int err;

	if (!umem)
		return 0;

	du.id = umem->id;
	du.size = umem->size;
	du.num_pages = umem->npgs;
	du.chunk_size = (__u32)(~umem->chunk_mask + 1);
	du.headroom = umem->headroom;
	du.ifindex = umem->dev ? umem->dev->ifindex : 0;
	du.queue_id = umem->queue_id;
	du.flags = 0;
	if (umem->zc)
		du.flags |= XDP_DU_F_ZEROCOPY;
	du.refs = refcount_read(&umem->users);

	err = nla_put(nlskb, XDP_DIAG_UMEM, sizeof(du), &du);

	if (!err && umem->fq)
		err = xsk_diag_put_ring(umem->fq, XDP_DIAG_UMEM_FILL_RING, nlskb);
	if (!err && umem->cq) {
		err = xsk_diag_put_ring(umem->cq, XDP_DIAG_UMEM_COMPLETION_RING,
					nlskb);
	}
	return err;
}

static int xsk_diag_fill(struct sock *sk, struct sk_buff *nlskb,
			 struct xdp_diag_req *req,
			 struct user_namespace *user_ns,
			 u32 portid, u32 seq, u32 flags, int sk_ino)
{
	struct xdp_sock *xs = xdp_sk(sk);
	struct xdp_diag_msg *msg;
	struct nlmsghdr *nlh;

	nlh = nlmsg_put(nlskb, portid, seq, SOCK_DIAG_BY_FAMILY, sizeof(*msg),
			flags);
	if (!nlh)
		return -EMSGSIZE;

	msg = nlmsg_data(nlh);
	memset(msg, 0, sizeof(*msg));
	msg->xdiag_family = AF_XDP;
	msg->xdiag_type = sk->sk_type;
	msg->xdiag_ino = sk_ino;
	sock_diag_save_cookie(sk, msg->xdiag_cookie);

	if ((req->xdiag_show & XDP_SHOW_INFO) && xsk_diag_put_info(xs, nlskb))
		goto out_nlmsg_trim;

	if ((req->xdiag_show & XDP_SHOW_INFO) &&
	    nla_put_u32(nlskb, XDP_DIAG_UID,
			from_kuid_munged(user_ns, sock_i_uid(sk))))
		goto out_nlmsg_trim;

	if ((req->xdiag_show & XDP_SHOW_RING_CFG) &&
	    xsk_diag_put_rings_cfg(xs, nlskb))
		goto out_nlmsg_trim;

	if ((req->xdiag_show & XDP_SHOW_UMEM) &&
	    xsk_diag_put_umem(xs, nlskb))
		goto out_nlmsg_trim;

	if ((req->xdiag_show & XDP_SHOW_MEMINFO) &&
	    sock_diag_put_meminfo(sk, nlskb, XDP_DIAG_MEMINFO))
		goto out_nlmsg_trim;

	nlmsg_end(nlskb, nlh);
	return 0;

out_nlmsg_trim:
	nlmsg_cancel(nlskb, nlh);
	return -EMSGSIZE;
}

static int xsk_diag_dump(struct sk_buff *nlskb, struct netlink_callback *cb)
{
	struct xdp_diag_req *req = nlmsg_data(cb->nlh);
	struct net *net = sock_net(nlskb->sk);
	int num = 0, s_num = cb->args[0];
	struct sock *sk;

	mutex_lock(&net->xdp.lock);

	sk_for_each(sk, &net->xdp.list) {
		if (!net_eq(sock_net(sk), net))
			continue;
		if (num++ < s_num)
			continue;

		if (xsk_diag_fill(sk, nlskb, req,
				  sk_user_ns(NETLINK_CB(cb->skb).sk),
				  NETLINK_CB(cb->skb).portid,
				  cb->nlh->nlmsg_seq, NLM_F_MULTI,
				  sock_i_ino(sk)) < 0) {
			num--;
			break;
		}
	}

	mutex_unlock(&net->xdp.lock);
	cb->args[0] = num;
	return nlskb->len;
}

static int xsk_diag_handler_dump(struct sk_buff *nlskb, struct nlmsghdr *hdr)
{
	struct netlink_dump_control c = { .dump = xsk_diag_dump };
	int hdrlen = sizeof(struct xdp_diag_req);
	struct net *net = sock_net(nlskb->sk);

	if (nlmsg_len(hdr) < hdrlen)
		return -EINVAL;

	if (!(hdr->nlmsg_flags & NLM_F_DUMP))
		return -EOPNOTSUPP;

	return netlink_dump_start(net->diag_nlsk, nlskb, hdr, &c);
}

static const struct sock_diag_handler xsk_diag_handler = {
	.family = AF_XDP,
	.dump = xsk_diag_handler_dump,
};

static int __init xsk_diag_init(void)
{
	return sock_diag_register(&xsk_diag_handler);
}

static void __exit xsk_diag_exit(void)
{
	sock_diag_unregister(&xsk_diag_handler);
}

module_init(xsk_diag_init);
module_exit(xsk_diag_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_NET_PF_PROTO_TYPE(PF_NETLINK, NETLINK_SOCK_DIAG, AF_XDP);
