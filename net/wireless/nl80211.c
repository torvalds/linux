/*
 * This is the new netlink-based wireless configuration interface.
 *
 * Copyright 2006, 2007	Johannes Berg <johannes@sipsolutions.net>
 */

#include <linux/if.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/if_ether.h>
#include <linux/ieee80211.h>
#include <linux/nl80211.h>
#include <linux/rtnetlink.h>
#include <linux/netlink.h>
#include <net/genetlink.h>
#include <net/cfg80211.h>
#include "core.h"
#include "nl80211.h"
#include "reg.h"

/* the netlink family */
static struct genl_family nl80211_fam = {
	.id = GENL_ID_GENERATE,	/* don't bother with a hardcoded ID */
	.name = "nl80211",	/* have users key off the name instead */
	.hdrsize = 0,		/* no private header */
	.version = 1,		/* no particular meaning now */
	.maxattr = NL80211_ATTR_MAX,
};

/* internal helper: get drv and dev */
static int get_drv_dev_by_info_ifindex(struct nlattr **attrs,
				       struct cfg80211_registered_device **drv,
				       struct net_device **dev)
{
	int ifindex;

	if (!attrs[NL80211_ATTR_IFINDEX])
		return -EINVAL;

	ifindex = nla_get_u32(attrs[NL80211_ATTR_IFINDEX]);
	*dev = dev_get_by_index(&init_net, ifindex);
	if (!*dev)
		return -ENODEV;

	*drv = cfg80211_get_dev_from_ifindex(ifindex);
	if (IS_ERR(*drv)) {
		dev_put(*dev);
		return PTR_ERR(*drv);
	}

	return 0;
}

/* policy for the attributes */
static struct nla_policy nl80211_policy[NL80211_ATTR_MAX+1] __read_mostly = {
	[NL80211_ATTR_WIPHY] = { .type = NLA_U32 },
	[NL80211_ATTR_WIPHY_NAME] = { .type = NLA_NUL_STRING,
				      .len = BUS_ID_SIZE-1 },
	[NL80211_ATTR_WIPHY_TXQ_PARAMS] = { .type = NLA_NESTED },
	[NL80211_ATTR_WIPHY_FREQ] = { .type = NLA_U32 },
	[NL80211_ATTR_WIPHY_CHANNEL_TYPE] = { .type = NLA_U32 },

	[NL80211_ATTR_IFTYPE] = { .type = NLA_U32 },
	[NL80211_ATTR_IFINDEX] = { .type = NLA_U32 },
	[NL80211_ATTR_IFNAME] = { .type = NLA_NUL_STRING, .len = IFNAMSIZ-1 },

	[NL80211_ATTR_MAC] = { .type = NLA_BINARY, .len = ETH_ALEN },

	[NL80211_ATTR_KEY_DATA] = { .type = NLA_BINARY,
				    .len = WLAN_MAX_KEY_LEN },
	[NL80211_ATTR_KEY_IDX] = { .type = NLA_U8 },
	[NL80211_ATTR_KEY_CIPHER] = { .type = NLA_U32 },
	[NL80211_ATTR_KEY_DEFAULT] = { .type = NLA_FLAG },

	[NL80211_ATTR_BEACON_INTERVAL] = { .type = NLA_U32 },
	[NL80211_ATTR_DTIM_PERIOD] = { .type = NLA_U32 },
	[NL80211_ATTR_BEACON_HEAD] = { .type = NLA_BINARY,
				       .len = IEEE80211_MAX_DATA_LEN },
	[NL80211_ATTR_BEACON_TAIL] = { .type = NLA_BINARY,
				       .len = IEEE80211_MAX_DATA_LEN },
	[NL80211_ATTR_STA_AID] = { .type = NLA_U16 },
	[NL80211_ATTR_STA_FLAGS] = { .type = NLA_NESTED },
	[NL80211_ATTR_STA_LISTEN_INTERVAL] = { .type = NLA_U16 },
	[NL80211_ATTR_STA_SUPPORTED_RATES] = { .type = NLA_BINARY,
					       .len = NL80211_MAX_SUPP_RATES },
	[NL80211_ATTR_STA_PLINK_ACTION] = { .type = NLA_U8 },
	[NL80211_ATTR_STA_VLAN] = { .type = NLA_U32 },
	[NL80211_ATTR_MNTR_FLAGS] = { /* NLA_NESTED can't be empty */ },
	[NL80211_ATTR_MESH_ID] = { .type = NLA_BINARY,
				.len = IEEE80211_MAX_MESH_ID_LEN },
	[NL80211_ATTR_MPATH_NEXT_HOP] = { .type = NLA_U32 },

	[NL80211_ATTR_REG_ALPHA2] = { .type = NLA_STRING, .len = 2 },
	[NL80211_ATTR_REG_RULES] = { .type = NLA_NESTED },

	[NL80211_ATTR_BSS_CTS_PROT] = { .type = NLA_U8 },
	[NL80211_ATTR_BSS_SHORT_PREAMBLE] = { .type = NLA_U8 },
	[NL80211_ATTR_BSS_SHORT_SLOT_TIME] = { .type = NLA_U8 },
	[NL80211_ATTR_BSS_BASIC_RATES] = { .type = NLA_BINARY,
					   .len = NL80211_MAX_SUPP_RATES },

	[NL80211_ATTR_MESH_PARAMS] = { .type = NLA_NESTED },

	[NL80211_ATTR_HT_CAPABILITY] = { .type = NLA_BINARY,
					 .len = NL80211_HT_CAPABILITY_LEN },
};

/* message building helper */
static inline void *nl80211hdr_put(struct sk_buff *skb, u32 pid, u32 seq,
				   int flags, u8 cmd)
{
	/* since there is no private header just add the generic one */
	return genlmsg_put(skb, pid, seq, &nl80211_fam, flags, cmd);
}

/* netlink command implementations */

static int nl80211_send_wiphy(struct sk_buff *msg, u32 pid, u32 seq, int flags,
			      struct cfg80211_registered_device *dev)
{
	void *hdr;
	struct nlattr *nl_bands, *nl_band;
	struct nlattr *nl_freqs, *nl_freq;
	struct nlattr *nl_rates, *nl_rate;
	struct nlattr *nl_modes;
	enum ieee80211_band band;
	struct ieee80211_channel *chan;
	struct ieee80211_rate *rate;
	int i;
	u16 ifmodes = dev->wiphy.interface_modes;

	hdr = nl80211hdr_put(msg, pid, seq, flags, NL80211_CMD_NEW_WIPHY);
	if (!hdr)
		return -1;

	NLA_PUT_U32(msg, NL80211_ATTR_WIPHY, dev->idx);
	NLA_PUT_STRING(msg, NL80211_ATTR_WIPHY_NAME, wiphy_name(&dev->wiphy));

	nl_modes = nla_nest_start(msg, NL80211_ATTR_SUPPORTED_IFTYPES);
	if (!nl_modes)
		goto nla_put_failure;

	i = 0;
	while (ifmodes) {
		if (ifmodes & 1)
			NLA_PUT_FLAG(msg, i);
		ifmodes >>= 1;
		i++;
	}

	nla_nest_end(msg, nl_modes);

	nl_bands = nla_nest_start(msg, NL80211_ATTR_WIPHY_BANDS);
	if (!nl_bands)
		goto nla_put_failure;

	for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
		if (!dev->wiphy.bands[band])
			continue;

		nl_band = nla_nest_start(msg, band);
		if (!nl_band)
			goto nla_put_failure;

		/* add HT info */
		if (dev->wiphy.bands[band]->ht_cap.ht_supported) {
			NLA_PUT(msg, NL80211_BAND_ATTR_HT_MCS_SET,
				sizeof(dev->wiphy.bands[band]->ht_cap.mcs),
				&dev->wiphy.bands[band]->ht_cap.mcs);
			NLA_PUT_U16(msg, NL80211_BAND_ATTR_HT_CAPA,
				dev->wiphy.bands[band]->ht_cap.cap);
			NLA_PUT_U8(msg, NL80211_BAND_ATTR_HT_AMPDU_FACTOR,
				dev->wiphy.bands[band]->ht_cap.ampdu_factor);
			NLA_PUT_U8(msg, NL80211_BAND_ATTR_HT_AMPDU_DENSITY,
				dev->wiphy.bands[band]->ht_cap.ampdu_density);
		}

		/* add frequencies */
		nl_freqs = nla_nest_start(msg, NL80211_BAND_ATTR_FREQS);
		if (!nl_freqs)
			goto nla_put_failure;

		for (i = 0; i < dev->wiphy.bands[band]->n_channels; i++) {
			nl_freq = nla_nest_start(msg, i);
			if (!nl_freq)
				goto nla_put_failure;

			chan = &dev->wiphy.bands[band]->channels[i];
			NLA_PUT_U32(msg, NL80211_FREQUENCY_ATTR_FREQ,
				    chan->center_freq);

			if (chan->flags & IEEE80211_CHAN_DISABLED)
				NLA_PUT_FLAG(msg, NL80211_FREQUENCY_ATTR_DISABLED);
			if (chan->flags & IEEE80211_CHAN_PASSIVE_SCAN)
				NLA_PUT_FLAG(msg, NL80211_FREQUENCY_ATTR_PASSIVE_SCAN);
			if (chan->flags & IEEE80211_CHAN_NO_IBSS)
				NLA_PUT_FLAG(msg, NL80211_FREQUENCY_ATTR_NO_IBSS);
			if (chan->flags & IEEE80211_CHAN_RADAR)
				NLA_PUT_FLAG(msg, NL80211_FREQUENCY_ATTR_RADAR);

			NLA_PUT_U32(msg, NL80211_FREQUENCY_ATTR_MAX_TX_POWER,
				    DBM_TO_MBM(chan->max_power));

			nla_nest_end(msg, nl_freq);
		}

