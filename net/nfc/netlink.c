// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2011 Instituto Nokia de Tecnologia
 *
 * Authors:
 *    Lauro Ramos Venancio <lauro.venancio@openbossa.org>
 *    Aloisio Almeida Jr <aloisio.almeida@openbossa.org>
 *
 * Vendor commands implementation based on net/wireless/nl80211.c
 * which is:
 *
 * Copyright 2006-2010	Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2013-2014  Intel Mobile Communications GmbH
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": %s: " fmt, __func__

#include <net/genetlink.h>
#include <linux/nfc.h>
#include <linux/slab.h>

#include "nfc.h"
#include "llcp.h"

static const struct genl_multicast_group nfc_genl_mcgrps[] = {
	{ .name = NFC_GENL_MCAST_EVENT_NAME, },
};

static struct genl_family nfc_genl_family;
static const struct nla_policy nfc_genl_policy[NFC_ATTR_MAX + 1] = {
	[NFC_ATTR_DEVICE_INDEX] = { .type = NLA_U32 },
	[NFC_ATTR_DEVICE_NAME] = { .type = NLA_STRING,
				.len = NFC_DEVICE_NAME_MAXSIZE },
	[NFC_ATTR_PROTOCOLS] = { .type = NLA_U32 },
	[NFC_ATTR_TARGET_INDEX] = { .type = NLA_U32 },
	[NFC_ATTR_COMM_MODE] = { .type = NLA_U8 },
	[NFC_ATTR_RF_MODE] = { .type = NLA_U8 },
	[NFC_ATTR_DEVICE_POWERED] = { .type = NLA_U8 },
	[NFC_ATTR_IM_PROTOCOLS] = { .type = NLA_U32 },
	[NFC_ATTR_TM_PROTOCOLS] = { .type = NLA_U32 },
	[NFC_ATTR_LLC_PARAM_LTO] = { .type = NLA_U8 },
	[NFC_ATTR_LLC_PARAM_RW] = { .type = NLA_U8 },
	[NFC_ATTR_LLC_PARAM_MIUX] = { .type = NLA_U16 },
	[NFC_ATTR_LLC_SDP] = { .type = NLA_NESTED },
	[NFC_ATTR_FIRMWARE_NAME] = { .type = NLA_STRING,
				     .len = NFC_FIRMWARE_NAME_MAXSIZE },
	[NFC_ATTR_SE_INDEX] = { .type = NLA_U32 },
	[NFC_ATTR_SE_APDU] = { .type = NLA_BINARY },
	[NFC_ATTR_VENDOR_ID] = { .type = NLA_U32 },
	[NFC_ATTR_VENDOR_SUBCMD] = { .type = NLA_U32 },
	[NFC_ATTR_VENDOR_DATA] = { .type = NLA_BINARY },

};

static const struct nla_policy nfc_sdp_genl_policy[NFC_SDP_ATTR_MAX + 1] = {
	[NFC_SDP_ATTR_URI] = { .type = NLA_STRING,
			       .len = U8_MAX - 4 },
	[NFC_SDP_ATTR_SAP] = { .type = NLA_U8 },
};

static int nfc_genl_send_target(struct sk_buff *msg, struct nfc_target *target,
				struct netlink_callback *cb, int flags)
{
	void *hdr;

	hdr = genlmsg_put(msg, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			  &nfc_genl_family, flags, NFC_CMD_GET_TARGET);
	if (!hdr)
		return -EMSGSIZE;

	genl_dump_check_consistent(cb, hdr);

	if (nla_put_u32(msg, NFC_ATTR_TARGET_INDEX, target->idx) ||
	    nla_put_u32(msg, NFC_ATTR_PROTOCOLS, target->supported_protocols) ||
	    nla_put_u16(msg, NFC_ATTR_TARGET_SENS_RES, target->sens_res) ||
	    nla_put_u8(msg, NFC_ATTR_TARGET_SEL_RES, target->sel_res))
		goto nla_put_failure;
	if (target->nfcid1_len > 0 &&
	    nla_put(msg, NFC_ATTR_TARGET_NFCID1, target->nfcid1_len,
		    target->nfcid1))
		goto nla_put_failure;
	if (target->sensb_res_len > 0 &&
	    nla_put(msg, NFC_ATTR_TARGET_SENSB_RES, target->sensb_res_len,
		    target->sensb_res))
		goto nla_put_failure;
	if (target->sensf_res_len > 0 &&
	    nla_put(msg, NFC_ATTR_TARGET_SENSF_RES, target->sensf_res_len,
		    target->sensf_res))
		goto nla_put_failure;

	if (target->is_iso15693) {
		if (nla_put_u8(msg, NFC_ATTR_TARGET_ISO15693_DSFID,
			       target->iso15693_dsfid) ||
		    nla_put(msg, NFC_ATTR_TARGET_ISO15693_UID,
			    sizeof(target->iso15693_uid), target->iso15693_uid))
			goto nla_put_failure;
	}

	if (target->ats_len > 0 &&
	    nla_put(msg, NFC_ATTR_TARGET_ATS, target->ats_len,
		    target->ats))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static struct nfc_dev *__get_device_from_cb(struct netlink_callback *cb)
{
	const struct genl_dumpit_info *info = genl_dumpit_info(cb);
	struct nfc_dev *dev;
	u32 idx;

	if (!info->info.attrs[NFC_ATTR_DEVICE_INDEX])
		return ERR_PTR(-EINVAL);

	idx = nla_get_u32(info->info.attrs[NFC_ATTR_DEVICE_INDEX]);

	dev = nfc_get_device(idx);
	if (!dev)
		return ERR_PTR(-ENODEV);

	return dev;
}

static int nfc_genl_dump_targets(struct sk_buff *skb,
				 struct netlink_callback *cb)
{
	int i = cb->args[0];
	struct nfc_dev *dev = (struct nfc_dev *) cb->args[1];
	int rc;

	if (!dev) {
		dev = __get_device_from_cb(cb);
		if (IS_ERR(dev))
			return PTR_ERR(dev);

		cb->args[1] = (long) dev;
	}

	device_lock(&dev->dev);

	cb->seq = dev->targets_generation;

	while (i < dev->n_targets) {
		rc = nfc_genl_send_target(skb, &dev->targets[i], cb,
					  NLM_F_MULTI);
		if (rc < 0)
			break;

		i++;
	}

	device_unlock(&dev->dev);

	cb->args[0] = i;

