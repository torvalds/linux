// SPDX-License-Identifier: GPL-2.0-only

#include <linux/ethtool.h>
#include <linux/phy.h>
#include <linux/slab.h>

#include "netlink.h"
#include "common.h"

/* Channels A-D only; WORST and LINK are exclusive alternatives */
#define PHY_MSE_CHANNEL_COUNT 4

struct mse_req_info {
	struct ethnl_req_info base;
};

struct mse_snapshot_entry {
	struct phy_mse_snapshot snapshot;
	int channel;
};

struct mse_reply_data {
	struct ethnl_reply_data base;
	struct phy_mse_capability capability;
	struct mse_snapshot_entry *snapshots;
	unsigned int num_snapshots;
};

static struct mse_reply_data *
mse_repdata(const struct ethnl_reply_data *reply_base)
{
	return container_of(reply_base, struct mse_reply_data, base);
}

const struct nla_policy ethnl_mse_get_policy[] = {
	[ETHTOOL_A_MSE_HEADER] = NLA_POLICY_NESTED(ethnl_header_policy_phy),
};

static int get_snapshot_if_supported(struct phy_device *phydev,
				     struct mse_reply_data *data,
				     unsigned int *idx, u32 cap_bit,
				     enum phy_mse_channel channel)
{
	int ret;

	if (data->capability.supported_caps & cap_bit) {
		ret = phydev->drv->get_mse_snapshot(phydev, channel,
					&data->snapshots[*idx].snapshot);
		if (ret)
			return ret;
		data->snapshots[*idx].channel = channel;
		(*idx)++;
	}

	return 0;
}

static int mse_get_channels(struct phy_device *phydev,
			    struct mse_reply_data *data)
{
	unsigned int i = 0;
	int ret;

	if (!data->capability.supported_caps)
		return 0;

	data->snapshots = kcalloc(PHY_MSE_CHANNEL_COUNT,
				  sizeof(*data->snapshots), GFP_KERNEL);
	if (!data->snapshots)
		return -ENOMEM;

	/* Priority 1: Individual channels */
	ret = get_snapshot_if_supported(phydev, data, &i, PHY_MSE_CAP_CHANNEL_A,
					PHY_MSE_CHANNEL_A);
	if (ret)
		return ret;
	ret = get_snapshot_if_supported(phydev, data, &i, PHY_MSE_CAP_CHANNEL_B,
					PHY_MSE_CHANNEL_B);
	if (ret)
		return ret;
	ret = get_snapshot_if_supported(phydev, data, &i, PHY_MSE_CAP_CHANNEL_C,
					PHY_MSE_CHANNEL_C);
	if (ret)
		return ret;
	ret = get_snapshot_if_supported(phydev, data, &i, PHY_MSE_CAP_CHANNEL_D,
					PHY_MSE_CHANNEL_D);
	if (ret)
		return ret;

	/* If any individual channels were found, we are done. */
	if (i > 0) {
		data->num_snapshots = i;
		return 0;
	}

	/* Priority 2: Worst channel, if no individual channels supported. */
	ret = get_snapshot_if_supported(phydev, data, &i,
					PHY_MSE_CAP_WORST_CHANNEL,
					PHY_MSE_CHANNEL_WORST);
	if (ret)
		return ret;

	/* If worst channel was found, we are done. */
	if (i > 0) {
		data->num_snapshots = i;
		return 0;
	}

	/* Priority 3: Link-wide, if nothing else is supported. */
	ret = get_snapshot_if_supported(phydev, data, &i, PHY_MSE_CAP_LINK,
					PHY_MSE_CHANNEL_LINK);
	if (ret)
		return ret;

	data->num_snapshots = i;
	return 0;
}

