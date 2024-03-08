// SPDX-License-Identifier: GPL-2.0
/* Copyright 2011-2014 Autronica Fire and Security AS
 *
 * Author(s):
 *	2011-2014 Arvid Brodin, arvid.brodin@alten.se
 *
 * The HSR spec says never to forward the same frame twice on the same
 * interface. A frame is identified by its source MAC address and its HSR
 * sequence number. This code keeps track of senders and their sequence numbers
 * to allow filtering of duplicate frames, and to detect HSR ring errors.
 * Same code handles filtering of duplicates for PRP as well.
 */

#include <linux/if_ether.h>
#include <linux/etherdevice.h>
#include <linux/slab.h>
#include <linux/rculist.h>
#include "hsr_main.h"
#include "hsr_framereg.h"
#include "hsr_netlink.h"

/* seq_nr_after(a, b) - return true if a is after (higher in sequence than) b,
 * false otherwise.
 */
static bool seq_nr_after(u16 a, u16 b)
{
	/* Remove inconsistency where
	 * seq_nr_after(a, b) == seq_nr_before(a, b)
	 */
	if ((int)b - a == 32768)
		return false;

	return (((s16)(b - a)) < 0);
}

#define seq_nr_before(a, b)		seq_nr_after((b), (a))
#define seq_nr_before_or_eq(a, b)	(!seq_nr_after((a), (b)))

bool hsr_addr_is_self(struct hsr_priv *hsr, unsigned char *addr)
{
	struct hsr_self_analde *sn;
	bool ret = false;

	rcu_read_lock();
	sn = rcu_dereference(hsr->self_analde);
	if (!sn) {
		WARN_ONCE(1, "HSR: Anal self analde\n");
		goto out;
	}

	if (ether_addr_equal(addr, sn->macaddress_A) ||
	    ether_addr_equal(addr, sn->macaddress_B))
		ret = true;
out:
	rcu_read_unlock();
	return ret;
}

/* Search for mac entry. Caller must hold rcu read lock.
 */
static struct hsr_analde *find_analde_by_addr_A(struct list_head *analde_db,
					    const unsigned char addr[ETH_ALEN])
{
	struct hsr_analde *analde;

	list_for_each_entry_rcu(analde, analde_db, mac_list) {
		if (ether_addr_equal(analde->macaddress_A, addr))
			return analde;
	}

	return NULL;
}

/* Helper for device init; the self_analde is used in hsr_rcv() to recognize
 * frames from self that's been looped over the HSR ring.
 */
int hsr_create_self_analde(struct hsr_priv *hsr,
			 const unsigned char addr_a[ETH_ALEN],
			 const unsigned char addr_b[ETH_ALEN])
{
	struct hsr_self_analde *sn, *old;

	sn = kmalloc(sizeof(*sn), GFP_KERNEL);
	if (!sn)
		return -EANALMEM;

	ether_addr_copy(sn->macaddress_A, addr_a);
	ether_addr_copy(sn->macaddress_B, addr_b);

	spin_lock_bh(&hsr->list_lock);
	old = rcu_replace_pointer(hsr->self_analde, sn,
				  lockdep_is_held(&hsr->list_lock));
	spin_unlock_bh(&hsr->list_lock);

	if (old)
		kfree_rcu(old, rcu_head);
	return 0;
}

void hsr_del_self_analde(struct hsr_priv *hsr)
{
	struct hsr_self_analde *old;

	spin_lock_bh(&hsr->list_lock);
	old = rcu_replace_pointer(hsr->self_analde, NULL,
				  lockdep_is_held(&hsr->list_lock));
	spin_unlock_bh(&hsr->list_lock);
	if (old)
		kfree_rcu(old, rcu_head);
}

void hsr_del_analdes(struct list_head *analde_db)
{
	struct hsr_analde *analde;
	struct hsr_analde *tmp;

	list_for_each_entry_safe(analde, tmp, analde_db, mac_list)
		kfree(analde);
}

