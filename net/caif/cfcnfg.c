/*
 * Copyright (C) ST-Ericsson AB 2010
 * Author:	Sjur Brendeland/sjur.brandeland@stericsson.com
 * License terms: GNU General Public License (GPL) version 2
 */
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <net/caif/caif_layer.h>
#include <net/caif/cfpkt.h>
#include <net/caif/cfcnfg.h>
#include <net/caif/cfctrl.h>
#include <net/caif/cfmuxl.h>
#include <net/caif/cffrml.h>
#include <net/caif/cfserl.h>
#include <net/caif/cfsrvl.h>

#include <linux/module.h>
#include <asm/atomic.h>

#define MAX_PHY_LAYERS 7
#define PHY_NAME_LEN 20

#define container_obj(layr) container_of(layr, struct cfcnfg, layer)
#define RFM_FRAGMENT_SIZE 4030

/* Information about CAIF physical interfaces held by Config Module in order
 * to manage physical interfaces
 */
struct cfcnfg_phyinfo {
	/* Pointer to the layer below the MUX (framing layer) */
	struct cflayer *frm_layer;
	/* Pointer to the lowest actual physical layer */
	struct cflayer *phy_layer;
	/* Unique identifier of the physical interface */
	unsigned int id;
	/* Preference of the physical in interface */
	enum cfcnfg_phy_preference pref;

	/* Reference count, number of channels using the device */
	int phy_ref_count;

	/* Information about the physical device */
	struct dev_info dev_info;

	/* Interface index */
	int ifindex;

	/* Use Start of frame extension */
	bool use_stx;

	/* Use Start of frame checksum */
	bool use_fcs;
};

struct cfcnfg {
	struct cflayer layer;
	struct cflayer *ctrl;
	struct cflayer *mux;
	u8 last_phyid;
	struct cfcnfg_phyinfo phy_layers[MAX_PHY_LAYERS];
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
	/* Initiate this layer */
	this = kzalloc(sizeof(struct cfcnfg), GFP_ATOMIC);
	if (!this) {
		pr_warning("CAIF: %s(): Out of memory\n", __func__);
		return NULL;
	}
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

	this->last_phyid = 1;

	cfmuxl_set_uplayer(this->mux, this->ctrl, 0);
	layer_set_dn(this->ctrl, this->mux);
	layer_set_up(this->ctrl, this);
	return this;
out_of_mem:
	pr_warning("CAIF: %s(): Out of memory\n", __func__);
	kfree(this->mux);
	kfree(this->ctrl);
	kfree(this);
	return NULL;
}
EXPORT_SYMBOL(cfcnfg_create);

void cfcnfg_remove(struct cfcnfg *cfg)
{
	if (cfg) {
		kfree(cfg->mux);
		kfree(cfg->ctrl);
		kfree(cfg);
	}
}

static void cfctrl_resp_func(void)
{
}

static void cfctrl_enum_resp(void)
{
}

struct dev_info *cfcnfg_get_phyid(struct cfcnfg *cnfg,
				  enum cfcnfg_phy_preference phy_pref)
{
	u16 i;

	/* Try to match with specified preference */
	for (i = 1; i < MAX_PHY_LAYERS; i++) {
		if (cnfg->phy_layers[i].id == i &&
		     cnfg->phy_layers[i].pref == phy_pref &&
		     cnfg->phy_layers[i].frm_layer != NULL) {
			caif_assert(cnfg->phy_layers != NULL);
			caif_assert(cnfg->phy_layers[i].id == i);
			return &cnfg->phy_layers[i].dev_info;
		}
	}
	/* Otherwise just return something */
	for (i = 1; i < MAX_PHY_LAYERS; i++) {
		if (cnfg->phy_layers[i].id == i) {
			caif_assert(cnfg->phy_layers != NULL);
			caif_assert(cnfg->phy_layers[i].id == i);
			return &cnfg->phy_layers[i].dev_info;
		}
	}

	return NULL;
}

