// SPDX-License-Identifier: GPL-2.0-only

#include "netlink.h"
#include "common.h"
#include "bitset.h"

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
				  struct genl_info *info)
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
		+ nla_total_size(sizeof(u8)) /* LINKMODES_DUPLEX */
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

	if (lsettings->master_slave_cfg != MASTER_SLAVE_CFG_UNSUPPORTED &&
	    nla_put_u8(skb, ETHTOOL_A_LINKMODES_MASTER_SLAVE_CFG,
		       lsettings->master_slave_cfg))
		return -EMSGSIZE;

	if (lsettings->master_slave_state != MASTER_SLAVE_STATE_UNSUPPORTED &&
	    nla_put_u8(skb, ETHTOOL_A_LINKMODES_MASTER_SLAVE_STATE,
		       lsettings->master_slave_state))
		return -EMSGSIZE;

	return 0;
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
};

/* LINKMODES_SET */

struct link_mode_info {
	int				speed;
	u8				duplex;
};

#define __DEFINE_LINK_MODE_PARAMS(_speed, _type, _duplex) \
	[ETHTOOL_LINK_MODE(_speed, _type, _duplex)] = { \
		.speed	= SPEED_ ## _speed, \
		.duplex	= __DUPLEX_ ## _duplex \
	}
#define __DUPLEX_Half DUPLEX_HALF
#define __DUPLEX_Full DUPLEX_FULL
#define __DEFINE_SPECIAL_MODE_PARAMS(_mode) \
	[ETHTOOL_LINK_MODE_ ## _mode ## _BIT] = { \
		.speed	= SPEED_UNKNOWN, \
		.duplex	= DUPLEX_UNKNOWN, \
	}

