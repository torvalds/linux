/* Copyright (C) 2007-2013 B.A.T.M.A.N. contributors:
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

#include <linux/crc32c.h>
#include <linux/highmem.h>
#include <linux/if_vlan.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/dsfield.h>
#include "main.h"
#include "sysfs.h"
#include "debugfs.h"
#include "routing.h"
#include "send.h"
#include "originator.h"
#include "soft-interface.h"
#include "icmp_socket.h"
#include "translation-table.h"
#include "hard-interface.h"
#include "gateway_client.h"
#include "bridge_loop_avoidance.h"
#include "distributed-arp-table.h"
#include "unicast.h"
#include "gateway_common.h"
#include "vis.h"
#include "hash.h"
#include "bat_algo.h"
#include "network-coding.h"


/* List manipulations on hardif_list have to be rtnl_lock()'ed,
 * list traversals just rcu-locked
 */
struct list_head batadv_hardif_list;
static int (*batadv_rx_handler[256])(struct sk_buff *,
				     struct batadv_hard_iface *);
char batadv_routing_algo[20] = "BATMAN_IV";
static struct hlist_head batadv_algo_list;

unsigned char batadv_broadcast_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

struct workqueue_struct *batadv_event_workqueue;

static void batadv_recv_handler_init(void);

static int __init batadv_init(void)
{
	INIT_LIST_HEAD(&batadv_hardif_list);
	INIT_HLIST_HEAD(&batadv_algo_list);

	batadv_recv_handler_init();

	batadv_iv_init();
	batadv_nc_init();

	batadv_event_workqueue = create_singlethread_workqueue("bat_events");

	if (!batadv_event_workqueue)
		return -ENOMEM;

	batadv_socket_init();
	batadv_debugfs_init();

	register_netdevice_notifier(&batadv_hard_if_notifier);
	rtnl_link_register(&batadv_link_ops);

	pr_info("B.A.T.M.A.N. advanced %s (compatibility version %i) loaded\n",
		BATADV_SOURCE_VERSION, BATADV_COMPAT_VERSION);

	return 0;
}

static void __exit batadv_exit(void)
{
	batadv_debugfs_destroy();
	rtnl_link_unregister(&batadv_link_ops);
	unregister_netdevice_notifier(&batadv_hard_if_notifier);
	batadv_hardif_remove_interfaces();

	flush_workqueue(batadv_event_workqueue);
	destroy_workqueue(batadv_event_workqueue);
	batadv_event_workqueue = NULL;

	rcu_barrier();
}

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
	spin_lock_init(&bat_priv->gw.list_lock);
	spin_lock_init(&bat_priv->vis.hash_lock);
	spin_lock_init(&bat_priv->vis.list_lock);
	spin_lock_init(&bat_priv->tvlv.container_list_lock);
	spin_lock_init(&bat_priv->tvlv.handler_list_lock);

	INIT_HLIST_HEAD(&bat_priv->forw_bat_list);
	INIT_HLIST_HEAD(&bat_priv->forw_bcast_list);
	INIT_HLIST_HEAD(&bat_priv->gw.list);
	INIT_LIST_HEAD(&bat_priv->tt.changes_list);
	INIT_LIST_HEAD(&bat_priv->tt.req_list);
	INIT_LIST_HEAD(&bat_priv->tt.roam_list);
	INIT_HLIST_HEAD(&bat_priv->tvlv.container_list);
	INIT_HLIST_HEAD(&bat_priv->tvlv.handler_list);

	ret = batadv_originator_init(bat_priv);
	if (ret < 0)
		goto err;

	ret = batadv_tt_init(bat_priv);
	if (ret < 0)
		goto err;

	batadv_tt_local_add(soft_iface, soft_iface->dev_addr,
			    BATADV_NULL_IFINDEX);

	ret = batadv_vis_init(bat_priv);
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

	atomic_set(&bat_priv->gw.reselect, 0);
	atomic_set(&bat_priv->mesh_state, BATADV_MESH_ACTIVE);

	return 0;

err:
	batadv_mesh_free(soft_iface);
	return ret;
}