static int mse_prepare_data(const struct ethnl_req_info *req_base,
			    struct ethnl_reply_data *reply_base,
			    const struct genl_info *info)
{
	struct mse_reply_data *data = mse_repdata(reply_base);
	struct net_device *dev = reply_base->dev;
	struct phy_device *phydev;
	int ret;

	phydev = ethnl_req_get_phydev(req_base, info->attrs,
				      ETHTOOL_A_MSE_HEADER, info->extack);
	if (IS_ERR(phydev))
		return PTR_ERR(phydev);
	if (!phydev)
		return -EOPNOTSUPP;

	ret = ethnl_ops_begin(dev);
	if (ret)
		return ret;

	mutex_lock(&phydev->lock);

	if (!phydev->drv || !phydev->drv->get_mse_capability ||
	    !phydev->drv->get_mse_snapshot) {
		ret = -EOPNOTSUPP;
		goto out_unlock;
	}
	if (!phydev->link) {
		ret = -ENETDOWN;
		goto out_unlock;
	}

	ret = phydev->drv->get_mse_capability(phydev, &data->capability);
	if (ret)
		goto out_unlock;

	ret = mse_get_channels(phydev, data);

out_unlock:
	mutex_unlock(&phydev->lock);
	ethnl_ops_complete(dev);
	if (ret)
		kfree(data->snapshots);
	return ret;
}

static void mse_cleanup_data(struct ethnl_reply_data *reply_base)
{
	struct mse_reply_data *data = mse_repdata(reply_base);

	kfree(data->snapshots);
}

static int mse_reply_size(const struct ethnl_req_info *req_base,
			  const struct ethnl_reply_data *reply_base)
{
	const struct mse_reply_data *data = mse_repdata(reply_base);
	size_t len = 0;
	unsigned int i;

	/* ETHTOOL_A_MSE_CAPABILITIES */
	len += nla_total_size(0);
	if (data->capability.supported_caps & PHY_MSE_CAP_AVG)
		/* ETHTOOL_A_MSE_CAPABILITIES_MAX_AVERAGE_MSE */
		len += nla_total_size(sizeof(u64));
	if (data->capability.supported_caps & (PHY_MSE_CAP_PEAK |
					       PHY_MSE_CAP_WORST_PEAK))
		/* ETHTOOL_A_MSE_CAPABILITIES_MAX_PEAK_MSE */
		len += nla_total_size(sizeof(u64));
	/* ETHTOOL_A_MSE_CAPABILITIES_REFRESH_RATE_PS */
	len += nla_total_size(sizeof(u64));
	/* ETHTOOL_A_MSE_CAPABILITIES_NUM_SYMBOLS */
	len += nla_total_size(sizeof(u64));

	for (i = 0; i < data->num_snapshots; i++) {
		size_t snapshot_len = 0;

		/* Per-channel nest (e.g., ETHTOOL_A_MSE_CHANNEL_A / _B / _C /
		 * _D / _WORST_CHANNEL / _LINK)
		 */
		snapshot_len += nla_total_size(0);

		if (data->capability.supported_caps & PHY_MSE_CAP_AVG)
			snapshot_len += nla_total_size(sizeof(u64));
		if (data->capability.supported_caps & PHY_MSE_CAP_PEAK)
			snapshot_len += nla_total_size(sizeof(u64));
		if (data->capability.supported_caps & PHY_MSE_CAP_WORST_PEAK)
			snapshot_len += nla_total_size(sizeof(u64));

		len += snapshot_len;
	}

	return len;
}

static int mse_channel_to_attr(int ch)
{
	switch (ch) {
	case PHY_MSE_CHANNEL_A:
		return ETHTOOL_A_MSE_CHANNEL_A;
	case PHY_MSE_CHANNEL_B:
		return ETHTOOL_A_MSE_CHANNEL_B;
	case PHY_MSE_CHANNEL_C:
		return ETHTOOL_A_MSE_CHANNEL_C;
	case PHY_MSE_CHANNEL_D:
		return ETHTOOL_A_MSE_CHANNEL_D;
	case PHY_MSE_CHANNEL_WORST:
		return ETHTOOL_A_MSE_WORST_CHANNEL;
	case PHY_MSE_CHANNEL_LINK:
		return ETHTOOL_A_MSE_LINK;
	default:
		return -EINVAL;
	}
}