static const struct link_mode_info link_mode_params[] = {
	__DEFINE_LINK_MODE_PARAMS(10, T, Half),
	__DEFINE_LINK_MODE_PARAMS(10, T, Full),
	__DEFINE_LINK_MODE_PARAMS(100, T, Half),
	__DEFINE_LINK_MODE_PARAMS(100, T, Full),
	__DEFINE_LINK_MODE_PARAMS(1000, T, Half),
	__DEFINE_LINK_MODE_PARAMS(1000, T, Full),
	__DEFINE_SPECIAL_MODE_PARAMS(Autoneg),
	__DEFINE_SPECIAL_MODE_PARAMS(TP),
	__DEFINE_SPECIAL_MODE_PARAMS(AUI),
	__DEFINE_SPECIAL_MODE_PARAMS(MII),
	__DEFINE_SPECIAL_MODE_PARAMS(FIBRE),
	__DEFINE_SPECIAL_MODE_PARAMS(BNC),
	__DEFINE_LINK_MODE_PARAMS(10000, T, Full),
	__DEFINE_SPECIAL_MODE_PARAMS(Pause),
	__DEFINE_SPECIAL_MODE_PARAMS(Asym_Pause),
	__DEFINE_LINK_MODE_PARAMS(2500, X, Full),
	__DEFINE_SPECIAL_MODE_PARAMS(Backplane),
	__DEFINE_LINK_MODE_PARAMS(1000, KX, Full),
	__DEFINE_LINK_MODE_PARAMS(10000, KX4, Full),
	__DEFINE_LINK_MODE_PARAMS(10000, KR, Full),
	[ETHTOOL_LINK_MODE_10000baseR_FEC_BIT] = {
		.speed	= SPEED_10000,
		.duplex = DUPLEX_FULL,
	},
	__DEFINE_LINK_MODE_PARAMS(20000, MLD2, Full),
	__DEFINE_LINK_MODE_PARAMS(20000, KR2, Full),
	__DEFINE_LINK_MODE_PARAMS(40000, KR4, Full),
	__DEFINE_LINK_MODE_PARAMS(40000, CR4, Full),
	__DEFINE_LINK_MODE_PARAMS(40000, SR4, Full),
	__DEFINE_LINK_MODE_PARAMS(40000, LR4, Full),
	__DEFINE_LINK_MODE_PARAMS(56000, KR4, Full),
	__DEFINE_LINK_MODE_PARAMS(56000, CR4, Full),
	__DEFINE_LINK_MODE_PARAMS(56000, SR4, Full),
	__DEFINE_LINK_MODE_PARAMS(56000, LR4, Full),
	__DEFINE_LINK_MODE_PARAMS(25000, CR, Full),
	__DEFINE_LINK_MODE_PARAMS(25000, KR, Full),
	__DEFINE_LINK_MODE_PARAMS(25000, SR, Full),
	__DEFINE_LINK_MODE_PARAMS(50000, CR2, Full),
	__DEFINE_LINK_MODE_PARAMS(50000, KR2, Full),
	__DEFINE_LINK_MODE_PARAMS(100000, KR4, Full),
	__DEFINE_LINK_MODE_PARAMS(100000, SR4, Full),
	__DEFINE_LINK_MODE_PARAMS(100000, CR4, Full),
	__DEFINE_LINK_MODE_PARAMS(100000, LR4_ER4, Full),
	__DEFINE_LINK_MODE_PARAMS(50000, SR2, Full),
	__DEFINE_LINK_MODE_PARAMS(1000, X, Full),
	__DEFINE_LINK_MODE_PARAMS(10000, CR, Full),
	__DEFINE_LINK_MODE_PARAMS(10000, SR, Full),
	__DEFINE_LINK_MODE_PARAMS(10000, LR, Full),
	__DEFINE_LINK_MODE_PARAMS(10000, LRM, Full),
	__DEFINE_LINK_MODE_PARAMS(10000, ER, Full),
	__DEFINE_LINK_MODE_PARAMS(2500, T, Full),
	__DEFINE_LINK_MODE_PARAMS(5000, T, Full),
	__DEFINE_SPECIAL_MODE_PARAMS(FEC_NONE),
	__DEFINE_SPECIAL_MODE_PARAMS(FEC_RS),
	__DEFINE_SPECIAL_MODE_PARAMS(FEC_BASER),
	__DEFINE_LINK_MODE_PARAMS(50000, KR, Full),
	__DEFINE_LINK_MODE_PARAMS(50000, SR, Full),
	__DEFINE_LINK_MODE_PARAMS(50000, CR, Full),
	__DEFINE_LINK_MODE_PARAMS(50000, LR_ER_FR, Full),
	__DEFINE_LINK_MODE_PARAMS(50000, DR, Full),
	__DEFINE_LINK_MODE_PARAMS(100000, KR2, Full),
	__DEFINE_LINK_MODE_PARAMS(100000, SR2, Full),
	__DEFINE_LINK_MODE_PARAMS(100000, CR2, Full),
	__DEFINE_LINK_MODE_PARAMS(100000, LR2_ER2_FR2, Full),
	__DEFINE_LINK_MODE_PARAMS(100000, DR2, Full),
	__DEFINE_LINK_MODE_PARAMS(200000, KR4, Full),
	__DEFINE_LINK_MODE_PARAMS(200000, SR4, Full),
	__DEFINE_LINK_MODE_PARAMS(200000, LR4_ER4_FR4, Full),
	__DEFINE_LINK_MODE_PARAMS(200000, DR4, Full),
	__DEFINE_LINK_MODE_PARAMS(200000, CR4, Full),
	__DEFINE_LINK_MODE_PARAMS(100, T1, Full),
	__DEFINE_LINK_MODE_PARAMS(1000, T1, Full),
	__DEFINE_LINK_MODE_PARAMS(400000, KR8, Full),
	__DEFINE_LINK_MODE_PARAMS(400000, SR8, Full),
	__DEFINE_LINK_MODE_PARAMS(400000, LR8_ER8_FR8, Full),
	__DEFINE_LINK_MODE_PARAMS(400000, DR8, Full),
	__DEFINE_LINK_MODE_PARAMS(400000, CR8, Full),
	__DEFINE_SPECIAL_MODE_PARAMS(FEC_LLRS),
	__DEFINE_LINK_MODE_PARAMS(100000, KR, Full),
	__DEFINE_LINK_MODE_PARAMS(100000, SR, Full),
	__DEFINE_LINK_MODE_PARAMS(100000, LR_ER_FR, Full),
	__DEFINE_LINK_MODE_PARAMS(100000, DR, Full),
	__DEFINE_LINK_MODE_PARAMS(100000, CR, Full),
	__DEFINE_LINK_MODE_PARAMS(200000, KR2, Full),
	__DEFINE_LINK_MODE_PARAMS(200000, SR2, Full),
	__DEFINE_LINK_MODE_PARAMS(200000, LR2_ER2_FR2, Full),
	__DEFINE_LINK_MODE_PARAMS(200000, DR2, Full),
	__DEFINE_LINK_MODE_PARAMS(200000, CR2, Full),
	__DEFINE_LINK_MODE_PARAMS(400000, KR4, Full),
	__DEFINE_LINK_MODE_PARAMS(400000, SR4, Full),
	__DEFINE_LINK_MODE_PARAMS(400000, LR4_ER4_FR4, Full),
	__DEFINE_LINK_MODE_PARAMS(400000, DR4, Full),
	__DEFINE_LINK_MODE_PARAMS(400000, CR4, Full),
	__DEFINE_LINK_MODE_PARAMS(100, FX, Half),
	__DEFINE_LINK_MODE_PARAMS(100, FX, Full),
};

