#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/if_vlan.h>
#include <linux/netpoll.h>
#include <linux/export.h>
#include "vlan.h"

bool vlan_do_receive(struct sk_buff **skbp, bool last_handler)
{
	struct sk_buff *skb = *skbp;
	u16 vlan_id = skb->vlan_tci & VLAN_VID_MASK;
	struct net_device *vlan_dev;
	struct vlan_pcpu_stats *rx_stats;

	vlan_dev = vlan_find_dev(skb->dev, vlan_id);
	if (!vlan_dev) {
		/* Only the last call to vlan_do_receive() should change
		 * pkt_type to PACKET_OTHERHOST
		 */
		if (vlan_id && last_handler)
			skb->pkt_type = PACKET_OTHERHOST;
		return false;
	}

	skb = *skbp = skb_share_check(skb, GFP_ATOMIC);
	if (unlikely(!skb))
		return false;

	skb->dev = vlan_dev;
	if (skb->pkt_type == PACKET_OTHERHOST) {
		/* Our lower layer thinks this is not local, let's make sure.
		 * This allows the VLAN to have a different MAC than the
		 * underlying device, and still route correctly. */
		if (ether_addr_equal(eth_hdr(skb)->h_dest, vlan_dev->dev_addr))
			skb->pkt_type = PACKET_HOST;
	}

	if (!(vlan_dev_priv(vlan_dev)->flags & VLAN_FLAG_REORDER_HDR)) {
		unsigned int offset = skb->data - skb_mac_header(skb);

		/*
		 * vlan_insert_tag expect skb->data pointing to mac header.
		 * So change skb->data before calling it and change back to
		 * original position later
		 */
		skb_push(skb, offset);
		skb = *skbp = vlan_insert_tag(skb, skb->vlan_tci);
		if (!skb)
			return false;
		skb_pull(skb, offset + VLAN_HLEN);
		skb_reset_mac_len(skb);
	}

	skb->priority = vlan_get_ingress_priority(vlan_dev, skb->vlan_tci);
	skb->vlan_tci = 0;

	rx_stats = this_cpu_ptr(vlan_dev_priv(vlan_dev)->vlan_pcpu_stats);

	u64_stats_update_begin(&rx_stats->syncp);
	rx_stats->rx_packets++;
	rx_stats->rx_bytes += skb->len;
	if (skb->pkt_type == PACKET_MULTICAST)
		rx_stats->rx_multicast++;
	u64_stats_update_end(&rx_stats->syncp);

	return true;
}

/* Must be invoked with rcu_read_lock or with RTNL. */
struct net_device *__vlan_find_dev_deep(struct net_device *real_dev,
					u16 vlan_id)
{
	struct vlan_info *vlan_info = rcu_dereference_rtnl(real_dev->vlan_info);

	if (vlan_info) {
		return vlan_group_get_device(&vlan_info->grp, vlan_id);
	} else {
		/*
		 * Bonding slaves do not have grp assigned to themselves.
		 * Grp is assigned to bonding master instead.
		 */
		if (netif_is_bond_slave(real_dev))
			return __vlan_find_dev_deep(real_dev->master, vlan_id);
	}

	return NULL;
}
EXPORT_SYMBOL(__vlan_find_dev_deep);

struct net_device *vlan_dev_real_dev(const struct net_device *dev)
{
	return vlan_dev_priv(dev)->real_dev;
}
EXPORT_SYMBOL(vlan_dev_real_dev);

u16 vlan_dev_vlan_id(const struct net_device *dev)
{
	return vlan_dev_priv(dev)->vlan_id;
}
EXPORT_SYMBOL(vlan_dev_vlan_id);

static struct sk_buff *vlan_reorder_header(struct sk_buff *skb)
{
	if (skb_cow(skb, skb_headroom(skb)) < 0)
		return NULL;
	memmove(skb->data - ETH_HLEN, skb->data - VLAN_ETH_HLEN, 2 * ETH_ALEN);
	skb->mac_header += VLAN_HLEN;
	return skb;
}