void batadv_mesh_free(struct net_device *soft_iface)
{
	struct batadv_priv *bat_priv = netdev_priv(soft_iface);

	atomic_set(&bat_priv->mesh_state, BATADV_MESH_DEACTIVATING);

	batadv_purge_outstanding_packets(bat_priv, NULL);

	batadv_vis_quit(bat_priv);

	batadv_gw_node_purge(bat_priv);
	batadv_nc_mesh_free(bat_priv);
	batadv_dat_free(bat_priv);
	batadv_bla_free(bat_priv);

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
 * batadv_is_my_mac - check if the given mac address belongs to any of the real
 * interfaces in the current mesh
 * @bat_priv: the bat priv with all the soft interface information
 * @addr: the address to check
 */
int batadv_is_my_mac(struct batadv_priv *bat_priv, const uint8_t *addr)
{
	const struct batadv_hard_iface *hard_iface;

	rcu_read_lock();
	list_for_each_entry_rcu(hard_iface, &batadv_hardif_list, list) {
		if (hard_iface->if_status != BATADV_IF_ACTIVE)
			continue;

		if (hard_iface->soft_iface != bat_priv->soft_iface)
			continue;

		if (batadv_compare_eth(hard_iface->net_dev->dev_addr, addr)) {
			rcu_read_unlock();
			return 1;
		}
	}
	rcu_read_unlock();
	return 0;
}

/**
 * batadv_seq_print_text_primary_if_get - called from debugfs table printing
 *  function that requires the primary interface
 * @seq: debugfs table seq_file struct
 *
 * Returns primary interface if found or NULL otherwise.
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
	batadv_hardif_free_ref(primary_if);
	primary_if = NULL;

out:
	return primary_if;
}

/**
 * batadv_skb_set_priority - sets skb priority according to packet content
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
	return NET_RX_DROP;
}

/* incoming packets with the batman ethertype received on any active hard
 * interface
 */
int batadv_batman_skb_recv(struct sk_buff *skb, struct net_device *dev,
			   struct packet_type *ptype,
			   struct net_device *orig_dev)
{
	struct batadv_priv *bat_priv;
	struct batadv_ogm_packet *batadv_ogm_packet;
	struct batadv_hard_iface *hard_iface;
	uint8_t idx;
	int ret;

	hard_iface = container_of(ptype, struct batadv_hard_iface,
				  batman_adv_ptype);
	skb = skb_share_check(skb, GFP_ATOMIC);

	/* skb was released by skb_share_check() */
	if (!skb)
		goto err_out;

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

	if (batadv_ogm_packet->header.version != BATADV_COMPAT_VERSION) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Drop packet: incompatible batman version (%i)\n",
			   batadv_ogm_packet->header.version);
		goto err_free;
	}

	/* all receive handlers return whether they received or reused
	 * the supplied skb. if not, we have to free the skb.
	 */
	idx = batadv_ogm_packet->header.packet_type;
	ret = (*batadv_rx_handler[idx])(skb, hard_iface);

	if (ret == NET_RX_DROP)
		kfree_skb(skb);

	/* return NET_RX_SUCCESS in any case as we
	 * most probably dropped the packet for
	 * routing-logical reasons.
	 */
	return NET_RX_SUCCESS;

err_free:
	kfree_skb(skb);
err_out:
	return NET_RX_DROP;
}

static void batadv_recv_handler_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(batadv_rx_handler); i++)
		batadv_rx_handler[i] = batadv_recv_unhandled_packet;

	/* batman icmp packet */
	batadv_rx_handler[BATADV_ICMP] = batadv_recv_icmp_packet;
	/* unicast with 4 addresses packet */
	batadv_rx_handler[BATADV_UNICAST_4ADDR] = batadv_recv_unicast_packet;
	/* unicast packet */
	batadv_rx_handler[BATADV_UNICAST] = batadv_recv_unicast_packet;
	/* fragmented unicast packet */
	batadv_rx_handler[BATADV_UNICAST_FRAG] = batadv_recv_ucast_frag_packet;
	/* broadcast packet */
	batadv_rx_handler[BATADV_BCAST] = batadv_recv_bcast_packet;
	/* vis packet */
	batadv_rx_handler[BATADV_VIS] = batadv_recv_vis_packet;
	/* Roaming advertisement */
	batadv_rx_handler[BATADV_ROAM_ADV] = batadv_recv_roam_adv;
	/* unicast tvlv packet */
	batadv_rx_handler[BATADV_UNICAST_TVLV] = batadv_recv_unicast_tvlv;
}

int
batadv_recv_handler_register(uint8_t packet_type,
			     int (*recv_handler)(struct sk_buff *,
						 struct batadv_hard_iface *))
{
	if (batadv_rx_handler[packet_type] != &batadv_recv_unhandled_packet)
		return -EBUSY;