	return skb->len;
}

static int nfc_genl_dump_targets_done(struct netlink_callback *cb)
{
	struct nfc_dev *dev = (struct nfc_dev *) cb->args[1];

	if (dev)
		nfc_put_device(dev);

	return 0;
}

int nfc_genl_targets_found(struct nfc_dev *dev)
{
	struct sk_buff *msg;
	void *hdr;

	dev->genl_data.poll_req_portid = 0;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put(msg, 0, 0, &nfc_genl_family, 0,
			  NFC_EVENT_TARGETS_FOUND);
	if (!hdr)
		goto free_msg;

	if (nla_put_u32(msg, NFC_ATTR_DEVICE_INDEX, dev->idx))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	return genlmsg_multicast(&nfc_genl_family, msg, 0, 0, GFP_ATOMIC);

nla_put_failure:
free_msg:
	nlmsg_free(msg);
	return -EMSGSIZE;
}

int nfc_genl_target_lost(struct nfc_dev *dev, u32 target_idx)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put(msg, 0, 0, &nfc_genl_family, 0,
			  NFC_EVENT_TARGET_LOST);
	if (!hdr)
		goto free_msg;

	if (nla_put_string(msg, NFC_ATTR_DEVICE_NAME, nfc_device_name(dev)) ||
	    nla_put_u32(msg, NFC_ATTR_TARGET_INDEX, target_idx))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_multicast(&nfc_genl_family, msg, 0, 0, GFP_KERNEL);

	return 0;

nla_put_failure:
free_msg:
	nlmsg_free(msg);
	return -EMSGSIZE;
}

int nfc_genl_tm_activated(struct nfc_dev *dev, u32 protocol)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put(msg, 0, 0, &nfc_genl_family, 0,
			  NFC_EVENT_TM_ACTIVATED);
	if (!hdr)
		goto free_msg;

	if (nla_put_u32(msg, NFC_ATTR_DEVICE_INDEX, dev->idx))
		goto nla_put_failure;
	if (nla_put_u32(msg, NFC_ATTR_TM_PROTOCOLS, protocol))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_multicast(&nfc_genl_family, msg, 0, 0, GFP_KERNEL);

	return 0;

nla_put_failure:
free_msg:
	nlmsg_free(msg);
	return -EMSGSIZE;
}

int nfc_genl_tm_deactivated(struct nfc_dev *dev)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put(msg, 0, 0, &nfc_genl_family, 0,
			  NFC_EVENT_TM_DEACTIVATED);
	if (!hdr)
		goto free_msg;

	if (nla_put_u32(msg, NFC_ATTR_DEVICE_INDEX, dev->idx))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_multicast(&nfc_genl_family, msg, 0, 0, GFP_KERNEL);

	return 0;

nla_put_failure:
free_msg:
	nlmsg_free(msg);
	return -EMSGSIZE;
}

static int nfc_genl_setup_device_added(struct nfc_dev *dev, struct sk_buff *msg)
{
	if (nla_put_string(msg, NFC_ATTR_DEVICE_NAME, nfc_device_name(dev)) ||
	    nla_put_u32(msg, NFC_ATTR_DEVICE_INDEX, dev->idx) ||
	    nla_put_u32(msg, NFC_ATTR_PROTOCOLS, dev->supported_protocols) ||
	    nla_put_u8(msg, NFC_ATTR_DEVICE_POWERED, dev->dev_up) ||
	    nla_put_u8(msg, NFC_ATTR_RF_MODE, dev->rf_mode))
		return -1;
	return 0;
}

int nfc_genl_device_added(struct nfc_dev *dev)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put(msg, 0, 0, &nfc_genl_family, 0,
			  NFC_EVENT_DEVICE_ADDED);
	if (!hdr)
		goto free_msg;

	if (nfc_genl_setup_device_added(dev, msg))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_multicast(&nfc_genl_family, msg, 0, 0, GFP_KERNEL);

	return 0;

nla_put_failure:
free_msg:
	nlmsg_free(msg);
	return -EMSGSIZE;
}

int nfc_genl_device_removed(struct nfc_dev *dev)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put(msg, 0, 0, &nfc_genl_family, 0,
			  NFC_EVENT_DEVICE_REMOVED);
	if (!hdr)
		goto free_msg;

	if (nla_put_u32(msg, NFC_ATTR_DEVICE_INDEX, dev->idx))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_multicast(&nfc_genl_family, msg, 0, 0, GFP_KERNEL);

	return 0;

nla_put_failure:
free_msg:
	nlmsg_free(msg);
	return -EMSGSIZE;
}

int nfc_genl_llc_send_sdres(struct nfc_dev *dev, struct hlist_head *sdres_list)
{
	struct sk_buff *msg;
	struct nlattr *sdp_attr, *uri_attr;
	struct nfc_llcp_sdp_tlv *sdres;
	struct hlist_node *n;
	void *hdr;
	int rc = -EMSGSIZE;
	int i;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put(msg, 0, 0, &nfc_genl_family, 0,
			  NFC_EVENT_LLC_SDRES);
	if (!hdr)
		goto free_msg;

	if (nla_put_u32(msg, NFC_ATTR_DEVICE_INDEX, dev->idx))
		goto nla_put_failure;

	sdp_attr = nla_nest_start_noflag(msg, NFC_ATTR_LLC_SDP);
	if (sdp_attr == NULL) {
		rc = -ENOMEM;
		goto nla_put_failure;
	}

	i = 1;
	hlist_for_each_entry_safe(sdres, n, sdres_list, node) {
		pr_debug("uri: %s, sap: %d\n", sdres->uri, sdres->sap);

		uri_attr = nla_nest_start_noflag(msg, i++);
		if (uri_attr == NULL) {
			rc = -ENOMEM;
			goto nla_put_failure;
		}

		if (nla_put_u8(msg, NFC_SDP_ATTR_SAP, sdres->sap))
			goto nla_put_failure;

		if (nla_put_string(msg, NFC_SDP_ATTR_URI, sdres->uri))
			goto nla_put_failure;

		nla_nest_end(msg, uri_attr);

		hlist_del(&sdres->node);

		nfc_llcp_free_sdp_tlv(sdres);
	}

	nla_nest_end(msg, sdp_attr);

	genlmsg_end(msg, hdr);

	return genlmsg_multicast(&nfc_genl_family, msg, 0, 0, GFP_ATOMIC);

nla_put_failure:
free_msg:
	nlmsg_free(msg);

	nfc_llcp_free_sdp_tlv_list(sdres_list);

	return rc;
}

int nfc_genl_se_added(struct nfc_dev *dev, u32 se_idx, u16 type)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put(msg, 0, 0, &nfc_genl_family, 0,
			  NFC_EVENT_SE_ADDED);
	if (!hdr)
		goto free_msg;

	if (nla_put_u32(msg, NFC_ATTR_DEVICE_INDEX, dev->idx) ||
	    nla_put_u32(msg, NFC_ATTR_SE_INDEX, se_idx) ||
	    nla_put_u8(msg, NFC_ATTR_SE_TYPE, type))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_multicast(&nfc_genl_family, msg, 0, 0, GFP_KERNEL);

	return 0;

nla_put_failure:
free_msg:
	nlmsg_free(msg);
	return -EMSGSIZE;
}

