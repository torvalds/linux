/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LLC_H
#define LLC_H
/*
 * Copyright (c) 1997 by Procom Technology, Inc.
 * 		 2001-2003 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 */

#include <linux/list.h>
#include <linux/refcount.h>

struct net_device;
struct packet_type;
struct sk_buff;

/**
 * struct llc_sap - Defines the SAP component
 *
 * @refcnt: reference count
 * @lsap: SAP number
 * @rcv_func: handler for the PDUs addressed to this SAP
 * @node: entry in the SAP list
 * @rcu: for deferred freeing
 */
struct llc_sap {
	refcount_t		 refcnt;
	unsigned char	 lsap;
	int		 (*rcv_func)(struct sk_buff *skb,
				     struct net_device *dev,
				     struct packet_type *pt,
				     struct net_device *orig_dev);
	struct list_head node;
	struct rcu_head rcu;
};

int llc_rcv(struct sk_buff *skb, struct net_device *dev, struct packet_type *pt,
	    struct net_device *orig_dev);

int llc_mac_hdr_init(struct sk_buff *skb, const unsigned char *sa,
		     const unsigned char *da);

struct llc_sap *llc_sap_open(unsigned char lsap,
			     int (*rcv)(struct sk_buff *skb,
					struct net_device *dev,
					struct packet_type *pt,
					struct net_device *orig_dev));

static inline bool llc_sap_hold_safe(struct llc_sap *sap)
{
	return refcount_inc_not_zero(&sap->refcnt);
}

void llc_sap_close(struct llc_sap *sap);

static inline void llc_sap_put(struct llc_sap *sap)
{
	if (refcount_dec_and_test(&sap->refcnt))
		llc_sap_close(sap);
}

struct llc_sap *llc_sap_find(unsigned char sap_value);

int llc_build_and_send_ui_pkt(struct llc_sap *sap, struct sk_buff *skb,
			      const unsigned char *dmac, unsigned char dsap);
#endif /* LLC_H */
