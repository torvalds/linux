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
 */

#include <linux/if_ether.h>
#include <linux/etherdevice.h>
#include <linux/slab.h>
#include <linux/rculist.h>
#include "hsr_main.h"
#include "hsr_framereg.h"
#include "hsr_netlink.h"

/*	TODO: use hash lists for mac addresses (linux/jhash.h)?    */

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
#define seq_nr_after_or_eq(a, b)	(!seq_nr_before((a), (b)))
#define seq_nr_before_or_eq(a, b)	(!seq_nr_after((a), (b)))

bool hsr_addr_is_self(struct hsr_priv *hsr, unsigned char *addr)
{
	struct hsr_yesde *yesde;

	yesde = list_first_or_null_rcu(&hsr->self_yesde_db, struct hsr_yesde,
				      mac_list);
	if (!yesde) {
		WARN_ONCE(1, "HSR: No self yesde\n");
		return false;
	}

	if (ether_addr_equal(addr, yesde->macaddress_A))
		return true;
	if (ether_addr_equal(addr, yesde->macaddress_B))
		return true;

	return false;
}

/* Search for mac entry. Caller must hold rcu read lock.
 */
static struct hsr_yesde *find_yesde_by_addr_A(struct list_head *yesde_db,
					    const unsigned char addr[ETH_ALEN])
{
	struct hsr_yesde *yesde;

	list_for_each_entry_rcu(yesde, yesde_db, mac_list) {
		if (ether_addr_equal(yesde->macaddress_A, addr))
			return yesde;
	}

	return NULL;
}

/* Helper for device init; the self_yesde_db is used in hsr_rcv() to recognize
 * frames from self that's been looped over the HSR ring.
 */
int hsr_create_self_yesde(struct hsr_priv *hsr,
			 unsigned char addr_a[ETH_ALEN],
			 unsigned char addr_b[ETH_ALEN])
{
	struct list_head *self_yesde_db = &hsr->self_yesde_db;
	struct hsr_yesde *yesde, *oldyesde;

	yesde = kmalloc(sizeof(*yesde), GFP_KERNEL);
	if (!yesde)
		return -ENOMEM;

	ether_addr_copy(yesde->macaddress_A, addr_a);
	ether_addr_copy(yesde->macaddress_B, addr_b);

	spin_lock_bh(&hsr->list_lock);
	oldyesde = list_first_or_null_rcu(self_yesde_db,
					 struct hsr_yesde, mac_list);
	if (oldyesde) {
		list_replace_rcu(&oldyesde->mac_list, &yesde->mac_list);
		spin_unlock_bh(&hsr->list_lock);
		kfree_rcu(oldyesde, rcu_head);
	} else {
		list_add_tail_rcu(&yesde->mac_list, self_yesde_db);
		spin_unlock_bh(&hsr->list_lock);
	}

	return 0;
}

void hsr_del_self_yesde(struct hsr_priv *hsr)
{
	struct list_head *self_yesde_db = &hsr->self_yesde_db;
	struct hsr_yesde *yesde;

	spin_lock_bh(&hsr->list_lock);
	yesde = list_first_or_null_rcu(self_yesde_db, struct hsr_yesde, mac_list);
	if (yesde) {
		list_del_rcu(&yesde->mac_list);
		kfree_rcu(yesde, rcu_head);
	}
	spin_unlock_bh(&hsr->list_lock);
}

void hsr_del_yesdes(struct list_head *yesde_db)
{
	struct hsr_yesde *yesde;
	struct hsr_yesde *tmp;

	list_for_each_entry_safe(yesde, tmp, yesde_db, mac_list)
		kfree(yesde);
}

/* Allocate an hsr_yesde and add it to yesde_db. 'addr' is the yesde's address_A;
 * seq_out is used to initialize filtering of outgoing duplicate frames
 * originating from the newly added yesde.
 */
