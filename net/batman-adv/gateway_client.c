/*
 * Copyright (C) 2009-2011 B.A.T.M.A.N. contributors:
 *
 * Marek Lindner
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
#include "gateway_client.h"
#include "gateway_common.h"
#include "hard-interface.h"
#include "originator.h"
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/udp.h>
#include <linux/if_vlan.h>

static void gw_node_free_ref(struct gw_node *gw_node)
{
	if (atomic_dec_and_test(&gw_node->refcount))
		kfree_rcu(gw_node, rcu);
}

static struct gw_node *gw_get_selected_gw_node(struct bat_priv *bat_priv)
{
	struct gw_node *gw_node;

	rcu_read_lock();
	gw_node = rcu_dereference(bat_priv->curr_gw);
	if (!gw_node)
		goto out;

	if (!atomic_inc_not_zero(&gw_node->refcount))
		gw_node = NULL;

out:
	rcu_read_unlock();
	return gw_node;
}

struct orig_node *gw_get_selected_orig(struct bat_priv *bat_priv)
{
	struct gw_node *gw_node;
	struct orig_node *orig_node = NULL;

	gw_node = gw_get_selected_gw_node(bat_priv);
	if (!gw_node)
		goto out;

	rcu_read_lock();
	orig_node = gw_node->orig_node;
	if (!orig_node)
		goto unlock;

	if (!atomic_inc_not_zero(&orig_node->refcount))
		orig_node = NULL;

unlock:
	rcu_read_unlock();
out:
	if (gw_node)
		gw_node_free_ref(gw_node);
	return orig_node;
}

static void gw_select(struct bat_priv *bat_priv, struct gw_node *new_gw_node)
{
	struct gw_node *curr_gw_node;

	spin_lock_bh(&bat_priv->gw_list_lock);

	if (new_gw_node && !atomic_inc_not_zero(&new_gw_node->refcount))
		new_gw_node = NULL;

	curr_gw_node = bat_priv->curr_gw;
	rcu_assign_pointer(bat_priv->curr_gw, new_gw_node);

	if (curr_gw_node)
		gw_node_free_ref(curr_gw_node);

	spin_unlock_bh(&bat_priv->gw_list_lock);
}

void gw_deselect(struct bat_priv *bat_priv)
{
	gw_select(bat_priv, NULL);
}

void gw_election(struct bat_priv *bat_priv)
{
	struct hlist_node *node;
	struct gw_node *gw_node, *curr_gw = NULL, *curr_gw_tmp = NULL;
	struct neigh_node *router;
	uint8_t max_tq = 0;
	uint32_t max_gw_factor = 0, tmp_gw_factor = 0;
	int down, up;

	/**
	 * The batman daemon checks here if we already passed a full originator
	 * cycle in order to make sure we don't choose the first gateway we
	 * hear about. This check is based on the daemon's uptime which we
	 * don't have.
	 **/
	if (atomic_read(&bat_priv->gw_mode) != GW_MODE_CLIENT)
		return;

	curr_gw = gw_get_selected_gw_node(bat_priv);
	if (curr_gw)
		goto out;

	rcu_read_lock();
	if (hlist_empty(&bat_priv->gw_list)) {
		bat_dbg(DBG_BATMAN, bat_priv,
			"Removing selected gateway - "
			"no gateway in range\n");
		gw_deselect(bat_priv);
		goto unlock;
	}

	hlist_for_each_entry_rcu(gw_node, node, &bat_priv->gw_list, list) {
		if (gw_node->deleted)
			continue;

		router = orig_node_get_router(gw_node->orig_node);
		if (!router)
			continue;

		switch (atomic_read(&bat_priv->gw_sel_class)) {
		case 1: /* fast connection */
			gw_bandwidth_to_kbit(gw_node->orig_node->gw_flags,
					     &down, &up);

			tmp_gw_factor = (router->tq_avg * router->tq_avg *
					 down * 100 * 100) /
					 (TQ_LOCAL_WINDOW_SIZE *
					 TQ_LOCAL_WINDOW_SIZE * 64);

			if ((tmp_gw_factor > max_gw_factor) ||
			    ((tmp_gw_factor == max_gw_factor) &&
			     (router->tq_avg > max_tq)))
				curr_gw_tmp = gw_node;
			break;

		default: /**
			  * 2:  stable connection (use best statistic)
			  * 3:  fast-switch (use best statistic but change as
			  *     soon as a better gateway appears)
			  * XX: late-switch (use best statistic but change as
			  *     soon as a better gateway appears which has
			  *     $routing_class more tq points)
			  **/
			if (router->tq_avg > max_tq)
				curr_gw_tmp = gw_node;
			break;
		}

		if (router->tq_avg > max_tq)
			max_tq = router->tq_avg;

		if (tmp_gw_factor > max_gw_factor)
			max_gw_factor = tmp_gw_factor;

		neigh_node_free_ref(router);
	}

	if (curr_gw != curr_gw_tmp) {
		router = orig_node_get_router(curr_gw_tmp->orig_node);
		if (!router)
			goto unlock;

		if ((curr_gw) && (!curr_gw_tmp))
			bat_dbg(DBG_BATMAN, bat_priv,
				"Removing selected gateway - "
				"no gateway in range\n");
		else if ((!curr_gw) && (curr_gw_tmp))
			bat_dbg(DBG_BATMAN, bat_priv,
				"Adding route to gateway %pM "
				"(gw_flags: %i, tq: %i)\n",
				curr_gw_tmp->orig_node->orig,
				curr_gw_tmp->orig_node->gw_flags,
				router->tq_avg);
		else
			bat_dbg(DBG_BATMAN, bat_priv,
				"Changing route to gateway %pM "
				"(gw_flags: %i, tq: %i)\n",
				curr_gw_tmp->orig_node->orig,
				curr_gw_tmp->orig_node->gw_flags,
				router->tq_avg);

		neigh_node_free_ref(router);
		gw_select(bat_priv, curr_gw_tmp);
	}

