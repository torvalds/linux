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

static const struct nla_policy
wol_get_policy[ETHTOOL_A_WOL_MAX + 1] = {
	[ETHTOOL_A_WOL_UNSPEC]		= { .type = NLA_REJECT },
	[ETHTOOL_A_WOL_HEADER]		= { .type = NLA_NESTED },
	[ETHTOOL_A_WOL_MODES]		= { .type = NLA_REJECT },
	[ETHTOOL_A_WOL_SOPASS]		= { .type = NLA_REJECT },
};

static int wol_prepare_data(const struct ethnl_req_info *req_base,
			    struct ethnl_reply_data *reply_base,
			    struct genl_info *info)
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
	data->show_sopass = info && (data->wol.supported & WAKE_MAGICSECURE);

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

const struct ethnl_request_ops ethnl_wol_request_ops = {
	.request_cmd		= ETHTOOL_MSG_WOL_GET,
	.reply_cmd		= ETHTOOL_MSG_WOL_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_WOL_HEADER,
	.max_attr		= ETHTOOL_A_WOL_MAX,
	.req_info_size		= sizeof(struct wol_req_info),
	.reply_data_size	= sizeof(struct wol_reply_data),
	.request_policy		= wol_get_policy,

	.prepare_data		= wol_prepare_data,
	.reply_size		= wol_reply_size,
	.fill_reply		= wol_fill_reply,
};

/* WOL_SET */

static const struct nla_policy
wol_set_policy[ETHTOOL_A_WOL_MAX + 1] = {
	[ETHTOOL_A_WOL_UNSPEC]		= { .type = NLA_REJECT },
	[ETHTOOL_A_WOL_HEADER]		= { .type = NLA_NESTED },
	[ETHTOOL_A_WOL_MODES]		= { .type = NLA_NESTED },
	[ETHTOOL_A_WOL_SOPASS]		= { .type = NLA_BINARY,
					    .len = SOPASS_MAX },
};

int ethnl_set_wol(struct sk_buff *skb, struct genl_info *info)
{
	struct ethtool_wolinfo wol = { .cmd = ETHTOOL_GWOL };
	struct nlattr *tb[ETHTOOL_A_WOL_MAX + 1];
	struct ethnl_req_info req_info = {};
	struct net_device *dev;
	bool mod = false;
	int ret;

	ret = nlmsg_parse(info->nlhdr, GENL_HDRLEN, tb, ETHTOOL_A_WOL_MAX,
			  wol_set_policy, info->extack);
	if (ret < 0)
		return ret;
	ret = ethnl_parse_header_dev_get(&req_info, tb[ETHTOOL_A_WOL_HEADER],
					 genl_info_net(info), info->extack,
					 true);
	if (ret < 0)
		return ret;
	dev = req_info.dev;
	ret = -EOPNOTSUPP;
	if (!dev->ethtool_ops->get_wol || !dev->ethtool_ops->set_wol)
		goto out_dev;

	rtnl_lock();
	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		goto out_rtnl;

	dev->ethtool_ops->get_wol(dev, &wol);
	ret = ethnl_update_bitset32(&wol.wolopts, WOL_MODE_COUNT,
				    tb[ETHTOOL_A_WOL_MODES], wol_mode_names,
				    info->extack, &mod);
	if (ret < 0)
		goto out_ops;
	if (wol.wolopts & ~wol.supported) {
		NL_SET_ERR_MSG_ATTR(info->extack, tb[ETHTOOL_A_WOL_MODES],
				    "cannot enable unsupported WoL mode");
		ret = -EINVAL;
		goto out_ops;
	}
	if (tb[ETHTOOL_A_WOL_SOPASS]) {
		if (!(wol.supported & WAKE_MAGICSECURE)) {
			NL_SET_ERR_MSG_ATTR(info->extack,
					    tb[ETHTOOL_A_WOL_SOPASS],
					    "magicsecure not supported, cannot set password");
			ret = -EINVAL;
			goto out_ops;
		}
		ethnl_update_binary(wol.sopass, sizeof(wol.sopass),
				    tb[ETHTOOL_A_WOL_SOPASS], &mod);
	}

	if (!mod)
		goto out_ops;
	ret = dev->ethtool_ops->set_wol(dev, &wol);
	if (ret)
		goto out_ops;
	dev->wol_enabled = !!wol.wolopts;
	ethtool_notify(dev, ETHTOOL_MSG_WOL_NTF, NULL);

out_ops:
	ethnl_ops_complete(dev);
out_rtnl:
	rtnl_unlock();
out_dev:
	dev_put(dev);
	return ret;
}
