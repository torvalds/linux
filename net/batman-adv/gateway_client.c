/* Copyright (C) 2009-2012 B.A.T.M.A.N. contributors:
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
 */

#include "main.h"
#include "sysfs.h"
#include "gateway_client.h"
#include "gateway_common.h"
#include "hard-interface.h"
#include "originator.h"
#include "translation-table.h"
#include "routing.h"
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/udp.h>
#include <linux/if_vlan.h>

/* This is the offset of the options field in a dhcp packet starting at
 * the beginning of the dhcp header
 */
#define BATADV_DHCP_OPTIONS_OFFSET 240
#define BATADV_DHCP_REQUEST 3

static void batadv_gw_node_free_ref(struct batadv_gw_node *gw_node)
{
	if (atomic_dec_and_test(&gw_node->refcount))
		kfree_rcu(gw_node, rcu);
}

static struct batadv_gw_node *
batadv_gw_get_selected_gw_node(struct batadv_priv *bat_priv)
{
	struct batadv_gw_node *gw_node;

	rcu_read_lock();
	gw_node = rcu_dereference(bat_priv->gw.curr_gw);
	if (!gw_node)
		goto out;

	if (!atomic_inc_not_zero(&gw_node->refcount))
		gw_node = NULL;

out:
	rcu_read_unlock();
	return gw_node;
}

struct batadv_orig_node *
batadv_gw_get_selected_orig(struct batadv_priv *bat_priv)
{
	struct batadv_gw_node *gw_node;
	struct batadv_orig_node *orig_node = NULL;

	gw_node = batadv_gw_get_selected_gw_node(bat_priv);
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
		batadv_gw_node_free_ref(gw_node);
	return orig_node;
}

static void batadv_gw_select(struct batadv_priv *bat_priv,
			     struct batadv_gw_node *new_gw_node)
{
	struct batadv_gw_node *curr_gw_node;

	spin_lock_bh(&bat_priv->gw.list_lock);

	if (new_gw_node && !atomic_inc_not_zero(&new_gw_node->refcount))
		new_gw_node = NULL;

	curr_gw_node = rcu_dereference_protected(bat_priv->gw.curr_gw, 1);
	rcu_assign_pointer(bat_priv->gw.curr_gw, new_gw_node);

	if (curr_gw_node)
		batadv_gw_node_free_ref(curr_gw_node);

	spin_unlock_bh(&bat_priv->gw.list_lock);
}

void batadv_gw_deselect(struct batadv_priv *bat_priv)
{
	atomic_set(&bat_priv->gw.reselect, 1);
}

static struct batadv_gw_node *
batadv_gw_get_best_gw_node(struct batadv_priv *bat_priv)
{
	struct batadv_neigh_node *router;
	struct hlist_node *node;
	struct batadv_gw_node *gw_node, *curr_gw = NULL;
	uint32_t max_gw_factor = 0, tmp_gw_factor = 0;
	uint32_t gw_divisor;
	uint8_t max_tq = 0;
	int down, up;
	uint8_t tq_avg;
	struct batadv_orig_node *orig_node;

	gw_divisor = BATADV_TQ_LOCAL_WINDOW_SIZE * BATADV_TQ_LOCAL_WINDOW_SIZE;
	gw_divisor *= 64;

	rcu_read_lock();
	hlist_for_each_entry_rcu(gw_node, node, &bat_priv->gw.list, list) {
		if (gw_node->deleted)
			continue;

		orig_node = gw_node->orig_node;
		router = batadv_orig_node_get_router(orig_node);
		if (!router)
			continue;

		if (!atomic_inc_not_zero(&gw_node->refcount))
			goto next;

		tq_avg = router->tq_avg;

		switch (atomic_read(&bat_priv->gw_sel_class)) {
		case 1: /* fast connection */
			batadv_gw_bandwidth_to_kbit(orig_node->gw_flags,
						    &down, &up);

			tmp_gw_factor = tq_avg * tq_avg * down * 100 * 100;
			tmp_gw_factor /= gw_divisor;

			if ((tmp_gw_factor > max_gw_factor) ||
			    ((tmp_gw_factor == max_gw_factor) &&
			     (tq_avg > max_tq))) {
				if (curr_gw)
					batadv_gw_node_free_ref(curr_gw);
				curr_gw = gw_node;
				atomic_inc(&curr_gw->refcount);
			}
			break;

		default: /* 2:  stable connection (use best statistic)
			  * 3:  fast-switch (use best statistic but change as
			  *     soon as a better gateway appears)
			  * XX: late-switch (use best statistic but change as
			  *     soon as a better gateway appears which has
			  *     $routing_class more tq points)
			  */
			if (tq_avg > max_tq) {
				if (curr_gw)
					batadv_gw_node_free_ref(curr_gw);
				curr_gw = gw_node;
				atomic_inc(&curr_gw->refcount);
			}
			break;
		}

		if (tq_avg > max_tq)
			max_tq = tq_avg;

		if (tmp_gw_factor > max_gw_factor)
			max_gw_factor = tmp_gw_factor;

		batadv_gw_node_free_ref(gw_node);

next:
		batadv_neigh_node_free_ref(router);
	}
	rcu_read_unlock();

	return curr_gw;
}

