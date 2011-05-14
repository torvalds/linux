/*
 * Copyright (C) 2007-2011 B.A.T.M.A.N. contributors:
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
 *
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
#include <linux/slab.h>
#include <linux/ethtool.h>
#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include "unicast.h"


static int bat_get_settings(struct net_device *dev, struct ethtool_cmd *cmd);
static void bat_get_drvinfo(struct net_device *dev,
			    struct ethtool_drvinfo *info);
static u32 bat_get_msglevel(struct net_device *dev);
static void bat_set_msglevel(struct net_device *dev, u32 value);
static u32 bat_get_link(struct net_device *dev);
static u32 bat_get_rx_csum(struct net_device *dev);
static int bat_set_rx_csum(struct net_device *dev, u32 data);

static const struct ethtool_ops bat_ethtool_ops = {
	.get_settings = bat_get_settings,
	.get_drvinfo = bat_get_drvinfo,
	.get_msglevel = bat_get_msglevel,
	.set_msglevel = bat_set_msglevel,
	.get_link = bat_get_link,
	.get_rx_csum = bat_get_rx_csum,
	.set_rx_csum = bat_set_rx_csum
};

int my_skb_head_push(struct sk_buff *skb, unsigned int len)
{
	int result;

	/**
	 * TODO: We must check if we can release all references to non-payload
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

static void softif_neigh_free_rcu(struct rcu_head *rcu)
{
	struct softif_neigh *softif_neigh;

	softif_neigh = container_of(rcu, struct softif_neigh, rcu);
	kfree(softif_neigh);
}

static void softif_neigh_free_ref(struct softif_neigh *softif_neigh)
{
	if (atomic_dec_and_test(&softif_neigh->refcount))
		call_rcu(&softif_neigh->rcu, softif_neigh_free_rcu);
}

void softif_neigh_purge(struct bat_priv *bat_priv)
{
	struct softif_neigh *softif_neigh, *softif_neigh_tmp;
	struct hlist_node *node, *node_tmp;

	spin_lock_bh(&bat_priv->softif_neigh_lock);

	hlist_for_each_entry_safe(softif_neigh, node, node_tmp,
				  &bat_priv->softif_neigh_list, list) {

		if ((!time_after(jiffies, softif_neigh->last_seen +
				msecs_to_jiffies(SOFTIF_NEIGH_TIMEOUT))) &&
		    (atomic_read(&bat_priv->mesh_state) == MESH_ACTIVE))
			continue;

		hlist_del_rcu(&softif_neigh->list);

		if (bat_priv->softif_neigh == softif_neigh) {
			bat_dbg(DBG_ROUTES, bat_priv,
				 "Current mesh exit point '%pM' vanished "
				 "(vid: %d).\n",
				 softif_neigh->addr, softif_neigh->vid);
			softif_neigh_tmp = bat_priv->softif_neigh;
			bat_priv->softif_neigh = NULL;
			softif_neigh_free_ref(softif_neigh_tmp);
		}

		softif_neigh_free_ref(softif_neigh);
	}

	spin_unlock_bh(&bat_priv->softif_neigh_lock);
}

static struct softif_neigh *softif_neigh_get(struct bat_priv *bat_priv,
					     uint8_t *addr, short vid)
{
	struct softif_neigh *softif_neigh;
	struct hlist_node *node;

	rcu_read_lock();
	hlist_for_each_entry_rcu(softif_neigh, node,
				 &bat_priv->softif_neigh_list, list) {
		if (!compare_eth(softif_neigh->addr, addr))
			continue;

		if (softif_neigh->vid != vid)
			continue;

		if (!atomic_inc_not_zero(&softif_neigh->refcount))
			continue;

		softif_neigh->last_seen = jiffies;
		goto out;
	}

	softif_neigh = kzalloc(sizeof(struct softif_neigh), GFP_ATOMIC);
	if (!softif_neigh)
		goto out;

	memcpy(softif_neigh->addr, addr, ETH_ALEN);
	softif_neigh->vid = vid;
	softif_neigh->last_seen = jiffies;
	/* initialize with 2 - caller decrements counter by one */
	atomic_set(&softif_neigh->refcount, 2);

	INIT_HLIST_NODE(&softif_neigh->list);
	spin_lock_bh(&bat_priv->softif_neigh_lock);
	hlist_add_head_rcu(&softif_neigh->list, &bat_priv->softif_neigh_list);
	spin_unlock_bh(&bat_priv->softif_neigh_lock);

out:
	rcu_read_unlock();
	return softif_neigh;
}