static int mse_fill_reply(struct sk_buff *skb,
			  const struct ethnl_req_info *req_base,
			  const struct ethnl_reply_data *reply_base)
{
	const struct mse_reply_data *data = mse_repdata(reply_base);
	struct nlattr *nest;
	unsigned int i;
	int ret;

	nest = nla_nest_start(skb, ETHTOOL_A_MSE_CAPABILITIES);
	if (!nest)
		return -EMSGSIZE;

	if (data->capability.supported_caps & PHY_MSE_CAP_AVG) {
		ret = nla_put_uint(skb,
				   ETHTOOL_A_MSE_CAPABILITIES_MAX_AVERAGE_MSE,
				   data->capability.max_average_mse);
		if (ret < 0)
			goto nla_put_nest_failure;
	}

	if (data->capability.supported_caps & (PHY_MSE_CAP_PEAK |
					       PHY_MSE_CAP_WORST_PEAK)) {
		ret = nla_put_uint(skb, ETHTOOL_A_MSE_CAPABILITIES_MAX_PEAK_MSE,
				   data->capability.max_peak_mse);
		if (ret < 0)
			goto nla_put_nest_failure;
	}

	ret = nla_put_uint(skb, ETHTOOL_A_MSE_CAPABILITIES_REFRESH_RATE_PS,
			   data->capability.refresh_rate_ps);
	if (ret < 0)
		goto nla_put_nest_failure;

	ret = nla_put_uint(skb, ETHTOOL_A_MSE_CAPABILITIES_NUM_SYMBOLS,
			   data->capability.num_symbols);
	if (ret < 0)
		goto nla_put_nest_failure;

	nla_nest_end(skb, nest);

	for (i = 0; i < data->num_snapshots; i++) {
		const struct mse_snapshot_entry *s = &data->snapshots[i];
		int chan_attr;

		chan_attr = mse_channel_to_attr(s->channel);
		if (chan_attr < 0)
			return chan_attr;

		nest = nla_nest_start(skb, chan_attr);
		if (!nest)
			return -EMSGSIZE;

		if (data->capability.supported_caps & PHY_MSE_CAP_AVG) {
			ret = nla_put_uint(skb,
					   ETHTOOL_A_MSE_SNAPSHOT_AVERAGE_MSE,
					   s->snapshot.average_mse);
			if (ret)
				goto nla_put_nest_failure;
		}
		if (data->capability.supported_caps & PHY_MSE_CAP_PEAK) {
			ret = nla_put_uint(skb, ETHTOOL_A_MSE_SNAPSHOT_PEAK_MSE,
					   s->snapshot.peak_mse);
			if (ret)
				goto nla_put_nest_failure;
		}
		if (data->capability.supported_caps & PHY_MSE_CAP_WORST_PEAK) {
			ret = nla_put_uint(skb,
					   ETHTOOL_A_MSE_SNAPSHOT_WORST_PEAK_MSE,
					   s->snapshot.worst_peak_mse);
			if (ret)
				goto nla_put_nest_failure;
		}

		nla_nest_end(skb, nest);
	}

	return 0;

nla_put_nest_failure:
	nla_nest_cancel(skb, nest);
	return ret;
}

const struct ethnl_request_ops ethnl_mse_request_ops = {
	.request_cmd = ETHTOOL_MSG_MSE_GET,
	.reply_cmd = ETHTOOL_MSG_MSE_GET_REPLY,
	.hdr_attr = ETHTOOL_A_MSE_HEADER,
	.req_info_size = sizeof(struct mse_req_info),
	.reply_data_size = sizeof(struct mse_reply_data),

	.prepare_data = mse_prepare_data,
	.cleanup_data = mse_cleanup_data,
	.reply_size = mse_reply_size,
	.fill_reply = mse_fill_reply,
};