void batadv_gw_election(struct batadv_priv *bat_priv)
{
	struct batadv_gw_node *curr_gw = NULL, *next_gw = NULL;
	struct batadv_neigh_node *router = NULL;
	char gw_addr[18] = { '\0' };

	/* The batman daemon checks here if we already passed a full originator
	 * cycle in order to make sure we don't choose the first gateway we
	 * hear about. This check is based on the daemon's uptime which we
	 * don't have.
	 */
	if (atomic_read(&bat_priv->gw_mode) != BATADV_GW_MODE_CLIENT)
		goto out;

	curr_gw = batadv_gw_get_selected_gw_node(bat_priv);

	if (!batadv_atomic_dec_not_zero(&bat_priv->gw.reselect) && curr_gw)
		goto out;

	next_gw = batadv_gw_get_best_gw_node(bat_priv);

	if (curr_gw == next_gw)
		goto out;

	if (next_gw) {
		sprintf(gw_addr, "%pM", next_gw->orig_node->orig);

		router = batadv_orig_node_get_router(next_gw->orig_node);
		if (!router) {
			batadv_gw_deselect(bat_priv);
			goto out;
		}
	}

	if ((curr_gw) && (!next_gw)) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Removing selected gateway - no gateway in range\n");
		batadv_throw_uevent(bat_priv, BATADV_UEV_GW, BATADV_UEV_DEL,
				    NULL);
	} else if ((!curr_gw) && (next_gw)) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Adding route to gateway %pM (gw_flags: %i, tq: %i)\n",
			   next_gw->orig_node->orig,
			   next_gw->orig_node->gw_flags, router->tq_avg);
		batadv_throw_uevent(bat_priv, BATADV_UEV_GW, BATADV_UEV_ADD,
				    gw_addr);
	} else {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Changing route to gateway %pM (gw_flags: %i, tq: %i)\n",
			   next_gw->orig_node->orig,
			   next_gw->orig_node->gw_flags, router->tq_avg);
		batadv_throw_uevent(bat_priv, BATADV_UEV_GW, BATADV_UEV_CHANGE,
				    gw_addr);
	}

	batadv_gw_select(bat_priv, next_gw);

out:
	if (curr_gw)
		batadv_gw_node_free_ref(curr_gw);
	if (next_gw)
		batadv_gw_node_free_ref(next_gw);
	if (router)
		batadv_neigh_node_free_ref(router);
}

void batadv_gw_check_election(struct batadv_priv *bat_priv,
			      struct batadv_orig_node *orig_node)
{
	struct batadv_orig_node *curr_gw_orig;
	struct batadv_neigh_node *router_gw = NULL, *router_orig = NULL;
	uint8_t gw_tq_avg, orig_tq_avg;

	curr_gw_orig = batadv_gw_get_selected_orig(bat_priv);
	if (!curr_gw_orig)
		goto deselect;

	router_gw = batadv_orig_node_get_router(curr_gw_orig);
	if (!router_gw)
		goto deselect;

	/* this node already is the gateway */
	if (curr_gw_orig == orig_node)
		goto out;

	router_orig = batadv_orig_node_get_router(orig_node);
	if (!router_orig)
		goto out;

	gw_tq_avg = router_gw->tq_avg;
	orig_tq_avg = router_orig->tq_avg;

	/* the TQ value has to be better */
	if (orig_tq_avg < gw_tq_avg)
		goto out;

	/* if the routing class is greater than 3 the value tells us how much
	 * greater the TQ value of the new gateway must be
	 */
	if ((atomic_read(&bat_priv->gw_sel_class) > 3) &&
	    (orig_tq_avg - gw_tq_avg < atomic_read(&bat_priv->gw_sel_class)))
		goto out;

	batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
		   "Restarting gateway selection: better gateway found (tq curr: %i, tq new: %i)\n",
		   gw_tq_avg, orig_tq_avg);

deselect:
	batadv_gw_deselect(bat_priv);
