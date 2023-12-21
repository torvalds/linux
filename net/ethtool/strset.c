// SPDX-License-Identifier: GPL-2.0-only

#include <linux/ethtool.h>
#include <linux/phy.h>
#include "netlink.h"
#include "common.h"

struct strset_info {
	bool per_dev;
	bool free_strings;
	unsigned int count;
	const char (*strings)[ETH_GSTRING_LEN];
};

static const struct strset_info info_template[] = {
	[ETH_SS_TEST] = {
		.per_dev	= true,
	},
	[ETH_SS_STATS] = {
		.per_dev	= true,
	},
	[ETH_SS_PRIV_FLAGS] = {
		.per_dev	= true,
	},
	[ETH_SS_FEATURES] = {
		.per_dev	= false,
		.count		= ARRAY_SIZE(netdev_features_strings),
		.strings	= netdev_features_strings,
	},
	[ETH_SS_RSS_HASH_FUNCS] = {
		.per_dev	= false,
		.count		= ARRAY_SIZE(rss_hash_func_strings),
		.strings	= rss_hash_func_strings,
	},
	[ETH_SS_TUNABLES] = {
		.per_dev	= false,
		.count		= ARRAY_SIZE(tunable_strings),
		.strings	= tunable_strings,
	},
	[ETH_SS_PHY_STATS] = {
		.per_dev	= true,
	},
	[ETH_SS_PHY_TUNABLES] = {
		.per_dev	= false,
		.count		= ARRAY_SIZE(phy_tunable_strings),
		.strings	= phy_tunable_strings,
	},
	[ETH_SS_LINK_MODES] = {
		.per_dev	= false,
		.count		= __ETHTOOL_LINK_MODE_MASK_NBITS,
		.strings	= link_mode_names,
	},
	[ETH_SS_MSG_CLASSES] = {
		.per_dev	= false,
		.count		= NETIF_MSG_CLASS_COUNT,
		.strings	= netif_msg_class_names,
	},
	[ETH_SS_WOL_MODES] = {
		.per_dev	= false,
		.count		= WOL_MODE_COUNT,
		.strings	= wol_mode_names,
	},
	[ETH_SS_SOF_TIMESTAMPING] = {
		.per_dev	= false,
		.count		= __SOF_TIMESTAMPING_CNT,
		.strings	= sof_timestamping_names,
	},
	[ETH_SS_TS_TX_TYPES] = {
		.per_dev	= false,
		.count		= __HWTSTAMP_TX_CNT,
		.strings	= ts_tx_type_names,
	},
	[ETH_SS_TS_RX_FILTERS] = {
		.per_dev	= false,
		.count		= __HWTSTAMP_FILTER_CNT,
		.strings	= ts_rx_filter_names,
	},
	[ETH_SS_UDP_TUNNEL_TYPES] = {
		.per_dev	= false,
		.count		= __ETHTOOL_UDP_TUNNEL_TYPE_CNT,
		.strings	= udp_tunnel_type_names,
	},
	[ETH_SS_STATS_STD] = {
		.per_dev	= false,
		.count		= __ETHTOOL_STATS_CNT,
		.strings	= stats_std_names,
	},
	[ETH_SS_STATS_ETH_PHY] = {
		.per_dev	= false,
		.count		= __ETHTOOL_A_STATS_ETH_PHY_CNT,
		.strings	= stats_eth_phy_names,
	},
	[ETH_SS_STATS_ETH_MAC] = {
		.per_dev	= false,
		.count		= __ETHTOOL_A_STATS_ETH_MAC_CNT,
		.strings	= stats_eth_mac_names,
	},
	[ETH_SS_STATS_ETH_CTRL] = {
		.per_dev	= false,
		.count		= __ETHTOOL_A_STATS_ETH_CTRL_CNT,
		.strings	= stats_eth_ctrl_names,
	},
	[ETH_SS_STATS_RMON] = {
		.per_dev	= false,
		.count		= __ETHTOOL_A_STATS_RMON_CNT,
		.strings	= stats_rmon_names,
	},
};

struct strset_req_info {
	struct ethnl_req_info		base;
	u32				req_ids;
	bool				counts_only;
};