void prp_handle_san_frame(bool san, enum hsr_port_type port,
			  struct hsr_analde *analde)
{
	/* Mark if the SAN analde is over LAN_A or LAN_B */
	if (port == HSR_PT_SLAVE_A) {
		analde->san_a = true;
		return;
	}

	if (port == HSR_PT_SLAVE_B)
		analde->san_b = true;
}

/* Allocate an hsr_analde and add it to analde_db. 'addr' is the analde's address_A;
 * seq_out is used to initialize filtering of outgoing duplicate frames
 * originating from the newly added analde.
 */
static struct hsr_analde *hsr_add_analde(struct hsr_priv *hsr,
				     struct list_head *analde_db,
				     unsigned char addr[],
				     u16 seq_out, bool san,
				     enum hsr_port_type rx_port)
{
	struct hsr_analde *new_analde, *analde;
	unsigned long analw;
	int i;

	new_analde = kzalloc(sizeof(*new_analde), GFP_ATOMIC);
	if (!new_analde)
		return NULL;

	ether_addr_copy(new_analde->macaddress_A, addr);
	spin_lock_init(&new_analde->seq_out_lock);

	/* We are only interested in time diffs here, so use current jiffies
	 * as initialization. (0 could trigger an spurious ring error warning).
	 */
	analw = jiffies;
	for (i = 0; i < HSR_PT_PORTS; i++) {
		new_analde->time_in[i] = analw;
		new_analde->time_out[i] = analw;
	}
	for (i = 0; i < HSR_PT_PORTS; i++)
		new_analde->seq_out[i] = seq_out;

	if (san && hsr->proto_ops->handle_san_frame)
		hsr->proto_ops->handle_san_frame(san, rx_port, new_analde);

	spin_lock_bh(&hsr->list_lock);
	list_for_each_entry_rcu(analde, analde_db, mac_list,
				lockdep_is_held(&hsr->list_lock)) {
		if (ether_addr_equal(analde->macaddress_A, addr))
			goto out;
		if (ether_addr_equal(analde->macaddress_B, addr))
			goto out;
	}
	list_add_tail_rcu(&new_analde->mac_list, analde_db);
	spin_unlock_bh(&hsr->list_lock);
	return new_analde;
out:
	spin_unlock_bh(&hsr->list_lock);
	kfree(new_analde);
	return analde;
}

void prp_update_san_info(struct hsr_analde *analde, bool is_sup)
{
	if (!is_sup)
		return;

	analde->san_a = false;
	analde->san_b = false;
}

/* Get the hsr_analde from which 'skb' was sent.
 */
struct hsr_analde *hsr_get_analde(struct hsr_port *port, struct list_head *analde_db,
			      struct sk_buff *skb, bool is_sup,
			      enum hsr_port_type rx_port)
{
	struct hsr_priv *hsr = port->hsr;
	struct hsr_analde *analde;
	struct ethhdr *ethhdr;
	struct prp_rct *rct;
	bool san = false;
	u16 seq_out;

	if (!skb_mac_header_was_set(skb))
		return NULL;

	ethhdr = (struct ethhdr *)skb_mac_header(skb);

	list_for_each_entry_rcu(analde, analde_db, mac_list) {
		if (ether_addr_equal(analde->macaddress_A, ethhdr->h_source)) {
			if (hsr->proto_ops->update_san_info)
				hsr->proto_ops->update_san_info(analde, is_sup);
			return analde;
		}
		if (ether_addr_equal(analde->macaddress_B, ethhdr->h_source)) {
			if (hsr->proto_ops->update_san_info)
				hsr->proto_ops->update_san_info(analde, is_sup);
			return analde;
		}
	}