	batadv_rx_handler[packet_type] = recv_handler;
	return 0;
}

void batadv_recv_handler_unregister(uint8_t packet_type)
{
	batadv_rx_handler[packet_type] = batadv_recv_unhandled_packet;
}

static struct batadv_algo_ops *batadv_algo_get(char *name)
{
	struct batadv_algo_ops *bat_algo_ops = NULL, *bat_algo_ops_tmp;

	hlist_for_each_entry(bat_algo_ops_tmp, &batadv_algo_list, list) {
		if (strcmp(bat_algo_ops_tmp->name, name) != 0)
			continue;

		bat_algo_ops = bat_algo_ops_tmp;
		break;
	}

	return bat_algo_ops;
}

int batadv_algo_register(struct batadv_algo_ops *bat_algo_ops)
{
	struct batadv_algo_ops *bat_algo_ops_tmp;
	int ret;

	bat_algo_ops_tmp = batadv_algo_get(bat_algo_ops->name);
	if (bat_algo_ops_tmp) {
		pr_info("Trying to register already registered routing algorithm: %s\n",
			bat_algo_ops->name);
		ret = -EEXIST;
		goto out;
	}

	/* all algorithms must implement all ops (for now) */
	if (!bat_algo_ops->bat_iface_enable ||
	    !bat_algo_ops->bat_iface_disable ||
	    !bat_algo_ops->bat_iface_update_mac ||
	    !bat_algo_ops->bat_primary_iface_set ||
	    !bat_algo_ops->bat_ogm_schedule ||
	    !bat_algo_ops->bat_ogm_emit) {
		pr_info("Routing algo '%s' does not implement required ops\n",
			bat_algo_ops->name);
		ret = -EINVAL;
		goto out;
	}

	INIT_HLIST_NODE(&bat_algo_ops->list);
	hlist_add_head(&bat_algo_ops->list, &batadv_algo_list);
	ret = 0;

out:
	return ret;
}

int batadv_algo_select(struct batadv_priv *bat_priv, char *name)
{
	struct batadv_algo_ops *bat_algo_ops;
	int ret = -EINVAL;

	bat_algo_ops = batadv_algo_get(name);
	if (!bat_algo_ops)
		goto out;

	bat_priv->bat_algo_ops = bat_algo_ops;
	ret = 0;

out:
	return ret;
}

int batadv_algo_seq_print_text(struct seq_file *seq, void *offset)
{
	struct batadv_algo_ops *bat_algo_ops;

	seq_puts(seq, "Available routing algorithms:\n");

	hlist_for_each_entry(bat_algo_ops, &batadv_algo_list, list) {
		seq_printf(seq, "%s\n", bat_algo_ops->name);
	}

	return 0;
}

/**
 * batadv_skb_crc32 - calculate CRC32 of the whole packet and skip bytes in
 *  the header
 * @skb: skb pointing to fragmented socket buffers
 * @payload_ptr: Pointer to position inside the head buffer of the skb
 *  marking the start of the data to be CRC'ed
 *
 * payload_ptr must always point to an address in the skb head buffer and not to
 * a fragment.
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
 * batadv_tvlv_handler_free_ref - decrement the tvlv handler refcounter and
 *  possibly free it
 * @tvlv_handler: the tvlv handler to free
 */
static void
batadv_tvlv_handler_free_ref(struct batadv_tvlv_handler *tvlv_handler)
{
	if (atomic_dec_and_test(&tvlv_handler->refcount))
		kfree_rcu(tvlv_handler, rcu);
}

/**
 * batadv_tvlv_handler_get - retrieve tvlv handler from the tvlv handler list
 *  based on the provided type and version (both need to match)
 * @bat_priv: the bat priv with all the soft interface information
 * @type: tvlv handler type to look for
 * @version: tvlv handler version to look for
 *
 * Returns tvlv handler if found or NULL otherwise.
 */
static struct batadv_tvlv_handler
*batadv_tvlv_handler_get(struct batadv_priv *bat_priv,
			 uint8_t type, uint8_t version)
{
	struct batadv_tvlv_handler *tvlv_handler_tmp, *tvlv_handler = NULL;

	rcu_read_lock();
	hlist_for_each_entry_rcu(tvlv_handler_tmp,
				 &bat_priv->tvlv.handler_list, list) {
		if (tvlv_handler_tmp->type != type)
			continue;

		if (tvlv_handler_tmp->version != version)
			continue;

		if (!atomic_inc_not_zero(&tvlv_handler_tmp->refcount))
			continue;

		tvlv_handler = tvlv_handler_tmp;
		break;
	}
	rcu_read_unlock();

	return tvlv_handler;
}