unlock:
	rcu_read_unlock();
out:
	if (curr_gw)
		gw_node_free_ref(curr_gw);
}

void gw_check_election(struct bat_priv *bat_priv, struct orig_node *orig_node)
{
	struct orig_node *curr_gw_orig;
	struct neigh_node *router_gw = NULL, *router_orig = NULL;
	uint8_t gw_tq_avg, orig_tq_avg;

	curr_gw_orig = gw_get_selected_orig(bat_priv);
	if (!curr_gw_orig)
		goto deselect;

	router_gw = orig_node_get_router(curr_gw_orig);
	if (!router_gw)
		goto deselect;

	/* this node already is the gateway */
	if (curr_gw_orig == orig_node)
		goto out;

	router_orig = orig_node_get_router(orig_node);
	if (!router_orig)
		goto out;

	gw_tq_avg = router_gw->tq_avg;
	orig_tq_avg = router_orig->tq_avg;

	/* the TQ value has to be better */
	if (orig_tq_avg < gw_tq_avg)
		goto out;

	/**
	 * if the routing class is greater than 3 the value tells us how much
	 * greater the TQ value of the new gateway must be
	 **/
	if ((atomic_read(&bat_priv->gw_sel_class) > 3) &&
	    (orig_tq_avg - gw_tq_avg < atomic_read(&bat_priv->gw_sel_class)))
		goto out;

	bat_dbg(DBG_BATMAN, bat_priv,
		"Restarting gateway selection: better gateway found (tq curr: "
		"%i, tq new: %i)\n",
		gw_tq_avg, orig_tq_avg);

deselect:
	gw_deselect(bat_priv);
out:
	if (curr_gw_orig)
		orig_node_free_ref(curr_gw_orig);
	if (router_gw)
		neigh_node_free_ref(router_gw);
	if (router_orig)
		neigh_node_free_ref(router_orig);

	return;
}

static void gw_node_add(struct bat_priv *bat_priv,
			struct orig_node *orig_node, uint8_t new_gwflags)
{
	struct gw_node *gw_node;
	int down, up;

	gw_node = kmalloc(sizeof(struct gw_node), GFP_ATOMIC);
	if (!gw_node)
		return;

	memset(gw_node, 0, sizeof(struct gw_node));
	INIT_HLIST_NODE(&gw_node->list);
	gw_node->orig_node = orig_node;
	atomic_set(&gw_node->refcount, 1);

	spin_lock_bh(&bat_priv->gw_list_lock);
	hlist_add_head_rcu(&gw_node->list, &bat_priv->gw_list);
	spin_unlock_bh(&bat_priv->gw_list_lock);

	gw_bandwidth_to_kbit(new_gwflags, &down, &up);
	bat_dbg(DBG_BATMAN, bat_priv,
		"Found new gateway %pM -> gw_class: %i - %i%s/%i%s\n",
		orig_node->orig, new_gwflags,
		(down > 2048 ? down / 1024 : down),
		(down > 2048 ? "MBit" : "KBit"),
		(up > 2048 ? up / 1024 : up),
		(up > 2048 ? "MBit" : "KBit"));
}