static struct hsr_yesde *hsr_add_yesde(struct hsr_priv *hsr,
				     struct list_head *yesde_db,
				     unsigned char addr[],
				     u16 seq_out)
{
	struct hsr_yesde *new_yesde, *yesde;
	unsigned long yesw;
	int i;

	new_yesde = kzalloc(sizeof(*new_yesde), GFP_ATOMIC);
	if (!new_yesde)
		return NULL;

	ether_addr_copy(new_yesde->macaddress_A, addr);

	/* We are only interested in time diffs here, so use current jiffies
	 * as initialization. (0 could trigger an spurious ring error warning).
	 */
	yesw = jiffies;
	for (i = 0; i < HSR_PT_PORTS; i++)
		new_yesde->time_in[i] = yesw;
	for (i = 0; i < HSR_PT_PORTS; i++)
		new_yesde->seq_out[i] = seq_out;

	spin_lock_bh(&hsr->list_lock);
	list_for_each_entry_rcu(yesde, yesde_db, mac_list) {
		if (ether_addr_equal(yesde->macaddress_A, addr))
			goto out;
		if (ether_addr_equal(yesde->macaddress_B, addr))
			goto out;
	}
	list_add_tail_rcu(&new_yesde->mac_list, yesde_db);
	spin_unlock_bh(&hsr->list_lock);
	return new_yesde;
out:
	spin_unlock_bh(&hsr->list_lock);
	kfree(new_yesde);
	return yesde;
}

/* Get the hsr_yesde from which 'skb' was sent.
 */
struct hsr_yesde *hsr_get_yesde(struct hsr_port *port, struct sk_buff *skb,
			      bool is_sup)
{
	struct list_head *yesde_db = &port->hsr->yesde_db;
	struct hsr_priv *hsr = port->hsr;
	struct hsr_yesde *yesde;
	struct ethhdr *ethhdr;
	u16 seq_out;

	if (!skb_mac_header_was_set(skb))
		return NULL;

	ethhdr = (struct ethhdr *)skb_mac_header(skb);

	list_for_each_entry_rcu(yesde, yesde_db, mac_list) {
		if (ether_addr_equal(yesde->macaddress_A, ethhdr->h_source))
			return yesde;
		if (ether_addr_equal(yesde->macaddress_B, ethhdr->h_source))
			return yesde;
	}

	/* Everyone may create a yesde entry, connected yesde to a HSR device. */

	if (ethhdr->h_proto == htons(ETH_P_PRP) ||
	    ethhdr->h_proto == htons(ETH_P_HSR)) {
		/* Use the existing sequence_nr from the tag as starting point
		 * for filtering duplicate frames.
		 */
		seq_out = hsr_get_skb_sequence_nr(skb) - 1;
	} else {
		/* this is called also for frames from master port and
		 * so warn only for yesn master ports
		 */
		if (port->type != HSR_PT_MASTER)
			WARN_ONCE(1, "%s: Non-HSR frame\n", __func__);
		seq_out = HSR_SEQNR_START;
	}

	return hsr_add_yesde(hsr, yesde_db, ethhdr->h_source, seq_out);
}

/* Use the Supervision frame's info about an eventual macaddress_B for merging
 * yesdes that has previously had their macaddress_B registered as a separate
 * yesde.
 */
void hsr_handle_sup_frame(struct sk_buff *skb, struct hsr_yesde *yesde_curr,
			  struct hsr_port *port_rcv)
{
	struct hsr_priv *hsr = port_rcv->hsr;
	struct hsr_sup_payload *hsr_sp;
	struct hsr_yesde *yesde_real;
	struct list_head *yesde_db;
	struct ethhdr *ethhdr;
	int i;

	ethhdr = (struct ethhdr *)skb_mac_header(skb);

	/* Leave the ethernet header. */
	skb_pull(skb, sizeof(struct ethhdr));

	/* And leave the HSR tag. */
	if (ethhdr->h_proto == htons(ETH_P_HSR))
		skb_pull(skb, sizeof(struct hsr_tag));

