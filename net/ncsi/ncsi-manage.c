/*
 * Copyright Gavin Shan, IBM Corporation 2016.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>

#include <net/ncsi.h>
#include <net/net_namespace.h>
#include <net/sock.h>

#include "internal.h"

LIST_HEAD(ncsi_dev_list);
DEFINE_SPINLOCK(ncsi_dev_lock);

static inline int ncsi_filter_size(int table)
{
	int sizes[] = { 2, 6, 6, 6 };

	BUILD_BUG_ON(ARRAY_SIZE(sizes) != NCSI_FILTER_MAX);
	if (table < NCSI_FILTER_BASE || table >= NCSI_FILTER_MAX)
		return -EINVAL;

	return sizes[table];
}

int ncsi_find_filter(struct ncsi_channel *nc, int table, void *data)
{
	struct ncsi_channel_filter *ncf;
	void *bitmap;
	int index, size;
	unsigned long flags;

	ncf = nc->filters[table];
	if (!ncf)
		return -ENXIO;

	size = ncsi_filter_size(table);
	if (size < 0)
		return size;

	spin_lock_irqsave(&nc->lock, flags);
	bitmap = (void *)&ncf->bitmap;
	index = -1;
	while ((index = find_next_bit(bitmap, ncf->total, index + 1))
	       < ncf->total) {
		if (!memcmp(ncf->data + size * index, data, size)) {
			spin_unlock_irqrestore(&nc->lock, flags);
			return index;
		}
	}
	spin_unlock_irqrestore(&nc->lock, flags);

	return -ENOENT;
}

int ncsi_add_filter(struct ncsi_channel *nc, int table, void *data)
{
	struct ncsi_channel_filter *ncf;
	int index, size;
	void *bitmap;
	unsigned long flags;

	size = ncsi_filter_size(table);
	if (size < 0)
		return size;

	index = ncsi_find_filter(nc, table, data);
	if (index >= 0)
		return index;

	ncf = nc->filters[table];
	if (!ncf)
		return -ENODEV;

	spin_lock_irqsave(&nc->lock, flags);
	bitmap = (void *)&ncf->bitmap;
	do {
		index = find_next_zero_bit(bitmap, ncf->total, 0);
		if (index >= ncf->total) {
			spin_unlock_irqrestore(&nc->lock, flags);
			return -ENOSPC;
		}
	} while (test_and_set_bit(index, bitmap));

	memcpy(ncf->data + size * index, data, size);
	spin_unlock_irqrestore(&nc->lock, flags);

	return index;
}

int ncsi_remove_filter(struct ncsi_channel *nc, int table, int index)
{
	struct ncsi_channel_filter *ncf;
	int size;
	void *bitmap;
	unsigned long flags;

	size = ncsi_filter_size(table);
	if (size < 0)
		return size;

	ncf = nc->filters[table];
	if (!ncf || index >= ncf->total)
		return -ENODEV;

	spin_lock_irqsave(&nc->lock, flags);
	bitmap = (void *)&ncf->bitmap;
	if (test_and_clear_bit(index, bitmap))
		memset(ncf->data + size * index, 0, size);
	spin_unlock_irqrestore(&nc->lock, flags);

	return 0;
}

struct ncsi_channel *ncsi_find_channel(struct ncsi_package *np,
				       unsigned char id)
{
	struct ncsi_channel *nc;

	NCSI_FOR_EACH_CHANNEL(np, nc) {
		if (nc->id == id)
			return nc;
	}

	return NULL;
}

struct ncsi_channel *ncsi_add_channel(struct ncsi_package *np, unsigned char id)
{
	struct ncsi_channel *nc, *tmp;
	int index;
	unsigned long flags;

	nc = kzalloc(sizeof(*nc), GFP_ATOMIC);
	if (!nc)
		return NULL;

	nc->id = id;
	nc->package = np;
	nc->state = NCSI_CHANNEL_INACTIVE;
	spin_lock_init(&nc->lock);
	for (index = 0; index < NCSI_CAP_MAX; index++)
		nc->caps[index].index = index;
	for (index = 0; index < NCSI_MODE_MAX; index++)
		nc->modes[index].index = index;

	spin_lock_irqsave(&np->lock, flags);
	tmp = ncsi_find_channel(np, id);
	if (tmp) {
		spin_unlock_irqrestore(&np->lock, flags);
		kfree(nc);
		return tmp;
	}

	list_add_tail_rcu(&nc->node, &np->channels);
	np->channel_num++;
	spin_unlock_irqrestore(&np->lock, flags);

	return nc;
}

static void ncsi_remove_channel(struct ncsi_channel *nc)
{
	struct ncsi_package *np = nc->package;
	struct ncsi_channel_filter *ncf;
	unsigned long flags;
	int i;

	/* Release filters */
	spin_lock_irqsave(&nc->lock, flags);
	for (i = 0; i < NCSI_FILTER_MAX; i++) {
		ncf = nc->filters[i];
		if (!ncf)
			continue;

		nc->filters[i] = NULL;
		kfree(ncf);
	}

	nc->state = NCSI_CHANNEL_INACTIVE;
	spin_unlock_irqrestore(&nc->lock, flags);

	/* Remove and free channel */
	spin_lock_irqsave(&np->lock, flags);
	list_del_rcu(&nc->node);
	np->channel_num--;
	spin_unlock_irqrestore(&np->lock, flags);

	kfree(nc);
}

