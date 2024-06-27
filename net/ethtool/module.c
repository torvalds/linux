// SPDX-License-Identifier: GPL-2.0-only

#include <linux/ethtool.h>

#include "netlink.h"
#include "common.h"
#include "bitset.h"
#include "module_fw.h"

struct module_req_info {
	struct ethnl_req_info base;
};

struct module_reply_data {
	struct ethnl_reply_data	base;
	struct ethtool_module_power_mode_params power;
};

#define MODULE_REPDATA(__reply_base) \
	container_of(__reply_base, struct module_reply_data, base)

/* MODULE_GET */

const struct nla_policy ethnl_module_get_policy[ETHTOOL_A_MODULE_HEADER + 1] = {
	[ETHTOOL_A_MODULE_HEADER] = NLA_POLICY_NESTED(ethnl_header_policy),
};

static int module_get_power_mode(struct net_device *dev,
				 struct module_reply_data *data,
				 struct netlink_ext_ack *extack)
{
	const struct ethtool_ops *ops = dev->ethtool_ops;

	if (!ops->get_module_power_mode)
		return 0;

	return ops->get_module_power_mode(dev, &data->power, extack);
}

static int module_prepare_data(const struct ethnl_req_info *req_base,
			       struct ethnl_reply_data *reply_base,
			       const struct genl_info *info)
{
	struct module_reply_data *data = MODULE_REPDATA(reply_base);
	struct net_device *dev = reply_base->dev;
	int ret;

	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;

	ret = module_get_power_mode(dev, data, info->extack);
	if (ret < 0)
		goto out_complete;

out_complete:
	ethnl_ops_complete(dev);
	return ret;
}

static int module_reply_size(const struct ethnl_req_info *req_base,
			     const struct ethnl_reply_data *reply_base)
{
	struct module_reply_data *data = MODULE_REPDATA(reply_base);
	int len = 0;

	if (data->power.policy)
		len += nla_total_size(sizeof(u8));	/* _MODULE_POWER_MODE_POLICY */

	if (data->power.mode)
		len += nla_total_size(sizeof(u8));	/* _MODULE_POWER_MODE */

	return len;
}

static int module_fill_reply(struct sk_buff *skb,
			     const struct ethnl_req_info *req_base,
			     const struct ethnl_reply_data *reply_base)
{
	const struct module_reply_data *data = MODULE_REPDATA(reply_base);

	if (data->power.policy &&
	    nla_put_u8(skb, ETHTOOL_A_MODULE_POWER_MODE_POLICY,
		       data->power.policy))
		return -EMSGSIZE;

	if (data->power.mode &&
	    nla_put_u8(skb, ETHTOOL_A_MODULE_POWER_MODE, data->power.mode))
		return -EMSGSIZE;

	return 0;
}

/* MODULE_SET */

const struct nla_policy ethnl_module_set_policy[ETHTOOL_A_MODULE_POWER_MODE_POLICY + 1] = {
	[ETHTOOL_A_MODULE_HEADER] = NLA_POLICY_NESTED(ethnl_header_policy),
	[ETHTOOL_A_MODULE_POWER_MODE_POLICY] =
		NLA_POLICY_RANGE(NLA_U8, ETHTOOL_MODULE_POWER_MODE_POLICY_HIGH,
				 ETHTOOL_MODULE_POWER_MODE_POLICY_AUTO),
};

static int
ethnl_set_module_validate(struct ethnl_req_info *req_info,
			  struct genl_info *info)
{
	const struct ethtool_ops *ops = req_info->dev->ethtool_ops;
	struct nlattr **tb = info->attrs;

	if (!tb[ETHTOOL_A_MODULE_POWER_MODE_POLICY])
		return 0;

	if (!ops->get_module_power_mode || !ops->set_module_power_mode) {
		NL_SET_ERR_MSG_ATTR(info->extack,
				    tb[ETHTOOL_A_MODULE_POWER_MODE_POLICY],
				    "Setting power mode policy is not supported by this device");
		return -EOPNOTSUPP;
	}

	return 1;
}

static int
ethnl_set_module(struct ethnl_req_info *req_info, struct genl_info *info)
{
	struct ethtool_module_power_mode_params power = {};
	struct ethtool_module_power_mode_params power_new;
	const struct ethtool_ops *ops;
	struct net_device *dev = req_info->dev;
	struct nlattr **tb = info->attrs;
	int ret;

	ops = dev->ethtool_ops;

	power_new.policy = nla_get_u8(tb[ETHTOOL_A_MODULE_POWER_MODE_POLICY]);
	ret = ops->get_module_power_mode(dev, &power, info->extack);
	if (ret < 0)
		return ret;

	if (power_new.policy == power.policy)
		return 0;

	ret = ops->set_module_power_mode(dev, &power_new, info->extack);
	return ret < 0 ? ret : 1;
}

