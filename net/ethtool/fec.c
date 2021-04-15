// SPDX-License-Identifier: GPL-2.0-only

#include "netlink.h"
#include "common.h"
#include "bitset.h"

struct fec_req_info {
	struct ethnl_req_info		base;
};

struct fec_reply_data {
	struct ethnl_reply_data		base;
	__ETHTOOL_DECLARE_LINK_MODE_MASK(fec_link_modes);
	u32 active_fec;
	u8 fec_auto;
};

#define FEC_REPDATA(__reply_base) \
	container_of(__reply_base, struct fec_reply_data, base)

#define ETHTOOL_FEC_MASK	((ETHTOOL_FEC_LLRS << 1) - 1)

const struct nla_policy ethnl_fec_get_policy[ETHTOOL_A_FEC_HEADER + 1] = {
	[ETHTOOL_A_FEC_HEADER]	= NLA_POLICY_NESTED(ethnl_header_policy),
};

static void
ethtool_fec_to_link_modes(u32 fec, unsigned long *link_modes, u8 *fec_auto)
{
	if (fec_auto)
		*fec_auto = !!(fec & ETHTOOL_FEC_AUTO);

	if (fec & ETHTOOL_FEC_OFF)
		__set_bit(ETHTOOL_LINK_MODE_FEC_NONE_BIT, link_modes);
	if (fec & ETHTOOL_FEC_RS)
		__set_bit(ETHTOOL_LINK_MODE_FEC_RS_BIT, link_modes);
	if (fec & ETHTOOL_FEC_BASER)
		__set_bit(ETHTOOL_LINK_MODE_FEC_BASER_BIT, link_modes);
	if (fec & ETHTOOL_FEC_LLRS)
		__set_bit(ETHTOOL_LINK_MODE_FEC_LLRS_BIT, link_modes);
}

static int
ethtool_link_modes_to_fecparam(struct ethtool_fecparam *fec,
			       unsigned long *link_modes, u8 fec_auto)
{
	memset(fec, 0, sizeof(*fec));

	if (fec_auto)
		fec->fec |= ETHTOOL_FEC_AUTO;

	if (__test_and_clear_bit(ETHTOOL_LINK_MODE_FEC_NONE_BIT, link_modes))
		fec->fec |= ETHTOOL_FEC_OFF;
	if (__test_and_clear_bit(ETHTOOL_LINK_MODE_FEC_RS_BIT, link_modes))
		fec->fec |= ETHTOOL_FEC_RS;
	if (__test_and_clear_bit(ETHTOOL_LINK_MODE_FEC_BASER_BIT, link_modes))
		fec->fec |= ETHTOOL_FEC_BASER;
	if (__test_and_clear_bit(ETHTOOL_LINK_MODE_FEC_LLRS_BIT, link_modes))
		fec->fec |= ETHTOOL_FEC_LLRS;

	if (!bitmap_empty(link_modes, __ETHTOOL_LINK_MODE_MASK_NBITS))
		return -EINVAL;

	return 0;
}

static int fec_prepare_data(const struct ethnl_req_info *req_base,
			    struct ethnl_reply_data *reply_base,
			    struct genl_info *info)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(active_fec_modes) = {};
	struct fec_reply_data *data = FEC_REPDATA(reply_base);
	struct net_device *dev = reply_base->dev;
	struct ethtool_fecparam fec = {};
	int ret;

	if (!dev->ethtool_ops->get_fecparam)
		return -EOPNOTSUPP;
	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;
	ret = dev->ethtool_ops->get_fecparam(dev, &fec);
	if (ret)
		goto out_complete;

	WARN_ON_ONCE(fec.reserved);

	ethtool_fec_to_link_modes(fec.fec, data->fec_link_modes,
				  &data->fec_auto);

	ethtool_fec_to_link_modes(fec.active_fec, active_fec_modes, NULL);
	data->active_fec = find_first_bit(active_fec_modes,
					  __ETHTOOL_LINK_MODE_MASK_NBITS);
	/* Don't report attr if no FEC mode set. Note that
	 * ethtool_fecparam_to_link_modes() ignores NONE and AUTO.
	 */
	if (data->active_fec == __ETHTOOL_LINK_MODE_MASK_NBITS)
		data->active_fec = 0;

out_complete:
	ethnl_ops_complete(dev);
	return ret;
}

static int fec_reply_size(const struct ethnl_req_info *req_base,
			  const struct ethnl_reply_data *reply_base)
{
	bool compact = req_base->flags & ETHTOOL_FLAG_COMPACT_BITSETS;
	const struct fec_reply_data *data = FEC_REPDATA(reply_base);
	int len = 0;
	int ret;

	ret = ethnl_bitset_size(data->fec_link_modes, NULL,
				__ETHTOOL_LINK_MODE_MASK_NBITS,
				link_mode_names, compact);
	if (ret < 0)
		return ret;
	len += ret;

	len += nla_total_size(sizeof(u8)) +	/* _FEC_AUTO */
	       nla_total_size(sizeof(u32));	/* _FEC_ACTIVE */

	return len;
}

