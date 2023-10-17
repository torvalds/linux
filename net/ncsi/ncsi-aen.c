// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright Gavin Shan, IBM Corporation 2016.
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
	struct ncsi_channel *nc, *tmp;
	struct ncsi_channel_mode *ncm;
	unsigned long old_data, data;
	struct ncsi_aen_lsc_pkt *lsc;
	struct ncsi_package *np;
	bool had_link, has_link;
	unsigned long flags;
	bool chained;
	int state;

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

	had_link = !!(old_data & 0x1);
	has_link = !!(data & 0x1);

	netdev_dbg(ndp->ndev.dev, "NCSI: LSC AEN - channel %u state %s\n",
		   nc->id, data & 0x1 ? "up" : "down");

	chained = !list_empty(&nc->link);
	state = nc->state;
	spin_unlock_irqrestore(&nc->lock, flags);

	if (state == NCSI_CHANNEL_INACTIVE)
		netdev_warn(ndp->ndev.dev,
			    "NCSI: Inactive channel %u received AEN!\n",
			    nc->id);

	if ((had_link == has_link) || chained)
		return 0;

	if (had_link)
		netif_carrier_off(ndp->ndev.dev);
	else
		netif_carrier_on(ndp->ndev.dev);

	if (!ndp->multi_package && !nc->package->multi_channel) {
		if (had_link) {
			ndp->flags |= NCSI_DEV_RESHUFFLE;
			ncsi_stop_channel_monitor(nc);
			spin_lock_irqsave(&ndp->lock, flags);
			list_add_tail_rcu(&nc->link, &ndp->channel_queue);
			spin_unlock_irqrestore(&ndp->lock, flags);
			return ncsi_process_next_channel(ndp);
		}
		/* Configured channel came up */
		return 0;
	}

	if (had_link) {
		ncm = &nc->modes[NCSI_MODE_TX_ENABLE];
		if (ncsi_channel_is_last(ndp, nc)) {
			/* No channels left, reconfigure */
			return ncsi_reset_dev(&ndp->ndev);
		} else if (ncm->enable) {
			/* Need to failover Tx channel */
			ncsi_update_tx_channel(ndp, nc->package, nc, NULL);
		}
	} else if (has_link && nc->package->preferred_channel == nc) {
		/* Return Tx to preferred channel */
		ncsi_update_tx_channel(ndp, nc->package, NULL, nc);
	} else if (has_link) {
		NCSI_FOR_EACH_PACKAGE(ndp, np) {
			NCSI_FOR_EACH_CHANNEL(np, tmp) {
				/* Enable Tx on this channel if the current Tx
				 * channel is down.
				 */
				ncm = &tmp->modes[NCSI_MODE_TX_ENABLE];
				if (ncm->enable &&
				    !ncsi_channel_has_link(tmp)) {
					ncsi_update_tx_channel(ndp, nc->package,
							       tmp, nc);
					break;
				}
			}
		}
	}

	/* Leave configured channels active in a multi-channel scenario so
	 * AEN events are still received.
	 */
	return 0;
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
	nc->modes[NCSI_MODE_TX_ENABLE].enable = 0;

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

	spin_lock_irqsave(&nc->lock, flags);
	ncm = &nc->modes[NCSI_MODE_LINK];
	hncdsc = (struct ncsi_aen_hncdsc_pkt *)h;
	ncm->data[3] = ntohl(hncdsc->status);
	spin_unlock_irqrestore(&nc->lock, flags);
	netdev_dbg(ndp->ndev.dev,
		   "NCSI: host driver %srunning on channel %u\n",
		   ncm->data[3] & 0x1 ? "" : "not ", nc->id);

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
	{ NCSI_PKT_AEN_HNCDSC,  8, ncsi_aen_handler_hncdsc }
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
	if (ret) {
		netdev_warn(ndp->ndev.dev,
			    "NCSI: 'bad' packet ignored for AEN type 0x%x\n",
			    h->type);
		goto out;
	}

	ret = nah->handler(ndp, h);
	if (ret)
		netdev_err(ndp->ndev.dev,
			   "NCSI: Handler for AEN type 0x%x returned %d\n",
			   h->type, ret);
out:
	consume_skb(skb);
	return ret;
}