out:
	if (curr_gw_orig)
		batadv_orig_node_free_ref(curr_gw_orig);
	if (router_gw)
		batadv_neigh_node_free_ref(router_gw);
	if (router_orig)
		batadv_neigh_node_free_ref(router_orig);

	return;
}

static void batadv_gw_node_add(struct batadv_priv *bat_priv,
			       struct batadv_orig_node *orig_node,
			       uint8_t new_gwflags)
{
	struct batadv_gw_node *gw_node;
	int down, up;

	gw_node = kzalloc(sizeof(*gw_node), GFP_ATOMIC);
	if (!gw_node)
		return;

	INIT_HLIST_NODE(&gw_node->list);
	gw_node->orig_node = orig_node;
	atomic_set(&gw_node->refcount, 1);

	spin_lock_bh(&bat_priv->gw.list_lock);
	hlist_add_head_rcu(&gw_node->list, &bat_priv->gw.list);
	spin_unlock_bh(&bat_priv->gw.list_lock);

	batadv_gw_bandwidth_to_kbit(new_gwflags, &down, &up);
	batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
		   "Found new gateway %pM -> gw_class: %i - %i%s/%i%s\n",
		   orig_node->orig, new_gwflags,
		   (down > 2048 ? down / 1024 : down),
		   (down > 2048 ? "MBit" : "KBit"),
		   (up > 2048 ? up / 1024 : up),
		   (up > 2048 ? "MBit" : "KBit"));
}

void batadv_gw_node_update(struct batadv_priv *bat_priv,
			   struct batadv_orig_node *orig_node,
			   uint8_t new_gwflags)
{
	struct hlist_node *node;
	struct batadv_gw_node *gw_node, *curr_gw;

	/* Note: We don't need a NULL check here, since curr_gw never gets
	 * dereferenced. If curr_gw is NULL we also should not exit as we may
	 * have this gateway in our list (duplication check!) even though we
	 * have no currently selected gateway.
	 */
	curr_gw = batadv_gw_get_selected_gw_node(bat_priv);

	rcu_read_lock();
	hlist_for_each_entry_rcu(gw_node, node, &bat_priv->gw.list, list) {
		if (gw_node->orig_node != orig_node)
			continue;

		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Gateway class of originator %pM changed from %i to %i\n",
			   orig_node->orig, gw_node->orig_node->gw_flags,
			   new_gwflags);

		gw_node->deleted = 0;

		if (new_gwflags == BATADV_NO_FLAGS) {
			gw_node->deleted = jiffies;
			batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
				   "Gateway %pM removed from gateway list\n",
				   orig_node->orig);

			if (gw_node == curr_gw)
				goto deselect;
		}

		goto unlock;
	}

	if (new_gwflags == BATADV_NO_FLAGS)
		goto unlock;

	batadv_gw_node_add(bat_priv, orig_node, new_gwflags);
	goto unlock;

deselect:
	batadv_gw_deselect(bat_priv);
unlock:
	rcu_read_unlock();

	if (curr_gw)
		batadv_gw_node_free_ref(curr_gw);
}

void batadv_gw_node_delete(struct batadv_priv *bat_priv,
			   struct batadv_orig_node *orig_node)
{
	batadv_gw_node_update(bat_priv, orig_node, 0);
}

void batadv_gw_node_purge(struct batadv_priv *bat_priv)
{
	struct batadv_gw_node *gw_node, *curr_gw;
	struct hlist_node *node, *node_tmp;
	unsigned long timeout = msecs_to_jiffies(2 * BATADV_PURGE_TIMEOUT);
	int do_deselect = 0;

	curr_gw = batadv_gw_get_selected_gw_node(bat_priv);

	spin_lock_bh(&bat_priv->gw.list_lock);

	hlist_for_each_entry_safe(gw_node, node, node_tmp,
				  &bat_priv->gw.list, list) {
		if (((!gw_node->deleted) ||
		     (time_before(jiffies, gw_node->deleted + timeout))) &&
		    atomic_read(&bat_priv->mesh_state) == BATADV_MESH_ACTIVE)
			continue;

		if (curr_gw == gw_node)
			do_deselect = 1;

		hlist_del_rcu(&gw_node->list);
		batadv_gw_node_free_ref(gw_node);
	}

	spin_unlock_bh(&bat_priv->gw.list_lock);

	/* gw_deselect() needs to acquire the gw_list_lock */
	if (do_deselect)
		batadv_gw_deselect(bat_priv);

	if (curr_gw)
		batadv_gw_node_free_ref(curr_gw);
}

