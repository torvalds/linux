/*
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	History
 *	03-01-2007	Added forwarding for x.25	Andrew Hendry
 */
#include <linux/if_arp.h>
#include <linux/init.h>
#include <net/x25.h>

struct list_head x25_forward_list = LIST_HEAD_INIT(x25_forward_list);
DEFINE_RWLOCK(x25_forward_list_lock);

int x25_forward_call(struct x25_address *dest_addr, struct x25_neigh *from,
			struct sk_buff *skb, int lci)
{
	struct x25_route *rt;
	struct x25_neigh *neigh_new = NULL;
	struct list_head *entry;
	struct x25_forward *x25_frwd, *new_frwd;
	struct sk_buff *skbn;
	short same_lci = 0;
	int rc = 0;

	if ((rt = x25_get_route(dest_addr)) == NULL)
		goto out_no_route;

	if ((neigh_new = x25_get_neigh(rt->dev)) == NULL) {
		/* This shouldnt happen, if it occurs somehow
		 * do something sensible
		 */
		goto out_put_route;
	}

	/* Avoid a loop. This is the normal exit path for a
	 * system with only one x.25 iface and default route
	 */
	if (rt->dev == from->dev) {
		goto out_put_nb;
	}

	/* Remote end sending a call request on an already
	 * established LCI? It shouldnt happen, just in case..
	 */
	read_lock_bh(&x25_forward_list_lock);
	list_for_each(entry, &x25_forward_list) {
		x25_frwd = list_entry(entry, struct x25_forward, node);
		if (x25_frwd->lci == lci) {
			printk(KERN_WARNING "X.25: call request for lci which is already registered!, transmitting but not registering new pair\n");
			same_lci = 1;
		}
	}
	read_unlock_bh(&x25_forward_list_lock);

	/* Save the forwarding details for future traffic */
	if (!same_lci){
		if ((new_frwd = kmalloc(sizeof(struct x25_forward),
						GFP_ATOMIC)) == NULL){
			rc = -ENOMEM;
			goto out_put_nb;
		}
		new_frwd->lci = lci;
		new_frwd->dev1 = rt->dev;
		new_frwd->dev2 = from->dev;
		write_lock_bh(&x25_forward_list_lock);
		list_add(&new_frwd->node, &x25_forward_list);
		write_unlock_bh(&x25_forward_list_lock);
	}

	/* Forward the call request */
	if ( (skbn = skb_clone(skb, GFP_ATOMIC)) == NULL){
		goto out_put_nb;
	}
	x25_transmit_link(skbn, neigh_new);
	rc = 1;


out_put_nb:
	x25_neigh_put(neigh_new);

out_put_route:
	x25_route_put(rt);

out_no_route:
	return rc;
}


int x25_forward_data(int lci, struct x25_neigh *from, struct sk_buff *skb) {

	struct x25_forward *frwd;
	struct list_head *entry;
	struct net_device *peer = NULL;
	struct x25_neigh *nb;
	struct sk_buff *skbn;
	int rc = 0;

	read_lock_bh(&x25_forward_list_lock);
	list_for_each(entry, &x25_forward_list) {
		frwd = list_entry(entry, struct x25_forward, node);
		if (frwd->lci == lci) {
			/* The call is established, either side can send */
			if (from->dev == frwd->dev1) {
				peer = frwd->dev2;
			} else {
				peer = frwd->dev1;
			}
			break;
		}
	}
	read_unlock_bh(&x25_forward_list_lock);

	if ( (nb = x25_get_neigh(peer)) == NULL)
		goto out;

	if ( (skbn = pskb_copy(skb, GFP_ATOMIC)) == NULL){
		goto output;

	}
	x25_transmit_link(skbn, nb);

	rc = 1;
output:
	x25_neigh_put(nb);
out:
	return rc;
}

void x25_clear_forward_by_lci(unsigned int lci)
{
	struct x25_forward *fwd;
	struct list_head *entry, *tmp;

	write_lock_bh(&x25_forward_list_lock);

	list_for_each_safe(entry, tmp, &x25_forward_list) {
		fwd = list_entry(entry, struct x25_forward, node);
		if (fwd->lci == lci) {
			list_del(&fwd->node);
			kfree(fwd);
		}
	}
	write_unlock_bh(&x25_forward_list_lock);
}


void x25_clear_forward_by_dev(struct net_device *dev)
{
	struct x25_forward *fwd;
	struct list_head *entry, *tmp;

	write_lock_bh(&x25_forward_list_lock);

	list_for_each_safe(entry, tmp, &x25_forward_list) {
		fwd = list_entry(entry, struct x25_forward, node);
		if ((fwd->dev1 == dev) || (fwd->dev2 == dev)){
			list_del(&fwd->node);
			kfree(fwd);
		}
	}
	write_unlock_bh(&x25_forward_list_lock);
}
