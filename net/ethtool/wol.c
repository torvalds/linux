// SPDX-License-Identifier: GPL-2.0-only

#include "netlink.h"
#include "common.h"
#include "bitset.h"

struct wol_req_info {
	struct ethnl_req_info		base;
};

struct wol_reply_data {
	struct ethnl_reply_data		base;
	struct ethtool_wolinfo		wol;
	bool				show_sopass;
};

#define WOL_REPDATA(__reply_base) \
	container_of(__reply_base, struct wol_reply_data, base)

const struct nla_policy ethnl_wol_get_policy[] = {
	[ETHTOOL_A_WOL_HEADER]		=
		NLA_POLICY_NESTED(ethnl_header_policy),
};

static int wol_prepare_data(const struct ethnl_req_info *req_base,
			    struct ethnl_reply_data *reply_base,
			    const struct genl_info *info)
{
	struct wol_reply_data *data = WOL_REPDATA(reply_base);
	struct net_device *dev = reply_base->dev;
	int ret;

	if (!dev->ethtool_ops->get_wol)
		return -EOPNOTSUPP;

	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;
	dev->ethtool_ops->get_wol(dev, &data->wol);
	ethnl_ops_complete(dev);
	/* do not include password in notifications */
	data->show_sopass = !genl_info_is_ntf(info) &&
		(data->wol.supported & WAKE_MAGICSECURE);

	return 0;
}

static int wol_reply_size(const struct ethnl_req_info *req_base,
			  const struct ethnl_reply_data *reply_base)
{
	bool compact = req_base->flags & ETHTOOL_FLAG_COMPACT_BITSETS;
	const struct wol_reply_data *data = WOL_REPDATA(reply_base);
	int len;

	len = ethnl_bitset32_size(&data->wol.wolopts, &data->wol.supported,
				  WOL_MODE_COUNT, wol_mode_names, compact);
	if (len < 0)
		return len;
	if (data->show_sopass)
		len += nla_total_size(sizeof(data->wol.sopass));

	return len;
}

static int wol_fill_reply(struct sk_buff *skb,
			  const struct ethnl_req_info *req_base,
			  const struct ethnl_reply_data *reply_base)
{
	bool compact = req_base->flags & ETHTOOL_FLAG_COMPACT_BITSETS;
	const struct wol_reply_data *data = WOL_REPDATA(reply_base);
	int ret;

	ret = ethnl_put_bitset32(skb, ETHTOOL_A_WOL_MODES, &data->wol.wolopts,
				 &data->wol.supported, WOL_MODE_COUNT,
				 wol_mode_names, compact);
	if (ret < 0)
		return ret;
	if (data->show_sopass &&
	    nla_put(skb, ETHTOOL_A_WOL_SOPASS, sizeof(data->wol.sopass),
		    data->wol.sopass))
		return -EMSGSIZE;

	return 0;
}

/* WOL_SET */

const struct nla_policy ethnl_wol_set_policy[] = {
	[ETHTOOL_A_WOL_HEADER]		=
		NLA_POLICY_NESTED(ethnl_header_policy),
	[ETHTOOL_A_WOL_MODES]		= { .type = NLA_NESTED },
	[ETHTOOL_A_WOL_SOPASS]		= { .type = NLA_BINARY,
					    .len = SOPASS_MAX },
};

static int
ethnl_set_wol_validate(struct ethnl_req_info *req_info, struct genl_info *info)
{
	const struct ethtool_ops *ops = req_info->dev->ethtool_ops;

	return ops->get_wol && ops->set_wol ? 1 : -EOPNOTSUPP;
}

static int
ethnl_set_wol(struct ethnl_req_info *req_info, struct genl_info *info)
{
	struct ethtool_wolinfo wol = { .cmd = ETHTOOL_GWOL };
	struct net_device *dev = req_info->dev;
	struct nlattr **tb = info->attrs;
	bool mod = false;
	int ret;

	dev->ethtool_ops->get_wol(dev, &wol);
	ret = ethnl_update_bitset32(&wol.wolopts, WOL_MODE_COUNT,
				    tb[ETHTOOL_A_WOL_MODES], wol_mode_names,
				    info->extack, &mod);
	if (ret < 0)
		return ret;
	if (wol.wolopts & ~wol.supported) {
		NL_SET_ERR_MSG_ATTR(info->extack, tb[ETHTOOL_A_WOL_MODES],
				    "cannot enable unsupported WoL mode");
		return -EINVAL;
	}
	if (tb[ETHTOOL_A_WOL_SOPASS]) {
		if (!(wol.supported & WAKE_MAGICSECURE)) {
			NL_SET_ERR_MSG_ATTR(info->extack,
					    tb[ETHTOOL_A_WOL_SOPASS],
					    "magicsecure not supported, cannot set password");
			return -EINVAL;
		}
		ethnl_update_binary(wol.sopass, sizeof(wol.sopass),
				    tb[ETHTOOL_A_WOL_SOPASS], &mod);
	}

	if (!mod)
		return 0;
	ret = dev->ethtool_ops->set_wol(dev, &wol);
	if (ret)
		return ret;
	dev->ethtool->wol_enabled = !!wol.wolopts;
	return 1;
}

const struct ethnl_request_ops ethnl_wol_request_ops = {
	.request_cmd		= ETHTOOL_MSG_WOL_GET,
	.reply_cmd		= ETHTOOL_MSG_WOL_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_WOL_HEADER,
	.req_info_size		= sizeof(struct wol_req_info),
	.reply_data_size	= sizeof(struct wol_reply_data),

	.prepare_data		= wol_prepare_data,
	.reply_size		= wol_reply_size,
	.fill_reply		= wol_fill_reply,

	.set_validate		= ethnl_set_wol_validate,
	.set			= ethnl_set_wol,
	.set_ntf_cmd		= ETHTOOL_MSG_WOL_NTF,
};