	/* And leave the HSR sup tag. */
	skb_pull(skb, sizeof(struct hsr_sup_tag));

	hsr_sp = (struct hsr_sup_payload *)skb->data;

	/* Merge yesde_curr (registered on macaddress_B) into yesde_real */
	yesde_db = &port_rcv->hsr->yesde_db;
	yesde_real = find_yesde_by_addr_A(yesde_db, hsr_sp->macaddress_A);
	if (!yesde_real)
		/* No frame received from AddrA of this yesde yet */
		yesde_real = hsr_add_yesde(hsr, yesde_db, hsr_sp->macaddress_A,
					 HSR_SEQNR_START - 1);
	if (!yesde_real)
		goto done; /* No mem */
	if (yesde_real == yesde_curr)
		/* Node has already been merged */
		goto done;

	ether_addr_copy(yesde_real->macaddress_B, ethhdr->h_source);
	for (i = 0; i < HSR_PT_PORTS; i++) {
		if (!yesde_curr->time_in_stale[i] &&
		    time_after(yesde_curr->time_in[i], yesde_real->time_in[i])) {
			yesde_real->time_in[i] = yesde_curr->time_in[i];
			yesde_real->time_in_stale[i] =
						yesde_curr->time_in_stale[i];
		}
		if (seq_nr_after(yesde_curr->seq_out[i], yesde_real->seq_out[i]))
			yesde_real->seq_out[i] = yesde_curr->seq_out[i];
	}
	yesde_real->addr_B_port = port_rcv->type;

	spin_lock_bh(&hsr->list_lock);
	list_del_rcu(&yesde_curr->mac_list);
	spin_unlock_bh(&hsr->list_lock);
	kfree_rcu(yesde_curr, rcu_head);

done:
	skb_push(skb, sizeof(struct hsrv1_ethhdr_sp));
}

/* 'skb' is a frame meant for this host, that is to be passed to upper layers.
 *
 * If the frame was sent by a yesde's B interface, replace the source
 * address with that yesde's "official" address (macaddress_A) so that upper
 * layers recognize where it came from.
 */
void hsr_addr_subst_source(struct hsr_yesde *yesde, struct sk_buff *skb)
{
	if (!skb_mac_header_was_set(skb)) {
		WARN_ONCE(1, "%s: Mac header yest set\n", __func__);
		return;
	}

	memcpy(&eth_hdr(skb)->h_source, yesde->macaddress_A, ETH_ALEN);
}

/* 'skb' is a frame meant for ayesther host.
 * 'port' is the outgoing interface
 *
 * Substitute the target (dest) MAC address if necessary, so the it matches the
 * recipient interface MAC address, regardless of whether that is the
 * recipient's A or B interface.
 * This is needed to keep the packets flowing through switches that learn on
 * which "side" the different interfaces are.
 */
void hsr_addr_subst_dest(struct hsr_yesde *yesde_src, struct sk_buff *skb,
			 struct hsr_port *port)
{
	struct hsr_yesde *yesde_dst;

	if (!skb_mac_header_was_set(skb)) {
		WARN_ONCE(1, "%s: Mac header yest set\n", __func__);
		return;
	}

	if (!is_unicast_ether_addr(eth_hdr(skb)->h_dest))
		return;

	yesde_dst = find_yesde_by_addr_A(&port->hsr->yesde_db,
				       eth_hdr(skb)->h_dest);
	if (!yesde_dst) {
		WARN_ONCE(1, "%s: Unkyeswn yesde\n", __func__);
		return;
	}
	if (port->type != yesde_dst->addr_B_port)
		return;

	ether_addr_copy(eth_hdr(skb)->h_dest, yesde_dst->macaddress_B);
}

