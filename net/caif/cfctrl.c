// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) ST-Ericsson AB 2010
 * Author:	Sjur Brendeland
 */

#define pr_fmt(fmt) KBUILD_MODNAME ":%s(): " fmt, __func__

#include <linux/stddef.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/pkt_sched.h>
#include <net/caif/caif_layer.h>
#include <net/caif/cfpkt.h>
#include <net/caif/cfctrl.h>

#define container_obj(layr) container_of(layr, struct cfctrl, serv.layer)
#define UTILITY_NAME_LENGTH 16
#define CFPKT_CTRL_PKT_LEN 20

#ifdef CAIF_NO_LOOP
static int handle_loop(struct cfctrl *ctrl,
		       int cmd, struct cfpkt *pkt){
	return -1;
}
#else
static int handle_loop(struct cfctrl *ctrl,
		       int cmd, struct cfpkt *pkt);
#endif
static int cfctrl_recv(struct cflayer *layr, struct cfpkt *pkt);
static void cfctrl_ctrlcmd(struct cflayer *layr, enum caif_ctrlcmd ctrl,
			   int phyid);


struct cflayer *cfctrl_create(void)
{
	struct dev_info dev_info;
	struct cfctrl *this =
		kzalloc(sizeof(struct cfctrl), GFP_ATOMIC);
	if (!this)
		return NULL;
	caif_assert(offsetof(struct cfctrl, serv.layer) == 0);
	memset(&dev_info, 0, sizeof(dev_info));
	dev_info.id = 0xff;
	cfsrvl_init(&this->serv, 0, &dev_info, false);
	atomic_set(&this->req_seq_no, 1);
	atomic_set(&this->rsp_seq_no, 1);
	this->serv.layer.receive = cfctrl_recv;
	sprintf(this->serv.layer.name, "ctrl");
	this->serv.layer.ctrlcmd = cfctrl_ctrlcmd;
#ifndef CAIF_NO_LOOP
	spin_lock_init(&this->loop_linkid_lock);
	this->loop_linkid = 1;
#endif
	spin_lock_init(&this->info_list_lock);
	INIT_LIST_HEAD(&this->list);
	return &this->serv.layer;
}

void cfctrl_remove(struct cflayer *layer)
{
	struct cfctrl_request_info *p, *tmp;
	struct cfctrl *ctrl = container_obj(layer);

	spin_lock_bh(&ctrl->info_list_lock);
	list_for_each_entry_safe(p, tmp, &ctrl->list, list) {
		list_del(&p->list);
		kfree(p);
	}
	spin_unlock_bh(&ctrl->info_list_lock);
	kfree(layer);
}

static bool param_eq(const struct cfctrl_link_param *p1,
		     const struct cfctrl_link_param *p2)
{
	bool eq =
	    p1->linktype == p2->linktype &&
	    p1->priority == p2->priority &&
	    p1->phyid == p2->phyid &&
	    p1->endpoint == p2->endpoint && p1->chtype == p2->chtype;

	if (!eq)
		return false;

	switch (p1->linktype) {
	case CFCTRL_SRV_VEI:
		return true;
	case CFCTRL_SRV_DATAGRAM:
		return p1->u.datagram.connid == p2->u.datagram.connid;
	case CFCTRL_SRV_RFM:
		return
		    p1->u.rfm.connid == p2->u.rfm.connid &&
		    strcmp(p1->u.rfm.volume, p2->u.rfm.volume) == 0;
	case CFCTRL_SRV_UTIL:
		return
		    p1->u.utility.fifosize_kb == p2->u.utility.fifosize_kb
		    && p1->u.utility.fifosize_bufs ==
		    p2->u.utility.fifosize_bufs
		    && strcmp(p1->u.utility.name, p2->u.utility.name) == 0
		    && p1->u.utility.paramlen == p2->u.utility.paramlen
		    && memcmp(p1->u.utility.params, p2->u.utility.params,
			      p1->u.utility.paramlen) == 0;

	case CFCTRL_SRV_VIDEO:
		return p1->u.video.connid == p2->u.video.connid;
	case CFCTRL_SRV_DBG:
		return true;
	case CFCTRL_SRV_DECM:
		return false;
	default:
		return false;
	}
	return false;
}

static bool cfctrl_req_eq(const struct cfctrl_request_info *r1,
			  const struct cfctrl_request_info *r2)
{
	if (r1->cmd != r2->cmd)
		return false;
	if (r1->cmd == CFCTRL_CMD_LINK_SETUP)
		return param_eq(&r1->param, &r2->param);
	else
		return r1->channel_id == r2->channel_id;
}

