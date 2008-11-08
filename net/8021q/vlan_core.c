#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/if_vlan.h>
#include "vlan.h"

struct vlan_hwaccel_cb {
	struct net_device	*dev;
};

static inline struct vlan_hwaccel_cb *vlan_hwaccel_cb(struct sk_buff *skb)
{
	return (struct vlan_hwaccel_cb *)skb->cb;
}

/* VLAN rx hw acceleration helper.  This acts like netif_{rx,receive_skb}(). */
int __vlan_hwaccel_rx(struct sk_buff *skb, struct vlan_group *grp,
		      u16 vlan_tci, int polling)
{
	struct vlan_hwaccel_cb *cb = vlan_hwaccel_cb(skb);

	if (skb_bond_should_drop(skb)) {
		dev_kfree_skb_any(skb);
		return NET_RX_DROP;
	}

	skb->vlan_tci = vlan_tci;
	cb->dev = vlan_group_get_device(grp, vlan_tci & VLAN_VID_MASK);

	return (polling ? netif_receive_skb(skb) : netif_rx(skb));
}
EXPORT_SYMBOL(__vlan_hwaccel_rx);

int vlan_hwaccel_do_receive(struct sk_buff *skb)
{
	struct vlan_hwaccel_cb *cb = vlan_hwaccel_cb(skb);
	struct net_device *dev = cb->dev;
	struct net_device_stats *stats;

	netif_nit_deliver(skb);

	if (dev == NULL) {
		kfree_skb(skb);
		return -1;
	}

	skb->dev = dev;
	skb->priority = vlan_get_ingress_priority(dev, skb->vlan_tci);
	skb->vlan_tci = 0;

	dev->last_rx = jiffies;

	stats = &dev->stats;
	stats->rx_packets++;
	stats->rx_bytes += skb->len;

	switch (skb->pkt_type) {
	case PACKET_BROADCAST:
		break;
	case PACKET_MULTICAST:
		stats->multicast++;
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
EXPORT_SYMBOL_GPL(vlan_dev_real_dev);

u16 vlan_dev_vlan_id(const struct net_device *dev)
{
	return vlan_dev_info(dev)->vlan_id;
}
EXPORT_SYMBOL_GPL(vlan_dev_vlan_id);