int softif_neigh_seq_print_text(struct seq_file *seq, void *offset)
{
	struct net_device *net_dev = (struct net_device *)seq->private;
	struct bat_priv *bat_priv = netdev_priv(net_dev);
	struct softif_neigh *softif_neigh;
	struct hlist_node *node;

	if (!bat_priv->primary_if) {
		return seq_printf(seq, "BATMAN mesh %s disabled - "
			       "please specify interfaces to enable it\n",
			       net_dev->name);
	}

	seq_printf(seq, "Softif neighbor list (%s)\n", net_dev->name);

	rcu_read_lock();
	hlist_for_each_entry_rcu(softif_neigh, node,
				 &bat_priv->softif_neigh_list, list)
		seq_printf(seq, "%s %pM (vid: %d)\n",
				bat_priv->softif_neigh == softif_neigh
				? "=>" : "  ", softif_neigh->addr,
				softif_neigh->vid);
	rcu_read_unlock();

	return 0;
}

static void softif_batman_recv(struct sk_buff *skb, struct net_device *dev,
			       short vid)
{
	struct bat_priv *bat_priv = netdev_priv(dev);
	struct ethhdr *ethhdr = (struct ethhdr *)skb->data;
	struct batman_packet *batman_packet;
	struct softif_neigh *softif_neigh, *softif_neigh_tmp;

	if (ntohs(ethhdr->h_proto) == ETH_P_8021Q)
		batman_packet = (struct batman_packet *)
					(skb->data + ETH_HLEN + VLAN_HLEN);
	else
		batman_packet = (struct batman_packet *)(skb->data + ETH_HLEN);

	if (batman_packet->version != COMPAT_VERSION)
		goto err;

	if (batman_packet->packet_type != BAT_PACKET)
		goto err;

	if (!(batman_packet->flags & PRIMARIES_FIRST_HOP))
		goto err;

	if (is_my_mac(batman_packet->orig))
		goto err;

	softif_neigh = softif_neigh_get(bat_priv, batman_packet->orig, vid);

	if (!softif_neigh)
		goto err;

	if (bat_priv->softif_neigh == softif_neigh)
		goto out;

	/* we got a neighbor but its mac is 'bigger' than ours  */
	if (memcmp(bat_priv->primary_if->net_dev->dev_addr,
		   softif_neigh->addr, ETH_ALEN) < 0)
		goto out;

	/* switch to new 'smallest neighbor' */
	if ((bat_priv->softif_neigh) &&
	    (memcmp(softif_neigh->addr, bat_priv->softif_neigh->addr,
							ETH_ALEN) < 0)) {
		bat_dbg(DBG_ROUTES, bat_priv,
			"Changing mesh exit point from %pM (vid: %d) "
			"to %pM (vid: %d).\n",
			 bat_priv->softif_neigh->addr,
			 bat_priv->softif_neigh->vid,
			 softif_neigh->addr, softif_neigh->vid);
		softif_neigh_tmp = bat_priv->softif_neigh;
		bat_priv->softif_neigh = softif_neigh;
		softif_neigh_free_ref(softif_neigh_tmp);
		/* we need to hold the additional reference */
		goto err;
	}

	/* close own batX device and use softif_neigh as exit node */
	if ((!bat_priv->softif_neigh) &&
	    (memcmp(softif_neigh->addr,
		    bat_priv->primary_if->net_dev->dev_addr, ETH_ALEN) < 0)) {
		bat_dbg(DBG_ROUTES, bat_priv,
			"Setting mesh exit point to %pM (vid: %d).\n",
			softif_neigh->addr, softif_neigh->vid);
		bat_priv->softif_neigh = softif_neigh;
		/* we need to hold the additional reference */
		goto err;
	}

out:
	softif_neigh_free_ref(softif_neigh);
err:
	kfree_skb(skb);
	return;
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

	/* only modify hna-table if it has been initialised before */
	if (atomic_read(&bat_priv->mesh_state) == MESH_ACTIVE) {
		hna_local_remove(bat_priv, dev->dev_addr,
				 "mac address changed");
		hna_local_add(dev, addr->sa_data);
	}

	memcpy(dev->dev_addr, addr->sa_data, ETH_ALEN);
	return 0;
}

static int interface_change_mtu(struct net_device *dev, int new_mtu)
{
	/* check ranges */
	if ((new_mtu < 68) || (new_mtu > hardif_min_mtu(dev)))
		return -EINVAL;

	dev->mtu = new_mtu;

	return 0;
}

