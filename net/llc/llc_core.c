/*
 * llc_core.c - Minimum needed routines for sap handling and module init/exit
 *
 * Copyright (c) 1997 by Procom Technology, Inc.
 * 		 2001-2003 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * This program can be redistributed or modified under the terms of the
 * GNU General Public License as published by the Free Software Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/if_ether.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/init.h>
#include <net/net_namespace.h>
#include <net/llc.h>

LIST_HEAD(llc_sap_list);
static DEFINE_SPINLOCK(llc_sap_list_lock);

/**
 *	llc_sap_alloc - allocates and initializes sap.
 *
 *	Allocates and initializes sap.
 */
static struct llc_sap *llc_sap_alloc(void)
{
	struct llc_sap *sap = kzalloc(sizeof(*sap), GFP_ATOMIC);
	int i;

	if (sap) {
		/* sap->laddr.mac - leave as a null, it's filled by bind */
		sap->state = LLC_SAP_STATE_ACTIVE;
		spin_lock_init(&sap->sk_lock);
		for (i = 0; i < LLC_SK_LADDR_HASH_ENTRIES; i++)
			INIT_HLIST_NULLS_HEAD(&sap->sk_laddr_hash[i], i);
		refcount_set(&sap->refcnt, 1);
	}
	return sap;
}

static struct llc_sap *__llc_sap_find(unsigned char sap_value)
{
	struct llc_sap *sap;

	list_for_each_entry(sap, &llc_sap_list, node)
		if (sap->laddr.lsap == sap_value)
			goto out;
	sap = NULL;
out:
	return sap;
}

/**
 *	llc_sap_find - searchs a SAP in station
 *	@sap_value: sap to be found
 *
 *	Searchs for a sap in the sap list of the LLC's station upon the sap ID.
 *	If the sap is found it will be refcounted and the user will have to do
 *	a llc_sap_put after use.
 *	Returns the sap or %NULL if not found.
 */
struct llc_sap *llc_sap_find(unsigned char sap_value)
{
	struct llc_sap *sap;

	rcu_read_lock_bh();
	sap = __llc_sap_find(sap_value);
	if (!sap || !llc_sap_hold_safe(sap))
		sap = NULL;
	rcu_read_unlock_bh();
	return sap;
}

/**
 *	llc_sap_open - open interface to the upper layers.
 *	@lsap: SAP number.
 *	@func: rcv func for datalink protos
 *
 *	Interface function to upper layer. Each one who wants to get a SAP
 *	(for example NetBEUI) should call this function. Returns the opened
 *	SAP for success, NULL for failure.
 */
struct llc_sap *llc_sap_open(unsigned char lsap,
			     int (*func)(struct sk_buff *skb,
					 struct net_device *dev,
					 struct packet_type *pt,
					 struct net_device *orig_dev))
{
	struct llc_sap *sap = NULL;

	spin_lock_bh(&llc_sap_list_lock);
	if (__llc_sap_find(lsap)) /* SAP already exists */
		goto out;
	sap = llc_sap_alloc();
	if (!sap)
		goto out;
	sap->laddr.lsap = lsap;
	sap->rcv_func	= func;
	list_add_tail_rcu(&sap->node, &llc_sap_list);
out:
	spin_unlock_bh(&llc_sap_list_lock);
	return sap;
}

/**
 *	llc_sap_close - close interface for upper layers.
 *	@sap: SAP to be closed.
 *
 *	Close interface function to upper layer. Each one who wants to
 *	close an open SAP (for example NetBEUI) should call this function.
 * 	Removes this sap from the list of saps in the station and then
 * 	frees the memory for this sap.
 */
void llc_sap_close(struct llc_sap *sap)
{
	WARN_ON(sap->sk_count);

	spin_lock_bh(&llc_sap_list_lock);
	list_del_rcu(&sap->node);
	spin_unlock_bh(&llc_sap_list_lock);

	kfree_rcu(sap, rcu);
}

static struct packet_type llc_packet_type __read_mostly = {
	.type = cpu_to_be16(ETH_P_802_2),
	.func = llc_rcv,
};

static struct packet_type llc_tr_packet_type __read_mostly = {
	.type = cpu_to_be16(ETH_P_TR_802_2),
	.func = llc_rcv,
};

static int __init llc_init(void)
{
	dev_add_pack(&llc_packet_type);
	dev_add_pack(&llc_tr_packet_type);
	return 0;
}

static void __exit llc_exit(void)
{
	dev_remove_pack(&llc_packet_type);
	dev_remove_pack(&llc_tr_packet_type);
}

module_init(llc_init);
module_exit(llc_exit);

EXPORT_SYMBOL(llc_sap_list);
EXPORT_SYMBOL(llc_sap_find);
EXPORT_SYMBOL(llc_sap_open);
EXPORT_SYMBOL(llc_sap_close);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Procom 1997, Jay Schullist 2001, Arnaldo C. Melo 2001-2003");
MODULE_DESCRIPTION("LLC IEEE 802.2 core support");