		nla_nest_end(msg, nl_freqs);

		/* add bitrates */
		nl_rates = nla_nest_start(msg, NL80211_BAND_ATTR_RATES);
		if (!nl_rates)
			goto nla_put_failure;

		for (i = 0; i < dev->wiphy.bands[band]->n_bitrates; i++) {
			nl_rate = nla_nest_start(msg, i);
			if (!nl_rate)
				goto nla_put_failure;

			rate = &dev->wiphy.bands[band]->bitrates[i];
			NLA_PUT_U32(msg, NL80211_BITRATE_ATTR_RATE,
				    rate->bitrate);
			if (rate->flags & IEEE80211_RATE_SHORT_PREAMBLE)
				NLA_PUT_FLAG(msg,
					NL80211_BITRATE_ATTR_2GHZ_SHORTPREAMBLE);

			nla_nest_end(msg, nl_rate);
		}

		nla_nest_end(msg, nl_rates);

		nla_nest_end(msg, nl_band);
	}
	nla_nest_end(msg, nl_bands);

	return genlmsg_end(msg, hdr);

 nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int nl80211_dump_wiphy(struct sk_buff *skb, struct netlink_callback *cb)
{
	int idx = 0;
	int start = cb->args[0];
	struct cfg80211_registered_device *dev;

	mutex_lock(&cfg80211_drv_mutex);
	list_for_each_entry(dev, &cfg80211_drv_list, list) {
		if (++idx <= start)
			continue;
		if (nl80211_send_wiphy(skb, NETLINK_CB(cb->skb).pid,
				       cb->nlh->nlmsg_seq, NLM_F_MULTI,
				       dev) < 0) {
			idx--;
			break;
		}
	}
	mutex_unlock(&cfg80211_drv_mutex);

	cb->args[0] = idx;

	return skb->len;
}

static int nl80211_get_wiphy(struct sk_buff *skb, struct genl_info *info)
{
	struct sk_buff *msg;
	struct cfg80211_registered_device *dev;

	dev = cfg80211_get_dev_from_info(info);
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	msg = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg)
		goto out_err;

	if (nl80211_send_wiphy(msg, info->snd_pid, info->snd_seq, 0, dev) < 0)
		goto out_free;

	cfg80211_put_dev(dev);

	return genlmsg_unicast(msg, info->snd_pid);

 out_free:
	nlmsg_free(msg);
 out_err:
	cfg80211_put_dev(dev);
	return -ENOBUFS;
}

static const struct nla_policy txq_params_policy[NL80211_TXQ_ATTR_MAX + 1] = {
	[NL80211_TXQ_ATTR_QUEUE]		= { .type = NLA_U8 },
	[NL80211_TXQ_ATTR_TXOP]			= { .type = NLA_U16 },
	[NL80211_TXQ_ATTR_CWMIN]		= { .type = NLA_U16 },
	[NL80211_TXQ_ATTR_CWMAX]		= { .type = NLA_U16 },
	[NL80211_TXQ_ATTR_AIFS]			= { .type = NLA_U8 },
};

static int parse_txq_params(struct nlattr *tb[],
			    struct ieee80211_txq_params *txq_params)
{
	if (!tb[NL80211_TXQ_ATTR_QUEUE] || !tb[NL80211_TXQ_ATTR_TXOP] ||
	    !tb[NL80211_TXQ_ATTR_CWMIN] || !tb[NL80211_TXQ_ATTR_CWMAX] ||
	    !tb[NL80211_TXQ_ATTR_AIFS])
		return -EINVAL;

	txq_params->queue = nla_get_u8(tb[NL80211_TXQ_ATTR_QUEUE]);
	txq_params->txop = nla_get_u16(tb[NL80211_TXQ_ATTR_TXOP]);
	txq_params->cwmin = nla_get_u16(tb[NL80211_TXQ_ATTR_CWMIN]);
	txq_params->cwmax = nla_get_u16(tb[NL80211_TXQ_ATTR_CWMAX]);
	txq_params->aifs = nla_get_u8(tb[NL80211_TXQ_ATTR_AIFS]);

	return 0;
}

static int nl80211_set_wiphy(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
	int result = 0, rem_txq_params = 0;
	struct nlattr *nl_txq_params;

	rdev = cfg80211_get_dev_from_info(info);
	if (IS_ERR(rdev))
		return PTR_ERR(rdev);

	if (info->attrs[NL80211_ATTR_WIPHY_NAME]) {
		result = cfg80211_dev_rename(
			rdev, nla_data(info->attrs[NL80211_ATTR_WIPHY_NAME]));
		if (result)
			goto bad_res;
	}

	if (info->attrs[NL80211_ATTR_WIPHY_TXQ_PARAMS]) {
		struct ieee80211_txq_params txq_params;
		struct nlattr *tb[NL80211_TXQ_ATTR_MAX + 1];

		if (!rdev->ops->set_txq_params) {
			result = -EOPNOTSUPP;
			goto bad_res;
		}

		nla_for_each_nested(nl_txq_params,
				    info->attrs[NL80211_ATTR_WIPHY_TXQ_PARAMS],
				    rem_txq_params) {
			nla_parse(tb, NL80211_TXQ_ATTR_MAX,
				  nla_data(nl_txq_params),
				  nla_len(nl_txq_params),
				  txq_params_policy);
			result = parse_txq_params(tb, &txq_params);
			if (result)
				goto bad_res;

			result = rdev->ops->set_txq_params(&rdev->wiphy,
							   &txq_params);
			if (result)
				goto bad_res;
		}
	}

	if (info->attrs[NL80211_ATTR_WIPHY_FREQ]) {
		enum nl80211_channel_type channel_type = NL80211_CHAN_NO_HT;
		struct ieee80211_channel *chan;
		struct ieee80211_sta_ht_cap *ht_cap;
		u32 freq, sec_freq;

		if (!rdev->ops->set_channel) {
			result = -EOPNOTSUPP;
			goto bad_res;
		}

		result = -EINVAL;

		if (info->attrs[NL80211_ATTR_WIPHY_CHANNEL_TYPE]) {
			channel_type = nla_get_u32(info->attrs[
					   NL80211_ATTR_WIPHY_CHANNEL_TYPE]);
			if (channel_type != NL80211_CHAN_NO_HT &&
			    channel_type != NL80211_CHAN_HT20 &&
			    channel_type != NL80211_CHAN_HT40PLUS &&
			    channel_type != NL80211_CHAN_HT40MINUS)
				goto bad_res;
		}

		freq = nla_get_u32(info->attrs[NL80211_ATTR_WIPHY_FREQ]);
		chan = ieee80211_get_channel(&rdev->wiphy, freq);

		/* Primary channel not allowed */
		if (!chan || chan->flags & IEEE80211_CHAN_DISABLED)
			goto bad_res;

		if (channel_type == NL80211_CHAN_HT40MINUS)
			sec_freq = freq - 20;
		else if (channel_type == NL80211_CHAN_HT40PLUS)
			sec_freq = freq + 20;
		else
			sec_freq = 0;

		ht_cap = &rdev->wiphy.bands[chan->band]->ht_cap;

		/* no HT capabilities */
		if (channel_type != NL80211_CHAN_NO_HT &&
		    !ht_cap->ht_supported)
			goto bad_res;

		if (sec_freq) {
			struct ieee80211_channel *schan;

			/* no 40 MHz capabilities */
			if (!(ht_cap->cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40) ||
			    (ht_cap->cap & IEEE80211_HT_CAP_40MHZ_INTOLERANT))
				goto bad_res;

			schan = ieee80211_get_channel(&rdev->wiphy, sec_freq);

			/* Secondary channel not allowed */
			if (!schan || schan->flags & IEEE80211_CHAN_DISABLED)
				goto bad_res;
		}

		result = rdev->ops->set_channel(&rdev->wiphy, chan,
						channel_type);
		if (result)
			goto bad_res;
	}


 bad_res:
	cfg80211_put_dev(rdev);
	return result;
}


static int nl80211_send_iface(struct sk_buff *msg, u32 pid, u32 seq, int flags,
			      struct net_device *dev)
{
	void *hdr;

	hdr = nl80211hdr_put(msg, pid, seq, flags, NL80211_CMD_NEW_INTERFACE);
	if (!hdr)
		return -1;

	NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, dev->ifindex);
	NLA_PUT_STRING(msg, NL80211_ATTR_IFNAME, dev->name);
	NLA_PUT_U32(msg, NL80211_ATTR_IFTYPE, dev->ieee80211_ptr->iftype);
	return genlmsg_end(msg, hdr);

 nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int nl80211_dump_interface(struct sk_buff *skb, struct netlink_callback *cb)
{
	int wp_idx = 0;
	int if_idx = 0;
	int wp_start = cb->args[0];
	int if_start = cb->args[1];
	struct cfg80211_registered_device *dev;
	struct wireless_dev *wdev;

	mutex_lock(&cfg80211_drv_mutex);
	list_for_each_entry(dev, &cfg80211_drv_list, list) {
		if (wp_idx < wp_start) {
			wp_idx++;
			continue;
		}
		if_idx = 0;

		mutex_lock(&dev->devlist_mtx);
		list_for_each_entry(wdev, &dev->netdev_list, list) {
			if (if_idx < if_start) {
				if_idx++;
				continue;
			}
			if (nl80211_send_iface(skb, NETLINK_CB(cb->skb).pid,
					       cb->nlh->nlmsg_seq, NLM_F_MULTI,
					       wdev->netdev) < 0) {
				mutex_unlock(&dev->devlist_mtx);
				goto out;
			}
			if_idx++;
		}
		mutex_unlock(&dev->devlist_mtx);

		wp_idx++;
	}
 out:
	mutex_unlock(&cfg80211_drv_mutex);

	cb->args[0] = wp_idx;
	cb->args[1] = if_idx;

	return skb->len;
}