int nfc_genl_se_removed(struct nfc_dev *dev, u32 se_idx)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put(msg, 0, 0, &nfc_genl_family, 0,
			  NFC_EVENT_SE_REMOVED);
	if (!hdr)
		goto free_msg;

	if (nla_put_u32(msg, NFC_ATTR_DEVICE_INDEX, dev->idx) ||
	    nla_put_u32(msg, NFC_ATTR_SE_INDEX, se_idx))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_multicast(&nfc_genl_family, msg, 0, 0, GFP_KERNEL);

	return 0;

nla_put_failure:
free_msg:
	nlmsg_free(msg);
	return -EMSGSIZE;
}

int nfc_genl_se_transaction(struct nfc_dev *dev, u8 se_idx,
			    struct nfc_evt_transaction *evt_transaction)
{
	struct nfc_se *se;
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put(msg, 0, 0, &nfc_genl_family, 0,
			  NFC_EVENT_SE_TRANSACTION);
	if (!hdr)
		goto free_msg;

	se = nfc_find_se(dev, se_idx);
	if (!se)
		goto free_msg;

	if (nla_put_u32(msg, NFC_ATTR_DEVICE_INDEX, dev->idx) ||
	    nla_put_u32(msg, NFC_ATTR_SE_INDEX, se_idx) ||
	    nla_put_u8(msg, NFC_ATTR_SE_TYPE, se->type) ||
	    nla_put(msg, NFC_ATTR_SE_AID, evt_transaction->aid_len,
		    evt_transaction->aid) ||
	    nla_put(msg, NFC_ATTR_SE_PARAMS, evt_transaction->params_len,
		    evt_transaction->params))
		goto nla_put_failure;

	/* evt_transaction is no more used */
	devm_kfree(&dev->dev, evt_transaction);

	genlmsg_end(msg, hdr);

	genlmsg_multicast(&nfc_genl_family, msg, 0, 0, GFP_KERNEL);

	return 0;

nla_put_failure:
free_msg:
	/* evt_transaction is no more used */
	devm_kfree(&dev->dev, evt_transaction);
	nlmsg_free(msg);
	return -EMSGSIZE;
}

int nfc_genl_se_connectivity(struct nfc_dev *dev, u8 se_idx)
{
	const struct nfc_se *se;
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put(msg, 0, 0, &nfc_genl_family, 0,
			  NFC_EVENT_SE_CONNECTIVITY);
	if (!hdr)
		goto free_msg;

	se = nfc_find_se(dev, se_idx);
	if (!se)
		goto free_msg;

	if (nla_put_u32(msg, NFC_ATTR_DEVICE_INDEX, dev->idx) ||
	    nla_put_u32(msg, NFC_ATTR_SE_INDEX, se_idx) ||
	    nla_put_u8(msg, NFC_ATTR_SE_TYPE, se->type))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_multicast(&nfc_genl_family, msg, 0, 0, GFP_KERNEL);

	return 0;

nla_put_failure:
free_msg:
	nlmsg_free(msg);
	return -EMSGSIZE;
}

static int nfc_genl_send_device(struct sk_buff *msg, struct nfc_dev *dev,
				u32 portid, u32 seq,
				struct netlink_callback *cb,
				int flags)
{
	void *hdr;

	hdr = genlmsg_put(msg, portid, seq, &nfc_genl_family, flags,
			  NFC_CMD_GET_DEVICE);
	if (!hdr)
		return -EMSGSIZE;

	if (cb)
		genl_dump_check_consistent(cb, hdr);

	if (nfc_genl_setup_device_added(dev, msg))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int nfc_genl_dump_devices(struct sk_buff *skb,
				 struct netlink_callback *cb)
{
	struct class_dev_iter *iter = (struct class_dev_iter *) cb->args[0];
	struct nfc_dev *dev = (struct nfc_dev *) cb->args[1];
	bool first_call = false;

	if (!iter) {
		first_call = true;
		iter = kmalloc(sizeof(struct class_dev_iter), GFP_KERNEL);
		if (!iter)
			return -ENOMEM;
		cb->args[0] = (long) iter;
	}

	mutex_lock(&nfc_devlist_mutex);

	cb->seq = nfc_devlist_generation;

	if (first_call) {
		nfc_device_iter_init(iter);
		dev = nfc_device_iter_next(iter);
	}

	while (dev) {
		int rc;

		rc = nfc_genl_send_device(skb, dev, NETLINK_CB(cb->skb).portid,
					  cb->nlh->nlmsg_seq, cb, NLM_F_MULTI);
		if (rc < 0)
			break;

		dev = nfc_device_iter_next(iter);
	}

	mutex_unlock(&nfc_devlist_mutex);

	cb->args[1] = (long) dev;

	return skb->len;
}

static int nfc_genl_dump_devices_done(struct netlink_callback *cb)
{
	struct class_dev_iter *iter = (struct class_dev_iter *) cb->args[0];

	if (iter) {
		nfc_device_iter_exit(iter);
		kfree(iter);
	}

	return 0;
}

int nfc_genl_dep_link_up_event(struct nfc_dev *dev, u32 target_idx,
			       u8 comm_mode, u8 rf_mode)
{
	struct sk_buff *msg;
	void *hdr;

	pr_debug("DEP link is up\n");

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put(msg, 0, 0, &nfc_genl_family, 0, NFC_CMD_DEP_LINK_UP);
	if (!hdr)
		goto free_msg;

	if (nla_put_u32(msg, NFC_ATTR_DEVICE_INDEX, dev->idx))
		goto nla_put_failure;
	if (rf_mode == NFC_RF_INITIATOR &&
	    nla_put_u32(msg, NFC_ATTR_TARGET_INDEX, target_idx))
		goto nla_put_failure;
	if (nla_put_u8(msg, NFC_ATTR_COMM_MODE, comm_mode) ||
	    nla_put_u8(msg, NFC_ATTR_RF_MODE, rf_mode))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	dev->dep_link_up = true;

	genlmsg_multicast(&nfc_genl_family, msg, 0, 0, GFP_ATOMIC);

	return 0;

nla_put_failure:
free_msg:
	nlmsg_free(msg);
	return -EMSGSIZE;
}

int nfc_genl_dep_link_down_event(struct nfc_dev *dev)
{
	struct sk_buff *msg;
	void *hdr;

	pr_debug("DEP link is down\n");

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put(msg, 0, 0, &nfc_genl_family, 0,
			  NFC_CMD_DEP_LINK_DOWN);
	if (!hdr)
		goto free_msg;

	if (nla_put_u32(msg, NFC_ATTR_DEVICE_INDEX, dev->idx))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_multicast(&nfc_genl_family, msg, 0, 0, GFP_ATOMIC);

	return 0;

nla_put_failure:
free_msg:
	nlmsg_free(msg);
	return -EMSGSIZE;
}

static int nfc_genl_get_device(struct sk_buff *skb, struct genl_info *info)
{
	struct sk_buff *msg;
	struct nfc_dev *dev;
	u32 idx;
	int rc = -ENOBUFS;

	if (!info->attrs[NFC_ATTR_DEVICE_INDEX])
		return -EINVAL;

	idx = nla_get_u32(info->attrs[NFC_ATTR_DEVICE_INDEX]);

	dev = nfc_get_device(idx);
	if (!dev)
		return -ENODEV;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		rc = -ENOMEM;
		goto out_putdev;
	}

	rc = nfc_genl_send_device(msg, dev, info->snd_portid, info->snd_seq,
				  NULL, 0);
	if (rc < 0)
		goto out_free;

	nfc_put_device(dev);

	return genlmsg_reply(msg, info);

out_free:
	nlmsg_free(msg);
out_putdev:
	nfc_put_device(dev);
	return rc;
}

