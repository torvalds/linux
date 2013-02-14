/* Copyright (C) 2007-2012 B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Simon Wunderlich
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 */

#include "main.h"
#include "soft-interface.h"
#include "hard-interface.h"
#include "distributed-arp-table.h"
#include "routing.h"
#include "send.h"
#include "debugfs.h"
#include "translation-table.h"
#include "hash.h"
#include "gateway_common.h"
#include "gateway_client.h"
#include "sysfs.h"
#include "originator.h"
#include <linux/slab.h>
#include <linux/ethtool.h>
#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/if_ether.h>
#include "unicast.h"
#include "bridge_loop_avoidance.h"


static int batadv_get_settings(struct net_device *dev, struct ethtool_cmd *cmd);
static void batadv_get_drvinfo(struct net_device *dev,
			       struct ethtool_drvinfo *info);
static u32 batadv_get_msglevel(struct net_device *dev);
static void batadv_set_msglevel(struct net_device *dev, u32 value);
static u32 batadv_get_link(struct net_device *dev);
static void batadv_get_strings(struct net_device *dev, u32 stringset, u8 *data);
static void batadv_get_ethtool_stats(struct net_device *dev,
				     struct ethtool_stats *stats, u64 *data);
static int batadv_get_sset_count(struct net_device *dev, int stringset);

static const struct ethtool_ops batadv_ethtool_ops = {
	.get_settings = batadv_get_settings,
	.get_drvinfo = batadv_get_drvinfo,
	.get_msglevel = batadv_get_msglevel,
	.set_msglevel = batadv_set_msglevel,
	.get_link = batadv_get_link,
	.get_strings = batadv_get_strings,
	.get_ethtool_stats = batadv_get_ethtool_stats,
	.get_sset_count = batadv_get_sset_count,
};

int batadv_skb_head_push(struct sk_buff *skb, unsigned int len)
{
	int result;

	/* TODO: We must check if we can release all references to non-payload
	 * data using skb_header_release in our skbs to allow skb_cow_header to
	 * work optimally. This means that those skbs are not allowed to read
	 * or write any data which is before the current position of skb->data
	 * after that call and thus allow other skbs with the same data buffer
	 * to write freely in that area.
	 */
	result = skb_cow_head(skb, len);
	if (result < 0)
		return result;

	skb_push(skb, len);
	return 0;
}

static int batadv_interface_open(struct net_device *dev)
{
	netif_start_queue(dev);
	return 0;
}

static int batadv_interface_release(struct net_device *dev)
{
	netif_stop_queue(dev);
	return 0;
}

static struct net_device_stats *batadv_interface_stats(struct net_device *dev)
{
	struct batadv_priv *bat_priv = netdev_priv(dev);
	struct net_device_stats *stats = &bat_priv->stats;

	stats->tx_packets = batadv_sum_counter(bat_priv, BATADV_CNT_TX);
	stats->tx_bytes = batadv_sum_counter(bat_priv, BATADV_CNT_TX_BYTES);
	stats->tx_dropped = batadv_sum_counter(bat_priv, BATADV_CNT_TX_DROPPED);
	stats->rx_packets = batadv_sum_counter(bat_priv, BATADV_CNT_RX);
	stats->rx_bytes = batadv_sum_counter(bat_priv, BATADV_CNT_RX_BYTES);
	return stats;
}

static int batadv_interface_set_mac_addr(struct net_device *dev, void *p)
{
	struct batadv_priv *bat_priv = netdev_priv(dev);
	struct sockaddr *addr = p;
	uint8_t old_addr[ETH_ALEN];

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(old_addr, dev->dev_addr, ETH_ALEN);
	memcpy(dev->dev_addr, addr->sa_data, ETH_ALEN);

	/* only modify transtable if it has been initialized before */
	if (atomic_read(&bat_priv->mesh_state) == BATADV_MESH_ACTIVE) {
		batadv_tt_local_remove(bat_priv, old_addr,
				       "mac address changed", false);
		batadv_tt_local_add(dev, addr->sa_data, BATADV_NULL_IFINDEX);
	}

	dev->addr_assign_type &= ~NET_ADDR_RANDOM;
	return 0;
}

static int batadv_interface_change_mtu(struct net_device *dev, int new_mtu)
{
	/* check ranges */
	if ((new_mtu < 68) || (new_mtu > batadv_hardif_min_mtu(dev)))
		return -EINVAL;

	dev->mtu = new_mtu;

	return 0;
}