/* Insert request at the end */
static void cfctrl_insert_req(struct cfctrl *ctrl,
			      struct cfctrl_request_info *req)
{
	spin_lock_bh(&ctrl->info_list_lock);
	atomic_inc(&ctrl->req_seq_no);
	req->sequence_no = atomic_read(&ctrl->req_seq_no);
	list_add_tail(&req->list, &ctrl->list);
	spin_unlock_bh(&ctrl->info_list_lock);
}

/* Compare and remove request */
static struct cfctrl_request_info *cfctrl_remove_req(struct cfctrl *ctrl,
						struct cfctrl_request_info *req)
{
	struct cfctrl_request_info *p, *tmp, *first;

	first = list_first_entry(&ctrl->list, struct cfctrl_request_info, list);

	list_for_each_entry_safe(p, tmp, &ctrl->list, list) {
		if (cfctrl_req_eq(req, p)) {
			if (p != first)
				pr_warn("Requests are not received in order\n");

			atomic_set(&ctrl->rsp_seq_no,
					 p->sequence_no);
			list_del(&p->list);
			goto out;
		}
	}
	p = NULL;
out:
	return p;
}

struct cfctrl_rsp *cfctrl_get_respfuncs(struct cflayer *layer)
{
	struct cfctrl *this = container_obj(layer);
	return &this->res;
}

static void init_info(struct caif_payload_info *info, struct cfctrl *cfctrl)
{
	info->hdr_len = 0;
	info->channel_id = cfctrl->serv.layer.id;
	info->dev_info = &cfctrl->serv.dev_info;
}

void cfctrl_enum_req(struct cflayer *layer, u8 physlinkid)
{
	struct cfpkt *pkt;
	struct cfctrl *cfctrl = container_obj(layer);
	struct cflayer *dn = cfctrl->serv.layer.dn;

	if (!dn) {
		pr_debug("not able to send enum request\n");
		return;
	}
	pkt = cfpkt_create(CFPKT_CTRL_PKT_LEN);
	if (!pkt)
		return;
	caif_assert(offsetof(struct cfctrl, serv.layer) == 0);
	init_info(cfpkt_info(pkt), cfctrl);
	cfpkt_info(pkt)->dev_info->id = physlinkid;
	cfctrl->serv.dev_info.id = physlinkid;
	cfpkt_addbdy(pkt, CFCTRL_CMD_ENUM);
	cfpkt_addbdy(pkt, physlinkid);
	cfpkt_set_prio(pkt, TC_PRIO_CONTROL);
	dn->transmit(dn, pkt);
}

int cfctrl_linkup_request(struct cflayer *layer,
			  struct cfctrl_link_param *param,
			  struct cflayer *user_layer)
{
	struct cfctrl *cfctrl = container_obj(layer);
	u32 tmp32;
	u16 tmp16;
	u8 tmp8;
	struct cfctrl_request_info *req;
	int ret;
	char utility_name[16];
	struct cfpkt *pkt;
	struct cflayer *dn = cfctrl->serv.layer.dn;

	if (!dn) {
		pr_debug("not able to send linkup request\n");
		return -ENODEV;
	}

	if (cfctrl_cancel_req(layer, user_layer) > 0) {
		/* Slight Paranoia, check if already connecting */
		pr_err("Duplicate connect request for same client\n");
		WARN_ON(1);
		return -EALREADY;
	}

	pkt = cfpkt_create(CFPKT_CTRL_PKT_LEN);
	if (!pkt)
		return -ENOMEM;
	cfpkt_addbdy(pkt, CFCTRL_CMD_LINK_SETUP);
	cfpkt_addbdy(pkt, (param->chtype << 4) | param->linktype);
	cfpkt_addbdy(pkt, (param->priority << 3) | param->phyid);
	cfpkt_addbdy(pkt, param->endpoint & 0x03);

