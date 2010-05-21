/*
 * Copyright (C) ST-Ericsson AB 2010
 * Author:	Sjur Brendeland/sjur.brandeland@stericsson.com
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/stddef.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <net/caif/caif_layer.h>
#include <net/caif/cfsrvl.h>
#include <net/caif/cfpkt.h>

#define container_obj(layr) ((struct cfsrvl *) layr)

#define DGM_CMD_BIT  0x80
#define DGM_FLOW_OFF 0x81
#define DGM_FLOW_ON  0x80
#define DGM_CTRL_PKT_SIZE 1

static int cfdgml_receive(struct cflayer *layr, struct cfpkt *pkt);
static int cfdgml_transmit(struct cflayer *layr, struct cfpkt *pkt);

struct cflayer *cfdgml_create(u8 channel_id, struct dev_info *dev_info)
{
	struct cfsrvl *dgm = kmalloc(sizeof(struct cfsrvl), GFP_ATOMIC);
	if (!dgm) {
		pr_warning("CAIF: %s(): Out of memory\n", __func__);
		return NULL;
	}
	caif_assert(offsetof(struct cfsrvl, layer) == 0);
	memset(dgm, 0, sizeof(struct cfsrvl));
	cfsrvl_init(dgm, channel_id, dev_info);
	dgm->layer.receive = cfdgml_receive;
	dgm->layer.transmit = cfdgml_transmit;
	snprintf(dgm->layer.name, CAIF_LAYER_NAME_SZ - 1, "dgm%d", channel_id);
	dgm->layer.name[CAIF_LAYER_NAME_SZ - 1] = '\0';
	return &dgm->layer;
}

static int cfdgml_receive(struct cflayer *layr, struct cfpkt *pkt)
{
	u8 cmd = -1;
	u8 dgmhdr[3];
	int ret;
	caif_assert(layr->up != NULL);
	caif_assert(layr->receive != NULL);
	caif_assert(layr->ctrlcmd != NULL);

	if (cfpkt_extr_head(pkt, &cmd, 1) < 0) {
		pr_err("CAIF: %s(): Packet is erroneous!\n", __func__);
		cfpkt_destroy(pkt);
		return -EPROTO;
	}

	if ((cmd & DGM_CMD_BIT) == 0) {
		if (cfpkt_extr_head(pkt, &dgmhdr, 3) < 0) {
			pr_err("CAIF: %s(): Packet is erroneous!\n", __func__);
			cfpkt_destroy(pkt);
			return -EPROTO;
		}
		ret = layr->up->receive(layr->up, pkt);
		return ret;
	}

	switch (cmd) {
	case DGM_FLOW_OFF:	/* FLOW OFF */
		layr->ctrlcmd(layr, CAIF_CTRLCMD_FLOW_OFF_IND, 0);
		cfpkt_destroy(pkt);
		return 0;
	case DGM_FLOW_ON:	/* FLOW ON */
		layr->ctrlcmd(layr, CAIF_CTRLCMD_FLOW_ON_IND, 0);
		cfpkt_destroy(pkt);
		return 0;
	default:
		cfpkt_destroy(pkt);
		pr_info("CAIF: %s(): Unknown datagram control %d (0x%x)\n",
			__func__, cmd, cmd);
		return -EPROTO;
	}
}

static int cfdgml_transmit(struct cflayer *layr, struct cfpkt *pkt)
{
	u32 zero = 0;
	struct caif_payload_info *info;
	struct cfsrvl *service = container_obj(layr);
	int ret;
	if (!cfsrvl_ready(service, &ret))
		return ret;

	cfpkt_add_head(pkt, &zero, 4);

	/* Add info for MUX-layer to route the packet out. */
	info = cfpkt_info(pkt);
	info->channel_id = service->layer.id;
	/* To optimize alignment, we add up the size of CAIF header
	 * before payload.
	 */
	info->hdr_len = 4;
	info->dev_info = &service->dev_info;
	ret = layr->dn->transmit(layr->dn, pkt);
	if (ret < 0) {
		u32 tmp32;
		cfpkt_extr_head(pkt, &tmp32, 4);
	}
	return ret;
}