/* fails if orig_node has no router */
static int batadv_write_buffer_text(struct batadv_priv *bat_priv,
				    struct seq_file *seq,
				    const struct batadv_gw_node *gw_node)
{
	struct batadv_gw_node *curr_gw;
	struct batadv_neigh_node *router;
	int down, up, ret = -1;

	batadv_gw_bandwidth_to_kbit(gw_node->orig_node->gw_flags, &down, &up);

	router = batadv_orig_node_get_router(gw_node->orig_node);
	if (!router)
		goto out;

	curr_gw = batadv_gw_get_selected_gw_node(bat_priv);

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

	batadv_neigh_node_free_ref(router);
	if (curr_gw)
		batadv_gw_node_free_ref(curr_gw);
out:
	return ret;
}

int batadv_gw_client_seq_print_text(struct seq_file *seq, void *offset)
{
	struct net_device *net_dev = (struct net_device *)seq->private;
	struct batadv_priv *bat_priv = netdev_priv(net_dev);
	struct batadv_hard_iface *primary_if;
	struct batadv_gw_node *gw_node;
	struct hlist_node *node;
	int gw_count = 0, ret = 0;

	primary_if = batadv_primary_if_get_selected(bat_priv);
	if (!primary_if) {
		ret = seq_printf(seq,
				 "BATMAN mesh %s disabled - please specify interfaces to enable it\n",
				 net_dev->name);
		goto out;
	}

	if (primary_if->if_status != BATADV_IF_ACTIVE) {
		ret = seq_printf(seq,
				 "BATMAN mesh %s disabled - primary interface not active\n",
				 net_dev->name);
		goto out;
	}

	seq_printf(seq,
		   "      %-12s (%s/%i) %17s [%10s]: gw_class ... [B.A.T.M.A.N. adv %s, MainIF/MAC: %s/%pM (%s)]\n",
		   "Gateway", "#", BATADV_TQ_MAX_VALUE, "Nexthop", "outgoingIF",
		   BATADV_SOURCE_VERSION, primary_if->net_dev->name,
		   primary_if->net_dev->dev_addr, net_dev->name);

	rcu_read_lock();
	hlist_for_each_entry_rcu(gw_node, node, &bat_priv->gw.list, list) {
		if (gw_node->deleted)
			continue;

		/* fails if orig_node has no router */
		if (batadv_write_buffer_text(bat_priv, seq, gw_node) < 0)
			continue;

		gw_count++;
	}
	rcu_read_unlock();

	if (gw_count == 0)
		seq_printf(seq, "No gateways in range ...\n");

out:
	if (primary_if)
		batadv_hardif_free_ref(primary_if);
	return ret;
}

static bool batadv_is_type_dhcprequest(struct sk_buff *skb, int header_len)
{
	int ret = false;
	unsigned char *p;
	int pkt_len;

	if (skb_linearize(skb) < 0)
		goto out;

	pkt_len = skb_headlen(skb);

	if (pkt_len < header_len + BATADV_DHCP_OPTIONS_OFFSET + 1)
		goto out;

	p = skb->data + header_len + BATADV_DHCP_OPTIONS_OFFSET;
	pkt_len -= header_len + BATADV_DHCP_OPTIONS_OFFSET + 1;

	/* Access the dhcp option lists. Each entry is made up by:
	 * - octet 1: option type
	 * - octet 2: option data len (only if type != 255 and 0)
	 * - octet 3: option data
	 */
	while (*p != 255 && !ret) {
		/* p now points to the first octet: option type */
		if (*p == 53) {
			/* type 53 is the message type option.
			 * Jump the len octet and go to the data octet
			 */
			if (pkt_len < 2)
				goto out;
			p += 2;

			/* check if the message type is what we need */
			if (*p == BATADV_DHCP_REQUEST)
				ret = true;
			break;
		} else if (*p == 0) {
			/* option type 0 (padding), just go forward */
			if (pkt_len < 1)
				goto out;
			pkt_len--;
			p++;
		} else {
			/* This is any other option. So we get the length... */
			if (pkt_len < 1)
				goto out;
			pkt_len--;
			p++;

			/* ...and then we jump over the data */
			if (pkt_len < 1 + (*p))
				goto out;
			pkt_len -= 1 + (*p);
			p += 1 + (*p);
		}
	}
out:
	return ret;
}