	switch (param->linktype) {
	case CFCTRL_SRV_VEI:
		break;
	case CFCTRL_SRV_VIDEO:
		cfpkt_addbdy(pkt, (u8) param->u.video.connid);
		break;
	case CFCTRL_SRV_DBG:
		break;
	case CFCTRL_SRV_DATAGRAM:
		tmp32 = cpu_to_le32(param->u.datagram.connid);
		cfpkt_add_body(pkt, &tmp32, 4);
		break;
	case CFCTRL_SRV_RFM:
		/* Construct a frame, convert DatagramConnectionID to network
		 * format long and copy it out...
		 */
		tmp32 = cpu_to_le32(param->u.rfm.connid);
		cfpkt_add_body(pkt, &tmp32, 4);
		/* Add volume name, including zero termination... */
		cfpkt_add_body(pkt, param->u.rfm.volume,
			       strlen(param->u.rfm.volume) + 1);
		break;
	case CFCTRL_SRV_UTIL:
		tmp16 = cpu_to_le16(param->u.utility.fifosize_kb);
		cfpkt_add_body(pkt, &tmp16, 2);
		tmp16 = cpu_to_le16(param->u.utility.fifosize_bufs);
		cfpkt_add_body(pkt, &tmp16, 2);
		memset(utility_name, 0, sizeof(utility_name));
		strscpy(utility_name, param->u.utility.name,
			UTILITY_NAME_LENGTH);
		cfpkt_add_body(pkt, utility_name, UTILITY_NAME_LENGTH);
		tmp8 = param->u.utility.paramlen;
		cfpkt_add_body(pkt, &tmp8, 1);
		cfpkt_add_body(pkt, param->u.utility.params,
			       param->u.utility.paramlen);
		break;
	default:
		pr_warn("Request setup of bad link type = %d\n",
			param->linktype);
		return -EINVAL;
	}
	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return -ENOMEM;
	req->client_layer = user_layer;
	req->cmd = CFCTRL_CMD_LINK_SETUP;
	req->param = *param;
	cfctrl_insert_req(cfctrl, req);
	init_info(cfpkt_info(pkt), cfctrl);
	/*
	 * NOTE:Always send linkup and linkdown request on the same
	 *	device as the payload. Otherwise old queued up payload
	 *	might arrive with the newly allocated channel ID.
	 */
	cfpkt_info(pkt)->dev_info->id = param->phyid;
	cfpkt_set_prio(pkt, TC_PRIO_CONTROL);
	ret =
	    dn->transmit(dn, pkt);
	if (ret < 0) {
		int count;

		count = cfctrl_cancel_req(&cfctrl->serv.layer,
						user_layer);
		if (count != 1) {
			pr_err("Could not remove request (%d)", count);
			return -ENODEV;
		}
	}
	return 0;
}

int cfctrl_linkdown_req(struct cflayer *layer, u8 channelid,
			struct cflayer *client)
{
	int ret;
	struct cfpkt *pkt;
	struct cfctrl *cfctrl = container_obj(layer);
	struct cflayer *dn = cfctrl->serv.layer.dn;

	if (!dn) {
		pr_debug("not able to send link-down request\n");
		return -ENODEV;
	}
	pkt = cfpkt_create(CFPKT_CTRL_PKT_LEN);
	if (!pkt)
		return -ENOMEM;
	cfpkt_addbdy(pkt, CFCTRL_CMD_LINK_DESTROY);
	cfpkt_addbdy(pkt, channelid);
	init_info(cfpkt_info(pkt), cfctrl);
	cfpkt_set_prio(pkt, TC_PRIO_CONTROL);
	ret =
	    dn->transmit(dn, pkt);
#ifndef CAIF_NO_LOOP
	cfctrl->loop_linkused[channelid] = 0;
#endif
	return ret;
}

int cfctrl_cancel_req(struct cflayer *layr, struct cflayer *adap_layer)
{
	struct cfctrl_request_info *p, *tmp;
	struct cfctrl *ctrl = container_obj(layr);
	int found = 0;
	spin_lock_bh(&ctrl->info_list_lock);

	list_for_each_entry_safe(p, tmp, &ctrl->list, list) {
		if (p->client_layer == adap_layer) {
			list_del(&p->list);
			kfree(p);
			found++;
		}
	}

	spin_unlock_bh(&ctrl->info_list_lock);
	return found;
}