static int nfc_genl_dev_up(struct sk_buff *skb, struct genl_info *info)
{
	struct nfc_dev *dev;
	int rc;
	u32 idx;

	if (!info->attrs[NFC_ATTR_DEVICE_INDEX])
		return -EINVAL;

	idx = nla_get_u32(info->attrs[NFC_ATTR_DEVICE_INDEX]);

	dev = nfc_get_device(idx);
	if (!dev)
		return -ENODEV;

	rc = nfc_dev_up(dev);

	nfc_put_device(dev);
	return rc;
}

static int nfc_genl_dev_down(struct sk_buff *skb, struct genl_info *info)
{
	struct nfc_dev *dev;
	int rc;
	u32 idx;

	if (!info->attrs[NFC_ATTR_DEVICE_INDEX])
		return -EINVAL;

	idx = nla_get_u32(info->attrs[NFC_ATTR_DEVICE_INDEX]);

	dev = nfc_get_device(idx);
	if (!dev)
		return -ENODEV;

	rc = nfc_dev_down(dev);

	nfc_put_device(dev);
	return rc;
}

static int nfc_genl_start_poll(struct sk_buff *skb, struct genl_info *info)
{
	struct nfc_dev *dev;
	int rc;
	u32 idx;
	u32 im_protocols = 0, tm_protocols = 0;

	pr_debug("Poll start\n");

	if (!info->attrs[NFC_ATTR_DEVICE_INDEX] ||
	    ((!info->attrs[NFC_ATTR_IM_PROTOCOLS] &&
	      !info->attrs[NFC_ATTR_PROTOCOLS]) &&
	      !info->attrs[NFC_ATTR_TM_PROTOCOLS]))
		return -EINVAL;

	idx = nla_get_u32(info->attrs[NFC_ATTR_DEVICE_INDEX]);

	if (info->attrs[NFC_ATTR_TM_PROTOCOLS])
		tm_protocols = nla_get_u32(info->attrs[NFC_ATTR_TM_PROTOCOLS]);

	if (info->attrs[NFC_ATTR_IM_PROTOCOLS])
		im_protocols = nla_get_u32(info->attrs[NFC_ATTR_IM_PROTOCOLS]);
	else if (info->attrs[NFC_ATTR_PROTOCOLS])
		im_protocols = nla_get_u32(info->attrs[NFC_ATTR_PROTOCOLS]);

	dev = nfc_get_device(idx);
	if (!dev)
		return -ENODEV;

	mutex_lock(&dev->genl_data.genl_data_mutex);

	rc = nfc_start_poll(dev, im_protocols, tm_protocols);
	if (!rc)
		dev->genl_data.poll_req_portid = info->snd_portid;

	mutex_unlock(&dev->genl_data.genl_data_mutex);

	nfc_put_device(dev);
	return rc;
}

static int nfc_genl_stop_poll(struct sk_buff *skb, struct genl_info *info)
{
	struct nfc_dev *dev;
	int rc;
	u32 idx;

	if (!info->attrs[NFC_ATTR_DEVICE_INDEX])
		return -EINVAL;

	idx = nla_get_u32(info->attrs[NFC_ATTR_DEVICE_INDEX]);

	dev = nfc_get_device(idx);
	if (!dev)
		return -ENODEV;

	device_lock(&dev->dev);

	if (!dev->polling) {
		device_unlock(&dev->dev);
		nfc_put_device(dev);
		return -EINVAL;
	}

	device_unlock(&dev->dev);

	mutex_lock(&dev->genl_data.genl_data_mutex);

	if (dev->genl_data.poll_req_portid != info->snd_portid) {
		rc = -EBUSY;
		goto out;
	}

	rc = nfc_stop_poll(dev);
	dev->genl_data.poll_req_portid = 0;

out:
	mutex_unlock(&dev->genl_data.genl_data_mutex);
	nfc_put_device(dev);
	return rc;
}

static int nfc_genl_activate_target(struct sk_buff *skb, struct genl_info *info)
{
	struct nfc_dev *dev;
	u32 device_idx, target_idx, protocol;
	int rc;

	if (!info->attrs[NFC_ATTR_DEVICE_INDEX] ||
	    !info->attrs[NFC_ATTR_TARGET_INDEX] ||
	    !info->attrs[NFC_ATTR_PROTOCOLS])
		return -EINVAL;

	device_idx = nla_get_u32(info->attrs[NFC_ATTR_DEVICE_INDEX]);

	dev = nfc_get_device(device_idx);
	if (!dev)
		return -ENODEV;

	target_idx = nla_get_u32(info->attrs[NFC_ATTR_TARGET_INDEX]);
	protocol = nla_get_u32(info->attrs[NFC_ATTR_PROTOCOLS]);

	nfc_deactivate_target(dev, target_idx, NFC_TARGET_MODE_SLEEP);
	rc = nfc_activate_target(dev, target_idx, protocol);

	nfc_put_device(dev);
	return rc;
}

static int nfc_genl_deactivate_target(struct sk_buff *skb,
				      struct genl_info *info)
{
	struct nfc_dev *dev;
	u32 device_idx, target_idx;
	int rc;

	if (!info->attrs[NFC_ATTR_DEVICE_INDEX] ||
	    !info->attrs[NFC_ATTR_TARGET_INDEX])
		return -EINVAL;

	device_idx = nla_get_u32(info->attrs[NFC_ATTR_DEVICE_INDEX]);

	dev = nfc_get_device(device_idx);
	if (!dev)
		return -ENODEV;

	target_idx = nla_get_u32(info->attrs[NFC_ATTR_TARGET_INDEX]);

	rc = nfc_deactivate_target(dev, target_idx, NFC_TARGET_MODE_SLEEP);

	nfc_put_device(dev);
	return rc;
}