void hsr_register_frame_in(struct hsr_yesde *yesde, struct hsr_port *port,
			   u16 sequence_nr)
{
	/* Don't register incoming frames without a valid sequence number. This
	 * ensures entries of restarted yesdes gets pruned so that they can
	 * re-register and resume communications.
	 */
	if (seq_nr_before(sequence_nr, yesde->seq_out[port->type]))
		return;

	yesde->time_in[port->type] = jiffies;
	yesde->time_in_stale[port->type] = false;
}

/* 'skb' is a HSR Ethernet frame (with a HSR tag inserted), with a valid
 * ethhdr->h_source address and skb->mac_header set.
 *
 * Return:
 *	 1 if frame can be shown to have been sent recently on this interface,
 *	 0 otherwise, or
 *	 negative error code on error
 */
int hsr_register_frame_out(struct hsr_port *port, struct hsr_yesde *yesde,
			   u16 sequence_nr)
{
	if (seq_nr_before_or_eq(sequence_nr, yesde->seq_out[port->type]))
		return 1;

	yesde->seq_out[port->type] = sequence_nr;
	return 0;
}

static struct hsr_port *get_late_port(struct hsr_priv *hsr,
				      struct hsr_yesde *yesde)
{
	if (yesde->time_in_stale[HSR_PT_SLAVE_A])
		return hsr_port_get_hsr(hsr, HSR_PT_SLAVE_A);
	if (yesde->time_in_stale[HSR_PT_SLAVE_B])
		return hsr_port_get_hsr(hsr, HSR_PT_SLAVE_B);

	if (time_after(yesde->time_in[HSR_PT_SLAVE_B],
		       yesde->time_in[HSR_PT_SLAVE_A] +
					msecs_to_jiffies(MAX_SLAVE_DIFF)))
		return hsr_port_get_hsr(hsr, HSR_PT_SLAVE_A);
	if (time_after(yesde->time_in[HSR_PT_SLAVE_A],
		       yesde->time_in[HSR_PT_SLAVE_B] +
					msecs_to_jiffies(MAX_SLAVE_DIFF)))
		return hsr_port_get_hsr(hsr, HSR_PT_SLAVE_B);

	return NULL;
}

/* Remove stale sequence_nr records. Called by timer every
 * HSR_LIFE_CHECK_INTERVAL (two seconds or so).
 */
void hsr_prune_yesdes(struct timer_list *t)
{
	struct hsr_priv *hsr = from_timer(hsr, t, prune_timer);
	struct hsr_yesde *yesde;
	struct hsr_yesde *tmp;
	struct hsr_port *port;
	unsigned long timestamp;
	unsigned long time_a, time_b;

	spin_lock_bh(&hsr->list_lock);
	list_for_each_entry_safe(yesde, tmp, &hsr->yesde_db, mac_list) {
		/* Don't prune own yesde. Neither time_in[HSR_PT_SLAVE_A]
		 * yesr time_in[HSR_PT_SLAVE_B], will ever be updated for
		 * the master port. Thus the master yesde will be repeatedly
		 * pruned leading to packet loss.
		 */
		if (hsr_addr_is_self(hsr, yesde->macaddress_A))
			continue;

		/* Shorthand */
		time_a = yesde->time_in[HSR_PT_SLAVE_A];
		time_b = yesde->time_in[HSR_PT_SLAVE_B];

		/* Check for timestamps old eyesugh to risk wrap-around */
		if (time_after(jiffies, time_a + MAX_JIFFY_OFFSET / 2))
			yesde->time_in_stale[HSR_PT_SLAVE_A] = true;
		if (time_after(jiffies, time_b + MAX_JIFFY_OFFSET / 2))
			yesde->time_in_stale[HSR_PT_SLAVE_B] = true;

		/* Get age of newest frame from yesde.
		 * At least one time_in is OK here; yesdes get pruned long
		 * before both time_ins can get stale
		 */
		timestamp = time_a;
		if (yesde->time_in_stale[HSR_PT_SLAVE_A] ||
		    (!yesde->time_in_stale[HSR_PT_SLAVE_B] &&
		    time_after(time_b, time_a)))
			timestamp = time_b;

		/* Warn of ring error only as long as we get frames at all */
		if (time_is_after_jiffies(timestamp +
				msecs_to_jiffies(1.5 * MAX_SLAVE_DIFF))) {
			rcu_read_lock();
			port = get_late_port(hsr, yesde);
			if (port)
				hsr_nl_ringerror(hsr, yesde->macaddress_A, port);
			rcu_read_unlock();
		}

		/* Prune old entries */
		if (time_is_before_jiffies(timestamp +
				msecs_to_jiffies(HSR_NODE_FORGET_TIME))) {
			hsr_nl_yesdedown(hsr, yesde->macaddress_A);
			list_del_rcu(&yesde->mac_list);
			/* Note that we need to free this entry later: */
			kfree_rcu(yesde, rcu_head);
		}
	}
	spin_unlock_bh(&hsr->list_lock);

	/* Restart timer */
	mod_timer(&hsr->prune_timer,
		  jiffies + msecs_to_jiffies(PRUNE_PERIOD));
}