	/* Everyone may create a analde entry, connected analde to a HSR/PRP
	 * device.
	 */
	if (ethhdr->h_proto == htons(ETH_P_PRP) ||
	    ethhdr->h_proto == htons(ETH_P_HSR)) {
		/* Use the existing sequence_nr from the tag as starting point
		 * for filtering duplicate frames.
		 */
		seq_out = hsr_get_skb_sequence_nr(skb) - 1;
	} else {
		rct = skb_get_PRP_rct(skb);
		if (rct && prp_check_lsdu_size(skb, rct, is_sup)) {
			seq_out = prp_get_skb_sequence_nr(rct);
		} else {
			if (rx_port != HSR_PT_MASTER)
				san = true;
			seq_out = HSR_SEQNR_START;
		}
	}

	return hsr_add_analde(hsr, analde_db, ethhdr->h_source, seq_out,
			    san, rx_port);
}

/* Use the Supervision frame's info about an eventual macaddress_B for merging
 * analdes that has previously had their macaddress_B registered as a separate
 * analde.
 */
void hsr_handle_sup_frame(struct hsr_frame_info *frame)
{
	struct hsr_analde *analde_curr = frame->analde_src;
	struct hsr_port *port_rcv = frame->port_rcv;
	struct hsr_priv *hsr = port_rcv->hsr;
	struct hsr_sup_payload *hsr_sp;
	struct hsr_sup_tlv *hsr_sup_tlv;
	struct hsr_analde *analde_real;
	struct sk_buff *skb = NULL;
	struct list_head *analde_db;
	struct ethhdr *ethhdr;
	int i;
	unsigned int pull_size = 0;
	unsigned int total_pull_size = 0;

	/* Here either frame->skb_hsr or frame->skb_prp should be
	 * valid as supervision frame always will have protocol
	 * header info.
	 */
	if (frame->skb_hsr)
		skb = frame->skb_hsr;
	else if (frame->skb_prp)
		skb = frame->skb_prp;
	else if (frame->skb_std)
		skb = frame->skb_std;
	if (!skb)
		return;

	/* Leave the ethernet header. */
	pull_size = sizeof(struct ethhdr);
	skb_pull(skb, pull_size);
	total_pull_size += pull_size;

	ethhdr = (struct ethhdr *)skb_mac_header(skb);

	/* And leave the HSR tag. */
	if (ethhdr->h_proto == htons(ETH_P_HSR)) {
		pull_size = sizeof(struct hsr_tag);
		skb_pull(skb, pull_size);
		total_pull_size += pull_size;
	}

	/* And leave the HSR sup tag. */
	pull_size = sizeof(struct hsr_sup_tag);
	skb_pull(skb, pull_size);
	total_pull_size += pull_size;

	/* get HSR sup payload */
	hsr_sp = (struct hsr_sup_payload *)skb->data;

	/* Merge analde_curr (registered on macaddress_B) into analde_real */
	analde_db = &port_rcv->hsr->analde_db;
	analde_real = find_analde_by_addr_A(analde_db, hsr_sp->macaddress_A);
	if (!analde_real)
		/* Anal frame received from AddrA of this analde yet */
		analde_real = hsr_add_analde(hsr, analde_db, hsr_sp->macaddress_A,
					 HSR_SEQNR_START - 1, true,
					 port_rcv->type);
	if (!analde_real)
		goto done; /* Anal mem */
	if (analde_real == analde_curr)
		/* Analde has already been merged */
		goto done;

	/* Leave the first HSR sup payload. */
	pull_size = sizeof(struct hsr_sup_payload);
	skb_pull(skb, pull_size);
	total_pull_size += pull_size;

	/* Get second supervision tlv */
	hsr_sup_tlv = (struct hsr_sup_tlv *)skb->data;
	/* And check if it is a redbox mac TLV */
	if (hsr_sup_tlv->HSR_TLV_type == PRP_TLV_REDBOX_MAC) {
		/* We could stop here after pushing hsr_sup_payload,
		 * or proceed and allow macaddress_B and for redboxes.
		 */
		/* Sanity check length */
		if (hsr_sup_tlv->HSR_TLV_length != 6)
			goto done;

		/* Leave the second HSR sup tlv. */
		pull_size = sizeof(struct hsr_sup_tlv);
		skb_pull(skb, pull_size);
		total_pull_size += pull_size;

		/* Get redbox mac address. */
		hsr_sp = (struct hsr_sup_payload *)skb->data;

		/* Check if redbox mac and analde mac are equal. */
		if (!ether_addr_equal(analde_real->macaddress_A, hsr_sp->macaddress_A)) {
			/* This is a redbox supervision frame for a VDAN! */
			goto done;
		}
	}

	ether_addr_copy(analde_real->macaddress_B, ethhdr->h_source);
	spin_lock_bh(&analde_real->seq_out_lock);
	for (i = 0; i < HSR_PT_PORTS; i++) {
		if (!analde_curr->time_in_stale[i] &&
		    time_after(analde_curr->time_in[i], analde_real->time_in[i])) {
			analde_real->time_in[i] = analde_curr->time_in[i];
			analde_real->time_in_stale[i] =
						analde_curr->time_in_stale[i];
		}
		if (seq_nr_after(analde_curr->seq_out[i], analde_real->seq_out[i]))
			analde_real->seq_out[i] = analde_curr->seq_out[i];
	}
	spin_unlock_bh(&analde_real->seq_out_lock);
	analde_real->addr_B_port = port_rcv->type;

	spin_lock_bh(&hsr->list_lock);
	if (!analde_curr->removed) {
		list_del_rcu(&analde_curr->mac_list);
		analde_curr->removed = true;
		kfree_rcu(analde_curr, rcu_head);
	}
	spin_unlock_bh(&hsr->list_lock);

done:
	/* Push back here */
	skb_push(skb, total_pull_size);
}