struct ncsi_package *ncsi_find_package(struct ncsi_dev_priv *ndp,
				       unsigned char id)
{
	struct ncsi_package *np;

	NCSI_FOR_EACH_PACKAGE(ndp, np) {
		if (np->id == id)
			return np;
	}

	return NULL;
}

struct ncsi_package *ncsi_add_package(struct ncsi_dev_priv *ndp,
				      unsigned char id)
{
	struct ncsi_package *np, *tmp;
	unsigned long flags;

	np = kzalloc(sizeof(*np), GFP_ATOMIC);
	if (!np)
		return NULL;

	np->id = id;
	np->ndp = ndp;
	spin_lock_init(&np->lock);
	INIT_LIST_HEAD(&np->channels);

	spin_lock_irqsave(&ndp->lock, flags);
	tmp = ncsi_find_package(ndp, id);
	if (tmp) {
		spin_unlock_irqrestore(&ndp->lock, flags);
		kfree(np);
		return tmp;
	}

	list_add_tail_rcu(&np->node, &ndp->packages);
	ndp->package_num++;
	spin_unlock_irqrestore(&ndp->lock, flags);

	return np;
}

void ncsi_remove_package(struct ncsi_package *np)
{
	struct ncsi_dev_priv *ndp = np->ndp;
	struct ncsi_channel *nc, *tmp;
	unsigned long flags;

	/* Release all child channels */
	list_for_each_entry_safe(nc, tmp, &np->channels, node)
		ncsi_remove_channel(nc);

	/* Remove and free package */
	spin_lock_irqsave(&ndp->lock, flags);
	list_del_rcu(&np->node);
	ndp->package_num--;
	spin_unlock_irqrestore(&ndp->lock, flags);

	kfree(np);
}

void ncsi_find_package_and_channel(struct ncsi_dev_priv *ndp,
				   unsigned char id,
				   struct ncsi_package **np,
				   struct ncsi_channel **nc)
{
	struct ncsi_package *p;
	struct ncsi_channel *c;

	p = ncsi_find_package(ndp, NCSI_PACKAGE_INDEX(id));
	c = p ? ncsi_find_channel(p, NCSI_CHANNEL_INDEX(id)) : NULL;

	if (np)
		*np = p;
	if (nc)
		*nc = c;
}

/* For two consecutive NCSI commands, the packet IDs shouldn't
 * be same. Otherwise, the bogus response might be replied. So
 * the available IDs are allocated in round-robin fashion.
 */
struct ncsi_request *ncsi_alloc_request(struct ncsi_dev_priv *ndp, bool driven)
{
	struct ncsi_request *nr = NULL;
	int i, limit = ARRAY_SIZE(ndp->requests);
	unsigned long flags;

	/* Check if there is one available request until the ceiling */
	spin_lock_irqsave(&ndp->lock, flags);
	for (i = ndp->request_id; !nr && i < limit; i++) {
		if (ndp->requests[i].used)
			continue;

		nr = &ndp->requests[i];
		nr->used = true;
		nr->driven = driven;
		if (++ndp->request_id >= limit)
			ndp->request_id = 0;
	}

