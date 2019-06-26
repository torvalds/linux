// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2007-2019  B.A.T.M.A.N. contributors:
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "main.h"

#include <linux/atomic.h>
#include <linux/build_bug.h>
#include <linux/byteorder/generic.h>
#include <linux/crc32c.h>
#include <linux/errno.h>
#include <linux/genetlink.h>
#include <linux/gfp.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/init.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/printk.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>
#include <linux/seq_file.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/workqueue.h>
#include <net/dsfield.h>
#include <net/rtnetlink.h>
#include <uapi/linux/batadv_packet.h>
#include <uapi/linux/batman_adv.h>

#include "bat_algo.h"
#include "bat_iv_ogm.h"
#include "bat_v.h"
#include "bridge_loop_avoidance.h"
#include "debugfs.h"
#include "distributed-arp-table.h"
#include "gateway_client.h"
#include "gateway_common.h"
#include "hard-interface.h"
#include "icmp_socket.h"
#include "log.h"
#include "multicast.h"
#include "netlink.h"
#include "network-coding.h"
#include "originator.h"
#include "routing.h"
#include "send.h"
#include "soft-interface.h"
#include "tp_meter.h"
#include "translation-table.h"

/* List manipulations on hardif_list have to be rtnl_lock()'ed,
 * list traversals just rcu-locked
 */
struct list_head batadv_hardif_list;
unsigned int batadv_hardif_generation;
static int (*batadv_rx_handler[256])(struct sk_buff *skb,
				     struct batadv_hard_iface *recv_if);

unsigned char batadv_broadcast_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

struct workqueue_struct *batadv_event_workqueue;

static void batadv_recv_handler_init(void);

static int __init batadv_init(void)
{
	int ret;

	ret = batadv_tt_cache_init();
	if (ret < 0)
		return ret;

	INIT_LIST_HEAD(&batadv_hardif_list);
	batadv_algo_init();

	batadv_recv_handler_init();

	batadv_v_init();
	batadv_iv_init();
	batadv_nc_init();
	batadv_tp_meter_init();

	batadv_event_workqueue = create_singlethread_workqueue("bat_events");
	if (!batadv_event_workqueue)
		goto err_create_wq;

	batadv_socket_init();
	batadv_debugfs_init();

	register_netdevice_notifier(&batadv_hard_if_notifier);
	rtnl_link_register(&batadv_link_ops);
	batadv_netlink_register();

	pr_info("B.A.T.M.A.N. advanced %s (compatibility version %i) loaded\n",
		BATADV_SOURCE_VERSION, BATADV_COMPAT_VERSION);

	return 0;

err_create_wq:
	batadv_tt_cache_destroy();

	return -ENOMEM;
}

static void __exit batadv_exit(void)
{
	batadv_debugfs_destroy();
	batadv_netlink_unregister();
	rtnl_link_unregister(&batadv_link_ops);
	unregister_netdevice_notifier(&batadv_hard_if_notifier);
	batadv_hardif_remove_interfaces();

	flush_workqueue(batadv_event_workqueue);
	destroy_workqueue(batadv_event_workqueue);
	batadv_event_workqueue = NULL;

	rcu_barrier();

	batadv_tt_cache_destroy();
}

/**
 * batadv_mesh_init() - Initialize soft interface
 * @soft_iface: netdev struct of the soft interface
 *
 * Return: 0 on success or negative error number in case of failure
 */