static int batadv_interface_tx(struct sk_buff *skb,
			       struct net_device *soft_iface)
{
	struct ethhdr *ethhdr = (struct ethhdr *)skb->data;
	struct batadv_priv *bat_priv = netdev_priv(soft_iface);
	struct batadv_hard_iface *primary_if = NULL;
	struct batadv_bcast_packet *bcast_packet;
	struct vlan_ethhdr *vhdr;
	__be16 ethertype = __constant_htons(ETH_P_BATMAN);
	static const uint8_t stp_addr[ETH_ALEN] = {0x01, 0x80, 0xC2, 0x00,
						   0x00, 0x00};
	static const uint8_t ectp_addr[ETH_ALEN] = {0xCF, 0x00, 0x00, 0x00,
						    0x00, 0x00};
	unsigned int header_len = 0;
	int data_len = skb->len, ret;
	short vid __maybe_unused = -1;
	bool do_bcast = false;
	uint32_t seqno;
	unsigned long brd_delay = 1;

	if (atomic_read(&bat_priv->mesh_state) != BATADV_MESH_ACTIVE)
		goto dropped;

	soft_iface->trans_start = jiffies;

	switch (ntohs(ethhdr->h_proto)) {
	case ETH_P_8021Q:
		vhdr = (struct vlan_ethhdr *)skb->data;
		vid = ntohs(vhdr->h_vlan_TCI) & VLAN_VID_MASK;

		if (vhdr->h_vlan_encapsulated_proto != ethertype)
			break;

		/* fall through */
	case ETH_P_BATMAN:
		goto dropped;
	}

	if (batadv_bla_tx(bat_priv, skb, vid))
		goto dropped;

	/* Register the client MAC in the transtable */
	batadv_tt_local_add(soft_iface, ethhdr->h_source, skb->skb_iif);

	/* don't accept stp packets. STP does not help in meshes.
	 * better use the bridge loop avoidance ...
	 *
	 * The same goes for ECTP sent at least by some Cisco Switches,
	 * it might confuse the mesh when used with bridge loop avoidance.
	 */
	if (batadv_compare_eth(ethhdr->h_dest, stp_addr))
		goto dropped;

	if (batadv_compare_eth(ethhdr->h_dest, ectp_addr))
		goto dropped;

	if (is_multicast_ether_addr(ethhdr->h_dest)) {
		do_bcast = true;

		switch (atomic_read(&bat_priv->gw_mode)) {
		case BATADV_GW_MODE_SERVER:
			/* gateway servers should not send dhcp
			 * requests into the mesh
			 */
			ret = batadv_gw_is_dhcp_target(skb, &header_len);
			if (ret)
				goto dropped;
			break;
		case BATADV_GW_MODE_CLIENT:
			/* gateway clients should send dhcp requests
			 * via unicast to their gateway
			 */
			ret = batadv_gw_is_dhcp_target(skb, &header_len);
			if (ret)
				do_bcast = false;
			break;
		case BATADV_GW_MODE_OFF:
		default:
			break;
		}
	}

	/* ethernet packet should be broadcasted */
	if (do_bcast) {
		primary_if = batadv_primary_if_get_selected(bat_priv);
		if (!primary_if)
			goto dropped;

		/* in case of ARP request, we do not immediately broadcasti the
		 * packet, instead we first wait for DAT to try to retrieve the
		 * correct ARP entry
		 */
		if (batadv_dat_snoop_outgoing_arp_request(bat_priv, skb))
			brd_delay = msecs_to_jiffies(ARP_REQ_DELAY);

		if (batadv_skb_head_push(skb, sizeof(*bcast_packet)) < 0)
			goto dropped;

		bcast_packet = (struct batadv_bcast_packet *)skb->data;
		bcast_packet->header.version = BATADV_COMPAT_VERSION;
		bcast_packet->header.ttl = BATADV_TTL;

		/* batman packet type: broadcast */
		bcast_packet->header.packet_type = BATADV_BCAST;
		bcast_packet->reserved = 0;

		/* hw address of first interface is the orig mac because only
		 * this mac is known throughout the mesh
		 */
		memcpy(bcast_packet->orig,
		       primary_if->net_dev->dev_addr, ETH_ALEN);

		/* set broadcast sequence number */
		seqno = atomic_inc_return(&bat_priv->bcast_seqno);
		bcast_packet->seqno = htonl(seqno);

		batadv_add_bcast_packet_to_list(bat_priv, skb, brd_delay);

		/* a copy is stored in the bcast list, therefore removing
		 * the original skb.
		 */
		kfree_skb(skb);

	/* unicast packet */
	} else {
		if (atomic_read(&bat_priv->gw_mode) != BATADV_GW_MODE_OFF) {
			ret = batadv_gw_out_of_range(bat_priv, skb, ethhdr);
			if (ret)
				goto dropped;
		}

		if (batadv_dat_snoop_outgoing_arp_request(bat_priv, skb))
			goto dropped;

		batadv_dat_snoop_outgoing_arp_reply(bat_priv, skb);

		ret = batadv_unicast_send_skb(bat_priv, skb);
		if (ret != 0)
			goto dropped_freed;
	}