const struct ethnl_request_ops ethnl_module_request_ops = {
	.request_cmd		= ETHTOOL_MSG_MODULE_GET,
	.reply_cmd		= ETHTOOL_MSG_MODULE_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_MODULE_HEADER,
	.req_info_size		= sizeof(struct module_req_info),
	.reply_data_size	= sizeof(struct module_reply_data),

	.prepare_data		= module_prepare_data,
	.reply_size		= module_reply_size,
	.fill_reply		= module_fill_reply,

	.set_validate		= ethnl_set_module_validate,
	.set			= ethnl_set_module,
	.set_ntf_cmd		= ETHTOOL_MSG_MODULE_NTF,
};

/* MODULE_FW_FLASH_NTF */

static int
ethnl_module_fw_flash_ntf_put_err(struct sk_buff *skb, char *err_msg,
				  char *sub_err_msg)
{
	int err_msg_len, sub_err_msg_len, total_len;
	struct nlattr *attr;

	if (!err_msg)
		return 0;

	err_msg_len = strlen(err_msg);
	total_len = err_msg_len + 2; /* For period and NUL. */

	if (sub_err_msg) {
		sub_err_msg_len = strlen(sub_err_msg);
		total_len += sub_err_msg_len + 2; /* For ", ". */
	}

	attr = nla_reserve(skb, ETHTOOL_A_MODULE_FW_FLASH_STATUS_MSG,
			   total_len);
	if (!attr)
		return -ENOMEM;

	if (sub_err_msg)
		sprintf(nla_data(attr), "%s, %s.", err_msg, sub_err_msg);
	else
		sprintf(nla_data(attr), "%s.", err_msg);

	return 0;
}

static void
ethnl_module_fw_flash_ntf(struct net_device *dev,
			  enum ethtool_module_fw_flash_status status,
			  struct ethnl_module_fw_flash_ntf_params *ntf_params,
			  char *err_msg, char *sub_err_msg,
			  u64 done, u64 total)
{
	struct sk_buff *skb;
	void *hdr;
	int ret;

	if (ntf_params->closed_sock)
		return;

	skb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		return;

	hdr = ethnl_unicast_put(skb, ntf_params->portid, ntf_params->seq,
				ETHTOOL_MSG_MODULE_FW_FLASH_NTF);
	if (!hdr)
		goto err_skb;

	ret = ethnl_fill_reply_header(skb, dev,
				      ETHTOOL_A_MODULE_FW_FLASH_HEADER);
	if (ret < 0)
		goto err_skb;

	if (nla_put_u32(skb, ETHTOOL_A_MODULE_FW_FLASH_STATUS, status))
		goto err_skb;

	ret = ethnl_module_fw_flash_ntf_put_err(skb, err_msg, sub_err_msg);
	if (ret < 0)
		goto err_skb;

	if (nla_put_uint(skb, ETHTOOL_A_MODULE_FW_FLASH_DONE, done))
		goto err_skb;

	if (nla_put_uint(skb, ETHTOOL_A_MODULE_FW_FLASH_TOTAL, total))
		goto err_skb;

	genlmsg_end(skb, hdr);
	genlmsg_unicast(dev_net(dev), skb, ntf_params->portid);
	return;

err_skb:
	nlmsg_free(skb);
}

void ethnl_module_fw_flash_ntf_err(struct net_device *dev,
				   struct ethnl_module_fw_flash_ntf_params *params,
				   char *err_msg, char *sub_err_msg)
{
	ethnl_module_fw_flash_ntf(dev, ETHTOOL_MODULE_FW_FLASH_STATUS_ERROR,
				  params, err_msg, sub_err_msg, 0, 0);
}

void
ethnl_module_fw_flash_ntf_start(struct net_device *dev,
				struct ethnl_module_fw_flash_ntf_params *params)
{
	ethnl_module_fw_flash_ntf(dev, ETHTOOL_MODULE_FW_FLASH_STATUS_STARTED,
				  params, NULL, NULL, 0, 0);
}

void
ethnl_module_fw_flash_ntf_complete(struct net_device *dev,
				   struct ethnl_module_fw_flash_ntf_params *params)
{
	ethnl_module_fw_flash_ntf(dev, ETHTOOL_MODULE_FW_FLASH_STATUS_COMPLETED,
				  params, NULL, NULL, 0, 0);
}

void
ethnl_module_fw_flash_ntf_in_progress(struct net_device *dev,
				      struct ethnl_module_fw_flash_ntf_params *params,
				      u64 done, u64 total)
{
	ethnl_module_fw_flash_ntf(dev,
				  ETHTOOL_MODULE_FW_FLASH_STATUS_IN_PROGRESS,
				  params, NULL, NULL, done, total);
}
