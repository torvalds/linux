// SPDX-License-Identifier: GPL-2.0-only

#include <linux/skbuff.h>
#include <linux/xarray.h>
#include <net/genetlink.h>
#include <net/psp.h>
#include <net/sock.h>

#include "psp-nl-gen.h"
#include "psp.h"

/* Netlink helpers */

static struct sk_buff *psp_nl_reply_new(struct genl_info *info)
{
	struct sk_buff *rsp;
	void *hdr;

	rsp = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!rsp)
		return NULL;

	hdr = genlmsg_iput(rsp, info);
	if (!hdr) {
		nlmsg_free(rsp);
		return NULL;
	}

	return rsp;
}

static int psp_nl_reply_send(struct sk_buff *rsp, struct genl_info *info)
{
	/* Note that this *only* works with a single message per skb! */
	nlmsg_end(rsp, (struct nlmsghdr *)rsp->data);

	return genlmsg_reply(rsp, info);
}

/* Device stuff */

static struct psp_dev *
psp_device_get_and_lock(struct net *net, struct nlattr *dev_id)
{
	struct psp_dev *psd;
	int err;

	mutex_lock(&psp_devs_lock);
	psd = xa_load(&psp_devs, nla_get_u32(dev_id));
	if (!psd) {
		mutex_unlock(&psp_devs_lock);
		return ERR_PTR(-ENODEV);
	}

	mutex_lock(&psd->lock);
	mutex_unlock(&psp_devs_lock);

	err = psp_dev_check_access(psd, net);
	if (err) {
		mutex_unlock(&psd->lock);
		return ERR_PTR(err);
	}

	return psd;
}

int psp_device_get_locked(const struct genl_split_ops *ops,
			  struct sk_buff *skb, struct genl_info *info)
{
	if (GENL_REQ_ATTR_CHECK(info, PSP_A_DEV_ID))
		return -EINVAL;

	info->user_ptr[0] = psp_device_get_and_lock(genl_info_net(info),
						    info->attrs[PSP_A_DEV_ID]);
	return PTR_ERR_OR_ZERO(info->user_ptr[0]);
}

void
psp_device_unlock(const struct genl_split_ops *ops, struct sk_buff *skb,
		  struct genl_info *info)
{
	struct psp_dev *psd = info->user_ptr[0];

	mutex_unlock(&psd->lock);
}

static int
psp_nl_dev_fill(struct psp_dev *psd, struct sk_buff *rsp,
		const struct genl_info *info)
{
	void *hdr;

	hdr = genlmsg_iput(rsp, info);
	if (!hdr)
		return -EMSGSIZE;

	if (nla_put_u32(rsp, PSP_A_DEV_ID, psd->id) ||
	    nla_put_u32(rsp, PSP_A_DEV_IFINDEX, psd->main_netdev->ifindex) ||
	    nla_put_u32(rsp, PSP_A_DEV_PSP_VERSIONS_CAP, psd->caps->versions) ||
	    nla_put_u32(rsp, PSP_A_DEV_PSP_VERSIONS_ENA, psd->config.versions))
		goto err_cancel_msg;

	genlmsg_end(rsp, hdr);
	return 0;

err_cancel_msg:
	genlmsg_cancel(rsp, hdr);
	return -EMSGSIZE;
}

void psp_nl_notify_dev(struct psp_dev *psd, u32 cmd)
{
	struct genl_info info;
	struct sk_buff *ntf;

	if (!genl_has_listeners(&psp_nl_family, dev_net(psd->main_netdev),
				PSP_NLGRP_MGMT))
		return;

	ntf = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!ntf)
		return;

	genl_info_init_ntf(&info, &psp_nl_family, cmd);
	if (psp_nl_dev_fill(psd, ntf, &info)) {
		nlmsg_free(ntf);
		return;
	}

	genlmsg_multicast_netns(&psp_nl_family, dev_net(psd->main_netdev), ntf,
				0, PSP_NLGRP_MGMT, GFP_KERNEL);
}

