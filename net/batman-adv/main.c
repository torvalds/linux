/*
 * Copyright (C) 2007-2012 B.A.T.M.A.N. contributors:
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
#include "bat_sysfs.h"
#include "bat_debugfs.h"
#include "routing.h"
#include "send.h"
#include "originator.h"
#include "soft-interface.h"
#include "icmp_socket.h"
#include "translation-table.h"
#include "hard-interface.h"
#include "gateway_client.h"
#include "bridge_loop_avoidance.h"
#include "vis.h"
#include "hash.h"
#include "bat_algo.h"


/* List manipulations on hardif_list have to be rtnl_lock()'ed,
 * list traversals just rcu-locked */
struct list_head hardif_list;
static int (*recv_packet_handler[256])(struct sk_buff *, struct hard_iface *);
char bat_routing_algo[20] = "BATMAN IV";
static struct hlist_head bat_algo_list;

unsigned char broadcast_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

struct workqueue_struct *bat_event_workqueue;

static void recv_handler_init(void);

static int __init batman_init(void)
{
	INIT_LIST_HEAD(&hardif_list);
	INIT_HLIST_HEAD(&bat_algo_list);

	recv_handler_init();

	bat_iv_init();

	/* the name should not be longer than 10 chars - see
	 * http://lwn.net/Articles/23634/ */
	bat_event_workqueue = create_singlethread_workqueue("bat_events");

	if (!bat_event_workqueue)
		return -ENOMEM;

	bat_socket_init();
	debugfs_init();

	register_netdevice_notifier(&hard_if_notifier);

	pr_info("B.A.T.M.A.N. advanced %s (compatibility version %i) loaded\n",
		SOURCE_VERSION, COMPAT_VERSION);

	return 0;
}

static void __exit batman_exit(void)
{
	debugfs_destroy();
	unregister_netdevice_notifier(&hard_if_notifier);
	hardif_remove_interfaces();

	flush_workqueue(bat_event_workqueue);
	destroy_workqueue(bat_event_workqueue);
	bat_event_workqueue = NULL;

	rcu_barrier();
}

int mesh_init(struct net_device *soft_iface)
{
	struct bat_priv *bat_priv = netdev_priv(soft_iface);

	spin_lock_init(&bat_priv->forw_bat_list_lock);
	spin_lock_init(&bat_priv->forw_bcast_list_lock);
	spin_lock_init(&bat_priv->tt_changes_list_lock);
	spin_lock_init(&bat_priv->tt_req_list_lock);
	spin_lock_init(&bat_priv->tt_roam_list_lock);
	spin_lock_init(&bat_priv->tt_buff_lock);
	spin_lock_init(&bat_priv->gw_list_lock);
	spin_lock_init(&bat_priv->vis_hash_lock);
	spin_lock_init(&bat_priv->vis_list_lock);

	INIT_HLIST_HEAD(&bat_priv->forw_bat_list);
	INIT_HLIST_HEAD(&bat_priv->forw_bcast_list);
	INIT_HLIST_HEAD(&bat_priv->gw_list);
	INIT_LIST_HEAD(&bat_priv->tt_changes_list);
	INIT_LIST_HEAD(&bat_priv->tt_req_list);
	INIT_LIST_HEAD(&bat_priv->tt_roam_list);

	if (originator_init(bat_priv) < 1)
		goto err;

	if (tt_init(bat_priv) < 1)
		goto err;

	tt_local_add(soft_iface, soft_iface->dev_addr, NULL_IFINDEX);

	if (vis_init(bat_priv) < 1)
		goto err;

	if (bla_init(bat_priv) < 1)
		goto err;

	atomic_set(&bat_priv->gw_reselect, 0);
	atomic_set(&bat_priv->mesh_state, MESH_ACTIVE);
	goto end;

err:
	mesh_free(soft_iface);
	return -1;

end:
	return 0;
}

void mesh_free(struct net_device *soft_iface)
{
	struct bat_priv *bat_priv = netdev_priv(soft_iface);

	atomic_set(&bat_priv->mesh_state, MESH_DEACTIVATING);

	purge_outstanding_packets(bat_priv, NULL);

	vis_quit(bat_priv);

	gw_node_purge(bat_priv);
	originator_free(bat_priv);

	tt_free(bat_priv);

	bla_free(bat_priv);

	atomic_set(&bat_priv->mesh_state, MESH_INACTIVE);
}

void inc_module_count(void)
{
	try_module_get(THIS_MODULE);
}

void dec_module_count(void)
{
	module_put(THIS_MODULE);
}

int is_my_mac(const uint8_t *addr)
{
	const struct hard_iface *hard_iface;

	rcu_read_lock();
	list_for_each_entry_rcu(hard_iface, &hardif_list, list) {
		if (hard_iface->if_status != IF_ACTIVE)
			continue;

		if (compare_eth(hard_iface->net_dev->dev_addr, addr)) {
			rcu_read_unlock();
			return 1;
		}
	}
	rcu_read_unlock();
	return 0;
}

static int recv_unhandled_packet(struct sk_buff *skb,
				 struct hard_iface *recv_if)
{
	return NET_RX_DROP;
}

/* incoming packets with the batman ethertype received on any active hard
 * interface
 */
int batman_skb_recv(struct sk_buff *skb, struct net_device *dev,
		    struct packet_type *ptype, struct net_device *orig_dev)
{
	struct bat_priv *bat_priv;
	struct batman_ogm_packet *batman_ogm_packet;
	struct hard_iface *hard_iface;
	uint8_t idx;
	int ret;

	hard_iface = container_of(ptype, struct hard_iface, batman_adv_ptype);
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