static struct cfcnfg_phyinfo *cfcnfg_get_phyinfo(struct cfcnfg *cnfg,
							u8 phyid)
{
	int i;
	/* Try to match with specified preference */
	for (i = 0; i < MAX_PHY_LAYERS; i++)
		if (cnfg->phy_layers[i].frm_layer != NULL &&
		    cnfg->phy_layers[i].id == phyid)
			return &cnfg->phy_layers[i];
	return NULL;
}

int cfcnfg_get_named(struct cfcnfg *cnfg, char *name)
{
	int i;

	/* Try to match with specified name */
	for (i = 0; i < MAX_PHY_LAYERS; i++) {
		if (cnfg->phy_layers[i].frm_layer != NULL
		    && strcmp(cnfg->phy_layers[i].phy_layer->name,
			      name) == 0)
			return cnfg->phy_layers[i].frm_layer->id;
	}
	return 0;
}

int cfcnfg_disconn_adapt_layer(struct cfcnfg *cnfg, struct cflayer *adap_layer)
{
	u8 channel_id = 0;
	int ret = 0;
	struct cflayer *servl = NULL;
	struct cfcnfg_phyinfo *phyinfo = NULL;
	u8 phyid = 0;
	caif_assert(adap_layer != NULL);
	channel_id = adap_layer->id;
	if (adap_layer->dn == NULL || channel_id == 0) {
		pr_err("CAIF: %s():adap_layer->id is 0\n", __func__);
		ret = -ENOTCONN;
		goto end;
	}
	servl = cfmuxl_remove_uplayer(cnfg->mux, channel_id);
	if (servl == NULL)
		goto end;
	layer_set_up(servl, NULL);
	ret = cfctrl_linkdown_req(cnfg->ctrl, channel_id, adap_layer);
	if (servl == NULL) {
		pr_err("CAIF: %s(): PROTOCOL ERROR "
		       "- Error removing service_layer Channel_Id(%d)",
			__func__, channel_id);
		ret = -EINVAL;
		goto end;
	}
	caif_assert(channel_id == servl->id);
	if (adap_layer->dn != NULL) {
		phyid = cfsrvl_getphyid(adap_layer->dn);

		phyinfo = cfcnfg_get_phyinfo(cnfg, phyid);
		if (phyinfo == NULL) {
			pr_warning("CAIF: %s(): "
				"No interface to send disconnect to\n",
				__func__);
			ret = -ENODEV;
			goto end;
		}
		if (phyinfo->id != phyid ||
			phyinfo->phy_layer->id != phyid ||
			phyinfo->frm_layer->id != phyid) {
			pr_err("CAIF: %s(): "
				"Inconsistency in phy registration\n",
				__func__);
			ret = -EINVAL;
			goto end;
		}
	}
	if (phyinfo != NULL && --phyinfo->phy_ref_count == 0 &&
		phyinfo->phy_layer != NULL &&
		phyinfo->phy_layer->modemcmd != NULL) {
		phyinfo->phy_layer->modemcmd(phyinfo->phy_layer,
					     _CAIF_MODEMCMD_PHYIF_USELESS);
	}
end:
	cfsrvl_put(servl);
	cfctrl_cancel_req(cnfg->ctrl, adap_layer);
	if (adap_layer->ctrlcmd != NULL)
		adap_layer->ctrlcmd(adap_layer, CAIF_CTRLCMD_DEINIT_RSP, 0);
	return ret;

}
EXPORT_SYMBOL(cfcnfg_disconn_adapt_layer);

void cfcnfg_release_adap_layer(struct cflayer *adap_layer)
{
	if (adap_layer->dn)
		cfsrvl_put(adap_layer->dn);
}
EXPORT_SYMBOL(cfcnfg_release_adap_layer);

static void cfcnfg_linkdestroy_rsp(struct cflayer *layer, u8 channel_id)
{
}

int protohead[CFCTRL_SRV_MASK] = {
	[CFCTRL_SRV_VEI] = 4,
	[CFCTRL_SRV_DATAGRAM] = 7,
	[CFCTRL_SRV_UTIL] = 4,
	[CFCTRL_SRV_RFM] = 3,
	[CFCTRL_SRV_DBG] = 3,
};