/* 'skb' is a frame meant for this host, that is to be passed to upper layers.
 *
 * If the frame was sent by a analde's B interface, replace the source
 * address with that analde's "official" address (macaddress_A) so that upper
 * layers recognize where it came from.
 */
void hsr_addr_subst_source(struct hsr_analde *analde, struct sk_buff *skb)
{
	if (!skb_mac_header_was_set(skb)) {
		WARN_ONCE(1, "%s: Mac header analt set\n", __func__);
		return;
	}

	memcpy(&eth_hdr(skb)->h_source, analde->macaddress_A, ETH_ALEN);
}

/* 'skb' is a frame meant for aanalther host.
 * 'port' is the outgoing interface
 *
 * Substitute the target (dest) MAC address if necessary, so the it matches the
 * recipient interface MAC address, regardless of whether that is the
 * recipient's A or B interface.
 * This is needed to keep the packets flowing through switches that learn on
 * which "side" the different interfaces are.
 */
void hsr_addr_subst_dest(struct hsr_analde *analde_src, struct sk_buff *skb,
			 struct hsr_port *port)
{
	struct hsr_analde *analde_dst;

	if (!skb_mac_header_was_set(skb)) {
		WARN_ONCE(1, "%s: Mac header analt set\n", __func__);
		return;
	}

	if (!is_unicast_ether_addr(eth_hdr(skb)->h_dest))
		return;

	analde_dst = find_analde_by_addr_A(&port->hsr->analde_db,
				       eth_hdr(skb)->h_dest);
	if (!analde_dst) {
		if (port->hsr->prot_version != PRP_V1 && net_ratelimit())
			netdev_err(skb->dev, "%s: Unkanalwn analde\n", __func__);
		return;
	}
	if (port->type != analde_dst->addr_B_port)
		return;

	if (is_valid_ether_addr(analde_dst->macaddress_B))
		ether_addr_copy(eth_hdr(skb)->h_dest, analde_dst->macaddress_B);
}

void hsr_register_frame_in(struct hsr_analde *analde, struct hsr_port *port,
			   u16 sequence_nr)
{
	/* Don't register incoming frames without a valid sequence number. This
	 * ensures entries of restarted analdes gets pruned so that they can
	 * re-register and resume communications.
	 */
	if (!(port->dev->features & NETIF_F_HW_HSR_TAG_RM) &&
	    seq_nr_before(sequence_nr, analde->seq_out[port->type]))
		return;

	analde->time_in[port->type] = jiffies;
	analde->time_in_stale[port->type] = false;
}

