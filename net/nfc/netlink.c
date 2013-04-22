/*
 * Copyright (C) 2011 Instituto Nokia de Tecnologia
 *
 * Authors:
 *    Lauro Ramos Venancio <lauro.venancio@openbossa.org>
 *    Aloisio Almeida Jr <aloisio.almeida@openbossa.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": %s: " fmt, __func__

#include <net/genetlink.h>
#include <linux/nfc.h>
#include <linux/slab.h>

#include "nfc.h"

#include "llcp/llcp.h"

static struct genl_multicast_group nfc_genl_event_mcgrp = {
	.name = NFC_GENL_MCAST_EVENT_NAME,
};

static struct genl_family nfc_genl_family = {
	.id = GENL_ID_GENERATE,
	.hdrsize = 0,
	.name = NFC_GENL_NAME,
	.version = NFC_GENL_VERSION,
	.maxattr = NFC_ATTR_MAX,
};

static const struct nla_policy nfc_genl_policy[NFC_ATTR_MAX + 1] = {
	[NFC_ATTR_DEVICE_INDEX] = { .type = NLA_U32 },
	[NFC_ATTR_DEVICE_NAME] = { .type = NLA_STRING,
				.len = NFC_DEVICE_NAME_MAXSIZE },
	[NFC_ATTR_PROTOCOLS] = { .type = NLA_U32 },
	[NFC_ATTR_COMM_MODE] = { .type = NLA_U8 },
	[NFC_ATTR_RF_MODE] = { .type = NLA_U8 },
	[NFC_ATTR_DEVICE_POWERED] = { .type = NLA_U8 },
	[NFC_ATTR_IM_PROTOCOLS] = { .type = NLA_U32 },
	[NFC_ATTR_TM_PROTOCOLS] = { .type = NLA_U32 },
	[NFC_ATTR_LLC_PARAM_LTO] = { .type = NLA_U8 },
	[NFC_ATTR_LLC_PARAM_RW] = { .type = NLA_U8 },
	[NFC_ATTR_LLC_PARAM_MIUX] = { .type = NLA_U16 },
	[NFC_ATTR_LLC_SDP] = { .type = NLA_NESTED },
};

static const struct nla_policy nfc_sdp_genl_policy[NFC_SDP_ATTR_MAX + 1] = {
	[NFC_SDP_ATTR_URI] = { .type = NLA_STRING },
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

	genl_dump_check_consistent(cb, hdr, &nfc_genl_family);

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

	return genlmsg_end(msg, hdr);

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static struct nfc_dev *__get_device_from_cb(struct netlink_callback *cb)
{
	struct nfc_dev *dev;
	int rc;
	u32 idx;

	rc = nlmsg_parse(cb->nlh, GENL_HDRLEN + nfc_genl_family.hdrsize,
			 nfc_genl_family.attrbuf,
			 nfc_genl_family.maxattr,
			 nfc_genl_policy);
	if (rc < 0)
		return ERR_PTR(rc);

	if (!nfc_genl_family.attrbuf[NFC_ATTR_DEVICE_INDEX])
		return ERR_PTR(-EINVAL);

	idx = nla_get_u32(nfc_genl_family.attrbuf[NFC_ATTR_DEVICE_INDEX]);

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

	return genlmsg_multicast(msg, 0, nfc_genl_event_mcgrp.id, GFP_ATOMIC);

nla_put_failure:
	genlmsg_cancel(msg, hdr);
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

	genlmsg_multicast(msg, 0, nfc_genl_event_mcgrp.id, GFP_KERNEL);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
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

	genlmsg_multicast(msg, 0, nfc_genl_event_mcgrp.id, GFP_KERNEL);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
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

	genlmsg_multicast(msg, 0, nfc_genl_event_mcgrp.id, GFP_KERNEL);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
free_msg:
	nlmsg_free(msg);
	return -EMSGSIZE;
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

	if (nla_put_string(msg, NFC_ATTR_DEVICE_NAME, nfc_device_name(dev)) ||
	    nla_put_u32(msg, NFC_ATTR_DEVICE_INDEX, dev->idx) ||
	    nla_put_u32(msg, NFC_ATTR_PROTOCOLS, dev->supported_protocols) ||
	    nla_put_u8(msg, NFC_ATTR_DEVICE_POWERED, dev->dev_up))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_multicast(msg, 0, nfc_genl_event_mcgrp.id, GFP_KERNEL);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
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

	genlmsg_multicast(msg, 0, nfc_genl_event_mcgrp.id, GFP_KERNEL);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
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

	sdp_attr = nla_nest_start(msg, NFC_ATTR_LLC_SDP);
	if (sdp_attr == NULL) {
		rc = -ENOMEM;
		goto nla_put_failure;
	}

	i = 1;
	hlist_for_each_entry_safe(sdres, n, sdres_list, node) {
		pr_debug("uri: %s, sap: %d\n", sdres->uri, sdres->sap);

		uri_attr = nla_nest_start(msg, i++);
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

	return genlmsg_multicast(msg, 0, nfc_genl_event_mcgrp.id, GFP_ATOMIC);

nla_put_failure:
	genlmsg_cancel(msg, hdr);

free_msg:
	nlmsg_free(msg);

	nfc_llcp_free_sdp_tlv_list(sdres_list);

	return rc;
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
		genl_dump_check_consistent(cb, hdr, &nfc_genl_family);

	if (nla_put_string(msg, NFC_ATTR_DEVICE_NAME, nfc_device_name(dev)) ||
	    nla_put_u32(msg, NFC_ATTR_DEVICE_INDEX, dev->idx) ||
	    nla_put_u32(msg, NFC_ATTR_PROTOCOLS, dev->supported_protocols) ||
	    nla_put_u32(msg, NFC_ATTR_SE, dev->supported_se) ||
	    nla_put_u8(msg, NFC_ATTR_DEVICE_POWERED, dev->dev_up) ||
	    nla_put_u8(msg, NFC_ATTR_RF_MODE, dev->rf_mode))
		goto nla_put_failure;

	return genlmsg_end(msg, hdr);

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

	nfc_device_iter_exit(iter);
	kfree(iter);

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

	genlmsg_multicast(msg, 0, nfc_genl_event_mcgrp.id, GFP_ATOMIC);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
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

	genlmsg_multicast(msg, 0, nfc_genl_event_mcgrp.id, GFP_ATOMIC);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
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

	return genlmsg_end(msg, hdr);

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
		goto exit;
	}

	rc = nfc_genl_send_params(msg, local, info->snd_portid, info->snd_seq);

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
		nfc_put_device(dev);
		rc = -ENODEV;
		goto exit;
	}

	if (info->attrs[NFC_ATTR_LLC_PARAM_LTO]) {
		if (dev->dep_link_up) {
			rc = -EINPROGRESS;
			goto exit;
		}

		local->lto = nla_get_u8(info->attrs[NFC_ATTR_LLC_PARAM_LTO]);
	}

	if (info->attrs[NFC_ATTR_LLC_PARAM_RW])
		local->rw = rw;

	if (info->attrs[NFC_ATTR_LLC_PARAM_MIUX])
		local->miux = cpu_to_be16(miux);

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
	if (!dev) {
		rc = -ENODEV;
		goto exit;
	}

	device_lock(&dev->dev);

	if (dev->dep_link_up == false) {
		rc = -ENOLINK;
		goto exit;
	}

	local = nfc_llcp_find_local(dev);
	if (!local) {
		nfc_put_device(dev);
		rc = -ENODEV;
		goto exit;
	}

	INIT_HLIST_HEAD(&sdreq_list);

	tlvs_len = 0;

	nla_for_each_nested(attr, info->attrs[NFC_ATTR_LLC_SDP], rem) {
		rc = nla_parse_nested(sdp_attrs, NFC_SDP_ATTR_MAX, attr,
				      nfc_sdp_genl_policy);

		if (rc != 0) {
			rc = -EINVAL;
			goto exit;
		}

		if (!sdp_attrs[NFC_SDP_ATTR_URI])
			continue;

		uri_len = nla_len(sdp_attrs[NFC_SDP_ATTR_URI]);
		if (uri_len == 0)
			continue;

		uri = nla_data(sdp_attrs[NFC_SDP_ATTR_URI]);
		if (uri == NULL || *uri == 0)
			continue;

		tid = local->sdreq_next_tid++;

		sdreq = nfc_llcp_build_sdreq_tlv(tid, uri, uri_len);
		if (sdreq == NULL) {
			rc = -ENOMEM;
			goto exit;
		}

		tlvs_len += sdreq->tlv_len;

		hlist_add_head(&sdreq->node, &sdreq_list);
	}

	if (hlist_empty(&sdreq_list)) {
		rc = -EINVAL;
		goto exit;
	}

	rc = nfc_llcp_send_snl_sdreq(local, &sdreq_list, tlvs_len);
exit:
	device_unlock(&dev->dev);

	nfc_put_device(dev);

	return rc;
}

static struct genl_ops nfc_genl_ops[] = {
	{
		.cmd = NFC_CMD_GET_DEVICE,
		.doit = nfc_genl_get_device,
		.dumpit = nfc_genl_dump_devices,
		.done = nfc_genl_dump_devices_done,
		.policy = nfc_genl_policy,
	},
	{
		.cmd = NFC_CMD_DEV_UP,
		.doit = nfc_genl_dev_up,
		.policy = nfc_genl_policy,
	},
	{
		.cmd = NFC_CMD_DEV_DOWN,
		.doit = nfc_genl_dev_down,
		.policy = nfc_genl_policy,
	},
	{
		.cmd = NFC_CMD_START_POLL,
		.doit = nfc_genl_start_poll,
		.policy = nfc_genl_policy,
	},
	{
		.cmd = NFC_CMD_STOP_POLL,
		.doit = nfc_genl_stop_poll,
		.policy = nfc_genl_policy,
	},
	{
		.cmd = NFC_CMD_DEP_LINK_UP,
		.doit = nfc_genl_dep_link_up,
		.policy = nfc_genl_policy,
	},
	{
		.cmd = NFC_CMD_DEP_LINK_DOWN,
		.doit = nfc_genl_dep_link_down,
		.policy = nfc_genl_policy,
	},
	{
		.cmd = NFC_CMD_GET_TARGET,
		.dumpit = nfc_genl_dump_targets,
		.done = nfc_genl_dump_targets_done,
		.policy = nfc_genl_policy,
	},
	{
		.cmd = NFC_CMD_LLC_GET_PARAMS,
		.doit = nfc_genl_llc_get_params,
		.policy = nfc_genl_policy,
	},
	{
		.cmd = NFC_CMD_LLC_SET_PARAMS,
		.doit = nfc_genl_llc_set_params,
		.policy = nfc_genl_policy,
	},
	{
		.cmd = NFC_CMD_LLC_SDREQ,
		.doit = nfc_genl_llc_sdreq,
		.policy = nfc_genl_policy,
	},
};


struct urelease_work {
	struct	work_struct w;
	int	portid;
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
		INIT_WORK((struct work_struct *) w, nfc_urelease_event_work);
		w->portid = n->portid;
		schedule_work((struct work_struct *) w);
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

	rc = genl_register_family_with_ops(&nfc_genl_family, nfc_genl_ops,
					   ARRAY_SIZE(nfc_genl_ops));
	if (rc)
		return rc;

	rc = genl_register_mc_group(&nfc_genl_family, &nfc_genl_event_mcgrp);

	netlink_register_notifier(&nl_notifier);

	return rc;
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