const struct nla_policy ethnl_linkmodes_set_policy[] = {
	[ETHTOOL_A_LINKMODES_HEADER]		=
		NLA_POLICY_NESTED(ethnl_header_policy),
	[ETHTOOL_A_LINKMODES_AUTONEG]		= { .type = NLA_U8 },
	[ETHTOOL_A_LINKMODES_OURS]		= { .type = NLA_NESTED },
	[ETHTOOL_A_LINKMODES_SPEED]		= { .type = NLA_U32 },
	[ETHTOOL_A_LINKMODES_DUPLEX]		= { .type = NLA_U8 },
	[ETHTOOL_A_LINKMODES_MASTER_SLAVE_CFG]	= { .type = NLA_U8 },
};

/* Set advertised link modes to all supported modes matching requested speed
 * and duplex values. Called when autonegotiation is on, speed or duplex is
 * requested but no link mode change. This is done in userspace with ioctl()
 * interface, move it into kernel for netlink.
 * Returns true if advertised modes bitmap was modified.
 */
static bool ethnl_auto_linkmodes(struct ethtool_link_ksettings *ksettings,
				 bool req_speed, bool req_duplex)
{
	unsigned long *advertising = ksettings->link_modes.advertising;
	unsigned long *supported = ksettings->link_modes.supported;
	DECLARE_BITMAP(old_adv, __ETHTOOL_LINK_MODE_MASK_NBITS);
	unsigned int i;

	BUILD_BUG_ON(ARRAY_SIZE(link_mode_params) !=
		     __ETHTOOL_LINK_MODE_MASK_NBITS);

	bitmap_copy(old_adv, advertising, __ETHTOOL_LINK_MODE_MASK_NBITS);

	for (i = 0; i < __ETHTOOL_LINK_MODE_MASK_NBITS; i++) {
		const struct link_mode_info *info = &link_mode_params[i];

		if (info->speed == SPEED_UNKNOWN)
			continue;
		if (test_bit(i, supported) &&
		    (!req_speed || info->speed == ksettings->base.speed) &&
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

static int ethnl_update_linkmodes(struct genl_info *info, struct nlattr **tb,
				  struct ethtool_link_ksettings *ksettings,
				  bool *mod)
{
	struct ethtool_link_settings *lsettings = &ksettings->base;
	bool req_speed, req_duplex;
	const struct nlattr *master_slave_cfg;
	int ret;

	master_slave_cfg = tb[ETHTOOL_A_LINKMODES_MASTER_SLAVE_CFG];
	if (master_slave_cfg) {
		u8 cfg = nla_get_u8(master_slave_cfg);

		if (lsettings->master_slave_cfg == MASTER_SLAVE_CFG_UNSUPPORTED) {
			NL_SET_ERR_MSG_ATTR(info->extack, master_slave_cfg,
					    "master/slave configuration not supported by device");
			return -EOPNOTSUPP;
		}

		if (!ethnl_validate_master_slave_cfg(cfg)) {
			NL_SET_ERR_MSG_ATTR(info->extack, master_slave_cfg,
					    "master/slave value is invalid");
			return -EOPNOTSUPP;
		}
	}

	*mod = false;
	req_speed = tb[ETHTOOL_A_LINKMODES_SPEED];
	req_duplex = tb[ETHTOOL_A_LINKMODES_DUPLEX];

	ethnl_update_u8(&lsettings->autoneg, tb[ETHTOOL_A_LINKMODES_AUTONEG],
			mod);
	ret = ethnl_update_bitset(ksettings->link_modes.advertising,
				  __ETHTOOL_LINK_MODE_MASK_NBITS,
				  tb[ETHTOOL_A_LINKMODES_OURS], link_mode_names,
				  info->extack, mod);
	if (ret < 0)
		return ret;
	ethnl_update_u32(&lsettings->speed, tb[ETHTOOL_A_LINKMODES_SPEED],
			 mod);
	ethnl_update_u8(&lsettings->duplex, tb[ETHTOOL_A_LINKMODES_DUPLEX],
			mod);
	ethnl_update_u8(&lsettings->master_slave_cfg, master_slave_cfg, mod);

	if (!tb[ETHTOOL_A_LINKMODES_OURS] && lsettings->autoneg &&
	    (req_speed || req_duplex) &&
	    ethnl_auto_linkmodes(ksettings, req_speed, req_duplex))
		*mod = true;

	return 0;
}

int ethnl_set_linkmodes(struct sk_buff *skb, struct genl_info *info)
{
	struct ethtool_link_ksettings ksettings = {};
	struct ethnl_req_info req_info = {};
	struct nlattr **tb = info->attrs;
	struct net_device *dev;
	bool mod = false;
	int ret;

	ret = ethnl_parse_header_dev_get(&req_info,
					 tb[ETHTOOL_A_LINKMODES_HEADER],
					 genl_info_net(info), info->extack,
					 true);
	if (ret < 0)
		return ret;
	dev = req_info.dev;
	ret = -EOPNOTSUPP;
	if (!dev->ethtool_ops->get_link_ksettings ||
	    !dev->ethtool_ops->set_link_ksettings)
		goto out_dev;

	rtnl_lock();
	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		goto out_rtnl;

	ret = __ethtool_get_link_ksettings(dev, &ksettings);
	if (ret < 0) {
		GENL_SET_ERR_MSG(info, "failed to retrieve link settings");
		goto out_ops;
	}

	ret = ethnl_update_linkmodes(info, tb, &ksettings, &mod);
	if (ret < 0)
		goto out_ops;

	if (mod) {
		ret = dev->ethtool_ops->set_link_ksettings(dev, &ksettings);
		if (ret < 0)
			GENL_SET_ERR_MSG(info, "link settings update failed");
		else
			ethtool_notify(dev, ETHTOOL_MSG_LINKMODES_NTF, NULL);
	}

out_ops:
	ethnl_ops_complete(dev);
out_rtnl:
	rtnl_unlock();
out_dev:
	dev_put(dev);
	return ret;
}