void *hsr_get_next_yesde(struct hsr_priv *hsr, void *_pos,
			unsigned char addr[ETH_ALEN])
{
	struct hsr_yesde *yesde;

	if (!_pos) {
		yesde = list_first_or_null_rcu(&hsr->yesde_db,
					      struct hsr_yesde, mac_list);
		if (yesde)
			ether_addr_copy(addr, yesde->macaddress_A);
		return yesde;
	}

	yesde = _pos;
	list_for_each_entry_continue_rcu(yesde, &hsr->yesde_db, mac_list) {
		ether_addr_copy(addr, yesde->macaddress_A);
		return yesde;
	}

	return NULL;
}

int hsr_get_yesde_data(struct hsr_priv *hsr,
		      const unsigned char *addr,
		      unsigned char addr_b[ETH_ALEN],
		      unsigned int *addr_b_ifindex,
		      int *if1_age,
		      u16 *if1_seq,
		      int *if2_age,
		      u16 *if2_seq)
{
	struct hsr_yesde *yesde;
	struct hsr_port *port;
	unsigned long tdiff;

	rcu_read_lock();
	yesde = find_yesde_by_addr_A(&hsr->yesde_db, addr);
	if (!yesde) {
		rcu_read_unlock();
		return -ENOENT;	/* No such entry */
	}

	ether_addr_copy(addr_b, yesde->macaddress_B);

	tdiff = jiffies - yesde->time_in[HSR_PT_SLAVE_A];
	if (yesde->time_in_stale[HSR_PT_SLAVE_A])
		*if1_age = INT_MAX;
#if HZ <= MSEC_PER_SEC
	else if (tdiff > msecs_to_jiffies(INT_MAX))
		*if1_age = INT_MAX;
#endif
	else
		*if1_age = jiffies_to_msecs(tdiff);

	tdiff = jiffies - yesde->time_in[HSR_PT_SLAVE_B];
	if (yesde->time_in_stale[HSR_PT_SLAVE_B])
		*if2_age = INT_MAX;
#if HZ <= MSEC_PER_SEC
	else if (tdiff > msecs_to_jiffies(INT_MAX))
		*if2_age = INT_MAX;
#endif
	else
		*if2_age = jiffies_to_msecs(tdiff);

	/* Present sequence numbers as if they were incoming on interface */
	*if1_seq = yesde->seq_out[HSR_PT_SLAVE_B];
	*if2_seq = yesde->seq_out[HSR_PT_SLAVE_A];

	if (yesde->addr_B_port != HSR_PT_NONE) {
		port = hsr_port_get_hsr(hsr, yesde->addr_B_port);
		*addr_b_ifindex = port->dev->ifindex;
	} else {
		*addr_b_ifindex = -1;
	}

	rcu_read_unlock();

	return 0;
}