	if (atomic_read(&bat_priv->mesh_state) != MESH_ACTIVE)
		goto err_free;

	/* discard frames on not active interfaces */
	if (hard_iface->if_status != IF_ACTIVE)
		goto err_free;

	batman_ogm_packet = (struct batman_ogm_packet *)skb->data;

	if (batman_ogm_packet->header.version != COMPAT_VERSION) {
		bat_dbg(DBG_BATMAN, bat_priv,
			"Drop packet: incompatible batman version (%i)\n",
			batman_ogm_packet->header.version);
		goto err_free;
	}

	/* all receive handlers return whether they received or reused
	 * the supplied skb. if not, we have to free the skb.
	 */
	idx = batman_ogm_packet->header.packet_type;
	ret = (*recv_packet_handler[idx])(skb, hard_iface);

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

static void recv_handler_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(recv_packet_handler); i++)
		recv_packet_handler[i] = recv_unhandled_packet;

	/* batman originator packet */
	recv_packet_handler[BAT_IV_OGM] = recv_bat_ogm_packet;
	/* batman icmp packet */
	recv_packet_handler[BAT_ICMP] = recv_icmp_packet;
	/* unicast packet */
	recv_packet_handler[BAT_UNICAST] = recv_unicast_packet;
	/* fragmented unicast packet */
	recv_packet_handler[BAT_UNICAST_FRAG] = recv_ucast_frag_packet;
	/* broadcast packet */
	recv_packet_handler[BAT_BCAST] = recv_bcast_packet;
	/* vis packet */
	recv_packet_handler[BAT_VIS] = recv_vis_packet;
	/* Translation table query (request or response) */
	recv_packet_handler[BAT_TT_QUERY] = recv_tt_query;
	/* Roaming advertisement */
	recv_packet_handler[BAT_ROAM_ADV] = recv_roam_adv;
}

int recv_handler_register(uint8_t packet_type,
			  int (*recv_handler)(struct sk_buff *,
					      struct hard_iface *))
{
	if (recv_packet_handler[packet_type] != &recv_unhandled_packet)
		return -EBUSY;

	recv_packet_handler[packet_type] = recv_handler;
	return 0;
}

void recv_handler_unregister(uint8_t packet_type)
{
	recv_packet_handler[packet_type] = recv_unhandled_packet;
}

static struct bat_algo_ops *bat_algo_get(char *name)
{
	struct bat_algo_ops *bat_algo_ops = NULL, *bat_algo_ops_tmp;
	struct hlist_node *node;

	hlist_for_each_entry(bat_algo_ops_tmp, node, &bat_algo_list, list) {
		if (strcmp(bat_algo_ops_tmp->name, name) != 0)
			continue;

		bat_algo_ops = bat_algo_ops_tmp;
		break;
	}

	return bat_algo_ops;
}

int bat_algo_register(struct bat_algo_ops *bat_algo_ops)
{
	struct bat_algo_ops *bat_algo_ops_tmp;
	int ret = -1;

	bat_algo_ops_tmp = bat_algo_get(bat_algo_ops->name);
	if (bat_algo_ops_tmp) {
		pr_info("Trying to register already registered routing algorithm: %s\n",
			bat_algo_ops->name);
		goto out;
	}

	/* all algorithms must implement all ops (for now) */
	if (!bat_algo_ops->bat_iface_enable ||
	    !bat_algo_ops->bat_iface_disable ||
	    !bat_algo_ops->bat_primary_iface_set ||
	    !bat_algo_ops->bat_ogm_update_mac ||
	    !bat_algo_ops->bat_ogm_schedule ||
	    !bat_algo_ops->bat_ogm_emit ||
	    !bat_algo_ops->bat_ogm_receive) {
		pr_info("Routing algo '%s' does not implement required ops\n",
			bat_algo_ops->name);
		goto out;
	}

	INIT_HLIST_NODE(&bat_algo_ops->list);
	hlist_add_head(&bat_algo_ops->list, &bat_algo_list);
	ret = 0;

out:
	return ret;
}

int bat_algo_select(struct bat_priv *bat_priv, char *name)
{
	struct bat_algo_ops *bat_algo_ops;
	int ret = -1;

	bat_algo_ops = bat_algo_get(name);
	if (!bat_algo_ops)
		goto out;

	bat_priv->bat_algo_ops = bat_algo_ops;
	ret = 0;

out:
	return ret;
}

int bat_algo_seq_print_text(struct seq_file *seq, void *offset)
{
	struct bat_algo_ops *bat_algo_ops;
	struct hlist_node *node;

	seq_printf(seq, "Available routing algorithms:\n");

	hlist_for_each_entry(bat_algo_ops, node, &bat_algo_list, list) {
		seq_printf(seq, "%s\n", bat_algo_ops->name);
	}

	return 0;
}

static int param_set_ra(const char *val, const struct kernel_param *kp)
{
	struct bat_algo_ops *bat_algo_ops;

	bat_algo_ops = bat_algo_get((char *)val);
	if (!bat_algo_ops) {
		pr_err("Routing algorithm '%s' is not supported\n", val);
		return -EINVAL;
	}

	return param_set_copystring(val, kp);
}

static const struct kernel_param_ops param_ops_ra = {
	.set = param_set_ra,
	.get = param_get_string,
};

static struct kparam_string __param_string_ra = {
	.maxlen = sizeof(bat_routing_algo),
	.string = bat_routing_algo,
};

module_param_cb(routing_algo, &param_ops_ra, &__param_string_ra, 0644);
module_init(batman_init);
module_exit(batman_exit);

MODULE_LICENSE("GPL");

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_SUPPORTED_DEVICE(DRIVER_DEVICE);
MODULE_VERSION(SOURCE_VERSION);