int cfcnfg_add_adaptation_layer(struct cfcnfg *cnfg,
				struct cfctrl_link_param *param,
				struct cflayer *adap_layer,
				int *ifindex,
				int *proto_head,
				int *proto_tail)
{
	struct cflayer *frml;
	if (adap_layer == NULL) {
		pr_err("CAIF: %s(): adap_layer is zero", __func__);
		return -EINVAL;
	}
	if (adap_layer->receive == NULL) {
		pr_err("CAIF: %s(): adap_layer->receive is NULL", __func__);
		return -EINVAL;
	}
	if (adap_layer->ctrlcmd == NULL) {
		pr_err("CAIF: %s(): adap_layer->ctrlcmd == NULL", __func__);
		return -EINVAL;
	}
	frml = cnfg->phy_layers[param->phyid].frm_layer;
	if (frml == NULL) {
		pr_err("CAIF: %s(): Specified PHY type does not exist!",
			__func__);
		return -ENODEV;
	}
	caif_assert(param->phyid == cnfg->phy_layers[param->phyid].id);
	caif_assert(cnfg->phy_layers[param->phyid].frm_layer->id ==
		     param->phyid);
	caif_assert(cnfg->phy_layers[param->phyid].phy_layer->id ==
		     param->phyid);

	*ifindex = cnfg->phy_layers[param->phyid].ifindex;
	*proto_head =
		protohead[param->linktype]+
		(cnfg->phy_layers[param->phyid].use_stx ? 1 : 0);

	*proto_tail = 2;

	/* FIXME: ENUMERATE INITIALLY WHEN ACTIVATING PHYSICAL INTERFACE */
	cfctrl_enum_req(cnfg->ctrl, param->phyid);
	return cfctrl_linkup_request(cnfg->ctrl, param, adap_layer);
}
EXPORT_SYMBOL(cfcnfg_add_adaptation_layer);

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

	if (adapt_layer == NULL) {
		pr_debug("CAIF: %s(): link setup response "
				"but no client exist, send linkdown back\n",
				__func__);
		cfctrl_linkdown_req(cnfg->ctrl, channel_id, NULL);
		return;
	}

	caif_assert(cnfg != NULL);
	caif_assert(phyid != 0);
	phyinfo = &cnfg->phy_layers[phyid];
	caif_assert(phyinfo->id == phyid);
	caif_assert(phyinfo->phy_layer != NULL);
	caif_assert(phyinfo->phy_layer->id == phyid);

	phyinfo->phy_ref_count++;
	if (phyinfo->phy_ref_count == 1 &&
	    phyinfo->phy_layer->modemcmd != NULL) {
		phyinfo->phy_layer->modemcmd(phyinfo->phy_layer,
					     _CAIF_MODEMCMD_PHYIF_USEFULL);
	}
	adapt_layer->id = channel_id;

	switch (serv) {
	case CFCTRL_SRV_VEI:
		servicel = cfvei_create(channel_id, &phyinfo->dev_info);
		break;
	case CFCTRL_SRV_DATAGRAM:
		servicel = cfdgml_create(channel_id, &phyinfo->dev_info);
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
		pr_err("CAIF: %s(): Protocol error. "
			"Link setup response - unknown channel type\n",
			__func__);
		return;
	}
	if (!servicel) {
		pr_warning("CAIF: %s(): Out of memory\n", __func__);
		return;
	}
	layer_set_dn(servicel, cnfg->mux);
	cfmuxl_set_uplayer(cnfg->mux, servicel, channel_id);
	layer_set_up(servicel, adapt_layer);
	layer_set_dn(adapt_layer, servicel);
	cfsrvl_get(servicel);
	servicel->ctrlcmd(servicel, CAIF_CTRLCMD_INIT_RSP, 0);
}

