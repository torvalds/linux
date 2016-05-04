/* Copyright (C) 2013-2016 B.A.T.M.A.N. contributors:
 *
 * Linus Lüssing, Marek Lindner
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

#include "bat_algo.h"
#include "main.h"

#include <linux/atomic.h>
#include <linux/bug.h>
#include <linux/cache.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/netdevice.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>
#include <linux/seq_file.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include "bat_v_elp.h"
#include "bat_v_ogm.h"
#include "hard-interface.h"
#include "hash.h"
#include "originator.h"
#include "packet.h"

static void batadv_v_iface_activate(struct batadv_hard_iface *hard_iface)
{
	/* B.A.T.M.A.N. V does not use any queuing mechanism, therefore it can
	 * set the interface as ACTIVE right away, without any risk of race
	 * condition
	 */
	if (hard_iface->if_status == BATADV_IF_TO_BE_ACTIVATED)
		hard_iface->if_status = BATADV_IF_ACTIVE;
}

static int batadv_v_iface_enable(struct batadv_hard_iface *hard_iface)
{
	int ret;

	ret = batadv_v_elp_iface_enable(hard_iface);
	if (ret < 0)
		return ret;

	ret = batadv_v_ogm_iface_enable(hard_iface);
	if (ret < 0)
		batadv_v_elp_iface_disable(hard_iface);

	/* enable link throughput auto-detection by setting the throughput
	 * override to zero
	 */
	atomic_set(&hard_iface->bat_v.throughput_override, 0);

	return ret;
}

static void batadv_v_iface_disable(struct batadv_hard_iface *hard_iface)
{
	batadv_v_elp_iface_disable(hard_iface);
}

static void batadv_v_iface_update_mac(struct batadv_hard_iface *hard_iface)
{
}

static void batadv_v_primary_iface_set(struct batadv_hard_iface *hard_iface)
{
	batadv_v_elp_primary_iface_set(hard_iface);
	batadv_v_ogm_primary_iface_set(hard_iface);
}

static void
batadv_v_hardif_neigh_init(struct batadv_hardif_neigh_node *hardif_neigh)
{
	ewma_throughput_init(&hardif_neigh->bat_v.throughput);
	INIT_WORK(&hardif_neigh->bat_v.metric_work,
		  batadv_v_elp_throughput_metric_update);
}

static void batadv_v_ogm_schedule(struct batadv_hard_iface *hard_iface)
{
}

static void batadv_v_ogm_emit(struct batadv_forw_packet *forw_packet)
{
}

/**
 * batadv_v_orig_print_neigh - print neighbors for the originator table
 * @orig_node: the orig_node for which the neighbors are printed
 * @if_outgoing: outgoing interface for these entries
 * @seq: debugfs table seq_file struct
 *
 * Must be called while holding an rcu lock.
 */
static void
batadv_v_orig_print_neigh(struct batadv_orig_node *orig_node,
			  struct batadv_hard_iface *if_outgoing,
			  struct seq_file *seq)
{
	struct batadv_neigh_node *neigh_node;
	struct batadv_neigh_ifinfo *n_ifinfo;

	hlist_for_each_entry_rcu(neigh_node, &orig_node->neigh_list, list) {
		n_ifinfo = batadv_neigh_ifinfo_get(neigh_node, if_outgoing);
		if (!n_ifinfo)
			continue;

		seq_printf(seq, " %pM (%9u.%1u)",
			   neigh_node->addr,
			   n_ifinfo->bat_v.throughput / 10,
			   n_ifinfo->bat_v.throughput % 10);

		batadv_neigh_ifinfo_put(n_ifinfo);
	}
}

/**
 * batadv_v_hardif_neigh_print - print a single ELP neighbour node
 * @seq: neighbour table seq_file struct
 * @hardif_neigh: hardif neighbour information
 */
static void
batadv_v_hardif_neigh_print(struct seq_file *seq,
			    struct batadv_hardif_neigh_node *hardif_neigh)
{
	int last_secs, last_msecs;
	u32 throughput;