	batadv_inc_counter(bat_priv, BATADV_CNT_TX);
	batadv_add_counter(bat_priv, BATADV_CNT_TX_BYTES, data_len);
	goto end;

dropped:
	kfree_skb(skb);
dropped_freed:
	batadv_inc_counter(bat_priv, BATADV_CNT_TX_DROPPED);
end:
	if (primary_if)
		batadv_hardif_free_ref(primary_if);
	return NETDEV_TX_OK;
}

void batadv_interface_rx(struct net_device *soft_iface,
			 struct sk_buff *skb, struct batadv_hard_iface *recv_if,
			 int hdr_size, struct batadv_orig_node *orig_node)
{
	struct batadv_priv *bat_priv = netdev_priv(soft_iface);
	struct ethhdr *ethhdr;
	struct vlan_ethhdr *vhdr;
	struct batadv_header *batadv_header = (struct batadv_header *)skb->data;
	short vid __maybe_unused = -1;
	__be16 ethertype = __constant_htons(ETH_P_BATMAN);
	bool is_bcast;

	is_bcast = (batadv_header->packet_type == BATADV_BCAST);

	/* check if enough space is available for pulling, and pull */
	if (!pskb_may_pull(skb, hdr_size))
		goto dropped;

	skb_pull_rcsum(skb, hdr_size);
	skb_reset_mac_header(skb);

	ethhdr = (struct ethhdr *)skb_mac_header(skb);

	switch (ntohs(ethhdr->h_proto)) {
	case ETH_P_8021Q:
		vhdr = (struct vlan_ethhdr *)skb->data;
		vid = ntohs(vhdr->h_vlan_TCI) & VLAN_VID_MASK;

		if (vhdr->h_vlan_encapsulated_proto != ethertype)
			break;

		/* fall through */
	case ETH_P_BATMAN:
		goto dropped;
	}

	/* skb->dev & skb->pkt_type are set here */
	if (unlikely(!pskb_may_pull(skb, ETH_HLEN)))
		goto dropped;
	skb->protocol = eth_type_trans(skb, soft_iface);

	/* should not be necessary anymore as we use skb_pull_rcsum()
	 * TODO: please verify this and remove this TODO
	 * -- Dec 21st 2009, Simon Wunderlich
	 */

	/* skb->ip_summed = CHECKSUM_UNNECESSARY; */

	batadv_inc_counter(bat_priv, BATADV_CNT_RX);
	batadv_add_counter(bat_priv, BATADV_CNT_RX_BYTES,
			   skb->len + ETH_HLEN);

	soft_iface->last_rx = jiffies;

	/* Let the bridge loop avoidance check the packet. If will
	 * not handle it, we can safely push it up.
	 */
	if (batadv_bla_rx(bat_priv, skb, vid, is_bcast))
		goto out;

	if (orig_node)
		batadv_tt_add_temporary_global_entry(bat_priv, orig_node,
						     ethhdr->h_source);

	if (batadv_is_ap_isolated(bat_priv, ethhdr->h_source, ethhdr->h_dest))
		goto dropped;

	netif_rx(skb);
	goto out;

dropped:
	kfree_skb(skb);
out:
	return;
}

/* batman-adv network devices have devices nesting below it and are a special
 * "super class" of normal network devices; split their locks off into a
 * separate class since they always nest.
 */
static struct lock_class_key batadv_netdev_xmit_lock_key;
static struct lock_class_key batadv_netdev_addr_lock_key;

/**
 * batadv_set_lockdep_class_one - Set lockdep class for a single tx queue
 * @dev: device which owns the tx queue
 * @txq: tx queue to modify
 * @_unused: always NULL
 */
static void batadv_set_lockdep_class_one(struct net_device *dev,
					 struct netdev_queue *txq,
					 void *_unused)
{
	lockdep_set_class(&txq->_xmit_lock, &batadv_netdev_xmit_lock_key);
}

/**
 * batadv_set_lockdep_class - Set txq and addr_list lockdep class
 * @dev: network device to modify
 */