static int nfc_genl_dep_link_up(struct sk_buff *skb, struct genl_info *info)
{
	struct nfc_dev *dev;
	int rc, tgt_idx;
	u32 idx;
	u8 comm;

	pr_debug("DEP link up\n");

	if (!info->attrs[NFC_ATTR_DEVICE_INDEX] ||
	    !info->attrs[NFC_ATTR_COMM_MODE])
		return -EINVAL;

	idx = nla_get_u32(info->attrs[NFC_ATTR_DEVICE_INDEX]);
	if (!info->attrs[NFC_ATTR_TARGET_INDEX])
		tgt_idx = NFC_TARGET_IDX_ANY;
	else
		tgt_idx = nla_get_u32(info->attrs[NFC_ATTR_TARGET_INDEX]);

	comm = nla_get_u8(info->attrs[NFC_ATTR_COMM_MODE]);

	if (comm != NFC_COMM_ACTIVE && comm != NFC_COMM_PASSIVE)
		return -EINVAL;

	dev = nfc_get_device(idx);
	if (!dev)
		return -ENODEV;

	rc = nfc_dep_link_up(dev, tgt_idx, comm);

	nfc_put_device(dev);

	return rc;
}

static int nfc_genl_dep_link_down(struct sk_buff *skb, struct genl_info *info)
{
	struct nfc_dev *dev;
	int rc;
	u32 idx;

	if (!info->attrs[NFC_ATTR_DEVICE_INDEX])
		return -EINVAL;

	idx = nla_get_u32(info->attrs[NFC_ATTR_DEVICE_INDEX]);

	dev = nfc_get_device(idx);
	if (!dev)
		return -ENODEV;

	rc = nfc_dep_link_down(dev);

	nfc_put_device(dev);
	return rc;
}

static int nfc_genl_send_params(struct sk_buff *msg,
				struct nfc_llcp_local *local,
				u32 portid, u32 seq)
{
	void *hdr;

	hdr = genlmsg_put(msg, portid, seq, &nfc_genl_family, 0,
			  NFC_CMD_LLC_GET_PARAMS);
	if (!hdr)
		return -EMSGSIZE;

	if (nla_put_u32(msg, NFC_ATTR_DEVICE_INDEX, local->dev->idx) ||
	    nla_put_u8(msg, NFC_ATTR_LLC_PARAM_LTO, local->lto) ||
	    nla_put_u8(msg, NFC_ATTR_LLC_PARAM_RW, local->rw) ||
	    nla_put_u16(msg, NFC_ATTR_LLC_PARAM_MIUX, be16_to_cpu(local->miux)))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int nfc_genl_llc_get_params(struct sk_buff *skb, struct genl_info *info)
{
	struct nfc_dev *dev;
	struct nfc_llcp_local *local;
	int rc = 0;
	struct sk_buff *msg = NULL;
	u32 idx;

	if (!info->attrs[NFC_ATTR_DEVICE_INDEX])
		return -EINVAL;

	idx = nla_get_u32(info->attrs[NFC_ATTR_DEVICE_INDEX]);

	dev = nfc_get_device(idx);
	if (!dev)
		return -ENODEV;

	device_lock(&dev->dev);

	local = nfc_llcp_find_local(dev);
	if (!local) {
		rc = -ENODEV;
		goto exit;
	}

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		rc = -ENOMEM;
		goto put_local;
	}

	rc = nfc_genl_send_params(msg, local, info->snd_portid, info->snd_seq);

put_local:
	nfc_llcp_local_put(local);

exit:
	device_unlock(&dev->dev);

	nfc_put_device(dev);

	if (rc < 0) {
		if (msg)
			nlmsg_free(msg);

		return rc;
	}

	return genlmsg_reply(msg, info);
}

static int nfc_genl_llc_set_params(struct sk_buff *skb, struct genl_info *info)
{
	struct nfc_dev *dev;
	struct nfc_llcp_local *local;
	u8 rw = 0;
	u16 miux = 0;
	u32 idx;
	int rc = 0;

	if (!info->attrs[NFC_ATTR_DEVICE_INDEX] ||
	    (!info->attrs[NFC_ATTR_LLC_PARAM_LTO] &&
	     !info->attrs[NFC_ATTR_LLC_PARAM_RW] &&
	     !info->attrs[NFC_ATTR_LLC_PARAM_MIUX]))
		return -EINVAL;

	if (info->attrs[NFC_ATTR_LLC_PARAM_RW]) {
		rw = nla_get_u8(info->attrs[NFC_ATTR_LLC_PARAM_RW]);

		if (rw > LLCP_MAX_RW)
			return -EINVAL;
	}

	if (info->attrs[NFC_ATTR_LLC_PARAM_MIUX]) {
		miux = nla_get_u16(info->attrs[NFC_ATTR_LLC_PARAM_MIUX]);

		if (miux > LLCP_MAX_MIUX)
			return -EINVAL;
	}

	idx = nla_get_u32(info->attrs[NFC_ATTR_DEVICE_INDEX]);

	dev = nfc_get_device(idx);
	if (!dev)
		return -ENODEV;

	device_lock(&dev->dev);

	local = nfc_llcp_find_local(dev);
	if (!local) {
		rc = -ENODEV;
		goto exit;
	}

	if (info->attrs[NFC_ATTR_LLC_PARAM_LTO]) {
		if (dev->dep_link_up) {
			rc = -EINPROGRESS;
			goto put_local;
		}

		local->lto = nla_get_u8(info->attrs[NFC_ATTR_LLC_PARAM_LTO]);
	}

	if (info->attrs[NFC_ATTR_LLC_PARAM_RW])
		local->rw = rw;

	if (info->attrs[NFC_ATTR_LLC_PARAM_MIUX])
		local->miux = cpu_to_be16(miux);

put_local:
	nfc_llcp_local_put(local);

exit:
	device_unlock(&dev->dev);

	nfc_put_device(dev);

	return rc;
}

static int nfc_genl_llc_sdreq(struct sk_buff *skb, struct genl_info *info)
{
	struct nfc_dev *dev;
	struct nfc_llcp_local *local;
	struct nlattr *attr, *sdp_attrs[NFC_SDP_ATTR_MAX+1];
	u32 idx;
	u8 tid;
	char *uri;
	int rc = 0, rem;
	size_t uri_len, tlvs_len;
	struct hlist_head sdreq_list;
	struct nfc_llcp_sdp_tlv *sdreq;

	if (!info->attrs[NFC_ATTR_DEVICE_INDEX] ||
	    !info->attrs[NFC_ATTR_LLC_SDP])
		return -EINVAL;

	idx = nla_get_u32(info->attrs[NFC_ATTR_DEVICE_INDEX]);

	dev = nfc_get_device(idx);
	if (!dev)
		return -ENODEV;

	device_lock(&dev->dev);

	if (dev->dep_link_up == false) {
		rc = -ENOLINK;
		goto exit;
	}

	local = nfc_llcp_find_local(dev);
	if (!local) {
		rc = -ENODEV;
		goto exit;
	}

	INIT_HLIST_HEAD(&sdreq_list);

	tlvs_len = 0;

	nla_for_each_nested(attr, info->attrs[NFC_ATTR_LLC_SDP], rem) {
		rc = nla_parse_nested_deprecated(sdp_attrs, NFC_SDP_ATTR_MAX,
						 attr, nfc_sdp_genl_policy,
						 info->extack);

		if (rc != 0) {
			rc = -EINVAL;
			goto put_local;
		}

		if (!sdp_attrs[NFC_SDP_ATTR_URI])
			continue;

		uri_len = nla_len(sdp_attrs[NFC_SDP_ATTR_URI]);
		if (uri_len == 0)
			continue;

		uri = nla_data(sdp_attrs[NFC_SDP_ATTR_URI]);
		if (*uri == 0)
			continue;

		tid = local->sdreq_next_tid++;

		sdreq = nfc_llcp_build_sdreq_tlv(tid, uri, uri_len);
		if (sdreq == NULL) {
			rc = -ENOMEM;
			goto put_local;
		}

		tlvs_len += sdreq->tlv_len;

		hlist_add_head(&sdreq->node, &sdreq_list);
	}

	if (hlist_empty(&sdreq_list)) {
		rc = -EINVAL;
		goto put_local;
	}

	rc = nfc_llcp_send_snl_sdreq(local, &sdreq_list, tlvs_len);

put_local:
	nfc_llcp_local_put(local);

exit:
	device_unlock(&dev->dev);

	nfc_put_device(dev);

	return rc;
}