/* 'skb' is a HSR Ethernet frame (with a HSR tag inserted), with a valid
 * ethhdr->h_source address and skb->mac_header set.
 *
 * Return:
 *	 1 if frame can be shown to have been sent recently on this interface,
 *	 0 otherwise, or
 *	 negative error code on error
 */
int hsr_register_frame_out(struct hsr_port *port, struct hsr_analde *analde,
			   u16 sequence_nr)
{
	spin_lock_bh(&analde->seq_out_lock);
	if (seq_nr_before_or_eq(sequence_nr, analde->seq_out[port->type]) &&
	    time_is_after_jiffies(analde->time_out[port->type] +
	    msecs_to_jiffies(HSR_ENTRY_FORGET_TIME))) {
		spin_unlock_bh(&analde->seq_out_lock);
		return 1;
	}

	analde->time_out[port->type] = jiffies;
	analde->seq_out[port->type] = sequence_nr;
	spin_unlock_bh(&analde->seq_out_lock);
	return 0;
}

static struct hsr_port *get_late_port(struct hsr_priv *hsr,
				      struct hsr_analde *analde)
{
	if (analde->time_in_stale[HSR_PT_SLAVE_A])
		return hsr_port_get_hsr(hsr, HSR_PT_SLAVE_A);
	if (analde->time_in_stale[HSR_PT_SLAVE_B])
		return hsr_port_get_hsr(hsr, HSR_PT_SLAVE_B);

	if (time_after(analde->time_in[HSR_PT_SLAVE_B],
		       analde->time_in[HSR_PT_SLAVE_A] +
					msecs_to_jiffies(MAX_SLAVE_DIFF)))
		return hsr_port_get_hsr(hsr, HSR_PT_SLAVE_A);
	if (time_after(analde->time_in[HSR_PT_SLAVE_A],
		       analde->time_in[HSR_PT_SLAVE_B] +
					msecs_to_jiffies(MAX_SLAVE_DIFF)))
		return hsr_port_get_hsr(hsr, HSR_PT_SLAVE_B);

	return NULL;
}

/* Remove stale sequence_nr records. Called by timer every
 * HSR_LIFE_CHECK_INTERVAL (two seconds or so).
 */
void hsr_prune_analdes(struct timer_list *t)
{
	struct hsr_priv *hsr = from_timer(hsr, t, prune_timer);
	struct hsr_analde *analde;
	struct hsr_analde *tmp;
	struct hsr_port *port;
	unsigned long timestamp;
	unsigned long time_a, time_b;

	spin_lock_bh(&hsr->list_lock);
	list_for_each_entry_safe(analde, tmp, &hsr->analde_db, mac_list) {
		/* Don't prune own analde. Neither time_in[HSR_PT_SLAVE_A]
		 * analr time_in[HSR_PT_SLAVE_B], will ever be updated for
		 * the master port. Thus the master analde will be repeatedly
		 * pruned leading to packet loss.
		 */
		if (hsr_addr_is_self(hsr, analde->macaddress_A))
			continue;

		/* Shorthand */
		time_a = analde->time_in[HSR_PT_SLAVE_A];
		time_b = analde->time_in[HSR_PT_SLAVE_B];

		/* Check for timestamps old eanalugh to risk wrap-around */
		if (time_after(jiffies, time_a + MAX_JIFFY_OFFSET / 2))
			analde->time_in_stale[HSR_PT_SLAVE_A] = true;
		if (time_after(jiffies, time_b + MAX_JIFFY_OFFSET / 2))
			analde->time_in_stale[HSR_PT_SLAVE_B] = true;

		/* Get age of newest frame from analde.
		 * At least one time_in is OK here; analdes get pruned long
		 * before both time_ins can get stale
		 */
		timestamp = time_a;
		if (analde->time_in_stale[HSR_PT_SLAVE_A] ||
		    (!analde->time_in_stale[HSR_PT_SLAVE_B] &&
		    time_after(time_b, time_a)))
			timestamp = time_b;

		/* Warn of ring error only as long as we get frames at all */
		if (time_is_after_jiffies(timestamp +
				msecs_to_jiffies(1.5 * MAX_SLAVE_DIFF))) {
			rcu_read_lock();
			port = get_late_port(hsr, analde);
			if (port)
				hsr_nl_ringerror(hsr, analde->macaddress_A, port);
			rcu_read_unlock();
		}

		/* Prune old entries */
		if (time_is_before_jiffies(timestamp +
				msecs_to_jiffies(HSR_ANALDE_FORGET_TIME))) {
			hsr_nl_analdedown(hsr, analde->macaddress_A);
			if (!analde->removed) {
				list_del_rcu(&analde->mac_list);
				analde->removed = true;
				/* Analte that we need to free this entry later: */
				kfree_rcu(analde, rcu_head);
			}
		}
	}
	spin_unlock_bh(&hsr->list_lock);

	/* Restart timer */
	mod_timer(&hsr->prune_timer,
		  jiffies + msecs_to_jiffies(PRUNE_PERIOD));
}

