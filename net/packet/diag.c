#include <linux/module.h>
#include <linux/sock_diag.h>
#include <linux/net.h>
#include <linux/netdevice.h>
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

static int pdiag_put_mclist(const struct packet_sock *po, struct sk_buff *nlskb)
{
	struct nlattr *mca;
	struct packet_mclist *ml;

	mca = nla_nest_start(nlskb, PACKET_DIAG_MCLIST);
	if (!mca)
		return -EMSGSIZE;

	rtnl_lock();
	for (ml = po->mclist; ml; ml = ml->next) {
		struct packet_diag_mclist *dml;

		dml = nla_reserve_nohdr(nlskb, sizeof(*dml));
		if (!dml) {
			rtnl_unlock();
			nla_nest_cancel(nlskb, mca);
			return -EMSGSIZE;
		}

		dml->pdmc_index = ml->ifindex;
		dml->pdmc_type = ml->type;
		dml->pdmc_alen = ml->alen;
		dml->pdmc_count = ml->count;
		BUILD_BUG_ON(sizeof(dml->pdmc_addr) != sizeof(ml->addr));
		memcpy(dml->pdmc_addr, ml->addr, sizeof(ml->addr));
	}

	rtnl_unlock();
	nla_nest_end(nlskb, mca);

	return 0;
}

static int pdiag_put_ring(struct packet_ring_buffer *ring, int ver, int nl_type,
		struct sk_buff *nlskb)
{
	struct packet_diag_ring pdr;

	if (!ring->pg_vec || ((ver > TPACKET_V2) &&
				(nl_type == PACKET_DIAG_TX_RING)))
		return 0;

	pdr.pdr_block_size = ring->pg_vec_pages << PAGE_SHIFT;
	pdr.pdr_block_nr = ring->pg_vec_len;
	pdr.pdr_frame_size = ring->frame_size;
	pdr.pdr_frame_nr = ring->frame_max + 1;

	if (ver > TPACKET_V2) {
		pdr.pdr_retire_tmo = ring->prb_bdqc.retire_blk_tov;
		pdr.pdr_sizeof_priv = ring->prb_bdqc.blk_sizeof_priv;
		pdr.pdr_features = ring->prb_bdqc.feature_req_word;
	} else {
		pdr.pdr_retire_tmo = 0;
		pdr.pdr_sizeof_priv = 0;
		pdr.pdr_features = 0;
	}

	return nla_put(nlskb, nl_type, sizeof(pdr), &pdr);
}

static int pdiag_put_rings_cfg(struct packet_sock *po, struct sk_buff *skb)
{
	int ret;

	mutex_lock(&po->pg_vec_lock);
	ret = pdiag_put_ring(&po->rx_ring, po->tp_version,
			PACKET_DIAG_RX_RING, skb);
	if (!ret)
		ret = pdiag_put_ring(&po->tx_ring, po->tp_version,
				PACKET_DIAG_TX_RING, skb);
	mutex_unlock(&po->pg_vec_lock);

	return ret;
}

static int pdiag_put_fanout(struct packet_sock *po, struct sk_buff *nlskb)
{
	int ret = 0;

	mutex_lock(&fanout_mutex);
	if (po->fanout) {
		u32 val;

		val = (u32)po->fanout->id | ((u32)po->fanout->type << 16);
		ret = nla_put_u32(nlskb, PACKET_DIAG_FANOUT, val);
	}
	mutex_unlock(&fanout_mutex);

	return ret;
}

static int sk_diag_fill(struct sock *sk, struct sk_buff *skb,
			struct packet_diag_req *req,
			bool may_report_filterinfo,
			struct user_namespace *user_ns,
			u32 portid, u32 seq, u32 flags, int sk_ino)
{
	struct nlmsghdr *nlh;
	struct packet_diag_msg *rp;
	struct packet_sock *po = pkt_sk(sk);

	nlh = nlmsg_put(skb, portid, seq, SOCK_DIAG_BY_FAMILY, sizeof(*rp), flags);
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

	if ((req->pdiag_show & PACKET_SHOW_INFO) &&
	    nla_put_u32(skb, PACKET_DIAG_UID,
			from_kuid_munged(user_ns, sock_i_uid(sk))))
		goto out_nlmsg_trim;

	if ((req->pdiag_show & PACKET_SHOW_MCLIST) &&
			pdiag_put_mclist(po, skb))
		goto out_nlmsg_trim;

	if ((req->pdiag_show & PACKET_SHOW_RING_CFG) &&
			pdiag_put_rings_cfg(po, skb))
		goto out_nlmsg_trim;

	if ((req->pdiag_show & PACKET_SHOW_FANOUT) &&
			pdiag_put_fanout(po, skb))
		goto out_nlmsg_trim;

	if ((req->pdiag_show & PACKET_SHOW_MEMINFO) &&
	    sock_diag_put_meminfo(sk, skb, PACKET_DIAG_MEMINFO))
		goto out_nlmsg_trim;

	if ((req->pdiag_show & PACKET_SHOW_FILTER) &&
	    sock_diag_put_filterinfo(may_report_filterinfo, sk, skb,
				     PACKET_DIAG_FILTER))
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
	bool may_report_filterinfo;

	net = sock_net(skb->sk);
	req = nlmsg_data(cb->nlh);
	may_report_filterinfo = ns_capable(net->user_ns, CAP_NET_ADMIN);

	mutex_lock(&net->packet.sklist_lock);
	sk_for_each(sk, &net->packet.sklist) {
		if (!net_eq(sock_net(sk), net))
			continue;
		if (num < s_num)
			goto next;

		if (sk_diag_fill(sk, skb, req,
				 may_report_filterinfo,
				 sk_user_ns(NETLINK_CB(cb->skb).sk),
				 NETLINK_CB(cb->skb).portid,
				 cb->nlh->nlmsg_seq, NLM_F_MULTI,
				 sock_i_ino(sk)) < 0)
			goto done;
next:
		num++;
	}
done:
	mutex_unlock(&net->packet.sklist_lock);
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
