// SPDX-License-Identifier: GPL-2.0-only

#include <linux/dim.h>
#include "netlink.h"
#include "common.h"

struct coalesce_req_info {
	struct ethnl_req_info		base;
};

struct coalesce_reply_data {
	struct ethnl_reply_data		base;
	struct ethtool_coalesce		coalesce;
	struct kernel_ethtool_coalesce	kernel_coalesce;
	u32				supported_params;
};

#define COALESCE_REPDATA(__reply_base) \
	container_of(__reply_base, struct coalesce_reply_data, base)

#define __SUPPORTED_OFFSET ETHTOOL_A_COALESCE_RX_USECS
static u32 attr_to_mask(unsigned int attr_type)
{
	return BIT(attr_type - __SUPPORTED_OFFSET);
}

/* build time check that indices in ethtool_ops::supported_coalesce_params
 * match corresponding attribute types with an offset
 */
#define __CHECK_SUPPORTED_OFFSET(x) \
	static_assert((ETHTOOL_ ## x) == \
		      BIT((ETHTOOL_A_ ## x) - __SUPPORTED_OFFSET))
__CHECK_SUPPORTED_OFFSET(COALESCE_RX_USECS);
__CHECK_SUPPORTED_OFFSET(COALESCE_RX_MAX_FRAMES);
__CHECK_SUPPORTED_OFFSET(COALESCE_RX_USECS_IRQ);
__CHECK_SUPPORTED_OFFSET(COALESCE_RX_MAX_FRAMES_IRQ);
__CHECK_SUPPORTED_OFFSET(COALESCE_TX_USECS);
__CHECK_SUPPORTED_OFFSET(COALESCE_TX_MAX_FRAMES);
__CHECK_SUPPORTED_OFFSET(COALESCE_TX_USECS_IRQ);
__CHECK_SUPPORTED_OFFSET(COALESCE_TX_MAX_FRAMES_IRQ);
__CHECK_SUPPORTED_OFFSET(COALESCE_STATS_BLOCK_USECS);
__CHECK_SUPPORTED_OFFSET(COALESCE_USE_ADAPTIVE_RX);
__CHECK_SUPPORTED_OFFSET(COALESCE_USE_ADAPTIVE_TX);
__CHECK_SUPPORTED_OFFSET(COALESCE_PKT_RATE_LOW);
__CHECK_SUPPORTED_OFFSET(COALESCE_RX_USECS_LOW);
__CHECK_SUPPORTED_OFFSET(COALESCE_RX_MAX_FRAMES_LOW);
__CHECK_SUPPORTED_OFFSET(COALESCE_TX_USECS_LOW);
__CHECK_SUPPORTED_OFFSET(COALESCE_TX_MAX_FRAMES_LOW);
__CHECK_SUPPORTED_OFFSET(COALESCE_PKT_RATE_HIGH);
__CHECK_SUPPORTED_OFFSET(COALESCE_RX_USECS_HIGH);
__CHECK_SUPPORTED_OFFSET(COALESCE_RX_MAX_FRAMES_HIGH);
__CHECK_SUPPORTED_OFFSET(COALESCE_TX_USECS_HIGH);
__CHECK_SUPPORTED_OFFSET(COALESCE_TX_MAX_FRAMES_HIGH);
__CHECK_SUPPORTED_OFFSET(COALESCE_RATE_SAMPLE_INTERVAL);

const struct nla_policy ethnl_coalesce_get_policy[] = {
	[ETHTOOL_A_COALESCE_HEADER]		=
		NLA_POLICY_NESTED(ethnl_header_policy),
};

static int coalesce_prepare_data(const struct ethnl_req_info *req_base,
				 struct ethnl_reply_data *reply_base,
				 const struct genl_info *info)
{
	struct coalesce_reply_data *data = COALESCE_REPDATA(reply_base);
	struct net_device *dev = reply_base->dev;
	int ret;

	if (!dev->ethtool_ops->get_coalesce)
		return -EOPNOTSUPP;
	data->supported_params = dev->ethtool_ops->supported_coalesce_params;
	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;
	ret = dev->ethtool_ops->get_coalesce(dev, &data->coalesce,
					     &data->kernel_coalesce,
					     info->extack);
	ethnl_ops_complete(dev);

	return ret;
}

static int coalesce_reply_size(const struct ethnl_req_info *req_base,
			       const struct ethnl_reply_data *reply_base)
{
	int modersz = nla_total_size(0) + /* _PROFILE_IRQ_MODERATION, nest */
		      nla_total_size(sizeof(u32)) + /* _IRQ_MODERATION_USEC */
		      nla_total_size(sizeof(u32)) + /* _IRQ_MODERATION_PKTS */
		      nla_total_size(sizeof(u32));  /* _IRQ_MODERATION_COMPS */

	int total_modersz = nla_total_size(0) +  /* _{R,T}X_PROFILE, nest */
			modersz * NET_DIM_PARAMS_NUM_PROFILES;

	return nla_total_size(sizeof(u32)) +	/* _RX_USECS */
	       nla_total_size(sizeof(u32)) +	/* _RX_MAX_FRAMES */
	       nla_total_size(sizeof(u32)) +	/* _RX_USECS_IRQ */
	       nla_total_size(sizeof(u32)) +	/* _RX_MAX_FRAMES_IRQ */
	       nla_total_size(sizeof(u32)) +	/* _TX_USECS */
	       nla_total_size(sizeof(u32)) +	/* _TX_MAX_FRAMES */
	       nla_total_size(sizeof(u32)) +	/* _TX_USECS_IRQ */
	       nla_total_size(sizeof(u32)) +	/* _TX_MAX_FRAMES_IRQ */
	       nla_total_size(sizeof(u32)) +	/* _STATS_BLOCK_USECS */
	       nla_total_size(sizeof(u8)) +	/* _USE_ADAPTIVE_RX */
	       nla_total_size(sizeof(u8)) +	/* _USE_ADAPTIVE_TX */
	       nla_total_size(sizeof(u32)) +	/* _PKT_RATE_LOW */
	       nla_total_size(sizeof(u32)) +	/* _RX_USECS_LOW */
	       nla_total_size(sizeof(u32)) +	/* _RX_MAX_FRAMES_LOW */
	       nla_total_size(sizeof(u32)) +	/* _TX_USECS_LOW */
	       nla_total_size(sizeof(u32)) +	/* _TX_MAX_FRAMES_LOW */
	       nla_total_size(sizeof(u32)) +	/* _PKT_RATE_HIGH */
	       nla_total_size(sizeof(u32)) +	/* _RX_USECS_HIGH */
	       nla_total_size(sizeof(u32)) +	/* _RX_MAX_FRAMES_HIGH */
	       nla_total_size(sizeof(u32)) +	/* _TX_USECS_HIGH */
	       nla_total_size(sizeof(u32)) +	/* _TX_MAX_FRAMES_HIGH */
	       nla_total_size(sizeof(u32)) +	/* _RATE_SAMPLE_INTERVAL */
	       nla_total_size(sizeof(u8)) +	/* _USE_CQE_MODE_TX */
	       nla_total_size(sizeof(u8)) +	/* _USE_CQE_MODE_RX */
	       nla_total_size(sizeof(u32)) +	/* _TX_AGGR_MAX_BYTES */
	       nla_total_size(sizeof(u32)) +	/* _TX_AGGR_MAX_FRAMES */
	       nla_total_size(sizeof(u32)) +	/* _TX_AGGR_TIME_USECS */
	       total_modersz * 2;		/* _{R,T}X_PROFILE */
}

static bool coalesce_put_u32(struct sk_buff *skb, u16 attr_type, u32 val,
			     u32 supported_params)
{
	if (!val && !(supported_params & attr_to_mask(attr_type)))
		return false;
	return nla_put_u32(skb, attr_type, val);
}

static bool coalesce_put_bool(struct sk_buff *skb, u16 attr_type, u32 val,
			      u32 supported_params)
{
	if (!val && !(supported_params & attr_to_mask(attr_type)))
		return false;
	return nla_put_u8(skb, attr_type, !!val);
}

/**
 * coalesce_put_profile - fill reply with a nla nest with four child nla nests.
 * @skb: socket buffer the message is stored in
 * @attr_type: nest attr type ETHTOOL_A_COALESCE_*X_PROFILE
 * @profile: data passed to userspace
 * @coal_flags: modifiable parameters supported by the driver
 *
 * Put a dim profile nest attribute. Refer to ETHTOOL_A_PROFILE_IRQ_MODERATION.
 *
 * Return: 0 on success or a negative error code.
 */
static int coalesce_put_profile(struct sk_buff *skb, u16 attr_type,
				const struct dim_cq_moder *profile,
				u8 coal_flags)
{
	struct nlattr *profile_attr, *moder_attr;
	int i, ret;

	if (!profile || !coal_flags)
		return 0;

	profile_attr = nla_nest_start(skb, attr_type);
	if (!profile_attr)
		return -EMSGSIZE;

	for (i = 0; i < NET_DIM_PARAMS_NUM_PROFILES; i++) {
		moder_attr = nla_nest_start(skb,
					    ETHTOOL_A_PROFILE_IRQ_MODERATION);
		if (!moder_attr) {
			ret = -EMSGSIZE;
			goto cancel_profile;
		}

		if (coal_flags & DIM_COALESCE_USEC) {
			ret = nla_put_u32(skb, ETHTOOL_A_IRQ_MODERATION_USEC,
					  profile[i].usec);
			if (ret)
				goto cancel_moder;
		}

		if (coal_flags & DIM_COALESCE_PKTS) {
			ret = nla_put_u32(skb, ETHTOOL_A_IRQ_MODERATION_PKTS,
					  profile[i].pkts);
			if (ret)
				goto cancel_moder;
		}

		if (coal_flags & DIM_COALESCE_COMPS) {
			ret = nla_put_u32(skb, ETHTOOL_A_IRQ_MODERATION_COMPS,
					  profile[i].comps);
			if (ret)
				goto cancel_moder;
		}

		nla_nest_end(skb, moder_attr);
	}

	nla_nest_end(skb, profile_attr);

	return 0;

cancel_moder:
	nla_nest_cancel(skb, moder_attr);
cancel_profile:
	nla_nest_cancel(skb, profile_attr);
	return ret;
}

static int coalesce_fill_reply(struct sk_buff *skb,
			       const struct ethnl_req_info *req_base,
			       const struct ethnl_reply_data *reply_base)
{
	const struct coalesce_reply_data *data = COALESCE_REPDATA(reply_base);
	const struct kernel_ethtool_coalesce *kcoal = &data->kernel_coalesce;
	const struct ethtool_coalesce *coal = &data->coalesce;
	u32 supported = data->supported_params;
	struct dim_irq_moder *moder;
	int ret = 0;

	if (coalesce_put_u32(skb, ETHTOOL_A_COALESCE_RX_USECS,
			     coal->rx_coalesce_usecs, supported) ||
	    coalesce_put_u32(skb, ETHTOOL_A_COALESCE_RX_MAX_FRAMES,
			     coal->rx_max_coalesced_frames, supported) ||
	    coalesce_put_u32(skb, ETHTOOL_A_COALESCE_RX_USECS_IRQ,
			     coal->rx_coalesce_usecs_irq, supported) ||
	    coalesce_put_u32(skb, ETHTOOL_A_COALESCE_RX_MAX_FRAMES_IRQ,
			     coal->rx_max_coalesced_frames_irq, supported) ||
	    coalesce_put_u32(skb, ETHTOOL_A_COALESCE_TX_USECS,
			     coal->tx_coalesce_usecs, supported) ||
	    coalesce_put_u32(skb, ETHTOOL_A_COALESCE_TX_MAX_FRAMES,
			     coal->tx_max_coalesced_frames, supported) ||
	    coalesce_put_u32(skb, ETHTOOL_A_COALESCE_TX_USECS_IRQ,
			     coal->tx_coalesce_usecs_irq, supported) ||
	    coalesce_put_u32(skb, ETHTOOL_A_COALESCE_TX_MAX_FRAMES_IRQ,
			     coal->tx_max_coalesced_frames_irq, supported) ||
	    coalesce_put_u32(skb, ETHTOOL_A_COALESCE_STATS_BLOCK_USECS,
			     coal->stats_block_coalesce_usecs, supported) ||
	    coalesce_put_bool(skb, ETHTOOL_A_COALESCE_USE_ADAPTIVE_RX,
			      coal->use_adaptive_rx_coalesce, supported) ||
	    coalesce_put_bool(skb, ETHTOOL_A_COALESCE_USE_ADAPTIVE_TX,
			      coal->use_adaptive_tx_coalesce, supported) ||
	    coalesce_put_u32(skb, ETHTOOL_A_COALESCE_PKT_RATE_LOW,
			     coal->pkt_rate_low, supported) ||
	    coalesce_put_u32(skb, ETHTOOL_A_COALESCE_RX_USECS_LOW,
			     coal->rx_coalesce_usecs_low, supported) ||
	    coalesce_put_u32(skb, ETHTOOL_A_COALESCE_RX_MAX_FRAMES_LOW,
			     coal->rx_max_coalesced_frames_low, supported) ||
	    coalesce_put_u32(skb, ETHTOOL_A_COALESCE_TX_USECS_LOW,
			     coal->tx_coalesce_usecs_low, supported) ||
	    coalesce_put_u32(skb, ETHTOOL_A_COALESCE_TX_MAX_FRAMES_LOW,
			     coal->tx_max_coalesced_frames_low, supported) ||
	    coalesce_put_u32(skb, ETHTOOL_A_COALESCE_PKT_RATE_HIGH,
			     coal->pkt_rate_high, supported) ||
	    coalesce_put_u32(skb, ETHTOOL_A_COALESCE_RX_USECS_HIGH,
			     coal->rx_coalesce_usecs_high, supported) ||
	    coalesce_put_u32(skb, ETHTOOL_A_COALESCE_RX_MAX_FRAMES_HIGH,
			     coal->rx_max_coalesced_frames_high, supported) ||
	    coalesce_put_u32(skb, ETHTOOL_A_COALESCE_TX_USECS_HIGH,
			     coal->tx_coalesce_usecs_high, supported) ||
	    coalesce_put_u32(skb, ETHTOOL_A_COALESCE_TX_MAX_FRAMES_HIGH,
			     coal->tx_max_coalesced_frames_high, supported) ||
	    coalesce_put_u32(skb, ETHTOOL_A_COALESCE_RATE_SAMPLE_INTERVAL,
			     coal->rate_sample_interval, supported) ||
	    coalesce_put_bool(skb, ETHTOOL_A_COALESCE_USE_CQE_MODE_TX,
			      kcoal->use_cqe_mode_tx, supported) ||
	    coalesce_put_bool(skb, ETHTOOL_A_COALESCE_USE_CQE_MODE_RX,
			      kcoal->use_cqe_mode_rx, supported) ||
	    coalesce_put_u32(skb, ETHTOOL_A_COALESCE_TX_AGGR_MAX_BYTES,
			     kcoal->tx_aggr_max_bytes, supported) ||
	    coalesce_put_u32(skb, ETHTOOL_A_COALESCE_TX_AGGR_MAX_FRAMES,
			     kcoal->tx_aggr_max_frames, supported) ||
	    coalesce_put_u32(skb, ETHTOOL_A_COALESCE_TX_AGGR_TIME_USECS,
			     kcoal->tx_aggr_time_usecs, supported))
		return -EMSGSIZE;

	if (!req_base->dev || !req_base->dev->irq_moder)
		return 0;

	moder = req_base->dev->irq_moder;
	rcu_read_lock();
	if (moder->profile_flags & DIM_PROFILE_RX) {
		ret = coalesce_put_profile(skb, ETHTOOL_A_COALESCE_RX_PROFILE,
					   rcu_dereference(moder->rx_profile),
					   moder->coal_flags);
		if (ret)
			goto out;
	}

	if (moder->profile_flags & DIM_PROFILE_TX)
		ret = coalesce_put_profile(skb, ETHTOOL_A_COALESCE_TX_PROFILE,
					   rcu_dereference(moder->tx_profile),
					   moder->coal_flags);

out:
	rcu_read_unlock();
	return ret;
}

/* COALESCE_SET */

static const struct nla_policy coalesce_irq_moderation_policy[] = {
	[ETHTOOL_A_IRQ_MODERATION_USEC]	= { .type = NLA_U32 },
	[ETHTOOL_A_IRQ_MODERATION_PKTS]	= { .type = NLA_U32 },
	[ETHTOOL_A_IRQ_MODERATION_COMPS] = { .type = NLA_U32 },
};

static const struct nla_policy coalesce_profile_policy[] = {
	[ETHTOOL_A_PROFILE_IRQ_MODERATION] =
		NLA_POLICY_NESTED(coalesce_irq_moderation_policy),
};

const struct nla_policy ethnl_coalesce_set_policy[] = {
	[ETHTOOL_A_COALESCE_HEADER]		=
		NLA_POLICY_NESTED(ethnl_header_policy),
	[ETHTOOL_A_COALESCE_RX_USECS]		= { .type = NLA_U32 },
	[ETHTOOL_A_COALESCE_RX_MAX_FRAMES]	= { .type = NLA_U32 },
	[ETHTOOL_A_COALESCE_RX_USECS_IRQ]	= { .type = NLA_U32 },
	[ETHTOOL_A_COALESCE_RX_MAX_FRAMES_IRQ]	= { .type = NLA_U32 },
	[ETHTOOL_A_COALESCE_TX_USECS]		= { .type = NLA_U32 },
	[ETHTOOL_A_COALESCE_TX_MAX_FRAMES]	= { .type = NLA_U32 },
	[ETHTOOL_A_COALESCE_TX_USECS_IRQ]	= { .type = NLA_U32 },
	[ETHTOOL_A_COALESCE_TX_MAX_FRAMES_IRQ]	= { .type = NLA_U32 },
	[ETHTOOL_A_COALESCE_STATS_BLOCK_USECS]	= { .type = NLA_U32 },
	[ETHTOOL_A_COALESCE_USE_ADAPTIVE_RX]	= { .type = NLA_U8 },
	[ETHTOOL_A_COALESCE_USE_ADAPTIVE_TX]	= { .type = NLA_U8 },
	[ETHTOOL_A_COALESCE_PKT_RATE_LOW]	= { .type = NLA_U32 },
	[ETHTOOL_A_COALESCE_RX_USECS_LOW]	= { .type = NLA_U32 },
	[ETHTOOL_A_COALESCE_RX_MAX_FRAMES_LOW]	= { .type = NLA_U32 },
	[ETHTOOL_A_COALESCE_TX_USECS_LOW]	= { .type = NLA_U32 },
	[ETHTOOL_A_COALESCE_TX_MAX_FRAMES_LOW]	= { .type = NLA_U32 },
	[ETHTOOL_A_COALESCE_PKT_RATE_HIGH]	= { .type = NLA_U32 },
	[ETHTOOL_A_COALESCE_RX_USECS_HIGH]	= { .type = NLA_U32 },
	[ETHTOOL_A_COALESCE_RX_MAX_FRAMES_HIGH]	= { .type = NLA_U32 },
	[ETHTOOL_A_COALESCE_TX_USECS_HIGH]	= { .type = NLA_U32 },
	[ETHTOOL_A_COALESCE_TX_MAX_FRAMES_HIGH]	= { .type = NLA_U32 },
	[ETHTOOL_A_COALESCE_RATE_SAMPLE_INTERVAL] = { .type = NLA_U32 },
	[ETHTOOL_A_COALESCE_USE_CQE_MODE_TX]	= NLA_POLICY_MAX(NLA_U8, 1),
	[ETHTOOL_A_COALESCE_USE_CQE_MODE_RX]	= NLA_POLICY_MAX(NLA_U8, 1),
	[ETHTOOL_A_COALESCE_TX_AGGR_MAX_BYTES] = { .type = NLA_U32 },
	[ETHTOOL_A_COALESCE_TX_AGGR_MAX_FRAMES] = { .type = NLA_U32 },
	[ETHTOOL_A_COALESCE_TX_AGGR_TIME_USECS] = { .type = NLA_U32 },
	[ETHTOOL_A_COALESCE_RX_PROFILE] =
		NLA_POLICY_NESTED(coalesce_profile_policy),
	[ETHTOOL_A_COALESCE_TX_PROFILE] =
		NLA_POLICY_NESTED(coalesce_profile_policy),
};

static int
ethnl_set_coalesce_validate(struct ethnl_req_info *req_info,
			    struct genl_info *info)
{
	const struct ethtool_ops *ops = req_info->dev->ethtool_ops;
	struct dim_irq_moder *irq_moder = req_info->dev->irq_moder;
	struct nlattr **tb = info->attrs;
	u32 supported_params;
	u16 a;

	if (!ops->get_coalesce || !ops->set_coalesce)
		return -EOPNOTSUPP;

	/* make sure that only supported parameters are present */
	supported_params = ops->supported_coalesce_params;
	if (irq_moder && irq_moder->profile_flags & DIM_PROFILE_RX)
		supported_params |= ETHTOOL_COALESCE_RX_PROFILE;

	if (irq_moder && irq_moder->profile_flags & DIM_PROFILE_TX)
		supported_params |= ETHTOOL_COALESCE_TX_PROFILE;

	for (a = ETHTOOL_A_COALESCE_RX_USECS; a < __ETHTOOL_A_COALESCE_CNT; a++)
		if (tb[a] && !(supported_params & attr_to_mask(a))) {
			NL_SET_ERR_MSG_ATTR(info->extack, tb[a],
					    "cannot modify an unsupported parameter");
			return -EINVAL;
		}

	return 1;
}

/**
 * ethnl_update_irq_moder - update a specific field in the given profile
 * @irq_moder: place that collects dim related information
 * @irq_field: field in profile to modify
 * @attr_type: attr type ETHTOOL_A_IRQ_MODERATION_*
 * @tb: netlink attribute with new values or null
 * @coal_bit: DIM_COALESCE_* bit from coal_flags
 * @mod: pointer to bool for modification tracking
 * @extack: netlink extended ack
 *
 * Return: 0 on success or a negative error code.
 */
static int ethnl_update_irq_moder(struct dim_irq_moder *irq_moder,
				  u16 *irq_field, u16 attr_type,
				  struct nlattr **tb,
				  u8 coal_bit, bool *mod,
				  struct netlink_ext_ack *extack)
{
	int ret = 0;
	u32 val;

	if (!tb[attr_type])
		return 0;

	if (irq_moder->coal_flags & coal_bit) {
		val = nla_get_u32(tb[attr_type]);
		if (*irq_field == val)
			return 0;

		*irq_field = val;
		*mod = true;
	} else {
		NL_SET_BAD_ATTR(extack, tb[attr_type]);
		ret = -EOPNOTSUPP;
	}

	return ret;
}

/**
 * ethnl_update_profile - get a profile nest with child nests from userspace.
 * @dev: netdevice to update the profile
 * @dst: profile get from the driver and modified by ethnl_update_profile.
 * @nests: nest attr ETHTOOL_A_COALESCE_*X_PROFILE to set profile.
 * @mod: pointer to bool for modification tracking
 * @extack: Netlink extended ack
 *
 * Layout of nests:
 *   Nested ETHTOOL_A_COALESCE_*X_PROFILE attr
 *     Nested ETHTOOL_A_PROFILE_IRQ_MODERATION attr
 *       ETHTOOL_A_IRQ_MODERATION_USEC attr
 *       ETHTOOL_A_IRQ_MODERATION_PKTS attr
 *       ETHTOOL_A_IRQ_MODERATION_COMPS attr
 *     ...
 *     Nested ETHTOOL_A_PROFILE_IRQ_MODERATION attr
 *       ETHTOOL_A_IRQ_MODERATION_USEC attr
 *       ETHTOOL_A_IRQ_MODERATION_PKTS attr
 *       ETHTOOL_A_IRQ_MODERATION_COMPS attr
 *
 * Return: 0 on success or a negative error code.
 */
static int ethnl_update_profile(struct net_device *dev,
				struct dim_cq_moder __rcu **dst,
				const struct nlattr *nests,
				bool *mod,
				struct netlink_ext_ack *extack)
{
	int len_irq_moder = ARRAY_SIZE(coalesce_irq_moderation_policy);
	struct nlattr *tb[ARRAY_SIZE(coalesce_irq_moderation_policy)];
	struct dim_irq_moder *irq_moder = dev->irq_moder;
	struct dim_cq_moder *new_profile, *old_profile;
	int ret, rem, i = 0, len;
	struct nlattr *nest;

	if (!nests)
		return 0;

	if (!*dst)
		return -EOPNOTSUPP;

	old_profile = rtnl_dereference(*dst);
	len = NET_DIM_PARAMS_NUM_PROFILES * sizeof(*old_profile);
	new_profile = kmemdup(old_profile, len, GFP_KERNEL);
	if (!new_profile)
		return -ENOMEM;

	nla_for_each_nested_type(nest, ETHTOOL_A_PROFILE_IRQ_MODERATION,
				 nests, rem) {
		ret = nla_parse_nested(tb, len_irq_moder - 1, nest,
				       coalesce_irq_moderation_policy,
				       extack);
		if (ret)
			goto err_out;

		ret = ethnl_update_irq_moder(irq_moder, &new_profile[i].usec,
					     ETHTOOL_A_IRQ_MODERATION_USEC,
					     tb, DIM_COALESCE_USEC,
					     mod, extack);
		if (ret)
			goto err_out;

		ret = ethnl_update_irq_moder(irq_moder, &new_profile[i].pkts,
					     ETHTOOL_A_IRQ_MODERATION_PKTS,
					     tb, DIM_COALESCE_PKTS,
					     mod, extack);
		if (ret)
			goto err_out;

		ret = ethnl_update_irq_moder(irq_moder, &new_profile[i].comps,
					     ETHTOOL_A_IRQ_MODERATION_COMPS,
					     tb, DIM_COALESCE_COMPS,
					     mod, extack);
		if (ret)
			goto err_out;

		i++;
	}

	/* After the profile is modified, dim itself is a dynamic
	 * mechanism and will quickly fit to the appropriate
	 * coalescing parameters according to the new profile.
	 */
	rcu_assign_pointer(*dst, new_profile);
	kfree_rcu(old_profile, rcu);

	return 0;

err_out:
	kfree(new_profile);
	return ret;
}

static int
__ethnl_set_coalesce(struct ethnl_req_info *req_info, struct genl_info *info,
		     bool *dual_change)
{
	struct kernel_ethtool_coalesce kernel_coalesce = {};
	struct net_device *dev = req_info->dev;
	struct ethtool_coalesce coalesce = {};
	bool mod_mode = false, mod = false;
	struct nlattr **tb = info->attrs;
	int ret;

	ret = dev->ethtool_ops->get_coalesce(dev, &coalesce, &kernel_coalesce,
					     info->extack);
	if (ret < 0)
		return ret;

	/* Update values */
	ethnl_update_u32(&coalesce.rx_coalesce_usecs,
			 tb[ETHTOOL_A_COALESCE_RX_USECS], &mod);
	ethnl_update_u32(&coalesce.rx_max_coalesced_frames,
			 tb[ETHTOOL_A_COALESCE_RX_MAX_FRAMES], &mod);
	ethnl_update_u32(&coalesce.rx_coalesce_usecs_irq,
			 tb[ETHTOOL_A_COALESCE_RX_USECS_IRQ], &mod);
	ethnl_update_u32(&coalesce.rx_max_coalesced_frames_irq,
			 tb[ETHTOOL_A_COALESCE_RX_MAX_FRAMES_IRQ], &mod);
	ethnl_update_u32(&coalesce.tx_coalesce_usecs,
			 tb[ETHTOOL_A_COALESCE_TX_USECS], &mod);
	ethnl_update_u32(&coalesce.tx_max_coalesced_frames,
			 tb[ETHTOOL_A_COALESCE_TX_MAX_FRAMES], &mod);
	ethnl_update_u32(&coalesce.tx_coalesce_usecs_irq,
			 tb[ETHTOOL_A_COALESCE_TX_USECS_IRQ], &mod);
	ethnl_update_u32(&coalesce.tx_max_coalesced_frames_irq,
			 tb[ETHTOOL_A_COALESCE_TX_MAX_FRAMES_IRQ], &mod);
	ethnl_update_u32(&coalesce.stats_block_coalesce_usecs,
			 tb[ETHTOOL_A_COALESCE_STATS_BLOCK_USECS], &mod);
	ethnl_update_u32(&coalesce.pkt_rate_low,
			 tb[ETHTOOL_A_COALESCE_PKT_RATE_LOW], &mod);
	ethnl_update_u32(&coalesce.rx_coalesce_usecs_low,
			 tb[ETHTOOL_A_COALESCE_RX_USECS_LOW], &mod);
	ethnl_update_u32(&coalesce.rx_max_coalesced_frames_low,
			 tb[ETHTOOL_A_COALESCE_RX_MAX_FRAMES_LOW], &mod);
	ethnl_update_u32(&coalesce.tx_coalesce_usecs_low,
			 tb[ETHTOOL_A_COALESCE_TX_USECS_LOW], &mod);
	ethnl_update_u32(&coalesce.tx_max_coalesced_frames_low,
			 tb[ETHTOOL_A_COALESCE_TX_MAX_FRAMES_LOW], &mod);
	ethnl_update_u32(&coalesce.pkt_rate_high,
			 tb[ETHTOOL_A_COALESCE_PKT_RATE_HIGH], &mod);
	ethnl_update_u32(&coalesce.rx_coalesce_usecs_high,
			 tb[ETHTOOL_A_COALESCE_RX_USECS_HIGH], &mod);
	ethnl_update_u32(&coalesce.rx_max_coalesced_frames_high,
			 tb[ETHTOOL_A_COALESCE_RX_MAX_FRAMES_HIGH], &mod);
	ethnl_update_u32(&coalesce.tx_coalesce_usecs_high,
			 tb[ETHTOOL_A_COALESCE_TX_USECS_HIGH], &mod);
	ethnl_update_u32(&coalesce.tx_max_coalesced_frames_high,
			 tb[ETHTOOL_A_COALESCE_TX_MAX_FRAMES_HIGH], &mod);
	ethnl_update_u32(&coalesce.rate_sample_interval,
			 tb[ETHTOOL_A_COALESCE_RATE_SAMPLE_INTERVAL], &mod);
	ethnl_update_u32(&kernel_coalesce.tx_aggr_max_bytes,
			 tb[ETHTOOL_A_COALESCE_TX_AGGR_MAX_BYTES], &mod);
	ethnl_update_u32(&kernel_coalesce.tx_aggr_max_frames,
			 tb[ETHTOOL_A_COALESCE_TX_AGGR_MAX_FRAMES], &mod);
	ethnl_update_u32(&kernel_coalesce.tx_aggr_time_usecs,
			 tb[ETHTOOL_A_COALESCE_TX_AGGR_TIME_USECS], &mod);

	if (dev->irq_moder && dev->irq_moder->profile_flags & DIM_PROFILE_RX) {
		ret = ethnl_update_profile(dev, &dev->irq_moder->rx_profile,
					   tb[ETHTOOL_A_COALESCE_RX_PROFILE],
					   &mod, info->extack);
		if (ret < 0)
			return ret;
	}

	if (dev->irq_moder && dev->irq_moder->profile_flags & DIM_PROFILE_TX) {
		ret = ethnl_update_profile(dev, &dev->irq_moder->tx_profile,
					   tb[ETHTOOL_A_COALESCE_TX_PROFILE],
					   &mod, info->extack);
		if (ret < 0)
			return ret;
	}

	/* Update operation modes */
	ethnl_update_bool32(&coalesce.use_adaptive_rx_coalesce,
			    tb[ETHTOOL_A_COALESCE_USE_ADAPTIVE_RX], &mod_mode);
	ethnl_update_bool32(&coalesce.use_adaptive_tx_coalesce,
			    tb[ETHTOOL_A_COALESCE_USE_ADAPTIVE_TX], &mod_mode);
	ethnl_update_u8(&kernel_coalesce.use_cqe_mode_tx,
			tb[ETHTOOL_A_COALESCE_USE_CQE_MODE_TX], &mod_mode);
	ethnl_update_u8(&kernel_coalesce.use_cqe_mode_rx,
			tb[ETHTOOL_A_COALESCE_USE_CQE_MODE_RX], &mod_mode);

	*dual_change = mod && mod_mode;
	if (!mod && !mod_mode)
		return 0;

	ret = dev->ethtool_ops->set_coalesce(dev, &coalesce, &kernel_coalesce,
					     info->extack);
	return ret < 0 ? ret : 1;
}

static int
ethnl_set_coalesce(struct ethnl_req_info *req_info, struct genl_info *info)
{
	bool dual_change;
	int err, ret;

	/* SET_COALESCE may change operation mode and parameters in one call.
	 * Changing operation mode may cause the driver to reset the parameter
	 * values, and therefore ignore user input (driver does not know which
	 * parameters come from user and which are echoed back from ->get).
	 * To not complicate the drivers if user tries to change both the mode
	 * and parameters at once - call the driver twice.
	 */
	err = __ethnl_set_coalesce(req_info, info, &dual_change);
	if (err < 0)
		return err;
	ret = err;

	if (ret && dual_change) {
		err = __ethnl_set_coalesce(req_info, info, &dual_change);
		if (err < 0)
			return err;
	}
	return ret;
}

const struct ethnl_request_ops ethnl_coalesce_request_ops = {
	.request_cmd		= ETHTOOL_MSG_COALESCE_GET,
	.reply_cmd		= ETHTOOL_MSG_COALESCE_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_COALESCE_HEADER,
	.req_info_size		= sizeof(struct coalesce_req_info),
	.reply_data_size	= sizeof(struct coalesce_reply_data),

	.prepare_data		= coalesce_prepare_data,
	.reply_size		= coalesce_reply_size,
	.fill_reply		= coalesce_fill_reply,

	.set_validate		= ethnl_set_coalesce_validate,
	.set			= ethnl_set_coalesce,
	.set_ntf_cmd		= ETHTOOL_MSG_COALESCE_NTF,
};
