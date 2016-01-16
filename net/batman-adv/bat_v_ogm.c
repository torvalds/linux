/* Copyright (C) 2013-2016 B.A.T.M.A.N. contributors:
 *
 * Antonio Quartulli
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

#include "bat_v_ogm.h"
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
#include <linux/slab.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include "hard-interface.h"
#include "packet.h"
#include "routing.h"
#include "send.h"
#include "translation-table.h"

/**
 * batadv_v_ogm_start_timer - restart the OGM sending timer
 * @bat_priv: the bat priv with all the soft interface information
 */
static void batadv_v_ogm_start_timer(struct batadv_priv *bat_priv)
{
	unsigned long msecs;
	/* this function may be invoked in different contexts (ogm rescheduling
	 * or hard_iface activation), but the work timer should not be reset
	 */
	if (delayed_work_pending(&bat_priv->bat_v.ogm_wq))
		return;

	msecs = atomic_read(&bat_priv->orig_interval) - BATADV_JITTER;
	msecs += prandom_u32() % (2 * BATADV_JITTER);
	queue_delayed_work(batadv_event_workqueue, &bat_priv->bat_v.ogm_wq,
			   msecs_to_jiffies(msecs));
}

/**
 * batadv_v_ogm_send_to_if - send a batman ogm using a given interface
 * @skb: the OGM to send
 * @hard_iface: the interface to use to send the OGM
 */
static void batadv_v_ogm_send_to_if(struct sk_buff *skb,
				    struct batadv_hard_iface *hard_iface)
{
	struct batadv_priv *bat_priv = netdev_priv(hard_iface->soft_iface);

	if (hard_iface->if_status != BATADV_IF_ACTIVE)
		return;

	batadv_inc_counter(bat_priv, BATADV_CNT_MGMT_TX);
	batadv_add_counter(bat_priv, BATADV_CNT_MGMT_TX_BYTES,
			   skb->len + ETH_HLEN);

	batadv_send_skb_packet(skb, hard_iface, batadv_broadcast_addr);
}

/**
 * batadv_v_ogm_send - periodic worker broadcasting the own OGM
 * @work: work queue item
 */
static void batadv_v_ogm_send(struct work_struct *work)
{
	struct batadv_hard_iface *hard_iface;
	struct batadv_priv_bat_v *bat_v;
	struct batadv_priv *bat_priv;
	struct batadv_ogm2_packet *ogm_packet;
	struct sk_buff *skb, *skb_tmp;
	unsigned char *ogm_buff, *pkt_buff;
	int ogm_buff_len;
	u16 tvlv_len = 0;

	bat_v = container_of(work, struct batadv_priv_bat_v, ogm_wq.work);
	bat_priv = container_of(bat_v, struct batadv_priv, bat_v);

	if (atomic_read(&bat_priv->mesh_state) == BATADV_MESH_DEACTIVATING)
		goto out;

	ogm_buff = bat_priv->bat_v.ogm_buff;
	ogm_buff_len = bat_priv->bat_v.ogm_buff_len;
	/* tt changes have to be committed before the tvlv data is
	 * appended as it may alter the tt tvlv container
	 */
	batadv_tt_local_commit_changes(bat_priv);
	tvlv_len = batadv_tvlv_container_ogm_append(bat_priv, &ogm_buff,
						    &ogm_buff_len,
						    BATADV_OGM2_HLEN);

	bat_priv->bat_v.ogm_buff = ogm_buff;
	bat_priv->bat_v.ogm_buff_len = ogm_buff_len;

	skb = netdev_alloc_skb_ip_align(NULL, ETH_HLEN + ogm_buff_len);
	if (!skb)
		goto reschedule;

	skb_reserve(skb, ETH_HLEN);
	pkt_buff = skb_put(skb, ogm_buff_len);
	memcpy(pkt_buff, ogm_buff, ogm_buff_len);

	ogm_packet = (struct batadv_ogm2_packet *)skb->data;
	ogm_packet->seqno = htonl(atomic_read(&bat_priv->bat_v.ogm_seqno));
	atomic_inc(&bat_priv->bat_v.ogm_seqno);
	ogm_packet->tvlv_len = htons(tvlv_len);

	/* broadcast on every interface */
	rcu_read_lock();
	list_for_each_entry_rcu(hard_iface, &batadv_hardif_list, list) {
		if (hard_iface->soft_iface != bat_priv->soft_iface)
			continue;

		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Sending own OGM2 packet (originator %pM, seqno %u, throughput %u, TTL %d) on interface %s [%pM]\n",
			   ogm_packet->orig, ntohl(ogm_packet->seqno),
			   ntohl(ogm_packet->throughput), ogm_packet->ttl,
			   hard_iface->net_dev->name,
			   hard_iface->net_dev->dev_addr);

		/* this skb gets consumed by batadv_v_ogm_send_to_if() */
		skb_tmp = skb_clone(skb, GFP_ATOMIC);
		if (!skb_tmp)
			break;

		batadv_v_ogm_send_to_if(skb_tmp, hard_iface);
	}
	rcu_read_unlock();

	consume_skb(skb);

