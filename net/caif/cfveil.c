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

#define VEI_PAYLOAD  0x00
#define VEI_CMD_BIT  0x80
#define VEI_FLOW_OFF 0x81
#define VEI_FLOW_ON  0x80
#define VEI_SET_PIN  0x82
#define VEI_CTRL_PKT_SIZE 1
#define container_obj(layr) container_of(layr, struct cfsrvl, layer)

static int cfvei_receive(struct cflayer *layr, struct cfpkt *pkt);
static int cfvei_transmit(struct cflayer *layr, struct cfpkt *pkt);

struct cflayer *cfvei_create(u8 channel_id, struct dev_info *dev_info)
{
	struct cfsrvl *vei = kmalloc(sizeof(struct cfsrvl), GFP_ATOMIC);
	if (!vei) {
		pr_warning("CAIF: %s(): Out of memory\n", __func__);
		return NULL;
	}
	caif_assert(offsetof(struct cfsrvl, layer) == 0);
	memset(vei, 0, sizeof(struct cfsrvl));
	cfsrvl_init(vei, channel_id, dev_info);
	vei->layer.receive = cfvei_receive;
	vei->layer.transmit = cfvei_transmit;
	snprintf(vei->layer.name, CAIF_LAYER_NAME_SZ - 1, "vei%d", channel_id);
	return &vei->layer;
}

static int cfvei_receive(struct cflayer *layr, struct cfpkt *pkt)
{
	u8 cmd;
	int ret;
	caif_assert(layr->up != NULL);
	caif_assert(layr->receive != NULL);
	caif_assert(layr->ctrlcmd != NULL);


	if (cfpkt_extr_head(pkt, &cmd, 1) < 0) {
		pr_err("CAIF: %s(): Packet is erroneous!\n", __func__);
		cfpkt_destroy(pkt);
		return -EPROTO;
	}
	switch (cmd) {
	case VEI_PAYLOAD:
		ret = layr->up->receive(layr->up, pkt);
		return ret;
	case VEI_FLOW_OFF:
		layr->ctrlcmd(layr, CAIF_CTRLCMD_FLOW_OFF_IND, 0);
		cfpkt_destroy(pkt);
		return 0;
	case VEI_FLOW_ON:
		layr->ctrlcmd(layr, CAIF_CTRLCMD_FLOW_ON_IND, 0);
		cfpkt_destroy(pkt);
		return 0;
	case VEI_SET_PIN:	/* SET RS232 PIN */
		cfpkt_destroy(pkt);
		return 0;
	default:		/* SET RS232 PIN */
		pr_warning("CAIF: %s():Unknown VEI control packet %d (0x%x)!\n",
			   __func__, cmd, cmd);
		cfpkt_destroy(pkt);
		return -EPROTO;
	}
}

static int cfvei_transmit(struct cflayer *layr, struct cfpkt *pkt)
{
	u8 tmp = 0;
	struct caif_payload_info *info;
	int ret;
	struct cfsrvl *service = container_obj(layr);
	if (!cfsrvl_ready(service, &ret))
		return ret;
	caif_assert(layr->dn != NULL);
	caif_assert(layr->dn->transmit != NULL);
	if (cfpkt_getlen(pkt) > CAIF_MAX_PAYLOAD_SIZE) {
		pr_warning("CAIF: %s(): Packet too large - size=%d\n",
			   __func__, cfpkt_getlen(pkt));
		return -EOVERFLOW;
	}

	if (cfpkt_add_head(pkt, &tmp, 1) < 0) {
		pr_err("CAIF: %s(): Packet is erroneous!\n", __func__);
		return -EPROTO;
	}

	/* Add info-> for MUX-layer to route the packet out. */
	info = cfpkt_info(pkt);
	info->channel_id = service->layer.id;
	info->hdr_len = 1;
	info->dev_info = &service->dev_info;
	ret = layr->dn->transmit(layr->dn, pkt);
	if (ret < 0)
		cfpkt_extr_head(pkt, &tmp, 1);
	return ret;
}