int batadv_mesh_init(struct net_device *soft_iface)
{
	struct batadv_priv *bat_priv = netdev_priv(soft_iface);
	int ret;

	spin_lock_init(&bat_priv->forw_bat_list_lock);
	spin_lock_init(&bat_priv->forw_bcast_list_lock);
	spin_lock_init(&bat_priv->tt.changes_list_lock);
	spin_lock_init(&bat_priv->tt.req_list_lock);
	spin_lock_init(&bat_priv->tt.roam_list_lock);
	spin_lock_init(&bat_priv->tt.last_changeset_lock);
	spin_lock_init(&bat_priv->tt.commit_lock);
	spin_lock_init(&bat_priv->gw.list_lock);
#ifdef CONFIG_BATMAN_ADV_MCAST
	spin_lock_init(&bat_priv->mcast.want_lists_lock);
#endif
	spin_lock_init(&bat_priv->tvlv.container_list_lock);
	spin_lock_init(&bat_priv->tvlv.handler_list_lock);
	spin_lock_init(&bat_priv->softif_vlan_list_lock);
	spin_lock_init(&bat_priv->tp_list_lock);

	INIT_HLIST_HEAD(&bat_priv->forw_bat_list);
	INIT_HLIST_HEAD(&bat_priv->forw_bcast_list);
	INIT_HLIST_HEAD(&bat_priv->gw.gateway_list);
#ifdef CONFIG_BATMAN_ADV_MCAST
	INIT_HLIST_HEAD(&bat_priv->mcast.want_all_unsnoopables_list);
	INIT_HLIST_HEAD(&bat_priv->mcast.want_all_ipv4_list);
	INIT_HLIST_HEAD(&bat_priv->mcast.want_all_ipv6_list);
#endif
	INIT_LIST_HEAD(&bat_priv->tt.changes_list);
	INIT_HLIST_HEAD(&bat_priv->tt.req_list);
	INIT_LIST_HEAD(&bat_priv->tt.roam_list);
#ifdef CONFIG_BATMAN_ADV_MCAST
	INIT_HLIST_HEAD(&bat_priv->mcast.mla_list);
#endif
	INIT_HLIST_HEAD(&bat_priv->tvlv.container_list);
	INIT_HLIST_HEAD(&bat_priv->tvlv.handler_list);
	INIT_HLIST_HEAD(&bat_priv->softif_vlan_list);
	INIT_HLIST_HEAD(&bat_priv->tp_list);

	bat_priv->gw.generation = 0;

	ret = batadv_v_mesh_init(bat_priv);
	if (ret < 0)
		goto err;

	ret = batadv_originator_init(bat_priv);
	if (ret < 0)
		goto err;

	ret = batadv_tt_init(bat_priv);
	if (ret < 0)
		goto err;

	ret = batadv_bla_init(bat_priv);
	if (ret < 0)
		goto err;

	ret = batadv_dat_init(bat_priv);
	if (ret < 0)
		goto err;

	ret = batadv_nc_mesh_init(bat_priv);
	if (ret < 0)
		goto err;

	batadv_gw_init(bat_priv);
	batadv_mcast_init(bat_priv);

	atomic_set(&bat_priv->gw.reselect, 0);
	atomic_set(&bat_priv->mesh_state, BATADV_MESH_ACTIVE);

	return 0;

err:
	batadv_mesh_free(soft_iface);
	return ret;
}

/**
 * batadv_mesh_free() - Deinitialize soft interface
 * @soft_iface: netdev struct of the soft interface
 */
void batadv_mesh_free(struct net_device *soft_iface)
{
	struct batadv_priv *bat_priv = netdev_priv(soft_iface);

	atomic_set(&bat_priv->mesh_state, BATADV_MESH_DEACTIVATING);

	batadv_purge_outstanding_packets(bat_priv, NULL);

	batadv_gw_node_free(bat_priv);

	batadv_v_mesh_free(bat_priv);
	batadv_nc_mesh_free(bat_priv);
	batadv_dat_free(bat_priv);
	batadv_bla_free(bat_priv);

	batadv_mcast_free(bat_priv);

	/* Free the TT and the originator tables only after having terminated
	 * all the other depending components which may use these structures for
	 * their purposes.
	 */
	batadv_tt_free(bat_priv);

	/* Since the originator table clean up routine is accessing the TT
	 * tables as well, it has to be invoked after the TT tables have been
	 * freed and marked as empty. This ensures that no cleanup RCU callbacks
	 * accessing the TT data are scheduled for later execution.
	 */
	batadv_originator_free(bat_priv);

	batadv_gw_free(bat_priv);

	free_percpu(bat_priv->bat_counters);
	bat_priv->bat_counters = NULL;

	atomic_set(&bat_priv->mesh_state, BATADV_MESH_INACTIVE);
}

