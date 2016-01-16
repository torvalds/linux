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
#include <linux/types.h>
#include <linux/workqueue.h>

#include "bat_v_elp.h"
#include "bat_v_ogm.h"
#include "originator.h"
#include "packet.h"

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
	.bat_iface_enable = batadv_v_iface_enable,
	.bat_iface_disable = batadv_v_iface_disable,
	.bat_iface_update_mac = batadv_v_iface_update_mac,
	.bat_primary_iface_set = batadv_v_primary_iface_set,
	.bat_hardif_neigh_init = batadv_v_hardif_neigh_init,
	.bat_ogm_emit = batadv_v_ogm_emit,
	.bat_ogm_schedule = batadv_v_ogm_schedule,
	.bat_neigh_cmp = batadv_v_neigh_cmp,
	.bat_neigh_is_similar_or_better = batadv_v_neigh_is_sob,
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