#define STRSET_REQINFO(__req_base) \
	container_of(__req_base, struct strset_req_info, base)

struct strset_reply_data {
	struct ethnl_reply_data		base;
	struct strset_info		sets[ETH_SS_COUNT];
};

#define STRSET_REPDATA(__reply_base) \
	container_of(__reply_base, struct strset_reply_data, base)

const struct nla_policy ethnl_strset_get_policy[] = {
	[ETHTOOL_A_STRSET_HEADER]	=
		NLA_POLICY_NESTED(ethnl_header_policy),
	[ETHTOOL_A_STRSET_STRINGSETS]	= { .type = NLA_NESTED },
	[ETHTOOL_A_STRSET_COUNTS_ONLY]	= { .type = NLA_FLAG },
};

static const struct nla_policy get_stringset_policy[] = {
	[ETHTOOL_A_STRINGSET_ID]	= { .type = NLA_U32 },
};

/**
 * strset_include() - test if a string set should be included in reply
 * @info: parsed client request
 * @data: pointer to request data structure
 * @id:   id of string set to check (ETH_SS_* constants)
 */
static bool strset_include(const struct strset_req_info *info,
			   const struct strset_reply_data *data, u32 id)
{
	bool per_dev;

	BUILD_BUG_ON(ETH_SS_COUNT >= BITS_PER_BYTE * sizeof(info->req_ids));

	if (info->req_ids)
		return info->req_ids & (1U << id);
	per_dev = data->sets[id].per_dev;
	if (!per_dev && !data->sets[id].strings)
		return false;

	return data->base.dev ? per_dev : !per_dev;
}

static int strset_get_id(const struct nlattr *nest, u32 *val,
			 struct netlink_ext_ack *extack)
{
	struct nlattr *tb[ARRAY_SIZE(get_stringset_policy)];
	int ret;

	ret = nla_parse_nested(tb, ARRAY_SIZE(get_stringset_policy) - 1, nest,
			       get_stringset_policy, extack);
	if (ret < 0)
		return ret;
	if (NL_REQ_ATTR_CHECK(extack, nest, tb, ETHTOOL_A_STRINGSET_ID))
		return -EINVAL;

	*val = nla_get_u32(tb[ETHTOOL_A_STRINGSET_ID]);
	return 0;
}

static const struct nla_policy strset_stringsets_policy[] = {
	[ETHTOOL_A_STRINGSETS_STRINGSET]	= { .type = NLA_NESTED },
};

static int strset_parse_request(struct ethnl_req_info *req_base,
				struct nlattr **tb,
				struct netlink_ext_ack *extack)
{
	struct strset_req_info *req_info = STRSET_REQINFO(req_base);
	struct nlattr *nest = tb[ETHTOOL_A_STRSET_STRINGSETS];
	struct nlattr *attr;
	int rem, ret;

	if (!nest)
		return 0;
	ret = nla_validate_nested(nest,
				  ARRAY_SIZE(strset_stringsets_policy) - 1,
				  strset_stringsets_policy, extack);
	if (ret < 0)
		return ret;

	req_info->counts_only = tb[ETHTOOL_A_STRSET_COUNTS_ONLY];
	nla_for_each_nested(attr, nest, rem) {
		u32 id;

		if (WARN_ONCE(nla_type(attr) != ETHTOOL_A_STRINGSETS_STRINGSET,
			      "unexpected attrtype %u in ETHTOOL_A_STRSET_STRINGSETS\n",
			      nla_type(attr)))
			return -EINVAL;

		ret = strset_get_id(attr, &id, extack);
		if (ret < 0)
			return ret;
		if (id >= ETH_SS_COUNT) {
			NL_SET_ERR_MSG_ATTR(extack, attr,
					    "unknown string set id");
			return -EOPNOTSUPP;
		}

		req_info->req_ids |= (1U << id);
	}

	return 0;
}

static void strset_cleanup_data(struct ethnl_reply_data *reply_base)
{
	struct strset_reply_data *data = STRSET_REPDATA(reply_base);
	unsigned int i;

	for (i = 0; i < ETH_SS_COUNT; i++)
		if (data->sets[i].free_strings) {
			kfree(data->sets[i].strings);
			data->sets[i].strings = NULL;
			data->sets[i].free_strings = false;
		}
}

