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
#include "vis.h"
#include "hash.h"


/* List manipulations on hardif_list have to be rtnl_lock()'ed,
 * list traversals just rcu-locked */
struct list_head hardif_list;

unsigned char broadcast_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

struct workqueue_struct *bat_event_workqueue;

static int __init batman_init(void)
{
	INIT_LIST_HEAD(&hardif_list);

	/* the name should not be longer than 10 chars - see
	 * http://lwn.net/Articles/23634/ */
	bat_event_workqueue = create_singlethread_workqueue("bat_events");

	if (!bat_event_workqueue)
		return -ENOMEM;

	bat_socket_init();
	debugfs_init();

	register_netdevice_notifier(&hard_if_notifier);

	pr_info("B.A.T.M.A.N. advanced %s (compatibility version %i) "
		"loaded\n", SOURCE_VERSION, COMPAT_VERSION);

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
	spin_lock_init(&bat_priv->softif_neigh_lock);
	spin_lock_init(&bat_priv->softif_neigh_vid_lock);

	INIT_HLIST_HEAD(&bat_priv->forw_bat_list);
	INIT_HLIST_HEAD(&bat_priv->forw_bcast_list);
	INIT_HLIST_HEAD(&bat_priv->gw_list);
	INIT_HLIST_HEAD(&bat_priv->softif_neigh_vids);
	INIT_LIST_HEAD(&bat_priv->tt_changes_list);
	INIT_LIST_HEAD(&bat_priv->tt_req_list);
	INIT_LIST_HEAD(&bat_priv->tt_roam_list);

	if (originator_init(bat_priv) < 1)
		goto err;

	if (tt_init(bat_priv) < 1)
		goto err;

	tt_local_add(soft_iface, soft_iface->dev_addr);

	if (vis_init(bat_priv) < 1)
		goto err;

	atomic_set(&bat_priv->gw_reselect, 0);
	atomic_set(&bat_priv->mesh_state, MESH_ACTIVE);
	goto end;

err:
	pr_err("Unable to allocate memory for mesh information structures: "
	       "out of mem ?\n");
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

	softif_neigh_purge(bat_priv);

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

module_init(batman_init);
module_exit(batman_exit);

MODULE_LICENSE("GPL");

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_SUPPORTED_DEVICE(DRIVER_DEVICE);
MODULE_VERSION(SOURCE_VERSION);