static int nl80211_get_interface(struct sk_buff *skb, struct genl_info *info)
{
	struct sk_buff *msg;
	struct cfg80211_registered_device *dev;
	struct net_device *netdev;
	int err;

	err = get_drv_dev_by_info_ifindex(info->attrs, &dev, &netdev);
	if (err)
		return err;

	msg = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg)
		goto out_err;

	if (nl80211_send_iface(msg, info->snd_pid, info->snd_seq, 0, netdev) < 0)
		goto out_free;

	dev_put(netdev);
	cfg80211_put_dev(dev);

	return genlmsg_unicast(msg, info->snd_pid);

 out_free:
	nlmsg_free(msg);
 out_err:
	dev_put(netdev);
	cfg80211_put_dev(dev);
	return -ENOBUFS;
}

static const struct nla_policy mntr_flags_policy[NL80211_MNTR_FLAG_MAX + 1] = {
	[NL80211_MNTR_FLAG_FCSFAIL] = { .type = NLA_FLAG },
	[NL80211_MNTR_FLAG_PLCPFAIL] = { .type = NLA_FLAG },
	[NL80211_MNTR_FLAG_CONTROL] = { .type = NLA_FLAG },
	[NL80211_MNTR_FLAG_OTHER_BSS] = { .type = NLA_FLAG },
	[NL80211_MNTR_FLAG_COOK_FRAMES] = { .type = NLA_FLAG },
};

static int parse_monitor_flags(struct nlattr *nla, u32 *mntrflags)
{
	struct nlattr *flags[NL80211_MNTR_FLAG_MAX + 1];
	int flag;

	*mntrflags = 0;

	if (!nla)
		return -EINVAL;

	if (nla_parse_nested(flags, NL80211_MNTR_FLAG_MAX,
			     nla, mntr_flags_policy))
		return -EINVAL;

	for (flag = 1; flag <= NL80211_MNTR_FLAG_MAX; flag++)
		if (flags[flag])
			*mntrflags |= (1<<flag);

	return 0;
}

static int nl80211_set_interface(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *drv;
	struct vif_params params;
	int err, ifindex;
	enum nl80211_iftype type;
	struct net_device *dev;
	u32 _flags, *flags = NULL;

	memset(&params, 0, sizeof(params));

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		return err;
	ifindex = dev->ifindex;
	type = dev->ieee80211_ptr->iftype;
	dev_put(dev);

	err = -EINVAL;
	if (info->attrs[NL80211_ATTR_IFTYPE]) {
		type = nla_get_u32(info->attrs[NL80211_ATTR_IFTYPE]);
		if (type > NL80211_IFTYPE_MAX)
			goto unlock;
	}

	if (!drv->ops->change_virtual_intf ||
	    !(drv->wiphy.interface_modes & (1 << type))) {
		err = -EOPNOTSUPP;
		goto unlock;
	}

	if (info->attrs[NL80211_ATTR_MESH_ID]) {
		if (type != NL80211_IFTYPE_MESH_POINT) {
			err = -EINVAL;
			goto unlock;
		}
		params.mesh_id = nla_data(info->attrs[NL80211_ATTR_MESH_ID]);
		params.mesh_id_len = nla_len(info->attrs[NL80211_ATTR_MESH_ID]);
	}

	if (info->attrs[NL80211_ATTR_MNTR_FLAGS]) {
		if (type != NL80211_IFTYPE_MONITOR) {
			err = -EINVAL;
			goto unlock;
		}
		err = parse_monitor_flags(info->attrs[NL80211_ATTR_MNTR_FLAGS],
					  &_flags);
		if (!err)
			flags = &_flags;
	}
	rtnl_lock();
	err = drv->ops->change_virtual_intf(&drv->wiphy, ifindex,
					    type, flags, &params);

	dev = __dev_get_by_index(&init_net, ifindex);
	WARN_ON(!dev || (!err && dev->ieee80211_ptr->iftype != type));

	rtnl_unlock();

 unlock:
	cfg80211_put_dev(drv);
	return err;
}

static int nl80211_new_interface(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *drv;
	struct vif_params params;
	int err;
	enum nl80211_iftype type = NL80211_IFTYPE_UNSPECIFIED;
	u32 flags;

	memset(&params, 0, sizeof(params));

	if (!info->attrs[NL80211_ATTR_IFNAME])
		return -EINVAL;

	if (info->attrs[NL80211_ATTR_IFTYPE]) {
		type = nla_get_u32(info->attrs[NL80211_ATTR_IFTYPE]);
		if (type > NL80211_IFTYPE_MAX)
			return -EINVAL;
	}

	drv = cfg80211_get_dev_from_info(info);
	if (IS_ERR(drv))
		return PTR_ERR(drv);

	if (!drv->ops->add_virtual_intf ||
	    !(drv->wiphy.interface_modes & (1 << type))) {
		err = -EOPNOTSUPP;
		goto unlock;
	}

	if (type == NL80211_IFTYPE_MESH_POINT &&
	    info->attrs[NL80211_ATTR_MESH_ID]) {
		params.mesh_id = nla_data(info->attrs[NL80211_ATTR_MESH_ID]);
		params.mesh_id_len = nla_len(info->attrs[NL80211_ATTR_MESH_ID]);
	}

	rtnl_lock();
	err = parse_monitor_flags(type == NL80211_IFTYPE_MONITOR ?
				  info->attrs[NL80211_ATTR_MNTR_FLAGS] : NULL,
				  &flags);
	err = drv->ops->add_virtual_intf(&drv->wiphy,
		nla_data(info->attrs[NL80211_ATTR_IFNAME]),
		type, err ? NULL : &flags, &params);
	rtnl_unlock();


 unlock:
	cfg80211_put_dev(drv);
	return err;
}

static int nl80211_del_interface(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *drv;
	int ifindex, err;
	struct net_device *dev;

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		return err;
	ifindex = dev->ifindex;
	dev_put(dev);

	if (!drv->ops->del_virtual_intf) {
		err = -EOPNOTSUPP;
		goto out;
	}

	rtnl_lock();
	err = drv->ops->del_virtual_intf(&drv->wiphy, ifindex);
	rtnl_unlock();

 out:
	cfg80211_put_dev(drv);
	return err;
}

struct get_key_cookie {
	struct sk_buff *msg;
	int error;
};

static void get_key_callback(void *c, struct key_params *params)
{
	struct get_key_cookie *cookie = c;

	if (params->key)
		NLA_PUT(cookie->msg, NL80211_ATTR_KEY_DATA,
			params->key_len, params->key);

	if (params->seq)
		NLA_PUT(cookie->msg, NL80211_ATTR_KEY_SEQ,
			params->seq_len, params->seq);

	if (params->cipher)
		NLA_PUT_U32(cookie->msg, NL80211_ATTR_KEY_CIPHER,
			    params->cipher);

	return;
 nla_put_failure:
	cookie->error = 1;
}

static int nl80211_get_key(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *drv;
	int err;
	struct net_device *dev;
	u8 key_idx = 0;
	u8 *mac_addr = NULL;
	struct get_key_cookie cookie = {
		.error = 0,
	};
	void *hdr;
	struct sk_buff *msg;

	if (info->attrs[NL80211_ATTR_KEY_IDX])
		key_idx = nla_get_u8(info->attrs[NL80211_ATTR_KEY_IDX]);

	if (key_idx > 3)
		return -EINVAL;

	if (info->attrs[NL80211_ATTR_MAC])
		mac_addr = nla_data(info->attrs[NL80211_ATTR_MAC]);

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		return err;

	if (!drv->ops->get_key) {
		err = -EOPNOTSUPP;
		goto out;
	}

	msg = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg) {
		err = -ENOMEM;
		goto out;
	}

	hdr = nl80211hdr_put(msg, info->snd_pid, info->snd_seq, 0,
			     NL80211_CMD_NEW_KEY);

	if (IS_ERR(hdr)) {
		err = PTR_ERR(hdr);
		goto out;
	}

	cookie.msg = msg;

	NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, dev->ifindex);
	NLA_PUT_U8(msg, NL80211_ATTR_KEY_IDX, key_idx);
	if (mac_addr)
		NLA_PUT(msg, NL80211_ATTR_MAC, ETH_ALEN, mac_addr);

	rtnl_lock();
	err = drv->ops->get_key(&drv->wiphy, dev, key_idx, mac_addr,
				&cookie, get_key_callback);
	rtnl_unlock();

	if (err)
		goto out;

	if (cookie.error)
		goto nla_put_failure;

	genlmsg_end(msg, hdr);
	err = genlmsg_unicast(msg, info->snd_pid);
	goto out;

 nla_put_failure:
	err = -ENOBUFS;
	nlmsg_free(msg);
 out:
	cfg80211_put_dev(drv);
	dev_put(dev);
	return err;
}