/**
 * batadv_is_my_mac() - check if the given mac address belongs to any of the
 *  real interfaces in the current mesh
 * @bat_priv: the bat priv with all the soft interface information
 * @addr: the address to check
 *
 * Return: 'true' if the mac address was found, false otherwise.
 */
bool batadv_is_my_mac(struct batadv_priv *bat_priv, const u8 *addr)
{
	const struct batadv_hard_iface *hard_iface;
	bool is_my_mac = false;

	rcu_read_lock();
	list_for_each_entry_rcu(hard_iface, &batadv_hardif_list, list) {
		if (hard_iface->if_status != BATADV_IF_ACTIVE)
			continue;

		if (hard_iface->soft_iface != bat_priv->soft_iface)
			continue;

		if (batadv_compare_eth(hard_iface->net_dev->dev_addr, addr)) {
			is_my_mac = true;
			break;
		}
	}
	rcu_read_unlock();
	return is_my_mac;
}

#ifdef CONFIG_BATMAN_ADV_DEBUGFS
/**
 * batadv_seq_print_text_primary_if_get() - called from debugfs table printing
 *  function that requires the primary interface
 * @seq: debugfs table seq_file struct
 *
 * Return: primary interface if found or NULL otherwise.
 */
struct batadv_hard_iface *
batadv_seq_print_text_primary_if_get(struct seq_file *seq)
{
	struct net_device *net_dev = (struct net_device *)seq->private;
	struct batadv_priv *bat_priv = netdev_priv(net_dev);
	struct batadv_hard_iface *primary_if;

	primary_if = batadv_primary_if_get_selected(bat_priv);

	if (!primary_if) {
		seq_printf(seq,
			   "BATMAN mesh %s disabled - please specify interfaces to enable it\n",
			   net_dev->name);
		goto out;
	}

	if (primary_if->if_status == BATADV_IF_ACTIVE)
		goto out;

	seq_printf(seq,
		   "BATMAN mesh %s disabled - primary interface not active\n",
		   net_dev->name);
	batadv_hardif_put(primary_if);
	primary_if = NULL;

out:
	return primary_if;
}
#endif

/**
 * batadv_max_header_len() - calculate maximum encapsulation overhead for a
 *  payload packet
 *
 * Return: the maximum encapsulation overhead in bytes.
 */
int batadv_max_header_len(void)
{
	int header_len = 0;

	header_len = max_t(int, header_len,
			   sizeof(struct batadv_unicast_packet));
	header_len = max_t(int, header_len,
			   sizeof(struct batadv_unicast_4addr_packet));
	header_len = max_t(int, header_len,
			   sizeof(struct batadv_bcast_packet));

#ifdef CONFIG_BATMAN_ADV_NC
	header_len = max_t(int, header_len,
			   sizeof(struct batadv_coded_packet));
#endif

	return header_len + ETH_HLEN;
}

/**
 * batadv_skb_set_priority() - sets skb priority according to packet content
 * @skb: the packet to be sent
 * @offset: offset to the packet content
 *
 * This function sets a value between 256 and 263 (802.1d priority), which
 * can be interpreted by the cfg80211 or other drivers.
 */