static void batadv_set_lockdep_class(struct net_device *dev)
{
	lockdep_set_class(&dev->addr_list_lock, &batadv_netdev_addr_lock_key);
	netdev_for_each_tx_queue(dev, batadv_set_lockdep_class_one, NULL);
}

/**
 * batadv_softif_init - Late stage initialization of soft interface
 * @dev: registered network device to modify
 *
 * Returns error code on failures
 */
static int batadv_softif_init(struct net_device *dev)
{
	batadv_set_lockdep_class(dev);

	return 0;
}

static const struct net_device_ops batadv_netdev_ops = {
	.ndo_init = batadv_softif_init,
	.ndo_open = batadv_interface_open,
	.ndo_stop = batadv_interface_release,
	.ndo_get_stats = batadv_interface_stats,
	.ndo_set_mac_address = batadv_interface_set_mac_addr,
	.ndo_change_mtu = batadv_interface_change_mtu,
	.ndo_start_xmit = batadv_interface_tx,
	.ndo_validate_addr = eth_validate_addr
};

static void batadv_interface_setup(struct net_device *dev)
{
	struct batadv_priv *priv = netdev_priv(dev);

	ether_setup(dev);

	dev->netdev_ops = &batadv_netdev_ops;
	dev->destructor = free_netdev;
	dev->tx_queue_len = 0;

	/* can't call min_mtu, because the needed variables
	 * have not been initialized yet
	 */
	dev->mtu = ETH_DATA_LEN;
	/* reserve more space in the skbuff for our header */
	dev->hard_header_len = BATADV_HEADER_LEN;

	/* generate random address */
	eth_hw_addr_random(dev);

	SET_ETHTOOL_OPS(dev, &batadv_ethtool_ops);

	memset(priv, 0, sizeof(*priv));
}

struct net_device *batadv_softif_create(const char *name)
{
	struct net_device *soft_iface;
	struct batadv_priv *bat_priv;
	int ret;
	size_t cnt_len = sizeof(uint64_t) * BATADV_CNT_NUM;

	soft_iface = alloc_netdev(sizeof(*bat_priv), name,
				  batadv_interface_setup);

	if (!soft_iface)
		goto out;

	bat_priv = netdev_priv(soft_iface);

	/* batadv_interface_stats() needs to be available as soon as
	 * register_netdevice() has been called
	 */
	bat_priv->bat_counters = __alloc_percpu(cnt_len, __alignof__(uint64_t));
	if (!bat_priv->bat_counters)
		goto free_soft_iface;

	ret = register_netdevice(soft_iface);
	if (ret < 0) {
		pr_err("Unable to register the batman interface '%s': %i\n",
		       name, ret);
		goto free_bat_counters;
	}

	atomic_set(&bat_priv->aggregated_ogms, 1);
	atomic_set(&bat_priv->bonding, 0);
	atomic_set(&bat_priv->bridge_loop_avoidance, 0);
#ifdef CONFIG_BATMAN_ADV_DAT
	atomic_set(&bat_priv->distributed_arp_table, 1);
#endif
	atomic_set(&bat_priv->ap_isolation, 0);
	atomic_set(&bat_priv->vis_mode, BATADV_VIS_TYPE_CLIENT_UPDATE);
	atomic_set(&bat_priv->gw_mode, BATADV_GW_MODE_OFF);
	atomic_set(&bat_priv->gw_sel_class, 20);
	atomic_set(&bat_priv->gw_bandwidth, 41);
	atomic_set(&bat_priv->orig_interval, 1000);
	atomic_set(&bat_priv->hop_penalty, 30);
	atomic_set(&bat_priv->log_level, 0);
	atomic_set(&bat_priv->fragmentation, 1);
	atomic_set(&bat_priv->bcast_queue_left, BATADV_BCAST_QUEUE_LEN);
	atomic_set(&bat_priv->batman_queue_left, BATADV_BATMAN_QUEUE_LEN);

	atomic_set(&bat_priv->mesh_state, BATADV_MESH_INACTIVE);
	atomic_set(&bat_priv->bcast_seqno, 1);
	atomic_set(&bat_priv->tt.vn, 0);
	atomic_set(&bat_priv->tt.local_changes, 0);
	atomic_set(&bat_priv->tt.ogm_append_cnt, 0);
#ifdef CONFIG_BATMAN_ADV_BLA
	atomic_set(&bat_priv->bla.num_requests, 0);
#endif
	bat_priv->tt.last_changeset = NULL;
	bat_priv->tt.last_changeset_len = 0;

	bat_priv->primary_if = NULL;
	bat_priv->num_ifaces = 0;