static int nl80211_set_key(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *drv;
	int err;
	struct net_device *dev;
	u8 key_idx;

	if (!info->attrs[NL80211_ATTR_KEY_IDX])
		return -EINVAL;

	key_idx = nla_get_u8(info->attrs[NL80211_ATTR_KEY_IDX]);

	if (key_idx > 3)
		return -EINVAL;

	/* currently only support setting default key */
	if (!info->attrs[NL80211_ATTR_KEY_DEFAULT])
		return -EINVAL;

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		return err;

	if (!drv->ops->set_default_key) {
		err = -EOPNOTSUPP;
		goto out;
	}

	rtnl_lock();
	err = drv->ops->set_default_key(&drv->wiphy, dev, key_idx);
	rtnl_unlock();

 out:
	cfg80211_put_dev(drv);
	dev_put(dev);
	return err;
}

static int nl80211_new_key(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *drv;
	int err;
	struct net_device *dev;
	struct key_params params;
	u8 key_idx = 0;
	u8 *mac_addr = NULL;

	memset(&params, 0, sizeof(params));

	if (!info->attrs[NL80211_ATTR_KEY_CIPHER])
		return -EINVAL;

	if (info->attrs[NL80211_ATTR_KEY_DATA]) {
		params.key = nla_data(info->attrs[NL80211_ATTR_KEY_DATA]);
		params.key_len = nla_len(info->attrs[NL80211_ATTR_KEY_DATA]);
	}

	if (info->attrs[NL80211_ATTR_KEY_IDX])
		key_idx = nla_get_u8(info->attrs[NL80211_ATTR_KEY_IDX]);

	params.cipher = nla_get_u32(info->attrs[NL80211_ATTR_KEY_CIPHER]);

	if (info->attrs[NL80211_ATTR_MAC])
		mac_addr = nla_data(info->attrs[NL80211_ATTR_MAC]);

	if (key_idx > 3)
		return -EINVAL;

	/*
	 * Disallow pairwise keys with non-zero index unless it's WEP
	 * (because current deployments use pairwise WEP keys with
	 * non-zero indizes but 802.11i clearly specifies to use zero)
	 */
	if (mac_addr && key_idx &&
	    params.cipher != WLAN_CIPHER_SUITE_WEP40 &&
	    params.cipher != WLAN_CIPHER_SUITE_WEP104)
		return -EINVAL;

	/* TODO: add definitions for the lengths to linux/ieee80211.h */
	switch (params.cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		if (params.key_len != 5)
			return -EINVAL;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		if (params.key_len != 32)
			return -EINVAL;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		if (params.key_len != 16)
			return -EINVAL;
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		if (params.key_len != 13)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		return err;

	if (!drv->ops->add_key) {
		err = -EOPNOTSUPP;
		goto out;
	}

	rtnl_lock();
	err = drv->ops->add_key(&drv->wiphy, dev, key_idx, mac_addr, &params);
	rtnl_unlock();

 out:
	cfg80211_put_dev(drv);
	dev_put(dev);
	return err;
}

static int nl80211_del_key(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *drv;
	int err;
	struct net_device *dev;
	u8 key_idx = 0;
	u8 *mac_addr = NULL;

	if (info->attrs[NL80211_ATTR_KEY_IDX])
		key_idx = nla_get_u8(info->attrs[NL80211_ATTR_KEY_IDX]);

	if (key_idx > 3)
		return -EINVAL;

	if (info->attrs[NL80211_ATTR_MAC])
		mac_addr = nla_data(info->attrs[NL80211_ATTR_MAC]);

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		return err;

	if (!drv->ops->del_key) {
		err = -EOPNOTSUPP;
		goto out;
	}

	rtnl_lock();
	err = drv->ops->del_key(&drv->wiphy, dev, key_idx, mac_addr);
	rtnl_unlock();

 out:
	cfg80211_put_dev(drv);
	dev_put(dev);
	return err;
}

static int nl80211_addset_beacon(struct sk_buff *skb, struct genl_info *info)
{
        int (*call)(struct wiphy *wiphy, struct net_device *dev,
		    struct beacon_parameters *info);
	struct cfg80211_registered_device *drv;
	int err;
	struct net_device *dev;
	struct beacon_parameters params;
	int haveinfo = 0;

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		return err;

	switch (info->genlhdr->cmd) {
	case NL80211_CMD_NEW_BEACON:
		/* these are required for NEW_BEACON */
		if (!info->attrs[NL80211_ATTR_BEACON_INTERVAL] ||
		    !info->attrs[NL80211_ATTR_DTIM_PERIOD] ||
		    !info->attrs[NL80211_ATTR_BEACON_HEAD]) {
			err = -EINVAL;
			goto out;
		}

		call = drv->ops->add_beacon;
		break;
	case NL80211_CMD_SET_BEACON:
		call = drv->ops->set_beacon;
		break;
	default:
		WARN_ON(1);
		err = -EOPNOTSUPP;
		goto out;
	}

	if (!call) {
		err = -EOPNOTSUPP;
		goto out;
	}

	memset(&params, 0, sizeof(params));

	if (info->attrs[NL80211_ATTR_BEACON_INTERVAL]) {
		params.interval =
		    nla_get_u32(info->attrs[NL80211_ATTR_BEACON_INTERVAL]);
		haveinfo = 1;
	}

	if (info->attrs[NL80211_ATTR_DTIM_PERIOD]) {
		params.dtim_period =
		    nla_get_u32(info->attrs[NL80211_ATTR_DTIM_PERIOD]);
		haveinfo = 1;
	}

	if (info->attrs[NL80211_ATTR_BEACON_HEAD]) {
		params.head = nla_data(info->attrs[NL80211_ATTR_BEACON_HEAD]);
		params.head_len =
		    nla_len(info->attrs[NL80211_ATTR_BEACON_HEAD]);
		haveinfo = 1;
	}

	if (info->attrs[NL80211_ATTR_BEACON_TAIL]) {
		params.tail = nla_data(info->attrs[NL80211_ATTR_BEACON_TAIL]);
		params.tail_len =
		    nla_len(info->attrs[NL80211_ATTR_BEACON_TAIL]);
		haveinfo = 1;
	}

	if (!haveinfo) {
		err = -EINVAL;
		goto out;
	}

	rtnl_lock();
	err = call(&drv->wiphy, dev, &params);
	rtnl_unlock();

 out:
	cfg80211_put_dev(drv);
	dev_put(dev);
	return err;
}

static int nl80211_del_beacon(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *drv;
	int err;
	struct net_device *dev;

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		return err;

	if (!drv->ops->del_beacon) {
		err = -EOPNOTSUPP;
		goto out;
	}

	rtnl_lock();
	err = drv->ops->del_beacon(&drv->wiphy, dev);
	rtnl_unlock();

 out:
	cfg80211_put_dev(drv);
	dev_put(dev);
	return err;
}

static const struct nla_policy sta_flags_policy[NL80211_STA_FLAG_MAX + 1] = {
	[NL80211_STA_FLAG_AUTHORIZED] = { .type = NLA_FLAG },
	[NL80211_STA_FLAG_SHORT_PREAMBLE] = { .type = NLA_FLAG },
	[NL80211_STA_FLAG_WME] = { .type = NLA_FLAG },
};

static int parse_station_flags(struct nlattr *nla, u32 *staflags)
{
	struct nlattr *flags[NL80211_STA_FLAG_MAX + 1];
	int flag;

	*staflags = 0;

	if (!nla)
		return 0;

	if (nla_parse_nested(flags, NL80211_STA_FLAG_MAX,
			     nla, sta_flags_policy))
		return -EINVAL;

	*staflags = STATION_FLAG_CHANGED;

	for (flag = 1; flag <= NL80211_STA_FLAG_MAX; flag++)
		if (flags[flag])
			*staflags |= (1<<flag);

	return 0;
}

static u16 nl80211_calculate_bitrate(struct rate_info *rate)
{
	int modulation, streams, bitrate;

	if (!(rate->flags & RATE_INFO_FLAGS_MCS))
		return rate->legacy;

	/* the formula below does only work for MCS values smaller than 32 */
	if (rate->mcs >= 32)
		return 0;

	modulation = rate->mcs & 7;
	streams = (rate->mcs >> 3) + 1;

	bitrate = (rate->flags & RATE_INFO_FLAGS_40_MHZ_WIDTH) ?
			13500000 : 6500000;

	if (modulation < 4)
		bitrate *= (modulation + 1);
	else if (modulation == 4)
		bitrate *= (modulation + 2);
	else
		bitrate *= (modulation + 3);

	bitrate *= streams;

	if (rate->flags & RATE_INFO_FLAGS_SHORT_GI)
		bitrate = (bitrate / 9) * 10;

	/* do NOT round down here */
	return (bitrate + 50000) / 100000;
}

static int nl80211_send_station(struct sk_buff *msg, u32 pid, u32 seq,
				int flags, struct net_device *dev,
				u8 *mac_addr, struct station_info *sinfo)
{
	void *hdr;
	struct nlattr *sinfoattr, *txrate;
	u16 bitrate;

	hdr = nl80211hdr_put(msg, pid, seq, flags, NL80211_CMD_NEW_STATION);
	if (!hdr)
		return -1;

	NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, dev->ifindex);
	NLA_PUT(msg, NL80211_ATTR_MAC, ETH_ALEN, mac_addr);

	sinfoattr = nla_nest_start(msg, NL80211_ATTR_STA_INFO);
	if (!sinfoattr)
		goto nla_put_failure;
	if (sinfo->filled & STATION_INFO_INACTIVE_TIME)
		NLA_PUT_U32(msg, NL80211_STA_INFO_INACTIVE_TIME,
			    sinfo->inactive_time);
	if (sinfo->filled & STATION_INFO_RX_BYTES)
		NLA_PUT_U32(msg, NL80211_STA_INFO_RX_BYTES,
			    sinfo->rx_bytes);
	if (sinfo->filled & STATION_INFO_TX_BYTES)
		NLA_PUT_U32(msg, NL80211_STA_INFO_TX_BYTES,
			    sinfo->tx_bytes);
	if (sinfo->filled & STATION_INFO_LLID)
		NLA_PUT_U16(msg, NL80211_STA_INFO_LLID,
			    sinfo->llid);
	if (sinfo->filled & STATION_INFO_PLID)
		NLA_PUT_U16(msg, NL80211_STA_INFO_PLID,
			    sinfo->plid);
	if (sinfo->filled & STATION_INFO_PLINK_STATE)
		NLA_PUT_U8(msg, NL80211_STA_INFO_PLINK_STATE,
			    sinfo->plink_state);
	if (sinfo->filled & STATION_INFO_SIGNAL)
		NLA_PUT_U8(msg, NL80211_STA_INFO_SIGNAL,
			   sinfo->signal);
	if (sinfo->filled & STATION_INFO_TX_BITRATE) {
		txrate = nla_nest_start(msg, NL80211_STA_INFO_TX_BITRATE);
		if (!txrate)
			goto nla_put_failure;

		/* nl80211_calculate_bitrate will return 0 for mcs >= 32 */
		bitrate = nl80211_calculate_bitrate(&sinfo->txrate);
		if (bitrate > 0)
			NLA_PUT_U16(msg, NL80211_RATE_INFO_BITRATE, bitrate);

		if (sinfo->txrate.flags & RATE_INFO_FLAGS_MCS)
			NLA_PUT_U8(msg, NL80211_RATE_INFO_MCS,
				    sinfo->txrate.mcs);
		if (sinfo->txrate.flags & RATE_INFO_FLAGS_40_MHZ_WIDTH)
			NLA_PUT_FLAG(msg, NL80211_RATE_INFO_40_MHZ_WIDTH);
		if (sinfo->txrate.flags & RATE_INFO_FLAGS_SHORT_GI)
			NLA_PUT_FLAG(msg, NL80211_RATE_INFO_SHORT_GI);

		nla_nest_end(msg, txrate);
	}
	nla_nest_end(msg, sinfoattr);

	return genlmsg_end(msg, hdr);

 nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int nl80211_dump_station(struct sk_buff *skb,
				struct netlink_callback *cb)
{
	struct station_info sinfo;
	struct cfg80211_registered_device *dev;
	struct net_device *netdev;
	u8 mac_addr[ETH_ALEN];
	int ifidx = cb->args[0];
	int sta_idx = cb->args[1];
	int err;

	if (!ifidx) {
		err = nlmsg_parse(cb->nlh, GENL_HDRLEN + nl80211_fam.hdrsize,
				  nl80211_fam.attrbuf, nl80211_fam.maxattr,
				  nl80211_policy);
		if (err)
			return err;

		if (!nl80211_fam.attrbuf[NL80211_ATTR_IFINDEX])
			return -EINVAL;

		ifidx = nla_get_u32(nl80211_fam.attrbuf[NL80211_ATTR_IFINDEX]);
		if (!ifidx)
			return -EINVAL;
	}

	netdev = dev_get_by_index(&init_net, ifidx);
	if (!netdev)
		return -ENODEV;

	dev = cfg80211_get_dev_from_ifindex(ifidx);
	if (IS_ERR(dev)) {
		err = PTR_ERR(dev);
		goto out_put_netdev;
	}

	if (!dev->ops->dump_station) {
		err = -ENOSYS;
		goto out_err;
	}

	rtnl_lock();

	while (1) {
		err = dev->ops->dump_station(&dev->wiphy, netdev, sta_idx,
					     mac_addr, &sinfo);
		if (err == -ENOENT)
			break;
		if (err)
			goto out_err_rtnl;

		if (nl80211_send_station(skb,
				NETLINK_CB(cb->skb).pid,
				cb->nlh->nlmsg_seq, NLM_F_MULTI,
				netdev, mac_addr,
				&sinfo) < 0)
			goto out;

		sta_idx++;
	}


 out:
	cb->args[1] = sta_idx;
	err = skb->len;
 out_err_rtnl:
	rtnl_unlock();
 out_err:
	cfg80211_put_dev(dev);
 out_put_netdev:
	dev_put(netdev);

	return err;
}

static int nl80211_get_station(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *drv;
	int err;
	struct net_device *dev;
	struct station_info sinfo;
	struct sk_buff *msg;
	u8 *mac_addr = NULL;

	memset(&sinfo, 0, sizeof(sinfo));

	if (!info->attrs[NL80211_ATTR_MAC])
		return -EINVAL;

	mac_addr = nla_data(info->attrs[NL80211_ATTR_MAC]);

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		return err;

	if (!drv->ops->get_station) {
		err = -EOPNOTSUPP;
		goto out;
	}

	rtnl_lock();
	err = drv->ops->get_station(&drv->wiphy, dev, mac_addr, &sinfo);
	rtnl_unlock();

	if (err)
		goto out;

	msg = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg)
		goto out;

	if (nl80211_send_station(msg, info->snd_pid, info->snd_seq, 0,
				 dev, mac_addr, &sinfo) < 0)
		goto out_free;

	err = genlmsg_unicast(msg, info->snd_pid);
	goto out;

 out_free:
	nlmsg_free(msg);

 out:
	cfg80211_put_dev(drv);
	dev_put(dev);
	return err;
}