/**
 * batadv_tvlv_container_free_ref - decrement the tvlv container refcounter and
 *  possibly free it
 * @tvlv_handler: the tvlv container to free
 */
static void batadv_tvlv_container_free_ref(struct batadv_tvlv_container *tvlv)
{
	if (atomic_dec_and_test(&tvlv->refcount))
		kfree(tvlv);
}

/**
 * batadv_tvlv_container_get - retrieve tvlv container from the tvlv container
 *  list based on the provided type and version (both need to match)
 * @bat_priv: the bat priv with all the soft interface information
 * @type: tvlv container type to look for
 * @version: tvlv container version to look for
 *
 * Has to be called with the appropriate locks being acquired
 * (tvlv.container_list_lock).
 *
 * Returns tvlv container if found or NULL otherwise.
 */
static struct batadv_tvlv_container
*batadv_tvlv_container_get(struct batadv_priv *bat_priv,
			   uint8_t type, uint8_t version)
{
	struct batadv_tvlv_container *tvlv_tmp, *tvlv = NULL;

	hlist_for_each_entry(tvlv_tmp, &bat_priv->tvlv.container_list, list) {
		if (tvlv_tmp->tvlv_hdr.type != type)
			continue;

		if (tvlv_tmp->tvlv_hdr.version != version)
			continue;

		if (!atomic_inc_not_zero(&tvlv_tmp->refcount))
			continue;

		tvlv = tvlv_tmp;
		break;
	}

	return tvlv;
}

/**
 * batadv_tvlv_container_list_size - calculate the size of the tvlv container
 *  list entries
 * @bat_priv: the bat priv with all the soft interface information
 *
 * Has to be called with the appropriate locks being acquired
 * (tvlv.container_list_lock).
 *
 * Returns size of all currently registered tvlv containers in bytes.
 */
static uint16_t batadv_tvlv_container_list_size(struct batadv_priv *bat_priv)
{
	struct batadv_tvlv_container *tvlv;
	uint16_t tvlv_len = 0;

	hlist_for_each_entry(tvlv, &bat_priv->tvlv.container_list, list) {
		tvlv_len += sizeof(struct batadv_tvlv_hdr);
		tvlv_len += ntohs(tvlv->tvlv_hdr.len);
	}

	return tvlv_len;
}

/**
 * batadv_tvlv_container_remove - remove tvlv container from the tvlv container
 *  list
 * @tvlv: the to be removed tvlv container
 *
 * Has to be called with the appropriate locks being acquired
 * (tvlv.container_list_lock).
 */
static void batadv_tvlv_container_remove(struct batadv_tvlv_container *tvlv)
{
	if (!tvlv)
		return;

	hlist_del(&tvlv->list);

	/* first call to decrement the counter, second call to free */
	batadv_tvlv_container_free_ref(tvlv);
	batadv_tvlv_container_free_ref(tvlv);
}

/**
 * batadv_tvlv_container_unregister - unregister tvlv container based on the
 *  provided type and version (both need to match)
 * @bat_priv: the bat priv with all the soft interface information
 * @type: tvlv container type to unregister
 * @version: tvlv container type to unregister
 */
void batadv_tvlv_container_unregister(struct batadv_priv *bat_priv,
				      uint8_t type, uint8_t version)
{
	struct batadv_tvlv_container *tvlv;

	spin_lock_bh(&bat_priv->tvlv.container_list_lock);
	tvlv = batadv_tvlv_container_get(bat_priv, type, version);
	batadv_tvlv_container_remove(tvlv);
	spin_unlock_bh(&bat_priv->tvlv.container_list_lock);
}

/**
 * batadv_tvlv_container_register - register tvlv type, version and content
 *  to be propagated with each (primary interface) OGM
 * @bat_priv: the bat priv with all the soft interface information
 * @type: tvlv container type
 * @version: tvlv container version
 * @tvlv_value: tvlv container content
 * @tvlv_value_len: tvlv container content length
 *
 * If a container of the same type and version was already registered the new
 * content is going to replace the old one.
 */