static int cfctrl_recv(struct cflayer *layer, struct cfpkt *pkt)
{
	u8 cmdrsp;
	u8 cmd;
	int ret = -1;
	u8 len;
	u8 param[255];
	u8 linkid = 0;
	struct cfctrl *cfctrl = container_obj(layer);
	struct cfctrl_request_info rsp, *req;


	cmdrsp = cfpkt_extr_head_u8(pkt);
	cmd = cmdrsp & CFCTRL_CMD_MASK;
	if (cmd != CFCTRL_CMD_LINK_ERR
	    && CFCTRL_RSP_BIT != (CFCTRL_RSP_BIT & cmdrsp)
		&& CFCTRL_ERR_BIT != (CFCTRL_ERR_BIT & cmdrsp)) {
		if (handle_loop(cfctrl, cmd, pkt) != 0)
			cmdrsp |= CFCTRL_ERR_BIT;
	}

	switch (cmd) {
	case CFCTRL_CMD_LINK_SETUP:
		{
			enum cfctrl_srv serv;
			enum cfctrl_srv servtype;
			u8 endpoint;
			u8 physlinkid;
			u8 prio;
			u8 tmp;
			u8 *cp;
			int i;
			struct cfctrl_link_param linkparam;
			memset(&linkparam, 0, sizeof(linkparam));

			tmp = cfpkt_extr_head_u8(pkt);

			serv = tmp & CFCTRL_SRV_MASK;
			linkparam.linktype = serv;

			servtype = tmp >> 4;
			linkparam.chtype = servtype;

			tmp = cfpkt_extr_head_u8(pkt);
			physlinkid = tmp & 0x07;
			prio = tmp >> 3;

			linkparam.priority = prio;
			linkparam.phyid = physlinkid;
			endpoint = cfpkt_extr_head_u8(pkt);
			linkparam.endpoint = endpoint & 0x03;

			switch (serv) {
			case CFCTRL_SRV_VEI:
			case CFCTRL_SRV_DBG:
				if (CFCTRL_ERR_BIT & cmdrsp)
					break;
				/* Link ID */
				linkid = cfpkt_extr_head_u8(pkt);
				break;
			case CFCTRL_SRV_VIDEO:
				tmp = cfpkt_extr_head_u8(pkt);
				linkparam.u.video.connid = tmp;
				if (CFCTRL_ERR_BIT & cmdrsp)
					break;
				/* Link ID */
				linkid = cfpkt_extr_head_u8(pkt);
				break;

			case CFCTRL_SRV_DATAGRAM:
				linkparam.u.datagram.connid =
				    cfpkt_extr_head_u32(pkt);
				if (CFCTRL_ERR_BIT & cmdrsp)
					break;
				/* Link ID */
				linkid = cfpkt_extr_head_u8(pkt);
				break;
			case CFCTRL_SRV_RFM:
				/* Construct a frame, convert
				 * DatagramConnectionID
				 * to network format long and copy it out...
				 */
				linkparam.u.rfm.connid =
				    cfpkt_extr_head_u32(pkt);
				cp = (u8 *) linkparam.u.rfm.volume;
				for (tmp = cfpkt_extr_head_u8(pkt);
				     cfpkt_more(pkt) && tmp != '\0';
				     tmp = cfpkt_extr_head_u8(pkt))
					*cp++ = tmp;
				*cp = '\0';

				if (CFCTRL_ERR_BIT & cmdrsp)
					break;
				/* Link ID */
				linkid = cfpkt_extr_head_u8(pkt);

				break;
			case CFCTRL_SRV_UTIL:
				/* Construct a frame, convert
				 * DatagramConnectionID
				 * to network format long and copy it out...
				 */
				/* Fifosize KB */
				linkparam.u.utility.fifosize_kb =
				    cfpkt_extr_head_u16(pkt);
				/* Fifosize bufs */
				linkparam.u.utility.fifosize_bufs =
				    cfpkt_extr_head_u16(pkt);
				/* name */
				cp = (u8 *) linkparam.u.utility.name;
				caif_assert(sizeof(linkparam.u.utility.name)
					     >= UTILITY_NAME_LENGTH);
				for (i = 0;
				     i < UTILITY_NAME_LENGTH
				     && cfpkt_more(pkt); i++) {
					tmp = cfpkt_extr_head_u8(pkt);
					*cp++ = tmp;
				}
				/* Length */
				len = cfpkt_extr_head_u8(pkt);
				linkparam.u.utility.paramlen = len;
				/* Param Data */
				cp = linkparam.u.utility.params;
				while (cfpkt_more(pkt) && len--) {
					tmp = cfpkt_extr_head_u8(pkt);
					*cp++ = tmp;
				}
				if (CFCTRL_ERR_BIT & cmdrsp)
					break;
				/* Link ID */
				linkid = cfpkt_extr_head_u8(pkt);
				/* Length */
				len = cfpkt_extr_head_u8(pkt);
				/* Param Data */
				cfpkt_extr_head(pkt, &param, len);
				break;
			default:
				pr_warn("Request setup, invalid type (%d)\n",
					serv);
				goto error;
			}

			rsp.cmd = cmd;
			rsp.param = linkparam;
			spin_lock_bh(&cfctrl->info_list_lock);
			req = cfctrl_remove_req(cfctrl, &rsp);

			if (CFCTRL_ERR_BIT == (CFCTRL_ERR_BIT & cmdrsp) ||
				cfpkt_erroneous(pkt)) {
				pr_err("Invalid O/E bit or parse error "
						"on CAIF control channel\n");
				cfctrl->res.reject_rsp(cfctrl->serv.layer.up,
						       0,
						       req ? req->client_layer
						       : NULL);
			} else {
				cfctrl->res.linksetup_rsp(cfctrl->serv.
							  layer.up, linkid,
							  serv, physlinkid,
							  req ? req->
							  client_layer : NULL);
			}

			kfree(req);

			spin_unlock_bh(&cfctrl->info_list_lock);
		}
		break;
	case CFCTRL_CMD_LINK_DESTROY:
		linkid = cfpkt_extr_head_u8(pkt);
		cfctrl->res.linkdestroy_rsp(cfctrl->serv.layer.up, linkid);
		break;
	case CFCTRL_CMD_LINK_ERR:
		pr_err("Frame Error Indication received\n");
		cfctrl->res.linkerror_ind();
		break;
	case CFCTRL_CMD_ENUM:
		cfctrl->res.enum_rsp();
		break;
	case CFCTRL_CMD_SLEEP:
		cfctrl->res.sleep_rsp();
		break;
	case CFCTRL_CMD_WAKE:
		cfctrl->res.wake_rsp();
		break;
	case CFCTRL_CMD_LINK_RECONF:
		cfctrl->res.restart_rsp();
		break;
	case CFCTRL_CMD_RADIO_SET:
		cfctrl->res.radioset_rsp();
		break;
	default:
		pr_err("Unrecognized Control Frame\n");
		goto error;
	}
	ret = 0;
error:
	cfpkt_destroy(pkt);
	return ret;
}