void gw_node_update(struct bat_priv *bat_priv,
		    struct orig_node *orig_node, uint8_t new_gwflags)
{
	struct hlist_node *node;
	struct gw_node *gw_node, *curr_gw;

	/**
	 * Note: We don't need a NULL check here, since curr_gw never gets
	 * dereferenced. If curr_gw is NULL we also should not exit as we may
	 * have this gateway in our list (duplication check!) even though we
	 * have no currently selected gateway.
	 */
	curr_gw = gw_get_selected_gw_node(bat_priv);

	rcu_read_lock();
	hlist_for_each_entry_rcu(gw_node, node, &bat_priv->gw_list, list) {
		if (gw_node->orig_node != orig_node)
			continue;

		bat_dbg(DBG_BATMAN, bat_priv,
			"Gateway class of originator %pM changed from "
			"%i to %i\n",
			orig_node->orig, gw_node->orig_node->gw_flags,
			new_gwflags);

		gw_node->deleted = 0;

		if (new_gwflags == 0) {
			gw_node->deleted = jiffies;
			bat_dbg(DBG_BATMAN, bat_priv,
				"Gateway %pM removed from gateway list\n",
				orig_node->orig);

			if (gw_node == curr_gw)
				goto deselect;
		}

		goto unlock;
	}

	if (new_gwflags == 0)
		goto unlock;

	gw_node_add(bat_priv, orig_node, new_gwflags);
	goto unlock;

deselect:
	gw_deselect(bat_priv);
unlock:
	rcu_read_unlock();

	if (curr_gw)
		gw_node_free_ref(curr_gw);
}

void gw_node_delete(struct bat_priv *bat_priv, struct orig_node *orig_node)
{
	return gw_node_update(bat_priv, orig_node, 0);
}

void gw_node_purge(struct bat_priv *bat_priv)
{
	struct gw_node *gw_node, *curr_gw;
	struct hlist_node *node, *node_tmp;
	unsigned long timeout = 2 * PURGE_TIMEOUT * HZ;
	char do_deselect = 0;

	curr_gw = gw_get_selected_gw_node(bat_priv);

	spin_lock_bh(&bat_priv->gw_list_lock);

	hlist_for_each_entry_safe(gw_node, node, node_tmp,
				  &bat_priv->gw_list, list) {
		if (((!gw_node->deleted) ||
		     (time_before(jiffies, gw_node->deleted + timeout))) &&
		    atomic_read(&bat_priv->mesh_state) == MESH_ACTIVE)
			continue;

		if (curr_gw == gw_node)
			do_deselect = 1;

		hlist_del_rcu(&gw_node->list);
		gw_node_free_ref(gw_node);
	}

	spin_unlock_bh(&bat_priv->gw_list_lock);

	/* gw_deselect() needs to acquire the gw_list_lock */
	if (do_deselect)
		gw_deselect(bat_priv);

	if (curr_gw)
		gw_node_free_ref(curr_gw);
}

/**
 * fails if orig_node has no router
 */
static int _write_buffer_text(struct bat_priv *bat_priv,
			      struct seq_file *seq, struct gw_node *gw_node)
{
	struct gw_node *curr_gw;
	struct neigh_node *router;
	int down, up, ret = -1;

	gw_bandwidth_to_kbit(gw_node->orig_node->gw_flags, &down, &up);

	router = orig_node_get_router(gw_node->orig_node);
	if (!router)
		goto out;

	curr_gw = gw_get_selected_gw_node(bat_priv);

	ret = seq_printf(seq, "%s %pM (%3i) %pM [%10s]: %3i - %i%s/%i%s\n",
			 (curr_gw == gw_node ? "=>" : "  "),
			 gw_node->orig_node->orig,
			 router->tq_avg, router->addr,
			 router->if_incoming->net_dev->name,
			 gw_node->orig_node->gw_flags,
			 (down > 2048 ? down / 1024 : down),
			 (down > 2048 ? "MBit" : "KBit"),
			 (up > 2048 ? up / 1024 : up),
			 (up > 2048 ? "MBit" : "KBit"));

	neigh_node_free_ref(router);
	if (curr_gw)
		gw_node_free_ref(curr_gw);
out:
	return ret;
}

