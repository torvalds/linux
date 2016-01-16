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

#include <linux/cache.h>
#include <linux/init.h>

#include "bat_v_elp.h"

static int batadv_v_iface_enable(struct batadv_hard_iface *hard_iface)
{
	return batadv_v_elp_iface_enable(hard_iface);
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
}

static void batadv_v_ogm_schedule(struct batadv_hard_iface *hard_iface)
{
}

static void batadv_v_ogm_emit(struct batadv_forw_packet *forw_packet)
{
}

static struct batadv_algo_ops batadv_batman_v __read_mostly = {
	.name = "BATMAN_V",
	.bat_iface_enable = batadv_v_iface_enable,
	.bat_iface_disable = batadv_v_iface_disable,
	.bat_iface_update_mac = batadv_v_iface_update_mac,
	.bat_primary_iface_set = batadv_v_primary_iface_set,
	.bat_ogm_emit = batadv_v_ogm_emit,
	.bat_ogm_schedule = batadv_v_ogm_schedule,
};

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
	return batadv_algo_register(&batadv_batman_v);
}