void batadv_tvlv_container_register(struct batadv_priv *bat_priv,
				    uint8_t type, uint8_t version,
				    void *tvlv_value, uint16_t tvlv_value_len)
{
	struct batadv_tvlv_container *tvlv_old, *tvlv_new;

	if (!tvlv_value)
		tvlv_value_len = 0;

	tvlv_new = kzalloc(sizeof(*tvlv_new) + tvlv_value_len, GFP_ATOMIC);
	if (!tvlv_new)
		return;

	tvlv_new->tvlv_hdr.version = version;
	tvlv_new->tvlv_hdr.type = type;
	tvlv_new->tvlv_hdr.len = htons(tvlv_value_len);

	memcpy(tvlv_new + 1, tvlv_value, ntohs(tvlv_new->tvlv_hdr.len));
	INIT_HLIST_NODE(&tvlv_new->list);
	atomic_set(&tvlv_new->refcount, 1);

	spin_lock_bh(&bat_priv->tvlv.container_list_lock);
	tvlv_old = batadv_tvlv_container_get(bat_priv, type, version);
	batadv_tvlv_container_remove(tvlv_old);
	hlist_add_head(&tvlv_new->list, &bat_priv->tvlv.container_list);
	spin_unlock_bh(&bat_priv->tvlv.container_list_lock);
}

/**
 * batadv_tvlv_realloc_packet_buff - reallocate packet buffer to accomodate
 *  requested packet size
 * @packet_buff: packet buffer
 * @packet_buff_len: packet buffer size
 * @packet_min_len: requested packet minimum size
 * @additional_packet_len: requested additional packet size on top of minimum
 *  size
 *
 * Returns true of the packet buffer could be changed to the requested size,
 * false otherwise.
 */
static bool batadv_tvlv_realloc_packet_buff(unsigned char **packet_buff,
					    int *packet_buff_len,
					    int min_packet_len,
					    int additional_packet_len)
{
	unsigned char *new_buff;

	new_buff = kmalloc(min_packet_len + additional_packet_len, GFP_ATOMIC);

	/* keep old buffer if kmalloc should fail */
	if (new_buff) {
		memcpy(new_buff, *packet_buff, min_packet_len);
		kfree(*packet_buff);
		*packet_buff = new_buff;
		*packet_buff_len = min_packet_len + additional_packet_len;
		return true;
	}

	return false;
}

/**
 * batadv_tvlv_container_ogm_append - append tvlv container content to given
 *  OGM packet buffer
 * @bat_priv: the bat priv with all the soft interface information
 * @packet_buff: ogm packet buffer
 * @packet_buff_len: ogm packet buffer size including ogm header and tvlv
 *  content
 * @packet_min_len: ogm header size to be preserved for the OGM itself
 *
 * The ogm packet might be enlarged or shrunk depending on the current size
 * and the size of the to-be-appended tvlv containers.
 *
 * Returns size of all appended tvlv containers in bytes.
 */
uint16_t batadv_tvlv_container_ogm_append(struct batadv_priv *bat_priv,
					  unsigned char **packet_buff,
					  int *packet_buff_len,
					  int packet_min_len)
{
	struct batadv_tvlv_container *tvlv;
	struct batadv_tvlv_hdr *tvlv_hdr;
	uint16_t tvlv_value_len;
	void *tvlv_value;
	bool ret;

	spin_lock_bh(&bat_priv->tvlv.container_list_lock);
	tvlv_value_len = batadv_tvlv_container_list_size(bat_priv);

	ret = batadv_tvlv_realloc_packet_buff(packet_buff, packet_buff_len,
					      packet_min_len, tvlv_value_len);

	if (!ret)
		goto end;

	if (!tvlv_value_len)
		goto end;

	tvlv_value = (*packet_buff) + packet_min_len;

	hlist_for_each_entry(tvlv, &bat_priv->tvlv.container_list, list) {
		tvlv_hdr = tvlv_value;
		tvlv_hdr->type = tvlv->tvlv_hdr.type;
		tvlv_hdr->version = tvlv->tvlv_hdr.version;
		tvlv_hdr->len = tvlv->tvlv_hdr.len;
		tvlv_value = tvlv_hdr + 1;
		memcpy(tvlv_value, tvlv + 1, ntohs(tvlv->tvlv_hdr.len));
		tvlv_value = (uint8_t *)tvlv_value + ntohs(tvlv->tvlv_hdr.len);
	}

end:
	spin_unlock_bh(&bat_priv->tvlv.container_list_lock);
	return tvlv_value_len;
}