	ret = batadv_algo_select(bat_priv, batadv_routing_algo);
	if (ret < 0)
		goto unreg_soft_iface;

	ret = batadv_sysfs_add_meshif(soft_iface);
	if (ret < 0)
		goto unreg_soft_iface;

	ret = batadv_debugfs_add_meshif(soft_iface);
	if (ret < 0)
		goto unreg_sysfs;

	ret = batadv_mesh_init(soft_iface);
	if (ret < 0)
		goto unreg_debugfs;

	return soft_iface;

unreg_debugfs:
	batadv_debugfs_del_meshif(soft_iface);
unreg_sysfs:
	batadv_sysfs_del_meshif(soft_iface);
unreg_soft_iface:
	free_percpu(bat_priv->bat_counters);
	unregister_netdevice(soft_iface);
	return NULL;

free_bat_counters:
	free_percpu(bat_priv->bat_counters);
free_soft_iface:
	free_netdev(soft_iface);
out:
	return NULL;
}

void batadv_softif_destroy(struct net_device *soft_iface)
{
	batadv_debugfs_del_meshif(soft_iface);
	batadv_sysfs_del_meshif(soft_iface);
	batadv_mesh_free(soft_iface);
	unregister_netdevice(soft_iface);
}

int batadv_softif_is_valid(const struct net_device *net_dev)
{
	if (net_dev->netdev_ops->ndo_start_xmit == batadv_interface_tx)
		return 1;

	return 0;
}

/* ethtool */
static int batadv_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	cmd->supported = 0;
	cmd->advertising = 0;
	ethtool_cmd_speed_set(cmd, SPEED_10);
	cmd->duplex = DUPLEX_FULL;
	cmd->port = PORT_TP;
	cmd->phy_address = 0;
	cmd->transceiver = XCVR_INTERNAL;
	cmd->autoneg = AUTONEG_DISABLE;
	cmd->maxtxpkt = 0;
	cmd->maxrxpkt = 0;

	return 0;
}

static void batadv_get_drvinfo(struct net_device *dev,
			       struct ethtool_drvinfo *info)
{
	strcpy(info->driver, "B.A.T.M.A.N. advanced");
	strcpy(info->version, BATADV_SOURCE_VERSION);
	strcpy(info->fw_version, "N/A");
	strcpy(info->bus_info, "batman");
}

static u32 batadv_get_msglevel(struct net_device *dev)
{
	return -EOPNOTSUPP;
}

static void batadv_set_msglevel(struct net_device *dev, u32 value)
{
}

static u32 batadv_get_link(struct net_device *dev)
{
	return 1;
}

/* Inspired by drivers/net/ethernet/dlink/sundance.c:1702
 * Declare each description string in struct.name[] to get fixed sized buffer
 * and compile time checking for strings longer than ETH_GSTRING_LEN.
 */
static const struct {
	const char name[ETH_GSTRING_LEN];
} batadv_counters_strings[] = {
	{ "tx" },
	{ "tx_bytes" },
	{ "tx_dropped" },
	{ "rx" },
	{ "rx_bytes" },
	{ "forward" },
	{ "forward_bytes" },
	{ "mgmt_tx" },
	{ "mgmt_tx_bytes" },
	{ "mgmt_rx" },
	{ "mgmt_rx_bytes" },
	{ "tt_request_tx" },
	{ "tt_request_rx" },
	{ "tt_response_tx" },
	{ "tt_response_rx" },
	{ "tt_roam_adv_tx" },
	{ "tt_roam_adv_rx" },
#ifdef CONFIG_BATMAN_ADV_DAT
	{ "dat_get_tx" },
	{ "dat_get_rx" },
	{ "dat_put_tx" },
	{ "dat_put_rx" },
	{ "dat_cached_reply_tx" },
#endif
};

static void batadv_get_strings(struct net_device *dev, uint32_t stringset,
			       uint8_t *data)
{
	if (stringset == ETH_SS_STATS)
		memcpy(data, batadv_counters_strings,
		       sizeof(batadv_counters_strings));
}

static void batadv_get_ethtool_stats(struct net_device *dev,
				     struct ethtool_stats *stats,
				     uint64_t *data)
{
	struct batadv_priv *bat_priv = netdev_priv(dev);
	int i;

	for (i = 0; i < BATADV_CNT_NUM; i++)
		data[i] = batadv_sum_counter(bat_priv, i);
}

static int batadv_get_sset_count(struct net_device *dev, int stringset)
{
	if (stringset == ETH_SS_STATS)
		return BATADV_CNT_NUM;

	return -EOPNOTSUPP;
}
