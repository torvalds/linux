/*
 * Copyright (C) ST-Ericsson AB 2010
 * Author:	Sjur Brendeland
 * License terms: GNU General Public License (GPL) version 2
 */

#define pr_fmt(fmt) KBUILD_MODNAME ":%s(): " fmt, __func__

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <net/caif/caif_layer.h>
#include <net/caif/cfsrvl.h>
#include <net/caif/cfpkt.h>

#define container_obj(layr) ((struct cfsrvl *) layr)

static int cfvidl_receive(struct cflayer *layr, struct cfpkt *pkt);
static int cfvidl_transmit(struct cflayer *layr, struct cfpkt *pkt);

struct cflayer *cfvidl_create(u8 channel_id, struct dev_info *dev_info)
{
	struct cfsrvl *vid = kzalloc(sizeof(struct cfsrvl), GFP_ATOMIC);
	if (!vid)
		return NULL;
	caif_assert(offsetof(struct cfsrvl, layer) == 0);

	cfsrvl_init(vid, channel_id, dev_info, false);
	vid->layer.receive = cfvidl_receive;
	vid->layer.transmit = cfvidl_transmit;
	snprintf(vid->layer.name, CAIF_LAYER_NAME_SZ, "vid1");
	return &vid->layer;
}

static int cfvidl_receive(struct cflayer *layr, struct cfpkt *pkt)
{
	u32 videoheader;
	if (cfpkt_extr_head(pkt, &videoheader, 4) < 0) {
		pr_err("Packet is erroneous!\n");
		cfpkt_destroy(pkt);
		return -EPROTO;
	}
	return layr->up->receive(layr->up, pkt);
}

static int cfvidl_transmit(struct cflayer *layr, struct cfpkt *pkt)
{
	struct cfsrvl *service = container_obj(layr);
	struct caif_payload_info *info;
	u32 videoheader = 0;
	int ret;

	if (!cfsrvl_ready(service, &ret)) {
		cfpkt_destroy(pkt);
		return ret;
	}

	cfpkt_add_head(pkt, &videoheader, 4);
	/* Add info for MUX-layer to route the packet out */
	info = cfpkt_info(pkt);
	info->channel_id = service->layer.id;
	info->dev_info = &service->dev_info;
	return layr->dn->transmit(layr->dn, pkt);
}