/*
 * Get vlan interface making sure it is on the right wiphy.
 */
static int get_vlan(struct nlattr *vlanattr,
		    struct cfg80211_registered_device *rdev,
		    struct net_device **vlan)
{
	*vlan = NULL;

	if (vlanattr) {
		*vlan = dev_get_by_index(&init_net, nla_get_u32(vlanattr));
		if (!*vlan)
			return -ENODEV;
		if (!(*vlan)->ieee80211_ptr)
			return -EINVAL;
		if ((*vlan)->ieee80211_ptr->wiphy != &rdev->wiphy)
			return -EINVAL;
	}
	return 0;
}

static int nl80211_set_station(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *drv;
	int err;
	struct net_device *dev;
	struct station_parameters params;
	u8 *mac_addr = NULL;

	memset(&params, 0, sizeof(params));

	params.listen_interval = -1;

	if (info->attrs[NL80211_ATTR_STA_AID])
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_MAC])
		return -EINVAL;

	mac_addr = nla_data(info->attrs[NL80211_ATTR_MAC]);

	if (info->attrs[NL80211_ATTR_STA_SUPPORTED_RATES]) {
		params.supported_rates =
			nla_data(info->attrs[NL80211_ATTR_STA_SUPPORTED_RATES]);
		params.supported_rates_len =
			nla_len(info->attrs[NL80211_ATTR_STA_SUPPORTED_RATES]);
	}

	if (info->attrs[NL80211_ATTR_STA_LISTEN_INTERVAL])
		params.listen_interval =
		    nla_get_u16(info->attrs[NL80211_ATTR_STA_LISTEN_INTERVAL]);

	if (info->attrs[NL80211_ATTR_HT_CAPABILITY])
		params.ht_capa =
			nla_data(info->attrs[NL80211_ATTR_HT_CAPABILITY]);

	if (parse_station_flags(info->attrs[NL80211_ATTR_STA_FLAGS],
				&params.station_flags))
		return -EINVAL;

	if (info->attrs[NL80211_ATTR_STA_PLINK_ACTION])
		params.plink_action =
		    nla_get_u8(info->attrs[NL80211_ATTR_STA_PLINK_ACTION]);

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		return err;

	err = get_vlan(info->attrs[NL80211_ATTR_STA_VLAN], drv, &params.vlan);
	if (err)
		goto out;

	if (!drv->ops->change_station) {
		err = -EOPNOTSUPP;
		goto out;
	}

	rtnl_lock();
	err = drv->ops->change_station(&drv->wiphy, dev, mac_addr, &params);
	rtnl_unlock();

 out:
	if (params.vlan)
		dev_put(params.vlan);
	cfg80211_put_dev(drv);
	dev_put(dev);
	return err;
}

static int nl80211_new_station(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *drv;
	int err;
	struct net_device *dev;
	struct station_parameters params;
	u8 *mac_addr = NULL;

	memset(&params, 0, sizeof(params));

	if (!info->attrs[NL80211_ATTR_MAC])
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_STA_AID])
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_STA_LISTEN_INTERVAL])
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_STA_SUPPORTED_RATES])
		return -EINVAL;

	mac_addr = nla_data(info->attrs[NL80211_ATTR_MAC]);
	params.supported_rates =
		nla_data(info->attrs[NL80211_ATTR_STA_SUPPORTED_RATES]);
	params.supported_rates_len =
		nla_len(info->attrs[NL80211_ATTR_STA_SUPPORTED_RATES]);
	params.listen_interval =
		nla_get_u16(info->attrs[NL80211_ATTR_STA_LISTEN_INTERVAL]);
	params.aid = nla_get_u16(info->attrs[NL80211_ATTR_STA_AID]);
	if (info->attrs[NL80211_ATTR_HT_CAPABILITY])
		params.ht_capa =
			nla_data(info->attrs[NL80211_ATTR_HT_CAPABILITY]);

	if (parse_station_flags(info->attrs[NL80211_ATTR_STA_FLAGS],
				&params.station_flags))
		return -EINVAL;

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		return err;

	err = get_vlan(info->attrs[NL80211_ATTR_STA_VLAN], drv, &params.vlan);
	if (err)
		goto out;

	if (!drv->ops->add_station) {
		err = -EOPNOTSUPP;
		goto out;
	}

	rtnl_lock();
	err = drv->ops->add_station(&drv->wiphy, dev, mac_addr, &params);
	rtnl_unlock();

 out:
	if (params.vlan)
		dev_put(params.vlan);
	cfg80211_put_dev(drv);
	dev_put(dev);
	return err;
}