static int strset_prepare_set(struct strset_info *info, struct net_device *dev,
			      struct phy_device *phydev, unsigned int id,
			      bool counts_only)
{
	const struct ethtool_phy_ops *phy_ops = ethtool_phy_ops;
	const struct ethtool_ops *ops = dev->ethtool_ops;
	void *strings;
	int count, ret;

	if (id == ETH_SS_PHY_STATS && phydev &&
	    !ops->get_ethtool_phy_stats && phy_ops &&
	    phy_ops->get_sset_count)
		ret = phy_ops->get_sset_count(phydev);
	else if (ops->get_sset_count && ops->get_strings)
		ret = ops->get_sset_count(dev, id);
	else
		ret = -EOPNOTSUPP;
	if (ret <= 0) {
		info->count = 0;
		return 0;
	}

	count = ret;
	if (!counts_only) {
		strings = kcalloc(count, ETH_GSTRING_LEN, GFP_KERNEL);
		if (!strings)
			return -ENOMEM;
		if (id == ETH_SS_PHY_STATS && phydev &&
		    !ops->get_ethtool_phy_stats && phy_ops &&
		    phy_ops->get_strings)
			phy_ops->get_strings(phydev, strings);
		else
			ops->get_strings(dev, id, strings);
		info->strings = strings;
		info->free_strings = true;
	}
	info->count = count;

	return 0;
}

static int strset_prepare_data(const struct ethnl_req_info *req_base,
			       struct ethnl_reply_data *reply_base,
			       const struct genl_info *info)
{
	const struct strset_req_info *req_info = STRSET_REQINFO(req_base);
	struct strset_reply_data *data = STRSET_REPDATA(reply_base);
	struct net_device *dev = reply_base->dev;
	unsigned int i;
	int ret;

	BUILD_BUG_ON(ARRAY_SIZE(info_template) != ETH_SS_COUNT);
	memcpy(&data->sets, &info_template, sizeof(data->sets));

	if (!dev) {
		for (i = 0; i < ETH_SS_COUNT; i++) {
			if ((req_info->req_ids & (1U << i)) &&
			    data->sets[i].per_dev) {
				if (info)
					GENL_SET_ERR_MSG(info, "requested per device strings without dev");
				return -EINVAL;
			}
		}
		return 0;
	}

	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		goto err_strset;
	for (i = 0; i < ETH_SS_COUNT; i++) {
		if (!strset_include(req_info, data, i) ||
		    !data->sets[i].per_dev)
			continue;

		ret = strset_prepare_set(&data->sets[i], dev, req_base->phydev,
					 i, req_info->counts_only);
		if (ret < 0)
			goto err_ops;
	}
	ethnl_ops_complete(dev);

	return 0;
err_ops:
	ethnl_ops_complete(dev);
err_strset:
	strset_cleanup_data(reply_base);
	return ret;
}

/* calculate size of ETHTOOL_A_STRSET_STRINGSET nest for one string set */
static int strset_set_size(const struct strset_info *info, bool counts_only)
{
	unsigned int len = 0;
	unsigned int i;

	if (info->count == 0)
		return 0;
	if (counts_only)
		return nla_total_size(2 * nla_total_size(sizeof(u32)));

	for (i = 0; i < info->count; i++) {
		const char *str = info->strings[i];

		/* ETHTOOL_A_STRING_INDEX, ETHTOOL_A_STRING_VALUE, nest */
		len += nla_total_size(nla_total_size(sizeof(u32)) +
				      ethnl_strz_size(str));
	}
	/* ETHTOOL_A_STRINGSET_ID, ETHTOOL_A_STRINGSET_COUNT */
	len = 2 * nla_total_size(sizeof(u32)) + nla_total_size(len);

	return nla_total_size(len);
}

