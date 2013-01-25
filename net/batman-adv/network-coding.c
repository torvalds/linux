/* Copyright (C) 2012-2013 B.A.T.M.A.N. contributors:
 *
 * Martin Hundebøll, Jeppe Ledet-Pedersen
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
#include "network-coding.h"

static void batadv_nc_worker(struct work_struct *work);

/**
 * batadv_nc_start_timer - initialise the nc periodic worker
 * @bat_priv: the bat priv with all the soft interface information
 */
static void batadv_nc_start_timer(struct batadv_priv *bat_priv)
{
	queue_delayed_work(batadv_event_workqueue, &bat_priv->nc.work,
			   msecs_to_jiffies(10));
}

/**
 * batadv_nc_init - initialise coding hash table and start house keeping
 * @bat_priv: the bat priv with all the soft interface information
 */
int batadv_nc_init(struct batadv_priv *bat_priv)
{
	INIT_DELAYED_WORK(&bat_priv->nc.work, batadv_nc_worker);
	batadv_nc_start_timer(bat_priv);

	return 0;
}

/**
 * batadv_nc_init_bat_priv - initialise the nc specific bat_priv variables
 * @bat_priv: the bat priv with all the soft interface information
 */
void batadv_nc_init_bat_priv(struct batadv_priv *bat_priv)
{
	atomic_set(&bat_priv->network_coding, 1);
}

/**
 * batadv_nc_worker - periodic task for house keeping related to network coding
 * @work: kernel work struct
 */
static void batadv_nc_worker(struct work_struct *work)
{
	struct delayed_work *delayed_work;
	struct batadv_priv_nc *priv_nc;
	struct batadv_priv *bat_priv;

	delayed_work = container_of(work, struct delayed_work, work);
	priv_nc = container_of(delayed_work, struct batadv_priv_nc, work);
	bat_priv = container_of(priv_nc, struct batadv_priv, nc);

	/* Schedule a new check */
	batadv_nc_start_timer(bat_priv);
}

/**
 * batadv_nc_free - clean up network coding memory
 * @bat_priv: the bat priv with all the soft interface information
 */
void batadv_nc_free(struct batadv_priv *bat_priv)
{
	cancel_delayed_work_sync(&bat_priv->nc.work);
}