struct sk_buff *vlan_untag(struct sk_buff *skb)
{
	struct vlan_hdr *vhdr;
	u16 vlan_tci;

	if (unlikely(vlan_tx_tag_present(skb))) {
		/* vlan_tci is already set-up so leave this for another time */
		return skb;
	}

	skb = skb_share_check(skb, GFP_ATOMIC);
	if (unlikely(!skb))
		goto err_free;

	if (unlikely(!pskb_may_pull(skb, VLAN_HLEN)))
		goto err_free;

	vhdr = (struct vlan_hdr *) skb->data;
	vlan_tci = ntohs(vhdr->h_vlan_TCI);
	__vlan_hwaccel_put_tag(skb, vlan_tci);

	skb_pull_rcsum(skb, VLAN_HLEN);
	vlan_set_encap_proto(skb, vhdr);

	skb = vlan_reorder_header(skb);
	if (unlikely(!skb))
		goto err_free;

	skb_reset_network_header(skb);
	skb_reset_transport_header(skb);
	skb_reset_mac_len(skb);

	return skb;

err_free:
	kfree_skb(skb);
	return NULL;
}


/*
 * vlan info and vid list
 */

static void vlan_group_free(struct vlan_group *grp)
{
	int i;

	for (i = 0; i < VLAN_GROUP_ARRAY_SPLIT_PARTS; i++)
		kfree(grp->vlan_devices_arrays[i]);
}

static void vlan_info_free(struct vlan_info *vlan_info)
{
	vlan_group_free(&vlan_info->grp);
	kfree(vlan_info);
}

static void vlan_info_rcu_free(struct rcu_head *rcu)
{
	vlan_info_free(container_of(rcu, struct vlan_info, rcu));
}

static struct vlan_info *vlan_info_alloc(struct net_device *dev)
{
	struct vlan_info *vlan_info;

	vlan_info = kzalloc(sizeof(struct vlan_info), GFP_KERNEL);
	if (!vlan_info)
		return NULL;

	vlan_info->real_dev = dev;
	INIT_LIST_HEAD(&vlan_info->vid_list);
	return vlan_info;
}

struct vlan_vid_info {
	struct list_head list;
	unsigned short vid;
	int refcount;
};

static struct vlan_vid_info *vlan_vid_info_get(struct vlan_info *vlan_info,
					       unsigned short vid)
{
	struct vlan_vid_info *vid_info;

	list_for_each_entry(vid_info, &vlan_info->vid_list, list) {
		if (vid_info->vid == vid)
			return vid_info;
	}
	return NULL;
}

static struct vlan_vid_info *vlan_vid_info_alloc(unsigned short vid)
{
	struct vlan_vid_info *vid_info;

	vid_info = kzalloc(sizeof(struct vlan_vid_info), GFP_KERNEL);
	if (!vid_info)
		return NULL;
	vid_info->vid = vid;

	return vid_info;
}

static int __vlan_vid_add(struct vlan_info *vlan_info, unsigned short vid,
			  struct vlan_vid_info **pvid_info)
{
	struct net_device *dev = vlan_info->real_dev;
	const struct net_device_ops *ops = dev->netdev_ops;
	struct vlan_vid_info *vid_info;
	int err;

	vid_info = vlan_vid_info_alloc(vid);
	if (!vid_info)
		return -ENOMEM;

	if ((dev->features & NETIF_F_HW_VLAN_FILTER) &&
	    ops->ndo_vlan_rx_add_vid) {
		err =  ops->ndo_vlan_rx_add_vid(dev, vid);
		if (err) {
			kfree(vid_info);
			return err;
		}
	}
	list_add(&vid_info->list, &vlan_info->vid_list);
	vlan_info->nr_vids++;
	*pvid_info = vid_info;
	return 0;
}