static int strset_reply_size(const struct ethnl_req_info *req_base,
			     const struct ethnl_reply_data *reply_base)
{
	const struct strset_req_info *req_info = STRSET_REQINFO(req_base);
	const struct strset_reply_data *data = STRSET_REPDATA(reply_base);
	unsigned int i;
	int len = 0;
	int ret;

	len += nla_total_size(0); /* ETHTOOL_A_STRSET_STRINGSETS */

	for (i = 0; i < ETH_SS_COUNT; i++) {
		const struct strset_info *set_info = &data->sets[i];

		if (!strset_include(req_info, data, i))
			continue;

		ret = strset_set_size(set_info, req_info->counts_only);
		if (ret < 0)
			return ret;
		len += ret;
	}

	return len;
}

/* fill one string into reply */
static int strset_fill_string(struct sk_buff *skb,
			      const struct strset_info *set_info, u32 idx)
{
	struct nlattr *string_attr;
	const char *value;

	value = set_info->strings[idx];

	string_attr = nla_nest_start(skb, ETHTOOL_A_STRINGS_STRING);
	if (!string_attr)
		return -EMSGSIZE;
	if (nla_put_u32(skb, ETHTOOL_A_STRING_INDEX, idx) ||
	    ethnl_put_strz(skb, ETHTOOL_A_STRING_VALUE, value))
		goto nla_put_failure;
	nla_nest_end(skb, string_attr);

	return 0;
nla_put_failure:
	nla_nest_cancel(skb, string_attr);
	return -EMSGSIZE;
}

/* fill one string set into reply */
static int strset_fill_set(struct sk_buff *skb,
			   const struct strset_info *set_info, u32 id,
			   bool counts_only)
{
	struct nlattr *stringset_attr;
	struct nlattr *strings_attr;
	unsigned int i;

	if (!set_info->per_dev && !set_info->strings)
		return -EOPNOTSUPP;
	if (set_info->count == 0)
		return 0;
	stringset_attr = nla_nest_start(skb, ETHTOOL_A_STRINGSETS_STRINGSET);
	if (!stringset_attr)
		return -EMSGSIZE;

	if (nla_put_u32(skb, ETHTOOL_A_STRINGSET_ID, id) ||
	    nla_put_u32(skb, ETHTOOL_A_STRINGSET_COUNT, set_info->count))
		goto nla_put_failure;

	if (!counts_only) {
		strings_attr = nla_nest_start(skb, ETHTOOL_A_STRINGSET_STRINGS);
		if (!strings_attr)
			goto nla_put_failure;
		for (i = 0; i < set_info->count; i++) {
			if (strset_fill_string(skb, set_info, i) < 0)
				goto nla_put_failure;
		}
		nla_nest_end(skb, strings_attr);
	}

	nla_nest_end(skb, stringset_attr);
	return 0;

nla_put_failure:
	nla_nest_cancel(skb, stringset_attr);
	return -EMSGSIZE;
}

static int strset_fill_reply(struct sk_buff *skb,
			     const struct ethnl_req_info *req_base,
			     const struct ethnl_reply_data *reply_base)
{
	const struct strset_req_info *req_info = STRSET_REQINFO(req_base);
	const struct strset_reply_data *data = STRSET_REPDATA(reply_base);
	struct nlattr *nest;
	unsigned int i;
	int ret;

	nest = nla_nest_start(skb, ETHTOOL_A_STRSET_STRINGSETS);
	if (!nest)
		return -EMSGSIZE;

	for (i = 0; i < ETH_SS_COUNT; i++) {
		if (strset_include(req_info, data, i)) {
			ret = strset_fill_set(skb, &data->sets[i], i,
					      req_info->counts_only);
			if (ret < 0)
				goto nla_put_failure;
		}
	}

	nla_nest_end(skb, nest);
	return 0;

nla_put_failure:
	nla_nest_cancel(skb, nest);
	return ret;
}

const struct ethnl_request_ops ethnl_strset_request_ops = {
	.request_cmd		= ETHTOOL_MSG_STRSET_GET,
	.reply_cmd		= ETHTOOL_MSG_STRSET_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_STRSET_HEADER,
	.req_info_size		= sizeof(struct strset_req_info),
	.reply_data_size	= sizeof(struct strset_reply_data),
	.allow_nodev_do		= true,

	.parse_request		= strset_parse_request,
	.prepare_data		= strset_prepare_data,
	.reply_size		= strset_reply_size,
	.fill_reply		= strset_fill_reply,
	.cleanup_data		= strset_cleanup_data,
};