static int nl80211_del_station(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *drv;
	int err;
	struct net_device *dev;
	u8 *mac_addr = NULL;

	if (info->attrs[NL80211_ATTR_MAC])
		mac_addr = nla_data(info->attrs[NL80211_ATTR_MAC]);

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		return err;

	if (!drv->ops->del_station) {
		err = -EOPNOTSUPP;
		goto out;
	}

	rtnl_lock();
	err = drv->ops->del_station(&drv->wiphy, dev, mac_addr);
	rtnl_unlock();

 out:
	cfg80211_put_dev(drv);
	dev_put(dev);
	return err;
}

static int nl80211_send_mpath(struct sk_buff *msg, u32 pid, u32 seq,
				int flags, struct net_device *dev,
				u8 *dst, u8 *next_hop,
				struct mpath_info *pinfo)
{
	void *hdr;
	struct nlattr *pinfoattr;

	hdr = nl80211hdr_put(msg, pid, seq, flags, NL80211_CMD_NEW_STATION);
	if (!hdr)
		return -1;

	NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, dev->ifindex);
	NLA_PUT(msg, NL80211_ATTR_MAC, ETH_ALEN, dst);
	NLA_PUT(msg, NL80211_ATTR_MPATH_NEXT_HOP, ETH_ALEN, next_hop);

	pinfoattr = nla_nest_start(msg, NL80211_ATTR_MPATH_INFO);
	if (!pinfoattr)
		goto nla_put_failure;
	if (pinfo->filled & MPATH_INFO_FRAME_QLEN)
		NLA_PUT_U32(msg, NL80211_MPATH_INFO_FRAME_QLEN,
			    pinfo->frame_qlen);
	if (pinfo->filled & MPATH_INFO_DSN)
		NLA_PUT_U32(msg, NL80211_MPATH_INFO_DSN,
			    pinfo->dsn);
	if (pinfo->filled & MPATH_INFO_METRIC)
		NLA_PUT_U32(msg, NL80211_MPATH_INFO_METRIC,
			    pinfo->metric);
	if (pinfo->filled & MPATH_INFO_EXPTIME)
		NLA_PUT_U32(msg, NL80211_MPATH_INFO_EXPTIME,
			    pinfo->exptime);
	if (pinfo->filled & MPATH_INFO_FLAGS)
		NLA_PUT_U8(msg, NL80211_MPATH_INFO_FLAGS,
			    pinfo->flags);
	if (pinfo->filled & MPATH_INFO_DISCOVERY_TIMEOUT)
		NLA_PUT_U32(msg, NL80211_MPATH_INFO_DISCOVERY_TIMEOUT,
			    pinfo->discovery_timeout);
	if (pinfo->filled & MPATH_INFO_DISCOVERY_RETRIES)
		NLA_PUT_U8(msg, NL80211_MPATH_INFO_DISCOVERY_RETRIES,
			    pinfo->discovery_retries);

	nla_nest_end(msg, pinfoattr);

	return genlmsg_end(msg, hdr);

 nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int nl80211_dump_mpath(struct sk_buff *skb,
			      struct netlink_callback *cb)
{
	struct mpath_info pinfo;
	struct cfg80211_registered_device *dev;
	struct net_device *netdev;
	u8 dst[ETH_ALEN];
	u8 next_hop[ETH_ALEN];
	int ifidx = cb->args[0];
	int path_idx = cb->args[1];
	int err;

	if (!ifidx) {
		err = nlmsg_parse(cb->nlh, GENL_HDRLEN + nl80211_fam.hdrsize,
				  nl80211_fam.attrbuf, nl80211_fam.maxattr,
				  nl80211_policy);
		if (err)
			return err;

		if (!nl80211_fam.attrbuf[NL80211_ATTR_IFINDEX])
			return -EINVAL;

		ifidx = nla_get_u32(nl80211_fam.attrbuf[NL80211_ATTR_IFINDEX]);
		if (!ifidx)
			return -EINVAL;
	}

	netdev = dev_get_by_index(&init_net, ifidx);
	if (!netdev)
		return -ENODEV;

	dev = cfg80211_get_dev_from_ifindex(ifidx);
	if (IS_ERR(dev)) {
		err = PTR_ERR(dev);
		goto out_put_netdev;
	}

	if (!dev->ops->dump_mpath) {
		err = -ENOSYS;
		goto out_err;
	}

	rtnl_lock();

	while (1) {
		err = dev->ops->dump_mpath(&dev->wiphy, netdev, path_idx,
					   dst, next_hop, &pinfo);
		if (err == -ENOENT)
			break;
		if (err)
			goto out_err_rtnl;

		if (nl80211_send_mpath(skb, NETLINK_CB(cb->skb).pid,
				       cb->nlh->nlmsg_seq, NLM_F_MULTI,
				       netdev, dst, next_hop,
				       &pinfo) < 0)
			goto out;

		path_idx++;
	}


 out:
	cb->args[1] = path_idx;
	err = skb->len;
 out_err_rtnl:
	rtnl_unlock();
 out_err:
	cfg80211_put_dev(dev);
 out_put_netdev:
	dev_put(netdev);

	return err;
}

static int nl80211_get_mpath(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *drv;
	int err;
	struct net_device *dev;
	struct mpath_info pinfo;
	struct sk_buff *msg;
	u8 *dst = NULL;
	u8 next_hop[ETH_ALEN];

	memset(&pinfo, 0, sizeof(pinfo));

	if (!info->attrs[NL80211_ATTR_MAC])
		return -EINVAL;

	dst = nla_data(info->attrs[NL80211_ATTR_MAC]);

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		return err;

	if (!drv->ops->get_mpath) {
		err = -EOPNOTSUPP;
		goto out;
	}

	rtnl_lock();
	err = drv->ops->get_mpath(&drv->wiphy, dev, dst, next_hop, &pinfo);
	rtnl_unlock();

	if (err)
		goto out;

	msg = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg)
		goto out;

	if (nl80211_send_mpath(msg, info->snd_pid, info->snd_seq, 0,
				 dev, dst, next_hop, &pinfo) < 0)
		goto out_free;

	err = genlmsg_unicast(msg, info->snd_pid);
	goto out;

 out_free:
	nlmsg_free(msg);

 out:
	cfg80211_put_dev(drv);
	dev_put(dev);
	return err;
}

static int nl80211_set_mpath(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *drv;
	int err;
	struct net_device *dev;
	u8 *dst = NULL;
	u8 *next_hop = NULL;

	if (!info->attrs[NL80211_ATTR_MAC])
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_MPATH_NEXT_HOP])
		return -EINVAL;

	dst = nla_data(info->attrs[NL80211_ATTR_MAC]);
	next_hop = nla_data(info->attrs[NL80211_ATTR_MPATH_NEXT_HOP]);

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		return err;

	if (!drv->ops->change_mpath) {
		err = -EOPNOTSUPP;
		goto out;
	}

	rtnl_lock();
	err = drv->ops->change_mpath(&drv->wiphy, dev, dst, next_hop);
	rtnl_unlock();

 out:
	cfg80211_put_dev(drv);
	dev_put(dev);
	return err;
}
static int nl80211_new_mpath(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *drv;
	int err;
	struct net_device *dev;
	u8 *dst = NULL;
	u8 *next_hop = NULL;

	if (!info->attrs[NL80211_ATTR_MAC])
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_MPATH_NEXT_HOP])
		return -EINVAL;

	dst = nla_data(info->attrs[NL80211_ATTR_MAC]);
	next_hop = nla_data(info->attrs[NL80211_ATTR_MPATH_NEXT_HOP]);

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		return err;

	if (!drv->ops->add_mpath) {
		err = -EOPNOTSUPP;
		goto out;
	}

	rtnl_lock();
	err = drv->ops->add_mpath(&drv->wiphy, dev, dst, next_hop);
	rtnl_unlock();

 out:
	cfg80211_put_dev(drv);
	dev_put(dev);
	return err;
}

static int nl80211_del_mpath(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *drv;
	int err;
	struct net_device *dev;
	u8 *dst = NULL;

	if (info->attrs[NL80211_ATTR_MAC])
		dst = nla_data(info->attrs[NL80211_ATTR_MAC]);

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		return err;

	if (!drv->ops->del_mpath) {
		err = -EOPNOTSUPP;
		goto out;
	}

	rtnl_lock();
	err = drv->ops->del_mpath(&drv->wiphy, dev, dst);
	rtnl_unlock();

 out:
	cfg80211_put_dev(drv);
	dev_put(dev);
	return err;
}

