// SPDX-License-Identifier: GPL-2.0-only

#include "netlink.h"
#include "common.h"
#include "bitset.h"

/* LINKMODES_GET */

struct linkmodes_req_info {
	struct ethnl_req_info		base;
};

struct linkmodes_reply_data {
	struct ethnl_reply_data		base;
	struct ethtool_link_ksettings	ksettings;
	struct ethtool_link_settings	*lsettings;
	bool				peer_empty;
};

#define LINKMODES_REPDATA(__reply_base) \
	container_of(__reply_base, struct linkmodes_reply_data, base)

const struct nla_policy ethnl_linkmodes_get_policy[] = {
	[ETHTOOL_A_LINKMODES_HEADER]		=
		NLA_POLICY_NESTED(ethnl_header_policy),
};

static int linkmodes_prepare_data(const struct ethnl_req_info *req_base,
				  struct ethnl_reply_data *reply_base,
				  const struct genl_info *info)
{
	struct linkmodes_reply_data *data = LINKMODES_REPDATA(reply_base);
	struct net_device *dev = reply_base->dev;
	int ret;

	data->lsettings = &data->ksettings.base;

	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;

	ret = __ethtool_get_link_ksettings(dev, &data->ksettings);
	if (ret < 0 && info) {
		GENL_SET_ERR_MSG(info, "failed to retrieve link settings");
		goto out;
	}

	if (!dev->ethtool_ops->cap_link_lanes_supported)
		data->ksettings.lanes = 0;

	data->peer_empty =
		bitmap_empty(data->ksettings.link_modes.lp_advertising,
			     __ETHTOOL_LINK_MODE_MASK_NBITS);

out:
	ethnl_ops_complete(dev);
	return ret;
}

static int linkmodes_reply_size(const struct ethnl_req_info *req_base,
				const struct ethnl_reply_data *reply_base)
{
	const struct linkmodes_reply_data *data = LINKMODES_REPDATA(reply_base);
	const struct ethtool_link_ksettings *ksettings = &data->ksettings;
	const struct ethtool_link_settings *lsettings = &ksettings->base;
	bool compact = req_base->flags & ETHTOOL_FLAG_COMPACT_BITSETS;
	int len, ret;

	len = nla_total_size(sizeof(u8)) /* LINKMODES_AUTONEG */
		+ nla_total_size(sizeof(u32)) /* LINKMODES_SPEED */
		+ nla_total_size(sizeof(u32)) /* LINKMODES_LANES */
		+ nla_total_size(sizeof(u8)) /* LINKMODES_DUPLEX */
		+ nla_total_size(sizeof(u8)) /* LINKMODES_RATE_MATCHING */
		+ 0;
	ret = ethnl_bitset_size(ksettings->link_modes.advertising,
				ksettings->link_modes.supported,
				__ETHTOOL_LINK_MODE_MASK_NBITS,
				link_mode_names, compact);
	if (ret < 0)
		return ret;
	len += ret;
	if (!data->peer_empty) {
		ret = ethnl_bitset_size(ksettings->link_modes.lp_advertising,
					NULL, __ETHTOOL_LINK_MODE_MASK_NBITS,
					link_mode_names, compact);
		if (ret < 0)
			return ret;
		len += ret;
	}

	if (lsettings->master_slave_cfg != MASTER_SLAVE_CFG_UNSUPPORTED)
		len += nla_total_size(sizeof(u8));

	if (lsettings->master_slave_state != MASTER_SLAVE_STATE_UNSUPPORTED)
		len += nla_total_size(sizeof(u8));

	return len;
}

static int linkmodes_fill_reply(struct sk_buff *skb,
				const struct ethnl_req_info *req_base,
				const struct ethnl_reply_data *reply_base)
{
	const struct linkmodes_reply_data *data = LINKMODES_REPDATA(reply_base);
	const struct ethtool_link_ksettings *ksettings = &data->ksettings;
	const struct ethtool_link_settings *lsettings = &ksettings->base;
	bool compact = req_base->flags & ETHTOOL_FLAG_COMPACT_BITSETS;
	int ret;

	if (nla_put_u8(skb, ETHTOOL_A_LINKMODES_AUTONEG, lsettings->autoneg))
		return -EMSGSIZE;

	ret = ethnl_put_bitset(skb, ETHTOOL_A_LINKMODES_OURS,
			       ksettings->link_modes.advertising,
			       ksettings->link_modes.supported,
			       __ETHTOOL_LINK_MODE_MASK_NBITS, link_mode_names,
			       compact);
	if (ret < 0)
		return -EMSGSIZE;
	if (!data->peer_empty) {
		ret = ethnl_put_bitset(skb, ETHTOOL_A_LINKMODES_PEER,
				       ksettings->link_modes.lp_advertising,
				       NULL, __ETHTOOL_LINK_MODE_MASK_NBITS,
				       link_mode_names, compact);
		if (ret < 0)
			return -EMSGSIZE;
	}

	if (nla_put_u32(skb, ETHTOOL_A_LINKMODES_SPEED, lsettings->speed) ||
	    nla_put_u8(skb, ETHTOOL_A_LINKMODES_DUPLEX, lsettings->duplex))
		return -EMSGSIZE;

	if (ksettings->lanes &&
	    nla_put_u32(skb, ETHTOOL_A_LINKMODES_LANES, ksettings->lanes))
		return -EMSGSIZE;

	if (lsettings->master_slave_cfg != MASTER_SLAVE_CFG_UNSUPPORTED &&
	    nla_put_u8(skb, ETHTOOL_A_LINKMODES_MASTER_SLAVE_CFG,
		       lsettings->master_slave_cfg))
		return -EMSGSIZE;

	if (lsettings->master_slave_state != MASTER_SLAVE_STATE_UNSUPPORTED &&
	    nla_put_u8(skb, ETHTOOL_A_LINKMODES_MASTER_SLAVE_STATE,
		       lsettings->master_slave_state))
		return -EMSGSIZE;

	if (nla_put_u8(skb, ETHTOOL_A_LINKMODES_RATE_MATCHING,
		       lsettings->rate_matching))
		return -EMSGSIZE;

	return 0;
}