void batadv_skb_set_priority(struct sk_buff *skb, int offset)
{
	struct iphdr ip_hdr_tmp, *ip_hdr;
	struct ipv6hdr ip6_hdr_tmp, *ip6_hdr;
	struct ethhdr ethhdr_tmp, *ethhdr;
	struct vlan_ethhdr *vhdr, vhdr_tmp;
	u32 prio;

	/* already set, do nothing */
	if (skb->priority >= 256 && skb->priority <= 263)
		return;

	ethhdr = skb_header_pointer(skb, offset, sizeof(*ethhdr), &ethhdr_tmp);
	if (!ethhdr)
		return;

	switch (ethhdr->h_proto) {
	case htons(ETH_P_8021Q):
		vhdr = skb_header_pointer(skb, offset + sizeof(*vhdr),
					  sizeof(*vhdr), &vhdr_tmp);
		if (!vhdr)
			return;
		prio = ntohs(vhdr->h_vlan_TCI) & VLAN_PRIO_MASK;
		prio = prio >> VLAN_PRIO_SHIFT;
		break;
	case htons(ETH_P_IP):
		ip_hdr = skb_header_pointer(skb, offset + sizeof(*ethhdr),
					    sizeof(*ip_hdr), &ip_hdr_tmp);
		if (!ip_hdr)
			return;
		prio = (ipv4_get_dsfield(ip_hdr) & 0xfc) >> 5;
		break;
	case htons(ETH_P_IPV6):
		ip6_hdr = skb_header_pointer(skb, offset + sizeof(*ethhdr),
					     sizeof(*ip6_hdr), &ip6_hdr_tmp);
		if (!ip6_hdr)
			return;
		prio = (ipv6_get_dsfield(ip6_hdr) & 0xfc) >> 5;
		break;
	default:
		return;
	}

	skb->priority = prio + 256;
}

static int batadv_recv_unhandled_packet(struct sk_buff *skb,
					struct batadv_hard_iface *recv_if)
{
	kfree_skb(skb);

	return NET_RX_DROP;
}

/* incoming packets with the batman ethertype received on any active hard
 * interface
 */

/**
 * batadv_batman_skb_recv() - Handle incoming message from an hard interface
 * @skb: the received packet
 * @dev: the net device that the packet was received on
 * @ptype: packet type of incoming packet (ETH_P_BATMAN)
 * @orig_dev: the original receive net device (e.g. bonded device)
 *
 * Return: NET_RX_SUCCESS on success or NET_RX_DROP in case of failure
 */
int batadv_batman_skb_recv(struct sk_buff *skb, struct net_device *dev,
			   struct packet_type *ptype,
			   struct net_device *orig_dev)
{
	struct batadv_priv *bat_priv;
	struct batadv_ogm_packet *batadv_ogm_packet;
	struct batadv_hard_iface *hard_iface;
	u8 idx;

	hard_iface = container_of(ptype, struct batadv_hard_iface,
				  batman_adv_ptype);

	/* Prevent processing a packet received on an interface which is getting
	 * shut down otherwise the packet may trigger de-reference errors
	 * further down in the receive path.
	 */
	if (!kref_get_unless_zero(&hard_iface->refcount))
		goto err_out;

	skb = skb_share_check(skb, GFP_ATOMIC);

	/* skb was released by skb_share_check() */
	if (!skb)
		goto err_put;

	/* packet should hold at least type and version */
	if (unlikely(!pskb_may_pull(skb, 2)))
		goto err_free;

	/* expect a valid ethernet header here. */
	if (unlikely(skb->mac_len != ETH_HLEN || !skb_mac_header(skb)))
		goto err_free;

	if (!hard_iface->soft_iface)
		goto err_free;

	bat_priv = netdev_priv(hard_iface->soft_iface);

	if (atomic_read(&bat_priv->mesh_state) != BATADV_MESH_ACTIVE)
		goto err_free;

	/* discard frames on not active interfaces */
	if (hard_iface->if_status != BATADV_IF_ACTIVE)
		goto err_free;

	batadv_ogm_packet = (struct batadv_ogm_packet *)skb->data;

	if (batadv_ogm_packet->version != BATADV_COMPAT_VERSION) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Drop packet: incompatible batman version (%i)\n",
			   batadv_ogm_packet->version);
		goto err_free;
	}

	/* reset control block to avoid left overs from previous users */
	memset(skb->cb, 0, sizeof(struct batadv_skb_cb));

	idx = batadv_ogm_packet->packet_type;
	(*batadv_rx_handler[idx])(skb, hard_iface);

	batadv_hardif_put(hard_iface);

	/* return NET_RX_SUCCESS in any case as we
	 * most probably dropped the packet for
	 * routing-logical reasons.
	 */
	return NET_RX_SUCCESS;