static int nl80211_set_bss(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *drv;
	int err;
	struct net_device *dev;
	struct bss_parameters params;

	memset(&params, 0, sizeof(params));
	/* default to not changing parameters */
	params.use_cts_prot = -1;
	params.use_short_preamble = -1;
	params.use_short_slot_time = -1;

	if (info->attrs[NL80211_ATTR_BSS_CTS_PROT])
		params.use_cts_prot =
		    nla_get_u8(info->attrs[NL80211_ATTR_BSS_CTS_PROT]);
	if (info->attrs[NL80211_ATTR_BSS_SHORT_PREAMBLE])
		params.use_short_preamble =
		    nla_get_u8(info->attrs[NL80211_ATTR_BSS_SHORT_PREAMBLE]);
	if (info->attrs[NL80211_ATTR_BSS_SHORT_SLOT_TIME])
		params.use_short_slot_time =
		    nla_get_u8(info->attrs[NL80211_ATTR_BSS_SHORT_SLOT_TIME]);
	if (info->attrs[NL80211_ATTR_BSS_BASIC_RATES]) {
		params.basic_rates =
			nla_data(info->attrs[NL80211_ATTR_BSS_BASIC_RATES]);
		params.basic_rates_len =
			nla_len(info->attrs[NL80211_ATTR_BSS_BASIC_RATES]);
	}

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		return err;

	if (!drv->ops->change_bss) {
		err = -EOPNOTSUPP;
		goto out;
	}

	rtnl_lock();
	err = drv->ops->change_bss(&drv->wiphy, dev, &params);
	rtnl_unlock();

 out:
	cfg80211_put_dev(drv);
	dev_put(dev);
	return err;
}

static const struct nla_policy
	reg_rule_policy[NL80211_REG_RULE_ATTR_MAX + 1] = {
	[NL80211_ATTR_REG_RULE_FLAGS]		= { .type = NLA_U32 },
	[NL80211_ATTR_FREQ_RANGE_START]		= { .type = NLA_U32 },
	[NL80211_ATTR_FREQ_RANGE_END]		= { .type = NLA_U32 },
	[NL80211_ATTR_FREQ_RANGE_MAX_BW]	= { .type = NLA_U32 },
	[NL80211_ATTR_POWER_RULE_MAX_ANT_GAIN]	= { .type = NLA_U32 },
	[NL80211_ATTR_POWER_RULE_MAX_EIRP]	= { .type = NLA_U32 },
};

static int parse_reg_rule(struct nlattr *tb[],
	struct ieee80211_reg_rule *reg_rule)
{
	struct ieee80211_freq_range *freq_range = &reg_rule->freq_range;
	struct ieee80211_power_rule *power_rule = &reg_rule->power_rule;

	if (!tb[NL80211_ATTR_REG_RULE_FLAGS])
		return -EINVAL;
	if (!tb[NL80211_ATTR_FREQ_RANGE_START])
		return -EINVAL;
	if (!tb[NL80211_ATTR_FREQ_RANGE_END])
		return -EINVAL;
	if (!tb[NL80211_ATTR_FREQ_RANGE_MAX_BW])
		return -EINVAL;
	if (!tb[NL80211_ATTR_POWER_RULE_MAX_EIRP])
		return -EINVAL;

	reg_rule->flags = nla_get_u32(tb[NL80211_ATTR_REG_RULE_FLAGS]);

	freq_range->start_freq_khz =
		nla_get_u32(tb[NL80211_ATTR_FREQ_RANGE_START]);
	freq_range->end_freq_khz =
		nla_get_u32(tb[NL80211_ATTR_FREQ_RANGE_END]);
	freq_range->max_bandwidth_khz =
		nla_get_u32(tb[NL80211_ATTR_FREQ_RANGE_MAX_BW]);

	power_rule->max_eirp =
		nla_get_u32(tb[NL80211_ATTR_POWER_RULE_MAX_EIRP]);

	if (tb[NL80211_ATTR_POWER_RULE_MAX_ANT_GAIN])
		power_rule->max_antenna_gain =
			nla_get_u32(tb[NL80211_ATTR_POWER_RULE_MAX_ANT_GAIN]);

	return 0;
}

static int nl80211_req_set_reg(struct sk_buff *skb, struct genl_info *info)
{
	int r;
	char *data = NULL;

	if (!info->attrs[NL80211_ATTR_REG_ALPHA2])
		return -EINVAL;

	data = nla_data(info->attrs[NL80211_ATTR_REG_ALPHA2]);

#ifdef CONFIG_WIRELESS_OLD_REGULATORY
	/* We ignore world regdom requests with the old regdom setup */
	if (is_world_regdom(data))
		return -EINVAL;
#endif
	mutex_lock(&cfg80211_drv_mutex);
	r = __regulatory_hint(NULL, REGDOM_SET_BY_USER, data, 0, ENVIRON_ANY);
	mutex_unlock(&cfg80211_drv_mutex);
	return r;
}

static int nl80211_get_mesh_params(struct sk_buff *skb,
	struct genl_info *info)
{
	struct cfg80211_registered_device *drv;
	struct mesh_config cur_params;
	int err;
	struct net_device *dev;
	void *hdr;
	struct nlattr *pinfoattr;
	struct sk_buff *msg;

	/* Look up our device */
	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		return err;

	if (!drv->ops->get_mesh_params) {
		err = -EOPNOTSUPP;
		goto out;
	}

	/* Get the mesh params */
	rtnl_lock();
	err = drv->ops->get_mesh_params(&drv->wiphy, dev, &cur_params);
	rtnl_unlock();
	if (err)
		goto out;

	/* Draw up a netlink message to send back */
	msg = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg) {
		err = -ENOBUFS;
		goto out;
	}
	hdr = nl80211hdr_put(msg, info->snd_pid, info->snd_seq, 0,
			     NL80211_CMD_GET_MESH_PARAMS);
	if (!hdr)
		goto nla_put_failure;
	pinfoattr = nla_nest_start(msg, NL80211_ATTR_MESH_PARAMS);
	if (!pinfoattr)
		goto nla_put_failure;
	NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, dev->ifindex);
	NLA_PUT_U16(msg, NL80211_MESHCONF_RETRY_TIMEOUT,
			cur_params.dot11MeshRetryTimeout);
	NLA_PUT_U16(msg, NL80211_MESHCONF_CONFIRM_TIMEOUT,
			cur_params.dot11MeshConfirmTimeout);
	NLA_PUT_U16(msg, NL80211_MESHCONF_HOLDING_TIMEOUT,
			cur_params.dot11MeshHoldingTimeout);
	NLA_PUT_U16(msg, NL80211_MESHCONF_MAX_PEER_LINKS,
			cur_params.dot11MeshMaxPeerLinks);
	NLA_PUT_U8(msg, NL80211_MESHCONF_MAX_RETRIES,
			cur_params.dot11MeshMaxRetries);
	NLA_PUT_U8(msg, NL80211_MESHCONF_TTL,
			cur_params.dot11MeshTTL);
	NLA_PUT_U8(msg, NL80211_MESHCONF_AUTO_OPEN_PLINKS,
			cur_params.auto_open_plinks);
	NLA_PUT_U8(msg, NL80211_MESHCONF_HWMP_MAX_PREQ_RETRIES,
			cur_params.dot11MeshHWMPmaxPREQretries);
	NLA_PUT_U32(msg, NL80211_MESHCONF_PATH_REFRESH_TIME,
			cur_params.path_refresh_time);
	NLA_PUT_U16(msg, NL80211_MESHCONF_MIN_DISCOVERY_TIMEOUT,
			cur_params.min_discovery_timeout);
	NLA_PUT_U32(msg, NL80211_MESHCONF_HWMP_ACTIVE_PATH_TIMEOUT,
			cur_params.dot11MeshHWMPactivePathTimeout);
	NLA_PUT_U16(msg, NL80211_MESHCONF_HWMP_PREQ_MIN_INTERVAL,
			cur_params.dot11MeshHWMPpreqMinInterval);
	NLA_PUT_U16(msg, NL80211_MESHCONF_HWMP_NET_DIAM_TRVS_TIME,
			cur_params.dot11MeshHWMPnetDiameterTraversalTime);
	nla_nest_end(msg, pinfoattr);
	genlmsg_end(msg, hdr);
	err = genlmsg_unicast(msg, info->snd_pid);
	goto out;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	err = -EMSGSIZE;
out:
	/* Cleanup */
	cfg80211_put_dev(drv);
	dev_put(dev);
	return err;
}

#define FILL_IN_MESH_PARAM_IF_SET(table, cfg, param, mask, attr_num, nla_fn) \
do {\
	if (table[attr_num]) {\
		cfg.param = nla_fn(table[attr_num]); \
		mask |= (1 << (attr_num - 1)); \
	} \
} while (0);\

static struct nla_policy
nl80211_meshconf_params_policy[NL80211_MESHCONF_ATTR_MAX+1] __read_mostly = {
	[NL80211_MESHCONF_RETRY_TIMEOUT] = { .type = NLA_U16 },
	[NL80211_MESHCONF_CONFIRM_TIMEOUT] = { .type = NLA_U16 },
	[NL80211_MESHCONF_HOLDING_TIMEOUT] = { .type = NLA_U16 },
	[NL80211_MESHCONF_MAX_PEER_LINKS] = { .type = NLA_U16 },
	[NL80211_MESHCONF_MAX_RETRIES] = { .type = NLA_U8 },
	[NL80211_MESHCONF_TTL] = { .type = NLA_U8 },
	[NL80211_MESHCONF_AUTO_OPEN_PLINKS] = { .type = NLA_U8 },

	[NL80211_MESHCONF_HWMP_MAX_PREQ_RETRIES] = { .type = NLA_U8 },
	[NL80211_MESHCONF_PATH_REFRESH_TIME] = { .type = NLA_U32 },
	[NL80211_MESHCONF_MIN_DISCOVERY_TIMEOUT] = { .type = NLA_U16 },
	[NL80211_MESHCONF_HWMP_ACTIVE_PATH_TIMEOUT] = { .type = NLA_U32 },
	[NL80211_MESHCONF_HWMP_PREQ_MIN_INTERVAL] = { .type = NLA_U16 },
	[NL80211_MESHCONF_HWMP_NET_DIAM_TRVS_TIME] = { .type = NLA_U16 },
};