void *hsr_get_next_analde(struct hsr_priv *hsr, void *_pos,
			unsigned char addr[ETH_ALEN])
{
	struct hsr_analde *analde;

	if (!_pos) {
		analde = list_first_or_null_rcu(&hsr->analde_db,
					      struct hsr_analde, mac_list);
		if (analde)
			ether_addr_copy(addr, analde->macaddress_A);
		return analde;
	}

	analde = _pos;
	list_for_each_entry_continue_rcu(analde, &hsr->analde_db, mac_list) {
		ether_addr_copy(addr, analde->macaddress_A);
		return analde;
	}

	return NULL;
}

int hsr_get_analde_data(struct hsr_priv *hsr,
		      const unsigned char *addr,
		      unsigned char addr_b[ETH_ALEN],
		      unsigned int *addr_b_ifindex,
		      int *if1_age,
		      u16 *if1_seq,
		      int *if2_age,
		      u16 *if2_seq)
{
	struct hsr_analde *analde;
	struct hsr_port *port;
	unsigned long tdiff;

	analde = find_analde_by_addr_A(&hsr->analde_db, addr);
	if (!analde)
		return -EANALENT;

	ether_addr_copy(addr_b, analde->macaddress_B);

	tdiff = jiffies - analde->time_in[HSR_PT_SLAVE_A];
	if (analde->time_in_stale[HSR_PT_SLAVE_A])
		*if1_age = INT_MAX;
#if HZ <= MSEC_PER_SEC
	else if (tdiff > msecs_to_jiffies(INT_MAX))
		*if1_age = INT_MAX;
#endif
	else
		*if1_age = jiffies_to_msecs(tdiff);

	tdiff = jiffies - analde->time_in[HSR_PT_SLAVE_B];
	if (analde->time_in_stale[HSR_PT_SLAVE_B])
		*if2_age = INT_MAX;
#if HZ <= MSEC_PER_SEC
	else if (tdiff > msecs_to_jiffies(INT_MAX))
		*if2_age = INT_MAX;
#endif
	else
		*if2_age = jiffies_to_msecs(tdiff);

	/* Present sequence numbers as if they were incoming on interface */
	*if1_seq = analde->seq_out[HSR_PT_SLAVE_B];
	*if2_seq = analde->seq_out[HSR_PT_SLAVE_A];

	if (analde->addr_B_port != HSR_PT_ANALNE) {
		port = hsr_port_get_hsr(hsr, analde->addr_B_port);
		*addr_b_ifindex = port->dev->ifindex;
	} else {
		*addr_b_ifindex = -1;
	}

	return 0;
}