void
cfcnfg_add_phy_layer(struct cfcnfg *cnfg, enum cfcnfg_phy_type phy_type,
		     struct net_device *dev, struct cflayer *phy_layer,
		     u16 *phyid, enum cfcnfg_phy_preference pref,
		     bool fcs, bool stx)
{
	struct cflayer *frml;
	struct cflayer *phy_driver = NULL;
	int i;


	if (cnfg->phy_layers[cnfg->last_phyid].frm_layer == NULL) {
		*phyid = cnfg->last_phyid;

		/* range: * 1..(MAX_PHY_LAYERS-1) */
		cnfg->last_phyid =
		    (cnfg->last_phyid % (MAX_PHY_LAYERS - 1)) + 1;
	} else {
		*phyid = 0;
		for (i = 1; i < MAX_PHY_LAYERS; i++) {
			if (cnfg->phy_layers[i].frm_layer == NULL) {
				*phyid = i;
				break;
			}
		}
	}
	if (*phyid == 0) {
		pr_err("CAIF: %s(): No Available PHY ID\n", __func__);
		return;
	}

	switch (phy_type) {
	case CFPHYTYPE_FRAG:
		phy_driver =
		    cfserl_create(CFPHYTYPE_FRAG, *phyid, stx);
		if (!phy_driver) {
			pr_warning("CAIF: %s(): Out of memory\n", __func__);
			return;
		}

		break;
	case CFPHYTYPE_CAIF:
		phy_driver = NULL;
		break;
	default:
		pr_err("CAIF: %s(): %d", __func__, phy_type);
		return;
		break;
	}

	phy_layer->id = *phyid;
	cnfg->phy_layers[*phyid].pref = pref;
	cnfg->phy_layers[*phyid].id = *phyid;
	cnfg->phy_layers[*phyid].dev_info.id = *phyid;
	cnfg->phy_layers[*phyid].dev_info.dev = dev;
	cnfg->phy_layers[*phyid].phy_layer = phy_layer;
	cnfg->phy_layers[*phyid].phy_ref_count = 0;
	cnfg->phy_layers[*phyid].ifindex = dev->ifindex;
	cnfg->phy_layers[*phyid].use_stx = stx;
	cnfg->phy_layers[*phyid].use_fcs = fcs;

	phy_layer->type = phy_type;
	frml = cffrml_create(*phyid, fcs);
	if (!frml) {
		pr_warning("CAIF: %s(): Out of memory\n", __func__);
		return;
	}
	cnfg->phy_layers[*phyid].frm_layer = frml;
	cfmuxl_set_dnlayer(cnfg->mux, frml, *phyid);
	layer_set_up(frml, cnfg->mux);

	if (phy_driver != NULL) {
		phy_driver->id = *phyid;
		layer_set_dn(frml, phy_driver);
		layer_set_up(phy_driver, frml);
		layer_set_dn(phy_driver, phy_layer);
		layer_set_up(phy_layer, phy_driver);
	} else {
		layer_set_dn(frml, phy_layer);
		layer_set_up(phy_layer, frml);
	}
}
EXPORT_SYMBOL(cfcnfg_add_phy_layer);

int cfcnfg_del_phy_layer(struct cfcnfg *cnfg, struct cflayer *phy_layer)
{
	struct cflayer *frml, *frml_dn;
	u16 phyid;
	phyid = phy_layer->id;
	caif_assert(phyid == cnfg->phy_layers[phyid].id);
	caif_assert(phy_layer == cnfg->phy_layers[phyid].phy_layer);
	caif_assert(phy_layer->id == phyid);
	caif_assert(cnfg->phy_layers[phyid].frm_layer->id == phyid);

	memset(&cnfg->phy_layers[phy_layer->id], 0,
	       sizeof(struct cfcnfg_phyinfo));
	frml = cfmuxl_remove_dnlayer(cnfg->mux, phy_layer->id);
	frml_dn = frml->dn;
	cffrml_set_uplayer(frml, NULL);
	cffrml_set_dnlayer(frml, NULL);
	kfree(frml);

	if (phy_layer != frml_dn) {
		layer_set_up(frml_dn, NULL);
		layer_set_dn(frml_dn, NULL);
		kfree(frml_dn);
	}
	layer_set_up(phy_layer, NULL);
	return 0;
}
EXPORT_SYMBOL(cfcnfg_del_phy_layer);