static int nl80211_set_mesh_params(struct sk_buff *skb, struct genl_info *info)
{
	int err;
	u32 mask;
	struct cfg80211_registered_device *drv;
	struct net_device *dev;
	struct mesh_config cfg;
	struct nlattr *tb[NL80211_MESHCONF_ATTR_MAX + 1];
	struct nlattr *parent_attr;

	parent_attr = info->attrs[NL80211_ATTR_MESH_PARAMS];
	if (!parent_attr)
		return -EINVAL;
	if (nla_parse_nested(tb, NL80211_MESHCONF_ATTR_MAX,
			parent_attr, nl80211_meshconf_params_policy))
		return -EINVAL;

	err = get_drv_dev_by_info_ifindex(info->attrs, &drv, &dev);
	if (err)
		return err;

	if (!drv->ops->set_mesh_params) {
		err = -EOPNOTSUPP;
		goto out;
	}

	/* This makes sure that there aren't more than 32 mesh config
	 * parameters (otherwise our bitfield scheme would not work.) */
	BUILD_BUG_ON(NL80211_MESHCONF_ATTR_MAX > 32);

	/* Fill in the params struct */
	mask = 0;
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, dot11MeshRetryTimeout,
			mask, NL80211_MESHCONF_RETRY_TIMEOUT, nla_get_u16);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, dot11MeshConfirmTimeout,
			mask, NL80211_MESHCONF_CONFIRM_TIMEOUT, nla_get_u16);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, dot11MeshHoldingTimeout,
			mask, NL80211_MESHCONF_HOLDING_TIMEOUT, nla_get_u16);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, dot11MeshMaxPeerLinks,
			mask, NL80211_MESHCONF_MAX_PEER_LINKS, nla_get_u16);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, dot11MeshMaxRetries,
			mask, NL80211_MESHCONF_MAX_RETRIES, nla_get_u8);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, dot11MeshTTL,
			mask, NL80211_MESHCONF_TTL, nla_get_u8);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, auto_open_plinks,
			mask, NL80211_MESHCONF_AUTO_OPEN_PLINKS, nla_get_u8);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, dot11MeshHWMPmaxPREQretries,
			mask, NL80211_MESHCONF_HWMP_MAX_PREQ_RETRIES,
			nla_get_u8);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, path_refresh_time,
			mask, NL80211_MESHCONF_PATH_REFRESH_TIME, nla_get_u32);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, min_discovery_timeout,
			mask, NL80211_MESHCONF_MIN_DISCOVERY_TIMEOUT,
			nla_get_u16);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, dot11MeshHWMPactivePathTimeout,
			mask, NL80211_MESHCONF_HWMP_ACTIVE_PATH_TIMEOUT,
			nla_get_u32);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, dot11MeshHWMPpreqMinInterval,
			mask, NL80211_MESHCONF_HWMP_PREQ_MIN_INTERVAL,
			nla_get_u16);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg,
			dot11MeshHWMPnetDiameterTraversalTime,
			mask, NL80211_MESHCONF_HWMP_NET_DIAM_TRVS_TIME,
			nla_get_u16);

	/* Apply changes */
	rtnl_lock();
	err = drv->ops->set_mesh_params(&drv->wiphy, dev, &cfg, mask);
	rtnl_unlock();

 out:
	/* cleanup */
	cfg80211_put_dev(drv);
	dev_put(dev);
	return err;
}

#undef FILL_IN_MESH_PARAM_IF_SET

static int nl80211_set_reg(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *tb[NL80211_REG_RULE_ATTR_MAX + 1];
	struct nlattr *nl_reg_rule;
	char *alpha2 = NULL;
	int rem_reg_rules = 0, r = 0;
	u32 num_rules = 0, rule_idx = 0, size_of_regd;
	struct ieee80211_regdomain *rd = NULL;

	if (!info->attrs[NL80211_ATTR_REG_ALPHA2])
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_REG_RULES])
		return -EINVAL;

	alpha2 = nla_data(info->attrs[NL80211_ATTR_REG_ALPHA2]);

	nla_for_each_nested(nl_reg_rule, info->attrs[NL80211_ATTR_REG_RULES],
			rem_reg_rules) {
		num_rules++;
		if (num_rules > NL80211_MAX_SUPP_REG_RULES)
			goto bad_reg;
	}

	if (!reg_is_valid_request(alpha2))
		return -EINVAL;

	size_of_regd = sizeof(struct ieee80211_regdomain) +
		(num_rules * sizeof(struct ieee80211_reg_rule));

	rd = kzalloc(size_of_regd, GFP_KERNEL);
	if (!rd)
		return -ENOMEM;

	rd->n_reg_rules = num_rules;
	rd->alpha2[0] = alpha2[0];
	rd->alpha2[1] = alpha2[1];

	nla_for_each_nested(nl_reg_rule, info->attrs[NL80211_ATTR_REG_RULES],
			rem_reg_rules) {
		nla_parse(tb, NL80211_REG_RULE_ATTR_MAX,
			nla_data(nl_reg_rule), nla_len(nl_reg_rule),
			reg_rule_policy);
		r = parse_reg_rule(tb, &rd->reg_rules[rule_idx]);
		if (r)
			goto bad_reg;

		rule_idx++;

		if (rule_idx > NL80211_MAX_SUPP_REG_RULES)
			goto bad_reg;
	}

	BUG_ON(rule_idx != num_rules);

	mutex_lock(&cfg80211_drv_mutex);
	r = set_regdom(rd);
	mutex_unlock(&cfg80211_drv_mutex);
	return r;

 bad_reg:
	kfree(rd);
	return -EINVAL;
}

static struct genl_ops nl80211_ops[] = {
	{
		.cmd = NL80211_CMD_GET_WIPHY,
		.doit = nl80211_get_wiphy,
		.dumpit = nl80211_dump_wiphy,
		.policy = nl80211_policy,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = NL80211_CMD_SET_WIPHY,
		.doit = nl80211_set_wiphy,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_GET_INTERFACE,
		.doit = nl80211_get_interface,
		.dumpit = nl80211_dump_interface,
		.policy = nl80211_policy,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = NL80211_CMD_SET_INTERFACE,
		.doit = nl80211_set_interface,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_NEW_INTERFACE,
		.doit = nl80211_new_interface,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_DEL_INTERFACE,
		.doit = nl80211_del_interface,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_GET_KEY,
		.doit = nl80211_get_key,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_SET_KEY,
		.doit = nl80211_set_key,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_NEW_KEY,
		.doit = nl80211_new_key,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_DEL_KEY,
		.doit = nl80211_del_key,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_SET_BEACON,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
		.doit = nl80211_addset_beacon,
	},
	{
		.cmd = NL80211_CMD_NEW_BEACON,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
		.doit = nl80211_addset_beacon,
	},
	{
		.cmd = NL80211_CMD_DEL_BEACON,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
		.doit = nl80211_del_beacon,
	},
	{
		.cmd = NL80211_CMD_GET_STATION,
		.doit = nl80211_get_station,
		.dumpit = nl80211_dump_station,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_SET_STATION,
		.doit = nl80211_set_station,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_NEW_STATION,
		.doit = nl80211_new_station,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_DEL_STATION,
		.doit = nl80211_del_station,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_GET_MPATH,
		.doit = nl80211_get_mpath,
		.dumpit = nl80211_dump_mpath,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_SET_MPATH,
		.doit = nl80211_set_mpath,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_NEW_MPATH,
		.doit = nl80211_new_mpath,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_DEL_MPATH,
		.doit = nl80211_del_mpath,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_SET_BSS,
		.doit = nl80211_set_bss,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_SET_REG,
		.doit = nl80211_set_reg,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_REQ_SET_REG,
		.doit = nl80211_req_set_reg,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_GET_MESH_PARAMS,
		.doit = nl80211_get_mesh_params,
		.policy = nl80211_policy,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = NL80211_CMD_SET_MESH_PARAMS,
		.doit = nl80211_set_mesh_params,
		.policy = nl80211_policy,
		.flags = GENL_ADMIN_PERM,
	},
};

/* multicast groups */
static struct genl_multicast_group nl80211_config_mcgrp = {
	.name = "config",
};

/* notification functions */

void nl80211_notify_dev_rename(struct cfg80211_registered_device *rdev)
{
	struct sk_buff *msg;

	msg = nlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg)
		return;

	if (nl80211_send_wiphy(msg, 0, 0, 0, rdev) < 0) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast(msg, 0, nl80211_config_mcgrp.id, GFP_KERNEL);
}

/* initialisation/exit functions */

int nl80211_init(void)
{
	int err, i;

	err = genl_register_family(&nl80211_fam);
	if (err)
		return err;

	for (i = 0; i < ARRAY_SIZE(nl80211_ops); i++) {
		err = genl_register_ops(&nl80211_fam, &nl80211_ops[i]);
		if (err)
			goto err_out;
	}

	err = genl_register_mc_group(&nl80211_fam, &nl80211_config_mcgrp);
	if (err)
		goto err_out;

	return 0;
 err_out:
	genl_unregister_family(&nl80211_fam);
	return err;
}

void nl80211_exit(void)
{
	genl_unregister_family(&nl80211_fam);
}