int interface_tx(struct sk_buff *skb, struct net_device *soft_iface)
{
	struct ethhdr *ethhdr = (struct ethhdr *)skb->data;
	struct bat_priv *bat_priv = netdev_priv(soft_iface);
	struct bcast_packet *bcast_packet;
	struct vlan_ethhdr *vhdr;
	int data_len = skb->len, ret;
	short vid = -1;
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
		softif_batman_recv(skb, soft_iface, vid);
		goto end;
	}

	/**
	 * if we have a another chosen mesh exit node in range
	 * it will transport the packets to the mesh
	 */
	if ((bat_priv->softif_neigh) && (bat_priv->softif_neigh->vid == vid))
		goto dropped;

	/* TODO: check this for locks */
	hna_local_add(soft_iface, ethhdr->h_source);

	if (is_multicast_ether_addr(ethhdr->h_dest)) {
		ret = gw_is_target(bat_priv, skb);

		if (ret < 0)
			goto dropped;

		if (ret == 0)
			do_bcast = true;
	}

	/* ethernet packet should be broadcasted */
	if (do_bcast) {
		if (!bat_priv->primary_if)
			goto dropped;

		if (my_skb_head_push(skb, sizeof(struct bcast_packet)) < 0)
			goto dropped;

		bcast_packet = (struct bcast_packet *)skb->data;
		bcast_packet->version = COMPAT_VERSION;
		bcast_packet->ttl = TTL;

		/* batman packet type: broadcast */
		bcast_packet->packet_type = BAT_BCAST;

		/* hw address of first interface is the orig mac because only
		 * this mac is known throughout the mesh */
		memcpy(bcast_packet->orig,
		       bat_priv->primary_if->net_dev->dev_addr, ETH_ALEN);

		/* set broadcast sequence number */
		bcast_packet->seqno =
			htonl(atomic_inc_return(&bat_priv->bcast_seqno));

		add_bcast_packet_to_list(bat_priv, skb);

		/* a copy is stored in the bcast list, therefore removing
		 * the original skb. */
		kfree_skb(skb);

	/* unicast packet */
	} else {
		ret = unicast_send_skb(skb, bat_priv);
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
	return NETDEV_TX_OK;
}

void interface_rx(struct net_device *soft_iface,
		  struct sk_buff *skb, struct hard_iface *recv_if,
		  int hdr_size)
{
	struct bat_priv *bat_priv = netdev_priv(soft_iface);
	struct unicast_packet *unicast_packet;
	struct ethhdr *ethhdr;
	struct vlan_ethhdr *vhdr;
	short vid = -1;
	int ret;

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

	/**
	 * if we have a another chosen mesh exit node in range
	 * it will transport the packets to the non-mesh network
	 */
	if ((bat_priv->softif_neigh) && (bat_priv->softif_neigh->vid == vid)) {
		skb_push(skb, hdr_size);
		unicast_packet = (struct unicast_packet *)skb->data;

		if ((unicast_packet->packet_type != BAT_UNICAST) &&
		    (unicast_packet->packet_type != BAT_UNICAST_FRAG))
			goto dropped;

		skb_reset_mac_header(skb);

		memcpy(unicast_packet->dest,
		       bat_priv->softif_neigh->addr, ETH_ALEN);
		ret = route_unicast_packet(skb, recv_if);
		if (ret == NET_RX_DROP)
			goto dropped;

		goto out;
	}

	/* skb->dev & skb->pkt_type are set here */
	if (unlikely(!pskb_may_pull(skb, ETH_HLEN)))
		goto dropped;
	skb->protocol = eth_type_trans(skb, soft_iface);

	/* should not be necessary anymore as we use skb_pull_rcsum()
	 * TODO: please verify this and remove this TODO
	 * -- Dec 21st 2009, Simon Wunderlich */

/*	skb->ip_summed = CHECKSUM_UNNECESSARY;*/

	bat_priv->stats.rx_packets++;
	bat_priv->stats.rx_bytes += skb->len + sizeof(struct ethhdr);

	soft_iface->last_rx = jiffies;

	netif_rx(skb);
	return;

dropped:
	kfree_skb(skb);
out:
	return;
}

#ifdef HAVE_NET_DEVICE_OPS
static const struct net_device_ops bat_netdev_ops = {
	.ndo_open = interface_open,
	.ndo_stop = interface_release,
	.ndo_get_stats = interface_stats,
	.ndo_set_mac_address = interface_set_mac_addr,
	.ndo_change_mtu = interface_change_mtu,
	.ndo_start_xmit = interface_tx,
	.ndo_validate_addr = eth_validate_addr
};
#endif

