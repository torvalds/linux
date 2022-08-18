// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) ST-Ericsson AB 2010
 * Author:	Sjur Brendeland
 */

#define pr_fmt(fmt) KBUILD_MODNAME ":%s(): " fmt, __func__

#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/module.h>
#include <net/caif/caif_layer.h>
#include <net/caif/cfpkt.h>
#include <net/caif/cfcnfg.h>
#include <net/caif/cfctrl.h>
#include <net/caif/cfmuxl.h>
#include <net/caif/cffrml.h>
#include <net/caif/cfserl.h>
#include <net/caif/cfsrvl.h>
#include <net/caif/caif_dev.h>

#define container_obj(layr) container_of(layr, struct cfcnfg, layer)

/* Information about CAIF physical interfaces held by Config Module in order
 * to manage physical interfaces
 */
struct cfcnfg_phyinfo {
	struct list_head node;
	bool up;

	/* Pointer to the layer below the MUX (framing layer) */
	struct cflayer *frm_layer;
	/* Pointer to the lowest actual physical layer */
	struct cflayer *phy_layer;
	/* Unique identifier of the physical interface */
	unsigned int id;
	/* Preference of the physical in interface */
	enum cfcnfg_phy_preference pref;

	/* Information about the physical device */
	struct dev_info dev_info;

	/* Interface index */
	int ifindex;

	/* Protocol head room added for CAIF link layer */
	int head_room;

	/* Use Start of frame checksum */
	bool use_fcs;
};

struct cfcnfg {
	struct cflayer layer;
	struct cflayer *ctrl;
	struct cflayer *mux;
	struct list_head phys;
	struct mutex lock;
};

static void cfcnfg_linkup_rsp(struct cflayer *layer, u8 channel_id,
			      enum cfctrl_srv serv, u8 phyid,
			      struct cflayer *adapt_layer);
static void cfcnfg_linkdestroy_rsp(struct cflayer *layer, u8 channel_id);
static void cfcnfg_reject_rsp(struct cflayer *layer, u8 channel_id,
			      struct cflayer *adapt_layer);
static void cfctrl_resp_func(void);
static void cfctrl_enum_resp(void);

struct cfcnfg *cfcnfg_create(void)
{
	struct cfcnfg *this;
	struct cfctrl_rsp *resp;

	might_sleep();

	/* Initiate this layer */
	this = kzalloc(sizeof(struct cfcnfg), GFP_ATOMIC);
	if (!this)
		return NULL;
	this->mux = cfmuxl_create();
	if (!this->mux)
		goto out_of_mem;
	this->ctrl = cfctrl_create();
	if (!this->ctrl)
		goto out_of_mem;
	/* Initiate response functions */
	resp = cfctrl_get_respfuncs(this->ctrl);
	resp->enum_rsp = cfctrl_enum_resp;
	resp->linkerror_ind = cfctrl_resp_func;
	resp->linkdestroy_rsp = cfcnfg_linkdestroy_rsp;
	resp->sleep_rsp = cfctrl_resp_func;
	resp->wake_rsp = cfctrl_resp_func;
	resp->restart_rsp = cfctrl_resp_func;
	resp->radioset_rsp = cfctrl_resp_func;
	resp->linksetup_rsp = cfcnfg_linkup_rsp;
	resp->reject_rsp = cfcnfg_reject_rsp;
	INIT_LIST_HEAD(&this->phys);

	cfmuxl_set_uplayer(this->mux, this->ctrl, 0);
	layer_set_dn(this->ctrl, this->mux);
	layer_set_up(this->ctrl, this);
	mutex_init(&this->lock);

	return this;
out_of_mem:
	synchronize_rcu();

	kfree(this->mux);
	kfree(this->ctrl);
	kfree(this);
	return NULL;
}

void cfcnfg_remove(struct cfcnfg *cfg)
{
	might_sleep();
	if (cfg) {
		synchronize_rcu();

		kfree(cfg->mux);
		cfctrl_remove(cfg->ctrl);
		kfree(cfg);
	}
}

static void cfctrl_resp_func(void)
{
}