	last_secs = jiffies_to_msecs(jiffies - hardif_neigh->last_seen) / 1000;
	last_msecs = jiffies_to_msecs(jiffies - hardif_neigh->last_seen) % 1000;
	throughput = ewma_throughput_read(&hardif_neigh->bat_v.throughput);

	seq_printf(seq, "%pM %4i.%03is (%9u.%1u) [%10s]\n",
		   hardif_neigh->addr, last_secs, last_msecs, throughput / 10,
		   throughput % 10, hardif_neigh->if_incoming->net_dev->name);
}

/**
 * batadv_v_neigh_print - print the single hop neighbour list
 * @bat_priv: the bat priv with all the soft interface information
 * @seq: neighbour table seq_file struct
 */
static void batadv_v_neigh_print(struct batadv_priv *bat_priv,
				 struct seq_file *seq)
{
	struct net_device *net_dev = (struct net_device *)seq->private;
	struct batadv_hardif_neigh_node *hardif_neigh;
	struct batadv_hard_iface *hard_iface;
	int batman_count = 0;

	seq_puts(seq,
		 "  Neighbor        last-seen ( throughput) [        IF]\n");

	rcu_read_lock();
	list_for_each_entry_rcu(hard_iface, &batadv_hardif_list, list) {
		if (hard_iface->soft_iface != net_dev)
			continue;

		hlist_for_each_entry_rcu(hardif_neigh,
					 &hard_iface->neigh_list, list) {
			batadv_v_hardif_neigh_print(seq, hardif_neigh);
			batman_count++;
		}
	}
	rcu_read_unlock();

	if (batman_count == 0)
		seq_puts(seq, "No batman nodes in range ...\n");
}

/**
 * batadv_v_orig_print - print the originator table
 * @bat_priv: the bat priv with all the soft interface information
 * @seq: debugfs table seq_file struct
 * @if_outgoing: the outgoing interface for which this should be printed
 */
static void batadv_v_orig_print(struct batadv_priv *bat_priv,
				struct seq_file *seq,
				struct batadv_hard_iface *if_outgoing)
{
	struct batadv_neigh_node *neigh_node;
	struct batadv_hashtable *hash = bat_priv->orig_hash;
	int last_seen_msecs, last_seen_secs;
	struct batadv_orig_node *orig_node;
	struct batadv_neigh_ifinfo *n_ifinfo;
	unsigned long last_seen_jiffies;
	struct hlist_head *head;
	int batman_count = 0;
	u32 i;

	seq_puts(seq,
		 "  Originator      last-seen ( throughput)           Nexthop [outgoingIF]:   Potential nexthops ...\n");

	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		rcu_read_lock();
		hlist_for_each_entry_rcu(orig_node, head, hash_entry) {
			neigh_node = batadv_orig_router_get(orig_node,
							    if_outgoing);
			if (!neigh_node)
				continue;

			n_ifinfo = batadv_neigh_ifinfo_get(neigh_node,
							   if_outgoing);
			if (!n_ifinfo)
				goto next;

			last_seen_jiffies = jiffies - orig_node->last_seen;
			last_seen_msecs = jiffies_to_msecs(last_seen_jiffies);
			last_seen_secs = last_seen_msecs / 1000;
			last_seen_msecs = last_seen_msecs % 1000;

			seq_printf(seq, "%pM %4i.%03is (%9u.%1u) %pM [%10s]:",
				   orig_node->orig, last_seen_secs,
				   last_seen_msecs,
				   n_ifinfo->bat_v.throughput / 10,
				   n_ifinfo->bat_v.throughput % 10,
				   neigh_node->addr,
				   neigh_node->if_incoming->net_dev->name);

			batadv_v_orig_print_neigh(orig_node, if_outgoing, seq);
			seq_puts(seq, "\n");
			batman_count++;

next:
			batadv_neigh_node_put(neigh_node);
			if (n_ifinfo)
				batadv_neigh_ifinfo_put(n_ifinfo);
		}
		rcu_read_unlock();
	}

	if (batman_count == 0)
		seq_puts(seq, "No batman nodes in range ...\n");
}

