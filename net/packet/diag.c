#include <linux/module.h>
#include <linux/sock_diag.h>
#include <linux/net.h>
#include <linux/packet_diag.h>
#include <net/net_namespace.h>
#include <net/sock.h>

#include "internal.h"

static int pdiag_put_info(const struct packet_sock *po, struct sk_buff *nlskb)
{
	struct packet_diag_info pinfo;

	pinfo.pdi_index = po->ifindex;
	pinfo.pdi_version = po->tp_version;
	pinfo.pdi_reserve = po->tp_reserve;
	pinfo.pdi_copy_thresh = po->copy_thresh;
	pinfo.pdi_tstamp = po->tp_tstamp;

	pinfo.pdi_flags = 0;
	if (po->running)
		pinfo.pdi_flags |= PDI_RUNNING;
	if (po->auxdata)
		pinfo.pdi_flags |= PDI_AUXDATA;
	if (po->origdev)
		pinfo.pdi_flags |= PDI_ORIGDEV;
	if (po->has_vnet_hdr)
		pinfo.pdi_flags |= PDI_VNETHDR;
	if (po->tp_loss)
		pinfo.pdi_flags |= PDI_LOSS;

	return nla_put(nlskb, PACKET_DIAG_INFO, sizeof(pinfo), &pinfo);
}

static int sk_diag_fill(struct sock *sk, struct sk_buff *skb, struct packet_diag_req *req,
		u32 pid, u32 seq, u32 flags, int sk_ino)
{
	struct nlmsghdr *nlh;
	struct packet_diag_msg *rp;
	const struct packet_sock *po = pkt_sk(sk);

	nlh = nlmsg_put(skb, pid, seq, SOCK_DIAG_BY_FAMILY, sizeof(*rp), flags);
	if (!nlh)
		return -EMSGSIZE;

	rp = nlmsg_data(nlh);
	rp->pdiag_family = AF_PACKET;
	rp->pdiag_type = sk->sk_type;
	rp->pdiag_num = ntohs(po->num);
	rp->pdiag_ino = sk_ino;
	sock_diag_save_cookie(sk, rp->pdiag_cookie);

	if ((req->pdiag_show & PACKET_SHOW_INFO) &&
			pdiag_put_info(po, skb))
		goto out_nlmsg_trim;

	return nlmsg_end(skb, nlh);

out_nlmsg_trim:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static int packet_diag_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	int num = 0, s_num = cb->args[0];
	struct packet_diag_req *req;
	struct net *net;
	struct sock *sk;
	struct hlist_node *node;

	net = sock_net(skb->sk);
	req = nlmsg_data(cb->nlh);

	rcu_read_lock();
	sk_for_each_rcu(sk, node, &net->packet.sklist) {
		if (!net_eq(sock_net(sk), net))
			continue;
		if (num < s_num)
			goto next;

		if (sk_diag_fill(sk, skb, req, NETLINK_CB(cb->skb).pid,
					cb->nlh->nlmsg_seq, NLM_F_MULTI,
					sock_i_ino(sk)) < 0)
			goto done;
next:
		num++;
	}
done:
	rcu_read_unlock();
	cb->args[0] = num;

	return skb->len;
}

static int packet_diag_handler_dump(struct sk_buff *skb, struct nlmsghdr *h)
{
	int hdrlen = sizeof(struct packet_diag_req);
	struct net *net = sock_net(skb->sk);
	struct packet_diag_req *req;

	if (nlmsg_len(h) < hdrlen)
		return -EINVAL;

	req = nlmsg_data(h);
	/* Make it possible to support protocol filtering later */
	if (req->sdiag_protocol)
		return -EINVAL;

	if (h->nlmsg_flags & NLM_F_DUMP) {
		struct netlink_dump_control c = {
			.dump = packet_diag_dump,
		};
		return netlink_dump_start(net->diag_nlsk, skb, h, &c);
	} else
		return -EOPNOTSUPP;
}

static const struct sock_diag_handler packet_diag_handler = {
	.family = AF_PACKET,
	.dump = packet_diag_handler_dump,
};

static int __init packet_diag_init(void)
{
	return sock_diag_register(&packet_diag_handler);
}

static void __exit packet_diag_exit(void)
{
	sock_diag_unregister(&packet_diag_handler);
}

module_init(packet_diag_init);
module_exit(packet_diag_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_NET_PF_PROTO_TYPE(PF_NETLINK, NETLINK_SOCK_DIAG, 17 /* AF_PACKET */);
