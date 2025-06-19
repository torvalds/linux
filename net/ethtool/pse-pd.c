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
#include <linux/export.h>
#include <linux/phy.h>

struct pse_req_info {
	struct ethnl_req_info base;
};

struct pse_reply_data {
	struct ethnl_reply_data	base;
	struct ethtool_pse_control_status status;
};

#define PSE_REPDATA(__reply_base) \
	container_of(__reply_base, struct pse_reply_data, base)

/* PSE_GET */

const struct nla_policy ethnl_pse_get_policy[ETHTOOL_A_PSE_HEADER + 1] = {
	[ETHTOOL_A_PSE_HEADER] = NLA_POLICY_NESTED(ethnl_header_policy_phy),
};

static int pse_get_pse_attributes(struct phy_device *phydev,
				  struct netlink_ext_ack *extack,
				  struct pse_reply_data *data)
{
	if (!phydev) {
		NL_SET_ERR_MSG(extack, "No PHY found");
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
	struct nlattr **tb = info->attrs;
	struct phy_device *phydev;
	int ret;

	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;

	phydev = ethnl_req_get_phydev(req_base, tb, ETHTOOL_A_PSE_HEADER,
				      info->extack);
	if (IS_ERR(phydev))
		return -ENODEV;

	ret = pse_get_pse_attributes(phydev, info->extack, data);

	ethnl_ops_complete(dev);

	return ret;
}

static int pse_reply_size(const struct ethnl_req_info *req_base,
			  const struct ethnl_reply_data *reply_base)
{
	const struct pse_reply_data *data = PSE_REPDATA(reply_base);
	const struct ethtool_pse_control_status *st = &data->status;
	int len = 0;

	if (st->pw_d_id)
		len += nla_total_size(sizeof(u32)); /* _PSE_PW_D_ID */
	if (st->podl_admin_state > 0)
		len += nla_total_size(sizeof(u32)); /* _PODL_PSE_ADMIN_STATE */
	if (st->podl_pw_status > 0)
		len += nla_total_size(sizeof(u32)); /* _PODL_PSE_PW_D_STATUS */
	if (st->c33_admin_state > 0)
		len += nla_total_size(sizeof(u32)); /* _C33_PSE_ADMIN_STATE */
	if (st->c33_pw_status > 0)
		len += nla_total_size(sizeof(u32)); /* _C33_PSE_PW_D_STATUS */
	if (st->c33_pw_class > 0)
		len += nla_total_size(sizeof(u32)); /* _C33_PSE_PW_CLASS */
	if (st->c33_actual_pw > 0)
		len += nla_total_size(sizeof(u32)); /* _C33_PSE_ACTUAL_PW */
	if (st->c33_ext_state_info.c33_pse_ext_state > 0) {
		len += nla_total_size(sizeof(u32)); /* _C33_PSE_EXT_STATE */
		if (st->c33_ext_state_info.__c33_pse_ext_substate > 0)
			/* _C33_PSE_EXT_SUBSTATE */
			len += nla_total_size(sizeof(u32));
	}
	if (st->c33_avail_pw_limit > 0)
		/* _C33_AVAIL_PSE_PW_LIMIT */
		len += nla_total_size(sizeof(u32));
	if (st->c33_pw_limit_nb_ranges > 0)
		/* _C33_PSE_PW_LIMIT_RANGES */
		len += st->c33_pw_limit_nb_ranges *
		       (nla_total_size(0) +
			nla_total_size(sizeof(u32)) * 2);
	if (st->prio_max)
		/* _PSE_PRIO_MAX + _PSE_PRIO */
		len += nla_total_size(sizeof(u32)) * 2;

	return len;
}

static int pse_put_pw_limit_ranges(struct sk_buff *skb,
				   const struct ethtool_pse_control_status *st)
{
	const struct ethtool_c33_pse_pw_limit_range *pw_limit_ranges;
	int i;

	pw_limit_ranges = st->c33_pw_limit_ranges;
	for (i = 0; i < st->c33_pw_limit_nb_ranges; i++) {
		struct nlattr *nest;

		nest = nla_nest_start(skb, ETHTOOL_A_C33_PSE_PW_LIMIT_RANGES);
		if (!nest)
			return -EMSGSIZE;

		if (nla_put_u32(skb, ETHTOOL_A_C33_PSE_PW_LIMIT_MIN,
				pw_limit_ranges->min) ||
		    nla_put_u32(skb, ETHTOOL_A_C33_PSE_PW_LIMIT_MAX,
				pw_limit_ranges->max)) {
			nla_nest_cancel(skb, nest);
			return -EMSGSIZE;
		}
		nla_nest_end(skb, nest);
		pw_limit_ranges++;
	}

	return 0;
}

static int pse_fill_reply(struct sk_buff *skb,
			  const struct ethnl_req_info *req_base,
			  const struct ethnl_reply_data *reply_base)
{
	const struct pse_reply_data *data = PSE_REPDATA(reply_base);
	const struct ethtool_pse_control_status *st = &data->status;

	if (st->pw_d_id &&
	    nla_put_u32(skb, ETHTOOL_A_PSE_PW_D_ID,
			st->pw_d_id))
		return -EMSGSIZE;

	if (st->podl_admin_state > 0 &&
	    nla_put_u32(skb, ETHTOOL_A_PODL_PSE_ADMIN_STATE,
			st->podl_admin_state))
		return -EMSGSIZE;

	if (st->podl_pw_status > 0 &&
	    nla_put_u32(skb, ETHTOOL_A_PODL_PSE_PW_D_STATUS,
			st->podl_pw_status))
		return -EMSGSIZE;

	if (st->c33_admin_state > 0 &&
	    nla_put_u32(skb, ETHTOOL_A_C33_PSE_ADMIN_STATE,
			st->c33_admin_state))
		return -EMSGSIZE;

	if (st->c33_pw_status > 0 &&
	    nla_put_u32(skb, ETHTOOL_A_C33_PSE_PW_D_STATUS,
			st->c33_pw_status))
		return -EMSGSIZE;

	if (st->c33_pw_class > 0 &&
	    nla_put_u32(skb, ETHTOOL_A_C33_PSE_PW_CLASS,
			st->c33_pw_class))
		return -EMSGSIZE;

	if (st->c33_actual_pw > 0 &&
	    nla_put_u32(skb, ETHTOOL_A_C33_PSE_ACTUAL_PW,
			st->c33_actual_pw))
		return -EMSGSIZE;

	if (st->c33_ext_state_info.c33_pse_ext_state > 0) {
		if (nla_put_u32(skb, ETHTOOL_A_C33_PSE_EXT_STATE,
				st->c33_ext_state_info.c33_pse_ext_state))
			return -EMSGSIZE;

		if (st->c33_ext_state_info.__c33_pse_ext_substate > 0 &&
		    nla_put_u32(skb, ETHTOOL_A_C33_PSE_EXT_SUBSTATE,
				st->c33_ext_state_info.__c33_pse_ext_substate))
			return -EMSGSIZE;
	}

	if (st->c33_avail_pw_limit > 0 &&
	    nla_put_u32(skb, ETHTOOL_A_C33_PSE_AVAIL_PW_LIMIT,
			st->c33_avail_pw_limit))
		return -EMSGSIZE;

	if (st->c33_pw_limit_nb_ranges > 0 &&
	    pse_put_pw_limit_ranges(skb, st))
		return -EMSGSIZE;

	if (st->prio_max &&
	    (nla_put_u32(skb, ETHTOOL_A_PSE_PRIO_MAX, st->prio_max) ||
	     nla_put_u32(skb, ETHTOOL_A_PSE_PRIO, st->prio)))
		return -EMSGSIZE;

	return 0;
}

static void pse_cleanup_data(struct ethnl_reply_data *reply_base)
{
	const struct pse_reply_data *data = PSE_REPDATA(reply_base);

	kfree(data->status.c33_pw_limit_ranges);
}

/* PSE_SET */

const struct nla_policy ethnl_pse_set_policy[ETHTOOL_A_PSE_MAX + 1] = {
	[ETHTOOL_A_PSE_HEADER] = NLA_POLICY_NESTED(ethnl_header_policy_phy),
	[ETHTOOL_A_PODL_PSE_ADMIN_CONTROL] =
		NLA_POLICY_RANGE(NLA_U32, ETHTOOL_PODL_PSE_ADMIN_STATE_DISABLED,
				 ETHTOOL_PODL_PSE_ADMIN_STATE_ENABLED),
	[ETHTOOL_A_C33_PSE_ADMIN_CONTROL] =
		NLA_POLICY_RANGE(NLA_U32, ETHTOOL_C33_PSE_ADMIN_STATE_DISABLED,
				 ETHTOOL_C33_PSE_ADMIN_STATE_ENABLED),
	[ETHTOOL_A_C33_PSE_AVAIL_PW_LIMIT] = { .type = NLA_U32 },
	[ETHTOOL_A_PSE_PRIO] = { .type = NLA_U32 },
};

static int
ethnl_set_pse_validate(struct phy_device *phydev, struct genl_info *info)
{
	struct nlattr **tb = info->attrs;

	if (IS_ERR_OR_NULL(phydev)) {
		NL_SET_ERR_MSG(info->extack, "No PHY is attached");
		return -EOPNOTSUPP;
	}

	if (!phydev->psec) {
		NL_SET_ERR_MSG(info->extack, "No PSE is attached");
		return -EOPNOTSUPP;
	}

	if (tb[ETHTOOL_A_PODL_PSE_ADMIN_CONTROL] &&
	    !pse_has_podl(phydev->psec)) {
		NL_SET_ERR_MSG_ATTR(info->extack,
				    tb[ETHTOOL_A_PODL_PSE_ADMIN_CONTROL],
				    "setting PoDL PSE admin control not supported");
		return -EOPNOTSUPP;
	}
	if (tb[ETHTOOL_A_C33_PSE_ADMIN_CONTROL] &&
	    !pse_has_c33(phydev->psec)) {
		NL_SET_ERR_MSG_ATTR(info->extack,
				    tb[ETHTOOL_A_C33_PSE_ADMIN_CONTROL],
				    "setting C33 PSE admin control not supported");
		return -EOPNOTSUPP;
	}

	return 0;
}

static int
ethnl_set_pse(struct ethnl_req_info *req_info, struct genl_info *info)
{
	struct nlattr **tb = info->attrs;
	struct phy_device *phydev;
	int ret;

	phydev = ethnl_req_get_phydev(req_info, tb, ETHTOOL_A_PSE_HEADER,
				      info->extack);
	ret = ethnl_set_pse_validate(phydev, info);
	if (ret)
		return ret;

	if (tb[ETHTOOL_A_PSE_PRIO]) {
		unsigned int prio;

		prio = nla_get_u32(tb[ETHTOOL_A_PSE_PRIO]);
		ret = pse_ethtool_set_prio(phydev->psec, info->extack, prio);
		if (ret)
			return ret;
	}

	if (tb[ETHTOOL_A_C33_PSE_AVAIL_PW_LIMIT]) {
		unsigned int pw_limit;

		pw_limit = nla_get_u32(tb[ETHTOOL_A_C33_PSE_AVAIL_PW_LIMIT]);
		ret = pse_ethtool_set_pw_limit(phydev->psec, info->extack,
					       pw_limit);
		if (ret)
			return ret;
	}

	/* These values are already validated by the ethnl_pse_set_policy */
	if (tb[ETHTOOL_A_PODL_PSE_ADMIN_CONTROL] ||
	    tb[ETHTOOL_A_C33_PSE_ADMIN_CONTROL]) {
		struct pse_control_config config = {};

		if (tb[ETHTOOL_A_PODL_PSE_ADMIN_CONTROL])
			config.podl_admin_control = nla_get_u32(tb[ETHTOOL_A_PODL_PSE_ADMIN_CONTROL]);
		if (tb[ETHTOOL_A_C33_PSE_ADMIN_CONTROL])
			config.c33_admin_control = nla_get_u32(tb[ETHTOOL_A_C33_PSE_ADMIN_CONTROL]);

		/* pse_ethtool_set_config() will do nothing if the config
		 * is zero
		 */
		ret = pse_ethtool_set_config(phydev->psec, info->extack,
					     &config);
		if (ret)
			return ret;
	}

	/* Return errno or zero - PSE has no notification */
	return ret;
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
	.cleanup_data		= pse_cleanup_data,

	.set			= ethnl_set_pse,
	/* PSE has no notification */
};

void ethnl_pse_send_ntf(struct net_device *netdev, unsigned long notifs)
{
	void *reply_payload;
	struct sk_buff *skb;
	int reply_len;
	int ret;

	ASSERT_RTNL();

	if (!netdev || !notifs)
		return;

	reply_len = ethnl_reply_header_size() +
		    nla_total_size(sizeof(u32)); /* _PSE_NTF_EVENTS */

	skb = genlmsg_new(reply_len, GFP_KERNEL);
	if (!skb)
		return;

	reply_payload = ethnl_bcastmsg_put(skb, ETHTOOL_MSG_PSE_NTF);
	if (!reply_payload)
		goto err_skb;

	ret = ethnl_fill_reply_header(skb, netdev, ETHTOOL_A_PSE_NTF_HEADER);
	if (ret < 0)
		goto err_skb;

	if (nla_put_uint(skb, ETHTOOL_A_PSE_NTF_EVENTS, notifs))
		goto err_skb;

	genlmsg_end(skb, reply_payload);
	ethnl_multicast(skb, netdev);
	return;

err_skb:
	nlmsg_free(skb);
}
EXPORT_SYMBOL_GPL(ethnl_pse_send_ntf);
