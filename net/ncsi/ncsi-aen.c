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

#include <net/ncsi.h>
#include <net/net_namespace.h>
#include <net/sock.h>

#include "internal.h"
#include "ncsi-pkt.h"

static int ncsi_validate_aen_pkt(struct ncsi_aen_pkt_hdr *h,
				 const unsigned short payload)
{
	u32 checksum;
	__be32 *pchecksum;

	if (h->common.revision != NCSI_PKT_REVISION)
		return -EINVAL;
	if (ntohs(h->common.length) != payload)
		return -EINVAL;

	/* Validate checksum, which might be zeroes if the
	 * sender doesn't support checksum according to NCSI
	 * specification.
	 */
	pchecksum = (__be32 *)((void *)(h + 1) + payload - 4);
	if (ntohl(*pchecksum) == 0)
		return 0;

	checksum = ncsi_calculate_checksum((unsigned char *)h,
					   sizeof(*h) + payload - 4);
	if (*pchecksum != htonl(checksum))
		return -EINVAL;

	return 0;
}

static int ncsi_aen_handler_lsc(struct ncsi_dev_priv *ndp,
				struct ncsi_aen_pkt_hdr *h)
{
	struct ncsi_aen_lsc_pkt *lsc;
	struct ncsi_channel *nc;
	struct ncsi_channel_mode *ncm;
	bool chained;
	int state;
	unsigned long old_data, data;
	unsigned long flags;

	/* Find the NCSI channel */
	ncsi_find_package_and_channel(ndp, h->common.channel, NULL, &nc);
	if (!nc)
		return -ENODEV;

	/* Update the link status */
	lsc = (struct ncsi_aen_lsc_pkt *)h;

	spin_lock_irqsave(&nc->lock, flags);
	ncm = &nc->modes[NCSI_MODE_LINK];
	old_data = ncm->data[2];
	data = ntohl(lsc->status);
	ncm->data[2] = data;
	ncm->data[4] = ntohl(lsc->oem_status);

	chained = !list_empty(&nc->link);
	state = nc->state;
	spin_unlock_irqrestore(&nc->lock, flags);

	if (!((old_data ^ data) & 0x1) || chained)
		return 0;
	if (!(state == NCSI_CHANNEL_INACTIVE && (data & 0x1)) &&
	    !(state == NCSI_CHANNEL_ACTIVE && !(data & 0x1)))
		return 0;

	if (!(ndp->flags & NCSI_DEV_HWA) &&
	    state == NCSI_CHANNEL_ACTIVE)
		ndp->flags |= NCSI_DEV_RESHUFFLE;

	ncsi_stop_channel_monitor(nc);
	spin_lock_irqsave(&ndp->lock, flags);
	list_add_tail_rcu(&nc->link, &ndp->channel_queue);
	spin_unlock_irqrestore(&ndp->lock, flags);

	return ncsi_process_next_channel(ndp);
}

static int ncsi_aen_handler_cr(struct ncsi_dev_priv *ndp,
			       struct ncsi_aen_pkt_hdr *h)
{
	struct ncsi_channel *nc;
	unsigned long flags;

	/* Find the NCSI channel */
	ncsi_find_package_and_channel(ndp, h->common.channel, NULL, &nc);
	if (!nc)
		return -ENODEV;

	spin_lock_irqsave(&nc->lock, flags);
	if (!list_empty(&nc->link) ||
	    nc->state != NCSI_CHANNEL_ACTIVE) {
		spin_unlock_irqrestore(&nc->lock, flags);
		return 0;
	}
	spin_unlock_irqrestore(&nc->lock, flags);

	ncsi_stop_channel_monitor(nc);
	spin_lock_irqsave(&nc->lock, flags);
	nc->state = NCSI_CHANNEL_INVISIBLE;
	spin_unlock_irqrestore(&nc->lock, flags);