int gw_client_seq_print_text(struct seq_file *seq, void *offset)
{
	struct net_device *net_dev = (struct net_device *)seq->private;
	struct bat_priv *bat_priv = netdev_priv(net_dev);
	struct hard_iface *primary_if;
	struct gw_node *gw_node;
	struct hlist_node *node;
	int gw_count = 0, ret = 0;

	primary_if = primary_if_get_selected(bat_priv);
	if (!primary_if) {
		ret = seq_printf(seq, "BATMAN mesh %s disabled - please "
				 "specify interfaces to enable it\n",
				 net_dev->name);
		goto out;
	}

	if (primary_if->if_status != IF_ACTIVE) {
		ret = seq_printf(seq, "BATMAN mesh %s disabled - "
				 "primary interface not active\n",
				 net_dev->name);
		goto out;
	}

	seq_printf(seq, "      %-12s (%s/%i) %17s [%10s]: gw_class ... "
		   "[B.A.T.M.A.N. adv %s%s, MainIF/MAC: %s/%pM (%s)]\n",
		   "Gateway", "#", TQ_MAX_VALUE, "Nexthop",
		   "outgoingIF", SOURCE_VERSION, REVISION_VERSION_STR,
		   primary_if->net_dev->name,
		   primary_if->net_dev->dev_addr, net_dev->name);

	rcu_read_lock();
	hlist_for_each_entry_rcu(gw_node, node, &bat_priv->gw_list, list) {
		if (gw_node->deleted)
			continue;

		/* fails if orig_node has no router */
		if (_write_buffer_text(bat_priv, seq, gw_node) < 0)
			continue;

		gw_count++;
	}
	rcu_read_unlock();

	if (gw_count == 0)
		seq_printf(seq, "No gateways in range ...\n");

out:
	if (primary_if)
		hardif_free_ref(primary_if);
	return ret;
}

int gw_is_target(struct bat_priv *bat_priv, struct sk_buff *skb)
{
	struct ethhdr *ethhdr;
	struct iphdr *iphdr;
	struct ipv6hdr *ipv6hdr;
	struct udphdr *udphdr;
	struct gw_node *curr_gw;
	unsigned int header_len = 0;

	if (atomic_read(&bat_priv->gw_mode) == GW_MODE_OFF)
		return 0;

	/* check for ethernet header */
	if (!pskb_may_pull(skb, header_len + ETH_HLEN))
		return 0;
	ethhdr = (struct ethhdr *)skb->data;
	header_len += ETH_HLEN;

	/* check for initial vlan header */
	if (ntohs(ethhdr->h_proto) == ETH_P_8021Q) {
		if (!pskb_may_pull(skb, header_len + VLAN_HLEN))
			return 0;
		ethhdr = (struct ethhdr *)(skb->data + VLAN_HLEN);
		header_len += VLAN_HLEN;
	}

	/* check for ip header */
	switch (ntohs(ethhdr->h_proto)) {
	case ETH_P_IP:
		if (!pskb_may_pull(skb, header_len + sizeof(struct iphdr)))
			return 0;
		iphdr = (struct iphdr *)(skb->data + header_len);
		header_len += iphdr->ihl * 4;

		/* check for udp header */
		if (iphdr->protocol != IPPROTO_UDP)
			return 0;

		break;
	case ETH_P_IPV6:
		if (!pskb_may_pull(skb, header_len + sizeof(struct ipv6hdr)))
			return 0;
		ipv6hdr = (struct ipv6hdr *)(skb->data + header_len);
		header_len += sizeof(struct ipv6hdr);

		/* check for udp header */
		if (ipv6hdr->nexthdr != IPPROTO_UDP)
			return 0;

		break;
	default:
		return 0;
	}

	if (!pskb_may_pull(skb, header_len + sizeof(struct udphdr)))
		return 0;
	udphdr = (struct udphdr *)(skb->data + header_len);
	header_len += sizeof(struct udphdr);

	/* check for bootp port */
	if ((ntohs(ethhdr->h_proto) == ETH_P_IP) &&
	     (ntohs(udphdr->dest) != 67))
		return 0;

	if ((ntohs(ethhdr->h_proto) == ETH_P_IPV6) &&
	    (ntohs(udphdr->dest) != 547))
		return 0;

	if (atomic_read(&bat_priv->gw_mode) == GW_MODE_SERVER)
		return -1;

	curr_gw = gw_get_selected_gw_node(bat_priv);
	if (!curr_gw)
		return 0;

	if (curr_gw)
		gw_node_free_ref(curr_gw);
	return 1;
}