/* LINKMODES_SET */

const struct nla_policy ethnl_linkmodes_set_policy[] = {
	[ETHTOOL_A_LINKMODES_HEADER]		=
		NLA_POLICY_NESTED(ethnl_header_policy),
	[ETHTOOL_A_LINKMODES_AUTONEG]		= { .type = NLA_U8 },
	[ETHTOOL_A_LINKMODES_OURS]		= { .type = NLA_NESTED },
	[ETHTOOL_A_LINKMODES_SPEED]		= { .type = NLA_U32 },
	[ETHTOOL_A_LINKMODES_DUPLEX]		= { .type = NLA_U8 },
	[ETHTOOL_A_LINKMODES_MASTER_SLAVE_CFG]	= { .type = NLA_U8 },
	[ETHTOOL_A_LINKMODES_LANES]		= NLA_POLICY_RANGE(NLA_U32, 1, 8),
};

/* Set advertised link modes to all supported modes matching requested speed,
 * lanes and duplex values. Called when autonegotiation is on, speed, lanes or
 * duplex is requested but no link mode change. This is done in userspace with
 * ioctl() interface, move it into kernel for netlink.
 * Returns true if advertised modes bitmap was modified.
 */
static bool ethnl_auto_linkmodes(struct ethtool_link_ksettings *ksettings,
				 bool req_speed, bool req_lanes, bool req_duplex)
{
	unsigned long *advertising = ksettings->link_modes.advertising;
	unsigned long *supported = ksettings->link_modes.supported;
	DECLARE_BITMAP(old_adv, __ETHTOOL_LINK_MODE_MASK_NBITS);
	unsigned int i;

	bitmap_copy(old_adv, advertising, __ETHTOOL_LINK_MODE_MASK_NBITS);

	for (i = 0; i < __ETHTOOL_LINK_MODE_MASK_NBITS; i++) {
		const struct link_mode_info *info = &link_mode_params[i];

		if (info->speed == SPEED_UNKNOWN)
			continue;
		if (test_bit(i, supported) &&
		    (!req_speed || info->speed == ksettings->base.speed) &&
		    (!req_lanes || info->lanes == ksettings->lanes) &&
		    (!req_duplex || info->duplex == ksettings->base.duplex))
			set_bit(i, advertising);
		else
			clear_bit(i, advertising);
	}

	return !bitmap_equal(old_adv, advertising,
			     __ETHTOOL_LINK_MODE_MASK_NBITS);
}

static bool ethnl_validate_master_slave_cfg(u8 cfg)
{
	switch (cfg) {
	case MASTER_SLAVE_CFG_MASTER_PREFERRED:
	case MASTER_SLAVE_CFG_SLAVE_PREFERRED:
	case MASTER_SLAVE_CFG_MASTER_FORCE:
	case MASTER_SLAVE_CFG_SLAVE_FORCE:
		return true;
	}

	return false;
}

static int ethnl_check_linkmodes(struct genl_info *info, struct nlattr **tb)
{
	const struct nlattr *master_slave_cfg, *lanes_cfg;

	master_slave_cfg = tb[ETHTOOL_A_LINKMODES_MASTER_SLAVE_CFG];
	if (master_slave_cfg &&
	    !ethnl_validate_master_slave_cfg(nla_get_u8(master_slave_cfg))) {
		NL_SET_ERR_MSG_ATTR(info->extack, master_slave_cfg,
				    "master/slave value is invalid");
		return -EOPNOTSUPP;
	}

	lanes_cfg = tb[ETHTOOL_A_LINKMODES_LANES];
	if (lanes_cfg && !is_power_of_2(nla_get_u32(lanes_cfg))) {
		NL_SET_ERR_MSG_ATTR(info->extack, lanes_cfg,
				    "lanes value is invalid");
		return -EINVAL;
	}

	return 0;
}