int psp_nl_dev_get_doit(struct sk_buff *req, struct genl_info *info)
{
	struct psp_dev *psd = info->user_ptr[0];
	struct sk_buff *rsp;
	int err;

	rsp = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!rsp)
		return -ENOMEM;

	err = psp_nl_dev_fill(psd, rsp, info);
	if (err)
		goto err_free_msg;

	return genlmsg_reply(rsp, info);

err_free_msg:
	nlmsg_free(rsp);
	return err;
}

static int
psp_nl_dev_get_dumpit_one(struct sk_buff *rsp, struct netlink_callback *cb,
			  struct psp_dev *psd)
{
	if (psp_dev_check_access(psd, sock_net(rsp->sk)))
		return 0;

	return psp_nl_dev_fill(psd, rsp, genl_info_dump(cb));
}

int psp_nl_dev_get_dumpit(struct sk_buff *rsp, struct netlink_callback *cb)
{
	struct psp_dev *psd;
	int err = 0;

	mutex_lock(&psp_devs_lock);
	xa_for_each_start(&psp_devs, cb->args[0], psd, cb->args[0]) {
		mutex_lock(&psd->lock);
		err = psp_nl_dev_get_dumpit_one(rsp, cb, psd);
		mutex_unlock(&psd->lock);
		if (err)
			break;
	}
	mutex_unlock(&psp_devs_lock);

	return err;
}

int psp_nl_dev_set_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct psp_dev *psd = info->user_ptr[0];
	struct psp_dev_config new_config;
	struct sk_buff *rsp;
	int err;

	memcpy(&new_config, &psd->config, sizeof(new_config));

	if (info->attrs[PSP_A_DEV_PSP_VERSIONS_ENA]) {
		new_config.versions =
			nla_get_u32(info->attrs[PSP_A_DEV_PSP_VERSIONS_ENA]);
		if (new_config.versions & ~psd->caps->versions) {
			NL_SET_ERR_MSG(info->extack, "Requested PSP versions not supported by the device");
			return -EINVAL;
		}
	} else {
		NL_SET_ERR_MSG(info->extack, "No settings present");
		return -EINVAL;
	}

	rsp = psp_nl_reply_new(info);
	if (!rsp)
		return -ENOMEM;

	if (memcmp(&new_config, &psd->config, sizeof(new_config))) {
		err = psd->ops->set_config(psd, &new_config, info->extack);
		if (err)
			goto err_free_rsp;

		memcpy(&psd->config, &new_config, sizeof(new_config));
	}

	psp_nl_notify_dev(psd, PSP_CMD_DEV_CHANGE_NTF);

	return psp_nl_reply_send(rsp, info);

err_free_rsp:
	nlmsg_free(rsp);
	return err;
}

int psp_nl_key_rotate_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct psp_dev *psd = info->user_ptr[0];
	struct genl_info ntf_info;
	struct sk_buff *ntf, *rsp;
	int err;

	rsp = psp_nl_reply_new(info);
	if (!rsp)
		return -ENOMEM;

	genl_info_init_ntf(&ntf_info, &psp_nl_family, PSP_CMD_KEY_ROTATE_NTF);
	ntf = psp_nl_reply_new(&ntf_info);
	if (!ntf) {
		err = -ENOMEM;
		goto err_free_rsp;
	}

	if (nla_put_u32(rsp, PSP_A_DEV_ID, psd->id) ||
	    nla_put_u32(ntf, PSP_A_DEV_ID, psd->id)) {
		err = -EMSGSIZE;
		goto err_free_ntf;
	}

	err = psd->ops->key_rotate(psd, info->extack);
	if (err)
		goto err_free_ntf;

	nlmsg_end(ntf, (struct nlmsghdr *)ntf->data);
	genlmsg_multicast_netns(&psp_nl_family, dev_net(psd->main_netdev), ntf,
				0, PSP_NLGRP_USE, GFP_KERNEL);
	return psp_nl_reply_send(rsp, info);

err_free_ntf:
	nlmsg_free(ntf);
err_free_rsp:
	nlmsg_free(rsp);
	return err;
}