bool batadv_gw_is_dhcp_target(struct sk_buff *skb, unsigned int *header_len)
{
	struct ethhdr *ethhdr;
	struct iphdr *iphdr;
	struct ipv6hdr *ipv6hdr;
	struct udphdr *udphdr;

	/* check for ethernet header */
	if (!pskb_may_pull(skb, *header_len + ETH_HLEN))
		return false;
	ethhdr = (struct ethhdr *)skb->data;
	*header_len += ETH_HLEN;

	/* check for initial vlan header */
	if (ntohs(ethhdr->h_proto) == ETH_P_8021Q) {
		if (!pskb_may_pull(skb, *header_len + VLAN_HLEN))
			return false;
		ethhdr = (struct ethhdr *)(skb->data + VLAN_HLEN);
		*header_len += VLAN_HLEN;
	}

	/* check for ip header */
	switch (ntohs(ethhdr->h_proto)) {
	case ETH_P_IP:
		if (!pskb_may_pull(skb, *header_len + sizeof(*iphdr)))
			return false;
		iphdr = (struct iphdr *)(skb->data + *header_len);
		*header_len += iphdr->ihl * 4;

		/* check for udp header */
		if (iphdr->protocol != IPPROTO_UDP)
			return false;

		break;
	case ETH_P_IPV6:
		if (!pskb_may_pull(skb, *header_len + sizeof(*ipv6hdr)))
			return false;
		ipv6hdr = (struct ipv6hdr *)(skb->data + *header_len);
		*header_len += sizeof(*ipv6hdr);

		/* check for udp header */
		if (ipv6hdr->nexthdr != IPPROTO_UDP)
			return false;

		break;
	default:
		return false;
	}

	if (!pskb_may_pull(skb, *header_len + sizeof(*udphdr)))
		return false;
	udphdr = (struct udphdr *)(skb->data + *header_len);
	*header_len += sizeof(*udphdr);

	/* check for bootp port */
	if ((ntohs(ethhdr->h_proto) == ETH_P_IP) &&
	    (ntohs(udphdr->dest) != 67))
		return false;

	if ((ntohs(ethhdr->h_proto) == ETH_P_IPV6) &&
	    (ntohs(udphdr->dest) != 547))
		return false;

	return true;
}

bool batadv_gw_out_of_range(struct batadv_priv *bat_priv,
			    struct sk_buff *skb, struct ethhdr *ethhdr)
{
	struct batadv_neigh_node *neigh_curr = NULL, *neigh_old = NULL;
	struct batadv_orig_node *orig_dst_node = NULL;
	struct batadv_gw_node *curr_gw = NULL;
	bool ret, out_of_range = false;
	unsigned int header_len = 0;
	uint8_t curr_tq_avg;

	ret = batadv_gw_is_dhcp_target(skb, &header_len);
	if (!ret)
		goto out;

	orig_dst_node = batadv_transtable_search(bat_priv, ethhdr->h_source,
						 ethhdr->h_dest);
	if (!orig_dst_node)
		goto out;

	if (!orig_dst_node->gw_flags)
		goto out;

	ret = batadv_is_type_dhcprequest(skb, header_len);
	if (!ret)
		goto out;

	switch (atomic_read(&bat_priv->gw_mode)) {
	case BATADV_GW_MODE_SERVER:
		/* If we are a GW then we are our best GW. We can artificially
		 * set the tq towards ourself as the maximum value
		 */
		curr_tq_avg = BATADV_TQ_MAX_VALUE;
		break;
	case BATADV_GW_MODE_CLIENT:
		curr_gw = batadv_gw_get_selected_gw_node(bat_priv);
		if (!curr_gw)
			goto out;

		/* packet is going to our gateway */
		if (curr_gw->orig_node == orig_dst_node)
			goto out;

		/* If the dhcp packet has been sent to a different gw,
		 * we have to evaluate whether the old gw is still
		 * reliable enough
		 */
		neigh_curr = batadv_find_router(bat_priv, curr_gw->orig_node,
						NULL);
		if (!neigh_curr)
			goto out;

		curr_tq_avg = neigh_curr->tq_avg;
		break;
	case BATADV_GW_MODE_OFF:
	default:
		goto out;
	}

	neigh_old = batadv_find_router(bat_priv, orig_dst_node, NULL);
	if (!neigh_old)
		goto out;

	if (curr_tq_avg - neigh_old->tq_avg > BATADV_GW_THRESHOLD)
		out_of_range = true;

out:
	if (orig_dst_node)
		batadv_orig_node_free_ref(orig_dst_node);
	if (curr_gw)
		batadv_gw_node_free_ref(curr_gw);
	if (neigh_old)
		batadv_neigh_node_free_ref(neigh_old);
	if (neigh_curr)
		batadv_neigh_node_free_ref(neigh_curr);
	return out_of_range;
}