static int ethnl_update_linkmodes(struct genl_info *info, struct nlattr **tb,
				  struct ethtool_link_ksettings *ksettings,
				  bool *mod, const struct net_device *dev)
{
	struct ethtool_link_settings *lsettings = &ksettings->base;
	bool req_speed, req_lanes, req_duplex;
	const struct nlattr *master_slave_cfg, *lanes_cfg;
	int ret;

	master_slave_cfg = tb[ETHTOOL_A_LINKMODES_MASTER_SLAVE_CFG];
	if (master_slave_cfg) {
		if (lsettings->master_slave_cfg == MASTER_SLAVE_CFG_UNSUPPORTED) {
			NL_SET_ERR_MSG_ATTR(info->extack, master_slave_cfg,
					    "master/slave configuration not supported by device");
			return -EOPNOTSUPP;
		}
	}

	*mod = false;
	req_speed = tb[ETHTOOL_A_LINKMODES_SPEED];
	req_lanes = tb[ETHTOOL_A_LINKMODES_LANES];
	req_duplex = tb[ETHTOOL_A_LINKMODES_DUPLEX];

	ethnl_update_u8(&lsettings->autoneg, tb[ETHTOOL_A_LINKMODES_AUTONEG],
			mod);

	lanes_cfg = tb[ETHTOOL_A_LINKMODES_LANES];
	if (lanes_cfg) {
		/* If autoneg is off and lanes parameter is not supported by the
		 * driver, return an error.
		 */
		if (!lsettings->autoneg &&
		    !dev->ethtool_ops->cap_link_lanes_supported) {
			NL_SET_ERR_MSG_ATTR(info->extack, lanes_cfg,
					    "lanes configuration not supported by device");
			return -EOPNOTSUPP;
		}
	} else if (!lsettings->autoneg && ksettings->lanes) {
		/* If autoneg is off and lanes parameter is not passed from user but
		 * it was defined previously then set the lanes parameter to 0.
		 */
		ksettings->lanes = 0;
		*mod = true;
	}

	ret = ethnl_update_bitset(ksettings->link_modes.advertising,
				  __ETHTOOL_LINK_MODE_MASK_NBITS,
				  tb[ETHTOOL_A_LINKMODES_OURS], link_mode_names,
				  info->extack, mod);
	if (ret < 0)
		return ret;
	ethnl_update_u32(&lsettings->speed, tb[ETHTOOL_A_LINKMODES_SPEED],
			 mod);
	ethnl_update_u32(&ksettings->lanes, lanes_cfg, mod);
	ethnl_update_u8(&lsettings->duplex, tb[ETHTOOL_A_LINKMODES_DUPLEX],
			mod);
	ethnl_update_u8(&lsettings->master_slave_cfg, master_slave_cfg, mod);

	if (!tb[ETHTOOL_A_LINKMODES_OURS] && lsettings->autoneg &&
	    (req_speed || req_lanes || req_duplex) &&
	    ethnl_auto_linkmodes(ksettings, req_speed, req_lanes, req_duplex))
		*mod = true;

	return 0;
}

static int
ethnl_set_linkmodes_validate(struct ethnl_req_info *req_info,
			     struct genl_info *info)
{
	const struct ethtool_ops *ops = req_info->dev->ethtool_ops;
	int ret;

	ret = ethnl_check_linkmodes(info, info->attrs);
	if (ret < 0)
		return ret;

	if (!ops->get_link_ksettings || !ops->set_link_ksettings)
		return -EOPNOTSUPP;
	return 1;
}

static int
ethnl_set_linkmodes(struct ethnl_req_info *req_info, struct genl_info *info)
{
	struct ethtool_link_ksettings ksettings = {};
	struct net_device *dev = req_info->dev;
	struct nlattr **tb = info->attrs;
	bool mod = false;
	int ret;

	ret = __ethtool_get_link_ksettings(dev, &ksettings);
	if (ret < 0) {
		GENL_SET_ERR_MSG(info, "failed to retrieve link settings");
		return ret;
	}

	ret = ethnl_update_linkmodes(info, tb, &ksettings, &mod, dev);
	if (ret < 0)
		return ret;
	if (!mod)
		return 0;

	ret = dev->ethtool_ops->set_link_ksettings(dev, &ksettings);
	if (ret < 0) {
		GENL_SET_ERR_MSG(info, "link settings update failed");
		return ret;
	}

	return 1;
}

const struct ethnl_request_ops ethnl_linkmodes_request_ops = {
	.request_cmd		= ETHTOOL_MSG_LINKMODES_GET,
	.reply_cmd		= ETHTOOL_MSG_LINKMODES_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_LINKMODES_HEADER,
	.req_info_size		= sizeof(struct linkmodes_req_info),
	.reply_data_size	= sizeof(struct linkmodes_reply_data),

	.prepare_data		= linkmodes_prepare_data,
	.reply_size		= linkmodes_reply_size,
	.fill_reply		= linkmodes_fill_reply,

	.set_validate		= ethnl_set_linkmodes_validate,
	.set			= ethnl_set_linkmodes,
	.set_ntf_cmd		= ETHTOOL_MSG_LINKMODES_NTF,
};