static struct cfcnfg_phyinfo *cfcnfg_get_phyinfo_rcu(struct cfcnfg *cnfg,
						     u8 phyid)
{
	struct cfcnfg_phyinfo *phy;

	list_for_each_entry_rcu(phy, &cnfg->phys, node)
		if (phy->id == phyid)
			return phy;
	return NULL;
}

static void cfctrl_enum_resp(void)
{
}

static struct dev_info *cfcnfg_get_phyid(struct cfcnfg *cnfg,
				  enum cfcnfg_phy_preference phy_pref)
{
	/* Try to match with specified preference */
	struct cfcnfg_phyinfo *phy;

	list_for_each_entry_rcu(phy, &cnfg->phys, node) {
		if (phy->up && phy->pref == phy_pref &&
				phy->frm_layer != NULL)

			return &phy->dev_info;
	}

	/* Otherwise just return something */
	list_for_each_entry_rcu(phy, &cnfg->phys, node)
		if (phy->up)
			return &phy->dev_info;

	return NULL;
}

static int cfcnfg_get_id_from_ifi(struct cfcnfg *cnfg, int ifi)
{
	struct cfcnfg_phyinfo *phy;

	list_for_each_entry_rcu(phy, &cnfg->phys, node)
		if (phy->ifindex == ifi && phy->up)
			return phy->id;
	return -ENODEV;
}

int caif_disconnect_client(struct net *net, struct cflayer *adap_layer)
{
	u8 channel_id;
	struct cfcnfg *cfg = get_cfcnfg(net);

	caif_assert(adap_layer != NULL);
	cfctrl_cancel_req(cfg->ctrl, adap_layer);
	channel_id = adap_layer->id;
	if (channel_id != 0) {
		struct cflayer *servl;
		servl = cfmuxl_remove_uplayer(cfg->mux, channel_id);
		cfctrl_linkdown_req(cfg->ctrl, channel_id, adap_layer);
		if (servl != NULL)
			layer_set_up(servl, NULL);
	} else
		pr_debug("nothing to disconnect\n");

	/* Do RCU sync before initiating cleanup */
	synchronize_rcu();
	if (adap_layer->ctrlcmd != NULL)
		adap_layer->ctrlcmd(adap_layer, CAIF_CTRLCMD_DEINIT_RSP, 0);
	return 0;

}
EXPORT_SYMBOL(caif_disconnect_client);

static void cfcnfg_linkdestroy_rsp(struct cflayer *layer, u8 channel_id)
{
}

static const int protohead[CFCTRL_SRV_MASK] = {
	[CFCTRL_SRV_VEI] = 4,
	[CFCTRL_SRV_DATAGRAM] = 7,
	[CFCTRL_SRV_UTIL] = 4,
	[CFCTRL_SRV_RFM] = 3,
	[CFCTRL_SRV_DBG] = 3,
};


static int caif_connect_req_to_link_param(struct cfcnfg *cnfg,
					  struct caif_connect_request *s,
					  struct cfctrl_link_param *l)
{
	struct dev_info *dev_info;
	enum cfcnfg_phy_preference pref;
	int res;

	memset(l, 0, sizeof(*l));
	/* In caif protocol low value is high priority */
	l->priority = CAIF_PRIO_MAX - s->priority + 1;

