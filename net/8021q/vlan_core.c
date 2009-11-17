#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/if_vlan.h>
#include <linux/netpoll.h>
#include "vlan.h"

/* VLAN rx hw acceleration helper.  This acts like netif_{rx,receive_skb}(). */
int __vlan_hwaccel_rx(struct sk_buff *skb, struct vlan_group *grp,
		      u16 vlan_tci, int polling)
{
	if (netpoll_rx(skb))
		return NET_RX_DROP;

	if (skb_bond_should_drop(skb))
		goto drop;

	__vlan_hwaccel_put_tag(skb, vlan_tci);
	skb->dev = vlan_group_get_device(grp, vlan_tci & VLAN_VID_MASK);

	if (!skb->dev)
		goto drop;

	return (polling ? netif_receive_skb(skb) : netif_rx(skb));

drop:
	dev_kfree_skb_any(skb);
	return NET_RX_DROP;
}
EXPORT_SYMBOL(__vlan_hwaccel_rx);

int vlan_hwaccel_do_receive(struct sk_buff *skb)
{
	struct net_device *dev = skb->dev;
	struct vlan_rx_stats     *rx_stats;

	skb->dev = vlan_dev_info(dev)->real_dev;
	netif_nit_deliver(skb);

	skb->dev = dev;
	skb->priority = vlan_get_ingress_priority(dev, skb->vlan_tci);
	skb->vlan_tci = 0;

	rx_stats = per_cpu_ptr(vlan_dev_info(dev)->vlan_rx_stats,
			       smp_processor_id());

	rx_stats->rx_packets++;
	rx_stats->rx_bytes += skb->len;

	switch (skb->pkt_type) {
	case PACKET_BROADCAST:
		break;
	case PACKET_MULTICAST:
		rx_stats->multicast++;
		break;
	case PACKET_OTHERHOST:
		/* Our lower layer thinks this is not local, let's make sure.
		 * This allows the VLAN to have a different MAC than the
		 * underlying device, and still route correctly. */
		if (!compare_ether_addr(eth_hdr(skb)->h_dest,
					dev->dev_addr))
			skb->pkt_type = PACKET_HOST;
		break;
	};
	return 0;
}

struct net_device *vlan_dev_real_dev(const struct net_device *dev)
{
	return vlan_dev_info(dev)->real_dev;
}
EXPORT_SYMBOL(vlan_dev_real_dev);

u16 vlan_dev_vlan_id(const struct net_device *dev)
{
	return vlan_dev_info(dev)->vlan_id;
}
EXPORT_SYMBOL(vlan_dev_vlan_id);

static gro_result_t
vlan_gro_common(struct napi_struct *napi, struct vlan_group *grp,
		unsigned int vlan_tci, struct sk_buff *skb)
{
	struct sk_buff *p;

	if (skb_bond_should_drop(skb))
		goto drop;

	__vlan_hwaccel_put_tag(skb, vlan_tci);
	skb->dev = vlan_group_get_device(grp, vlan_tci & VLAN_VID_MASK);

	if (!skb->dev)
		goto drop;

	for (p = napi->gro_list; p; p = p->next) {
		NAPI_GRO_CB(p)->same_flow =
			p->dev == skb->dev && !compare_ether_header(
				skb_mac_header(p), skb_gro_mac_header(skb));
		NAPI_GRO_CB(p)->flush = 0;
	}

	return dev_gro_receive(napi, skb);

drop:
	return GRO_DROP;
}

gro_result_t vlan_gro_receive(struct napi_struct *napi, struct vlan_group *grp,
			      unsigned int vlan_tci, struct sk_buff *skb)
{
	if (netpoll_rx_on(skb))
		return vlan_hwaccel_receive_skb(skb, grp, vlan_tci)
			? GRO_DROP : GRO_NORMAL;

	skb_gro_reset_offset(skb);

	return napi_skb_finish(vlan_gro_common(napi, grp, vlan_tci, skb), skb);
}
EXPORT_SYMBOL(vlan_gro_receive);

gro_result_t vlan_gro_frags(struct napi_struct *napi, struct vlan_group *grp,
			    unsigned int vlan_tci)
{
	struct sk_buff *skb = napi_frags_skb(napi);

	if (!skb)
		return GRO_DROP;

	if (netpoll_rx_on(skb)) {
		skb->protocol = eth_type_trans(skb, skb->dev);
		return vlan_hwaccel_receive_skb(skb, grp, vlan_tci)
			? GRO_DROP : GRO_NORMAL;
	}

	return napi_frags_finish(napi, skb,
				 vlan_gro_common(napi, grp, vlan_tci, skb));
}
EXPORT_SYMBOL(vlan_gro_frags);
