/*
 * Copyright (C) ST-Ericsson AB 2010
 * Author:	Sjur Brendeland/sjur.brandeland@stericsson.com
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <net/caif/caif_layer.h>
#include <net/caif/cfsrvl.h>
#include <net/caif/cfpkt.h>

#define SRVL_CTRL_PKT_SIZE 1
#define SRVL_FLOW_OFF 0x81
#define SRVL_FLOW_ON  0x80
#define SRVL_SET_PIN  0x82
#define SRVL_CTRL_PKT_SIZE 1

#define container_obj(layr) container_of(layr, struct cfsrvl, layer)

static void cfservl_ctrlcmd(struct cflayer *layr, enum caif_ctrlcmd ctrl,
				int phyid)
{
	struct cfsrvl *service = container_obj(layr);
	caif_assert(layr->up != NULL);
	caif_assert(layr->up->ctrlcmd != NULL);
	switch (ctrl) {
	case CAIF_CTRLCMD_INIT_RSP:
		service->open = true;
		layr->up->ctrlcmd(layr->up, ctrl, phyid);
		break;
	case CAIF_CTRLCMD_DEINIT_RSP:
	case CAIF_CTRLCMD_INIT_FAIL_RSP:
		service->open = false;
		layr->up->ctrlcmd(layr->up, ctrl, phyid);
		break;
	case _CAIF_CTRLCMD_PHYIF_FLOW_OFF_IND:
		if (phyid != service->dev_info.id)
			break;
		if (service->modem_flow_on)
			layr->up->ctrlcmd(layr->up,
					  CAIF_CTRLCMD_FLOW_OFF_IND, phyid);
		service->phy_flow_on = false;
		break;
	case _CAIF_CTRLCMD_PHYIF_FLOW_ON_IND:
		if (phyid != service->dev_info.id)
			return;
		if (service->modem_flow_on) {
			layr->up->ctrlcmd(layr->up,
					   CAIF_CTRLCMD_FLOW_ON_IND,
					   phyid);
		}
		service->phy_flow_on = true;
		break;
	case CAIF_CTRLCMD_FLOW_OFF_IND:
		if (service->phy_flow_on) {
			layr->up->ctrlcmd(layr->up,
					  CAIF_CTRLCMD_FLOW_OFF_IND, phyid);
		}
		service->modem_flow_on = false;
		break;
	case CAIF_CTRLCMD_FLOW_ON_IND:
		if (service->phy_flow_on) {
			layr->up->ctrlcmd(layr->up,
					  CAIF_CTRLCMD_FLOW_ON_IND, phyid);
		}
		service->modem_flow_on = true;
		break;
	case _CAIF_CTRLCMD_PHYIF_DOWN_IND:
		/* In case interface is down, let's fake a remove shutdown */
		layr->up->ctrlcmd(layr->up,
				CAIF_CTRLCMD_REMOTE_SHUTDOWN_IND, phyid);
		break;
	case CAIF_CTRLCMD_REMOTE_SHUTDOWN_IND:
		layr->up->ctrlcmd(layr->up, ctrl, phyid);
		break;
	default:
		pr_warning("CAIF: %s(): "
			   "Unexpected ctrl in cfsrvl (%d)\n", __func__, ctrl);
		/* We have both modem and phy flow on, send flow on */
		layr->up->ctrlcmd(layr->up, ctrl, phyid);
		service->phy_flow_on = true;
		break;
	}
}

static int cfservl_modemcmd(struct cflayer *layr, enum caif_modemcmd ctrl)
{
	struct cfsrvl *service = container_obj(layr);
	caif_assert(layr != NULL);
	caif_assert(layr->dn != NULL);
	caif_assert(layr->dn->transmit != NULL);
	switch (ctrl) {
	case CAIF_MODEMCMD_FLOW_ON_REQ:
		{
			struct cfpkt *pkt;
			struct caif_payload_info *info;
			u8 flow_on = SRVL_FLOW_ON;
			pkt = cfpkt_create(SRVL_CTRL_PKT_SIZE);
			if (!pkt) {
				pr_warning("CAIF: %s(): Out of memory\n",
					__func__);
				return -ENOMEM;
			}

			if (cfpkt_add_head(pkt, &flow_on, 1) < 0) {
				pr_err("CAIF: %s(): Packet is erroneous!\n",
					__func__);
				cfpkt_destroy(pkt);
				return -EPROTO;
			}
			info = cfpkt_info(pkt);
			info->channel_id = service->layer.id;
			info->hdr_len = 1;
			info->dev_info = &service->dev_info;
			return layr->dn->transmit(layr->dn, pkt);
		}
	case CAIF_MODEMCMD_FLOW_OFF_REQ:
		{
			struct cfpkt *pkt;
			struct caif_payload_info *info;
			u8 flow_off = SRVL_FLOW_OFF;
			pkt = cfpkt_create(SRVL_CTRL_PKT_SIZE);
			if (!pkt) {
				pr_warning("CAIF: %s(): Out of memory\n",
					__func__);
				return -ENOMEM;
			}

			if (cfpkt_add_head(pkt, &flow_off, 1) < 0) {
				pr_err("CAIF: %s(): Packet is erroneous!\n",
					__func__);
				cfpkt_destroy(pkt);
				return -EPROTO;
			}
			info = cfpkt_info(pkt);
			info->channel_id = service->layer.id;
			info->hdr_len = 1;
			info->dev_info = &service->dev_info;
			return layr->dn->transmit(layr->dn, pkt);
		}
	default:
	  break;
	}
	return -EINVAL;
}

void cfservl_destroy(struct cflayer *layer)
{
	kfree(layer);
}

void cfsrvl_init(struct cfsrvl *service,
		 u8 channel_id,
		 struct dev_info *dev_info)
{
	caif_assert(offsetof(struct cfsrvl, layer) == 0);
	service->open = false;
	service->modem_flow_on = true;
	service->phy_flow_on = true;
	service->layer.id = channel_id;
	service->layer.ctrlcmd = cfservl_ctrlcmd;
	service->layer.modemcmd = cfservl_modemcmd;
	service->dev_info = *dev_info;
	kref_init(&service->ref);
}

void cfsrvl_release(struct kref *kref)
{
	struct cfsrvl *service = container_of(kref, struct cfsrvl, ref);
	kfree(service);
}

bool cfsrvl_ready(struct cfsrvl *service, int *err)
{
	if (service->open && service->modem_flow_on && service->phy_flow_on)
		return true;
	if (!service->open) {
		*err = -ENOTCONN;
		return false;
	}
	caif_assert(!(service->modem_flow_on && service->phy_flow_on));
	*err = -EAGAIN;
	return false;
}
u8 cfsrvl_getphyid(struct cflayer *layer)
{
	struct cfsrvl *servl = container_obj(layer);
	return servl->dev_info.id;
}

bool cfsrvl_phyid_match(struct cflayer *layer, int phyid)
{
	struct cfsrvl *servl = container_obj(layer);
	return servl->dev_info.id == phyid;
}
