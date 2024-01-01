// SPDX-License-Identifier: GPL-2.0-only
//
// ethtool interface for Ethernet PSE (Power Sourcing Equipment)
// and PD (Powered Device)
//
// Copyright (c) 2022 Pengutronix, Oleksij Rempel <kernel@pengutronix.de>
//

#include "common.h"
#include "linux/pse-pd/pse.h"
#include "netlink.h"
#include <linux/ethtool_netlink.h>
#include <linux/ethtool.h>
#include <linux/phy.h>

struct pse_req_info {
	struct ethnl_req_info base;
};

struct pse_reply_data {
	struct ethnl_reply_data	base;
	struct pse_control_status status;
};

#define PSE_REPDATA(__reply_base) \
	container_of(__reply_base, struct pse_reply_data, base)

/* PSE_GET */

const struct nla_policy ethnl_pse_get_policy[ETHTOOL_A_PSE_HEADER + 1] = {
	[ETHTOOL_A_PSE_HEADER] = NLA_POLICY_NESTED(ethnl_header_policy),
};

static int pse_get_pse_attributes(struct phy_device *phydev,
				  struct netlink_ext_ack *extack,
				  struct pse_reply_data *data)
{
	if (!phydev) {
		NL_SET_ERR_MSG(extack, "No PHY is attached");
		return -EOPNOTSUPP;
	}

	if (!phydev->psec) {
		NL_SET_ERR_MSG(extack, "No PSE is attached");
		return -EOPNOTSUPP;
	}

	memset(&data->status, 0, sizeof(data->status));

	return pse_ethtool_get_status(phydev->psec, extack, &data->status);
}

static int pse_prepare_data(const struct ethnl_req_info *req_base,
			    struct ethnl_reply_data *reply_base,
			    const struct genl_info *info)
{
	struct pse_reply_data *data = PSE_REPDATA(reply_base);
	struct net_device *dev = reply_base->dev;
	int ret;

	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;

	ret = pse_get_pse_attributes(req_base->phydev, info->extack, data);

	ethnl_ops_complete(dev);

	return ret;
}

static int pse_reply_size(const struct ethnl_req_info *req_base,
			  const struct ethnl_reply_data *reply_base)
{
	const struct pse_reply_data *data = PSE_REPDATA(reply_base);
	const struct pse_control_status *st = &data->status;
	int len = 0;

	if (st->podl_admin_state > 0)
		len += nla_total_size(sizeof(u32)); /* _PODL_PSE_ADMIN_STATE */
	if (st->podl_pw_status > 0)
		len += nla_total_size(sizeof(u32)); /* _PODL_PSE_PW_D_STATUS */

	return len;
}

static int pse_fill_reply(struct sk_buff *skb,
			  const struct ethnl_req_info *req_base,
			  const struct ethnl_reply_data *reply_base)
{
	const struct pse_reply_data *data = PSE_REPDATA(reply_base);
	const struct pse_control_status *st = &data->status;

	if (st->podl_admin_state > 0 &&
	    nla_put_u32(skb, ETHTOOL_A_PODL_PSE_ADMIN_STATE,
			st->podl_admin_state))
		return -EMSGSIZE;

	if (st->podl_pw_status > 0 &&
	    nla_put_u32(skb, ETHTOOL_A_PODL_PSE_PW_D_STATUS,
			st->podl_pw_status))
		return -EMSGSIZE;

	return 0;
}

/* PSE_SET */

const struct nla_policy ethnl_pse_set_policy[ETHTOOL_A_PSE_MAX + 1] = {
	[ETHTOOL_A_PSE_HEADER] = NLA_POLICY_NESTED(ethnl_header_policy),
	[ETHTOOL_A_PODL_PSE_ADMIN_CONTROL] =
		NLA_POLICY_RANGE(NLA_U32, ETHTOOL_PODL_PSE_ADMIN_STATE_DISABLED,
				 ETHTOOL_PODL_PSE_ADMIN_STATE_ENABLED),
};

static int
ethnl_set_pse_validate(struct ethnl_req_info *req_info, struct genl_info *info)
{
	return !!info->attrs[ETHTOOL_A_PODL_PSE_ADMIN_CONTROL];
}

static int
ethnl_set_pse(struct ethnl_req_info *req_info, struct genl_info *info)
{
	struct pse_control_config config = {};
	struct nlattr **tb = info->attrs;
	struct phy_device *phydev;

	/* this values are already validated by the ethnl_pse_set_policy */
	config.admin_cotrol = nla_get_u32(tb[ETHTOOL_A_PODL_PSE_ADMIN_CONTROL]);

	phydev = req_info->phydev;
	if (!phydev) {
		NL_SET_ERR_MSG(info->extack, "No PHY is attached");
		return -EOPNOTSUPP;
	}

	if (!phydev->psec) {
		NL_SET_ERR_MSG(info->extack, "No PSE is attached");
		return -EOPNOTSUPP;
	}

	/* Return errno directly - PSE has no notification */
	return pse_ethtool_set_config(phydev->psec, info->extack, &config);
}

const struct ethnl_request_ops ethnl_pse_request_ops = {
	.request_cmd		= ETHTOOL_MSG_PSE_GET,
	.reply_cmd		= ETHTOOL_MSG_PSE_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_PSE_HEADER,
	.req_info_size		= sizeof(struct pse_req_info),
	.reply_data_size	= sizeof(struct pse_reply_data),

	.prepare_data		= pse_prepare_data,
	.reply_size		= pse_reply_size,
	.fill_reply		= pse_fill_reply,

	.set_validate		= ethnl_set_pse_validate,
	.set			= ethnl_set_pse,
	/* PSE has no notification */
};