err_free:
	kfree_skb(skb);
err_put:
	batadv_hardif_put(hard_iface);
err_out:
	return NET_RX_DROP;
}

static void batadv_recv_handler_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(batadv_rx_handler); i++)
		batadv_rx_handler[i] = batadv_recv_unhandled_packet;

	for (i = BATADV_UNICAST_MIN; i <= BATADV_UNICAST_MAX; i++)
		batadv_rx_handler[i] = batadv_recv_unhandled_unicast_packet;

	/* compile time checks for sizes */
	BUILD_BUG_ON(sizeof(struct batadv_bla_claim_dst) != 6);
	BUILD_BUG_ON(sizeof(struct batadv_ogm_packet) != 24);
	BUILD_BUG_ON(sizeof(struct batadv_icmp_header) != 20);
	BUILD_BUG_ON(sizeof(struct batadv_icmp_packet) != 20);
	BUILD_BUG_ON(sizeof(struct batadv_icmp_packet_rr) != 116);
	BUILD_BUG_ON(sizeof(struct batadv_unicast_packet) != 10);
	BUILD_BUG_ON(sizeof(struct batadv_unicast_4addr_packet) != 18);
	BUILD_BUG_ON(sizeof(struct batadv_frag_packet) != 20);
	BUILD_BUG_ON(sizeof(struct batadv_bcast_packet) != 14);
	BUILD_BUG_ON(sizeof(struct batadv_coded_packet) != 46);
	BUILD_BUG_ON(sizeof(struct batadv_unicast_tvlv_packet) != 20);
	BUILD_BUG_ON(sizeof(struct batadv_tvlv_hdr) != 4);
	BUILD_BUG_ON(sizeof(struct batadv_tvlv_gateway_data) != 8);
	BUILD_BUG_ON(sizeof(struct batadv_tvlv_tt_vlan_data) != 8);
	BUILD_BUG_ON(sizeof(struct batadv_tvlv_tt_change) != 12);
	BUILD_BUG_ON(sizeof(struct batadv_tvlv_roam_adv) != 8);

	i = FIELD_SIZEOF(struct sk_buff, cb);
	BUILD_BUG_ON(sizeof(struct batadv_skb_cb) > i);

	/* broadcast packet */
	batadv_rx_handler[BATADV_BCAST] = batadv_recv_bcast_packet;

	/* unicast packets ... */
	/* unicast with 4 addresses packet */
	batadv_rx_handler[BATADV_UNICAST_4ADDR] = batadv_recv_unicast_packet;
	/* unicast packet */
	batadv_rx_handler[BATADV_UNICAST] = batadv_recv_unicast_packet;
	/* unicast tvlv packet */
	batadv_rx_handler[BATADV_UNICAST_TVLV] = batadv_recv_unicast_tvlv;
	/* batman icmp packet */
	batadv_rx_handler[BATADV_ICMP] = batadv_recv_icmp_packet;
	/* Fragmented packets */
	batadv_rx_handler[BATADV_UNICAST_FRAG] = batadv_recv_frag_packet;
}

/**
 * batadv_recv_handler_register() - Register handler for batman-adv packet type
 * @packet_type: batadv_packettype which should be handled
 * @recv_handler: receive handler for the packet type
 *
 * Return: 0 on success or negative error number in case of failure
 */
int
batadv_recv_handler_register(u8 packet_type,
			     int (*recv_handler)(struct sk_buff *,
						 struct batadv_hard_iface *))
{
	int (*curr)(struct sk_buff *skb,
		    struct batadv_hard_iface *recv_if);
	curr = batadv_rx_handler[packet_type];

	if (curr != batadv_recv_unhandled_packet &&
	    curr != batadv_recv_unhandled_unicast_packet)
		return -EBUSY;

	batadv_rx_handler[packet_type] = recv_handler;
	return 0;
}