static int nfc_genl_fw_download(struct sk_buff *skb, struct genl_info *info)
{
	struct nfc_dev *dev;
	int rc;
	u32 idx;
	char firmware_name[NFC_FIRMWARE_NAME_MAXSIZE + 1];

	if (!info->attrs[NFC_ATTR_DEVICE_INDEX] || !info->attrs[NFC_ATTR_FIRMWARE_NAME])
		return -EINVAL;

	idx = nla_get_u32(info->attrs[NFC_ATTR_DEVICE_INDEX]);

	dev = nfc_get_device(idx);
	if (!dev)
		return -ENODEV;

	nla_strscpy(firmware_name, info->attrs[NFC_ATTR_FIRMWARE_NAME],
		    sizeof(firmware_name));

	rc = nfc_fw_download(dev, firmware_name);

	nfc_put_device(dev);
	return rc;
}

int nfc_genl_fw_download_done(struct nfc_dev *dev, const char *firmware_name,
			      u32 result)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put(msg, 0, 0, &nfc_genl_family, 0,
			  NFC_CMD_FW_DOWNLOAD);
	if (!hdr)
		goto free_msg;

	if (nla_put_string(msg, NFC_ATTR_FIRMWARE_NAME, firmware_name) ||
	    nla_put_u32(msg, NFC_ATTR_FIRMWARE_DOWNLOAD_STATUS, result) ||
	    nla_put_u32(msg, NFC_ATTR_DEVICE_INDEX, dev->idx))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_multicast(&nfc_genl_family, msg, 0, 0, GFP_ATOMIC);

	return 0;

nla_put_failure:
free_msg:
	nlmsg_free(msg);
	return -EMSGSIZE;
}

static int nfc_genl_enable_se(struct sk_buff *skb, struct genl_info *info)
{
	struct nfc_dev *dev;
	int rc;
	u32 idx, se_idx;

	if (!info->attrs[NFC_ATTR_DEVICE_INDEX] ||
	    !info->attrs[NFC_ATTR_SE_INDEX])
		return -EINVAL;

	idx = nla_get_u32(info->attrs[NFC_ATTR_DEVICE_INDEX]);
	se_idx = nla_get_u32(info->attrs[NFC_ATTR_SE_INDEX]);

	dev = nfc_get_device(idx);
	if (!dev)
		return -ENODEV;

	rc = nfc_enable_se(dev, se_idx);

	nfc_put_device(dev);
	return rc;
}

static int nfc_genl_disable_se(struct sk_buff *skb, struct genl_info *info)
{
	struct nfc_dev *dev;
	int rc;
	u32 idx, se_idx;

	if (!info->attrs[NFC_ATTR_DEVICE_INDEX] ||
	    !info->attrs[NFC_ATTR_SE_INDEX])
		return -EINVAL;

	idx = nla_get_u32(info->attrs[NFC_ATTR_DEVICE_INDEX]);
	se_idx = nla_get_u32(info->attrs[NFC_ATTR_SE_INDEX]);

	dev = nfc_get_device(idx);
	if (!dev)
		return -ENODEV;

	rc = nfc_disable_se(dev, se_idx);

	nfc_put_device(dev);
	return rc;
}

static int nfc_genl_send_se(struct sk_buff *msg, struct nfc_dev *dev,
				u32 portid, u32 seq,
				struct netlink_callback *cb,
				int flags)
{
	void *hdr;
	struct nfc_se *se, *n;

	list_for_each_entry_safe(se, n, &dev->secure_elements, list) {
		hdr = genlmsg_put(msg, portid, seq, &nfc_genl_family, flags,
				  NFC_CMD_GET_SE);
		if (!hdr)
			goto nla_put_failure;

		if (cb)
			genl_dump_check_consistent(cb, hdr);

		if (nla_put_u32(msg, NFC_ATTR_DEVICE_INDEX, dev->idx) ||
		    nla_put_u32(msg, NFC_ATTR_SE_INDEX, se->idx) ||
		    nla_put_u8(msg, NFC_ATTR_SE_TYPE, se->type))
			goto nla_put_failure;

		genlmsg_end(msg, hdr);
	}

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int nfc_genl_dump_ses(struct sk_buff *skb,
				 struct netlink_callback *cb)
{
	struct class_dev_iter *iter = (struct class_dev_iter *) cb->args[0];
	struct nfc_dev *dev = (struct nfc_dev *) cb->args[1];
	bool first_call = false;

	if (!iter) {
		first_call = true;
		iter = kmalloc(sizeof(struct class_dev_iter), GFP_KERNEL);
		if (!iter)
			return -ENOMEM;
		cb->args[0] = (long) iter;
	}

	mutex_lock(&nfc_devlist_mutex);

	cb->seq = nfc_devlist_generation;

	if (first_call) {
		nfc_device_iter_init(iter);
		dev = nfc_device_iter_next(iter);
	}

	while (dev) {
		int rc;

		rc = nfc_genl_send_se(skb, dev, NETLINK_CB(cb->skb).portid,
					  cb->nlh->nlmsg_seq, cb, NLM_F_MULTI);
		if (rc < 0)
			break;

		dev = nfc_device_iter_next(iter);
	}

	mutex_unlock(&nfc_devlist_mutex);

	cb->args[1] = (long) dev;

	return skb->len;
}

static int nfc_genl_dump_ses_done(struct netlink_callback *cb)
{
	struct class_dev_iter *iter = (struct class_dev_iter *) cb->args[0];

	if (iter) {
		nfc_device_iter_exit(iter);
		kfree(iter);
	}

	return 0;
}

static int nfc_se_io(struct nfc_dev *dev, u32 se_idx,
		     u8 *apdu, size_t apdu_length,
		     se_io_cb_t cb, void *cb_context)
{
	struct nfc_se *se;
	int rc;

	pr_debug("%s se index %d\n", dev_name(&dev->dev), se_idx);

	device_lock(&dev->dev);

	if (!device_is_registered(&dev->dev)) {
		rc = -ENODEV;
		goto error;
	}

	if (!dev->dev_up) {
		rc = -ENODEV;
		goto error;
	}

	if (!dev->ops->se_io) {
		rc = -EOPNOTSUPP;
		goto error;
	}

	se = nfc_find_se(dev, se_idx);
	if (!se) {
		rc = -EINVAL;
		goto error;
	}

	if (se->state != NFC_SE_ENABLED) {
		rc = -ENODEV;
		goto error;
	}

	rc = dev->ops->se_io(dev, se_idx, apdu,
			apdu_length, cb, cb_context);

	device_unlock(&dev->dev);
	return rc;

error:
	device_unlock(&dev->dev);
	kfree(cb_context);
	return rc;
}

struct se_io_ctx {
	u32 dev_idx;
	u32 se_idx;
};

static void se_io_cb(void *context, u8 *apdu, size_t apdu_len, int err)
{
	struct se_io_ctx *ctx = context;
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		kfree(ctx);
		return;
	}

	hdr = genlmsg_put(msg, 0, 0, &nfc_genl_family, 0,
			  NFC_CMD_SE_IO);
	if (!hdr)
		goto free_msg;

	if (nla_put_u32(msg, NFC_ATTR_DEVICE_INDEX, ctx->dev_idx) ||
	    nla_put_u32(msg, NFC_ATTR_SE_INDEX, ctx->se_idx) ||
	    nla_put(msg, NFC_ATTR_SE_APDU, apdu_len, apdu))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_multicast(&nfc_genl_family, msg, 0, 0, GFP_KERNEL);

	kfree(ctx);

	return;

nla_put_failure:
free_msg:
	nlmsg_free(msg);
	kfree(ctx);

	return;
}