/**
 * batadv_tvlv_call_handler - parse the given tvlv buffer to call the
 *  appropriate handlers
 * @bat_priv: the bat priv with all the soft interface information
 * @tvlv_handler: tvlv callback function handling the tvlv content
 * @ogm_source: flag indicating wether the tvlv is an ogm or a unicast packet
 * @orig_node: orig node emitting the ogm packet
 * @src: source mac address of the unicast packet
 * @dst: destination mac address of the unicast packet
 * @tvlv_value: tvlv content
 * @tvlv_value_len: tvlv content length
 *
 * Returns success if handler was not found or the return value of the handler
 * callback.
 */
static int batadv_tvlv_call_handler(struct batadv_priv *bat_priv,
				    struct batadv_tvlv_handler *tvlv_handler,
				    bool ogm_source,
				    struct batadv_orig_node *orig_node,
				    uint8_t *src, uint8_t *dst,
				    void *tvlv_value, uint16_t tvlv_value_len)
{
	if (!tvlv_handler)
		return NET_RX_SUCCESS;

	if (ogm_source) {
		if (!tvlv_handler->ogm_handler)
			return NET_RX_SUCCESS;

		if (!orig_node)
			return NET_RX_SUCCESS;

		tvlv_handler->ogm_handler(bat_priv, orig_node,
					  BATADV_NO_FLAGS,
					  tvlv_value, tvlv_value_len);
		tvlv_handler->flags |= BATADV_TVLV_HANDLER_OGM_CALLED;
	} else {
		if (!src)
			return NET_RX_SUCCESS;

		if (!dst)
			return NET_RX_SUCCESS;

		if (!tvlv_handler->unicast_handler)
			return NET_RX_SUCCESS;

		return tvlv_handler->unicast_handler(bat_priv, src,
						     dst, tvlv_value,
						     tvlv_value_len);
	}

	return NET_RX_SUCCESS;
}

/**
 * batadv_tvlv_containers_process - parse the given tvlv buffer to call the
 *  appropriate handlers
 * @bat_priv: the bat priv with all the soft interface information
 * @ogm_source: flag indicating wether the tvlv is an ogm or a unicast packet
 * @orig_node: orig node emitting the ogm packet
 * @src: source mac address of the unicast packet
 * @dst: destination mac address of the unicast packet
 * @tvlv_value: tvlv content
 * @tvlv_value_len: tvlv content length
 *
 * Returns success when processing an OGM or the return value of all called
 * handler callbacks.
 */
int batadv_tvlv_containers_process(struct batadv_priv *bat_priv,
				   bool ogm_source,
				   struct batadv_orig_node *orig_node,
				   uint8_t *src, uint8_t *dst,
				   void *tvlv_value, uint16_t tvlv_value_len)
{
	struct batadv_tvlv_handler *tvlv_handler;
	struct batadv_tvlv_hdr *tvlv_hdr;
	uint16_t tvlv_value_cont_len;
	uint8_t cifnotfound = BATADV_TVLV_HANDLER_OGM_CIFNOTFND;
	int ret = NET_RX_SUCCESS;

	while (tvlv_value_len >= sizeof(*tvlv_hdr)) {
		tvlv_hdr = tvlv_value;
		tvlv_value_cont_len = ntohs(tvlv_hdr->len);
		tvlv_value = tvlv_hdr + 1;
		tvlv_value_len -= sizeof(*tvlv_hdr);

		if (tvlv_value_cont_len > tvlv_value_len)
			break;

		tvlv_handler = batadv_tvlv_handler_get(bat_priv,
						       tvlv_hdr->type,
						       tvlv_hdr->version);

		ret |= batadv_tvlv_call_handler(bat_priv, tvlv_handler,
						ogm_source, orig_node,
						src, dst, tvlv_value,
						tvlv_value_cont_len);
		if (tvlv_handler)
			batadv_tvlv_handler_free_ref(tvlv_handler);
		tvlv_value = (uint8_t *)tvlv_value + tvlv_value_cont_len;
		tvlv_value_len -= tvlv_value_cont_len;
	}

	if (!ogm_source)
		return ret;

	rcu_read_lock();
	hlist_for_each_entry_rcu(tvlv_handler,
				 &bat_priv->tvlv.handler_list, list) {
		if ((tvlv_handler->flags & BATADV_TVLV_HANDLER_OGM_CIFNOTFND) &&
		    !(tvlv_handler->flags & BATADV_TVLV_HANDLER_OGM_CALLED))
			tvlv_handler->ogm_handler(bat_priv, orig_node,
						  cifnotfound, NULL, 0);

		tvlv_handler->flags &= ~BATADV_TVLV_HANDLER_OGM_CALLED;
	}
	rcu_read_unlock();

	return NET_RX_SUCCESS;
}