int vlan_vid_add(struct net_device *dev, unsigned short vid)
{
	struct vlan_info *vlan_info;
	struct vlan_vid_info *vid_info;
	bool vlan_info_created = false;
	int err;

	ASSERT_RTNL();

	vlan_info = rtnl_dereference(dev->vlan_info);
	if (!vlan_info) {
		vlan_info = vlan_info_alloc(dev);
		if (!vlan_info)
			return -ENOMEM;
		vlan_info_created = true;
	}
	vid_info = vlan_vid_info_get(vlan_info, vid);
	if (!vid_info) {
		err = __vlan_vid_add(vlan_info, vid, &vid_info);
		if (err)
			goto out_free_vlan_info;
	}
	vid_info->refcount++;

	if (vlan_info_created)
		rcu_assign_pointer(dev->vlan_info, vlan_info);

	return 0;

out_free_vlan_info:
	if (vlan_info_created)
		kfree(vlan_info);
	return err;
}
EXPORT_SYMBOL(vlan_vid_add);

static void __vlan_vid_del(struct vlan_info *vlan_info,
			   struct vlan_vid_info *vid_info)
{
	struct net_device *dev = vlan_info->real_dev;
	const struct net_device_ops *ops = dev->netdev_ops;
	unsigned short vid = vid_info->vid;
	int err;

	if ((dev->features & NETIF_F_HW_VLAN_FILTER) &&
	     ops->ndo_vlan_rx_kill_vid) {
		err = ops->ndo_vlan_rx_kill_vid(dev, vid);
		if (err) {
			pr_warn("failed to kill vid %d for device %s\n",
				vid, dev->name);
		}
	}
	list_del(&vid_info->list);
	kfree(vid_info);
	vlan_info->nr_vids--;
}

void vlan_vid_del(struct net_device *dev, unsigned short vid)
{
	struct vlan_info *vlan_info;
	struct vlan_vid_info *vid_info;

	ASSERT_RTNL();

	vlan_info = rtnl_dereference(dev->vlan_info);
	if (!vlan_info)
		return;

	vid_info = vlan_vid_info_get(vlan_info, vid);
	if (!vid_info)
		return;
	vid_info->refcount--;
	if (vid_info->refcount == 0) {
		__vlan_vid_del(vlan_info, vid_info);
		if (vlan_info->nr_vids == 0) {
			RCU_INIT_POINTER(dev->vlan_info, NULL);
			call_rcu(&vlan_info->rcu, vlan_info_rcu_free);
		}
	}
}
EXPORT_SYMBOL(vlan_vid_del);

int vlan_vids_add_by_dev(struct net_device *dev,
			 const struct net_device *by_dev)
{
	struct vlan_vid_info *vid_info;
	struct vlan_info *vlan_info;
	int err;

	ASSERT_RTNL();

	vlan_info = rtnl_dereference(by_dev->vlan_info);
	if (!vlan_info)
		return 0;

	list_for_each_entry(vid_info, &vlan_info->vid_list, list) {
		err = vlan_vid_add(dev, vid_info->vid);
		if (err)
			goto unwind;
	}
	return 0;

unwind:
	list_for_each_entry_continue_reverse(vid_info,
					     &vlan_info->vid_list,
					     list) {
		vlan_vid_del(dev, vid_info->vid);
	}

	return err;
}
EXPORT_SYMBOL(vlan_vids_add_by_dev);

void vlan_vids_del_by_dev(struct net_device *dev,
			  const struct net_device *by_dev)
{
	struct vlan_vid_info *vid_info;
	struct vlan_info *vlan_info;

	ASSERT_RTNL();

	vlan_info = rtnl_dereference(by_dev->vlan_info);
	if (!vlan_info)
		return;

	list_for_each_entry(vid_info, &vlan_info->vid_list, list)
		vlan_vid_del(dev, vid_info->vid);
}
EXPORT_SYMBOL(vlan_vids_del_by_dev);

bool vlan_uses_dev(const struct net_device *dev)
{
	return rtnl_dereference(dev->vlan_info) ? true : false;
}
EXPORT_SYMBOL(vlan_uses_dev);
