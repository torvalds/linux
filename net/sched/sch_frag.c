// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
#include <net/netlink.h>
#include <net/sch_generic.h>
#include <net/dst.h>
#include <net/ip.h>
#include <net/ip6_fib.h>

struct sch_frag_data {
	unsigned long dst;
	struct qdisc_skb_cb cb;
	__be16 inner_protocol;
	u16 vlan_tci;
	__be16 vlan_proto;
	unsigned int l2_len;
	u8 l2_data[VLAN_ETH_HLEN];
	int (*xmit)(struct sk_buff *skb);
};

static DEFINE_PER_CPU(struct sch_frag_data, sch_frag_data_storage);

static int sch_frag_xmit(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	struct sch_frag_data *data = this_cpu_ptr(&sch_frag_data_storage);

	if (skb_cow_head(skb, data->l2_len) < 0) {
		kfree_skb(skb);
		return -ENOMEM;
	}

	__skb_dst_copy(skb, data->dst);
	*qdisc_skb_cb(skb) = data->cb;
	skb->inner_protocol = data->inner_protocol;
	if (data->vlan_tci & VLAN_CFI_MASK)
		__vlan_hwaccel_put_tag(skb, data->vlan_proto,
				       data->vlan_tci & ~VLAN_CFI_MASK);
	else
		__vlan_hwaccel_clear_tag(skb);

	/* Reconstruct the MAC header.  */
	skb_push(skb, data->l2_len);
	memcpy(skb->data, &data->l2_data, data->l2_len);
	skb_postpush_rcsum(skb, skb->data, data->l2_len);
	skb_reset_mac_header(skb);

	return data->xmit(skb);
}

static void sch_frag_prepare_frag(struct sk_buff *skb,
				  int (*xmit)(struct sk_buff *skb))
{
	unsigned int hlen = skb_network_offset(skb);
	struct sch_frag_data *data;

	data = this_cpu_ptr(&sch_frag_data_storage);
	data->dst = skb->_skb_refdst;
	data->cb = *qdisc_skb_cb(skb);
	data->xmit = xmit;
	data->inner_protocol = skb->inner_protocol;
	if (skb_vlan_tag_present(skb))
		data->vlan_tci = skb_vlan_tag_get(skb) | VLAN_CFI_MASK;
	else
		data->vlan_tci = 0;
	data->vlan_proto = skb->vlan_proto;
	data->l2_len = hlen;
	memcpy(&data->l2_data, skb->data, hlen);

	memset(IPCB(skb), 0, sizeof(struct inet_skb_parm));
	skb_pull(skb, hlen);
}

static unsigned int
sch_frag_dst_get_mtu(const struct dst_entry *dst)
{
	return dst->dev->mtu;
}

static struct dst_ops sch_frag_dst_ops = {
	.family = AF_UNSPEC,
	.mtu = sch_frag_dst_get_mtu,
};

static int sch_fragment(struct net *net, struct sk_buff *skb,
			u16 mru, int (*xmit)(struct sk_buff *skb))
{
	int ret = -1;

	if (skb_network_offset(skb) > VLAN_ETH_HLEN) {
		net_warn_ratelimited("L2 header too long to fragment\n");
		goto err;
	}

	if (skb_protocol(skb, true) == htons(ETH_P_IP)) {
		struct dst_entry sch_frag_dst;
		unsigned long orig_dst;

		sch_frag_prepare_frag(skb, xmit);
		dst_init(&sch_frag_dst, &sch_frag_dst_ops, NULL, 1,
			 DST_OBSOLETE_NONE, DST_NOCOUNT);
		sch_frag_dst.dev = skb->dev;

		orig_dst = skb->_skb_refdst;
		skb_dst_set_noref(skb, &sch_frag_dst);
		IPCB(skb)->frag_max_size = mru;

		ret = ip_do_fragment(net, skb->sk, skb, sch_frag_xmit);
		refdst_drop(orig_dst);
	} else if (skb_protocol(skb, true) == htons(ETH_P_IPV6)) {
		unsigned long orig_dst;
		struct rt6_info sch_frag_rt;

		sch_frag_prepare_frag(skb, xmit);
		memset(&sch_frag_rt, 0, sizeof(sch_frag_rt));
		dst_init(&sch_frag_rt.dst, &sch_frag_dst_ops, NULL, 1,
			 DST_OBSOLETE_NONE, DST_NOCOUNT);
		sch_frag_rt.dst.dev = skb->dev;

		orig_dst = skb->_skb_refdst;
		skb_dst_set_noref(skb, &sch_frag_rt.dst);
		IP6CB(skb)->frag_max_size = mru;

		ret = ipv6_stub->ipv6_fragment(net, skb->sk, skb,
					       sch_frag_xmit);
		refdst_drop(orig_dst);
	} else {
		net_warn_ratelimited("Fail frag %s: eth=%x, MRU=%d, MTU=%d\n",
				     netdev_name(skb->dev),
				     ntohs(skb_protocol(skb, true)), mru,
				     skb->dev->mtu);
		goto err;
	}

	return ret;
err:
	kfree_skb(skb);
	return ret;
}

int sch_frag_xmit_hook(struct sk_buff *skb, int (*xmit)(struct sk_buff *skb))
{
	u16 mru = qdisc_skb_cb(skb)->mru;
	int err;

	if (mru && skb->len > mru + skb->dev->hard_header_len)
		err = sch_fragment(dev_net(skb->dev), skb, mru, xmit);
	else
		err = xmit(skb);

	return err;
}
EXPORT_SYMBOL_GPL(sch_frag_xmit_hook);
