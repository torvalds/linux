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
#include "routing.h"
#include "send.h"
#include "bat_debugfs.h"
#include "translation-table.h"
#include "hash.h"
#include "gateway_common.h"
#include "gateway_client.h"
#include "bat_sysfs.h"
#include "originator.h"
#include <linux/slab.h>
#include <linux/ethtool.h>
#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include "unicast.h"
#include "bridge_loop_avoidance.h"


static int bat_get_settings(struct net_device *dev, struct ethtool_cmd *cmd);
static void bat_get_drvinfo(struct net_device *dev,
			    struct ethtool_drvinfo *info);
static u32 bat_get_msglevel(struct net_device *dev);
static void bat_set_msglevel(struct net_device *dev, u32 value);
static u32 bat_get_link(struct net_device *dev);
static void batadv_get_strings(struct net_device *dev, u32 stringset, u8 *data);
static void batadv_get_ethtool_stats(struct net_device *dev,
				     struct ethtool_stats *stats, u64 *data);
static int batadv_get_sset_count(struct net_device *dev, int stringset);

static const struct ethtool_ops bat_ethtool_ops = {
	.get_settings = bat_get_settings,
	.get_drvinfo = bat_get_drvinfo,
	.get_msglevel = bat_get_msglevel,
	.set_msglevel = bat_set_msglevel,
	.get_link = bat_get_link,
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

static int interface_open(struct net_device *dev)
{
	netif_start_queue(dev);
	return 0;
}

static int interface_release(struct net_device *dev)
{
	netif_stop_queue(dev);
	return 0;
}

static struct net_device_stats *interface_stats(struct net_device *dev)
{
	struct bat_priv *bat_priv = netdev_priv(dev);
	return &bat_priv->stats;
}

static int interface_set_mac_addr(struct net_device *dev, void *p)
{
	struct bat_priv *bat_priv = netdev_priv(dev);
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	/* only modify transtable if it has been initialized before */
	if (atomic_read(&bat_priv->mesh_state) == MESH_ACTIVE) {
		batadv_tt_local_remove(bat_priv, dev->dev_addr,
				       "mac address changed", false);
		batadv_tt_local_add(dev, addr->sa_data, NULL_IFINDEX);
	}

	memcpy(dev->dev_addr, addr->sa_data, ETH_ALEN);
	dev->addr_assign_type &= ~NET_ADDR_RANDOM;
	return 0;
}

static int interface_change_mtu(struct net_device *dev, int new_mtu)
{
	/* check ranges */
	if ((new_mtu < 68) || (new_mtu > batadv_hardif_min_mtu(dev)))
		return -EINVAL;

	dev->mtu = new_mtu;

	return 0;
}

static int interface_tx(struct sk_buff *skb, struct net_device *soft_iface)
{
	struct ethhdr *ethhdr = (struct ethhdr *)skb->data;
	struct bat_priv *bat_priv = netdev_priv(soft_iface);
	struct hard_iface *primary_if = NULL;
	struct bcast_packet *bcast_packet;
	struct vlan_ethhdr *vhdr;
	static const uint8_t stp_addr[ETH_ALEN] = {0x01, 0x80, 0xC2, 0x00, 0x00,
						   0x00};
	unsigned int header_len = 0;
	int data_len = skb->len, ret;
	short vid __maybe_unused = -1;
	bool do_bcast = false;

	if (atomic_read(&bat_priv->mesh_state) != MESH_ACTIVE)
		goto dropped;

	soft_iface->trans_start = jiffies;

	switch (ntohs(ethhdr->h_proto)) {
	case ETH_P_8021Q:
		vhdr = (struct vlan_ethhdr *)skb->data;
		vid = ntohs(vhdr->h_vlan_TCI) & VLAN_VID_MASK;

		if (ntohs(vhdr->h_vlan_encapsulated_proto) != ETH_P_BATMAN)
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
	 */
	if (compare_eth(ethhdr->h_dest, stp_addr))
		goto dropped;

	if (is_multicast_ether_addr(ethhdr->h_dest)) {
		do_bcast = true;

		switch (atomic_read(&bat_priv->gw_mode)) {
		case GW_MODE_SERVER:
			/* gateway servers should not send dhcp
			 * requests into the mesh
			 */
			ret = batadv_gw_is_dhcp_target(skb, &header_len);
			if (ret)
				goto dropped;
			break;
		case GW_MODE_CLIENT:
			/* gateway clients should send dhcp requests
			 * via unicast to their gateway
			 */
			ret = batadv_gw_is_dhcp_target(skb, &header_len);
			if (ret)
				do_bcast = false;
			break;
		case GW_MODE_OFF:
		default:
			break;
		}
	}

	/* ethernet packet should be broadcasted */
	if (do_bcast) {
		primary_if = batadv_primary_if_get_selected(bat_priv);
		if (!primary_if)
			goto dropped;

		if (batadv_skb_head_push(skb, sizeof(*bcast_packet)) < 0)
			goto dropped;

		bcast_packet = (struct bcast_packet *)skb->data;
		bcast_packet->header.version = COMPAT_VERSION;
		bcast_packet->header.ttl = TTL;

		/* batman packet type: broadcast */
		bcast_packet->header.packet_type = BAT_BCAST;

		/* hw address of first interface is the orig mac because only
		 * this mac is known throughout the mesh
		 */
		memcpy(bcast_packet->orig,
		       primary_if->net_dev->dev_addr, ETH_ALEN);

		/* set broadcast sequence number */
		bcast_packet->seqno =
			htonl(atomic_inc_return(&bat_priv->bcast_seqno));

		batadv_add_bcast_packet_to_list(bat_priv, skb, 1);

		/* a copy is stored in the bcast list, therefore removing
		 * the original skb.
		 */
		kfree_skb(skb);

	/* unicast packet */
	} else {
		if (atomic_read(&bat_priv->gw_mode) != GW_MODE_OFF) {
			ret = batadv_gw_out_of_range(bat_priv, skb, ethhdr);
			if (ret)
				goto dropped;
		}

		ret = batadv_unicast_send_skb(skb, bat_priv);
		if (ret != 0)
			goto dropped_freed;
	}

	bat_priv->stats.tx_packets++;
	bat_priv->stats.tx_bytes += data_len;
	goto end;

dropped:
	kfree_skb(skb);
dropped_freed:
	bat_priv->stats.tx_dropped++;
end:
	if (primary_if)
		batadv_hardif_free_ref(primary_if);
	return NETDEV_TX_OK;
}

void batadv_interface_rx(struct net_device *soft_iface,
			 struct sk_buff *skb, struct hard_iface *recv_if,
			 int hdr_size)
{
	struct bat_priv *bat_priv = netdev_priv(soft_iface);
	struct ethhdr *ethhdr;
	struct vlan_ethhdr *vhdr;
	short vid __maybe_unused = -1;

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

		if (ntohs(vhdr->h_vlan_encapsulated_proto) != ETH_P_BATMAN)
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

	bat_priv->stats.rx_packets++;
	bat_priv->stats.rx_bytes += skb->len + ETH_HLEN;

	soft_iface->last_rx = jiffies;

	if (batadv_is_ap_isolated(bat_priv, ethhdr->h_source, ethhdr->h_dest))
		goto dropped;

	/* Let the bridge loop avoidance check the packet. If will
	 * not handle it, we can safely push it up.
	 */
	if (batadv_bla_rx(bat_priv, skb, vid))
		goto out;

	netif_rx(skb);
	goto out;

dropped:
	kfree_skb(skb);
out:
	return;
}

static const struct net_device_ops bat_netdev_ops = {
	.ndo_open = interface_open,
	.ndo_stop = interface_release,
	.ndo_get_stats = interface_stats,
	.ndo_set_mac_address = interface_set_mac_addr,
	.ndo_change_mtu = interface_change_mtu,
	.ndo_start_xmit = interface_tx,
	.ndo_validate_addr = eth_validate_addr
};

static void interface_setup(struct net_device *dev)
{
	struct bat_priv *priv = netdev_priv(dev);

	ether_setup(dev);

	dev->netdev_ops = &bat_netdev_ops;
	dev->destructor = free_netdev;
	dev->tx_queue_len = 0;

	/* can't call min_mtu, because the needed variables
	 * have not been initialized yet
	 */
	dev->mtu = ETH_DATA_LEN;
	/* reserve more space in the skbuff for our header */
	dev->hard_header_len = BAT_HEADER_LEN;

	/* generate random address */
	eth_hw_addr_random(dev);

	SET_ETHTOOL_OPS(dev, &bat_ethtool_ops);

	memset(priv, 0, sizeof(*priv));
}

struct net_device *batadv_softif_create(const char *name)
{
	struct net_device *soft_iface;
	struct bat_priv *bat_priv;
	int ret;

	soft_iface = alloc_netdev(sizeof(*bat_priv), name, interface_setup);

	if (!soft_iface)
		goto out;

	ret = register_netdevice(soft_iface);
	if (ret < 0) {
		pr_err("Unable to register the batman interface '%s': %i\n",
		       name, ret);
		goto free_soft_iface;
	}

	bat_priv = netdev_priv(soft_iface);

	atomic_set(&bat_priv->aggregated_ogms, 1);
	atomic_set(&bat_priv->bonding, 0);
	atomic_set(&bat_priv->bridge_loop_avoidance, 0);
	atomic_set(&bat_priv->ap_isolation, 0);
	atomic_set(&bat_priv->vis_mode, VIS_TYPE_CLIENT_UPDATE);
	atomic_set(&bat_priv->gw_mode, GW_MODE_OFF);
	atomic_set(&bat_priv->gw_sel_class, 20);
	atomic_set(&bat_priv->gw_bandwidth, 41);
	atomic_set(&bat_priv->orig_interval, 1000);
	atomic_set(&bat_priv->hop_penalty, 30);
	atomic_set(&bat_priv->log_level, 0);
	atomic_set(&bat_priv->fragmentation, 1);
	atomic_set(&bat_priv->bcast_queue_left, BCAST_QUEUE_LEN);
	atomic_set(&bat_priv->batman_queue_left, BATMAN_QUEUE_LEN);

	atomic_set(&bat_priv->mesh_state, MESH_INACTIVE);
	atomic_set(&bat_priv->bcast_seqno, 1);
	atomic_set(&bat_priv->ttvn, 0);
	atomic_set(&bat_priv->tt_local_changes, 0);
	atomic_set(&bat_priv->tt_ogm_append_cnt, 0);
	atomic_set(&bat_priv->bla_num_requests, 0);

	bat_priv->tt_buff = NULL;
	bat_priv->tt_buff_len = 0;
	bat_priv->tt_poss_change = false;

	bat_priv->primary_if = NULL;
	bat_priv->num_ifaces = 0;

	bat_priv->bat_counters = __alloc_percpu(sizeof(uint64_t) * BAT_CNT_NUM,
						__alignof__(uint64_t));
	if (!bat_priv->bat_counters)
		goto unreg_soft_iface;

	ret = batadv_algo_select(bat_priv, batadv_routing_algo);
	if (ret < 0)
		goto free_bat_counters;

	ret = batadv_sysfs_add_meshif(soft_iface);
	if (ret < 0)
		goto free_bat_counters;

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
free_bat_counters:
	free_percpu(bat_priv->bat_counters);
unreg_soft_iface:
	unregister_netdevice(soft_iface);
	return NULL;

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
	if (net_dev->netdev_ops->ndo_start_xmit == interface_tx)
		return 1;

	return 0;
}

/* ethtool */
static int bat_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
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

static void bat_get_drvinfo(struct net_device *dev,
			    struct ethtool_drvinfo *info)
{
	strcpy(info->driver, "B.A.T.M.A.N. advanced");
	strcpy(info->version, SOURCE_VERSION);
	strcpy(info->fw_version, "N/A");
	strcpy(info->bus_info, "batman");
}

static u32 bat_get_msglevel(struct net_device *dev)
{
	return -EOPNOTSUPP;
}

static void bat_set_msglevel(struct net_device *dev, u32 value)
{
}

static u32 bat_get_link(struct net_device *dev)
{
	return 1;
}

/* Inspired by drivers/net/ethernet/dlink/sundance.c:1702
 * Declare each description string in struct.name[] to get fixed sized buffer
 * and compile time checking for strings longer than ETH_GSTRING_LEN.
 */
static const struct {
	const char name[ETH_GSTRING_LEN];
} bat_counters_strings[] = {
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
};

static void batadv_get_strings(struct net_device *dev, uint32_t stringset,
			       uint8_t *data)
{
	if (stringset == ETH_SS_STATS)
		memcpy(data, bat_counters_strings,
		       sizeof(bat_counters_strings));
}

static void batadv_get_ethtool_stats(struct net_device *dev,
				     struct ethtool_stats *stats,
				     uint64_t *data)
{
	struct bat_priv *bat_priv = netdev_priv(dev);
	int i;

	for (i = 0; i < BAT_CNT_NUM; i++)
		data[i] = batadv_sum_counter(bat_priv, i);
}

static int batadv_get_sset_count(struct net_device *dev, int stringset)
{
	if (stringset == ETH_SS_STATS)
		return BAT_CNT_NUM;

	return -EOPNOTSUPP;
}