	spin_lock_irqsave(&ndp->lock, flags);
	nc->state = NCSI_CHANNEL_INACTIVE;
	list_add_tail_rcu(&nc->link, &ndp->channel_queue);
	spin_unlock_irqrestore(&ndp->lock, flags);

	return ncsi_process_next_channel(ndp);
}

static int ncsi_aen_handler_hncdsc(struct ncsi_dev_priv *ndp,
				   struct ncsi_aen_pkt_hdr *h)
{
	struct ncsi_channel *nc;
	struct ncsi_channel_mode *ncm;
	struct ncsi_aen_hncdsc_pkt *hncdsc;
	unsigned long flags;

	/* Find the NCSI channel */
	ncsi_find_package_and_channel(ndp, h->common.channel, NULL, &nc);
	if (!nc)
		return -ENODEV;

	/* If the channel is active one, we need reconfigure it */
	spin_lock_irqsave(&nc->lock, flags);
	ncm = &nc->modes[NCSI_MODE_LINK];
	hncdsc = (struct ncsi_aen_hncdsc_pkt *)h;
	ncm->data[3] = ntohl(hncdsc->status);
	if (!list_empty(&nc->link) ||
	    nc->state != NCSI_CHANNEL_ACTIVE) {
		spin_unlock_irqrestore(&nc->lock, flags);
		return 0;
	}

	spin_unlock_irqrestore(&nc->lock, flags);
	if (!(ndp->flags & NCSI_DEV_HWA) && !(ncm->data[3] & 0x1))
		ndp->flags |= NCSI_DEV_RESHUFFLE;

	/* If this channel is the active one and the link doesn't
	 * work, we have to choose another channel to be active one.
	 * The logic here is exactly similar to what we do when link
	 * is down on the active channel.
	 *
	 * On the other hand, we need configure it when host driver
	 * state on the active channel becomes ready.
	 */
	ncsi_stop_channel_monitor(nc);

	spin_lock_irqsave(&nc->lock, flags);
	nc->state = (ncm->data[3] & 0x1) ? NCSI_CHANNEL_INACTIVE :
					   NCSI_CHANNEL_ACTIVE;
	spin_unlock_irqrestore(&nc->lock, flags);

	spin_lock_irqsave(&ndp->lock, flags);
	list_add_tail_rcu(&nc->link, &ndp->channel_queue);
	spin_unlock_irqrestore(&ndp->lock, flags);

	ncsi_process_next_channel(ndp);

	return 0;
}

static struct ncsi_aen_handler {
	unsigned char type;
	int           payload;
	int           (*handler)(struct ncsi_dev_priv *ndp,
				 struct ncsi_aen_pkt_hdr *h);
} ncsi_aen_handlers[] = {
	{ NCSI_PKT_AEN_LSC,    12, ncsi_aen_handler_lsc    },
	{ NCSI_PKT_AEN_CR,      4, ncsi_aen_handler_cr     },
	{ NCSI_PKT_AEN_HNCDSC,  4, ncsi_aen_handler_hncdsc }
};

int ncsi_aen_handler(struct ncsi_dev_priv *ndp, struct sk_buff *skb)
{
	struct ncsi_aen_pkt_hdr *h;
	struct ncsi_aen_handler *nah = NULL;
	int i, ret;

	/* Find the handler */
	h = (struct ncsi_aen_pkt_hdr *)skb_network_header(skb);
	for (i = 0; i < ARRAY_SIZE(ncsi_aen_handlers); i++) {
		if (ncsi_aen_handlers[i].type == h->type) {
			nah = &ncsi_aen_handlers[i];
			break;
		}
	}

	if (!nah) {
		netdev_warn(ndp->ndev.dev, "Invalid AEN (0x%x) received\n",
			    h->type);
		return -ENOENT;
	}

	ret = ncsi_validate_aen_pkt(h, nah->payload);
	if (ret)
		goto out;

	ret = nah->handler(ndp, h);
out:
	consume_skb(skb);
	return ret;
}
