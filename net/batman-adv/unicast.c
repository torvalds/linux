/* Copyright (C) 2010-2012 B.A.T.M.A.N. contributors:
 *
 * Andreas Langer
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
#include "unicast.h"
#include "send.h"
#include "soft-interface.h"
#include "gateway_client.h"
#include "originator.h"
#include "hash.h"
#include "translation-table.h"
#include "routing.h"
#include "hard-interface.h"


static struct sk_buff *frag_merge_packet(struct list_head *head,
					 struct frag_packet_list_entry *tfp,
					 struct sk_buff *skb)
{
	struct unicast_frag_packet *up =
		(struct unicast_frag_packet *)skb->data;
	struct sk_buff *tmp_skb;
	struct unicast_packet *unicast_packet;
	int hdr_len = sizeof(*unicast_packet);
	int uni_diff = sizeof(*up) - hdr_len;

	/* set skb to the first part and tmp_skb to the second part */
	if (up->flags & UNI_FRAG_HEAD) {
		tmp_skb = tfp->skb;
	} else {
		tmp_skb = skb;
		skb = tfp->skb;
	}

	if (skb_linearize(skb) < 0 || skb_linearize(tmp_skb) < 0)
		goto err;

	skb_pull(tmp_skb, sizeof(*up));
	if (pskb_expand_head(skb, 0, tmp_skb->len, GFP_ATOMIC) < 0)
		goto err;

	/* move free entry to end */
	tfp->skb = NULL;
	tfp->seqno = 0;
	list_move_tail(&tfp->list, head);

	memcpy(skb_put(skb, tmp_skb->len), tmp_skb->data, tmp_skb->len);
	kfree_skb(tmp_skb);

	memmove(skb->data + uni_diff, skb->data, hdr_len);
	unicast_packet = (struct unicast_packet *)skb_pull(skb, uni_diff);
	unicast_packet->header.packet_type = BAT_UNICAST;

	return skb;

err:
	/* free buffered skb, skb will be freed later */
	kfree_skb(tfp->skb);
	return NULL;
}

static void frag_create_entry(struct list_head *head, struct sk_buff *skb)
{
	struct frag_packet_list_entry *tfp;
	struct unicast_frag_packet *up =
		(struct unicast_frag_packet *)skb->data;

	/* free and oldest packets stand at the end */
	tfp = list_entry((head)->prev, typeof(*tfp), list);
	kfree_skb(tfp->skb);

	tfp->seqno = ntohs(up->seqno);
	tfp->skb = skb;
	list_move(&tfp->list, head);
	return;
}

static int frag_create_buffer(struct list_head *head)
{
	int i;
	struct frag_packet_list_entry *tfp;

	for (i = 0; i < FRAG_BUFFER_SIZE; i++) {
		tfp = kmalloc(sizeof(*tfp), GFP_ATOMIC);
		if (!tfp) {
			batadv_frag_list_free(head);
			return -ENOMEM;
		}
		tfp->skb = NULL;
		tfp->seqno = 0;
		INIT_LIST_HEAD(&tfp->list);
		list_add(&tfp->list, head);
	}

	return 0;
}

static struct frag_packet_list_entry *frag_search_packet(struct list_head *head,
					   const struct unicast_frag_packet *up)
{
	struct frag_packet_list_entry *tfp;
	struct unicast_frag_packet *tmp_up = NULL;
	uint16_t search_seqno;

	if (up->flags & UNI_FRAG_HEAD)
		search_seqno = ntohs(up->seqno)+1;
	else
		search_seqno = ntohs(up->seqno)-1;

	list_for_each_entry(tfp, head, list) {

		if (!tfp->skb)
			continue;

		if (tfp->seqno == ntohs(up->seqno))
			goto mov_tail;

		tmp_up = (struct unicast_frag_packet *)tfp->skb->data;

		if (tfp->seqno == search_seqno) {

			if ((tmp_up->flags & UNI_FRAG_HEAD) !=
			    (up->flags & UNI_FRAG_HEAD))
				return tfp;
			else
				goto mov_tail;
		}
	}
	return NULL;

mov_tail:
	list_move_tail(&tfp->list, head);
	return NULL;
}