static void cfctrl_ctrlcmd(struct cflayer *layr, enum caif_ctrlcmd ctrl,
			   int phyid)
{
	struct cfctrl *this = container_obj(layr);
	switch (ctrl) {
	case _CAIF_CTRLCMD_PHYIF_FLOW_OFF_IND:
	case CAIF_CTRLCMD_FLOW_OFF_IND:
		spin_lock_bh(&this->info_list_lock);
		if (!list_empty(&this->list))
			pr_debug("Received flow off in control layer\n");
		spin_unlock_bh(&this->info_list_lock);
		break;
	case _CAIF_CTRLCMD_PHYIF_DOWN_IND: {
		struct cfctrl_request_info *p, *tmp;

		/* Find all connect request and report failure */
		spin_lock_bh(&this->info_list_lock);
		list_for_each_entry_safe(p, tmp, &this->list, list) {
			if (p->param.phyid == phyid) {
				list_del(&p->list);
				p->client_layer->ctrlcmd(p->client_layer,
						CAIF_CTRLCMD_INIT_FAIL_RSP,
						phyid);
				kfree(p);
			}
		}
		spin_unlock_bh(&this->info_list_lock);
		break;
	}
	default:
		break;
	}
}

#ifndef CAIF_NO_LOOP
static int handle_loop(struct cfctrl *ctrl, int cmd, struct cfpkt *pkt)
{
	static int last_linkid;
	static int dec;
	u8 linkid, linktype, tmp;
	switch (cmd) {
	case CFCTRL_CMD_LINK_SETUP:
		spin_lock_bh(&ctrl->loop_linkid_lock);
		if (!dec) {
			for (linkid = last_linkid + 1; linkid < 254; linkid++)
				if (!ctrl->loop_linkused[linkid])
					goto found;
		}
		dec = 1;
		for (linkid = last_linkid - 1; linkid > 1; linkid--)
			if (!ctrl->loop_linkused[linkid])
				goto found;
		spin_unlock_bh(&ctrl->loop_linkid_lock);
		return -1;
found:
		if (linkid < 10)
			dec = 0;

		if (!ctrl->loop_linkused[linkid])
			ctrl->loop_linkused[linkid] = 1;

		last_linkid = linkid;

		cfpkt_add_trail(pkt, &linkid, 1);
		spin_unlock_bh(&ctrl->loop_linkid_lock);
		cfpkt_peek_head(pkt, &linktype, 1);
		if (linktype ==  CFCTRL_SRV_UTIL) {
			tmp = 0x01;
			cfpkt_add_trail(pkt, &tmp, 1);
			cfpkt_add_trail(pkt, &tmp, 1);
		}
		break;

	case CFCTRL_CMD_LINK_DESTROY:
		spin_lock_bh(&ctrl->loop_linkid_lock);
		cfpkt_peek_head(pkt, &linkid, 1);
		ctrl->loop_linkused[linkid] = 0;
		spin_unlock_bh(&ctrl->loop_linkid_lock);
		break;
	default:
		break;
	}
	return 0;
}
#endif