static int batadv_v_neigh_cmp(struct batadv_neigh_node *neigh1,
			      struct batadv_hard_iface *if_outgoing1,
			      struct batadv_neigh_node *neigh2,
			      struct batadv_hard_iface *if_outgoing2)
{
	struct batadv_neigh_ifinfo *ifinfo1, *ifinfo2;

	ifinfo1 = batadv_neigh_ifinfo_get(neigh1, if_outgoing1);
	ifinfo2 = batadv_neigh_ifinfo_get(neigh2, if_outgoing2);

	if (WARN_ON(!ifinfo1 || !ifinfo2))
		return 0;

	return ifinfo1->bat_v.throughput - ifinfo2->bat_v.throughput;
}

static bool batadv_v_neigh_is_sob(struct batadv_neigh_node *neigh1,
				  struct batadv_hard_iface *if_outgoing1,
				  struct batadv_neigh_node *neigh2,
				  struct batadv_hard_iface *if_outgoing2)
{
	struct batadv_neigh_ifinfo *ifinfo1, *ifinfo2;
	u32 threshold;

	ifinfo1 = batadv_neigh_ifinfo_get(neigh1, if_outgoing1);
	ifinfo2 = batadv_neigh_ifinfo_get(neigh2, if_outgoing2);

	threshold = ifinfo1->bat_v.throughput / 4;
	threshold = ifinfo1->bat_v.throughput - threshold;

	return ifinfo2->bat_v.throughput > threshold;
}

static struct batadv_algo_ops batadv_batman_v __read_mostly = {
	.name = "BATMAN_V",
	.bat_iface_activate = batadv_v_iface_activate,
	.bat_iface_enable = batadv_v_iface_enable,
	.bat_iface_disable = batadv_v_iface_disable,
	.bat_iface_update_mac = batadv_v_iface_update_mac,
	.bat_primary_iface_set = batadv_v_primary_iface_set,
	.bat_hardif_neigh_init = batadv_v_hardif_neigh_init,
	.bat_ogm_emit = batadv_v_ogm_emit,
	.bat_ogm_schedule = batadv_v_ogm_schedule,
	.bat_orig_print = batadv_v_orig_print,
	.bat_neigh_cmp = batadv_v_neigh_cmp,
	.bat_neigh_is_similar_or_better = batadv_v_neigh_is_sob,
	.bat_neigh_print = batadv_v_neigh_print,
};

/**
 * batadv_v_mesh_init - initialize the B.A.T.M.A.N. V private resources for a
 *  mesh
 * @bat_priv: the object representing the mesh interface to initialise
 *
 * Return: 0 on success or a negative error code otherwise
 */
int batadv_v_mesh_init(struct batadv_priv *bat_priv)
{
	return batadv_v_ogm_init(bat_priv);
}

/**
 * batadv_v_mesh_free - free the B.A.T.M.A.N. V private resources for a mesh
 * @bat_priv: the object representing the mesh interface to free
 */
void batadv_v_mesh_free(struct batadv_priv *bat_priv)
{
	batadv_v_ogm_free(bat_priv);
}

/**
 * batadv_v_init - B.A.T.M.A.N. V initialization function
 *
 * Description: Takes care of initializing all the subcomponents.
 * It is invoked upon module load only.
 *
 * Return: 0 on success or a negative error code otherwise
 */
int __init batadv_v_init(void)
{
	int ret;

	/* B.A.T.M.A.N. V echo location protocol packet  */
	ret = batadv_recv_handler_register(BATADV_ELP,
					   batadv_v_elp_packet_recv);
	if (ret < 0)
		return ret;

	ret = batadv_recv_handler_register(BATADV_OGM2,
					   batadv_v_ogm_packet_recv);
	if (ret < 0)
		goto elp_unregister;

	ret = batadv_algo_register(&batadv_batman_v);
	if (ret < 0)
		goto ogm_unregister;

	return ret;

ogm_unregister:
	batadv_recv_handler_unregister(BATADV_OGM2);

elp_unregister:
	batadv_recv_handler_unregister(BATADV_ELP);

	return ret;
}