void batadv_frag_list_free(struct list_head *head)
{
	struct frag_packet_list_entry *pf, *tmp_pf;

	if (!list_empty(head)) {

		list_for_each_entry_safe(pf, tmp_pf, head, list) {
			kfree_skb(pf->skb);
			list_del(&pf->list);
			kfree(pf);
		}
	}
	return;
}

/* frag_reassemble_skb():
 * returns NET_RX_DROP if the operation failed - skb is left intact
 * returns NET_RX_SUCCESS if the fragment was buffered (skb_new will be NULL)
 * or the skb could be reassembled (skb_new will point to the new packet and
 * skb was freed)
 */
int batadv_frag_reassemble_skb(struct sk_buff *skb, struct bat_priv *bat_priv,
			       struct sk_buff **new_skb)
{
	struct orig_node *orig_node;
	struct frag_packet_list_entry *tmp_frag_entry;
	int ret = NET_RX_DROP;
	struct unicast_frag_packet *unicast_packet =
		(struct unicast_frag_packet *)skb->data;

	*new_skb = NULL;

	orig_node = orig_hash_find(bat_priv, unicast_packet->orig);
	if (!orig_node)
		goto out;

	orig_node->last_frag_packet = jiffies;

	if (list_empty(&orig_node->frag_list) &&
	    frag_create_buffer(&orig_node->frag_list)) {
		pr_debug("couldn't create frag buffer\n");
		goto out;
	}

	tmp_frag_entry = frag_search_packet(&orig_node->frag_list,
					    unicast_packet);

	if (!tmp_frag_entry) {
		frag_create_entry(&orig_node->frag_list, skb);
		ret = NET_RX_SUCCESS;
		goto out;
	}

	*new_skb = frag_merge_packet(&orig_node->frag_list, tmp_frag_entry,
				     skb);
	/* if not, merge failed */
	if (*new_skb)
		ret = NET_RX_SUCCESS;

out:
	if (orig_node)
		batadv_orig_node_free_ref(orig_node);
	return ret;
}

int batadv_frag_send_skb(struct sk_buff *skb, struct bat_priv *bat_priv,
			 struct hard_iface *hard_iface, const uint8_t dstaddr[])
{
	struct unicast_packet tmp_uc, *unicast_packet;
	struct hard_iface *primary_if;
	struct sk_buff *frag_skb;
	struct unicast_frag_packet *frag1, *frag2;
	int uc_hdr_len = sizeof(*unicast_packet);
	int ucf_hdr_len = sizeof(*frag1);
	int data_len = skb->len - uc_hdr_len;
	int large_tail = 0, ret = NET_RX_DROP;
	uint16_t seqno;

	primary_if = batadv_primary_if_get_selected(bat_priv);
	if (!primary_if)
		goto dropped;

	frag_skb = dev_alloc_skb(data_len - (data_len / 2) + ucf_hdr_len);
	if (!frag_skb)
		goto dropped;
	skb_reserve(frag_skb, ucf_hdr_len);

	unicast_packet = (struct unicast_packet *)skb->data;
	memcpy(&tmp_uc, unicast_packet, uc_hdr_len);
	skb_split(skb, frag_skb, data_len / 2 + uc_hdr_len);

	if (batadv_skb_head_push(skb, ucf_hdr_len - uc_hdr_len) < 0 ||
	    batadv_skb_head_push(frag_skb, ucf_hdr_len) < 0)
		goto drop_frag;

	frag1 = (struct unicast_frag_packet *)skb->data;
	frag2 = (struct unicast_frag_packet *)frag_skb->data;

	memcpy(frag1, &tmp_uc, sizeof(tmp_uc));

