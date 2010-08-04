/*
 * Copyright (C) ST-Ericsson AB 2010
 * Author:	Sjur Brendeland/sjur.brandeland@stericsson.com
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/stddef.h>
#include <linux/slab.h>
#include <net/caif/caif_layer.h>
#include <net/caif/cfsrvl.h>
#include <net/caif/cfpkt.h>

static int cfdbgl_receive(struct cflayer *layr, struct cfpkt *pkt);
static int cfdbgl_transmit(struct cflayer *layr, struct cfpkt *pkt);

struct cflayer *cfdbgl_create(u8 channel_id, struct dev_info *dev_info)
{
	struct cfsrvl *dbg = kmalloc(sizeof(struct cfsrvl), GFP_ATOMIC);
	if (!dbg) {
		pr_warning("CAIF: %s(): Out of memory\n", __func__);
		return NULL;
	}
	caif_assert(offsetof(struct cfsrvl, layer) == 0);
	memset(dbg, 0, sizeof(struct cfsrvl));
	cfsrvl_init(dbg, channel_id, dev_info);
	dbg->layer.receive = cfdbgl_receive;
	dbg->layer.transmit = cfdbgl_transmit;
	snprintf(dbg->layer.name, CAIF_LAYER_NAME_SZ - 1, "dbg%d", channel_id);
	return &dbg->layer;
}

static int cfdbgl_receive(struct cflayer *layr, struct cfpkt *pkt)
{
	return layr->up->receive(layr->up, pkt);
}

static int cfdbgl_transmit(struct cflayer *layr, struct cfpkt *pkt)
{
	return layr->dn->transmit(layr->dn, pkt);
}