	/* Fail back to check from the starting cursor */
	for (i = 0; !nr && i < ndp->request_id; i++) {
		if (ndp->requests[i].used)
			continue;

		nr = &ndp->requests[i];
		nr->used = true;
		nr->driven = driven;
		if (++ndp->request_id >= limit)
			ndp->request_id = 0;
	}
	spin_unlock_irqrestore(&ndp->lock, flags);

	return nr;
}

void ncsi_free_request(struct ncsi_request *nr)
{
	struct ncsi_dev_priv *ndp = nr->ndp;
	struct sk_buff *cmd, *rsp;
	unsigned long flags;

	if (nr->enabled) {
		nr->enabled = false;
		del_timer_sync(&nr->timer);
	}

	spin_lock_irqsave(&ndp->lock, flags);
	cmd = nr->cmd;
	rsp = nr->rsp;
	nr->cmd = NULL;
	nr->rsp = NULL;
	nr->used = false;
	spin_unlock_irqrestore(&ndp->lock, flags);

	/* Release command and response */
	consume_skb(cmd);
	consume_skb(rsp);
}

struct ncsi_dev *ncsi_find_dev(struct net_device *dev)
{
	struct ncsi_dev_priv *ndp;

	NCSI_FOR_EACH_DEV(ndp) {
		if (ndp->ndev.dev == dev)
			return &ndp->ndev;
	}

	return NULL;
}

static void ncsi_request_timeout(unsigned long data)
{
	struct ncsi_request *nr = (struct ncsi_request *)data;
	struct ncsi_dev_priv *ndp = nr->ndp;
	unsigned long flags;

	/* If the request already had associated response,
	 * let the response handler to release it.
	 */
	spin_lock_irqsave(&ndp->lock, flags);
	nr->enabled = false;
	if (nr->rsp || !nr->cmd) {
		spin_unlock_irqrestore(&ndp->lock, flags);
		return;
	}
	spin_unlock_irqrestore(&ndp->lock, flags);

	/* Release the request */
	ncsi_free_request(nr);
}

struct ncsi_dev *ncsi_register_dev(struct net_device *dev,
				   void (*handler)(struct ncsi_dev *ndev))
{
	struct ncsi_dev_priv *ndp;
	struct ncsi_dev *nd;
	unsigned long flags;
	int i;

	/* Check if the device has been registered or not */
	nd = ncsi_find_dev(dev);
	if (nd)
		return nd;

	/* Create NCSI device */
	ndp = kzalloc(sizeof(*ndp), GFP_ATOMIC);
	if (!ndp)
		return NULL;

	nd = &ndp->ndev;
	nd->state = ncsi_dev_state_registered;
	nd->dev = dev;
	nd->handler = handler;

	/* Initialize private NCSI device */
	spin_lock_init(&ndp->lock);
	INIT_LIST_HEAD(&ndp->packages);
	ndp->request_id = 0;
	for (i = 0; i < ARRAY_SIZE(ndp->requests); i++) {
		ndp->requests[i].id = i;
		ndp->requests[i].ndp = ndp;
		setup_timer(&ndp->requests[i].timer,
			    ncsi_request_timeout,
			    (unsigned long)&ndp->requests[i]);
	}

	spin_lock_irqsave(&ncsi_dev_lock, flags);
	list_add_tail_rcu(&ndp->node, &ncsi_dev_list);
	spin_unlock_irqrestore(&ncsi_dev_lock, flags);

	return nd;
}
EXPORT_SYMBOL_GPL(ncsi_register_dev);

void ncsi_unregister_dev(struct ncsi_dev *nd)
{
	struct ncsi_dev_priv *ndp = TO_NCSI_DEV_PRIV(nd);
	struct ncsi_package *np, *tmp;
	unsigned long flags;

	list_for_each_entry_safe(np, tmp, &ndp->packages, node)
		ncsi_remove_package(np);

	spin_lock_irqsave(&ncsi_dev_lock, flags);
	list_del_rcu(&ndp->node);
	spin_unlock_irqrestore(&ncsi_dev_lock, flags);

	kfree(ndp);
}
EXPORT_SYMBOL_GPL(ncsi_unregister_dev);