	frag1->header.ttl--;
	frag1->header.version = COMPAT_VERSION;
	frag1->header.packet_type = BAT_UNICAST_FRAG;

	memcpy(frag1->orig, primary_if->net_dev->dev_addr, ETH_ALEN);
	memcpy(frag2, frag1, sizeof(*frag2));

	if (data_len & 1)
		large_tail = UNI_FRAG_LARGETAIL;

	frag1->flags = UNI_FRAG_HEAD | large_tail;
	frag2->flags = large_tail;

	seqno = atomic_add_return(2, &hard_iface->frag_seqno);
	frag1->seqno = htons(seqno - 1);
	frag2->seqno = htons(seqno);

	batadv_send_skb_packet(skb, hard_iface, dstaddr);
	batadv_send_skb_packet(frag_skb, hard_iface, dstaddr);
	ret = NET_RX_SUCCESS;
	goto out;

drop_frag:
	kfree_skb(frag_skb);
dropped:
	kfree_skb(skb);
out:
	if (primary_if)
		batadv_hardif_free_ref(primary_if);
	return ret;
}

int batadv_unicast_send_skb(struct sk_buff *skb, struct bat_priv *bat_priv)
{
	struct ethhdr *ethhdr = (struct ethhdr *)skb->data;
	struct unicast_packet *unicast_packet;
	struct orig_node *orig_node;
	struct neigh_node *neigh_node;
	int data_len = skb->len;
	int ret = 1;

	/* get routing information */
	if (is_multicast_ether_addr(ethhdr->h_dest)) {
		orig_node = batadv_gw_get_selected_orig(bat_priv);
		if (orig_node)
			goto find_router;
	}

	/* check for tt host - increases orig_node refcount.
	 * returns NULL in case of AP isolation
	 */
	orig_node = batadv_transtable_search(bat_priv, ethhdr->h_source,
					     ethhdr->h_dest);
find_router:
	/* find_router():
	 *  - if orig_node is NULL it returns NULL
	 *  - increases neigh_nodes refcount if found.
	 */
	neigh_node = batadv_find_router(bat_priv, orig_node, NULL);
	if (!neigh_node)
		goto out;

	if (batadv_skb_head_push(skb, sizeof(*unicast_packet)) < 0)
		goto out;

	unicast_packet = (struct unicast_packet *)skb->data;

	unicast_packet->header.version = COMPAT_VERSION;
	/* batman packet type: unicast */
	unicast_packet->header.packet_type = BAT_UNICAST;
	/* set unicast ttl */
	unicast_packet->header.ttl = TTL;
	/* copy the destination for faster routing */
	memcpy(unicast_packet->dest, orig_node->orig, ETH_ALEN);
	/* set the destination tt version number */
	unicast_packet->ttvn =
		(uint8_t)atomic_read(&orig_node->last_ttvn);

	/* inform the destination node that we are still missing a correct route
	 * for this client. The destination will receive this packet and will
	 * try to reroute it because the ttvn contained in the header is less
	 * than the current one
	 */
	if (batadv_tt_global_client_is_roaming(bat_priv, ethhdr->h_dest))
		unicast_packet->ttvn = unicast_packet->ttvn - 1;

	if (atomic_read(&bat_priv->fragmentation) &&
	    data_len + sizeof(*unicast_packet) >
				neigh_node->if_incoming->net_dev->mtu) {
		/* send frag skb decreases ttl */
		unicast_packet->header.ttl++;
		ret = batadv_frag_send_skb(skb, bat_priv,
					   neigh_node->if_incoming,
					   neigh_node->addr);
		goto out;
	}

	batadv_send_skb_packet(skb, neigh_node->if_incoming, neigh_node->addr);
	ret = 0;
	goto out;

out:
	if (neigh_node)
		batadv_neigh_node_free_ref(neigh_node);
	if (orig_node)
		batadv_orig_node_free_ref(orig_node);
	if (ret == 1)
		kfree_skb(skb);
	return ret;
}