static void interface_setup(struct net_device *dev)
{
	struct bat_priv *priv = netdev_priv(dev);
	char dev_addr[ETH_ALEN];

	ether_setup(dev);

#ifdef HAVE_NET_DEVICE_OPS
	dev->netdev_ops = &bat_netdev_ops;
#else
	dev->open = interface_open;
	dev->stop = interface_release;
	dev->get_stats = interface_stats;
	dev->set_mac_address = interface_set_mac_addr;
	dev->change_mtu = interface_change_mtu;
	dev->hard_start_xmit = interface_tx;
#endif
	dev->destructor = free_netdev;

	/**
	 * can't call min_mtu, because the needed variables
	 * have not been initialized yet
	 */
	dev->mtu = ETH_DATA_LEN;
	dev->hard_header_len = BAT_HEADER_LEN; /* reserve more space in the
						* skbuff for our header */

	/* generate random address */
	random_ether_addr(dev_addr);
	memcpy(dev->dev_addr, dev_addr, ETH_ALEN);

	SET_ETHTOOL_OPS(dev, &bat_ethtool_ops);

	memset(priv, 0, sizeof(struct bat_priv));
}

struct net_device *softif_create(char *name)
{
	struct net_device *soft_iface;
	struct bat_priv *bat_priv;
	int ret;

	soft_iface = alloc_netdev(sizeof(struct bat_priv) , name,
				   interface_setup);

	if (!soft_iface) {
		pr_err("Unable to allocate the batman interface: %s\n", name);
		goto out;
	}

	ret = register_netdev(soft_iface);
	if (ret < 0) {
		pr_err("Unable to register the batman interface '%s': %i\n",
		       name, ret);
		goto free_soft_iface;
	}

	bat_priv = netdev_priv(soft_iface);

	atomic_set(&bat_priv->aggregated_ogms, 1);
	atomic_set(&bat_priv->bonding, 0);
	atomic_set(&bat_priv->vis_mode, VIS_TYPE_CLIENT_UPDATE);
	atomic_set(&bat_priv->gw_mode, GW_MODE_OFF);
	atomic_set(&bat_priv->gw_sel_class, 20);
	atomic_set(&bat_priv->gw_bandwidth, 41);
	atomic_set(&bat_priv->orig_interval, 1000);
	atomic_set(&bat_priv->hop_penalty, 10);
	atomic_set(&bat_priv->log_level, 0);
	atomic_set(&bat_priv->fragmentation, 1);
	atomic_set(&bat_priv->bcast_queue_left, BCAST_QUEUE_LEN);
	atomic_set(&bat_priv->batman_queue_left, BATMAN_QUEUE_LEN);

	atomic_set(&bat_priv->mesh_state, MESH_INACTIVE);
	atomic_set(&bat_priv->bcast_seqno, 1);
	atomic_set(&bat_priv->hna_local_changed, 0);

	bat_priv->primary_if = NULL;
	bat_priv->num_ifaces = 0;
	bat_priv->softif_neigh = NULL;

	ret = sysfs_add_meshif(soft_iface);
	if (ret < 0)
		goto unreg_soft_iface;

	ret = debugfs_add_meshif(soft_iface);
	if (ret < 0)
		goto unreg_sysfs;

	ret = mesh_init(soft_iface);
	if (ret < 0)
		goto unreg_debugfs;

	return soft_iface;

unreg_debugfs:
	debugfs_del_meshif(soft_iface);
unreg_sysfs:
	sysfs_del_meshif(soft_iface);
unreg_soft_iface:
	unregister_netdev(soft_iface);
	return NULL;

free_soft_iface:
	free_netdev(soft_iface);
out:
	return NULL;
}

void softif_destroy(struct net_device *soft_iface)
{
	debugfs_del_meshif(soft_iface);
	sysfs_del_meshif(soft_iface);
	mesh_free(soft_iface);
	unregister_netdevice(soft_iface);
}

int softif_is_valid(struct net_device *net_dev)
{
#ifdef HAVE_NET_DEVICE_OPS
	if (net_dev->netdev_ops->ndo_start_xmit == interface_tx)
		return 1;
#else
	if (net_dev->hard_start_xmit == interface_tx)
		return 1;
#endif

	return 0;
}

/* ethtool */
static int bat_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	cmd->supported = 0;
	cmd->advertising = 0;
	cmd->speed = SPEED_10;
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

static u32 bat_get_rx_csum(struct net_device *dev)
{
	return 0;
}

static int bat_set_rx_csum(struct net_device *dev, u32 data)
{
	return -EOPNOTSUPP;
}