reschedule:
	batadv_v_ogm_start_timer(bat_priv);
out:
	return;
}

/**
 * batadv_v_ogm_iface_enable - prepare an interface for B.A.T.M.A.N. V
 * @hard_iface: the interface to prepare
 *
 * Takes care of scheduling own OGM sending routine for this interface.
 *
 * Return: 0 on success or a negative error code otherwise
 */
int batadv_v_ogm_iface_enable(struct batadv_hard_iface *hard_iface)
{
	struct batadv_priv *bat_priv = netdev_priv(hard_iface->soft_iface);

	batadv_v_ogm_start_timer(bat_priv);

	return 0;
}

/**
 * batadv_v_ogm_primary_iface_set - set a new primary interface
 * @primary_iface: the new primary interface
 */
void batadv_v_ogm_primary_iface_set(struct batadv_hard_iface *primary_iface)
{
	struct batadv_priv *bat_priv = netdev_priv(primary_iface->soft_iface);
	struct batadv_ogm2_packet *ogm_packet;

	if (!bat_priv->bat_v.ogm_buff)
		return;

	ogm_packet = (struct batadv_ogm2_packet *)bat_priv->bat_v.ogm_buff;
	ether_addr_copy(ogm_packet->orig, primary_iface->net_dev->dev_addr);
}

/**
 * batadv_v_ogm_packet_recv - OGM2 receiving handler
 * @skb: the received OGM
 * @if_incoming: the interface where this OGM has been received
 *
 * Return: NET_RX_SUCCESS and consume the skb on success or returns NET_RX_DROP
 * (without freeing the skb) on failure
 */
int batadv_v_ogm_packet_recv(struct sk_buff *skb,
			     struct batadv_hard_iface *if_incoming)
{
	struct batadv_priv *bat_priv = netdev_priv(if_incoming->soft_iface);
	struct batadv_ogm2_packet *ogm_packet;
	struct ethhdr *ethhdr = eth_hdr(skb);

	/* did we receive a OGM2 packet on an interface that does not have
	 * B.A.T.M.A.N. V enabled ?
	 */
	if (strcmp(bat_priv->bat_algo_ops->name, "BATMAN_V") != 0)
		return NET_RX_DROP;

	if (!batadv_check_management_packet(skb, if_incoming, BATADV_OGM2_HLEN))
		return NET_RX_DROP;

	if (batadv_is_my_mac(bat_priv, ethhdr->h_source))
		return NET_RX_DROP;

	ogm_packet = (struct batadv_ogm2_packet *)skb->data;

	if (batadv_is_my_mac(bat_priv, ogm_packet->orig))
		return NET_RX_DROP;

	batadv_inc_counter(bat_priv, BATADV_CNT_MGMT_RX);
	batadv_add_counter(bat_priv, BATADV_CNT_MGMT_RX_BYTES,
			   skb->len + ETH_HLEN);

	consume_skb(skb);
	return NET_RX_SUCCESS;
}

/**
 * batadv_v_ogm_init - initialise the OGM2 engine
 * @bat_priv: the bat priv with all the soft interface information
 *
 * Return: 0 on success or a negative error code in case of failure
 */
int batadv_v_ogm_init(struct batadv_priv *bat_priv)
{
	struct batadv_ogm2_packet *ogm_packet;
	unsigned char *ogm_buff;
	u32 random_seqno;

	bat_priv->bat_v.ogm_buff_len = BATADV_OGM2_HLEN;
	ogm_buff = kzalloc(bat_priv->bat_v.ogm_buff_len, GFP_ATOMIC);
	if (!ogm_buff)
		return -ENOMEM;

	bat_priv->bat_v.ogm_buff = ogm_buff;
	ogm_packet = (struct batadv_ogm2_packet *)ogm_buff;
	ogm_packet->packet_type = BATADV_OGM2;
	ogm_packet->version = BATADV_COMPAT_VERSION;
	ogm_packet->ttl = BATADV_TTL;
	ogm_packet->flags = BATADV_NO_FLAGS;
	ogm_packet->throughput = htonl(BATADV_THROUGHPUT_MAX_VALUE);

	/* randomize initial seqno to avoid collision */
	get_random_bytes(&random_seqno, sizeof(random_seqno));
	atomic_set(&bat_priv->bat_v.ogm_seqno, random_seqno);
	INIT_DELAYED_WORK(&bat_priv->bat_v.ogm_wq, batadv_v_ogm_send);

	return 0;
}

/**
 * batadv_v_ogm_free - free OGM private resources
 * @bat_priv: the bat priv with all the soft interface information
 */
void batadv_v_ogm_free(struct batadv_priv *bat_priv)
{
	cancel_delayed_work_sync(&bat_priv->bat_v.ogm_wq);

	kfree(bat_priv->bat_v.ogm_buff);
	bat_priv->bat_v.ogm_buff = NULL;
	bat_priv->bat_v.ogm_buff_len = 0;
}