static int fec_fill_reply(struct sk_buff *skb,
			  const struct ethnl_req_info *req_base,
			  const struct ethnl_reply_data *reply_base)
{
	bool compact = req_base->flags & ETHTOOL_FLAG_COMPACT_BITSETS;
	const struct fec_reply_data *data = FEC_REPDATA(reply_base);
	int ret;

	ret = ethnl_put_bitset(skb, ETHTOOL_A_FEC_MODES,
			       data->fec_link_modes, NULL,
			       __ETHTOOL_LINK_MODE_MASK_NBITS,
			       link_mode_names, compact);
	if (ret < 0)
		return ret;

	if (nla_put_u8(skb, ETHTOOL_A_FEC_AUTO, data->fec_auto) ||
	    (data->active_fec &&
	     nla_put_u32(skb, ETHTOOL_A_FEC_ACTIVE, data->active_fec)))
		return -EMSGSIZE;

	return 0;
}

const struct ethnl_request_ops ethnl_fec_request_ops = {
	.request_cmd		= ETHTOOL_MSG_FEC_GET,
	.reply_cmd		= ETHTOOL_MSG_FEC_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_FEC_HEADER,
	.req_info_size		= sizeof(struct fec_req_info),
	.reply_data_size	= sizeof(struct fec_reply_data),

	.prepare_data		= fec_prepare_data,
	.reply_size		= fec_reply_size,
	.fill_reply		= fec_fill_reply,
};

/* FEC_SET */

const struct nla_policy ethnl_fec_set_policy[ETHTOOL_A_FEC_AUTO + 1] = {
	[ETHTOOL_A_FEC_HEADER]	= NLA_POLICY_NESTED(ethnl_header_policy),
	[ETHTOOL_A_FEC_MODES]	= { .type = NLA_NESTED },
	[ETHTOOL_A_FEC_AUTO]	= NLA_POLICY_MAX(NLA_U8, 1),
};

int ethnl_set_fec(struct sk_buff *skb, struct genl_info *info)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(fec_link_modes) = {};
	struct ethnl_req_info req_info = {};
	struct nlattr **tb = info->attrs;
	struct ethtool_fecparam fec = {};
	const struct ethtool_ops *ops;
	struct net_device *dev;
	bool mod = false;
	u8 fec_auto;
	int ret;

	ret = ethnl_parse_header_dev_get(&req_info, tb[ETHTOOL_A_FEC_HEADER],
					 genl_info_net(info), info->extack,
					 true);
	if (ret < 0)
		return ret;
	dev = req_info.dev;
	ops = dev->ethtool_ops;
	ret = -EOPNOTSUPP;
	if (!ops->get_fecparam || !ops->set_fecparam)
		goto out_dev;

	rtnl_lock();
	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		goto out_rtnl;
	ret = ops->get_fecparam(dev, &fec);
	if (ret < 0)
		goto out_ops;

	ethtool_fec_to_link_modes(fec.fec, fec_link_modes, &fec_auto);

	ret = ethnl_update_bitset(fec_link_modes,
				  __ETHTOOL_LINK_MODE_MASK_NBITS,
				  tb[ETHTOOL_A_FEC_MODES],
				  link_mode_names, info->extack, &mod);
	if (ret < 0)
		goto out_ops;
	ethnl_update_u8(&fec_auto, tb[ETHTOOL_A_FEC_AUTO], &mod);

	ret = 0;
	if (!mod)
		goto out_ops;

	ret = ethtool_link_modes_to_fecparam(&fec, fec_link_modes, fec_auto);
	if (ret) {
		NL_SET_ERR_MSG_ATTR(info->extack, tb[ETHTOOL_A_FEC_MODES],
				    "invalid FEC modes requested");
		goto out_ops;
	}
	if (!fec.fec) {
		ret = -EINVAL;
		NL_SET_ERR_MSG_ATTR(info->extack, tb[ETHTOOL_A_FEC_MODES],
				    "no FEC modes set");
		goto out_ops;
	}

	ret = dev->ethtool_ops->set_fecparam(dev, &fec);
	if (ret < 0)
		goto out_ops;
	ethtool_notify(dev, ETHTOOL_MSG_FEC_NTF, NULL);

out_ops:
	ethnl_ops_complete(dev);
out_rtnl:
	rtnl_unlock();
out_dev:
	dev_put(dev);
	return ret;
}