	if (s->ifindex != 0) {
		res = cfcnfg_get_id_from_ifi(cnfg, s->ifindex);
		if (res < 0)
			return res;
		l->phyid = res;
	} else {
		switch (s->link_selector) {
		case CAIF_LINK_HIGH_BANDW:
			pref = CFPHYPREF_HIGH_BW;
			break;
		case CAIF_LINK_LOW_LATENCY:
			pref = CFPHYPREF_LOW_LAT;
			break;
		default:
			return -EINVAL;
		}
		dev_info = cfcnfg_get_phyid(cnfg, pref);
		if (dev_info == NULL)
			return -ENODEV;
		l->phyid = dev_info->id;
	}
	switch (s->protocol) {
	case CAIFPROTO_AT:
		l->linktype = CFCTRL_SRV_VEI;
		l->endpoint = (s->sockaddr.u.at.type >> 2) & 0x3;
		l->chtype = s->sockaddr.u.at.type & 0x3;
		break;
	case CAIFPROTO_DATAGRAM:
		l->linktype = CFCTRL_SRV_DATAGRAM;
		l->chtype = 0x00;
		l->u.datagram.connid = s->sockaddr.u.dgm.connection_id;
		break;
	case CAIFPROTO_DATAGRAM_LOOP:
		l->linktype = CFCTRL_SRV_DATAGRAM;
		l->chtype = 0x03;
		l->endpoint = 0x00;
		l->u.datagram.connid = s->sockaddr.u.dgm.connection_id;
		break;
	case CAIFPROTO_RFM:
		l->linktype = CFCTRL_SRV_RFM;
		l->u.datagram.connid = s->sockaddr.u.rfm.connection_id;
		strscpy(l->u.rfm.volume, s->sockaddr.u.rfm.volume,
			sizeof(l->u.rfm.volume));
		break;
	case CAIFPROTO_UTIL:
		l->linktype = CFCTRL_SRV_UTIL;
		l->endpoint = 0x00;
		l->chtype = 0x00;
		strscpy(l->u.utility.name, s->sockaddr.u.util.service,
			sizeof(l->u.utility.name));
		caif_assert(sizeof(l->u.utility.name) > 10);
		l->u.utility.paramlen = s->param.size;
		if (l->u.utility.paramlen > sizeof(l->u.utility.params))
			l->u.utility.paramlen = sizeof(l->u.utility.params);

		memcpy(l->u.utility.params, s->param.data,
		       l->u.utility.paramlen);

		break;
	case CAIFPROTO_DEBUG:
		l->linktype = CFCTRL_SRV_DBG;
		l->endpoint = s->sockaddr.u.dbg.service;
		l->chtype = s->sockaddr.u.dbg.type;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

int caif_connect_client(struct net *net, struct caif_connect_request *conn_req,
			struct cflayer *adap_layer, int *ifindex,
			int *proto_head, int *proto_tail)
{
	struct cflayer *frml;
	struct cfcnfg_phyinfo *phy;
	int err;
	struct cfctrl_link_param param;
	struct cfcnfg *cfg = get_cfcnfg(net);

	rcu_read_lock();
	err = caif_connect_req_to_link_param(cfg, conn_req, &param);
	if (err)
		goto unlock;

	phy = cfcnfg_get_phyinfo_rcu(cfg, param.phyid);
	if (!phy) {
		err = -ENODEV;
		goto unlock;
	}
	err = -EINVAL;

	if (adap_layer == NULL) {
		pr_err("adap_layer is zero\n");
		goto unlock;
	}
	if (adap_layer->receive == NULL) {
		pr_err("adap_layer->receive is NULL\n");
		goto unlock;
	}
	if (adap_layer->ctrlcmd == NULL) {
		pr_err("adap_layer->ctrlcmd == NULL\n");
		goto unlock;
	}

	err = -ENODEV;
	frml = phy->frm_layer;
	if (frml == NULL) {
		pr_err("Specified PHY type does not exist!\n");
		goto unlock;
	}
	caif_assert(param.phyid == phy->id);
	caif_assert(phy->frm_layer->id ==
		     param.phyid);
	caif_assert(phy->phy_layer->id ==
		     param.phyid);

	*ifindex = phy->ifindex;
	*proto_tail = 2;
	*proto_head = protohead[param.linktype] + phy->head_room;

	rcu_read_unlock();

	/* FIXME: ENUMERATE INITIALLY WHEN ACTIVATING PHYSICAL INTERFACE */
	cfctrl_enum_req(cfg->ctrl, param.phyid);
	return cfctrl_linkup_request(cfg->ctrl, &param, adap_layer);

unlock:
	rcu_read_unlock();
	return err;
}
EXPORT_SYMBOL(caif_connect_client);

static void cfcnfg_reject_rsp(struct cflayer *layer, u8 channel_id,
			      struct cflayer *adapt_layer)
{
	if (adapt_layer != NULL && adapt_layer->ctrlcmd != NULL)
		adapt_layer->ctrlcmd(adapt_layer,
				     CAIF_CTRLCMD_INIT_FAIL_RSP, 0);
}

static void
cfcnfg_linkup_rsp(struct cflayer *layer, u8 channel_id, enum cfctrl_srv serv,
		  u8 phyid, struct cflayer *adapt_layer)
{
	struct cfcnfg *cnfg = container_obj(layer);
	struct cflayer *servicel = NULL;
	struct cfcnfg_phyinfo *phyinfo;
	struct net_device *netdev;

	if (channel_id == 0) {
		pr_warn("received channel_id zero\n");
		if (adapt_layer != NULL && adapt_layer->ctrlcmd != NULL)
			adapt_layer->ctrlcmd(adapt_layer,
						CAIF_CTRLCMD_INIT_FAIL_RSP, 0);
		return;
	}

	rcu_read_lock();

	if (adapt_layer == NULL) {
		pr_debug("link setup response but no client exist, send linkdown back\n");
		cfctrl_linkdown_req(cnfg->ctrl, channel_id, NULL);
		goto unlock;
	}

	caif_assert(cnfg != NULL);
	caif_assert(phyid != 0);

	phyinfo = cfcnfg_get_phyinfo_rcu(cnfg, phyid);
	if (phyinfo == NULL) {
		pr_err("ERROR: Link Layer Device disappeared while connecting\n");
		goto unlock;
	}

	caif_assert(phyinfo != NULL);
	caif_assert(phyinfo->id == phyid);
	caif_assert(phyinfo->phy_layer != NULL);
	caif_assert(phyinfo->phy_layer->id == phyid);

	adapt_layer->id = channel_id;

	switch (serv) {
	case CFCTRL_SRV_VEI:
		servicel = cfvei_create(channel_id, &phyinfo->dev_info);
		break;
	case CFCTRL_SRV_DATAGRAM:
		servicel = cfdgml_create(channel_id,
					&phyinfo->dev_info);
		break;
	case CFCTRL_SRV_RFM:
		netdev = phyinfo->dev_info.dev;
		servicel = cfrfml_create(channel_id, &phyinfo->dev_info,
						netdev->mtu);
		break;
	case CFCTRL_SRV_UTIL:
		servicel = cfutill_create(channel_id, &phyinfo->dev_info);
		break;
	case CFCTRL_SRV_VIDEO:
		servicel = cfvidl_create(channel_id, &phyinfo->dev_info);
		break;
	case CFCTRL_SRV_DBG:
		servicel = cfdbgl_create(channel_id, &phyinfo->dev_info);
		break;
	default:
		pr_err("Protocol error. Link setup response - unknown channel type\n");
		goto unlock;
	}
	if (!servicel)
		goto unlock;
	layer_set_dn(servicel, cnfg->mux);
	cfmuxl_set_uplayer(cnfg->mux, servicel, channel_id);
	layer_set_up(servicel, adapt_layer);
	layer_set_dn(adapt_layer, servicel);

	rcu_read_unlock();

	servicel->ctrlcmd(servicel, CAIF_CTRLCMD_INIT_RSP, 0);
	return;
unlock:
	rcu_read_unlock();
}

int
cfcnfg_add_phy_layer(struct cfcnfg *cnfg,
		     struct net_device *dev, struct cflayer *phy_layer,
		     enum cfcnfg_phy_preference pref,
		     struct cflayer *link_support,
		     bool fcs, int head_room)
{
	struct cflayer *frml;
	struct cfcnfg_phyinfo *phyinfo = NULL;
	int i, res = 0;
	u8 phyid;

	mutex_lock(&cnfg->lock);

	/* CAIF protocol allow maximum 6 link-layers */
	for (i = 0; i < 7; i++) {
		phyid = (dev->ifindex + i) & 0x7;
		if (phyid == 0)
			continue;
		if (cfcnfg_get_phyinfo_rcu(cnfg, phyid) == NULL)
			goto got_phyid;
	}
	pr_warn("Too many CAIF Link Layers (max 6)\n");
	res = -EEXIST;
	goto out;

got_phyid:
	phyinfo = kzalloc(sizeof(struct cfcnfg_phyinfo), GFP_ATOMIC);
	if (!phyinfo) {
		res = -ENOMEM;
		goto out;
	}

	phy_layer->id = phyid;
	phyinfo->pref = pref;
	phyinfo->id = phyid;
	phyinfo->dev_info.id = phyid;
	phyinfo->dev_info.dev = dev;
	phyinfo->phy_layer = phy_layer;
	phyinfo->ifindex = dev->ifindex;
	phyinfo->head_room = head_room;
	phyinfo->use_fcs = fcs;

	frml = cffrml_create(phyid, fcs);

	if (!frml) {
		res = -ENOMEM;
		goto out_err;
	}
	phyinfo->frm_layer = frml;
	layer_set_up(frml, cnfg->mux);

	if (link_support != NULL) {
		link_support->id = phyid;
		layer_set_dn(frml, link_support);
		layer_set_up(link_support, frml);
		layer_set_dn(link_support, phy_layer);
		layer_set_up(phy_layer, link_support);
	} else {
		layer_set_dn(frml, phy_layer);
		layer_set_up(phy_layer, frml);
	}

	list_add_rcu(&phyinfo->node, &cnfg->phys);
out:
	mutex_unlock(&cnfg->lock);
	return res;

out_err:
	kfree(phyinfo);
	mutex_unlock(&cnfg->lock);
	return res;
}
EXPORT_SYMBOL(cfcnfg_add_phy_layer);

int cfcnfg_set_phy_state(struct cfcnfg *cnfg, struct cflayer *phy_layer,
			 bool up)
{
	struct cfcnfg_phyinfo *phyinfo;

	rcu_read_lock();
	phyinfo = cfcnfg_get_phyinfo_rcu(cnfg, phy_layer->id);
	if (phyinfo == NULL) {
		rcu_read_unlock();
		return -ENODEV;
	}

	if (phyinfo->up == up) {
		rcu_read_unlock();
		return 0;
	}
	phyinfo->up = up;

	if (up) {
		cffrml_hold(phyinfo->frm_layer);
		cfmuxl_set_dnlayer(cnfg->mux, phyinfo->frm_layer,
					phy_layer->id);
	} else {
		cfmuxl_remove_dnlayer(cnfg->mux, phy_layer->id);
		cffrml_put(phyinfo->frm_layer);
	}

	rcu_read_unlock();
	return 0;
}
EXPORT_SYMBOL(cfcnfg_set_phy_state);

int cfcnfg_del_phy_layer(struct cfcnfg *cnfg, struct cflayer *phy_layer)
{
	struct cflayer *frml, *frml_dn;
	u16 phyid;
	struct cfcnfg_phyinfo *phyinfo;

	might_sleep();

	mutex_lock(&cnfg->lock);

	phyid = phy_layer->id;
	phyinfo = cfcnfg_get_phyinfo_rcu(cnfg, phyid);

	if (phyinfo == NULL) {
		mutex_unlock(&cnfg->lock);
		return 0;
	}
	caif_assert(phyid == phyinfo->id);
	caif_assert(phy_layer == phyinfo->phy_layer);
	caif_assert(phy_layer->id == phyid);
	caif_assert(phyinfo->frm_layer->id == phyid);

	list_del_rcu(&phyinfo->node);
	synchronize_rcu();

	/* Fail if reference count is not zero */
	if (cffrml_refcnt_read(phyinfo->frm_layer) != 0) {
		pr_info("Wait for device inuse\n");
		list_add_rcu(&phyinfo->node, &cnfg->phys);
		mutex_unlock(&cnfg->lock);
		return -EAGAIN;
	}

	frml = phyinfo->frm_layer;
	frml_dn = frml->dn;
	cffrml_set_uplayer(frml, NULL);
	cffrml_set_dnlayer(frml, NULL);
	if (phy_layer != frml_dn) {
		layer_set_up(frml_dn, NULL);
		layer_set_dn(frml_dn, NULL);
	}
	layer_set_up(phy_layer, NULL);

	if (phyinfo->phy_layer != frml_dn)
		kfree(frml_dn);

	cffrml_free(frml);
	kfree(phyinfo);
	mutex_unlock(&cnfg->lock);

	return 0;
}
EXPORT_SYMBOL(cfcnfg_del_phy_layer);