static int nfc_genl_se_io(struct sk_buff *skb, struct genl_info *info)
{
	struct nfc_dev *dev;
	struct se_io_ctx *ctx;
	u32 dev_idx, se_idx;
	u8 *apdu;
	size_t apdu_len;
	int rc;

	if (!info->attrs[NFC_ATTR_DEVICE_INDEX] ||
	    !info->attrs[NFC_ATTR_SE_INDEX] ||
	    !info->attrs[NFC_ATTR_SE_APDU])
		return -EINVAL;

	dev_idx = nla_get_u32(info->attrs[NFC_ATTR_DEVICE_INDEX]);
	se_idx = nla_get_u32(info->attrs[NFC_ATTR_SE_INDEX]);

	dev = nfc_get_device(dev_idx);
	if (!dev)
		return -ENODEV;

	if (!dev->ops || !dev->ops->se_io) {
		rc = -EOPNOTSUPP;
		goto put_dev;
	}

	apdu_len = nla_len(info->attrs[NFC_ATTR_SE_APDU]);
	if (apdu_len == 0) {
		rc = -EINVAL;
		goto put_dev;
	}

	apdu = nla_data(info->attrs[NFC_ATTR_SE_APDU]);

	ctx = kzalloc(sizeof(struct se_io_ctx), GFP_KERNEL);
	if (!ctx) {
		rc = -ENOMEM;
		goto put_dev;
	}

	ctx->dev_idx = dev_idx;
	ctx->se_idx = se_idx;

	rc = nfc_se_io(dev, se_idx, apdu, apdu_len, se_io_cb, ctx);

put_dev:
	nfc_put_device(dev);
	return rc;
}

static int nfc_genl_vendor_cmd(struct sk_buff *skb,
			       struct genl_info *info)
{
	struct nfc_dev *dev;
	const struct nfc_vendor_cmd *cmd;
	u32 dev_idx, vid, subcmd;
	u8 *data;
	size_t data_len;
	int i, err;

	if (!info->attrs[NFC_ATTR_DEVICE_INDEX] ||
	    !info->attrs[NFC_ATTR_VENDOR_ID] ||
	    !info->attrs[NFC_ATTR_VENDOR_SUBCMD])
		return -EINVAL;

	dev_idx = nla_get_u32(info->attrs[NFC_ATTR_DEVICE_INDEX]);
	vid = nla_get_u32(info->attrs[NFC_ATTR_VENDOR_ID]);
	subcmd = nla_get_u32(info->attrs[NFC_ATTR_VENDOR_SUBCMD]);

	dev = nfc_get_device(dev_idx);
	if (!dev)
		return -ENODEV;

	if (!dev->vendor_cmds || !dev->n_vendor_cmds) {
		err = -ENODEV;
		goto put_dev;
	}

	if (info->attrs[NFC_ATTR_VENDOR_DATA]) {
		data = nla_data(info->attrs[NFC_ATTR_VENDOR_DATA]);
		data_len = nla_len(info->attrs[NFC_ATTR_VENDOR_DATA]);
		if (data_len == 0) {
			err = -EINVAL;
			goto put_dev;
		}
	} else {
		data = NULL;
		data_len = 0;
	}

	for (i = 0; i < dev->n_vendor_cmds; i++) {
		cmd = &dev->vendor_cmds[i];

		if (cmd->vendor_id != vid || cmd->subcmd != subcmd)
			continue;

		dev->cur_cmd_info = info;
		err = cmd->doit(dev, data, data_len);
		dev->cur_cmd_info = NULL;
		goto put_dev;
	}

	err = -EOPNOTSUPP;

put_dev:
	nfc_put_device(dev);
	return err;
}

/* message building helper */
static inline void *nfc_hdr_put(struct sk_buff *skb, u32 portid, u32 seq,
				int flags, u8 cmd)
{
	/* since there is no private header just add the generic one */
	return genlmsg_put(skb, portid, seq, &nfc_genl_family, flags, cmd);
}

static struct sk_buff *
__nfc_alloc_vendor_cmd_skb(struct nfc_dev *dev, int approxlen,
			   u32 portid, u32 seq,
			   enum nfc_attrs attr,
			   u32 oui, u32 subcmd, gfp_t gfp)
{
	struct sk_buff *skb;
	void *hdr;

	skb = nlmsg_new(approxlen + 100, gfp);
	if (!skb)
		return NULL;

	hdr = nfc_hdr_put(skb, portid, seq, 0, NFC_CMD_VENDOR);
	if (!hdr) {
		kfree_skb(skb);
		return NULL;
	}

	if (nla_put_u32(skb, NFC_ATTR_DEVICE_INDEX, dev->idx))
		goto nla_put_failure;
	if (nla_put_u32(skb, NFC_ATTR_VENDOR_ID, oui))
		goto nla_put_failure;
	if (nla_put_u32(skb, NFC_ATTR_VENDOR_SUBCMD, subcmd))
		goto nla_put_failure;

	((void **)skb->cb)[0] = dev;
	((void **)skb->cb)[1] = hdr;

	return skb;

nla_put_failure:
	kfree_skb(skb);
	return NULL;
}