/**
 * batadv_tvlv_ogm_receive - process an incoming ogm and call the appropriate
 *  handlers
 * @bat_priv: the bat priv with all the soft interface information
 * @batadv_ogm_packet: ogm packet containing the tvlv containers
 * @orig_node: orig node emitting the ogm packet
 */
void batadv_tvlv_ogm_receive(struct batadv_priv *bat_priv,
			     struct batadv_ogm_packet *batadv_ogm_packet,
			     struct batadv_orig_node *orig_node)
{
	void *tvlv_value;
	uint16_t tvlv_value_len;

	if (!batadv_ogm_packet)
		return;

	tvlv_value_len = ntohs(batadv_ogm_packet->tvlv_len);
	if (!tvlv_value_len)
		return;

	tvlv_value = batadv_ogm_packet + 1;

	batadv_tvlv_containers_process(bat_priv, true, orig_node, NULL, NULL,
				       tvlv_value, tvlv_value_len);
}

/**
 * batadv_tvlv_handler_register - register tvlv handler based on the provided
 *  type and version (both need to match) for ogm tvlv payload and/or unicast
 *  payload
 * @bat_priv: the bat priv with all the soft interface information
 * @optr: ogm tvlv handler callback function. This function receives the orig
 *  node, flags and the tvlv content as argument to process.
 * @uptr: unicast tvlv handler callback function. This function receives the
 *  source & destination of the unicast packet as well as the tvlv content
 *  to process.
 * @type: tvlv handler type to be registered
 * @version: tvlv handler version to be registered
 * @flags: flags to enable or disable TVLV API behavior
 */
void batadv_tvlv_handler_register(struct batadv_priv *bat_priv,
				  void (*optr)(struct batadv_priv *bat_priv,
					       struct batadv_orig_node *orig,
					       uint8_t flags,
					       void *tvlv_value,
					       uint16_t tvlv_value_len),
				  int (*uptr)(struct batadv_priv *bat_priv,
					      uint8_t *src, uint8_t *dst,
					      void *tvlv_value,
					      uint16_t tvlv_value_len),
				  uint8_t type, uint8_t version, uint8_t flags)
{
	struct batadv_tvlv_handler *tvlv_handler;

	tvlv_handler = batadv_tvlv_handler_get(bat_priv, type, version);
	if (tvlv_handler) {
		batadv_tvlv_handler_free_ref(tvlv_handler);
		return;
	}

	tvlv_handler = kzalloc(sizeof(*tvlv_handler), GFP_ATOMIC);
	if (!tvlv_handler)
		return;

	tvlv_handler->ogm_handler = optr;
	tvlv_handler->unicast_handler = uptr;
	tvlv_handler->type = type;
	tvlv_handler->version = version;
	tvlv_handler->flags = flags;
	atomic_set(&tvlv_handler->refcount, 1);
	INIT_HLIST_NODE(&tvlv_handler->list);

	spin_lock_bh(&bat_priv->tvlv.handler_list_lock);
	hlist_add_head_rcu(&tvlv_handler->list, &bat_priv->tvlv.handler_list);
	spin_unlock_bh(&bat_priv->tvlv.handler_list_lock);
}

/**
 * batadv_tvlv_handler_unregister - unregister tvlv handler based on the
 *  provided type and version (both need to match)
 * @bat_priv: the bat priv with all the soft interface information
 * @type: tvlv handler type to be unregistered
 * @version: tvlv handler version to be unregistered
 */
void batadv_tvlv_handler_unregister(struct batadv_priv *bat_priv,
				    uint8_t type, uint8_t version)
{
	struct batadv_tvlv_handler *tvlv_handler;

	tvlv_handler = batadv_tvlv_handler_get(bat_priv, type, version);
	if (!tvlv_handler)
		return;

	batadv_tvlv_handler_free_ref(tvlv_handler);
	spin_lock_bh(&bat_priv->tvlv.handler_list_lock);
	hlist_del_rcu(&tvlv_handler->list);
	spin_unlock_bh(&bat_priv->tvlv.handler_list_lock);
	batadv_tvlv_handler_free_ref(tvlv_handler);
}