/**
 * batadv_recv_handler_unregister() - Unregister handler for packet type
 * @packet_type: batadv_packettype which should no longer be handled
 */
void batadv_recv_handler_unregister(u8 packet_type)
{
	batadv_rx_handler[packet_type] = batadv_recv_unhandled_packet;
}

/**
 * batadv_skb_crc32() - calculate CRC32 of the whole packet and skip bytes in
 *  the header
 * @skb: skb pointing to fragmented socket buffers
 * @payload_ptr: Pointer to position inside the head buffer of the skb
 *  marking the start of the data to be CRC'ed
 *
 * payload_ptr must always point to an address in the skb head buffer and not to
 * a fragment.
 *
 * Return: big endian crc32c of the checksummed data
 */
__be32 batadv_skb_crc32(struct sk_buff *skb, u8 *payload_ptr)
{
	u32 crc = 0;
	unsigned int from;
	unsigned int to = skb->len;
	struct skb_seq_state st;
	const u8 *data;
	unsigned int len;
	unsigned int consumed = 0;

	from = (unsigned int)(payload_ptr - skb->data);

	skb_prepare_seq_read(skb, from, to, &st);
	while ((len = skb_seq_read(consumed, &data, &st)) != 0) {
		crc = crc32c(crc, data, len);
		consumed += len;
	}

	return htonl(crc);
}

/**
 * batadv_get_vid() - extract the VLAN identifier from skb if any
 * @skb: the buffer containing the packet
 * @header_len: length of the batman header preceding the ethernet header
 *
 * Return: VID with the BATADV_VLAN_HAS_TAG flag when the packet embedded in the
 * skb is vlan tagged. Otherwise BATADV_NO_FLAGS.
 */
unsigned short batadv_get_vid(struct sk_buff *skb, size_t header_len)
{
	struct ethhdr *ethhdr = (struct ethhdr *)(skb->data + header_len);
	struct vlan_ethhdr *vhdr;
	unsigned short vid;

	if (ethhdr->h_proto != htons(ETH_P_8021Q))
		return BATADV_NO_FLAGS;

	if (!pskb_may_pull(skb, header_len + VLAN_ETH_HLEN))
		return BATADV_NO_FLAGS;

	vhdr = (struct vlan_ethhdr *)(skb->data + header_len);
	vid = ntohs(vhdr->h_vlan_TCI) & VLAN_VID_MASK;
	vid |= BATADV_VLAN_HAS_TAG;

	return vid;
}

/**
 * batadv_vlan_ap_isola_get() - return AP isolation status for the given vlan
 * @bat_priv: the bat priv with all the soft interface information
 * @vid: the VLAN identifier for which the AP isolation attributed as to be
 *  looked up
 *
 * Return: true if AP isolation is on for the VLAN idenfied by vid, false
 * otherwise
 */
bool batadv_vlan_ap_isola_get(struct batadv_priv *bat_priv, unsigned short vid)
{
	bool ap_isolation_enabled = false;
	struct batadv_softif_vlan *vlan;

	/* if the AP isolation is requested on a VLAN, then check for its
	 * setting in the proper VLAN private data structure
	 */
	vlan = batadv_softif_vlan_get(bat_priv, vid);
	if (vlan) {
		ap_isolation_enabled = atomic_read(&vlan->ap_isolation);
		batadv_softif_vlan_put(vlan);
	}

	return ap_isolation_enabled;
}

module_init(batadv_init);
module_exit(batadv_exit);

MODULE_LICENSE("GPL");

MODULE_AUTHOR(BATADV_DRIVER_AUTHOR);
MODULE_DESCRIPTION(BATADV_DRIVER_DESC);
MODULE_SUPPORTED_DEVICE(BATADV_DRIVER_DEVICE);
MODULE_VERSION(BATADV_SOURCE_VERSION);
MODULE_ALIAS_RTNL_LINK("batadv");
MODULE_ALIAS_GENL_FAMILY(BATADV_NL_NAME);