struct sk_buff *__nfc_alloc_vendor_cmd_reply_skb(struct nfc_dev *dev,
						 enum nfc_attrs attr,
						 u32 oui, u32 subcmd,
						 int approxlen)
{
	if (WARN_ON(!dev->cur_cmd_info))
		return NULL;

	return __nfc_alloc_vendor_cmd_skb(dev, approxlen,
					  dev->cur_cmd_info->snd_portid,
					  dev->cur_cmd_info->snd_seq, attr,
					  oui, subcmd, GFP_KERNEL);
}
EXPORT_SYMBOL(__nfc_alloc_vendor_cmd_reply_skb);

int nfc_vendor_cmd_reply(struct sk_buff *skb)
{
	struct nfc_dev *dev = ((void **)skb->cb)[0];
	void *hdr = ((void **)skb->cb)[1];

	/* clear CB data for netlink core to own from now on */
	memset(skb->cb, 0, sizeof(skb->cb));

	if (WARN_ON(!dev->cur_cmd_info)) {
		kfree_skb(skb);
		return -EINVAL;
	}

	genlmsg_end(skb, hdr);
	return genlmsg_reply(skb, dev->cur_cmd_info);
}
EXPORT_SYMBOL(nfc_vendor_cmd_reply);

static const struct genl_ops nfc_genl_ops[] = {
	{
		.cmd = NFC_CMD_GET_DEVICE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nfc_genl_get_device,
		.dumpit = nfc_genl_dump_devices,
		.done = nfc_genl_dump_devices_done,
	},
	{
		.cmd = NFC_CMD_DEV_UP,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nfc_genl_dev_up,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NFC_CMD_DEV_DOWN,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nfc_genl_dev_down,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NFC_CMD_START_POLL,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nfc_genl_start_poll,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NFC_CMD_STOP_POLL,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nfc_genl_stop_poll,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NFC_CMD_DEP_LINK_UP,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nfc_genl_dep_link_up,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NFC_CMD_DEP_LINK_DOWN,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nfc_genl_dep_link_down,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NFC_CMD_GET_TARGET,
		.validate = GENL_DONT_VALIDATE_STRICT |
			    GENL_DONT_VALIDATE_DUMP_STRICT,
		.dumpit = nfc_genl_dump_targets,
		.done = nfc_genl_dump_targets_done,
	},
	{
		.cmd = NFC_CMD_LLC_GET_PARAMS,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nfc_genl_llc_get_params,
	},
	{
		.cmd = NFC_CMD_LLC_SET_PARAMS,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nfc_genl_llc_set_params,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NFC_CMD_LLC_SDREQ,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nfc_genl_llc_sdreq,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NFC_CMD_FW_DOWNLOAD,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nfc_genl_fw_download,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NFC_CMD_ENABLE_SE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nfc_genl_enable_se,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NFC_CMD_DISABLE_SE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nfc_genl_disable_se,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NFC_CMD_GET_SE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.dumpit = nfc_genl_dump_ses,
		.done = nfc_genl_dump_ses_done,
	},
	{
		.cmd = NFC_CMD_SE_IO,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nfc_genl_se_io,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NFC_CMD_ACTIVATE_TARGET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nfc_genl_activate_target,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NFC_CMD_VENDOR,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nfc_genl_vendor_cmd,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NFC_CMD_DEACTIVATE_TARGET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nfc_genl_deactivate_target,
		.flags = GENL_ADMIN_PERM,
	},
};

static struct genl_family nfc_genl_family __ro_after_init = {
	.hdrsize = 0,
	.name = NFC_GENL_NAME,
	.version = NFC_GENL_VERSION,
	.maxattr = NFC_ATTR_MAX,
	.policy = nfc_genl_policy,
	.module = THIS_MODULE,
	.ops = nfc_genl_ops,
	.n_ops = ARRAY_SIZE(nfc_genl_ops),
	.resv_start_op = NFC_CMD_DEACTIVATE_TARGET + 1,
	.mcgrps = nfc_genl_mcgrps,
	.n_mcgrps = ARRAY_SIZE(nfc_genl_mcgrps),
};


struct urelease_work {
	struct	work_struct w;
	u32	portid;
};

static void nfc_urelease_event_work(struct work_struct *work)
{
	struct urelease_work *w = container_of(work, struct urelease_work, w);
	struct class_dev_iter iter;
	struct nfc_dev *dev;

	pr_debug("portid %d\n", w->portid);

	mutex_lock(&nfc_devlist_mutex);

	nfc_device_iter_init(&iter);
	dev = nfc_device_iter_next(&iter);

	while (dev) {
		mutex_lock(&dev->genl_data.genl_data_mutex);

		if (dev->genl_data.poll_req_portid == w->portid) {
			nfc_stop_poll(dev);
			dev->genl_data.poll_req_portid = 0;
		}

		mutex_unlock(&dev->genl_data.genl_data_mutex);

		dev = nfc_device_iter_next(&iter);
	}

	nfc_device_iter_exit(&iter);

	mutex_unlock(&nfc_devlist_mutex);

	kfree(w);
}

static int nfc_genl_rcv_nl_event(struct notifier_block *this,
				 unsigned long event, void *ptr)
{
	struct netlink_notify *n = ptr;
	struct urelease_work *w;

	if (event != NETLINK_URELEASE || n->protocol != NETLINK_GENERIC)
		goto out;

	pr_debug("NETLINK_URELEASE event from id %d\n", n->portid);

	w = kmalloc(sizeof(*w), GFP_ATOMIC);
	if (w) {
		INIT_WORK(&w->w, nfc_urelease_event_work);
		w->portid = n->portid;
		schedule_work(&w->w);
	}

out:
	return NOTIFY_DONE;
}

void nfc_genl_data_init(struct nfc_genl_data *genl_data)
{
	genl_data->poll_req_portid = 0;
	mutex_init(&genl_data->genl_data_mutex);
}

void nfc_genl_data_exit(struct nfc_genl_data *genl_data)
{
	mutex_destroy(&genl_data->genl_data_mutex);
}

static struct notifier_block nl_notifier = {
	.notifier_call  = nfc_genl_rcv_nl_event,
};

/**
 * nfc_genl_init() - Initialize netlink interface
 *
 * This initialization function registers the nfc netlink family.
 */
int __init nfc_genl_init(void)
{
	int rc;

	rc = genl_register_family(&nfc_genl_family);
	if (rc)
		return rc;

	netlink_register_notifier(&nl_notifier);

	return 0;
}

/**
 * nfc_genl_exit() - Deinitialize netlink interface
 *
 * This exit function unregisters the nfc netlink family.
 */
void nfc_genl_exit(void)
{
	netlink_unregister_notifier(&nl_notifier);
	genl_unregister_family(&nfc_genl_family);
}