/**
 * batadv_tvlv_unicast_send - send a unicast packet with tvlv payload to the
 *  specified host
 * @bat_priv: the bat priv with all the soft interface information
 * @src: source mac address of the unicast packet
 * @dst: destination mac address of the unicast packet
 * @type: tvlv type
 * @version: tvlv version
 * @tvlv_value: tvlv content
 * @tvlv_value_len: tvlv content length
 */
void batadv_tvlv_unicast_send(struct batadv_priv *bat_priv, uint8_t *src,
			      uint8_t *dst, uint8_t type, uint8_t version,
			      void *tvlv_value, uint16_t tvlv_value_len)
{
	struct batadv_unicast_tvlv_packet *unicast_tvlv_packet;
	struct batadv_tvlv_hdr *tvlv_hdr;
	struct batadv_orig_node *orig_node;
	struct sk_buff *skb = NULL;
	unsigned char *tvlv_buff;
	unsigned int tvlv_len;
	ssize_t hdr_len = sizeof(*unicast_tvlv_packet);
	bool ret = false;

	orig_node = batadv_orig_hash_find(bat_priv, dst);
	if (!orig_node)
		goto out;

	tvlv_len = sizeof(*tvlv_hdr) + tvlv_value_len;

	skb = netdev_alloc_skb_ip_align(NULL, ETH_HLEN + hdr_len + tvlv_len);
	if (!skb)
		goto out;

	skb->priority = TC_PRIO_CONTROL;
	skb_reserve(skb, ETH_HLEN);
	tvlv_buff = skb_put(skb, sizeof(*unicast_tvlv_packet) + tvlv_len);
	unicast_tvlv_packet = (struct batadv_unicast_tvlv_packet *)tvlv_buff;
	unicast_tvlv_packet->header.packet_type = BATADV_UNICAST_TVLV;
	unicast_tvlv_packet->header.version = BATADV_COMPAT_VERSION;
	unicast_tvlv_packet->header.ttl = BATADV_TTL;
	unicast_tvlv_packet->reserved = 0;
	unicast_tvlv_packet->tvlv_len = htons(tvlv_len);
	unicast_tvlv_packet->align = 0;
	memcpy(unicast_tvlv_packet->src, src, ETH_ALEN);
	memcpy(unicast_tvlv_packet->dst, dst, ETH_ALEN);

	tvlv_buff = (unsigned char *)(unicast_tvlv_packet + 1);
	tvlv_hdr = (struct batadv_tvlv_hdr *)tvlv_buff;
	tvlv_hdr->version = version;
	tvlv_hdr->type = type;
	tvlv_hdr->len = htons(tvlv_value_len);
	tvlv_buff += sizeof(*tvlv_hdr);
	memcpy(tvlv_buff, tvlv_value, tvlv_value_len);

	if (batadv_send_skb_to_orig(skb, orig_node, NULL) != NET_XMIT_DROP)
		ret = true;

out:
	if (skb && !ret)
		kfree_skb(skb);
	if (orig_node)
		batadv_orig_node_free_ref(orig_node);
}

static int batadv_param_set_ra(const char *val, const struct kernel_param *kp)
{
	struct batadv_algo_ops *bat_algo_ops;
	char *algo_name = (char *)val;
	size_t name_len = strlen(algo_name);

	if (name_len > 0 && algo_name[name_len - 1] == '\n')
		algo_name[name_len - 1] = '\0';

	bat_algo_ops = batadv_algo_get(algo_name);
	if (!bat_algo_ops) {
		pr_err("Routing algorithm '%s' is not supported\n", algo_name);
		return -EINVAL;
	}

	return param_set_copystring(algo_name, kp);
}

static const struct kernel_param_ops batadv_param_ops_ra = {
	.set = batadv_param_set_ra,
	.get = param_get_string,
};

static struct kparam_string batadv_param_string_ra = {
	.maxlen = sizeof(batadv_routing_algo),
	.string = batadv_routing_algo,
};

module_param_cb(routing_algo, &batadv_param_ops_ra, &batadv_param_string_ra,
		0644);
module_init(batadv_init);
module_exit(batadv_exit);

MODULE_LICENSE("GPL");

MODULE_AUTHOR(BATADV_DRIVER_AUTHOR);
MODULE_DESCRIPTION(BATADV_DRIVER_DESC);
MODULE_SUPPORTED_DEVICE(BATADV_DRIVER_DEVICE);
MODULE_VERSION(BATADV_SOURCE_VERSION);
