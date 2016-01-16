/* Copyright (C) 2011-2016 B.A.T.M.A.N. contributors:
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

#include "bat_v_elp.h"
#include "main.h"

#include <linux/atomic.h>
#include <linux/byteorder/generic.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/fs.h>
#include <linux/if_ether.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/random.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>
#include <linux/skbuff.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include "bat_algo.h"
#include "hard-interface.h"
#include "packet.h"
#include "send.h"

/**
 * batadv_v_elp_start_timer - restart timer for ELP periodic work
 * @hard_iface: the interface for which the timer has to be reset
 */
static void batadv_v_elp_start_timer(struct batadv_hard_iface *hard_iface)
{
	unsigned int msecs;

	msecs = atomic_read(&hard_iface->bat_v.elp_interval) - BATADV_JITTER;
	msecs += prandom_u32() % (2 * BATADV_JITTER);

	queue_delayed_work(batadv_event_workqueue, &hard_iface->bat_v.elp_wq,
			   msecs_to_jiffies(msecs));
}

/**
 * batadv_v_elp_periodic_work - ELP periodic task per interface
 * @work: work queue item
 *
 * Emits broadcast ELP message in regular intervals.
 */
static void batadv_v_elp_periodic_work(struct work_struct *work)
{
	struct batadv_hard_iface *hard_iface;
	struct batadv_hard_iface_bat_v *bat_v;
	struct batadv_elp_packet *elp_packet;
	struct batadv_priv *bat_priv;
	struct sk_buff *skb;
	u32 elp_interval;

	bat_v = container_of(work, struct batadv_hard_iface_bat_v, elp_wq.work);
	hard_iface = container_of(bat_v, struct batadv_hard_iface, bat_v);
	bat_priv = netdev_priv(hard_iface->soft_iface);

	if (atomic_read(&bat_priv->mesh_state) == BATADV_MESH_DEACTIVATING)
		goto out;

	/* we are in the process of shutting this interface down */
	if ((hard_iface->if_status == BATADV_IF_NOT_IN_USE) ||
	    (hard_iface->if_status == BATADV_IF_TO_BE_REMOVED))
		goto out;

	/* the interface was enabled but may not be ready yet */
	if (hard_iface->if_status != BATADV_IF_ACTIVE)
		goto restart_timer;

	skb = skb_copy(hard_iface->bat_v.elp_skb, GFP_ATOMIC);
	if (!skb)
		goto restart_timer;

	elp_packet = (struct batadv_elp_packet *)skb->data;
	elp_packet->seqno = htonl(atomic_read(&hard_iface->bat_v.elp_seqno));
	elp_interval = atomic_read(&hard_iface->bat_v.elp_interval);
	elp_packet->elp_interval = htonl(elp_interval);

	batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
		   "Sending broadcast ELP packet on interface %s, seqno %u\n",
		   hard_iface->net_dev->name,
		   atomic_read(&hard_iface->bat_v.elp_seqno));

	batadv_send_skb_packet(skb, hard_iface, batadv_broadcast_addr);

	atomic_inc(&hard_iface->bat_v.elp_seqno);

restart_timer:
	batadv_v_elp_start_timer(hard_iface);
out:
	return;
}

/**
 * batadv_v_elp_iface_enable - setup the ELP interface private resources
 * @hard_iface: interface for which the data has to be prepared
 *
 * Return: 0 on success or a -ENOMEM in case of failure.
 */
int batadv_v_elp_iface_enable(struct batadv_hard_iface *hard_iface)
{
	struct batadv_elp_packet *elp_packet;
	unsigned char *elp_buff;
	u32 random_seqno;
	size_t size;
	int res = -ENOMEM;

	size = ETH_HLEN + NET_IP_ALIGN + BATADV_ELP_HLEN;
	hard_iface->bat_v.elp_skb = dev_alloc_skb(size);
	if (!hard_iface->bat_v.elp_skb)
		goto out;

	skb_reserve(hard_iface->bat_v.elp_skb, ETH_HLEN + NET_IP_ALIGN);
	elp_buff = skb_push(hard_iface->bat_v.elp_skb, BATADV_ELP_HLEN);
	elp_packet = (struct batadv_elp_packet *)elp_buff;
	memset(elp_packet, 0, BATADV_ELP_HLEN);

	elp_packet->packet_type = BATADV_ELP;
	elp_packet->version = BATADV_COMPAT_VERSION;

	/* randomize initial seqno to avoid collision */
	get_random_bytes(&random_seqno, sizeof(random_seqno));
	atomic_set(&hard_iface->bat_v.elp_seqno, random_seqno);
	atomic_set(&hard_iface->bat_v.elp_interval, 500);

	INIT_DELAYED_WORK(&hard_iface->bat_v.elp_wq,
			  batadv_v_elp_periodic_work);
	batadv_v_elp_start_timer(hard_iface);
	res = 0;

out:
	return res;
}

/**
 * batadv_v_elp_iface_disable - release ELP interface private resources
 * @hard_iface: interface for which the resources have to be released
 */
void batadv_v_elp_iface_disable(struct batadv_hard_iface *hard_iface)
{
	cancel_delayed_work_sync(&hard_iface->bat_v.elp_wq);

	dev_kfree_skb(hard_iface->bat_v.elp_skb);
	hard_iface->bat_v.elp_skb = NULL;
}

/**
 * batadv_v_elp_primary_iface_set - change internal data to reflect the new
 *  primary interface
 * @primary_iface: the new primary interface
 */
void batadv_v_elp_primary_iface_set(struct batadv_hard_iface *primary_iface)
{
	struct batadv_hard_iface *hard_iface;
	struct batadv_elp_packet *elp_packet;
	struct sk_buff *skb;

	/* update orig field of every elp iface belonging to this mesh */
	rcu_read_lock();
	list_for_each_entry_rcu(hard_iface, &batadv_hardif_list, list) {
		if (primary_iface->soft_iface != hard_iface->soft_iface)
			continue;

		if (!hard_iface->bat_v.elp_skb)
			continue;

		skb = hard_iface->bat_v.elp_skb;
		elp_packet = (struct batadv_elp_packet *)skb->data;
		ether_addr_copy(elp_packet->orig,
				primary_iface->net_dev->dev_addr);
	}
	rcu_read_unlock();
}
